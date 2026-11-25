#pragma once

// The clock for animations and seeds that follow real time (a car fading, a showroom's turn,
// the replay cameras' clock-value seeds). Normally the wall clock. With fixed steps (--shot:
// every frame 1/60 s of game time) it is the frames stepped so far, so frame N is the same
// picture on every run, with a window or without (platform/windowless.h).

#include "base/bytes.h"

namespace gt3r::frame_clock {

// Nanoseconds: SDL_GetTicksNS, or with fixed steps a count that starts at 1 s.
u64 now_ns();
// A seed the game takes from a clock value: SDL_GetPerformanceCounter, or now_ns with fixed steps.
u64 counter();
// A seed for a random generator: std::random_device, or a constant with fixed steps.
u32 random_seed();

bool fixed();
// From now on fixed steps (App::init, --shot).
void set_fixed();
// Fixed steps: one frame of `ns` more (App::iterate, once a frame).
void step(u64 ns);

} // namespace gt3r::frame_clock
