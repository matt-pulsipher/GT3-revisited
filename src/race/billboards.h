#pragma once

// Course billboard sprites (BLBd, course file slot 30; docs/formats/course.md): trees,
// spectators and the like, drawn with the scenery as quads that turn about the vertical
// axis to face the camera. Header: +0x10 u16 list count, +0x14 u16 type count, +0x18
// rel-ptr to the list pointers (one list per slot 1 node), +0x1C rel-ptr to an
// embedded Tex1, +0x24 rel-ptr to the sprite types {f32 half-width, f32 height, rel-ptr to
// 4 UVs, rel-ptr to 4 RGBA colours}. A list: u32 count, then 16-byte instances
// {u16 type, u16 texture + 1, f32 x, y, z} (bottom centre). The corners stand where their
// UVs put them in the picture (upright: smaller t on top), corner 0 on the left.

#include "base/bytes.h"
#include "race/clut_animations.h"
#include "render/gpu_model.h"
#include "render/view3d.h"
#include "scene/model.h"

#include <memory>
#include <vector>

namespace gt3r::race {

// Where a sprite's corners stand, from their UVs (s, t as the type lists them): cx in half-widths
// (-1 left, +1 right), cy in heights (0 foot, 1 top). The picture stands upright whichever order the
// type lists the corners in: the corners on corner 0's side of the UV rectangle are on the left (a
// type listing the right side first is mirrored), the smaller t on top.
void sprite_corners(const float (&s)[4], const float (&t)[4], float (&cx)[4], float (&cy)[4]);

class Billboards {
public:
    // False when the block is absent or empty.
    bool load(render::GpuContext& ctx, ByteSpan blbd, const std::string& name);
    size_t count() const { return sprites_.size(); }
    // CLUT animations of the billboards' texture set.
    ClutAnimations& clut_animations() { return cluts_; }
    // Turns every sprite to the camera (its right axis, flattened) and appends the item: the lists
    // of `nodes` in that order, as GT3 draws them after the view's scenery nodes
    // (CourseScene::visible_nodes; null = every list), with `fade` < 1 dithered (nodes fading in
    // or out, SceneryFade; DrawItem::fade).
    void add_items(std::vector<render::DrawItem>& items, const render::Vec3& camera_right,
                   const std::vector<u16>* nodes = nullptr, float fade = 1.0f);

private:
    struct Sprite {
        float x, y, z, w, h;
        // Each corner's place, from its UV (Billboards::load): x in half-widths (-1 left, +1
        // right), y in heights (0 foot, 1 top).
        float cx[4], cy[4];
    };
    std::vector<Sprite> sprites_; // in vertex order (4 vertices each)
    std::vector<scene::Vertex> verts_;
    std::unique_ptr<render::GpuModel> gpu_;
    ClutAnimations cluts_;
    render::Vec3 last_right_{0, 0, 0};
};

} // namespace gt3r::race
