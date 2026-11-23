#pragma once

// Binary glTF 2.0 (.glb) import for mod car bodies (docs/MODDING.md "Car
// models"). Triangle meshes with positions, normals (computed when missing),
// one UV set, vertex colours, node transforms, and materials with a base
// colour factor and an embedded PNG base colour texture.
//
// Axes: glTF's convention (+Y up, the model faces +Z, metres) is turned into
// GT3's (+Y up, nose towards -Z, +X to the car's right) by a half turn about
// Y. The origin should be the wheelbase midpoint on the ground, like GT3's
// own car models.
//
// Materials are recognised by name (case-insensitive substring):
//   paint, body        car paint: takes the chosen paint colour, reflective
//   glass, window      tinted glass (alpha blended when its alpha < 1), reflective
//   brake, tail        rear lamps: light up while braking
//   head, lamp, light  lamps: self-lit
//   anything else      lit with its base colour (trim, tyres, grille...)

#include "scene/model.h"

#include <array>
#include <optional>
#include <string>

namespace gt3r {

struct GlbOptions {
    std::optional<std::array<float, 3>> paint; // replaces the paint materials' colour (0..1)
    bool reflections = true;                   // environment-map passes on paint and glass
    u32 max_texture_dim = 8192;                // larger embedded images are rejected (downloads: 2048)
};

// Mesh "body" (all materials), plus "body_env" (reflection passes) when
// enabled. Throws FormatError on malformed files.
scene::Model load_glb(ByteSpan file, const GlbOptions& opt = {}, const std::string& name = {});

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
