#pragma once

// A whole race in GT3's own arithmetic: everything GT3's race step (entry mode 0) reads and writes, kept in
// one object and stepped from the cars' pad frames with nothing taken from the original. Built from the race's setup as the
// race's initialisation builds it (the conductor's hooks: the program's tables, the race configuration, the AI course,
// every car's record from start_cars, the collision bodies, the matrices), then per race frame: the race information's
// start countdown (which the race's frame runs before the step) and the step: the race-flow gate, phases 1-9, the
// collision hook-up (phase 10, the conductor's collision step: the bodies from the cars, the all-bodies or walls-only step, the
// results back, each hit's course-out and pad feedback), phase 11, phase 12 with the tyre effect emitter's particles and its
// draws of the VU0 random register, phase 13, the race flow and the pit lane, the race clock, the gap matrices, the ranking,
// the rubber band and the race's end countdown [physics §1.1-§1.2, §5.3; ai §9-§13].
//
// A race frame (step) is that step and what race's frame does after it that the race's later frames depend on: each car's
// spark bursts and the sparks' update (sparks.h), which draw the VU0 random register after the step's tyre effects and flaps.
//
// The race flow's outputs for the game (race_flow.h): the race display's events among the queue posts, the entries' and the
// session's time records, the data logger's tables (race info + 0x70 / + 0x74), the free run's recorder events (its lap
// recordings and ghost car: the ghost's extra step, the race step in its second entry mode, runs inside). Not modelled
// (outputs the game makes itself): the sounds' pass-by state (on conductor + 0xBA34), the pad's vibration output,
// the tyre effect particles' ageing and drawing (their pool: the race's frame update / drawing).

#include "base/game_random.h"
#include "formats/course/ai_lines.h"
#include "formats/course/driver_sets.h"
#include "formats/course/road.h"
#include "sim/collision_exact.h"
#include "sim/exact/ai_course.h"
#include "sim/exact/course_ground.h"
#include "sim/exact/course_query.h"
#include "sim/exact/pit_lane.h"
#include "sim/exact/race_flow.h"
#include "sim/exact/race_inputs.h"
#include "sim/exact/sparks.h"
#include "sim/exact/start.h"
#include "sim/exact/tick.h"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace gt3r::sim {
class CourseCollision;
}

namespace gt3r::sim::exact {

// A course as a race reads it, parsed and built once and shared (by every race on it and every copy of a race).
struct RaceCourse {
    course::Road road;       // slot 28 (GTRW)
    course::AiLines lines;   // slot 31
    bool has_lines = false;
    course::DriverSets sets; // slots 37-40
    std::vector<u8> env;     // slot 32 (Env0), with the tyre effects' parameters (+0x4D0 + 0x44 kind, +0x910) as the race's
                             // initialisation writes them
    std::unique_ptr<CourseGround> ground;              // the vehicle tick's ground query
    std::unique_ptr<CourseQuery> query;                // the race's progress, grid, sectors
    std::unique_ptr<sim::CourseCollision> collision;   // the collision step's walls
    AiCourse ai;                                       // the AI course object (ai_course_setup)

