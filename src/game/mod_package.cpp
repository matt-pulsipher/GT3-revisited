#include "game/mod_package.h"

#include "base/file.h"
#include "formats/model/gltf.h"
#include "formats/png_reader.h"
#include "formats/png_writer.h"
#include "game/data/car_database.h"
#include "game/data/mod_cars.h"
#include "net/protocol.h"
#include "vfs/vfs.h"

#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace gt3r::game {

namespace {

constexpr char kMagic[8] = {'G', 'T', '3', 'P', 'K', 'G', '1', 0};
constexpr size_t kMaxIni = 64u << 10;
constexpr size_t kMaxGlb = 16u << 20;
constexpr size_t kMaxPng = 4u << 20;
constexpr u32 kMaxImageDim = 2048;
constexpr size_t kMaxVertices = 500000;

std::string ext_of(const std::string& p) {
    size_t d = p.rfind('.');
    std::string e = d == std::string::npos ? "" : p.substr(d + 1);
    for (char& c : e) c = char(std::tolower(u8(c)));
    return e;
}

bool fail(std::string* error, const std::string& m) {
    if (error) *error = m;
    return false;
}

// Host path -> path relative to `root` ('/' separated), empty when outside it.
std::string relative_to(const std::string& root, const std::string& path) {
    std::string r = root, p = path;
    std::replace(r.begin(), r.end(), '\\', '/');
    std::replace(p.begin(), p.end(), '\\', '/');
    if (!r.empty() && r.back() != '/') r += '/';
    if (p.compare(0, r.size(), r) != 0) return {};
    return p.substr(r.size());
}

// A value a downloaded definition may set (unset = the donor's, always fine).
template <class T>
bool in_range(const std::optional<T>& v, double lo, double hi) {
    return !v || (std::isfinite(double(*v)) && double(*v) >= lo && double(*v) <= hi);
}

} // namespace

bool safe_package_path(const std::string& path) {
    if (path.empty() || path.size() > 96) return false;
    int levels = 0;
    size_t start = 0;
    for (;;) {
        size_t slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part.empty() || part[0] == '.' || part.size() > 48) return false; // no "", ".", "..", hidden names
        for (char c : part)
            if (!std::isalnum(u8(c)) && c != '.' && c != '_' && c != '-') return false;
        if (++levels > 3) return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return true;
}

std::optional<Bytes> build_mod_package(const data::CarDatabase& db, const Vfs& vfs, const std::string& label,
                                       std::string* error) {
    const data::ModCar* m = db.mod_car(label);
    if (!m) {
        fail(error, label + " is not a mod car");
        return std::nullopt;
    }
    if (m->exported) { // (its files are a disc's: never sent to another player)
        fail(error, label + " was exported from a disc: players who want it export it from their own");
        return std::nullopt;
    }
    std::string root;
    for (const ModInfo& mi : vfs.mods())
        if (mi.id == m->mod) root = mi.root;
    if (root.empty()) {
        fail(error, "the mod of " + label + " is not installed");
        return std::nullopt;
    }
    ModPackage p;
    p.label = label;
    auto add = [&](const std::string& host_path) -> bool {
        if (host_path.empty()) return true;
        std::string rel = relative_to(root, host_path);
        if (rel.empty() || !safe_package_path(rel)) return fail(error, host_path + " is outside the mod folder or has an unusual name");
        for (const ModPackage::File& f : p.files)
            if (f.path == rel) return true;
        std::optional<Bytes> data = read_host_file(host_path);
        if (!data) return fail(error, "cannot read " + host_path);
        p.files.push_back({rel, std::move(*data)});
        return true;
    };
    if (!add(m->file) || !add(m->picture) || !add(m->thumb) || !add(m->logo)) return std::nullopt;
    for (const std::string& body : m->model_files)
        if (!add(body)) return std::nullopt;
    return serialize_mod_package(p);
}

Bytes serialize_mod_package(const ModPackage& in) {
    // Canonical order (the definition first, then by path) so the same car always hashes the same.
    ModPackage p = in;
    std::stable_sort(p.files.begin(), p.files.end(), [](const ModPackage::File& a, const ModPackage::File& b) {
        bool ai = a.path.rfind("cars/", 0) == 0 && ext_of(a.path) == "ini", bi = b.path.rfind("cars/", 0) == 0 && ext_of(b.path) == "ini";
        if (ai != bi) return ai;
        return a.path < b.path;
    });
    net::Writer w;
    w.bytes(ByteSpan(reinterpret_cast<const u8*>(kMagic), 8));
    w.str(p.label);
    w.u8_(u8(p.files.size()));
    for (const ModPackage::File& f : p.files) {
        w.str(f.path);
        w.u32_(u32(f.data.size()));
        w.bytes(f.data);
    }
    return w.data;
}

