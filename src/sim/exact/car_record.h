#pragma once

// GT3's per-car record (0x16DC bytes; car = conductor + 0xC8 + 0x16DC i) as the vehicle tick's phases read and write it,
// in GT3's single floats (ee::F): the state of the reimplemented tick [physics §1.2-§1.3]. Fields are named by
// meaning where it is known (physics.md), otherwise by their offset until the phase that writes them is ported; the
// record's offsets are in the comments and in the field table (car_fields.h), which copies record images to and from the
// mirror and compares mirrors. Each block grows as its phases are ported: add the field here and its line to the field
// table together.
//
// Frames: the physics frame, Z up: (x, y) horizontal, z height [physics §1.3]; course points (the ground query records) are
// in the course frame (x, height, z).

#include "base/ee_float.h"
#include "sim/exact/program.h"

#include <array>

namespace gt3r::sim::exact {

using ee::F;

// The most cars a race holds. GT3's conductor keeps 8 places (its races use 6 at most); our own races (Quick race's custom
// race, mod events, online) take up to 32. Every table indexed by a car is this long; a race of 8 cars or fewer reads and
// writes only the places GT3 has, as GT3 does.
constexpr size_t kMaxCars = 32;
// The places GT3's conductor has (its tables' size in the program's memory).
constexpr size_t kGt3Cars = 8;

struct Vec3 {
    F x, y, z;
};

// A wheel (car + 0xFE4 + 0xA4 w; F + 0x60 + 0xA4 w).
struct Wheel {
    F ground0;              // +0x00 ground height under the hub (the ground query's, as +0x04)
    F ground;               // +0x04 ground height under the hub
    F load;                 // +0x08 tyre load Fz (N) [physics §4.2]
    F steer;                // +0x0C road-wheel steer angle (phase 4)
    F camber, half_track;   // +0x10 / +0x14 visual camber and hub half-track at the suspension travel (0x1E9348)
    F susp;                 // +0x18 suspension coordinate x [physics §4.2]
    F damper_v;             // +0x1C damper speed (unclamped)
    F rim_speed;            // +0x20 (m/s)
    u8 surface = 0;         // +0x24 surface index [physics §3.6]
    u8 x25 = 0;             // +0x25 the ground record's +0x32
    u8 surface_flags = 0;   // +0x26 the ground triangle's flags byte
    u8 x28 = 0;             // +0x28
    u8 x29 = 0;             // +0x29 (0xFF from the standing start)
    u8 smoke_count = 0;     // +0x2A steps the effects still run after the wheel left the ground (6 while loaded)
    F smoke_emit;           // +0x2C smoke / dust emission intensity [physics §8.3b]
    F slip_intensity;       // +0x30 slip intensity (skid sound, wear) [physics §8.3]
    F skid_pitch;           // +0x34 the tyre loops' pitch factor [physics §8.3c]
    F spin;                 // +0x38 wheel spin angle (drawn; 0x1EAFC0)
    Vec3 hub;               // +0x3C hub offset from the CoG, physics frame (body offsets turned by the attitude)
    F slip_gain;            // +0x48 slip gain g [physics §3.4]
    F hub_vx, hub_vy;       // +0x4C / +0x50 hub velocity, along / across the car [physics §10.1]
    F peak_force;           // +0x54 Fp [physics §3.1]
    F wear_grip;            // +0x58 grip factor of the wear [physics §8.4]
    u8 wear_byte = 0;       // +0x5C min(cap, slip intensity) x rate x 255 of the last wear step (no reader)
    s8 wear_level = 0;      // +0x5D the HUD's wear level (-0x80 .. 0x7F)
    u8 flags = 0;           // +0x5F bit 0 no load, bit 1 braked, bit 2 coupled through the centre clutch, bit 3 driven [physics §3.8]
    F bump;                 // +0x60 road-bump term under the hub
    F circle;               // +0x64 friction-circle radius D(slip ratio), a fraction of Fp
    F slip_ratio;           // +0x68 slip ratio, negative in wheelspin [physics §3.4]
    F speed_ramp;           // +0x6C min(1, 0.02 |hub speed|) [physics §8.3]
    F spring_f, damper_f;   // +0x70 / +0x74 spring and damper force [physics §4.2]
    F slip_dir;             // +0x78 velocity-direction angle phi = atan2(-vy, vx) in the body frame [physics §3.2] (phase 2)
    F slip_fade;            // +0x7C low-speed fade weight k (0 below 1 km/h, 1 above 10 km/h) (phase 2)
    F force_a, force_b;     // +0x80 / +0x84 slip-angle force: curve A (across the velocity), curve B (against it)
    F fx_drive, fx_brake;   // +0x88 / +0x8C longitudinal force along the wheel heading, drive side / brake side
    F brake;                // +0x90 brake pressure: the pedal (phase 3), then the aids' (phase 6) [physics §4.5]
    F brake_heat;           // +0x94 brake heat (phase 8) [physics §8.5]
    F flap;                 // +0x98 flap pendulum [physics §8.8]
    F flap_v;               // +0x9C flap pendulum velocity
    F wear;                 // +0xA0 tyre wear [physics §8.2]
};

// A wheel's ground query record (car + 0x14BC + 0x34 w): the hub's query point and the road under it [physics §1.3].
struct Ground {
    F x, h, z;        // +0x00 query point (course x, height, z); the query sets h to the ground height
    s32 seg = 0;      // +0x0C road segment of the ground triangle, -1 none (wheel 0's is the next query's hint)
    F height;         // +0x10 ground height, -FLT_MAX on a miss
    F attr_a, attr_b; // +0x14 / +0x18 the triangle's interpolated vertex scalars
    Vec3 normal;      // +0x1C unit normal of the ground plane (course frame, y up)
    F plane_d;        // +0x28 the plane: normal . p + plane_d = 0
    u16 attr_a_raw = 0, attr_b_raw = 0; // +0x2C / +0x2E the triangle's first vertex's scalars (x 65535)
    u8 miss = 0;      // +0x30 1: no road triangle under the point (the fields above keep their last values)
    u8 material = 0;  // +0x31 the triangle's material byte
    u8 x32 = 0;       // +0x32 cleared by the query
    u8 flags = 0;     // +0x33 the triangle's flags byte (bit 0 pit lane, bit 1 wet: the alternate surface)
};

// What phase 1 keeps of the last tick (car + 0x15C0 ..), for finite differences.
struct Previous {
    F pitch, roll, heading; // +0x15C0 (car +0x13EC ..)
    F ground_pitch, ground_roll; // +0x15CC (car +0x1418 ..)
    std::array<F, 4> susp, steer, camber, half_track, spin, brake_heat, attr_a, attr_b; // +0x15D4, +0x15E4, ... +0x1644
    std::array<Vec3, 4> hub;                                                       // +0x1654 (query x, wheel ground, query z)
    std::array<F, 4> flap;                                                         // +0x1684
    F steer_view;                                                                  // +0x1694 car +0x15A4
    F lag_heading_diff, lag_pitch_diff, f3e4;                                      // +0x1698 / +0x169C / +0x16A0 (F +0x4F8, ...)
};

// A piecewise-linear table as the program keeps it, {s16 n; float* x; float* y}, with the arrays it points at (up to N
// points); looked up by 0x1D8FB8 (lookup()).
template <size_t N> struct Table {
    s16 n = 0;
    std::array<F, N> x, y;
};

// A tyre record of the parameter block (P + 0x398 + 0x1B4 r; r 0..2 the front axle's tyre classes, 3..5 the rear's)
// [physics §6.5].
struct TireRecord {
    F grip;                    // +0x00 base grip: 9.8 x curve D at zero slip; 9.8 for every compound but medium_evo's, 9.8 x
                               // 1.02 (test params_tire_base_grip_disc)
    F wear_rate;               // +0x04 the compound's wear rate [physics §8.2]
    std::array<F, 16> mu;      // +0x08 grip per surface; mu[11] is the wear grip's floor
    Table<8> curve_a;          // +0x48 (x +0x54, y +0x74) across-velocity force fraction vs folded slip angle
    F peak_slip;               // +0x94 the slip angle where curve A peaks, at most 45 degrees (the AI's steering limit)
    Table<6> curve_b;          // +0x98 (x +0xA4, y +0xBC) along-velocity force fraction vs folded slip angle
    Table<12> curve_c;         // +0xD4 (x +0xE0, y +0x110) longitudinal force fraction vs slip ratio
    F slip_drive;              // +0x140 optimum drive slip (the TCS target)
    F slip_brake;              // +0x144 optimum braking slip (the ABS target)
    F brake_peak;              // +0x148 curve C's maximum on the braking side (the AI's braking grip) [ai §4.3]
    std::array<F, 12> curve_d; // +0x158 friction-circle radius vs slip ratio (table +0x14C, on curve C's axis)
    Table<4> load_curve;       // +0x188 (x +0x194, y +0x1A4) load sensitivity L(Fz / width) [physics §3.1]
};

// A driving-aid block [physics §4.6]: the car's own (P + 0xE7C, from its spec) or its driver's (car + 0xEF8). The ASCC
// channels: off the brake pedal A brakes the outer wheels against oversteer and C the inner ones against understeer; while
// braking D and E release the inner / outer brakes.
struct Aids {
    F a_gain, a_thr, d_gain;                     // +0x00 / +0x04 / +0x08
    std::array<u8, 2> a_wheels{}, d_wheels{};    // +0x0C / +0x0E the channel acts on the front / rear wheel
    F c_gain, c_thr, e_gain;                     // +0x10 / +0x14 / +0x18
    std::array<u8, 2> c_wheels{}, e_wheels{};    // +0x1C / +0x1E
    F tcs_gain, tcs_lat_a, tcs_lat_b, tcs_scale; // +0x20 .. +0x2C the TCS (gain 0: off)
    std::array<u8, 4> on{};                      // +0x30 .. +0x33 channel enables A, D, C, E
};

// The engine block E (P + 0x138 = car + 0x148) [physics §2.1-§2.3].
struct Engine {
    Table<16> torque;                      // +0x00 (car + 0x148; x +0x0C, y +0x4C) full-throttle torque vs engine speed [physics §2.1]
    u16 rev_limit = 0;                     // +0x8C rpm
    u16 idle = 0;                          // +0x8E idle rpm
    F friction;                            // +0x90 (car + 0x1D8) friction torque per rad/s
    std::array<s16, 2> turbo_knee{};       // +0x94 per stage: knee N (rpm)
    std::array<u8, 2> turbo_shape{};       // +0x98 per stage: shape type below the knee
    u8 power_cap_on = 0;                   // +0x9B power-restricted event
    F power_cap;                           // +0x9C power limit (W)
    F power_cap_soft;                      // +0xA0 soft-limit band (fraction of the limit)
    std::array<F, 2> turbo_peak;           // +0xA4 per stage: peak A (0 = no stage)
    std::array<F, 2> turbo_rate;           // +0xAC per stage: response rate R (/s)
    std::array<F, 2> turbo_coef;           // +0xB4 per stage: the shape's square-term coefficient
    F boost_max;                           // +0xBC boost cap - 1 (0 = no turbo)
};

// The driveline block (P + 0x1F8 = car + 0x208) [physics §2.4-§2.7].
struct Driveline {
    u8 layout = 0;                         // +0x00 layout code L (also the TCS's wheels, physics §4.6)
    u8 unequal = 0;                        // +0x01 the later builds': layout 5 with axles of unequal tyre radii; GT3: unused
    u8 kind = 0;                           // +0x12 the later builds' gearbox kind (spec +0xE1): 1 / 2 without steps, 3 one
                                           // gear; GT3: unused
    u8 gears = 0;                          // +0x02 forward gears N
    u8 driven_axle = 0;                    // +0x03 0 front, 1 rear
    F awd_k;                               // +0x04 coupling k
    F awd_capacity;                        // +0x08 centre-clutch capacity (N m)
    F engine_inertia;                      // +0x0C J_e
    std::array<u8, 2> diff_type{};         // +0x10 per axle, index in "frnmhvyt"
    std::array<F, 2> diff_initial;         // +0x14 per axle: initial torque (viscous: cap)
    std::array<F, 2> diff_accel;           // +0x1C per axle: accel ratio (viscous: gain)
    std::array<F, 2> diff_decel;           // +0x24 per axle: decel ratio
    u16 launch_rpm = 0;                    // +0x2C
    u16 up_rpm = 0;                        // +0x2E automatic up-shift rpm
    std::array<u16, 6> down_rpm{};         // +0x30 automatic down-shift rpm, gear g = 2.. at g - 2
    std::array<F, 8> ratio;                // +0x3C total ratio per gear (0 = reverse, negative)
    std::array<F, 8> gear_speed;           // +0x5C engine rad/s per m/s per gear
    F shift_clutch_torque;                 // +0x9C transmitted while shifting (spec 0xCD x 9.8, always 0)
    std::array<F, 2> wheel_gain;           // +0xA0 per axle: r / Ia
    std::array<F, 2> axle_inertia;         // +0xA8 J_front, J_rear
    F inv_axle_inertia;                    // +0xB0 1 / (J_front + J_rear)
    F inv_engine_inertia;                  // +0xB4 1 / J_e
};

// The AI driver object (car + 0xEC8; D below) [ai §1]: its parameters (from the driver parameter set and the event's overrides,
// set when the car is made [ai §9]), the AI line and speed plan, the recovery state, the outputs 0x1F79F0 applies to the
// controls, what the proximity phase writes for it. D +0x00 / +0x04 / +0x08 point at the conductor, the car and the course's AI
// lines.
struct Driver {
    F brake_factor;          // +0x0C k_brake: the braking distance's factor (100 / brake percent) [ai §4.3]
    F speed_scale;           // +0x10 the corner speeds' scale [ai §4.2]
    F speed_offset;          // +0x14 the corner speeds' offset (m/s)
    F yaw_gain;              // +0x18 K_y: the wanted slip angle per rad/s of yaw-rate shortfall [ai §6]
    F sat_throttle;          // +0x1C K_t: the throttle cut per unit of steering saturation [ai §5]
    F sat_brake;             // +0x20 K_b: the brake per unit of steering saturation
    F look_speed;            // +0x24 1 / v_L: the forward speed's share of the full look-ahead [ai §3]
    F look_min, look_max;    // +0x28 / +0x2C the look-ahead L0 at rest, L1 at v_L and beyond (m)
    Aids aids;               // +0x30 (car + 0xEF8) the driver's aid block [physics §4.6]
    F front_weight;          // +0x64 w: the front axle's share of the corner speeds' grip [ai §4.2]
    F corner_bonus;          // +0x68 added to an arc's target once the car has been in the arc bonus_ticks ticks (m/s) [ai §5]
    F steer_rate;            // +0x6C the steering command's slew rate (rad/s) [ai §6]
    F pit_speed;             // +0x70 the pit lane's speed (m/s) [ai §4.4]
    s32 bonus_ticks = 0;     // +0x74
    F throttle_scale;        // +0x78 the event's throttle multiplier [ai §9.2]
    F downforce;             // +0x7C k: the downforce term of the corner speeds, the lift coefficients over the mass [ai §4.2]
    u8 state = 0;            // +0x80 recovery state: 0 racing, 1 reversing out, 2 turning round [ai §7]
    u8 line = 0;             // +0x81 the game line driven (1..7) [ai §2.1]
    u8 next = 0;             // +0x82 the next control point of the line (count: past the last) [ai §4.1]
    u8 mode = 0;             // +0x83 the segment mode: 0 straight, 1 / 2 arc (the throttle moves by at most dt per tick
                             // towards the request), 3 / 4 braking from a straight / in an arc [ai §5]
    u8 next_kind = 0;        // +0x84 the next segment's kind (0 straight, 1 arc)
    u8 pit_limit = 0;        // +0x85 1 while held at the pit speed (the stop line, the pit lane's limit section)
    u8 prev_line = 0;        // +0x86 the line before the last change (the blend's old line) [ai §3]
    u16 arc_ticks = 0;       // +0x88 ticks in the arc (at most 6000)
    F v_cur, v_next;         // +0x8C / +0x90 the current and the next segment's target speed (m/s) [ai §4.1]
    F blend;                 // +0x94 the line blend's weight w: 1 after a change, down by dt per tick [ai §3]
    s8 trim = 0;             // +0x98 the corner speeds' trim in steps of 10 km/h (debug) [ai §4.2]
    u8 x99 = 0;              // +0x99
    s16 cooldown = 0;        // +0x9A lane-change cooldown ticks [ai §14.1]
    F steer;                 // +0x9C the road-wheel steering command (rad)
    F throttle, brake;       // +0xA0 / +0xA4
    u8 reverse = 0;          // +0xA8 reverse request
    u8 xa9 = 0;              // +0xA9 (cleared with the reverse request)
    s16 shift_lock = 0;      // +0xAA gearbox lock-out ticks (its low byte goes to F +0x43A when not 0)
    s8 gear_hint = 0;        // +0xAC the gear for the speed the plan aims at (-1 none; the HUD's shift indicator) [ai §16]
    u8 gear_braking = 0;     // +0xAD 1 while braking to that speed (modes 3 / 4)
    s8 stop = 0;             // +0xAE the later builds' stop state: 1 heading for the stop point, 2 standing on it; GT3: unused
    s8 status = 0;           // +0xAF 0, 1 approaching a braking point, 2 braking, 3 / 4 an arc at full throttle in mode 2 / 1,
                             // 5 an arc at part throttle [ai §5]
    F contact;               // +0xB0 contact intensity c for the AI [ai §8.1] (phase 11)
    std::array<u8, 8> zone{}; // +0xB4 the AI's zone cells around the car (phase 11)
};

// An axle's suspension block (P + 0x2B8 + 0x44 axle) [physics §4.2, §10.5].
struct Suspension {
    F knee;                  // +0x00 below it the progressive rate k2 adds
    F bump_limit;            // +0x04 the bump stop: x is clamped to it
    F droop;                 // +0x08 maximum droop: beyond it the wheel is in the air
    F offset;                // +0x0C added to the hub's height above the ground less the tyre radius
    F x_static;              // +0x10 static position x_s
    F k;                     // +0x14 spring rate
    F bump_t, bump_o, bump_g; // +0x18 / +0x1C / +0x20 damper fit, compression: threshold, offset, gain
    F reb_t, reb_o, reb_g;   // +0x24 / +0x28 / +0x2C damper fit, extension
    F anti_roll;             // +0x30 anti-roll gain
    F k2;                    // +0x34 progressive rate
    F preload;               // +0x3C unsprung weight
    F bump_damping;          // +0x40 extra damping below the knee
};

// The parameter block P (car + 0x10): the car's constants, built from its spec when the car is made.
struct Params {
    F body_front, body_rear, body_width, body_height; // +0x00 .. +0x0C the body: ahead of and behind the CoG, width, height
    F dist_front, dist_rear;      // +0x10 / +0x14 CoG to the front / rear axle (a, b)
    F wheelbase;                  // +0x18
    F track;                      // +0x1C mean track
    F rear_weight;                // +0x20 1 - the front weight fraction
    std::array<F, 2> half_track;  // +0x28 per axle
    u8 special = 0;               // +0x32 the later builds' special car kind (spec +0x11; special.h); GT3: 0
    u8 drive_class = 0;           // +0x31 the AI's driveline class: 0 FR / MR / RR, 1 FF and 4WD system 0, 2 other 4WD, 3 4WD
                                  // system 3 [ai §9.1]
    F lat_transfer, long_transfer; // +0x34 / +0x38 load transfer gains [physics §4.2]
    F cog_height;                 // +0x3C the CoG's static height above the ground [physics §9.2]
    std::array<F, 2> ride_height; // +0x40 / +0x44 the front / rear axle's ride height: both the front setting's (spec
                                  // 0x5C), so their difference, which pitches the car at the start, is 0 [physics §4.2]
    F mass, inv_mass;             // +0x48 / +0x4C
    F inv_sprung_mass;            // +0x50
    F x54;                        // +0x54 the drawn car's offset behind the CoG, along the forward axis
    F max_lock;                   // +0x58 full steering lock (rad)
    Table<6> steer_b, steer_a, steer_c; // +0x5C / +0x98 / +0xD4 (x +0xC, y +0x24) lock (rad) vs forward speed: pad, steering
                                        // wheel, dirt
    std::array<F, 2> steer_ratio; // +0x110 front wheel i's share of the steering command when it is >= 0 (wheel 1 - i's when < 0)
    F yaw_inertia;                // +0x118 the yaw inertia I_z (the collision body's, 0x1FA740)
    F inv_yaw_inertia;            // +0x11C 1 / P+0x118
    std::array<F, 2> pitch_lever; // +0x120 per axle: a 2 pi / Y, b 2 pi / Y [physics §4.3]
    std::array<F, 2> roll_lever;  // +0x128 per axle: half track 2 pi / Z
    std::array<F, 2> pitch_tire;  // +0x130 per axle: the longitudinal force's pitch gain kP
    Engine e;                     // +0x138
    Driveline d;                  // +0x1F8
    std::array<F, 2> tire_radius; // +0x274 per axle
    std::array<F, 2> inv_tire_radius; // +0x27C per axle
    std::array<F, 2> width;       // +0x284 per axle: tyre width
    std::array<F, 2> wheel_mass;  // +0x2B0 per axle: a free wheel's extra mass in the forward step, Ia / r^2
    std::array<Suspension, 2> susp; // +0x2B8 per axle, 0x44 each
    std::array<Table<4>, 2> camber_grip; // +0x340 + 0xC axle (x +0x358 + 0x10 axle, y +0x378 + 0x10 axle) camber angle -> grip factor
    std::array<TireRecord, 6> tire;               // +0x398 + 0x1B4 r
    std::array<std::array<u8, 16>, 2> tire_index{}; // +0xDD0 + 16 axle: the axle's tyre record per surface
    std::array<F, 2> axle_grip;   // +0xDF0 per axle: grip factor [physics §3.1]
    F top_speed;                  // +0xDF8 the spec converter's top speed (m/s; the AI's straight target) [ai §4.2]
    std::array<F, 2> camber;      // +0xDFC per axle: camber offset (rad; the grip's camber and the visual one)
    std::array<F, 2> camber_gain; // +0xE04 per axle: camber change per metre of travel (visual)
    std::array<F, 2> cam_offset;  // +0xE0C per axle: the camber grip table at the camber offset (the AI's braking grip)
    std::array<F, 2> swing_arm;   // +0xE14 per axle: swing-arm length (visual)
    F steer_camber;               // +0xE1C steer-coupled camber
    std::array<F, 2> toe;         // +0xE20 per axle: subtracted on the left wheel, added on the right
    std::array<F, 2> brake_torque;     // +0xE28 per axle at full pressure (N m)
    F handbrake_torque;                // +0xE30 rear wheels
    F brake_coef;                      // +0xE34 C_car: mass x front tyre radius / (4 x front brake torque), the AI braking
                                       // distance's least coefficient [ai §4.3]
    std::array<F, 2> abs_gain;         // +0xE38 per axle: the ABS slope G (brake balance x 0.1) [physics §4.5]
    std::array<F, 2> brake_slip_scale; // +0xE40 per axle: scales the tyres' optimum braking slip (the ABS target)
    std::array<F, 2> abs_peak_scale;   // +0xE48 per axle: E, scales the peak tyre force
    F weight;                          // +0xE50 mass x g (N)
    F xe54;                            // +0xE54 the steering force's lateral force scale
    F xe58;                            // +0xE58 the rear axle's static sprung load (+0xE54 the front's: each wheel's start
                                       // load is half its axle's plus the unsprung weight) [physics §9.3]
    F drag_coef;                       // +0xE60 aerodynamic drag coefficient [physics §5.2]
    std::array<F, 2> downforce_coef;   // +0xE64 front / rear downforce coefficients
    std::array<F, 2> wear_load_offset; // +0xE6C per axle: the wear load term's offset (N)
    std::array<F, 2> wear_load_gain;   // +0xE74 per axle: its gain on the tyre load (1 / m)
    Aids aids;                         // +0xE7C (car + 0xE8C) the car's own aids
};

// A wheel's slip / driveline record T_i (car + 0x1274 + 0x24 w).
struct Drive {
    F brake_torque;             // +0x00 brake torque (pedal and handbrake)
    F react_drive, react_brake; // +0x04 / +0x08 the longitudinal force times the slip gain, drive / brake side (wheel spin)
    F torque;                   // +0x0C wheel net torque [physics §2.7]
    s8 slip_state = 0;          // +0x10 st: +1 wheelspin branch, -1 braking or stopped [physics §3.4]
    s8 slip_sign = 0;           // +0x12 sg: the longitudinal force's sign
    F long_frac;                // +0x14 C(slip ratio), the longitudinal force fraction of Fp
    F steer_sin, steer_cos;     // +0x18 / +0x1C sine and cosine of the wheel's steer angle (phase 4)
    F slip_angle;               // +0x20 beta: the steer angle less the velocity direction, in [-pi, pi)
};

// The controller settings' gearbox bytes (static data at 0x2D4628 that no code writes; the defaults are the shipped bytes):
// the automatic box's options [physics §2.6].
struct GearSettings {
    u8 mode = 1;          // +0x14 1: a held shift input holds the automatic gear; 2: it shifts (with the anti-hunt counter)
    u8 down_throttle = 5; // +0x15 B3: down-shift rpm - 100 B3 with throttle > 0.25
    u8 down_lift = 0;     // +0x16 B1: + 100 B1 with throttle < 0.05
    u8 down_brake = 0;    // +0x17 B2: + 100 B2 with brake > 0.8
};

// An electric mod car's drive, which GT3's step does not have: the motor turns with the driven wheels through the
// single reduction, so the clutch stays locked (no slip, no launch) and the car never shifts, changing direction only
// near standstill (tick_gear.cpp), and braking as with the accelerator up while a change waits; the motor's curve starts
// at standstill; the top speed (the rev limit) fades the drive out instead of cutting it; a lifted accelerator brakes
// with the motor (regen, eased off as the driven tyres slip), which the friction brakes take over near standstill to
// stop and hold the car (tick_engine.cpp).
struct ElectricDrive {
    bool on = false;
    F regen_torque; // the lift-off braking torque at the motor (N m)
    F regen_power;  // its power limit (W), which caps it at speed
    u8 waiting = 0; // 1: the last gear step refused a change of direction (at 5 km/h or more)
};

// The car record. Members by record offset; F = car + 0xF84.
struct Car {
    Params p;               // +0x10
    F dt;                   // +0xEC0 the car's step length dt_car
    F flow;                 // +0xEC4 the race-flow accumulator (race_flow_gate)
    Driver ai;              // +0xEC8 the AI driver object
    Vec3 pos;               // F +0x00 CoG position (z = height)
    Vec3 fwd, lat, up;      // F +0x0C / +0x18 / +0x24 body axes from the attitude (0x1D8A80) [physics §1.3]
    Vec3 ground_pos;        // F +0x30 the ground under the CoG (x, y, height)
    Vec3 ground_fwd, ground_lat, ground_up; // F +0x3C / +0x48 / +0x54 ground frame axes
    std::array<Wheel, 4> w; // F +0x60
    std::array<Drive, 4> t; // F +0x2F0 (car +0x1274)
    s8 reverse_req = 0;     // F +0x380 (car +0x1304, O[0]) reverse request
    s8 shift_req = 0;       // F +0x381 (O[1]) shift request: +1 up, -1 down
    u8 throttle_state = 0;  // F +0x382 (O[2]) digital throttle: 0 idle, 1 rising, 2 rising from the back axis (reverse)
    u8 steer_class = 0;     // F +0x383 (O[3]) steering rate class: 1 digital or DualShock, 2 table-mapped analog, 0 wheel
    std::array<F, 2> drive_torque; // F +0x384 per axle
    F clutch_engine, clutch_wheel; // F +0x38C / +0x390 engine and wheel-side speeds at the clutch check (slipping clutch)
    F inv_mass_eff;         // F +0x394 the forward step's 1 / (mass + the free wheels' extra mass)
    F rolling;              // F +0x398 rolling-resistance force
    F understeer, oversteer; // F +0x39C / +0x3A0 the steering indicators (phase 6) [physics §4.6]
    F terrain;              // F +0x3A4 terrain factor s = ground up z / body up z
    std::array<F, 4> susp_x;     // F +0x3B4 suspension coordinate x per wheel
    std::array<F, 4> body_force; // F +0x3C4 per-wheel body force (spring, anti-roll, load transfer, aero)
    F f3e4;                 // F +0x3E4
    u8 index = 0;           // F +0x3E8 the car's index in the race (its collision body)
    s8 control = 0;         // F +0x3E9 (car +0x136D) control source: 0 pad, 1 the AI drives with the pad decoded for display,
                            // 2 the AI [ai §1]
    u8 ghost = 0;           // F +0x3EA (car +0x136E) not in the race (a ghost)
    s8 wheel_type = 0;      // F +0x3EB (car +0x136F) steering-wheel type
    s32 road_seg = 0;       // F +0x3EC wheel 0's road segment (the collision step's hint)
    F dist_total, odo_carry; // F +0x3F0 distance driven (wraps at 8000 km) / F +0x3F4 the odometer's carry
    u32 odometer = 0;       // F +0x3F8 (car +0x137C) engine condition word 0: distance in 2 m units [physics §10.3]
    F engine_wear, oil, dirt; // F +0x3FC / +0x400 / +0x404 (car +0x1380 ..) engine condition words 1..3 (E, O, the car's dirt)
    std::array<u32, 4> x408{}; // F +0x408 .. +0x414 engine condition words 4..7 (the race entry's, carried)
    F clutch_req;           // F +0x418 clutch request
    F steer_cmd;            // F +0x41C the steering command (rad), before each wheel's ratio and toe
    F x420;                 // F +0x420 (1 after every pad frame)
    F steer_rate;           // F +0x424 the steering follower's rate (rad/s)
    F throttle_pedal, brake_pedal, handbrake; // F +0x428 / +0x42C / +0x430 the controls, 0..1 (phase 3)
    s16 laps = 0;           // F +0x434 laps counted by the race flow (0x1EE978); the gap's laps are laps - lap_base. The start
                            // gives 0 to a car behind the line, else 1 [physics §9.2]
    u8 gear = 0;            // F +0x436 0 reverse, 1..N
    u8 clutch = 0;          // F +0x437 clutch link state S: 0 open, 1 locked, 2 slipping, 3 shift [physics §2.4]
    std::array<u8, 2> diff_lock{}; // F +0x438 per axle: clutch-pack LSD locked
    u8 shift_lockout = 0;   // F +0x43A
    u8 rev_limiter = 0;     // F +0x43B fuel cut
    F turbo_load_prev, turbo_load; // F +0x43C / +0x440
    std::array<s16, 2> turbo_lag{}; // F +0x444 per stage: lagged rpm
    F omega;                // F +0x448 engine speed (rad/s)
    Vec3 vel;               // F +0x44C world velocity
    std::array<F, 2> axle_speed; // F +0x458 front, rear (rad/s)
    u8 centre_locked = 0;   // F +0x460 L4-6 centre clutch engaged
    u8 pedal_style = 0;     // F +0x461 race entry pedal style: 0-2 the back axis replaces a smaller throttle, 3 the brake axis
                            // is a clutch, 5 no back axis
    u16 antihunt = 0;       // F +0x462
    u8 kickdown = 0, kickdown_timer = 0, kickdown_count = 0; // F +0x464 / +0x465 / +0x466
    u8 lap_base = 0;        // F +0x467 subtracted from the lap count (0x1ED018): 1 from a pit-box start past half the lap
    F pitch, roll, heading; // F +0x468 / +0x46C / +0x470 (car +0x13EC ..) the body attitude
    F lat_g;                // F +0x474 lateral acceleration over the four wheels' grip ("lateral g / mu", signed)
    F yaw_accel, yaw_rate;  // F +0x478 / +0x47C
    F pitch_rate, roll_rate; // F +0x480 / +0x484
    F speed, lat_speed;     // F +0x488 / +0x48C forward and lateral speed
    s16 rpm = 0;            // F +0x490
    u8 sector = 0;          // F +0x492 the sector the car is in (the first whose start is ahead of it)
    u8 x493 = 0;            // F +0x493
    F ground_pitch, ground_roll; // F +0x494 / +0x498 (car +0x1418 ..) the ground's pitch and roll under the car
    F countersteer;         // F +0x49C the slip-angle extension of the lock, then the steering command as a fraction of it (0..1)
    F inv_dt;               // F +0x4A0 1 / dt_car
    F throttle;             // F +0x4A4 the throttle the engine takes (phase 3; the TCS scales it)
    F lock_lo, lock_hi;     // F +0x4A8 / +0x4AC the steering command's bounds (rad)
    F torque_wot;           // F +0x4B0 full-throttle torque T_wot [physics §2.1]
    F torque_friction;      // F +0x4B4 (phase 2)
    F drag;                 // F +0x4B8 aerodynamic drag force (longitudinal)
    std::array<F, 2> downforce; // F +0x4BC / +0x4C0 aero downforce front / rear
    F a_x, a_y;             // F +0x4C4 / +0x4C8 longitudinal and lateral acceleration
    F heave_acc;            // F +0x4CC heave acceleration less gravity along the up axis
    F x4d0, x4d4;           // F +0x4D0 / +0x4D4 external longitudinal / lateral force (cleared every tick)
    F drive_sum;            // F +0x4D8 the longitudinal force sum (the load transfer reads it)
    u8 race_hold = 0;       // F +0x4DC set while the race's start sequence holds the cars
    u8 bump_mask = 0;       // F +0x4DD wheels beyond their bump stop: high nibble at the phase's start, low now
    s8 pit_box = 0;         // F +0x4DE the pit box: its point on the stop line (game line 4), the count - (index + 4)
    u8 off_count = 0;       // F +0x4DF course-out ticks (the conductor's virtual 40; 0xFF: at once)
    u8 finished = 0;        // F +0x4E0 the car's race has ended
    s8 rank = 0;            // F +0x4E1 (car +0x1465) the race position the ranking (conductor virtual 3) gives the car, 1 first
    u16 turbo_pitch = 0;    // F +0x4E2 spool pitch: the larger lag
    u8 x4e4 = 0, x4e5 = 0;  // F +0x4E4 / +0x4E5 engine sound levels
    u8 turbo_spool = 0;     // F +0x4E6 spool level
    u8 blowoff_state = 0;   // F +0x4E7 0 armed, 1 fired
    F blowoff;              // F +0x4E8 blow-off strength (this tick)
    F intake;               // F +0x4EC intake smoothing (sound)
    F hit_rumble;           // F +0x4F0 the last wall hit's strength (collision step), for the rumble
    F clutch_torque;        // F +0x4F4 |transmitted torque|
    F lag_heading_diff, lag_pitch_diff; // F +0x4F8 / +0x4FC heading and pitch less their lagging copies (0x1E0768)
    F lag_heading, lag_heading_rate, lag_heading_acc; // F +0x500 / +0x504 / +0x508 a lagging copy of the heading
    F lag_pitch, lag_pitch_rate; // F +0x50C / +0x510 a lagging copy of the pitch
    F ffb;                  // F +0x514 the steering force before its output scale (mode 3: the held peak)
    s32 ffb_count = 0;      // F +0x518 mode 3: steps since the peak
    u8 x51c = 0;            // F +0x51C
    u8 x51d = 0;            // F +0x51D a countdown, the HUD message timer (phase 2 steps it towards 0; 0xFF stays)
    u8 course_out = 0;      // F +0x51E the course-out flag of time trials and licences: 2 off the course (virtual 40), bit 0 a
                            // hard wall hit (virtual 41) [physics §11]
    u8 blip = 0;            // F +0x51F the start hold's throttle blip: 1 rising, 0 falling [ai §10]
    u8 ascc_status = 0;     // F +0x520 0, 1 ASCC on, 2 acting (phase 6)
    u8 tcs_status = 0;      // F +0x521 0 off, 1 on, 2 limiting (phase 6)
    u8 full_throttle = 0;   // F +0x522 1 unless the pad's throttle is released (digital) or short of full (analog)
    s8 steer_pct = 0;       // F +0x523 the steering in percent of full lock (HUD)
    F time_scale;           // F +0x524 the car's time scale (dt = time_scale / 60)
    F slip_target;          // F +0x528 the slipstream target x: the factors behind every other car, summed, at most 1 (phase 11)
    F slip_filter;          // F +0x52C its filtered value y (phase 2) [physics §5.2]
    u32 x530 = 0;           // F +0x530
    u8 driver_kind = 0;     // F +0x534 0 normal, 1 the AI drives, 2.. pit lane / stop line / leaving, 8 the stop driver's car
                            // at rest (held) [physics §8.6, §9.4, ai §1]
    u8 x535 = 0;            // F +0x535 flags: bit 2 tyres worn, bit 3 pit request [physics §8.7], bit 4 on the pit-lane
                            // surface [physics §8.6]
    u16 hold = 0;           // F +0x536 spawn / stop hold counter [physics §4.3]
    std::array<Ground, 4> g; // +0x14BC
    Vec3 draw_pos, draw_ground; // +0x158C / +0x1598 body and ground points behind the CoG by P +0x54 (course frame)
    F steer_view;           // +0x15A4 clamp(0.5 - 0.5 steering command / full lock, 0, 1)
    Vec3 draw_pos_prev, draw_ground_prev; // +0x15A8 / +0x15B4 last tick's +0x158C / +0x1598
    Previous prev;          // +0x15C0
    s16 wheel_pos = 0, wheel_pos_prev = 0; // +0x16A4 / +0x16A6 steering-wheel position (the pad frame's steer), this and the
                                           // last frame (phase 3)
    F speed_abs, max_speed; // +0x16A8 |speed| / +0x16AC its maximum while racing
    u32 best_lap = 0;       // +0x16C4 the best lap time (ms; 0x157529FF none, from the race's start)
    u32 feedback = 0;       // +0x16C8 the pad feedback word, cleared every tick: the strongest hit of the collision step
                            // (float bits, 0x1DFD98)
    u8 hit_kind = 0;        // +0x16CC that hit's kind (0 a wall, 1 a car)
    u8 hit_part = 0;        // +0x16CD and the car's part it struck (0x1DFEB8's argument)
    u16 ai_hold = 0;        // +0x16CE steps a player's car is driven by the AI after the start (rolling start 300) [physics §9.4]
    u8 x16d0 = 0;           // +0x16D0 1 unless the gearbox part's kind is 0x13 (0x234EC0)
    u8 x16d1 = 0;           // +0x16D1 (1 from the race's start)
    u8 x16d4 = 0;           // +0x16D4
    u8 rumble = 0;          // +0x16D5 the pad's vibration level (0..127)
    u8 steer_force = 0;     // +0x16D6 the steering wheel's force (0x80 centred)
    F speedo;               // +0x16D8 the speedometer's value
    // The later builds' own words of the record (Gran Turismo Concept [physics §14.5]): not in GT3's record or the field
    // table, zero on GT3's discs.
    struct Later {
        F ratio;                     // their record's +0x1328: a float the moving start sets to 1
        std::array<u8, 8> special{}; // their record's +0x149C .. +0x14A3: a special car's state (special.h; layout 7's: hybrid.h)
    } later;
    // Not in the record: how the race behaves where the releases' routines differ (sim/program.h: whose rules, the step
    // rate). The caller sets it from ProgramData::behaviour() (start_cars does; load_image keeps it).
    Behaviour behaviour;
    // Not in the record either: an electric mod car's drive (GT3 has no electric cars). The caller sets it from the
    // database (electric_drive, start.h; load_image keeps it); off, the car is GT3's.
    ElectricDrive ev;
};

// The conductor's per-pair state the proximity phase writes (conductor + 0xB7B4 / + 0xB8B4), [car][other car].
struct Proximity {
    std::array<std::array<F, kMaxCars>, kMaxCars> slip;  // +0xB7B4: car's slipstream factor behind the other car [physics §5.2]
    std::array<std::array<u8, kMaxCars>, kMaxCars> code{}; // +0xB8B4: the other car's relative-position code in car's frame [physics §5.1]
};

// The later builds' AWD system 3 settings (ProgramData's Awd3 table; the two floats as the driveline builder leaves them
// in the program's data): the coupling gain's lateral fade and its switch [physics §14.5]. Unused under GT3's rules.
struct Awd3 {
    F fade_start, fade_floor; // the lateral acceleration (g) the gain starts to fade at, the factor at 1 g and beyond
    u8 negative_full = 0;     // 1: a negative torque gives minus the full-transfer value
};

// The later builds' ASCC switches (ProgramData's RaceSwitches table, bytes +3 .. +5; Rule::Ascc) [physics §14.5]: all three
// are 1 on the discs. Unused under GT3's rules (later 0).
struct AsccSwitches {
    u8 later = 0;      // the race's build has them
    u8 c_status = 0;   // channel C counts for the status byte (on, and acting)
    u8 c_throttle = 0; // channel C also acts with the throttle applied
    u8 e_quiet = 0;    // channel E does not count for the status byte; an ASM switched off keeps channel E's wheels
};

// The later builds' stepless gearbox settings (ProgramData's Stepless table, its bytes x 0.01 as the routine 0x1D9A08 of
// Concept 2001 Tokyo leaves them): the ratio's rates a second [physics §14.5]. Unused under GT3's rules.
struct Stepless {
    F full, none; // kind 1: at full throttle and with the pedal released
    F down, up;   // kind 2: towards a lower and a higher ratio
};

// The later builds' layout 7 system (AWD system 5: an engine on the rear axle with a motor of its own, two front motors and a
// store; ProgramData's Hybrid table): the floats the set-up routine (Concept 2001 Tokyo 0x1D9CF0) makes of the table's bytes,
// in its globals 0x33565C .. [physics §14.5]. Unused under GT3's rules (on 0).
struct Hybrid {
    u8 on = 0;                 // the race's build has the system (its table found, the later builds' rules)
    F front, rear;             // +0 / +1 (0x33565C / 60) the front motors' (each) and the rear motor's ratings (kW)
    F front_drive, rear_drive; // +2 / +3 x 10 (0x335664 / 68) the front motors' torque factor driving, the rear motor's torque cap
    F front_regen, rear_regen; // +4 / +5 x 10 (0x33566C / 70) the same regenerating
    F front_scale;             // +6 x 0.1 (0x335674) the front motors' torque scale (their cap: x front_drive / front_regen)
    F capacity;                // +7 x (2 front + rear) (0x335678) the store's capacity
    F front_charge_cap;        // +8 x 5 (0x33567C) a front wheel's braking charge per second, at most
    F rear_charge_cap;         // +9 x 5 (0x335680) the rear wheels' braking charge per second, at most
    F idle_rpm;                // +0xA x 100 (0x335684) the open clutch's charging: the rpm below which the throttle rises
    F idle_gain;               // +0xB / 100000 (0x335688) its throttle per rpm below that
    F idle_share;              // +0xC / 100 (0x33568C) its torque at most this share of the full-throttle torque
    F idle_power;              // +0xD (0x335690) and at most this power (kW)
    F low, high;               // +0xE / +0xF % of the capacity (0x335694 / 98): the marks the store's share fades between
    F reserve;                 // +0x10 / front (0x33569C) the front motors' share of the demand with the store at the low mark
    s8 gear_lo = 0, gear_hi = 0; // +0x11 / +0x12 the gears in which the rear motor's demand can pass 0
    s8 gear_min = 0;           // +0x13 the rear motor drives only above this gear
    F front_share;             // +0x14 / 100 (0x3356A0) the front motors' share of the engine's power
    F drive_accel, drive_lat;  // +0x15 / +0x16 / 980 (0x3356A4 / A8) driving: the yaw split's gains on the lateral
                               // acceleration and the wanted lateral acceleration
    F drive_dead;              // +0x17 / 100 (0x3356AC) and its dead band
    F regen_accel, regen_lat;  // +0x18 / +0x19 / 980 (0x3356B0 / B4) the same regenerating
    F regen_dead;              // +0x1A / 100 (0x3356B8)
    F flow_scale;              // (0x3356BC) the flow display's scale: 6000 (2002 Tokyo-Geneva Europe's 5000) / (2 front + rear)
};

// The program's static physics block 0x2D28B0 (shipped bytes, its tables built by 0x1DAE70): the parts phases 5 and 7 read.
struct Statics {
    Awd3 awd3;                          // (the later builds')
    Stepless stepless;                  // (the later builds')
    Hybrid hybrid;                      // (the later builds')
    std::array<u8, 2> slip_fade{};      // +0x84 (0x2D2934 / 35) the slip ratio's low-speed fade speed (km/h) for slip state < 0 /
                                        // > 0; 0 = no fade
    std::array<u8, 2> slip_gain_on{};   // +0x86 (0x2D2936 / 37) per side (0 braking, 1 driving): 0 the slip gain is 1 (phase 5);
                                        // 2 the side's longitudinal force is the gained one (phase 7)
    std::array<u8, 2> slip_gain_base{}; // +0x88 (0x2D2938 / 39) the slip gain at rest (%)
    std::array<u8, 2> slip_gain_knee{}; // +0x8A (0x2D293A / 3B) the hub speed where it reaches 1 (km/h)
    Table<16> rolling_curve;            // +0x430 (0x2D2CE0) rolling-resistance factor vs |forward speed|
    std::array<F, 16> rolling_mu;       // +0x440 (0x2D2CF0) rolling-resistance coefficient per surface
};

} // namespace gt3r::sim::exact
