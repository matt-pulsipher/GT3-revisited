// The tick's inputs outside the car record, built as the original builds them; see race_inputs.h.

#include "sim/exact/race_inputs.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace gt3r::sim::exact {

namespace {

using T = ProgramData::Table;

const F kZero{}, kOne = F::raw(0x3F800000), kTen = F::raw(0x41200000), kHundred = F::raw(0x42C80000);

F byte_f(u32 v) { return ee::int_to_f(s32(v)); } // an unsigned byte as a float

// The table header over the arrays the builder filled.
template <size_t N> void set_table(Table<N>& t, s32 n) { t.n = s16(n); }

} // namespace

Statics statics(const ProgramData& d) {
    Statics s;
    const u32 n = d.b(T::Static, 0x0B);
    if (n < 1 || n > 8) throw std::runtime_error("static physics block: " + std::to_string(n) + " rolling-resistance points (1..8)");
    for (u32 t = 0; t < n; ++t) {
        s.rolling_curve.x[t] = byte_f(u32(d.b(T::Static, 0x0C + t)) << 1) * F::raw(0x3E8E38E4); // 1/3.6
        s.rolling_curve.y[t] = byte_f(d.b(T::Static, 0x14 + t)) * F::raw(0x3A83126F);           // 0.001
    }
    set_table(s.rolling_curve, s32(n));
    for (u32 t = 0; t < 16; ++t)
        s.rolling_mu[t] = ee::int_to_f(s16(d.h(T::Static, 0x2C + 2 * t))) * F::raw(0x3C23D70A); // 0.01
    for (u32 k = 0; k < 2; ++k) {
        s.slip_fade[k] = d.b(T::Static, 0x84 + k), s.slip_gain_on[k] = d.b(T::Static, 0x86 + k);
        s.slip_gain_base[k] = d.b(T::Static, 0x88 + k), s.slip_gain_knee[k] = d.b(T::Static, 0x8A + k);
    }
    return s;
}

SurfaceBumps surface_bumps(const ProgramData& d) {
    SurfaceBumps sb{};
    for (u32 t = 0; t < 16; ++t) {
        sb.amp[t] = byte_f(d.b(T::Static, 0x1C + t)) * F::raw(0x3A83126F);                         // 0.001
        sb.freq[t] = F::raw(0x40C90FDB) / (byte_f(d.b(T::Static, 0x4C + t)) * F::raw(0x3DCCCCCD));
        sb.scaled[t] = s8(d.b(T::Static, 0x5C + t));
        sb.mode[t] = s8(d.b(T::Static, 0x6C + t));
        sb.soft[t] = d.w(T::Soft, 4 * t);
    }
    return sb;
}

Slipstream slipstream(const ProgramData& d) {
    Slipstream s;
    const u8 b0 = d.b(T::Static, 0);
    if (b0 != 0) {
        s.drag_share = byte_f(d.b(T::Static, 1)) / byte_f(b0);
        s.downforce_share = byte_f(d.b(T::Static, 2)) / byte_f(b0);
    }
    const u8 up = d.b(T::Static, 5), down = d.b(T::Static, 6);
    s.k_up = up != 0 ? kTen / byte_f(up) : kOne;
    s.k_down = down != 0 ? kTen / byte_f(down) : F::raw(0x3E800000);
    return s;
}

Condition condition(const ProgramData& d) {
    std::array<u8, 9> b{};
    for (u32 k = 0; k < 9; ++k) b[k] = d.b(T::Static, 0xA0 + k);
    return condition_params(b);
}

AidInputs aid_tunables(const ProgramData& d, u8 course_category) {
    AidInputs in;
    in.course_category = course_category;
    in.over_angle = {d.b(T::Static, 0xAD), d.b(T::Static, 0xAE)};
    in.over_gain = {d.b(T::Static, 0xAF), d.b(T::Static, 0xB0)};
    in.dirt_steer = d.b(T::Static, 0xB5);
    in.dirt_cut = {d.b(T::Static, 0xB6), d.b(T::Static, 0xB7)};
    return in;
}

