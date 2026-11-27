#pragma once

// PS2 memory-card save containers (for moving GT3 saves between GT3 Revisited,
// PCSX2 and a real PS2; docs/dev/GT_MODE.md "Save format"):
//
//   .psu   EMS / uLaunchELF single-save export: 512-byte entries in the card's
//          directory-entry layout (the save directory, ".", "..", then each file
//          followed by its data padded to 1 KB).
//   card   raw 8 MB card image (PCSX2 .ps2 / .mcd): 512-byte pages, with or without
//          the 16-byte spare area (ECC) per page; 2-page clusters, a two-level FAT
//          and 512-byte directory entries (superblock "Sony PS2 Memory Card Format").
//
// Directory entry (512 bytes): +0x00 u16 mode, +0x04 u32 length (files: bytes,
// directories: entries), +0x08 created and +0x18 modified time {u8 0, sec, min,
// hour, day, month, u16 year} (Japan time), +0x10 u32 first cluster, +0x14 u32
// entry index in the parent, +0x20 u32 attributes, +0x40 char[32] name.

#include "base/bytes.h"

#include <string>
#include <vector>

namespace gt3r::ps2 {

struct McFile {
    std::string name;
    Bytes data;
};
struct McDir {
    std::string name; // e.g. "BASCUS-97102GAMEDATA"
    std::vector<McFile> files;
    s64 time = 0;     // modification time (seconds since 1970, UTC); 0 = unknown
};

// Throws FormatError on malformed input.
McDir read_psu(ByteSpan psu);
Bytes write_psu(const McDir& dir);

constexpr size_t kCardSize = 0x800000;        // 8 MB of data pages
constexpr size_t kCardSizeEcc = 0x840000;     // with the 16-byte spare area per page
bool is_card_image(ByteSpan image);
// Every directory in the card's root with its files. Throws FormatError.
std::vector<McDir> read_card(ByteSpan image);
// A freshly formatted 8 MB card image with ECC (the PCSX2 .ps2 layout) holding `dirs`.
// `root_time` (seconds since 1970, UTC) is the root directory's time and that of every
// directory without one; 0 = the clock's now. Throws FormatError when they do not fit.
Bytes make_card(const std::vector<McDir>& dirs, s64 root_time = 0);

} // namespace gt3r::ps2
