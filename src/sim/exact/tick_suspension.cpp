// Phase 12's suspension half: hub offsets, the speed ramp, the suspension coordinates and the per-wheel spring, damper and body
// forces; see tick.h.

#include "base/ee_math.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

const F kZero = F::raw(0), kHalf = F::raw(0x3F000000), kOne = F::raw(0x3F800000), kMinusOne = F::raw(0xBF800000);
const F kRampGain = F::raw(0x3CA3D709); // 0x350548: 0.02 per m/s

} // namespace

void hub_offsets_xy(Car& c) {
    const Params& p = c.p;
    const F rf = c.lat.x * p.half_track[1], rr = c.lat.y * p.half_track[1];
    const F fr = c.lat.y * p.half_track[0], ff = c.lat.x * p.half_track[0];
    const F bx = c.fwd.x * p.dist_rear, by = c.fwd.y * p.dist_rear;
    const F ay = c.fwd.y * p.dist_front, ax = c.fwd.x * p.dist_front;
    c.w[1].hub.x = ff + ax;
    c.w[1].hub.y = fr + ay;
    c.w[3].hub.x = rf - bx;
    c.w[3].hub.y = rr - by;
    c.w[0].hub.x = -ff + ax;
    c.w[0].hub.y = -fr + ay;
    c.w[2].hub.x = -rf - bx;
    c.w[2].hub.y = -rr - by;
}

void hub_offsets_z(Car& c) {
    const Params& p = c.p;
    const F r = c.lat.z * p.half_track[1], f = c.lat.z * p.half_track[0];
    const F b = c.fwd.z * p.dist_rear, a = c.fwd.z * p.dist_front;
    c.w[1].hub.z = f + a;
    c.w[3].hub.z = r - b;
    c.w[0].hub.z = -f + a;
    c.w[2].hub.z = -r - b;
}

void hub_offsets(Car& c) {
    hub_offsets_z(c);
    hub_offsets_xy(c);
}

void speed_ramp(Car& c) {
    const F gain = folded(c.behaviour, kRampGain.bits, 0x3CA3D70B); // (2002 Tokyo-Geneva's, Europe 0x1F2870)
    for (Wheel& w : c.w) {
        const F v = ee::sqrt(w.hub_vx * w.hub_vx + w.hub_vy * w.hub_vy) * gain;
        w.speed_ramp = kOne < v ? kOne : v;
    }
}

void suspension_coords(Car& c, F s) {
    const F base[2] = {-c.p.tire_radius[0] + c.p.susp[0].offset, -c.p.tire_radius[1] + c.p.susp[1].offset};
    const F z = c.pos.z;
    for (size_t i = 0; i < 4; ++i) {
        const Wheel& w = c.w[i];
        c.susp_x[i] = (((z + w.hub.z) - w.ground0) - w.bump) * s + base[i >> 1];
    }
}

u32 bump_mask(const Car& c, u32 mask) {
    for (size_t i = 0; i < 4; ++i)
        if (c.susp_x[i] < c.p.susp[i >> 1].bump_limit) mask |= 1u << i;
    return mask;
}

void terrain_factor(Car& c) {
    c.terrain = c.ground_up.z / c.up.z;
    suspension_coords(c, c.terrain);
    c.bump_mask = u8(bump_mask(c, 0));
}

F damper_force(const Suspension& s, F v) {
    if (v < kZero) {
        if (v < kMinusOne) v = kMinusOne;
        const F r = ee::sqrt(-v - s.bump_t);
        return s.bump_o + s.bump_g * r;
    }
    if (kOne < v) v = kOne;
    const F r = ee::sqrt(v - s.reb_t);
    return -s.reb_o - s.reb_g * r;
}

F kind7_ride_force(const Car& c, int i, F x, const Kind7Ride& r) {
    const size_t a = size_t(i >> 1);
    const Suspension& s = c.p.susp[a];
    const F v = c.speed;
    F d = x - s.x_static, drop = kZero;
    if (!(r.v1 <= v)) {
        if (r.v0 < v) drop = (r.drop * (v - r.v0)) / (r.v1 - r.v0);
    } else drop = r.drop;
    d = d + drop;
    F f = kZero;
    if (d < kZero) {
        const F lo = -r.travel;
        F g = r.lift / r.share[a];
        if (lo < d) g = (g * d) / lo;
        f = (g * (a ? c.p.xe58 : c.p.xe54)) * kHalf;
    }
    return f - s.k * drop;
}