BrakeHeatConsts brake_heat_consts(const ProgramData& d) {
    u32 heat = d.b(T::Static, 0x7C), cool = d.b(T::Static, 0x7D);
    if (heat == 0) heat = 1;
    if (cool == 0) cool = 1;
    BrakeHeatConsts k;
    k.c0 = kOne / (byte_f(heat) * F::raw(0x46192000));                        // 9800
    k.c1 = kOne / ((byte_f(cool) * F::raw(0x3DCCCCCD)) * kHundred);            // 0.1
    k.c2 = byte_f(d.b(T::Static, 0x7E)) * F::raw(0x3E8E38E4);                    // 1/3.6
    k.c3 = byte_f(d.b(T::Static, 0x7F)) * F::raw(0x3C23D70A);                    // 0.01
    return k;
}

PedalCurve pedal_curve_record(u8 type, u8 param) {
    PedalCurve k;
    k.type = s32(type);
    s32 p = param;
    if (type == 1) {
        p = p > 0 ? (p < 101 ? p : 100) : 1;
        const F c = byte_f(u32(p)) * F::raw(0x3C23D70A); // 0.01
        const F one_c = kOne - c;
        k.c = c;
        const F two_c = c + c;
        k.a = (-one_c) / two_c;
        k.b = ((-c) * k.a) * k.a;
    } else if (type == 2) {
        p = p < 10 ? 10 : (p < 101 ? p : 100);
        const F c = byte_f(u32(p)) * F::raw(0x3DCCCCCD); // 0.1
        F c2 = c * c;
        k.c = c;
        F a = c2 - kOne;
        c2 = c2 * F::raw(0x40800000);
        a = a * a;
        k.a = (-a) / c2;
        k.b = (-c) * sqrt(-k.a); // the single-precision square root
    } else {
        k.a = k.b = k.c = kZero;
    }
    return k;
}

SteerFeel steer_feel(const ProgramData& d) {
    const F pct = F::raw(0x3C23D70A); // 0.01
    SteerFeel f{};
    f.lat_gain = byte_f(d.b(T::Controller, 0x34)) * pct;
    f.align_gain = byte_f(d.b(T::Controller, 0x38)) * F::raw(0x3DCCCCCD); // 0.1
    f.curve = pedal_curve_record(d.b(T::Controller, 0x35), d.b(T::Controller, 0x36));
    f.steer_max = byte_f(d.b(T::Controller, 0x3C)) * F::raw(0x3AE4C388); // 0.1 degree
    f.pad_gain = (byte_f(d.b(T::Controller, 0x3A)) * pct) * F::raw(0x39800000);
    f.steer_gain = d.b(T::Controller, 0x3B) == 0 ? kZero : (byte_f(d.b(T::Controller, 0x3B)) * pct) / f.steer_max;
    f.slip_max = byte_f(d.b(T::Controller, 0x3E)) * F::raw(0x3AE4C388);
    f.slip_gain = d.b(T::Controller, 0x3D) == 0 ? kZero : (byte_f(d.b(T::Controller, 0x3D)) * F::raw(0x3C23D70A)) / f.slip_max;
    f.use_x420 = d.b(T::Controller, 0x37);
    f.mode = d.b(T::Controller, 0x39);
    return f;
}

void clamp_effect_block(ProgramData& d) {
    const u32 e = 0; // offsets in the effect block
    // A channel's point counts 4 (curve a, the two halves of curve b), the first y of each half zeroed on
    // channel 0, the speed ramp's threshold at most 50 (x 1/3.6 m/s).
    auto channel = [&](u32 a, bool first) {
        d.poke(T::Effects, a + 3, 4), d.poke(T::Effects, a + 1, 4), d.poke(T::Effects, a + 2, 4);
        if (first) d.poke(T::Effects, a + 8, 0), d.poke(T::Effects, a + 0x10, 0), d.poke(T::Effects, a + 0x18, 0);
        if (d.b(T::Effects, a + 0x21) >= 0x33) d.poke(T::Effects, a + 0x21, 0x32);
    };
    for (u32 k = 0; k < 4; ++k) {
        const u32 cls = e + 0x28 * k;
        channel(cls, true);
        channel(cls + 0xA0, false);
        const u32 base = cls + 0xC0; // channel 1's base: 50 .. 200
        const u8 v = d.b(T::Effects, base);
        if (v < 0x32) d.poke(T::Effects, base, 0x32);
        else if (v >= 0xC9) d.poke(T::Effects, base, 0xC8);
        channel(cls + 0x140, false);
    }
    // A pair of thresholds kept ascending.
    auto order = [&](u32 a, u32 b) {
        const u8 lo = d.b(T::Effects, a), hi = d.b(T::Effects, b);
        if (lo < hi) return;
        if (lo != 0xFF) d.poke(T::Effects, b, u8(lo + 1));
        else if (hi != 0) d.poke(T::Effects, a, u8(hi - 1));
    };
    order(e + 0x1E9, e + 0x1EA), order(e + 0x1F0, e + 0x1F1), order(e + 0x1F7, e + 0x1F8), order(e + 0x201, e + 0x203);
    if (d.b(T::Effects, e + 0x1E2) == 0) d.poke(T::Effects, e + 0x1E2, 1);
}

