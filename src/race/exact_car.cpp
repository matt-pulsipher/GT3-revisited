// The exact core's car record as the race screen reads a car; see exact_car.h.

#include "race/exact_car.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt3r::race {

namespace {

float h(ex::F v) { return v.host(); }

// Rotation matrix columns (car X -> lat, Y -> up, Z -> -fwd) to a quaternion (VehicleState::orientation).
sim::Quat axes_quat(const sim::Vec3& lat, const sim::Vec3& up, const sim::Vec3& fwd) {
    const double m00 = lat.x, m10 = lat.y, m20 = lat.z;
    const double m01 = up.x, m11 = up.y, m21 = up.z;
    const double m02 = -fwd.x, m12 = -fwd.y, m22 = -fwd.z;
    const double tr = m00 + m11 + m22;
    double w, x, y, z;
    if (tr > 0) {
        double s = std::sqrt(tr + 1.0) * 2;
        w = 0.25 * s, x = (m21 - m12) / s, y = (m02 - m20) / s, z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        double s = std::sqrt(1.0 + m00 - m11 - m22) * 2;
        w = (m21 - m12) / s, x = 0.25 * s, y = (m01 + m10) / s, z = (m02 + m20) / s;
    } else if (m11 > m22) {
        double s = std::sqrt(1.0 + m11 - m00 - m22) * 2;
        w = (m02 - m20) / s, x = (m01 + m10) / s, y = 0.25 * s, z = (m12 + m21) / s;
    } else {
        double s = std::sqrt(1.0 + m22 - m00 - m11) * 2;
        w = (m10 - m01) / s, x = (m02 + m20) / s, y = (m12 + m21) / s, z = 0.25 * s;
    }
    const double n = std::sqrt(w * w + x * x + y * y + z * z);
    return {float(w / n), float(x / n), float(y / n), float(z / n)};
}

// The CoG in the car frame: cog_height above the ground, (a - b) / 2 along the car (the nose at -Z).
sim::Vec3 cog_local(const ex::Params& p) { return {0, h(p.cog_height), (h(p.dist_front) - h(p.dist_rear)) * 0.5f}; }

constexpr float kRadToRpm = 9.549296586f;

} // namespace

sim::VehicleSetup exact_setup(const ex::Car& c) {
    const ex::Params& p = c.p;
    sim::VehicleSetup s;
    s.cog_offset = cog_local(p);
    s.cog_front_extent = h(p.body_front);
    s.cog_rear_extent = h(p.body_rear);
    s.wheelbase = h(p.wheelbase);
    s.length = h(p.body_front) + h(p.body_rear);
    s.width = h(p.body_width);
    s.height = h(p.body_height);
    s.steer_lock = h(p.max_lock);
    s.forward_gears = std::max(1, int(p.d.gears));
    s.idle_rpm = float(p.e.idle);
    s.rev_limit_rpm = float(p.e.rev_limit);
    s.max_rpm = std::ceil((s.rev_limit_rpm + 800) / 1000.0f) * 1000.0f;
    s.upshift_rpm = p.d.up_rpm ? float(p.d.up_rpm) : s.rev_limit_rpm - 200;
    s.has_turbo = h(p.e.boost_max) > 0;
    s.max_boost = h(p.e.boost_max);
    for (size_t i = 0; i < 4; ++i) {
        const size_t a = i >> 1;
        sim::VehicleSetup::Wheel& w = s.wheels[i];
        const float side = (i & 1) ? 1.0f : -1.0f;
        w.radius = h(p.tire_radius[a]);
        w.width = h(p.width[a]);
        w.rest_center = {side * h(p.half_track[a]), w.radius, a == 0 ? -s.wheelbase * 0.5f : s.wheelbase * 0.5f};
        w.static_suspension = -h(p.susp[a].x_static);
        w.ride_height = h(p.ride_height[a]);
    }
    return s;
}

