#pragma once

// The vehicle tick [physics §1.2] in GT3's own arithmetic: each phase a function on the car record (car_record.h),
// in the program's operation order, and the race step's phases for every car (tick, at the end).

#include "base/game_random.h"
#include "sim/exact/car_record.h"
#include "sim/exact/special.h"

#include <functional>
#include <span>

namespace gt3r::sim::exact {

class CourseGround; // course_ground.h
struct AiCourse;    // ai_course.h
class CourseQuery;  // course_query.h
struct FlowStep;    // pit_lane.h

// The race's behaviour (Car::behaviour, sim/program.h): PAL's rate (50 steps a second: its step length and tick counts),
// PAL's rules, NTSC-J's rules.
inline bool pal_rate(const Car& c) { return c.behaviour.rate == 50; }
// Routine r as PAL's / NTSC-J's (the race's rules, or the disc's own variant of that routine: Behaviour::rule).
inline bool pal_rules(const Car& c, Rule r) { return c.behaviour.rule(r) == Program::Pal; }
inline bool ntsc_j_rules(const Car& c, Rule r) { return c.behaviour.rule(r) == Program::NtscJ; }

// Shared by several phases: the program's table lookups, the tyre record of a wheel, its angle helpers, pedal curves and the
// race's tyre wear curve.

// 0x1D8FB8: y(v) on a table's n points, the end values outside the x range, linear in the first segment whose end is above v.
F lookup(s16 n, const F* x, const F* y, F v);
template <size_t N> F lookup(const Table<N>& t, F v) { return lookup(t.n, t.x.data(), t.y.data(), v); }
// 0x1D8E50: the same lookup with a bisection first (a knot hit exactly returns its y).
F lookup_bisect(s16 n, const F* x, const F* y, F v);
template <size_t N> F lookup_bisect(const Table<N>& t, F v) { return lookup_bisect(t.n, t.x.data(), t.y.data(), v); }
// 0x1DB9B8: the tyre record of a wheel on a surface (< 16): the axle's index byte for the surface picks the record.
int tire_record_index(const Params& p, int wheel, u8 surface);
const TireRecord& tire_record(const Params& p, int wheel, u8 surface);
// Whether the TCS reads an axle's wheels (0 front, 1 rear) under a drive layout [physics §4.6].
// (later: Rule::Layout's later builds, whose layout 7 drives the rear axle: its front wheels are not read either)
inline bool tcs_reads_axle(u8 layout, int axle, bool later = false) {
    return axle == 0 ? layout != 0 && layout != 5 && layout != 6 && !(later && layout == 7) : layout != 1;
}
// 0x1D8C00 / 0x1D8C80: an angle into [-pi, pi), and the wrapped difference a - b. NTSC-J's 0x1D8C00 adds or takes 2 pi until
// the angle is inside (NTSC-U and PAL take whole turns at once beyond 3 pi, 0x1D8BA8).
F angle_wrap(F a, Program p = Program::NtscU);
F angle_diff(F a, F b, Program p = Program::NtscU);
// 0x1E7A68: a slip angle folded into [0, pi/2], with the sign (+-1) that flips beyond 90 degrees.
F slip_angle_fold(const Behaviour& h, F beta, F& sign);
// A pedal curve as 0x1D92E0 takes it ({s32 type; float a, b, c}): type 1 c (|x| - a)^2 + b, type 2 c sqrt(|x| - a) + b,
// otherwise |x|; with the sign of x.
struct PedalCurve {
    s32 type = 0;
    F a, b, c;
};
// 0x1D92E0 (without the slope output).
F pedal_curve(const PedalCurve& k, F x);
// The race's tyre wear parameters (cp = conductor + 0x84) [physics §8.1].
struct WearParams {
    F fresh, fresh_loss, knee1, loss1, knee2, loss2, max, max_loss; // +0x00 .. +0x1C; max = 0: wear is off
    F worn;     // +0x20 the "tyres worn" grip factor
    F slip_cap; // +0x24 the cap on the slip intensity
};

// Phase 1 (0x1EADB8): the current suspension, steering, ground and body values into the "previous tick" slots.
void save_previous(Car& c);

// Phase 2 (0x1EA428), what it reads outside the car record.
struct Slipstream { // globals 0x352924 .. 0x352930, set at race initialisation from the static block 0x2D28B0 [physics §5.2]
    F drag_share, downforce_share, k_up, k_down; // A, B, K_up, K_down
};
struct Condition { // engine condition parameters (block 0x2D2B00 +0x00 .. +0x20, built by 0x1DAB50) [physics §10.3]
    F e1, c1, e2, e3, c2, o1, co, omax, x20; // e3: the engine wear's limit, omax: the oil's
};
struct PrepInput {
    s32 start_count = 0; // the race's start countdown (race info + 0x5C); > 0 holds the cars
    Slipstream slip;
    WearParams wear;
    Condition condition;
};
// 0x1DAB50: the condition parameters from the static bytes 0x2D2950 .. 0x2D2958.
Condition condition_params(const std::array<u8, 9>& b, bool toward_zero = false); // (2002 Tokyo-Geneva's 0.001)
// 0x1DAD28: the engine condition factor f_E(E) x f_O(O).
F condition_factor(const Condition& k, F e, F o);
// ... as the race's build has it (Rule::EngineCondition): the later builds' routine (Concept 2001 Tokyo 0x1D5928) returns 1,
// no engine wear or oil factor.
F condition_factor(const Behaviour& b, const Condition& k, F e, F o);
// 0x1E0BC0: the full-throttle torque at omega times the condition factor.
F full_throttle_torque(const Car& c, const Condition& k, F omega);
// 0x1E0C28: the engine friction torque at omega.
F friction_torque(const Car& c, F omega);
// 0x1DDFF0: steps the countdown F +0x51D.
void step_x51d(Car& c);
// 0x1ECFC8: dt_car and its inverse from the car's time scale.
void step_length(Car& c);
// 0x1DB118: the slipstream filter, then drag and downforce.
void aero(Car& c, const Slipstream& s);
// 0x1E9E80: each wheel's wear grip factor and HUD level; false (nothing done) when wear is off or the car is on the pit surface.
bool wear_grip(Car& c, const WearParams& cp);
// 0x1EA1C0: wheel i's load per metre of its axle's section width.
F load_per_width(const Params& p, int wheel, F load);
// 0x1EA1E8: each wheel's peak tire force [physics §3.1], wear grip first.
void peak_force(Car& c, const WearParams& cp);
// 0x1EA348: the velocity direction atan2(-vy, vx) of a hub velocity and its low-speed fade (0 when the fade is).
F slip_angle(F vx, F vy, F& fade);
// ... as the race's build has it (Rule::SlipAngle): the later builds' atan2f (build_math.h); 2002 Tokyo-Geneva's fade speeds
// and slope are its own constants (Europe 0x33426C, 0x334270, 0x334274).
F slip_angle(const Behaviour& b, F vx, F vy, F& fade);
// 0x1EA3D8: each wheel's slip direction and fade from its hub velocity.
void slip_angles(Car& c);
// Phase 2 (0x1EA428): the race hold, the countdowns, dt_car, aero, the engine's torque at omega, peak forces and slip angles.
void prep(Car& c, const PrepInput& in);

// Phase 3, driver input [physics §4.4].
// A pad / replay frame (16 bytes) as the pad read 0x210740 and the conductor's pad hook (virtual 20; only free run's merges
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
// The controller constants the driver input reads. 0x1E3EA0 derives them, when a race is set up, from the controller settings
// (static data at 0x2D4628 that no code writes); pad_constants (race_inputs.h) builds them from the program's data.
struct PadConstants {
    F throttle_up, throttle_down, brake_up, brake_down, third_up, third_down; // 0x2D4930 .. 0x2D4944 digital ramps per tick
    F k1, k2, k3;                // $gp 0x352970 / 0x352974 / 0x352978 the steering follower's gains
    F wheel_slew;                // $gp 0x35297C the steering wheel's bounds slew rate (rad/s)
    std::array<F, 4> wheel_lock; // $gp 0x352980 .. the steering wheel's least lock: types 1 / 3, on dirt; type 2, on dirt
    F analog_jump;               // $gp 0x352990 c: an analog pedal x > 0 gives x + c (1 - x)
    std::array<F, 3> rate_out, rate_back, rate; // 0x49D080 / 0x49D090 / 0x49D0A0 per rate class: the follower's rate limits
                                                // (rad/s), away from and back to the centre for steering class 1, both ways else
    PedalCurve wheel_throttle, wheel_brake;     // 0x49D060 / 0x49D070 the steering wheel's pedal curves
    std::array<u8, 16> surface_class{};         // 0x2D4948 a surface's class for the lock curve on dirt (1: loose, both front
                                                // wheels for curve C; 2: rate class 2)
};

// 0x1E4320: v moved towards target by at most step.
void slew(F& v, F target, F step);
// 0x1E4368: the steering follower: the command F+0x41C and its rate F+0x424 towards target, the rate limited to rate_out (to
// rate_back while returning towards the centre), landing on the target when it reaches it.
void steer_follow(Car& c, const PadConstants& k, F target, F rate_out, F rate_back);
// 0x1E4180: the pad clutch request F+0x418 for the race entry's pedal style (0 while the AI or a parked car drives).
void pad_clutch(Car& c, const PadFrame& f, u8 style);
// 0x1E4478: the frame into the controls: the steering bounds from the lock curves and the slip-angle extension, the steering
// command (the follower, or the steering wheel's position), throttle, brake, third axis, the reverse, shift and clutch
// requests. course_kind is the course category (0x1F2F68: the conductor's byte +0x8000 +0x37AD, 0xFF read as 0; 2 dirt).
void pad_controls(Car& c, const PadConstants& k, const PadFrame& f, u8 course_kind);
// 0x1EAC60: the brake pressure of every wheel.
void set_brake(Car& c, F p);
// 0x1E4290: the HUD bytes of the frame: the steering in percent and the full-throttle flag (cleared unless the throttle is
// pressed, all the way when analog).
void pad_hud(Car& c, const PadFrame& f);
// 0x1E4CB0: pad_controls, then the throttle into the engine's throttle and the brake pedal into every wheel.
void pad_input(Car& c, const PadConstants& k, const PadFrame& f, u8 course_kind);
// Phase 3's pad path: control source 0, and driver kind 0 or the hold counter running (the other cars take the AI paths).
bool on_pad_path(const Car& c);
// Phase 3 (0x1E4D38) for a car on the pad path (control source 0 and driver kind 0 or the hold counter running): the
// full-throttle flag set, pad_hud, pad_input; the phase then runs the AI driver object's update (ai_update; driver_input below
// is the whole phase).
void driver_input_pad(Car& c, const PadConstants& k, const PadFrame& f, u8 course_kind);
// 0x1EAC90: the brake pedal and every wheel's brake pressure.
void set_brake_pedal(Car& c, F p);
// 0x1F79F0: the AI driver object's outputs into the controls: the throttle (moved by at most dt per tick in modes 1 / 2), the
// steering command, the brake, the reverse request, the gearbox lock-out and the HUD steering percent.
void ai_outputs(Car& c);
// 0x1E4CF0 (the AI paths of phase 3, after the AI's step): ai_outputs; a parked car (driver kind 8) has the handbrake on.
void ai_controls(Car& c);
// (Rule::DriverInput) The end of phase 3's paths where the AI drives, in the later builds: the HUD's steering percentage
// two thirds of the way to the command's (in percent of 15 degrees).
void ai_steer_pct(Car& c);

// Phase 3, the AI driver [ai §1, §3, §5-§8, §10; physics §4.4]. The decisions call the line evaluation and the speed planning
// (the course's AI lines, the corner speeds and braking points) through AiPlan: the program's functions there, or another
// source of their results.
// The later builds' stop zone (their conductor's +0xE408 .. +0xE47B; Gran Turismo Concept [physics §14.5]): a stretch of
// the lap where the cars the AI drives stop once a lap, each at its own point. The conductor sets it up (race_flow.h), the
// AI's braking picks a car's point and stores it, the decision drives to it and holds the car there until the race flow
// has counted the stop. Unused under GT3's rules.
struct StopZone {
    u8 active = 0;               // +0xE408: 1 while the race has a stop zone
    std::array<u8, kMaxCars> stops{}; // +0xE40A + car: the stops it made
    F base, spacing;             // +0xE414 / +0xE418: the zone's start along the lap and its length (m)
    std::array<F, kMaxCars> x, y, dist; // +0xE41C / +0xE43C / +0xE45C + 4 car: the car's stop point and its race distance
    int cars = 0;                // (+0xE3E4, the car count)
    bool has(int i) const { return active == 1 && i >= 0 && i < cars; }
    // Later 0x1EE6A0: the stops car owes, its laps counted less its stops made (-1 without a zone).
    s32 owed(const Car& c) const { return active == 1 ? s32(c.laps) - s32(stops[size_t(c.index) % kMaxCars]) : -1; }
};

struct AiPlan {
    virtual ~AiPlan() = default;
    virtual StopZone* stop_zone() { return nullptr; }                   // the later builds' stop zone (none: no such race)
    virtual F braking(Car& c) = 0;                                      // 0x1F7088: the point bookkeeping, the braking point and
                                                                        // the status byte; returns the throttle multiplier
    virtual F target(const Car& c, int line, int hint, F look, F& dx, F& dy) = 0; // 0x1F7658: the aim point on a line
                                                                        // relative to the car; returns the look-ahead it used
    virtual F line_heading(const Car& c, int line, int hint, F s) = 0;  // 2002 Tokyo-Geneva's (Europe 0x1F6E38): the line's
                                                                        // heading at race distance s (ai_line_heading)
    virtual void speed_control(Car& c, F target, F gain) = 0;           // 0x1F7590: throttle and brake towards a speed
    virtual bool request_line(Car& c, int line, bool at_once) = 0;      // 0x1F8980: a line change (blended unless at_once)
    virtual void replan(Car& c) = 0;                                    // 0x1F6A20: the segments re-planned (an AI take-over)
    virtual s32 lane_left(int line, int hint, F s) = 0;                 // 0x1F9CE0: the passing line on the left, -1 none
    virtual s32 lane_right(int line, int hint, F s) = 0;                // 0x1F9D38
    virtual F lap_length() = 0;                                         // 0x18FA10
    virtual s32 stop_count() = 0;                                       // the pit stop line's (game line 4) point count
    virtual F stop_distance(s32 k) = 0;                                 // and its point k's distance as the loader left it
};
// What the AI's step reads outside the car record.
struct AiInputs {
    s32 start_count = 0;    // the race's start countdown (race info + 0x5C, 0x1F2B98)
    u32 clock = 0;          // the race clock (conductor + 0x8000 + 0x6380; / 3 = ms)
    u32 hit_x = 0;          // the collision step's flags of the car's body (conductor + 0xBBB4 + 0xC + 0x4F0 index): 0x1FB870's
                            // (always 0), +0x434 (B: hit at the front), +0x438 (A: hit at the rear) [ai §7]
    u32 hit_front = 0, hit_rear = 0;
    u8 course_category = 0; // the course category byte (0x1F2F90: 2 dirt, where the wanted slip angle is not clamped)
    u8 start_speed = 0;     // the race-rule block's byte 0 (conductor + 0xE340), the start speed: 2002 Tokyo-Geneva's long
                            // start look-ahead is for a standing or slow start only
};
// 0x1F77B8: the steering command D +0x9C towards target by at most the steering rate times dt.
void ai_steer(Car& c, F target);
// 0x1F7900: the parked car (driver kind 8): outputs cleared, steering slewed to 0.
void ai_park(Car& c);
// 0x1F7810: the stop driver: steering slewed to target, no throttle, the brake on (kind 5: left), and once the car is at rest
// (3-D speed < 0.25 km/h and angular speed < 3 deg/s) driver kind 8 (0x1E3E90) with the brake off.
void ai_stop(Car& c, F target);
// 0x1F8A00: the start hold's throttle (the hold count n: >= 13 a blip between 3/4 and 15/16 of the up-shift rpm, else a hold
// around it), clutch request 0 [ai §10].
void ai_hold(Car& c, s32 n);
// 0x1F6D70: the traffic step: the lane-change cooldown, a lane change when closing on a car ahead or with a car alongside, the
// return to the main line, the throttle lift behind a car [ai §14.1].
void ai_traffic(Car& c, AiPlan& plan);
// 0x1F7AE0: the AI's decision: the aim point and heading error, the recovery state, steering, throttle and brake by mode,
// traffic, the steering saturation's cut [ai §3, §5-§7].
void ai_decide(Car& c, const AiInputs& in, AiPlan& plan);
// 0x1E35D8: the AI's step: the start hold, the parked car, the AI clutch request, the decision.
void ai_step(Car& c, const AiInputs& in, AiPlan& plan);
// 0x1F6578: no gear hint (D +0xAC -1, +0xAD 0).
void ai_gear_hint_clear(Driver& d);
// 0x1F6588: the gear hint D +0xAC / +0xAD for the speed the plan aims at (0x1E4E80), when the car is not already slower
// (braking modes 3 / 4: the lower of the two targets in 4).
void ai_gear_hint(Car& c);
// 0x1F6648: the AI driver object's update on the pad path: the point bookkeeping (0x1F7088) and the gear hint.
void ai_update(Car& c, AiPlan& plan);
// Phase 3 (0x1E4D38) for one car, by its control source: the pad path (driver_input_pad, then ai_update), or the AI's step
// and its outputs (with the HUD bytes of the frame for control source 1 and for a pad car an AI drives).
void driver_input(Car& c, const PadConstants& k, const PadFrame& f, u8 course_kind, const AiInputs& in, AiPlan& plan);

// Phase 4 (0x1E8978): each wheel's steer angle from the steering command, then its cosine and sine.
void wheel_steer(Car& c);
// 0x1E8910: wheel i's steer angle for the command delta: the front wheels take delta times their ratio, every wheel its axle's
// toe (out on the left, in on the right).
F wheel_steer_angle(const Params& p, int i, F delta);

// Phase 5 (0x1E7328): each wheel's slip ratio from its hub speed along its heading and its rim speed [physics §3.4]; the
// static block's slip bytes (Statics) set the fade and the gains.
void slip_ratios(Car& c, const Statics& sb);
// 0x1E7068: one wheel's slip ratio, state and force sign for hub speed x along the wheel's heading.
void slip_ratio(Drive& t, Wheel& w, F x, const Statics& sb);
// 0x1E6FE0: slip gain k (0 braking, 1 driving) at hub speed u.
F slip_gain(const Statics& sb, int k, F u);

// What phase 6 reads outside the car record: the course's category byte (conductor + 0x8000 + 0x37AD, the conductor at
// car + 4) and the program's aid tunables (static bytes of the block at 0x2D28B0).
struct AidInputs {
    u8 course_category = 0;
    std::array<u8, 2> over_angle{}; // 0x2D295D / 0x2D295E the oversteer threshold (deg) at rest / from the full blend
    std::array<u8, 2> over_gain{};  // 0x2D295F / 0x2D2960 the oversteer gain (x 0.1) at rest / from the full blend
    u8 dirt_steer = 0;              // 0x2D2965 the steering command (deg) that cuts the dirt brake fully (0: no cut)
    std::array<u8, 2> dirt_cut{};   // 0x2D2966 / 0x2D2967 the cut (%) of the front / rear brakes
    AsccSwitches ascc;              // (the later builds' ASCC switches)
};
// Phase 6 (0x1E3B18): the steering indicators, then by the control source the car's own aids (a pad car, unless the AI
// drives it) or its driver's: TCS, the dirt brake cut, ASCC and the ABS limit [physics §4.5, §4.6, §10.4].
void aids(Car& c, const AidInputs& in);
// 0x1E3708: the understeer and oversteer indicators.
void steer_indicators(Car& c, const AidInputs& in);
// 0x1E3A68: a pad car's aids: its own block, ASCC off while the handbrake is at 0.1 or more.
void driver_aids(Car& c, const AidInputs& in);
// 0x1E3AD0: an AI-driven car's aids: the driver's block, ASCC on.
void scripted_aids(Car& c, const AidInputs& in);
// 0x1E3360: the TCS: scales the throttle down while a driven wheel spins past its target slip; status F +0x521.
void tcs(Car& c, const Aids& a);
// 0x1E32D8: the TCS's lateral factor: 1 while |x| <= b, then falling with slope a to 0.
F tcs_lateral(F x, F a, F b);
// 0x1E3338: 1 when the block's TCS gain is not 0.
u8 tcs_on(const Aids& a);
// 0x1E2CF8: 1 when channel A or D is enabled.
u8 ascc_on(const Aids& a, const AsccSwitches& sw = {});
// 0x1E3068: the brake pressures: the dirt cut, ASCC (with the tyres' braking slip targets), the ABS limit.
void brakes(Car& c, const Aids& a, bool ascc, const AidInputs& in);
// 0x1E2D08: ASCC: scales the targets cap (wheels 0..3) by the axle's factor, then (when on) the channels; status F +0x520.
void ascc(Car& c, const Aids& a, std::array<F, 4>& cap, bool on, const AsccSwitches& sw = {});
// 0x1E2CC8: adds x to a wheel's brake pressure, at most 1.
void brake_add(Wheel& w, F x);
// 0x1F2F68: the course category (the byte; 0xFF reads as 0). 0x1F2F90: whether it is 2, dirt.
u8 course_category(u8 raw);
bool dirt_course(u8 raw);


// Phase 7 (0x1E8598) [physics §2, §3.5, §3.8, §4.1] runs, on the car and in this order: 0x1E6EC8 brakes_and_clutch,
// 0x1E84D8 rolling_resistance, 0x1E73C0 tyre_curves, 0x1E7450 tyre_long_force, 0x1E7C08 tyre_slip_force, 0x1E7848
// forward_step, 0x1E5768 wheel_net_torque, 0x1E57F8 torque_vectoring, 0x1E5B98 axle_step, 0x1E6488 wheel_speeds, 0x1E7500
// engine_speed, 0x1E77E0 gear_step, then lateral_yaw and stop_hold with airborne(c) as it is at that point.
// The engine, clutch, gearbox and driveline [physics §2]:
// 0x1E6EC8: the clutch link's start (0x1E6D48), the brake torques (pedal pressure and handbrake) and the wheel flag bytes.
void brakes_and_clutch(Car& c);
// 0x1E6D48: clutch request and engagement before the step (0 -> 2 on a launch, 1 -> 0 on request 0), the slipping clutch's
// engine and wheel-side speeds and the shift cut / blip.
void clutch_engage(Car& c);
// 0x1E6CA8: the wheel-side engine speed: the driven axle's speed times the gear's ratio, floored at 0.
F wheel_side_speed(const Car& c);
// 0x1E6C28: the driven axle speed by layout (L3 the mean, L7 rear + k (front - rear)).
F driven_axle_speed(const Car& c);
// 0x1E5768: each wheel's net torque from its tyre reaction and its brake torque.
void wheel_net_torque(Car& c);
// 0x1E57F8: torque vectoring of the y / t diffs. (h: the later builds' layout 7 system, whose front motors come first,
// Concept 2001 Tokyo 0x1F16F0 calling 0x1DA6B0; unused under GT3's rules)
void torque_vectoring(Car& c, const Hybrid& h = {});
// 0x1E5B98: the clutch's transmitted torque (engine speed while slipping), the AWD front torque and centre clutch, and the
// axle speeds.
// (k: the later builds' AWD system 3 settings, Rule::AwdGain; unused under GT3's rules)
// (h: the later builds' layout 7 system: its engine's throttle and the rear motor's torque; unused under GT3's rules)
void axle_step(Car& c, const Awd3& k = {}, const Stepless& sl = {}, const Hybrid& h = {});
// 0x1E5998: the L5 coupling gain, or the full-transfer value 98000. The later builds' (Rule::AwdGain, Concept 2001 Tokyo
// 0x1F18B8): the lateral fade from their settings, the torque's sign turned in reverse gear, minus the full value for a
// negative torque with the settings' switch, and the full-transfer test on the torque plus both axles' terms.
F awd5_gain(const Car& c, F tg, F ff, F fr, const Awd3& k = {});
// 0x1E5B30: the slipping clutch's torque factor for the speed difference dw.
F clutch_slip_factor(F dw, bool toward_zero = false); // (toward_zero: 2002 Tokyo-Geneva's constants)
// 0x1E6488: the wheel speeds from the axle speeds and the differentials.
void wheel_speeds(Car& c);
// 0x1E7500: engine speed (free while open, the wheel side while locked), the clutch's lock and release, the rev cap, rpm.
// (h: the later builds' layout 7 system, whose open clutch's torque charges the store, Concept 2001 Tokyo 0x1DE0B8 calling
// 0x1DA218; unused under GT3's rules)
void engine_speed(Car& c, const Hybrid& h = {});
// 0x1E6D00: the integer rpm from the engine speed, at least idle - 1.
void engine_rpm(Car& c);
// 0x1E17F0: engine torque for the throttle x: turbo load and lag, the turbo sound values, the fuel cut and power cap.
F engine_torque(Car& c, F x);
// 0x1E1768: the throttle blend x (T_wot + friction) - friction (friction only under the fuel cut), power-capped.
F engine_output(Car& c, F x);
// 0x1E0C38: the power cap of power-restricted events.
F power_cap(const Engine& e, F t, F w);
// 0x1E0ED8: the capped boost multiplier with the stages at rpm n0 / n1; 0x1E0E08 uncapped; 0x1E0CD8 one stage.
F boost(const Engine& e, s16 n0, s16 n1);
F boost_raw(const Engine& e, s16 n0, s16 n1);
F turbo_stage(const Engine& e, s16 n, int i);
// 0x1E77E0: the gear decision and change (the clutch to 2 or 3 if engaged).
void gear_step(Car& c, const GearSettings& s);
// 0x1E4E78: sets the gear byte.
void set_gear(Car& c, u8 gear);
// 0x1E5690: the new gear or -1, by the gearbox mode (the pedal style for a pad-driven car).
int gear_decision(Car& c, const GearSettings& s);
// 0x1E4FF8: the automatic box (modes 0 and 2; kickdown: mode 2's speed-picked gear plus kick-down).
int shift_auto(Car& c, const GearSettings& s, bool kickdown);
// 0x1E4F10: mode 2's kick-down level from the throttle pedal.
void kickdown_step(Car& c);
// 0x1E4E80: the lowest gear in 1..N-1 below (up rpm - 500) at speed v, else N (0 for v < 0).
int gear_for_speed(const Car& c, F v);
// 0x1E5458 (mode 1), 0x1E5540 (modes 3, 4, 6), 0x1E5600 (mode 5): the manual boxes.
int shift_manual(Car& c);
int shift_sequential(Car& c);
int shift_sequential_reverse(Car& c);
// The tyre forces and the planar motion [physics §3.5, §3.8, §4.1]:
// 0x1E84D8: clears the tick's longitudinal sums, external forces and longitudinal acceleration, then the rolling-resistance
// force: -sign(u) RR(|u|) times the wheels' summed load x surface coefficient.
void rolling_resistance(Car& c, const Statics& s);
// 0x1E73C0: each wheel's longitudinal force fraction C and friction-circle radius D at its slip ratio.
void tyre_curves(Car& c);
// 0x1E7450: each wheel's drive / brake-side force (C Fp with the slip sign) and its gained reaction for the wheel spin.
void tyre_long_force(Car& c, const Statics& s);
// 0x1E7C08: each wheel's slip angle and slip-angle force (curves A and B times the fade weight, within the friction circle,
// times Fp).
void tyre_slip_force(Car& c);
// 0x1E7848: the forward speed and longitudinal acceleration from the drive forces, rolling resistance, drag and the tyre
// reactions (0 when the speed would change sign).
void forward_step(Car& c);
// 0x1E7AD8: a wheel's low-speed hold force, a sideways damper below 5 km/h (0 otherwise).
F low_speed_hold(const Wheel& w, bool toward_zero = false); // (toward_zero: 2002 Tokyo-Geneva's constants)
// 0x1E7D90: lateral acceleration, yaw acceleration and rate, and the lateral grip ratio; airborne: the lateral acceleration
// and grip ratio zeroed, the yaw rate kept.
void lateral_yaw(Car& c, bool airborne);
// 0x1E8008: the spawn / stop counter and stop holds zero the planar motion; otherwise the world velocity gains the
// accelerations along the body axes (nothing airborne).
void stop_hold(Car& c, bool airborne);
// 0x1E8598's airborne flag for lateral_yaw and stop_hold: the four wheels' loads sum to 0.
bool airborne(const Car& c);
// The race rule block's per-class grip percentages (+0x18 + the driveline class P +0x31, signed; 2002 Tokyo-Geneva's phase 7).
using ClassGrip = std::array<s8, 4>;
// 2002 Tokyo-Geneva's addition to phase 7 after tyre_curves (Europe 0x1DFB40; recognized with the build's soft doubles,
// toward_zero()): a car the AI drives (control source 1 or 2, or any driver kind) gets its class's percentage on its wheels'
// slip fade and friction circle, x (1 + percent x 0.01).
void class_grip_scale(Car& c, const ClassGrip& grip);
// Phase 7 (0x1E8598), the whole phase: the calls above in its order, the airborne flag taken after gear_step.
void dynamics(Car& c, const Statics& s, const GearSettings& g, const ClassGrip& grip = {});

// The brake heat constants (0x49D0B0 .. 0x49D0BC, set from the static block 0x2D292C by 0x1E6878 at race init) [physics §8.5].
struct BrakeHeatConsts {
    F c0, c1, c2, c3; // heating per N m m/s, cooling rate, cooling speed offset, heat offset
};
// Phase 8 (0x1E6928): each wheel's brake heat (W +0x94).
void brake_heat(Car& c, const BrakeHeatConsts& k);

// Phase 9 (0x1DFD90): clears the pad feedback word.
void clear_feedback(Car& c);

// Phase 11 (0x1EC098): for every pair of the race's cars (in order, at most 8): the AI's zone cells and contact intensity,
// the relative-position codes and slipstream factors (k), then each car's slipstream target [physics §5.1, §5.2; ai §14].
void proximity(std::span<Car> cars, Proximity& k);
// 0x1EBBC8: whether cars a and b see each other (both active, the same pit-surface bit, within 100 m and the height window),
// with b's centre in a's frame (ua, va), a's in b's (ub, vb) and the centre distance.
struct PairView {
    F ua, va, ub, vb, dist;
};
bool pair_view(const Car& a, const Car& b, PairView& out);
// 0x1EBF68: the relative-position code of a car at (u, v) in this car's frame, `ahead` / `behind` the combined extents, w the
// half of the widths' sum.
u8 position_code(F dist, F u, F v, F ahead, F behind, F w);
// 0x1F6B90: marks the zone cell of a car at (u, v) (2) and its neighbour (1).
void zone_cells(Car& c, F dist, F u, F v, F ahead, F behind, F w);
// 0x1EBDA0: the bumper gap to car b ahead at (u, v) and the closing speed; false when there is none (or a is a pad car in driver kind 0).
bool gap_closing(const Car& a, const Car& b, F u, F v, F& gap, F& closing);
// 0x1F6CC0: raises the contact intensity for a gap and closing speed.
void contact_intensity(Car& c, F gap, F closing);
// 0x1EBF00: the slipstream factor of a car at (x, y) in the leader's frame.
F slipstream_factor(const Car& leader, F x, F y);

// Phase 12 (0x1EB218) calls, with the car unless noted, and does nothing else: 0x1E9D48 hub_offsets_xy, 0x1E9E18 speed_ramp
// (F), 0x1E8B68 (ground queries), 0x1E91A8 terrain_factor, 0x1E9348 suspension, 0x1E96F0 attitude, 0x1E9998 bump_nudge,
// 0x1DF2B0, 0x1E0068, 0x1DEBB0, 0x1DF148, 0x1DFC60, 0x1E06D8, 0x1E0768 (F), 0x1E9DE8 hub_offsets, 0x1D8A80 body_axes,
// 0x1EA4D8 ground_frame, 0x1EA9B8, 0x1EAFC0, 0x1DF150. The suspension and body attitude ones
// [physics §4.2, §4.3]:
// 0x1D8A80: the body (or ground) axes from pitch, roll and heading: R = Rz(heading) Ry(roll) Rx(pitch), columns forward, lateral,
// up [physics §1.3].
void euler_axes(F pitch, F roll, F heading, Vec3& fwd, Vec3& lat, Vec3& up);
// The body axes from the car's attitude (0x1EB218's own call of 0x1D8A80).
void body_axes(Car& c);
// 0x1E9D48 / 0x1E9CE8: the hubs' horizontal / vertical offsets from the CoG along the body axes; 0x1E9DE8: both (z first).
void hub_offsets_xy(Car& c);
void hub_offsets_z(Car& c);
void hub_offsets(Car& c);
// 0x1E9E18: each wheel's speed ramp from its hub speed.
void speed_ramp(Car& c);
// 0x1E9010: each wheel's suspension coordinate x from the body height, hub offset, ground and road bump, scaled by s.
void suspension_coords(Car& c, F s);
// 0x1E90A0: mask with bit i set for each wheel beyond its bump stop.
u32 bump_mask(const Car& c, u32 mask);
// 0x1E91A8: the terrain factor, the suspension coordinates and the bump-stop mask.
void terrain_factor(Car& c);
// 0x1E8DD0: the damper force at speed v (clamped to +-1 m/s).
F damper_force(const Suspension& s, F v);
// 2002 Tokyo-Geneva's special kind 7 ride (ProgramData's Kind7Ride as the race's initialization converts it, Europe
// 0x1DC5B0: the travel and drop x 0.001 m, the lift and shares / 100, the speeds x 1 / 3.6 m/s).
struct Kind7Ride {
    bool on = false; // the table's byte +0 (Europe 0x1DC5A0)
    F travel, lift;
    std::array<F, 2> share;
    F drop, v0, v1;
};
// 0x1E8E58: one wheel's spring and damper step at coordinate x; returns the wheel's body force (spring + damper); the later
// builds' special kind 7 with ride (and c, i: its car and wheel) adds kind7_ride_force to its damper force.
F wheel_suspension(Wheel& w, const Suspension& s, F inv_dt, F x, const Kind7Ride* ride = nullptr, const Car* c = nullptr,
                   int i = 0);
// Europe 0x1DC6E0, in wheel_suspension of a car of special kind 7 (on its damper force): from the coordinate x less the
// static one, lowered by the drop between the speeds v0 and v1, a lift below it (lift / the axle's share x the axle's sprung
// weight / 2, in full past -travel), less the spring rate x the drop.
F kind7_ride_force(const Car& c, int i, F x, const Kind7Ride& r);
// 0x1E8FD0: an axle's anti-roll force from its two wheels' clamped coordinates.
F anti_roll(F x_left, F x_right, const Suspension& s);
// 0x1E9218 / 0x1E92A8: wheel i's visual camber and hub half-track at the travel (0x1E92A8 ignores the steer).
F wheel_camber(const Params& p, int i, F steer, F travel);
F hub_half_track(const Behaviour& b, const Params& p, int i, F travel); // (b: the build's atan2f)
// 0x1E9348: every wheel's suspension step, visual camber and half-track, then the per-wheel body forces.
void suspension(Car& c, const Kind7Ride* ride = nullptr);
// 0x1E9640: the longitudinal tyre forces' pitch acceleration a and heave force b.
void pitch_terms(const Car& c, F& a, F& b);
// 0x1E90F8: the spawn hold's height offset for counter n and its mode (0 none, 1 rising, 2 held and turning to the pit
// box's heading). PAL counts 250 steps where NTSC counts 300 (rising from 225, held from 25).
F spawn_offset(s32 n, s32& mode, bool pal_rate = false);
// 0x1E9160: the car at or below the ground + offset snaps to it with no vertical speed, pitch or roll; false when above.
bool spawn_settle(Car& c, F offset);
// 0x1E9528: the spawn / stop hold; true when it held the car (the attitude step is skipped). box_heading: the car's pit
// box's heading (pit_box_heading, race_inputs.h), read only in mode 2.
bool spawn_hold(Car& c, F box_heading);
// 0x1E96F0: pitch, roll and heave from the body and damper forces (after the spawn hold).
void attitude(Car& c, F box_heading);
// 0x1E9998: the bump-stop attitude nudge and the downward speed limit.
void bump_nudge(Car& c);
// 0x1EA4D8: the ground under the CoG, the ground pitch and roll and the ground frame.
void ground_frame(Car& c);
// The per-surface road bump tables (16 rows, by surface index) the ground contact reads, built at race start by 0x1DAE70
// from the program's static surface block 0x2D28B0 [physics §3.6] (surface_bumps in race_inputs.h), and the soft materials.
struct SurfaceBumps {
    std::array<F, 16> amp;    // 0x2D2D30 bump amplitude (m)
    std::array<F, 16> freq;   // 0x2D2D70 2 pi / wavelength
    std::array<s8, 16> scaled; // 0x2D2DB0 nonzero: the amplitude is scaled by the hub speed ramp
    std::array<s8, 16> mode;   // 0x2D2DC0 wave mode (1: signed square)
    std::array<u32, 16> soft{}; // 0x2D53D8 by material byte: 1 for the soft materials (grass, gravel, sand)
};
// What the ground contact counts for the conductor's course-out hook (its virtual 40, called with these right after: a
// no-op in single races, the course-out rule of the base conductor in time trials and licences [physics §11]).
struct GroundCounts {
    int flagged = 0; // wheels on a pit-lane triangle (flags bit 0)
    int soft = 0;    // wheels on a soft material (grass, gravel, sand)
};
// Phase 12, 0x1E8B68: each wheel's query point (body position + hub offset, in the course frame), the course's ground query
// for all four (CourseGround::query, 0x18FB58), then per wheel that found the road: its ground height, surface index,
// triangle flags and road bump; the road segment hint (wheel 0's segment); F +0x535 bit 4 (on the pit-lane surface) when a
// wheel is on a pit-lane triangle and the conductor allows it (`mark_flagged`: its virtual 39; 1 in single races, 0 in time
// trials, licences, free runs) [physics §1.3].
GroundCounts ground_contact(Car& c, const CourseGround& course, const SurfaceBumps& sb, bool mark_flagged);
// The base conductor's course-out rule (its virtual 40, called by the ground contact with the counts: time trial, licence,
// free run and machine test; the other conductors' virtual 40 is empty) [physics §11]: with all four wheels on pit-lane
// triangles while the lap progress F +0x3E4 is inside the pit stretch, the answer `pit` (virtual 37); with all four on soft
// materials, `soft` (virtual 38); the larger counts the course-out ticks up (1) or sets them to 0xFF (2), 0 clears them, and
// any sets the course-out flag. The pit stretch (0x1FA5B0) is the AI course's game line 4 (the pit stop line): from its
// point 1's lap distance to its point n - 2's, the progress taken a lap on when it is below half a lap; none when the line
// has fewer than 6 points.
struct CourseOut {
    bool on = false;      // the conductor has the rule
    s32 pit = 0, soft = 0; // virtuals 37 and 38
    bool stretch = false; // the course has the pit stretch
    F lap, lo, hi;        // the lap length (road +0x18) and the stretch's ends (line 4 points 1 and n - 2, +0x14)
};
// The rule (0x1DA020) on the counts ground_contact returned; nothing while the course-out flag is set or the car is not
// under control source 0.
void course_out(Car& c, const CourseOut& r, const GroundCounts& n);
// 0x1FA5B0: the progress p inside the pit stretch.
bool in_pit_stretch(const CourseOut& r, F p);
// The counts of the wheels whose ground records found the road (what ground_contact returns).
GroundCounts ground_counts(const Car& c, const SurfaceBumps& sb);
// 0x1E8A48: the surface index of a ground record [physics §3.6]: the material, or with flags bit 1 the alternate surface.
int surface_index(const Ground& g);
// 0x1E8AC0: the ground record's material is soft (0x2D53D8).
bool soft_material(const Ground& g, const SurfaceBumps& sb);
// 0x1E8AE0: the road bump wave at course (x, z): sinf(freq x) + cosf(freq z), mode 1 its signed square.
F bump_wave(int mode, F x, F z, F freq);
// A wheel's tyre effect request (0x3C bytes): what the course's tyre smoke / dust emitter (0x18FDE0 -> 0x1A46C8, on the
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
    // Not in the request: the wheel's ground record's flags byte (+0x33), which the later builds' emitter reads beside it: a
    // flag 0x20 asks the leaf maker too (leaves.h).
    u8 ground_flags = 0;
};
// Phase 12's last step, 0x1DF150: the four wheels' tyre effect requests, in wheel order. The emitter changes only the
// course's effects (and draws the VU0 random register R, 0x245D5C, six times per particle), not the car record.
std::array<TyreEffect, 4> tyre_effect_requests(const Car& c);
// Phase 12's speeds, wheel spin and effects. The constants the program builds at start-up from its static blocks come in as
// inputs.
// 0x1DF2B0: the pad's vibration level (car +0x16D5) from the front tyres' drive and brake forces, the dampers, kerbs under
// the front wheels and the last wall hit.
void pad_rumble(Car& c);

