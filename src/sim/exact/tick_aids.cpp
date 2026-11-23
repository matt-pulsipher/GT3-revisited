// Phase 6, the steering indicators and the driving aids (TCS, ASCC, ABS, the dirt brake cut); see tick.h.

#include "base/ee_math.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {
constexpr F kOne = F::raw(0x3F800000);
constexpr F kDeg = F::raw(0x3C8EFA35);       // pi / 180
constexpr F kTenth = F::raw(0x3DCCCCCD);     // 0.1
constexpr F kHundredth = F::raw(0x3C23D70A); // 0.01
} // namespace

u8 course_category(u8 raw) { return raw == 0xFF ? 0 : raw; }

bool dirt_course(u8 raw) { return course_category(raw) == 2; }

void steer_indicators(Car& c, const AidInputs& in) {
    const F zero{};
    c.understeer = zero;
    c.oversteer = zero;
    if (c.speed <= c.understeer) return;
    F grip = c.understeer;
    for (int i = 0; i < 4; ++i) grip = grip + tire_record(c.p, i, c.w[size_t(i)].surface).grip;
    F delta = c.steer_cmd, r = c.yaw_rate, ra = c.yaw_accel;
    const Behaviour& h = c.behaviour; // (2002 Tokyo-Geneva's folded constants: Europe 0x1EEFF0)
    const F v = c.speed;
    // Understeer: the yaw rate short of v sin(delta) / L, while the lateral acceleration is more than 0.2 of the grip.
    if (zero <= delta * r) {
        if (delta < zero) {
            delta = -delta;
            r = -r;
            ra = -ra;
        }
        const F rt = v * ee::sin(delta) / c.p.wheelbase;
        if (zero < rt) {
            F lat = c.a_y;
            if (lat < zero) lat = -lat;
            lat = lat / grip;
            if (folded(h, 0x3E4CCCCD, 0x3E4CCCCC) < lat) { // 0.2
                if (zero < ra * r) r = r + ra * F::raw(0x3F000000); // + yaw acceleration / 2
                if (r < rt) c.understeer = kOne - r / rt;
            }
        }
    }
    if (u8(c.control - 1) < 2 || c.driver_kind != 0) {
        // The AI's: the yaw rate times the speed beyond the lateral acceleration, when they turn opposite ways.
        const F s = ee::sqrt(v * v + c.lat_speed * c.lat_speed);
        if (pal_rules(c, Rule::Oversteer) && s < folded(h, 0x40DE38E4, 0x40DE38E2)) return; // PAL only: none below 25 km/h (0x351D84)
        F a = s * c.yaw_rate, lat = c.a_y;
        if (zero <= a * lat) return;
        if (a < zero) a = -a;
        if (lat < zero) lat = -lat;
        a = a - lat;
        if (a < zero) a = zero;
        c.oversteer = (a + a) / grip;
        return;
    }
    // Oversteer: the larger rear slip angle past a threshold that falls with the speed.
    F m = zero;
    for (size_t i = 2; i < 4; ++i) {
        F b = c.t[i].slip_angle;
        if (b < zero) b = -b;
        if (m < b) m = b;
    }
    F q = ee::sqrt(v * v + c.lat_speed * c.lat_speed) * folded(h, 0x3C9374BC, 0x3C9374BD); // 0.018
    if (kOne < q) q = kOne;
    const F deg = folded(h, kDeg.bits, 0x3C8EFA34), tenth = folded(h, kTenth.bits, 0x3DCCCCCC);
    const F a0 = ee::int_to_f(in.over_angle[0]) * deg, a1 = ee::int_to_f(in.over_angle[1]) * deg;
    const F th = q * (a1 - a0) + a0;
    if (!(th <= m)) return;
    m = m - th;
    const F g1 = ee::int_to_f(in.over_gain[1]) * tenth, g0 = ee::int_to_f(in.over_gain[0]) * tenth;
    const F span = folded(h, 0x3FC90FDB, 0x3FC90FD9) - th; // pi / 2
    F o = (q * (g1 - g0) + g0) * m / span;
    if (kOne < o) o = kOne;
    c.oversteer = o;
}

