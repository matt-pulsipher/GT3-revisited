#pragma once

// Car paint colours for a race, as GT3 assigns them (research summary in
// docs/dev/RACE.md "Paint colours").
//
// database/carcolor.db ("GT2K"): +0x08 u32 model count, +0x0C offset of the
// id lists, +0x10 offset of the colour definitions; model records at 0x18,
// 16 bytes {u64 id_hash(model), u32 count, u32 offset (& ~3, from +0x0C)}
// -> count u32 colour ids; definitions: u32 n, then n x {u32 id, u32, u32,
// u32 rgb (bytes R, G, B)}. A car's paint index (the Tex1 paint variant) is
// the position of its colour id in its model's list.
//
// Opponent colours come from the event record (+0x110: u32 colour id per
// pool entry, 0xFFFFFFFF = choose at race start; race/pool_draw.h). Unset ones
// are drawn in entry order with a weighted random pick that avoids colours
// close to those already taken, using the game's RNG (base/game_random.h).
// The cars keep them for the whole race (its practice, retries, replays), and
// a championship's field keeps them for every round.

#include "base/bytes.h"
#include "base/game_random.h"

#include <map>
#include <string>
#include <vector>

namespace gt3r {
class Vfs;
}

namespace gt3r::race {

class CarColors {
public:
    bool load(const Vfs& vfs);
    bool parse(ByteSpan carcolor_db);
    const std::vector<u32>* ids(const std::string& model) const;
    u32 rgb(u32 id) const; // bytes R, G, B (0 when unknown)
    int index_of(const std::string& model, u32 id) const; // -1 if absent
    u32 id_at(const std::string& model, int index) const;  // 0xFFFFFFFF if out of range

private:
    std::map<u64, std::vector<u32>> models_;
    std::map<u32, u32> rgb_;
};

// The race module's generator state (base/game_random.h); a race takes
// race_info_random(), which the opponent pool draw has used before it.
using PaintRng = GameRandom;

struct PaintEntry {
    std::string model;
    u32 id = 0xFFFFFFFFu; // fixed colour id, or unset
    bool operator==(const PaintEntry&) const = default;
};

// Assigns every unset entry a colour (in list order) and returns the paint
// index per entry (-1 when the model has no colour list or the id is not in it).
std::vector<int> assign_paints(const CarColors& colors, std::vector<PaintEntry> entries, PaintRng& rng);

} // namespace gt3r::race
