#include "game/livery_package.h"

#include "base/config.h"
#include "base/file.h"
#include "formats/model/car_model.h"
#include "formats/model/gtm1.h"
#include "formats/png_reader.h"
#include "formats/png_writer.h"
#include "formats/texture_pack.h"
#include "net/protocol.h"
#include "race/custom_paints.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

namespace gt3r::game {

namespace {

constexpr char kMagic[8] = {'G', 'T', '3', 'R', 'L', 'I', 'V', '1'};

bool fail(std::string* error, const std::string& m) {
    if (error) *error = m;
    return false;
}

bool safe_name(const std::string& n) {
    if (n.empty() || n.size() > 96 || n[0] == '.') return false;
    return std::all_of(n.begin(), n.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
    });
}

// A car's textures in a paint, by key: every Tex1 set of cars/day/<model> (the template's, and any other a livery may name).
std::map<u64, scene::Texture> textures_by_key(const Vfs& vfs, const std::string& model, int paint) {
    std::map<u64, scene::Texture> out;
    std::optional<Bytes> data = vfs.read("cars/day/" + model);
    if (!data || !CarFile::is_car(*data)) return out;
    const CarFile f = CarFile::parse(*data);
    const gtm1::Gtm1 g = gtm1::Gtm1::parse(f.body);
    for (u32 off : g.texsets) {
        ByteSpan set = f.body.subspan(off);
        if (!Tex1::is_tex1(set)) continue;
        std::vector<scene::Texture> ts;
        gtm1::decode_texture_set(set, ts, "", paint, nullptr, false);
        for (scene::Texture& t : ts) out.emplace(texture_pack::key_of(t.width, t.height, t.rgba.data()), std::move(t));
    }
    return out;
}

std::string clean(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r' || c == '[' || c == ']' || c == '=' || c == ';' || c == '#') c = ' ';
    return s;
}

std::string livery_ini(const race::CustomPaint& p, const std::string& from) {
    std::string s = "[livery]\nname = " + clean(p.name) + "\nauthor = " + clean(p.author) + "\nmodel = " + p.model +
                    "\npaint = " + std::to_string(p.finish) + "\n";
    if (p.colour) s += "color = " + race::rgb_text(p.rgb) + "\n";
    if (!from.empty()) s += "from = " + clean(from) + "\n";
    return s;
}

} // namespace

std::optional<Bytes> build_livery_package(const race::CustomPaint& p, const Vfs& vfs, std::string* error) {
    const size_t slash = p.file.rfind('/');
    if (!p.pictures || slash == std::string::npos) {
        fail(error, "not a livery");
        return std::nullopt;
    }
    const std::map<u64, scene::Texture> textures = textures_by_key(vfs, p.model, p.finish);
    if (textures.empty()) {
        fail(error, "cars/day/" + p.model + " cannot be read");
        return std::nullopt;
    }
    LiveryPackage pkg;
    const std::string ini = livery_ini(p, {});
    pkg.files.push_back({"livery.ini", Bytes(ini.begin(), ini.end())});
    size_t total = ini.size();
    for (const auto& [key, path] : texture_pack::pictures_in(p.file.substr(0, slash))) {
        auto t = textures.find(key);
        if (t == textures.end()) continue; // (a picture for another car's texture: never shown on this one)
        std::optional<Bytes> png = read_host_file(path);
        if (!png) continue;
        texture_pack::Image pic;
        try {
            PngImage img = decode_png(*png, texture_pack::Pack::kMaxSide);
            pic.width = img.width, pic.height = img.height, pic.rgba = std::move(img.rgba);
        } catch (const std::exception&) {
            continue; // (the game skips it too)
        }
        // Only what was painted: the coverage as alpha, the rest transparent.
        const std::vector<u8> cover = gtm1::livery_coverage(pic, {t->second.rgba.data(), t->second.width, t->second.height});
        bool any = false;
        for (size_t i = 0; i < pic.rgba.size(); ++i) {
            pic.rgba[i] = cover[i] ? (pic.rgba[i] & 0xFFFFFFu) | u32(cover[i]) << 24 : 0;
            any = any || cover[i];
        }
        if (!any) continue;
        Bytes out = encode_png(pic.width, pic.height, pic.rgba);
        total += out.size();
        if (pkg.files.size() >= kMaxLiveryFiles || total > kMaxLiveryBytes) {
            fail(error, "more than " + std::to_string(kMaxLiveryBytes >> 20) + " MB of pictures");
            return std::nullopt;
        }
        pkg.files.push_back({"p-" + texture_pack::key_name(key) + ".png", std::move(out)});
    }
    if (pkg.files.size() == 1) {
        fail(error, "nothing painted");
        return std::nullopt;
    }
    return serialize_livery_package(pkg);
}

Bytes serialize_livery_package(const LiveryPackage& p) {
    net::Writer w;
    w.bytes(ByteSpan(reinterpret_cast<const u8*>(kMagic), sizeof kMagic));
    w.u16_(u16(p.files.size()));
    for (const LiveryPackage::File& f : p.files) {
        w.str(f.name);
        w.u32_(u32(f.data.size()));
        w.bytes(f.data);
    }
    return w.data;
}

