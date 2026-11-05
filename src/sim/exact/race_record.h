#pragma once

// A race on the exact core as a replay keeps it: what the race was built from (RaceSetup without its pointers: the race
// configuration block, the course by its label, every car's race entry as it was) and what each of its frames was given (every
// car's pad frame, and the game's own changes to the race between frames: an AI take-over, the pause menu's gearbox, ...). An
// ExactRace built from the record and stepped from it runs the same race again bit for bit; CPU cars need nothing recorded
// (their frames are empty and cost a few bytes). GT3's own replays work the same way: its recorder keeps the cars' pad frames
// (the disc's demo runs are such recordings, race/demo_replay.h).

#include "sim/exact/race.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gt3r::sim::exact {

// One of the game's changes to a race between two frames (made before the frame's step).
struct RaceChange {
    enum Kind : u8 {
        PedalStyle = 1,     // car's Car::pedal_style = a (the pause menu's AT / MT while a pad drives)
        Condition = 2,      // car's engine condition counters: odometer a, engine_wear bits b, oil bits c
        DriveOn = 3,        // ExactRace::drive_on(car) (the AI drives the car on: a demonstration)
        HandBack = 4,       // ExactRace::hand_back(car)
        StartCountdown = 5, // ExactRace::start_countdown()
        StartCount = 6,     // ExactRace::set_start_count(a)
        StartType = 7,      // ExactRace::set_start_type(a)
        TimeScale = 8,      // ExactRace::set_time_scale(F::raw(a))
        FlowByte = 9,       // ExactRace::set_flow_byte(a)
        Timer = 10,         // ExactRace::set_timer(a)
        Hit = 11,           // the conductor's hit rule for car's hit of kind a, impulse F::raw(b) (hit_course_out): the
                            // course's rigid bodies (the game's pylons) touched by the car
    };
    u32 frame = 0;
    Kind kind = PedalStyle;
    u8 car = 0;
    u32 a = 0, b = 0, c = 0;
    bool operator==(const RaceChange&) const = default;
};
// Makes the change on the race.
void apply_change(ExactRace& race, const RaceChange& c);

struct RaceRecord {
    static constexpr u32 kVersion = 5; // the record's own layout (inside a replay file; versions 1 to 4 still read)

    // ---- the program and the course ----
    u64 program = 0;        // ProgramData::fingerprint of the program the race ran on
    u64 data = 0;           // the game data it was built from besides the program (race::exact_data_fingerprint; 0 none)
    Behaviour behaviour{};  // the rules and step rate it ran with (ProgramData::behaviour; sim/program.h)
    std::string course;     // the course's label (the game loads its file)
    // ---- RaceSetup ----
    u8 course_category = 0;
    Conductor kind = Conductor::SinglePlayer;
    u8 config = 0;
    s32 laps = -1;
    s32 start_type = 0, countdown = 0, battle_laps = 0, battle_wear = 0, handicap = 0, battle_catchup = 0;
    bool replay = false, unjudged = false, locked = false;
    u32 logger = 0, logger_record = 0;
    Splits session;                    // the session's best splits and lap at the run's start
    std::vector<EntryRecords> records; // the entries' time records at the run's start
    std::array<u32, 3> licence_times{};
    u8 speed_unit = 0;
    u32 ghost_switch = 0;
    RaceConfig rules{};     // the race configuration block as the race read it (race_config(event) or RaceSetup::rules)
    bool effects = true;
    u32 info = 0, conductor = 0, course_addr = 0;
    u32 road_addr = 0; // (version 2)
    std::vector<StartEntry> entries;
    // The solo modes' recorders at the run's start (RaceSetup::recorders, version 3: the free run's best lap and ghost carried
    // from the runs before, a whole-run ghost's recording), the ghost car (RaceSetup::ghost_car) and whether its frames are the
    // record's (a record from before version 3). A replay starts from them.
    std::shared_ptr<const SoloRecorders> recorders;
    s32 ghost_car = -1;
    bool external_ghost = false;
    // The player's recording at the run's end (version 3; SoloRecorders::run_recording): what a ghost of this run plays.
    std::vector<u8> run_recording;
    // Each entry's spark points as the race had them (RaceSetup::sparks, version 4; a record from before: none, the race as it
    // ran then).
    std::vector<std::vector<SparkPoint>> sparks;
    // ---- the frames ----
    u32 frames = 0;                           // frames recorded (ExactRace::step calls)
    std::vector<std::vector<PadFrame>> pads;  // pads[i][t]: car i's frame at frame t (one stream per entry; frames past a
                                              // stream's end are empty: a CPU car's stream is empty)
    std::vector<RaceChange> changes;          // in frame order
    // The free run ghost's extra steps (the race step once more inside a frame: ExactRace::set_nested_pads), each with the
    // pad frames of its own read, in frame order.
    struct Nested {
        u32 frame = 0;
        std::vector<PadFrame> pads;
    };
    std::vector<Nested> nested;
    // ---- the end ----
    u64 final_hash = 0; // race_hash after the last frame (0 = none)

    // The setup as recorded, on these tables, cars and course (RaceSetup::data / db / course); `d` must have the record's
    // behaviour (ProgramData::set_behaviour).
    RaceSetup setup(const ProgramData& d, const data::CarDatabase& db, std::shared_ptr<const RaceCourse> course) const;
    // The record of a race built from `s` (no frames yet): the configuration block as the race reads it.
    static RaceRecord of(const RaceSetup& s, std::string course_label);

    // Recording: one frame's pads (cars beyond pads.size() get empty frames), its changes (before add_frame).
    void add_change(RaceChange c);
    void add_frame(std::span<const PadFrame> frame_pads);
    // The pads of an extra step inside the last added frame.
    void add_nested(std::span<const PadFrame> pads);
    // Car i's frame at frame t (an empty frame past the end).
    PadFrame pad(int i, u32 t) const;

    Bytes serialize() const;
    // Throws FormatError.
    static RaceRecord parse(ByteSpan data);
};

// The player's recording of a run (its run_recording; for a record from before version 3, its car 0's frames recorded as the
// player's device records them, every frame).
std::vector<u8> run_recording_of(const RaceRecord& r);

// A hash of everything a race step leaves that the next one reads or the game shows: every car's record, the collision
// bodies, the race order, the race flow's clock and end countdown, the VU0 random register, the start words, the sparks (in a
// race with spark points). Two runs with the same hash after the same frame are the same race (to the hash's 64 bits).
u64 race_hash(const ExactRace& race);

// Steps a race from a record: before frame t its changes, then its pads.
class RacePlayback {
public:
    explicit RacePlayback(const RaceRecord* r = nullptr) : r_(r) {}
    bool done() const { return !r_ || t_ >= r_->frames; }
    u32 frame() const { return t_; }
    // The next frame: its changes on `race`, then race.step with its pads. False when the record has no frames left.
    bool step(ExactRace& race);
    // The changes due before the next frame (on `race`), then the frame's pads (cars beyond the record: empty); advances.
    // For a caller that steps the race itself.
    std::span<const PadFrame> next(ExactRace& race);
    // On to frame t (the frames between not played: a jump of the race).
    void seek(u32 t);

private:
    const RaceRecord* r_;
    u32 t_ = 0;
    size_t change_ = 0, nested_ = 0;
    std::vector<PadFrame> pads_;
};

} // namespace gt3r::sim::exact
