#include "game/prize_count_up.h"

namespace gt3r::game {

namespace {

// Program constants of the counter's step 0x1292C0 (called by PrizeCounterControlItem::vf2
// 0x129FB8): the creep's 90 steps (90.0f at 0x129360), the creep velocity 0x34FAF0, the
// acceleration cap 0x34FAF4, the acceleration 0x34FAF8 and the velocity cap 0x34FAFC. The
// NTSC-J program (0x128A40, 0x349D6C..) holds the same values and counts its x100 sums with
// them unchanged. (The PAL program's, for its 50 Hz frames, are 75 steps, 0.2857143,
// 569.3805, 7.591740 and the same cap.)
constexpr int kCreepSteps = 90; // the creep's last step is the one after the 90th
constexpr float kCreepVelocity = 0.2380952388048172f, kAccel = 5.2720417976379395f;
constexpr float kAccelCap = 474.4837646484375f, kVelocityCap = 142345.125f;

} // namespace

bool step_prize_count_up(PrizeCountUp& c, bool confirm) {
    if (c.state == 3) return false;
    if (c.wait < c.delay + 2) {
        ++c.wait;
        return false;
    }
    if (confirm) {
        if (c.presses == 0) c.speed = 3.0f;
        else c.cur = c.target;
        ++c.presses;
    }
    ++c.n;
    float inc = 0;
    if (c.state == 1) {
        c.v = kCreepVelocity;
        if (float(c.n) > float(kCreepSteps)) c.state = 2; // this frame still steps as the creep
    } else if (c.state == 2) {
        // Both caps test the old values, so a passes its cap for one frame.
        inc = kAccel;
        if (c.a >= kAccelCap) c.a = kAccelCap, inc = 0;
        if (c.v >= kVelocityCap) c.v = kVelocityCap, c.a = 0, inc = 0;
    }
    // One float operation per statement: nothing fused or kept at a wider precision.
    c.a = c.a + inc;
    const float t = c.v + c.a;
    const float step = t * c.speed;
    const float sum = c.frac + step;
    c.v = step;
    const s32 whole = s32(sum);
    c.cur += whole;
    c.frac = sum - float(whole);
    if (c.cur >= c.target) {
        c.cur = c.target;
        c.state = 3;
        return false;
    }
    c.beep = c.beep + step;
    ++c.beep_n;
    if (c.beep >= 1.0f) {
        if (c.beep_n >= 3) {
            c.beep = 0, c.beep_n = 0;
            return true;
        }
        c.beep = 1.0f;
    } else if (c.beep_n >= 3) {
        c.beep_n = 3;
    }
    return false;
}

} // namespace gt3r::game
