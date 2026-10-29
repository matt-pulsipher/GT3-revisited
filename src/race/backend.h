#pragma once

// The race scene talks to the course and the player's car only through these small interfaces: RaceTrack (the course's
// track runtime, track::Track: its surfaces, start grid, centre line and lap timer) and RaceVehicle (the player's car of a
// race on the exact core, race/exact_field.h).

#include "formats/model/car_model.h"
#include "input/virtual_pad.h"
#include "sim/car_spec.h"
#include "sim/sim_types.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gt3r {
class Vfs;
namespace data {
class CarDatabase;
}
namespace track {
class Track;
}
} // namespace gt3r

namespace gt3r::race {

struct TrackPose {
    sim::Vec3 position;
    sim::Quat orientation;
};

// A split-point or line crossing of a car's lap timer (track::LapTimer::Crossing;
// presentation §D6).
struct LapCrossing {
    int point = 0;          // split point index; at the line, the points passed this lap
    bool line = false;      // a completed lap
    int lap = 0;            // laps completed before it
    long long ms = 0;       // race clock (ms)
    long long split_ms = 0; // since the lap's start (at the line: the lap time)
};

// A race that times the player's laps itself (the exact core's race flow): the track's lap queries answer from it
// (RaceTrack::set_lap_source) instead of the track's own lap timer.
class LapSource {
public:
    virtual ~LapSource() = default;
    virtual bool lap_completed() const = 0; // the last step completed a lap
    virtual int laps_completed() const = 0;
    virtual bool lap_started() const = 0;
    virtual double current_lap_time(double now) const = 0;
    virtual double last_lap_time() const = 0;
    virtual double best_lap_time() const = 0;
    virtual float lap_progress() const = 0;
    virtual bool wrong_way() const = 0;
    virtual std::vector<LapCrossing> lap_crossings() const = 0;
    virtual double race_distance() const = 0;
};

// Track runtime as seen by the race scene.
class RaceTrack {
public:
    // The lap queries from `src` while it is set (nullptr: the track's own lap timer).
    virtual void set_lap_source(const LapSource* src) { (void)src; }
    virtual ~RaceTrack() = default;
    virtual const char* backend_name() const = 0;
    virtual const sim::TrackQuery& query() const = 0;
    virtual float length() const = 0;

    virtual int grid_count() const = 0;
    virtual TrackPose grid_pose(int slot) const = 0;
    // Centre-line point at track distance s (wrapped), `lateral` metres to the left.
    virtual sim::Vec3 point_at(float s, float lateral = 0) const = 0;
    // Track distance of the centre-line point nearest to p.
    virtual float distance_of(const sim::Vec3& p) const = 0;
    // The track runtime behind it (its road, AI lines and walls).
    virtual const track::Track* runtime() const { return nullptr; }
    // Player's race distance (laps * length + distance), for positions.
    virtual double race_distance() const = 0;

    // Surface classes for the Modern rumble (race::RumbleModel).
    virtual bool is_kerb(u32 surface) const = 0;
    virtual bool is_offroad(const sim::SurfaceHit& hit) const = 0;

