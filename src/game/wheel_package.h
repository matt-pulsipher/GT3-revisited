#pragma once

// Mod wheel packages: what an online player sends when another player lacks the mod wheel on
// their car (docs/dev/ONLINE.md "Mod wheels"), as a mod car travels (game/mod_package.h).
//
// A package is the wheel's definition (wheels/<label>.ini, written afresh: its label, brand,
// models, tile, sale and maker's logo) and the files it references (its glTF rims, at most two,
// and its pictures, at most two), as one blob whose SHA-256 is its identity.
//
// Packages come from other players, so everything is checked before it is written or loaded
// (validate_wheel_package):
// - structure: magic, at most 5 files, 32 MB in all, per-type caps, plain relative paths
//   (safe_package_path); only wheels/<label>.ini (exactly one), .glb and .png;
// - the definition parses, names the advertised label, a label that is no wheel of this disc,
//   and the files it references are exactly the package's other files;
// - pictures: PNG, at most 2048 x 2048; rims: GLBs that load with textures of at most
//   2048 x 2048 and at most 500,000 vertices.
// It installs as a mod car's package does (install_mod_package: mods/dl-<hash>/, priority -100,
// every picture and rim our own encoding).

#include "base/bytes.h"
#include "game/mod_package.h"

#include <optional>
#include <string>

namespace gt3r {
class Vfs;
}

namespace gt3r::game {

constexpr size_t kMaxWheelPackageFiles = 5;

// The package of the enabled mod wheel `label` (nullopt with *error when there is none, or a file is missing).
std::optional<Bytes> build_wheel_package(const Vfs& vfs, const std::string& label, std::string* error = nullptr);
Bytes serialize_wheel_package(const ModPackage& p);
// Structure only (magic, counts, sizes, paths, file types). ModPackage::label is the wheel's.
std::optional<ModPackage> parse_wheel_package(ByteSpan blob, std::string* error = nullptr);
// Everything (see above), against this machine's disc (`vfs`).
bool validate_wheel_package(const ModPackage& p, const Vfs& vfs, const std::string& expected_label,
                            std::string* error = nullptr);

} // namespace gt3r::game
