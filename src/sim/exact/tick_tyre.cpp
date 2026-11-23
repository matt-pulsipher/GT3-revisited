// Phase 7's tyre forces: rolling resistance, the slip-ratio curves, the longitudinal and slip-angle forces; see tick.h.

#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

constexpr F kPi = F::raw(0x40490FDB);
constexpr F kHalfPi = F::raw(0x3FC90FDB), kNegHalfPi = F::raw(0xBFC90FDB);
constexpr F kOne = F::raw(0x3F800000), kNegOne = F::raw(0xBF800000);

// 0x1D9098: two tables on table c's axis at once (the same point k for both): c's y and y2's.
void lookup2(const Table<12>& c, const std::array<F, 12>& y2, F v, F& out_c, F& out_2) {
    const u32 last = u32(s32(c.n) - 1);
    if (v <= c.x[0]) {
        out_c = c.y[0];
        out_2 = y2[0];
        return;
    }
    if (c.x[last] <= v) {
        out_c = c.y[last];
        out_2 = y2[last];
        return;
    }
    u32 k = 1;
    while (k < last && !(v < c.x[k])) ++k;
    const F dv = v - c.x[k - 1];
    const F dx = c.x[k] - c.x[k - 1];
    out_c = c.y[k - 1] + dv * (c.y[k] - c.y[k - 1]) / dx;
    out_2 = y2[k - 1] + dv * (y2[k] - y2[k - 1]) / dx;
}

} // namespace

F slip_angle_fold(const Behaviour& h, F b, F& sign) {
    // (2002 Tokyo-Geneva's pi / 2 and pi one unit lower: Europe 0x1DF048)
    const F pi = folded(h, kPi.bits, 0x40490FDA);
    if (b < folded(h, kNegHalfPi.bits, 0xBFC90FDA)) {
        sign = kOne;
        return b + pi;
    }
    if (b < F()) {
        sign = kNegOne;
        return -b;
    }
    if (b < folded(h, kHalfPi.bits, 0x3FC90FDA)) {
        sign = kOne;
        return b;
    }
    sign = kNegOne;
    return pi - b;
}

void rolling_resistance(Car& c, const Statics& s) {
    const F u = c.speed;
    c.x4d0 = F();
    c.drive_sum = F();
    c.x4d4 = F();
    c.a_x = F();
    c.rolling = F();
    F load = c.x4d0;
    for (const Wheel& w : c.w) load = load + w.load * s.rolling_mu[w.surface];
    const F rr = F() < u ? -lookup(s.rolling_curve, u) : lookup(s.rolling_curve, -u);
    c.rolling = rr * load;
}

void tyre_curves(Car& c) {
    for (int i = 0; i < 4; ++i) {
        Wheel& w = c.w[size_t(i)];
        const TireRecord& r = tire_record(c.p, i, w.surface);
        lookup2(r.curve_c, r.curve_d, w.slip_ratio, c.t[size_t(i)].long_frac, w.circle);
    }
}

void tyre_long_force(Car& c, const Statics& s) {
    for (size_t i = 0; i < 4; ++i) {
        Drive& t = c.t[i];
        Wheel& w = c.w[i];
        F f = t.long_frac * w.peak_force;
        if (t.slip_sign < 0) f = -f;
        const F g = w.slip_gain * f;
        const bool drive = t.slip_state > 0;
        t.react_drive = drive ? g : F();
        t.react_brake = drive ? F() : g;
        w.fx_brake = drive ? F() : (s.slip_gain_on[0] != 2 ? f : g);
        w.fx_drive = drive ? (s.slip_gain_on[1] != 2 ? f : g) : F();
    }
}

void tyre_slip_force(Car& c) {
    for (int i = 0; i < 4; ++i) {
        Wheel& w = c.w[size_t(i)];
        Drive& t = c.t[size_t(i)];
        const TireRecord& r = tire_record(c.p, i, w.surface);
        t.slip_angle = F();
        if (w.slip_fade == F()) {
            w.force_b = F();
            w.force_a = F();
            continue;
        }
        F a = w.steer;
        if (w.hub_vx < F()) a = a + folded(c.behaviour, kPi.bits, 0x40490FDA); // rolling backwards (Europe 0x1DF1D8)
        const F beta = angle_diff(a, w.slip_dir, c.behaviour.rule(Rule::AngleWrap));
        t.slip_angle = beta;
        F sign;
        const F v = slip_angle_fold(c.behaviour, beta, sign);
        F u1 = sign * lookup(r.curve_a, v);
        u1 = u1 * w.slip_fade;
        F u2 = lookup(r.curve_b, v) * w.slip_fade;
        const F m = ee::sqrt(u1 * u1 + u2 * u2);
        if (w.circle < m) {
            const F q = w.circle / m;
            u2 = u2 * q;
            u1 = u1 * q;
        }
        w.force_b = u2 * w.peak_force;
        w.force_a = u1 * w.peak_force;
    }
}

} // namespace gt3r::sim::exact
