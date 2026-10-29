#pragma once

// Hook between the interactive menus and gameplay. When the menus reach a
// point where the original starts a race (arcade single race / time trial,
// GT-mode event, license test, ...) the menu screen raises a GameRequest.
// Gameplay code (e.g. the race workstream's time trial) registers a handler
// from its own file without touching the menu code:
//
//   static const bool reg = [] {
//       gt3r::menu::flow::set_game_request_handler([](const gt3r::menu::flow::GameRequest& r, gt3r::App& app) {
//           if (r.kind != gt3r::menu::flow::GameRequest::Kind::StartRace) return false;
//           app.push(make_time_trial_screen(r.course, r.car));
//           return true;
//       });
//       return true;
//   }();
//
// Without a handler, the menu screen logs the request and shows it on screen.

#include "sim/car_spec.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gt3r {
class App;
}

namespace gt3r::menu::flow {

struct GameRequest {
    // PlayMovie: a full-screen movie (VOL path in `course`), e.g. a GT-mode ending.
    enum class Kind { StartRace, Quit, PlayMovie };
    Kind kind = Kind::StartRace;
    // What to run: arcade "single_race", "time_trial", "free_run", "two_player_battle";
    // GT mode "go_race", "go_license", "go_machine_test", "go_free_run", "go_preview";
    // Replay Theater "theater_replay" (replay_file), "theater_demo" (the disc's demonstration
    // of `event`), "logger" (the Analyzer: replay_file = track A, replay_file2 = track B).
    std::string mode;
    std::string race_code;  // the game's race code (e.g. "AEST0001", "Gff_e_0001", "L0A0001"), may be empty
    int difficulty = -1;    // arcade single race: 0 easy, 1 normal, 2 hard, 3 professional
    std::string course;     // course id chosen in the menus ("akasaka", "r_akasaka", "tt01", ...; empty = default)
    std::string car;        // car id chosen in the menus (pmb element name without a leading '_'; empty = default)
    std::string car_class;  // arcade class chosen ("class_c", ...)
    std::string page;       // menu page the request came from

