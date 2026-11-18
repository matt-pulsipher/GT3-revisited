// Phase 5, the wheels' slip ratios; see tick.h.

#include "base/ee_math.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

const F kOne = F::raw(0x3F800000);
const F kMinusOne = F::raw(0xBF800000);
const F kPercent = F::raw(0x3C23D70A); // 0.01
// 1 / 3.6 and 1 / 57.6 m/s (minus);
// 2002 Tokyo-Geneva's a unit lower (Europe).
struct SlipK {
    F kmh, eps, minus_eps;
};
const SlipK kSlip{F::raw(0x3E8E38E4), F::raw(0x3C8E38E4), F::raw(0xBC8E38E4)};
const SlipK kSlipGeneva{F::raw(0x3E8E38E3), F::raw(0x3C8E38E3), F::raw(0xBC8E38E3)};

} // namespace

F slip_gain(const Statics& sb, int k, F u, const SlipK& K) {
    F g = kOne;
    if (!sb.slip_gain_on[size_t(k)]) return g;
    const F knee = ee::int_to_f(sb.slip_gain_knee[size_t(k)]) * K.kmh;
    const F base = ee::int_to_f(sb.slip_gain_base[size_t(k)]) * kPercent;
    const F room = g - base;
    if (u < F()) u = -u;
    if (u < knee) g = base + room * u / knee;
    return g;
}

void slip_ratio(Drive& t, Wheel& w, F x, const Statics& sb, const SlipK& K) {
    const F kEps = K.eps, kMinusEps = K.minus_eps;
    const F rim = w.rim_speed;
    F s, g;
    t.slip_state = 0;
    if (kMinusEps < rim && rim < kEps) {
        // The wheel is stopped: full slip whenever the hub moves.
        g = slip_gain(sb, 0, w.hub_vx, K);
        if (rim < x) s = kOne, t.slip_sign = -1;
        else if (x < rim) s = kOne, t.slip_sign = 1;
        else s = F(), t.slip_sign = 0;
        t.slip_state = -1;
    } else {
        s = F();
        if (F() < rim) {
            if (x <= rim) {
                s = x - rim;
                g = slip_gain(sb, 1, w.hub_vx, K);
                t.slip_state = 1;
                s = s * g;
                s = s / rim;
                t.slip_sign = 1;
            } else {
                g = slip_gain(sb, 0, w.hub_vx, K);
                if (!(x < kEps && kMinusEps < x)) s = (x - rim) * g / x;
                t.slip_state = -1;
                t.slip_sign = -1;
            }
        } else if (rim <= x) {
            s = x - rim;
            g = slip_gain(sb, 1, w.hub_vx, K);
            s = s * g;
            t.slip_sign = -1;
            s = s / rim;
            t.slip_state = 1;
        } else {
            g = slip_gain(sb, 0, w.hub_vx, K);
            if (!(x < kEps && kMinusEps < x)) s = (x - rim) * g / x;
            t.slip_sign = 1;
            t.slip_state = -1;
        }
    }
    w.slip_gain = g;
    if (kOne < s) s = kOne;
    else if (s < kMinusOne) s = kMinusOne;
    const s8 st = t.slip_state;
    if (st == 0) {
        w.slip_ratio = F();
        return;
    }
    // Low-speed fade by the hub speed (state < 0) or the rim speed (state > 0).
    F r = st < 0 ? x : rim;
    const u8 b = sb.slip_fade[st < 0 ? 0 : 1];
    if (b) {
        const F v0 = ee::int_to_f(b) * K.kmh;
        if (r < F()) r = -r;
        if (r < v0) s = s * (r / v0);
    }
    w.slip_ratio = s;
}

void slip_ratios(Car& c, const Statics& sb) {
    for (size_t i = 0; i < 4; ++i) {
        Wheel& w = c.w[i];
        const F a = w.steer;
        const F along = ee::cos(a) * w.hub_vx;
        slip_ratio(c.t[i], w, along - ee::sin(a) * w.hub_vy, sb, toward_zero(c.behaviour) ? kSlipGeneva : kSlip);
    }
}

} // namespace gt3r::sim::exact
