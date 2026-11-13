#include "formats/texture_pack.h"

#include "base/file.h"
#include "base/log.h"
#include "formats/clut_animation.h"
#include "formats/menu/pmb.h"
#include "formats/model/gtm1.h"
#include "formats/png_reader.h"
#include "formats/png_writer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace gt3r::texture_pack {

u64 key_of(u32 width, u32 height, const u32* rgba) {
    u64 h = 14695981039346656037ull;
    auto add = [&h](u32 w) { h = (h ^ w) * 1099511628211ull; };
    add(width);
    add(height);
    for (size_t i = 0, n = size_t(width) * height; i < n; ++i) add(rgba[i]);
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    return h ^ (h >> 31);
}

std::string key_name(u64 key) {
    char s[17];
    std::snprintf(s, sizeof s, "%016llx", static_cast<unsigned long long>(key));
    return s;
}

std::optional<u64> key_in_file_name(std::string_view name) {
    if (name.size() < 4 + 16) return std::nullopt;
    std::string_view ext = name.substr(name.size() - 4);
    if (!(ext[0] == '.' && (ext[1] | 0x20) == 'p' && (ext[2] | 0x20) == 'n' && (ext[3] | 0x20) == 'g')) return std::nullopt;
    std::string_view stem = name.substr(0, name.size() - 4);
    std::string_view hex = stem.substr(stem.size() - 16);
    if (stem.size() > 16) {
        const char sep = stem[stem.size() - 17];
        if (sep != '-' && sep != '_' && sep != '.' && sep != ' ') return std::nullopt;
    }
    u64 key = 0;
    for (char c : hex) {
        int d = c >= '0' && c <= '9' ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10 : -1;
        if (d < 0) return std::nullopt;
        key = key << 4 | u64(d);
    }
    return key;
}

// --- Pack ----------------------------------------------------------------------

bool Pack::add_file(u64 key, std::string path, std::string source) {
    return files_.emplace(key, File{std::move(path), std::move(source)}).second;
}

std::string Pack::source_of(u64 key) const {
    auto it = files_.find(key);
    return it == files_.end() ? std::string() : it->second.source;
}

namespace {

void collect_pngs(const std::string& dir, int depth, std::vector<std::pair<u64, std::string>>& out) {
    if (depth > 16) return;
    struct Ctx {
        std::vector<std::string> names;
    } ctx;
    SDL_EnumerateDirectory(
        dir.c_str(),
        [](void* user, const char*, const char* name) {
            static_cast<Ctx*>(user)->names.push_back(name);
            return SDL_ENUM_CONTINUE;
        },
        &ctx);
    std::sort(ctx.names.begin(), ctx.names.end());
    for (const std::string& name : ctx.names) {
        const std::string path = dir + "/" + name;
        if (host_is_directory(path)) collect_pngs(path, depth + 1, out);
        else if (std::optional<u64> key = key_in_file_name(name)) out.emplace_back(*key, path);
    }
}

} // namespace

std::vector<std::pair<u64, std::string>> pictures_in(const std::string& dir) {
    std::vector<std::pair<u64, std::string>> found;
    collect_pngs(dir, 0, found);
    return found;
}

size_t Pack::add_folder(const std::string& dir, const std::string& source) {
    size_t added = 0;
    for (auto& [key, path] : pictures_in(dir)) added += add_file(key, std::move(path), source) ? 1 : 0;
    return added;
}

std::shared_ptr<const Image> Pack::find(u64 key) const {
    auto file = files_.find(key);
    if (file == files_.end()) return nullptr;
    {
        std::lock_guard lock(mutex_);
        if (auto it = cache_.find(key); it != cache_.end()) {
            uses_.splice(uses_.begin(), uses_, it->second.use);
            return it->second.image;
        }
        if (broken_.count(key)) return nullptr;
    }
    std::shared_ptr<Image> img;
    std::string error;
    const std::string& path = file->second.path;
    if (std::optional<Bytes> data = read_host_file(path)) {
        try {
            PngImage png = decode_png(*data, kMaxSide);
            img = std::make_shared<Image>();
            img->width = png.width, img->height = png.height, img->rgba = std::move(png.rgba);
        } catch (const std::exception& e) {
            error = e.what();
        }
    } else {
        error = "cannot read the file";
    }
    std::lock_guard lock(mutex_);
    if (!img) {
        if (broken_.insert(key).second) LOG_WARN("Texture pack: %s: %s", path.c_str(), error.c_str());
        return nullptr;
    }
    if (auto it = cache_.find(key); it != cache_.end()) return it->second.image; // another thread was first
    uses_.push_front(key);
    cache_[key] = {img, uses_.begin()};
    cache_bytes_ += img->rgba.size() * 4;
    while (cache_bytes_ > kCacheBytes && uses_.size() > 1) {
        auto old = cache_.find(uses_.back());
        cache_bytes_ -= old->second.image->rgba.size() * 4;
        cache_.erase(old);
        uses_.pop_back();
    }
    return img;
}

