#pragma once

// The race flow after the vehicle phases (the race step after phase 13): each car's lap progress (F +0x3E4), its
// crossings of the start line, the sector lines, the data logger's marks and the split line (laps F +0x434, lap base F +0x467,
// sector F +0x492, logger mark F +0x493, the crossing time F +0x530), the finish (F +0x4E0) and the course-out flags it clears,
// the wrong-way flag (F +0x535 bit 0) and the HUD message (F +0x51C / +0x51D), the race clock, the hand-backs to the player, the
// machine test's top speed, the race's end countdown and the race info's result words, in the program's arithmetic, by race
// mode: the conductors' hooks for single races, both battles, the time trial, the licence
// tests, free practice, the machine test and free run. And what it leaves for the race display, the results and the replays:
// the posts to the race's event queue, the race display's events among them (RaceEvent), the race entries' and the session's
// time records, the single races' sector order (the sector handler and the time-difference hook), the data
// logger's table (race info + 0x70 / + 0x74) and the free run's lap recordings and ghost car
// (RecorderEvent). The AI driver's hand-overs, the pit lane's state machine and the free run ghost's extra
// step are the caller's (hooks).

#include "sim/collision_exact.h"
#include "sim/exact/control_record.h"
#include "sim/exact/course_query.h"
#include "sim/exact/program_data.h"
#include "sim/exact/race_inputs.h"
#include "sim/exact/start.h"
#include "sim/exact/tick.h"

#include <functional>
#include <map>
#include <span>
#include <vector>

