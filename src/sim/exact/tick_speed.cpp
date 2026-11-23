// Phase 12's speeds and angles: ground-frame speeds, odometer and engine condition, speedometer, free-rolling wheels,
// wheel spin angles, the drawn car's points and the lagging view angles; see tick.h.

#include "base/ee_math.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

const F kZero = F::raw(0), kHalf = F::raw(0x3F000000), kOne = F::raw(0x3F800000);

// 0x1EA908: the odometer, the carry and the distance total from the signed forward speed (car +0x16A8 at this point).
void odometer(Car& c) {
    F d = c.speed_abs;
    if (d < kZero) return;
    d = d * c.dt;
    c.odo_carry = odometer_add(c.odometer, c.odo_carry + d);
    F t = c.dist_total + d;
    const F wrap = F::raw(0x4AF42400); // 0x35056C: 8000 km
    c.dist_total = t;
    if (wrap <= t) c.dist_total = t - wrap;
}

// 0x1EA870: the speedometer follows |speed| by at most 111.111 m/s per second.
void speedometer(Car& c) {
    const F s = c.speed_abs, v = c.speedo;
    const F step = c.dt * folded(c.behaviour, 0x42DE38E4, 0x42DE38E2); // 0x350568 (2002 Tokyo-Geneva's: Europe 0x1E1BD8)
    if (v < s) {
        c.speedo = v + step;
        if (s < c.speedo) c.speedo = s;
    } else {
        c.speedo = v - step;
        if (c.speedo < s) c.speedo = s;
    }
}

// 0x1EA720: grounded, unbraked, undriven wheels roll with the ground; the axles' mean spin [physics §10.2].
void free_wheels(Car& c) {
    bool free[2] = {};
    for (size_t i = 0; i < 4; ++i) {
        Wheel& w = c.w[i];
        if (w.flags != 0) continue;
        const F a = ee::cos(w.steer) * w.hub_vx;
        const F b = ee::sin(w.steer) * w.hub_vy;
        free[i >> 1] = true;
        w.rim_speed = a - b;
    }
    for (size_t k = 0; k < 2; ++k) {
        if (!free[k] && c.p.d.diff_type[k] != 0) continue;
        F m = c.w[2 * k].rim_speed + c.w[2 * k + 1].rim_speed;
        m = m * kHalf;
        c.axle_speed[k] = c.p.inv_tire_radius[k] * m;
    }
}

} // namespace

void hub_velocities(Car& c) {
    const F v[3] = {c.vel.x, c.vel.y, c.vel.z};
    const F t1[3] = {c.ground_fwd.x, c.ground_fwd.y, c.ground_fwd.z}, t2[3] = {c.ground_lat.x, c.ground_lat.y, c.ground_lat.z};
    F u = kZero, l = kZero;
    for (size_t k = 0; k < 3; ++k) {
        u = u + t1[k] * v[k];
        l = l + t2[k] * v[k];
    }
    c.lat_speed = l;
    c.speed = u;
    // 0x1EA5F8: hub i's velocity from the yaw rate and its lever (half track across, axle distance along).
    const F r = c.yaw_rate;
    const F xf = -c.p.half_track[0] * r, xr = -c.p.half_track[1] * r;
    const F yf = -c.p.dist_front * r, yr = c.p.dist_rear * r;
    const F along[4] = {xf, -xf, xr, -xr}, across[4] = {yf, yf, yr, yr};
    for (size_t i = 0; i < 4; ++i) {
        c.w[i].hub_vx = c.speed + along[i];
        c.w[i].hub_vy = c.lat_speed + across[i];
    }
}

F odometer_add(u32& odometer, F x) {
    if (x <= kZero) return kZero;
    const s32 n = ee::to_int(x);
    const s32 half = s32(u32(n) + (u32(n) >> 31)) >> 1;
    odometer += u32(half);
    if (0xEE6B27A5u < odometer) {
        odometer = 0xEE6B27A6u;
        return kZero;
    }
    return x - ee::int_to_f(s32(u32(half) << 1));
}

void speed_bookkeeping(Car& c, const Condition& cond) {
    hub_velocities(c);
    c.speed_abs = c.speed;
    odometer(c);
    if (c.speed_abs < kZero) c.speed_abs = -c.speed_abs;
    // 0x1DAD18: the distance of the step. (Rule::EngineCondition: the later builds' routine returns 0, Concept 2001 Tokyo
    // 0x1D5918: the wear and oil counters do not advance in a race; their limits below still apply.)
    const F d = gt3_program(c.behaviour.rule(Rule::EngineCondition)) ? c.speed_abs * c.dt : kZero;
    // 0x1D9C80: engine wear E += d, at most its limit e3.
    c.engine_wear = c.engine_wear + d;
    if (cond.e3 < c.engine_wear) c.engine_wear = cond.e3;
    // 0x1D9D78: oil O += d; a negative O that reaches 0 jumps to its limit omax; at most omax.
    const F o = c.oil;
    c.oil = o + d;
    if (o < kZero && kZero <= c.oil) c.oil = -(-cond.omax);
    if (cond.omax < c.oil) c.oil = cond.omax;
    if (c.finished == 0 && c.max_speed < c.speed_abs) c.max_speed = c.speed_abs;
    speedometer(c);
    free_wheels(c);
}

