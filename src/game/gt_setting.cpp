#include "game/gt_setting.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace gt3r::game {

namespace {

bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

const ByteSpan* part_rec(const GtMode& gt, const GarageCar& c, int table) {
    int slot = part_slot(table);
    return slot < 0 ? nullptr : gt.db().record(data::TableId(table), c.parts[size_t(slot)]);
}
int b8(const ByteSpan* r, size_t off) { return r && r->size() > off ? (*r)[off] : 0; }
int b16(const ByteSpan* r, size_t off) { return r && r->size() > off + 1 ? int(rd16(*r, off)) : 0; }

// The game's "%.Nt": an integer printed with a decimal point before its last N digits.
std::string fixed_point(int v, int digits) {
    if (digits <= 0) return std::to_string(v);
    std::string s = std::to_string(v < 0 ? -v : v);
    while (int(s.size()) <= digits) s.insert(s.begin(), '0');
    s.insert(s.size() - size_t(digits), ".");
    return v < 0 ? "-" + s : s;
}

const char* const kDegree = "\xA1\xEB"; // "°" in the game's text encoding (EUC)

// Which page predicate locks each slider (also the "disable_*" classes).
enum class Group { Spring, Height, Damper, BoundRebound, Camber, Toe, Stabilizer, Brake, LsdInit, LsdAccel,
                   LsdDecel, Gear, Aero, Ayc, Ascc, Tcs, Vcd, None };

bool group_enabled(const GtMode& gt, const GarageCar& c, Group g) {
    const ByteSpan* sus = part_rec(gt, c, 18);
    const ByteSpan* lsd = part_rec(gt, c, 21);
    switch (g) {
    case Group::Spring: return b8(sus, 0x21) != b8(sus, 0x22) || b8(sus, 0x24) != b8(sus, 0x25);
    case Group::Height: return b16(sus, 0x50) != b16(sus, 0x52) || b16(sus, 0x56) != b16(sus, 0x58);
    case Group::Damper: { int st = b8(sus, 0x10); return st == 1 || st == 2; }
    case Group::BoundRebound: return b8(sus, 0x10) == 3;
    case Group::Camber: return b8(sus, 0x11) != b8(sus, 0x12) || b8(sus, 0x14) != b8(sus, 0x15);
    case Group::Toe: return b8(sus, 0x17) != b8(sus, 0x18) || b8(sus, 0x1A) != b8(sus, 0x1B);
    case Group::Stabilizer: return b8(sus, 0x47) != 1 || b8(sus, 0x4B) != 1;
    case Group::Brake: {
        const ByteSpan* bc = part_rec(gt, c, 1);
        return b8(bc, 0x11) != 1 || b8(bc, 0x15) != 1;
    }
    case Group::LsdInit: return b8(lsd, 0x13) != b8(lsd, 0x14) || b8(lsd, 0x1D) != b8(lsd, 0x1E);
    case Group::LsdAccel: return b8(lsd, 0x16) != b8(lsd, 0x17) || b8(lsd, 0x20) != b8(lsd, 0x21);
    case Group::LsdDecel: return b8(lsd, 0x19) != b8(lsd, 0x1A) || b8(lsd, 0x23) != b8(lsd, 0x24);
    case Group::Gear: {
        const ByteSpan* gear = part_rec(gt, c, 17);
        return gear && b16(gear, 0x26) != b16(gear, 0x24);
    }
    case Group::Aero: {
        const ByteSpan* body = part_rec(gt, c, 5);
        return b8(body, 0x11) != b8(body, 0x12) || b8(body, 0x3A) != b8(body, 0x3B);
    }
    case Group::Ayc: return b8(lsd, 0x10) == 5;
    case Group::Ascc: return b8(part_rec(gt, c, 23), 0x11) != 1;
    case Group::Tcs: return b8(part_rec(gt, c, 22), 0x12) != 1;
    case Group::Vcd: {
        const ByteSpan* dt = part_rec(gt, c, 13);
        return b8(dt, 0x11) != b8(dt, 0x12);
    }
    case Group::None: return true;
    }
    return true;
}

Group group_of_slider(const std::string& item) {
    struct M { const char* prefix; Group g; };
    static const M map[] = {
        {"slider_rate_", Group::Spring},        {"slider_height_", Group::Height},
        {"slider_damper_", Group::Damper},      {"slider_bound_", Group::BoundRebound},
        {"slider_rebound_", Group::BoundRebound}, {"slider_camber_", Group::Camber},
        {"slider_toe_", Group::Toe},            {"slider_stabilizer_", Group::Stabilizer},
        {"slider_brake_", Group::Brake},        {"slider_lsd_init_", Group::LsdInit},
        {"slider_lsd_accl_", Group::LsdAccel},  {"slider_lsd_decl_", Group::LsdDecel},
        {"slider_gear_", Group::Gear},          {"slider_aero_", Group::Aero},
        {"slider_elec_aycc", Group::Ayc},       {"slider_elec_ascc", Group::Ascc},
        {"slider_elec_tcsc", Group::Tcs},       {"slider_elec_vcdc", Group::Vcd},
    };
    for (const M& m : map)
        if (starts(item, m.prefix)) return m.g;
    return Group::None;
}

// Reference gear ratios (physics §6.7 generator at the reference final drive) and the
// per-gear clamp ranges [lo, hi] (gt_mode §14).
struct GearRanges {
    int n = 0;
    std::array<int, 8> lo{}, hi{};
};

GearRanges gear_ranges(const GtMode& gt, const GarageCar& real, const GarageCar& work) {
    GearRanges r;
    const ByteSpan* g = part_rec(gt, real, 17);
    r.n = std::clamp(b8(g, 0x11), 0, 7);
    sim::CarSettings ref = work.settings;
    ref.final_drive = real.ref_final > 0 ? real.ref_final : real.settings.final_drive;
    if (!sim::apply_gear_slider(gt.db(), real.parts, ref)) ref = real.settings;
    const auto& v = ref.gears;
    if (r.n <= 0) return r;
    r.hi[1] = v[1] * 9 / 8;
    for (int i = 1; i < r.n; ++i) {
        r.lo[size_t(i)] = (2 * v[size_t(i)] + v[size_t(i + 1)]) / 3;
        r.hi[size_t(i + 1)] = (v[size_t(i)] + 2 * v[size_t(i + 1)]) / 3;
    }
    r.lo[size_t(r.n)] = v[size_t(r.n)] * 7 / 8;
    return r;
}

} // namespace

