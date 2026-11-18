#pragma once

// The field table of the car record mirror: every member of sim::exact::Car with its offset in GT3's car record (0x16DC
// bytes; car = conductor + 0xC8 + 0x16DC i), and the record image <-> mirror copies built on it. The record image is what a
// start state or the parameter builder produces; the mirror is what the tick's ports work on.

#include "sim/exact/car_record.h"

#include <string>
#include <vector>

namespace gt3r::sim::exact {

constexpr u32 kCarSize = 0x16DC;
constexpr u32 kF = 0xF84;                         // the dynamics block F
constexpr u32 kWheel0 = 0xFE4, kWheelSize = 0xA4; // W_i = F + 0x60 + 0xA4 i
constexpr u32 kGround0 = 0x14BC, kGroundSize = 0x34;
constexpr u32 kParams = 0x10;                       // the parameter block P
constexpr u32 kDrive0 = 0x1274, kDriveSize = 0x24;  // T_i

// Every field of the record: v(name, index (-1 for a single field), offset from the car, member). Add a line here with
// each new ex::Car member. Members are ee::F, u32, s32, u16, s16, u8 or s8 (the record's own widths).
template <class C, class V> void fields(C& c, V&& v) {
    auto vec = [&](const char* x, const char* y, const char* z, int i, u32 off, auto& m) {
        v(x, i, off, m.x), v(y, i, off + 4, m.y), v(z, i, off + 8, m.z);
    };
    for (int i = 0; i < 2; ++i) {
        v("p.steer_ratio", i, kParams + 0x110 + 4 * u32(i), c.p.steer_ratio[size_t(i)]);
        v("p.toe", i, kParams + 0xE20 + 4 * u32(i), c.p.toe[size_t(i)]);
    }
    v("steer_cmd", -1, kF + 0x41C, c.steer_cmd);
    v("pitch", -1, 0x13EC, c.pitch), v("roll", -1, 0x13F0, c.roll), v("heading", -1, 0x13F4, c.heading);
    v("ground_pitch", -1, 0x1418, c.ground_pitch), v("ground_roll", -1, 0x141C, c.ground_roll);
    v("f3e4", -1, kF + 0x3E4, c.f3e4), v("lag_heading_diff", -1, kF + 0x4F8, c.lag_heading_diff),
        v("lag_pitch_diff", -1, kF + 0x4FC, c.lag_pitch_diff);
    for (int i = 0; i < 4; ++i) {
        auto& w = c.w[size_t(i)];
        const u32 W = kWheel0 + kWheelSize * u32(i);
        v("w.ground", i, W + 0x04, w.ground), v("w.steer", i, W + 0x0C, w.steer), v("w.camber", i, W + 0x10, w.camber),
            v("w.half_track", i, W + 0x14, w.half_track), v("w.susp", i, W + 0x18, w.susp), v("w.spin", i, W + 0x38, w.spin),
            v("w.brake_heat", i, W + 0x94, w.brake_heat), v("w.flap", i, W + 0x98, w.flap);
        auto& g = c.g[size_t(i)];
        const u32 G = kGround0 + kGroundSize * u32(i);
        v("g.x", i, G + 0x00, g.x), v("g.h", i, G + 0x04, g.h), v("g.z", i, G + 0x08, g.z), v("g.seg", i, G + 0x0C, g.seg),
            v("g.height", i, G + 0x10, g.height), v("g.attr_a", i, G + 0x14, g.attr_a), v("g.attr_b", i, G + 0x18, g.attr_b);
        vec("g.normal.x", "g.normal.y", "g.normal.z", i, G + 0x1C, g.normal);
        v("g.plane_d", i, G + 0x28, g.plane_d), v("g.attr_a_raw", i, G + 0x2C, g.attr_a_raw), v("g.attr_b_raw", i, G + 0x2E, g.attr_b_raw),
            v("g.miss", i, G + 0x30, g.miss), v("g.material", i, G + 0x31, g.material), v("g.x32", i, G + 0x32, g.x32),
            v("g.flags", i, G + 0x33, g.flags);
        const u32 T = kDrive0 + kDriveSize * u32(i);
        v("t.steer_sin", i, T + 0x18, c.t[size_t(i)].steer_sin), v("t.steer_cos", i, T + 0x1C, c.t[size_t(i)].steer_cos);
        auto& p = c.prev;
        const u32 k = 4 * u32(i);
        v("prev.susp", i, 0x15D4 + k, p.susp[size_t(i)]), v("prev.steer", i, 0x15E4 + k, p.steer[size_t(i)]),
            v("prev.camber", i, 0x15F4 + k, p.camber[size_t(i)]), v("prev.half_track", i, 0x1604 + k, p.half_track[size_t(i)]),
            v("prev.spin", i, 0x1614 + k, p.spin[size_t(i)]), v("prev.brake_heat", i, 0x1624 + k, p.brake_heat[size_t(i)]),
            v("prev.attr_a", i, 0x1634 + k, p.attr_a[size_t(i)]), v("prev.attr_b", i, 0x1644 + k, p.attr_b[size_t(i)]);
        vec("prev.hub.x", "prev.hub.ground", "prev.hub.z", i, 0x1654 + 0xC * u32(i), p.hub[size_t(i)]);
        v("prev.flap", i, 0x1684 + k, p.flap[size_t(i)]);
    }
    v("prev.pitch", -1, 0x15C0, c.prev.pitch), v("prev.roll", -1, 0x15C4, c.prev.roll), v("prev.heading", -1, 0x15C8, c.prev.heading);
    v("prev.ground_pitch", -1, 0x15CC, c.prev.ground_pitch), v("prev.ground_roll", -1, 0x15D0, c.prev.ground_roll);
    v("prev.steer_view", -1, 0x1694, c.prev.steer_view), v("prev.lag_heading_diff", -1, 0x1698, c.prev.lag_heading_diff),
        v("prev.lag_pitch_diff", -1, 0x169C, c.prev.lag_pitch_diff), v("prev.f3e4", -1, 0x16A0, c.prev.f3e4);
    vec("draw_pos.x", "draw_pos.h", "draw_pos.z", -1, 0x158C, c.draw_pos), vec("draw_ground.x", "draw_ground.h", "draw_ground.z", -1, 0x1598, c.draw_ground);
    v("steer_view", -1, 0x15A4, c.steer_view);
    vec("draw_pos_prev.x", "draw_pos_prev.h", "draw_pos_prev.z", -1, 0x15A8, c.draw_pos_prev),
        vec("draw_ground_prev.x", "draw_ground_prev.h", "draw_ground_prev.z", -1, 0x15B4, c.draw_ground_prev);
    v("wheel_pos", -1, 0x16A4, c.wheel_pos), v("wheel_pos_prev", -1, 0x16A6, c.wheel_pos_prev);
    v("speed_abs", -1, 0x16A8, c.speed_abs), v("max_speed", -1, 0x16AC, c.max_speed);
    v("feedback", -1, 0x16C8, c.feedback);
    v("rumble", -1, 0x16D5, c.rumble), v("steer_force", -1, 0x16D6, c.steer_force), v("speedo", -1, 0x16D8, c.speedo);
    auto& p = c.p;
    v("p.body_front", -1, kParams + 0x00, p.body_front), v("p.body_rear", -1, kParams + 0x04, p.body_rear),
        v("p.body_width", -1, kParams + 0x08, p.body_width), v("p.body_height", -1, kParams + 0x0C, p.body_height);
    v("p.dist_front", -1, kParams + 0x10, p.dist_front), v("p.dist_rear", -1, kParams + 0x14, p.dist_rear);
    v("p.wheelbase", -1, kParams + 0x18, p.wheelbase), v("p.track", -1, kParams + 0x1C, p.track);
    v("p.rear_weight", -1, kParams + 0x20, p.rear_weight);
    v("p.x54", -1, kParams + 0x54, p.x54), v("p.max_lock", -1, kParams + 0x58, p.max_lock), v("p.xe54", -1, kParams + 0xE54, p.xe54);
    // A lookup table: the count at the header, the arrays where the builder points them (n index = the table, x / y index =
    // table * N + k).
    auto table = [&](const char* n, const char* x, const char* y, int i, u32 head, u32 xs, u32 ys, auto& t) {
        const int base = i < 0 ? 0 : i * int(t.x.size());
        v(n, i, head, t.n);
        for (size_t k = 0; k < t.x.size(); ++k) v(x, base + int(k), xs + 4 * u32(k), t.x[k]), v(y, base + int(k), ys + 4 * u32(k), t.y[k]);
    };
    table("p.steer_b.n", "p.steer_b.x", "p.steer_b.y", -1, kParams + 0x5C, kParams + 0x68, kParams + 0x80, p.steer_b);
    table("p.steer_a.n", "p.steer_a.x", "p.steer_a.y", -1, kParams + 0x98, kParams + 0xA4, kParams + 0xBC, p.steer_a);
    table("p.steer_c.n", "p.steer_c.x", "p.steer_c.y", -1, kParams + 0xD4, kParams + 0xE0, kParams + 0xF8, p.steer_c);
    {
        auto& e = p.e;
        const u32 E = kParams + 0x138;
        table("p.e.torque.n", "p.e.torque.x", "p.e.torque.y", -1, E + 0x00, E + 0x0C, E + 0x4C, e.torque);
        v("p.e.friction", -1, E + 0x90, e.friction);
        v("p.e.rev_limit", -1, E + 0x8C, e.rev_limit), v("p.e.idle", -1, E + 0x8E, e.idle);
        v("p.e.power_cap_on", -1, E + 0x9B, e.power_cap_on), v("p.e.power_cap", -1, E + 0x9C, e.power_cap);
        v("p.e.power_cap_soft", -1, E + 0xA0, e.power_cap_soft), v("p.e.boost_max", -1, E + 0xBC, e.boost_max);
        auto& d = p.d;
        const u32 D = kParams + 0x1F8;
        v("p.d.unequal", -1, D + 0x01, d.unequal), v("p.d.kind", -1, D + 0x12, d.kind);
        v("p.d.layout", -1, D + 0x00, d.layout), v("p.d.gears", -1, D + 0x02, d.gears), v("p.d.driven_axle", -1, D + 0x03, d.driven_axle);
        v("p.d.awd_k", -1, D + 0x04, d.awd_k), v("p.d.awd_capacity", -1, D + 0x08, d.awd_capacity);
        v("p.d.engine_inertia", -1, D + 0x0C, d.engine_inertia);
        v("p.d.launch_rpm", -1, D + 0x2C, d.launch_rpm), v("p.d.up_rpm", -1, D + 0x2E, d.up_rpm);
        v("p.d.shift_clutch_torque", -1, D + 0x9C, d.shift_clutch_torque), v("p.d.inv_axle_inertia", -1, D + 0xB0, d.inv_axle_inertia);
        v("p.d.inv_engine_inertia", -1, D + 0xB4, d.inv_engine_inertia);
        for (int i = 0; i < 2; ++i) {
            const size_t j = size_t(i);
            const u32 k = 4 * u32(i);
            v("p.e.turbo_knee", i, E + 0x94 + 2 * u32(i), e.turbo_knee[j]), v("p.e.turbo_shape", i, E + 0x98 + u32(i), e.turbo_shape[j]);
            v("p.e.turbo_peak", i, E + 0xA4 + k, e.turbo_peak[j]), v("p.e.turbo_rate", i, E + 0xAC + k, e.turbo_rate[j]);
            v("p.e.turbo_coef", i, E + 0xB4 + k, e.turbo_coef[j]);
            v("p.d.diff_type", i, D + 0x10 + u32(i), d.diff_type[j]), v("p.d.diff_initial", i, D + 0x14 + k, d.diff_initial[j]);
            v("p.d.diff_accel", i, D + 0x1C + k, d.diff_accel[j]), v("p.d.diff_decel", i, D + 0x24 + k, d.diff_decel[j]);
            v("p.d.wheel_gain", i, D + 0xA0 + k, d.wheel_gain[j]), v("p.d.axle_inertia", i, D + 0xA8 + k, d.axle_inertia[j]);
        }
        for (int g = 0; g < 6; ++g) v("p.d.down_rpm", g, D + 0x30 + 2 * u32(g), d.down_rpm[size_t(g)]);
        for (int g = 0; g < 8; ++g)
            v("p.d.ratio", g, D + 0x3C + 4 * u32(g), d.ratio[size_t(g)]), v("p.d.gear_speed", g, D + 0x5C + 4 * u32(g), d.gear_speed[size_t(g)]);
    }
    v("p.handbrake_torque", -1, kParams + 0xE30, p.handbrake_torque), v("p.drag_coef", -1, kParams + 0xE60, p.drag_coef);
    v("p.lat_transfer", -1, kParams + 0x34, p.lat_transfer), v("p.long_transfer", -1, kParams + 0x38, p.long_transfer);
    v("p.mass", -1, kParams + 0x48, p.mass), v("p.inv_mass", -1, kParams + 0x4C, p.inv_mass);
    v("p.inv_sprung_mass", -1, kParams + 0x50, p.inv_sprung_mass), v("p.steer_camber", -1, kParams + 0xE1C, p.steer_camber);
    v("p.inv_yaw_inertia", -1, kParams + 0x11C, p.inv_yaw_inertia);
    for (int a = 0; a < 2; ++a) {
        const size_t j = size_t(a);
        const u32 k = 4 * u32(a);
        v("p.half_track", a, kParams + 0x28 + k, p.half_track[j]), v("p.pitch_lever", a, kParams + 0x120 + k, p.pitch_lever[j]);
        v("p.roll_lever", a, kParams + 0x128 + k, p.roll_lever[j]), v("p.pitch_tire", a, kParams + 0x130 + k, p.pitch_tire[j]);
        v("p.tire_radius", a, kParams + 0x274 + k, p.tire_radius[j]), v("p.inv_tire_radius", a, kParams + 0x27C + k, p.inv_tire_radius[j]), v("p.width", a, kParams + 0x284 + k, p.width[j]);
        v("p.wheel_mass", a, kParams + 0x2B0 + k, p.wheel_mass[j]);
        v("p.camber", a, kParams + 0xDFC + k, p.camber[j]), v("p.camber_gain", a, kParams + 0xE04 + k, p.camber_gain[j]);
        v("p.swing_arm", a, kParams + 0xE14 + k, p.swing_arm[j]);
        v("p.brake_torque", a, kParams + 0xE28 + k, p.brake_torque[j]);
        v("p.wear_load_offset", a, kParams + 0xE6C + k, p.wear_load_offset[j]), v("p.wear_load_gain", a, kParams + 0xE74 + k, p.wear_load_gain[j]);
        v("p.axle_grip", a, kParams + 0xDF0 + k, p.axle_grip[j]), v("p.downforce_coef", a, kParams + 0xE64 + k, p.downforce_coef[j]);
        table("p.camber_grip.n", "p.camber_grip.x", "p.camber_grip.y", a, kParams + 0x340 + 0xC * u32(a), kParams + 0x358 + 0x10 * u32(a),
              kParams + 0x378 + 0x10 * u32(a), p.camber_grip[j]);
        v("p.abs_gain", a, kParams + 0xE38 + k, p.abs_gain[j]), v("p.brake_slip_scale", a, kParams + 0xE40 + k, p.brake_slip_scale[j]);
        v("p.abs_peak_scale", a, kParams + 0xE48 + k, p.abs_peak_scale[j]);
        for (u32 m = 0; m < 16; ++m) v("p.tire_index", 16 * a + int(m), kParams + 0xDD0 + 16 * u32(a) + m, p.tire_index[j][m]);
        auto& s = p.susp[j];
        const u32 S = kParams + 0x2B8 + 0x44 * u32(a);
        v("p.susp.knee", a, S + 0x00, s.knee), v("p.susp.bump_limit", a, S + 0x04, s.bump_limit);
        v("p.susp.droop", a, S + 0x08, s.droop), v("p.susp.offset", a, S + 0x0C, s.offset);
        v("p.susp.x_static", a, S + 0x10, s.x_static), v("p.susp.k", a, S + 0x14, s.k);
        v("p.susp.bump_t", a, S + 0x18, s.bump_t), v("p.susp.bump_o", a, S + 0x1C, s.bump_o);
        v("p.susp.bump_g", a, S + 0x20, s.bump_g), v("p.susp.reb_t", a, S + 0x24, s.reb_t);
        v("p.susp.reb_o", a, S + 0x28, s.reb_o), v("p.susp.reb_g", a, S + 0x2C, s.reb_g);
        v("p.susp.anti_roll", a, S + 0x30, s.anti_roll), v("p.susp.k2", a, S + 0x34, s.k2);
        v("p.susp.preload", a, S + 0x3C, s.preload), v("p.susp.bump_damping", a, S + 0x40, s.bump_damping);
    }
    for (int r = 0; r < 6; ++r) {
        auto& t = p.tire[size_t(r)];
        const u32 R = kParams + 0x398 + 0x1B4 * u32(r);
        v("p.tire.grip", r, R, t.grip), v("p.tire.wear_rate", r, R + 0x04, t.wear_rate), v("p.tire.slip_drive", r, R + 0x140, t.slip_drive), v("p.tire.slip_brake", r, R + 0x144, t.slip_brake);
        for (u32 s = 0; s < 16; ++s) v("p.tire.mu", r, R + 0x08 + 4 * s, t.mu[s]);
        v("p.tire.curve_a.n", r, R + 0x48, t.curve_a.n), v("p.tire.curve_b.n", r, R + 0x98, t.curve_b.n),
            v("p.tire.curve_c.n", r, R + 0xD4, t.curve_c.n);
        for (u32 k = 0; k < 8; ++k) v("p.tire.curve_a.x", r, R + 0x54 + 4 * k, t.curve_a.x[k]), v("p.tire.curve_a.y", r, R + 0x74 + 4 * k, t.curve_a.y[k]);
        for (u32 k = 0; k < 6; ++k) v("p.tire.curve_b.x", r, R + 0xA4 + 4 * k, t.curve_b.x[k]), v("p.tire.curve_b.y", r, R + 0xBC + 4 * k, t.curve_b.y[k]);
        for (u32 k = 0; k < 12; ++k)
            v("p.tire.curve_c.x", r, R + 0xE0 + 4 * k, t.curve_c.x[k]), v("p.tire.curve_c.y", r, R + 0x110 + 4 * k, t.curve_c.y[k]),
                v("p.tire.curve_d", r, R + 0x158 + 4 * k, t.curve_d[k]);
        table("p.tire.load_curve.n", "p.tire.load_curve.x", "p.tire.load_curve.y", r, R + 0x188, R + 0x194, R + 0x1A4, t.load_curve);
    }
    for (int i = 0; i < 4; ++i) {
        auto& w = c.w[size_t(i)];
        const u32 W = kWheel0 + kWheelSize * u32(i);
        v("w.ground0", i, W + 0x00, w.ground0), v("w.load", i, W + 0x08, w.load), v("w.damper_v", i, W + 0x1C, w.damper_v),
            v("w.rim_speed", i, W + 0x20, w.rim_speed), v("w.surface", i, W + 0x24, w.surface), v("w.x25", i, W + 0x25, w.x25),
            v("w.surface_flags", i, W + 0x26, w.surface_flags), v("w.x28", i, W + 0x28, w.x28), v("w.smoke_count", i, W + 0x2A, w.smoke_count),
            v("w.smoke_emit", i, W + 0x2C, w.smoke_emit), v("w.slip_intensity", i, W + 0x30, w.slip_intensity),
            v("w.skid_pitch", i, W + 0x34, w.skid_pitch);
        vec("w.hub.x", "w.hub.y", "w.hub.z", i, W + 0x3C, w.hub);
        v("w.slip_gain", i, W + 0x48, w.slip_gain), v("w.hub_vx", i, W + 0x4C, w.hub_vx), v("w.hub_vy", i, W + 0x50, w.hub_vy),
            v("w.peak_force", i, W + 0x54, w.peak_force), v("w.wear_grip", i, W + 0x58, w.wear_grip), v("w.wear_byte", i, W + 0x5C, w.wear_byte),
            v("w.wear_level", i, W + 0x5D, w.wear_level), v("w.flags", i, W + 0x5F, w.flags), v("w.bump", i, W + 0x60, w.bump),
            v("w.circle", i, W + 0x64, w.circle), v("w.slip_ratio", i, W + 0x68, w.slip_ratio), v("w.speed_ramp", i, W + 0x6C, w.speed_ramp);
        v("w.spring_f", i, W + 0x70, w.spring_f), v("w.damper_f", i, W + 0x74, w.damper_f), v("w.slip_dir", i, W + 0x78, w.slip_dir),
            v("w.slip_fade", i, W + 0x7C, w.slip_fade), v("w.force_a", i, W + 0x80, w.force_a), v("w.force_b", i, W + 0x84, w.force_b),
            v("w.fx_drive", i, W + 0x88, w.fx_drive), v("w.fx_brake", i, W + 0x8C, w.fx_brake), v("w.brake", i, W + 0x90, w.brake);
        v("w.flap_v", i, W + 0x9C, w.flap_v), v("w.wear", i, W + 0xA0, w.wear);
        auto& t = c.t[size_t(i)];
        const u32 T = kDrive0 + kDriveSize * u32(i);
        v("t.brake_torque", i, T + 0x00, t.brake_torque), v("t.react_drive", i, T + 0x04, t.react_drive),
            v("t.react_brake", i, T + 0x08, t.react_brake), v("t.torque", i, T + 0x0C, t.torque),
            v("t.slip_state", i, T + 0x10, t.slip_state), v("t.slip_sign", i, T + 0x12, t.slip_sign), v("t.long_frac", i, T + 0x14, t.long_frac),
            v("t.slip_angle", i, T + 0x20, t.slip_angle);
        const u32 k = 4 * u32(i);
        v("susp_x", i, kF + 0x3B4 + k, c.susp_x[size_t(i)]), v("body_force", i, kF + 0x3C4 + k, c.body_force[size_t(i)]);
    }
    // The aid blocks: aids[0] the car's own (P + 0xE7C), aids[1] its driver's (car + 0xEF8).
    for (int b = 0; b < 2; ++b) {
        auto& a = b ? c.ai.aids : c.p.aids;
        const u32 A = b ? 0xEF8 : kParams + 0xE7C;
        v("aids.a_gain", b, A + 0x00, a.a_gain), v("aids.a_thr", b, A + 0x04, a.a_thr), v("aids.d_gain", b, A + 0x08, a.d_gain);
        v("aids.c_gain", b, A + 0x10, a.c_gain), v("aids.c_thr", b, A + 0x14, a.c_thr), v("aids.e_gain", b, A + 0x18, a.e_gain);
        v("aids.a_wheels[0]", b, A + 0x0C, a.a_wheels[0]), v("aids.a_wheels[1]", b, A + 0x0D, a.a_wheels[1]);
        v("aids.d_wheels[0]", b, A + 0x0E, a.d_wheels[0]), v("aids.d_wheels[1]", b, A + 0x0F, a.d_wheels[1]);
        v("aids.c_wheels[0]", b, A + 0x1C, a.c_wheels[0]), v("aids.c_wheels[1]", b, A + 0x1D, a.c_wheels[1]);
        v("aids.e_wheels[0]", b, A + 0x1E, a.e_wheels[0]), v("aids.e_wheels[1]", b, A + 0x1F, a.e_wheels[1]);
        v("aids.tcs_gain", b, A + 0x20, a.tcs_gain), v("aids.tcs_lat_a", b, A + 0x24, a.tcs_lat_a);
        v("aids.tcs_lat_b", b, A + 0x28, a.tcs_lat_b), v("aids.tcs_scale", b, A + 0x2C, a.tcs_scale);
        v("aids.on[0]", b, A + 0x30, a.on[0]), v("aids.on[1]", b, A + 0x31, a.on[1]), v("aids.on[2]", b, A + 0x32, a.on[2]),
            v("aids.on[3]", b, A + 0x33, a.on[3]);
    }
    v("dt", -1, 0xEC0, c.dt);
    const u32 A = 0xEC8; // the AI driver object
    v("ai.mode", -1, A + 0x83, c.ai.mode), v("ai.steer", -1, A + 0x9C, c.ai.steer), v("ai.throttle", -1, A + 0xA0, c.ai.throttle),
        v("ai.brake", -1, A + 0xA4, c.ai.brake), v("ai.reverse", -1, A + 0xA8, c.ai.reverse), v("ai.shift_lock", -1, A + 0xAA, c.ai.shift_lock);
    v("ai.contact", -1, A + 0xB0, c.ai.contact);
    for (int k = 0; k < 8; ++k) v("ai.zone", k, A + 0xB4 + u32(k), c.ai.zone[size_t(k)]);
    v("reverse_req", -1, kF + 0x380, c.reverse_req), v("shift_req", -1, kF + 0x381, c.shift_req),
        v("throttle_state", -1, kF + 0x382, c.throttle_state), v("steer_class", -1, kF + 0x383, c.steer_class);
    for (int i = 0; i < 2; ++i) {
        const size_t j = size_t(i);
        v("drive_torque", i, kF + 0x384 + 4 * u32(i), c.drive_torque[j]), v("diff_lock", i, kF + 0x438 + u32(i), c.diff_lock[j]);
        v("turbo_lag", i, kF + 0x444 + 2 * u32(i), c.turbo_lag[j]), v("axle_speed", i, kF + 0x458 + 4 * u32(i), c.axle_speed[j]);
    }
    v("clutch_engine", -1, kF + 0x38C, c.clutch_engine), v("clutch_wheel", -1, kF + 0x390, c.clutch_wheel);
    vec("pos.x", "pos.y", "pos.z", -1, kF + 0x00, c.pos);
    vec("fwd.x", "fwd.y", "fwd.z", -1, kF + 0x0C, c.fwd), vec("lat.x", "lat.y", "lat.z", -1, kF + 0x18, c.lat);
    vec("up.x", "up.y", "up.z", -1, kF + 0x24, c.up);
    vec("ground_pos.x", "ground_pos.y", "ground_pos.z", -1, kF + 0x30, c.ground_pos);
    vec("ground_fwd.x", "ground_fwd.y", "ground_fwd.z", -1, kF + 0x3C, c.ground_fwd);
    vec("ground_lat.x", "ground_lat.y", "ground_lat.z", -1, kF + 0x48, c.ground_lat);
    vec("ground_up.x", "ground_up.y", "ground_up.z", -1, kF + 0x54, c.ground_up);
    v("inv_mass_eff", -1, kF + 0x394, c.inv_mass_eff), v("rolling", -1, kF + 0x398, c.rolling), v("terrain", -1, kF + 0x3A4, c.terrain);
    v("understeer", -1, kF + 0x39C, c.understeer), v("oversteer", -1, kF + 0x3A0, c.oversteer), v("control", -1, kF + 0x3E9, c.control);
    v("ghost", -1, kF + 0x3EA, c.ghost), v("wheel_type", -1, kF + 0x3EB, c.wheel_type);
    v("clutch_req", -1, kF + 0x418, c.clutch_req), v("x420", -1, kF + 0x420, c.x420), v("steer_rate", -1, kF + 0x424, c.steer_rate);
    v("gear", -1, kF + 0x436, c.gear), v("clutch", -1, kF + 0x437, c.clutch), v("shift_lockout", -1, kF + 0x43A, c.shift_lockout);
    v("rev_limiter", -1, kF + 0x43B, c.rev_limiter), v("turbo_load_prev", -1, kF + 0x43C, c.turbo_load_prev);
    v("turbo_load", -1, kF + 0x440, c.turbo_load), v("omega", -1, kF + 0x448, c.omega);
    v("centre_locked", -1, kF + 0x460, c.centre_locked), v("pedal_style", -1, kF + 0x461, c.pedal_style), v("antihunt", -1, kF + 0x462, c.antihunt);
    v("kickdown", -1, kF + 0x464, c.kickdown), v("kickdown_timer", -1, kF + 0x465, c.kickdown_timer);
    v("kickdown_count", -1, kF + 0x466, c.kickdown_count);
    v("road_seg", -1, kF + 0x3EC, c.road_seg), v("dist_total", -1, kF + 0x3F0, c.dist_total), v("odo_carry", -1, kF + 0x3F4, c.odo_carry);
    v("odometer", -1, kF + 0x3F8, c.odometer), v("engine_wear", -1, kF + 0x3FC, c.engine_wear), v("oil", -1, kF + 0x400, c.oil),
        v("dirt", -1, kF + 0x404, c.dirt);
    v("throttle_pedal", -1, kF + 0x428, c.throttle_pedal), v("brake_pedal", -1, kF + 0x42C, c.brake_pedal),
        v("handbrake", -1, kF + 0x430, c.handbrake);
    vec("vel.x", "vel.y", "vel.z", -1, kF + 0x44C, c.vel);
    v("lat_g", -1, kF + 0x474, c.lat_g), v("yaw_accel", -1, kF + 0x478, c.yaw_accel), v("yaw_rate", -1, kF + 0x47C, c.yaw_rate);
    v("pitch_rate", -1, kF + 0x480, c.pitch_rate), v("roll_rate", -1, kF + 0x484, c.roll_rate);
    v("speed", -1, kF + 0x488, c.speed), v("lat_speed", -1, kF + 0x48C, c.lat_speed), v("inv_dt", -1, kF + 0x4A0, c.inv_dt), v("drag", -1, kF + 0x4B8, c.drag);
    v("rpm", -1, kF + 0x490, c.rpm), v("countersteer", -1, kF + 0x49C, c.countersteer);
    v("throttle", -1, kF + 0x4A4, c.throttle), v("torque_wot", -1, kF + 0x4B0, c.torque_wot), v("torque_friction", -1, kF + 0x4B4, c.torque_friction);
    v("lock_lo", -1, kF + 0x4A8, c.lock_lo), v("lock_hi", -1, kF + 0x4AC, c.lock_hi);
    v("downforce[0]", -1, kF + 0x4BC, c.downforce[0]), v("downforce[1]", -1, kF + 0x4C0, c.downforce[1]);
    v("a_x", -1, kF + 0x4C4, c.a_x), v("a_y", -1, kF + 0x4C8, c.a_y), v("heave_acc", -1, kF + 0x4CC, c.heave_acc);
    v("x4d0", -1, kF + 0x4D0, c.x4d0), v("x4d4", -1, kF + 0x4D4, c.x4d4), v("drive_sum", -1, kF + 0x4D8, c.drive_sum);
    v("race_hold", -1, kF + 0x4DC, c.race_hold), v("bump_mask", -1, kF + 0x4DD, c.bump_mask), v("pit_box", -1, kF + 0x4DE, c.pit_box);
    v("finished", -1, kF + 0x4E0, c.finished), v("turbo_pitch", -1, kF + 0x4E2, c.turbo_pitch);
    v("x4e4", -1, kF + 0x4E4, c.x4e4), v("x4e5", -1, kF + 0x4E5, c.x4e5), v("turbo_spool", -1, kF + 0x4E6, c.turbo_spool);
    v("blowoff_state", -1, kF + 0x4E7, c.blowoff_state), v("blowoff", -1, kF + 0x4E8, c.blowoff), v("intake", -1, kF + 0x4EC, c.intake);
    v("hit_rumble", -1, kF + 0x4F0, c.hit_rumble), v("clutch_torque", -1, kF + 0x4F4, c.clutch_torque);
    v("lag_heading", -1, kF + 0x500, c.lag_heading), v("lag_heading_rate", -1, kF + 0x504, c.lag_heading_rate),
        v("lag_heading_acc", -1, kF + 0x508, c.lag_heading_acc), v("lag_pitch", -1, kF + 0x50C, c.lag_pitch),
        v("lag_pitch_rate", -1, kF + 0x510, c.lag_pitch_rate);
    v("ffb", -1, kF + 0x514, c.ffb), v("ffb_count", -1, kF + 0x518, c.ffb_count);
    v("x51d", -1, kF + 0x51D, c.x51d), v("ascc_status", -1, kF + 0x520, c.ascc_status), v("tcs_status", -1, kF + 0x521, c.tcs_status);
    v("full_throttle", -1, kF + 0x522, c.full_throttle), v("steer_pct", -1, kF + 0x523, c.steer_pct);
    v("time_scale", -1, kF + 0x524, c.time_scale), v("slip_target", -1, kF + 0x528, c.slip_target),
        v("slip_filter", -1, kF + 0x52C, c.slip_filter);
    v("driver_kind", -1, kF + 0x534, c.driver_kind), v("x535", -1, kF + 0x535, c.x535), v("hold", -1, kF + 0x536, c.hold);
    // The AI driver object's parameters, line and speed plan, recovery state and gear hint; the tyre records' peak slip angle.
    for (int r = 0; r < 6; ++r) v("p.tire.peak_slip", r, kParams + 0x398 + 0x1B4 * u32(r) + 0x94, p.tire[size_t(r)].peak_slip);
    {
        auto& d = c.ai;
        v("ai.brake_factor", -1, A + 0x0C, d.brake_factor), v("ai.speed_scale", -1, A + 0x10, d.speed_scale),
            v("ai.speed_offset", -1, A + 0x14, d.speed_offset), v("ai.yaw_gain", -1, A + 0x18, d.yaw_gain);
        v("ai.sat_throttle", -1, A + 0x1C, d.sat_throttle), v("ai.sat_brake", -1, A + 0x20, d.sat_brake),
            v("ai.look_speed", -1, A + 0x24, d.look_speed), v("ai.look_min", -1, A + 0x28, d.look_min),
            v("ai.look_max", -1, A + 0x2C, d.look_max);
        v("ai.front_weight", -1, A + 0x64, d.front_weight), v("ai.corner_bonus", -1, A + 0x68, d.corner_bonus),
            v("ai.steer_rate", -1, A + 0x6C, d.steer_rate), v("ai.pit_speed", -1, A + 0x70, d.pit_speed),
            v("ai.bonus_ticks", -1, A + 0x74, d.bonus_ticks), v("ai.throttle_scale", -1, A + 0x78, d.throttle_scale),
            v("ai.downforce", -1, A + 0x7C, d.downforce);
        v("ai.state", -1, A + 0x80, d.state), v("ai.line", -1, A + 0x81, d.line), v("ai.next", -1, A + 0x82, d.next),
            v("ai.next_kind", -1, A + 0x84, d.next_kind), v("ai.pit_limit", -1, A + 0x85, d.pit_limit),
            v("ai.prev_line", -1, A + 0x86, d.prev_line), v("ai.arc_ticks", -1, A + 0x88, d.arc_ticks);
        v("ai.v_cur", -1, A + 0x8C, d.v_cur), v("ai.v_next", -1, A + 0x90, d.v_next), v("ai.blend", -1, A + 0x94, d.blend);
        v("ai.trim", -1, A + 0x98, d.trim), v("ai.x99", -1, A + 0x99, d.x99), v("ai.cooldown", -1, A + 0x9A, d.cooldown);
        v("ai.xa9", -1, A + 0xA9, d.xa9), v("ai.gear_hint", -1, A + 0xAC, d.gear_hint), v("ai.gear_braking", -1, A + 0xAD, d.gear_braking), v("ai.stop", -1, A + 0xAE, d.stop),
            v("ai.status", -1, A + 0xAF, d.status);
    }
    v("index", -1, kF + 0x3E8, c.index), v("blip", -1, kF + 0x51F, c.blip);
    // The AI's speed plan: the top speed, the braking constants and grip.
    v("p.top_speed", -1, kParams + 0xDF8, p.top_speed), v("p.brake_coef", -1, kParams + 0xE34, p.brake_coef),
        v("p.weight", -1, kParams + 0xE50, p.weight);
    for (int a = 0; a < 2; ++a) v("p.cam_offset", a, kParams + 0xE0C + 4 * u32(a), p.cam_offset[size_t(a)]);
    for (int r = 0; r < 6; ++r) v("p.tire.brake_peak", r, kParams + 0x398 + 0x1B4 * u32(r) + 0x148, p.tire[size_t(r)].brake_peak);
    // What the race's start sets up besides the tick's fields.
    v("p.drive_class", -1, kParams + 0x31, p.drive_class), v("p.special", -1, kParams + 0x32, p.special), v("p.cog_height", -1, kParams + 0x3C, p.cog_height),
        v("p.xe58", -1, kParams + 0xE58, p.xe58);
    for (int a = 0; a < 2; ++a) v("p.ride_height", a, kParams + 0x40 + 4 * u32(a), p.ride_height[size_t(a)]);
    for (int i = 0; i < 4; ++i) {
        v("w.x29", i, kWheel0 + kWheelSize * u32(i) + 0x29, c.w[size_t(i)].x29);
        v("x408", i, kF + 0x408 + 4 * u32(i), c.x408[size_t(i)]);
    }
    v("sector", -1, kF + 0x492, c.sector), v("x493", -1, kF + 0x493, c.x493), v("x51c", -1, kF + 0x51C, c.x51c),
        v("x530", -1, kF + 0x530, c.x530);
    v("best_lap", -1, 0x16C4, c.best_lap), v("ai_hold", -1, 0x16CE, c.ai_hold), v("x16d0", -1, 0x16D0, c.x16d0),
        v("x16d1", -1, 0x16D1, c.x16d1), v("x16d4", -1, 0x16D4, c.x16d4);
    v("laps", -1, kF + 0x434, c.laps), v("lap_base", -1, kF + 0x467, c.lap_base), v("off_count", -1, kF + 0x4DF, c.off_count);
    v("rank", -1, kF + 0x4E1, c.rank), v("course_out", -1, kF + 0x51E, c.course_out), v("flow", -1, 0xEC4, c.flow);
    // The collision hook-up's (race.h).
    v("p.yaw_inertia", -1, kParams + 0x118, p.yaw_inertia);
    v("hit_kind", -1, 0x16CC, c.hit_kind), v("hit_part", -1, 0x16CD, c.hit_part);
}

// A record image (kCarSize bytes in GT3's layout) into the mirror and back: the bytes of every field of the table;
// store_image leaves the image's other bytes as they are.
void load_image(Car& c, const u8* image);
void store_image(const Car& c, u8* image);
// The first field (in table order) whose bits differ, "" when none: "w[2].susp 0x3F800000 vs 0x3F800001 (1 vs 1.0000001)".
std::string first_difference(const Car& ours, const Car& theirs);
// The fields at record offsets in [lo, hi) whose bits differ between `before` and `after`, copied from `after` into `dst`
// (what an unported call changed, applied to our record).
void copy_changed(Car& dst, const Car& before, const Car& after, u32 lo = 0, u32 hi = kCarSize);
// kCarSize entries: 1 where a field of the table covers the record byte.
const std::vector<u8>& covered_bytes();

} // namespace gt3r::sim::exact
