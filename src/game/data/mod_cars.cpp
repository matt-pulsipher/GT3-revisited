#include "game/data/mod_cars.h"
#include "game/data/weight_class.h"

#include "base/config.h"
#include "base/log.h"
#include "formats/db/gt_db.h"
#include "formats/text/charset.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace gt3r::data {

namespace {

void put8(Bytes& b, size_t o, u8 v) {
    if (o < b.size()) b[o] = v;
}
void put16(Bytes& b, size_t o, u16 v) {
    if (o + 2 <= b.size()) std::memcpy(b.data() + o, &v, 2);
}
void put32(Bytes& b, size_t o, u32 v) {
    if (o + 4 <= b.size()) std::memcpy(b.data() + o, &v, 4);
}
void put64(Bytes& b, size_t o, u64 v) {
    if (o + 8 <= b.size()) std::memcpy(b.data() + o, &v, 8);
}
void putf(Bytes& b, size_t o, float v) {
    if (o + 4 <= b.size()) std::memcpy(b.data() + o, &v, 4);
}
float getf(const Bytes& b, size_t o) {
    float v = 0;
    if (o + 4 <= b.size()) std::memcpy(&v, b.data() + o, 4);
    return v;
}
u16 get16(const Bytes& b, size_t o) { return o + 2 <= b.size() ? u16(b[o] | b[o + 1] << 8) : 0; }

u8 clamp8(double v) { return u8(std::clamp(std::lround(v), 0L, 255L)); }
u16 clamp16(double v) { return u16(std::clamp(std::lround(v), 0L, 65535L)); }
s16 clamps16(double v) { return s16(std::clamp(std::lround(v), -32768L, 32767L)); }

// Value text without a trailing " ; comment" / " # comment".
std::string value_text(std::string_view v) {
    for (size_t i = 1; i < v.size(); ++i)
        if ((v[i] == ';' || v[i] == '#') && (v[i - 1] == ' ' || v[i - 1] == '\t')) {
            v = v.substr(0, i);
            break;
        }
    return std::string(trim(v));
}

std::optional<double> number(std::string_view s) {
    std::string t(trim(s));
    if (t.empty()) return std::nullopt;
    char* end = nullptr;
    double v = (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) ? double(std::strtoull(t.c_str(), &end, 16))
                                                                              : std::strtod(t.c_str(), &end);
    if (!end || *end) return std::nullopt;
    // Definitions can come from other players (online downloads): no value needs more than
    // this, and every later integer conversion stays defined.
    if (!std::isfinite(v) || std::fabs(v) > 1e9) return std::nullopt;
    return v;
}

std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == ',' || c == '\t') {
            if (!cur.empty()) out.push_back(std::move(cur)), cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

// A path in a definition file: relative to the mod folder (the parent of cars/ or
// makers/), else to the file's own folder; absolute paths as they are.
std::string mod_path(const std::string& def_file, const std::string& p) {
    if (p.empty() || p[0] == '/') return p;
    std::string dir = def_file.substr(0, def_file.find_last_of('/') + 1);
    for (const char* sub : {"/cars/", "/makers/"}) {
        size_t n = std::strlen(sub);
        if (dir.size() >= n && dir.compare(dir.size() - n, n, sub) == 0) {
            dir.resize(dir.size() - n + 1);
            break;
        }
    }
    return dir + p;
}

std::string to_upper_ascii(std::string s) {
    for (char& ch : s) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

bool valid_label(const std::string& l) {
    if (l.empty() || l.size() > 40) return false;
    for (char c : l)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

std::optional<TableId> table_by_name(std::string_view n) {
    std::string s = to_lower(n);
    if (s == "body") return TableId::RacingModify;
    if (s == "gearbox") return TableId::Gear;
    if (s == "tire_front") return TableId::FrontTire;
    if (s == "tire_rear") return TableId::RearTire;
    for (int i = 0; i < int(TableId::Count); ++i)
        if (s == table_name(TableId(i))) return TableId(i);
    return std::nullopt;
}

// Engine-only upgrade tables: an electric car gets none of these.
bool engine_part_table(int t) {
    return t == 7 || t == 8 || t == 9 || t == 10 || t == 11 || t == 12 || t == 14 || t == 15 || t == 16 || t == 19 || t == 20;
}

// An arcade record's tuning settings (table 35, +0xE8..) set to the car's stock settings: each one from the car's own part
// record and clamped to that record's range, as the race builds a car's stock equipment (stock_equipment in
// sim/exact/spec_build.cpp), at the offsets arcade_equipment reads them from. The disc's arcade cars mostly hold exactly
// these; a copy of the donor's would replace the mod car's final drive, output factor and suspension with the donor's.
// `part(slot)` is the car's part record for a kCarPartTables slot (empty when it has none). The output word (+0x10A) is 0:
// the engine record's own factor, as in a garage car's equipment.
template <class Part>
void stock_arcade_settings(Bytes& a, Part&& part) {
    auto clamp = [](int v, int lo, int hi) { return std::min(std::max(v, lo), hi); }; // max with lo, then min with hi
    auto b = [](ByteSpan r, size_t o) -> int { return o < r.size() ? r[o] : 0; };
    auto h = [&](ByteSpan r, size_t o) { return b(r, o) | b(r, o + 1) << 8; };
    auto set = [&](size_t o, int v) { put8(a, o, u8(v)); };
    auto set16 = [&](size_t o, int v) { put16(a, o, u16(v)); };
    const ByteSpan ls = part(17), ge = part(18), su = part(19), rm = part(5), as = part(24), tc = part(25);
    // LSD: the front's initial, accel and decel, then the rear's.
    const size_t lsd[6][3] = {{0x12, 0x13, 0x14}, {0x15, 0x16, 0x17}, {0x18, 0x19, 0x1A},
                              {0x1C, 0x1D, 0x1E}, {0x1F, 0x20, 0x21}, {0x22, 0x23, 0x24}};
    for (size_t i = 0; i < 6; ++i) set(0xE8 + i, ls.empty() ? 0 : clamp(b(ls, lsd[i][0]), b(ls, lsd[i][1]), b(ls, lsd[i][2])));
    set(0xEE, ge.empty() ? 0 : clamp(b(ge, 0x29), b(ge, 0x2A), b(ge, 0x2B)));
    set16(0x10C, ge.empty() ? 0 : clamp(h(ge, 0x22), h(ge, 0x26), h(ge, 0x24)));
    set(0xF8, rm.empty() ? 0 : clamp(b(rm, 0x13), b(rm, 0x11), b(rm, 0x12)));
    set(0xF9, rm.empty() ? 0 : clamp(b(rm, 0x3C), b(rm, 0x3A), b(rm, 0x3B)));
    if (!su.empty()) {
        const int f1 = b(su, 0x2B), f2 = b(su, 0x32), r1 = b(su, 0x39), r2 = b(su, 0x40);
        const std::pair<size_t, int> damper[8] = {{0x2E, f1}, {0x31, f1}, {0x35, f2}, {0x38, f2},
                                                  {0x3C, r1}, {0x3F, r1}, {0x43, r2}, {0x46, r2}};
        for (size_t i = 0; i < 8; ++i) set(0xF0 + i, clamp(b(su, damper[i].first), 1, damper[i].second));
        set(0xFA, clamp(b(su, 0x13), b(su, 0x11), b(su, 0x12))), set(0xFB, clamp(b(su, 0x16), b(su, 0x14), b(su, 0x15)));
        set(0xFC, b(su, 0x19)), set(0xFD, b(su, 0x1C));
        set(0xFE, clamp(b(su, 0x4A), 1, b(su, 0x47))), set(0xFF, clamp(b(su, 0x4E), 1, b(su, 0x4B)));
        set16(0x100, clamp(h(su, 0x54), h(su, 0x50), h(su, 0x52))), set16(0x102, clamp(h(su, 0x5A), h(su, 0x56), h(su, 0x58)));
        set16(0x104, clamp(b(su, 0x23), b(su, 0x21), b(su, 0x22))), set16(0x106, clamp(b(su, 0x26), b(su, 0x24), b(su, 0x25)));
    } else {
        for (size_t o = 0xF0; o < 0x100; ++o)
            if (o != 0xF8 && o != 0xF9) set(o, 0);
        for (size_t o : {0x100, 0x102, 0x104, 0x106}) set16(o, 0);
    }
    set(0x108, tc.empty() ? 0 : clamp(b(tc, 0x11), 1, b(tc, 0x12)));
    set(0x109, as.empty() ? 0 : clamp(b(as, 0x12), 1, b(as, 0x11)));
    set16(0x10A, 0);
}

} // namespace

std::optional<ModCarDef::TireSize> parse_tire_size(std::string_view s) {
    // "255/35R21", "255/35 R21", "255/35ZR21"
    std::string t;
    for (char c : s)
        if (c != ' ') t.push_back(char(std::toupper(static_cast<unsigned char>(c))));
    size_t slash = t.find('/'), r = t.find('R', slash == std::string::npos ? 0 : slash);
    if (slash == std::string::npos || r == std::string::npos) return std::nullopt;
    std::string asp = t.substr(slash + 1, r - slash - 1);
    if (!asp.empty() && asp.back() == 'Z') asp.pop_back();
    auto w = number(t.substr(0, slash)), a = number(asp), d = number(t.substr(r + 1));
    if (!w || !a || !d) return std::nullopt;
    int wi = int(*w), ai = int(*a), di = int(*d);
    if (wi % 5 || ai % 5 || wi < 100 || wi > 1275 || ai < 5 || ai > 100 || di < 10 || di > 30) return std::nullopt;
    return ModCarDef::TireSize{u8(wi / 5), u8(ai / 5), u8(di)};
}

std::vector<std::pair<float, float>> electric_curve(float torque_nm, float power_kw, float max_rpm, float base_rpm) {
    std::vector<std::pair<float, float>> c;
    if (torque_nm <= 0 || power_kw <= 0 || max_rpm <= 0) return c;
    const float k = 60.0f / (2 * 3.14159265f);
    // Base speed: where constant torque meets constant power.
    float base = base_rpm > 0 ? base_rpm : power_kw * 1000.0f / torque_nm * k;
    base = std::clamp(base, 1000.0f, max_rpm * 0.9f);
    auto torque_at = [&](float rpm) {
        if (rpm <= base) return torque_nm;
        float p = power_kw * 1000.0f;
        float fade = rpm > 0.8f * max_rpm ? 1.0f - 0.15f * (rpm - 0.8f * max_rpm) / (0.2f * max_rpm) : 1.0f;
        return std::min(torque_nm, p * fade / (rpm / k));
    };
    // 16 points: 0, base/2, base, then 13 above base up to max_rpm. The axis is
    // stored in 100 rpm steps, so points are rounded and deduplicated.
    std::vector<float> rpms = {0, base * 0.5f, base};
    for (int i = 1; i <= 13; ++i) rpms.push_back(base + (max_rpm - base) * float(i) / 13.0f);
    float last = -1;
    for (float r : rpms) {
        float q = std::round(r / 100.0f) * 100.0f;
        if (q <= last) continue;
        last = q;
        c.push_back({q, torque_at(q)});
    }
    return c;
}

std::optional<ModCarDef> parse_mod_car(std::string_view text, std::string* error, std::vector<std::string>* warnings) {
    auto fail = [&](const std::string& m) -> std::optional<ModCarDef> {
        if (error) *error = m;
        return std::nullopt;
    };
    auto warn = [&](const std::string& m) {
        if (warnings) warnings->push_back(m);
    };
    IniDocument ini = IniDocument::parse(text);
    ModCarDef d;
    for (const IniDocument::Section& sec : ini.sections()) {
        std::string sn = to_lower(sec.name);
        std::optional<TableId> raw;
        if (sn.rfind("raw.", 0) == 0) {
            raw = table_by_name(sn.substr(4));
            if (!raw) return fail("[" + sec.name + "]: unknown table");
        }
        for (const auto& [key_raw, val_raw] : sec.entries) {
            std::string key = to_lower(key_raw), v = value_text(val_raw);
            auto num = [&]() -> std::optional<double> {
                auto n = number(v);
                if (!n) warn("[" + sec.name + "] " + key + ": not a number: " + v);
                return n;
            };
            auto set_u16 = [&](std::optional<u16>& o) {
                if (auto n = num()) o = clamp16(*n);
            };
            auto set_u8 = [&](std::optional<u8>& o) {
                if (auto n = num()) o = clamp8(*n);
            };
            auto set_f = [&](std::optional<float>& o) {
                if (auto n = num()) o = float(*n);
            };
            bool known = true;
            if (raw) {
                // 0x44 = 10   0x30:u16 = 1000   0x44:f32 = 2.1
                std::string off = key, type = "u8";
                if (size_t c = key.find(':'); c != std::string::npos) off = key.substr(0, c), type = key.substr(c + 1);
                auto o = number(off);
                auto n = number(v);
                char t = type == "u8" ? 'b' : type == "u16" ? 'h' : type == "u32" ? 'i' : type == "f32" ? 'f' : 0;
                if (!o || !n || !t) return fail("[" + sec.name + "] bad poke '" + key_raw + " = " + v + "'");
                d.pokes.push_back({*raw, size_t(*o), t, *n});
                continue;
            }
            if (sn == "car") {
                if (key == "label") d.label = v;
                else if (key == "name") d.name = v;
                else if (key == "maker") d.maker = v;
                else if (key == "donor") d.donor = v;
                else if (key == "model") d.model = v;
                else if (key == "year") set_u16(d.year);
                else if (key == "price") {
                    if (auto n = num()) d.price = u32(std::max(0.0, *n));
                } else if (key == "category") {
                    std::string c = to_lower(v);
                    d.category = u8(c == "race" ? 1 : c == "rally" ? 2 : 0);
                } else if (key == "fit_body") d.fit_body = to_lower(v) != "no" && v != "0" && to_lower(v) != "off";
                else known = false;
            } else if (sn == "body") {
                if (key == "weight_kg") set_u16(d.weight_kg);
                else if (key == "front_weight_pct") set_u8(d.front_weight_pct);
                else if (key == "length_mm") set_u16(d.length_mm);
                else if (key == "width_mm") set_u16(d.width_mm);
                else if (key == "height_mm") set_u16(d.height_mm);
                else if (key == "wheelbase_mm") set_u16(d.wheelbase_mm);
                else if (key == "front_track_mm") set_u16(d.front_track_mm);
                else if (key == "rear_track_mm") set_u16(d.rear_track_mm);
                else if (key == "cd") set_f(d.cd);
                else if (key == "frontal_area_m2") set_f(d.frontal_area_m2);
                else if (key == "ride_height_mm") set_u16(d.ride_height_mm);
                else if (key == "scale_suspension") d.scale_suspension = to_lower(v) != "no" && v != "0";
                else if (key == "scale_brakes") d.scale_brakes = to_lower(v) != "no" && v != "0";
                else known = false;
            } else if (sn == "powertrain") {
                if (key == "type") {
                    std::string t = to_lower(v);
                    if (t != "electric" && t != "engine" && t != "combustion") return fail("[powertrain] type must be electric or engine");
                    d.electric = t == "electric";
                } else if (key == "power_kw") set_f(d.power_kw);
                else if (key == "power_hp") {
                    if (auto n = num()) d.power_kw = float(*n * 0.7457);
                } else if (key == "rated_kw") set_f(d.rated_kw);
                else if (key == "torque_nm") set_f(d.torque_nm);
                else if (key == "torque_lbft") {
                    if (auto n = num()) d.torque_nm = float(*n * 1.35582);
                } else if (key == "max_rpm") set_f(d.max_rpm);
                else if (key == "base_rpm") set_f(d.base_rpm);
                else if (key == "idle_rpm") set_f(d.idle_rpm);
                else if (key == "regen_kw") set_f(d.regen_kw);
                else if (key == "regen_torque_nm") set_f(d.regen_torque_nm);
                else if (key == "regen_g") set_f(d.regen_g);
                else if (key == "motor_inertia") set_f(d.motor_inertia);
                else if (key == "output_pct") set_u8(d.output_pct);
                else if (key == "sound_id") set_u16(d.sound_id);
                else if (key == "layout") d.layout_text = v;
                else if (key == "aspiration") d.aspiration_text = v;
                else if (key == "displacement") d.displacement_text = v;
                else if (key == "curve") {
                    for (const std::string& w : words(v)) {
                        size_t c = w.find(':');
                        auto r = c == std::string::npos ? std::nullopt : number(w.substr(0, c));
                        auto t = c == std::string::npos ? std::nullopt : number(w.substr(c + 1));
                        if (!r || !t) return fail("[powertrain] curve: bad point '" + w + "' (want rpm:Nm)");
                        d.curve.push_back({float(*r), float(*t)});
                    }
                    if (d.curve.size() > 16) return fail("[powertrain] curve: at most 16 points");
                } else known = false;
            } else if (sn == "drivetrain") {
                if (key == "drive") {
                    std::string t = to_lower(v);
                    if (t == "fr") d.drive = u8(0);
                    else if (t == "ff") d.drive = u8(1);
                    else if (t == "awd" || t == "4wd") d.drive = u8(2);
                    else if (t == "mr") d.drive = u8(3);
                    else if (t == "rr") d.drive = u8(4);
                    else return fail("[drivetrain] drive must be FF, FR, MR, RR or AWD");
                } else if (key == "front_torque_pct") set_u8(d.front_torque_pct);
                else if (key == "gears") {
                    d.gears.clear();
                    for (const std::string& w : words(v)) {
                        auto n = number(w);
                        if (!n || *n <= 0) return fail("[drivetrain] gears: bad ratio '" + w + "'");
                        d.gears.push_back(float(*n));
                    }
                    if (d.gears.size() > 7) return fail("[drivetrain] gears: at most 7");
                } else if (key == "final_drive") set_f(d.final_drive);
                else if (key == "reverse") set_f(d.reverse);
                else known = false;
            } else if (sn == "tires") {
                if (key == "front" || key == "rear" || key == "size") {
                    auto t = parse_tire_size(v);
                    if (!t) return fail("[tires] " + key + ": bad size '" + v + "' (e.g. 255/35R21; width and aspect multiples of 5)");
                    if (key != "rear") d.front_tire = t;
                    if (key != "front") d.rear_tire = t;
                } else if (key == "grip_pct") set_f(d.grip_pct);
                else known = false;
            } else if (sn == "model") {
                if (key == "file") d.model_file = v;
                else if (key == "wheel") d.wheel = v;
                else if (key == "lower_mm") {
                    if (auto n = num()) d.model_lower_mm = float(*n);
                }
                else known = false;
            } else if (sn == "paints") {
                std::string hex = v;
                if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
                char* end = nullptr;
                unsigned long rgb = hex.size() == 6 ? std::strtoul(hex.c_str(), &end, 16) : 0;
                if (hex.size() != 6 || !end || *end) return fail("[paints] " + key_raw + ": want #RRGGBB, got '" + v + "'");
                d.paints.push_back({key_raw, {float((rgb >> 16) & 0xFF) / 255.0f, float((rgb >> 8) & 0xFF) / 255.0f,
                                              float(rgb & 0xFF) / 255.0f}});
            } else if (sn == "dealer") {
                if (key == "for_sale") d.for_sale = to_lower(v) != "no" && v != "0" && to_lower(v) != "off";
                else if (key == "description") d.description = v;
                else if (key == "picture") d.picture = v;
                else if (key == "thumb") d.thumb = v;
                else if (key == "logo") d.logo = v;
                else if (key == "picture_paint") {} // for the mod's own picture tools
                else known = false;
            } else if (sn == "arcade") {
                if (key == "class") {
                    std::string c = to_lower(v);
                    if (c != "auto" && c != "c" && c != "b" && c != "a" && c != "s" && c != "dirt" && c != "none")
                        return fail("[arcade] class must be auto, c, b, a, s, dirt or none");
                    d.arcade_class = c;
                } else if (key == "race_variant") {
                    d.arcade_variant = to_lower(v) == "auto" ? -1 : std::clamp(std::atoi(v.c_str()), 0, 1);
                } else if (key == "level") {
                    if (auto n = num()) d.arcade_level = std::clamp(int(*n), 0, 12);
                } else known = false;
            } else if (sn == "reference") {
                // Real-world figures for `gt3tool modcar` to compare against; not used by the game.
            } else if (sn == "sound") {
                if (key == "whine_order") set_f(d.whine_order);
                else if (key == "whine_level") set_f(d.whine_level);
                else if (key == "inverter_level") set_f(d.inverter_level);
                else known = false;
            } else {
                known = false;
            }
            if (!known) warn("[" + sec.name + "] unknown key '" + key_raw + "'");
        }
    }
    if (!valid_label(d.label)) return fail("[car] label must be 1-40 characters of a-z, 0-9 and _");
    if (d.name.empty()) return fail("[car] name is required");
    if (d.donor.empty()) return fail("[car] donor is required (a car on the disc to borrow unlisted parts from)");
    if (d.electric) {
        if (d.curve.empty() && (!d.torque_nm || !d.power_kw || !d.max_rpm))
            return fail("[powertrain] electric: give torque_nm, power_kw and max_rpm (or a curve and max_rpm)");
        if (!d.max_rpm) return fail("[powertrain] electric: max_rpm is required");
        if (d.gears.size() > 1) return fail("[drivetrain] electric: one reduction ratio (gears = <ratio> or final_drive)");
    }
    if (d.max_rpm && *d.max_rpm > 25500) return fail("[powertrain] max_rpm: at most 25500");
    return d;
}

bool add_mod_car(CarDatabase& db, const ModCarDef& def, std::string* error, const text::JisTable* jis) {
    auto fail = [&](const std::string& m) {
        if (error) *error = m;
        return false;
    };
    auto game = [jis](const std::string& utf8) { return text::encode_game_text(text::utf8_decode(utf8), jis); };
    if (db.car(def.label) && !db.mod_car(def.label)) return fail("label '" + def.label + "' is already a disc car");
    const Car* donor = db.car(def.donor);
    if (!donor) return fail("donor '" + def.donor + "' is not a car on this disc");
    if (db.mod_car(def.donor)) return fail("donor '" + def.donor + "' is a mod car; use a disc car");
    const u64 donor_code = db::id_hash(def.donor), car_code = db::id_hash(def.label);

    // --- clone the donor's parts --------------------------------------------
    struct Rec {
        TableId table;
        Bytes bytes;
    };
    std::vector<Rec> recs;
    std::map<u64, size_t> remap; // donor part id -> index into recs
    auto relabel = [&](const std::string& l) -> std::string {
        size_t p = l.find(def.donor);
        return p == std::string::npos ? std::string() : l.substr(0, p) + def.label + l.substr(p + def.donor.size());
    };
    auto clone = [&](TableId t, ByteSpan r, std::string new_label) {
        Bytes b(r.begin(), r.end());
        put64(b, 0, db::id_hash(new_label));
        put64(b, 8, car_code);
        db.add_label(new_label);
        remap[rd64(r, 0)] = recs.size();
        recs.push_back({t, std::move(b)});
    };
    std::set<u64> stock(donor->parts.begin(), donor->parts.end());
    for (int ti = 0; ti <= 29; ++ti) {
        bool part_table = ti <= 21 || ti == 28 || ti == 29;
        if (!part_table || ti == 2) continue; // steer records are shared ("n", "n1")
        if (def.electric && engine_part_table(ti)) continue;
        TableId t = TableId(ti);
        std::vector<ByteSpan> rows = db.table(t).records;
        for (ByteSpan r : rows) {
            if (r.size() < 16 || rd64(r, 8) != donor_code) continue;
            u64 id = rd64(r, 0);
            // Race bodies use other models; electric cars keep only their stock gearing/driveline.
            bool only_stock = t == TableId::RacingModify || t == TableId::Engine ||
                              (def.electric && (t == TableId::Gear || t == TableId::Drivetrain));
            if (only_stock && !stock.count(id)) continue;
            std::string nl = relabel(db.label(id));
            if (nl.empty()) continue;
            clone(t, r, nl);
        }
    }
    std::array<u64, 27> parts = donor->parts;
    for (size_t i = 0; i < parts.size(); ++i) {
        int t = kCarPartTables[i];
        if (def.electric && engine_part_table(t)) {
            parts[i] = 0;
            continue;
        }
        if (parts[i] == 0) continue;
        if (!remap.count(parts[i]) && (t == 3 || t == 5 || t == 6 || t == 13 || t == 17 || t == 18 || t == 28 || t == 29)) {
            // A stock part we must edit but whose label does not name the donor: give it one.
            if (const ByteSpan* r = db.record(TableId(t), parts[i]))
                clone(TableId(t), *r, std::string(table_name(TableId(t))) + "_" + def.label);
        }
        if (auto it = remap.find(parts[i]); it != remap.end()) parts[i] = rd64(recs[it->second].bytes, 0);
    }
    auto stock_rec = [&](int slot) -> Bytes* {
        u64 id = parts[size_t(slot)];
        for (Rec& r : recs)
            if (rd64(r.bytes, 0) == id) return &r.bytes;
        return nullptr;
    };
    Bytes* ch = stock_rec(3);
    Bytes* body = stock_rec(5);
    Bytes* en = stock_rec(6);
    Bytes* dt = stock_rec(13);
    Bytes* ge = stock_rec(18);
    if (!ch || !body || !en || !dt || !ge) return fail("donor '" + def.donor + "' lacks a stock chassis/body/engine/drivetrain/gear record");

    // --- chassis and body ----------------------------------------------------
    const u16 d_weight = get16(*ch, 0x1A), d_len = get16(*ch, 0x14), d_height = get16(*ch, 0x16), d_wb = get16(*ch, 0x18);
    const u8 d_front = (*ch)[0x10];
    const u16 d_width = get16(*body, 0x36);
    const u16 weight = def.weight_kg.value_or(d_weight), len = def.length_mm.value_or(d_len);
    const u16 height = def.height_mm.value_or(d_height), wb = def.wheelbase_mm.value_or(d_wb);
    const u16 width = def.width_mm.value_or(d_width);
    const u8 front = def.front_weight_pct.value_or(d_front);
    put8(*ch, 0x10, front);
    put16(*ch, 0x14, len);
    put16(*ch, 0x16, height);
    put16(*ch, 0x18, wb);
    put16(*ch, 0x1A, weight);
    {
        // Yaw inertia byte ~ m (L^2 + W^2): scale the donor's.
        double r = double(weight) / std::max<u16>(d_weight, 1) *
                   (double(len) * len + double(width) * width) / std::max(1.0, double(d_len) * d_len + double(d_width) * d_width);
        put8(*ch, 0x1C, clamp8((*ch)[0x1C] * r));
    }
    const float rl = d_len ? float(len) / d_len : 1, rw = d_width ? float(width) / d_width : 1, rh = d_height ? float(height) / d_height : 1;
    if (!def.model.empty()) {
        put64(*body, 0x18, db::id_hash(def.model));
        db.add_label(def.model);
    }
    for (int i = 0; i < 4; ++i) {
        float f = getf(*body, 0x20 + size_t(i) * 4);
        float k = i < 2 ? rl : i == 2 ? rw : rh;
        if (f > 0) putf(*body, 0x20 + size_t(i) * 4, std::round(f * k));
    }
    if (def.front_track_mm) put16(*body, 0x32, *def.front_track_mm);
    if (def.rear_track_mm) put16(*body, 0x34, *def.rear_track_mm);
    put16(*body, 0x36, width);
    if (def.cd) put8(*body, 0x39, clamp8(*def.cd * 100));
    if (def.frontal_area_m2) putf(*body, 0x44, *def.frontal_area_m2);
    else if (getf(*body, 0x44) > 0) putf(*body, 0x44, getf(*body, 0x44) * rw * rh);

    // --- suspension (every clone, so the shop's kits suit the new weight) ----
    std::optional<int> ride_delta;
    if (def.ride_height_mm)
        if (Bytes* su = stock_rec(19); su && su->size() >= 0x5C) ride_delta = int(*def.ride_height_mm) - int(get16(*su, 0x54));
    for (Rec& r : recs) {
        if (r.table != TableId::Suspension || r.bytes.size() < 0x7C) continue;
        Bytes& b = r.bytes;
        if (def.scale_suspension && d_weight) {
            double rf = double(weight) * front / std::max(1.0, double(d_weight) * d_front);
            double rr = double(weight) * (100 - front) / std::max(1.0, double(d_weight) * (100 - d_front));
            for (size_t o : {0x21, 0x22, 0x23, 0x2C, 0x2D, 0x2F, 0x30, 0x33, 0x34, 0x36, 0x37, 0x48, 0x49}) b[o] = clamp8(b[o] * rf);
            for (size_t o : {0x24, 0x25, 0x26, 0x3A, 0x3B, 0x3D, 0x3E, 0x41, 0x42, 0x44, 0x45, 0x4C, 0x4D}) b[o] = clamp8(b[o] * rr);
        }
        if (ride_delta)
            for (size_t o : {0x50, 0x52, 0x54, 0x56, 0x58, 0x5A}) put16(b, o, clamp16(int(get16(b, o)) + *ride_delta));
    }

    // --- tyres -----------------------------------------------------------------
    auto size_id = [&](const ModCarDef::TireSize& s) -> u64 {
        for (ByteSpan r : db.table(TableId::TireSize).records)
            if (r.size() >= 0x0B && r[0x08] == s.rim_in && r[0x09] == s.aspect_5 && r[0x0A] == s.width_mm_5) return rd64(r, 0);
        std::string l = "szm" + std::to_string(s.width_mm_5 * 5) + "_" + std::to_string(s.aspect_5 * 5) + "r" + std::to_string(s.rim_in);
        const db::Table& t = db.table(TableId::TireSize);
        Bytes b = t.records.empty() ? Bytes(16, 0) : Bytes(t.records[0].begin(), t.records[0].end());
        put64(b, 0, db::id_hash(l));
        put8(b, 0x08, s.rim_in);
        put8(b, 0x09, s.aspect_5);
        put8(b, 0x0A, s.width_mm_5);
        db.add_label(l);
        db.put_record(TableId::TireSize, std::move(b));
        return db::id_hash(l);
    };
    std::optional<u64> fsz, rsz;
    if (def.front_tire) fsz = size_id(*def.front_tire);
    if (def.rear_tire) rsz = size_id(*def.rear_tire);
    for (Rec& r : recs) {
        if (r.table == TableId::FrontTire && fsz) put64(r.bytes, 0x18, *fsz);
        if (r.table == TableId::RearTire && rsz) put64(r.bytes, 0x18, *rsz);
    }
    // Street-tyre grip: the street compounds (stock "normal", arcade "arc_normal" /
    // "arc_d_normal") are replaced by copies with the base grip (+0x08, mu x 100) scaled.
    if (def.grip_pct && std::fabs(*def.grip_pct - 100) > 0.01f) {
        std::map<u64, u64> street; // compound id -> this car's copy
        for (const char* name : {"normal", "arc_normal", "arc_d_normal"}) {
            const ByteSpan* c = db.record(TableId::TireCompound, db::id_hash(name));
            if (!c) continue;
            std::string l = def.label + "_" + name;
            Bytes b(c->begin(), c->end());
            put64(b, 0, db::id_hash(l));
            put8(b, 0x08, clamp8((*c)[0x08] * *def.grip_pct / 100.0f));
            db.add_label(l);
            db.put_record(TableId::TireCompound, std::move(b));
            street[db::id_hash(name)] = db::id_hash(l);
        }
        for (Rec& r : recs) {
            if (r.table != TableId::FrontTire && r.table != TableId::RearTire) continue;
            for (size_t k = 0; k < 4; ++k)
                if (auto it = street.find(rd64(r.bytes, 0x20 + k * 8)); it != street.end()) put64(r.bytes, 0x20 + k * 8, it->second);
        }
    }
    // Brakes: the donor's torques scaled by the weight ratio (a heavier car keeps the donor's
    // deceleration rather than stopping on its brakes).
    if (def.scale_brakes && d_weight)
        for (Rec& r : recs)
            if (r.table == TableId::Brake && r.bytes.size() > 0x13)
                for (size_t o : {0x11, 0x12, 0x13}) r.bytes[o] = clamp8(r.bytes[o] * float(weight) / d_weight);

    // --- engine / motor ----------------------------------------------------------
    std::vector<std::pair<float, float>> curve = def.curve;
    if (curve.empty() && def.electric) curve = electric_curve(*def.torque_nm, *def.power_kw, *def.max_rpm, def.base_rpm.value_or(0));
    if (curve.empty() && def.power_kw) { // combustion: the donor's curve scaled to the given power
        if (auto e = db.engine(donor->parts[6])) {
            float k = *def.power_kw * 1.35962f / std::max(1.0f, e->curve_peak_ps());
            for (size_t i = 0; i < e->rpm.size(); ++i) curve.push_back({float(e->rpm[i]), e->torque[i] * 9.80665f * k});
        }
    }
    float peak_nm = 0, peak_kw = 0, peak_nm_rpm = 0, peak_kw_rpm = 0;
    if (!curve.empty()) {
        std::sort(curve.begin(), curve.end());
        int n = 0;
        int last = -1;
        for (const auto& [rpm, nm] : curve) {
            int a = int(std::lround(rpm / 100.0f));
            if (a <= last || a > 255 || n >= 16) continue;
            last = a;
            put8(*en, 0x47 + size_t(n), u8(a));
            put16(*en, 0x18 + size_t(n) * 2, clamp16(nm / 0.098)); // N*m = raw x 9.8 x 0.01 (scale byte 100)
            ++n;
            float kw = nm * a * 100.0f * 2 * 3.14159265f / 60.0f / 1000.0f;
            if (nm > peak_nm) peak_nm = nm, peak_nm_rpm = a * 100.0f;
            if (kw > peak_kw) peak_kw = kw, peak_kw_rpm = a * 100.0f;
        }
        if (n < 2) return fail("[powertrain] curve needs at least two distinct points (100 rpm apart)");
        for (int i = n; i < 16; ++i) {
            put8(*en, 0x47 + size_t(i), 0xFF);
            put16(*en, 0x18 + size_t(i) * 2, 0);
        }
        put8(*en, 0x57, u8(n));
        put8(*en, 0x42, 100);
        float limit = def.max_rpm.value_or(float(last) * 100.0f);
        u8 lim = clamp8(limit / 100.0f);
        put8(*en, 0x45, lim);
        put8(*en, 0x46, u8(std::max(0, int(lim) - 5)));
        put16(*en, 0x3A, clamp16((def.rated_kw ? *def.rated_kw : def.power_kw ? *def.power_kw : peak_kw) * 1.35962f));
        put16(*en, 0x3E, clamp16((def.torque_nm ? *def.torque_nm : peak_nm) / 9.80665f * 100.0f));
        auto rpm_text = [](float r) { return std::to_string(int(std::lround(r / 100.0f)) * 100) + "rpm"; };
        put16(*en, 0x3C, db.add_unistring(rpm_text(peak_kw_rpm)));
        put16(*en, 0x40, db.add_unistring(def.electric ? "0rpm" : rpm_text(peak_nm_rpm)));
    } else if (def.max_rpm) { // the donor's curve with this rev limit
        u8 lim = clamp8(*def.max_rpm / 100.0f);
        put8(*en, 0x45, lim);
        put8(*en, 0x46, u8(std::max(0, int(lim) - 5)));
    }
    if (def.output_pct) put8(*en, 0x42, *def.output_pct); // also on the donor's curve
    const int output_pct = (*en)[0x42];
    if (peak_kw <= 0)
        if (auto e = db.engine(donor->parts[6])) peak_kw = e->curve_peak_ps() / 1.35962f; // the donor's curve, for the arcade class
    if (def.electric) {
        put8(*en, 0x44, 0); // no idle
        put16(*en, 0x16, 0); // no sampled engine sound: the race synthesises the motor
        put16(*en, 0x10, db.add_unistring(def.layout_text.empty() ? "Electric" : game(def.layout_text)));
        put16(*en, 0x12, db.add_unistring("Motor"));
        put16(*en, 0x14, db.add_unistring(def.aspiration_text.empty() ? "EV" : game(def.aspiration_text)));
        put16(*en, 0x38, db.add_string(def.displacement_text.empty() ? "-" : game(def.displacement_text)));
    } else {
        if (def.idle_rpm) put8(*en, 0x44, clamp8(*def.idle_rpm / 10.0f));
        if (def.sound_id) put16(*en, 0x16, *def.sound_id);
        if (!def.layout_text.empty()) put16(*en, 0x10, db.add_unistring(game(def.layout_text)));
        if (!def.aspiration_text.empty()) put16(*en, 0x14, db.add_unistring(game(def.aspiration_text)));
        if (!def.displacement_text.empty()) put16(*en, 0x38, db.add_string(game(def.displacement_text)));
    }

    // --- drivetrain ----------------------------------------------------------------
    const u8 drive = def.drive.value_or((*dt)[0x14]);
    put8(*dt, 0x14, drive);
    if (drive == 2) {
        u8 f = def.front_torque_pct.value_or((*dt)[0x13] ? (*dt)[0x13] : 40);
        if (def.electric || (*dt)[0x15] == 0xFE) put8(*dt, 0x15, 7); // fixed split: one motor per axle
        if (def.front_torque_pct || def.electric) put8(*dt, 0x11, f), put8(*dt, 0x12, f), put8(*dt, 0x13, f);
    } else {
        put8(*dt, 0x15, 0xFE);
    }
    if (def.electric) {
        put16(*dt, 0x16, 12); // motor drag: ~12 N*m at max speed (regen is separate)
        put16(*dt, 0x20, clamp16(def.motor_inertia.value_or(0.06f) * 1000.0f));
    } else if (def.motor_inertia) {
        put16(*dt, 0x20, clamp16(*def.motor_inertia * 1000.0f));
    }

    // --- gearbox ---------------------------------------------------------------------
    float overall_ratio = 0; // electric: motor revolutions per wheel revolution
    if (def.electric || !def.gears.empty() || def.final_drive) {
        std::vector<float> gears = def.gears;
        float fin = def.final_drive.value_or(0);
        if (def.electric) {
            if (gears.empty()) gears = {1.0f};
            if (fin <= 0) fin = gears[0], gears[0] = 1.0f; // "gears = 9.04": the whole reduction
        }
        if (!gears.empty()) {
            put8(*ge, 0x10, 0);
            put8(*ge, 0x11, u8(gears.size()));
            for (size_t i = 0; i < 7; ++i) put16(*ge, 0x14 + i * 2, u16(i < gears.size() ? clamps16(gears[i] * 1000) : 0));
            put8(*ge, 0x28, 0);
        }
        if (!gears.empty() && fin > 0) overall_ratio = gears[0] * fin;
        if (fin > 0) {
            s16 f = clamps16(fin * 1000);
            // The shop's gearboxes (the donor's, cloned) share the stock final drive, as every gearbox on the disc does:
            // this car's, with an adjustable box's range widened to hold it. Their ratios are generated for the car.
            const s16 d_fin = s16(get16(*ge, 0x22));
            for (Rec& r : recs) {
                if (r.table != TableId::Gear || &r.bytes == ge || r.bytes.size() < 0x28) continue;
                for (size_t o : {0x24, 0x26})
                    if (s16(get16(r.bytes, o)) == d_fin) put16(r.bytes, o, u16(f));
                put16(r.bytes, 0x22, u16(f));
                if (s16(get16(r.bytes, 0x24)) < f) put16(r.bytes, 0x24, u16(f));
                if (s16(get16(r.bytes, 0x26)) > f) put16(r.bytes, 0x26, u16(f));
            }
            put16(*ge, 0x22, u16(f));
            put16(*ge, 0x24, u16(f));
            put16(*ge, 0x26, u16(f));
        }
        if (def.reverse || def.electric) {
            s16 old = s16(get16(*ge, 0x12));
            float rv = def.reverse.value_or(gears.empty() ? 1.0f : gears[0]);
            put16(*ge, 0x12, u16(clamps16((old < 0 ? -1 : 1) * rv * 1000)));
        }
    }

    // --- raw pokes -----------------------------------------------------------------------
    for (const ModCarDef::Poke& p : def.pokes) {
        Bytes* b = nullptr;
        for (size_t i = 0; i < kCarPartTables.size() && !b; ++i)
            if (kCarPartTables[i] == int(p.table)) b = stock_rec(int(i));
        if (!b) return fail(std::string("[raw.") + table_name(p.table) + "]: the car has no own record in that table");
        size_t w = p.type == 'b' ? 1 : p.type == 'h' ? 2 : 4;
        if (p.offset < 0x10 || p.offset + w > b->size()) return fail(std::string("[raw.") + table_name(p.table) + "]: offset out of range");
        if (p.type == 'b') put8(*b, p.offset, clamp8(p.value));
        else if (p.type == 'h') put16(*b, p.offset, u16(std::lround(p.value)));
        else if (p.type == 'i') put32(*b, p.offset, u32(std::llround(p.value)));
        else putf(*b, p.offset, float(p.value));
    }

    Bytes arcade_rec;
    // --- the car record ----------------------------------------------------------------------
    Bytes car(donor->bytes.begin(), donor->bytes.end());
    put64(car, 0, car_code);
    for (size_t i = 0; i < parts.size(); ++i) put64(car, 8 + i * 8, parts[i]);
    u16 maker_id = donor->maker_id;
    std::string maker = def.maker.empty() ? donor->maker : game(def.maker); // ("Citroën" finds the disc's)
    for (const Car& c : db.cars())
        if (!db.mod_car(c.label) && to_lower(c.maker) == to_lower(maker)) {
            maker_id = c.maker_id;
            break;
        }
    put16(car, 0xE0, maker_id);
    put16(car, 0xE2, maker_id);
    u16 name = db.add_unistring(game(def.name));
    put16(car, 0xE4, name);
    put16(car, 0xE6, name);
    put16(car, 0xE8, db.add_string(def.label));
    put16(car, 0xEA, def.year.value_or(donor->year));
    // The price as the disc stores its own cars' (docs/MODDING.md): credits in the US and
    // European languages, shown and charged x100 in Japanese like every price (GtMode::car_price).
    put32(car, 0xEC, def.price.value_or(donor->price));
    put8(car, 0xF0, 0);
    if (def.category) put8(car, 0xF1, *def.category);
    // The same names in every language (car_name_fields), and offered in every language
    // (region byte +0xF7 = 0).
    for (size_t o : {0xF2, 0xF4, 0x100, 0x102}) put16(car, o, name);
    put8(car, 0xF7, 0);
    put16(car, 0x104, db.add_unistring(maker));
    db.add_label(def.label);

    // Arcade car (table 35): the car with its own parts and stock settings, as the arcade
    // pickers and races look cars up there (the tyres switch to its ft_/rt_ stage 10-12
    // records), so it races the same as from its own parts in GT Mode.
    {
        const db::Table& at = db.table(TableId::ArcadeCar);
        ByteSpan tmpl = at.records.empty() ? ByteSpan{} : at.records[0];
        for (ByteSpan r : at.records)
            if (r.size() >= 16 && rd64(r, 8) == donor_code) {
                tmpl = r;
                break;
            }
        if (!tmpl.empty()) {
            Bytes a(tmpl.begin(), tmpl.end());
            put64(a, 0, car_code);
            put64(a, 8, car_code);
            for (size_t i = 0; i < parts.size(); ++i) put64(a, 0x10 + i * 8, parts[i]);
            stock_arcade_settings(a, [&](int slot) -> ByteSpan {
                if (const Bytes* b = stock_rec(slot)) return *b;
                const ByteSpan* r = parts[size_t(slot)] ? db.record(TableId(kCarPartTables[size_t(slot)]), parts[size_t(slot)]) : nullptr;
                return r ? *r : ByteSpan{};
            });
            arcade_rec = std::move(a);
        }
    }
    for (Rec& r : recs) db.put_record(r.table, std::move(r.bytes));
    db.put_record(TableId::Car, std::move(car));
    if (!arcade_rec.empty()) db.put_record(TableId::ArcadeCar, std::move(arcade_rec));

    ModCar m;
    m.label = def.label;
    m.mod = def.mod;
    m.file = def.file;
    m.donor = def.donor;
    m.electric = def.electric;
    m.paints = def.paints;
    for (auto& p : m.paints) p.first = game(p.first); // (as the disc's colour names)
    m.for_sale = def.for_sale;
    m.description = game(def.description);
    {
        // Arcade class by weight / power, the game's garage-car table (weight_class.h).
        // Power: the curve's peak with the hidden output factor.
        float ps = peak_kw * 1.35962f * float(output_pct) / 100.0f;
        const WeightClass w = class_by_weight(float(weight) / std::max(ps, 1.0f));
        const std::string by_weight(1, "?cbas"[w.cls]);
        std::string cls = def.arcade_class;
        int variant = def.arcade_variant;
        if (cls == "auto") cls = by_weight;
        if (variant < 0) variant = by_weight == cls ? w.next : 0;
        if (cls != "none" && cls != "auto") m.arcade_class = "CLASS_" + to_upper_ascii(cls);
        m.arcade_variant = std::max(variant, 0);
        m.arcade_level = def.arcade_level;
    }
    m.wheel = def.wheel;
    m.model_lower = def.model_lower_mm * 0.001f;
    m.model_file = mod_path(def.file, def.model_file);
    m.picture = mod_path(def.file, def.picture);
    m.thumb = mod_path(def.file, def.thumb);
    m.logo = mod_path(def.file, def.logo);
    if (def.electric) {
        // Regen: lift-off deceleration regen_g (default 0.2 g, typical one-pedal driving) turned
        // into motor torque through the driven tyre and the reduction; power-limited at speed.
        float r = 0.33f; // driven tyre radius, m
        if (auto ts = drive == 1 ? def.front_tire : def.rear_tire)
            r = ts->rim_in * 0.0127f + ts->width_mm_5 * 5 * 0.001f * ts->aspect_5 * 5 * 0.01f;
        float mass = float(weight) + 120.0f; // as the spec builder counts it
        float g = def.regen_g.value_or(0.2f);
        m.regen_torque_nm = def.regen_torque_nm.value_or(overall_ratio > 0 ? g * 9.81f * mass * r / overall_ratio
                                                                            : 0.3f * (def.torque_nm ? *def.torque_nm : peak_nm));
        // Enough power for the full rate up to ~70 km/h, but no more than 70 % of the motor's.
        float want_kw = g * 9.81f * mass * (70 / 3.6f) / 1000;
        m.regen_kw = def.regen_kw.value_or(std::min(want_kw, 0.7f * (def.power_kw ? *def.power_kw : peak_kw)));
        m.whine_order = def.whine_order.value_or(1.0f);
        m.whine_level = def.whine_level.value_or(1.0f);
        m.inverter_level = def.inverter_level.value_or(1.0f);
    }
    if (def.fit_body && def.model.empty()) { // also used for the shadows under a glTF body
        m.fit = {float(d_len), float(d_wb), float(d_width), float(d_height), float(len), float(wb), float(width), float(height)};
    }
    db.add_mod_car(std::move(m));
    return true;
}

void load_mod_car_files(CarDatabase& db, const std::vector<std::string>& files, const std::string& mod,
                        const text::JisTable* jis) {
    bool any = false;
    for (const std::string& f : files) {
        auto bytes = read_host_file(f);
        if (!bytes) {
            db.add_mod_car_error(f + ": cannot read");
            continue;
        }
        std::string err;
        std::vector<std::string> warns;
        auto def = parse_mod_car(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), &err, &warns);
        for (const std::string& w : warns) LOG_WARN("Mod car %s: %s", f.c_str(), w.c_str());
        if (def) {
            def->mod = mod;
            def->file = f;
        }
        if (!def || !add_mod_car(db, *def, &err, jis)) {
            LOG_WARN("Mod car %s: %s", f.c_str(), err.c_str());
            db.add_mod_car_error(f + ": " + err);
            continue;
        }
        LOG_INFO("Mod car '%s' (%s) from %s", def->label.c_str(), def->name.c_str(), mod.empty() ? f.c_str() : mod.c_str());
        any = true;
    }
    if (any) db.rebuild();
}

void load_mod_maker_file(CarDatabase& db, const std::string& file, const std::string& mod, const text::JisTable* jis) {
    auto ini = IniDocument::load(file);
    if (!ini) {
        db.add_mod_car_error(file + ": cannot read");
        return;
    }
    auto get = [&](const char* k) { return value_text(ini->get("maker", k).value_or("")); };
    ModMaker m;
    std::string base = file.substr(file.find_last_of('/') + 1);
    m.id = to_lower(base.substr(0, base.size() - 4));
    m.name = get("name");
    m.region = to_lower(get("region"));
    m.description = get("description");
    m.icon = mod_path(file, get("icon"));
    m.background = mod_path(file, get("background"));
    m.logo = mod_path(file, get("logo"));
    m.mod = mod;
    m.file = file;
    static const char* kRegions[] = {"jp", "us", "gr", "fr", "it", "uk", "bel", "aus"};
    bool ok = !m.name.empty() && valid_label(m.id);
    bool region = false;
    for (const char* r : kRegions) region = region || m.region == r;
    if (!ok || !region) {
        db.add_mod_car_error(file + ": [maker] needs a name and a region (jp, us, gr, fr, it, uk, bel or aus)");
        return;
    }
    LOG_INFO("Mod maker '%s' (%s, region %s) from %s", m.id.c_str(), m.name.c_str(), m.region.c_str(), mod.c_str());
    // Game text, as the cars' maker strings it is matched with (add_mod_car) and the menus draw.
    m.name = text::encode_game_text(text::utf8_decode(m.name), jis);
    m.description = text::encode_game_text(text::utf8_decode(m.description), jis);
    db.add_mod_maker(std::move(m));
}

} // namespace gt3r::data
