// Phase 12's effects: tyre smoke and sound, engine sound levels, pad vibration and steering force; see tick.h.

#include "base/ee_math.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

const F kZero = F::raw(0), kQuarter = F::raw(0x3E800000), kHalf = F::raw(0x3F000000), kOne = F::raw(0x3F800000),
        kMinusOne = F::raw(0xBF800000), kEight = F::raw(0x41000000), k127 = F::raw(0x42FE0000);

// Truncated to an integer, then 127 for anything >= 128.
s32 cap127(F x) {
    const s32 n = ee::to_int(x);
    return n < 0x80 ? n : 0x7F;
}

// Surface index -> effect class (the jump table on surface - 2): loose 2..5 -> 1, 6 and 12..14 -> 2, else 0.
int effect_class(u8 surface) {
    const u32 k = u32(surface) - 2;
    if (k >= 13) return 0;
    if (k <= 3) return 1;
    if (k == 4 || k >= 10) return 2;
    return 0;
}

} // namespace

// The PAL program's routines leave a car the AI drives (control source 1 or 2, or a driver kind) at no vibration and a centred
// wheel force; NTSC-U computes them for every car and only its pad writer skips those cars.
static bool pal_ai_driven(const Car& c, Rule r) { return pal_rules(c, r) && (u32(u8(c.control)) - 1 < 2 || c.driver_kind != 0); }

void pad_rumble(Car& c) {
    if (pal_ai_driven(c, Rule::PadRumble)) {
        c.x16d4 = 0, c.rumble = 0;
        return;
    }
    // (2002 Tokyo-Geneva's constants one unit lower: Europe)
    const F cap = folded(c.behaviour, 0x3EAA7EFA, 0x3EAA7EF9);      // 0.333
    const F fx_scale = folded(c.behaviour, 0x46992000, 0x46991FFF); // 19600
    F a = c.w[0].slip_ratio * c.w[0].slip_ratio;
    F b = c.w[1].slip_ratio * c.w[1].slip_ratio;
    const F drive = c.w[0].fx_drive + c.w[1].fx_drive;
    a = a + b;
    a = a * kHalf;
    F d = a + folded(c.behaviour, 0x3DCCCCCD, 0x3DCCCCCC); // 0.1
    d = d * drive;
    if (d < kZero) d = -d;
    d = d * folded(c.behaviour, 0x3A2A7EFA, 0x3A2A7EF9);
    if (cap < d) d = cap;
    F br = c.w[0].fx_brake + c.w[1].fx_brake;
    br = br / fx_scale;
    br = br * br;
    if (cap < br) br = cap;
    F hit = kZero;
    const F forces = d + br;
    if (kZero < c.hit_rumble) hit = c.hit_rumble * kEight + kQuarter;
    F bump = kZero, kerb = kZero;
    for (size_t i = 0; i < 4; ++i) {
        if (c.race_hold != 0) continue;
        const Wheel& w = c.w[i];
        F x = w.damper_f;
        if (kZero < x) {
            x = x / fx_scale;
            bump = bump + (kOne < x ? kOne : x);
        }
        if (i < 2 && w.surface == 1) {
            F k = w.load / folded(c.behaviour, 0x4537C000, 0x4537BFFF); // 2940
            const F v = c.speed * F::raw(0x3C800000);
            k = k * v;
            if (k < kZero) k = -k;
            kerb = kerb + (kQuarter < k ? kQuarter : k);
        }
    }
    F level = bump + kerb;
    if (level < forces) level = forces;
    if (level < hit) level = hit;
    c.rumble = u8(cap127(level * F::raw(0x43800000)));
}