void wheel_spin(Car& c) {
    const F dt = c.dt;
    // (2002 Tokyo-Geneva's 2 pi a unit lower: Europe 0x1E2260)
    const F kTwoPi = folded(c.behaviour, 0x40C90FDB, 0x40C90FDA), kMinusTwoPi = folded(c.behaviour, 0xC0C90FDB, 0xC0C90FDA);
    const F h = c.heading;
    if (kTwoPi <= h) { // 0x350570
        c.heading = h - kTwoPi;
        c.prev.heading = c.prev.heading - kTwoPi;
    } else if (h <= kMinusTwoPi) { // 0x350574
        c.heading = h + kTwoPi;
        c.prev.heading = c.prev.heading + kTwoPi;
    }
    for (size_t i = 0; i < 4; ++i) {
        Wheel& w = c.w[i];
        F d = w.rim_speed * c.p.inv_tire_radius[i >> 1];
        d = d * dt;
        if (c.race_hold != 0) d = kZero;
        F a = w.spin + d;
        while (kTwoPi <= a) { // 0x350578
            a = a - kTwoPi;
            c.prev.spin[i] = c.prev.spin[i] - kTwoPi;
        }
        while (a <= kMinusTwoPi) { // 0x35057C
            a = a + kTwoPi;
            c.prev.spin[i] = c.prev.spin[i] + kTwoPi;
        }
        w.spin = a;
    }
    // The drawn car's points: the body and ground positions moved back by P +0x54 along their forward axes, course frame.
    const F k = c.p.x54;
    c.draw_pos_prev = c.draw_pos;
    c.draw_ground_prev = c.draw_ground;
    c.prev.steer_view = c.steer_view;
    c.draw_pos = {c.pos.x - k * c.fwd.x, c.pos.z - k * c.fwd.z, -(c.pos.y - k * c.fwd.y)};
    c.draw_ground = {c.ground_pos.x - k * c.ground_fwd.x, c.ground_pos.z - k * c.ground_fwd.z, -(c.ground_pos.y - k * c.ground_fwd.y)};
    F s = c.steer_cmd / c.p.max_lock;
    s = s * kHalf;
    F v = kHalf - s;
    if (v < kZero) v = kZero;
    else if (kOne < v) v = kOne;
    c.steer_view = v;
}

void view_follow(Car& c) {
    const F step = pal_rate(c) ? F::raw(0x3CA3D70A) : folded(c.behaviour, 0x3C888889, 0x3C888888); // 0x35030C: 1/60 (PAL 1/50; Asia 0x1EE010's)
    const F k10 = F::raw(0x41200000);  // 0x352960
    // Heading: acceleration 30 (diff) - 10 rate, integrated with the mean of this and the last acceleration.
    F a = angle_diff(c.heading, c.lag_heading, c.behaviour.rule(Rule::AngleWrap));
    a = a * F::raw(0x41F00000); // 0x352958: 30
    a = a - c.lag_heading_rate * k10;
    F m = c.lag_heading_acc + a;
    c.lag_heading_acc = a;
    m = m * kHalf;
    m = m * step;
    const F rate = c.lag_heading_rate + m;
    c.lag_heading_rate = rate;
    c.lag_heading = c.lag_heading + rate * step;
    c.lag_heading = angle_wrap(c.lag_heading, c.behaviour.rule(Rule::AngleWrap));
    c.lag_heading_diff = angle_diff(c.heading, c.lag_heading, c.behaviour.rule(Rule::AngleWrap));
    // Pitch: acceleration 20 (diff) - 10 rate.
    F p = angle_diff(c.pitch, c.lag_pitch, c.behaviour.rule(Rule::AngleWrap));
    p = p * F::raw(0x41A00000);
    p = p - c.lag_pitch_rate * k10;
    p = p * step;
    const F prate = c.lag_pitch_rate + p;
    c.lag_pitch_rate = prate;
    c.lag_pitch = c.lag_pitch + prate * step;
    c.lag_pitch = angle_wrap(c.lag_pitch, c.behaviour.rule(Rule::AngleWrap));
    c.lag_pitch_diff = angle_diff(c.pitch, c.lag_pitch, c.behaviour.rule(Rule::AngleWrap));
}

} // namespace gt3r::sim::exact
