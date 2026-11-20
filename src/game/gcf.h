#pragma once

// gameconf/*.gcf: the game's configuration tree.
//
//   +0x00 u32 section count, +0x04 u32 offset of the section table (8)
//   section table: count x {u32 tag, u32 absolute offset}
//   section: u32 entry count, u32 offset of the entry table (8, relative
//            to the section), u32 entry offsets[count] (relative to the section)
//   entry:   u32 offset of the entry's name (relative to the entry), then
//            tag-specific u32 fields, the NUL-terminated name, then
//            tag-specific data. Names within a section are sorted.
//
// Tags used by GT mode (product.gcf / product_eu.gcf): 10 the game's sums by name
// (`initial` = a new game's credits, `car_wash`, `oil_change`, `wheel` = the shop prices;
// value = u32 at entry +4, in the disc's money units: the Japanese game shows them x100),
// 4 GT-mode race codes, 6 courses, 13 license / challenge tests, 15 prize keys (prize cars),
// 14 arcade classes, 8 arcade modes, 12 demo races.
//
// Gran Turismo Concept's gameconf/concept*.gcf has the same sections and entry layouts (only
// counts and contents differ, docs/formats/database.md "Gran Turismo Concept"): its prize keys
// name a car by arcade class and place ("CLASS_S#008"), its arcade class cars name their race.

#include "base/bytes.h"

#include <optional>
#include <string>
#include <vector>

namespace gt3r::game {

class Gcf {
public:
    struct Entry {
        std::string name;
        ByteSpan bytes; // the whole entry (up to the next entry or section end)
        u32 u32_at(size_t off) const { return off + 4 <= bytes.size() ? rd32(bytes, off) : 0; }
        // NUL-terminated string at `off` inside the entry.
        std::string str_at(size_t off) const;
    };
    struct Section {
        u32 tag = 0;
        std::vector<Entry> entries;
        const Entry* find(const std::string& name) const;
    };

    Gcf() = default;
    Gcf(const Gcf&) = delete; // entries point into data_ (moving keeps the buffer)
    Gcf& operator=(const Gcf&) = delete;
    Gcf(Gcf&&) = default;
    Gcf& operator=(Gcf&&) = default;

    // Throws FormatError on malformed data.
    static Gcf parse(Bytes data);

    const Section* section(u32 tag) const;
    // A u32 at `off` inside a section (sections that are not entry lists, e.g.
    // tag 0 whose first word is the title idle time in seconds).
    std::optional<u32> section_u32(u32 tag, size_t off) const;
    // Tag-10 value ("initial", "car_wash", ...), as stored (no region scaling).
    std::optional<u32> value(const std::string& name) const;

private:
    Bytes data_;
    std::vector<Section> sections_;
    std::vector<std::pair<u32, u32>> offsets_; // tag -> absolute section offset
};

} // namespace gt3r::game
