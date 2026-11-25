#pragma once

// Livery templates (race/custom_paints.h, docs/MODDING.md "Liveries"): the pictures a livery is
// painted on, made from the player's own disc. A template never leaves the machine that made it:
// a livery holds only what its author painted over it (gtm1::overlay_livery leaves the template's
// own pixels out).
//
// For a car file (cars/day/<model>) in one of its paints, every texture its full-detail body draws (a mod car's
// glTF body: every textured or paint material with UVs, of each of its models):
//   canvas  the texture in that paint, enlarged `scale` times (nearest texel, so GT3's texels stay
//           readable), opaque. Named "<kind>-set<k>-<i>-<key>.png", <kind> "body" for the textures
//           the paints recolour (else "part": lamps, badges, glass, interior; a car of one paint:
//           all "body"), <key> the texture's key
//           in that paint, as a texture pack names it: the livery takes the canvas as it is. A glTF
//           body's are "<kind>-<texture name>-<key>.png", the key its embedded image's, at most
//           texture_pack::Pack::kMaxSide a side (enlarged only up to kGlbCanvas); a paint material
//           without a texture: a kGlbCanvas square in its paint.
//   guide   the same size, transparent: the body's triangles on that texture, filled by the side of
//           the car they face and outlined, since GT3's texture layouts are hard to read. Named
//           "guide-set<k>-<i>.png" (no key: a livery ignores it).

#include "base/bytes.h"

#include "scene/model.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gt3r {
class Vfs;
}
namespace gt3r::data {
class CarDatabase;
}

namespace gt3r::race {

struct LiveryTemplate {
    struct Picture {
        std::string file;   // file name in the livery folder
        u32 width = 0, height = 0;
        std::vector<u32> rgba;
        bool body = false;  // a canvas the paints recolour
    };
    std::vector<Picture> canvases, guides;
    std::string ini; // livery.ini to fill in
};

// The guide's fills by the side a triangle faces (RGBA8, R in the low byte): left, right, top,
// bottom, front, rear.
inline constexpr u32 kGuideSide[6] = {0x60E08030u, 0x603080E0u, 0x6040C040u, 0x60808080u, 0x604040E0u, 0x60C040C0u};

std::optional<LiveryTemplate> make_livery_template(ByteSpan car_file, const std::string& model, int paint, int scale,
                                                   std::string* error = nullptr);

// The largest side a glTF texture's canvas is enlarged to, and a textureless paint's.
inline constexpr u32 kGlbCanvas = 1024;

// The template of a glTF body: `files` are the mod car's models (the races', then the menus': a texture they share is
// painted once), `paint` the colour of its paint materials (unset: their own); `model` ("mod:<label>") and `paint_index`
// go into livery.ini.
std::optional<LiveryTemplate> make_glb_livery_template(const std::vector<Bytes>& files, const std::string& model,
                                                       const std::optional<std::array<float, 3>>& paint, int paint_index, int scale,
                                                       std::string* error = nullptr);

// Any car's, from the files of this disc and its mods: `model` as a livery names it (CustomPaint::model: a disc body, or
// "mod:<label>" of a mod car in `db`).
std::optional<LiveryTemplate> make_livery_template(const Vfs& vfs, const data::CarDatabase& db, const std::string& model, int paint,
                                                   int scale, std::string* error = nullptr);

// The textures a livery for `model` can cover, by key, as the car shows them in `paint` (a car file's: every Tex1 set's).
// Empty when the car is not here. `db`: needed for mod cars.
std::map<u64, scene::Texture> livery_textures(const Vfs& vfs, const data::CarDatabase* db, const std::string& model, int paint);

} // namespace gt3r::race