std::optional<SliderRange> slider_range(const GtMode& gt, const GarageCar& real, const GarageCar& work,
                                        const std::string& item) {
    const sim::CarSettings& s = work.settings;
    const ByteSpan* sus = part_rec(gt, real, 18);
    const ByteSpan* lsd = part_rec(gt, real, 21);
    const ByteSpan* gear = part_rec(gt, real, 17);
    const ByteSpan* body = part_rec(gt, real, 5);
    auto range = [](int lo, int hi, int value) { return SliderRange{lo, hi + 1, value}; };
    auto index = [](int steps, int value) { return SliderRange{1, std::max(steps, 1) + 1, value}; };
    if (item == "slider_rate_f") return range(b8(sus, 0x21), b8(sus, 0x22), s.spring[0]);
    if (item == "slider_rate_r") return range(b8(sus, 0x24), b8(sus, 0x25), s.spring[1]);
    if (item == "slider_height_f") return range(b16(sus, 0x50), b16(sus, 0x52), s.ride_height[0]);
    if (item == "slider_height_r") return range(b16(sus, 0x56), b16(sus, 0x58), s.ride_height[1]);
    if (item == "slider_damper_f" || item == "slider_bound_f") return index(b8(sus, 0x2B), s.damper[0]);
    if (item == "slider_damper_r" || item == "slider_bound_r") return index(b8(sus, 0x39), s.damper[4]);
    if (item == "slider_rebound_f") return index(b8(sus, 0x32), s.damper[2]);
    if (item == "slider_rebound_r") return index(b8(sus, 0x40), s.damper[6]);
    if (item == "slider_camber_f") return range(b8(sus, 0x11), b8(sus, 0x12), s.camber[0]);
    if (item == "slider_camber_r") return range(b8(sus, 0x14), b8(sus, 0x15), s.camber[1]);
    if (item == "slider_toe_f") return range(b8(sus, 0x17), b8(sus, 0x18), s.toe[0]);
    if (item == "slider_toe_r") return range(b8(sus, 0x1A), b8(sus, 0x1B), s.toe[1]);
    if (item == "slider_stabilizer_f") return index(b8(sus, 0x47), s.arb[0]);
    if (item == "slider_stabilizer_r") return index(b8(sus, 0x4B), s.arb[1]);
    if (item == "slider_brake_f") return index(b8(part_rec(gt, real, 1), 0x11), s.brake_front);
    if (item == "slider_brake_r") return index(b8(part_rec(gt, real, 1), 0x15), s.brake_rear);
    if (item == "slider_lsd_init_f") return range(b8(lsd, 0x13), b8(lsd, 0x14), s.lsd[0]);
    if (item == "slider_lsd_init_r") {
        if (b8(lsd, 0x10) == 5) return SliderRange{0, 1, 0}; // AYC fitted: its own slider owns this word
        return range(b8(lsd, 0x1D), b8(lsd, 0x1E), s.lsd[1]);
    }
    if (item == "slider_lsd_accl_f") return range(b8(lsd, 0x16), b8(lsd, 0x17), s.lsd[2]);
    if (item == "slider_lsd_accl_r") return range(b8(lsd, 0x20), b8(lsd, 0x21), s.lsd[3]);
    if (item == "slider_lsd_decl_f") return range(b8(lsd, 0x19), b8(lsd, 0x1A), s.lsd[4]);
    if (item == "slider_lsd_decl_r") return range(b8(lsd, 0x23), b8(lsd, 0x24), s.lsd[5]);
    if (item == "slider_elec_aycc") return range(b8(lsd, 0x1D), b8(lsd, 0x1E), s.lsd[1]);
    if (item == "slider_gear_ratio_final")
        return SliderRange{b16(gear, 0x26), std::max(b16(gear, 0x24), b16(gear, 0x26) + 1), s.final_drive};
    if (item == "slider_gear_auto_level") return range(b8(gear, 0x2A), b8(gear, 0x2B), s.gear_slider);
    if (starts(item, "slider_gear_ratio_")) {
        int i = std::atoi(item.c_str() + 18);
        GearRanges r = gear_ranges(gt, real, work);
        if (i < 1 || i > r.n) return SliderRange{0, 1, 0};
        SliderRange sr;
        sr.begin = r.lo[size_t(r.n)];
        sr.end = r.hi[1] + 1;
        sr.sub_begin = r.lo[size_t(i)];
        sr.sub_end = r.hi[size_t(i)] + 1;
        sr.value = s.gears[size_t(i)];
        if (sr.end - sr.begin < 2) sr.sub_begin = sr.sub_end = 0;
        return sr;
    }
    if (item == "slider_aero_f") return range(b8(body, 0x11), b8(body, 0x12), s.downforce_front);
    if (item == "slider_aero_r") return range(b8(body, 0x3A), b8(body, 0x3B), s.downforce_rear);
    if (item == "slider_elec_ascc") return index(b8(part_rec(gt, real, 23), 0x11), s.ascc[0]);
    if (item == "slider_elec_tcsc") {
        int steps = std::max(b8(part_rec(gt, real, 22), 0x12), 1);
        return SliderRange{1, steps + 1, steps + 1 - s.tcs}; // reversed: Level 0 = TCS off
    }
    if (item == "slider_elec_vcdc") {
        const ByteSpan* dt = part_rec(gt, real, 13);
        return range(b8(dt, 0x11), b8(dt, 0x12), s.front_torque);
    }
    return std::nullopt;
}