    // GT mode (filled by game/gt_mode_actions.cpp; empty / null elsewhere):
    std::string event;      // event record label (database table 32), e.g. "Gsunday_e_0001", "L0B0001"
    // The player's garage car: its fitted parts and settings (the race builds its car from them; `car` names the base car).
    std::optional<std::array<std::uint64_t, 27>> car_parts;
    std::optional<sim::CarSettings> car_settings;
    std::string car_model;  // car model id for the garage car ("ni0137"), may be empty
    int car_color = 0;      // paint index of the garage car
    std::string car_wheel;  // fitted aftermarket wheel (wheel/<label>, CarModelOptions::wheel_override), "" = stock
    // Arcade car page choices: transmission 0 automatic / 1 manual, tyres 0 racing /
    // 1 drift (-1 = not chosen, e.g. GT mode).
    int transmission = -1;
    int tire_mode = -1;
    // 2 Player Battle: player 2's car, class, paint and choices ("" / -1 = none).
    std::string car2, car2_class;
    int car2_color = 0, transmission2 = -1, tire_mode2 = -1;
    // Player 2's garage car (arcade garage classes) as car_parts / car_model / car_wheel are
    // player 1's: its fitted parts and settings, model and bought wheels.
    std::optional<std::array<std::uint64_t, 27>> car2_parts;
    std::optional<sim::CarSettings> car2_settings;
    std::string car2_model, car2_wheel;
    bool racing_gearbox = false; // the garage car has a non-stock transmission (gear part +0x10 != 0): gear whine
    // The engine condition player 1's car starts the run with (physics §10.3), for the modes
    // that carry it and hand it back (GT race, machine test, free run, arcade race;
    // game::GtMode::carries_engine_condition): the garage car's odometer (2 m units),
    // running-in / wear and oil (metres); an arcade car's are zeros. garage_car: its index
    // in the GT-mode garage (-1 = none). car_condition = false: a fresh engine, nothing back.
    bool car_condition = false;
    std::uint32_t car_odometer = 0;
    float car_wear_m = 0, car_oil_m = 0;
    int garage_car = -1;
    bool racing_gearbox2 = false; // the same for player 2's garage car (2 Player Battle)
    // Opponents (car variant labels) chosen by GT mode, e.g. a championship
    // field kept for every round; empty = draw them from the event's entries.
    std::vector<std::string> opponents;
    // Championship round (GT mode): 1-based round and round count, 0 = not a championship.
    int champ_round = 0;
    int champ_rounds = 0;
    bool champ_last = false;
    // A practice run from our Quick race screen: GT mode events (licence tests, machine
    // tests) run as the game runs them, but nothing goes into the GT Mode save (no medals,
    // records, prizes) and the GT Mode garage settings / records menus are not offered.
    bool practice = false;
    // Replay Theater / Analyzer: replay files ("" = none) and the Analyzer's view
    // (0 REPLAY, 1 GRAPH).
    std::string replay_file, replay_file2;
    int logger_view = 0;
};

// What gameplay reports back when a requested race ends (or is abandoned).
// The race screen posts it before it pops itself; the menus take it when
// they resume (GT mode awards prizes / license medals from it).
struct GameResult {
    std::string mode;       // GameRequest::mode
    std::string race_code;  // GameRequest::race_code
    std::string event;      // GameRequest::event
    std::string course;     // GameRequest::course (the menus fill it in when empty)
    bool finished = false;  // race completed (false = retired / quit / license test failed)
    int position = 0;       // 1-based finishing position, 0 = unknown / not classified
    int cars = 0;           // cars in the race (player included)
    double total_time = -1; // seconds, < 0 = none
    double best_lap = -1;   // seconds, < 0 = none
    std::vector<double> lap_times;
    // The request carried an engine condition (GameRequest::car_condition): the player's car's
    // words when the last live run ended, the odometer units (2 m each) the runs added, and
    // the request's garage_car.
    bool car_condition = false;
    std::uint32_t car_odometer = 0, odometer_added = 0;
    float car_wear_m = 0, car_oil_m = 0;
    int garage_car = -1;
    // Every car in finishing order (finished cars by time, then by race
    // distance): an index into GameRequest::opponents, -1 = the player.
    std::vector<int> order;
    double car_dirt = -1;      // the car's dirt after the race (0..0.8), < 0 = not tracked
    double top_speed_kmh = -1; // machine test / any race: the player's top speed (< 0 = unknown)
    // The player quit a championship ("exit sure"): the championship ends (a round already
    // run still counts).
    bool abandoned = false;
    // The live race was started (START in the pre-race menu, or a run with no such menu); false
    // when the player left from the pre-race menu: GT3 counts and records only a started race
    // (gt_mode §3.4; START).
    bool started = true;
    // License tests judged by the race: -2 = not judged (GT mode judges from
    // finished + total_time), -1 = failed / no medal, 0 gold, 1 silver, 2 bronze.
    int license_medal = -2;
    std::string failure;    // why a test failed ("out of the course", "over the times"), "" when OK
    // The race entered runs of this visit into their rankings (licence tests, machine tests,
    // time trials, free runs: it records each run, with its name entry, when it ends, as the
    // original does); the menus never record from a result, they only save it.
    bool records_done = false;
    // The race already showed the race prize page (cup / consolation prize, the arcade win
    // trophy) itself, before its replay.
    bool prize_shown = false;
};

void post_game_result(GameResult r);
std::optional<GameResult> take_game_result();

// Returns true if the request was handled (the menu then waits for gameplay
// to return, e.g. when the pushed screen is popped).
using GameRequestHandler = std::function<bool(const GameRequest&, App&)>;

void set_game_request_handler(GameRequestHandler h);
const GameRequestHandler& game_request_handler();

} // namespace gt3r::menu::flow
