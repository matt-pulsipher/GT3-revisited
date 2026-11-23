#pragma once

// Livery packages: what an online player sends when another player lacks the livery their car wears
// (race/custom_paints.h; docs/dev/ONLINE.md "Liveries"). Built from the installed livery, it holds
// only what its author painted: each picture with its coverage as alpha (the parts that still show
// the template, the disc's own pixels, left transparent; gtm1::overlay_livery), pictures that cover
// nothing left out, and a livery.ini written afresh (name, author, model, paint, colour). The
// SHA-256 of the blob is the livery's identity: the lobby advertises it, the download is checked
// against it, and the install folder is named after it.
//
// Packages come from other players, so everything is checked before anything is written
// (validate_livery_package):
// - structure: magic "GT3RLIV1", at most kMaxLiveryFiles files, kMaxLiveryBytes in all; plain file
//   names (letters, digits, '.', '_', '-'; no folders);
// - exactly one livery.ini, which parses (race::parse_custom_paint), names a model of this disc
//   with that paint, and nothing else but PNGs named by a key ("<name>-<16 hex digits>.png");
// - every key is one of that car's textures in that paint on this disc;
// - pictures: PNG, at most texture_pack::Pack::kMaxSide per side (read from the header before
//   decoding), and decode cleanly.
// The pictures are written as our own encoding of what they decode to, never the sender's bytes, so
// whatever else opens them (a file manager's thumbnails, the system's media scan) reads only what our
// PNG encoder writes. The decode itself is our decoder on another player's data: fuzzed (`png`,
// `livery_package`), and the reason other players' liveries are asked about first by default.

#include "base/bytes.h"

#include <optional>
#include <string>
#include <vector>

namespace gt3r {
class Vfs;
}
namespace gt3r::race {
struct CustomPaint;
}

namespace gt3r::game {

struct LiveryPackage {
    struct File {
        std::string name; // "livery.ini" or "<name>-<key>.png"
        Bytes data;
    };
    std::vector<File> files;
};

constexpr size_t kMaxLiveryBytes = 16u << 20;
constexpr size_t kMaxLiveryFiles = 64;
// The mod downloaded liveries install into, as liveries/dl-<first 12 hash digits>/.
inline constexpr const char* kDownloadedLiveriesMod = "downloaded_liveries";

// The package of an installed livery (`p` with pictures), from this disc's car; nullopt with *error when the car or a
// picture cannot be read, or nothing is painted.
std::optional<Bytes> build_livery_package(const race::CustomPaint& p, const Vfs& vfs, std::string* error = nullptr);

Bytes serialize_livery_package(const LiveryPackage& p);
// Structure only (magic, counts, sizes, names).
std::optional<LiveryPackage> parse_livery_package(ByteSpan blob, std::string* error = nullptr);
// Everything (see above), against this machine's disc. `model`: the one the lobby advertised (empty: any).
bool validate_livery_package(const LiveryPackage& p, const Vfs& vfs, const std::string& model, std::string* error = nullptr);

// Writes a validated package as <mods_dir>/downloaded_liveries/liveries/dl-<first 12 hash digits>/ (the mod made when
// missing), its livery.ini noting who it came from. Returns the folder.
std::optional<std::string> install_livery_package(const LiveryPackage& p, const std::string& hash, const std::string& mods_dir,
                                                  const std::string& from, std::string* error = nullptr);
// The livery id (race::CustomPaint::id) a package with this hash installs as.
std::string downloaded_livery_id(const std::string& hash);

} // namespace gt3r::game
