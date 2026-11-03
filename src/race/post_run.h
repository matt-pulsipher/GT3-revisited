#pragma once

#include <string_view>

// What follows a live run, as the original's race drivers sequence it after every run
// (presentation §A8, §C4, §C5, §F; menus §11), the quick menu's backdrop, and the pause
// window's EXIT that counts as a finish (presentation §E):
//
// - Every run but a licence test and the free practice before a GT race plays its replay by
//   itself once it is over, finished or not: the arcade race after its win trophy, the GT race
//   and the championship round after their prize page, the time trial, free run and machine
//   test after their record's name entry; the 2P battle always. A replay plays once, from the
//   start of the run to the frame the live run ended (or to a pause EXIT that counted as a
//   finish), with no result screens; then the quick menu.
// - The race-exit transition runs over the last frame of the live run and again over the last
//   frame of every replay; the replay and the menu cut in.
// - Behind the time trial's, free run's and machine test's quick menus (and the GT race's and
//   championship's pre-race menus) the race runs as a replay of the last run in play mode 2:
//   records off, a random view mode, no end: once its recording runs out the cars get a cleared
//   input frame (no steering, throttle or brake) and the race goes on. Before any run the
//   recording is empty. The result menus of the arcade race, 2P battle, GT race and
//   championship have no race behind them (their layouts' own background); the licence menu
//   shows the run (or the demonstration) in its preview box.
// - Pause EXIT ends a live run at once, except in a free run with a completed lap and in the
//   top-speed machine test once its car has moved (and has not reached its goal): there it is
//   the run's finish (the record overlay, the end-of-run driver, the recording closed at the
//   press; the top-speed test shows its overlay after 0.1 s instead of 5 s). A second EXIT
//   ends the run.

namespace gt3r::race {

// The runs the original's drivers tell apart after a run.
enum class RunKind {
    ArcadeRace,   // arcade single race (also our debug races with a field)
    TwoPlayer,    // 2P battle
    TimeTrial,    // arcade time trial challenge (one timed lap)
    FreeRun,      // arcade time attack, GT Mode free run on a course (no laps to run)
    MachineTest,  // 0-400 m, 0-1000 m, top speed
    Licence,      // licence test
    GtRace,       // GT Mode single race
    Championship, // a championship round
    FreePractice, // the free run before a GT race (back to its pre-race menu)
    Other,        // online battles, the attract demo, the Replay Theater: none of this
};

// How a live run ended.
struct RunOutcome {
    bool finished = false; // the player's car finished (a time trial's FAIL is not a finish)
    int place = -1;        // races: the player's place, 0 = first; -1 none
    bool ranked = false;   // its record made the ranking: the name entry is queued
};

// The steps after a live run, in this order; then the quick menu (the free practice: the GT
// race's pre-race menu).
struct PostRun {
    bool prize = false;      // the GT race / championship prize page, the arcade win trophy
    bool name_entry = false; // the record's name entry
    bool replay = false;     // the run's replay plays by itself
};
PostRun post_run(RunKind kind, const RunOutcome& o);

// What the quick menu shows behind its layout.
enum class MenuBackdrop {
    None,    // no race (the result menus)
    Race,    // the race in play mode 2: the computer drives on and on (a run alone, a field after its countdown)
    Preview, // the licence menu's preview box
};
// `before_run`: the menu that opens a GT race, championship round, time trial, free run or
// machine test (also after a free practice).
MenuBackdrop menu_backdrop(RunKind kind, bool before_run);

// How a race is played: the live run, a replay (the automatic one, the REPLAY item), the quick
// menu's backdrop (play mode 2).
enum class PlayMode { Live, Replay, Backdrop };
// A replay ends when its recording has run out; the backdrop never ends.
bool replay_over(PlayMode m, bool recording_done);

// Which items of a quick menu's root page can be chosen (menus §11). The original's race
// drivers keep these switches for a visit (one driver call: every run, retry and menu until
// EXIT) and the root page asks them by item name; a disabled item stays focusable, is drawn
// darkened under its layout's `disable` widget and refuses confirm with SE "disable" (menus §3,
// §5.3). Dialog pages have no rule.
struct QuickMenuItems {
    // Set by the visit's runs.
    bool replay = false;         // REPLAY, SAVE REPLAY: a run to replay
    bool session_record = false; // SAVE GHOST, DATA LOGGER: a run of the visit set a session record
    bool exit_locked = false;    // EXIT, SAVE GAME: the championship's last round has been run
    // Fixed for the visit.
    bool setting_locked = false;  // SETTING (and save / load setting)
    bool free_run_locked = false; // FREE RUN: the free practice before a GT race
    bool enabled(std::string_view item) const;

    // The fixed switches: the time trial has no SETTING (nor its layout the item), an arcade
    // race or free run with a car of the arcade's own (not a garage car) neither; a GT race on
    // a dirt course has no FREE RUN (the championship's pre-race menu keeps it).
    void visit(RunKind kind, bool garage_car, bool dirt_course);
    // After a live run (not a pause RETRY, which restarts within the run): REPLAY, except
    // after a licence test left before its end (the demonstration is loaded in its place); a
    // session record (the time trial's lap, a 0-400 m / 0-1000 m goal, a judged licence run at
    // or under the session's best; a free run's lap under it) enables SAVE GHOST and DATA
    // LOGGER for the rest of the visit; a championship's last round locks EXIT and SAVE GAME
    // (NEXT then ends the championship).
    void after_run(RunKind kind, bool ended, bool record, bool last_round);
    // The time trial's and licence test's DEMONSTRATION loads its run over the last one.
    void after_demonstration() { replay = false; }
};

// The pause window's EXIT in a live run.
struct PauseExitRun {
    RunKind kind = RunKind::Other;
    bool top_speed_test = false; // the machine test ranks its top speed
    bool finished = false;       // the run is already over (a second EXIT)
    int laps_completed = 0;
    float top_speed_mps = 0;     // the run's highest speed so far
};
// True when it counts as the run's finish; false: the run ends at once.
bool pause_exit_finishes(const PauseExitRun& r);
// Seconds from such a finish to the record overlay (5 s after any other finish).
double exit_finish_overlay_delay(const PauseExitRun& r);
// The record overlay's usual delay after a finish (presentation §B7).
constexpr double kRecordOverlayDelay = 5.0;
// ... and after the top-speed test's EXIT.
constexpr double kTopSpeedExitOverlayDelay = 0.1;

} // namespace gt3r::race
