#include "game/gt_mode.h"
#include "game/language_rules.h"
#include "sim/engine_condition.h"

#include "formats/bgm_index.h"
#include "menu/flow/menu_set.h"

#include "base/log.h"
#include "base/ps2_float.h"
#include "race/pool_draw.h"
#include "race/time_text.h"
#include "disc/core_image.h"
#include "vfs/vfs.h"
#include "formats/menu/pmb.h"
#include "formats/text/mltx.h"
#include "menu/menu_scene.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace gt3r::game {

// ---------------------------------------------------------------------------
// Small helpers

namespace {

bool ends(const std::string& s, const char* p) {
    std::string_view v(p);
    return s.size() >= v.size() && s.compare(s.size() - v.size(), v.size(), v) == 0;
}

// Championship points per finishing place: program constant, the signed bytes read by
// place (after them the program's data holds zeros, so places 7 and 8 score none).
// The same table.
constexpr int kChampPoints[6] = {10, 6, 4, 3, 2, 1};

// Event record (table 32) offsets (docs/formats/database.md).
constexpr size_t kEvRegulation = 0x190, kEvLaps = 0x199, kEvLicKind = 0x19A, kEvLicense = 0x19B, kEvCars = 0x1B5,
                 kEvGold = 0x1C4, kEvSpecial = 0x1D2, kEvPrize = 0x1D4, kEvBonus = 0x1E2, kEvLeague = 0x1E5,
                 kEvClass = 0x1F4, kEvAspiration = 0x1F5, kEvDrive = 0x1F6;

u32 license_time(ByteSpan r, size_t off) { return u32(r[off]) * 60000 + u32(r[off + 1]) * 1000 + rd16(r, off + 2); }

// Settings groups re-initialised when a part of the table is fitted.
void refresh_settings(sim::CarSettings& s, const sim::CarSettings& stock, int table) {
    switch (table) {
    case 1: s.brake_front = stock.brake_front, s.brake_rear = stock.brake_rear; break;
    case 5: s.downforce_front = stock.downforce_front, s.downforce_rear = stock.downforce_rear; break;
    case 11:
    case 12: s.turbo = stock.turbo; break;
    case 13: s.front_torque = stock.front_torque; break;
    case 17:
        s.gears = stock.gears, s.final_drive = stock.final_drive, s.gear_slider = stock.gear_slider;
        break;
    case 18:
        s.camber = stock.camber, s.ride_height = stock.ride_height, s.toe = stock.toe, s.spring = stock.spring;
        s.motion_ratio = stock.motion_ratio, s.damper = stock.damper, s.arb = stock.arb;
        break;
    case 21: s.lsd = stock.lsd; break;
    case 22: s.tcs = stock.tcs; break;
    case 23: s.ascc = stock.ascc; break;
    default: break;
    }
}

bool same_settings(const sim::CarSettings& a, const sim::CarSettings& b) {
    static_assert(std::is_trivially_copyable_v<sim::CarSettings>);
    return std::memcmp(&a, &b, sizeof a) == 0;
}

} // namespace

const char* license_name(License l) {
    static const char* const n[] = {"B", "A", "IB", "IA", "S", "R"};
    int i = int(l);
    return i >= 0 && i < 6 ? n[i] : "-";
}

const char* license_menu_id(License l) {
    static const char* const n[] = {"b", "a", "ib", "ia", "s", "d"};
    int i = int(l);
    return i >= 0 && i < 6 ? n[i] : "";
}

License license_from_menu_id(const std::string& id) {
    for (int i = 0; i < 6; ++i)
        if (id == license_menu_id(License(i))) return License(i);
    return License::None;
}

const char* medal_name(Medal m) {
    switch (m) {
    case Medal::Gold: return "gold";
    case Medal::Silver: return "silver";
    case Medal::Bronze: return "bronze";
    default: return "none";
    }
}

const char* check_name(Check c) {
    switch (c) {
    case Check::Ok: return "";
    case Check::NotForBuy: return "00cmn/not_for_buy";
    case Check::NoMoney: return "00cmn/no_money";
    case Check::MaxCar: return "00cmn/max_car";
    case Check::NotForSell: return "00cmn/not_for_sell";
    case Check::NoRideCar: return "00cmn/ride_car";
    case Check::NotForChangeWheel: return "00cmn/not_for_change_wheel";
    case Check::AlreadyHave: return "ts/already_you_have";
    case Check::AlreadyHaveTires: return "ts/already_you_have_all_tires";
    case Check::NotExist: return "ts/not_exist";
    case Check::NeedDirtTire: return "hm/need_dirt_tire";
    case Check::NoLicense: return "lc/no_license";
    case Check::NoLastExam: return "lc/no_last_exam";
    case Check::LicenseViolation: return "00cmn/license_violation";
    case Check::RegulationViolation: return "00cmn/regulation_violation";
    case Check::Unknown: return "?";
    }
    return "?";
}

std::string format_credits(s64 v) {
    // "%ld", then from the
    // right a ',' (immediate 0x2C) after every third character that has more before it. A
    // minus sign counts as a character (-100 -> "-,100"; the game never shows negative money).
    const std::string d = std::to_string(v);
    std::string out;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out.push_back(',');
        out.push_back(d[i]);
    }
    return out;
}

namespace {
// A program format string with its "%s" fields filled in order.
std::string fill(std::string_view format, std::initializer_list<std::string_view> fields) {
    std::string out;
    auto f = fields.begin();
    for (size_t i = 0; i < format.size(); ++i) {
        if (format.compare(i, 2, "%s") == 0 && f != fields.end()) {
            out += *f++;
            ++i;
        } else {
            out.push_back(format[i]);
        }
    }
    return out;
}
} // namespace

// Program constants, the same strings in the PAL and NTSC-J programs.
std::string credits_text(s64 v) { return fill("Cr. %s", {format_credits(v)}); }
std::string prize_remain_text(std::string_view prefix, s64 v) { return fill("%s Cr. %s", {prefix, format_credits(v)}); }

std::string format_time_ms(u32 ms) { return race::menu_time_text(ms); }

u32 record_stamp_now() {
    const std::time_t now = std::time(nullptr);
    const std::tm* t = std::localtime(&now);
    if (!t) return 1;
    // Days from 2000-01-01 to the local date (the civil-from-days inverse, March-based years).
    const int y = t->tm_year + 1900 - (t->tm_mon < 2 ? 1 : 0);
    const int m = t->tm_mon + 1;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t->tm_mday - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const s64 days = s64(era) * 146097 + doe - 730425; // (0000-03-01 to 2000-01-01)
    const s64 s = days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
    return s > 0 && s < s64(0xFFFFFFFF) ? u32(s) : 1;
}

std::string record_date_text(u32 stamp, Language lang) {
    if (stamp == 0) return std::string(race::kNoDateText); // (the font's dashes, every language)
    // The original's conversion: four-year cycles of 1461 days, each starting with a leap
    // year, then the month by the days before it; it picks the leap year's month table only
    // for the year 2000.
    static const int kYear[4] = {0, 366, 731, 1096};
    static const int kMonth[2][12] = {{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334},
                                      {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335}};
    const u32 days = stamp / 86400, d = days % 1461;
    int yi = 0;
    while (yi < 3 && int(d) >= kYear[yi + 1]) ++yi;
    const u32 year = days / 1461 * 4 + u32(yi);
    const int doy = int(d) - kYear[yi];
    const int* month = kMonth[year == 0 ? 1 : 0];
    int mi = 0;
    while (mi < 11 && doy >= month[mi + 1]) ++mi;
    char b[32];
    if (lang == Language::JP) std::snprintf(b, sizeof b, "%02u/%02d/%02d", year % 100, mi + 1, doy - month[mi] + 1);
    else std::snprintf(b, sizeof b, "%02d/%02d/%02u", doy - month[mi] + 1, mi + 1, year % 100);
    return b;
}

// ---------------------------------------------------------------------------
// GtData

std::string GtData::ArcadeCar::image() const {
    // "arm_tt_00" -> "tt_00"; "nsx_s_zero_97_290" -> "nsx_s_zero_97" (the power suffix).
    if (label.rfind("arm_", 0) == 0) return label.substr(4);
    size_t u = label.rfind('_');
    if (u == std::string::npos || u + 1 >= label.size()) return label;
    for (size_t i = u + 1; i < label.size(); ++i)
        if (label[i] < '0' || label[i] > '9') return label;
    return label.substr(0, u);
}