bool slider_set(const GtMode& gt, const GarageCar& real, GarageCar& work, const std::string& item, int v,
                const std::string& event) {
    sim::CarSettings& s = work.settings;
    const sim::CarSettings before = s;
    bool gear = starts(item, "slider_gear_");
    if (item == "slider_rate_f") s.spring[0] = v;
    else if (item == "slider_rate_r") s.spring[1] = v;
    else if (item == "slider_height_f") s.ride_height[0] = v;
    else if (item == "slider_height_r") s.ride_height[1] = v;
    else if (item == "slider_damper_f") s.damper[0] = s.damper[1] = s.damper[2] = s.damper[3] = v;
    else if (item == "slider_damper_r") s.damper[4] = s.damper[5] = s.damper[6] = s.damper[7] = v;
    else if (item == "slider_bound_f") s.damper[0] = s.damper[1] = v;
    else if (item == "slider_bound_r") s.damper[4] = s.damper[5] = v;
    else if (item == "slider_rebound_f") s.damper[2] = s.damper[3] = v; // no effect in the original either
    else if (item == "slider_rebound_r") s.damper[6] = s.damper[7] = v;
    else if (item == "slider_camber_f") s.camber[0] = v;
    else if (item == "slider_camber_r") s.camber[1] = v;
    else if (item == "slider_toe_f") s.toe[0] = v;
    else if (item == "slider_toe_r") s.toe[1] = v;
    else if (item == "slider_stabilizer_f") s.arb[0] = v;
    else if (item == "slider_stabilizer_r") s.arb[1] = v;
    else if (item == "slider_brake_f") s.brake_front = v;
    else if (item == "slider_brake_r") s.brake_rear = v;
    else if (item == "slider_lsd_init_f") s.lsd[0] = v;
    else if (item == "slider_lsd_init_r" || item == "slider_elec_aycc") s.lsd[1] = v;
    else if (item == "slider_lsd_accl_f") s.lsd[2] = v;
    else if (item == "slider_lsd_accl_r") s.lsd[3] = v;
    else if (item == "slider_lsd_decl_f") s.lsd[4] = v;
    else if (item == "slider_lsd_decl_r") s.lsd[5] = v;
    else if (item == "slider_aero_f") s.downforce_front = v;
    else if (item == "slider_aero_r") s.downforce_rear = v;
    else if (item == "slider_elec_ascc") s.ascc[0] = v;
    else if (item == "slider_elec_tcsc") {
        int steps = std::max(b8(part_rec(gt, real, 22), 0x12), 1);
        s.tcs = steps + 1 - v;
    } else if (item == "slider_elec_vcdc") s.front_torque = v;
    else if (item == "slider_gear_ratio_final") s.final_drive = v;
    else if (item == "slider_gear_auto_level") {
        // Regenerate the ratios at the current final drive, clamp them into the ranges
        // built from the old reference final, then the reference follows the final.
        s.gear_slider = v;
        sim::CarSettings regen = s;
        if (sim::apply_gear_slider(gt.db(), work.parts, regen)) {
            GearRanges r = gear_ranges(gt, real, work);
            for (int i = 1; i <= r.n; ++i)
                s.gears[size_t(i)] = std::clamp(regen.gears[size_t(i)], r.lo[size_t(i)],
                                                std::max(r.lo[size_t(i)], r.hi[size_t(i)]));
        }
        work.ref_final = s.final_drive;
    } else if (starts(item, "slider_gear_ratio_")) {
        int i = std::atoi(item.c_str() + 18);
        if (i < 1 || i > 7) return false;
        s.gears[size_t(i)] = v;
    } else {
        return false;
    }
    if (!gear && !event.empty() && !gt.check_entry(event, work).ok()) {
        s = before;
        return false;
    }
    return true;
}

