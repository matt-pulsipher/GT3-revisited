#include "race/machine_test.h"

#include "race/time_text.h"
#include "track/track.h"

#include <cmath>
#include <cstdio>

namespace gt3r::race {

std::vector<MachineTest::Crossing> MachineTest::update(double before_m, double after_m, double lap_m, double t_prev,
                                                       double t) {
    std::vector<Crossing> out;
    if (done_ || lap_m <= 0) return out;
    // Within one lap only: a tick across the line times nothing else, and a forward crossing
    // starts the split points again; behind the line before the first crossing nothing counts.
    const double lap_b = std::floor(before_m / lap_m), lap_a = std::floor(after_m / lap_m);
    if (lap_a != lap_b) {
        if (lap_a > lap_b) next_ = 0;
        return out;
    }
    if (lap_a < 0) return out;
    const double base = lap_a * lap_m;
    const int goal = points() - 1;
    // The next split point only, then the goal on its own (presentation §B7).
    if (next_ < goal) {
        const double d = base + kMarks[size_t(next_)];
        if (before_m < d && after_m >= d) {
            out.push_back({next_, false, track::LapTimer::crossing_ms(before_m, after_m, d, t_prev, t)});
            ++next_;
        }
    }
    const double g = base + goal_m();
    if (before_m < g && after_m >= g) {
        out.push_back({goal, true, track::LapTimer::crossing_ms(before_m, after_m, g, t_prev, t)});
        done_ = true;
    }
    return out;
}

std::string record_speed_text(double kmh, bool imperial) {
    const char* unit = imperial ? "mph" : "km/h";
    if (kmh <= 0) return std::string(kNoSpeedText) + unit;
    const float v = imperial ? float(kmh) * 0.62139f : float(kmh);
    char b[32];
    std::snprintf(b, sizeof b, "%d.%1d%s", int(v), int(v * 10.0f) % 10, unit);
    return b;
}

EventDisplay::Message machine_test_message(const MachineTest::Crossing& c) {
    EventDisplay::Message m; // green, queued (presentation §D6)
    m.text = c.goal ? lap_time_text(c.ms) : split_text(c.point + 1, c.ms);
    m.large = c.goal;
    return m;
}

EventDisplay::Message machine_speed_message(double kmh, bool imperial) {
    EventDisplay::Message m; // green, sw_16, queued
    const float v = imperial ? float(kmh) * 0.62139f : float(kmh);
    char b[32];
    std::snprintf(b, sizeof b, "%.1f%s", double(v), imperial ? "mph" : "km/h");
    m.text = b;
    return m;
}

} // namespace gt3r::race
