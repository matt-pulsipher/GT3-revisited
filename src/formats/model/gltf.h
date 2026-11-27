#pragma once

// Binary glTF 2.0 (.glb) import for mod car bodies and rims (docs/MODDING.md "Car
// models", "Adding wheels"). Triangle meshes with positions, normals (computed when missing),
// one UV set, vertex colours, node transforms, and materials with a base
// colour factor, an embedded PNG base colour texture and an alpha mode (BLEND
// blended, MASK cut out at alphaCutoff).
//
// Axes: glTF's convention (+Y up, the model faces +Z, metres) is turned into
// GT3's (+Y up, nose towards -Z, +X to the car's right) by a half turn about
// Y. The origin should be the wheelbase midpoint on the ground, like GT3's
// own car models.
//
// Materials are recognised by name (case-insensitive substring):
//   paint, body        car paint: takes the chosen paint colour, reflective
//   glass, window      tinted glass (alpha blended when its alpha < 1), reflective
//   chrome, metal      its base colour, shiny and reflective (not where cut out)
//   brake, tail        rear lamps: light up while braking
//   head, lamp, light  lamps: self-lit
//   anything else      lit with its base colour (trim, tyres, grille...)

#include "scene/model.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace gt3r {

namespace texture_pack {
class Pack;
}

struct GlbOptions {
    std::optional<std::array<float, 3>> paint; // replaces the paint materials' colour (0..1)
    bool reflections = true;                   // environment-map passes on paint and glass
    u32 max_texture_dim = 8192;                // larger embedded images are rejected (downloads: 2048)
    // A livery (race/custom_paints.h): its pictures over the textures, keyed by the embedded images as they
    // decode (glb_livery_textures). A texture one covers shows the picture as painted: the base colour (the
    // paint) of its material goes into the texture under the picture instead of the vertex colours.
    const texture_pack::Pack* livery = nullptr;
    // The paint colour the pictures were painted over (the livery's paint; unset: the materials' own).
    std::optional<std::array<float, 3>> livery_paint;
};

// Mesh "body" (all materials), plus "body_env" (reflection passes) when
// enabled. Throws FormatError on malformed files.
scene::Model load_glb(ByteSpan file, const GlbOptions& opt = {}, const std::string& name = {});

// A texture a livery can cover, as its template shows it: the picture a primitive's material draws in `paint`
// (unset: the materials' own base colours), and where the body uses it.
struct GlbLiveryTexture {
    u64 key = 0;        // of the embedded image as it decodes (texture_pack::key_of), whatever the paint
    std::string name;   // the image's name, or its material's
    u32 width = 0, height = 0;
    std::vector<u32> rgba; // times the base colour: the first paint material's drawing it, else the first material's
    bool paint = false;    // a paint material draws it
    bool blank = false;    // no image: paint materials without a texture (1x1; their UVs still place a picture)
    struct Triangle {
        float uv[3][2];
        float normal[3]; // GT3 axes, the three corners' sum
    };
    std::vector<Triangle> triangles;
};
// The textures of a .glb's triangles with UVs: every embedded PNG drawn, one per key, and the paint materials
// that have none. Throws FormatError on malformed files.
std::vector<GlbLiveryTexture> glb_livery_textures(ByteSpan file, const std::optional<std::array<float, 3>>& paint,
                                                  u32 max_texture_dim = 8192);

// The same .glb with every image our own: each embedded PNG decoded and encoded again by our
// PNG encoder, every other image (JPEG, or a file beside it) a 1x1 white PNG (what load_glb
// draws for them anyway), the sender's image bytes zeroed in the binary buffer where no
// accessor reads them. For a downloaded car (game/mod_package.h), so whatever else opens the
// file reads only what our encoder wrote. Throws FormatError on malformed files.
Bytes reencode_glb_images(ByteSpan file, u32 max_texture_dim);

// The images of a .glb that load_glb does not read and draws white: a JPEG (or any type but PNG)
// and an image in a file beside it, one line each for the modder ("texture \"door\": JPEG,
// shows white (use PNG)"). Reads only the JSON chunk; empty when the file is
// no .glb.
std::vector<std::string> glb_image_warnings(ByteSpan file);

} // namespace gt3r