std::optional<std::string> setting_text(const GtMode& gt, const GarageCar& real, const GarageCar& work,
                                        const std::string& cls) {
    struct C { const char* cls; const char* item; };
    static const C map[] = {
        {"spring_rate_f", "slider_rate_f"},         {"spring_rate_r", "slider_rate_r"},
        {"spring_height_f", "slider_height_f"},     {"spring_height_r", "slider_height_r"},
        {"spring_damper_f", "slider_damper_f"},     {"spring_damper_r", "slider_damper_r"},
        {"spring_bound_f", "slider_bound_f"},       {"spring_bound_r", "slider_bound_r"},
        {"spring_rebound_f", "slider_rebound_f"},   {"spring_rebound_r", "slider_rebound_r"},
        {"spring_camber_f", "slider_camber_f"},     {"spring_camber_r", "slider_camber_r"},
        {"spring_toe_f", "slider_toe_f"},           {"spring_toe_r", "slider_toe_r"},
        {"spring_stabilizer_f", "slider_stabilizer_f"}, {"spring_stabilizer_r", "slider_stabilizer_r"},
        {"spring_brake_f", "slider_brake_f"},       {"spring_brake_r", "slider_brake_r"},
        {"lsd_int_f", "slider_lsd_init_f"},         {"lsd_int_r", "slider_lsd_init_r"},
        {"lsd_acl_f", "slider_lsd_accl_f"},         {"lsd_acl_r", "slider_lsd_accl_r"},
        {"lsd_dec_f", "slider_lsd_decl_f"},         {"lsd_dec_r", "slider_lsd_decl_r"},
        {"gear_ratio_final", "slider_gear_ratio_final"}, {"gear_ratio_auto", "slider_gear_auto_level"},
        {"aero_f", "slider_aero_f"},                {"aero_r", "slider_aero_r"},
        {"elec_aycc", "slider_elec_aycc"},          {"elec_ascc", "slider_elec_ascc"},
        {"elec_tcsc", "slider_elec_tcsc"},          {"elec_vcdc", "slider_elec_vcdc"},
    };
    std::string item;
    for (const C& c : map)
        if (cls == c.cls) item = c.item;
    if (item.empty() && starts(cls, "gear_ratio_")) item = "slider_gear_ratio_" + cls.substr(11);
    if (item.empty()) return std::nullopt;
    const std::string dash = "----";
    if (!group_enabled(gt, real, group_of_slider(item))) return dash;
    std::optional<SliderRange> r = slider_range(gt, real, work, item);
    if (!r) return dash;
    const sim::CarSettings& s = work.settings;
    char b[48];
    auto level = [&](int n) {
        std::snprintf(b, sizeof b, "Level %d", n);
        return std::string(b);
    };
    if (starts(item, "slider_rate_")) return fixed_point(r->value, 1) + "kgf/mm";
    if (starts(item, "slider_height_")) return std::to_string(r->value) + "mm";
    if (starts(item, "slider_damper_") || starts(item, "slider_bound_") || starts(item, "slider_rebound_") ||
        starts(item, "slider_stabilizer_") || starts(item, "slider_brake_") || item == "slider_elec_ascc")
        return level(r->value);
    if (item == "slider_elec_tcsc") return level(r->value - 1);
    if (starts(item, "slider_camber_")) return fixed_point(r->value, 1) + kDegree;
    if (starts(item, "slider_toe_")) return fixed_point(5 * (r->value - 128), 1) + kDegree;
    if (item == "slider_elec_vcdc") return std::to_string(r->value) + " %";
    if (starts(item, "slider_lsd_") || item == "slider_elec_aycc") {
        if (item == "slider_lsd_init_r" && r->fixed() && r->begin == 0 && r->end == 1) return dash;
        return std::to_string(r->value);
    }
    if (item == "slider_gear_auto_level") return r->fixed() ? dash : level(r->value);
    if (item == "slider_gear_ratio_final") return fixed_point(r->value, 3);
    if (starts(item, "slider_gear_ratio_")) {
        int i = std::atoi(item.c_str() + 18);
        const ByteSpan* g = part_rec(gt, real, 17);
        if (i > b8(g, 0x11)) return dash;
        return fixed_point(s.gears[size_t(i)], 3);
    }
    if (starts(item, "slider_aero_")) return fixed_point(r->value, 2);
    return dash;
}

