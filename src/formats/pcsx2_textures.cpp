#include "formats/pcsx2_textures.h"

#include "base/file.h"
#include "formats/png_reader.h"
#include "formats/png_writer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace gt3r::texture_pack {

namespace {

std::vector<u32> cut(const scene::Texture& t, const Tex1::Region& r) {
    std::vector<u32> out(size_t(r.width) * r.height);
    for (u32 y = 0; y < r.height; ++y)
        std::memcpy(&out[size_t(y) * r.width], &t.rgba[size_t(r.y + y) * t.width + r.x], size_t(r.width) * 4);
    return out;
}

// Files under `dir` (any depth), by name.
void files_in(const std::string& dir, int depth, std::vector<std::pair<std::string, std::string>>& out) {
    if (depth > 16) return;
    std::vector<std::string> names;
    SDL_EnumerateDirectory(
        dir.c_str(),
        [](void* user, const char*, const char* name) {
            static_cast<std::vector<std::string>*>(user)->push_back(name);
            return SDL_ENUM_CONTINUE;
        },
        &names);
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
        const std::string path = dir + "/" + name;
        if (host_is_directory(path)) files_in(path, depth + 1, out);
        else out.emplace_back(name, path);
    }
}

bool ends_with_png(const std::string& name) {
    return name.size() > 4 && name[name.size() - 4] == '.' && (name[name.size() - 3] | 0x20) == 'p' &&
           (name[name.size() - 2] | 0x20) == 'n' && (name[name.size() - 1] | 0x20) == 'g';
}

} // namespace

void Pcsx2Index::add_file(const std::string& path, ByteSpan data) {
    const u8* parsed_at = nullptr;
    Tex1 set;
    for_each_texture(path, data, [&](const FileTexture& ft) {
        if (ft.set.data() != parsed_at) set = Tex1::parse(ft.set), parsed_at = ft.set.data();
        const scene::Texture& t = *ft.texture;
        Match m;
        m.key = ft.key, m.path = path, m.name = ft.name, m.width = t.width, m.height = t.height;
        m.part = {0, 0, t.width, t.height};
        by_part_.emplace(ft.key, m); // PCSX2 dumps a texture without a region whole
        const Tex1::Region r = set.region(ft.index);
        if (r.x + r.width > t.width || r.y + r.height > t.height || (r.width == t.width && r.height == t.height)) return;
        m.part = r;
        const std::vector<u32> px = cut(t, r);
        by_part_.emplace(key_of(r.width, r.height, px.data()), m);
    });
}

std::string pcsx2_serial(const std::string& serial) {
    std::string out;
    for (char c : serial)
        if (c != '.') out += c == '_' ? '-' : c;
    return out;
}

std::string dump_folder(const std::string& serial) {
    return "texture-dump/" + (serial.empty() ? std::string("disc") : pcsx2_serial(serial));
}

u32 widen_pcsx2_alpha(u32 c, int alpha) {
    const u32 a = c >> 24;
    const u32 w = alpha == 2 ? 255u : alpha == 1 ? std::min<u32>(255, (a * 255 + 64) / 128) : std::min<u32>(255, a * 2);
    return (c & 0x00FFFFFFu) | w << 24;
}

std::optional<Pcsx2Index::Match> Pcsx2Index::find(u32 width, u32 height, const std::vector<u32>& rgba) const {
    std::vector<u32> px(rgba.size());
    for (int alpha = 0; alpha < 3; ++alpha) {
        for (size_t i = 0; i < rgba.size(); ++i) px[i] = widen_pcsx2_alpha(rgba[i], alpha);
        if (auto it = by_part_.find(key_of(width, height, px.data())); it != by_part_.end()) {
            Match m = it->second;
            m.alpha = alpha;
            return m;
        }
    }
    return std::nullopt;
}

