#pragma once

// The vehicle tick [physics §1.2] in GT3's own arithmetic: each phase a function on the car record (car_record.h),
// in the program's operation order, and the race step's phases for every car (tick, at the end).

#include "base/game_random.h"
#include "sim/exact/car_record.h"

#include <functional>
#include <span>

namespace gt3r::sim::exact {

class CourseGround; // course_ground.h
struct AiCourse;    // ai_course.h
struct FlowStep;    // pit_lane.h

// The race's behaviour (Car::behaviour, sim/program.h): PAL's rate (50 steps a second: its step length and tick counts),
// PAL's rules, NTSC-J's rules.
inline bool pal_rate(const Car& c) { return c.behaviour.rate == 50; }
// Routine r as PAL's / NTSC-J's (the race's rules, or the disc's own variant of that routine: Behaviour::rule).
inline bool pal_rules(const Car& c, Rule r) { return c.behaviour.rule(r) == Program::Pal; }
inline bool ntsc_j_rules(const Car& c, Rule r) { return c.behaviour.rule(r) == Program::NtscJ; }

// Shared by several phases: the program's table lookups, the tyre record of a wheel, its angle helpers, pedal curves and the
// race's tyre wear curve.

// y(v) on a table's n points, the end values outside the x range, linear in the first segment whose end is above v.
F lookup(s16 n, const F* x, const F* y, F v);
template <size_t N> F lookup(const Table<N>& t, F v) { return lookup(t.n, t.x.data(), t.y.data(), v); }
// The same lookup with a bisection first (a knot hit exactly returns its y).
F lookup_bisect(s16 n, const F* x, const F* y, F v);
template <size_t N> F lookup_bisect(const Table<N>& t, F v) { return lookup_bisect(t.n, t.x.data(), t.y.data(), v); }
// The tyre record of a wheel on a surface (< 16): the axle's index byte for the surface picks the record.
int tire_record_index(const Params& p, int wheel, u8 surface);
const TireRecord& tire_record(const Params& p, int wheel, u8 surface);
// Whether the TCS reads an axle's wheels (0 front, 1 rear) under a drive layout [physics §4.6].
inline bool tcs_reads_axle(u8 layout, int axle) { return axle == 0 ? layout != 0 && layout != 5 && layout != 6 : layout != 1; }
// An angle into [-pi, pi), and the wrapped difference a - b. The NTSC-J program adds or takes 2 pi until
// the angle is inside (NTSC-U and PAL take whole turns at once beyond 3 pi).
F angle_wrap(F a, Program p = Program::NtscU);
F angle_diff(F a, F b, Program p = Program::NtscU);
// A slip angle folded into [0, pi/2], with the sign (+-1) that flips beyond 90 degrees.
F slip_angle_fold(F beta, F& sign);
// A pedal curve as the program takes it ({s32 type; float a, b, c}): type 1 c (|x| - a)^2 + b, type 2 c sqrt(|x| - a) + b,
// otherwise |x|; with the sign of x.
struct PedalCurve {
    s32 type = 0;
    F a, b, c;
};
// Without the slope output.
F pedal_curve(const PedalCurve& k, F x);
// The race's tyre wear parameters (cp = conductor + 0x84) [physics §8.1].
struct WearParams {
    F fresh, fresh_loss, knee1, loss1, knee2, loss2, max, max_loss; // +0x00 .. +0x1C; max = 0: wear is off
    F worn;     // +0x20 the "tyres worn" grip factor
    F slip_cap; // +0x24 the cap on the slip intensity
};

// Phase 1: the current suspension, steering, ground and body values into the "previous tick" slots.
void save_previous(Car& c);

// Phase 2, what it reads outside the car record.
struct Slipstream { // globals, set at race initialisation from the static block [physics §5.2]
    F drag_share, downforce_share, k_up, k_down; // A, B, K_up, K_down
};
struct Condition { // engine condition parameters (block +0x00 .. +0x20) [physics §10.3]
    F e1, c1, e2, e3, c2, o1, co, omax, x20; // e3: the engine wear's limit, omax: the oil's
};
struct PrepInput {
    s32 start_count = 0; // the race's start countdown (race info + 0x5C); > 0 holds the cars
    Slipstream slip;
    WearParams wear;
    Condition condition;
};
// The condition parameters from the static bytes.
Condition condition_params(const std::array<u8, 9>& b);
// The engine condition factor f_E(E) x f_O(O).
F condition_factor(const Condition& k, F e, F o);
// The full-throttle torque at omega times the condition factor.
F full_throttle_torque(const Car& c, const Condition& k, F omega);
// The engine friction torque at omega.
F friction_torque(const Car& c, F omega);
// Steps the countdown F +0x51D.
void step_x51d(Car& c);
// dt_car and its inverse from the car's time scale.
void step_length(Car& c);
// The slipstream filter, then drag and downforce.
void aero(Car& c, const Slipstream& s);
// Each wheel's wear grip factor and HUD level; false (nothing done) when wear is off or the car is on the pit surface.
bool wear_grip(Car& c, const WearParams& cp);
// Wheel i's load per metre of its axle's section width.
F load_per_width(const Params& p, int wheel, F load);
// Each wheel's peak tire force [physics §3.1], wear grip first.
void peak_force(Car& c, const WearParams& cp);
// The velocity direction atan2(-vy, vx) of a hub velocity and its low-speed fade (0 when the fade is).
F slip_angle(F vx, F vy, F& fade);
// Each wheel's slip direction and fade from its hub velocity.
void slip_angles(Car& c);
// Phase 2: the race hold, the countdowns, dt_car, aero, the engine's torque at omega, peak forces and slip angles.
void prep(Car& c, const PrepInput& in);

// Phase 3, driver input [physics §4.4].
// A pad / replay frame (16 bytes) as the pad read and the conductor's pad hook (the pad hook; only free run's merges
// another device) deliver it.
struct PadFrame {
    u8 flags = 0;     // +0x0 analog axes: bit 0 steer, 1 throttle, 2 brake, 3 back axis, 4 third axis (clear: a 0 / 1 button)
    u8 kind = 0;      // +0x1 controller kind: 0 digital, 2 table-mapped analog, 3 DualShock stick, 4 steering wheel
    s8 shift = 0;     // +0x2 shift request (+1 up, -1 down)
    s16 steer = 0;    // +0x4 +-4096 full lock (+ = left)
    u16 throttle = 0; // +0x8
    u16 brake = 0;    // +0xA
    u16 back = 0;     // +0xC the back (reverse) axis
    u16 third = 0;    // +0xE the third axis (handbrake)
};
// The controller constants the driver input reads. The program derives them, when a race is set up, from the controller settings
// (static data that no code writes); pad_constants (race_inputs.h) builds them from the program's data.
struct PadConstants {
    F throttle_up, throttle_down, brake_up, brake_down, third_up, third_down; // digital ramps per tick
    F k1, k2, k3;                // the steering follower's gains
    F wheel_slew;                // the steering wheel's bounds slew rate (rad/s)
    std::array<F, 4> wheel_lock; // the steering wheel's least lock: types 1 / 3, on dirt; type 2, on dirt
    F analog_jump;               // c: an analog pedal x > 0 gives x + c (1 - x)
    std::array<F, 3> rate_out, rate_back, rate; // per rate class: the follower's rate limits
                                                // (rad/s), away from and back to the centre for steering class 1, both ways else
    PedalCurve wheel_throttle, wheel_brake;     // the steering wheel's pedal curves
    std::array<u8, 16> surface_class{};         // a surface's class for the lock curve on dirt (1: loose, both front
                                                // wheels for curve C; 2: rate class 2)
};

// v moved towards target by at most step.
void slew(F& v, F target, F step);
// The steering follower: the command F+0x41C and its rate F+0x424 towards target, the rate limited to rate_out (to
// rate_back while returning towards the centre), landing on the target when it reaches it.
void steer_follow(Car& c, const PadConstants& k, F target, F rate_out, F rate_back);
// The pad clutch request F+0x418 for the race entry's pedal style (0 while the AI or a parked car drives).
void pad_clutch(Car& c, const PadFrame& f, u8 style);
// The frame into the controls: the steering bounds from the lock curves and the slip-angle extension, the steering
// command (the follower, or the steering wheel's position), throttle, brake, third axis, the reverse, shift and clutch
// requests. course_kind is the course category (the conductor's byte +0x8000 +0x37AD, 0xFF read as 0; 2 dirt).
void pad_controls(Car& c, const PadConstants& k, const PadFrame& f, u8 course_kind);
// The brake pressure of every wheel.
void set_brake(Car& c, F p);
// The HUD bytes of the frame: the steering in percent and the full-throttle flag (cleared unless the throttle is
// pressed, all the way when analog).
void pad_hud(Car& c, const PadFrame& f);
// pad_controls, then the throttle into the engine's throttle and the brake pedal into every wheel.
void pad_input(Car& c, const PadConstants& k, const PadFrame& f, u8 course_kind);
// Phase 3's pad path: control source 0, and driver kind 0 or the hold counter running (the other cars take the AI paths).
bool on_pad_path(const Car& c);
// Phase 3 for a car on the pad path (control source 0 and driver kind 0 or the hold counter running): the
// full-throttle flag set, pad_hud, pad_input; the phase then runs the AI driver object's update (ai_update; driver_input below
// is the whole phase).
void driver_input_pad(Car& c, const PadConstants& k, const PadFrame& f, u8 course_kind);
// The brake pedal and every wheel's brake pressure.
void set_brake_pedal(Car& c, F p);
// The AI driver object's outputs into the controls: the throttle (moved by at most dt per tick in modes 1 / 2), the
// steering command, the brake, the reverse request, the gearbox lock-out and the HUD steering percent.
void ai_outputs(Car& c);
// the AI paths of phase 3, after the AI's step: ai_outputs; a parked car (driver kind 8) has the handbrake on.
void ai_controls(Car& c);

// Phase 3, the AI driver [ai §1, §3, §5-§8, §10; physics §4.4]. The decisions call the line evaluation and the speed planning
// (the course's AI lines, the corner speeds and braking points) through AiPlan: the program's functions there, or another
// source of their results.
struct AiPlan {
    virtual ~AiPlan() = default;
    virtual F braking(Car& c) = 0;                                      // the point bookkeeping, the braking point and
                                                                        // the status byte; returns the throttle multiplier
    virtual F target(const Car& c, int line, int hint, F look, F& dx, F& dy) = 0; // the aim point on a line
                                                                        // relative to the car; returns the look-ahead it used
    virtual void speed_control(Car& c, F target, F gain) = 0;           // throttle and brake towards a speed
    virtual bool request_line(Car& c, int line, bool at_once) = 0;      // a line change (blended unless at_once)
    virtual void replan(Car& c) = 0;                                    // the segments re-planned (an AI take-over)
    virtual s32 lane_left(int line, int hint, F s) = 0;                 // the passing line on the left, -1 none
    virtual s32 lane_right(int line, int hint, F s) = 0;
    virtual F lap_length() = 0;
    virtual s32 stop_count() = 0;                                       // the pit stop line's (game line 4) point count
    virtual F stop_distance(s32 k) = 0;                                 // and its point k's distance as the loader left it
};
// What the AI's step reads outside the car record.
struct AiInputs {
    s32 start_count = 0;    // the race's start countdown (race info + 0x5C)
    u32 clock = 0;          // the race clock (conductor + 0x8000 + 0x6380; / 3 = ms)
    u32 hit_x = 0;          // the collision step's flags of the car's body (conductor + 0xBBB4 + 0xC + 0x4F0 index): the
                            // (always 0), +0x434 (B: hit at the front), +0x438 (A: hit at the rear) [ai §7]
    u32 hit_front = 0, hit_rear = 0;
    u8 course_category = 0; // the course category byte (2 dirt, where the wanted slip angle is not clamped)
};
// The steering command D +0x9C towards target by at most the steering rate times dt.
void ai_steer(Car& c, F target);
// The parked car (driver kind 8): outputs cleared, steering slewed to 0.
void ai_park(Car& c);
// The stop driver: steering slewed to target, no throttle, the brake on (kind 5: left), and once the car is at rest
// (3-D speed < 0.25 km/h and angular speed < 3 deg/s) driver kind 8 with the brake off.
void ai_stop(Car& c, F target);
// The start hold's throttle (the hold count n: >= 13 a blip between 3/4 and 15/16 of the up-shift rpm, else a hold
// around it), clutch request 0 [ai §10].
void ai_hold(Car& c, s32 n);
// The traffic step: the lane-change cooldown, a lane change when closing on a car ahead or with a car alongside, the
// return to the main line, the throttle lift behind a car [ai §14.1].
void ai_traffic(Car& c, AiPlan& plan);
// The AI's decision: the aim point and heading error, the recovery state, steering, throttle and brake by mode,
// traffic, the steering saturation's cut [ai §3, §5-§7].
void ai_decide(Car& c, const AiInputs& in, AiPlan& plan);
// The AI's step: the start hold, the parked car, the AI clutch request, the decision.
void ai_step(Car& c, const AiInputs& in, AiPlan& plan);
// No gear hint (D +0xAC -1, +0xAD 0).
void ai_gear_hint_clear(Driver& d);
// The gear hint D +0xAC / +0xAD for the speed the plan aims at, when the car is not already slower
// (braking modes 3 / 4: the lower of the two targets in 4).
void ai_gear_hint(Car& c);
// The AI driver object's update on the pad path: the point bookkeeping and the gear hint.
void ai_update(Car& c, AiPlan& plan);
// Phase 3 for one car, by its control source: the pad path (driver_input_pad, then ai_update), or the AI's step
// and its outputs (with the HUD bytes of the frame for control source 1 and for a pad car an AI drives).
void driver_input(Car& c, const PadConstants& k, const PadFrame& f, u8 course_kind, const AiInputs& in, AiPlan& plan);

// Phase 4: each wheel's steer angle from the steering command, then its cosine and sine.
void wheel_steer(Car& c);
// Wheel i's steer angle for the command delta: the front wheels take delta times their ratio, every wheel its axle's
// toe (out on the left, in on the right).
F wheel_steer_angle(const Params& p, int i, F delta);

// Phase 5: each wheel's slip ratio from its hub speed along its heading and its rim speed [physics §3.4]; the
// static block's slip bytes (Statics) set the fade and the gains.
void slip_ratios(Car& c, const Statics& sb);
// One wheel's slip ratio, state and force sign for hub speed x along the wheel's heading.
void slip_ratio(Drive& t, Wheel& w, F x, const Statics& sb);
// Slip gain k (0 braking, 1 driving) at hub speed u.
F slip_gain(const Statics& sb, int k, F u);

// What phase 6 reads outside the car record: the course's category byte (conductor + 0x8000 + 0x37AD, the conductor at
// car + 4) and the program's aid tunables (static bytes of the block).
struct AidInputs {
    u8 course_category = 0;
    std::array<u8, 2> over_angle{}; // the oversteer threshold (deg) at rest / from the full blend
    std::array<u8, 2> over_gain{};  // the oversteer gain (x 0.1) at rest / from the full blend
    u8 dirt_steer = 0;              // the steering command (deg) that cuts the dirt brake fully (0: no cut)
    std::array<u8, 2> dirt_cut{};   // the cut (%) of the front / rear brakes
};
// Phase 6: the steering indicators, then by the control source the car's own aids (a pad car, unless the AI
// drives it) or its driver's: TCS, the dirt brake cut, ASCC and the ABS limit [physics §4.5, §4.6, §10.4].
void aids(Car& c, const AidInputs& in);
// The understeer and oversteer indicators.
void steer_indicators(Car& c, const AidInputs& in);
// A pad car's aids: its own block, ASCC off while the handbrake is at 0.1 or more.
void driver_aids(Car& c, const AidInputs& in);
// An AI-driven car's aids: the driver's block, ASCC on.
void scripted_aids(Car& c, const AidInputs& in);
// The TCS: scales the throttle down while a driven wheel spins past its target slip; status F +0x521.
void tcs(Car& c, const Aids& a);
// The TCS's lateral factor: 1 while |x| <= b, then falling with slope a to 0.
F tcs_lateral(F x, F a, F b);
// 1 when the block's TCS gain is not 0.
u8 tcs_on(const Aids& a);
// 1 when channel A or D is enabled.
u8 ascc_on(const Aids& a);
// The brake pressures: the dirt cut, ASCC (with the tyres' braking slip targets), the ABS limit.
void brakes(Car& c, const Aids& a, bool ascc, const AidInputs& in);
// ASCC: scales the targets cap (wheels 0..3) by the axle's factor, then (when on) the channels; status F +0x520.
void ascc(Car& c, const Aids& a, std::array<F, 4>& cap, bool on);
// Adds x to a wheel's brake pressure, at most 1.
void brake_add(Wheel& w, F x);
// The course category (the byte; 0xFF reads as 0). Whether it is 2, dirt.
u8 course_category(u8 raw);
bool dirt_course(u8 raw);


// Phase 7 [physics §2, §3.5, §3.8, §4.1] runs, on the car and in this order: brakes_and_clutch,
// rolling_resistance, tyre_curves, tyre_long_force, tyre_slip_force,
// forward_step, wheel_net_torque, torque_vectoring, axle_step, wheel_speeds,
// engine_speed, gear_step, then lateral_yaw and stop_hold with airborne(c) as it is at that point.
// The engine, clutch, gearbox and driveline [physics §2]:
// The clutch link's start, the brake torques (pedal pressure and handbrake) and the wheel flag bytes.
void brakes_and_clutch(Car& c);
// Clutch request and engagement before the step (0 -> 2 on a launch, 1 -> 0 on request 0), the slipping clutch's
// engine and wheel-side speeds and the shift cut / blip.
void clutch_engage(Car& c);
// The wheel-side engine speed: the driven axle's speed times the gear's ratio, floored at 0.
F wheel_side_speed(const Car& c);
// The driven axle speed by layout (L3 the mean, L7 rear + k (front - rear)).
F driven_axle_speed(const Car& c);
// Each wheel's net torque from its tyre reaction and its brake torque.
void wheel_net_torque(Car& c);
// Torque vectoring of the y / t diffs.
void torque_vectoring(Car& c);
// The clutch's transmitted torque (engine speed while slipping), the AWD front torque and centre clutch, and the
// axle speeds.
void axle_step(Car& c);
// The L5 coupling gain, or the full-transfer value 98000.
F awd5_gain(const Car& c, F tg, F ff, F fr);
// The slipping clutch's torque factor for the speed difference dw.
F clutch_slip_factor(F dw);
// The wheel speeds from the axle speeds and the differentials.
void wheel_speeds(Car& c);
// Engine speed (free while open, the wheel side while locked), the clutch's lock and release, the rev cap, rpm.
void engine_speed(Car& c);
// The integer rpm from the engine speed, at least idle - 1.
void engine_rpm(Car& c);
// Engine torque for the throttle x: turbo load and lag, the turbo sound values, the fuel cut and power cap.
F engine_torque(Car& c, F x);
// The throttle blend x (T_wot + friction) - friction (friction only under the fuel cut), power-capped.
F engine_output(Car& c, F x);
// The power cap of power-restricted events.
F power_cap(const Engine& e, F t, F w);
// The capped boost multiplier with the stages at rpm n0 / n1; uncapped; one stage.
F boost(const Engine& e, s16 n0, s16 n1);
F boost_raw(const Engine& e, s16 n0, s16 n1);
F turbo_stage(const Engine& e, s16 n, int i);
// The gear decision and change (the clutch to 2 or 3 if engaged).
void gear_step(Car& c, const GearSettings& s);
// Sets the gear byte.
void set_gear(Car& c, u8 gear);
// The new gear or -1, by the gearbox mode (the pedal style for a pad-driven car).
int gear_decision(Car& c, const GearSettings& s);
// The automatic box (modes 0 and 2; kickdown: mode 2's speed-picked gear plus kick-down).
int shift_auto(Car& c, const GearSettings& s, bool kickdown);
// Mode 2's kick-down level from the throttle pedal.
void kickdown_step(Car& c);
// The lowest gear in 1..N-1 below (up rpm - 500) at speed v, else N (0 for v < 0).
int gear_for_speed(const Car& c, F v);
// Mode 1, modes 3, 4, 6, mode 5: the manual boxes.
int shift_manual(Car& c);
int shift_sequential(Car& c);
int shift_sequential_reverse(Car& c);
// The tyre forces and the planar motion [physics §3.5, §3.8, §4.1]:
// Clears the tick's longitudinal sums, external forces and longitudinal acceleration, then the rolling-resistance
// force: -sign(u) RR(|u|) times the wheels' summed load x surface coefficient.
void rolling_resistance(Car& c, const Statics& s);
// Each wheel's longitudinal force fraction C and friction-circle radius D at its slip ratio.
void tyre_curves(Car& c);
// Each wheel's drive / brake-side force (C Fp with the slip sign) and its gained reaction for the wheel spin.
void tyre_long_force(Car& c, const Statics& s);
// Each wheel's slip angle and slip-angle force (curves A and B times the fade weight, within the friction circle,
// times Fp).
void tyre_slip_force(Car& c);
// The forward speed and longitudinal acceleration from the drive forces, rolling resistance, drag and the tyre
// reactions (0 when the speed would change sign).
void forward_step(Car& c);
// A wheel's low-speed hold force, a sideways damper below 5 km/h (0 otherwise).
F low_speed_hold(const Wheel& w);
// Lateral acceleration, yaw acceleration and rate, and the lateral grip ratio; airborne: the lateral acceleration
// and grip ratio zeroed, the yaw rate kept.
void lateral_yaw(Car& c, bool airborne);
// The spawn / stop counter and stop holds zero the planar motion; otherwise the world velocity gains the
// accelerations along the body axes (nothing airborne).
void stop_hold(Car& c, bool airborne);
// The airborne flag for lateral_yaw and stop_hold: the four wheels' loads sum to 0.
bool airborne(const Car& c);
// Phase 7, the whole phase: the calls above in its order, the airborne flag taken after gear_step.
void dynamics(Car& c, const Statics& s, const GearSettings& g);

// The brake heat constants (set from the static block at race init) [physics §8.5].
struct BrakeHeatConsts {
    F c0, c1, c2, c3; // heating per N m m/s, cooling rate, cooling speed offset, heat offset
};
// Phase 8: each wheel's brake heat (W +0x94).
void brake_heat(Car& c, const BrakeHeatConsts& k);

// Phase 9: clears the pad feedback word.
void clear_feedback(Car& c);

// Phase 11: for every pair of the race's cars (in order, at most 8): the AI's zone cells and contact intensity,
// the relative-position codes and slipstream factors (k), then each car's slipstream target [physics §5.1, §5.2; ai §14].
void proximity(std::span<Car> cars, Proximity& k);
// Whether cars a and b see each other (both active, the same pit-surface bit, within 100 m and the height window),
// with b's centre in a's frame (ua, va), a's in b's (ub, vb) and the centre distance.
struct PairView {
    F ua, va, ub, vb, dist;
};
bool pair_view(const Car& a, const Car& b, PairView& out);
// The relative-position code of a car at (u, v) in this car's frame, `ahead` / `behind` the combined extents, w the
// half of the widths' sum.
u8 position_code(F dist, F u, F v, F ahead, F behind, F w);
// Marks the zone cell of a car at (u, v) (2) and its neighbour (1).
void zone_cells(Car& c, F dist, F u, F v, F ahead, F behind, F w);
// The bumper gap to car b ahead at (u, v) and the closing speed; false when there is none (or a is a pad car in driver kind 0).
bool gap_closing(const Car& a, const Car& b, F u, F v, F& gap, F& closing);
// Raises the contact intensity for a gap and closing speed.
void contact_intensity(Car& c, F gap, F closing);
// The slipstream factor of a car at (x, y) in the leader's frame.
F slipstream_factor(const Car& leader, F x, F y);

// Phase 12 calls, with the car unless noted, and does nothing else: hub_offsets_xy, speed_ramp
// (F), (ground queries), terrain_factor, suspension, attitude, bump_nudge,
// F, hub_offsets, body_axes,
// ground_frame. The suspension and body attitude ones
// [physics §4.2, §4.3]:
// The body (or ground) axes from pitch, roll and heading: R = Rz(heading) Ry(roll) Rx(pitch), columns forward, lateral,
// up [physics §1.3].
void euler_axes(F pitch, F roll, F heading, Vec3& fwd, Vec3& lat, Vec3& up);
// The body axes from the car's attitude (its own call).
void body_axes(Car& c);
// The hubs' horizontal / vertical offsets from the CoG along the body axes; both (z first).
void hub_offsets_xy(Car& c);
void hub_offsets_z(Car& c);
void hub_offsets(Car& c);
// Each wheel's speed ramp from its hub speed.
void speed_ramp(Car& c);
// Each wheel's suspension coordinate x from the body height, hub offset, ground and road bump, scaled by s.
void suspension_coords(Car& c, F s);
// Mask with bit i set for each wheel beyond its bump stop.
u32 bump_mask(const Car& c, u32 mask);
// The terrain factor, the suspension coordinates and the bump-stop mask.
void terrain_factor(Car& c);
// The damper force at speed v (clamped to +-1 m/s).
F damper_force(const Suspension& s, F v);
// One wheel's spring and damper step at coordinate x; returns the wheel's body force (spring + damper).
F wheel_suspension(Wheel& w, const Suspension& s, F inv_dt, F x);
// An axle's anti-roll force from its two wheels' clamped coordinates.
F anti_roll(F x_left, F x_right, const Suspension& s);
// Wheel i's visual camber and hub half-track at the travel (the program ignores the steer).
F wheel_camber(const Params& p, int i, F steer, F travel);
F hub_half_track(const Params& p, int i, F travel);
// Every wheel's suspension step, visual camber and half-track, then the per-wheel body forces.
void suspension(Car& c);
// The longitudinal tyre forces' pitch acceleration a and heave force b.
void pitch_terms(const Car& c, F& a, F& b);
// The spawn hold's height offset for counter n and its mode (0 none, 1 rising, 2 held and turning to the pit
// box's heading). PAL counts 250 steps where NTSC counts 300 (rising from 225, held from 25).
F spawn_offset(s32 n, s32& mode, bool pal_rate = false);
// The car at or below the ground + offset snaps to it with no vertical speed, pitch or roll; false when above.
bool spawn_settle(Car& c, F offset);
// The spawn / stop hold; true when it held the car (the attitude step is skipped). box_heading: the car's pit
// box's heading (pit_box_heading, race_inputs.h), read only in mode 2.
bool spawn_hold(Car& c, F box_heading);
// Pitch, roll and heave from the body and damper forces (after the spawn hold).
void attitude(Car& c, F box_heading);
// The bump-stop attitude nudge and the downward speed limit.
void bump_nudge(Car& c);
// The ground under the CoG, the ground pitch and roll and the ground frame.
void ground_frame(Car& c);
// The per-surface road bump tables (16 rows, by surface index) the ground contact reads, built at race start
// from the program's static surface block [physics §3.6] (surface_bumps in race_inputs.h), and the soft materials.
struct SurfaceBumps {
    std::array<F, 16> amp;    // bump amplitude (m)
    std::array<F, 16> freq;   // 2 pi / wavelength
    std::array<s8, 16> scaled; // nonzero: the amplitude is scaled by the hub speed ramp
    std::array<s8, 16> mode;   // wave mode (1: signed square)
    std::array<u32, 16> soft{}; // by material byte: 1 for the soft materials (grass, gravel, sand)
};
// What the ground contact counts for the conductor's course-out hook (its course-out rule, called with these right after: a
// no-op in single races, the course-out rule of the base conductor in time trials and licences [physics §11]).
struct GroundCounts {
    int flagged = 0; // wheels on a pit-lane triangle (flags bit 0)
    int soft = 0;    // wheels on a soft material (grass, gravel, sand)
};
// Phase 12: each wheel's query point (body position + hub offset, in the course frame), the course's ground query
// for all four (CourseGround::query), then per wheel that found the road: its ground height, surface index,
// triangle flags and road bump; the road segment hint (wheel 0's segment); F +0x535 bit 4 (on the pit-lane surface) when a
// wheel is on a pit-lane triangle and the conductor allows it (`mark_flagged`: its pit test; 1 in single races, 0 in time
// trials, licences, free runs) [physics §1.3].
GroundCounts ground_contact(Car& c, const CourseGround& course, const SurfaceBumps& sb, bool mark_flagged);
// The base conductor's course-out rule (its course-out rule, called by the ground contact with the counts: time trial, licence,
// free run and machine test; the other conductors' course-out rule is empty) [physics §11]: with all four wheels on pit-lane
// triangles while the lap progress F +0x3E4 is inside the pit stretch, the answer `pit` (the pit answer); with all four on soft
// materials, `soft` (the soft answer); the larger counts the course-out ticks up (1) or sets them to 0xFF (2), 0 clears them, and
// any sets the course-out flag. The pit stretch is the AI course's game line 4 (the pit stop line): from its
// point 1's lap distance to its point n - 2's, the progress taken a lap on when it is below half a lap; none when the line
// has fewer than 6 points.
struct CourseOut {
    bool on = false;      // the conductor has the rule
    s32 pit = 0, soft = 0; // the pit answer and the soft answer
    bool stretch = false; // the course has the pit stretch
    F lap, lo, hi;        // the lap length (road +0x18) and the stretch's ends (line 4 points 1 and n - 2, +0x14)
};
// The rule on the counts ground_contact returned; nothing while the course-out flag is set or the car is not
// under control source 0.
void course_out(Car& c, const CourseOut& r, const GroundCounts& n);
// The progress p inside the pit stretch.
bool in_pit_stretch(const CourseOut& r, F p);
// The counts of the wheels whose ground records found the road (what ground_contact returns).
GroundCounts ground_counts(const Car& c, const SurfaceBumps& sb);
// The surface index of a ground record [physics §3.6]: the material, or with flags bit 1 the alternate surface.
int surface_index(const Ground& g);
// The ground record's material is soft.
bool soft_material(const Ground& g, const SurfaceBumps& sb);
// The road bump wave at course (x, z): sinf(freq x) + cosf(freq z), mode 1 its signed square.
F bump_wave(int mode, F x, F z, F freq);
// A wheel's tyre effect request (0x3C bytes): what the course's tyre smoke / dust emitter (on the
// course's effects object) is given for the wheel each tick.
struct TyreEffect {
    s32 wheel;      // +0x00 (odd: right side)
    s32 surface;    // +0x04 the wheel's surface index
    F x, h, z;      // +0x08 the hub over the ground (course frame: x, the wheel's ground height, z)
    F yaw;          // +0x14 heading + the wheel's steer angle
    F attr_a;       // +0x18 the ground record's first scalar
    Vec3 normal;    // +0x1C the ground plane, raised 0.05 m (plane_d - 0.05 normal.y)
    F plane_d;      // +0x28
    F smoke_emit;   // +0x2C wheel +0x2C
    F slip_ratio;   // +0x30 wheel +0x68
    F hub_vx;       // +0x34 wheel +0x4C
    F rim_speed;    // +0x38 wheel +0x20
};
// Phase 12's last step: the four wheels' tyre effect requests, in wheel order. The emitter changes only the
// course's effects (and draws the VU0 random register R, six times per particle), not the car record.
std::array<TyreEffect, 4> tyre_effect_requests(const Car& c);
// Phase 12's speeds, wheel spin and effects. The constants the program builds at start-up from its static blocks come in as
// inputs.
// The pad's vibration level (car +0x16D5) from the front tyres' drive and brake forces, the dampers, kerbs under
// the front wheels and the last wall hit.
void pad_rumble(Car& c);

// The force feedback's parameters (built at race start from the static block + 0x34 ..).
struct SteerFeel {
    F align_gain;    // the front tyres' aligning term
    F lat_gain;      // the front lateral forces
    F pad_gain;      // the pad's steering movement
    F steer_gain;    // the steering command (clamped to +-steer_max)
    F steer_max;
    F slip_gain;     // the front slip angles (clamped to +-slip_max)
    F slip_max;
    u8 use_x420 = 0; // scale the lateral term by F +0x420
    u8 mode = 0;     // 0 as is, 1 mean of two steps, 2 the larger of two steps, 3 a decaying peak
    PedalCurve curve; // the odd curve on the lateral term: type 1 quadratic, 2 square root, else identity
};
// The steering wheel's force (car +0x16D6) and its filter state (F +0x514 / +0x518).
void steer_force(Car& c, const SteerFeel& s);

// One effect channel of a surface class (0x84 bytes + 0x84 class).
struct EffectCurves {
    Table<8> a, b;  // +0x00 on the lateral speed or the slip angle, +0x4C on the slip speed or the slip ratio
    F base, r0, g;  // +0x58 / +0x5C / +0x60: base, and the speed ramp's threshold and gain
};
struct EffectTables {
    // [channel][class]: channel 0 the slip intensity (+0x30), 1 the skid pitch (+0x34), 2 the smoke emission (+0x2C).
    std::array<std::array<EffectCurves, 3>, 3> set;
    // The curves' inputs (the tyre effect block + 0xA0 channel + 0x28 class, +0x00 / +0x1F): a non-zero by_lat feeds curve a the
    // lateral speed (else the slip angle), a non-zero by_slip feeds curve b the slip speed (else the slip ratio).
    std::array<std::array<u8, 3>, 3> by_lat{}, by_slip{};
    F kerb_scale, kerb_x0, kerb_y0, kerb_x1, kerb_y1; // kerb factor, skid pitch ramp on |rim speed|
};
// Each wheel's slip intensity, skid pitch and smoke emission, and the car's dirt (condition word 3) from the
// emission on loose and wet surfaces. course_kind: the course's category byte (the input; 0xFF reads as 0).
void tyre_effects(Car& c, const EffectTables& t, u8 course_kind);
// Returns at once.
void phase12_stub(Car& c);
// The engine sound levels F +0x4E4 .. +0x4E8 (scales: + 0x64 .. + 0x70).
void engine_sound(Car& c, const std::array<F, 4>& scales);
// The car's draw mode: 3 for a car that is not a ghost; a ghost: 1 (not drawn) while both words are 0, else 0 (not
// drawn) when it is CPU-controlled or driver kind 1, else 2 (translucent) [presentation.md]. x70: the conductor's +0x70
// ([car +0] + 0x70); ghost_switch: the race's ghost switch +0x217A4 ([[car +0] + 0x3C] + 0x217A4, read only when x70 is 0).
int draw_mode(const Car& c, u32 x70, u32 ghost_switch);
// For a ghost car (draw mode not 3), clears the tyre effects (pitch 1) and the engine sound levels.
void effects_reset(Car& c, u32 x70, u32 ghost_switch);
// The lagging heading and pitch (a spring-damper follower each) and their differences F +0x4F8 / +0x4FC.
void view_follow(Car& c);

// Forward / lateral speed along the ground frame and each hub's velocity [physics §10.1].
void hub_velocities(Car& c);
// Adds x (metres) to the odometer in 2 m units; returns the new carry.
F odometer_add(u32& odometer, F x);
// Speeds, odometer, engine wear and oil (at most k.e3 / k.omax), top speed, speedometer, free-rolling wheels
// [physics §10.1-§10.3].
void speed_bookkeeping(Car& c, const Condition& k);
// Heading wrap, each wheel's spin angle (W +0x38), the drawn car's points and the steering view value.
void wheel_spin(Car& c);
// What phase 12 reads outside the car record: the course's ground query and the road bump tables, whether the conductor
// marks pit-lane wheels (ground_contact's mark_flagged), the pit box's heading (spawn_hold), the force feedback, the tyre
// effect tables and the course category byte, the engine sound scales, the conductor's draw mode words (effects_reset) and
// the engine condition block.
struct ChassisInputs {
    const CourseGround* course = nullptr;
    const SurfaceBumps* bumps = nullptr;
    bool mark_flagged = false;
    F box_heading;
    const SteerFeel* feel = nullptr;
    const EffectTables* effects = nullptr;
    u8 course_kind = 0;
    std::array<F, 4> sound_scales;
    u32 x70 = 0, ghost_switch = 0;
    Condition condition;
    const CourseOut* course_out = nullptr; // the conductor's course-out rule after the ground contact (null: none)
};
// What phase 12 hands on: the counts for the conductor's course-out hook, the tyre effect requests for the emitter.
struct ChassisOutputs {
    GroundCounts counts;
    std::array<TyreEffect, 4> effects;
};
// Phase 12, the whole phase: its calls in its order.
ChassisOutputs chassis(Car& c, const ChassisInputs& in);

// Phase 13: tyre wear from slip intensity, load and compound [physics §8.2].
void tire_wear(Car& c, const WearParams& cp);
// What the conductor answers for the wear flags: the pit test (the mode has pit stops), the wear answer (the code, asked
// only for a car with control source 0) and the race-flow byte conductor + 0x8000 + 0x37AC.
struct PitMode {
    bool pits = false;
    s32 mode = 0;
    u8 kind = 0; // the race-flow byte
};
// Phase 13: the "tyres worn" flag and the pit request (F +0x535 bits 2 and 3) [physics §8.7].
void wear_flags(Car& c, const WearParams& cp, const PitMode& m);
// The VU0 random register's next value, 1.m - 1 in [0, 1) (base/game_random.h).
F vu0_random(Vu0Random& rng);
// Phase 13: the wheels' flap pendulums, drawing from the VU0 random register [physics §8.8].
void flaps(Car& c, Vu0Random& rng);

// The AI's lines and speed plan [ai §2-§4]: the AI driver object's (Car::ai) point bookkeeping, corner speeds, braking point,
// target point and speed controller on the AI course object k (ai_course.h; the driver's +0x08). cp is the conductor's
// (the driver's +0x00) wear curve: the corner speeds take the wheels' wear grip while its max is not 0.
// The start of the AI's tick: the outputs cleared (the steering to the current command), the line blend's weight
// down by dt (not below 0).
void ai_begin(Car& c);
// The corner speed of the arc from point i of the driver's line [ai §4.2].
F ai_corner_speed(const Car& c, const AiCourse& k, const WearParams& cp, int i);
// The next point index changed: the next segment's kind and target become the current ones, and the new next
// segment's are planned (point next, the last point past the end; kinds 3 and up keep the target) [ai §4.1].
void ai_next_segment(Car& c, const AiCourse& k, const WearParams& cp);
// Re-plan: the next point from a search of the race distance, the segment before it planned, then
// ai_next_segment.
void ai_replan(Car& c, const AiCourse& k, const WearParams& cp);
// The line at once (the blend's old line too, weight 0; `line` already resolved), then ai_replan.
void ai_set_line(Car& c, const AiCourse& k, const WearParams& cp, int line);
// A line request (the fallback applies): blended (old line := current, weight 1) or at once; false when no line
// of the request's fallback is present (nothing changes).
bool ai_request_line(Car& c, const AiCourse& k, const WearParams& cp, int line, bool at_once);
// The status cleared; except on the stop line (line 4): the next point steps (ai_next_segment on a change), then
// in a braking mode the status 2, else the braking distance to the next arc and the braking zone (mode 3 / 4, status 2),
// the soft zone and status 1 [ai §4.3]. Returns the throttle multiplier: the soft zone's factor times the event's
// throttle scale (not on the pit lane, line 3; 1 on the stop line).
F ai_braking(Car& c, const AiCourse& k, const WearParams& cp);
// The look-ahead distance at forward speed v [ai §3] (the driver's look_min, look_max and look_speed).
F ai_lookahead(const Driver& d, F v);
// The target point of line `line` at the car's race distance + look (the stop line 4 by its own evaluation),
// the look-ahead growing by 1 m while the target is nearer than 5 m (at most 9 times); (dx, dy) the target less the car's
// position. Returns the look-ahead used. `missed` (optional) is set when a line evaluation found no span (the program then
// keeps whatever its stack held; here the last values, 0 at first).
F ai_target(const Car& c, const AiCourse& k, int line, int hint, F look, F& dx, F& dy, bool* missed = nullptr);
// The speed controller's throttle base + gain (v - vt), the speed difference clamped to +-10 km/h, the result
// to [0, 1].
F ai_throttle_for(F v, F vt, F base, F gain);
// The throttle towards target speed vt (aiming 2 km/h below it, base 0.75), the brake at 1 when the throttle is 0
// and the car is faster, the gearbox lock-out (12 ticks) at a part throttle [ai §4.4].
void ai_speed_control(Car& c, F vt, F gain);
// The AI's decisions' plan calls (AiPlan) on these ports: the AI course object k and the conductor's wear curve cp.
class CoursePlan final : public AiPlan {
public:
    CoursePlan(const AiCourse& k, const WearParams& cp) : k_(k), cp_(cp) {}
    F braking(Car& c) override;
    F target(const Car& c, int line, int hint, F look, F& dx, F& dy) override;
    void speed_control(Car& c, F target, F gain) override;
    bool request_line(Car& c, int line, bool at_once) override;
    void replan(Car& c) override;
    s32 lane_left(int line, int hint, F s) override;
    s32 lane_right(int line, int hint, F s) override;
    F lap_length() override;
    s32 stop_count() override;
    F stop_distance(s32 k) override;

private:
    const AiCourse& k_;
    const WearParams& cp_;
};

// The race step's vehicle phases, phase-major over the cars as the step runs them [physics §1.2]: phases 1-9,
// the collision step (phase 10, the caller's), phases 11, 12 and 13. The step's own bookkeeping around them (the race-flow
// time accumulators car +0xEC4 that can skip a whole step, the conductor's hooks after phase 13) is the caller's.

// What a car's phases read outside its record.
struct CarInputs {
    PadFrame pad;                  // phase 3 on the pad path: the frame the pad read and the conductor's pad hook give
    AiInputs ai;                   // phase 3's AI step
    bool mark_flagged = false;     // phase 12 (ChassisInputs)
    F box_heading;                 // phase 12
    u32 x70 = 0, ghost_switch = 0; // phase 12
    PitMode pit;                   // phase 13's wear flags
    // A car whose record the caller writes before every step (an online race's remote player, ExactRace::pose): its phases
    // are skipped; phase 11 and the collision step still see it.
    bool posed = false;
};
// What the phases read outside the cars' records, the same for every car of the race step.
struct RaceInputs {
    PrepInput prep;            // phase 2 (and its wear curve and condition block for phases 12 and 13)
    AidInputs aids;            // phases 3 (its course category byte), 6 and 12
    Statics statics;           // phases 5 and 7
    GearSettings gears;        // phase 7
    BrakeHeatConsts brake_heat; // phase 8
    const CourseGround* course = nullptr; // phase 12
    SurfaceBumps bumps;
    SteerFeel feel;
    EffectTables effects;
    std::array<F, 4> sound_scales;
    PadConstants pad;          // phase 3 on the pad path
    CourseOut course_out;      // phase 12 (the conductor's course-out rule)
};
// The part of the step that is not ported, run in its place: `emit` for the tyre effect emitter that the program
// hands car i's requests to (it draws the VU0 random register).
using EmitHook = std::function<void(int i, const ChassisOutputs& out, Vu0Random& rng)>;
// Phases 1-9 for every car, each phase for all cars before the next; `plan` is the AI's line evaluation and speed planning.
void tick_until_collision(std::span<Car> cars, std::span<const CarInputs> in, const RaceInputs& race, AiPlan& plan);
// Phases 11 (`k`: the conductor's proximity matrices; null skips the phase, as the step's second entry mode does), 12 and 13.
void tick_after_collision(std::span<Car> cars, std::span<const CarInputs> in, const RaceInputs& race, Proximity* k,
                          Vu0Random& rng, const EmitHook& emit);
// The whole sequence with the collision step `collide` between them, then (`flow` not null) the step's loop after phase 13 over
// the race order: the race flow, the pit lane, the end of a run (tick_flow, pit_lane.h).
void tick(std::span<Car> cars, std::span<const CarInputs> in, const RaceInputs& race, AiPlan& plan,
          const std::function<void(std::span<Car>)>& collide, Proximity* k, Vu0Random& rng, const EmitHook& emit,
          FlowStep* flow = nullptr);

// The race step's own parts around the vehicle phases that the physics reads back: the race-flow gate before
// phase 1 and the rubber band after the race flow's hooks [physics §1.1; ai §9.3].

// The step's start: while the race-flow byte (conductor + 0x8000 + 0x37AC, the initialisation's argument) is set, each car's
// accumulator (car +0xEC4) loses the conductor's time scale (+0xB7B0, 1 from the initialisation); the step runs (true) when one went
// below 0, which then gains 1. With the byte clear it always runs.
bool race_flow_gate(std::span<Car> cars, u8 flow, F scale);

// The rubber band's parameters (conductor + 0xAC .. + 0xC4, set at the race's initialisation).
struct RubberBand {
    F slow, slow_start, slow_full; // +0xAC / +0xB0 / +0xB4 the slow-down: amount (fraction), the gaps it starts and is full at (m)
    F fast, fast_start, fast_full; // +0xB8 / +0xBC / +0xC0 the speed-up
    F global;                      // +0xC4 every scale's factor
};
// The car's lap count less its base.
s32 lap_count(const Car& c);
// car's distance behind ref: whole laps and metres (the progress difference, a lap taken over when the laps and
// the metres disagree in sign). lap: the course's lap length.
void race_gap(const Car& ref, const Car& car, F lap, s32& laps, F& gap);
// The speed-up of a car behind (laps, gap); 1 on the pit-lane surface.
F speed_up(const RubberBand& k, const Car& car, s32 laps, F gap);
// The slow-down of a car ahead (laps and gap negated).
F slow_down(const RubberBand& k, s32 laps, F gap);
// The conductor's time-scale hook: car's time scale F +0x524 against ref, times `scale` (and the global factor); returns
// it before the global factor. A finished car keeps `scale`. cap: the i.LINK battle's version, which caps the
// result at 1 + the speed-up amount. PAL caps every mode at 1.3 instead (and its i.LINK battle adds none).
F band_scale(const RubberBand& k, const Car& ref, Car& car, F lap, F scale, bool cap);
// The pass after the race flow's hooks, by the race mode (the conductor's rubber-band mode: the race configuration's +0x22 in single
// races and battles, -1 elsewhere): the reference is the first car of the ranking order (conductor + 0xE388) that the AI
// does not drive (control source not 2; none: car 0). Mode 1: every other car against it. Modes 0 and 2: from the reference's
// rank r (F +0x4E1) outwards, each car against its neighbour in the order (mode 0 chaining the scales). Other modes: none.
void rubber_band(std::span<Car> cars, std::span<const s32> order, s32 mode, const RubberBand& k, F lap, bool cap);

} // namespace gt3r::sim::exact
