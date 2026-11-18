// Phase 7's driveline: wheel net torques, torque vectoring, the axle step (clutch torque, AWD coupling, centre clutch) and the
// wheel speeds; see tick.h.

#include "sim/exact/build_math.h"
#include "sim/exact/hybrid.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

constexpr F kOne = F::raw(0x3F800000);
constexpr F kHalf = F::raw(0x3F000000);
constexpr F kFull = F::raw(0x47BF6800); // 98000: the AWD "full transfer" value

bool reverse_locked(const Car& c) { return c.clutch == 1 && c.gear == 0; }

// The full-transfer value as a build's constant: 2002 Tokyo-Geneva's compiler folded 10000 x 9.8 one unit lower.
F full_value(Program p) { return p >= Program::ConceptGenevaEu ? F::raw(0x47BF67FF) : kFull; }


} // namespace

void wheel_net_torque(Car& c) {
    for (size_t i = 0; i < 4; ++i) {
        Drive& t = c.t[i];
        const F f = -c.p.tire_radius[i >> 1] * (t.react_drive + t.react_brake);
        t.torque = f;
        const F w = c.w[i].rim_speed;
        if (w == F()) continue;
        t.torque = F() < w ? f - t.brake_torque : f + t.brake_torque;
    }
}

void torque_vectoring(Car& c, const Hybrid& h) {
    if (hybrid_car(c, h)) hybrid_front_motors(c, h); // (the later builds' layout 7 front motors)
    for (size_t a = 0; a < 2; ++a) {
        const u8 type = c.p.d.diff_type[a];
        if (type < 6 || type >= 8) continue;
        const F v = c.speed;
        // (2002 Tokyo-Geneva's constants: Europe)
        if (v < folded(c.behaviour, 0x4031C71D, 0x4031C71B) || folded(c.behaviour, 0x42480001, 0x4247FFFF) < v) continue; // 10 km/h .. 50 m/s
        const F steer = c.steer_cmd;
        F s = steer < F() ? -steer : steer;
        s = s * folded(c.behaviour, 0x40747645, 0x40747648); // 1 / 15 deg
        if (kOne < s) s = kOne;
        const F tq = s * c.p.d.diff_initial[a];
        if (tq == F()) continue;
        const F w_odd = c.w[2 * a + 1].rim_speed;
        F m = (c.w[2 * a].rim_speed + w_odd) * kHalf;
        const F d = m * F::raw(0x3E000000);
        if (F() < steer) m = m + d;
        else if (steer < F()) m = m - d;
        const F more = tq * F::raw(0x3F100000), less = tq * F::raw(0x3EE00000); // 0.5625 T, 0.4375 T
        Drive& te = c.t[2 * a];
        Drive& to = c.t[2 * a + 1];
        if (w_odd < m) {
            to.torque = to.torque + less;
            te.torque = te.torque - more;
        } else if (m < w_odd) {
            to.torque = to.torque - more;
            te.torque = te.torque + less;
        }
    }
}

F clutch_slip_factor(F d, bool toward_zero) {
    if (d < F()) d = -d;
    d = d - F::raw(toward_zero ? 0x42517082 : 0x42517084); // (2002 Tokyo-Geneva's: Europe)
    if (d < F()) d = F();
    F f = d / F::raw(toward_zero ? 0x44517082 : 0x44517084) + kOne;
    if (F::raw(0x3F900000) <= f) f = F::raw(0x3F900000);
    return f;
}