void steer_force(Car& c, const SteerFeel& s) {
    if (pal_ai_driven(c, Rule::SteerForce)) {
        c.steer_force = 0x80;
        return;
    }
    F x = c.w[0].force_a + c.w[1].force_a;
    x = x / c.p.xe54;
    if (s.use_x420 != 0) x = x * c.x420;
    x = x * s.lat_gain;
    if (kOne < x) x = kOne;
    else if (x < kMinusOne) x = kMinusOne;
    F out = kZero;
    const F y = pedal_curve(s.curve, x);
    const s32 dp = (s32(c.wheel_pos) - s32(c.wheel_pos_prev)) * (pal_rate(c) ? 50 : 60); // steps a second
    F st = c.steer_cmd;
    const F pad = ee::int_to_f(dp) * s.pad_gain;
    if (s.steer_max < st) st = s.steer_max;
    else if (st < -s.steer_max) st = -s.steer_max;
    const F steer = s.steer_gain * st;
    x = y + pad;
    x = x + steer;
    for (size_t i = 0; i < 2; ++i) {
        F b = c.t[i].slip_angle;
        if (s.slip_max < b) b = s.slip_max;
        else if (b < -s.slip_max) b = -s.slip_max;
        x = x + s.slip_gain * b;
    }
    switch (s.mode) {
    case 0:
        out = x;
        c.ffb = x;
        break;
    case 1: {
        const F t = c.ffb + x;
        c.ffb = x;
        out = t * kHalf;
        break;
    }
    case 2: {
        const F o = c.ffb;
        F r = x;
        if (kZero <= x) {
            if (x < o) r = o;
        } else if (o < x) r = o;
        out = r;
        c.ffb = x;
        break;
    }
    case 3: {
        const F o = c.ffb;
        s32 n = c.ffb_count;
        if (n < (pal_rate(c) ? 50 : 60)) c.ffb_count = ++n;
        F r = o;
        if (kZero <= o) {
            if (n >= (pal_rate(c) ? 6 : 7)) {
                const F v = o - c.dt * kEight;
                c.ffb = v;
                r = v;
                if (v < kZero) r = c.ffb = kZero;
            }
            if (r < x || x < kZero) {
                c.ffb = x;
                c.ffb_count = 0;
                r = x;
            }
        } else {
            if (n >= (pal_rate(c) ? 6 : 7)) {
                const F v = o + c.dt * kEight;
                c.ffb = v;
                r = v;
                if (kZero < v) r = c.ffb = kZero;
            }
            if (x < r || kZero < x) {
                c.ffb = x;
                c.ffb_count = 0;
                r = x;
            }
        }
        out = r;
        break;
    }
    default:
        break;
    }
    // The front tyres' aligning term: each wheel's lateral axis (body lateral and forward axes turned by its steer angle)
    // against the road normal under it, times its load.
    F align = kZero;
    for (size_t i = 0; i < 2; ++i) {
        const F cs = ee::cos(c.w[i].steer), sn = ee::sin(c.w[i].steer);
        const F d[3] = {cs * c.lat.x - sn * c.fwd.x, cs * c.lat.y - sn * c.fwd.y, cs * c.lat.z - sn * c.fwd.z};
        const Vec3& g = c.g[i].normal;
        const F n[3] = {g.x, -g.z, g.y}; // course (x, height, z) -> physics frame
        F dot = kZero;
        for (size_t k = 0; k < 3; ++k) dot = dot + d[k] * n[k];
        align = align + dot * c.w[i].load;
    }
    // (2002 Tokyo-Geneva's constants: Europe)
    F k = c.p.susp[0].k + folded(c.behaviour, 0x473F6800, 0x473F67FF); // 49000
    k = k / folded(c.behaviour, 0x483F6800, 0x483F67FF);              // 196000
    F h = c.vel.x * c.vel.x;
    h = h + c.vel.y * c.vel.y;
    align = align * k;
    F t = align * s.align_gain;
    t = t / c.p.xe54;
    out = out + t;
    h = ee::sqrt(h);
    if (h < folded(c.behaviour, 0x3F0E38E4, 0x3F0E38E3)) { // 2 km/h
        F st2 = c.steer_cmd;
        const F hi = folded(c.behaviour, 0x3DB2B8C2, 0x3DB2B8C1), lo = folded(c.behaviour, 0xBDB2B8C2, 0xBDB2B8C1);
        if (hi < st2) st2 = hi;                                        // 5 degrees
        else if (st2 < lo) st2 = lo;
        out = st2 * folded(c.behaviour, 0x40B758B4, 0x40B758B5);       // 1 / 10 degrees
    }
    if (kOne < out) out = kOne;
    else if (out < kMinusOne) out = kMinusOne;
    c.steer_force = u8(-0x80 - ee::to_int(out * k127));
}

