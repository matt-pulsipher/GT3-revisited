#pragma once

// Machine tests (presentation §B7, §D6): 0-400 m, 0-1000 m and the top-speed test.
//
// - The event's lap-count byte (+0x199) is the test: 1 = 0-400 m, 2 = 0-1000 m, 3 = top speed;
//   4 or more reads as 0, which runs like the top-speed test. A machine test has no laps.
// - Distances are track distances from the control line within a lap, counted once the car
//   has crossed the line (the car starts on the grid at the line, so the distance counts from
//   its crossing, not from GO). The test's timing points are the first 1, 2 or 3 of 400 m, 1000 m and
//   6845 m; the last one is the goal, where the run ends (the top-speed test ends 6845 m into
//   the test course's first lap; on a course shorter than the goal a run never ends). The
//   split points are taken in order, starting again at every line crossing; a tick that
//   crosses the line times nothing else. Line crossings themselves count for nothing: no lap,
//   no lap time.
// - Times run from GO; a mark passed within a tick is timed like a lap line (the clock before
//   the tick plus the passed fraction of it, track::LapTimer::crossing_ms).
// - Pop-ups (green, queued, no gaps), the time tests only: "T<n> m'ss.mmm" at each split point
//   and the time to the goal like a lap time (sw_20). The top-speed test's HUD has no event
//   display, so it shows none; its HUD has two speed lines instead of the times (race::Hud).
//   Its replays, on the replay layout, show its split times and, at the goal, its top speed.
// - The time tests rank and record the time to the goal, the top-speed test its top speed.

#include "race/lap_events.h"

#include <array>
#include <string>
#include <vector>

namespace gt3r::race {

class MachineTest {
public:
    static constexpr std::array<float, 3> kMarks{400.0f, 1000.0f, 6845.0f}; // metres past the line

    // From the event's test byte (its lap-count field).
    explicit MachineTest(int type_byte = 1) : type_(type_byte >= 0 && type_byte < 4 ? type_byte : 0) {}

    int type() const { return type_; }
    // Ranked by the time to the goal (0-400 m, 0-1000 m), else by the top speed.
    bool timed() const { return type_ == 1 || type_ == 2; }
    // Timing points with the goal (1, 2 or 3) and the goal's distance.
    int points() const { return type_ == 1 ? 1 : type_ == 2 ? 2 : 3; }
    float goal_m() const { return kMarks[size_t(points() - 1)]; }

    struct Crossing {
        int point = 0;     // 0-based; points() - 1 is the goal
        bool goal = false;
        long long ms = 0;  // since GO
    };
    // Back to the start of a run.
    void reset() { next_ = 0, done_ = false; }
    // One tick: the car's unwrapped track distance from the control line before and after it
    // (< 0 behind the line, lap k from k x lap_m), the lap length and the race clock (s since
    // GO) before and after the tick. Returns the timing points passed: the next split point (at
    // most one a tick), then the goal, which ends the run (nothing counts after it).
    std::vector<Crossing> update(double before_m, double after_m, double lap_m, double t_prev, double t);
    bool done() const { return done_; }

private:
    int type_ = 1;
    int next_ = 0; // the next split point
    bool done_ = false;
};

// A top speed in the records (the result's "Max Speed", the quick menu's Top 3, GT Mode's
// records page), in the speed unit: whole units and tenths, both truncated, and the unit
// ("312.4km/h", "194.1mph"); 0 or less kNoSpeedText (the font's dashes) and the unit.
std::string record_speed_text(double kmh, bool imperial);

// A time test's pop-up for a crossing: the split "T<n> m'ss.mmm" or the time to the goal (as
// a lap time).
EventDisplay::Message machine_test_message(const MachineTest::Crossing& c);
// The top-speed test's pop-up at the goal (shown in its replays, presentation §D6): the top
// speed so far in the speed unit, rounded to tenths, and the unit ("312.4km/h"); green sw_16,
// queued.
EventDisplay::Message machine_speed_message(double kmh, bool imperial);

} // namespace gt3r::race
