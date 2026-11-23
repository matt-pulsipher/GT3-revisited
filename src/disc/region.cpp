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

const char* game_name(GameKind g) { return g == GameKind::Concept ? "Gran Turismo Concept" : "Gran Turismo 3"; }

const char* concept_release(std::string_view serial) {
    if (serial == "SCPS_150.10") return "2001 Tokyo";
    if (serial == "SCPS_560.05") return "Tokyo-Seoul"; // v1.02 and v2.00
    if (serial == "SCES_508.58") return "Tokyo-Geneva";
    if (serial == "SCPS_559.02" || serial == "SCPS_559.03") return "Tokyo-Geneva Asia";
    return nullptr;
}

namespace {

// Every whole string of the program that starts with `prefix` and ends with `suffix`, in the
// program's order.
std::vector<std::string> strings_like(ByteSpan p, std::string_view prefix, std::string_view suffix) {
    std::vector<std::string> out;
    const u8* b = reinterpret_cast<const u8*>(prefix.data());
    for (auto it = p.begin(); (it = std::search(it, p.end(), b, b + prefix.size())) != p.end(); ++it) {
        if (it != p.begin() && *(it - 1) != 0) continue; // not the start of a string
        const auto end = std::find(it, p.end(), u8(0));
        if (end == p.end()) break;
        std::string s(it, end);
        const bool printable = std::all_of(s.begin(), s.end(), [](char c) { return c > 0x20 && c < 0x7F && c != '%'; });
        if (printable && s.size() >= prefix.size() + suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0 &&
            std::find(out.begin(), out.end(), s) == out.end())
            out.push_back(std::move(s));
    }
    return out;
}

constexpr std::string_view kDbPrefix = "database/paramdb", kDbExt = ".db";
constexpr std::string_view kGcfPrefix = "gameconf/", kGcfExt = ".gcf";

} // namespace

ProgramFiles files_of_program(ByteSpan program) {
    ProgramFiles f;
    std::vector<std::string> dbs;
    for (const std::string& s : strings_like(program, kDbPrefix, kDbExt))
        dbs.push_back(s.substr(kDbPrefix.size(), s.size() - kDbPrefix.size() - kDbExt.size()));
    for (const char* known : {"_eu", "_us", ""}) // region_of_program's order
        if (!f.db_suffix && std::find(dbs.begin(), dbs.end(), known) != dbs.end()) f.db_suffix = known;
    if (!f.db_suffix && !dbs.empty()) f.db_suffix = dbs.front();
    f.gcf = strings_like(program, kGcfPrefix, kGcfExt);
    return f;
}

GameKind ProgramFiles::game() const {
    for (const std::string& g : gcf)
        if (g.compare(kGcfPrefix.size(), 7, "concept") == 0) return GameKind::Concept;
    return GameKind::Gt3;
}

std::string ProgramFiles::gcf_for(Language lang) const {
    if (gcf.empty()) return {};
    if (gcf.size() == 1) return gcf.front();
    // The plain name: the shortest (the others append "_<set>" to it).
    const std::string& plain = *std::min_element(gcf.begin(), gcf.end(), [](const std::string& a, const std::string& b) { return a.size() < b.size(); });
    if (lang == Language::JP) return plain;
    const char* set = lang == Language::US ? "_us" : lang == Language::KR ? "_kr" : "_eu";
    const std::string want = plain.substr(0, plain.size() - kGcfExt.size()) + set + std::string(kGcfExt);
    return std::find(gcf.begin(), gcf.end(), want) != gcf.end() ? want : plain;
}

std::optional<Region> region_of_serial(std::string_view serial) {
    std::string s(serial.substr(0, 4));
    for (char& c : s) c = char(c >= 'a' && c <= 'z' ? c - 32 : c);
    if (s == "SCES" || s == "SLES" || s == "SCED") return Region::Europe;
    if (s == "SCPS" || s == "SLPS" || s == "PAPX" || s == "PCPX") return Region::Japan;
    if (s == "SCUS" || s == "SLUS" || serial == "PBPX_955.03") return Region::America;
    return std::nullopt;
}

RegionData RegionData::of_program(Region region, const ProgramFiles& files) {
    RegionData rd{region};
    const RegionData plain{region};
    rd.game = files.game();
    if (files.db_suffix && *files.db_suffix != plain.db_suffix()) rd.program_db_suffix = files.db_suffix;
    std::string g = files.gcf_for(plain.default_language());
    // A Concept program that names a config for each language (Tokyo-Seoul v1.02: six, picked by its language,
    // 0x220FE0) starts in its release's own language, the one of the database it names (paramdb_kr.db: the "_kr" config).
    if (rd.game == GameKind::Concept && files.gcf.size() > 1 && files.db_suffix && !files.db_suffix->empty()) {
        const std::string& base = *std::min_element(files.gcf.begin(), files.gcf.end(), [](const std::string& a, const std::string& b) { return a.size() < b.size(); });
        const std::string want = base.substr(0, base.size() - kGcfExt.size()) + *files.db_suffix + std::string(kGcfExt);
        if (std::find(files.gcf.begin(), files.gcf.end(), want) != files.gcf.end()) g = want, rd.gcf_per_language = true;
    }
    if (!g.empty() && g != plain.gcf_path()) rd.program_gcf = std::move(g);
    return rd;
}