std::optional<LiveryPackage> parse_livery_package(ByteSpan blob, std::string* error) {
    if (blob.size() > kMaxLiveryBytes + 64 * 1024 || blob.size() < sizeof kMagic + 2 ||
        std::memcmp(blob.data(), kMagic, sizeof kMagic) != 0) {
        fail(error, "not a livery package");
        return std::nullopt;
    }
    net::Reader r(blob.subspan(sizeof kMagic));
    const u16 n = r.u16_();
    if (n == 0 || n > kMaxLiveryFiles) {
        fail(error, "bad file count");
        return std::nullopt;
    }
    LiveryPackage p;
    size_t total = 0;
    for (u16 i = 0; i < n && r.ok(); ++i) {
        LiveryPackage::File f;
        f.name = r.str();
        const u32 size = r.u32_();
        if (!r.ok() || size > r.remaining() || (total += size) > kMaxLiveryBytes) {
            fail(error, "package truncated or too large");
            return std::nullopt;
        }
        if (!safe_name(f.name)) {
            fail(error, "bad file name");
            return std::nullopt;
        }
        const ByteSpan d = r.bytes(size);
        f.data.assign(d.begin(), d.end());
        p.files.push_back(std::move(f));
    }
    if (!r.ok() || r.remaining() != 0) {
        fail(error, "package truncated");
        return std::nullopt;
    }
    return p;
}

bool validate_livery_package(const LiveryPackage& p, const Vfs& vfs, const std::string& model, std::string* error) {
    const LiveryPackage::File* ini = nullptr;
    std::vector<std::pair<u64, const LiveryPackage::File*>> pictures;
    for (const LiveryPackage::File& f : p.files) {
        if (f.name == "livery.ini") {
            if (ini) return fail(error, "two livery.ini files");
            ini = &f;
            continue;
        }
        const std::optional<u64> key = texture_pack::key_in_file_name(f.name);
        if (!key || f.name.size() < 4 || to_lower(f.name.substr(f.name.size() - 4)) != ".png")
            return fail(error, f.name + ": only livery.ini and pictures named by a key");
        pictures.emplace_back(*key, &f);
    }
    if (!ini) return fail(error, "no livery.ini");
    if (pictures.empty()) return fail(error, "no pictures");
    std::string err;
    std::optional<race::CustomPaint> cp =
        race::parse_custom_paint(std::string_view(reinterpret_cast<const char*>(ini->data.data()), ini->data.size()), &err);
    if (!cp) return fail(error, "livery.ini: " + err);
    if (!model.empty() && cp->model != model) return fail(error, "livery.ini names " + cp->model + ", not " + model);
    const int paint = cp->finish < 0 ? 0 : cp->finish;
    race::CarColors colors;
    colors.load(vfs);
    const std::vector<u32>* ids = colors.ids(cp->model);
    if (!ids || size_t(paint) >= ids->size()) return fail(error, cp->model + " has no paint " + std::to_string(paint) + " on this disc");
    const std::map<u64, scene::Texture> textures = textures_by_key(vfs, cp->model, paint);
    for (const auto& [key, f] : pictures) {
        if (!textures.count(key)) return fail(error, f->name + ": not one of " + cp->model + "'s textures on this disc");
        const std::optional<std::pair<u32, u32>> size = png_size(f->data);
        if (!size) return fail(error, f->name + ": not a PNG");
        if (size->first == 0 || size->second == 0 || size->first > texture_pack::Pack::kMaxSide ||
            size->second > texture_pack::Pack::kMaxSide)
            return fail(error, f->name + ": larger than " + std::to_string(texture_pack::Pack::kMaxSide) + " pixels a side");
        try {
            decode_png(f->data, texture_pack::Pack::kMaxSide);
        } catch (const std::exception& e) {
            return fail(error, f->name + ": " + e.what());
        }
    }
    return true;
}

std::string downloaded_livery_id(const std::string& hash) {
    return std::string(kDownloadedLiveriesMod) + "/dl-" + hash.substr(0, std::min<size_t>(12, hash.size()));
}

std::optional<std::string> install_livery_package(const LiveryPackage& p, const std::string& hash, const std::string& mods_dir,
                                                  const std::string& from, std::string* error) {
    if (hash.size() < 12) {
        fail(error, "bad package hash");
        return std::nullopt;
    }
    const std::string mod = mods_dir + "/" + kDownloadedLiveriesMod;
    const std::string dir = mod + "/liveries/dl-" + hash.substr(0, 12);
    if (!host_make_dir(dir)) {
        fail(error, "cannot create " + dir);
        return std::nullopt;
    }
    if (!host_path_exists(mod + "/mod.ini")) {
        const std::string mi = "[mod]\nname = Downloaded liveries\ndescription = Liveries other players' cars wore in online "
                               "battles\npriority = -100\n";
        write_host_file(mod + "/mod.ini", ByteSpan(reinterpret_cast<const u8*>(mi.data()), mi.size()));
    }
    for (const LiveryPackage::File& f : p.files) {
        if (!safe_name(f.name)) { // parse_livery_package checked this; never write outside the folder
            fail(error, "unsafe name");
            return std::nullopt;
        }
        ByteSpan data = f.data;
        std::string ini;
        Bytes clean_png;
        if (f.name != "livery.ini") {
            // Our own encoding of the picture, never the sender's bytes: whatever else opens the file (a file manager's
            // thumbnails, the system's media scan) reads only what our encoder writes.
            try {
                const PngImage img = decode_png(f.data, texture_pack::Pack::kMaxSide);
                clean_png = encode_png(img.width, img.height, img.rgba);
            } catch (const std::exception& e) {
                fail(error, f.name + ": " + e.what());
                return std::nullopt;
            }
            data = clean_png;
        } else { // noting who it came from
            ini.assign(f.data.begin(), f.data.end());
            if (!ini.empty() && ini.back() != '\n') ini += '\n';
            ini += "from = " + clean(from) + "\n";
            data = ByteSpan(reinterpret_cast<const u8*>(ini.data()), ini.size());
        }
        if (!write_host_file(dir + "/" + f.name, data)) {
            fail(error, "cannot write " + dir + "/" + f.name);
            return std::nullopt;
        }
    }
    return dir;
}

} // namespace gt3r::game