Pcsx2Import import_pcsx2_pack(const std::string& pack, const Pcsx2Index& index, const std::string& textures_dir,
                              const std::function<std::optional<Bytes>(const std::string&)>& read,
                              const std::function<void(size_t, size_t)>& progress) {
    Pcsx2Import out;
    std::vector<std::pair<std::string, std::string>> dumps, pictures;
    files_in(pack + "/dumps", 0, dumps);
    files_in(pack + "/replacements", 0, pictures);
    std::map<std::string, std::string> dump_of(dumps.begin(), dumps.end());
    // Whole textures of the last game file used (pictures of part of a texture).
    std::string whole_path;
    std::map<std::string, scene::Texture> whole;
    size_t done = 0;
    for (const auto& [name, path] : pictures) {
        if (progress) progress(done, pictures.size());
        ++done;
        if (name.find("-mip") != std::string::npos) continue; // a mipmap level: the engine makes its own
        if (!ends_with_png(name)) {
            if (name[0] != '.') out.failed.push_back(name + " (not PNG)");
            continue;
        }
        ++out.pictures;
        auto dump = dump_of.find(name);
        if (dump == dump_of.end()) {
            out.no_dump.push_back(name);
            continue;
        }
        std::optional<Pcsx2Index::Match> m;
        PngImage pic;
        try {
            std::optional<Bytes> d = read_host_file(dump->second);
            if (!d) throw FormatError("cannot read its dump");
            PngImage orig = decode_png(*d);
            m = index.find(orig.width, orig.height, orig.rgba);
            if (!m) {
                out.unmatched.push_back(name);
                continue;
            }
            std::optional<Bytes> r = read_host_file(path);
            if (!r) throw FormatError("cannot read it");
            pic = decode_png(*r, Pack::kMaxSide);
        } catch (const std::exception& e) {
            out.failed.push_back(name + " (" + e.what() + ")");
            continue;
        }
        for (u32& c : pic.rgba) c = widen_pcsx2_alpha(c, m->alpha);
        // A picture of part of the texture: into a picture of the whole, at the same scale.
        if (m->part.width != m->width || m->part.height != m->height) {
            if (whole_path != m->path) {
                whole.clear();
                whole_path = m->path;
                if (std::optional<Bytes> d = read(m->path))
                    for_each_texture(m->path, *d, [&](const FileTexture& ft) { whole[ft.name] = *ft.texture; });
            }
            auto w = whole.find(m->name);
            if (w == whole.end()) {
                out.failed.push_back(name + " (cannot read " + m->path + ")");
                continue;
            }
            const scene::Texture& t = w->second;
            const double sx = double(pic.width) / m->part.width, sy = double(pic.height) / m->part.height;
            const u32 cw = u32(std::max(1.0, std::round(t.width * sx))), ch = u32(std::max(1.0, std::round(t.height * sy)));
            if (cw > Pack::kMaxSide || ch > Pack::kMaxSide) {
                out.failed.push_back(name + " (too large for the whole texture)");
                continue;
            }
            std::vector<u32> canvas(size_t(cw) * ch);
            for (u32 y = 0; y < ch; ++y)
                for (u32 x = 0; x < cw; ++x)
                    canvas[size_t(y) * cw + x] = t.rgba[std::min<size_t>(t.height - 1, size_t(y / sy)) * t.width +
                                                        std::min<size_t>(t.width - 1, size_t(x / sx))];
            const u32 ox = u32(std::round(m->part.x * sx)), oy = u32(std::round(m->part.y * sy));
            for (u32 y = 0; y < pic.height && oy + y < ch; ++y)
                for (u32 x = 0; x < pic.width && ox + x < cw; ++x)
                    canvas[size_t(oy + y) * cw + ox + x] = pic.rgba[size_t(y) * pic.width + x];
            pic.width = cw, pic.height = ch, pic.rgba = std::move(canvas);
        }
        const std::string stem = name.substr(0, name.size() - 4);
        const std::string file = textures_dir + "/" + stem + "-" + key_name(m->key) + ".png";
        if (!host_make_dir(textures_dir) || !write_host_file(file, encode_png(pic.width, pic.height, pic.rgba))) {
            out.failed.push_back(name + " (could not be written)");
            continue;
        }
        ++out.converted;
    }
    if (progress) progress(pictures.size(), pictures.size());
    return out;
}

} // namespace gt3r::texture_pack