void exact_state(const ex::Car& c, const sim::VehicleSetup& setup, sim::VehicleState& out) {
    const sim::Vec3 fwd = exact_to_world(c.fwd), lat = exact_to_world(c.lat), up = exact_to_world(c.up);
    const sim::Vec3 cog = exact_to_world(c.pos), cl = setup.cog_offset;
    out.orientation = axes_quat(lat, up, fwd);
    out.position = cog - (lat * cl.x + up * cl.y - fwd * cl.z);
    out.velocity = exact_to_world(c.vel);
    out.angular_velocity = sim::Vec3{0, h(c.yaw_rate), 0} + lat * h(c.pitch_rate) + fwd * h(c.roll_rate);
    out.engine_rpm = h(c.omega) * kRadToRpm;
    out.gear = c.gear == 0 ? -1 : int(c.gear);
    out.throttle = h(c.throttle);
    out.speed_kmh = h(c.speedo) * 3.6f;
    out.rev_limiter = c.rev_limiter != 0;
    out.steer = -h(c.steer_cmd);
    for (size_t i = 0; i < 4; ++i) {
        const ex::Wheel& w = c.w[i];
        const ex::Ground& g = c.g[i];
        sim::WheelState& o = out.wheels[i];
        o.contact = {h(g.x), h(w.ground), h(g.z)};
        o.steer_angle = -h(w.steer);
        o.spin_angle = h(w.spin);
        // The record's previous spin is moved by the turns the angle wrapped by (the loops): the tick's own turn.
        o.spin_step = h(w.spin) - h(c.prev.spin[i]);
        o.angular_velocity = h(w.rim_speed) * h(c.p.inv_tire_radius[i >> 1]);
        o.suspension = -h(w.susp);
        o.slip_ratio = h(w.slip_ratio);
        o.slip_angle = h(w.slip_dir);
        o.load = h(w.load);
        o.grounded = h(w.load) > 0;
        o.surface = u32(g.material) | u32(g.flags) << 8;
        o.flap = h(w.flap);
        o.camber = h(w.camber);
        o.brake_heat = h(w.brake_heat);
    }
}

namespace {

// sim::VehicleTelemetry::traction_level at a wheel: both slips as shares of the tyre's best, the edge at 1.
float traction_at(const ex::Car& c, size_t i) {
    const ex::Wheel& w = c.w[i];
    const ex::TireRecord& tire = ex::tire_record(c.p, int(i), w.surface);
    const float best = -h(tire.slip_drive), peak = h(tire.peak_slip), circle = h(w.circle) * h(w.peak_force);
    if (best <= 0 || peak <= 0) return 0;
    const float along = std::max(-h(w.slip_ratio), 0.0f) / best;
    const float angle = std::fabs(h(c.t[i].slip_angle)) * h(w.slip_fade) / peak;
    const float across = std::max(angle, circle > 0 ? std::hypot(h(w.force_a), h(w.force_b)) / circle : 0.0f);
    const float grip = std::hypot(along, across);
    return grip < 1 ? 0.4f * std::clamp((grip - 0.85f) / 0.15f, 0.0f, 1.0f) : 0.4f + 0.6f * std::min(grip - 1, 1.0f);
}

} // namespace

