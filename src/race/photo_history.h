#pragma once

// Photo mode's recent past (ours, race/photo_camera.h): the cars' states after each of a replay's
// last ticks, so a shutter longer than the paused tick can draw the cars where they were before
// it. The race screen pushes every car's state (the player's first, then the opponents') after
// each tick it plays; a tick that does not follow the last one pushed (a seek, a new replay)
// starts the past again.
//
// Times count in ticks from the newest state: 0 is the state after the last tick, -1 the one
// before it, and so on; between two states a car is interpolated as between frames.
//
// SDL-free (tests).

#include "base/bytes.h"
#include "race/interp.h"

#include <deque>
#include <optional>
#include <vector>

namespace gt3r::race {

class PhotoHistory {
public:
    static constexpr int kStates = 12; // the newest and the 11 before it

    void clear() { states_.clear(); }
    // The cars after tick `tick`.
    void push(u32 tick, std::vector<sim::VehicleState> cars);
    // How far back it reaches (ticks before the newest state; 0 with one state, -1 with none).
    int span() const { return int(states_.size()) - 1; }
    // Car `car` at time `t` (clamped to the past kept); none without a state of it.
    std::optional<sim::VehicleState> at(size_t car, float t) const;

private:
    struct Tick {
        u32 tick = 0;
        std::vector<sim::VehicleState> cars;
    };
    std::deque<Tick> states_; // oldest first
};

} // namespace gt3r::race