EffectTables effect_tables(const ProgramData& data) {
    ProgramData d = data;
    clamp_effect_block(d);
    EffectTables t{};
    const F scale = F::raw(0x451F6000); // 2550
    // Curve a's x scale with its selector byte set / clear (the lateral speed, 1/9 km/h, or the slip angle, 0.2 degree), curve
    // b's (the slip speed, 1/18 km/h, or the slip ratio, 1/255); the same per channel.
    constexpr u32 kAx[2] = {0x3DE38E3A, 0x3B64C388}, kBx[2] = {0x3D638E3A, 0x3B808081};
    for (u32 ch = 0; ch < 3; ++ch)
        for (u32 cl = 0; cl < 3; ++cl) {
            const u32 src = 0xA0 * ch + 0x28 * cl;
            auto b = [&](u32 o) { return d.b(T::Effects, src + o); };
            EffectCurves& e = t.set[ch][cl];
            // Curve a.
            F fx = F::raw(kAx[b(0) != 0 ? 0 : 1]);
            F fy = byte_f(b(0x1C)) / scale;
            const u32 na = b(1);
            for (u32 i = 0; i < na && i < 8; ++i) e.a.x[i] = byte_f(b(4 + i)) * fx, e.a.y[i] = byte_f(b(8 + i)) * fy;
            set_table(e.a, s32(na));
            // Curve b, its first half mirrored to negative x.
            fx = F::raw(kBx[b(0x1F) != 0 ? 0 : 1]);
            fy = byte_f(b(0x1D)) / scale;
            const F fy2 = byte_f(b(0x1E)) / scale;
            const u32 n1 = b(2), n2 = b(3);
            for (u32 i = 0; i < n1 && i < 8; ++i)
                e.b.x[i] = (-byte_f(b(0xC + (n1 - 1 - i)))) * fx, e.b.y[i] = byte_f(b(0x10 + (n1 - 1 - i))) * fy;
            for (u32 j = 0; j < n2 && n1 + j < 8; ++j) e.b.x[n1 + j] = byte_f(b(0x14 + j)) * fx, e.b.y[n1 + j] = byte_f(b(0x18 + j)) * fy2;
            set_table(e.b, s32(n1 + n2));
            // Base, the speed ramp's threshold and gain.
            const F k = F::raw(0x3C23D70A); // 0.01
            e.base = byte_f(b(0x20)) * k;
            const F v = byte_f(b(0x21)) * F::raw(0x3E8E38E4); // 1/3.6
            const F div = F::raw(0x42480001);
            e.r0 = v / div;
            const F span = F::raw(0x41DE38E4) - v; // 100 km/h
            e.g = ((byte_f(b(0x22)) * k) * div) / span;
            t.by_lat[ch][cl] = b(0), t.by_slip[ch][cl] = b(0x1F);
        }
    const u32 kerb = 0x1E0;
    t.kerb_scale = byte_f(d.b(T::Effects, kerb + 0x20)) * F::raw(0x3DCCCCCD); // 0.1
    t.kerb_x0 = byte_f(d.b(T::Effects, kerb + 0x21)) * F::raw(0x4031C71D);    // 10 km/h
    t.kerb_y0 = byte_f(d.b(T::Effects, kerb + 0x22)) * F::raw(0x3DCCCCCD);
    t.kerb_x1 = byte_f(d.b(T::Effects, kerb + 0x23)) * F::raw(0x4031C71D);
    t.kerb_y1 = byte_f(d.b(T::Effects, kerb + 0x24)) * F::raw(0x3DCCCCCD);
    return t;
}