namespace {

// The text slot (DATABASEPARTS "PTEXT_<name>") of each goods on the part rows: the program's
// goods name table (the same in every release). Stage-0 goods mostly read "Normal",
// "N/A" or "-", the engine works "Installed".
const char* const kGoodsText[kGoodsCount] = {
    "NORMAL", "BRAKE_SP", "NOTHING", "EQUIP", "NORMAL", "CLUTCH_1", "CLUTCH_2", "CLUTCH_3", "COMPUTER_NORMAL",
    "COMPUTER_SPORTS", "NO_MODIFY", "MODIFY", "MODIFY", "NO_MODIFY", "EQUIP", "NORMAL", "FLYWHEEL_1", "FLYWHEEL_2",
    "FLYWHEEL_3", "NORMAL", "GEAR_1", "GEAR_2", "GEAR_3", "NORMAL", "INTERCOOLER_1", "INTERCOOLER_2", "NO_MODIFY",
    "STAGE_1", "STAGE_2", "STAGE_3", "NORMAL", "LSD_1", "LSD_2", "LSD_3", "LSD_4", "LSD_5", "NORMAL", "MUFFLER_1",
    "MUFFLER_2", "MUFFLER_3", "NOTHING", "STAGE_1", "STAGE_2", "STAGE_3", "NO_MODIFY", "MODIFY", "NORMAL",
    "PROPELLERSHAFT_1", "NO_MODIFY", "MODIFY", "NORMAL", "SUSPENSION_1", "SUSPENSION_2", "SUSPENSION_3", "NO_EQUIP",
    "EQUIP", "TIRE_NORMAL", "TIRE_SPORTS", "TIRE_SUPER_HARD", "TIRE_HARD", "TIRE_MEDIUM_HARD", "TIRE_MEDIUM",
    "TIRE_MEDIUM_SOFT", "TIRE_SOFT", "TIRE_SUPER_SOFT", "TIRE_CONTROL", "TIRE_DIRT", "TIRE_NORMAL", "TIRE_SPORTS",
    "TIRE_SUPER_HARD", "TIRE_HARD", "TIRE_MEDIUM_HARD", "TIRE_MEDIUM", "TIRE_MEDIUM_SOFT", "TIRE_SOFT",
    "TIRE_SUPER_SOFT", "TIRE_CONTROL", "TIRE_DIRT", "NORMAL", "STAGE_1", "STAGE_2", "STAGE_3", "STAGE_4", "NOTHING",
    "EQUIP", "NORMAL", "CONTROL",
};

} // namespace

