#pragma once

// "GTTW" wheel models: wheel/<id> (464 files: one per car plus aftermarket
// wheels such as wheel/enw001_s) and section 4 of every car file.
//
//   0x00 "GTTW"   0x0C u32 size   0x10 u32 flags (0 or 8)   0x1C u32 GTM1 offset (0x20)
//
// The embedded GTM1 holds only the rim face, modelled at unit scale (radius
// about 1.08, outer face towards -X, i.e. a right-hand wheel); the game scales it to the car's wheel size and the
// tyre is not part of it. See docs/formats/car_model.md.
//
// A mod wheel's rim (vfs/mod_wheels.h) is a binary glTF model in the same frame, as
// `gt3tool model <iso> wheel/<id> <dir> --gltf` exports a disc rim: unit scale, the outer face
// on x = 0 (glTF axes: facing +X, the car's left), the axle along X. load_wheel_model reads
// either kind.
//
// A mod wheel can reshape its rim (RimShape: [shape] in its file). Its rim then comes as a
// "GT3RIM1" wrapper: the shape and the rim it reshapes (a GTTW or glTF one), which
// load_wheel_model loads and reshapes.
//
//   0x00 "GT3RIM1\0"   0x08 f32 depth, dish, lip   0x14 the rim

#include "formats/model/gtm1.h"
#include "scene/model.h"

#include <string>

namespace gt3r {

struct WheelFile {
    u32 flags = 0;
    ByteSpan gtm1;

    static bool is_wheel(ByteSpan data);
    static WheelFile parse(ByteSpan data);
};

// How a mod wheel reshapes its rim, in the rim's unit frame (radius about 1, depth along +X in
// GT3's axes from the outer face at x = 0):
struct RimShape {
    float depth = 1; // the rim's depth times this (the dish and the barrel deeper or shallower)
    float dish = 0;  // the middle pushed in by this much (units of the rim's radius), easing to nothing at the lip
    float lip = 1;   // the lip (the outer 15 % of the radius) this many times as wide, the spokes' face shrunk inside it
    bool any() const { return depth != 1 || dish != 0 || lip != 1; }
    bool operator==(const RimShape&) const = default;
};
// The ranges a shape may take (a value outside one is refused, not clamped).
constexpr float kRimDepthMin = 0.25f, kRimDepthMax = 4, kRimDishMax = 0.5f, kRimLipMin = 0.25f, kRimLipMax = 4;
bool valid_rim_shape(const RimShape& s);

// A binary glTF rim (a mod wheel's).
bool is_glb_rim(ByteSpan data);
// A reshaped rim (the wrapper above).
bool is_shaped_rim(ByteSpan data);
// A rim of any kind: a GTTW wheel, a binary glTF one, or either reshaped.
bool is_rim(ByteSpan data);
// The wrapper of `rim` reshaped by `shape`; the rim inside a wrapper (and its shape into *shape),
// else `data` itself (no shape). Throws FormatError on a wrapper too short or with a shape out of range.
Bytes wrap_shaped_rim(ByteSpan rim, const RimShape& shape);
ByteSpan unwrap_rim(ByteSpan data, RimShape* shape = nullptr);
// Reshapes a rim model in place (normals follow the depth; the others move little).
void apply_rim_shape(scene::Model& rim, const RimShape& shape);

// A GTTW wheel's rim, or a glTF one's (drawn the same at every LOD; `opt.reflection` gives its
// reflection passes, formats/model/gltf.h), reshaped when wrapped.
scene::Model load_wheel_model(ByteSpan data, const gtm1::Options& opt = {}, const std::string& name = {});

} // namespace gt3r
