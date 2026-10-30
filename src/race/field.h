#pragma once

// A race as the race screen runs it: RaceField, over GT3's own race step (race/exact_field.h: the exact core's cars, AI and
// race flow). The player's car is exposed as a RaceVehicle: stepping it steps the whole race, so the race screen's
// fixed-tick loop, replays and HUD read every car the same way. Every race (with opponents, a split-screen battle, an
// online race, and the solo modes: time trial, licence test, machine test, free run, free practice) is one.
//
// Start: the race's own start (its countdown and holds; race_time < 0 during the countdown).

#include "race/backend.h"
#include "race/net_race.h"
#include "sim/car_spec.h"
#include "sim/program.h"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gt3r::data {
struct Event;
} // namespace gt3r::data
namespace gt3r::sim::exact {
class ProgramData;
enum class Conductor : u8;
struct RaceRecord;
struct SoloRecorders;
struct Snapshot;
} // namespace gt3r::sim::exact

namespace gt3r::race {

// A car as the game picks it, for the exact core's equipment (exact_setup.h): a label (a database car, table 30; a variant,
// 31; an arcade car, 35), optionally with its own parts and settings (a GT-mode garage car, a demonstration's car record)
// or an arcade car's tyre stage.
struct ExactCarSource {
    ExactCarSource() = default;
    ExactCarSource(std::string l, std::optional<std::array<u64, 27>> p = std::nullopt, std::optional<sim::CarSettings> s = std::nullopt,
                   int stage = -1)
        : label(std::move(l)), parts(std::move(p)), settings(std::move(s)), tire_stage(stage) {}
    std::string label;
    std::optional<std::array<u64, 27>> parts;  // the car's fitted parts (data::Car::parts order)
    std::optional<sim::CarSettings> settings;  // with the parts: its settings (none: the parts' stock settings)
    int tire_stage = -1;                       // an arcade car's tyres (10 racing, 11 drift, 12 dirt, + 13 wet)
};

// A human's race entry for the exact core: the car and what GT3's race information holds of the player (entry + 0x20E4 ..).
struct ExactHuman {
    ExactCarSource car;
    int pedal_style = 0;               // 0 the automatic gearbox, 1 the manual one (a pad) [physics §2.6]
    bool wheel = false;                // a steering wheel (the pad read's wheel path, steering-wheel type 1)
    // [race] analog_response = original: the race pad read as GT3 reads it (its pressure scales reach full throttle at
    // 18 % of a DualShock 2's pressure range, brake 33 %, handbrake 10 %; its stick curve has a dead band over the inner
    // 29 %). Default (linear): the same frames with every analog axis linear in the race pad's, so the steering,
    // throttle, brake and handbrake after phase 3's shaping follow the input.
    bool original_response = false;
    bool asm_on = true, tcs_on = true; // the Options' ASM / TCS (entry + 0x20F0 / + 0x20F4)
    sim::EngineCondition condition;    // the run's start
    float dirt = 0;                    // the car's dirt (condition word 3)
};

// A race on the exact core (exact_field.h): GT3's own race step, cars and race flow.
struct ExactFieldOptions {
    std::shared_ptr<const sim::exact::ProgramData> data;
    // Whose rules the race follows (sim/program.h): unset, the disc's own release's (`data`'s behaviour, the races the player
    // drives); online races and the game's demos (attract, quick-menu demonstration, licence demonstrations) NTSC-U's, on a copy
    // of `data` with that behaviour. Every race steps 60 times a second (tick_hz).
    std::optional<sim::Program> rules;
    Bytes course_file;          // the course file (its road, AI lines, driver sets, Env0)
    std::string course;         // its label (the course record's category)
    sim::exact::Conductor kind{}; // the race mode (SinglePlayer, Battle2P, ...)
    u8 config = 0;              // the race-flow byte (the race's play kind): 0 live, 2 the attract demo (every car a CPU car),
                                // 3 the AI drives the players' cars (the pad decoded)
    int laps = 0;               // the race's laps (0: the event's time limit)
    ExactHuman player, second;
    // A replay: the race as recorded (sim/exact/race_record.h), built from its setup and stepped from its frames and changes
    // (the cars, grid, laps and words above are not used; the players' pads and inputs are ignored).
    std::shared_ptr<const sim::exact::RaceRecord> playback;
    // A whole-run ghost (time trial, licence test, machine test): car 0 of that run's record, as GT3 races it: a car of this
    // race after the players (its entry the run's car 0's; the solo conductors' entry set-up flags it a ghost), its device
    // playing the run's recording (RaceRecord::run_recording: every frame but the last, then empty ones). The screen draws it
    // from the race (exact_ghost).
    std::shared_ptr<const sim::exact::RaceRecord> ghost;
    // The solo modes (kind TimeTrial, License, MachineTest, FreeRun, FreePractice: the player alone, no opponents) and the race
    // information's words they read: the session's best sector times and lap (race info + 0x8AC .. + 0x8BC, ms; 0x157529FF
    // none: the visit's, the screen carries them from run to run), the licence test's judging times (gold, silver, bronze;
    // ms), a licence test that is not judged (+0x80), the speed display's unit (0 km/h, 1 mph).
    std::array<u32, 5> session{0x157529FF, 0x157529FF, 0x157529FF, 0x157529FF, 0x157529FF};
    std::array<u32, 3> licence_times{};
    bool unjudged = false;
    u8 speed_unit = 0;
    // The free run's ghost car without a whole-run ghost (car 1, the player's car once more: the start flags it a ghost),
    // driven by the race's recorders (sim::exact::SoloRecorders: its lap recordings, best lap and the ghost's playback);
    // `ghost_shown`: the race object's ghost switch (+0x217A4: the ghost car is drawn, translucent); `ghost_option`: the Options
    // menu's Ghost (on: a lap better than the ghost's becomes the ghost at once).
    bool free_run_ghost = true, ghost_shown = true, ghost_option = true;
    // The visit's free run recorders (GT3's free run keeps its race object, and with it its best lap's store and the ghost's
    // playback, from run to run until the player leaves): the race starts from them and, when the field is done, leaves them
    // as its run left them (the screen holds them for the visit). Null: the race's own, empty at its start.
    std::shared_ptr<sim::exact::SoloRecorders> free_run;
    // The data logger (race info + 0x70 / + 0x74): the race plays its run for the logger (the marks are timed; the cars drawn
    // as the logger draws them) and, with `logger_record`, records each recorded car's table at the course's logger marks
    // (exact_logger_tables: the Analyzer's pass over a replay). A replay's own words are kept otherwise.
    bool logger = false, logger_record = false;
    // The Analyzer's race (a replay's, `playback` its track A; the time trial, licence test and machine test): GT3's logger
    // race (the solo race's devices, both playing): car 0 plays track A's recording, car 1, a ghost car with track B's
    // car, plays `analysis_b`'s (none: no car 1); `logger_tables` (optional) the tables of a pass over it, which the race's
    // jumps (exact_logger_jump) restore from.
    std::shared_ptr<const sim::exact::RaceRecord> analysis_b;
    bool analysis = false;
    std::shared_ptr<const std::vector<std::map<s32, sim::exact::Snapshot>>> logger_tables;
};
// The solo conductors: the time trial, licence test, machine test, free run and free practice (the player alone; free
// practice with the race's field, FieldConfig::opponents).
bool exact_solo_kind(sim::exact::Conductor k);

struct FieldConfig {
    // The race on the exact core: the program, rules, course, mode, players and solo words (make_exact_field).
    ExactFieldOptions exact;
    std::string event;                  // event label (opponents, AI parameters, rubber band); may be empty
    std::vector<std::string> opponents; // car (variant) labels; empty = the event's entries
    int max_opponents = 5;
    // GT-mode grid: opponents ranked by whole kg per whole power (gt_grid_places), the power in HP (BHP, CV) when `grid_hp`
    // (the language's Units::power_hp, as its menus show it), else PS; otherwise they take the slots in the given order
    // (arcade: the random draw).
    bool grid_by_power = false;
    bool grid_hp = false;
    // The split-screen battle's options (its Options page, docs/dev/RACE.md "Options"): the catch-up for the
    // trailing player (0 off, 1 low, 2 high), the handicap in metres (> 0: player 2 starts back, < 0: player 1), tyre damage.
    int battle_catchup = 0;
    float handicap_m = 0;
    bool battle_wear = false;
    // Online battle (docs/dev/ONLINE.md): the other players' cars, on their grid slots, posed from the network before each
    // step (RaceField::set_remote_pose), never stepped here.
    struct Remote {
        std::string car; // car label
        int slot = 0;
        // The car as raced (a garage car's parts and settings, an arcade car's tyres; empty label: `car`) and its engine
        // condition.
        ExactCarSource source;
        sim::EngineCondition condition;
    };
    std::vector<Remote> remote;
    int player_slot = -1; // the local player's grid slot (-1: behind the opponents)
    // The grid a free practice left (race::practice_grid): entry k's slot, 0 the player, k opponent k - 1; empty (or not
    // one slot per car): the order above.
    std::vector<int> grid_slots;
    // Free practice: each entry's best lap so far (ms, entry + 0x11D8: GT3's race information keeps it from run to run, and
    // the conductor's restart, starts each car's best lap from it); empty (or not one per car): none.
    std::vector<u32> entry_best;
};

// How an event starts (event record, database table 32; docs/dev/RACE.md "Starts"):
//   +0x198 u8 initial speed (km/h; 0 for every race), +0x199 u8 laps, +0x1B5 u8 cars
//   (player included), +0x1E4 u8 time limit (minutes), +0x1E6 s16 flying-start
//   distance (metres before the line, negative), +0x1F2 u8 countdown seconds (type 0),
//   +0x1F3 u8 start type: 0 demo / machine test, 1 standing (240 frames), 2 standing
//   (600 frames, every single race), 3 rolling (GT endurance) or flying (time attack,
//   moving licence tests: initial speed > 0 at the flying-start distance).
struct EventStart {
    int type = 2;
    int laps = 0, cars = 0;
    float initial_kmh = 0;
    double time_limit_s = 0;
    float flying_m = 0;    // metres before the line (positive)
    double countdown = 10; // seconds the cars are held (0 = no hold)
    bool rolling() const { return type == 3 && initial_kmh <= 0; }
    bool flying() const { return type == 3 && initial_kmh > 0; }
};
EventStart event_start(const data::Event& ev);
// Rolling start: the computer drives the player's car for this long after the start (300 frames).
constexpr double kRollingAutoDrive = 5.0;

// GT Mode's starting grid (gt_mode §22.1; GT3's GT race grid and championship grid). A car's figure is its
// whole kg (the showroom spec's mass, sim::exact::Showroom::mass) over the whole power the menus show for it (HP, BHP or CV
// where the language shows them, else PS: sim::exact::shown_power), truncated: gt_grid_figure (exact_grid_figure builds a
// car's). The opponents take the grid from pole, the lowest figure first and equal figures in entry order; the player
// starts behind them. gt_grid_places gives each opponent's place (0 = pole) from the figures in entry order.
int gt_grid_figure(int mass_kg, int power);
std::vector<int> gt_grid_places(std::span<const int> figures);

class RaceField {
public:
    // nullptr with *error set when the race cannot be set up (make_exact_field).
    static std::unique_ptr<RaceField> create(const RaceTrack& track, const data::CarDatabase& db,
                                             const FieldConfig& cfg, std::string* error = nullptr);
    virtual ~RaceField() = default;