std::array<F, 4> sound_scales(const ProgramData& data) {
    ProgramData d = data;
    clamp_effect_block(d);
    const u32 k = 0x1E0;
    return {byte_f(d.b(T::Effects, k + 0x1C)) / kHundred, byte_f(d.b(T::Effects, k + 0x1D)) / kHundred, byte_f(d.b(T::Effects, k + 0x1E)) / kHundred,
            byte_f(d.b(T::Effects, k + 0x1F)) / kTen};
}

GearSettings gear_settings(const ProgramData& d) {
    return {d.b(T::Controller, 0x14), d.b(T::Controller, 0x15), d.b(T::Controller, 0x16), d.b(T::Controller, 0x17)};
}

PadConstants pad_constants(const ProgramData& d) {
    auto b = [&](u32 o) { return d.b(T::Controller, o); };
    PadConstants k{};
    const F sixty = F::raw(d.behaviour().pal_rate() ? 0x42480000 : 0x42700000); // steps a second: 60 (PAL 50)
    // The digital ramps per tick: 100 / (60 x byte), at most 1 (PAL: 50 x byte).
    F* ramps[6] = {&k.throttle_up, &k.throttle_down, &k.brake_up, &k.brake_down, &k.third_up, &k.third_down};
    for (u32 i = 0; i < 6; ++i) {
        F r = kHundred / (byte_f(b(i)) * sixty);
        if (kOne < r) r = kOne;
        *ramps[i] = r;
    }
    const F deg = F::raw(0x3C8EFA35); // pi / 180
    k.analog_jump = byte_f(b(6)) * F::raw(0x3C23D70A); // 0.01
    k.k1 = byte_f(b(0xC));
    k.k2 = byte_f(b(0xD)) * F::raw(0x3DCCCCCD); // 0.1
    k.k3 = byte_f(b(0xE));
    const F out = byte_f(u32(b(0xF)) * 10) * deg, both = byte_f(u32(b(0x11)) * 10) * deg, back = byte_f(u32(b(0x10)) * 10) * deg;
    const F rate = byte_f(u32(b(0x12)) * 10) * deg;
    k.rate_out = {out, both, out};
    k.rate_back = {back, both, back};
    k.rate = {rate, rate, rate};
    k.wheel_slew = byte_f(u32(b(0x23)) * 10) * deg;
    k.wheel_lock = {byte_f(b(0x24)) * deg, byte_f(b(0x25)) * deg, F::raw(0x3E567750), F::raw(0x3EB2B8C2)}; // [2], [3]
    k.wheel_throttle = pedal_curve_record(b(0x2C), b(0x2D));
    k.wheel_brake = pedal_curve_record(b(0x30), b(0x31));
    for (u32 i = 0; i < 16; ++i) k.surface_class[i] = d.b(T::SurfaceClass, i);
    return k;
}

const char* conductor_name(Conductor k) {
    switch (k) {
    case Conductor::SinglePlayer: return "single player";
    case Conductor::TimeTrial: return "time trial";
    case Conductor::License: return "licence";
    case Conductor::FreeRun: return "free run";
    case Conductor::Battle2P: return "battle (split screen)";
    case Conductor::BattleMP: return "battle (i.LINK)";
    case Conductor::MachineTest: return "machine test";
    case Conductor::FreePractice: return "free practice";
    }
    return "?";
}

RaceConfig race_config(ByteSpan ev) {
    if (ev.size() < 0x1C4) throw std::runtime_error("event record: too short for the race configuration");
    RaceConfig c{};
    c[0] = ev[0x198], c[1] = ev[0x199], c[2] = ev[0x19A];
    for (u32 i = 0; i < 0x18; ++i) c[4 + i] = ev[0x19C + i];
    // +0x38 / +0x3A (u16) are set from +0x1E4 / +0x1E6 (the s16 stores).
    c[0x38] = ev[0x1E4], c[0x39] = 0;
    c[0x3A] = ev[0x1E6], c[0x3B] = ev[0x1E7];
    c[0x20] = ev[0x1B4], c[0x22] = ev[0x1B6], c[0x24] = ev[0x1B7], c[0x25] = ev[0x1B8], c[0x26] = ev[0x1B9];
    c[0x28] = ev[0x1BC], c[0x29] = ev[0x1BA], c[0x2A] = ev[0x1BB];
    c[0x2C] = ev[0x1BD], c[0x2D] = ev[0x1BE], c[0x2E] = ev[0x1BF], c[0x2F] = ev[0x1C0], c[0x30] = ev[0x1C1];
    c[0x33] = ev[0x1C2], c[0x34] = ev[0x1C3];
    if (ev[0x1BD] != 0) c[0x32] = 0, c[0x31] = 4;
    return c;
}