std::optional<ModPackage> parse_mod_package(ByteSpan blob, std::string* error) {
    auto bad = [&](const std::string& m) -> std::optional<ModPackage> {
        fail(error, "package: " + m);
        return std::nullopt;
    };
    if (blob.size() > kMaxPackageBytes) return bad("too large");
    if (blob.size() < 10 || std::memcmp(blob.data(), kMagic, 8) != 0) return bad("not a car package");
    net::Reader r(blob.subspan(8));
    ModPackage p;
    p.label = r.str();
    size_t n = r.u8_();
    if (!r.ok() || n == 0 || n > kMaxPackageFiles) return bad("bad file count");
    std::set<std::string> seen;
    int inis = 0;
    for (size_t i = 0; i < n; ++i) {
        ModPackage::File f;
        f.path = r.str();
        u32 size = r.u32_();
        if (!r.ok() || size > r.remaining()) return bad("truncated");
        if (!safe_package_path(f.path)) return bad("unsafe path '" + f.path + "'");
        std::string lower = f.path;
        for (char& c : lower) c = char(std::tolower(u8(c)));
        if (!seen.insert(lower).second) return bad("duplicate path '" + f.path + "'");
        std::string e = ext_of(f.path);
        if (e == "ini") {
            if (f.path != "cars/" + p.label + ".ini") return bad("the definition must be cars/" + p.label + ".ini");
            if (size > kMaxIni) return bad("definition too large");
            ++inis;
        } else if (e == "glb") {
            if (size > kMaxGlb) return bad(f.path + " is larger than 16 MB");
        } else if (e == "png") {
            if (size > kMaxPng) return bad(f.path + " is larger than 4 MB");
        } else {
            return bad("file type not allowed: " + f.path);
        }
        ByteSpan d = r.bytes(size);
        f.data.assign(d.begin(), d.end());
        p.files.push_back(std::move(f));
    }
    if (!r.at_end()) return bad("trailing data");
    if (inis != 1) return bad("exactly one car definition expected");
    return p;
}

bool validate_mod_package(const ModPackage& p, const data::CarDatabase& db, const std::string& expected_label,
                          std::string* error) {
    if (p.label != expected_label) return fail(error, "package is for " + p.label + ", not " + expected_label);
    const ModPackage::File* ini = nullptr;
    for (const ModPackage::File& f : p.files)
        if (ext_of(f.path) == "ini") ini = &f;
    if (!ini) return fail(error, "no car definition");
    std::string err;
    std::optional<data::ModCarDef> def =
        data::parse_mod_car(std::string_view(reinterpret_cast<const char*>(ini->data.data()), ini->data.size()), &err);
    if (!def) return fail(error, "definition: " + err);
    if (def->label != p.label) return fail(error, "definition names " + def->label + ", not " + p.label);
    if (!db.car(def->donor)) return fail(error, "its donor car " + def->donor + " is not on this disc");
    // Plausible values only (a definition can be hand-written; these bound what physics sees).
    struct Range {
        bool ok;
        const char* what;
    };
    const Range ranges[] = {
        {in_range(def->weight_kg, 300, 5000), "weight"},
        {in_range(def->length_mm, 2000, 7000) && in_range(def->width_mm, 1200, 2700) && in_range(def->height_mm, 800, 2600),
         "dimensions"},
        {in_range(def->wheelbase_mm, 1500, 4500) && in_range(def->front_track_mm, 1000, 2400) &&
             in_range(def->rear_track_mm, 1000, 2400),
         "wheelbase / track"},
        {in_range(def->front_weight_pct, 20, 80), "weight distribution"},
        {in_range(def->cd, 0.1, 1.5) && in_range(def->frontal_area_m2, 0.8, 6.0), "aerodynamics"},
        {in_range(def->ride_height_mm, 30, 400), "ride height"},
        {in_range(def->power_kw, 1, 1500) && in_range(def->torque_nm, 1, 3000) && in_range(def->rated_kw, 1, 1500),
         "power / torque"},
        {in_range(def->max_rpm, 1000, 25000) && in_range(def->idle_rpm, 0, 3000) && in_range(def->base_rpm, 0, 25000),
         "engine speed"},
        {in_range(def->regen_kw, 0, 1000) && in_range(def->regen_torque_nm, 0, 3000) && in_range(def->regen_g, 0, 1.5),
         "regeneration"},
        {in_range(def->motor_inertia, 0, 10), "motor inertia"},
        {in_range(def->output_pct, 50, 200), "output factor"},
        {in_range(def->front_torque_pct, 0, 100), "torque split"},
        {in_range(def->final_drive, 1, 15) && in_range(def->reverse, 0.5, 8), "final drive"},
        {in_range(def->grip_pct, 50, 150), "tyre grip"},
        {in_range(def->whine_order, 0, 200) && in_range(def->whine_level, 0, 4) && in_range(def->inverter_level, 0, 4),
         "motor sound"},
        {def->pokes.size() <= 64, "raw values"},
        {std::abs(def->model_lower_mm) <= 500, "body offset"},
    };
    for (const Range& r : ranges)
        if (!r.ok) return fail(error, std::string("implausible ") + r.what);
    for (float g : def->gears) // electric single-speed reductions run up to ~15:1
        if (!(g >= 0.2f && g <= 20.0f)) return fail(error, "implausible gear ratio");
    for (const auto& [rpm, nm] : def->curve)
        if (!(rpm >= 0 && rpm <= 25000 && nm >= 0 && nm <= 3000)) return fail(error, "implausible torque curve");
    // The files it references are exactly the package's other files.
    // (Bodies: the ones its scenes draw, at most one per scene, as build_mod_package packs them;
    // a key every scene overrides names nothing.)
    std::set<std::string> refs;
    for (const std::string& b : data::model_files_of(*def))
        if (!safe_package_path(b)) return fail(error, "unsafe reference '" + b + "'");
    const std::array<std::string, data::kCarScenes> bodies = data::resolve_model_files(*def);
    std::vector<const std::string*> named = {&def->picture, &def->thumb, &def->logo};
    for (const std::string& b : bodies) named.push_back(&b);
    for (const std::string* s : named)
        if (!s->empty()) {
            if (!safe_package_path(*s)) return fail(error, "unsafe reference '" + *s + "'");
            refs.insert(*s);
        }
    std::set<std::string> others;
    for (const ModPackage::File& f : p.files)
        if (&f != ini) others.insert(f.path);
    if (refs != others) return fail(error, "the package's files do not match what the definition references");
    for (const std::string& b : bodies)
        if (!b.empty() && ext_of(b) != "glb") return fail(error, "the body must be a .glb file");
    for (const std::string* s : {&def->picture, &def->thumb, &def->logo})
        if (!s->empty() && ext_of(*s) != "png") return fail(error, "pictures must be .png files");
    // Contents.
    for (const ModPackage::File& f : p.files) {
        std::string e = ext_of(f.path);
        try {
            if (e == "png") {
                std::optional<std::pair<u32, u32>> sz = png_size(f.data);
                if (!sz || sz->first > kMaxImageDim || sz->second > kMaxImageDim)
                    return fail(error, f.path + ": not a PNG of at most 2048 x 2048");
                decode_png(f.data, kMaxImageDim);
            } else if (e == "glb") {
                GlbOptions o;
                o.max_texture_dim = kMaxImageDim;
                o.reflections = false;
                scene::Model m = load_glb(f.data, o, f.path);
                size_t verts = 0;
                for (const scene::Mesh& mesh : m.meshes) verts += mesh.vertices.size();
                if (verts > kMaxVertices) return fail(error, f.path + ": more than 500,000 vertices");
            }
        } catch (const std::exception& ex) {
            return fail(error, f.path + ": " + ex.what());
        }
    }
    return true;
}