    // From the course file's bytes; throws std::runtime_error when it has no road block.
    static std::shared_ptr<const RaceCourse> load(ByteSpan file, const ProgramData& d);
    ~RaceCourse();
};

// What a race is built from.
struct RaceSetup {
    const ProgramData* data = nullptr;     // the program's tables (the race keeps its own copy)
    const data::CarDatabase* db = nullptr; // the cars (start_cars)
    std::shared_ptr<const RaceCourse> course;
    ByteSpan event;                        // the event record (database table 32): race_config; copied
    u8 course_category = 0;                // the course record's +8 (race_course_category)
    Conductor kind = Conductor::SinglePlayer;
    u8 config = 0;                         // the race start's byte, kept as the race-flow byte (conductor + 0x8000 + 0x37AC)
    s32 laps = -1;                         // the configuration's +1 as the race sets it (the laps; the licence's and
                                           // machine test's type); -1 keeps the event's
    // The race information's words.
    s32 start_type = 0;     // +0x54: -1 none, 0 a countdown of `countdown` seconds, 1 four seconds, 2 ten, 3 the rolling start
    s32 countdown = 0;      // +0x58: the seconds of start type 0
    s32 battle_laps = 0;    // +0x08: the battles' laps (their lap count)
    s32 battle_wear = 0;    // +0x0C: the battles' tyre wear setting (2 on)
    s32 handicap = 0;       // +0x10: the split-screen battle's handicap
    s32 battle_catchup = 0; // +0x14: the battles' catch-up setting (1 or 2)
    bool replay = false;    // +0x6C: no finish is judged
    bool unjudged = false;  // +0x80: a licence test that is not judged
    bool locked = false;    // +0x7C: no time enters the records
    u32 logger = 0;         // +0x70: the data logger plays the run (phase 12's draw mode; its marks are timed)
    u32 logger_record = 0;  // +0x74: and records its table (the logger's pass over the replay)
    Splits session;         // +0x8AC: the session's (the visit's) best splits and lap, the record time; none at first
    std::vector<EntryRecords> records; // the entries' time records at the run's start (missing: none; new_run applies)
    std::array<u32, 3> licence_times{}; // the licence test's judging times (ms)
    u8 speed_unit = 0;      // the speed display's unit (the machine test's speed event)
    u32 ghost_switch = 0;   // the race object's +0x217A4: a ghost car is drawn (phase 12's draw mode, with +0x70 clear)
    // The race configuration block itself (conductor + 0xE340) instead of the event record's (null: race_config(event)).
    const RaceConfig* rules = nullptr;
    // The entries in grid order (entry i is car i, at most 8).
    std::vector<StartEntry> entries;
    // The course's tyre effect emitter (the race's particle system, RaceCourse +4): it draws the VU0 random register for every
    // particle. Off only to reproduce a race run without one.
    bool effects = true;
    // Each entry's car model's spark points (GTCI +0x58 / +0x5C, the model folder the race draws; entry i's at i, none past the
    // list): the race's sparks [presentation §L7]. All empty: a race without them (a replay recorded before they were made, a
    // test of the race step alone).
    std::vector<std::vector<SparkPoint>> sparks;
    // The addresses the records' pointer words hold (car + 0 / + 4 / + 8, the parameter block's tables): they matter only
    // where records are compared byte for byte.
    u32 info = 0, conductor = 0, course_addr = 0;
    // The road block's address (StartRace::road): only a start on a grid slot past the course's table reads it.
    u32 road_addr = 0;
    // The solo race modes' recorders as the run starts (race_flow.h SoloRecorders: the race object's input devices and their
    // recordings; the race keeps its own copy). With them car 0 is the player's car, whose frames (pads[0]) its device
    // records, and `ghost_car` (if any) the ghost car, whose frames its device plays, both when their phases run, and the
    // free run's recorder events are made on them as the lap handlers make them. Without them every car's frame is the
    // caller's and so are the free run's recorders (set_recorder, set_nested_pads).
    std::shared_ptr<const SoloRecorders> recorders;
    int ghost_car = -1;
    // The ghost car's frames are the caller's after all (a replay recorded before the race kept its recorders: its frames
    // and extra steps' frames as the record has them); the recorders still answer the lap handlers.
    bool external_ghost = false;
};

// The race information's start words (race info + 0x50 .. + 0x68).
struct RaceInfoStart {
    s32 start = 0;       // +0x50 the countdown's start request
    s32 start_type = 0;  // +0x54: -1 none, 0 a countdown of `seconds`, 1 four seconds, 2 ten, 3 the rolling start
    s32 seconds = 0;     // +0x58
    s32 start_count = 0; // +0x5C the frames the cars are still held (the race's start countdown)
    s32 timer_on = 0;    // +0x60 the information's timer (event 7 at its end)
    s32 timer = 0;       // +0x68
};
// The race information's frame (the race's frame runs it before the race step): a start request sets the countdown
// from the start type (with its posts: 2 the countdown, 0x14 its sounds), then the countdown runs down (event 4 at its end)
// or, once it is over, the timer. Its countdown display is the game's. steps: the race's steps a second
// (steps_per_second of the race's behaviour; PAL's own release counts 50).
void race_info_frame(RaceInfoStart& info, std::vector<RaceEvent>& events, int steps = 60);

// One collision response as the collision step reports it to the race (the conductor's hit rule, then the car's
// hit record) [physics §5.3].
struct CollisionHit {
    s32 car = 0;  // the car whose body the response is for
    s32 kind = 0; // 0 a wall (one of the car's points on a wall, or a wall corner on its edge), 1 another car
    s32 part = 0; // the car's point (0..7) on a wall; its edge + 8 for a corner or a car on its edge; the striking car's vertex
    F strength;   // |j| for a wall, |j| / 15000 for a car
};

// A tyre smoke / dust particle as the course's tyre effect emitter makes it from a wheel's request (its 0x40-byte
// particle record; the pool, ageing and drawing are the game's).
struct SmokeParticle {
    s32 car = 0, wheel = 0;
    u8 size_class = 0; // +0x02 0..7 (a random draw), + 0x20 for a right-side wheel
    u8 kind = 0;       // +0x03 the effect kind: the surface's, its parameters at Env0 +0x4D0 + 0x44 kind
    u32 colour = 0;    // +0x04 RGBA
    F size;            // +0x08
    F emit;            // +0x0C the request's emission scaled by the size class
    F x10;             // +0x10
    F angle;           // +0x14 (degrees, random)
    F growth;          // +0x18
    s16 life = 0;      // +0x1C
    u8 x1e = 0, x1f = 0; // +0x1E / +0x1F
    F x, h, z;         // +0x20 the hub over the ground (course frame)
    F ground;          // +0x2C the ground plane's height at (x, z)
    Vec3 normal;       // +0x30 the ground plane (raised 0.05 m)
    F plane_d;         // +0x3C
};

class ExactRace {
public:
    // Throws std::runtime_error for a setup the port does not cover (no entries, more than 8, a course file without a road
    // block or with too short an Env0 block, a start the start port refuses: free practice on a course whose stop line has
    // no pit box for the car, a grid slot outside 0 .. 7). Nothing a step does throws.
    explicit ExactRace(const RaceSetup& setup);