DriverOverrides driver_overrides(const RaceConfig& c) {
    DriverOverrides d;
    std::copy_n(c.begin() + kDriverOverridesAt, d.size(), d.begin());
    return d;
}

void clear_rubber_band(RaceConfig& c) {
    c[0x24] = c[0x25] = c[0x26] = 0; // the slow-down
    c[0x28] = c[0x29] = c[0x2A] = 0; // the speed-up
}

bool wear_answer(Conductor k, RaceConfig& c, u8 course_category, s32 battle_wear, Program p) {
    switch (k) {
    case Conductor::SinglePlayer:
    case Conductor::FreePractice: return !dirt_course(course_category); // the wear setting: 1; the initialisation gives 0 on dirt
    case Conductor::Battle2P:
    case Conductor::BattleMP: {
        if (dirt_course(course_category)) return false;
        // With setting 2 when the race info's word is set, else 0; NTSC-J's with the word itself.
        const s32 set = p == Program::NtscJ ? battle_wear : battle_wear != 0 ? 2 : 0;
        if (set == 1) {
            c[0x2F] = 2, c[0x2E] = 9, c[0x30] = 6, c[0x31] = 8, c[0x34] = 5, c[0x2C] = 0x2A, c[0x2D] = 0x1E, c[0x32] = 0, c[0x33] = 0x1E;
        } else if (set == 2) {
            c[0x30] = 6, c[0x2E] = 9, c[0x31] = 4, c[0x33] = 0x0F, c[0x34] = 5, c[0x2C] = 0x15, c[0x2D] = 0x1E, c[0x2F] = 2, c[0x32] = 0;
        } else {
            c[0x2E] = 0, c[0x2F] = 0, c[0x30] = 0, c[0x31] = 0, c[0x33] = 0, c[0x34] = 0, c[0x2C] = 0, c[0x2D] = 0;
        }
        return set != 0;
    }
    default: return false; // the base conductor's wear setting
    }
}

bool rubber_band_answer(Conductor k, RaceConfig& c, s32 battle_catchup, Program p) {
    switch (k) {
    case Conductor::SinglePlayer: return true;
    case Conductor::Battle2P:
    case Conductor::BattleMP:
        if (p == Program::NtscJ) battle_catchup = battle_catchup != 0 ? 2 : 0; // the NTSC-J program
        if (battle_catchup == 1) {
            c[0x28] = 0x0F, c[0x2A] = 100, c[0x29] = 0x14, c[0x22] = 0;
            return true;
        }
        if (battle_catchup == 2) {
            c[0x2A] = 0x14, c[0x29] = 0x0A, c[0x22] = 0, c[0x28] = 0x14;
            return true;
        }
        c[0x2A] = 0, c[0x22] = 0xFF, c[0x28] = 0, c[0x29] = 0;
        return false;
    default: return false;
    }
}

WearParams wear_params(const RaceConfig& c, const ProgramData& d, bool on) {
    WearParams w{};
    if (!on || c[0x2C] == 0) return w;
    if (!(c[0x33] < c[0x2C]) || !(c[0x34] < c[0x2D]) || !(c[0x31] < c[0x33]) || !(c[0x32] < c[0x34]) || !(c[0x2E] < c[0x2D])) return w;
    const F k1 = F::raw(0x461C4000), k2 = F::raw(0x3C23D70A); // 10000, 0.01
    w.max = byte_f(c[0x2C]) * k1;
    w.max_loss = byte_f(c[0x2D]) * k2;
    w.worn = ee::int_to_f(s32(100 - u32(c[0x2E]))) * k2;
    w.fresh = (-byte_f(c[0x2F])) * k1;
    w.fresh_loss = byte_f(c[0x30]) * k2;
    w.knee1 = byte_f(c[0x31]) * k1;
    w.loss1 = byte_f(c[0x32]) * k2;
    w.knee2 = byte_f(c[0x33]) * k1;
    w.loss2 = byte_f(c[0x34]) * k2;
    w.slip_cap = byte_f(d.b(T::Static, 0x8E)) / kHundred;
    return w;
}