// --- the current pack ------------------------------------------------------------

namespace {

std::mutex g_pack_mutex;
std::shared_ptr<const Pack> g_pack;

} // namespace

void set_pack(std::shared_ptr<const Pack> pack) {
    if (pack && pack->size() == 0) pack.reset();
    std::lock_guard lock(g_pack_mutex);
    g_pack = std::move(pack);
}

std::shared_ptr<const Pack> current_pack() {
    std::lock_guard lock(g_pack_mutex);
    return g_pack;
}

std::shared_ptr<const Image> replacement(u32 width, u32 height, const u32* rgba) {
    std::shared_ptr<const Pack> pack = current_pack();
    if (!pack || width == 0 || height == 0) return nullptr;
    return pack->find(key_of(width, height, rgba));
}

std::vector<u64> keys_of_set(ByteSpan tex1, int paint) {
    std::vector<scene::Texture> full;
    gtm1::decode_texture_set(tex1, full, "", paint, nullptr, false);
    std::vector<u64> keys;
    keys.reserve(full.size());
    for (const scene::Texture& t : full) keys.push_back(key_of(t.width, t.height, t.rgba.data()));
    return keys;
}

namespace {

// The keys of the last set looked up on this thread: callers ask for each texture of a set in turn.
struct SetKeys {
    size_t size = 0;
    u64 digest = 0;
    std::vector<u64> keys;
};

u64 digest_of(ByteSpan b) {
    u64 h = 14695981039346656037ull;
    for (u8 c : b) h = (h ^ c) * 1099511628211ull;
    return h;
}

} // namespace

std::optional<Image> replacement(ByteSpan tex1, size_t index, const Tex1::Region& region) {
    std::shared_ptr<const Pack> pack = current_pack();
    if (!pack) return std::nullopt;
    thread_local SetKeys last;
    if (!Tex1::is_tex1(tex1)) return std::nullopt;
    tex1 = tex1.subspan(0, std::min<size_t>(tex1.size(), rd32(tex1, 0x0C))); // the set, not what follows it
    const u64 digest = digest_of(tex1);
    if (last.size != tex1.size() || last.digest != digest || last.keys.empty()) {
        try {
            last.keys = keys_of_set(tex1);
        } catch (const std::exception&) {
            last.keys.clear();
        }
        last.size = tex1.size(), last.digest = digest;
    }
    if (index >= last.keys.size()) return std::nullopt;
    std::shared_ptr<const Image> r = pack->find(last.keys[index]);
    if (!r) return std::nullopt;
    // The texture's full size, to cut the same part of the picture.
    const Tex1 set = Tex1::parse(tex1);
    const u32 fw = set.textures.at(index).tex0.width(), fh = set.textures.at(index).tex0.height();
    if (region.x == 0 && region.y == 0 && region.width == fw && region.height == fh) return *r;
    const double sx = double(r->width) / fw, sy = double(r->height) / fh;
    auto edge = [](double v, u32 limit) { return u32(std::clamp<long long>(std::llround(v), 0, limit)); };
    const u32 x0 = edge(region.x * sx, r->width), x1 = edge((region.x + region.width) * sx, r->width);
    const u32 y0 = edge(region.y * sy, r->height), y1 = edge((region.y + region.height) * sy, r->height);
    if (x1 <= x0 || y1 <= y0) return std::nullopt;
    Image out;
    out.width = x1 - x0, out.height = y1 - y0;
    out.rgba.resize(size_t(out.width) * out.height);
    for (u32 y = 0; y < out.height; ++y)
        std::memcpy(&out.rgba[size_t(y) * out.width], &r->rgba[size_t(y0 + y) * r->width + x0], size_t(out.width) * 4);
    return out;
}

std::optional<Image> replacement(ByteSpan tex1, size_t index) {
    if (!current_pack()) return std::nullopt;
    const Tex1 set = Tex1::parse(tex1);
    if (index >= set.textures.size()) return std::nullopt;
    return replacement(tex1, index, set.region(index));
}

bool replace_decoded(ByteSpan tex1, size_t index, Tex1::Image& img) {
    std::optional<Image> r = replacement(tex1, index);
    if (!r) return false;
    img.width = r->width, img.height = r->height, img.rgba = std::move(r->rgba);
    return true;
}

// --- Dump ------------------------------------------------------------------------

bool dump_reads(const std::string& path) {
    for (const char* dir : {"bgm/", "music/", "sound/", "stream/"})
        if (path.rfind(dir, 0) == 0) return false;
    for (const char* ext : {".pss", ".ads", ".ins", ".seq", ".vag"})
        if (path.size() > std::strlen(ext) && path.compare(path.size() - std::strlen(ext), std::string::npos, ext) == 0)
            return false;
    return true;
}

