#pragma once

// A livery on the lower-detail models (docs/dev/CUSTOM_PAINTS.md "Liveries"): a car's race file
// draws its body at three levels of detail, and the lower two have texture layouts of their own,
// so a livery painted on the full-detail textures does not reach them by key. Each of their
// texels the livery does not cover is carried from the full-detail body instead: the texel's
// point on its own triangle, the nearest point on the full-detail body that faces the same way
// (within kReach), and the livery's colour and coverage there. Painted once, it shows at every
// distance; a texel whose nearest full-detail surface the livery does not cover stays as it is.

#include "base/bytes.h"
#include "scene/model.h"

namespace gt3r {

struct CarModelOptions;

namespace livery_transfer {

inline constexpr float kReach = 0.06f; // metres: further from the full-detail body, a texel keeps its own colour
inline constexpr u32 kScale = 2;       // the lower-detail textures a livery reaches are drawn at twice their size

// `model`: a car's model (load_car_model) whose meshes of LOD 1 and lower take the livery from
// `reference`, the same car's LOD 0 with the livery (textures with Texture::livery_cover).
// Returns how many textures it changed.
int apply(scene::Model& model, const scene::Model& reference);

} // namespace livery_transfer
} // namespace gt3r
