// The packed car spec (the builder and its per-table helpers); see params_build.h. Integer code: each helper
// copies record bytes into the spec and scales a few with the program's 32-bit multiply and truncating divide.

#include "sim/exact/params_build.h"

#include "game/data/car_database.h"
#include "sim/car_spec.h"

#include <algorithm>

namespace gt3r::sim::exact {

namespace {

using data::TableId;

// A part record as the program reads it (a missing record reads as zeros).
struct Rec {
    const ByteSpan* r = nullptr;
    u8 b(u32 o) const { return r && o < r->size() ? (*r)[o] : 0; }
    u16 h(u32 o) const { return u16(b(o) | (b(o + 1) << 8)); }
    F f(u32 o) const { return F::raw(u32(h(o)) | (u32(h(o + 2)) << 16)); }
    u64 d(u32 o) const { return u64(f(o).bits) | u64(f(o + 4).bits) << 32; }
};

struct S {
    Spec& s;
    u8 b(u32 o) const { return s[o]; }
    u16 h(u32 o) const { return u16(s[o] | (s[o + 1] << 8)); }
    s16 sh(u32 o) const { return s16(h(o)); }
    void b(u32 o, s32 v) { s[o] = u8(v); }
    void h(u32 o, s32 v) { s[o] = u8(v), s[o + 1] = u8(u32(v) >> 8); }
};

s32 mul(s32 a, s32 b) { return s32(u32(a) * u32(b)); } // mult: the low word

// A setting index (1-based) into the record's [lo, hi] range of `steps` steps.
s32 interp(s32 lo, s32 hi, s32 steps, s32 set) {
    if (steps < 2) return lo;
    return lo + mul(hi - lo, set - 1) / (steps - 1);
}

// The setting word's low byte / half (lbu / lhu of the word).
u8 sb(const Equipment& eq, u32 o) { return u8(eq.word(o)); }

void brake(S& s, const Rec& r) {
    s.b(0xDE, r.b(0x11)), s.b(0xDF, r.b(0x12)), s.b(0xE0, r.b(0x13));
}

void brake_controller(const Equipment& eq, S& s, const Rec& r) {
    s.b(0x10A, interp(r.b(0x12), r.b(0x13), r.b(0x11), eq.word(0x11C)));
    s.b(0x10B, interp(r.b(0x16), r.b(0x17), r.b(0x15), eq.word(0x120)));
}

void steer(S& s, const Rec& r) { // lock, then the three 6-point curves A (0xFC), B (0xE2), C (0xEF)
    s.b(0xFC, 6);
    s.b(0x35, r.b(0x11));
    for (u32 i = 0; i < 12; ++i) s.b(0xFD + i, r.b(0x12 + i));
    s.b(0xE2, 6);
    for (u32 i = 0; i < 12; ++i) s.b(0xE3 + i, r.b(0x1E + i));
    s.b(0xEF, 6);
    for (u32 i = 0; i < 12; ++i) s.b(0xF0 + i, r.b(0x2A + i));
}

void chassis(S& s, const Rec& r) {
    s.b(0x2C, r.b(0x10));
    s.b(0x36, r.b(0x12) + 0x7C);
    s.b(0x37, r.b(0x1E) + 0x7C);
    s.h(0x24, r.h(0x18));
    s.h(0x2A, r.h(0x1A));
    s.b(0x2E, r.b(0x1C));
    s.b(0x31, r.b(0x1D));
    s.h(0x1A, r.h(0x14));
}

void engine(S& s, const Rec& r) {
    for (u32 i = 0; i < 16; ++i) s.h(0x82 + 2 * i, r.h(0x18 + 2 * i));
    s.h(0xA2, mul(r.b(0x42), 10));
    s.b(0xC9, r.b(0x43));
    s.b(0x6E, r.b(0x44));
    s.b(0x6F, r.b(0x45));
    s.b(0x70, r.b(0x46));
    for (u32 i = 0; i < 16; ++i) s.b(0x72 + i, r.b(0x47 + i));
    s.b(0x71, r.b(0x57));
}

void drivetrain(S& s, const Rec& r) {
    s.b(0xB4, r.b(0x13));
    s.b(0xB2, r.b(0x14));
    s.b(0xB3, r.b(0x15));
    s.h(0x6C, r.h(0x16));
    s.h(0xD2, r.h(0x18));
    s.h(0xD4, r.h(0x1A));
    s.h(0xCE, r.h(0x1C));
    s.h(0xD0, r.h(0x1E));
    s.h(0x6A, r.h(0x20));
}

void gear(const Equipment& eq, S& s, const Rec& r) {
    for (u32 i = 0; i < 8; ++i) s.h(0xB6 + 2 * i, u16(eq.word(0xF0 + 4 * i)));
    s.h(0xC6, u16(eq.word(0x110)));
    s.b(0xCB, sb(eq, 0x114));
    s.b(0xCA, 0);
    s.b(0xC8, r.b(0x11));
}

void suspension(const Equipment& eq, S& s, const Rec& r) {
    for (u32 i = 0; i < 4; ++i) s.b(0x64 + i, r.b(0x1D + i));
    s.b(0x3C, r.b(0x29));
    s.b(0x4C, r.b(0x2A));
    for (u32 i = 0; i < 4; ++i) s.b(0x122 + i, r.b(0x60 + i));
    for (u32 i = 0; i < 4; ++i) s.b(0x12A + i, r.b(0x64 + i));
    for (u32 i = 0; i < 4; ++i) s.b(0x126 + i, r.b(0x68 + i));
    for (u32 i = 0; i < 4; ++i) s.b(0x12E + i, r.b(0x6C + i));
    s.b(0x62, r.b(0x5E)), s.b(0x63, r.b(0x5F));
    s.b(0x3D, r.b(0x5C)), s.b(0x4D, r.b(0x5D));
    s.b(0x3E, r.b(0x70)), s.b(0x40, r.b(0x71)), s.b(0x42, r.b(0x72)), s.b(0x44, r.b(0x73));
    s.b(0x4E, r.b(0x74)), s.b(0x50, r.b(0x75)), s.b(0x52, r.b(0x76)), s.b(0x54, r.b(0x77));
    s.b(0x46, r.b(0x78)), s.b(0x56, r.b(0x79)), s.b(0x47, r.b(0x7A)), s.b(0x57, r.b(0x7B));
    s.b(0x5A, sb(eq, 0x144)), s.b(0x5B, sb(eq, 0x148));
    s.b(0x5C, sb(eq, 0x14C)), s.b(0x5D, sb(eq, 0x150));
    s.b(0x60, sb(eq, 0x154)), s.b(0x61, sb(eq, 0x158));
    s.b(0x3A, sb(eq, 0x15C)), s.b(0x4A, sb(eq, 0x160));
    s.b(0x5E, sb(eq, 0x164)), s.b(0x5F, sb(eq, 0x168));
    // Dampers: the front's second stage reuses the first stage's settings (0x16C / 0x170).
    s.b(0x3F, interp(r.b(0x2C), r.b(0x2D), r.b(0x2B), eq.word(0x16C)));
    s.b(0x41, interp(r.b(0x2F), r.b(0x30), r.b(0x2B), eq.word(0x170)));
    s.b(0x43, interp(r.b(0x33), r.b(0x34), r.b(0x32), eq.word(0x16C)));
    s.b(0x45, interp(r.b(0x36), r.b(0x37), r.b(0x32), eq.word(0x170)));
    s.b(0x4F, interp(r.b(0x3A), r.b(0x3B), r.b(0x39), eq.word(0x17C)));
    s.b(0x51, interp(r.b(0x3D), r.b(0x3E), r.b(0x39), eq.word(0x180)));
    s.b(0x53, interp(r.b(0x41), r.b(0x42), r.b(0x40), eq.word(0x184)));
    s.b(0x55, interp(r.b(0x44), r.b(0x45), r.b(0x40), eq.word(0x188)));
    s.b(0x3B, interp(r.b(0x48), r.b(0x49), r.b(0x47), eq.word(0x18C)));
    s.b(0x4B, interp(r.b(0x4C), r.b(0x4D), r.b(0x4B), eq.word(0x190)));
}

void lsd(const Equipment& eq, S& s, const Rec& r) {
    s.b(0xD6, r.b(0x11)), s.b(0xD7, r.b(0x1B));
    for (u32 i = 0; i < 6; ++i) s.b(0xD8 + i, sb(eq, 0x194 + 4 * i));
}

// One tyre block (0x70 bytes at `o`) from a compound record c (table 27) and a force-vol record f (table 26).
void tire_block(S& s, u32 o, const Rec& c, const Rec& f) {
    s.b(o + 0x00, c.b(0x08));
    s.b(o + 0x01, c.b(0x09));
    s.b(o + 0x02, 8);
    s.b(o + 0x6B, c.b(0x52));
    for (u32 i = 0; i < 16; ++i) s.b(o + 0x03 + i, c.b(0x12 + i));
    s.b(o + 0x13, 6);
    for (u32 i = 0; i < 18; ++i) s.b(o + 0x14 + i, c.b(0x22 + i));
    s.b(o + 0x26, 6);
    for (u32 i = 0; i < 18; ++i) s.b(o + 0x27 + i, c.b(0x34 + i));
    s.b(o + 0x39, 4);
    for (u32 i = 0; i < 8; ++i) s.b(o + 0x3A + i, c.b(0x0A + i));
    s.b(o + 0x5E, 6);
    for (u32 i = 0; i < 12; ++i) s.b(o + 0x5F + i, c.b(0x46 + i));
    for (u32 i = 0; i < 10; ++i) s.b(o + 0x4B + i, f.b(0x08 + i));
    s.b(o + 0x57, f.b(0x12)), s.b(o + 0x58, f.b(0x13));
    s.b(o + 0x5D, 0);
    s.b(o + 0x59, f.b(0x14));
    s.b(o + 0x5B, 0), s.b(o + 0x5C, 0);
}

// The three compound blocks of a tyre part (table 28 front, 29 rear) and its size (table 25).
void tire(const data::CarDatabase& db, S& s, const Rec& r, bool rear) {
    for (u32 k = 0; k < 3; ++k)
        tire_block(s, (rear ? 0x292 : 0x142) + 0x70 * k, Rec{db.record(TableId::TireCompound, r.d(0x20 + 8 * k))},
                   Rec{db.record(TableId::TireForceVol, r.d(0x40 + 8 * k))});
    const Rec z{db.record(TableId::TireSize, r.d(0x18))};
    const u32 a = rear ? 1 : 0;
    s.b(0x11C + a, z.b(0x08));
    s.b(0x11A + a, z.b(0x0A));
    s.b(0x11E + a, z.b(0x09));
    if (!rear) s.b(0x120, 4), s.b(0x121, 4); // the camber curves' point counts
}

void lightweight(S& s, const Rec& r) {
    s.h(0x2A, mul(s.sh(0x2A), r.h(0x12)) / 1000);
    s.b(0x2E, mul(s.b(0x2E), r.b(0x11)) / 100);
}

void body(const Equipment& eq, S& s, const Rec& r) {
    s.h(0x2A, mul(s.sh(0x2A), r.h(0x30)) / 1000);
    s.b(0x2E, mul(s.b(0x2E), r.b(0x38)) / 100);
    s.b(0x32, r.b(0x39));
    s.h(0x26, r.h(0x32));
    s.h(0x28, r.h(0x34));
    s.b(0x33, sb(eq, 0x124));
    s.b(0x34, sb(eq, 0x128));
    s.b(0x68, r.b(0x3D));
    s.b(0x69, r.b(0x3E));
    s.h(0x20, ee::to_int(r.f(0x28)));
    s.h(0x22, ee::to_int(r.f(0x2C)));
    s.h(0x1C, ee::to_int(r.f(0x20)));
    s.h(0x1E, ee::to_int(r.f(0x24)));
    s.b(0x38, r.b(0x3F));
    s.b(0x39, r.b(0x40));
    s.h(0x16, ee::to_int(r.f(0x44) * F::raw(0x461C4000))); // x 10000
}

// The power factors (0xA4 per mille, 0xA6 percent) times the part's two percentages: port polish, displacement, computer,
// muffler, intercooler.
void power_factors(S& s, const Rec& r) {
    s.h(0xA4, mul(s.h(0xA4), r.b(0x11)) / 100);
    s.b(0xA6, mul(s.b(0xA6), r.b(0x12)) / 100);
}

// Engine balance / NA tune: +0x11 rpm/100 onto every curve point, +0x12 onto the rev limit, then the
// factors from +0x13 / +0x14.
void rpm_tune(S& s, const Rec& r) {
    for (u32 i = 0; i < 16; ++i) s.b(0x72 + i, s.b(0x72 + i) + r.b(0x11));
    s.b(0x6F, s.b(0x6F) + r.b(0x12));
    s.h(0xA4, mul(s.h(0xA4), r.b(0x13)) / 100);
    s.b(0xA6, mul(s.b(0xA6), r.b(0x14)) / 100);
}

void turbo(const Equipment& eq, S& s, const Rec& r) {
    s.h(0xA4, mul(s.h(0xA4), r.h(0x1C)) / 100);
    const u8 rev = s.b(0x6F), upshift = s.b(0x70);
    s.b(0xA6, mul(s.b(0xA6), r.b(0x1B)) / 100);
    s.b(0xA7, r.b(0x11));
    s.b(0x6F, rev + r.b(0x18));
    s.b(0x70, upshift + r.b(0x19));
    s.b(0xAC, sb(eq, 0x12C)), s.b(0xAA, sb(eq, 0x130)), s.b(0xAE, sb(eq, 0x134));
    s.b(0xAD, sb(eq, 0x138)), s.b(0xAB, sb(eq, 0x13C)), s.b(0xAF, sb(eq, 0x140));
}

// Flywheel / clutch: friction 0x6C, inertia 0x6A, the axle words 0xD2 / 0xD4 in percent; the clutch
// also sets 0xCC.
void flywheel(S& s, const Rec& r) {
    s.h(0x6C, mul(s.h(0x6C), r.b(0x11)) / 100);
    s.h(0x6A, mul(s.h(0x6A), r.b(0x12)) / 100);
    s.h(0xD2, mul(s.sh(0xD2), r.b(0x13)) / 100);
    s.h(0xD4, mul(s.sh(0xD4), r.b(0x14)) / 100);
}

void prop_shaft(S& s, const Rec& r) {
    s.h(0x6C, mul(s.h(0x6C), r.b(0x11)) / 100);
    s.h(0xD2, mul(s.sh(0xD2), r.b(0x12)) / 100);
    s.h(0xD4, mul(s.sh(0xD4), r.b(0x13)) / 100);
    s.h(0xCE, mul(s.sh(0xCE), r.b(0x14)) / 100);
    s.h(0xD0, mul(s.sh(0xD0), r.b(0x15)) / 100);
}

void ascc(const Equipment& eq, S& s, const Rec& r) {
    s.b(0x10C, interp(r.b(0x14), r.b(0x15), r.b(0x11), eq.word(0x1BC)));
    s.b(0x10D, r.b(0x13));
    s.b(0x111, interp(r.b(0x19), r.b(0x1A), r.b(0x18), eq.word(0x1C0)));
    s.b(0x114, interp(r.b(0x1F), r.b(0x20), r.b(0x1E), eq.word(0x1C4)));
    s.b(0x115, r.b(0x22));
    s.b(0x116, interp(r.b(0x24), r.b(0x25), r.b(0x23), eq.word(0x1C8)));
    s.b(0x109, (r.b(0x17) & 3) | (r.b(0x16) & 3) << 2 | (r.b(0x1D) & 3) << 4 | (r.b(0x1C) & 3) << 6);
}

void tcs(const Equipment& eq, S& s, const Rec& r) {
    s.b(0x110, r.b(0x13));
    s.b(0x10E, r.b(0x14));
    s.b(0x10F, r.b(0x17));
    s.b(0x117, interp(r.b(0x15), r.b(0x16), r.b(0x12), eq.word(0x1B0)));
}

// The id of the first tyre part of table t fitted to this car with this stage byte, 0 when none.
u64 tire_for_stage(const data::CarDatabase& db, TableId t, u64 car, s32 stage) {
    for (const ByteSpan& r : db.table(t).records)
        if (rd64(r, 8) == car && r[0x10] == u8(stage)) return rd64(r, 0);
    return 0;
}

} // namespace

Equipment make_equipment(const data::CarDatabase& db, const std::string& car_label, const std::array<u64, 27>& parts,
                         const CarSettings& c) {
    Equipment eq;
    eq.car = db::id_hash(car_label);
    for (size_t i = 0; i < parts.size(); ++i) eq.part[size_t(data::kCarPartTables[i])] = parts[i];
    auto set = [&](u32 off, s32 v) { eq.setting[(off - 0xF0) / 4] = v; };
    for (u32 i = 0; i < 8; ++i) set(0xF0 + 4 * i, u16(c.gears[i])); // the words hold the u16 ratio (-999 = 0xFC19)
    set(0x110, c.final_drive), set(0x114, c.gear_slider), set(0x118, c.front_torque);
    set(0x11C, c.brake_front), set(0x120, c.brake_rear);
    set(0x124, c.downforce_front), set(0x128, c.downforce_rear);
    for (u32 i = 0; i < 6; ++i) set(0x12C + 4 * i, c.turbo[i]);
    for (u32 i = 0; i < 2; ++i) {
        set(0x144 + 4 * i, c.camber[i]), set(0x14C + 4 * i, c.ride_height[i]), set(0x154 + 4 * i, c.toe[i]);
        set(0x15C + 4 * i, c.spring[i]), set(0x164 + 4 * i, c.motion_ratio[i]), set(0x18C + 4 * i, c.arb[i]);
    }
    for (u32 i = 0; i < 8; ++i) set(0x16C + 4 * i, c.damper[i]);
    for (u32 i = 0; i < 6; ++i) set(0x194 + 4 * i, c.lsd[i]);
    set(0x1B0, c.tcs);
    for (u32 i = 0; i < 4; ++i) set(0x1BC + 4 * i, c.ascc[i]);
    return eq;
}

Equipment equipment_image(ByteSpan eq) {
    if (eq.size() < kEquipmentImageSize) throw FormatError("car equipment record too short");
    // The parts table of each EQ slot (EQ +0x18 + 8 k; race_setup's eq_slot_for with Car::parts' tables).
    static constexpr std::array<int, 26> kSlotTables = {0, 1, 3, 6, 13, 17, 18, 21, 28, 29, 2, 4, 5,
                                                        7, 8, 9, 10, 11, 12, 14, 15, 16, 20, 19, 23, 22};
    Equipment e;
    e.car = rd64(eq, 0);
    for (size_t k = 0; k < kSlotTables.size(); ++k) e.part[size_t(kSlotTables[k])] = rd64(eq, 0x18 + 8 * k);
    for (size_t k = 0; k < e.setting.size(); ++k) e.setting[k] = s32(rd32(eq, 0xF0 + 4 * k));
    return e;
}

Equipment stock_equipment(const data::CarDatabase& db, const std::string& car_label, const std::array<u64, 27>& parts) {
    CarSettings c = stock_settings(db, car_label, parts);
    // Max with the low bound, then min with the high bound (so an empty range gives its high bound).
    auto clamp = [](int v, int lo, int hi) { return std::min(std::max(v, lo), hi); };
    std::array<u64, 30> part{};
    for (size_t i = 0; i < parts.size(); ++i) part[size_t(data::kCarPartTables[i])] = parts[i];
    auto rec = [&](TableId t) { return Rec{db.record(t, part[size_t(t)])}; };
    if (const Rec g = rec(TableId::Gear); g.r) { // (a computed box's words come from stock_settings' generator)
        const int fd = clamp(g.h(0x22), g.h(0x26), g.h(0x24)), sl = clamp(g.b(0x29), g.b(0x2A), g.b(0x2B));
        if (fd != c.final_drive || sl != c.gear_slider) {
            c.final_drive = fd, c.gear_slider = sl;
            if (g.b(0x28)) apply_gear_slider(db, parts, c);
        }
    }
    if (const Rec d = rec(TableId::Drivetrain); d.r) c.front_torque = clamp(d.b(0x13), d.b(0x11), d.b(0x12));
    if (const Rec b = rec(TableId::BrakeController); b.r)
        c.brake_front = clamp(b.b(0x14), 1, b.b(0x11)), c.brake_rear = clamp(b.b(0x14), 1, b.b(0x15));
    if (const Rec b = rec(TableId::RacingModify); b.r)
        c.downforce_front = clamp(b.b(0x13), b.b(0x11), b.b(0x12)), c.downforce_rear = clamp(b.b(0x3C), b.b(0x3A), b.b(0x3B));
    if (const Rec su = rec(TableId::Suspension); su.r) {
        c.camber = {clamp(su.b(0x13), su.b(0x11), su.b(0x12)), clamp(su.b(0x16), su.b(0x14), su.b(0x15))};
        c.ride_height = {clamp(su.h(0x54), su.h(0x50), su.h(0x52)), clamp(su.h(0x5A), su.h(0x56), su.h(0x58))};
        c.spring = {clamp(su.b(0x23), su.b(0x21), su.b(0x22)), clamp(su.b(0x26), su.b(0x24), su.b(0x25))};
        const int f1 = su.b(0x2B), f2 = su.b(0x32), r1 = su.b(0x39), r2 = su.b(0x40);
        c.damper = {clamp(su.b(0x2E), 1, f1), clamp(su.b(0x31), 1, f1), clamp(su.b(0x35), 1, f2), clamp(su.b(0x38), 1, f2),
                    clamp(su.b(0x3C), 1, r1), clamp(su.b(0x3F), 1, r1), clamp(su.b(0x43), 1, r2), clamp(su.b(0x46), 1, r2)};
        c.arb = {clamp(su.b(0x4A), 1, su.b(0x47)), clamp(su.b(0x4E), 1, su.b(0x4B))};
    }
    if (const Rec l = rec(TableId::Lsd); l.r)
        c.lsd = {clamp(l.b(0x12), l.b(0x13), l.b(0x14)), clamp(l.b(0x1C), l.b(0x1D), l.b(0x1E)), clamp(l.b(0x15), l.b(0x16), l.b(0x17)),
                 clamp(l.b(0x1F), l.b(0x20), l.b(0x21)), clamp(l.b(0x18), l.b(0x19), l.b(0x1A)), clamp(l.b(0x22), l.b(0x23), l.b(0x24))};
    if (const Rec a = rec(TableId::Ascc); a.r)
        c.ascc = {clamp(a.b(0x12), 1, a.b(0x11)), clamp(a.b(0x1B), 1, a.b(0x18)), clamp(a.b(0x21), 1, a.b(0x1E)), clamp(a.b(0x26), 1, a.b(0x23))};
    if (const Rec t = rec(TableId::Tcsc); t.r) c.tcs = clamp(t.b(0x11), 1, t.b(0x12));
    return make_equipment(db, car_label, parts, c);
}

Equipment variant_equipment(const data::CarDatabase& db, const data::CarVariant& v) {
    Equipment eq = stock_equipment(db, v.car, v.parts);
    const Rec r{&v.bytes};
    eq.car = r.d(8);
    auto set = [&](u32 off, s32 x) { eq.setting[(off - 0xF0) / 4] = x; };
    set(0x110, r.h(0x10C)), set(0x114, r.b(0xEE));
    set(0x194, r.b(0xE8)), set(0x19C, r.b(0xE9)), set(0x1A4, r.b(0xEA)), set(0x198, r.b(0xEB)), set(0x1A0, r.b(0xEC)), set(0x1A8, r.b(0xED));
    set(0x124, r.b(0xF8)), set(0x128, r.b(0xF9));
    for (u32 i = 0; i < 8; ++i) set(0x16C + 4 * i, r.b(0xF0 + i));
    set(0x144, r.b(0xFA)), set(0x148, r.b(0xFB)), set(0x154, r.b(0xFC)), set(0x158, r.b(0xFD)), set(0x18C, r.b(0xFE)), set(0x190, r.b(0xFF));
    set(0x14C, r.h(0x100)), set(0x150, r.h(0x102)), set(0x15C, r.h(0x104)), set(0x160, r.h(0x106));
    set(0x1B0, r.b(0x108)), set(0x1B8, r.h(0x10A)), set(0x1BC, r.b(0x109));
    return eq;
}

bool course_tires(const data::CarDatabase& db, Equipment& eq, u8 category) {
    auto stage = [&](TableId t) { return s32(Rec{db.record(t, eq.part[size_t(t)])}.b(0x10)); };
    const s32 front = stage(TableId::FrontTire), rear = stage(TableId::RearTire);
    s32 want = -1;
    if (category == 2) { // unless both are dirt tyres already
        if (front != 12 || rear != 12) want = 12;
    } else if (category == 1) { // unless both are wet tyres already: the front's stage + 13
        auto wet = [](s32 s) { return u32(s - 13) < 12; };
        if (!(wet(front) && wet(rear)) && front < 12) want = front + 13;
    }
    if (want < 0) return false;
    // Both tyres or neither.
    const u64 f = tire_for_stage(db, TableId::FrontTire, eq.car, want);
    const u64 r = f ? tire_for_stage(db, TableId::RearTire, eq.car, want) : 0;
    if (!r) return false;
    eq.part[size_t(TableId::FrontTire)] = f, eq.part[size_t(TableId::RearTire)] = r;
    return true;
}

void build_spec(const data::CarDatabase& db, const ProgramData& d, Equipment& eq, int flag, int mode, bool tire_override, Spec& spec) {
    spec.fill(0);
    S s{spec};
    if (tire_override) {
        s32 stage = -1;
        if (mode == 2) stage = 0xC;
        else if (mode != 1 && flag != 0) stage = 0xB;
        if (stage != -1) {
            if (u64 id = tire_for_stage(db, TableId::FrontTire, eq.car, stage)) eq.part[size_t(TableId::FrontTire)] = id;
            if (u64 id = tire_for_stage(db, TableId::RearTire, eq.car, stage)) eq.part[size_t(TableId::RearTire)] = id;
        }
    }
    s.b(0xA6, 100);
    s.b(0xA7, 0);
    s.h(0xA4, 1000);
    auto rec = [&](TableId t) { return Rec{db.record(t, eq.part[size_t(t)])}; };
    auto has = [&](TableId t) { return eq.part[size_t(t)] != 0; };
    brake(s, rec(TableId::Brake));
    brake_controller(eq, s, rec(TableId::BrakeController));
    steer(s, rec(TableId::Steer));
    chassis(s, rec(TableId::Chassis));
    engine(s, rec(TableId::Engine));
    drivetrain(s, rec(TableId::Drivetrain));
    gear(eq, s, rec(TableId::Gear));
    suspension(eq, s, rec(TableId::Suspension));
    lsd(eq, s, rec(TableId::Lsd));
    tire(db, s, rec(TableId::FrontTire), false);
    tire(db, s, rec(TableId::RearTire), true);
    if (has(TableId::Lightweight)) lightweight(s, rec(TableId::Lightweight));
    body(eq, s, rec(TableId::RacingModify));
    if (has(TableId::PortPolish)) power_factors(s, rec(TableId::PortPolish));
    if (has(TableId::EngineBalance)) rpm_tune(s, rec(TableId::EngineBalance));
    if (has(TableId::Displacement)) power_factors(s, rec(TableId::Displacement));
    if (has(TableId::Computer)) power_factors(s, rec(TableId::Computer));
    if (has(TableId::NaTune)) rpm_tune(s, rec(TableId::NaTune));
    if (has(TableId::TurbineKit)) turbo(eq, s, rec(TableId::TurbineKit));
    if (has(TableId::Flywheel)) flywheel(s, rec(TableId::Flywheel));
    if (has(TableId::Clutch)) flywheel(s, rec(TableId::Clutch)), s.b(0xCC, rec(TableId::Clutch).b(0x15));
    if (has(TableId::PortPolish)) power_factors(s, rec(TableId::PortPolish)); // called twice
    if (has(TableId::PropellerShaft)) prop_shaft(s, rec(TableId::PropellerShaft));
    if (has(TableId::Muffler)) power_factors(s, rec(TableId::Muffler));
    if (has(TableId::Intercooler)) power_factors(s, rec(TableId::Intercooler));
    if (has(TableId::Ascc)) ascc(eq, s, rec(TableId::Ascc));
    if (has(TableId::Tcsc)) tcs(eq, s, rec(TableId::Tcsc));
    s.b(0x30, s.b(0x2E) >> 1);
    s.b(0x2F, s.b(0x2E));
    for (s32 i = s.b(0xC8) + 1; i < 8; ++i) s.h(0xB6 + 2 * u32(i), -1);
    for (s32 i = s.b(0x71); i < 16; ++i) s.b(0x72 + u32(i), 0xFF), s.h(0x82 + 2 * u32(i), 0);
    if (eq.word(0x1B8) != 0) s.h(0xA2, u16(eq.word(0x1B8)));
    s.b(0x0F, 9);
    // The fix-ups [physics §6.3]
    for (u32 o : {0x1Cu, 0x1Eu})
        if (s.sh(o) == 0) s.h(o, s.sh(0x1A) / 2);
    s.b(0x2D, Rec{db.record(TableId::RacingModify, eq.part[size_t(TableId::RacingModify)])}.b(0x10) != 0);
    s.b(0x10, 2);
    auto steer_x = [&](u32 i) { return d.b(ProgramData::Table::SpecCurves, i); };
    auto steer_y = [&](u32 i) { return d.b(ProgramData::Table::SpecCurves, 8 + i); };
    if (s.b(0xFC) == 0) {
        s.b(0xFC, 6);
        for (u32 i = 0; i < 6; ++i) s.b(0xFD + i, steer_x(i)), s.b(0x103 + i, steer_y(i));
    }
    if (s.b(0xF5) == 0) { // curve C's last x
        s.b(0xEF, 6);
        for (u32 i = 0; i < 6; ++i) s.b(0xF0 + i, steer_x(i)), s.b(0xF6 + i, steer_y(i));
    }
    if (mode == 2)
        for (u32 o : {0x10Eu, 0x10Fu, 0x110u, 0x109u, 0x10Cu, 0x10Du, 0x111u, 0x114u, 0x115u, 0x116u}) s.b(o, 0);
    if (s.b(0x112) == 0) s.b(0x112, 100);
    if (s.b(0x113) == 0) s.b(0x113, 100);
    if (s.b(0x117) == 100) s.b(0x110, 0);
    s.b(0x12, 1);
    for (u32 i = 0; i < 16; ++i) s.b(0x132 + i, d.b(ProgramData::Table::SpecSurfaceClass, i));
    // Tyre blocks: front block 0's wet surfaces 12..14 default to 0, 1, 9; every block's empty bytes default.
    if (s.b(0x142 + 0x57) == 0) s.b(0x142 + 0x57, s.b(0x142 + 0x4B));
    if (s.b(0x142 + 0x58) == 0) s.b(0x142 + 0x58, s.b(0x142 + 0x4C));
    if (s.b(0x142 + 0x59) == 0) s.b(0x142 + 0x59, s.b(0x142 + 0x54));
    for (u32 k = 0; k < 6; ++k) {
        const u32 o = 0x142 + 0x70 * k;
        if (s.b(o + 1) == 0) s.b(o + 1, 0x2D);
        if (s.b(o + 0x5E) == 0) {
            s.b(o + 0x5E, 6);
            for (u32 i = 0; i < 6; ++i)
                s.b(o + 0x5F + i, d.b(ProgramData::Table::SpecCurveB, i)), s.b(o + 0x65 + i, d.b(ProgramData::Table::SpecCurveB, 8 + i));
        }
        for (u32 i = 0; i < 16; ++i)
            if (s.b(o + 0x4B + i) == 0) s.b(o + 0x4B + i, s.b(0x142 + 0x4B + i)); // from front block 0
    }
}

} // namespace gt3r::sim::exact
