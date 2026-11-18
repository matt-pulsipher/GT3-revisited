#pragma once

#include "scene/model.h"

namespace gt3r::gtm1 {

// Marks the primitives of `mesh` that GT3 draws over an earlier primitive's surface from other
// vertices (scene::kCoplanarOverlay): a triangle that faces the same way as a triangle of an
// earlier depth-writing primitive, lies within a few millimetres of its plane and overlaps it
// (more than along an edge). GT3's Z buffer (PSMZ24) gives such faces the same depth and its
// GEQUAL test lets the later one win (Midfield's grandstand: the window frames over their glass,
// issue 349); the renderer gives the marked ones a small depth bias towards the eye instead.
// A primitive whose triangles repeat a marked one's vertices (a later pass over the same
// vertices, which GT3 draws at that depth) is marked too. Returns how many it marked.
int mark_coplanar_overlays(scene::Mesh& mesh);

} // namespace gt3r::gtm1