// The force feedback's parameters (built by 0x1DFF40 at race start from the static block 0x2D4628 + 0x34 ..).
struct SteerFeel {
    F align_gain;    // $gp 0x352934: the front tyres' aligning term
    F lat_gain;      // 0x352938: the front lateral forces
    F pad_gain;      // 0x35293C: the pad's steering movement
    F steer_gain;    // 0x352940: the steering command (clamped to +-steer_max)
    F steer_max;     // 0x352944
    F slip_gain;     // 0x352948: the front slip angles (clamped to +-slip_max)
    F slip_max;      // 0x35294C
    u8 use_x420 = 0; // 0x2D465F: scale the lateral term by F +0x420
    u8 mode = 0;     // 0x2D4661: 0 as is, 1 mean of two steps, 2 the larger of two steps, 3 a decaying peak
    PedalCurve curve; // 0x49D050: the odd curve (0x1D92E0) on the lateral term: type 1 quadratic, 2 square root, else identity
};
// 0x1E0068: the steering wheel's force (car +0x16D6) and its filter state (F +0x514 / +0x518).
void steer_force(Car& c, const SteerFeel& s);

// One effect channel of a surface class (0x84 bytes at 0x49C9B0 / 0x49CBC0 / 0x49CDD0 + 0x84 class, built by 0x1DE830).
struct EffectCurves {
    Table<8> a, b;  // +0x00 on the lateral speed or the slip angle, +0x4C on the slip speed or the slip ratio
    F base, r0, g;  // +0x58 / +0x5C / +0x60: base, and the speed ramp's threshold and gain
};
struct EffectTables {
    // [channel][class]: channel 0 the slip intensity (+0x30), 1 the skid pitch (+0x34), 2 the smoke emission (+0x2C).
    std::array<std::array<EffectCurves, 3>, 3> set;
    // The curves' inputs (the tyre effect block 0x2D35B8 + 0xA0 channel + 0x28 class, +0x00 / +0x1F): a non-zero by_lat feeds curve a the
    // lateral speed (else the slip angle), a non-zero by_slip feeds curve b the slip speed (else the slip ratio).
    std::array<std::array<u8, 3>, 3> by_lat{}, by_slip{};
    F kerb_scale, kerb_x0, kerb_y0, kerb_x1, kerb_y1; // 0x49CFC0 ..: kerb factor, skid pitch ramp on |rim speed|
};
// 0x1DEBB0: each wheel's slip intensity, skid pitch and smoke emission, and the car's dirt (condition word 3) from the
// emission on loose and wet surfaces. course_kind: the course's category byte (0x1F2F68's input; 0xFF reads as 0).
void tyre_effects(Car& c, const EffectTables& t, u8 course_kind);
// 0x1DF148: returns at once.
void phase12_stub(Car& c);
// 0x1DFC60: the engine sound levels F +0x4E4 .. +0x4E8 (scales: 0x49CFD8 + 0x64 .. + 0x70, built by 0x1DF4C0).
void engine_sound(Car& c, const std::array<F, 4>& scales);
// 0x1E0668, the car's draw mode: 3 for a car that is not a ghost; a ghost: 1 (not drawn) while both words are 0, else 0 (not
// drawn) when it is CPU-controlled or driver kind 1, else 2 (translucent) [presentation.md]. x70: the conductor's +0x70
// ([car +0] + 0x70); ghost_switch: the race's ghost switch +0x217A4 ([[car +0] + 0x3C] + 0x217A4, read only when x70 is 0).
int draw_mode(const Car& c, u32 x70, u32 ghost_switch);
// 0x1E06D8: for a ghost car (draw mode not 3), clears the tyre effects (pitch 1) and the engine sound levels.
void effects_reset(Car& c, u32 x70, u32 ghost_switch);
// 0x1E0768: the lagging heading and pitch (a spring-damper follower each) and their differences F +0x4F8 / +0x4FC.
void view_follow(Car& c);

