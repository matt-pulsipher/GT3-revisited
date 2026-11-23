#pragma once

// The two-car race on the original's race step that race_tick_exact and race_exact_lockstep use: the lanes on the course, the
// scenarios, the original's side (run_original) and its hooks.

#include "harness.h"
#include "race_setup.h"

#include "game/data/car_database.h"
#include "track/track.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gt3r::diff::race_hits {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDt = 1.0 / 60.0;
constexpr Pa kPairResponse = 0x001FC5C0;

std::string fmt(const char* f, ...);
double env_num(const char* k, double d);

struct V2 {
    double x = 0, z = 0;
    V2 operator+(V2 o) const { return {x + o.x, z + o.z}; }
    V2 operator-(V2 o) const { return {x - o.x, z - o.z}; }
    V2 operator*(double k) const { return {x * k, z * k}; }
    double dot(V2 o) const { return x * o.x + z * o.z; }
    double len() const { return std::hypot(x, z); }
};

// A straight stretch: centre-line point m, lane direction t (along the lap), right r, road height y.
struct Lane {
    V2 m, t, r;
    double y = 0, distance = 0, bend = 0;
    double side = 1;    // wall lanes: +1 = the wall is on the right of travel, -1 = on the left
    double wall_d = 0;  // wall lanes: distance of the wall line from the lane line m + t s
    bool ok = false;
};

struct Extents {
    double front = 0, rear = 0, half_w = 0;
};

struct Scenario {
    const char* name;
    double offset;  // striker CoG sideways from the target's, in widths (+ = right of the lane)
    double deg;     // striker heading towards the target's centre line
    double v_target, v_striker; // km/h
    double along;   // side swipes: striker CoG ahead of the target's by this (m); NaN = behind, rear-end
    double throttle_target, throttle_striker;
    bool wall = false; // on the wall lane: + offsets are towards the wall
    // race_tick_exact's pad scenarios (tick_scenarios()): from tick `from` (-1: never) both cars' pad frames carry this throttle,
    // brake, steering (-1..1, + = left) and handbrake; both cars start with control source `control` (F +0x3E9) and driver kind
    // `kind` (F +0x534); with `all_ascc` their own aid blocks get every ASCC channel on both axles and a TCS lateral factor (no
    // car's data has the rear A / E channels).
    int from = -1;
    double throttle = 0, brake = 0, steer = 0, handbrake = 0;
    int control = 0, kind = 0;
    bool all_ascc = false;
    // Pad scenarios (race_tick_exact): the 16-byte frame of car i at race step `tick` instead of the fixed throttle frame, record
    // writes to car i before each step (tick -1: once after the placement), and the run's length (0: the caller's).
    void (*pad)(int car, int tick, u8* frame) = nullptr;
    void (*prep)(Ee& ee, u32 car, int i, int tick) = nullptr;
    int ticks = 0;
};

struct Pose {
    V2 cog, fwd;
    double v = 0;
};

// One tick of one car, in the lane frame.
struct CarRow {
    double x = 0, z = 0;  // CoG
    double vt = 0, vr = 0; // velocity along the lane / to its right
    double hdg = 0;       // heading to the lane (rad, + = to the right)
    double yaw = 0;       // yaw rate (rad/s, + = turning right)
    double speed = 0;     // km/h
    double dt = kDt;      // per-car time step (original car+0xEC0)
    double fz = 0;        // sum of the four tyre loads (N)
    double wgap = 1e9;    // wall lanes: the outline's distance from the wall line (< 0 = through it)
    int wall_hits = 0;    // original: point-wall + wall-corner impulse calls; ours: 1 when the tick had a wall impulse
    double pitch = 0, roll = 0; // body attitude (rad)
};

struct Row {
    CarRow c[2];
    int contacts = 0;     // original: car-car responses this tick; ours: World contacts
    double impulse = 0;   // ours: |j| sum; original: the body +0x440 words
    int flagged = 0;      // original: car vertices flagged (point +0x20 bit 1) at the end of the tick
};

struct Run {
    bool ok = false;
    std::string error, init;
    std::vector<Row> rows;
};

Lane find_lane(const track::Track& trk);
Lane find_wall_lane(const track::Track& trk, double offset);
std::vector<V2> octagon(const Extents& e);
// The cars run kSettle ticks apart first (the placement drops them onto the road).
constexpr int kSettle = 90;
void start_poses(const Lane& ln, const Extents& ext, const Scenario& sc, Pose& tgt, Pose& stk);
void fill(CarRow& r, const Lane& ln, double x, double z, V2 f, V2 v, double yaw_y);
double wall_gap(const Lane& ln, const Extents& ext, double x, double z, V2 f);

// Hooks for the exact tests: the collision step's entry and exit (probes), after each tick (false ends the run early), and
// more probes installed after the race is set up.
extern std::function<void(Ee&, bool)> g_step_probe;
extern std::function<bool(Ee&, int)> g_after_tick;
extern std::function<void(Ee&)> g_install;
// Set: run_original keeps the race display's time events (0x1EC520 / 0x1EC620; else stubbed out).
extern bool g_race_display;
// The pad frame each car reads in run_original (all analog, kind 2, the scenario's throttle at +8); g_after_tick may change it
// for the next tick.
extern u8 g_frame[2][16];
extern u32 g_car[2]; // the two cars' records in run_original's race

Run run_original(Ee& ee, const data::CarDatabase& db, const Bytes& gtar, const data::Event& ev, ByteSpan course_file,
                 const std::string& car, const track::Track& trk, const Lane& ln, const Scenario& sc, int ticks, Extents& ext_out);

const std::vector<Scenario>& car_hit_scenarios();
// race_tick_exact's extra pad scenarios on the plain lane: braking, steering, the handbrake, wheelspin, AI driving.
const std::vector<Scenario>& tick_scenarios();
// race_tick_exact's pad scenarios on the open lane: steering sweeps both ways and to full lock, digital and analog axes, every
// controller kind, steering-wheel types, pedal styles, braking to a stop and reversing, gear requests, the hold counter.
const std::vector<Scenario>& pad_scenarios();
// The frames of race_tick_exact's CPU races with car 0 on the pad: 4 s of the analog sweeps, then 4 s of a steering wheel, in turn.
void pad_race_frame(int tick, u8* frame);

// What both race tests need: the database, the event (GT3R_CARHIT_EVENT, default the first G* trialmt event), its course, the
// car (GT3R_CARHIT_CAR) and the two lanes.
struct HitSetup {
    data::CarDatabase db;
    Bytes gtar, course_file;
    const data::Event* ev = nullptr;
    std::unique_ptr<track::Track> trk;
    std::string car;
    Lane ln, wl;
};
bool load_hit_setup(DiffContext& ctx, HitSetup& h);

} // namespace gt3r::diff::race_hits
