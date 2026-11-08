#pragma once

// The opponent draw of GT3's race setup: every
// race that takes its field from an event's entry pool (GT-mode races, the
// first championship round, arcade Single Race, the attract demo) draws the
// cars with the race module's generator (race_info_random()), which then
// assigns the paint colours.

#include "base/game_random.h"

#include <string>
#include <vector>

namespace gt3r::data {
class CarDatabase;
struct Event;
} // namespace gt3r::data

namespace gt3r::race {

// The event's entry pool: the labels at +0x10 up to the first "-" (at most 32).
std::vector<std::string> event_pool(const data::CarDatabase& db, const data::Event& ev);

// Whether a pool entry can race in the database's region: the label resolved
// through the arcade-car (35) and car-variant (31) tables to its base car
// (30), whose region byte +0xF7 is tested. A label without a base car is
// unavailable.
bool pool_entry_available(const data::CarDatabase& db, const std::string& label);

// `draws` pool indices: each a random index (unit x pool size, truncated),
// moved forward (wrapping) to the first entry not yet taken; unavailable
// entries count as taken. Once as many entries were taken as the pool has,
// draws are plain random indices (repeats). `unavailable` is per pool entry.
std::vector<int> draw_pool(GameRandom& rng, const std::vector<bool>& unavailable, int draws);

// The paint of pool entry `k` (+0x110 + 4 k: a carcolor id, 0xFFFFFFFF = drawn at the race start). The car
// build gives it to the car drawn from that entry, so it follows the entry, not the grid slot.
u32 pool_color_id(const data::Event& ev, int k);

// The labels of `draws` opponents drawn from the event's pool (empty when
// the pool is empty); `color_ids` (optional) gets each one's pool_color_id.
std::vector<std::string> draw_opponents(const data::CarDatabase& db, const data::Event& ev, int draws,
                                        GameRandom& rng, std::vector<u32>* color_ids = nullptr);

} // namespace gt3r::race
