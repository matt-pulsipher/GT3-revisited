#pragma once

// A race on the exact core: RaceField over sim::exact::ExactRace (GT3's own race step, its cars' records and its race flow),
// so the race screen, HUD, sounds, cameras, results and GT-mode rewards run on it unchanged.
//
// Car 0 is player 1 (car 1 player 2 in a split-screen battle), the opponents follow (opponent i = car humans + i), as GT3
// orders its race entries. The players' cars read their race pads through the pad read (pad_read.h; RaceVehicle::set_pad),
// or, where the screen gives no pad (scripts, demos, replays), a frame made from its ControlInputs. Everything else is the
// race's own: the start countdown and the holds, the AI of every CPU car and of a player's car when it drives it (the
// rolling start's run-up, the pit lane, the end of a run), laps, splits, finishes, the ranking, the rubber band, tyre wear
// and pit stops. No respawns (the original has none). Online races: below.

#include "net/protocol.h"
#include "race/field.h"
#include "sim/exact/tick.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gt3r::sim::exact {
class ExactRace;
struct RaceRecord;
struct SoloRecorders;
struct Snapshot;
} // namespace gt3r::sim::exact

namespace gt3r::race {

// nullptr with *error set when the race cannot be set up (no event / course data the exact core needs, a start the port
// refuses). cfg.exact must be set.
std::unique_ptr<RaceField> make_exact_field(const RaceTrack& track, const data::CarDatabase& db, const FieldConfig& cfg,
                                            std::string* error = nullptr);
// The exact race behind a field (nullptr for another field), and the pad frames its last step gave the cars (the players'
// from their pads or inputs, empty frames for the others): for tests, replays and the online code.
const sim::exact::ExactRace* exact_race_of(const RaceField& f);
const std::vector<sim::exact::PadFrame>* exact_frames_of(const RaceField& f);
// Each car's best laps of the run (ms; sim::exact::kNoTime for none), player 1's first, then each opponent's (empty for another
// field): the car's (car + 0x16C4: every lap, and in free practice its entry's at the start) and its race entry's best lap's
// splits' (entry + 0x11D8: laps without a course-out, from none at the run's start).
struct ExactBestLaps {
    std::vector<u32> car, entry;
};
ExactBestLaps exact_best_laps(const RaceField& f);
// Player 1's laps as the race times them, for RaceTrack::set_lap_source (nullptr for another field).
const LapSource* exact_lap_source(const RaceField& f);
// The race's record so far (sim/exact/race_record.h): its setup, every frame's pads and the game's changes between frames, the
// race's hash after the last frame (a copy; nullptr for another field). A replay keeps it; ExactFieldOptions::playback runs it
// again. A field playing a record gives that record.
std::shared_ptr<const sim::exact::RaceRecord> exact_recording(const RaceField& f);
// The race's record closed now (pause EXIT as the run's finish, presentation §E5: the run's replay ends at the press): from
// here exact_recording gives the record as it is now while the race runs on. False for another field or a field playing a
// record.
bool exact_close_recording(RaceField& f);
// Pause EXIT counted as the run's finish (presentation §E5) as GT3's free run and top-speed test make it: the record closed
// (exact_close_recording), the race's own recordings closed (the player's; the free run's lap recording) and car 0's end of
// run: the AI's end-of-run driver takes it (`top_speed_test`: slowed, as at the test's goal). False for another field or a
// field playing a record.
bool exact_exit_finish(RaceField& f, bool top_speed_test);
// Whether a field playing a record has played all its frames and ended on the recorded race (its hash); nullopt while it
// plays, for a live field or another field.
std::optional<bool> exact_playback_matches(const RaceField& f);
// The race's ghost car (a car flagged a ghost, F +0x3EA: ExactFieldOptions::ghost, the free run's), its state between the
// last two steps at `alpha` and its geometry; false when the race has none or it is not drawn now (the draw mode: not
// while the computer drives it, the flying start's run-up).
bool exact_ghost(const RaceField& f, float alpha, sim::VehicleState& state, const sim::VehicleSetup** setup = nullptr);
// The frame the field gives a player's car for a step without a pad (scripts, the test autopilot, replays of inputs):
// every axis analog (kind 2; kind 3 for the d-pad's ControlInputs::controller_kind 3), the steering +-4096 positive left,
// throttle / brake 0..4096, the handbrake on the third axis, the shift request; with `reverse` the back axis at 4096.
// (A pad-read step's frame comes back from its recorded inputs: the screen records them from exact_frames_of.)
sim::exact::PadFrame exact_inputs_frame(const sim::ControlInputs& in, bool reverse);
// A hit of `car` the collision step does not see (kind 2: the course's rigid bodies, the screen's pylons; licence tests and
// time trials fail on it): the conductor's hit rule (hit_course_out) before the next step, kept in the record. False for
// another field.
bool exact_report_hit(RaceField& f, int car, int kind);
// The solo modes' recorders (sim/exact/race_flow.h SoloRecorders: the player's recording, the ghost's playback, the free run's
// lap recording and best lap) as the race's steps have left them: nullptr for another field or race mode.
const sim::exact::SoloRecorders* exact_solo_recorders(const RaceField& f);
// The free run's best lap becomes the ghost (1, 0: the quick menu's "Update Ghost?"): false for another field.
bool exact_best_lap_to_ghost(RaceField& f);
// LOAD GHOST in a free run: another free run's best lap becomes the live run's ghost (SoloRecorders::ghost_from_best; the
// visit's recorders take it from the run as it ends): false for another field, or when `from` has no best lap.
bool exact_ghost_from_best(RaceField& f, const sim::exact::SoloRecorders& from);
// The Analyzer's REPLAY jump (ExactRace::logger_jump) to the logger's entry k of a field built with ExactFieldOptions::analysis;
// `step`: the record's frame the race plays on from (its changes from there). False for another field or no such entry.
bool exact_logger_jump(RaceField& f, int k, u32 step);
// The field is an Analyzer race (ExactFieldOptions::analysis, built).
bool exact_analysis_field(const RaceField& f);
// Car i's input device's frame (race input device +0xFC: its recorder's frames recorded or played; 0 without the solo recorders).
u32 exact_device_frame(const RaceField& f, int i);
// The data logger's recording stopped from the next step (the Analyzer's pass is over: race info + 0x74 cleared).
void exact_logger_record(RaceField& f, bool on);
// The data logger's tables (ExactFieldOptions::logger_record; sim/exact/race_flow.h RaceFlow::logger): per car its entries at
// the course's logger marks (the car at the entry's start, its input frame, the frames and the time to its end); nullptr for
// another field.
const std::vector<std::map<s32, sim::exact::Snapshot>>* exact_logger_tables(const RaceField& f);
// The frame a player's race pad gives (ExactHuman: the wheel path, the linear or original analog response).
sim::exact::PadFrame exact_pad_frame(const PadState& pad, const ExactHuman& h, const sim::exact::ProgramData& d);

// Online races (docs/dev/ONLINE.md; FieldConfig::remote with the exact core's Conductor::BattleMP): the other players' cars
// are opponents posed from their owners' states (RaceField::set_remote_pose with RemoteCar::Pose::car), never stepped here.
// The prediction shadow of remote opponent i: a one-car race of its car, entry and grid slot on this race's course, with no
// laps to finish and no tyre effects (null for another field or an index that is not a remote car, or when the race cannot
// be built).
std::unique_ptr<sim::exact::ExactRace> exact_shadow(const RaceField& f, int opponent, const net::CarEntry& entry);
// Player 1's entry flags for its snapshots (null for another field).
std::optional<net::CarEntry> exact_player_entry(const RaceField& f);

} // namespace gt3r::race