F wheel_suspension(Wheel& w, const Suspension& s, F inv_dt, F x, const Kind7Ride* ride, const Car* c, int i) {
    if (s.droop < x) { // in the air
        w.susp = s.droop;
        w.load = kZero;
        w.damper_v = kZero;
        w.damper_f = kZero;
        w.spring_f = -s.preload;
        return -s.preload;
    }
    const F xc = x < s.bump_limit ? s.bump_limit : x;
    const F e = xc - s.knee;
    // (Rule::PodSpring, the later builds: the rate is the caller's, later 0x1DBB28's for special car 1)
    F fs = -xc * (c ? pod_spring(*c, i, s.k) : s.k);
    if (e < kZero) fs = fs + (e * e) * s.k2;
    F v = inv_dt * (x - w.susp);
    w.damper_v = v;
    F fd = damper_force(s, v);
    if (v < kZero && e < kZero) {
        if (v < kMinusOne) v = kMinusOne;
        fd = fd - s.bump_damping * v;
    }
    if (ride && ride->on && c && c->p.special == 7) fd = fd + kind7_ride_force(*c, i, xc, *ride); // (Europe 0x1E03F0)
    w.damper_f = fd;
    const F sum = fs + fd;
    w.susp = xc;
    w.spring_f = fs;
    const F load = sum + s.preload;
    w.load = load < kZero ? kZero : load;
    return sum;
}

F anti_roll(F x_left, F x_right, const Suspension& s) {
    const F d = x_left - x_right, g = s.anti_roll;
    return kZero <= d ? (g * d) * d : (-g * d) * d;
}

F wheel_camber(const Params& p, int i, F steer, F travel) {
    const size_t ax = size_t(i >> 1);
    const F r = p.camber[ax] + p.camber_gain[ax] * travel;
    if (i >= 2 || p.steer_camber == kZero) return r;
    const F t = steer * p.steer_camber;
    if (kZero <= t) return i ? r - p.steer_ratio[1] * t : r + p.steer_ratio[0] * t;
    return i ? r - p.steer_ratio[0] * t : r + p.steer_ratio[1] * t;
}

F hub_half_track(const Behaviour& b, const Params& p, int i, F travel) {
    const size_t ax = size_t(i >> 1);
    const F arm = p.swing_arm[ax];
    F h = p.half_track[ax];
    if (!(arm == kZero)) h = h - (kOne - ee::cos(atan2(b, travel, arm))) * arm; // (the build's atan2f)
    return (i & 1) ? h : -h;
}

void suspension(Car& c, const Kind7Ride* ride) {
    for (int i = 0; i < 4; ++i) {
        Wheel& w = c.w[size_t(i)];
        const Suspension& s = c.p.susp[size_t(i >> 1)];
        wheel_suspension(w, s, c.inv_dt, c.susp_x[size_t(i)], ride, &c, i);
        const F travel = w.susp - s.x_static;
        w.half_track = hub_half_track(c.behaviour, c.p, i, travel);
        w.camber = wheel_camber(c.p, i, c.steer_cmd, travel);
    }
    const F roll_f = anti_roll(c.w[0].susp, c.w[1].susp, c.p.susp[0]);
    const F roll_r = anti_roll(c.w[2].susp, c.w[3].susp, c.p.susp[1]);
    const F lon = c.p.long_transfer * (-c.x4d0 + c.drive_sum);
    const F aero_f = c.downforce[0] * kHalf, aero_r = c.downforce[1] * kHalf;
    const F lat = c.p.lat_transfer * -c.a_y;
    c.body_force[3] = (((c.w[3].spring_f + roll_r) - aero_r) - lon) - lat;
    c.body_force[0] = (((c.w[0].spring_f - roll_f) - aero_f) + lon) + lat;
    c.body_force[1] = (((c.w[1].spring_f + roll_f) - aero_f) + lon) - lat;
    c.body_force[2] = (((c.w[2].spring_f - roll_r) - aero_r) - lon) + lat;
}

} // namespace gt3r::sim::exact