    // One race frame (60 Hz): the start countdown, the race step, then the sparks' bursts and update (RaceSetup::sparks).
    // pads[i] is car i's frame (the pad read and the conductor's pad hook; a car without an input device has an empty one);
    // missing entries read as empty frames.
    void step(std::span<const PadFrame> pads);
    // The race step alone (as the race's frame calls it, after the countdown; no sparks), for a caller that drives the
    // race information's countdown itself (set_start_count).
    void race_step(std::span<const PadFrame> pads);

    // ---- the race information's start countdown (race info + 0x50 .. + 0x68) ----
    // the countdown starts at the next frame (by the start type: +0x5C = 60 x its seconds).
    void start_countdown();
    // Race info + 0x5C directly (the frames the cars are still held).
    void set_start_count(s32 n);
    s32 start_count() const { return info_.start_count; }
    s32 start_type() const { return info_.start_type; }
    const RaceInfoStart& info() const { return info_; }
    // The start type after the start (the race information's +0x54: the rolling start's hand-back reads it).
    void set_start_type(s32 t);
    // The information's timer (+0x60 / +0x68).
    void set_timer(s32 steps) { info_.timer_on = 1, info_.timer = steps; }
    // The race flow's time scale (conductor + 0x8000 + 0x37B0, 1; a slow replay lowers it) and byte (+0x37AC).
    void set_time_scale(F s) { time_scale_ = s; }
    F time_scale() const { return time_scale_; }
    u8 flow_byte() const { return flow_byte_; }
    u32 ghost_switch() const { return ghost_switch_; } // RaceSetup::ghost_switch
    void set_flow_byte(u8 b);
    // The free run's recorders before the next step (race_flow.h; the snapshots are the caller's, kept until then), and the
    // data logger's input objects (car i's frame, its table counting from its first entry).
    void set_recorder(const FreeRunRecorder& r) { flow_in_.recorder = r; }
    void set_logger_input(int i, u32 frame, bool relative);
    // The frames of the free run ghost's extra step (the race step once more in its second entry mode, inside a step: the
    // ghost's pad is read again); by default the step's own.
    void set_nested_pads(std::function<void(std::span<PadFrame>)> f) { nested_pads_ = std::move(f); }
    // The data logger (race info + 0x70 / + 0x74): its tables recorded or not from the next step (the Analyzer's pass ends:
    // + 0x74 cleared), its tables replaced (a race restarted to replay a pass: the pass's), and the Analyzer's REPLAY jump to
    // entry k: race info + 0x50 cleared; car 0 and, with a recording on the ghost's device, car 1 back on their
    // table's entry k (the record from the entry's snapshot, and its device's playback sought to the
    // entry's frame from the recording's start). False when car 0's table has no such entry (nothing changes).
    void set_logger_record(bool on) { flow_in_.logger_record = on; }
    void set_logger_tables(const std::vector<std::map<s32, Snapshot>>& t) { flow_.logger = t; }
    bool logger_jump(int k);
    // The solo race modes' recorders (RaceSetup::recorders; null without them): as the steps have left them.
    const SoloRecorders* recorders() const { return rec_ ? &*rec_ : nullptr; }
    SoloRecorders* recorders() { return rec_ ? &*rec_ : nullptr; }
    int ghost_car() const { return ghost_car_; }