F tcs_lateral(F x, F a, F b) {
    const F zero{};
    if (x < zero) x = -x;
    x = x - b;
    if (x < zero) return kOne;
    x = x * a;
    if (kOne < x) return zero;
    return kOne - x;
}

u8 tcs_on(const Aids& a) { return a.tcs_gain == F() ? 0 : 1; }

u8 ascc_on(const Aids& a, const AsccSwitches& sw) {
    if ((a.on[0] | a.on[1]) != 0) return 1;
    // (Rule::Ascc, the later builds: channel C counts with its status switch, channel E without its own)
    if (!sw.later || sw.c_status == 0) return 0;
    if (a.on[2]) return 1;
    if (sw.e_quiet != 0) return 0;
    return a.on[3] != 0 ? 1 : 0;
}

void tcs(Car& c, const Aids& a) {
    const u8 on = tcs_on(a);
    c.tcs_status = on;
    if (!on) return;
    const F lat = tcs_lateral(c.lat_g, a.tcs_lat_a, a.tcs_lat_b);
    const F zero{};
    if (c.torque_wot <= zero || c.throttle == zero) return;
    // The most negative slip of the driven wheels with load, and that wheel's target and gain.
    F smin = kOne, target = kOne, g = kOne;
    for (int axle = 0; axle < 2; ++axle) {
        if (!tcs_reads_axle(c.p.d.layout, axle, !gt3_program(c.behaviour.rule(Rule::Layout)))) continue;
        for (int k = 0; k < 2; ++k) {
            const int i = 2 * axle + k;
            const Wheel& w = c.w[size_t(i)];
            if (w.load == zero || !(w.slip_ratio < smin)) continue;
            smin = w.slip_ratio;
            target = tire_record(c.p, i, w.surface).slip_drive;
            g = w.slip_gain;
        }
    }
    F f = (smin - target * lat * a.tcs_scale) * (a.tcs_gain * g) + kOne;
    if (f < zero) f = zero;
    else if (kOne < f) f = kOne;
    if (f < kOne) c.tcs_status = 2;
    c.throttle = c.throttle * f;
}

void brake_add(Wheel& w, F x) {
    w.brake = w.brake + x;
    if (kOne < w.brake) w.brake = kOne;
}

void ascc(Car& c, const Aids& a, std::array<F, 4>& cap, bool on, const AsccSwitches& sw) {
    const F kTenth = F::raw(sw.later && c.behaviour.rule(Rule::Ascc) >= Program::ConceptGenevaEu ? 0x3DCCCCCC : 0x3DCCCCCD);
    for (size_t i = 0; i < 4; ++i) cap[i] = cap[i] * c.p.brake_slip_scale[i >> 1];
    if (!on) return;
    const F zero{};
    bool acted = false;
    const F u = c.understeer, o = c.oversteer;
    const size_t in = zero < c.yaw_rate ? 0 : 1, out = 1 - in; // the inner / outer front wheel (+2: rear)
    auto release = [&](size_t i, F f) {
        cap[i] = cap[i] * f;
        c.w[i].brake = c.w[i].brake * f;
    };
    if (c.brake_pedal == zero) {
        if (a.on[0]) {
            const F x = (o - a.a_thr) * a.a_gain;
            if (zero < x) {
                acted = true;
                if (a.a_wheels[0]) brake_add(c.w[out], x);
                if (a.a_wheels[1]) brake_add(c.w[2 + out], x);
            }
        }
        if (((sw.later && sw.c_throttle != 0) || c.throttle_pedal == zero) && a.on[2]) {
            const F x = (u - a.c_thr) * a.c_gain;
            if (zero < x) {
                if (sw.later && sw.c_status != 0) acted = true;
                if (a.c_wheels[0]) brake_add(c.w[in], x);
                if (a.c_wheels[1]) brake_add(c.w[2 + in], x);
            }
        }
    } else {
        if (a.on[1]) {
            F f = kOne - (o - kTenth) * a.d_gain;
            if (f < zero) f = zero;
            if (f < kOne) {
                acted = true;
                if (a.d_wheels[0]) release(in, f);
                if (a.d_wheels[1]) release(2 + in, f);
            }
        }
        if (a.on[3]) {
            F f = kOne - (u - kTenth) * a.e_gain;
            if (f < zero) f = zero;
            if (f < kOne) {
                if (sw.later && sw.c_status != 0 && sw.e_quiet == 0) acted = true;
                if (a.e_wheels[0]) release(out, f);
                if (a.e_wheels[1]) release(2 + out, f);
            }
        }
    }
    if (acted) c.ascc_status = 2;
    else if (ascc_on(a, sw)) c.ascc_status = 1;
}