namespace {

F awd5_gain_later(const Car& c, F tg, F ff, F fr, const Awd3& k) {
    const F kFull = full_value(c.behaviour.rule(Rule::AwdGain)); // (2002 Tokyo-Geneva's constant is its own)
    F g = c.lat_g;
    if (g < F()) g = -g;
    F f = kOne;
    if (!(g <= k.fade_start)) {
        if (kOne <= g) f = k.fade_floor;
        else f = kOne - ((kOne - k.fade_floor) * (g - k.fade_start)) / (kOne - k.fade_start);
    }
    F d = c.axle_speed[1] - c.axle_speed[0];
    if (d < F()) d = -d;
    F u = d * c.p.d.awd_k;
    if (c.gear == 0) tg = -tg;
    if (k.negative_full && tg < F()) return -kFull;
    const F cap = c.p.d.awd_capacity;
    if (!(cap == F())) {
        if (reverse_locked(c) || c.clutch == 0) return kFull;
        if (f == kOne) {
            if (c.clutch == 2 || F::raw(0x3F400000) < c.brake_pedal) return kFull;
            const F sum = (tg + ff) + fr;
            if (F() < tg) {
                if (F() < sum) return kFull;
            } else if (tg < F()) {
                if (sum < F()) return kFull;
            }
        }
    }
    u = u * f;
    if (kOne <= u) u = cap == F() ? kOne : kFull;
    return u;
}

} // namespace

F awd5_gain(const Car& c, F tg, F ff, F fr, const Awd3& k) {
    if (!gt3_program(c.behaviour.rule(Rule::AwdGain))) return awd5_gain_later(c, tg, ff, fr, k);
    (void)ff;
    F g = c.lat_g;
    if (g < F()) g = -g;
    const F quarter = F::raw(0x3E800000), three_q = F::raw(0x3F400000);
    F f;
    if (g <= quarter) f = kOne;
    else if (kOne <= g) f = quarter;
    else f = kOne - ((g - quarter) * three_q) / three_q;
    const F cap = c.p.d.awd_capacity;
    if (!(cap == F())) {
        if (reverse_locked(c) || c.clutch == 0) return kFull;
        if (f == kOne) {
            if (c.clutch == 2 || three_q < c.brake_pedal) return kFull;
            if (F() < tg) {
                if (F() < tg + fr) return kFull;
            } else if (tg < F()) {
                if (tg + fr < F()) return kFull;
            }
        }
    }
    F d = c.axle_speed[1] - c.axle_speed[0];
    if (d < F()) d = -d;
    F u = (d * c.p.d.awd_k) * f;
    if (kOne <= u) u = cap == F() ? kOne : kFull;
    return u;
}