bool has_pits(Conductor k) { return k != Conductor::TimeTrial && k != Conductor::License && k != Conductor::FreeRun; }

s32 pit_mode(Conductor k, u8 course_category) {
    if (k == Conductor::License) return 3;
    if (k == Conductor::TimeTrial) return dirt_course(course_category) ? 0 : 3;
    return 0;
}

void hit_course_out(Car& c, Conductor k, s32 kind, F j) {
    if (k != Conductor::TimeTrial && k != Conductor::License) return;
    if (c.index != 0 || c.finished != 0) return;
    const bool out = kind == 0 ? F::raw(0x41200000) < j * c.p.inv_mass : kind == 2;
    if (out) c.course_out = u8(c.course_out | 1);
}

CourseOut course_out_rule(Conductor k, u8 course_category) {
    CourseOut r;
    switch (k) {
    case Conductor::TimeTrial:
    case Conductor::License:
        r.on = true, r.pit = 1, r.soft = dirt_course(course_category) ? 0 : 1;
        break;
    case Conductor::FreeRun:
    case Conductor::MachineTest: r.on = true; break; // the base conductor's answers: 0, 0
    default: break;
    }
    return r;
}

CourseOut course_out_rule(Conductor k, u8 course_category, const AiCourse& ai) {
    CourseOut r = course_out_rule(k, course_category);
    const std::vector<AiPoint>& stop = ai.lines[4];
    r.lap = ai.lap_length;
    r.stretch = stop.size() >= 6;
    if (r.stretch) r.lo = stop[1].dist, r.hi = stop[stop.size() - 2].dist;
    return r;
}

F pit_box_heading(const AiCourse& ai, int box) {
    const std::vector<AiPoint>& stop = ai.lines[4];
    return box >= 0 && size_t(box) < stop.size() ? stop[size_t(box)].heading : F();
}

RubberBand rubber_band_params(const RaceConfig& c, bool on) {
    RubberBand b;
    if (!on) {
        b.global = kOne;
        return b;
    }
    const F pct = F::raw(0x3C23D70A); // 0.01
    b.slow_start = byte_f(c[0x25]);
    b.slow_full = byte_f(c[0x26]) * kTen;
    b.slow = byte_f(c[0x24]) * pct;
    b.fast_start = byte_f(c[0x29]);
    b.fast_full = byte_f(c[0x2A]) * kTen;
    b.fast = byte_f(c[0x28]) * pct;
    b.global = byte_f(c[0x20] != 0 ? c[0x20] : 100) * pct;
    return b;
}

s32 rubber_band_mode(Conductor k, const RaceConfig& c) {
    switch (k) {
    case Conductor::SinglePlayer:
    case Conductor::Battle2P:
    case Conductor::BattleMP: return c[0x22];
    default: return -1;
    }
}

bool rubber_band_cap(Conductor k) { return k == Conductor::BattleMP; }

RaceInputs race_inputs(const ProgramData& d, Conductor k, const RaceConfig& cfg, bool wear_on, u8 course_category) {
    RaceInputs r;
    r.prep.slip = slipstream(d);
    r.prep.wear = wear_params(cfg, d, wear_on);
    r.prep.condition = condition(d);
    r.aids = aid_tunables(d, course_category);
    r.statics = statics(d);
    r.gears = gear_settings(d);
    r.brake_heat = brake_heat_consts(d);
    r.bumps = surface_bumps(d);
    r.feel = steer_feel(d);
    r.effects = effect_tables(d);
    r.sound_scales = sound_scales(d);
    r.pad = pad_constants(d);
    r.course_out = course_out_rule(k, course_category);
    return r;
}

u8 race_course_category(ByteSpan course_record) {
    if (course_record.size() < 9) throw std::runtime_error("course record: too short");
    return course_record[8];
}

} // namespace gt3r::sim::exact