// 0x1EA6B0 (+ 0x1EA5F8): forward / lateral speed along the ground frame and each hub's velocity [physics §10.1].
void hub_velocities(Car& c);
// 0x1D9BB0: adds x (metres) to the odometer in 2 m units; returns the new carry.
F odometer_add(u32& odometer, F x);
// 0x1EA9B8: speeds, odometer, engine wear and oil (at most k.e3 / k.omax), top speed, speedometer, free-rolling wheels
// [physics §10.1-§10.3].
void speed_bookkeeping(Car& c, const Condition& k);
// 0x1EAFC0: heading wrap, each wheel's spin angle (W +0x38), the drawn car's points and the steering view value.
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
    const Kind7Ride* ride = nullptr;       // 2002 Tokyo-Geneva's special kind 7 ride (null: none)
};
// What phase 12 hands on: the counts for the conductor's course-out hook, the tyre effect requests for the emitter.
struct ChassisOutputs {
    GroundCounts counts;
    std::array<TyreEffect, 4> effects;
};
// Phase 12 (0x1EB218), the whole phase: its calls in its order.
ChassisOutputs chassis(Car& c, const ChassisInputs& in);

// Phase 13 (0x1E8190): tyre wear from slip intensity, load and compound [physics §8.2].
void tire_wear(Car& c, const WearParams& cp);
// What the conductor answers for the wear flags: virtual 39 (the mode has pit stops), virtual 35 (0x1D9FE0's code, asked
// only for a car with control source 0) and the race-flow byte conductor + 0x8000 + 0x37AC.
struct PitMode {
    bool pits = false;
    s32 mode = 0;
    u8 kind = 0; // the race-flow byte
};
// Phase 13 (0x1E8340): the "tyres worn" flag and the pit request (F +0x535 bits 2 and 3) [physics §8.7].
void wear_flags(Car& c, const WearParams& cp, const PitMode& m);
// 0x245D5C: the VU0 random register's next value, 1.m - 1 in [0, 1) (base/game_random.h).
F vu0_random(Vu0Random& rng);
// Phase 13 (0x1E6A38): the wheels' flap pendulums, drawing from the VU0 random register [physics §8.8].
void flaps(Car& c, Vu0Random& rng);

