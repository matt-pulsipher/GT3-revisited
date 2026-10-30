#include "race/field.h"
#include "race/exact_field.h"

#include "base/bytes.h"
#include "game/data/car_database.h"

#include <algorithm>
#include <limits>

namespace gt3r::race {

EventStart event_start(const data::Event& ev) {
    EventStart e;
    e.laps = ev.laps;
    e.cars = ev.cars_in_race;
    ByteSpan b = ev.bytes;
    if (b.size() < 0x1F4) return e;
    e.initial_kmh = float(b[0x198]);
    e.time_limit_s = 60.0 * double(b[0x1E4]); // u8 (+0x1E5 is the league byte)
    e.flying_m = -float(s16(rd16(b, 0x1E6)));
    e.type = b[0x1F3];
    switch (e.type) {
    case 0: e.countdown = double(b[0x1F2]); break;
    case 1: e.countdown = 4.0; break;
    case 2: e.countdown = 10.0; break;
    default: e.countdown = 0; break;
    }
    return e;
}

// Integer division, truncating. A power of 0 has no figure in GT3; ours puts such a car last.
int gt_grid_figure(int mass_kg, int power) { return power > 0 ? mass_kg / power : std::numeric_limits<int>::max(); }

// The grids' ranking: each opponent counts the opponents with a lower figure (equal ones neither), then
// takes that place or, when an earlier opponent has it, the next free one.
std::vector<int> gt_grid_places(std::span<const int> f) {
    const size_t n = f.size();
    std::vector<int> ahead(n, 0), place(n, 0);
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            if (f[i] != f[j]) ++ahead[f[j] < f[i] ? i : j];
    std::vector<bool> taken(n, false);
    for (size_t i = 0; i < n; ++i) {
        size_t p = size_t(ahead[i]);
        while (taken[p]) ++p;
        taken[p] = true;
        place[i] = int(p);
    }
    return place;
}

bool ranks_ahead(const RaceField::Standing& a, const RaceField::Standing& b) {
    if (a.laps != b.laps) return a.laps > b.laps;
    if (a.finished != b.finished) return !a.finished; // a finished car's place on the lap stopped at the line
    if (a.finished) return a.time < b.time;
    return a.distance > b.distance;
}

std::unique_ptr<RaceField> RaceField::create(const RaceTrack& track, const data::CarDatabase& db,
                                             const FieldConfig& cfg, std::string* error) {
    return make_exact_field(track, db, cfg, error);
}

} // namespace gt3r::race