void exact_telemetry(const ex::Car& c, bool wear_on, float wall, sim::VehicleTelemetry& t) {
    t = sim::VehicleTelemetry{};
    t.engine_torque = h(c.clutch_torque);
    t.clutch = c.clutch == 0 ? 0.0f : 1.0f;
    t.clutch_locked = c.clutch == 1;
    t.clutch_state = c.clutch;
    t.pitch = h(c.pitch), t.roll = h(c.roll), t.pitch_rate = h(c.pitch_rate), t.roll_rate = h(c.roll_rate);
    t.steer_angle = -0.5f * (h(c.w[0].steer) + h(c.w[1].steer));
    t.steer_rate = -h(c.steer_rate);
    t.g_over_mu = h(c.lat_g), t.yaw_acc = h(c.yaw_accel);
    t.understeer = h(c.understeer), t.oversteer = h(c.oversteer);
    t.throttle_processed = h(c.throttle);
    t.turbo_load = h(c.turbo_load);
    t.turbo_lag = {c.turbo_lag[0], c.turbo_lag[1]};
    t.boost = h(c.p.e.boost_max) > 0 ? h(ex::boost(c.p.e, c.turbo_lag[0], c.turbo_lag[1])) - 1 : 0;
    t.slipstream = h(c.slip_filter);
    t.drag_force = h(c.drag);
    t.downforce = h(c.downforce[0]) + h(c.downforce[1]);
    t.tcs_active = c.tcs_status == 2;
    t.tcs_status = c.tcs_status, t.asm_status = c.ascc_status;
    t.steer_pct = c.steer_pct, t.throttle_pedal = h(c.throttle_pedal), t.brake_pedal = h(c.brake_pedal);
    const float pedal = h(c.brake_pedal), throttle = h(c.throttle_pedal);
    for (size_t i = 0; i < 4; ++i) {
        const ex::Wheel& w = c.w[i];
        t.beta[i] = h(c.t[i].slip_angle);
        t.fx[i] = h(w.fx_drive) + h(w.fx_brake);
        t.fy[i] = h(w.force_a);
        t.peak_force[i] = h(w.peak_force);
        t.slip_gain[i] = h(w.slip_gain);
        t.brake_pressure[i] = h(w.brake);
        t.brake_torque[i] = h(c.t[i].brake_torque);
        t.tire_wear[i] = h(w.wear);
        t.wear_grip[i] = h(w.wear_grip);
        t.wear_level[i] = w.wear_level;
        t.smoke[i] = h(w.slip_intensity);
        t.smoke_emit[i] = h(w.smoke_emit);
        t.skid_pitch[i] = h(w.skid_pitch);
        t.brake_heat[i] = h(w.brake_heat);
        if (throttle > 0.05f && h(w.load) > 0 && ex::tcs_reads_axle(c.p.d.layout, int(i >> 1)))
            t.traction_level = std::max(t.traction_level, traction_at(c, i));
        // The ABS (ex::brakes' limit) on a braked wheel on the ground: nearing the tyre's best braking slip (its
        // target, before ASCC's own cut) up to 0.4, then the share of the pressure the ABS takes off past it.
        if (pedal <= 0.05f || h(w.load) <= 0) continue;
        const size_t axle = i >> 1;
        const float slip = h(w.slip_ratio);
        const float target = h(ex::tire_record(c.p, int(i), w.surface).slip_brake) * h(c.p.brake_slip_scale[axle]);
        if (target <= 0 || slip <= 0) continue;
        float level = 0.4f * std::clamp((slip / target - 0.85f) / 0.15f, 0.0f, 1.0f);
        if (const float over = slip - target; over > 0) {
            const float g = h(c.p.abs_gain[axle]);
            level = 0.4f + 0.6f * std::min(1.0f, g > 0 ? over * g * h(w.slip_gain) : over / 0.5f);
        }
        t.abs_level = std::max(t.abs_level, level);
    }
    // The TCS holds the slip short of the tyre's best, so its cut counts by itself (not a gear change's, clutch state 3).
    if (c.tcs_status == 2 && c.clutch != 3 && throttle > 0)
        t.traction_level = std::max(t.traction_level, 0.4f + 0.6f * std::clamp(1 - h(c.throttle) / throttle, 0.0f, 1.0f));
    t.traction_level *= std::min(throttle * 2, 1.0f);
    t.diff_locked = {c.diff_lock[0] != 0, c.diff_lock[1] != 0};
    t.tires_worn = (c.x535 & 4) != 0;
    t.on_pit_surface = (c.x535 & 0x10) != 0;
    t.wall_impulse = wall;
    t.pad_rumble = c.rumble;
    t.pad_output = c.control != 1 && c.control != 2 && c.driver_kind == 0;
    t.wheel_force = c.steer_force;
    t.wear_on = wear_on;
    t.dirt = h(c.dirt);
}

CarFacts exact_facts(const ex::Car& c) {
    const ex::Params& p = c.p;
    CarFacts f;
    f.mass = h(p.mass);
    f.front_weight = 1 - h(p.rear_weight);
    const float ahead = (h(p.dist_rear) - h(p.dist_front)) * 0.5f; // the CoG ahead of the wheelbase midpoint
    f.front_extent = h(p.body_front) + ahead;
    f.rear_extent = h(p.body_rear) - ahead;
    f.width = h(p.body_width);
    f.length = h(p.body_front) + h(p.body_rear);
    f.turbo = h(p.e.boost_max) > 0;
    for (int g = 1; g <= int(p.d.gears) && g < int(p.d.ratio.size()); ++g) f.ratios.push_back(h(p.d.ratio[size_t(g)]));
    f.reverse_ratio = std::fabs(h(p.d.ratio[0]));
    // The driven axle by the layout code [physics §2.6]: 1, 2 the front; 3, 7 both; else the rear.
    switch (p.d.layout) {
    case 1:
    case 2: f.drive = sim::Drive::FF; break;
    case 3:
    case 7: f.drive = sim::Drive::AWD; break;
    default: f.drive = sim::Drive::FR; break;
    }
    return f;
}

sim::EngineCondition exact_condition(const ex::Car& c) { return {c.odometer, h(c.engine_wear), h(c.oil)}; }

std::array<u32, 8> exact_condition_words(const sim::EngineCondition& c, float dirt) {
    std::array<u32, 8> w{};
    w[0] = c.odometer;
    w[1] = ex::F(c.wear_m).bits;
    w[2] = ex::F(c.oil_m).bits;
    w[3] = ex::F(dirt).bits;
    return w;
}

} // namespace gt3r::race
