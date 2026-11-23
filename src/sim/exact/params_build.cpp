// The converter 0x1DBFD8 (spec -> P) and its sub-builders; see params_build.h. Each statement is one ee::F operation, in
// the program's order; offsets are P's (P = car + 0x10), the engine block E = P + 0x138, the
// driveline block D = P + 0x1F8, a tyre record R = P + 0x398 + 0x1B4 r, a suspension block S = P + 0x2B8 + 0x44 axle, a spec
// tyre block B = spec + 0x142 + 0x70 k [physics §6.4-§6.5].

#include "sim/exact/params_build.h"

#include "base/ee_double.h"
#include "sim/exact/build_math.h"
#include "base/ee_math.h"
#include "sim/exact/race_inputs.h"

namespace gt3r::sim::exact {

namespace {

using ee::D;
using ee::int_to_f;
using ee::to_int;

// Program constants (the read-only words the converter reads).
constexpr F k0 = F::raw(0x00000000), k1 = F::raw(0x3F800000), kHalf = F::raw(0x3F000000), kQuarter = F::raw(0x3E800000);
constexpr F k001 = F::raw(0x3C23D70A); // 0.01
constexpr F k5 = F::raw(0x40A00000), k10 = F::raw(0x41200000), k20 = F::raw(0x41A00000), k60 = F::raw(0x42700000);
constexpr F k100 = F::raw(0x42C80000), k200 = F::raw(0x43480000), k255 = F::raw(0x437F0000);
constexpr F k10000 = F::raw(0x461C4000), kToKmh = F::raw(0x40666666); // 3.6

// The constants 2002 Tokyo-Geneva's compiler folded toward zero (Rule::SoftDouble, build_math.h) [physics §14.2]: GT3's and
// that build's, each read from the build's routine (Europe's addresses: the converter 0x1D7260, the engine 0x1D9AC8, the
// driveline 0x1DCCD8, the layout 0x1DD860, the tire sizes 0x1DD7D0, the springs 0x1D69E8, the tire curves 0x1D6F40, the
// steering curves 0x1D64B0, the top speed 0x1D6550; Asia's hold the same values).
struct K {
    F milli, tenth, g, deg, quarter_pi, two_pi, rpm, kmh, fifth, twentieth, k98, k980, k9800, k7162, ten_kmh, awd3_gain;
    F half_pi_range, half_pi_b, camber_step;
    explicit K(bool z) {
        auto v = [z](u32 x, u32 y) { return F::raw(z ? y : x); };
        milli = v(0x3A83126F, 0x3A83126E);         // 0.001
        tenth = v(0x3DCCCCCD, 0x3DCCCCCC);         // 0.1
        g = v(0x411CCCCD, 0x411CCCCC);             // 9.8
        deg = v(0x3C8EFA35, 0x3C8EFA34);           // pi / 180
        quarter_pi = v(0x3F490FDB, 0x3F490FD9);    // pi / 4 (Europe 0x333F88, 0x333FD4)
        two_pi = v(0x40C90FDB, 0x40C90FDA);        // 2 pi
        rpm = v(0x3DD67750, 0x3DD6774E);           // 2 pi / 60
        kmh = v(0x3E8E38E4, 0x3E8E38E3);           // 1 / 3.6
        fifth = v(0x3E4CCCCD, 0x3E4CCCCC);         // 0.2 (the steering curves' degrees)
        twentieth = v(0x3D4CCCCD, 0x3D4CCCCC);     // 0.05 (the sidewall)
        k98 = v(0x42C40000, 0x42C3FFFF);           // 98 (Europe 0x333F94)
        k980 = v(0x44750000, 0x4474FFFE);          // 980 (0x333F4C)
        k9800 = v(0x46192000, 0x46191FFF);         // 9800 (0x333F98)
        k7162 = v(0x44330CCD, 0x44330CCC);         // 716.2
        ten_kmh = v(0x4031C71D, 0x4031C71B);       // 10 / 3.6
        awd3_gain = v(0x3EB851EB, 0x3EB851ED);     // 0.36 (0x334160)
        half_pi_range = v(0x3FC90FDB, 0x3FC90FDA); // curve A's range cap (0x333F68)
        half_pi_b = v(0x3FC90FDB, 0x3FC90FD9);     // curve B's span (0x333F6C)
        camber_step = v(0x3D8EFA35, 0x3D8EFA34);   // 4 degrees (0x333F90)
    }
};

// The static block 0x2D28B0's bytes the converter reads: +3 / +4 the mass added to every car (65 + 55 kg on the disc), +7 the
// pitch-tyre byte, +8 the power cap's soft share, +0x8C / +0x8D the load-curve knees, +0x8F + k surface 11's grip cap (x 0.01)
// per compound.

// The spec and P as the program addresses them (little-endian images).
struct Img {
    u8* d;
    u8 b(u32 o) const { return d[o]; }
    s8 sb(u32 o) const { return s8(d[o]); }
    u16 h(u32 o) const { return u16(d[o] | (d[o + 1] << 8)); }
    s16 sh(u32 o) const { return s16(h(o)); }
    u32 w(u32 o) const { return u32(h(o)) | (u32(h(o + 2)) << 16); }
    F f(u32 o) const { return F::raw(w(o)); }
    void set8(u32 o, s32 v) { d[o] = u8(v); }
    void set16(u32 o, s32 v) { d[o] = u8(v), d[o + 1] = u8(u32(v) >> 8); }
    void set32(u32 o, u32 v) { set16(o, s32(v & 0xFFFF)), set16(o + 2, s32(v >> 16)); }
    void set(u32 o, F v) { set32(o, v.bits); }
};

struct Ctx {
    Img s, p;
    const ConvertInputs& in;
    Condition condition; // the engine-condition limits (0x2D2B00, 0x1DAB50 from the static block)
    Behaviour h = in.data->behaviour();
    K k{toward_zero(h)};
    u32 addr(u32 off) const { return in.base + off; } // a pointer into P as the tables store it
    u32 off(u32 addr) const { return addr - in.base; }
    u8 st(u32 o) const { return in.data->b(ProgramData::Table::Static, o); }
};

F cvt(s32 v) { return int_to_f(v); }

// 0x1D8DD0: a curve table {s16 n; u8, u8 = 0; F* x; F* y} with its arrays in P.
void table_init(Ctx& c, u32 t, s32 n, u32 x, u32 y) {
    c.p.set16(t, n);
    c.p.set32(t + 4, c.addr(x));
    c.p.set32(t + 8, c.addr(y));
    c.p.set8(t + 3, 0);
    c.p.set8(t + 2, 0);
}

// 0x1D8FB8: the curve at v, clamped at both ends; the segment found by a linear walk from the second point.
template <class X, class Y> F lookup(s32 n, X x, Y y, F v) {
    if (v <= x(0)) return y(0);
    if (x(n - 1) <= v) return y(n - 1);
    const u32 last = u32(n - 1); // the walk compares unsigned
    s32 i = 1;
    if (1u < last && !(v < x(1)))
        for (++i; u32(i) < last && !(v < x(i)); ++i) {
        }
    return y(i - 1) + ((v - x(i - 1)) * (y(i) - y(i - 1))) / (x(i) - x(i - 1));
}

// 0x1D8E50: the same with a bisection down to four points first (a tie with a point returns its value).
template <class X, class Y> F lookup_bisect(s32 n, X x, Y y, F v) {
    if (v <= x(0)) return y(0);
    if (x(n - 1) <= v) return y(n - 1);
    u32 lo = 0, hi = u32(n - 1);
    if (hi >= 4) {
        do {
            const u32 mid = (hi + lo) >> 1;
            if (v < x(s32(mid))) hi = mid;
            else if (v == x(s32(mid))) return y(s32(mid));
            else lo = mid;
        } while (hi - lo >= 4);
    }
    u32 i = lo;
    while (i < hi && !(v < x(s32(i)))) ++i;
    return y(s32(i) - 1) + ((y(s32(i)) - y(s32(i) - 1)) * (v - x(s32(i) - 1))) / (x(s32(i)) - x(s32(i) - 1));
}

// A curve table of P at offset t.
F p_lookup(const Ctx& c, u32 t, F v, bool bisect = false) {
    const s32 n = c.p.sh(t);
    const u32 x = c.off(c.p.w(t + 4)), y = c.off(c.p.w(t + 8));
    auto X = [&](s32 i) { return c.p.f(x + 4 * u32(i)); };
    auto Y = [&](s32 i) { return c.p.f(y + 4 * u32(i)); };
    return bisect ? lookup_bisect(n, X, Y, v) : lookup(n, X, Y, v);
}

// ---- engine (E = P + 0x138) -----------------------------------------------------------------------------------------------------

// 0x1E0CD8: stage `st`'s boost at rpm (below the knee by the stage's shape: 0 parabola to the peak, 1 rising parabola, 2 linear;
// above it falling to zero at three times the knee).
F stage_boost(const Ctx& c, u32 E, s32 rpm, u32 st) {
    const s32 knee = c.p.sh(E + 0x94 + 2 * st);
    if (!(rpm < knee)) {
        if (!(rpm < knee * 3)) return k0;
        const F d = cvt(rpm - knee);
        return c.p.f(E + 0xA4 + 4 * st) - c.p.f(E + 0xB4 + 4 * st) * d * d * kQuarter;
    }
    const u8 shape = c.p.b(E + 0x98 + st);
    if (shape == 1) {
        const F r = cvt(rpm);
        return c.p.f(E + 0xB4 + 4 * st) * r * r;
    }
    if (shape >= 2) return shape == 2 ? (c.p.f(E + 0xA4 + 4 * st) * cvt(rpm)) / cvt(knee) : k0;
    const F d = cvt(rpm - knee);
    return c.p.f(E + 0xA4 + 4 * st) - c.p.f(E + 0xB4 + 4 * st) * d * d;
}

// 0x1E0E08: 1 + the two stages' boost (rpm per stage); each stage's share into out.
F boost(const Ctx& c, u32 E, s32 rpm0, s32 rpm1, F* out) {
    const s16 rpm[2] = {s16(rpm0), s16(rpm1)};
    F sum = k0;
    out[0] = k0, out[1] = k0;
    for (u32 st = 0; st < 2; ++st) {
        if (c.p.f(E + 0xA4 + 4 * st) == k0) continue;
        const F b = stage_boost(c, E, rpm[st], st);
        sum = sum + b;
        out[st] = b;
    }
    return sum + k1;
}

// 0x1E0ED8: the boost, capped at 1 + E+0xBC.
F boost_capped(const Ctx& c, u32 E, s32 rpm0, s32 rpm1, F* out) {
    F b = boost(c, E, s16(rpm0), s16(rpm1), out);
    const F cap = c.p.f(E + 0xBC) + k1;
    if (cap < b) b = cap;
    return b;
}

// 0x1E0F38: the turbo stages from the spec's turbo bytes.
void turbo(Ctx& c, u32 E) {
    const Img& s = c.s;
    Img& p = c.p;
    const u8 cap = s.b(0xA7);
    if (cap == 0) p.set(E + 0xBC, k0);
    else {
        p.set(E + 0xBC, cvt(cap) * c.k.tenth);
        if (s.b(0xAA) == 0 || s.b(0xAC) == 0 || s.b(0xAE) == 0) p.set(E + 0xBC, k0);
    }
    if (p.f(E + 0xBC) == k0) {
        p.set(E + 0xB8, k0), p.set16(E + 0x96, 0), p.set8(E + 0x98, 0), p.set(E + 0xA4, k0), p.set(E + 0xAC, k0), p.set(E + 0xB4, k0);
        p.set16(E + 0x94, 0), p.set8(E + 0x99, 0), p.set(E + 0xA8, k0), p.set(E + 0xB0, k0);
        return;
    }
    p.set8(E + 0x98, s.b(0xA8));
    if (!(s.b(0xA8) < 3)) p.set8(E + 0x98, 0);
    const F peak = cvt(s.b(0xAC)) * c.k.tenth;
    p.set(E + 0xA4, peak);
    p.set(E + 0xAC, cvt(s.b(0xAE)) * c.k.tenth);
    const s32 knee = s32(s.b(0xAA)) * 100;
    p.set(E + 0xB8, k0), p.set8(E + 0x99, 0), p.set(E + 0xA8, k0), p.set(E + 0xB0, k0), p.set16(E + 0x96, 0);
    F kf = cvt(knee);
    p.set16(E + 0x94, knee);
    kf = kf * kf;
    p.set(E + 0xB4, peak / kf);
    const u8 a = s.b(0xAD), r = s.b(0xAF), k = s.b(0xAB);
    if (u32(a) + r + k == 0 || a == 0 || k == 0 || r == 0) return;
    p.set8(E + 0x99, s.b(0xA9));
    if (!(s.b(0xA9) < 3)) p.set8(E + 0x99, 0);
    const F peak1 = cvt(a) * c.k.tenth;
    p.set(E + 0xA8, peak1);
    p.set(E + 0xB0, cvt(r) * c.k.tenth);
    const s32 knee1 = s32(k) * 100;
    F k1f = cvt(knee1);
    p.set16(E + 0x96, knee1);
    k1f = k1f * k1f;
    p.set(E + 0xB8, peak1 / k1f);
}

// 0x1E10E8: the engine: idle and rev limit, the torque curve (rpm in rad/s, torque in N m with the spec's power factors, high
// rpm at 0xA4 per mille and low rpm at 0xA6 percent, interpolated over the curve), its friction, the turbo and the boost cap's
// share of each point, and the race's power cap (`cap` = the rule block's byte x 10, 0 = none).
void engine(Ctx& c, u32 E, s32 cap) {
    Img& s = c.s;
    Img& p = c.p;
    p.set16(E + 0x8E, s32(s.b(0x6E)) * 10);
    const u8 lim = s.b(0x6F);
    p.set16(E + 0x8C, lim ? s32(lim) * 100 : s32(s.b(0x71 + s.b(0x71))) * 100);
    const s32 floor = s32(s.b(0x70)) * 100 + 200;
    if (p.h(E + 0x8C) < floor) p.set16(E + 0x8C, floor);
    s32 n = s.b(0x71);
    const u32 last = 0x82 + 2 * u32(n - 1);
    if (s.sh(last) == 0) s.set16(last, s.h(0x82 + 2 * u32(n - 2)));
    n = s.b(0x71);
    const u16 hi_pm = s.h(0xA4), lo_pm = s.h(0xA2);
    for (s32 i = 0; i < n; ++i) p.set(E + 0xC + 4 * u32(i), cvt(s32(s.b(0x72 + u32(i))) * 100) * c.k.rpm);
    const u8 lo_pct = s.b(0xA6);
    const F f_lo = lo_pct ? cvt(lo_pct) * k001 : k1;
    const F f_hi = hi_pm ? cvt(hi_pm) * c.k.milli : k1;
    const F w0 = p.f(E + 0xC), wn = p.f(E + 0xC + 4 * u32(n - 1));
    F scale = lo_pm ? cvt(s.h(0xA2)) * c.k.milli : k1;
    scale = scale * k001;
    if (n != 0) {
        const F span = f_hi - f_lo, range = wn - w0;
        for (s32 i = 0; i < n; ++i) {
            const F x = p.f(E + 0xC + 4 * u32(i)) - w0;
            s32 t = s.sh(0x82 + 2 * u32(i));
            const F f = scale * (f_lo + (span * x) / range);
            if (t < -1) t += 0x10000;
            p.set(E + 0x4C + 4 * u32(i), cvt(t) * c.k.g * f);
        }
    }
    table_init(c, E, s.b(0x71), E + 0xC, E + 0x4C);
    const F friction = (cvt(s.h(0x6C)) * c.k.tenth) * c.k.g;
    p.set(E + 0x90, friction / p.f(E + 0xC + 4 * u32(s.b(0x71) - 1)));
    turbo(c, E);
    for (s32 i = 0; i < p.sh(E); ++i) {
        const s32 rpm = s32(s.b(0x72 + u32(i))) * 100;
        F out[2];
        F f = boost_capped(c, E, rpm, rpm, out);
        f = f / boost(c, E, s16(rpm), s16(rpm), out);
        p.set(E + 0x4C + 4 * u32(i), p.f(E + 0x4C + 4 * u32(i)) * f);
    }
    p.set8(E + 0x9B, 0);
    p.set(E + 0x9C, k0);
    if (cap <= 0) return;
    p.set8(E + 0x9B, 1);
    F w = ((cvt(cap) * c.k.g) * c.k.k7162) * c.k.two_pi; // x 716.2
    const F soft = cvt(c.st(8)) * k001;
    w = w / k60;
    p.set(E + 0xA0, soft);
    p.set(E + 0x9C, w / (k1 - soft * kHalf));
}

// ---- driveline (D = P + 0x1F8) --------------------------------------------------------------------------------------------------

// 0x1E2700: the layout D+0 from the drive type and AWD system, the driven axle D+3, the AWD coupling D+4 / D+8.
// The later builds' (Rule::Layout, Concept 2001 Tokyo 0x1DCE80): AWD system 3's gain and capacity come from the program's
// settings (ProgramData's Awd3 table) and not the spec's capacity byte; system 5 is a layout of its own, 7, and system 7's
// layout is 8.
void layout(Ctx& c, u32 Dv) {
    const Img& s = c.s;
    Img& p = c.p;
    const ProgramData& d = *c.in.data;
    const bool later = !gt3_program(d.behaviour().rule(Rule::Layout));
    p.set(Dv + 4, k0), p.set(Dv + 8, k0);
    const u8 drive = s.b(0xB2);
    if (drive >= 5) return;
    if (drive == 0 || drive == 3 || drive == 4) return p.set8(Dv + 0, 0), p.set8(Dv + 3, 1);
    if (drive == 1) return p.set8(Dv + 0, 1), p.set8(Dv + 3, 0);
    p.set(Dv + 4, cvt(s.b(0xB4)) * k001);
    p.set8(Dv + 3, 1);
    const u8 sys = s.b(0xB3);
    if (sys >= 8) return;
    auto cap10 = [&](s32 dflt, F k) {
        const u8 b = s.b(0xB5);
        p.set(Dv + 8, cvt((b ? s32(b) : dflt) * 10) * k);
    };
    switch (sys) {
    case 0: return p.set8(Dv + 0, 2), p.set8(Dv + 3, 0);
    case 1: return p.set8(Dv + 0, 3);
    case 2:
        p.set8(Dv + 0, 4);
        if (!(s.b(0xB4) < 50)) p.set(Dv + 4, k1);
        return cap10(8, c.k.g);
    case 3:
        p.set8(Dv + 0, 5);
        p.set(Dv + 4, p.f(Dv + 4) * c.k.awd3_gain); // x 0.36
        if (later) {
            using T = ProgramData::Table;
            const F gain = cvt(d.b(T::Awd3, 0)) / k100, cap = cvt(s32(d.b(T::Awd3, 1)) * 10) * c.k.g;
            p.set(Dv + 4, p.f(Dv + 4) * gain);
            return p.set(Dv + 8, cap);
        }
        return cap10(15, c.k.g);
    case 4: {
        p.set8(Dv + 0, 6);
        const F k = p.f(Dv + 4);
        if (!(k == k0)) p.set(Dv + 4, k1 / ((k * k100) * c.k.deg));
        return cap10(15, c.k.g);
    }
    case 5:
        if (later) p.set8(Dv + 0, 7);
        return;
    case 7: return p.set8(Dv + 0, later ? 8 : 7);
    default: return;
    }
}

// 0x1E1B28: the differential type's index in "frnmhvyt" (an unknown character reads as itself).
s8 lsd_type(const Ctx& c, s8 ch) {
    auto letter = [&](u32 i) { return s8(c.in.data->b(ProgramData::Table::LsdTypes, i)); };
    if (letter(0) == ch) return 0;
    for (s32 i = 1; i < 8; ++i)
        if (letter(u32(i)) == ch) return s8(i);
    return ch;
}

// 0x1E2670: an axle's tyre radius, width and sidewall from the spec's size bytes (a rear width of 0 reads as 10 mm).
void tyre_size(const Ctx& c, u32 axle, F& radius, F& width, F& sidewall) {
    const Img& s = c.s;
    s32 w5 = s32(s.b(0x11A + axle)) * 5;
    if (axle && w5 == 0) w5 = 10;
    const F w = cvt(w5) * c.k.milli;
    F rim = cvt(s.b(0x11C + axle)) * F::raw(0x3CD013A9); // x 0.0254
    F side = w * cvt(s.b(0x11E + axle));
    width = w;
    rim = rim * kHalf;
    side = side * c.k.twentieth; // x 0.05
    radius = rim + side;
    sidewall = side;
}

// 0x1E1C98: the driveline: layout, differentials, gear ratios, tyre sizes, inertias, launch / shift rpm, gear speed factors.
void driveline(Ctx& c, u32 Dv, u32 E) {
    Img& s = c.s;
    Img& p = c.p;
    // (Rule::Driveline: the later builds' routine, Concept 2001 Tokyo 0x1DC278)
    const bool later = !gt3_program(c.in.data->behaviour().rule(Rule::Driveline));
    layout(c, Dv);
    for (u32 i = 0; i < 2; ++i) p.set8(Dv + 0x10 + i, lsd_type(c, s.sb(0xD6 + i)));
    const u8 lay = p.b(Dv);
    auto dflt = [&](u32 i) {
        if (p.b(Dv + 0x10 + i) == 0) p.set8(Dv + 0x10 + i, 2);
    };
    if (lay == 0 || (later && lay == 7)) { // (the later builds' layout 7 drives the rear axle, as layout 0)
        if (p.b(Dv + 0x10)) p.set8(Dv + 0x10, 0);
        dflt(1);
    } else if (lay == 1) {
        dflt(0);
        if (p.b(Dv + 0x11)) p.set8(Dv + 0x11, 0);
    } else if (lay < (later ? 9 : 8)) {
        dflt(0);
        dflt(1);
    }
    for (u32 a = 0; a < 2; ++a) {
        const u8 t = p.b(Dv + 0x10 + a);
        if (t >= 8) continue;
        const u32 o = Dv + 4 * a;
        const F ini = cvt(s.b(0xD8 + a)), acc = cvt(s.b(0xDA + a)), dec = cvt(s.b(0xDC + a));
        switch (t) {
        case 0: case 1: case 2: p.set(o + 0x14, k0), p.set(o + 0x1C, k0), p.set(o + 0x24, k0); break;
        case 3: case 4: case 6: p.set(o + 0x14, ini * c.k.g), p.set(o + 0x1C, acc * k001), p.set(o + 0x24, dec * k001); break;
        case 5: p.set(o + 0x14, ini * ini), p.set(o + 0x1C, (acc * c.k.tenth) * c.k.g), p.set(o + 0x24, k0); break;
        default: p.set(o + 0x14, ini * k001), p.set(o + 0x1C, acc * k001), p.set(o + 0x24, dec * k001); break;
        }
    }
    // 0x1E1C10 would generate the gear words here (0x1E28A8) when spec 0xCA is set; the spec builder always leaves it 0.
    const u8 gears = s.b(0xC8);
    p.set8(Dv + 2, gears);
    if (later) {
        // the gearbox kind (spec +0xE1): kind 3 has one gear, kind 1 needs two; at most 7 gears
        const u8 kind = s.b(0xE1);
        p.set8(Dv + 0x12, kind);
        if (!(gears < 8)) p.set8(Dv + 2, 7);
        else if (kind == 3) {
            if (gears != 1) p.set8(Dv + 2, 1);
        } else if (kind == 1 && p.b(Dv + 2) < 2) {
            p.set8(Dv + 0x12, 0);
        }
    }
    for (u32 g = 0; g <= p.b(Dv + 2); ++g) {
        const F fd = cvt(s16(s.h(0xC6))) * c.k.milli;
        p.set(Dv + 0x3C + 4 * g, (cvt(s.sh(0xB6 + 2 * g)) * c.k.milli) * fd);
    }
    p.set(Dv + 0x3C, -p.f(Dv + 0x3C));
    F r0, w0, sw0, r1, w1, sw1;
    tyre_size(c, 0, r0, w0, sw0);
    p.set(Dv + 0x7C, r0), p.set(Dv + 0x8C, w0), p.set(Dv + 0x94, sw0);
    tyre_size(c, 1, r1, w1, sw1);
    p.set(Dv + 0x80, r1), p.set(Dv + 0x90, w1), p.set(Dv + 0x98, sw1);
    if (!(p.f(Dv + 0x7C) == k0)) p.set(Dv + 0x84, k1 / p.f(Dv + 0x7C));
    if (!(p.f(Dv + 0x80) == k0)) p.set(Dv + 0x88, k1 / p.f(Dv + 0x80));
    // (the later builds: layout 5 with axles of unequal tyre radii)
    if (later) p.set8(Dv + 1, p.b(Dv) == 5 && !(p.f(Dv + 0x7C) == p.f(Dv + 0x80)));
    const u16 inertia = s.h(0x6A);
    F spin[2];
    for (u32 a = 0; a < 2; ++a) spin[a] = cvt(s.sh(0xD2 + 2 * a)) * k001;
    for (u32 a = 0; a < 2; ++a) {
        const F i = spin[a];
        if (i == k0) continue;
        const F inv_r = p.f(Dv + 0x84 + 4 * a);
        p.set(Dv + 0xA0 + 4 * a, p.f(Dv + 0x7C + 4 * a) / i);
        p.set(Dv + 0xB8 + 4 * a, (i * inv_r) * inv_r);
    }
    const F ie = cvt(inertia) * c.k.milli;
    p.set(Dv + 0xC, ie);
    if (inertia != 0) p.set(Dv + 0xB4, k1 / ie);
    const F fd = cvt(s16(s.h(0xC6))) * c.k.milli;
    for (u32 a = 0; a < 2; ++a) {
        F shaft = cvt(s.sh(0xCE + 2 * a)) * k001;
        // (the later builds, unequal axles: the other axle's shaft term by the driven axle's tyre radius over its own)
        if (later && p.b(Dv + 1) != 0 && a != p.b(Dv + 3)) shaft = shaft * (p.f(Dv + 0x7C + 4 * u32(p.b(Dv + 3))) / p.f(Dv + 0x7C + 4 * a));
        p.set(Dv + 0xA8 + 4 * a, (shaft * fd) + (spin[a] + spin[a]));
    }
    p.set(Dv + 0xB0, k1 / (p.f(Dv + 0xA8) + p.f(Dv + 0xAC)));
    p.set16(Dv + 0x2C, s32(s.b(0xC9)) * 10);
    p.set16(Dv + 0x2E, s32(s.b(0x70)) * 100);
    p.set(Dv + 0x9C, cvt(s.b(0xCD)) * c.k.g); // 0xCD: never written by the spec builder
    const u16 idle = p.h(E + 0x8E);
    for (u32 g = 0; g <= p.b(Dv + 2); ++g)
        p.set(Dv + 0x5C + 4 * g, p.f(Dv + 0x3C + 4 * g) / p.f(Dv + 0x7C + 4 * u32(p.b(Dv + 3))));
    // (the later builds' gearbox kinds 1 and 2, without steps: the launch speed is the idle speed)
    const bool stepless = later && (p.b(Dv + 0x12) == 1 || p.b(Dv + 0x12) == 2);
    if (stepless) p.set16(Dv + 0x2C, idle);
    else if (s32(p.h(Dv + 0x2C)) - 500 < s32(idle)) p.set16(Dv + 0x2C, s32(idle) + 500);
    for (u32 g = 1; g < p.b(Dv + 2); ++g) {
        const u32 n = g + 1;
        const F up = (cvt(p.h(Dv + 0x2E)) * c.k.rpm) * p.f(Dv + 0x3C + 4 * n);
        const F v = p.f(Dv + 0x5C + 4 * n) * c.k.ten_kmh; // x 10 / 3.6
        const s32 floor = s32(p.h(Dv + 0x2C)) + 500;
        const F rpm_v = (v * k60) / c.k.two_pi;
        const F rpm_d = ((up / p.f(Dv + 0x3C + 4 * g)) * k60) / c.k.two_pi;
        s32 d = to_int(rpm_d) - to_int(rpm_v);
        if (d < floor) d = floor;
        p.set16(Dv + 0x30 + 2 * (g - 1), d);
    }
    // ... and its ratios from the second on are the gear words over the first's (the range the ratio word runs over)
    if (stepless)
        for (u32 g = 2; g <= p.b(Dv + 2); ++g) p.set(Dv + 0x3C + 4 * g, cvt(s.sh(0xB6 + 2 * g)) / cvt(s.sh(0xB8)));
}

// ---- tyre records (R = P + 0x398 + 0x1B4 r, from spec block B) -------------------------------------------------------------------

// 0x1DB9E0: curves C (+0xD4) and D (+0x14C) on one slip-ratio axis (+0xE0; the negative side reversed), the optimum slips
// +0x140 / +0x144 and C's braking-side maximum +0x148. Clears the block's curve origins.
void tyre_slip_curves(Ctx& c, u32 R, u32 B) {
    Img& s = c.s;
    Img& p = c.p;
    s.set8(B + 0x14, 0);
    const u8 d0 = s.b(B + 0x33);
    const s32 n1 = s.b(B + 0x13), n2 = s.b(B + 0x26);
    s.set8(B + 0x20, d0);
    s.set8(B + 0x1A, 0), s.set8(B + 0x27, 0), s.set8(B + 0x2D, 0);
    F best = k0;
    p.set(R + 0x140, best);
    for (s32 i = 0; i < n1; ++i) {
        const u32 k = 4 * u32(n1 - (i + 1));
        p.set(R + 0xE0 + k, (-cvt(s.b(B + 0x14 + u32(i)))) / k255);
        const F cv = cvt(s.b(B + 0x1A + u32(i))) / k200;
        p.set(R + 0x110 + k, cv);
        if (best < cv) best = cv, p.set(R + 0x140, p.f(R + 0xE0 + k));
        p.set(R + 0x158 + k, cvt(s.b(B + 0x20 + u32(i))) / k200);
    }
    best = k0;
    p.set(R + 0x144, best);
    p.set(R + 0x148, k1);
    for (s32 i = 0; i < n2; ++i) {
        const u32 k = 4 * u32(n1 + i - 1);
        p.set(R + 0xE0 + k, cvt(s.b(B + 0x27 + u32(i))) / k255);
        const F cv = cvt(s.b(B + 0x2D + u32(i))) / k200;
        p.set(R + 0x110 + k, cv);
        if (best < cv) best = cv, p.set(R + 0x148, best), p.set(R + 0x144, p.f(R + 0xE0 + k));
        p.set(R + 0x158 + k, cvt(s.b(B + 0x33 + u32(i))) / k200);
    }
    table_init(c, R + 0xD4, n1 + n2 - 1, R + 0xE0, R + 0x110);
    table_init(c, R + 0x14C, n1 + n2 - 1, R + 0xE0, R + 0x158);
}

// 0x1DBC50: curve A (+0x48: slip angle up to the block's range, at most pi/2) and curve B (+0x98: up to pi/2).
void tyre_angle_curves(Ctx& c, u32 R, u32 B) {
    Img& s = c.s;
    Img& p = c.p;
    const u8 deg = s.b(B + 1);
    s.set8(B + 3, 0);
    F range = cvt(deg ? deg : 0x5A) * c.k.deg;
    s.set8(B + 0xB, 0);
    if (c.k.half_pi_range < range) range = c.k.half_pi_range;
    const s32 n = s.b(B + 2);
    for (s32 i = 0; i < n; ++i) {
        p.set(R + 0x54 + 4 * u32(i), (cvt(s.b(B + 3 + u32(i))) * range) / k255);
        p.set(R + 0x74 + 4 * u32(i), cvt(s.b(B + 0xB + u32(i))) / k200);
    }
    table_init(c, R + 0x48, n, R + 0x54, R + 0x74);
    const s32 m = s.b(B + 0x5E);
    s.set8(B + 0x5F, 0);
    s.set8(B + 0x65, 0);
    for (s32 i = 0; i < m; ++i) {
        p.set(R + 0xA4 + 4 * u32(i), (cvt(s.b(B + 0x5F + u32(i))) * c.k.half_pi_b) / k255);
        p.set(R + 0xBC + 4 * u32(i), cvt(s.b(B + 0x65 + u32(i))) / k200);
    }
    table_init(c, R + 0x98, m, R + 0xA4, R + 0xBC);
}

// 0x1E8670: the whole degree 0..45 where curve A peaks (90 when it never rises above 0), in rad.
F peak_slip_angle(const Ctx& c, u32 R) {
    F best = k0;
    s32 at = 0x5A;
    for (s32 d = 0; d < 0x2E; ++d) {
        F sign;
        const F a = slip_angle_fold(c.h, (cvt(d) * c.k.deg) - k0, sign); // 0x1E7A68
        const F v = p_lookup(c, R + 0x48, a) * sign;
        if (best < v) best = v, at = d;
    }
    return cvt(at) * c.k.deg;
}

// 0x1DBE40: base grip and the per-surface grip (+0x08 + 4 s; surface 11 capped at min(1, cap / mu)), the wear factor +0x04,
// the load curve L (+0x188), and +0x00 = D(0) x 9.8.
void tyre_grip(Ctx& c, u32 R, u32 B, F cap) {
    Img& s = c.s;
    Img& p = c.p;
    const u8 mu100 = s.b(B);
    const F mu = cvt(mu100 ? mu100 : 100) * k001;
    cap = cap / mu;
    for (u32 i = 0; i < 16; ++i) {
        const u8 v = s.b(B + 0x4B + i);
        p.set(R + 8 + 4 * i, v ? (cvt(v) * k001) * mu : mu);
    }
    p.set(R + 0x34, cap);
    p.set(R + 4, cvt(s.b(B + 0x6B)) * k001);
    if (k1 < cap) p.set(R + 0x34, k1);
    const s32 n = s.b(B + 0x39);
    s.set8(B + 0x3A, 0);
    for (s32 i = 0; i < n; ++i) {
        p.set(R + 0x194 + 4 * u32(i), (cvt(s.b(B + 0x3A + u32(i))) * k20) * c.k.g);
        p.set(R + 0x1A4 + 4 * u32(i), cvt(s.b(B + 0x3E + u32(i))) / k200);
    }
    table_init(c, R + 0x188, n, R + 0x194, R + 0x1A4);
    p.set(R, p_lookup(c, R + 0x14C, k0, true) * c.k.g);
}

// ---- suspension (S = P + 0x2B8 + 0x44 axle) ----------------------------------------------------------------------------------

// 0x1DB5C0: a damper's F(v) = o + g sqrt(v - t) through the two points (v1, F1), (v2, F2); all 0 when they do not fit.
void damper_fit(Ctx& c, u32 t, u32 o, u32 g, F v1, F f1, F v2, F f2) {
    Img& p = c.p;
    p.set(g, k0), p.set(o, k0), p.set(t, k0);
    if (f1 == k0 || f2 == k0 || v1 == k0 || v2 == k0 || v2 <= v1 || f2 <= f1) return;
    const F d = f1 * v2 - f2 * v1;
    if (d <= k0) return;
    const F e = ((f1 * f1) * v2) - ((f2 * f2) * v1);
    if (k0 < e) return;
    F q = e / (d + d);
    F h = ((f1 * f2) * (f2 - f1)) / d;
    const F gv = ee::sqrt(h);
    q = q * q;
    p.set(g, gv);
    const F tv = (-q) / h;
    p.set(t, tv);
    p.set(o, (-p.f(g)) * ee::sqrt(-tv));
}

// 0x1DB6B8: an axle block's spring (x the motion ratio %), dampers, anti-roll, progressive gain, unsprung mass and bump damping
// from the spec's axle bytes (0x3A front, 0x4A rear).
void spring_block(Ctx& c, u32 S, u32 b, u8 ratio) {
    Img& s = c.s;
    Img& p = c.p;
    const F r = cvt(ratio) * k001;
    p.set(S + 0x14, (cvt(s.b(b)) * c.k.k980) * r);
    const F scale = k100 / cvt(ratio); // 0x1DB510
    // 0x1DB538: the damper's two points (speed bytes x 0.01 x the scale, force bytes x 5 x 9.8)
    auto points = [&](u32 o, F& v1, F& f1, F& v2, F& f2) {
        f1 = (cvt(s.b(b + o + 1)) * k5) * c.k.g;
        f2 = (cvt(s.b(b + o + 3)) * k5) * c.k.g;
        v1 = (cvt(s.b(b + o)) * k001) * scale;
        v2 = (cvt(s.b(b + o + 2)) * k001) * scale;
    };
    F v1, f1, v2, f2;
    points(4, v1, f1, v2, f2);
    damper_fit(c, S + 0x18, S + 0x1C, S + 0x20, v1, f1, v2, f2);
    points(8, v1, f1, v2, f2);
    damper_fit(c, S + 0x24, S + 0x28, S + 0x2C, v1, f1, v2, f2);
    p.set(S + 0x30, (cvt(s.b(b + 1)) * k10) * c.k.g);
    p.set(S + 0x34, (cvt(s.b(b + 2)) * k10000) * c.k.g);
    p.set(S + 0x38, cvt(s.b(b + 3)) * kQuarter);
    p.set(S + 0x3C, (cvt(s.b(b + 3)) * kQuarter) * c.k.g);
    p.set(S + 0x40, (cvt(s.b(b + 0xD)) * k10) * c.k.g);
}

// 0x1DB890: an axle block's static position +0x10 (the sprung load over the spring), its offset +0x0C from the tyre radius and
// ride height, the bump-stop limit +0x04, the knee +0x00 and the droop limit +0x08.
void spring_static(Ctx& c, u32 S, u32 a) {
    const Img& s = c.s;
    Img& p = c.p;
    const F k = p.f(S + 0x14);
    if (!(k == k0)) p.set(S + 0x10, ((-p.f(0xE54 + 4 * a)) * kHalf) / k);
    const F ride = p.f(0x40 + 4 * a);
    const F x = p.f(S + 0x10);
    p.set(S + 0xC, (p.f(0x274 + 4 * a) - ride) + x);
    F room = cvt(s.b(0x64 + a)) * c.k.milli;
    const F top = x - ride;
    if (ride < room) room = ride;
    const F limit = top + room;
    p.set(S + 4, limit);
    const F knee = cvt(s.b(0x46 + 16 * a)) * c.k.milli;
    if (ride < knee + room) p.set(S + 0, limit);
    else p.set(S + 0, (top + ride) - knee);
    const F pre = p.f(S + 0x3C) / k;
    const F droop = x + cvt(s.b(0x66 + a)) * c.k.milli;
    p.set(S + 8, droop);
    if (pre < droop) p.set(S + 8, pre);
}

// ---- top speed ------------------------------------------------------------------------------------------------------------------

// 0x1E0BC0: full-throttle torque at engine speed w (the curve, bisected) x the condition factor (0x1DAD28 on the entry
// block's +4 / +8 words).
F torque_at(const Ctx& c, u32 E, F w) {
    const F t = p_lookup(c, E, w, true);
    return t * condition_factor(c.in.data ? c.in.data->behaviour() : Behaviour{}, c.condition, F::raw(c.in.condition[1]),
                                F::raw(c.in.condition[2]));
}

// 0x1E0C38: the torque under the race's power cap (soft above (1 - share) x cap).
F capped_torque(const Ctx& c, u32 E, F t, F w) {
    const Img& p = c.p;
    if (p.b(E + 0x9B) == 0) return t;
    const F share = p.f(E + 0xA0);
    const F pw = t * w;
    const F cap = p.f(E + 0x9C);
    if (pw <= cap * (k1 - share)) return t;
    F lim = cap * (k1 - share * kHalf);
    if (!(cap <= pw)) {
        const F d = cap - pw;
        lim = lim - (d * d) / ((share + share) * cap);
    }
    lim = lim / w;
    return lim < t ? lim : t;
}

// 0x1DB2C8: the top speed P+0xDF8: from 0 to its first estimate in whole km/h steps (estimate / 500 + 1), shifting up past the
// upshift rpm, the last speed where the drive force beats rolling resistance (with downforce on the load) and drag.
void top_speed(Ctx& c) {
    Img& p = c.p;
    const u32 E = 0x138, Dv = 0x1F8;
    const s32 kmh = to_int(p.f(0xDF8) * kToKmh);
    const s32 step = kmh / 500 + 1;
    s32 gear = 1;
    const Program conv = c.in.data->behaviour().rule(Rule::ParamsBuild);
    const bool cvt1 = !gt3_program(conv) && p.b(Dv + 0x12) == 1;
    auto rx = [&](s32 i) { return c.in.roll_x[size_t(i)]; };
    auto ry = [&](s32 i) { return c.in.roll_y[size_t(i)]; };
    for (s32 k = 0; k < kmh; k += step) {
        const F v = cvt(k) * c.k.kmh;
        const u32 g = 0x254 + 4 * u32(gear);
        F w = v * p.f(g);
        const F v2 = v * v;
        // (Rule::ParamsBuild, the later builds: a stepless gearbox of kind 1 holds the engine at its hold speed, 0x1D9948 of
        // Concept 2001 Tokyo: above it the ratio falls, down to the last gear's relative ratio; no gear is changed)
        F ratio = k1;
        if (cvt1) {
            const F hold = cvt(p.h(Dv + 0x30)) * F::raw(conv >= Program::ConceptGenevaEu ? 0x3DD6774E : 0x3DD67750);
            if (!(w < hold)) {
                ratio = hold / w;
                const F last = p.f(Dv + 0x3C + 4 * u32(p.b(Dv + 2)));
                if (ratio < last) ratio = last;
            }
            w = ratio * w;
        }
        const s32 rpm = to_int((w * k60) / c.k.two_pi);
        const F t = capped_torque(c, E, torque_at(c, E, w), w);
        F drive = t * p.f(g);
        if (cvt1) drive = drive * ratio;
        const F lift_f = p.f(0xE64) * v2, lift_r = p.f(0xE68) * v2;
        const F roll = lookup(c.in.roll_n, rx, ry, v) * c.in.roll_scale;
        const F resist = roll * ((p.f(0xE50) + lift_f) + lift_r) + p.f(0xE60) * v2;
        if (resist < drive) p.set(0xDF8, v);
        if (!cvt1 && gear < s32(p.b(Dv + 2))) gear += s32(p.h(Dv + 0x2E)) < rpm;
    }
}

// 0x1E14E8: the engine's peak torque and power over its curve in 100 rpm steps (EnginePeak).
EnginePeak engine_peak_of(const Ctx& c) {
    const Img& p = c.p;
    const u32 E = 0x138;
    // The curve points' rpm (x 60 / 2 pi, truncated) rounded to 100, each kept when above the last kept one.
    std::array<s16, 16> pts{};
    s32 m = 0;
    for (s32 i = 0; i < p.sh(E) && i < s32(pts.size()); ++i) {
        const s32 rpm = to_int((p.f(E + 0xC + 4 * u32(i)) * k60) / c.k.two_pi);
        const s32 r = (rpm + 50) / 100 * 100;
        if (m <= 0 || pts[size_t(m - 1)] < r) pts[size_t(m++)] = s16(r);
    }
    EnginePeak out;
    if (m == 0) return out; // (no curve: GT3 keeps the out record's old words; no car has none)
    const s32 rev = p.h(E + 0x8C);
    const s32 top = std::max<s32>(rev, pts[size_t(m - 1)]);
    for (s32 rpm = pts[0]; rpm <= top; rpm += 100) {
        const F n = int_to_f(rpm);
        const F w = n * c.k.rpm;
        const F kgfm = capped_torque(c, E, torque_at(c, E, w), w) / c.k.g;
        const F ps = (kgfm * n) / c.k.k7162;
        if (rev < rpm) continue;
        if (out.kgfm < kgfm) out.kgfm = kgfm, out.torque_rpm = rpm;
        if (out.ps < ps) out.ps = ps, out.power_rpm = rpm;
    }
    return out;
}

// 0x1DB498: the top speed's start (the rev limit in top gear) and the braking constant P+0xE34, then the walk.
void top_speed_start(Ctx& c) {
    Img& p = c.p;
    F brake = p.f(0xE28) * p.f(0x27C);
    const F rev = cvt(p.h(0x1C4)) * c.k.rpm;
    brake = brake + brake;
    brake = brake + brake;
    p.set(0xDF8, rev / p.f(0x254 + 4 * u32(p.b(0x1FA))));
    p.set(0xE34, p.f(0x48) / brake);
    // (Rule::ParamsBuild, the later builds: a stepless gearbox of kind 1 holds the engine at its peak power's speed, at
    // most 500 rpm below the up-shift speed: the down-shift word)
    if (!gt3_program(c.in.data->behaviour().rule(Rule::ParamsBuild)) && p.b(0x1F8 + 0x12) == 1) {
        const s32 peak = u16(engine_peak_of(c).power_rpm), cap = s32(p.h(0x1F8 + 0x2E)) - 500;
        p.set16(0x1F8 + 0x30, cap < peak ? cap : peak);
    }
    top_speed(c);
}

// 0x1F5EF0: the driveline class: 1 FF and AWD system 0, 3 AWD system 3, 2 the other AWD systems, 0 the rest.
u8 driveline_class(u8 drive, u8 sys, bool later = false) {
    if (drive == 1) return 1;
    if (drive != 2) return 0;
    if (sys == 0) return 1;
    if (later && sys == 5) return 0; // (Rule::DriveClass: the later builds' layout 7, Concept 2001 Tokyo 0x1F5808)
    return sys == 3 ? 3 : 2;
}

} // namespace

void ConvertInputs::physics_globals() {
    // The static block 0x2D28B0: +0 the air density x 10 (0x1DB020: x [0x350164] 0.1); the tarmac rolling-resistance curve and
    // scale as 0x1DAE70 builds them (race_inputs.h).
    air_density = cvt(data->b(ProgramData::Table::Static, 0)) * K(toward_zero(data->behaviour())).tenth; // (Europe 0x1D62B8)
    const Statics st = statics(*data);
    roll_n = st.rolling_curve.n;
    roll_x = {}, roll_y = {};
    for (size_t i = 0; i < size_t(roll_n) && i < roll_x.size(); ++i) roll_x[i] = st.rolling_curve.x[i], roll_y[i] = st.rolling_curve.y[i];
    roll_scale = st.rolling_mu[0];
}

s32 convert_params(Spec& spec, const ConvertInputs& in, ParamImage& image) {
    Ctx c{Img{spec.data()}, Img{image.data()}, in, condition(*in.data)};
    Img& s = c.s;
    Img& p = c.p;
    p.set8(0x33, s.b(0x2D));
    // (the later builds' converter: the special kind, Concept 2001 Tokyo 0x1D6880)
    if (!gt3_program(in.data->behaviour().rule(Rule::SpecBuild))) p.set8(0x32, s.b(0x11));
    if (!(s.b(0xB2) < 5)) s.set8(0xB2, 0);
    // Chassis: extents, wheelbase, CoG distances, tracks (the mean track and 1 - wf in the program's doubles).
    const F wf = cvt(s.b(0x2C)) * k001;
    p.set(0x00, cvt(s.sh(0x1C)) * c.k.milli);
    p.set(0x04, cvt(s.sh(0x1E)) * c.k.milli);
    p.set(0x24, wf);
    const F L = cvt(s.sh(0x24)) * c.k.milli;
    const F b = L * wf;
    p.set(0x18, L);
    p.set(0x14, b);
    p.set(0x10, L - b);
    const Behaviour h = in.data->behaviour(); // (the build's soft doubles: build_math.h)
    p.set(0x20, narrow(h, sub(h, D::raw(0x3FF0000000000000ull), widen(h, wf))));
    const F half = (-(p.f(0x10) - p.f(0x14))) * kHalf;
    p.set(0x54, half);
    p.set(0x04, p.f(0x04) + half);
    p.set(0x00, p.f(0x00) - half);
    p.set(0x08, cvt(s.sh(0x20)) * c.k.milli);
    const F hf = (cvt(s32(s.sh(0x26)) - s32(s.sb(0x68)) * 2) * c.k.milli) * kHalf;
    p.set(0x28, hf);
    p.set(0x2C, (cvt(s32(s.sh(0x28)) - s32(s.sb(0x69)) * 2) * c.k.milli) * kHalf);
    const D front = widen(h, hf * wf), rear = widen(h, p.f(0x2C));
    const D mean = add(h, front, mul(h, rear, sub(h, D::raw(0x3FF0000000000000ull), widen(h, wf))));
    p.set(0x1C, narrow(h, add(h, mean, mean)));
    p.set(0x0C, cvt(s.sh(0x22)) * c.k.milli);
    engine(c, 0x138, s32(in.rules[3]) * 10);
    driveline(c, 0x1F8, 0x138);
    p.set8(0x31, driveline_class(s.b(0xB2), s.b(0xB3), !gt3_program(in.data->behaviour().rule(Rule::DriveClass))));
    // Tyre records: surface s uses front record c_s and rear c_s + 3 (classes >= 3 read as 0); only used records are built.
    u8 used[6] = {};
    for (u32 i = 0; i < 16; ++i) {
        u8 cls = s.b(0x132 + i);
        if (!(cls < 3)) cls = 0;
        p.set8(0xDD0 + i, cls);
        p.set8(0xDE0 + i, cls + 3);
        ++used[cls];
        ++used[cls + 3];
    }
    for (u32 r = 0; r < 6; ++r) {
        if (s8(used[r]) == 0) continue;
        const u32 blk = s.b(0x12) ? r : r % 3;
        const u32 R = 0x398 + 0x1B4 * r, B = 0x142 + 0x70 * blk;
        tyre_slip_curves(c, R, B);
        tyre_angle_curves(c, R, B);
        const F peak = peak_slip_angle(c, R);
        p.set(R + 0x94, peak);
        if (c.k.quarter_pi < peak) p.set(R + 0x94, c.k.quarter_pi);
        tyre_grip(c, R, B, cvt(c.st(0x8F + blk % 3)) * k001);
        const u32 axle = r / 3;
        const F load = (p.f(0xE54 + 4 * axle) * kHalf) + p.f(0x2F4 + 0x44 * axle);
        const F per_width = load / p.f(0x284 + 4 * ((axle * 2) >> 1)); // 0x1EA1C0
        p.set(R, p.f(R) * p_lookup(c, R + 0x188, per_width));
    }
    // Camber curves per axle: x = (128 - code) x 4 deg / 32, y = code / 200, stored in reverse.
    for (u32 a = 0; a < 2; ++a) {
        const s32 n = s.b(0x120 + a);
        for (s32 i = 0; i < n; ++i) {
            const u32 k = 4 * u32(n - (i + 1)) + 16 * a;
            const F x = (cvt(0x80 - s32(s.b(0x122 + 4 * a + u32(i)))) * c.k.camber_step) * F::raw(0x3D000000);
            p.set(0x358 + k, x);
            p.set(0x378 + k, cvt(s.b(0x12A + 4 * a + u32(i))) / k200);
        }
        table_init(c, 0x340 + 12 * a, n, 0x358 + 16 * a, 0x378 + 16 * a);
    }
    // Tyre load knees per axle (static bytes x 98 N) over the width.
    for (u32 a = 0; a < 2; ++a) {
        const F lo = cvt(c.st(0x8C)) * c.k.k98, hi = cvt(c.st(0x8D)) * c.k.k98;
        const F d = hi - lo;
        p.set(0xE6C + 4 * a, lo - d);
        p.set(0xE74 + 4 * a, d / (p.f(0x284 + 4 * a) * c.k.k9800));
    }
    // Per-axle grip factors (chassis bytes 1000 + d, low byte kept: (100 + d) + 900) x 0.001; CPU cars x the rule block's
    // per-class percentages.
    u32 pct[2] = {s.b(0x36), s.b(0x37)};
    for (u32 a = 0; a < 2; ++a) p.set(0xDF0 + 4 * a, cvt(s32(pct[a]) + 0x384) * c.k.milli);
    if (in.control == 2) {
        const u8 cls = p.b(0x31);
        pct[0] = in.rules[4 + cls], pct[1] = in.rules[8 + cls];
        for (u32 a = 0; a < 2; ++a) {
            if (pct[a] == 0) pct[a] = 100;
            p.set(0xDF0 + 4 * a, p.f(0xDF0 + 4 * a) * (cvt(s32(pct[a])) * k001));
        }
    }
    // Aerodynamics: frontal area (spec 0x16 / 10000 m^2, or width x height) x the air density / 2, then Cd and lift.
    F area;
    if (const s16 a = s16(s.h(0x16)); a != 0) area = (cvt(a) / k10000) * (in.air_density * kHalf);
    else area = ((p.f(0x08) * p.f(0x0C)) * in.air_density) * kHalf;
    const F cd = cvt(s.b(0x32)) * k001;
    F drag = area * cd;
    p.set(0xE5C, cd);
    p.set(0xE60, drag);
    F lf = area * (cvt(s.b(0x33)) * k001);
    p.set(0xE64, lf);
    const F lr = area * (cvt(s.b(0x34)) * k001);
    lf = lf + lr;
    p.set(0xE68, lr);
    lf = lf * c.k.tenth;
    drag = drag + lf;
    p.set(0xE60, drag);
    // Brakes: torque per wheel (bytes x 98 N m), radius over torque, the handbrake, the controller settings.
    for (u32 a = 0; a < 2; ++a) {
        const u8 t = s.b(0xDE + a);
        const F torque = cvt(s32(t) * 10) * c.k.g;
        p.set(0xE28 + 4 * a, torque);
        if (t == 0) p.set32(0xE48 + 4 * a, 0);
        else p.set(0xE48 + 4 * a, p.f(0x274 + 4 * a) / torque);
    }
    p.set(0xE30, cvt(s32(s.b(0xE0)) * 10) * c.k.g);
    for (u32 a = 0; a < 2; ++a) {
        p.set(0xE38 + 4 * a, cvt(s.b(0x10A + a)) * c.k.tenth);
        p.set(0xE40 + 4 * a, cvt(s.b(0x112 + a)) * k001);
    }
    // Aids (A = P + 0xE7C): ASCC channels and flags, TCS.
    {
        const u32 A = 0xE7C;
        const u8 fl = s.b(0x109);
        p.set(A + 0x00, cvt(s.b(0x10C)) * k001);
        p.set(A + 0x04, cvt(s.b(0x10D)) * k001);
        p.set(A + 0x08, cvt(s.b(0x111)) * k001);
        p.set8(A + 0x0C, fl & 1), p.set8(A + 0x0D, (fl >> 1) & 1), p.set8(A + 0x0E, (fl >> 2) & 1), p.set8(A + 0x0F, (fl >> 3) & 1);
        u32 word = p.w(A + 0x0C);
        p.set8(A + 0x30, (word & 0xFFFF) != 0 && !(p.f(A + 0x00) == k0));
        p.set8(A + 0x31, (word & 0xFFFF0000u) != 0 && !(p.f(A + 0x08) == k0));
        const F c_gain = cvt(s.b(0x114)) * k001;
        p.set(A + 0x10, c_gain);
        p.set(A + 0x14, cvt(s.b(0x115)) * k001);
        p.set8(A + 0x1C, (fl >> 4) & 1), p.set8(A + 0x1D, (fl >> 5) & 1), p.set8(A + 0x1E, (fl >> 6) & 1), p.set8(A + 0x1F, fl >> 7);
        p.set(A + 0x18, cvt(s.b(0x116)) * k001);
        word = p.w(A + 0x1C);
        p.set8(A + 0x32, (word & 0xFFFF) != 0 && !(c_gain == k0));
        p.set8(A + 0x33, (word & 0xFFFF0000u) != 0 && !(p.f(A + 0x18) == k0));
        p.set(A + 0x24, cvt(s.b(0x10E)) * c.k.tenth);
        p.set(A + 0x28, cvt(s.b(0x10F)) * k001);
        p.set(A + 0x20, cvt(s.b(0x110)) * c.k.tenth);
        const u8 lvl = s.b(0x117);
        p.set(A + 0x2C, lvl == 100 || lvl == 0 ? k1 : cvt(lvl) * k001);
    }
    // Steering: full lock, the three speed curves (0x1DB220: x = byte / 3.6 m/s, y = byte x 0.2 deg), Ackermann ratios,
    // the steer-coupled camber.
    p.set(0x58, cvt(s.b(0x35)) * c.k.deg);
    auto steer_curve = [&](u32 t, u32 x, u32 y, u32 n, u32 sx, u32 sy) { // 0x1DB220
        for (u32 i = 0; i < n; ++i) {
            p.set(x + 4 * i, cvt(s.b(sx + i)) * c.k.kmh);
            p.set(y + 4 * i, (cvt(s.b(sy + i)) * c.k.fifth) * c.k.deg);
        }
        table_init(c, t, s32(n), x, y);
    };
    steer_curve(0x5C, 0x68, 0x80, s.b(0xE2), 0xE3, 0xE9);
    steer_curve(0x98, 0xA4, 0xBC, s.b(0xFC), 0xFD, 0x103);
    steer_curve(0xD4, 0xE0, 0xF8, s.b(0xEF), 0xF0, 0xF6);
    {
        const F wb = p.f(0x18), ht = p.f(0x28);
        p.set(0x110, atan2(c.h, wb, wb - ht) / c.k.quarter_pi);
        p.set(0x114, atan2(c.h, wb, wb + ht) / c.k.quarter_pi);
    }
    if (const u8 sc = s.b(0x18); sc == 0) p.set32(0xE1C, 0);
    else p.set(0xE1C, ee::sin(cvt(sc) * c.k.deg));
    // Camber and toe per axle: toe = atan2((byte - 128) x 0.0005, radius), camber offset, its cached table value, the visual
    // camber gain and swing-arm length.
    for (u32 a = 0; a < 2; ++a) {
        const F radius = p.f(0x274 + 4 * a);
        const F toe = (cvt(s32(s.b(0x60 + a)) - 0x80) * kHalf) * c.k.milli;
        p.set(0xE20 + 4 * a, atan2(c.h, toe, radius));
        const F camber = -((cvt(s.b(0x5A + a)) * c.k.tenth) * c.k.deg);
        p.set(0xDFC + 4 * a, camber);
        p.set(0xE04 + 4 * a, cvt(s.sb(0x62 + a)) * c.k.deg);
        p.set(0xE0C + 4 * a, p_lookup(c, 0x340 + 12 * a, p.f(0xDFC + 4 * a), true));
        p.set(0xE14 + 4 * a, cvt(s.b(0x38 + a)) * k001);
    }
    // Inertia parameters: yaw (6000 if 0), pitch (4000) and roll (2000) and the lever scales.
    {
        const u8 y = s.b(0x2E);
        const F yaw = y ? cvt(y) * k100 : F::raw(0x45BB8000);
        p.set(0x118, yaw);
        p.set(0x11C, k1 / yaw);
        const u8 pi = s.b(0x2F);
        const F pitch = pi ? cvt(pi) * k100 : F::raw(0x457A0000);
        F a = p.f(0x10) * c.k.two_pi, bb = p.f(0x14) * c.k.two_pi;
        p.set(0x120, a / pitch);
        p.set(0x124, bb / pitch);
        const u8 ro = s.b(0x30);
        const F roll = ro ? cvt(ro) * k100 : F::raw(0x44FA0000);
        a = p.f(0x28) * c.k.two_pi, bb = p.f(0x2C) * c.k.two_pi;
        p.set(0x128, a / roll);
        p.set(0x12C, bb / roll);
        // Springs and dampers.
        spring_block(c, 0x2B8, 0x3A, s.b(0x5E));
        spring_block(c, 0x2FC, 0x4A, s.b(0x5F));
        // Ride heights (both read spec 0x5C) and the load-transfer heights.
        const F rh = cvt(s.b(0x5C)) * c.k.milli;
        const F wfr = p.f(0x24), wre = p.f(0x20), tr = p.f(0x1C), wb = p.f(0x18);
        p.set(0x40, rh);
        // (Rule::ParamsBuild: from 2002 Tokyo-Seoul on the rear ride height reads its own spec byte)
        const Program conv = c.in.data->behaviour().rule(Rule::ParamsBuild);
        const F rh2 = cvt(s.b(conv >= Program::ConceptSeoul ? 0x5D : 0x5C)) * c.k.milli;
        p.set(0x44, rh2);
        const F mid = rh * wfr + rh2 * wre;
        p.set(0x3C, mid);
        const F h = ((cvt(s.b(0x31)) * c.k.milli) + mid) * kHalf;
        const F lon = h / wb, lat = h / tr;
        p.set(0x38, lon);
        p.set(0x34, lat);
        // Mass: catalogue weight + 120 kg, its inverse and weight; the sprung mass and its loads per axle.
        if (const s16 kg = s.sh(0x2A); kg != 0) {
            F m = cvt(kg);
            const F unsprung = p.f(0x2F0) + p.f(0x334);
            p.set(0x48, m);
            const F u2 = unsprung + unsprung;
            // (the later builds: a layout 7 car carries the static block's first weight alone)
            if (!gt3_program(conv) && p.b(0x1F8) == 7) m = m + cvt(c.st(3));
            else m = m + cvt(s32(c.st(3)) + c.st(4));
            const F sprung = m - u2;
            p.set(0x48, m);
            p.set(0x4C, k1 / m);
            p.set(0xE50, m * c.k.g);
            if (!(sprung <= k0)) {
                const F share = sprung / m;
                const F wsp = sprung * c.k.g;
                p.set(0x50, k1 / sprung);
                p.set(0x34, sprung * lat);
                p.set(0xE58, wsp * wre);
                p.set(0xE54, wsp * wfr);
                p.set(0x38, share * lon);
            }
        }
        // The pitch-tyre terms (static byte x 0.01 x 2 pi over the pitch inertia + m d^2).
        const F m = p.f(0x48), da = p.f(0x10), db = p.f(0x14);
        const F na = (m * da) * da, nb = (m * db) * db;
        // (2002 Tokyo-Seoul v2.00: none for the special cars of kind 7)
        const F t = (cvt(conv == Program::ConceptSeoul2 && s.b(0x11) == 7 ? 0 : c.st(7)) * k001) * c.k.two_pi;
        p.set(0x130, t / (pitch + na));
        p.set(0x134, t / (pitch + nb));
    }
    for (u32 a = 0; a < 2; ++a) spring_static(c, 0x2B8 + 0x44 * a, a);
    top_speed_start(c);
    return 0;
}

ParamImage race_params(const data::CarDatabase& db, const ProgramData& d, Equipment eq, int mode, bool asm_on, bool tcs_on,
                       ConvertInputs in) {
    Spec spec{};
    build_spec(db, d, eq, 0, mode, false, spec);
    if (!asm_on) {
        using T = ProgramData::Table;
        const bool keep_e = !gt3_program(d.behaviour().rule(Rule::Ascc)) && d.has(T::RaceSwitches) && d.b(T::RaceSwitches, 5) != 0;
        spec[0x109] = keep_e ? u8(spec[0x109] & 0xC0) : 0;
    }
    if (!tcs_on) spec[0x110] = 0;
    in.data = &d;
    in.physics_globals();
    ParamImage p{};
    convert_params(spec, in, p);
    return p;
}

EnginePeak engine_peak(const ParamImage& image, const ConvertInputs& in) {
    ParamImage copy = image;
    Spec none{};
    const Ctx c{Img{none.data()}, Img{copy.data()}, in, condition(*in.data)};
    return engine_peak_of(c);
}

GearChart gear_chart(const data::CarDatabase& db, Equipment eq) {
    static const ProgramData none = ProgramData::synthetic();
    Spec spec{};
    build_spec(db, none, eq, 0, 0, false, spec); // (0x1DA308's fix-ups after the builder touch no byte read here)
    ConvertInputs in;
    in.data = &none;
    ParamImage image{};
    Ctx c{Img{spec.data()}, Img{image.data()}, in, condition(none)};
    const u32 E = 0x138, Dv = 0x1F8; // (the program builds both blocks on the stack; the offsets do not matter)
    engine(c, E, 0);
    driveline(c, Dv, E);
    // 0x1E2450
    const Img& p = c.p;
    GearChart g;
    g.n = p.b(Dv + 2);
    for (s32 i = 0; i <= g.n && i < 8; ++i) {
        const u32 k = u32(i);
        const F factor = p.f(Dv + 0x5C + 4 * k);
        auto speed = [&](u16 rpm) {
            F w = cvt(rpm) * c.k.rpm; // 0x3503A0 / 0x3503A4: 2 pi / 60
            if (i == 0) w = -w;
            return w / factor;
        };
        g.rpm_lo[k] = i < 2 ? 1000 : p.h(Dv + 0x30 + 2 * (k - 2));
        g.speed_lo[k] = speed(g.rpm_lo[k]);
        g.rpm_hi[k] = i == 0 || i == g.n ? p.h(E + 0x8C) : p.h(Dv + 0x2E);
        g.speed_hi[k] = speed(g.rpm_hi[k]);
    }
    return g;
}

Showroom showroom_build(const data::CarDatabase& db, const ProgramData& d, Equipment eq, const std::array<u32, 8>& condition) {
    // The showroom's rule record 0x2D2988 (PAL 0x2D3F48, NTSC-J 0x2CF680), the same bytes on every disc: no power cap (byte
    // 3), the CPU grip percentages (read only for control source 2).
    constexpr std::array<u8, 12> kShowroomRules = {80, 0, 0, 0, 100, 100, 100, 100, 100, 100, 100, 100};
    Showroom s;
    build_spec(db, d, eq, 0, 0, false, s.spec);
    ConvertInputs in;
    in.data = &d;
    in.control = 0;
    in.rules = kShowroomRules;
    in.condition = condition;
    in.physics_globals();
    convert_params(s.spec, in, s.p);
    s.peak = engine_peak(s.p, in);
    // (Rule::Layout: the later builds' showroom, Concept 2001 Tokyo 0x1D42F0 through 0x1DB098, adds a layout 7 car's (AWD
    // system 5) motors to the peak: the two front motors' and the rear motor's ratings, the Hybrid table's bytes +0 / +1,
    // over 0.7355 kW a PS)
    if (!gt3_program(d.behaviour().rule(Rule::Layout)) && d.has(ProgramData::Table::Hybrid) && s.spec[0xB2] == 2 && s.spec[0xB3] == 5) {
        const F front = int_to_f(d.b(ProgramData::Table::Hybrid, 0)), rear = int_to_f(d.b(ProgramData::Table::Hybrid, 1));
        s.peak.ps = s.peak.ps + ((front + front) + rear) / F::raw(0x3F3C49BA);
    }
    return s;
}

s32 shown_power(F ps, bool hp) {
    return to_int(hp ? ps * F::raw(0x3F7C76F3) : ps); // 0x350A1C: 0.98619
}

} // namespace gt3r::sim::exact