namespace {

// The program's table of built-in record names (the time-trial seeds, gt_mode §16): three
// per time trial (30) consecutive pointers to short "X.Name" strings in the program's
// data. Found by a pattern search over the program (the same table on 1.00 and
// 1.10); an unexpected program gives none.
std::vector<std::string> find_builtin_names(const CoreImage& img) {
    std::vector<std::string> out;
    const Bytes& p = img.program;
    const u32 base = img.load_address, end = base + u32(p.size());
    auto name_at = [&](u32 addr, std::string& s) {
        if (addr < base || addr >= end) return false;
        s.clear();
        for (u32 a = addr; a < end && s.size() < 16; ++a) {
            char c = char(p[a - base]);
            if (!c) break;
            s.push_back(c);
        }
        // "X.Surname": a capital, a dot, a capital, then at least two letters.
        if (s.size() < 5 || s.size() > 15 || !std::isupper(u8(s[0])) || s[1] != '.' || !std::isupper(u8(s[2])))
            return false;
        for (size_t i = 3; i < s.size(); ++i)
            if (!std::isalpha(u8(s[i]))) return false;
        return true;
    };
    constexpr u32 kNames = 3 * kTimeTrials;
    auto table_at = [&](u32 addr) {
        std::vector<std::string> t(kNames);
        if (addr < base || addr + 4 * kNames > end || (addr & 3)) return std::vector<std::string>{};
        for (u32 i = 0; i < kNames; ++i)
            if (!name_at(rd32(p, addr - base + 4 * i), t[i])) return std::vector<std::string>{};
        return t;
    };
    for (u32 a = base; out.empty() && a + 4 * kNames <= end; a += 4) out = table_at(a);
    if (out.empty()) LOG_WARN("gt mode: the program's built-in record names were not found");
    return out;
}

// The built-in default text of a realtime text slot (text/realtime.dat key "<group>_<name>",
// used when the language's value is empty). The program registers each group with its name,
// an array of slot names, an array of the slots' texts (initially the defaults) and the slot
// count, all set up just before the call: the name's address built from its two halves, the
// arrays' addresses as offsets from the global pointer, the count a small constant
// (PRIZE_RT_MSG: names, texts, one slot).
// The global pointer is the value the start-up code
// builds and hands the thread setup.
// "" when the program has no such slot.
std::string find_realtime_default(const CoreImage& img, std::string_view group, std::string_view name) {
    const Bytes& p = img.program;
    const u32 base = img.load_address, end = base + u32(p.size());
    auto word = [&](u32 a) { return a >= base && a + 4 <= end && !(a & 3) ? rd32(p, a - base) : 0u; };
    auto string_at = [&](u32 a) {
        std::string s;
        for (; a >= base && a < end && p[a - base] && s.size() < 256; ++a) s.push_back(char(p[a - base]));
        return s;
    };
    constexpr u32 kZero = 0, kA0 = 4, kA1 = 5, kA2 = 6, kA3 = 7, kT0 = 8, kGp = 28;
    auto is_lui = [](u32 op, u32 rt) { return op >> 26 == 0x0F && (op >> 16 & 31) == rt; };
    auto is_addiu = [](u32 op, u32 rs, u32 rt) { return op >> 26 == 0x09 && (op >> 21 & 31) == rs && (op >> 16 & 31) == rt; };
    auto imm = [](u32 op) { return u32(s32(s16(op & 0xFFFF))); };
    // The global pointer: the value the start-up code builds and then moves into it.
    u32 gp = 0, a0_hi = 0, a0 = 0;
    for (u32 a = img.entry_point; a < img.entry_point + 4 * 64 && !gp; a += 4) {
        const u32 op = word(a);
        if (is_lui(op, kA0)) a0_hi = op << 16, a0 = a0_hi;
        else if (is_addiu(op, kA0, kA0)) a0 = a0_hi + imm(op);
        else if (op == (kA0 << 21 | kZero << 16 | kGp << 11 | 0x2D)) gp = a0;
    }
    if (!gp) return {};
    for (u32 a = base; a + 4 <= end; a += 4) {
        const u32 lui = word(a);
        if (!is_lui(lui, kA1)) continue;
        u32 group_at = 0, names = 0, texts = 0, count = 0;
        for (u32 k = 1; k <= 8; ++k) {
            const u32 op = word(a + 4 * k);
            if (is_addiu(op, kA1, kA1)) group_at = (lui << 16) + imm(op);
            else if (is_addiu(op, kGp, kA2)) names = gp + imm(op);
            else if (is_addiu(op, kGp, kA3)) texts = gp + imm(op);
            else if (is_addiu(op, kZero, kT0)) count = imm(op);
        }
        if (!group_at || !names || !texts || !count || count > 256 || string_at(group_at) != group) continue;
        for (u32 i = 0; i < count; ++i)
            if (string_at(word(names + 4 * i)) == name) return string_at(word(texts + 4 * i));
    }
    return {};
}

} // namespace

const std::map<std::string, std::string>& GtData::name_entry_chars(const std::string& lang) const {
    static const std::map<std::string, std::string> kNone;
    for (const std::string& l : {lang, name_language_, std::string("US")})
        if (auto it = name_chars.find(l); it != name_chars.end() && !it->second.empty()) return it->second;
    return kNone;
}

bool GtData::load(const Reader& read) {
    try {
        // The disc's regional data set and the game's language (disc/region.h): the database
        // and game config; the units, money and names.
        const RegionData rd = region_data(read);
        region = rd.region;
        language = language_of(read);
        units = language_units(language);
        money_scale = gt3r::money_scale(language);
        if (!db.load(read, std::string(rd.db_suffix()))) {
            error_ = db.player_error();
            return false;
        }
        auto need = [&](const std::string& p) {
            auto b = read(p);
            if (!b) throw FormatError(p + " not found");
            return std::move(*b);
        };
        gcf = Gcf::parse(need(rd.gcf_path()));
        // The game's sums, by name (gcf tag 10; x100 in Japanese). The program reads
        // a missing one through a null pointer, so a game config without them is refused.
        auto sum = [&](const char* name) {
            std::optional<u32> v = gcf.value(name);
            if (!v) throw FormatError(std::string(rd.gcf_path()) + ": no '" + name + "' value (tag 10)");
            return money(*v);
        };
        initial_credits_ = sum("initial");
        car_wash_price_ = sum("car_wash");
        oil_change_price_ = sum("oil_change");
        wheel_price_ = sum("wheel");
        if (auto c = read("database/carcolor.db")) {
            colors_.parse(*c);
            // Colour names: the definitions {u32 id, u32 name, u32 second name, u32 rgb} index
            // carcolor.sdb; the language picks the name (docs/dev/GT_MODE.md "Language", color_name_field).
            color_names_.clear();
            if (auto sdb = read("database/carcolor.sdb")) {
                db::StringTable names = db::StringTable::parse(*sdb);
                ByteSpan d(*c);
                u32 defs = rd32(d, 0x10), n = rd32(d, defs);
                auto name_at = [&](size_t o) {
                    std::string nm = names.at(rd32(d, o));
                    while (!nm.empty() && nm.back() == '\0') nm.pop_back();
                    return nm;
                };
                for (u32 i = 0; i < n && defs + 4 + 16 * (i + 1) <= d.size(); ++i) {
                    size_t o = defs + 4 + 16 * size_t(i);
                    color_names_[rd32(d, o)] = {name_at(o + color_name_field(language)), name_at(o + 4)};
                }
            }
        }
        // From the program (CORE.GT3): its built-in record names and realtime-text defaults.
        builtin_names.clear();
        std::string prize_default;
        if (auto core = read(Vfs::kProgramFile)) {
            try {
                const CoreImage img = CoreImage::decode(*core);
                builtin_names = find_builtin_names(img);
                prize_default = find_realtime_default(img, "PRIZE_RT_MSG", "BONUS_PREFIX");
                if (prize_default.empty()) LOG_WARN("gt mode: the program's PRIZE_RT_MSG_BONUS_PREFIX was not found");
            } catch (const std::exception& e) {
                LOG_WARN("gt mode: %s: %s", Vfs::kProgramFile, e.what());
            }
        }
        // The prize pages' "%s" before "Cr.": the language's realtime text, else the default.
        prize_prefix = prize_default;
        if (auto rt = read("text/realtime.dat")) {
            try {
                const text::StringTable t = text::StringTable::parse(*rt);
                const int k = t.find("PRIZE_RT_MSG_BONUS_PREFIX");
                if (k >= 0 && int(language) < t.language_count() && !t.raw(size_t(k), language).empty())
                    prize_prefix = t.raw(size_t(k), language);
            } catch (const std::exception& e) {
                LOG_WARN("gt mode: text/realtime.dat: %s", e.what());
            }
        }
        // The race songs of bgm/ads.inf in index order (the options' music list, gt_mode §12.2).
        race_songs.clear();
        if (auto ads = read("bgm/ads.inf"))
            for (const BgmTrack& t : parse_bgm_index(*ads))
                if (t.name.rfind("race_", 0) == 0) race_songs.push_back(t.file);
        Bytes mbl = need("menu/gt_mode.mbl");
        // Name entry characters: the text of each item of each language's name entry page.
        name_chars.clear();
        name_language_ = text::language_code(rd.default_language());
        for (int i = 0; menu::kMenuLanguages[i]; ++i)
            if (auto pmb = read(std::string("menu/") + menu::kMenuLanguages[i] + "/name_entry1.pmb")) {
                try { // some discs carry unfinished pages for languages their program never uses
                    menu::PmbFile f = menu::PmbFile::parse(*pmb);
                    std::map<std::string, std::string>& chars = name_chars[menu::kMenuLanguages[i]];
                    for (const menu::Page& p : f.pages)
                        for (const menu::Element& el : p.items)
                            if (el.text) chars[el.name] = el.text->text;
                } catch (const std::exception& e) {
                    LOG_INFO("gt mode: menu/%s/name_entry1.pmb: %s", menu::kMenuLanguages[i], e.what());
                }
            }
        {
            // The same flow the menus see (mod cars and makers at the dealers).
            menu::MblFile flow = menu::MblFile::parse(mbl);
            if (const auto& patch = menu::flow::menu_patches().flow) patch("gt_mode", read, flow);
            catalog = Catalog::from_mbl(flow, &db);
        }
        prizes.clear();
        if (const Gcf::Section* s = gcf.section(15)) {
            for (const Gcf::Entry& e : s->entries) {
                PrizeSet p;
                p.name = e.name;
                u32 n = e.u32_at(4), list = e.u32_at(8);
                for (u32 j = 0; j < n && list + 4 * j + 4 <= e.bytes.size(); ++j) {
                    s32 se = s32(e.u32_at(list + 4 * j));
                    if (se < 0 || size_t(se) + 16 > e.bytes.size()) continue;
                    PrizeCar c;
                    c.id = s32(e.u32_at(size_t(se)));
                    c.car = e.str_at(size_t(se) + e.u32_at(size_t(se) + 4));
                    c.color_id = s32(e.u32_at(size_t(se) + 8));
                    c.weight = s32(e.u32_at(size_t(se) + 12));
                    p.cars.push_back(std::move(c));
                }
                prizes.push_back(std::move(p));
            }
        }
        demos.clear();
        if (const Gcf::Section* s = gcf.section(12))
            for (const Gcf::Entry& e : s->entries)
                demos.push_back({e.name, e.str_at(e.u32_at(4)), e.u32_at(8) ? e.str_at(e.u32_at(8)) : std::string(),
                                 int(e.u32_at(16))});
        if (auto idle = gcf.section_u32(0, 0); idle && *idle > 0 && *idle < 3600) title_idle_seconds = int(*idle);
        // Arcade lists: group entries {name, count, list} whose list offsets
        // (relative to the group) lead to the course / car entries.
        auto groups = [&](u32 tag, auto&& each) {
            if (const Gcf::Section* sec = gcf.section(tag))
                for (const Gcf::Entry& e : sec->entries) {
                    u32 n = e.u32_at(4), list = e.u32_at(8);
                    for (u32 j = 0; j < n && list + 4 * j + 4 <= e.bytes.size(); ++j) {
                        s32 se = s32(e.u32_at(list + 4 * j));
                        if (se >= 0 && size_t(se) + 16 <= e.bytes.size()) each(e, size_t(se));
                    }
                }
        };
        arcade_courses.clear();
        groups(8, [&](const Gcf::Entry& e, size_t se) {
            ArcadeCourse c;
            c.course = e.str_at(se + e.u32_at(se));
            c.level = int(s16(rd16(e.bytes, se + 6)));
            c.length_m = e.u32_at(se + 8);
            arcade_courses[e.name].push_back(std::move(c));
        });
        arcade_classes.clear();
        groups(14, [&](const Gcf::Entry& e, size_t se) {
            ArcadeCar c;
            c.label = e.str_at(se + e.u32_at(se));
            c.race_code = e.str_at(se + e.u32_at(se + 4));
            c.enabled = rd16(e.bytes, se + 8) == 1;
            c.level = int(s16(rd16(e.bytes, se + 0xA)));
            c.variant = s32(e.u32_at(se + 0xC));
            arcade_classes[e.name].push_back(std::move(c));
        });
        // Mod cars join their arcade class after the disc's own (game/data/mod_cars.h [arcade]).
        for (const data::ModCar& m : db.mod_cars()) {
            if (m.arcade_class.empty()) continue;
            ArcadeCar c;
            c.label = m.label;
            c.level = m.arcade_level;
            c.variant = m.arcade_variant;
            arcade_classes[m.arcade_class].push_back(std::move(c));
        }
        race_codes.clear();
        unlock_level.clear();
        if (const Gcf::Section* s = gcf.section(4))
            for (const Gcf::Entry& e : s->entries) {
                race_codes.push_back(e.name);
                unlock_level[e.name] = int(e.u32_at(4));
            }
    } catch (const std::exception& e) {
        error_ = e.what();
        return false;
    }
    return true;
}

