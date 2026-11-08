#pragma once

#include "disc/iso9660.h"
#include "disc/region.h"
#include "disc/volume.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gt3r {

// Known retail discs. The engine accepts any image whose layout matches,
// but only listed ones have been verified; see docs/COMPATIBILITY.md.
struct DiscInfo {
    std::string serial;   // e.g. "PBPX-955.03" from SYSTEM.CNF BOOT2
    std::string version;  // SYSTEM.CNF VER
    std::string video;    // SYSTEM.CNF VMODE
    std::string volume_id;
    bool verified = false; // serial is in the tested list
    bool reported = false; // in the list as reported working by players
    std::string description;
    Region region = Region::America; // the release's data set (disc/region.h)
    std::string problem; // a problem with the image for the player (incomplete_image), empty when none
    // The files' fingerprint: 16 hex digits of SHA-256 over CORE.GT3 and the game volumes' tables of contents.
    std::string digest;
    bool modified = false; // a release in the known list whose files are not that release's (a patched copy)
    std::string key;       // disc_key: where the disc keeps its saves, replays and mod choices
};

// The name a disc keeps its saves, replays and mod choices under: the region's ("us", "eu", "jp";
// region_key) for a release in the known list, patched copies included, so those stay where they
// always were; else the serial the way PCSX2 writes it ("SCEF-50294"); else "disc-<digest>".
std::string disc_key(const DiscInfo& info);
// A serial the way PCSX2 names its folders: "PBPX_955.03" -> "PBPX-95503".
std::string short_serial(std::string_view serial);

// The player's message for an image shorter than its volume says (a dump that stopped early), none when it is whole:
// "This disc image is incomplete (2.47 of 3.67 GB). Make the image again from your disc."
std::string incomplete_image(u64 file_bytes, u64 volume_bytes);

// A user-supplied Gran Turismo 3 disc image. Owns the open file and the
// parsed GT3.VOL archive.
class Disc {
public:
    // Throws FormatError with a user-presentable message on failure.
    static std::unique_ptr<Disc> open(const std::string& path);

    const DiscInfo& info() const { return info_; }
    Iso9660& iso() { return *iso_; }
    Volume& volume() { return *volume_; }
    Bytes read_core_gt3();
    // The image's file systems (a dual-layer image: one per layer), and a file in the first that has it.
    std::vector<Iso9660*> file_systems() const;
    std::optional<std::pair<Iso9660*, Iso9660::Entry>> find_file(const std::string& path) const;

private:
    Disc() = default;
    std::unique_ptr<Reader> file_;
    std::unique_ptr<Iso9660> iso_;                   // the first layer's file system
    std::vector<std::unique_ptr<Iso9660>> layers_;  // every layer's but the first
    std::unique_ptr<Volume> volume_;
    DiscInfo info_;
};

} // namespace gt3r