namespace gt3r::sim::exact {

constexpr u32 kNoTime = 0x157529FF; // a time (ms) not set

// Four sector times and a lap's, "no time" at first: a race entry's best lap's (+0x11C8) and current lap's
// (+0x11DC), the session's best (race info + 0x8AC; its lap + 0x8BC is the record time compares).
struct Splits {
    std::array<u32, 4> sector{kNoTime, kNoTime, kNoTime, kNoTime};
    u32 lap = kNoTime;
};
// A race entry's lap list (lap list, entry + 0x210), reset at every run's start.
struct LapList {
    u32 counted_only = 0;  // +0x00 set: laps are counted, not listed
    u32 count = 0;         // +0x04 the laps counted
    u32 dropped = 0;       // +0x08 the laps dropped from the front of the full list
    u32 total = 0;         // +0x10 the listed laps' sum
    std::vector<u32> laps; // +0x0C its size, +0x14 the last 1000 laps
};
// What the race flow keeps in a car's race entry (the race info's entry list, entry i for car i).
struct EntryRecords {
    LapList laps;           // +0x210
    Splits best, current;   // +0x11C8 / +0x11DC (a sector, the lap: the best lap's splits kept)
    u32 best_lap = kNoTime; // +0x11F0 the visit's best
    F top_speed;            // +0x2100 the machine test's top speed
    // The entry's other words a store of the race flow reached, by their offset: a licence test's sector times past the
    // current lap's splits run on into the entry; the race flow reads none of them.
    std::map<u32, u32> words;
};
// A new run of the race entry's (the race's start-up): its lap list emptied; its best times stay.
void new_run(EntryRecords& e);

// The lead cars' times at each sector of the single races and battles (conductor + 0x8000 + 0x3AF8 .. + 0x3BA7; as the race's
// initialisation, the initialisation's, leaves it: no times, no lap, no cars): the conductor's sector handler keeps per
// sector the lead lap and the cars that reached it on that lap with their times, the time-difference hook posts the time
// differences of the new ones.
struct SectorOrder {
    SectorOrder() {
        for (auto& t : time) t.fill(kNoTime);
        lap.fill(-1);
    }
    std::array<std::array<u32, 8>, 4> time;   // +0x3AF8 + 32 s + 4 p: in order of the times
    std::array<s16, 4> lap;                   // +0x3B78 + 2 s: the lead lap
    std::array<s8, 4> count{}, shown{};       // +0x3B80 / +0x3B84 + s: the cars that reached it on that lap, those posted
    std::array<std::array<s8, 8>, 4> car{};   // +0x3B88 + 8 s + p (not moved on an insertion, as in GT3)
};

// A car at a crossing, as the data logger's table entries and the free run's lap recordings keep it (a 0x1B00-byte entry:
// The program's routines write it restores a car from it).
struct Snapshot {
    u8 sub = 0;     // +0x02 the crossing's part of the step after it (50 less the steps' 50ths before it)
    u8 phase = 0;   // +0x03 (the race clock + 50) mod 3
    u32 frame = 0;  // +0x04 the logger's: the car's input frame at the crossing (less the table's first entry's, see below)
    u32 frames = 0; // +0x08 the logger's: the frames to the entry's end
    F frac;         // +0x0C the crossing's fraction of the step
    u32 time = 0;   // +0x10 the time to the entry's end (ms): the logger's mark time, the recording's lap time
    Car car;        // +0x24 the car record
    // +0x1700: the car's collision body's points (their sweep and wall state; a restore keeps the rebuilt body's
    // local geometry, +0x00 .. +0x0F of each), filled by the caller's RaceFlowHooks::save_body.
    std::array<collide::exact::Point, 8> body{};
    bool has_body = false;
};

// A post to the race's event queue (race info + 0x84): {type, car, arg} are the entry's type, word and priority.
// The race flow's: 4 a hand-back to the player, 5 a car finished (car, 2), 9 a licence test failed (car), 10 the time trial's
// course-out, 13 a data logger mark (the car's index), 19 a new record. The race display's (types 11-18, priority 2;
// display_word packs the word): 11 a lap's number (car, lap), 12 a sector's (car, lap, sector), 14 a time (value: the lap's
// or the sector's, ms), 15 a time difference (value, ms), 16 laps behind the lead car (value), 17 a speed (speed, in the
// display's unit), 18 a note (value: 1 into the pit lane, 2 a new best lap, 3 free practice's start, 6 out of the pit lane).
struct RaceEvent {
    s32 type = 0, car = 0, arg = 0;
    s32 lap = 0, sector = 0, value = 0;
    F speed;
};
constexpr bool display_event(s32 type) { return type >= 11 && type <= 18; }
// The queue word of a race display event (car, lap, sector clamped as the event's queue word does), and its bits the event
// sets (the original takes the others from a pointer); the race flow's events' word is their car.
u32 display_word(const RaceEvent& e);
u32 display_bits(s32 type);

// The free run's replay recorders as its lap handlers find them before the step (the race's replay recorders: 0x100 the
// player's recorder, 1 the ghost's playback).
struct FreeRunRecorder {
    bool lap_ok = true;       // the lap's recording (+0x128) is usable: not flagged (+0x30) and under 36,000 frames (+0x38)
    u32 best = kNoTime;       // the best lap's store (+0x124): its header's lap time
    bool ghost = false;       // the ghost's playback has a recording (its +0x140 clear)
    u32 ghost_time = kNoTime; // its header's lap time
    const Snapshot* ghost_start = nullptr; // its header (of its +0xC4): the ghost car's lap start
    // The lap recording's header (of +0x128): the lap's start (lap_start at the last crossing). A lap that becomes the
    // ghost at once (RecorderEvent::ghost with the Ghost option on) is the ghost car's start at once.
    const Snapshot* lap_start = nullptr;
    // The Options menu's Ghost: off, a lap that beats the ghost leaves the ghost's playback as it is (empty, or the
    // old lap) until "Update Ghost?" after the run.
    bool ghost_option = true;
};
// What the free run's lap handlers do to the recorders, in order.
struct RecorderEvent {
    enum Kind : u8 {
        close,         // the lap's recording closed (every lap, with the race-flow byte clear)
        lap,           // a usable lap's time into its header (+0x10)
        best,          // the lap into the best lap's store (race info + 0x4C set)
        ghost,         // 1, 1: the best lap's store becomes the ghost (race info + 0x48 set)
        ghost_restart, // the ghost car put back on its recording's lap start (its laps `time`), its playback restarted
                       // ; `extra`: the step ran once more for it (RaceFlowHooks::ghost_step)
        lap_start,     // the lap's start into the lap recording's header (`snapshot`), its recording restarted
    } kind{};
    int car = 0;
    u32 time = 0;
    bool extra = false;
    Snapshot snapshot;
};

// The car: c restored from a snapshot but its ghost flag, index and control source; its last crossing's time from the
// snapshot's remainder and the race clock. (Its collision body is the caller's.)
void restore_car(Car& c, const Snapshot& s, u32 clock);

// The solo race modes' recorders: the race object's (solo race, race info + 0x3C; race free-run)
// input devices and their recordings in the game's record memory, its two pools ((0x20000, 2)):
//   - the player's device (+0xC8C0, its manager mode 0 on pool 0): the run's pad frames as the pad read records them
//     (every live frame its car's phase 3 reads), which a replay plays and a ghost takes;
//   - the ghost car's device (+0xCA08, playing): mode 0 on pool 1 (a whole run's recording, the time trial's, licence's and
//     machine test's ghost), the free run's mode 2 on pool 1's store 0;
//   - the free run's lap recording (+0x21744: mode 2, pool 1's store 1; every step's frame of the player's car, from
//     its lap start) and best lap (+0x216E4: store 2), each store's car snapshot (0x1B00 bytes) before its recording.
// The free run's lap handlers' recorder events (RecorderEvent) are made on them in order (apply); the race reads its ghost car's
// frames from the ghost's device (ghost_frame) and records the player's (player_frame, lap_frame) when its phases run.
struct SoloRecorders {
    std::vector<u8> pool0, pool1;     // the record memory
    std::array<Snapshot, 4> snapshot; // pool 1's stores' snapshots: 0 the ghost's, 1 the lap's, 2 the best lap's
    ControlManager player, ghost, lap, best;
    bool ghost_empty = true;  // the ghost device's +0x140: no recording to play (its frames are empty)
    bool player_plays = false; // the player's device plays its recording (+0x130 1: the Analyzer's race plays track A)
    bool ghost_option = true; // the Options menu's Ghost (off, a better lap does not become the ghost at once)
    bool free_run = false;

