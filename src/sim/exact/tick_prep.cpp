// Phase 2, the per-tick preparation; see tick.h.

#include "sim/exact/build_math.h"
#include "base/ee_double.h"
#include "base/ee_math.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

constexpr F kOne = F::raw(0x3F800000);

s8 low_byte(s32 v) { return s8(u8(u32(v))); } // sb

// 1 - a - b in the program's double: (double)1 - (double)a, minus (double)b, rounded back to a single (the build's soft
// doubles: build_math.h).
F one_minus(const Behaviour& h, F a, F b) {
    return narrow(h, sub(h, sub(h, ee::D::raw(0x3FF0000000000000ull), widen(h, a)), widen(h, b)));
}

} // namespace

Condition condition_params(const std::array<u8, 9>& b, bool toward_zero) {
    const F milli = F::raw(toward_zero ? 0x3A83126E : 0x3A83126F); // 0.001
    auto tenk = [](u32 byte) { return ee::int_to_f(s32(byte * 10000)); };
    Condition k;
    k.c1 = ee::int_to_f(b[1]) * milli;
    k.e1 = -tenk(b[0]);
    k.e2 = tenk(b[2]);
    k.e3 = tenk(b[3]) + k.e2;
    k.c2 = ee::int_to_f(b[4]) * milli;
    k.co = ee::int_to_f(b[6]) * milli;
    k.o1 = -tenk(b[7]);
    k.omax = tenk(u32(b[5]) + b[7]);
    k.x20 = tenk(b[8]);
    return k;
}

F condition_factor(const Condition& k, F e, F o) {
    F fe = kOne;
    if (e < k.e1) fe = kOne - k.c1;
    else if (e < F()) fe = kOne - e * k.c1 / k.e1;
    else if (k.e2 < e) fe = e < k.e3 ? kOne - (e - k.e2) * k.c2 / (k.e3 - k.e2) : kOne - k.c2;
    F fo = kOne;
    if (o < k.o1) fo = k.co + kOne;
    else if (o < F()) fo = o * k.co / k.o1 + kOne;
    return fe * fo;
}

F condition_factor(const Behaviour& b, const Condition& k, F e, F o) {
    return gt3_program(b.rule(Rule::EngineCondition)) ? condition_factor(k, e, o) : kOne;
}

F full_throttle_torque(const Car& c, const Condition& k, F omega) {
    return lookup_bisect(c.p.e.torque, omega) * condition_factor(c.behaviour, k, c.engine_wear, c.oil);
}

F friction_torque(const Car& c, F omega) { return c.p.e.friction * omega; }

void step_x51d(Car& c) {
    if (c.x51d != 0 && c.x51d != 0xFF) --c.x51d;
}

void step_length(Car& c) {
    const bool pal = pal_rate(c); // PAL: 50 steps a second
    // 1/60 (PAL 1/50; 2002 Tokyo-Geneva Asia's 1/60 one unit lower, Asia 0x1E37D8)
    c.dt = c.time_scale * (pal ? F::raw(0x3CA3D70A) : folded(c.behaviour, 0x3C888889, 0x3C888888));
    c.inv_dt = F::raw(pal ? 0x42480000 : 0x42700000) / c.time_scale; // 60 (PAL 50)
}

void aero(Car& c, const Slipstream& s) {
    const F v2 = c.speed * c.speed;
    if (c.slip_target == F()) {
        const F y = c.slip_filter - s.k_down * c.dt;
        c.slip_filter = y < F() ? F() : y;
    } else {
        const F y = c.slip_filter + c.slip_target * s.k_up * c.dt;
        c.slip_filter = kOne < y ? kOne : y;
    }
    const F u = kOne - c.slip_filter;
    const F d = c.p.drag_coef * ((s.drag_share + (kOne - s.drag_share) * u) * v2);
    c.drag = F() <= c.speed ? -d : d;
    const F a = (s.downforce_share + (kOne - s.downforce_share) * u) * v2;
    for (size_t j = 0; j < 2; ++j) c.downforce[j] = c.p.downforce_coef[j] * a;
}

