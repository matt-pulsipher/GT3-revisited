#pragma once

// The pop-ups in the middle of the race screen (presentation §D6): the lap time when a lap
// is completed, the split time at the course's split points, the gap to another car (with
// that car's picture) and "+N Laps" for a car a lap or more down, and the event display
// that shows them.
//
// - Lap time: green (73, 204, 43) "m'ss.mmm" in sw_20; split: green "T<n> m'ss.mmm" in sw_16;
//   both queued (one after the other), 3 s each, at the display's anchor (one player:
//   (320, 130) of the 640 x 448 frame, centred, y the baseline).
// - Races: PassingOrder records, per timing point, the cars in the order they pass on the
//   lead lap. A car behind the first one through gets "+gap" to it; when the second car
//   passes, the first one gets "-lead" (its lead over the second); a car on an earlier lap
//   gets "+N Laps".
// - Time trials and free runs: the gap to the best lap so far (BestLapSplits).
// - A gap is red (240, 0, 0) when behind (>= 0), light blue (128, 128, 255) when ahead, sw_16,
//   3 s, shown at once (in parallel with the current group), 25 units under the anchor;
//   with the picture of the car in race position 1 (when behind) or 2 (when ahead): the text
//   37 units to the right (left) and the picture 62 units to the left (right), blinking off
//   for 12 frames three times.

#include "base/bytes.h"
#include "race/hud_layout.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace gt3r::race {

// What a run's event display is given (presentation §D6, §D9). A replay re-runs the race, so
// it posts what the live run did, except: no gap to the session's best lap (time trials, free
// runs), no lap counts for the session (the best lap and its split times, Record Lap), a
// licence test's time at its end comes as a lap time (shown in replays only), and the top-speed
// test's split times and top speed show (the replay layout has the display its live one
// lacks). Demonstrations and the attract demo get none (ours).
struct LapEventRules {
    bool lap_times = false;     // the lap time at every line crossing (machine tests: the goal's time)
    bool splits = false;        // the split times
    bool race_gaps = false;     // races: the gap to the first car through, "+N Laps"
    bool best_lap_gaps = false; // time trials, free runs: the gap to the session's best lap
    bool session_laps = false;  // the laps count for the session (the gaps' reference, Record Lap)
    bool license_time = false;  // licence tests: the time at the end, as a lap time
    bool top_speed = false;     // the top-speed test: its top speed at the goal
};
LapEventRules lap_event_rules(HudRun run, HudPlay play);

// The event display that shows a car's messages (0 the single view's or player 1's half, 1
// player 2's half), or -1 for none. Live: each player's own (car -1 / -2). Replays: the car
// the cameras follow (`followed`, as the car ids: -1 the player, >= 0 an opponent), on the
// single view (presentation §D6, §G3).
int event_display_of(int car, HudPlay play, int followed);

// Race gaps: who passed each timing point first on the lead lap.
class PassingOrder {
public:
    static constexpr int kPoints = 4; // up to 3 split points and the line
    static constexpr int kCars = 8;   // cars kept per point and lap

    void reset();
    // A car passes timing point `point` (0..kPoints-1) on lap index `lap` at `ms`. Returns
    // the laps it is behind the first car through (> 0: "+N Laps" at once), else 0: the pass
    // is kept, in time order among this tick's passes, for take_gaps().
    int pass(int car, int lap, int point, long long ms);
    struct Gap {
        int car = -1;
        long long ms = 0; // > 0: behind the first car through; < 0: the first car's lead
    };
    // The gaps of the passes kept since the last call; call once per tick after every pass.
    std::vector<Gap> take_gaps();

private:
    struct Point {
        int lead_lap = -1;
        int count = 0, done = 0;
        std::array<long long, kCars> ms{};
        std::array<int, kCars> car{};
    };
    std::array<Point, kPoints> points_{};
};

// Time trials and free runs: the session's best lap so far and its split times, the gaps'
// reference, taken before the lap updates it. A session starts with none (no stored record
// seeds it: the first lap shows no gap) and lasts the whole visit, its runs and retries alike.
class BestLapSplits {
public:
    static constexpr int kPoints = 4;
    BestLapSplits() { reset(); }
    void reset();     // a new session: no best lap
    void begin_run(); // a new run of the session: the current lap's split times go, the best stays
    std::optional<long long> split_gap(int point, long long split_ms) const;
    std::optional<long long> lap_gap(long long lap_ms) const;
    void split(int point, long long split_ms); // the current lap's split time
    void lap_done(long long lap_ms);            // kept, with its split times, when faster
    long long best_lap() const { return best_lap_; }

private:
    std::array<long long, kPoints> best_{}, cur_{};
    long long best_lap_ = -1;
};

// "m'ss.mmm" (the HUD's time format).
std::string lap_time_text(long long ms);
// "T<number> m'ss.mmm".
std::string split_text(int number, long long ms);
// A licence test's split number: its conductor counts the splits on over the laps, the line short of the run's end
// included. Point k of lap l (from 0) is l x points + k + 1, the line ending lap l (l + 1) x points; `points`: the lap's
// timing points with the line.
int license_split_number(bool line, int lap, int point, int points);
// '+' and the time for a gap >= 0, the font's dash glyph (0x7F) for < 0.
std::string gap_text(long long gap_ms);
// '+' (dash glyph when negative) and `format` ("%d Laps") with |laps|.
std::string lap_diff_text(int laps, const std::string& format);

// The event display: eight message slots and two timers, the end of the queue and the start
// of the current parallel group, both counting down a frame at a time.
class EventDisplay {
public:
    static constexpr int kSlots = 8;
    static constexpr float kDuration = 3.0f;
    static constexpr float kFrame = 1.0f / 60.0f;

    struct Message {
        std::string text;
        bool large = false;  // sw_20 (lap time), else sw_16
        u8 r = 73, g = 204, b = 43;
        float dx = 0, dy = 0; // from the display's anchor (640 x 448 units)
    };
    // Queued: starts when the messages queued before it have run out.
    void queue(Message m, float duration = kDuration);
    // In parallel with the current group (normally at once).
    void parallel(Message m, float duration = kDuration);
    // `delay` (> 0) seconds after the current group's start, in parallel with it.
    void after(Message m, float delay, float duration = kDuration);
    // A gap message, with the other car's picture when `picture` (`car` is the caller's id
    // for it): text and picture offsets by the gap's sign, started in parallel.
    void gap(long long gap_ms, bool picture, int car = 0);
    // A lap-down message ("+N Laps"), in parallel.
    void lap_down(std::string text);

    // One frame (the original updates after the frame's events, before drawing).
    void tick();
    void clear();

    // Messages to draw now.
    std::vector<const Message*> visible() const;
    struct Icon {
        bool on = false;
        int car = 0;
        float dx = 0, dy = 0;
    };
    // The car picture to draw now.
    Icon icon() const;

    // Seconds until the queue has run out / since the current group started (for tests).
    float queue_end() const { return queue_end_; }
    float group_start() const { return group_start_; }

private:
    struct Slot {
        Message m;
        float elapsed = 0, duration = 0;
        bool used = false;
    };
    void add(Message m, float duration, float delay);
    std::array<Slot, kSlots> slots_{};
    float queue_end_ = 0, group_start_ = 0;
    struct IconState {
        bool set = false;
        int car = 0;
        float dx = 0, dy = 0, elapsed = 0, duration = 0;
        int blink = 0;
    } icon_;
};

} // namespace gt3r::race
