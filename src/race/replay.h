#pragma once

// Replays: the race on the exact core (sim/exact/race_record.h: its setup, every frame's pad frames and the game's changes
// between frames), played again bit for bit, with what the theater and the memory-card pages show (course, car, kind,
// laps, times) and the players' inputs per tick for the HUD.
//
// File format (little endian), ".gt3replay" in <user data>/replays/:
//   0x00 "GT3RRPL1"
//   u32 version (12; files of older versions, and version 11 files without a race record, are refused: ReplayTooOld),
//   u32 tick count N, f64 tick rate (Hz)
//   string course, string car (u16 length + bytes), u8 automatic, u8 paint
//   pose start (f32 x, y, z, qw, qx, qy, qz)
//   u32 event count; events {u32 tick, u8 kind, pose} (written 0; read and ignored)
//   f64 best lap time (< 0 = none), u32 best lap first tick, u32 best lap end tick
//   f32 final x, y, z (position after the last tick, for verification)
//   v2: string event (race with opponents; empty = time trial), u16 count + strings opponents
//   v3: string car2 (2P battle; empty = none), then (with car2) N x input for player 2 after
//       player 1's inputs.
//   v4: f32 start speed (m/s; rolling / flying starts, 0 = from rest), after car2
//   v6: u32 paint seed (the race generator's state before the paint draw), after the start speed
//   N x input {s16 steer * 32767, u16 throttle * 65535, u16 brake * 65535, u8 flags}
//        flags: 1 handbrake, 2 shift up, 4 shift down, 8 automatic,
//        v5: 16 controller kind 2 (analog), 32 kind 3 (digital steering); neither = 0
//        v10: then u8 flags2: 1 the reverse request (the back input at least the throttle; the
//        shift logic's, physics §2.6); the same for player 2's inputs
//   v7 (after the v6 paint seed, before the inputs): string comment (the file name the
//       memory-card pages show and edit), u8 kind (ReplayKind), u16 laps completed, f64 total
//       time (< 0 = none), f32 power (PS), string car model, string wheel, s64 saved (unix
//       seconds, 0 = unknown), u32 n + n bytes car spec (written 0; read and skipped)
//   v8 (after the v7 car spec): the engine condition the run started with (physics §10.3):
//       u32 odometer (2 m units), f32 running-in / wear, f32 oil (older replays: a fresh engine)
//   v9 (after the v8 condition): u8 the field's rules as the run had them: bits 0-1 the grid
//       by power-to-weight (GT mode), bits 2-3 0; each 0 = not recorded, 1 = off, 2 = on. Then
//       with car2, the 2P battle: u8 recorded (0 = no, nothing follows); u16 laps, f32 catch-up
//       (0 off, 0.15 low, 0.20 high), f32 x2 0, f32 handicap (m), u8 tyre damage, string model2,
//       string wheel2, u8 paint2, u32 n + n bytes (written 0; read and skipped)
//   v11 (after the v9 battle block, before the inputs): u32 n + n bytes, the race on the exact
//       core (sim/exact/race_record.h: its setup, every car's race entry, every frame's pad frames
//       and the game's changes between frames, the race's hash at the end). Playback builds the
//       same race from it and steps it from the same frames, so it runs again bit for bit; the
//       inputs above are for the HUD only.
//   v12: the same layout; races on the EE's measured multiplier and divider (a v11 race, run on
//       the earlier rounding, would play differently: refused).

#include "base/game_random.h"
#include "race/backend.h"
#include "sim/sim_types.h"

#include <memory>

#include <optional>
#include <string>
#include <vector>

namespace gt3r::sim::exact {
struct RaceRecord;
}