const PrizeSet* GtData::prize_set(const std::string& name) const {
    for (const PrizeSet& p : prizes)
        if (p.name == name) return &p;
    return nullptr;
}

PrizeCar GtData::prize_candidate(const PrizeCar& pc) const {
    PrizeCar c = pc;
    bool swapped = false;
    c.car = prize_car_label(pc.car, language, &swapped);
    if (swapped) c.color_id = -1;
    return c;
}

std::string GtData::model(const std::string& car) const {
    const data::Car* c = db.car(car);
    return c ? db.model_of(*c) : std::string();
}

// Mod cars' own paints ([paints]) get ids of their own: kModColor | car << 12 | paint.
namespace {
constexpr u32 kModColor = 0xF0000000u;
const data::ModCar* mod_with_paints(const data::CarDatabase& db, const std::string& car) {
    const data::ModCar* m = db.mod_car(car);
    return m && !m->paints.empty() ? m : nullptr;
}
} // namespace

int GtData::color_count(const std::string& car) const {
    if (const data::ModCar* m = mod_with_paints(db, car)) return int(m->paints.size());
    const std::vector<u32>* ids = colors_.ids(model(car));
    return ids ? int(ids->size()) : 0;
}

int GtData::color_index(const std::string& car, int color_id) const {
    if (color_id < 0) return 0;
    if (mod_with_paints(db, car) && (u32(color_id) & 0xF0000000u) == kModColor) return int(u32(color_id) & 0xFFFu);
    int i = colors_.index_of(model(car), u32(color_id));
    return i < 0 ? 0 : i;
}

u32 GtData::color_id(const std::string& car, int index) const {
    if (const data::ModCar* m = mod_with_paints(db, car)) {
        if (index < 0 || index >= int(m->paints.size())) return 0xFFFFFFFFu;
        u32 car_no = u32(m - db.mod_cars().data());
        return kModColor | (car_no & 0xFFFFu) << 12 | u32(index);
    }
    return colors_.id_at(model(car), index);
}

namespace {
const std::pair<std::string, std::array<float, 3>>* mod_paint(const data::CarDatabase& db, u32 id) {
    if ((id & 0xF0000000u) != kModColor) return nullptr;
    size_t car = (id >> 12) & 0xFFFFu, i = id & 0xFFFu;
    if (car >= db.mod_cars().size() || i >= db.mod_cars()[car].paints.size()) return nullptr;
    return &db.mod_cars()[car].paints[i];
}
} // namespace