namespace {

// (the later builds' gearbox kinds, D +0x12: 1 and 2 are without steps)
bool stepless(const Driveline& d) { return u8(d.kind - 1) < 2; }

// The later builds' code (Concept 2001 Tokyo's address): a stepless gearbox's ratio word for this step and the engine
// torque through it. Locked in a forward gear, the ratio is the engine's speed over the wheel side's, between the last
// gear's relative ratio and 1, and moves towards its target by rate x dt x the stored ratio: kind 1's target holds the
// engine at the hold speed (the down-shift word) scaled by the pedal, kind 2's is the selected gear's relative ratio.
F stepless_torque(Car& c, const Stepless& k) {
    const Driveline& d = c.p.d;
    const bool geneva = c.behaviour.rule(Rule::AxleStep) >= Program::ConceptGenevaEu;
    F r = kOne;
    if (c.clutch == 1 && c.gear != 0) {
        const F last = d.ratio[d.gears];
        r = c.omega / c.clutch_wheel;
        if (kOne < r) r = kOne;
        else if (r < last) r = last;
        F target = kOne, rate;
        bool lock = false; // the gearbox lock-out while the ratio moves
        if (d.kind == 1) {
            const F pedal = c.throttle_pedal;
            const F hold = (ee::int_to_f(d.down_rpm[0]) * F::raw(geneva ? 0x3DD6774E : 0x3DD67750)) *
                           (pedal * F::raw(geneva ? 0x3ECCCCCC : 0x3ECCCCCD) + F::raw(geneva ? 0x3F199999 : 0x3F19999A));
            if (hold < c.clutch_wheel) {
                target = hold / c.clutch_wheel;
                if (target < last) target = last;
                else if (kOne < target) target = kOne;
            }
            rate = k.none + pedal * (k.full - k.none);
        } else {
            if (!(c.gear < 2)) target = d.ratio[c.gear];
            rate = target < r ? k.down : k.up;
            lock = c.control == 1 || c.driver_kind != 0 || c.pedal_style == 0;
        }
        if (!(target == r)) {
            const F step = (rate * c.dt) * c.later.ratio;
            bool reached;
            if (r < target) r = r + step, reached = target <= r;
            else r = r - step, reached = r <= target;
            if (reached) r = target;
            else if (lock) c.shift_lockout = 1;
        }
    }
    c.later.ratio = r;
    return r * engine_torque(c, c.throttle);
}

// The later builds' axle step (Rule::AxleStep, Concept 2001 Tokyo). Against GT3's: a stepless gearbox's torque
// and its ratio on the engine's reflected inertia; kind 2's gear index at most 1; the other axle's wheel torques scaled by
// the radii when the axles' tyres differ (layout 5); AWD system 7 is layout 8, and layout 7 (system 5) drives the rear axle
// and keeps the clutch torque word; the centre clutch stays locked while the torque difference is within the requested
// front torque's magnitude and opens at minus the full-transfer value; the front torque is the request's magnitude, its
// sign the axles' speed difference's.
void axle_step_later(Car& c, const Awd3& k, const Stepless& sl, const Hybrid& h) {
    const Driveline& d = c.p.d;
    const F dt = c.dt;
    const bool geneva = c.behaviour.rule(Rule::AxleStep) >= Program::ConceptGenevaEu;
    const F kFull = full_value(c.behaviour.rule(Rule::AxleStep)), kHand = F::raw(geneva ? 0x3E4CCCCC : 0x3E4CCCCD);
    F tc{};
    if (c.clutch != 0) {
        auto mark = [&](size_t w, u8 bits) { c.w[w].flags = u8(c.w[w].flags | bits); };
        switch (d.layout) {
        case 0:
        case 4:
        case 5:
        case 6: mark(2, 8), mark(3, 8); break;
        case 1: mark(0, 8), mark(1, 8); break;
        case 2: mark(0, 8), mark(1, 8), mark(2, 4), mark(3, 4); break;
        case 3:
        case 7:
        case 8: mark(0, 4), mark(1, 4), mark(2, 8), mark(3, 8); break;
        default: break;
        }
        // (layout 7: the engine at its share of the throttle, and the rear motor's torque, [physics §14.5])
        F t;
        if (stepless(d)) t = stepless_torque(c, sl);
        else if (d.layout == 7 && hybrid_car(c, h)) {
            t = engine_torque(c, hybrid_engine_throttle(c, h));
            t = t + hybrid_rear_torque(c, h);
        } else t = engine_torque(c, c.throttle);
        if (c.clutch == 1) tc = t;
        else {
            if (c.clutch == 3) tc = d.shift_clutch_torque;
            else {
                tc = c.torque_wot * F::raw(geneva ? 0x3D4CCCCC : 0x3D4CCCCD);
                if (F() < t) tc = tc + clutch_slip_factor(c.clutch_engine - c.clutch_wheel, geneva) * t;
            }
            if (c.clutch_engine < c.clutch_wheel) tc = -tc;
            c.omega = c.omega + ((t - tc) * d.inv_engine_inertia) * dt;
        }
    }
    if (d.layout != 7) c.clutch_torque = F() <= tc ? tc : -tc;
    size_t g = c.gear;
    if (d.kind == 2 && !(g < 2)) g = 1;
    F f[2] = {c.t[0].torque + c.t[1].torque, c.t[2].torque + c.t[3].torque};
    const F tg = tc * d.ratio[g];
    if (d.unequal != 0) {
        const size_t da = d.driven_axle, o = 1 - da;
        f[o] = f[o] * (c.p.tire_radius[o] * c.p.inv_tire_radius[da]);
    }
    const F ff = f[0], fr = f[1];
    F q{}; // the front axle's torque, its magnitude
    int mode = 0;
    switch (d.layout) {
    case 2: mode = 1; break;
    case 3: mode = 2; break;
    case 4:
    case 5:
    case 6:
    case 8: {
        F qa{};
        if (d.layout == 4) {
            if (d.awd_k == kOne) qa = kFull;
            else if (kHand < c.handbrake) qa = F();
            else if (tg <= F() || reverse_locked(c)) qa = kFull;
            else qa = d.awd_k * tg;
        } else if (d.layout == 5) {
            qa = awd5_gain(c, tg, ff, fr, k);
            if (!(qa == kFull) && !(qa == -kFull)) qa = (qa * tg) * kHalf;
        } else if (d.layout == 6) {
            if (kHand < c.handbrake) qa = F();
            else if (reverse_locked(c)) qa = kFull;
            else {
                qa = kFull;
                F st = c.steer_cmd;
                if (st < F()) st = -st;
                F gn = kOne - st * d.awd_k;
                if (gn < F()) gn = F();
                if (gn < F::raw(geneva ? 0x3F7D70A3 : 0x3F7D70A4)) qa = gn;
            }
            if (!(qa == kFull) && !(qa == -kFull)) qa = (qa * tg) * kHalf;
        } else qa = d.awd_k * tg;
        bool locked = false;
        if (c.centre_locked != 0) {
            if (qa == kFull) locked = true;
            else if (qa == -kFull) c.centre_locked = 0;
            else {
                F x = (ff - fr) - tg;
                if (x < F()) x = -x;
                const F a = qa < F() ? -qa : qa;
                if (a < x) c.centre_locked = 0;
                else locked = true;
            }
        }
        if (locked) mode = 3;
        else {
            if (qa == -kFull) q = F();
            else if (qa == kFull) q = d.awd_capacity;
            else q = qa < F() ? -qa : qa;
            if (!(q == F())) mode = 1;
        }
        if (mode != 0) {
            c.w[0].flags = u8(c.w[0].flags | 4);
            c.w[1].flags = u8(c.w[1].flags | 4);
        }
        break;
    }
    default: break;
    }
    F jref{};
    if (c.clutch == 1) {
        jref = d.engine_inertia * d.ratio[g];
        if (c.gear == 0) jref = -jref;
    }
    if (stepless(d)) jref = jref * c.later.ratio;
    const auto brakes = [&](size_t a) { return c.t[2 * a].brake_torque + c.t[2 * a + 1].brake_torque; };
    if (mode == 3 || mode == 2) {
        const F j = (d.axle_inertia[0] + d.axle_inertia[1]) + jref;
        const F acc = ((tg + ff) + fr) / j;
        const F braked = ((c.t[0].brake_torque + c.t[1].brake_torque) + c.t[2].brake_torque) + c.t[3].brake_torque;
        const F half = tg * kHalf;
        if (mode == 3) {
            const F w0 = c.axle_speed[0];
            const F w = w0 + acc * dt;
            c.axle_speed[0] = w;
            if (!(braked == F()) && (w * w0 < F() || w0 == F())) c.axle_speed[0] = F();
            c.axle_speed[1] = c.axle_speed[0];
            c.drive_torque[1] = half;
            c.drive_torque[0] = half;
            return;
        }
        const F wf = c.axle_speed[0], wr = c.axle_speed[1];
        const F m = (wf + wr) * kHalf;
        F m1 = m + acc * dt;
        if (!(braked == F()) && (m1 * m < F() || m == F())) m1 = F();
        const F diff = wf - wr;
        const F h = diff * kHalf;
        F h1 = h + (((ff - fr) - d.awd_k * diff) * d.inv_axle_inertia) * dt;
        if (h * h1 < F()) h1 = F();
        c.drive_torque[1] = half;
        c.axle_speed[1] = m1 - h1;
        c.axle_speed[0] = m1 + h1;
        c.drive_torque[0] = half;
        return;
    }
    std::array<F, 2> j{};
    switch (d.layout) {
    case 0:
    case 7:
        c.drive_torque[1] = tg;
        j = {F(), d.axle_inertia[1] + jref};
        c.drive_torque[0] = F();
        break;
    case 1:
        c.drive_torque[0] = tg;
        j = {d.axle_inertia[0] + jref, F()};
        c.drive_torque[1] = F();
        break;
    case 2: {
        const F kd = d.awd_k * (c.axle_speed[0] - c.axle_speed[1]);
        j = {d.axle_inertia[0] + jref, d.axle_inertia[1]};
        c.drive_torque[1] = kd;
        c.drive_torque[0] = tg - kd;
        break;
    }
    case 4:
    case 5:
    case 6:
    case 8:
        if (c.axle_speed[1] < c.axle_speed[0]) c.drive_torque[1] = tg + q, c.drive_torque[0] = -q;
        else c.drive_torque[1] = tg - q, c.drive_torque[0] = q;
        j = {d.axle_inertia[0], d.axle_inertia[1] + jref};
        break;
    default: break;
    }
    F d0{};
    if (mode == 1) d0 = c.axle_speed[0] - c.axle_speed[1];
    for (size_t a = 0; a < 2; ++a) {
        if (d.diff_type[a] == 0) continue;
        const F w0 = c.axle_speed[a];
        F acc{};
        if (!(j[a] == F())) acc = ((c.drive_torque[a] + f[a]) / j[a]) * dt;
        const F w = w0 + acc;
        c.axle_speed[a] = w;
        if (!(brakes(a) == F()) && (w * w0 < F() || w0 == F())) c.axle_speed[a] = F();
    }
    if (mode != 1) return;
    const F d1 = c.axle_speed[0] - c.axle_speed[1];
    const bool crossed = F() < d0 ? d1 <= F() : (d0 < F() && F() <= d1);
    if (!crossed) return;
    const size_t da = d.driven_axle;
    c.axle_speed[1 - da] = c.axle_speed[da];
    if (u32(d.layout) - 4u < 3u) c.centre_locked = 1;
}

} // namespace

