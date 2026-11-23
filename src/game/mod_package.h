#pragma once

// Mod car packages: what an online player sends when another player lacks their mod car
// (docs/dev/ONLINE.md "Mod cars").
//
// A car exported from a disc (data::ModCar::exported) has no package: its files are that disc's.
//
// A package is the car's definition file (cars/<label>.ini) and the files it references
// (the glTF bodies its menus and races draw, and its pictures), with paths relative to the mod folder, as one blob. The
// SHA-256 of the blob is the package's identity: the lobby advertises it, the download is
// checked against it, and the install folder is named after it.
//
// Packages come from other players, so everything is checked before it is written or
// loaded (validate_mod_package):
// - structure: magic, at most 8 files, 32 MB total, per-type size caps; plain relative
//   paths (letters, digits, '.', '_', '-', at most 3 levels, no "..", no absolute or
//   hidden names);
// - only cars/<label>.ini (exactly one), .glb and .png files: a package can only add a
//   car, never replace a game file (ordinary mods can, a download cannot);
// - the definition parses, names the advertised label, its donor car is on this disc, its
//   values are in plausible ranges, and the files it draws (its bodies by scene,
//   data::resolve_model_files, and pictures) are exactly the package's other files;
// - pictures: PNG, at most 2048 x 2048 (checked before decoding), decode cleanly;
// - the body: a GLB that loads with embedded textures of at most 2048 x 2048 and at most
//   500,000 vertices.
// Installed pictures and bodies are our own encoding, never the sender's bytes: each PNG decoded and
// encoded again, each body's images likewise (reencode_glb_images: other images a 1x1 white PNG, the
// sender's image bytes zeroed), so whatever else opens the files (a file manager's thumbnails, the
// system's media scan) reads only what our encoders wrote.

#include "base/bytes.h"

#include <optional>
#include <string>
#include <vector>

namespace gt3r {
class Vfs;
}
namespace gt3r::data {
class CarDatabase;
}

namespace gt3r::game {

struct ModPackage {
    struct File {
        std::string path; // relative to the mod folder, '/' separated
        Bytes data;
    };
    std::string label; // the car
    std::vector<File> files;
};

constexpr size_t kMaxPackageBytes = 32u << 20;
constexpr size_t kMaxPackageFiles = 8;

// The package of an installed mod car (nullopt with *error when the car is no mod car or a
// file is missing).
std::optional<Bytes> build_mod_package(const data::CarDatabase& db, const Vfs& vfs, const std::string& label,
                                       std::string* error = nullptr);

Bytes serialize_mod_package(const ModPackage& p);
// Structure only (magic, counts, sizes, paths, file types).
std::optional<ModPackage> parse_mod_package(ByteSpan blob, std::string* error = nullptr);
// Everything (see above), against this machine's database.
bool validate_mod_package(const ModPackage& p, const data::CarDatabase& db, const std::string& expected_label,
                          std::string* error = nullptr);

// A path a package may contain (exposed for tests).
bool safe_package_path(const std::string& path);

// Writes a validated package as a mod: <mods_dir>/dl-<first 12 hash digits>/ with a mod.ini
// naming the car and who it came from. Returns the mod folder.
std::optional<std::string> install_mod_package(const ModPackage& p, const std::string& hash, const std::string& mods_dir,
                                               const std::string& car_name, const std::string& from,
                                               std::string* error = nullptr);

} // namespace gt3r::game
