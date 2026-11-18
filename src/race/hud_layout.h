#pragma once

// The race display's layout per mode (presentation §D4, §D5, §D6, §D7, §D9): which HUD parts a
// mode shows and where. The rows are the game's own layout tables (database/racemode.db and
// racedetail.db; test `race_hud_layout_disc` checks every row against the disc): a part
// without a row is not drawn at all in that mode.
//
// - Positions are in the 640 x 448 frame. `align`: 0 left, 1 right, 2 centre, + 4 pushed
//   outward in 16:9 (Hud's anchor()); `valign` likewise for top / bottom / centre.
// - `view`: the row applies to one camera view only (0 the in-car view, 1 the outside
//   views), or to both (kAnyView).
// - Meters sub 0 / 1 are the in-car and outside meter clusters; sub 64 the outside view's back
//   panel alone (the replay's small cluster, the 2P panel).
// - TimeLine sub picks the value and its label (TimeLine below); w / h of a P.P. line are the
//   car picture's offset.
// - Rank and Map: w / 100 is the scale.

#include "base/bytes.h"

#include <span>

namespace gt3r::race {

// The display modes the game picks per run (the numbers are its layout table's).
enum class HudMode : u8 {
    Race = 0,          // single races, GT races and championships, online
    Endurance = 1,     // a race of 10 laps or more, or with a time limit
    License = 2,
    MachineTime = 4,   // machine tests 0-400 m / 0-1000 m
    FreeRun = 6,       // arcade and GT free runs
    Battle = 7,        // 2P, 4:3 (top / bottom halves of 640 x 224)
    BattleWide = 8,    // 2P, 16:9 (side by side halves of 426.667 x 448)
    MachineSpeed = 9,  // the top-speed machine test
    Replay = 11,
    Demo = 12,         // demonstrations
    FreePractice = 13, // the free run before a GT race
    TimeTrial = 14,    // the arcade time trial
    Logger = 15,       // the Analyzer's REPLAY: the logger display over track A's (and track B's) car
};

enum class HudPart : u8 {
    Rank = 1, LapCount = 2, Meters = 3, Boost = 4, Tires = 5, Messages = 6 /* WRONG WAY, AUTO DRIVE */,
    Events = 7 /* the pop-ups */, Target = 8 /* licence target time */, Map = 9, TimeLine = 11,
    Steering = 12, AccelBrake = 13, OnboardTach = 14, RemoteTach = 15, BarTach = 16, Speedometer = 17,
    DigitalSpeed = 18, Gear = 19, Odometer = 20, Trip = 21, Lamps = 22, CarName = 23,
    ViewMode = 24 /* the replay view mode indicator */, Events2 = 25, Title = 26 /* course name, FINISH */,
};

// The time lines: the value shown and its label.
enum class TimeLine : u8 {
    LapTimes = 1,     // the last laps' times, numbered (the lap-time list)
    Total = 2,        // "Total": the laps run so far and the current one
    Best = 3,         // "Best": the best lap
    Last = 4,         // "Last": the last lap
    Lap = 5,          // "Lap": the current lap
    BestTime = 6,     // "Best", as Best
    Record = 7,       // "Record": the session's fastest lap
    RecordLap = 8,    // "Record Lap": the session's fastest lap
    PolePosition = 9, // "P.P.": the session's fastest lap, with that car's picture
    Time = 10,        // "Time": the current lap (the licence clock)
    RecordSpeed = 11, // "Record": a speed the game never sets (dashes)
    BestSpeed = 12,   // "Best": the highest speed so far
    Fastest = 13,     // "Fastest": the session's fastest lap
    TotalTime = 14,   // "Time": as Total
};

constexpr u8 kAnyView = 127;

struct HudRow {
    HudPart part;
    u8 sub;
    u8 view;
    s16 x, y, w, h;
    u8 align, valign;
};

std::span<const HudRow> hud_layout(HudMode m);
// Another release's rows in place of the built-in ones (Gran Turismo Concept's discs: the same tables with other rows,
// concept §9.3): its database/racemode.db and racedetail.db, read as the game reads them (a mode's 32 row ids; a
// row's part, sub, alignments, view and box), the empty rows left out. A mode the tables do not hold keeps the
// built-in rows. False (and nothing changes) when a table does not parse. clear_hud_tables(): the built-in rows again.
bool set_hud_tables(ByteSpan racemode, ByteSpan racedetail);
void clear_hud_tables();
// The mode's row for a part (and sub, < 0 any) in a view (0 in-car, 1 outside); null if none.
const HudRow* hud_row(HudMode m, HudPart part, int sub, int view);
// The same in the built-in rows (Gran Turismo 3's), whatever tables are set: what a drawing tuned on GT3's rows
// compares a disc's row with.
const HudRow* hud_builtin_row(HudMode m, HudPart part, int sub, int view);
// The label a time line shows ("" for the lap-time list).
const char* time_line_label(TimeLine t);

// Which mode a run uses (presentation §D4), as the game picks it.
enum class HudRun : u8 { Race, TimeTrial, FreeRun, FreePractice, License, MachineTime, MachineSpeed, Battle };
enum class HudPlay : u8 { Live, Replay, Demo };
struct HudModeSelect {
    HudRun run = HudRun::Race;
    int event_laps = 0;      // the event's laps
    int time_limit_min = 0;  // the event's time limit (minutes), 0 = none
    bool wide = true;        // 2P: side by side (16:9), else top / bottom
    HudPlay play = HudPlay::Live;
    bool battle_split_replay = false; // a 2P replay drawn as both halves
};
HudMode hud_mode(const HudModeSelect& s);

// Lines of the lap-time list: the run's laps when 1..3, else 3 (free runs count 0 laps).
int lap_list_lines(int laps);

} // namespace gt3r::race