    // The constructors: the managers bound as solo race's (the free run's ghost device in mode 2), the best lap "no
    // time"; then a run's start (start).
    explicit SoloRecorders(bool free_run = false);
    // A run's start (the solo race, the race free-run, the race's): the lap and best
    // lap's recorders rewound, an empty ghost's device on store 0 (free run), the player's and the ghost's devices restarted.
    void start();

    // The player's device live (mode 0): the frame its car reads, recorded.
    PadFrame player_frame(const PadFrame& live);
    // The ghost's device (mode 1): its recording's next frame (an empty one with none).
    PadFrame ghost_frame();
    // The free run's pad hook: a car's frame into the lap recording.
    void lap_frame(const PadFrame& f);
    // The lap handlers' view (FreeRunRecorder; its snapshots point into this). `how`: NTSC-J's lap check is its own.
    FreeRunRecorder inputs(const Behaviour& how) const;
    // A recorder event of the free run's lap handlers, as the program makes it.
    void apply(const RecorderEvent& e);
    // Race object, kind 1, now: the best lap's store becomes the ghost's; `now` (a lap handler's, only with the
    // Ghost option on): its playback restarted, the ghost car runs it at once.
    void best_to_ghost(bool now);
    // Kind 0: a whole run's recording (as run_recording gives it) becomes the ghost's: pool 1 holds it.
    void run_to_ghost(std::span<const u8> recording);
    // LOAD GHOST (ours: a saved free run in place of a memory card's ghost file): another visit's best lap (its store, the
    // snapshot and the recording) becomes the ghost's, copied as the ghost's taking of the best lap copies it;
    // false (nothing changes) when it has none.
    bool ghost_from_best(const SoloRecorders& from);
    // The best lap's time (ms; "no time" without one).
    u32 best_time() const { return snapshot[best.store / kRecordStore].time; }
    // The race's end (every input device's recording, the solo race the lap and best lap's): closed.
    void end();
    // The player's recording (pool 0's block: its header and stream, as far as the header says, and the byte filled in part)
    // once closed.
    std::vector<u8> run_recording() const;
    // The Analyzer's race (the logger's devices, the solo race: both playing, each over its whole memory): the player's
    // device plays run recording `a`, the ghost's `b` (none: empty); then a run's start.
    void analysis(std::span<const u8> a, std::span<const u8> b);
};

// What the race flow keeps of the race (the conductor's and the race info's), besides the cars.
struct RaceFlow {
    u32 clock = 0;     // conductor + 0xE380: the race clock, 50 a step (3000 a second), at most 0x405F7DFD
    u16 end_count = 0; // conductor + 0x8000 + 0x37AE: the steps until the race ends (with race-flow byte 2)
    // Race info + 0x40 / + 0x44: the result (a licence test: 1 + the index of the first judging time it beat, 0 none, -1 out
    // of the course) and its time; + 0x48 the free run's lap beat the ghost's; + 0x4C a new record.
    s32 result = 0;
    u32 result_time = 0;
    u32 ghost = 0;
    u32 record = 0;
    Splits session;                    // race info + 0x8AC: the session's best (the visit's: not reset per run)
    std::vector<EntryRecords> entries; // per car (grown as needed)
    SectorOrder order;                 // single races and battles
    // Per car: its data logger table (car + 0xC, the entry's input object: its + 0x12C, entry k at + 0x1B00 k), the entries
    // the race flow wrote.
    std::vector<std::map<s32, Snapshot>> logger;
    std::vector<RecorderEvent> recorder; // the free run's, in order (the caller applies and clears them)
};

// What the race flow reads outside the cars' records.
struct RaceFlowInputs {
    Conductor kind = Conductor::SinglePlayer; // the race mode: its conductor's virtuals
    const CourseQuery* course = nullptr;      // the lap progress, the lap length, the sector lines, the logger marks
    F split;                                  // the road's split line (header +0x1C; 0 none): the base conductor's split line
    s32 start_count = 0;                      // race info + 0x5C: the start countdown (> 0: no line is counted yet)
    s32 start_type = 0;                       // race info + 0x54: 3 the rolling start
    s32 laps = 0;                             // the conductor's lap count: the race's laps (race_laps; 0 none)
    std::array<u8, 3> config{};               // the race configuration's +0 .. +2 (+1 the laps / machine test's type, +2 the
                                              // licence test's type)
    u16 time_limit = 0;                       // the configuration's +0x38: the race's minutes (0 none)
    u8 flow = 0;                              // the race-flow byte
    std::array<u32, 16> messages{};           // + 8 m + 4: each HUD message's display steps
    std::array<F, 4> machine_sectors{};       // the machine test's sector lines (its split line / sector start)
    // The race info's: + 0x6C a replay (no finish is judged), + 0x7C the records are locked, + 0x80 a licence test that is not
    // judged, + 0x70 the data logger plays the run (its marks are timed), + 0x74 its table is recorded (the logger's pass over
    // the replay), and the licence test's judging times (its record + 0x1C4 / 0x1C8 / 0x1CC: minutes, seconds, milliseconds; ms).
    bool replay = false, locked = false, unjudged = false, logger = false, logger_record = false;
    std::array<u32, 3> licence_times{};
    // The data logger's: per car its input object's frame (car + 0xC: + 0xFC) and whether its table counts frames from its
    // first entry's (+ 0xC8 is 2).
    std::array<u32, 8> logger_frame{};
    std::array<u8, 8> logger_relative{};
    u8 speed_unit = 0;        // the speed display's unit (0 km/h, 1 mph, others m/s): the machine test's speed event
    FreeRunRecorder recorder; // the free run's
    // How the race behaves (ProgramData::behaviour): at PAL's rate the clock counts 60 a step (3000 a second at 50 steps)
    // and the end countdown 50 steps a second; under PAL's rules a new record must beat the old one and the licence's stop
    // speed is 1.5 km/h.
    Behaviour behaviour;
};
// The HUD messages' display steps from the program's table; in NTSC-U behaviour on a PAL disc at 60 steps a second.
std::array<u32, 16> hud_messages(const ProgramData& d);
// The machine test's sector lines (the table).
std::array<F, 4> machine_sectors(const ProgramData& d);
// The conductor's lap count, the race's laps: the configuration's +1 (the base conductor's, single races and the licence
// tests), the race info's +8 (`info_laps`, both battles), 1 in the time trial, 0 in free run, free practice and the machine
// test.
s32 race_laps(Conductor k, const RaceConfig& cfg, s32 info_laps);

// What the race flow hands on that is not ported here.
struct RaceFlowHooks {
    // The calls after the finish flag, by its mode (1-3; maps 4 to 2 on the pit-lane surface, else 0): a pad
    // car's driver re-made, then mode 3 and the driver kind switch, 2, 1,
    // 0: the AI drives the finished car.
    std::function<void(Car& c, int i, int mode)> finish_drive;
    // The pit lane's state machine, with the new and the old progress.
    std::function<void(Car& c, int i, F progress, F old)> pit_lane;
    // A car handed back to its player (the rolling start's end, the first crossing of time trials, licences, free
    // runs).
    std::function<void(Car& c)> rolling_handback;
    // the split-screen battle's lap handler on the other pad cars, set to control source 2: the AI takes car j over.
    std::function<void(Car& c, int j)> ai_takeover;
    // the free run's crossings while the data logger records, race info + 0x74: the AI drives car i on.
    std::function<void(Car& c, int i)> drive_on;
    // The free run ghost's extra step (the race step in its second entry mode, a1 = 1).
    std::function<void()> ghost_step;
    // A snapshot's collision body (of the body of index `index`), and a restore's (the body
    // rebuilt from its car, the pair tables, then the snapshot's points).
    std::function<void(int index, Snapshot& s)> save_body;
    std::function<void(int index, const Snapshot& s)> restore_body;
};

// The conductor's race progress: the lap progress at the car's position (the road segment as the hint); the machine
// test's is at the nose (nose_progress, start.h).
F car_progress(const Car& c, const CourseQuery& course);
// Virtual 28 by race mode: nose_progress in the machine test, else car_progress.
F race_progress(const Car& c, Conductor k, const CourseQuery& course);
// The conductor's sector count, by race mode: the course's (CourseQuery::sectors); the machine test's by its
// type (the configuration's laps byte `laps_byte` below 4, else 0): 1 and 2 for types 1 and 2, else 3.
int sector_count(Conductor k, u8 laps_byte, const CourseQuery& course);
// Virtual 34, sector i's line: the course's (CourseQuery::sector_start); the machine test's from its table (`machine`,
// machine_sectors: entry i below the last sector, else entry 3).
F sector_line(Conductor k, u8 laps_byte, const std::array<F, 4>& machine, const CourseQuery& course, int i);
// The car's best lap (car + 0x16C4) keeps the lower of it and `lap`.
void keep_best_lap(Car& c, u32 lap);
// Shows HUD message m (its display steps from the table) unless another is still showing and m has none.
void hud_message(Car& c, const RaceFlowInputs& in, u8 m);

// The race display's events and the records (posted to `events`, kept in `flow`).
// A car's times do not enter the records: a ghost (not on PAL: its callers leave a ghost out first), the race-flow
// byte set, the race info's + 0x7C or + 0x6C.
bool records_locked(const Car& c, const RaceFlowInputs& in);
// Car n's lap time `lap` (laps: its lap number; `failed`: the course-out flags the caller read): the best lap,
// events 11 (laps) and 14 (lap); unless locked the lap into the entry's list and, unless failed, the
// entry's best, its splits and the session's best. PAL: a ghost's ends after its best lap. NTSC-J:
// no event 14; a locked car's laps stay out of the list, its best and the session's, but its lap still goes into
// its entry's splits.
void lap_time(std::span<Car> cars, int n, s32 laps, u32 lap, u8 failed, const RaceFlowInputs& in, RaceFlow& flow,
              std::vector<RaceEvent>& events);
// Car i's time `split` at a sector line (lap, sector: the event's numbers): events 12 and 14; unless failed or
// locked the time into the entry's current splits. PAL: nothing for a ghost. NTSC-J: event 12 only, and
// the time is kept unless failed (no lock test).
void sector_time(std::span<Car> cars, int i, s32 lap, s32 sector, u32 split, u8 failed, const RaceFlowInputs& in, RaceFlow& flow,
                 std::vector<RaceEvent>& events);
// The conductors' sector handler, car i at a sector line or the start line (lap: its lap less 1, t: the crossing's clock / 3):
// the single races' and battles' code (the sector order; event 16 for a car laps behind); nothing in the others.
void race_mode_sector_order(std::span<Car> cars, int i, s32 lap, s32 sector, u32 t, const RaceFlowInputs& in, RaceFlow& flow,
                            std::vector<RaceEvent>& events);
// The conductors' sector-line handler, car i over sector line `sector` (laps: its laps, split: the time since its last start line
// crossing): the sector's time events with the mode's lap and sector numbers, the time trial's, free run's and free
// practice's difference to the session's best (event 15).
void race_mode_sector(std::span<Car> cars, int i, s32 laps, s32 sector, u32 split, const RaceFlowInputs& in, RaceFlow& flow,
                      std::vector<RaceEvent>& events);
// The conductors' time-difference hook, once a step after the race flow: the single races' and battles' code (each sector's new cars'
// differences to the lead car, event 15); nothing in the others.
void race_mode_display(std::span<const Car> cars, const RaceFlowInputs& in, RaceFlow& flow, std::vector<RaceEvent>& events);

// The conductors' first-crossing handler, the race's first crossing of car i (t: the crossing's clock / 3, sub: 50 less its 50ths of a
// step, frac: its fraction of the step): the time trial hands a pad car back to its player, the licence with its
// configuration's +0 set, free run car 0 when the AI drove its run-up (then its ghost car and lap
// recording); all three and free practice (event 18 for car 0) keep t (F +0x530); nothing in the others.
void race_mode_first_crossing(std::span<Car> cars, int i, u32 t, s32 sub, F frac, const RaceFlowInputs& in, RaceFlow& flow,
                              std::vector<RaceEvent>& events, const RaceFlowHooks& hooks);
// The conductors' lap handler, a completed lap of car i (laps: the laps before this crossing; t: its clock / 3; lap: t less
// the last crossing's; sub, frac as above): the base conductor's code (single races, the i.LINK battle: the race's end for
// the car when the leader finished, the laps are done or the time limit passed), the split-screen battle's code (no time
// limit; the AI takes the other pad cars over), the time trial's code (every lap ends the run), free practice's code,
// free run's code (its lap recording, best lap and ghost), the licence's code (a type 3 test ends with its laps), the
// machine test's code (nothing); with their display events and records.
void race_mode_lap(std::span<Car> cars, std::span<const s32> order, int i, s32 laps, u32 t, u32 lap, s32 sub, F frac,
                   const RaceFlowInputs& in, RaceFlow& flow, std::vector<RaceEvent>& events, const RaceFlowHooks& hooks);
// The conductors' split handler, car i over the split line (laps: its laps, t: the crossing's clock / 3, lap: the time since its
// last crossing): the machine test's run ends there (the top speed kept first; the time or the top speed posted),
// the licence's type 2 test; true when the run ended (0 elsewhere).
bool race_mode_split(std::span<Car> cars, int i, s32 laps, u32 t, u32 lap, const RaceFlowInputs& in, RaceFlow& flow,
                     std::vector<RaceEvent>& events, const RaceFlowHooks& hooks);
// The line crossings between the old progress and the new one (F +0x3E4) of car i: 1 when it crossed the start line
// backwards. The lap handlers (the conductors' hooks) need the race's order (conductor + 0xE388) and its cars; with
// the data logger (race info + 0x70) a recorded car's marks are timed.
int line_crossings(std::span<Car> cars, std::span<const s32> order, int i, F old, const RaceFlowInputs& in, RaceFlow& flow,
                   std::vector<RaceEvent>& events, const RaceFlowHooks& hooks);
// Car i's race flow: the progress (the race progress: car_progress; the machine test's nose_progress), the
// crossings, the wrong-way flag (hit: the car's collision body hit a wall or a car this step) and messages, then the
// pit lane's state machine (hooks.pit_lane).
void race_flow_car(std::span<Car> cars, std::span<const s32> order, int i, bool hit, const RaceFlowInputs& in, RaceFlow& flow,
                   std::vector<RaceEvent>& events, const RaceFlowHooks& hooks);
// The single-player conductor's mode start: with the rolling start, car 0's AI run-up counts down and hands the car
// back at 0.
void rolling_start(Car& car0, const RaceFlowInputs& in, std::vector<RaceEvent>& events, const RaceFlowHooks& hooks);
// The conductor's mode start by race mode (before the step's loop over the cars): single races the rolling start (above), free
// run car 0's run-up counted down whatever the start, the time trial car 0's course-out ends its run;
// empty in the others.
void race_mode_start(std::span<Car> cars, const RaceFlowInputs& in, std::vector<RaceEvent>& events, const RaceFlowHooks& hooks);
// The conductors whose race flow is ported (all).
bool race_flow_ported(Conductor k);
// The base conductor's race clock (also the time trial's and free run's): the race clock, once the start countdown
// is over.
void race_clock(RaceFlow& flow, const RaceFlowInputs& in);
// The conductor's race clock by race mode: the licence tests first judge every car (out of the course, or stopped
// in the stop zone in a type 1 test), then at the countdown's last step the data logger's first entries of the
// recorded cars (the modes of the logger-start test, with race info + 0x74) and the race clock.
void race_mode_clock(std::span<Car> cars, const RaceFlowInputs& in, RaceFlow& flow, std::vector<RaceEvent>& events,
                     const RaceFlowHooks& hooks);

// After the race clock (the step runs these hooks, the hooks, only in its entry mode 0): the gap
// matrices, then the ranking.

// The conductor's gap matrices (conductor + 0xB8F4 / + 0xB934, cleared at the initialisation), which the AI's
// queries read.
struct GapMatrix {
    std::array<std::array<s8, 8>, 8> laps{}; // +0xB8F4 + 8 i + j: car i's whole laps behind car j (clamped to -127 .. 127)
    std::array<std::array<F, 8>, 8> gap{};   // +0xB934 + 32 i + 4 j: and its metres
};
// For every pair i != j, race_gap(car j, car i) with the lap length `lap`; the diagonal is left alone.
void gap_matrix(std::span<const Car> cars, F lap, GapMatrix& m);
// The conductor's ranking: the base conductor's code (rank, start.h: the order re-sorted by insertion)
// in every race mode but free practice, whose code sorts by the best lap (car + 0x16C4, the smaller ahead,
// unsigned); both then set each car's rank F +0x4E1 to its position + 1.
void race_ranking(std::span<Car> cars, RaceOrder& order, Conductor k);

} // namespace gt3r::sim::exact
