#include "vfs/mod_wheels.h"

#include "base/config.h"
#include "base/file.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>

namespace gt3r {

namespace {

// A value without its trailing "; comment" (IniDocument keeps those).
std::string value_of(const IniDocument& ini, const char* section, const char* key) {
    std::string v = ini.get(section, key).value_or("");
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] == ';' && (i == 0 || std::isspace(static_cast<unsigned char>(v[i - 1])))) {
            v.resize(i);
            break;
        }
    return std::string(trim(v));
}

std::string stem_of(const std::string& path) {
    std::string name = path.substr(path.find_last_of('/') + 1);
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

} // namespace

std::string disc_rim(const std::string& rim) { return rim.rfind("disc:", 0) == 0 ? rim.substr(5) : std::string(); }

bool valid_wheel_label(const std::string& label) {
    if (label.empty() || label.size() > 32) return false;
    for (char c : label)
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_'))
            return false;
    return true;
}

std::string wheel_brand_key(const std::string& brand, bool first_word) {
    std::string out;
    for (char c : trim(brand)) {
        if (first_word && std::isspace(static_cast<unsigned char>(c))) break;
        if (std::isalnum(static_cast<unsigned char>(c))) out += char(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

ModWheelDef load_mod_wheel(const std::string& file, const std::string& mod_id, const std::string& root) {
    std::optional<Bytes> text = read_host_file(file);
    if (!text) {
        ModWheelDef d;
        d.file = file;
        d.mod_id = mod_id;
        d.error = "cannot be read";
        return d;
    }
    return parse_mod_wheel(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()), file, mod_id, root);
}

ModWheelDef parse_mod_wheel(std::string_view text, const std::string& file, const std::string& mod_id,
                            const std::string& root, bool check_files) {
    ModWheelDef d;
    d.file = file;
    d.mod_id = mod_id;
    const IniDocument parsed = IniDocument::parse(text);
    const IniDocument* ini = &parsed;
    d.label = to_lower(value_of(*ini, "wheel", "label"));
    if (d.label.empty()) d.label = to_lower(stem_of(file));
    d.brand = value_of(*ini, "wheel", "brand");
    auto path = [&](const char* section, const char* key) {
        const std::string v = value_of(*ini, section, key);
        return v.empty() ? v : root + "/" + v;
    };
    auto rim = [&](const char* key) { // (a disc rim as written, lower case)
        const std::string v = value_of(*ini, "wheel", key);
        return to_lower(v).rfind("disc:", 0) == 0 ? "disc:" + to_lower(trim(v.substr(5))) : path("wheel", key);
    };
    d.model = rim("model");
    d.menu = rim("menu");
    d.race = rim("race");
    std::string shape_error;
    auto number = [&](const char* key, float& out) {
        const std::string v = value_of(*ini, "shape", key);
        if (v.empty()) return;
        char* end = nullptr;
        const float f = std::strtof(v.c_str(), &end);
        if (end == v.c_str() || *end) shape_error = "shape " + std::string(key) + " \"" + v + "\" is no number";
        else out = f;
    };
    number("depth", d.shape.depth);
    number("dish", d.shape.dish);
    number("lip", d.shape.lip);
    const std::array<const std::string*, 3> rims = {&d.model, &d.menu, &d.race};
    d.picture = path("wheel", "picture");
    d.brand_logo = path("brand", "logo");
    const std::string sale = to_lower(value_of(*ini, "wheel", "for_sale"));
    d.for_sale = !(sale == "no" || sale == "false" || sale == "off" || sale == "0");
    if (!shape_error.empty())
        d.error = shape_error;
    else if (!valid_rim_shape(d.shape))
        d.error = "shape out of range (depth 0.25 to 4, dish -0.5 to 0.5, lip 0.25 to 4)";
    else if (!valid_wheel_label(d.label))
        d.error = "label \"" + d.label + "\": use 1-32 lower case letters, digits and _";
    else if (d.model.empty() && d.menu.empty() && d.race.empty())
        d.error = "no model (model = <file>.glb)";
    else if (std::any_of(rims.begin(), rims.end(), [](const std::string* r) { return r->rfind("disc:", 0) == 0 && !valid_wheel_label(disc_rim(*r)); }))
        d.error = "a disc rim names no wheel (disc:<wheel>, as gt3r --browse wheel/ lists them)";
    else if (d.for_sale && wheel_brand_key(d.brand).empty())
        d.error = "no brand (brand = <the wheel shop maker>)";
    else if (check_files)
        for (const std::string* p : {&d.model, &d.menu, &d.race})
            if (!p->empty() && disc_rim(*p).empty() && !host_path_exists(*p)) {
                d.error = "missing " + p->substr(root.size() + 1);
                break;
            }
    return d;
}

std::vector<ModWheelDef> mod_wheels(const ModWheelReader& read) {
    std::vector<ModWheelDef> out;
    std::optional<Bytes> list = read ? read(Vfs::kModWheelsList) : std::nullopt;
    if (!list) return out;
    // "<mod id>\t<host path>\t<mod folder>\t<error the Vfs found>" per line.
    const std::string text(list->begin(), list->end());
    for (std::string_view line : split(text, '\n')) {
        std::vector<std::string_view> f = split(line, '\t');
        if (f.size() < 3) continue;
        ModWheelDef d = load_mod_wheel(std::string(f[1]), std::string(f[0]), std::string(f[2]));
        if (d.error.empty() && f.size() > 3) d.error = std::string(f[3]);
        out.push_back(std::move(d));
    }
    return out;
}

} // namespace gt3r