std::optional<std::string> install_mod_package(const ModPackage& p, const std::string& hash, const std::string& mods_dir,
                                               const std::string& car_name, const std::string& from, std::string* error) {
    if (hash.size() < 12) {
        fail(error, "bad package hash");
        return std::nullopt;
    }
    std::string dir = mods_dir;
    if (!dir.empty() && dir.back() != '/') dir += '/';
    dir += "dl-" + hash.substr(0, 12);
    auto clean = [](std::string s) {
        for (char& c : s)
            if (c == '\n' || c == '\r' || c == '[' || c == ']' || c == '=') c = ' ';
        return s;
    };
    std::string ini = "[mod]\nname = " + clean(car_name) + " (downloaded)\nauthor = " + clean(from) +
                      "\ndescription = Downloaded from an online battle. Package " + hash +
                      "\npriority = -100\n";
    if (!SDL_CreateDirectory(dir.c_str())) {
        fail(error, "cannot create " + dir);
        return std::nullopt;
    }
    for (const ModPackage::File& f : p.files) {
        if (!safe_package_path(f.path)) { // parse_mod_package checked this; never write outside the folder
            fail(error, "unsafe path");
            return std::nullopt;
        }
        std::string path = dir + "/" + f.path;
        std::string parent = path.substr(0, path.find_last_of('/'));
        SDL_CreateDirectory(parent.c_str());
        // Pictures and bodies as our own encoding, never the sender's bytes: whatever else opens the files (a file
        // manager's thumbnails, the system's media scan) reads only what our encoders wrote.
        Bytes ours;
        try {
            if (ext_of(f.path) == "png") {
                const PngImage img = decode_png(f.data, kMaxImageDim);
                ours = encode_png(img.width, img.height, img.rgba);
            } else if (ext_of(f.path) == "glb") {
                ours = reencode_glb_images(f.data, kMaxImageDim);
            }
        } catch (const std::exception& ex) {
            fail(error, f.path + ": " + ex.what());
            return std::nullopt;
        }
        if (!write_host_file(path, ours.empty() ? ByteSpan(f.data) : ByteSpan(ours))) {
            fail(error, "cannot write " + path);
            return std::nullopt;
        }
    }
    if (!write_host_file(dir + "/mod.ini", ByteSpan(reinterpret_cast<const u8*>(ini.data()), ini.size()))) {
        fail(error, "cannot write mod.ini");
        return std::nullopt;
    }
    return dir;
}

} // namespace gt3r::game
