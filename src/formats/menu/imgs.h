#pragma once

#include "formats/tex1.h"

#include <string>
#include <vector>

namespace gt3r::menu {

// "IMGS" named image set (menu/*.imgs, menu/<LANG>/*.imgs, race/<LANG>/*.imgs).
// A sorted name table followed by one single-texture Tex1 set per entry:
//
//   0x00 "IMGS"
//   0x04 u32 0
//   0x08 u32 entry count
//   0x0C u32 offset of the first Tex1 set (= 0x10 + count * entry size)
//   0x10 entries: char name[S - 4]; u32 index    (entry size S = 0x20, or 0x40
//        in arcade_menu.imgs which has long names; derived from the header)
//   ...  Tex1 sets back to back, each 16-byte aligned, in index order
//
// Entry names are sorted (the game binary-searches them); `index` is the
// ordinal of the entry's Tex1 set.
struct Imgs {
    struct Entry {
        std::string name;
        u32 index = 0;
        size_t offset = 0; // of the entry's Tex1 set in the file
        size_t size = 0;
    };
    std::vector<Entry> entries;

    static bool is_imgs(ByteSpan data);
    static Imgs parse(ByteSpan data);

    // Index into `entries` by name; -1 if missing.
    int find(std::string_view name) const;
    // Entry `i`'s Tex1 set in the file `data`.
    ByteSpan set(ByteSpan data, size_t i) const { return data.subspan(entries.at(i).offset, entries.at(i).size); }
    // Decodes entry `i` (texture 0 of its Tex1 set) to RGBA8.
    Tex1::Image decode(ByteSpan data, size_t i) const;
};

} // namespace gt3r::menu
