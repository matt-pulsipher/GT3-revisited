#include "game/wheel_package.h"

#include "base/file.h"
#include "net/protocol.h"
#include "vfs/mod_wheels.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>

namespace gt3r::game {

namespace {

constexpr char kMagic[8] = {'G', 'T', '3', 'P', 'K', 'W', '1', 0};
constexpr size_t kMaxIni = 16u << 10;
constexpr size_t kMaxGlb = 16u << 20;
constexpr size_t kMaxPng = 4u << 20;

std::string ext_of(const std::string& p) {
    const size_t d = p.rfind('.');
    std::string e = d == std::string::npos ? "" : p.substr(d + 1);
    for (char& c : e) c = char(std::tolower(u8(c)));
    return e;
}

bool fail(std::string* error, const std::string& m) {
    if (error) *error = m;
    return false;
}

// Host path -> path relative to `root` ('/' separated), empty when outside it.
std::string relative_to(std::string root, std::string path) {
    std::replace(root.begin(), root.end(), '\\', '/');
    std::replace(path.begin(), path.end(), '\\', '/');
    if (!root.empty() && root.back() != '/') root += '/';
    return path.compare(0, root.size(), root) == 0 ? path.substr(root.size()) : std::string();
}

// A value as an .ini line keeps it (no line breaks, brackets or comment marks).
std::string clean(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r' || c == '[' || c == ']' || c == ';' || c == '#' || c == '=') c = ' ';
    return s;
}

} // namespace

Bytes serialize_wheel_package(const ModPackage& in) {
    // Canonical order (the definition first, then by path) so the same wheel always hashes the same.
    ModPackage p = in;
    std::stable_sort(p.files.begin(), p.files.end(), [](const ModPackage::File& a, const ModPackage::File& b) {
        const bool ai = ext_of(a.path) == "ini", bi = ext_of(b.path) == "ini";
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

std::optional<Bytes> build_wheel_package(const Vfs& vfs, const std::string& label, std::string* error) {
    const ModWheelDef* d = nullptr;
    for (const ModWheelDef& w : vfs.mod_wheels())
        if (w.label == label && w.error.empty()) d = &w;
    if (!d) {
        fail(error, label + " is not a mod wheel");
        return std::nullopt;
    }
    std::string root;
    for (const ModInfo& m : vfs.mods())
        if (m.id == d->mod_id) root = m.root;
    ModPackage p;
    p.label = label;
    // Each file once, at its path in the mod; "" when there is none.
    auto add = [&](const std::string& host_path, std::string& rel) -> bool {
        rel.clear();
        if (host_path.empty()) return true;
        if (!disc_rim(host_path).empty()) { // (the disc's own rim: every player's disc has it)
            rel = host_path;
            return true;
        }
        rel = relative_to(root, host_path);
        if (rel.empty() || !safe_package_path(rel))
            return fail(error, host_path + " is outside the mod folder or has an unusual name");
        for (const ModPackage::File& f : p.files)
            if (f.path == rel) return true;
        std::optional<Bytes> data = read_host_file(host_path);
        if (!data) return fail(error, "cannot read " + host_path);
        p.files.push_back({rel, std::move(*data)});
        return true;
    };
    std::string model, menu, race, picture, logo;
    if (!add(d->model, model) || !add(d->menu, menu) || !add(d->race, race) || !add(d->picture, picture) ||
        !add(d->brand_logo, logo))
        return std::nullopt;
    std::string ini = "[wheel]\nlabel = " + label + "\nbrand = " + clean(d->brand) + "\n";
    if (!model.empty()) ini += "model = " + model + "\n";
    if (!menu.empty()) ini += "menu = " + menu + "\n";
    if (!race.empty()) ini += "race = " + race + "\n";
    if (!picture.empty()) ini += "picture = " + picture + "\n";
    ini += std::string("for_sale = ") + (d->for_sale ? "yes" : "no") + "\n";
    if (!logo.empty()) ini += "[brand]\nlogo = " + logo + "\n";
    if (d->shape.any()) {
        char shape[160];
        std::snprintf(shape, sizeof shape, "[shape]\ndepth = %.9g\ndish = %.9g\nlip = %.9g\n", double(d->shape.depth),
                      double(d->shape.dish), double(d->shape.lip));
        ini += shape;
    }
    p.files.push_back({"wheels/" + label + ".ini", Bytes(ini.begin(), ini.end())});
    return serialize_wheel_package(p);
}

std::optional<ModPackage> parse_wheel_package(ByteSpan blob, std::string* error) {
    auto bad = [&](const std::string& m) -> std::optional<ModPackage> {
        fail(error, "package: " + m);
        return std::nullopt;
    };
    if (blob.size() > kMaxPackageBytes) return bad("too large");
    if (blob.size() < 10 || std::memcmp(blob.data(), kMagic, 8) != 0) return bad("not a wheel package");
    net::Reader r(blob.subspan(8));
    ModPackage p;
    p.label = r.str();
    const size_t n = r.u8_();
    if (!r.ok() || !valid_wheel_label(p.label)) return bad("bad label");
    if (n == 0 || n > kMaxWheelPackageFiles) return bad("bad file count");
    std::set<std::string> seen;
    int inis = 0;
    for (size_t i = 0; i < n; ++i) {
        ModPackage::File f;
        f.path = r.str();
        const u32 size = r.u32_();
        if (!r.ok() || size > r.remaining()) return bad("truncated");
        if (!safe_package_path(f.path)) return bad("unsafe path '" + f.path + "'");
        std::string lower = f.path;
        for (char& c : lower) c = char(std::tolower(u8(c)));
        if (!seen.insert(lower).second) return bad("duplicate path '" + f.path + "'");
        const std::string e = ext_of(f.path);
        if (e == "ini") {
            if (f.path != "wheels/" + p.label + ".ini") return bad("the definition must be wheels/" + p.label + ".ini");
            if (size > kMaxIni) return bad("definition too large");
            ++inis;
        } else if (e == "glb") {
            if (size > kMaxGlb) return bad(f.path + " is larger than 16 MB");
        } else if (e == "png") {
            if (size > kMaxPng) return bad(f.path + " is larger than 4 MB");
        } else {
            return bad("file type not allowed: " + f.path);
        }
        const ByteSpan d = r.bytes(size);
        f.data.assign(d.begin(), d.end());
        p.files.push_back(std::move(f));
    }
    if (!r.at_end()) return bad("trailing data");
    if (inis != 1) return bad("exactly one wheel definition expected");
    return p;
}

bool validate_wheel_package(const ModPackage& p, const Vfs& vfs, const std::string& expected_label, std::string* error) {
    if (p.label != expected_label) return fail(error, "package is for " + p.label + ", not " + expected_label);
    const ModPackage::File* ini = nullptr;
    for (const ModPackage::File& f : p.files)
        if (ext_of(f.path) == "ini") ini = &f;
    if (!ini) return fail(error, "no wheel definition");
    // The definition, its paths relative to an imaginary mod folder ("/" + path).
    const ModWheelDef d = parse_mod_wheel(std::string_view(reinterpret_cast<const char*>(ini->data.data()), ini->data.size()),
                                          ini->path, "package", "", false);
    if (!d.error.empty()) return fail(error, "definition: " + d.error);
    if (d.label != p.label) return fail(error, "definition names " + d.label + ", not " + p.label);
    if (vfs.source_of("wheel/" + d.label) == "disc") return fail(error, d.label + " is a wheel of this disc");
    // The files it references are exactly the package's other files.
    std::set<std::string> refs, others;
    for (const std::string* s : {&d.model, &d.menu, &d.race, &d.picture, &d.brand_logo}) {
        if (s->empty()) continue;
        if (const std::string w = disc_rim(*s); !w.empty()) { // (the disc's own rim: none in the package)
            if (!vfs.exists("wheel/" + w)) return fail(error, "its rim " + w + " is not on this disc");
            continue;
        }
        refs.insert(s->substr(1));
    }
    for (const ModPackage::File& f : p.files)
        if (&f != ini) others.insert(f.path);
    if (refs != others) return fail(error, "the package's files do not match what the definition references");
    for (const std::string* s : {&d.model, &d.menu, &d.race})
        if (!s->empty() && disc_rim(*s).empty() && ext_of(*s) != "glb") return fail(error, "rims must be .glb files");
    for (const std::string* s : {&d.picture, &d.brand_logo})
        if (!s->empty() && ext_of(*s) != "png") return fail(error, "pictures must be .png files");
    return check_package_contents(p, error);
}

} // namespace gt3r::game