    virtual RaceVehicle& player() = 0;
    // The player's start pose (behind the opponents).
    virtual TrackPose player_grid() const = 0;

    // Opponents (index 0..n-1).
    virtual int opponents() const = 0;
    virtual const std::string& opponent_label(int i) const = 0;
    virtual const std::string& opponent_model(int i) const = 0; // cars/day/<model>
    virtual const sim::VehicleState& opponent_state(int i) const = 0;
    virtual const sim::VehicleState& opponent_prev_state(int i) const = 0; // before the last tick (interpolation)
    virtual const sim::VehicleSetup& opponent_setup(int i) const = 0;
    virtual int opponent_laps(int i) const = 0;
    virtual int opponent_lap_count(int i) const = 0; // lap_counter: 0 before the start line

    // Seconds since the start signal (< 0 during the countdown).
    virtual double race_time() const = 0;
    // Player's position (1 = leader) in the race order (standings()), as the HUD shows it.
    virtual int player_position() const = 0;
    // Race time of the player's / second player's finish (finishes_at_line), < 0 = still racing.
    virtual double player_finish() const = 0;
    virtual double second_finish() const = 0;
    virtual int contacts_last_tick() const = 0;
    virtual const std::vector<std::string>& labels() const = 0; // opponent labels as configured

    // Second human player (2P battle), when configured. Its inputs are set
    // before each player().step() and used by that step.
    virtual bool has_second() const = 0;
    virtual void set_second_inputs(const sim::ControlInputs& in) = 0;
    virtual void set_second_reverse(bool on) { (void)on; } // player 2's reverse request
    virtual const sim::VehicleState& second_state() const = 0;
    virtual const sim::VehicleState& second_prev_state() const = 0;
    virtual const sim::VehicleSetup& second_setup() const = 0;
    virtual TrackPose second_grid() const = 0;
    virtual float second_boost() const = 0;
    // The last step's telemetry and the make-up of player 2's car and of opponent i (null / nullopt: none).
    virtual const sim::VehicleTelemetry* second_telemetry() const = 0;
    virtual const sim::VehicleTelemetry* opponent_telemetry(int i) const = 0;
    virtual std::optional<CarFacts> second_facts() const = 0;
    virtual std::optional<CarFacts> opponent_facts(int i) const = 0;
    // Player 2's race pad for the next step (RaceVehicle::set_pad).
    virtual void set_second_pad(const PadState* pad) { (void)pad; }
    // How often the race steps (the one place the screen takes the rate from: 60 for every race a player drives; a
    // disc's own demo on a PAL disc runs at 50).
    virtual double tick_hz() const { return sim::kDefaultTickHz; }
    // The brake pedal the car was given on the last tick (lamp state).
    virtual float opponent_brake(int i) const = 0;
    // Strongest car-car impulse on opponent i in the last step (N s), for its clash sound.
    virtual float opponent_car_impulse(int i) const { (void)i; return 0; }
    virtual float second_brake() const = 0;
    virtual float second_wall_impulse() const = 0;
    virtual float second_car_impulse() const = 0;
    virtual float second_impact() const { return 0; } // RaceVehicle::last_impact
    struct LapInfo {
        int laps = 0;
        bool started = false, wrong_way = false;
        double current = 0, last = -1, best = -1, race_distance = 0;
    };
    virtual LapInfo second_laps() const = 0;
    virtual int second_position() const = 0;