std::optional<std::string> setting_parts_text(const GtMode& gt, const GarageCar* car, const std::string& cls,
                                              bool dirt_event) {
    // Each row's parts table (the program has one text routine per class).
    struct R { const char* cls; int table; };
    static const R rows[] = {
        {"parts_sus_s_kit", 18},      {"parts_sus_b_kit", 0},          {"parts_sus_b_balance", 1},
        {"parts_eng_muffler", 20},    {"parts_eng_computer", 10},      {"parts_eng_na_tune", 11},
        {"parts_eng_port_polish", 7}, {"parts_eng_engine_balance", 8}, {"parts_eng_displacement", 9},
        {"parts_eng_turbine", 12},    {"parts_eng_inter_cooler", 19},  {"parts_drv_mission", 17},
        {"parts_drv_clutch", 15},     {"parts_drv_fly", 14},           {"parts_drv_lsd", 21},
        {"parts_drv_shaft", 16},      {"parts_tir_f", 28},             {"parts_tir_r", 29},
        {"parts_oth_asm_s", 23},      {"parts_oth_tcs_s", 22},         {"parts_oth_vcd_s", 13},
        {"parts_oth_weight_r", 4},    {"parts_oth_ayc_s", 21},
    };
    int table = -1;
    for (const R& r : rows)
        if (cls == r.cls) table = r.table;
    if (table < 0) return std::nullopt;
    const std::string dash = "----";
    if (!car) return dash;
    auto text = [&](const char* slot) {
        auto it = gt.data().part_texts.find(std::string("PTEXT_") + slot);
        return it != gt.data().part_texts.end() ? it->second : std::string();
    };
    auto name = [&](int goods) { return goods >= 0 && goods < kGoodsCount ? text(kGoodsText[goods]) : std::string(); };
    // The fitted part's goods: its record's stage (+0x10); the stock goods without a record.
    auto fitted = [&](int t) {
        int g = goods_id(t, b8(part_rec(gt, *car, t), 0x10));
        return g >= 0 ? g : goods_id(t, 0);
    };
    // Turbo: the fitted turbine record's +0x11 (as the events' aspiration rule).
    const bool turbo = b8(part_rec(gt, *car, 12), 0x11) != 0;
    if (cls == "parts_oth_ayc_s") return text(fitted(21) == 35 ? "EQUIP" : "NOTHING"); // AYC = LSD stage 5
    if (table == 12) {
        int g = fitted(12);
        return !turbo && g == goods_id(12, 0) ? text("NOTHING") : name(g);
    }
    if (table == 19) return turbo ? name(fitted(19)) : text("NO_EQUIP");
    if (table == 16) {
        // No driveshaft on FF, MR and RR cars (drivetrain +0x14 = 1, 3, 4).
        int drive = b8(part_rec(gt, *car, 13), 0x14);
        return drive == 1 || drive == 3 || drive == 4 ? text("NO_EQUIP") : name(fitted(16));
    }
    if ((table == 28 || table == 29) && dirt_event) return car->owned.test(66) ? name(66) : dash;
    return name(fitted(table));
}

bool slider_enabled(const GtMode& gt, const GarageCar& real, const std::string& item) {
    return group_enabled(gt, real, group_of_slider(item));
}