    // Lap timing for the player's car.
    virtual void lap_reset(double time) = 0;
    virtual bool lap_update(const sim::Vec3& p, double time) = 0; // true when a lap was completed
    virtual int laps_completed() const = 0;
    virtual bool lap_started() const = 0;
    virtual double current_lap_time(double now) const = 0;
    virtual double last_lap_time() const = 0; // < 0 when none
    virtual double best_lap_time() const = 0; // < 0 when none
    virtual float lap_progress() const = 0;   // 0..1
    virtual bool wrong_way() const = 0;
    // track::LapTimer::set_lap1_from_go (races: lap 1 from GO) and freeze (after the finish).
    virtual void lap_set_lap1_from_go(bool from_go, bool before_line) { (void)from_go, (void)before_line; }
    virtual void lap_freeze() {}
    // The player's split-point / line crossings of the last lap_update().
    virtual std::vector<LapCrossing> lap_crossings() const { return {}; }
    // The course's split points (track distances), as the lap timer uses them.
    virtual std::vector<float> split_points() const { return {}; }
};

// What the race scene knows about the chosen car; filled from the database.
struct CarSetup {
    std::string label;      // database car label, e.g. "r34_gtr_v_99"
    std::string name;       // display name
    std::string model;      // car model id, e.g. "ni0137"
    float limit_rpm = 7500;    // the engine's rev limit (the HUD's speedometer scale)
    std::vector<float> ratios; // forward gears
    float final_drive = 4.0f;
    WheelSpec wheels;          // placement and tyre size
    CarBodyMod body_mod;       // mod cars: stretched shell or a body of their own (paint 0)
    u16 sound_id = 0;          // engine sound bank
    u8 sound_variant = 0;
};

// Fills a CarSetup from the database (defaults where something is missing).
CarSetup car_setup_from_database(const data::CarDatabase& db, const std::string& car_label);

// What the race screen reads of a car's make-up besides its geometry (VehicleSetup): sounds, force feedback, pylons.
struct CarFacts {
    float mass = 1300;            // kg
    float front_weight = 0.55f;   // the front axle's static share
    float front_extent = 2.25f, rear_extent = 2.25f; // body ahead of / behind the car-frame origin (m)
    float width = 1.75f, length = 4.5f;
    bool turbo = false;
    std::vector<float> ratios; // forward gears (any common scale: only their proportions are read)
    float reverse_ratio = 0;
    sim::Drive drive = sim::Drive::FR;
};

// The player's car (race/exact_field.h): the car's record as the screen reads it, its own race logic (its AI drives the
// run-ups, the pit lane and the end of a run; it judges laps and finishes) and its race pad.
class RaceVehicle {
public:
    virtual ~RaceVehicle() = default;
    virtual const char* backend_name() const = 0;
    // One race step: the race pad set with set_pad (or, where the screen gives none - scripts, demos, replays of inputs - a
    // frame made from `in`).
    virtual void step(const sim::ControlInputs& in, double dt) = 0;
    virtual const sim::VehicleState& state() const = 0;
    // Wheel placement, gear count and rpm range (tach) in the car frame.
    virtual const sim::VehicleSetup& setup() const = 0;
    // Turbo boost (sim::VehicleTelemetry::boost; 0 for NA cars).
    virtual float boost() const { return 0; }
    // The brake pedal the car's last step ran with (0..1; its brake lamps).
    virtual float brake_pedal() const { return 0; }
    // The last step's telemetry (sounds, smoke, dirt, force feedback).
    virtual const sim::VehicleTelemetry* telemetry() const = 0;
    // The car's make-up.
    virtual std::optional<CarFacts> facts() const = 0;
    // The recommended-gear hint the car's race logic keeps (the AI driver object's +0xAC / +0xAD: the gear, -1 none, and
    // whether it blinks).
    virtual int gear_hint(bool& blink) const { blink = false; return -1; }
    // The driver kind the car's last step ran with (0 normal, 1 the AI drives, 2-4 the pit lane, 5-7 stopping, 8 parked).
    virtual int driver_kind() const { return 0; }
    // The race pad's state for the next step (the car reads the pad itself, as in GT3).
    virtual void set_pad(const PadState* pad) { (void)pad; }
    // The car's own AI takes the car over (a race's demonstration); false when it cannot.
    virtual bool ai_take_over() { return false; }
    // Player reverse (GT3's "back" action) for a step without a pad.
    virtual void set_reverse_request(bool on) { (void)on; }
    // Engine condition (physics §10.3): what the run starts with and, read back, what it hands on.
    virtual void set_engine_condition(const sim::EngineCondition& c) { (void)c; }
    virtual sim::EngineCondition engine_condition() const { return {}; }
    // Wall impulse of the last step (N*s; sim::VehicleTelemetry::wall_impulse), for sounds.
    virtual float wall_impulse() const { return 0; }
    // Strongest car-to-car contact impulse of the last step involving this car (N*s).
    virtual float car_impulse() const { return 0; }
    // The speed change of the last step's strongest wall or car hit (m/s), for the Modern rumble.
    virtual float last_impact() const { return 0; }
};

// The course's track runtime; throws FormatError when the course file has none.
std::unique_ptr<RaceTrack> make_track(const Vfs& vfs, ByteSpan course_file, const std::string& label);

} // namespace gt3r::race
