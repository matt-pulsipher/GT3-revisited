#pragma once

#include "disc/iso9660.h"
#include "disc/volume.h"

#include <memory>
#include <string>

namespace gt3r {

// Known retail discs. The engine accepts any image whose layout matches,
// but only listed ones have been verified; see docs/COMPATIBILITY.md.
struct DiscInfo {
    std::string serial;   // e.g. "PBPX-955.03" from SYSTEM.CNF BOOT2
    std::string version;  // SYSTEM.CNF VER
    std::string video;    // SYSTEM.CNF VMODE
    std::string volume_id;
    bool verified = false; // serial is in the tested list
    // The regional data set the program loads (database/paramdb<suffix>.db,
    // gameconf/product<suffix>.gcf): "_us" (NTSC-U), "_eu" (PAL), "" (NTSC-J).
    std::string db_suffix = "_us";
    std::string description;
};

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
    // The db_suffix of the disc opened last ("_us" before any).
    static const std::string& current_db_suffix();

private:
    Disc() = default;
    std::unique_ptr<File> file_;
    std::unique_ptr<Iso9660> iso_;
    std::unique_ptr<Volume> volume_;
    DiscInfo info_;
};

} // namespace gt3r