    // ---- the race ----
    int size() const { return int(cars_.size()); }
    std::span<const Car> cars() const { return cars_; }
    // The records, writable for the game's own changes between steps (an AI take-over: drive_on below).
    std::span<Car> cars() { return cars_; }
    const RaceOrder& order() const { return order_; }    // conductor + 0xE388: car index per position
    const RaceFlow& flow() const { return flow_; }       // the race clock, end countdown, result words, records, the logger's
                                                         // tables, the last frame's recorder events
    const GapMatrix& gaps() const { return gaps_; }
    const Proximity& proximity() const { return prox_; }
    const std::vector<collide::exact::Body>& bodies() const { return bodies_; }
    std::vector<collide::exact::Body>& bodies() { return bodies_; }
    const Vu0Random& random() const { return rng_; }
    // The sparks (RaceSetup::sparks; none in a race without spark points): as the last frame left them.
    bool has_sparks() const { return sparks_on_; }
    const Sparks& sparks() const { return sparks_; }
    // The random register as the last frame's race step left it, before its sparks drew from it.
    const Vu0Random& random_before_sparks() const { return rng_step_; }

    // ---- the last frame's outputs ----
    bool ran() const { return ran_; }                                      // the race-flow gate let the step's phases run
    const std::vector<RaceEvent>& events() const { return events_; }       // the race's event queue posts
    const std::vector<PitEvent>& pit_events() const { return pit_events_; }
    const std::vector<CollisionHit>& hits() const { return hits_; }
    const std::vector<SmokeParticle>& particles() const { return particles_; }

    // ---- the race's fixed inputs ----
    Conductor kind() const { return kind_; }
    const RaceInputs& inputs() const { return race_; }
    const RaceFlowInputs& flow_inputs() const { return flow_in_; }
    const RaceCourse& course() const { return *course_; }
    const RubberBand& band() const { return band_; }
    const RaceConfig& rules() const { return cfg_; }

    // The AI's switches the game may use on a car between steps (pit_lane.h), on this race's AI course: the AI drives it on,
    // it is handed back to its player.
    bool drive_on(int i);
    bool hand_back(int i);
    // On car i between steps with `mode` (a race object's own end of a run outside the race flow: the free run's
    // pause EXIT counted as its finish, mode 4; the top-speed test's, mode 3): the car marked finished, the AI's end-of-run
    // driver (pit_lane.h end_of_run).
    void end_run(int i, int mode);
    // The same pause EXIT's recordings closed: the player's and, with `lap`, the free run's lap recording
    void close_recordings(bool lap);

