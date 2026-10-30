#include "race/time_text.h"

#include "sim/exact/race_flow.h"

#include <cmath>
#include <cstdio>

namespace gt3r::race {

// The menus' formatter: the placeholder for exactly "no time", else unsigned
// divisions into hours, minutes, seconds and milliseconds.
std::string menu_time_text(u32 ms) {
    if (ms == sim::exact::kNoTime) return std::string(kNoTimeText);
    const u32 s = ms / 1000, m = s / 60, h = m / 60;
    char b[32];
    if (h) std::snprintf(b, sizeof b, "%u:%02u'%02u.%03u", h, m % 60, s % 60, ms % 1000);
    else std::snprintf(b, sizeof b, "%u'%02u.%03u", m, s % 60, ms % 1000);
    return b;
}

std::string menu_time_text(double seconds) {
    if (!(seconds >= 0)) return std::string(kNoTimeText);
    const double ms = std::round(seconds * 1000.0);
    return menu_time_text(ms >= 4294967295.0 ? u32(0xFFFFFFFFu) : u32(ms));
}

} // namespace gt3r::race
