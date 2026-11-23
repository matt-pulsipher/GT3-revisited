#pragma once

// The prize counter of the GT-mode prize pages (the race cups, the consolation prize, the
// championship trophy; presentation §F): the balance counting up by the prize, one
// step per frame, and the beep (SE "cursor") the steps play. The arithmetic is single
// precision, so the credits arrive on the same frames as in the original game.

#include "base/bytes.h"

namespace gt3r::game {

struct PrizeCountUp {
    s64 cur = 0, target = 0;  // the balance shown and where it stops
    float frac = 0;           // the summed steps' fraction not yet in the balance
    float v = 0, a = 0;       // velocity (the last step, stored back) and acceleration
    float speed = 1;          // 3 after the first Confirm
    float beep = 0;           // steps summed since the last beep
    int state = 1;            // 1 creep, 2 accelerate, 3 done
    int n = 0;                // steps taken
    int wait = 0;             // frames waited before the first step
    int presses = 0;          // Confirm presses while counting
    int beep_n = 0;           // steps since the last beep (held at 3)
    // Frames before the count starts: program constants of the item's three constructors,
    // 120 on the cup and consolation pages (0x1294E0, 0x1295E8), 360 on the trophy page
    // (0x1296F0); the same in the NTSC-J program (PAL: 100 / 100 / 300 for 50 Hz).
    int delay = 120;
    bool done() const { return state == 3; }
};

// One frame of the counter (`confirm`: Confirm pressed on this frame). Returns true when the
// beep plays on this frame. Frames 1 .. delay + 2 only wait; then each frame steps:
// - state 1 (the creep): v = 0.2380952 on every frame; after the 91st step state 2;
// - state 2: a += 5.2720418 (a at or over 474.48376 first snaps back to it and adds nothing;
//   v at or over 142345.125 snaps to it and drops a to 0);
// - step = (v + a) x speed, stored back as v; its whole part (with the fraction carried) goes
//   to the balance; at the target the count is done (no beep on that frame);
// - else the step is added to the beep sum and the step count; the beep plays when the sum
//   reached 1 and at least 3 steps passed since the last one (both reset); a sum of 1 or more
//   before the third step is held at 1.
// Confirm while counting: the first press sets the speed to 3 for the rest of the count (so
// the stored v triples on every accelerating frame), the second completes the count.
bool step_prize_count_up(PrizeCountUp& c, bool confirm);

} // namespace gt3r::game