    // ---- posed cars (an online race's remote players, docs/dev/ONLINE.md) ----
    // A posed car is not stepped here: its owner's machine runs it, and the caller writes its motion before every step (pose).
    // The step skips its phases (CarInputs::posed) but keeps it in phase 11 (the others' slipstream and proximity) and in the
    // collision step, whose result for it is thrown away (its record keeps the pose); the race flow, the ranking and the gap
    // matrices run on it as on every car, so its laps, finish and place are what this machine sees. Without posed cars the
    // step is GT3's.
    void set_posed(int i, bool on);
    bool posed(int i) const;
    // Car i's motion at the end of the coming step (pose_motion from src, e.g. a prediction of the car on its own one-car
    // race). The collision step starts its body back along it (position - velocity x dt, heading - yaw rate x dt).
    void pose(int i, const Car& src);
    // An impulse on car i in the horizontal plane (physics frame; N s at the point (px, py)), as the collision step's car
    // response changes a body: the velocity by j / m, the yaw rate by (r x j) / (10 I_z) (its body's im / ii). Online: a hit
    // only the other player's machine saw.
    void apply_impulse(int i, F px, F py, F jx, F jy);
    // The last step's car-car responses (the collision step's trace) with the contact point and the impulse on the struck car
    // (physics frame, N s; the striking car got the opposite).
    struct CarContact {
        s32 struck = 0, striker = 0;
        float x = 0, y = 0;
        float jx = 0, jy = 0;
    };
    const std::vector<CarContact>& car_contacts() const { return car_contacts_; }

private:
    void run(std::span<const PadFrame> pads, int mode); // conductor, mode: 1 the free run ghost's extra step
    void collide(size_t first);      // phase 10 (mode 1: for the cars from `first`)
    void emit(int i, const ChassisOutputs& out); // phase 12's emitter calls
    void spark_frame();              // The race's frame's bursts (per car), then the race's frame update's update
    void flow_after(AiPlan& plan, std::span<const PadFrame> pads, int mode); // after phase 13
    void copy_in();
    void restore_body(int k, const Snapshot& s); // the body
    PadFrame device_frame(size_t i, const PadFrame& given); // and the free run's pad hook for car i
    void apply_recorder_events();    // the free run's recorder events so far, on the recorders

    std::shared_ptr<const RaceCourse> course_;
    std::shared_ptr<const ProgramData> data_;
    Conductor kind_ = Conductor::SinglePlayer;
    RaceConfig cfg_{};
    RaceInputs race_;
    RubberBand band_;
    s32 band_mode_ = -1;
    bool band_cap_ = false;
    RaceFlowInputs flow_in_;
    PitInputs pit_in_;
    std::array<u32, 16> effect_kinds_{}; // a surface's effect kind
    std::array<F, 8> effect_scales_{};   // the size classes' emission scales
    RaceInfoStart info_;
    u8 flow_byte_ = 0;
    F time_scale_ = F::raw(0x3F800000);
    u32 x70_ = 0, ghost_switch_ = 0;
    bool effects_ = true;
    u8 category_ = 0;

    std::vector<Car> cars_;
    std::vector<collide::exact::Body> bodies_;
    RaceOrder order_{};
    RaceFlow flow_;
    GapMatrix gaps_;
    Proximity prox_;
    Vu0Random rng_, rng_step_;

    std::function<void(std::span<PadFrame>)> nested_pads_;
    std::optional<SoloRecorders> rec_;
    int ghost_car_ = -1;
    bool external_ghost_ = false;
    size_t rec_applied_ = 0; // the recorder events of the step made on rec_
    bool ran_ = false;
    std::vector<RaceEvent> events_;
    std::vector<PitEvent> pit_events_;
    std::vector<CollisionHit> hits_;
    std::vector<SmokeParticle> particles_;
    std::vector<std::vector<SparkPoint>> spark_points_;
    bool sparks_on_ = false;
    Sparks sparks_;
    std::vector<u8> hit_, hit_43c_;
    std::vector<u8> posed_;
    std::vector<CarContact> car_contacts_;
};

// The fields of a car's record that a posed car takes from its pose: the position, velocity, attitude and their rates, the
// body axes, speeds, the road segment hint, the step length and time scale, the steering, pedals, gear and engine speed, the
// pit-lane surface flag (F +0x535 bit 4). The race flow's fields (laps, sector, progress, finish, rank) stay the race's.
void pose_motion(Car& dst, const Car& src);

} // namespace gt3r::sim::exact