namespace gt3r::race {

// A file of a replay version before 12 (11: run on the earlier float rounding; older ones, and 11 without a race record, not on
// the exact core): refused, not played approximately.
struct ReplayTooOld : FormatError {
    using FormatError::FormatError;
};
inline constexpr const char* kReplayTooOld = "This replay was recorded with an older version and cannot be played.";

// The value the simulation sees after a save/load round trip.
sim::ControlInputs quantize(const sim::ControlInputs& in);

// v9, 2P battle: the battle as its run had it (the Options menu's laps, catch-up boost,
// handicap and tyre damage; player 2's car from its car page). Playback builds the battle with
// these, not with the Options menu's settings at the time it is watched.
struct ReplayBattle {
    int laps = 0;
    int catchup = 0;          // the catch-up for the trailing player: 0 off, 1 low, 2 high (FieldConfig::battle_catchup)
    float handicap_m = 0;     // FieldConfig::handicap_m
    bool tyre_damage = false; // tyre wear on both cars (FieldConfig::battle_wear)
    std::string model2, wheel2; // player 2's model / fitted wheel overrides ("" = its own)
    int paint2 = 1;
};

// What kind of run a replay holds (the memory-card info panel's "Mode").
enum class ReplayKind : u8 {
    Unknown = 0,
    TimeTrial = 1,  // our time trial / arcade time trial or free run
    ArcadeRace = 2, // arcade single race
    TwoPlayer = 3,  // 2 player battle
    GtRace = 4,     // GT mode event / championship round
    License = 5,    // licence test
    Practice = 6,   // GT mode free run / machine test, quick race practice
    Online = 7,
};
const char* replay_kind_name(ReplayKind k); // "Arcade", "GT", "License", ...

struct Replay {
    static constexpr u32 kVersion = 12;
    std::string course, car; // database labels
    bool automatic = true;
    int paint = 0;
    double tick_hz = sim::kDefaultTickHz;
    TrackPose start;
    std::vector<sim::ControlInputs> inputs; // one per tick (already quantised)
    // Player 1's driver kind per tick (RaceVehicle::driver_kind: 0 the driver, 1 the AI drives, 5 the stop driver (kinds
    // 5-7), 8 parked; empty = all 0). Stored in input flag bits 64 (1), 128 (5) and both (8).
    std::vector<u8> driver_kind;
    // v10: the players' reverse requests per tick (1 = requested; empty = none, as before v10).
    std::vector<u8> reverse, reverse2;
    double best_lap = -1;
    u32 best_lap_begin = 0, best_lap_end = 0; // tick range [begin, end) of the best lap
    sim::Vec3 final_position;
    std::string event;                  // race with opponents (event label); empty = time trial
    std::vector<std::string> opponents; // opponent car labels, grid order
    std::string car2;                    // 2P battle: player 2's car label
    std::vector<sim::ControlInputs> inputs2; // player 2's inputs (same length as inputs)
    float start_speed = 0;               // m/s: rolling / flying start (0 = from rest)
    u32 paint_seed = GameRandom::kBootSeed; // race_info_random() before the paint draw (older replays: the boot seed)
    // v7: what the theater and the memory-card pages show, and the car the run used.
    std::string comment;                 // file name ("" = none given)
    ReplayKind kind = ReplayKind::Unknown;
    int laps = 0;                        // laps completed by player 1
    double total_time = -1;              // race / run time at the end (s), < 0 = none
    float power_ps = 0;                  // the car's power
    std::string car_model, wheel;        // model / fitted wheel overrides ("" = the car's own)
    s64 saved = 0;                       // when it was saved (unix seconds)
    sim::EngineCondition condition;      // v8: player 1's engine condition at the start (zeros = fresh)
    // v9: the field's rules as the run had them (nullopt: not recorded).
    std::optional<bool> grid_by_power;   // FieldConfig::grid_by_power
    std::optional<ReplayBattle> battle;  // v9 with car2 (nullopt: not recorded)
    // v11: the race on the exact core. Playback runs it again from its own setup and frames
    // (race::ExactFieldOptions::playback); a parsed replay always has one.
    std::shared_ptr<const sim::exact::RaceRecord> exact;