std::string RegionData::to_text() const {
    std::string t = region_key(region);
    if (game == GameKind::Concept) t += "\ngame=concept";
    if (program_db_suffix) t += "\ndb=" + *program_db_suffix;
    if (program_gcf) t += "\ngcf=" + *program_gcf;
    if (gcf_per_language) t += "\ngcfs=per-language";
    return t;
}

std::optional<RegionData> RegionData::from_text(std::string_view text) {
    RegionData rd;
    bool first = true;
    while (!text.empty() || first) {
        const size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (first) {
            const std::optional<Region> r = region_from_key(line);
            if (!r) return std::nullopt;
            rd.region = *r;
            first = false;
        } else if (line == "game=concept") {
            rd.game = GameKind::Concept;
        } else if (line.rfind("db=", 0) == 0) {
            rd.program_db_suffix = std::string(line.substr(3));
        } else if (line.rfind("gcf=", 0) == 0) {
            rd.program_gcf = std::string(line.substr(4));
        } else if (line == "gcfs=per-language") {
            rd.gcf_per_language = true;
        }
    }
    return rd;
}

std::string RegionData::db_suffix() const {
    if (program_db_suffix) return *program_db_suffix;
    switch (region) {
    case Region::Japan: return "";
    case Region::Europe: return "_eu";
    case Region::America: break;
    }
    return "_us";
}

std::string RegionData::gcf_path() const {
    if (program_gcf) return *program_gcf;
    return region == Region::Europe ? "gameconf/product_eu.gcf" : "gameconf/product.gcf";
}

namespace {

// A Concept release's own language, from the game config its program names (ProgramFiles): each program's language
// object starts as one language (its constructor: Tokyo-Geneva Europe 0x2469D8 stores 2, British English; Tokyo-Seoul
// 0x24A568 stores 7, Korean in that program's order; Tokyo-Geneva Asia 0x247028 stores 10, Traditional Chinese; the
// 2001 Tokyo program sets 0, Japanese, as it starts, 0x100108), and that is the language of the config it loads.
Language concept_language(const std::optional<std::string>& gcf) {
    const std::string g = gcf.value_or("");
    auto ends = [&](std::string_view tail) { return g.size() >= tail.size() && g.compare(g.size() - tail.size(), tail.size(), tail) == 0; };
    if (ends("_eu.gcf")) return Language::BR;
    if (ends("_kr.gcf")) return Language::KR;
    if (ends("_tw.gcf")) return Language::TW;
    if (ends("_cn.gcf")) return Language::CN;
    if (ends("_us.gcf")) return Language::US;
    return Language::JP;
}

} // namespace

std::vector<Language> RegionData::languages() const {
    if (game == GameKind::Concept) {
        // What the release's language page offers (menu/language.mbl: Europe's page "root", items br, fr, it, gr, sp,
        // pt; Asia's "root_tw", items tw, us); the releases without a language page run in their own language only.
        switch (concept_language(program_gcf)) {
        case Language::BR: return {Language::BR, Language::FR, Language::GR, Language::IT, Language::SP, Language::PT};
        case Language::TW: return {Language::TW, Language::US};
        default: return {concept_language(program_gcf)};
        }
    }
    switch (region) {
    case Region::Japan: return {Language::JP};
    case Region::Europe: return {Language::BR, Language::FR, Language::GR, Language::IT, Language::SP};
    case Region::America: break;
    }
    return {Language::US};
}

Language RegionData::default_language() const {
    if (game == GameKind::Concept) return concept_language(program_gcf);
    switch (region) {
    case Region::Japan: return Language::JP;
    case Region::Europe: return Language::BR;
    case Region::America: break;
    }
    return Language::US;
}

std::string RegionData::language_page() const {
    return game == GameKind::Concept && default_language() == Language::TW ? "root_tw" : "root";
}

std::string RegionData::course_path(std::string_view label, Language lang) const {
    // GT3's NTSC-J program reads courses/<label>; every other program, Concept's Japanese ones
    // too, goes through the language's folder (each names the four folders below).
    if (region == Region::Japan && game == GameKind::Gt3) return "courses/" + std::string(label);
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
        for (Language l : {Language::US, Language::JP, Language::BR, Language::FR, Language::GR, Language::IT, Language::SP,
                           Language::PT, Language::KR, Language::CN, Language::TW})
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
    // The languages Gran Turismo Concept adds, from its programs' language-to-units table (Tokyo-Geneva 0x325C80,
    // its Asian release 0x3263E0, Tokyo-Seoul 0x329520; the unit sets: switch 0x246F18): Portuguese takes French's
    // set (km/h, CV, kgf.m), Korean and both Chinese the Japanese one (km/h, PS, kgf.m).
    case Language::PT: u.power = "CV", u.power_hp = true; break;
    case Language::KR:
    case Language::CN:
    case Language::TW: break;
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

// The x100 of 0x21DCE0 / 0x237BC0 / 0x237C80 / 0x238950 / 0x238BC0 (immediate 0x64) when
// 0x231680 finds language 0.
int money_scale(Language lang) { return lang == Language::JP ? 100 : 1; }

bool is_european(Language lang) { return int(lang) >= int(Language::BR); }

} // namespace gt3r
