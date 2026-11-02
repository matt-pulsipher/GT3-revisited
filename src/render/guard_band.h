#pragma once

// GT3's clipping at the guard band (presentation §M6), as its VU1 microcode does it: a triangle
// with a vertex beyond the planes |x| = 2 w, |y| = 2 w of clip space (twice the screen), nearer
// than the near plane or farther than the far plane is cut at them, and the piece is drawn as one
// strip. The colour the GS interpolates linearly in screen space then spans that piece, not the
// whole triangle: View3D draws such triangles from the pieces (DrawItem::guard_band).

#include "base/bytes.h"
#include "scene/model.h"

namespace gt3r::render {

// A vertex in clip space: x and y as the projection leaves them (x / w, y / w in -1..1 on the
// screen), w the depth from the eye.
struct ClipPos {
    float x = 0, y = 0, w = 0;
};

// The VU's clip flags (CLIP against |w|) of a vertex on the guard band's coordinates
// (x / 2, y / 2, w): 0x20 nearer than `near` (w < near, behind the eye too), 0x10 farther than
// `far` (w > far; 0 = no far plane), 0x08 / 0x04 beyond the bottom / top, 0x02 / 0x01 beyond the
// left / right.
u32 guard_flags(const ClipPos& p, float near, float far);
// The same against the screen itself (x, y, w): a triangle whose three vertices share one is
// culled.
u32 screen_flags(const ClipPos& p, float near, float far);

// A corner of a clipped triangle: the weights of the triangle's three vertices, the clip-space
// interpolation the VU makes of every attribute (position, texture coordinates, colour).
struct ClipCorner {
    float b[3] = {0, 0, 0};
};
constexpr int kMaxClipCorners = 9; // 3 + one per plane

// Clips the triangle v[0..2] (in GT3's strip order: the strip's vertices i, i + 1, i + 2) as the
// VU does: the planes in the order near, far, bottom, top, left, right, each cutting the polygon
// from its first vertex (kept vertices and the crossings of their edges in order). Writes the
// corners in the order GT3 sends them as a strip (p0, p(n-1), p1, p(n-2), ...) and returns their
// count; 0 when nothing is drawn (culled by screen_flags, or nothing left). `far` 0: no far plane.
int guard_clip(const ClipPos v[3], float near, float far, ClipCorner out[kMaxClipCorners]);

// A corner as the renderer draws it (PipelineKey::clipped, model.vert built with CLIPPED): the
// triangle's three vertices and the corner's weights of them.
struct ClipVertex {
    scene::Vertex v[3];
    float b[3] = {0, 0, 0};
};

} // namespace gt3r::render