void axle_step(Car& c, const Awd3& k, const Stepless& sl, const Hybrid& h) {
    if (!gt3_program(c.behaviour.rule(Rule::AxleStep))) return axle_step_later(c, k, sl, h);
    const Driveline& d = c.p.d;
    const F dt = c.dt;
    F tc{}; // the transmitted clutch torque
    if (c.clutch != 0) {
        auto mark = [&](size_t w, u8 bits) { c.w[w].flags = u8(c.w[w].flags | bits); };
        switch (d.layout) {
        case 0:
        case 4:
        case 5:
        case 6: mark(2, 8), mark(3, 8); break;
        case 1: mark(0, 8), mark(1, 8); break;
        case 2: mark(0, 8), mark(1, 8), mark(2, 4), mark(3, 4); break;
        case 3:
        case 7: mark(0, 4), mark(1, 4), mark(2, 8), mark(3, 8); break;
        default: break;
        }
        const F t = engine_torque(c, c.throttle);
        if (c.clutch == 1) tc = t;
        else {
            if (c.clutch == 3) tc = d.shift_clutch_torque;
            else {
                tc = c.torque_wot * F::raw(0x3D4CCCCD);
                if (F() < t) tc = tc + clutch_slip_factor(c.clutch_engine - c.clutch_wheel, toward_zero(c.behaviour)) * t;
            }
            if (c.clutch_engine < c.clutch_wheel) tc = -tc;
            c.omega = c.omega + ((t - tc) * d.inv_engine_inertia) * dt;
        }
    }
    c.clutch_torque = F() <= tc ? tc : -tc;
    F q{}; // the front axle's torque (L4-7)
    const F ff = c.t[0].torque + c.t[1].torque;
    const F fr = c.t[2].torque + c.t[3].torque;
    const F tg = tc * d.ratio[c.gear];
    int mode = 0; // 1 centre coupling, 2 the L3 coupling, 3 centre clutch locked
    switch (d.layout) {
    case 2: mode = 1; break;
    case 3: mode = 2; break;
    case 4:
    case 5:
    case 6:
    case 7: {
        F qa{};
        if (d.layout == 4) {
            if (d.awd_k == kOne) qa = kFull;
            else if (F::raw(0x3E4CCCCD) < c.handbrake) qa = F();
            else if (tg <= F() || reverse_locked(c)) qa = kFull;
            else qa = d.awd_k * tg;
        } else if (d.layout == 5) {
            qa = awd5_gain(c, tg, ff, fr, k);
            if (!(qa == kFull)) qa = (qa * tg) * kHalf;
        } else if (d.layout == 6) {
            if (F::raw(0x3E4CCCCD) < c.handbrake) qa = F();
            else if (reverse_locked(c)) qa = kFull;
            else {
                qa = kFull;
                F st = c.steer_cmd;
                if (st < F()) st = -st;
                F g = kOne - st * d.awd_k;
                if (g < F()) g = F();
                if (g < F::raw(0x3F7D70A4)) qa = g;
            }
            if (!(qa == kFull)) qa = (qa * tg) * kHalf;
        } else qa = d.awd_k * tg;
        bool locked = false;
        if (c.centre_locked != 0) {
            if (qa == kFull) locked = true;
            else {
                F x = (ff - fr) - tg;
                if (x < F()) x = -x;
                if (!(d.awd_capacity < x)) locked = true;
                else c.centre_locked = 0;
            }
        }
        if (locked) mode = 3;
        else {
            if (qa == kFull) q = c.axle_speed[1] - c.axle_speed[0] < F() ? -d.awd_capacity : d.awd_capacity;
            else q = qa;
            if (!(q == F())) mode = 1;
        }
        if (mode != 0) { // the front wheels coupled through the centre clutch
            c.w[0].flags = u8(c.w[0].flags | 4);
            c.w[1].flags = u8(c.w[1].flags | 4);
        }
        break;
    }
    default: break;
    }
    F jref{}; // the engine's inertia reflected to the axle, while locked
    if (c.clutch == 1) {
        jref = d.engine_inertia * d.ratio[c.gear];
        if (c.gear == 0) jref = -jref;
    }
    const auto brakes = [&](size_t a) { return c.t[2 * a].brake_torque + c.t[2 * a + 1].brake_torque; };
    if (mode == 3 || mode == 2) {
        const F j = (d.axle_inertia[0] + d.axle_inertia[1]) + jref;
        const F acc = ((tg + ff) + fr) / j;
        const F braked = ((c.t[0].brake_torque + c.t[1].brake_torque) + c.t[2].brake_torque) + c.t[3].brake_torque;
        const F half = tg * kHalf;
        if (mode == 3) { // one common axle speed
            const F w0 = c.axle_speed[0];
            const F w = w0 + acc * dt;
            c.axle_speed[0] = w;
            if (!(braked == F()) && (w * w0 < F() || w0 == F())) c.axle_speed[0] = F();
            c.axle_speed[1] = c.axle_speed[0];
            c.drive_torque[1] = half;
            c.drive_torque[0] = half;
            return;
        }
        // L3: the mean speed and the half-difference
        const F wf = c.axle_speed[0], wr = c.axle_speed[1];
        const F m = (wf + wr) * kHalf;
        F m1 = m + acc * dt;
        if (!(braked == F()) && (m1 * m < F() || m == F())) m1 = F();
        const F diff = wf - wr;
        const F h = diff * kHalf;
        F h1 = h + (((ff - fr) - d.awd_k * diff) * d.inv_axle_inertia) * dt;
        if (h * h1 < F()) h1 = F();
        c.drive_torque[1] = half;
        c.axle_speed[1] = m1 - h1;
        c.axle_speed[0] = m1 + h1;
        c.drive_torque[0] = half;
        return;
    }
    std::array<F, 2> j{};
    switch (d.layout) {
    case 0:
        c.drive_torque[1] = tg;
        j = {F(), d.axle_inertia[1] + jref};
        c.drive_torque[0] = F();
        break;
    case 1:
        c.drive_torque[0] = tg;
        j = {d.axle_inertia[0] + jref, F()};
        c.drive_torque[1] = F();
        break;
    case 2: {
        const F kd = d.awd_k * (c.axle_speed[0] - c.axle_speed[1]);
        j = {d.axle_inertia[0] + jref, d.axle_inertia[1]};
        c.drive_torque[1] = kd;
        c.drive_torque[0] = tg - kd;
        break;
    }
    case 4:
    case 5:
    case 6:
    case 7:
        c.drive_torque[0] = q;
        c.drive_torque[1] = tg - q;
        j = {d.axle_inertia[0], d.axle_inertia[1] + jref};
        break;
    default: break; // (the builder makes no other layout; the program would read stale stack words)
    }
    F d0{};
    if (mode == 1) d0 = c.axle_speed[0] - c.axle_speed[1];
    const F fsum[2] = {ff, fr};
    for (size_t a = 0; a < 2; ++a) {
        if (d.diff_type[a] == 0) continue; // an 'f' axle is not integrated
        const F w0 = c.axle_speed[a];
        F acc{};
        if (!(j[a] == F())) acc = ((c.drive_torque[a] + fsum[a]) / j[a]) * dt;
        const F w = w0 + acc;
        c.axle_speed[a] = w;
        if (!(brakes(a) == F()) && (w * w0 < F() || w0 == F())) c.axle_speed[a] = F();
    }
    if (mode != 1) return;
    const F d1 = c.axle_speed[0] - c.axle_speed[1];
    const bool crossed = F() < d0 ? d1 <= F() : (d0 < F() && F() <= d1);
    if (!crossed) return;
    const size_t da = d.driven_axle;
    c.axle_speed[1 - da] = c.axle_speed[da];
    if (u32(d.layout) - 4u < 3u) c.centre_locked = 1;
}