// The AI's lines and speed plan [ai §2-§4]: the AI driver object's (Car::ai) point bookkeeping, corner speeds, braking point,
// target point and speed controller on the AI course object k (ai_course.h; the driver's +0x08). cp is the conductor's
// (the driver's +0x00) wear curve: the corner speeds take the wheels' wear grip while its max is not 0.
// 0x1F79A0: the start of the AI's tick: the outputs cleared (the steering to the current command), the line blend's weight
// down by dt (not below 0).
void ai_begin(Car& c);
// 0x1F6678: the corner speed of the arc from point i of the driver's line [ai §4.2].
F ai_corner_speed(const Car& c, const AiCourse& k, const WearParams& cp, int i);
// 0x1F6958: the next point index changed: the next segment's kind and target become the current ones, and the new next
// segment's are planned (point next, the last point past the end; kinds 3 and up keep the target) [ai §4.1].
void ai_next_segment(Car& c, const AiCourse& k, const WearParams& cp);
// 0x1F6A20: re-plan: the next point from a search of the race distance, the segment before it planned, then
// ai_next_segment.
void ai_replan(Car& c, const AiCourse& k, const WearParams& cp);
// 0x1F8958: the line at once (the blend's old line too, weight 0; `line` already resolved), then ai_replan.
void ai_set_line(Car& c, const AiCourse& k, const WearParams& cp, int line);
// 0x1F8980: a line request (the fallback applies): blended (old line := current, weight 1) or at once; false when no line
// of the request's fallback is present (nothing changes).
bool ai_request_line(Car& c, const AiCourse& k, const WearParams& cp, int line, bool at_once);
// 0x1F7088: the status cleared; except on the stop line (line 4): the next point steps (ai_next_segment on a change), then
// in a braking mode the status 2, else the braking distance to the next arc and the braking zone (mode 3 / 4, status 2),
// the soft zone and status 1 [ai §4.3]. Returns the throttle multiplier: the soft zone's factor times the event's
// throttle scale (not on the pit lane, line 3; 1 on the stop line).
F ai_braking(Car& c, const AiCourse& k, const WearParams& cp, StopZone* zone = nullptr, const CourseQuery* course = nullptr);
// 0x1F7948: the look-ahead distance at forward speed v [ai §3] (the driver's look_min, look_max and look_speed).
F ai_lookahead(const Driver& d, F v);
// 0x1F7658: the target point of line `line` at the car's race distance + look (the stop line 4 by its own evaluation),
// the look-ahead growing by 1 m while the target is nearer than 5 m (at most 9 times); (dx, dy) the target less the car's
// position. Returns the look-ahead used. `missed` (optional) is set when a line evaluation found no span (the program then
// keeps whatever its stack held; here the last values, 0 at first).
F ai_target(const Car& c, const AiCourse& k, int line, int hint, F look, F& dx, F& dy, bool* missed = nullptr);
// 0x1F7520: the speed controller's throttle base + gain (v - vt), the speed difference clamped to +-10 km/h, the result
// to [0, 1].
F ai_throttle_for(F v, F vt, F base, F gain, bool toward_zero = false); // (2002 Tokyo-Geneva's constants)
// 0x1F7590: the throttle towards target speed vt (aiming 2 km/h below it, base 0.75), the brake at 1 when the throttle is 0
// and the car is faster, the gearbox lock-out (12 ticks) at a part throttle [ai §4.4].
void ai_speed_control(Car& c, F vt, F gain);
// The AI's decisions' plan calls (AiPlan) on these ports: the AI course object k and the conductor's wear curve cp.
class CoursePlan final : public AiPlan {
public:
    CoursePlan(const AiCourse& k, const WearParams& cp, StopZone* zone = nullptr, const CourseQuery* course = nullptr)
        : k_(k), cp_(cp), zone_(zone), course_(course) {}
    StopZone* stop_zone() override { return zone_; }
    F braking(Car& c) override;
    F target(const Car& c, int line, int hint, F look, F& dx, F& dy) override;
    F line_heading(const Car& c, int line, int hint, F s) override;
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
    StopZone* zone_;            // (the later builds')
    const CourseQuery* course_; // ... and the course, for a stop point's race distance
};

