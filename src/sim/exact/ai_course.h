#pragma once

// The AI course object K (conductor + 0xBBA8; the AI driver object's +0x08) [ai §2]: the course's AI lines as the AI course
// setup 0x1F9B98 / 0x1F92F8 leaves them (its port fills them from our parse of the course file), and the program's line
// queries in EE arithmetic: the fallback 0x1F9BF8, the span search 0x1F9F08, the line's position at a race distance 0x1FA018
// and the stop line's 0x1FA368, the side map 0x1FA530 and the passing-line lookups 0x1F9C80 / 0x1F9CB0 / 0x1F9CE0 / 0x1F9D38.

#include "sim/exact/car_record.h"

#include <array>
#include <vector>

namespace gt3r::sim::exact {

// A line's control point (0x28 bytes in the course file's slot 31, converted in place by the course setup) [ai §2.2].
struct AiPoint {
    u8 kind = 0;          // +0x00 the segment from here: 0 straight, 1 / 2 arc (the course setup's line builder writes 1 / 2)
    s8 brake_adjust = 0;  // +0x01 braking-distance adjustment (%)
    s8 speed_adjust = 0;  // +0x02 corner speed adjustment (km/h)
    u8 x03 = 0;           // +0x03
    F x, y;               // +0x04 / +0x08 position (physics frame)
    F height;             // +0x0C the road's height at the point (0 where the setup's ground probes miss)
    s16 seg = 0;          // +0x10 road segment
    s8 surface = 0;       // +0x12 surface index of the road at the point
    u8 x13 = 0;           // +0x13
    F dist;               // +0x14 race distance (line 4: a lap added from its first point in the first half of the lap on)
    F radius;             // +0x18 signed arc radius (0 on straights)
    F slope, bank;        // +0x1C / +0x20 the road's slope along the heading and its bank across it (rad)
    F heading;            // +0x24 (rad)
};

struct AiCourse {
    F lap_length;           // the course's road block +0x18 (0x18FA10: [K +0x00] -> course -> road)
    bool table = false;     // K +0x04 is set: the course file has the AI line section (slot 31)
    // K +0x04 -> +0x08 + 4 k: game line k (file line k - 1) [ai §2.1]; empty: the slot is null (no line, or no points).
    std::array<std::vector<AiPoint>, 8> lines;
    bool pit_lines = false; // K +0x08: lines 3 and 4 are both present
    // The program's table 0x2DA508: per requested line, the lines tried in order, -1 ending the row [ai §2.1].
    std::array<std::array<s8, 6>, 8> fallback{};
    Behaviour behaviour;    // the program's (its atan2f and, on 2002 Tokyo-Geneva, its folded constants: build_math.h)

    // 0x1F9BF8: the first line of the request's fallback row that is present (a request outside 0..7 asks for 6), -1 none.
    int resolve(int line) const;
    // 0x1F9F08: the span of line `line` that holds race distance s: 1 before its first point, 2 at or past its last (both
    // from = the last point, to = 0), 3 the first span from = k - 1 .. to = k with dist(from) <= s < dist(to) found trying
    // k = hint, hint + 1, hint - 1, hint + 2, ... (wrapped once into the line, k = 0 skipped), 0 none (from / to untouched).
    // `beyond` (optional) is set when the search reads a point outside the line, which the program reads from whatever
    // follows the line in memory (here those k are skipped).
    int span(int line, int hint, F s, int& to, int& from, bool* beyond = nullptr) const;
    // 0x1FA018: the position (x, y) and height h of line `line` at race distance s (wrapped once into the lap), searched
    // from point `hint`: straight segments interpolate, arcs turn about their centre [ai §2.3]. False when the span search
    // finds nothing; the outputs are then untouched (as in the program).
    bool evaluate(int line, int hint, F s, F& x, F& y, F& h) const;
    // 0x1FA368, the stop line's (line 4) evaluation: s wrapped into the lap and a lap added in its first half (as the
    // setup did to the line's distances), straight interpolation in the first span with dist(k) <= s < dist(k + 1) (a point
    // where none: the last span's start). Returns the span's first point, -1 outside the line (outputs untouched).
    int evaluate_stop(int line, F s, F& x, F& y, F& h) const;
    // 0x1FA530: the side map (line 5) swaps left and right at race distance s: the kind byte of the span search's `from`
    // point (hint: the driver's next point, 0 when past the map's count); false without a map or a span.
    bool side_swapped(int hint, F s) const;
    // 0x1F9C80: from lines 1 and 6, line 2 (B) if present; 0x1F9CB0: from lines 2 and 6, line 1 (A) if present; else -1.
    int line_b(int line) const;
    int line_a(int line) const;
    // 0x1F9CE0 / 0x1F9D38: the passing line to the left / right of `line` at race distance s: B / A, swapped where the side
    // map says so [ai §8.2].
    int lane_left(int line, int hint, F s) const;
    int lane_right(int line, int hint, F s) const;
};

// 2002 Tokyo-Geneva's (Europe 0x1F6E38, Rule::AiDecide): the heading of line `line` at race distance s, from its position
// there to the first point at least 0.2 m on in 0.5 m steps (at most 10), atan2(-dx, dy); the stop line (4) by its own
// evaluation.
F ai_line_heading(const AiCourse& k, int line, int hint, F s);

} // namespace gt3r::sim::exact