void wheel_speeds(Car& c) {
    const Driveline& d = c.p.d;
    const F dt = c.dt;
    for (size_t a = 0; a < 2; ++a) {
        Wheel& we = c.w[2 * a];
        Wheel& wo = c.w[2 * a + 1];
        Drive& te = c.t[2 * a];
        Drive& to = c.t[2 * a + 1];
        const F dw = we.rim_speed - wo.rim_speed; // the previous step's speeds
        const u8 type = d.diff_type[a];
        bool locked = false;
        auto transfer = [&](F x) {
            if (F() < dw) {
                te.torque = te.torque - x;
                to.torque = to.torque + x;
            } else if (dw < F()) {
                te.torque = te.torque + x;
                to.torque = to.torque - x;
            }
        };
        if (type == 1) locked = true; // 'r'
        else if (type == 3 || type == 4) { // clutch-pack LSD
            const F ta = c.drive_torque[a];
            F cap = F() <= ta ? ta * d.diff_accel[a] : d.diff_decel[a] * -ta;
            if (cap < d.diff_initial[a]) cap = d.diff_initial[a];
            bool slip = true;
            if (c.diff_lock[a] != 0) {
                const F x = te.torque - to.torque;
                const F c2 = cap + cap;
                if (c2 < x || x < -c2) c.diff_lock[a] = 0;
                else locked = true, slip = false;
            }
            if (slip) transfer(cap);
        } else if (type == 5) { // viscous
            F x = dw * dw;
            if (d.diff_initial[a] < x) x = d.diff_initial[a];
            transfer(d.diff_accel[a] * x);
        }
        const u8 kind = locked ? 1 : type;
        if (kind == 0) { // 'f': each wheel alone
            for (size_t k = 0; k < 2; ++k) {
                Wheel& w = c.w[2 * a + k];
                const Drive& t = c.t[2 * a + k];
                const F w0 = w.rim_speed;
                const F w1 = w0 + (t.torque * d.wheel_gain[a]) * dt;
                bool zero = false;
                if (!(t.brake_torque == F())) {
                    if (F() < w0) zero = w1 < F();
                    else if (w0 < F()) zero = F() < w1;
                    else zero = true;
                }
                w.rim_speed = zero ? F() : w1;
            }
        } else if (kind == 1) { // both wheels at the axle speed
            const F v = c.p.tire_radius[a] * c.axle_speed[a];
            wo.rim_speed = v;
            we.rim_speed = v;
        } else {
            F h = dw * kHalf + (((te.torque - to.torque) * d.wheel_gain[a]) * kHalf) * dt;
            if (dw * h < F()) {
                if (u32(type) - 3u < 2u) c.diff_lock[a] = 1;
                h = F();
            }
            const F v = c.p.tire_radius[a] * c.axle_speed[a];
            we.rim_speed = v + h;
            wo.rim_speed = v - h;
        }
    }
}

} // namespace gt3r::sim::exact