// The race step 0x1F39D8's vehicle phases, phase-major over the cars as the step runs them [physics §1.2]: phases 1-9,
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
    CourseOut course_out;      // phase 12 (the conductor's virtual 40)
    LaterInputs later;         // the later builds' special cars (special.h); unused under GT3's rules
    ClassGrip class_grip{};    // phase 7 (2002 Tokyo-Geneva's): the rule block's +0x18 .. +0x1B
    Kind7Ride ride;            // phase 12 (2002 Tokyo-Geneva's special kind 7)
};
// Whether the race's build has the later builds' own routines of the race step (Rule::HybridStep and the three after it).
inline bool later_step(const Car& c) { return !gt3_program(c.behaviour.rule(Rule::HybridStep)); }
// The part of the step that is not ported, run in its place: `emit` for the tyre effect emitter 0x1A46C8 that 0x1DF150
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

// The race step's own parts around the vehicle phases (0x1F39D8) that the physics reads back: the race-flow gate before
// phase 1 and the rubber band after the race flow's hooks [physics §1.1; ai §9.3].

// The step's start: while the race-flow byte (conductor + 0x8000 + 0x37AC, the initialisation's argument) is set, each car's
// accumulator (car +0xEC4) loses the conductor's time scale (+0xB7B0, 1 from virtual 21); the step runs (true) when one went
// below 0, which then gains 1. With the byte clear it always runs.
bool race_flow_gate(std::span<Car> cars, u8 flow, F scale);

