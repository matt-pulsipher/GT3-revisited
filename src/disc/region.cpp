#include "disc/region.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace gt3r {

const char* region_key(Region r) {
    switch (r) {
    case Region::Japan: return "jp";
    case Region::Europe: return "eu";
    case Region::America: break;
    }
    return "us";
}

const char* region_name(Region r) {
    switch (r) {
    case Region::Japan: return "NTSC-J";
    case Region::Europe: return "PAL";
    case Region::America: break;
    }
    return "NTSC-U";
}

std::optional<Region> region_from_key(std::string_view key) {
    for (Region r : {Region::Japan, Region::America, Region::Europe})
        if (key == region_key(r)) return r;
    return std::nullopt;
}

namespace {

// A NUL-terminated string in the program's data.
bool has_string(ByteSpan p, const char* s) {
    const size_t n = std::strlen(s) + 1; // with the terminator
    const u8* b = reinterpret_cast<const u8*>(s);
    for (auto it = p.begin(); (it = std::search(it, p.end(), b, b + n)) != p.end(); ++it)
        if (it == p.begin() || *(it - 1) == 0) return true; // the start of a string
    return false;
}

} // namespace

std::optional<Region> region_of_program(ByteSpan program) {
    if (has_string(program, "database/paramdb_eu.db")) return Region::Europe;
    if (has_string(program, "database/paramdb_us.db")) return Region::America;
    if (has_string(program, "database/paramdb.db")) return Region::Japan;
    return std::nullopt;
}

std::optional<Region> region_of_serial(std::string_view serial) {
    std::string s(serial.substr(0, 4));
    for (char& c : s) c = char(c >= 'a' && c <= 'z' ? c - 32 : c);
    if (s == "SCES" || s == "SLES" || s == "SCED") return Region::Europe;
    if (s == "SCPS" || s == "SLPS" || s == "PAPX" || s == "PCPX") return Region::Japan;
    if (s == "SCUS" || s == "SLUS" || serial == "PBPX_955.03") return Region::America;
    return std::nullopt;
}

const char* RegionData::db_suffix() const {
    switch (region) {
    case Region::Japan: return "";
    case Region::Europe: return "_eu";
    case Region::America: break;
    }
    return "_us";
}

const char* RegionData::gcf_path() const {
    return region == Region::Europe ? "gameconf/product_eu.gcf" : "gameconf/product.gcf";
}

std::vector<Language> RegionData::languages() const {
    switch (region) {
    case Region::Japan: return {Language::JP};
    case Region::Europe: return {Language::BR, Language::FR, Language::GR, Language::IT, Language::SP};
    case Region::America: break;
    }
    return {Language::US};
}

Language RegionData::default_language() const {
    switch (region) {
    case Region::Japan: return Language::JP;
    case Region::Europe: return Language::BR;
    case Region::America: break;
    }
    return Language::US;
}

std::string RegionData::course_path(std::string_view label, Language lang) const {
    if (region == Region::Japan) return "courses/" + std::string(label);
    const char* dir = "courses/EU/";
    switch (lang) {
    case Language::JP: dir = "courses/JP/"; break;
    case Language::US: dir = "courses/US/"; break;
    case Language::BR: dir = "courses/UK/"; break;
    default: break;
    }
    return dir + std::string(label);
}

std::vector<Language> playable_languages(const RegionData& rd, const std::function<bool(Language)>& has_menus) {
    std::vector<Language> out;
    for (Language l : rd.languages())
        if (has_menus(l)) out.push_back(l);
    if (out.empty())
        for (Language l : {Language::US, Language::JP, Language::BR, Language::FR, Language::GR, Language::IT, Language::SP})
            if (has_menus(l)) out.push_back(l);
    return out;
}

Language choose_language(const RegionData& rd, const std::vector<Language>& playable, std::optional<Language> wanted) {
    auto has = [&](Language l) { return std::find(playable.begin(), playable.end(), l) != playable.end(); };
    if (wanted && has(*wanted)) return *wanted;
    if (has(rd.default_language()) || playable.empty()) return rd.default_language();
    return playable.front();
}

Units language_units(Language lang) {
    Units u;
    switch (lang) {
    case Language::US: u.mph = true, u.power = "HP", u.power_hp = true, u.torque_ftlb = true; break;
    case Language::BR:
    case Language::IT:
    case Language::SP: u.power = "BHP", u.power_hp = true; break;
    case Language::FR: u.power = "CV", u.power_hp = true; break;
    case Language::JP:
    case Language::GR: break;
    }
    return u;
}

namespace {
std::optional<bool> g_speed_unit;
} // namespace

void set_speed_unit(std::optional<bool> mph) { g_speed_unit = mph; }

std::optional<bool> speed_unit() { return g_speed_unit; }

std::optional<bool> parse_speed_unit(std::string_view value) {
    while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
    std::string v(value);
    for (char& c : v) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (v == "kmh" || v == "km/h") return false;
    if (v == "mph") return true;
    return std::nullopt;
}

// The x100 (immediate 0x64) when
// the program finds language 0.
int money_scale(Language lang) { return lang == Language::JP ? 100 : 1; }

bool is_european(Language lang) { return int(lang) >= int(Language::BR); }

} // namespace gt3r
