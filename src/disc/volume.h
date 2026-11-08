#pragma once

#include "base/file.h"
#include "disc/region.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gt3r {

// GT3.VOL: Polyphony's packed filesystem holding nearly all game assets.
//
// Layout (all little endian), see docs/FORMATS.md:
//   0x00 u32 magic 0xACB990AD
//   0x04 u16, u16 (2, 2)
//   0x08 u32 toc_size           -- header + nodes + name pool
//   0x0C u32 name_pool_offset
//   0x10 u32 unknown
//   0x14 node tree, root directory first
//
// Each node starts with u32 `name_offset | flags << 24`. Names are stored
// bit-inverted, terminated by 0xFF (an inverted NUL).
//   flags & 1: directory  { u32 count; u32 parent; u32 children[count-1] }
//   flags & 2: gzip file  { u32 sector; u32 size; u32 packed_size }
//   otherwise: plain file { u32 sector; u32 size }
// Sectors are 2048 bytes, relative to the start of GT3.VOL.
class Volume {
public:
    static constexpr u32 kMagic = 0xACB990ADu;

    struct Entry {
        std::string path; // lower-case, '/' separated, as stored
        u32 sector = 0;
        u32 size = 0;        // logical (uncompressed) size
        u32 stored_size = 0; // bytes on disc
        bool compressed = false;
        u8 part = 0; // which of the set's volumes holds it (add_part)
    };

    // `base_offset` is the byte offset of GT3.VOL within the disc Reader `file`.
    Volume(Reader& file, u64 base_offset, u64 length);
    // Another volume of the set in the same disc Reader (whose files are spread over several *.VOL files): its entries
    // join this one's, a path already here staying this one's.
    void add_part(u64 base_offset, u64 length);

    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(const std::string& path) const;

    // Lists immediate children of a directory ("" = root). Directories are
    // returned with a trailing '/'.
    std::vector<std::string> list_dir(const std::string& dir) const;

    Bytes read(const Entry& e) const;      // decompressed contents
    Bytes read_raw(const Entry& e) const;  // as stored on disc
    // Reads part of an uncompressed entry (for streaming audio/video).
    void read_range(const Entry& e, u64 offset, void* out, size_t len) const;

    // The disc's packed game program (CORE.GT3, outside GT3.VOL), when the owner of the
    // volume provides it (Disc does). Read-only reference data for loaders that need a
    // table from the program; the engine never executes it.
    void set_program_reader(std::function<Bytes()> f) { program_reader_ = std::move(f); }
    std::optional<Bytes> read_program() const;
    // The release's data set (disc/region.h), set by the owner (Disc: from its program).
    void set_region(Region r) { region_ = r; }
    Region region() const { return region_; }
    // The disc's serial ("PBPX_955.03") and where it keeps its saves, replays and mod choices
    // (DiscInfo::key), set by the owner (Disc); without one, the region's key.
    void set_identity(std::string key, std::string serial) { disc_key_ = std::move(key), serial_ = std::move(serial); }
    std::string disc_key() const { return disc_key_.empty() ? region_key(region_) : disc_key_; }
    const std::string& serial() const { return serial_; }

private:
    void walk(ByteSpan toc, u32 node, const std::string& prefix, int depth, u8 part);
    void read_toc(u8 part);
    std::function<Bytes()> program_reader_;
    Region region_ = Region::America;
    std::string disc_key_, serial_;
    static std::string node_name(ByteSpan toc, u32 offset);

    Reader& file_;
    struct Part {
        u64 base, length;
    };
    std::vector<Part> parts_;
    std::vector<Entry> entries_;
    std::map<std::string, size_t> index_;
};

} // namespace gt3r