bool setting_page_item_enabled(const GtMode& gt, const GarageCar& real, const std::string& page, int index) {
    static const std::vector<Group> sus = {Group::Spring, Group::Height, Group::Damper, Group::BoundRebound,
                                           Group::BoundRebound, Group::Camber, Group::Toe, Group::Stabilizer};
    static const std::vector<Group> brk = {Group::Brake};
    static const std::vector<Group> drv = {Group::LsdInit, Group::LsdAccel, Group::LsdDecel, Group::Gear};
    static const std::vector<Group> oth = {Group::Aero, Group::Ayc, Group::Ascc, Group::Tcs, Group::Vcd};
    const std::vector<Group>* g = page == "set/adjast_sus"   ? &sus
                                  : page == "set/adjast_brk" ? &brk
                                  : page == "set/adjast_drv" ? &drv
                                  : page == "set/adjast_oth" ? &oth
                                                             : nullptr;
    if (!g || index < 0 || index >= int(g->size())) return true;
    return group_enabled(gt, real, (*g)[size_t(index)]);
}

bool setting_locked(const GtMode& gt, const GarageCar& real, const std::string& cls) {
    struct D { const char* cls; Group g; };
    static const D map[] = {
        {"disable_spring_rate", Group::Spring}, {"disable_ride_height", Group::Height},
        {"disable_damper", Group::Damper},      {"disable_bound", Group::BoundRebound},
        {"disable_rebound", Group::BoundRebound}, {"disable_camber", Group::Camber},
        {"disable_toe", Group::Toe},            {"disable_stabilizer", Group::Stabilizer},
        {"disable_brake_balance", Group::Brake}, {"disable_lsd_initial", Group::LsdInit},
        {"disable_lsd_accel", Group::LsdAccel}, {"disable_lsd_decrease", Group::LsdDecel},
        {"disable_gear_ratio", Group::Gear},    {"disable_aero", Group::Aero},
        {"disable_ayc", Group::Ayc},            {"disable_asm", Group::Ascc},
        {"disable_tcs", Group::Tcs},            {"disable_vcd", Group::Vcd},
    };
    for (const D& d : map)
        if (cls == d.cls) return !group_enabled(gt, real, d.g);
    return false;
}

int setting_goods(const std::string& a) {
    struct G { const char* name; int goods; };
    static const G map[] = {
        {"brc_off", 2}, {"brc_on", 3}, {"brk_normal", 0}, {"brk_racing", 1},
        {"clt_normal", 4}, {"clt_big", 5}, {"clt_twin", 6}, {"clt_triple", 7},
        {"fly_normal", 15}, {"fly_light", 16}, {"fly_crmo", 17}, {"fly_light_crmo", 18},
        {"lsd_normal", 30}, {"lsd_1way", 31}, {"lsd_15way", 33}, {"lsd_2way", 32}, {"lsd_variable", 34},
        {"msn_normal", 19}, {"msn_close", 20}, {"msn_super_close", 21}, {"msn_variable", 22},
        {"shf_normal", 46}, {"shf_propeller", 47}, {"cpu_off", 8}, {"cpu_on", 9},
        {"muf_normal", 36}, {"muf_sports", 37}, {"muf_semi_racing", 38}, {"muf_racing", 39},
        {"nat_off", 40}, {"nat_stage1", 41}, {"nat_stage2", 42}, {"nat_stage3", 43},
        {"asm_off", 83}, {"asm_on", 84}, {"ayc_off", 30}, {"ayc_on", 35}, {"tcs_off", 54}, {"tcs_on", 55},
        {"vcd_off", 85}, {"vcd_on", 86}, {"sus_normal", 50}, {"sus_sports", 51}, {"sus_semi_racing", 52},
        {"sus_racing", 53}, {"coo_normal", 23}, {"coo_1", 24}, {"coo_2", 25}, {"tur_off", 78},
        {"tur_stage1", 79}, {"tur_stage2", 80}, {"tur_stage3", 81}, {"tur_stage4", 82},
        {"tir_f_normal", 56}, {"tir_f_sports", 57}, {"tir_f_racing_sh", 58}, {"tir_f_racing_h", 59},
        {"tir_f_racing_mh", 60}, {"tir_f_racing_m", 61}, {"tir_f_racing_ms", 62}, {"tir_f_racing_s", 63},
        {"tir_f_racing_ss", 64}, {"tir_f_control", 65},
    };
    std::string n = starts(a, "set_") ? a.substr(4) : a;
    bool rear = starts(n, "tir_r_");
    if (rear) n = "tir_f_" + n.substr(6);
    for (const G& g : map)
        if (n == g.name) return rear ? g.goods + 11 : g.goods;
    return -1;
}