    // The race order: every car (player = -1, second player = -2, opponents 0..n-1), ranked
    // by ranks_ahead after every tick (ties keep the previous order).
    struct Standing {
        int car = -1;
        bool finished = false;
        double time = 0; // race time at the finish (s)
        int laps = 0;    // laps completed (frozen at the finish)
        double distance = 0;
    };
    virtual void set_laps(int laps) = 0;
    // The split-point / line crossings of every car on the last tick, the cars taken in the
    // race order before that tick's ranking (the leader first), as the original's crossing
    // handler takes them (presentation §D6).
    struct CarCrossing {
        int car = -1; // as Standing::car
        LapCrossing crossing;
    };
    virtual const std::vector<CarCrossing>& last_crossings() const = 0;
    // Opponents that entered the pit lane since the last call ("%s Pitting in...").
    // (-1 / -2: player 1's / 2's car, from a field that drives the players' pit lane itself.)
    virtual std::vector<int> take_pit_entries() = 0;
    virtual bool opponent_in_pits(int i) const = 0;
    virtual std::vector<Standing> standings() const = 0;

    // Online battle: remote players (opponents with FieldConfig::remote entries).
    virtual bool opponent_remote(int i) const { (void)i; return false; }
    // The remote car's pose at the end of the coming tick (used by the next step).
    virtual void set_remote_pose(int i, const RemoteCar::Pose& p) { (void)i, (void)p; }
    // Its owner's finish time (the owner is the authority), or that it left (no contact any more).
    virtual void set_remote_finish(int i, double race_time) { (void)i, (void)race_time; }
    virtual void set_remote_retired(int i) { (void)i; }
    virtual bool opponent_retired(int i) const { (void)i; return false; }
    // Contacts of the last step between the player's car and remote cars: the impulse the remote
    // car felt (its owner applies it if its machine missed the hit), and when the player's car last
    // touched each remote car (race time; < -1e9 = never).
    struct RemoteHit {
        int opponent = -1;
        float point_x = 0, point_z = 0; // world
        float jx = 0, jz = 0;           // impulse on the remote car, N*s
    };
    virtual std::vector<RemoteHit> take_remote_hits() { return {}; }
    virtual double last_contact_with(int i) const { (void)i; return -1e18; }
    // When this machine saw a remote car complete the race distance (race time; < 0 = not yet).
    virtual double remote_observed_finish(int i) const { (void)i; return -1; }
    // A hit on the player's car that another player's machine saw (contact message).
    virtual void apply_player_impulse(float point_x, float point_z, float jx, float jz) { (void)point_x, (void)point_z, (void)jx, (void)jz; }
    // A ghosted remote car is drawn translucent and has no contact (lobby ping threshold).
    virtual void set_remote_ghost(int i, bool on) { (void)i, (void)on; }
    virtual bool opponent_ghost(int i) const { (void)i; return false; }
};

// The race rules of the original (presentation §B9).
// Ranking: more laps completed first; at equal laps two running cars by race distance, a
// running car ahead of a finished one, two finished cars by finish time.
bool ranks_ahead(const RaceField::Standing& a, const RaceField::Standing& b);
// The game's lap counter of a car: 0 before its first crossing of the start line, then the lap
// it is on (1 during lap 1). The replay cameras' lap variant comes from it (presentation §H6).
constexpr int lap_counter(bool started, int completed) { return started ? completed + 1 : 0; }

} // namespace gt3r::race