bool wear_grip(Car& c, const WearParams& cp) {
    if (cp.max == F()) return false;
    if (c.x535 & 0x10) return false;
    for (size_t i = 0; i < 4; ++i) {
        Wheel& w = c.w[i];
        const F x = w.wear;
        if (cp.max <= x) {
            w.wear = cp.max;
            w.wear_level = 0x7F;
            w.wear_grip = kOne - cp.max_loss;
        } else if (cp.knee2 <= x) {
            const F t = (x - cp.knee2) / (cp.max - cp.knee2);
            w.wear_grip = one_minus(c.behaviour, cp.loss2, t * (cp.max_loss - cp.loss2));
            w.wear_level = kOne <= t ? s8(0x7F) : low_byte(ee::to_int(t * folded(c.behaviour, 0x427F999A, 0x427F9999)) + 0x40); // 63.9 (Europe 0x1E1200's)
        } else if (cp.knee1 <= x) {
            const F t = (x - cp.knee1) / (cp.knee2 - cp.knee1);
            w.wear_grip = one_minus(c.behaviour, cp.loss1, t * (cp.loss2 - cp.loss1));
            w.wear_level = kOne <= t ? s8(0x40) : low_byte(ee::to_int(t * F::raw(0x41FF3333)) + 0x20); // 31.9
        } else if (F() <= x) {
            const F t = x / cp.knee1;
            w.wear_grip = kOne - cp.loss1 * t;
            w.wear_level = kOne <= t ? s8(0x20) : low_byte(ee::to_int(t * F::raw(0x41FF3333)));
        } else if (cp.fresh <= x) {
            const F t = -x / -cp.fresh;
            w.wear_grip = kOne - cp.fresh_loss * t;
            w.wear_level = kOne <= t ? s8(-0x3F) : low_byte(-ee::to_int(t * folded(c.behaviour, 0x427F999A, 0x427F9999)));
        } else {
            w.wear_level = s8(-0x80);
            w.wear_grip = kOne;
        }
        const F floor = tire_record(c.p, int(i), w.surface).mu[11];
        if (w.wear_grip < floor) w.wear_grip = floor;
    }
    return true;
}

F load_per_width(const Params& p, int wheel, F load) { return load / p.width[size_t(wheel >> 1)]; }

void peak_force(Car& c, const WearParams& cp) {
    const bool wear = wear_grip(c, cp);
    const F d = c.roll - c.ground_roll; // body roll - ground roll
    const F side[2] = {-d, d};
    for (int i = 0; i < 4; ++i) {
        Wheel& w = c.w[size_t(i)];
        const TireRecord& r = tire_record(c.p, i, w.surface);
        F f = w.load * lookup(r.load_curve, load_per_width(c.p, i, w.load));
        if (wear) f = f * w.wear_grip;
        const size_t k = size_t(i >> 1);
        const F camber = side[i & 1] + c.p.camber[k];
        f = f * c.p.axle_grip[k];
        const F g = lookup(c.p.camber_grip[k], camber);
        w.peak_force = f * r.mu[w.surface] * g;
    }
}

F slip_angle(F vx, F vy, F& fade) {
    const F lo = F::raw(0x3E8E38E4); // 1 km/h
    const F v = ee::sqrt(vy * vy + vx * vx);
    if (F::raw(0x4031C71D) <= v) fade = kOne; // 10 km/h
    else if (v <= lo) fade = F();
    else fade = (v - lo) * F::raw(0x3ECCCCCD); // 0.4
    if (fade == F()) return F();
    return ee::atan2(-vy, vx);
}

F slip_angle(const Behaviour& b, F vx, F vy, F& fade) {
    const Program p = b.rule(Rule::SlipAngle);
    if (gt3_program(p)) return slip_angle(vx, vy, fade);
    // 2002 Tokyo-Geneva's compiler folded 1 / 3.6, 10 / 3.6 and the slope between them in arithmetic that rounds toward zero.
    const bool geneva = p >= Program::ConceptGenevaEu;
    const F lo = F::raw(geneva ? 0x3E8E38E3 : 0x3E8E38E4), hi = F::raw(geneva ? 0x4031C71B : 0x4031C71D);
    const F v = ee::sqrt(vy * vy + vx * vx);
    if (hi <= v) fade = kOne;
    else if (v <= lo) fade = F();
    else fade = (v - lo) * F::raw(geneva ? 0x3ECCCCCF : 0x3ECCCCCD);
    if (fade == F()) return F();
    return atan2(b, -vy, vx);
}

void slip_angles(Car& c) {
    for (Wheel& w : c.w) w.slip_dir = slip_angle(c.behaviour, w.hub_vx, w.hub_vy, w.slip_fade);
}

void prep(Car& c, const PrepInput& in) {
    c.race_hold = in.start_count > 0 ? 1 : 0; // 0x1EAAE8
    if (c.hold) --c.hold;
    step_x51d(c);
    step_length(c);
    aero(c, in.slip);
    c.torque_wot = full_throttle_torque(c, in.condition, c.omega);
    c.torque_friction = friction_torque(c, c.omega);
    peak_force(c, in.wear);
    slip_angles(c);
}

} // namespace gt3r::sim::exact