    u32 ticks() const { return u32(inputs.size()); }
    double duration() const { return double(inputs.size()) / tick_hz; }
    // The run-up of a rolling / flying start (physics §9.4): the leading ticks the computer
    // drove, driver kind 1 from tick 0 to the hand-over (0 = none).
    u32 runup_ticks() const;

    Bytes serialize() const;
    static Replay parse(ByteSpan data); // throws FormatError (ReplayTooOld: before 11, or no race record)
};

// Records a run tick by tick.
class ReplayRecorder {
public:
    void begin(std::string course, std::string car, bool automatic, int paint, const TrackPose& start, double hz,
               std::string event = {}, std::vector<std::string> opponents = {});
    // Returns the quantised inputs to feed to the simulation for this tick.
    sim::ControlInputs record(const sim::ControlInputs& in);
    // Replaces this tick's recorded input (the computer took over after record());
    // returns the quantised value.
    sim::ControlInputs replace_last(const sim::ControlInputs& in) {
        sim::ControlInputs q = quantize(in);
        if (active_ && !replay_.inputs.empty()) replay_.inputs.back() = q;
        return q;
    }
    // 2P: player 2's inputs for the same tick (call before record()).
    sim::ControlInputs record_second(const sim::ControlInputs& in);
    // Replaces this tick's recorded input of player 2; returns the quantised value.
    sim::ControlInputs replace_last_second(const sim::ControlInputs& in) {
        sim::ControlInputs q = quantize(in);
        if (active_ && !replay_.inputs2.empty()) replay_.inputs2.back() = q;
        return q;
    }
    // This tick's driver kind (after record()); only 0, 1, 5 and 8 are stored.
    void set_driver_kind(int kind);
    // This tick's reverse request of player 1 (after record()) and player 2 (after record_second()).
    void set_reverse(bool on);
    void set_second_reverse(bool on);
    void set_second_car(std::string car2) { replay_.car2 = std::move(car2); }
    void set_start_speed(float mps) { replay_.start_speed = mps; }
    void set_paint_seed(u32 seed) { replay_.paint_seed = seed; }
    void set_engine_condition(const sim::EngineCondition& c) { replay_.condition = c; }
    void lap(double time, u32 begin_tick, u32 end_tick); // a completed lap; keeps the best
    void finish(const sim::Vec3& final_position) { replay_.final_position = final_position; }
    // Puts a finished recording back (e.g. after a demonstration run); recording stops.
    void assign(Replay r) { replay_ = std::move(r); active_ = false; }
    u32 tick() const { return replay_.ticks(); }
    const Replay& replay() const { return replay_; }
    bool active() const { return active_; }

private:
    Replay replay_;
    bool active_ = false;
};

// Steps through a replay's inputs (the HUD's; the race plays from its record).
class ReplayPlayer {
public:
    explicit ReplayPlayer(const Replay* r = nullptr) : r_(r) {}
    void reset() { tick_ = 0; }
    // On to tick t (the ticks between not played: the Analyzer's REPLAY jump).
    void seek(u32 t) { tick_ = t; }
    bool done() const { return !r_ || tick_ >= r_->ticks(); }
    u32 tick() const { return tick_; }
    sim::ControlInputs next_inputs(); // advances one tick
    // 2P: player 2's inputs for the tick the last next_inputs() returned.
    sim::ControlInputs second_inputs() const;
    // The driver kind recorded for that tick.
    int driver_kind() const;

private:
    const Replay* r_;
    u32 tick_ = 0;
};

// Saving/loading under <dir> (created on demand). save() returns the file path.
std::optional<std::string> save_replay(const Replay& r, const std::string& dir);
struct ReplayFile {
    std::string path, name; // name = file name without directory
};
std::vector<ReplayFile> list_replays(const std::string& dir); // newest first

} // namespace gt3r::race
