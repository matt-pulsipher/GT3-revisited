#include "race/photo_history.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gt3r::race {

void PhotoHistory::push(u32 tick, std::vector<sim::VehicleState> cars) {
    if (!states_.empty() && (tick != states_.back().tick + 1 || cars.size() != states_.back().cars.size())) states_.clear();
    states_.push_back({tick, std::move(cars)});
    while (int(states_.size()) > kStates) states_.pop_front();
}

std::optional<sim::VehicleState> PhotoHistory::at(size_t car, float t) const {
    if (states_.empty() || car >= states_.back().cars.size()) return std::nullopt;
    const int n = int(states_.size());
    if (n == 1) return states_.back().cars[car];
    // From the oldest state: x in 0..n-1.
    const float x = std::clamp(t + float(n - 1), 0.0f, float(n - 1));
    const int i = std::min(int(std::floor(x)), n - 2);
    return interpolate(states_[size_t(i)].cars[car], states_[size_t(i) + 1].cars[car], x - float(i));
}

} // namespace gt3r::race