u32 GtData::color_rgb(u32 id) const {
    if (const auto* p = mod_paint(db, id)) {
        auto b = [](float v) { return u32(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
        return b(p->second[0]) | b(p->second[1]) << 8 | b(p->second[2]) << 16;
    }
    return colors_.rgb(id);
}

std::string GtData::color_name(u32 id) const {
    if (const auto* p = mod_paint(db, id)) return p->first;
    auto it = color_names_.find(id);
    return it == color_names_.end() ? std::string() : it->second.first;
}

std::string GtData::color_name_latin(u32 id) const {
    if (const auto* p = mod_paint(db, id)) return p->first;
    auto it = color_names_.find(id);
    return it == color_names_.end() ? std::string() : it->second.second;
}

// ---------------------------------------------------------------------------
// GtMode

GtMode::GtMode(std::shared_ptr<const GtData> data) : data_(std::move(data)) {
    new_game();
    // The game's start-up seeding of the GT-mode state (gt_mode §20.2): a hardware
    // random value, stepped once.
    state_.rng = hardware_random();
    GameRandom::next(state_.rng);
}

void GtMode::new_game() {
    u32 rng = state_.rng; // a new game keeps the running generator state
    state_ = GameState{};
    state_.rng = rng;
    state_.credits = data_->starting_credits();
    state_.day = 1;
}

void GtMode::stir_random() {
    // The trophy_get / car_get controls open with 1..12 steps (gt_mode §20.2).
    GameRandom::stir(state_.rng, hardware_random(), shared_random());
}

int prize_paint_index(int count, u32& rng) {
    int i = ps2::to_int(ps2::add(ps2::mul(float(count), GameRandom::rand01(rng)), 0.5f));
    return u32(i) < u32(count) ? i : 0;
}

int pick_weighted(const std::vector<int>& weights, u32& rng) {
    // Weights as floats, at least 1; the first whose running sum reaches unit x total.
    std::vector<float> w;
    float total = 0;
    for (int c : weights) {
        w.push_back(std::max(float(c), 1.0f));
        total = ps2::add(total, w.back());
    }
    float x = ps2::mul(total, GameRandom::rand01(rng)), sum = 0;
    for (size_t i = 0; i < w.size(); ++i) {
        sum = ps2::add(w[i], sum);
        if (x <= sum) return int(i);
    }
    return 0;
}

int pick_trophy_index(float completion, u32& rng) {
    // Weight_i + 165.333 c^3; the first trophy whose running sum reaches unit x total (none: 0).
    static const float kWeight[10] = {400, 350, 250, 450, 2, 160, 1, 500, 25, 120};
    const float c = completion;
    const float extra = ps2::mul(ps2::mul(ps2::mul(c, 165.333f), c), c);
    auto weight = [&](int i) { return ps2::add(kWeight[i], extra > 0 ? extra : 0.0f); };
    float total = 0;
    for (int i = 0; i < 10; ++i) total = ps2::add(total, weight(i));
    float x = ps2::mul(total, GameRandom::rand01(rng)), sum = 0;
    for (int i = 0; i < 10; ++i) {
        sum = ps2::add(sum, weight(i));
        if (x <= sum) return i;
    }
    return 0;
}

s64 GtMode::car_price(const std::string& car) const {
    // The car record's u32 +0xEC, x100 in Japanese.
    const data::Car* c = db().car(car);
    return c ? s64(data_->money(c->price)) : 0;
}

u8 GtMode::car_flags(const std::string& car) const {
    const data::Car* c = db().car(car);
    return c && c->bytes.size() > 0xF1 ? c->bytes[0xF0] : 0;
}

u8 GtMode::car_category(const std::string& car) const {
    const data::Car* c = db().car(car);
    return c && c->bytes.size() > 0xF1 ? c->bytes[0xF1] : 0;
}

s64 GtMode::sell_price(const GarageCar& c) const {
    // Program constant: the catalogue price (scaled) / 4, a signed 32-bit division (sra 2 after
    // the round-toward-zero +3): the sale, the garage car page's price
    // and the garage list's price column (garage button). The same in the
    // PAL and NTSC-J programs.
    return s64(s32(car_price(c.car)) / 4);
}

void GtMode::set_stock_ownership(GarageCar& g) const {
    g.owned.reset();
    for (size_t i = 0; i < g.parts.size(); ++i) {
        int t = data::kCarPartTables[i];
        if (!g.parts[i] || t < 0) continue;
        const ByteSpan* r = db().record(data::TableId(t), g.parts[i]);
        if (!r || r->size() <= 0x10) continue;
        int id = goods_id(t, (*r)[0x10]);
        if (id >= 0) g.owned.set(size_t(id));
    }
    // Front tire goods also own the matching rear set.
    for (int f = 56; f <= 65; ++f)
        if (g.owned.test(size_t(f))) g.owned.set(size_t(f + 11));
    // Rally cars come with dirt tyres.
    if (car_category(g.car) == 2) g.owned.set(66), g.owned.set(77);
}

std::optional<GarageCar> GtMode::make_car(const std::string& car) const {
    const data::Car* c = db().car(car);
    if (!c) return std::nullopt;
    GarageCar g;
    g.car = car;
    g.parts = c->parts;
    g.parts[26] = 0; // wheel slot: stock
    g.settings = sim::stock_settings(db(), car, g.parts);
    g.ref_final = g.settings.final_drive;
    set_stock_ownership(g);
    g.cond = CarCondition{};
    return g;
}

Check GtMode::can_buy_car(const std::string& car) const {
    if (!db().car(car)) return Check::Unknown;
    if (car_flags(car) & 1) return Check::NotForBuy;
    if (state_.credits < car_price(car)) return Check::NoMoney;
    if (int(state_.garage.size()) >= kMaxGarageCars) return Check::MaxCar;
    return Check::Ok;
}

int GtMode::place_in_garage(GarageCar c) {
    if (int(state_.garage.size()) >= kMaxGarageCars) return -1;
    c.serial = state_.next_serial++;
    // The original inserts the new car at the head of the garage list.
    state_.garage.insert(state_.garage.begin(), std::move(c));
    if (state_.ride >= 0) ++state_.ride;
    return 0;
}

int GtMode::buy_car(const std::string& car, int color) {
    if (can_buy_car(car) != Check::Ok) return -1;
    std::optional<GarageCar> g = make_car(car);
    if (!g) return -1;
    int n = data_->color_count(car);
    g->color = n > 0 ? std::clamp(color, 0, n - 1) : std::max(color, 0);
    int idx = place_in_garage(std::move(*g));
    if (idx < 0) return -1;
    state_.credits -= car_price(car);
    if (state_.ride < 0) state_.ride = idx; // the first car is the ride car
    return idx;
}

Check GtMode::can_buy_guest_car(const GarageCar& c) const {
    if (!db().car(c.car)) return Check::Unknown;
    if (car_flags(c.car) & 2) return Check::NotForBuy; // the "not for sale" bit (prize-only models)
    if (state_.credits < s64(car_price(c.car))) return Check::NoMoney;
    if (int(state_.garage.size()) >= kMaxGarageCars) return Check::MaxCar;
    return Check::Ok;
}

int GtMode::buy_guest_car(const GarageCar& c) {
    if (can_buy_guest_car(c) != Check::Ok) return -1;
    GarageCar g = c;
    g.wheel.clear(); // the wheel is not in the buyer's inventory
    int idx = place_in_garage(std::move(g));
    if (idx < 0) return -1;
    state_.credits -= car_price(c.car);
    if (state_.ride < 0) state_.ride = idx;
    return idx;
}

int GtMode::add_prize_car(const std::string& car, int color_id, const std::string& origin) {
    return add_prize_car_paint(car, data_->color_index(car, color_id), origin);
}

int GtMode::add_prize_car_paint(const std::string& car, int paint, const std::string& origin) {
    std::optional<GarageCar> g = make_car(car);
    if (!g) return -1;
    g->color = paint;
    g->origin = origin;
    int idx = place_in_garage(std::move(*g));
    if (idx < 0) return -1;
    ++state_.prize_cars;
    if (state_.ride < 0) state_.ride = idx;
    return idx;
}

Check GtMode::can_sell(int index) const {
    if (index < 0 || index >= int(state_.garage.size())) return Check::Unknown;
    if (car_flags(state_.garage[size_t(index)].car) & 2) return Check::NotForSell;
    return Check::Ok;
}

s64 GtMode::sell_car(int index) {
    if (can_sell(index) != Check::Ok) return 0;
    s64 v = sell_price(state_.garage[size_t(index)]);
    state_.garage.erase(state_.garage.begin() + index);
    if (state_.ride == index) state_.ride = -1;
    else if (state_.ride > index) --state_.ride;
    state_.credits += v;
    return v;
}

void GtMode::ride(int index) {
    if (index >= 0 && index < int(state_.garage.size())) state_.ride = index;
}

std::vector<int> GtMode::garage_order(int key) const {
    const auto& g = state_.garage;
    std::vector<int> order(g.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
    auto name = [&](const GarageCar& c) {
        const data::Car* car = db().car(c.car);
        return car ? car->name : c.car;
    };
    auto maker = [&](const GarageCar& c) {
        const data::Car* car = db().car(c.car);
        return car ? car->maker : std::string();
    };
    auto drive = [&](const GarageCar& c) {
        auto d = db().drivetrain(c.parts[size_t(part_slot(13))]);
        return d ? int(d->drive_type) : 9;
    };
    auto turbo = [&](const GarageCar& c) {
        const ByteSpan* t = db().record(data::TableId::TurbineKit, c.parts[size_t(part_slot(12))]);
        return t && t->size() > 0x11 && (*t)[0x11] != 0 ? 1 : 0;
    };
    std::vector<float> power;
    if (key == 6)
        for (const GarageCar& c : g) power.push_back(power_ps(c));
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const GarageCar& x = g[size_t(a)];
        const GarageCar& y = g[size_t(b)];
        switch (key) {
        case 1: return name(x) < name(y);
        case 2: return maker(x) != maker(y) ? maker(x) < maker(y) : name(x) < name(y);
        case 3: return x.color < y.color;
        case 4: return car_price(x.car) > car_price(y.car);
        case 5: return x.cond.odometer_m > y.cond.odometer_m;
        case 6: return power[size_t(a)] > power[size_t(b)];
        case 7: return drive(x) < drive(y);
        case 8: return turbo(x) < turbo(y);
        default: return x.serial > y.serial; // date: newest first
        }
    });
    return order;
}

std::string GtMode::sort_value(const GarageCar& c, int key) const {
    char b[64];
    switch (key) {
    case 2: {
        const data::Car* car = db().car(c.car);
        return car ? car->maker : std::string();
    }
    case 3: return data_->color_name(data_->color_id(c.car, c.color));
    // The price column (garage button): what the car sells for, digits only; "-" for
    // a car that cannot be sold (flag bit 1).
    case 4: return car_flags(c.car) & 2 ? std::string("-") : format_credits(sell_price(c));
    case 5: std::snprintf(b, sizeof b, "%.1f km", c.cond.odometer_m / 1000.0); return b;
    case 6: return power_text(power_ps(c), " ");
    case 7: {
        auto d = db().drivetrain(c.parts[size_t(part_slot(13))]);
        return d ? data::drive_type_name(d->drive_type) : "";
    }
    case 8: {
        const ByteSpan* t = db().record(data::TableId::TurbineKit, c.parts[size_t(part_slot(12))]);
        return t && t->size() > 0x11 && (*t)[0x11] != 0 ? "TURBO" : "NA";
    }
    default: return {};
    }
}

std::string GtMode::model_of(const GarageCar& c) const {
    const data::Car* car = db().car(c.car);
    if (!car) return {};
    if (auto b = db().body(c.parts[5])) return b->model;
    return db().model_of(*car);
}

// --- Tune shop --------------------------------------------------------------

std::optional<PartOffer> GtMode::part_offer(int goods) const {
    const GarageCar* c = state_.ride_car();
    if (!c) return std::nullopt;
    std::optional<PartOffer> o = find_part(db(), c->car, goods);
    if (o) o->price = part_price(o->price);
    return o;
}

bool GtMode::owns(int goods) const {
    const GarageCar* c = state_.ride_car();
    return c && goods >= 0 && goods < kGoodsCount && c->owned.test(size_t(goods));
}

Check GtMode::can_buy_part(int goods) const {
    const GarageCar* c = state_.ride_car();
    if (!c) return Check::NoRideCar;
    if (owns(goods)) return Check::AlreadyHave;
    std::optional<PartOffer> o = part_offer(goods);
    if (!o) return Check::NotExist;
    if (state_.credits < s64(o->price)) return Check::NoMoney;
    return Check::Ok;
}

bool GtMode::buy_part(int goods) {
    if (can_buy_part(goods) != Check::Ok) return false;
    GarageCar& c = *state_.ride_car();
    state_.credits -= part_offer(goods)->price;
    c.owned.set(size_t(goods));
    if (int r = rear_tire_goods(goods); r >= 0) c.owned.set(size_t(r));
    return true;
}

bool GtMode::fit(GarageCar& c, int goods) const {
    Goods g = goods_info(goods);
    int slot = part_slot(g.table);
    const data::Car* car = db().car(c.car);
    if (slot < 0 || !car) return false;
    u64 id = 0;
    if (std::optional<PartOffer> o = find_part(db(), c.car, goods)) id = o->id;
    else if (g.stage == 0) id = car->parts[size_t(slot)]; // stage 0 without a record = back to stock
    else return false;
    c.parts[size_t(slot)] = id;
    // Turbo kits and NA tunes exclude each other: fitting one restores the other's stock part.
    if (g.table == 11) c.parts[size_t(part_slot(12))] = car->parts[size_t(part_slot(12))];
    if (g.table == 12) c.parts[size_t(part_slot(11))] = car->parts[size_t(part_slot(11))];
    sim::CarSettings stock = sim::stock_settings(db(), c.car, c.parts);
    refresh_settings(c.settings, stock, g.table);
    if (g.table == 17) c.ref_final = c.settings.final_drive;
    // A tire set: the front goods fits the matching rear tire too.
    if (int r = rear_tire_goods(goods); r >= 0) fit(c, r);
    return true;
}

bool GtMode::fit_part(int goods) {
    GarageCar* c = state_.ride_car();
    if (!c || goods < 0 || goods >= kGoodsCount || !c->owned.test(size_t(goods))) return false;
    return fit(*c, goods);
}

s64 GtMode::all_tires_price() const {
    s64 sum = 0;
    for (int g = 58; g <= 64; ++g)
        if (!owns(g))
            if (auto o = part_offer(g)) sum += o->price;
    return sum;
}

Check GtMode::can_buy_all_tires() const {
    if (!state_.ride_car()) return Check::NoRideCar;
    s64 sum = all_tires_price();
    if (sum == 0) return Check::AlreadyHaveTires;
    if (state_.credits < sum) return Check::NoMoney;
    return Check::Ok;
}

bool GtMode::buy_all_tires() {
    if (can_buy_all_tires() != Check::Ok) return false;
    GarageCar& c = *state_.ride_car();
    for (int g = 58; g <= 64; ++g)
        if (!c.owned.test(size_t(g)))
            if (auto o = part_offer(g)) {
                state_.credits -= o->price;
                c.owned.set(size_t(g));
                c.owned.set(size_t(rear_tire_goods(g)));
            }
    return true;
}

PowerPeak GtMode::power_peak(const GarageCar& c) const {
    // The menu figure: the built curve x the garage car's condition factor (gt_mode §5).
    std::optional<sim::CarSpec> s = car_spec(c);
    return s ? spec_power_peak(*s, engine_factor(c.cond)) : PowerPeak{};
}

float GtMode::power_ps(const GarageCar& c) const { return power_peak(c).ps; }

std::string GtMode::power_text(float ps, const char* sep) const {
    return std::to_string(display_power(ps, data_->units)) + sep + power_unit(data_->units);
}

float GtMode::ride_power_ps() const {
    const GarageCar* c = state_.ride_car();
    return c ? power_ps(*c) : 0;
}

float GtMode::power_with(int goods) const {
    const GarageCar* c = state_.ride_car();
    if (!c) return 0;
    GarageCar copy = *c;
    if (!fit(copy, goods)) return ride_power_ps();
    return power_ps(copy);
}

// --- Wheels, wash, oil ---------------------------------------------------------

Check GtMode::can_buy_wheel(const std::string& wheel) const {
    if (!state_.ride_car()) return Check::NoRideCar;
    if (car_flags(state_.ride_car()->car) & 4) return Check::NotForChangeWheel;
    if (std::find(state_.wheels.begin(), state_.wheels.end(), wheel) != state_.wheels.end())
        return Check::AlreadyHave;
    if (state_.credits < s64(data_->wheel_price())) return Check::NoMoney;
    return Check::Ok;
}

bool GtMode::buy_wheel(const std::string& wheel) {
    // The inventory's slots hold the stock-wheel placeholder too; with none free the original
    // adds nothing and charges nothing, as here.
    if (can_buy_wheel(wheel) != Check::Ok || int(state_.wheels.size()) + 1 >= kMaxWheels) return false;
    state_.credits -= data_->wheel_price();
    state_.wheels.push_back(wheel);
    return true;
}

bool GtMode::fit_wheel(int index, const std::string& wheel) {
    if (index < 0 || index >= int(state_.garage.size())) return false;
    GarageCar& c = state_.garage[size_t(index)];
    if (car_flags(c.car) & 4) return false;
    if (!wheel.empty() && std::find(state_.wheels.begin(), state_.wheels.end(), wheel) == state_.wheels.end())
        return false;
    c.wheel = wheel;
    return true;
}

bool GtMode::fit_shop_wheel(const std::string& wheel) {
    // The shop's fit action (the same code): the ride
    // car's wheel becomes the id of the focused wheel's name, the wheel the purchase was asked
    // for (set_focus_wheel_code; the purchase hands the same name to the slot search).
    // It checks neither the inventory nor the car's no-wheel-change flag (the shop does not open
    // for such a car) and charges nothing. After a purchase refused for want of a free slot the
    // car so wears a wheel the inventory does not hold: the garage's wheel list then
    // marks none of its buttons as the car's, and the car keeps the wheel until it is changed
    // (the PS2 save stores the car's wheel by id, the inventory's wheels by name).
    GarageCar* c = state_.ride_car();
    if (!c) return false;
    c->wheel = wheel;
    return true;
}

Check GtMode::can_wash() const {
    if (!state_.ride_car()) return Check::NoRideCar;
    return state_.credits < s64(data_->car_wash_price()) ? Check::NoMoney : Check::Ok;
}

bool GtMode::wash() {
    if (can_wash() != Check::Ok) return false;
    state_.credits -= data_->car_wash_price();
    state_.ride_car()->cond.dirt = 0;
    return true;
}

Check GtMode::can_change_oil() const {
    if (!state_.ride_car()) return Check::NoRideCar;
    return state_.credits < s64(data_->oil_change_price()) ? Check::NoMoney : Check::Ok;
}

bool GtMode::change_oil() {
    if (can_change_oil() != Check::Ok) return false;
    state_.credits -= data_->oil_change_price();
    state_.ride_car()->cond.oil_m = -300000;
    return true;
}

float GtMode::engine_factor(const CarCondition& c) {
    // Break-in / wear and oil (physics §10.3; verified there): 97 % new
    // (E <= -300 km), 100 % from 0 to 500 km, 98 % from 600 km; oil +5 % fading out.
    return sim::engine_condition_factor(c.engine_wear_m, c.oil_m);
}

float GtMode::oil_age(float oil_m) {
    constexpr float kLife = 300000; // the oil change's -300 km, the worn value +300 km
    return std::min(oil_m < 0 ? (oil_m + kLife) / kLife : oil_m / kLife, 1.0f);
}

// --- Engine condition through a run (gt_mode §13.1, physics §10.3) -------------------

bool GtMode::carries_engine_condition(const std::string& mode) {
    return mode == "go_race" || mode == "go_machine_test" || mode == "go_free_run" || mode == "single_race" ||
           mode == "free_run";
}

sim::EngineCondition GtMode::run_condition(const CarCondition& c) {
    const double units = std::clamp(c.odometer_m / 2, 0.0, double(sim::kOdometerMax));
    return {u32(units), c.engine_wear_m, c.oil_m};
}

sim::EngineCondition GtMode::driven(sim::EngineCondition c, float metres) {
    if (metres <= 0) return c;
    sim::odometer_step(c.odometer, metres);
    sim::engine_condition_step(c.wear_m, c.oil_m, metres);
    return c;
}

void GtMode::end_run(int index, const sim::EngineCondition& end, u32 odometer_added) {
    state_.distance_m += 2.0 * double(odometer_added);
    if (index < 0 || index >= int(state_.garage.size())) return;
    CarCondition& c = state_.garage[size_t(index)].cond;
    c.odometer_m = 2.0 * double(end.odometer);
    c.engine_wear_m = end.wear_m;
    c.oil_m = end.oil_m;
}

int GtMode::garage_index(const GarageCar& c) const {
    for (size_t i = 0; i < state_.garage.size(); ++i)
        if (&state_.garage[i] == &c) return int(i);
    return -1;
}

// --- Licenses -------------------------------------------------------------------

int GtMode::license_index_of(const std::string& code) {
    if (code.size() < 4 || code[0] != 'L') return -1;
    std::string k = code.substr(0, 3);
    if (k == "L0B") return 0;
    if (k == "L0A") return 1;
    if (k == "LIB") return 2;
    if (k == "LIA") return 3;
    if (k == "L0S") return 4;
    if (k == "L0R") return 5;
    return -1;
}

static int test_number(const std::string& code) { return code.size() >= 7 ? code[6] - '0' : 0; }

bool GtMode::license_held(int lic) const {
    if (lic < 0 || lic >= kLicenseCount) return false;
    for (int t = 0; t < kTestsPerLicense; ++t)
        if (state_.license_tests[size_t(lic * 8 + t)].medal == Medal::None) return false;
    return true;
}

bool GtMode::license_all_gold(int lic) const {
    if (lic < 0 || lic >= kLicenseCount) return false;
    for (int t = 0; t < kTestsPerLicense; ++t)
        if (state_.license_tests[size_t(lic * 8 + t)].medal != Medal::Gold) return false;
    return true;
}

int GtMode::highest_license() const {
    int h = -1;
    for (int l = 0; l <= 4; ++l)
        if (license_held(l)) h = l;
    return h;
}

Check GtMode::can_open_license_page(int lic) const {
    if (lic <= 0 || lic >= 5) return Check::Ok; // B and rally are always open
    for (int l = 0; l < lic; ++l)
        if (!license_held(l)) return Check::NoLicense;
    return Check::Ok;
}

Check GtMode::can_take_test(const std::string& code) const {
    int lic = license_index_of(code);
    if (lic < 0) return Check::Unknown;
    if (Check c = can_open_license_page(lic); c != Check::Ok) return c;
    if (test_number(code) == 8)
        for (int t = 0; t < 7; ++t)
            if (state_.license_tests[size_t(lic * 8 + t)].medal == Medal::None) return Check::NoLastExam;
    return Check::Ok;
}

Medal GtMode::judge(const std::string& code, bool finished, double seconds) const {
    std::optional<EventRules> r = rules(code);
    if (!r || !finished || seconds <= 0) return Medal::None;
    u32 ms = u32(std::llround(seconds * 1000));
    if (ms <= r->license_ms[0]) return Medal::Gold;
    if (ms <= r->license_ms[1]) return Medal::Silver;
    if (ms <= r->license_ms[2]) return Medal::Bronze;
    return Medal::None;
}

Award GtMode::record_license(const std::string& code, bool finished, double seconds) {
    return record_license_medal(code, judge(code, finished, seconds), seconds);
}

Award GtMode::record_license_medal(const std::string& code, Medal medal, double seconds) {
    Award a;
    int lic = license_index_of(code), t = test_number(code) - 1;
    if (lic < 0 || t < 0 || t >= 8) return a;
    bool held = license_held(lic), gold = license_all_gold(lic);
    a.medal = medal;
    LicenseTestRecord& rec = state_.license_tests[size_t(lic * 8 + t)];
    if (a.medal != Medal::None) {
        // The medal jingle also sets an award bit: 0x20000 gold, 0x40000 silver, 0x80000 bronze.
        state_.arcade.awards |= 0x20000u << int(a.medal);
        if (rec.medal == Medal::None || int(a.medal) < int(rec.medal)) rec.medal = a.medal;
        if (rec.best_time < 0 || seconds < rec.best_time) rec.best_time = seconds;
    }
    a.license_obtained = !held && license_held(lic);
    // The license card page (gt_mode §3.2) marks the card received: bit 1 << license of +0xC0.
    if (a.license_obtained) state_.trophy_flags |= 1u << lic;
    a.all_gold = !gold && license_all_gold(lic);
    if (a.license_obtained) a.messages.push_back(std::string("License ") + license_name(License(lic)) + " obtained");
    if (license_all_gold(lic) && a.medal == Medal::Gold) {
        std::string set = code.substr(0, 3) + "0000";
        a.new_cars = award_prizes(set);
        a.prize_check = true;
    }
    return a;
}

// --- Races --------------------------------------------------------------------------

std::optional<EventRules> GtMode::rules(const std::string& code) const {
    const data::Event* e = db().event(code);
    if (!e || e->bytes.size() < 0x1F8) return std::nullopt;
    ByteSpan r = e->bytes;
    EventRules x;
    x.code = code;
    x.course = e->course;
    x.required_license = r[kEvLicense];
    x.cars = r[kEvCars];
    x.laps = r[kEvLaps];
    // Program constant: the prizes and the bonus are stored in hundreds of credits; the readers
    // multiply by 100 (immediate 0x64: race prizes, the championship bonus),
    // then x100 again in
    // Japanese.
    constexpr u32 kHundreds = 100;
    for (int p = 0; p < 6; ++p) x.prize[size_t(p)] = data_->money(u32(rd16(r, kEvPrize + 2 * size_t(p))) * kHundreds);
    x.champ_bonus = data_->money(u32(rd16(r, kEvBonus)) * kHundreds);
    x.league = r[kEvLeague];
    x.car_class = r[kEvClass];
    x.aspiration = r[kEvAspiration];
    x.drivetrain = r[kEvDrive];
    x.dirt_tires = r[kEvSpecial] == 9;
    x.trophy_class = r[kEvSpecial + 1];
    x.regulation = e->regulation;
    if (x.regulation == "-") x.regulation.clear();
    (void)kEvRegulation;
    for (int m = 0; m < 3; ++m) x.license_ms[size_t(m)] = license_time(r, kEvGold + 4 * size_t(m));
    x.license_kind = r[kEvLicKind];
    x.entries = e->entries;
    return x;
}

bool GtMode::tuned(const GarageCar& c) const { return tuned(db(), c.car, c.parts, c.settings); }

bool GtMode::tuned(const data::CarDatabase& db, const std::string& label, const std::array<u64, 27>& parts,
                   const sim::CarSettings& settings) {
    const data::Car* car = db.car(label);
    if (!car) return false;
    for (size_t i = 0; i + 1 < parts.size(); ++i)
        if (parts[i] != car->parts[i]) return true;
    return !same_settings(settings, sim::stock_settings(db, label, car->parts));
}

EntryResult GtMode::check_entry(const std::string& code, const GarageCar& c) const {
    EntryResult res;
    std::optional<EventRules> r = rules(code);
    if (!r) return res;
    if (r->dirt_tires && !c.owned.test(66)) res.dirt_tires = true;
    if (r->required_license > 0 && !license_held(r->required_license - 1)) res.license = true;
    if (r->drivetrain > 0) {
        static const int kWant[6] = {-1, 1, 0, 2, 3, 4}; // FF, FR, 4WD, MR, RR -> database drive types
        auto dt = db().drivetrain(c.parts[size_t(part_slot(13))]);
        if (r->drivetrain > 5 || !dt || dt->drive_type != kWant[r->drivetrain]) res.drivetrain = true;
    }
    if (!r->regulation.empty()) {
        bool found = false;
        for (const data::Regulation& g : db().regulations())
            if (g.label == r->regulation) found = std::find(g.cars.begin(), g.cars.end(), c.car) != g.cars.end();
        res.regulation = !found;
    }
    if (r->aspiration > 0) {
        const ByteSpan* t = db().record(data::TableId::TurbineKit, c.parts[size_t(part_slot(12))]);
        bool turbo = t && t->size() > 0x11 && (*t)[0x11] != 0;
        res.aspiration = r->aspiration == 1 ? turbo : !turbo;
    }
    if (r->car_class == 1) res.car_class = car_category(c.car) == 0;
    else if (r->car_class == 2) res.car_class = !tuned(c);
    else if (r->car_class == 3) res.car_class = car_category(c.car) != 0 || tuned(c);
    return res;
}

Check GtMode::can_enter(const std::string& code) const {
    const GarageCar* c = state_.ride_car();
    if (!c) return Check::NoRideCar;
    EntryResult r = check_entry(code, *c);
    if (r.ok()) return Check::Ok;
    if (r.license) return Check::LicenseViolation;
    return Check::RegulationViolation;
}

bool GtMode::series_unlocked(const std::string& code) const {
    auto it = data_->unlock_level.find(code);
    int level = it == data_->unlock_level.end() ? 0 : it->second;
    return (license_held(4) ? 1 : 0) >= level;
}

std::vector<std::string> GtMode::pick_opponents(const std::string& code, GameRandom& rng) {
    std::optional<EventRules> r = rules(code);
    const data::Event* ev = db().event(code);
    if (!r || !ev) return {};
    // Slots 1 .. cars-1 (slot 0 is the player), gt_mode §20.4.
    return race::draw_opponents(db(), *ev, std::max(r->cars, 1) - 1, rng);
}

// The race prize page's bookkeeping (gt_mode §3.2, presentation §F): a GT-mode cup (prize money, places
// 1-3, trophy class 5..8) adds a trophy and sets award bit 1 << ((class - 5) x 3 + place);
// the consolation prize (places 4-6) sets bit 0x10000.
void GtMode::note_race_prize(const EventRules& r, int place, s64 money) {
    if (r.trophy_class < 5 || r.trophy_class > 8 || place < 0 || money <= 0) return;
    if (place < 3) {
        ++state_.trophies;
        state_.arcade.awards |= 1u << ((r.trophy_class - 5) * 3 + place);
    } else {
        state_.arcade.awards |= 0x10000u;
    }
}

Award GtMode::record_race(const std::string& code, int place) {
    Award a;
    a.place = place;
    std::optional<EventRules> r = rules(code);
    ++state_.races;
    if (!r || place < 0) return a;
    if (place < 6) a.money = r->prize[size_t(place)];
    note_race_prize(*r, place, a.money);
    state_.credits += a.money;
    state_.prize_money += a.money;
    if (place == 0) ++state_.wins;
    int& best = state_.race_best.try_emplace(code, kNoPosition).first->second;
    best = std::min(best, place);
    if (place == 0) a.new_cars = award_prizes(code), a.prize_check = true;
    return a;
}

bool GtMode::start_championship(const std::string& first_round) {
    const RaceSeries* s = data_->catalog.series_for(first_round);
    if (!s || !s->championship()) return false;
    Championship c;
    c.series = s->id;
    c.races = s->races;
    c.next = 0;
    for (size_t i = 0; i < c.races.size(); ++i)
        if (c.races[i] == first_round) c.next = int(i);
    state_.championship = c;
    // The field is drawn once and kept for every round.
    state_.championship.entrants = pick_opponents(first_round, race_info_random());
    state_.championship.points.assign(state_.championship.entrants.size() + 1, 0);
    return true;
}

Award GtMode::record_round(int place, const std::vector<int>& order) {
    Championship& ch = state_.championship;
    if (!ch.active()) return {};
    const std::string code = ch.races[size_t(ch.next)];
    // Each round pays its own place prize (no prize car per round).
    Award a;
    a.place = place;
    std::optional<EventRules> r = rules(code);
    ++state_.races;
    if (r && place >= 0 && place < 6) a.money = r->prize[size_t(place)];
    if (r) note_race_prize(*r, place, a.money);
    state_.credits += a.money;
    state_.prize_money += a.money;
    if (place == 0) ++state_.wins;
    if (place >= 0) {
        int& best = state_.race_best.try_emplace(code, kNoPosition).first->second;
        best = std::min(best, place);
    }
    // Points by place (kChampPoints).
    const int n = int(ch.points.size());
    std::vector<int> finish = order;
    if (finish.empty()) {
        // Only the player's place is known: the others fill the remaining places in entry order.
        for (int i = 1; i < n; ++i) finish.push_back(i);
        if (place >= 0) finish.insert(finish.begin() + std::min(place, n - 1), 0);
    }
    // A round the player left (pause EXIT, not finished) still counts: every other car takes the
    // points of its place in the order (no one moves up), the player none; a finished round
    // scores every car.
    const bool player_scores = place >= 0;
    for (size_t p = 0; p < finish.size() && p < 6; ++p)
        if (finish[p] >= 0 && finish[p] < n && (player_scores || finish[p] != 0))
            ch.points[size_t(finish[p])] += kChampPoints[p];
    ++ch.next;
    if (ch.next >= int(ch.races.size())) {
        a.championship_over = true;
        bool champion = true;
        for (int i = 1; i < n; ++i) champion = champion && ch.points[0] > ch.points[size_t(i)];
        a.champion = champion;
        if (champion) {
            ++state_.trophies; // the championship trophy (gt_mode §3.2)
            a.bonus = r ? r->champ_bonus : 0;
            a.money += a.bonus;
            state_.credits += a.bonus;
            state_.prize_money += a.bonus;
            // The championship's own code: round code with the round number 00 (…_0501 -> …_0500).
            std::string champ = code.substr(0, code.size() - 2) + "00";
            state_.race_best[champ] = 0;
            // The trophy_get presentation picks its trophy before car_get rolls the prize car.
            a.trophy = pick_trophy();
            a.new_cars = award_prizes(champ);
            a.prize_check = true;
        }
        ch = Championship{};
    }
    return a;
}

bool GtMode::league_complete(const std::string& tier) const {
    bool any = false;
    for (const RaceSeries& s : data_->catalog.series) {
        if (s.tier != tier) continue;
        any = true;
        if (s.championship()) {
            std::string champ = s.races.front().substr(0, s.races.front().size() - 2) + "00";
            if (state_.best_position(champ) != 0) return false;
        } else {
            for (const std::string& r : s.races)
                if (state_.best_position(r) != 0) return false;
        }
    }
    return any;
}

float GtMode::completion() const {
    // gt_mode §4: four (done, total) pairs. GT Mode: the gcf tag-4 result slots won
    // over their count; arcade: the Clear Status count over the courses
    // of the gcf SINGLE_RACE list over a fixed 102; time trials beaten
    // over 10; licence tests with a medal over 48.
    // All done = 1, else the ratio, at most 0.99. Program constants: the three
    // fixed totals and the cap; the same in the PAL and NTSC-J programs.
    constexpr int kArcadeTotal = 102;
    constexpr float kCap = 0.99f;
    int won = 0, tests = 0;
    for (const std::string& c : data_->race_codes) won += state_.best_position(c) == 0;
    for (const LicenseTestRecord& t : state_.license_tests) tests += t.medal != Medal::None;
    won += arcade_completion();
    for (int r : state_.arcade.at_best_rank) won += r == 0;
    const int total = int(data_->race_codes.size()) + kArcadeTotal + kTimeTrials + kLicenseCount * kTestsPerLicense;
    if (won + tests == total) return 1;
    return std::min(kCap, float(won + tests) / float(total));
}

std::string GtMode::completion_text() const {
    float r = completion();
    if (r == 1.0f) return "100.0 %";
    int pm = std::min(999, ps2::to_int(ps2::mul(r, 1000.0f)));
    char b[32];
    std::snprintf(b, sizeof b, "%d.%d %%", pm / 10, pm % 10);
    return b;
}

float GtMode::completion_percent() const { return ps2::mul(completion(), 100.0f); }

std::string GtMode::completion_file_text(float percent) {
    int t = ps2::to_int(ps2::mul(percent, 10.0f));
    char b[32];
    std::snprintf(b, sizeof b, "%d.%01d%%", t / 10, t % 10);
    return b;
}

std::vector<int> GtMode::award_prizes(const std::string& code) {
    std::vector<int> got;
    const std::string series = code.size() > 4 ? code.substr(0, code.size() - 4) : code;
    bool stirred = false;
    for (const PrizeSet& set : data_->prizes) {
        if (set.cars.empty()) continue;
        const std::string& n = set.name;
        bool multi = set.cars.size() > 1;
        bool eligible = false;
        if (n == "arcade_compulsory_complete") {
            // Every arcade time trial cleared (best rank 0).
            eligible = true;
            for (int r : state_.arcade.at_best_rank) eligible = eligible && r == 0;
        } else if (n == code) {
            // Championship (…_0500/_0700/_1000), endurance (E…_0001), license (L??0000) or a series set
            // named by the code itself: the caller has established the win.
            if (int lic = license_index_of(n); lic >= 0) eligible = license_all_gold(lic);
            else eligible = state_.best_position(code) == 0 || ends(n, "_0000");
        } else if (ends(n, "_0000") && n.size() > 4 && n.substr(0, n.size() - 4) == series &&
                   license_index_of(n) < 0) {
            // Every race of the series won.
            eligible = true;
            for (const std::string& rc : data_->race_codes)
                if (rc.size() == n.size() && rc.compare(0, series.size(), series) == 0 && !ends(rc, "_0000"))
                    eligible = eligible && state_.best_position(rc) == 0;
        }
        // A league's key only with a code of that league (by the code's class; the G
        // codes' league by "_e_" / "_n_" / "_h_"): the league is complete.
        else if (n == "gtmode_beginner_complete") eligible = code_league(code) == "beginner" && league_complete("beginner");
        else if (n == "gtmode_amateur_complete") eligible = code_league(code) == "amateur" && league_complete("amateur");
        else if (n == "gtmode_pro_complete") eligible = code_league(code) == "pro" && league_complete("pro");
        else if (n == "gtmode_dirt_complete") eligible = code_league(code) == "dirt" && league_complete("dirt");
        else if (n == "gtmode_enduro_complete") eligible = code_league(code) == "end" && league_complete("end");
        // The whole game: checked with a GT-mode code (not arcade), every GT-mode result won.
        else if (n == "gtmode_complete") eligible = gt_mode_code(code) && all_races_won();
        // Completion at or over a program constant: 0.5, 0.75, 1.0
        else if (n == "achieve_half") eligible = completion() >= 0.5f;
        else if (n == "achieve_3_quoter") eligible = completion() >= 0.75f;
        else if (n == "achieve_complete") eligible = completion() >= 1.0f;
        if (!eligible) continue;
        if (!multi && state_.prizes_given.count(n)) continue;
        if (multi && n != code) continue;
        // The car_get control opens with a stir (gt_mode §20.3), even when the garage is full.
        if (!stirred) stir_random(), stirred = true;
        if (int(state_.garage.size()) >= kMaxGarageCars) { // garage full: nothing given
            ++prizes_refused_;
            continue;
        }
        // car_get (gt_mode §20.3): per set every candidate, its label in the game's language
        // (docs/dev/GT_MODE.md "Language"), gets its paint, then the weighted pick.
        std::vector<PrizeCar> cands;
        for (const PrizeCar& c : set.cars) cands.push_back(data_->prize_candidate(c));
        std::vector<int> paint(cands.size());
        for (size_t i = 0; i < cands.size(); ++i) paint[i] = prize_paint(cands[i]);
        const size_t pick = size_t(pick_prize(set));
        int idx = add_prize_car_paint(cands[pick].car, paint[pick], n);
        if (idx < 0) continue;
        state_.prizes_given.insert(n);
        // Earlier indices shift by one with each insertion at the head.
        for (int& g : got) ++g;
        got.push_back(idx);
    }
    return got;
}

bool GtMode::gt_mode_code(const std::string& code) {
    if (code.empty()) return false;
    switch (code[0]) {
    case 'G': // series races and championships
    case 'E': // endurance
    case 'D': // rally
        return true;
    case 'L': // licences B, A, IB, IA, S, R: the third character
        return code.size() > 2 && (code[2] == 'A' || code[2] == 'B' || code[2] == 'R' || code[2] == 'S');
    case 'M': // machine tests M400, M1000, MMAX: the second character
        return code.size() > 1 && (code[1] == '4' || code[1] == '1' || code[1] == 'M');
    default:
        return false;
    }
}

std::string GtMode::code_league(const std::string& code) {
    if (code.empty()) return {};
    switch (code[0]) {
    case 'G':
        if (code.find("_e_") != std::string::npos) return "beginner";
        if (code.find("_n_") != std::string::npos) return "amateur";
        if (code.find("_h_") != std::string::npos) return "pro";
        return {};
    case 'E': return "end";
    case 'D': return "dirt";
    default: return {};
    }
}

bool GtMode::all_races_won() const {
    if (data_->race_codes.empty()) return false;
    for (const std::string& c : data_->race_codes)
        if (state_.best_position(c) != 0) return false;
    return true;
}

int GtMode::prize_paint(const PrizeCar& pc) {
    // A fixed colour id in the model's list, else a random paint (gt_mode §20.3).
    if (pc.color_id >= 0) {
        int i = data_->color_index(pc.car, pc.color_id);
        if (data_->color_id(pc.car, i) == u32(pc.color_id)) return i;
    }
    return prize_paint_index(data_->color_count(pc.car), state_.rng);
}

int GtMode::pick_prize(const PrizeSet& set) {
    std::vector<int> w;
    for (const PrizeCar& c : set.cars) w.push_back(c.weight);
    return pick_weighted(w, state_.rng);
}

int GtMode::pick_trophy() {
    // trophy_get (gt_mode §18, gt_mode §20.3): a stir, then the weighted pick.
    stir_random();
    int pick = pick_trophy_index(completion(), state_.rng);
    state_.trophy_flags |= 0x40u << pick;
    return pick;
}

// --- Arcade progression ---------------------------------------------------------

int GtMode::arcade_difficulty(const std::string& code) {
    if (code.size() < 2 || code[0] != 'A') return -1;
    switch (code[1]) {
    case 'E': return 0;
    case 'N': return 1;
    case 'H': return 2;
    case 'P': return 3;
    default: return -1;
    }
}

int GtMode::arcade_course_count() const {
    auto it = data_->arcade_courses.find("SINGLE_RACE");
    if (it == data_->arcade_courses.end()) return 0;
    return std::min(int(it->second.size()), kArcadeResultSlots / kArcadeDifficulties);
}

int GtMode::arcade_course_index(const std::string& course) const {
    auto it = data_->arcade_courses.find("SINGLE_RACE");
    if (it == data_->arcade_courses.end()) return -1;
    for (int i = 0; i < arcade_course_count(); ++i)
        if (it->second[size_t(i)].course == course) return i;
    return -1;
}

int GtMode::arcade_course_level(const std::string& course) const {
    for (const auto& [list, courses] : data_->arcade_courses)
        for (const GtData::ArcadeCourse& c : courses)
            if (c.course == course) return c.level;
    return -1;
}

bool GtMode::arcade_course_open(const std::string& course) const {
    int l = arcade_course_level(course);
    return l >= 0 && l <= state_.arcade.course_level;
}

bool GtMode::arcade_course_dirt(const std::string& course) const {
    for (const data::Course& c : db().courses())
        if (c.label == course) return c.surface == 2;
    return false;
}

bool GtMode::arcade_car_level_open(int level) const {
    if (level <= 0) return true;
    return level <= kArcadeCarLevels && state_.arcade.car_flags[size_t(level - 1)];
}

// The arcade car filter's region test (gt_mode §16): the base car is the arcade-car (35)
// or car-variant (31) record's +8, else the label itself; a car missing from the car table
// (30) is hidden, otherwise its region byte +0xF7 against the game's language decides
// (car_region_offered).
bool GtMode::arcade_car_region_hidden(const std::string& label) const {
    u64 base = db::id_hash(label);
    if (const ByteSpan* r = db().record(data::TableId::ArcadeCar, label); r && r->size() >= 16) base = rd64(*r, 8);
    else if (const ByteSpan* v = db().record(data::TableId::CarVariant, label); v && v->size() >= 16) base = rd64(*v, 8);
    const ByteSpan* car = db().record(data::TableId::Car, base);
    if (!car || car->size() <= 0xF7) return true;
    return !data::car_region_offered((*car)[0xF7], data_->language);
}

std::vector<const GtData::ArcadeCar*> GtMode::arcade_cars(const std::string& car_class) const {
    std::string tag = car_class;
    for (char& ch : tag) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    std::vector<const GtData::ArcadeCar*> out;
    auto it = data_->arcade_classes.find(tag);
    if (it == data_->arcade_classes.end()) return out;
    for (const GtData::ArcadeCar& c : it->second) {
        if (!arcade_car_level_open(c.level)) continue;
        if (arcade_car_region_hidden(c.label)) continue;
        out.push_back(&c);
    }
    return out;
}

std::vector<std::string> GtMode::arcade_courses(const std::string& list) const {
    std::vector<std::string> out;
    auto it = data_->arcade_courses.find(list);
    if (it == data_->arcade_courses.end()) return out;
    for (const GtData::ArcadeCourse& c : it->second)
        if (c.level <= state_.arcade.course_level) out.push_back(c.course);
    return out;
}

int GtMode::arcade_best(int d, int course) const {
    const int n = arcade_course_count();
    if (d < 0 || d >= kArcadeDifficulties || course < 0 || course >= n) return kArcadeNoPosition;
    return state_.arcade.best[size_t(d * n + course)];
}

bool GtMode::arcade_won_at_least(int course, int d) const {
    for (int k = std::max(d, 0); k <= 2; ++k)
        if (arcade_best(k, course) == 0) return true;
    return false;
}

bool GtMode::arcade_group_won(int g, int d) const {
    auto it = data_->arcade_courses.find("SINGLE_RACE");
    if (it == data_->arcade_courses.end()) return false;
    bool any = false;
    for (int i = 0; i < arcade_course_count(); ++i) {
        if (it->second[size_t(i)].level != g) continue;
        any = true;
        if (!arcade_won_at_least(i, d)) return false;
    }
    return any;
}

bool GtMode::arcade_all_won_at(int d) const {
    if (d < 0 || d >= kArcadeDifficulties || arcade_course_count() == 0) return false;
    for (int i = 0; i < arcade_course_count(); ++i)
        if (arcade_best(d, i) != 0) return false;
    return true;
}

int GtMode::arcade_completion() const {
    // Per course of the SINGLE_RACE list, won at Easy, Normal or Hard; at Normal or
    // Hard; at Hard.
    int n = 0;
    for (int i = 0; i < arcade_course_count(); ++i)
        for (int d = 0; d <= 2; ++d) n += arcade_won_at_least(i, d) ? 1 : 0;
    return n;
}

bool GtMode::arcade_ending_open() const {
    if (league_complete("beginner") || league_complete("amateur") || league_complete("pro")) return true;
    for (int d = 0; d < kArcadeDifficulties; ++d)
        if (arcade_all_won_at(d)) return true;
    return false;
}

bool GtMode::arcade_long_ending_open() const {
    return league_complete("amateur") || league_complete("pro") || arcade_all_won_at(1) || arcade_all_won_at(2) ||
           arcade_all_won_at(3);
}

void GtMode::arcade_unlock_pass(ArcadeOutcome& out) {
    ArcadeProgress& a = state_.arcade;
    auto it = data_->arcade_courses.find("SINGLE_RACE");
    if (it == data_->arcade_courses.end()) return;
    int max_level = 0;
    for (const GtData::ArcadeCourse& c : it->second) max_level = std::max(max_level, c.level);
    // Course groups: the next group opens when every course of the current
    // one has been won at Easy, Normal or Hard (several may open at once).
    const int courses = arcade_course_count();
    auto group_won_any = [&](int g) {
        bool any = false;
        for (int i = 0; i < courses; ++i) {
            if (it->second[size_t(i)].level != g) continue;
            any = true;
            if (!arcade_won_at_least(i, 0)) return false;
        }
        return any;
    };
    while (a.course_level < max_level + 1 && group_won_any(a.course_level)) {
        ++a.course_level;
        ++out.courses_opened;
    }
    // Car flags: group g all won at exactly Normal -> flag g+1; all at exactly
    // Hard -> flags g+1 and g+7. Flags are never cleared.
    auto group_all_at = [&](int g, int d) {
        bool any = false;
        for (int i = 0; i < courses; ++i) {
            if (it->second[size_t(i)].level != g) continue;
            any = true;
            if (arcade_best(d, i) != 0) return false;
        }
        return any;
    };
    auto set_flag = [&](int level) {
        bool& f = a.car_flags[size_t(level - 1)];
        if (!f) out.car_levels.push_back(level);
        f = true;
    };
    for (int g = 0; g < 6; ++g)
        if (group_all_at(g, 1)) set_flag(g + 1);
    for (int g = 0; g < 6; ++g)
        if (group_all_at(g, 2)) {
            set_flag(g + 7);
            set_flag(g + 1);
        }
}

GtMode::ArcadeOutcome GtMode::arcade_single_race(const std::string& code, const std::string& course, int place) {
    ArcadeOutcome out;
    int d = arcade_difficulty(code), i = arcade_course_index(course);
    if (d < 0 || i < 0 || place < 0) return out;
    out.recorded = true;
    ArcadeProgress& a = state_.arcade;
    if (place == 0) {
        ++state_.trophies;
        a.awards |= 0x1000u << d;
    }
    int& best = a.best[size_t(d * arcade_course_count() + i)]; // difficulty x list size + course
    if (place < best) {
        best = place;
        out.improved = true;
        arcade_unlock_pass(out);
    }
    return out;
}

int GtMode::time_trial_index(const std::string& s) {
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (s.size() == 8 && s[0] == 'A' && s[1] == 'T' && digit(s[2]) && digit(s[3])) {
        int n = (s[2] - '0') * 10 + (s[3] - '0') - 1;
        return n >= 0 && n < kTimeTrials ? n : -1;
    }
    if (s.size() == 4 && s[0] == 't' && s[1] == 't' && digit(s[2]) && digit(s[3])) {
        int n = (s[2] - '0') * 10 + (s[3] - '0') - 1;
        return n >= 0 && n < kTimeTrials ? n : -1;
    }
    return -1;
}

std::vector<double> GtMode::time_trial_defaults(const std::string& code) const {
    std::vector<double> out;
    const ByteSpan* r = db().record(data::TableId::Event, code);
    if (!r || r->size() < 0x1D0) return out;
    for (size_t k = 0; k < 3; ++k) {
        size_t o = 0x1C4 + 4 * k;
        double t = (*r)[o] * 60.0 + (*r)[o + 1] + rd16(*r, o + 2) / 1000.0;
        if (t > 0) out.push_back(t);
    }
    return out;
}

std::vector<RankEntry> GtMode::time_trial_seed_ranking(const data::CarDatabase& db, const std::string& code) {
    std::vector<RankEntry> out;
    const int i = time_trial_index(code);
    const ByteSpan* r = db.record(data::TableId::Event, code);
    if (i < 0 || !r || r->size() < 0x1D0) return out;
    // The original's new game (gt_mode §16): one name byte counting down from 0xFF over the
    // ten challenges' three records each, every time inserted before the first time that
    // is not faster. (A record without a time is left out here; the disc has none.)
    for (int k = 0; k < 3; ++k) {
        size_t o = 0x1C4 + 4 * size_t(k);
        double t = (*r)[o] * 60.0 + (*r)[o + 1] + rd16(*r, o + 2) / 1000.0;
        if (t <= 0) continue;
        size_t pos = 0;
        while (pos < out.size() && out[pos].value < t) ++pos;
        out.insert(out.begin() + std::ptrdiff_t(pos), RankEntry{t, "", 0, time_trial_seed_name(i, k), 0, ""});
    }
    return out;
}

std::string GtMode::record_name(const std::string& stored) const {
    if (!is_record_mark(stored)) return stored;
    // The original reads the name's first byte signed and, when negative, shows entry
    // ~byte of the program's built-in name table (gt_mode §16): the
    // seeds' marks 0xFF - 3 i - k give entries 3 i + k (30 names, none past them).
    size_t k = size_t(u8(~u8(stored[0])));
    return k < data_->builtin_names.size() ? data_->builtin_names[k] : std::string("-");
}

int GtMode::arcade_time_trial(const std::string& code, double seconds, const std::string& car) {
    int i = time_trial_index(code);
    if (i < 0 || seconds <= 0) return -1;
    std::string key = "at:" + code;
    std::vector<RankEntry>& v = state_.rankings[key];
    if (v.empty()) v = time_trial_seed_ranking(db(), code);
    // Rank = the number of times strictly faster (the new time goes before equal ones).
    size_t pos = 0;
    while (pos < v.size() && v[pos].value < seconds) ++pos;
    if (pos >= 12) return -1;
    v.insert(v.begin() + std::ptrdiff_t(pos),
             RankEntry{seconds, car, state_.day, state_.player_name, record_stamp_now(), ""});
    if (v.size() > 12) v.resize(12);
    int& best = state_.arcade.at_best_rank[size_t(i)];
    best = std::min(best, int(pos));
    award_prizes("arcade_compulsory_complete");
    return int(pos);
}

std::vector<RankEntry> GtMode::ranking_top(const std::string& key, size_t n) const {
    std::vector<RankEntry> v;
    if (auto it = state_.rankings.find(key); it != state_.rankings.end()) v = it->second;
    else if (key.rfind("at:", 0) == 0) v = time_trial_seed_ranking(db(), key.substr(3));
    if (v.size() > n) v.resize(n);
    return v;
}

int GtMode::add_record(const std::string& key, double value, const std::string& car, bool ascending, size_t max) {
    if (value <= 0) return -1;
    std::vector<RankEntry>& v = state_.rankings[key];
    // The first place whose entry is not better: a new entry goes before equal values.
    size_t pos = 0;
    while (pos < v.size() && (ascending ? v[pos].value < value : v[pos].value > value)) ++pos;
    if (pos >= max) return -1;
    v.insert(v.begin() + std::ptrdiff_t(pos),
             RankEntry{value, car, state_.day, state_.player_name, record_stamp_now(), ""});
    if (v.size() > max) v.resize(max);
    return int(pos);
}

int GtMode::record_machine_test(const std::string& code, double value, const std::string& car) {
    if (value <= 0) return -1;
    MachineRecords& m = state_.machine;
    if (code == "MMAX") {
        if (value > m.top_speed) m.top_speed = value, m.car_top = car;
        return add_record("mt:MMAX", value, car, false, 12);
    }
    if (code == "M400" && (m.t400 < 0 || value < m.t400)) m.t400 = value, m.car400 = car;
    if (code == "M1000" && (m.t1000 < 0 || value < m.t1000)) m.t1000 = value, m.car1000 = car;
    return add_record("mt:" + code, value, car, true, 12);
}

int GtMode::course_time_trial_index(const std::string& course) const {
    auto it = data_->arcade_courses.find("TIME_ATTACK");
    if (it == data_->arcade_courses.end()) return -1;
    for (size_t i = 0; i < it->second.size(); ++i)
        if (it->second[i].course == course) return i < size_t(kCourseTimeTrials) ? int(i) : -1;
    return -1;
}

int GtMode::record_course_time_trial(const std::string& course, double best_lap, const std::string& car) {
    // The best lap in whole ms, entered when the run has one (not the "no time" value, at most
    // 0x3FFFFF ms).
    const double ms = std::round(best_lap * 1000.0);
    if (course_time_trial_index(course) < 0 || !(ms >= 1) || ms > 0x3FFFFF) return -1;
    return add_record("ct:" + course, ms / 1000.0, car, true, 12);
}

// --- Car spec bridge -----------------------------------------------------------

std::optional<sim::CarSpec> GtMode::car_spec(const GarageCar& c, std::string* error) const {
    return sim::build_car_spec(db(), c.car, c.parts, c.settings, error);
}

} // namespace gt3r::game