void for_each_texture(const std::string& path, ByteSpan data, const std::function<void(const FileTexture&)>& each) {
    // The file's Tex1 sets, each with a name: a menu page's pictures, or the sets in the file.
    std::vector<std::pair<std::string, ByteSpan>> sets;
    std::vector<Bytes> pictures; // a page's pictures, unpacked
    bool page = false;
    if (menu::PmbFile::is_pmb(data)) {
        try {
            menu::PmbFile pmb = menu::PmbFile::parse(data);
            pictures.reserve(pmb.images.size());
            for (size_t i = 0; i < pmb.images.size(); ++i) {
                try {
                    pictures.push_back(pmb.image_data(data, i));
                    sets.emplace_back("img" + std::to_string(i), pictures.back());
                } catch (const std::exception&) {
                }
            }
            page = true;
        } catch (const std::exception&) {
        }
    }
    if (!page) {
        size_t n = 0;
        for (size_t off : find_tex1_sets(data)) sets.emplace_back("set" + std::to_string(n++), data.subspan(off));
    }
    for (const auto& [label, bytes] : sets) {
        if (!Tex1::is_tex1(bytes)) continue;
        int paints = 1;
        try {
            paints = std::max<int>(1, int(gtm1::paint_variants(bytes).size()));
        } catch (const std::exception&) {
        }
        std::vector<u64> plain; // the paint 0 keys
        for (int p = 0; p < paints; ++p) {
            std::vector<scene::Texture> full;
            ClutTextures clut; // the textures its CLUT animations recolour (none with a paint)
            try {
                gtm1::decode_texture_set(bytes, full, "", p, &clut, false);
            } catch (const std::exception& e) {
                LOG_WARN("Textures: %s %s: %s", path.c_str(), label.c_str(), e.what());
                break;
            }
            if (page && full.size() > 1) full.resize(1);
            const std::vector<u32> animated = clut.records(); // record order: sorted
            for (size_t i = 0; i < full.size(); ++i) {
                const scene::Texture& t = full[i];
                if (t.width == 0 || t.height == 0) continue;
                FileTexture ft;
                ft.key = key_of(t.width, t.height, t.rgba.data());
                if (p == 0) plain.resize(std::max(plain.size(), i + 1)), plain[i] = ft.key;
                else if (i < plain.size() && plain[i] == ft.key) continue; // the paint leaves it as it is
                ft.name = label + "-" + std::to_string(i) + (p ? "-paint" + std::to_string(p) : "");
                ft.set = bytes, ft.index = i, ft.paint = p, ft.texture = &t;
                ft.animated = std::binary_search(animated.begin(), animated.end(), u32(i));
                each(ft);
            }
        }
    }
}

size_t Dump::add_file(const std::string& path, ByteSpan data, bool* ok) {
    size_t saved = 0;
    const std::string dir = root_ + "/" + path;
    bool made_dir = false;
    for_each_texture(path, data, [&](const FileTexture& ft) {
        const std::string name = ft.name + (ft.animated ? "-anim" : "");
        auto [it, fresh] = uses_.try_emplace(ft.key);
        it->second.push_back(path + "\t" + name);
        if (!fresh) {
            // Saved still, animated here (the same pixels at the animation's reset): a replacement
            // would stop this animation, so the file is marked too.
            auto still = still_.find(ft.key);
            if (ft.animated && still != still_.end()) {
                const std::string& from = still->second;
                const std::string to = from.substr(0, from.size() - 21) + "-anim" + from.substr(from.size() - 21);
                if (!host_rename(from, to) && ok) *ok = false;
                still_.erase(still);
            }
            return;
        }
        order_.push_back(ft.key);
        if (!made_dir) made_dir = host_make_dir(dir);
        const scene::Texture& t = *ft.texture;
        const std::string png = dir + "/" + name + "-" + key_name(ft.key) + ".png";
        if (write_host_file(png, encode_png(t.width, t.height, t.rgba))) {
            ++saved;
            if (!ft.animated) still_.emplace(ft.key, png);
        } else if (ok)
            *ok = false;
    });
    saved_ += saved;
    return saved;
}

bool Dump::finish() const {
    std::string text = "# Texture keys and where the game uses them (docs/MODDING.md \"Texture packs\").\n"
                       "# key\tgame file\ttexture (picture or set - texture [- paint] [- anim: its colours animate,\n"
                       "# and a replacement shows as it is])\n";
    for (u64 key : order_)
        for (const std::string& use : uses_.at(key)) text += key_name(key) + "\t" + use + "\n";
    if (!host_make_dir(root_)) return false;
    return write_host_file(root_ + "/textures.txt", ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()));
}

} // namespace gt3r::texture_pack