// The rubber band's parameters (conductor + 0xAC .. + 0xC4, set by 0x1ECBA8 at the race's initialisation).
struct RubberBand {
    F slow, slow_start, slow_full; // +0xAC / +0xB0 / +0xB4 the slow-down: amount (fraction), the gaps it starts and is full at (m)
    F fast, fast_start, fast_full; // +0xB8 / +0xBC / +0xC0 the speed-up
    F global;                      // +0xC4 every scale's factor
};
// 0x1ED018: the car's lap count less its base.
s32 lap_count(const Car& c);
// 0x1ED028: car's distance behind ref: whole laps and metres (the progress difference, a lap taken over when the laps and
// the metres disagree in sign). lap: the course's lap length.
void race_gap(const Car& ref, const Car& car, F lap, s32& laps, F& gap);
// 0x1ECDA8: the speed-up of a car behind (laps, gap); 1 on the pit-lane surface.
F speed_up(const RubberBand& k, const Car& car, s32 laps, F gap);
// 0x1ECE40: the slow-down of a car ahead (laps and gap negated).
F slow_down(const RubberBand& k, s32 laps, F gap);
// The conductor's virtual 4 (0x1ECEB8): car's time scale F +0x524 against ref, times `scale` (and the global factor); returns
// it before the global factor. A finished car keeps `scale`. cap: the i.LINK battle's version (0x1F0478), which caps the
// result at 1 + the speed-up amount. PAL caps every mode at 1.3 instead (and its i.LINK battle adds none).
F band_scale(const RubberBand& k, const Car& ref, Car& car, F lap, F scale, bool cap);
// The pass after the race flow's hooks, by the race mode (the conductor's virtual 32: the race configuration's +0x22 in single
// races and battles, -1 elsewhere): the reference is the first car of the ranking order (conductor + 0xE388) that the AI
// does not drive (control source not 2; none: car 0). Mode 1: every other car against it. Modes 0 and 2: from the reference's
// rank r (F +0x4E1) outwards, each car against its neighbour in the order (mode 0 chaining the scales). Other modes: none.
void rubber_band(std::span<Car> cars, std::span<const s32> order, s32 mode, const RubberBand& k, F lap, bool cap);

} // namespace gt3r::sim::exact