void brakes(Car& c, const Aids& a, bool ascc_enabled, const AidInputs& in) {
    const F zero{};
    if (dirt_course(in.course_category) && in.dirt_steer != 0) {
        F d = c.steer_cmd;
        const F full = ee::int_to_f(in.dirt_steer) * folded(c.behaviour, kDeg.bits, 0x3C8EFA34); // (2002 Tokyo-Geneva's: Europe 0x1EED98)
        if (d < zero) d = -d;
        const F s = full <= d ? kOne : d / full;
        const F rear = kOne - s * (ee::int_to_f(in.dirt_cut[1]) * kHundredth);
        const F front = kOne - s * (ee::int_to_f(in.dirt_cut[0]) * kHundredth);
        c.w[3].brake = c.w[3].brake * rear;
        c.w[1].brake = c.w[1].brake * front;
        c.w[0].brake = c.w[0].brake * front;
        c.w[2].brake = c.w[2].brake * rear;
    }
    std::array<F, 4> cap;
    for (int i = 0; i < 4; ++i) cap[size_t(i)] = tire_record(c.p, i, c.w[size_t(i)].surface).slip_brake;
    ascc(c, a, cap, ascc_enabled, in.ascc);
    // The ABS limit: past the target slip the pressure falls with slope G g; 0 is kept at 0.001.
    for (size_t i = 0; i < 4; ++i) {
        Wheel& w = c.w[i];
        const size_t axle = i >> 1;
        const F p = w.brake, peak = w.peak_force * c.p.abs_peak_scale[axle];
        if (p == zero) continue;
        F f = peak * (kOne - (w.slip_ratio - cap[i]) * (c.p.abs_gain[axle] * w.slip_gain));
        if (f < zero) f = zero * p;
        else {
            if (kOne < f) f = kOne;
            f = f * p;
        }
        w.brake = f == zero ? folded(c.behaviour, 0x3A83126F, 0x3A83126E) : f; // 0.001 (2002 Tokyo-Geneva's: Europe 0x1EF3C8's)
    }
}

void driver_aids(Car& c, const AidInputs& in) {
    tcs(c, c.p.aids);
    brakes(c, c.p.aids, c.handbrake < folded(c.behaviour, kTenth.bits, 0x3DCCCCCC), in); // (2002 Tokyo-Geneva's: Europe 0x1EF328)
}

void scripted_aids(Car& c, const AidInputs& in) {
    tcs(c, c.ai.aids);
    brakes(c, c.ai.aids, true, in);
}

void aids(Car& c, const AidInputs& in) {
    steer_indicators(c, in);
    c.tcs_status = 0;
    c.ascc_status = 0;
    const s8 control = s8(c.control);
    if (control == 1 || control == 2) {
        scripted_aids(c, in);
        // (Rule::Aids, from 2002 Tokyo-Seoul v2.00 on: a computer's car shows its own block's switches)
        if (control == 2 && c.behaviour.rule(Rule::Aids) >= Program::ConceptSeoul2) {
            c.tcs_status = tcs_on(c.p.aids);
            c.ascc_status = ascc_on(c.p.aids, in.ascc);
        }
    } else if (control == 0) {
        if (c.driver_kind == 0 || c.hold != 0) driver_aids(c, in);
        else {
            scripted_aids(c, in);
            c.tcs_status = tcs_on(c.p.aids);
            c.ascc_status = ascc_on(c.p.aids, in.ascc);
        }
    }
}

} // namespace gt3r::sim::exact
