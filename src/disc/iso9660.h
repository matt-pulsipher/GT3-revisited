#pragma once

#include "base/file.h"

#include <string>
#include <vector>

namespace gt3r {

constexpr u64 kSectorSize = 2048;

// Read-only ISO9660 reader for PS2 DVD images (2048-byte user-data sectors).
// PS2 discs are ISO9660 + UDF bridge; the ISO9660 view is sufficient.
class Iso9660;
// The image's file systems: the first, and on a dual-layer image (two file systems, one per layer) the second, whose
// primary volume descriptor sits right after the first's volume (or 16 sectors either side of its end).
std::vector<std::unique_ptr<Iso9660>> iso_layers(Reader& file);

class Iso9660 {
public:
    struct Entry {
        std::string name; // without ";1" version suffix
        u32 lba = 0;
        u64 size = 0;
        bool is_dir = false;
    };

    // The file system whose sector 0 is `base_sector` of the image (0; a dual-layer image's second layer: its start).
    explicit Iso9660(Reader& file, u64 base_sector = 0);

    const std::string& volume_id() const { return volume_id_; }
    // The volume's size as its primary volume descriptor gives it (volume space size x logical block size).
    u64 volume_bytes() const { return volume_bytes_; }
    // Lists a directory. `path` uses '/' separators; "" is the root.
    std::vector<Entry> list(const std::string& path) const;
    // Finds a file by path (case-insensitive, as PS2 paths are upper case).
    std::optional<Entry> find(const std::string& path) const;

    Bytes read(const Entry& e) const;
    // An entry's byte offset in the image's Reader.
    u64 offset(const Entry& e) const { return (base_ + e.lba) * u64(kSectorSize); }
    u64 base_sector() const { return base_; }

private:
    std::vector<Entry> read_dir(u32 lba, u32 size) const;
    Reader& file_;
    u64 base_ = 0;
    std::string volume_id_;
    u64 volume_bytes_ = 0;
    Entry root_;
};

} // namespace gt3r