int setting_owned_goods(const std::string& a) {
    int g = setting_goods(a);
    return g >= 67 && g <= 76 ? g - 11 : g; // rear tyres: tested on the front goods
}

void save_setting_file(GameState& s, const GarageCar& car) {
    SettingFile f;
    f.car = car.car;
    f.parts = car.parts;
    f.settings = car.settings;
    f.ref_final = car.ref_final;
    f.day = s.day;
    s.setting_files.push_back(std::move(f));
    int n = 0;
    for (const SettingFile& x : s.setting_files) n += x.car == car.car;
    for (auto it = s.setting_files.begin(); n > kSettingFilesPerCar && it != s.setting_files.end();)
        if (it->car == car.car) it = s.setting_files.erase(it), --n;
        else ++it;
}

void write_setting_file(GameState& s, const GarageCar& car, int index, s64 time, int power_ps) {
    SettingFile f;
    f.car = car.car;
    f.parts = car.parts;
    f.settings = car.settings;
    f.ref_final = car.ref_final;
    f.day = s.day;
    f.time = time;
    f.power_ps = power_ps;
    if (index >= 0 && index < int(s.setting_files.size())) {
        f.comment = s.setting_files[size_t(index)].comment;
        s.setting_files.erase(s.setting_files.begin() + index); // rewritten: now the newest
    }
    s.setting_files.push_back(std::move(f));
}

bool delete_setting_file(GameState& s, int index) {
    if (index < 0 || index >= int(s.setting_files.size())) return false;
    s.setting_files.erase(s.setting_files.begin() + index);
    return true;
}

const SettingFile* latest_setting_file(const GameState& s, const std::string& car) {
    for (auto it = s.setting_files.rbegin(); it != s.setting_files.rend(); ++it)
        if (it->car == car) return &*it;
    return nullptr;
}

int apply_setting_file(const SettingFile& f, GarageCar& car) {
    if (f.car != car.car) return -1;
    int miss = 0;
    auto same = [&](int table) {
        int slot = part_slot(table);
        bool ok = slot >= 0 && f.parts[size_t(slot)] == car.parts[size_t(slot)];
        if (!ok) ++miss;
        return ok;
    };
    sim::CarSettings& s = car.settings;
    const sim::CarSettings& t = f.settings;
    if (same(17)) s.gears = t.gears, s.final_drive = t.final_drive, s.gear_slider = t.gear_slider, car.ref_final = f.ref_final;
    if (same(13)) s.front_torque = t.front_torque;
    if (same(1)) s.brake_front = t.brake_front, s.brake_rear = t.brake_rear;
    if (same(5)) s.downforce_front = t.downforce_front, s.downforce_rear = t.downforce_rear;
    if (same(12)) s.turbo = t.turbo;
    if (same(18)) {
        s.camber = t.camber, s.ride_height = t.ride_height, s.toe = t.toe, s.spring = t.spring;
        s.motion_ratio = t.motion_ratio, s.damper = t.damper, s.arb = t.arb;
    }
    if (same(21)) s.lsd = t.lsd;
    if (same(23)) s.ascc = t.ascc;
    if (same(22)) s.tcs = t.tcs;
    return miss;
}

bool setting_attached(const GtMode& gt, const GarageCar& car, const std::string& cls) {
    if (!starts(cls, "attach_")) return false;
    std::string n = cls.substr(7);
    if (n == "sms_off" || n == "sms_on") n = n == "sms_off" ? "asm_off" : "asm_on";
    int goods = setting_goods(n);
    if (goods < 0) return false;
    Goods g = goods_info(goods);
    if (g.table < 0) return false;
    int slot = part_slot(g.table);
    if (slot < 0) return false;
    const ByteSpan* r = gt.db().record(data::TableId(g.table), car.parts[size_t(slot)]);
    int stage = r && r->size() > 0x10 ? (*r)[0x10] : 0;
    // Stage-0 goods also mean "the stock part" (e.g. an NA car's "turbo off").
    return stage == g.stage;
}

} // namespace gt3r::game
