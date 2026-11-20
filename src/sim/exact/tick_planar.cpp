// Phase 7's planar motion: the forward step, the low-speed hold, lateral force and yaw, the stop hold; see tick.h.

#include "base/ee_math.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

constexpr F k2Pi = F::raw(0x40C90FDB), kNeg2Pi = F::raw(0xC0C90FDB); // the yaw rate's bound

} // namespace

void forward_step(Car& c) {
    const F dt = c.dt;
    const F rolling = c.rolling, ext = c.x4d0, drag = c.drag, a_x = c.a_x, u = c.speed, sum = c.drive_sum;
    F fx{};
    for (size_t i = 0; i < 4; ++i) fx = fx + c.t[i].steer_cos * c.w[i].fx_drive;
    F f = sum + fx;
    f = f + rolling;
    c.drive_sum = f;
    // The free wheels' extra mass: wheels whose flag byte is 0 (loaded, not braked).
    F extra{};
    for (int i = 0; i < 4; ++i)
        if (c.w[size_t(i)].flags == 0) extra = extra + c.p.wheel_mass[size_t(i >> 1)];
    c.inv_mass_eff = F() < extra ? F::raw(0x3F800000) / (extra + c.p.mass) : c.p.inv_mass;
    F a0 = f + ext;
    a0 = a0 + drag;
    a0 = a0 * c.inv_mass_eff;
    a0 = a_x + a0;
    c.a_x = a0;
    c.speed = u + a0 * dt;
    if (c.speed == F()) return;
    // The tyre reactions: brake-side forces along the wheel heading, the slip-angle forces along the velocity directions.
    F ft{};
    for (size_t i = 0; i < 4; ++i) {
        const Wheel& w = c.w[i];
        ft = ft + c.t[i].steer_cos * w.fx_brake;
        ft = ft - ee::sin(w.slip_dir) * w.force_a;
        ft = ft - ee::cos(w.slip_dir) * w.force_b;
    }
    const F at = ft * c.inv_mass_eff;
    const F u1 = c.speed, u2 = u1 + at * dt;
    if (F() < u1 * u2) {
        c.drive_sum = c.drive_sum + ft;
        c.a_x = c.a_x + at;
        c.speed = u2;
    } else {
        // The speed would change sign: it stops, the rest of it removed this tick.
        c.a_x = c.a_x + c.inv_dt * -u1;
        c.speed = F();
    }
}

F low_speed_hold(const Wheel& w, bool z) {
    if (w.peak_force == F()) return F();
    // (z: 2002 Tokyo-Geneva's speeds, folded toward zero: Europe)
    auto v = [z](u32 x, u32 y) { return F::raw(z ? y : x); };
    const F ws = w.rim_speed, slip = w.hub_vx - ws;
    if (ws < v(0xBFB1C71D, 0xBFB1C71B) || v(0x3FB1C71D, 0x3FB1C71B) < ws) return F();     // |w| > 5 km/h
    if (slip < v(0xBF0E38E4, 0xBF0E38E3) || v(0x3F0E38E4, 0x3F0E38E3) < slip) return F(); // |u_h - w| > 2 km/h
    // The hub speed across the wheel plane.
    F across = ee::cos(w.steer) * w.hub_vy;
    across = across + ee::sin(w.steer) * w.hub_vx;
    if (v(0x3F8E38E4, 0x3F8E38E3) < across || across < v(0xBF8E38E4, 0xBF8E38E3)) return F(); // > 4 km/h
    const F s = ee::sin(across * F::raw(0x4034F4AB));                           // 0.9 pi
    return s * ee::cos(ws * F::raw(0x3F90C3BC)) * w.peak_force;                       // 0.36 pi
}

void lateral_yaw(Car& c, bool airborne) {
    const F dt = c.dt;
    if (airborne) {
        c.lat_g = F();
        c.a_y = F();
        return;
    }
    const Params& p = c.p;
    const F lever_x[4] = {-p.half_track[0], p.half_track[0], -p.half_track[1], p.half_track[1]};
    const F lever_y[4] = {p.dist_front, p.dist_front, -p.dist_rear, -p.dist_rear};
    F lat{}, torque{};
    for (size_t i = 0; i < 4; ++i) {
        const Wheel& w = c.w[i];
        const Drive& t = c.t[i];
        const F g = w.fx_drive + w.fx_brake;
        const F cs = ee::cos(w.slip_dir), sn = ee::sin(w.slip_dir);
        // Fa across the car, Fb along it: the slip-angle forces, the hold force and the longitudinal force.
        F fb = -sn * w.force_a + -cs * w.force_b;
        F fa = -cs * w.force_a + sn * w.force_b;
        const F hold = low_speed_hold(w, toward_zero(c.behaviour));
        fa = fa - t.steer_cos * hold;
        fb = fb - t.steer_sin * hold;
        fb = fb + t.steer_cos * g;
        fa = fa - t.steer_sin * g;
        lat = lat + fa;
        torque = torque + (lever_x[i] * fb - lever_y[i] * fa);
    }
    c.yaw_accel = torque * p.inv_yaw_inertia;
    const F r = c.yaw_rate + c.yaw_accel * dt;
    const F hi = folded(c.behaviour, k2Pi.bits, 0x40C90FDA), lo = folded(c.behaviour, kNeg2Pi.bits, 0xC0C90FDA); // (Europe's)
    c.yaw_rate = hi < r ? hi : r < lo ? lo : r;
    c.a_y = (lat - c.x4d4) * p.inv_mass;
    F grip{};
    for (int i = 0; i < 4; ++i) {
        const u8 s = c.w[size_t(i)].surface;
        const TireRecord& rec = tire_record(p, i, s);
        grip = grip + rec.mu[s] * rec.grip;
    }
    c.lat_g = c.a_y * F::raw(0x40800000) / grip;
}

void stop_hold(Car& c, bool airborne) {
    const F dt = c.dt;
    if (c.hold != 0) {
        c.vel.y = F();
        c.drive_sum = F();
        c.yaw_rate = F();
        c.vel.x = F();
        return;
    }
    if (airborne) return;
    // Near standstill (|u| < 1 km/h) with no fade weight on any wheel and a braked wheel.
    auto braked_stop = [&] {
        if (!(folded(c.behaviour, 0xBE8E38E4, 0xBE8E38E3) < c.speed && c.speed < folded(c.behaviour, 0x3E8E38E4, 0x3E8E38E3))) return false; // (Europe's)
        for (const Wheel& w : c.w)
            if (!(w.slip_fade == F())) return false;
        for (const Wheel& w : c.w)
            if (w.flags & 2) return true;
        return false;
    };
    if (c.race_hold != 0 || c.driver_kind == 8 || braked_stop()) {
        c.lat_speed = F();
        c.yaw_rate = F();
        c.vel.x = F();
        c.vel.y = F();
        c.drive_sum = F();
        c.a_x = F();
        c.a_y = F();
        c.lat_g = F();
        c.speed = F();
        return;
    }
    const F ax = c.a_x * dt, ay = c.a_y * dt;
    c.vel.x = c.vel.x + (ax * c.fwd.x + ay * c.lat.x);
    c.vel.y = c.vel.y + (ax * c.fwd.y + ay * c.lat.y);
}

bool airborne(const Car& c) {
    F sum = c.w[0].load + c.w[1].load;
    sum = sum + c.w[2].load;
    sum = sum + c.w[3].load;
    return sum == F();
}

} // namespace gt3r::sim::exact