void tyre_effects(Car& c, const EffectTables& t, u8 course_kind) {
    F dirt = kZero;
    for (size_t i = 0; i < 4; ++i) {
        Wheel& w = c.w[i];
        if (w.load == kZero) {
            if (w.smoke_count != 0) --w.smoke_count;
        } else
            w.smoke_count = pal_rate(c) ? 5 : 6;
        if (w.smoke_count == 0) {
            w.smoke_emit = kZero;
            w.skid_pitch = kOne;
            w.slip_intensity = kZero;
            continue;
        }
        const int cls = effect_class(w.surface);
        F angle = c.t[i].slip_angle;
        if (angle < kZero) angle = -angle;
        const F cs = ee::cos(w.steer), sn = ee::sin(w.steer);
        F lat = cs * w.hub_vy + sn * w.hub_vx;
        if (lat < kZero) lat = -lat;
        const F hub = cs * w.hub_vx - sn * w.hub_vy;
        F slip = hub - w.rim_speed;
        if (hub < kZero) slip = -slip;
        const F ratio = w.slip_ratio;
        // sqrt(b(..)^2 + a(..)^2) of a channel, and the speed ramp above r0.
        auto magnitude = [&](int ch, F& ramp) {
            const EffectCurves& e = t.set[size_t(ch)][size_t(cls)];
            const F a = lookup(e.a, t.by_lat[size_t(ch)][size_t(cls)] != 0 ? lat : angle);
            const F b = lookup(e.b, t.by_slip[size_t(ch)][size_t(cls)] != 0 ? slip : ratio);
            ramp = w.speed_ramp - e.r0;
            return ee::sqrt(b * b + a * a);
        };
        if (w.surface != 1 && w.surface != 13) {
            const EffectCurves& e0 = t.set[0][size_t(cls)];
            F ramp;
            F m = magnitude(0, ramp);
            if (kZero < ramp) m = m + ramp * e0.g;
            w.slip_intensity = m == kZero ? m : m + e0.base;
            const EffectCurves& e1 = t.set[1][size_t(cls)];
            const F m1 = magnitude(1, ramp);
            F p = e1.base + m1;
            if (kZero < ramp) p = p + ramp * e1.g;
            w.skid_pitch = p;
        } else {
            F k = w.load / folded(c.behaviour, 0x45192000, 0x45191FFF); // 2450 (Europe's)
            k = k - kQuarter;
            if (k < kZero) k = kZero;
            else if (kOne < k) k = kOne;
            F rim = w.rim_speed;
            if (rim < kZero) rim = -w.rim_speed;
            F e = rim - folded(c.behaviour, 0x3FB1C71D, 0x3FB1C71B); // 5 km/h
            e = e * folded(c.behaviour, 0x3E1374BC, 0x3E1374BD);
            if (e < kZero) e = kZero;
            else if (kOne < e) e = kOne;
            e = e * k;
            w.slip_intensity = e * t.kerb_scale;
            F p;
            if (rim <= t.kerb_x0) p = t.kerb_y0;
            else if (t.kerb_x1 <= rim) p = t.kerb_y1;
            else p = t.kerb_y0 + ((rim - t.kerb_x0) * (t.kerb_y1 - t.kerb_y0)) / (t.kerb_x1 - t.kerb_x0);
            w.skid_pitch = p;
        }
        const EffectCurves& e2 = t.set[2][size_t(cls)];
        F ramp;
        F m = magnitude(2, ramp);
        if (kZero < ramp) m = m + ramp * e2.g;
        if (!gt3_program(c.behaviour.rule(Rule::TyreEffects)) && w.surface == 8) m = kZero; // (the later builds)
        if (!(m == kZero)) m = m + e2.base;
        if (kOne < m) m = kOne;
        w.smoke_emit = m;
        if (cls != 0) dirt = dirt + m;
    }
    const u8 kind = course_kind == 0xFF ? 0 : course_kind;
    if (kind == 0) dirt = dirt * kEight;
    F add = dirt * folded(c.behaviour, 0x3A83126F, 0x3A83126E); // 0.001
    add = add * c.dt;
    // Condition word 3 += add, at most 0.8.
    c.dirt = c.dirt + add;
    if (const F cap = folded(c.behaviour, 0x3F4CCCCD, 0x3F4CCCCC); cap < c.dirt) c.dirt = cap; // (Europe's)
}

void phase12_stub(Car&) {}

void engine_sound(Car& c, const std::array<F, 4>& scales) {
    const F k375 = F::raw(0x3EC00000);
    F l = c.turbo_load;
    if (kOne < l) l = kOne;
    F a = l * F::raw(0x40400000);
    F d = l - k375;
    a = a + kOne;
    a = a * F::raw(0x42000000);
    a = a * scales[1];
    const s32 n1 = cap127(a);
    if (d < kZero) d = -d;
    d = d + k375;
    F v = d * (c.clutch == 1 ? kOne : kHalf);
    v = v * k127;
    u32 s = u32(c.x4e5) + u32(n1);
    c.x4e5 = u8(s32(s + (s >> 31)) >> 1);
    v = v * scales[0];
    F sp = ee::int_to_f(s32(c.turbo_spool));
    s = u32(c.x4e4) + u32(cap127(v));
    c.x4e4 = u8(s32(s + (s >> 31)) >> 1);
    sp = sp * scales[2];
    c.turbo_spool = u8(cap127(sp));
    c.blowoff = c.blowoff * scales[3];
}

int draw_mode(const Car& c, u32 x70, u32 ghost_switch) {
    if (c.ghost == 0) return 3;
    if ((x70 == 0 || ntsc_j_rules(c, Rule::DrawMode)) && ghost_switch == 0) return 1; // the NTSC-J program: no +0x70 test
    if (c.control == 1 || c.driver_kind == 1) return 0;
    return 2;
}

void effects_reset(Car& c, u32 x70, u32 ghost_switch) {
    if (draw_mode(c, x70, ghost_switch) == 3) return;
    for (Wheel& w : c.w) {
        w.smoke_emit = kZero;
        w.slip_intensity = kZero;
        w.skid_pitch = kOne;
    }
    c.x4e4 = c.x4e5 = c.turbo_spool = 0;
}

} // namespace gt3r::sim::exact
