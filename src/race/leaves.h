#pragma once

// The later builds' leaf draw (Gran Turismo Concept's leaf particle: 2002 Tokyo-Geneva Europe, with the sparks in
// the one-player view). The leaves themselves are the race's (sim/exact/leaves.h, in the sparks' pool). Each leaf is its
// shape's quad (w x h about its center, in its plane) turned by its three angles and placed at its position,
// textured with race/particle.img's picture of its shape (shape k: picture k + 1; the sparks' glow is picture 0), UVs (0, 0)
// .. (1, 1) over the corners, the picture's alpha cutting the leaf out, both faces. Its color is lit by the
// course's Env0 car light 0 as the file has it (direction +0xD0, ambient +0xE0, diffuse +0xF0; the manager's light
// pointer is the course's Env0 block, set by the race's start-up from the course's slot 32; no flag bit gates it,
// no per-car shade scales it): with the leaf's normal n (its matrix's second row), the ground's shade a under the wheel
// that threw it and L the light's direction, c = -(L.n) / |L| x (0.3 + 0.7 a) (LeafRules::light_base / light_gain),
// negated when the leaf's position less the point P is on n's side ((pos - P).n >= 0), at least 0; then each channel
// trunc((diffuse c + ambient a) x 128), at most 0x80, alpha 0x80. P is the negated translation of the view matrix the
// draw starts with (the camera's position in its own eye axes, not rotated back to the world: the program's own side
// test).
//
// Ours draws between the race's frames (each leaf's turn and position blended from the last update's) with the sparks'
// view (one-player view; the VR view's from the head).

#include "render/view3d.h"
#include "sim/exact/sparks.h"

#include <array>
#include <memory>
#include <vector>

namespace gt3r {
class Vfs;
}

namespace gt3r::race {

// The light a leaf is drawn with: the course's Env0 car light 0 as the file has it (direction +0xD0, ambient +0xE0,
// diffuse +0xF0; race::SunParams::light_dir / light_ambient / light_diffuse).
struct LeafLight {
    std::array<float, 3> dir{0.3f, 0.85f, 0.35f};
    std::array<float, 3> ambient{0.3f, 0.3f, 0.3f}, diffuse{1, 1, 1};
};

// A leaf's color in the GS's units (RGBA, 0x80 = 1) from its normal `n`, `near_side` (its position less the
// view's point is on n's other side), the ground's shade `a`, the light and the build's weights `base` / `gain`.
u32 leaf_color(const render::Vec3& n, bool near_side, float a, const LeafLight& l, float base, float gain);

class LeafDraw {
public:
    LeafDraw();
    ~LeafDraw();
    // race/particle.img's leaf pictures (1 .. 8); false when the file is missing or has fewer pictures (no leaves drawn).
    bool load(render::GpuContext& ctx, const Vfs& vfs);
    bool loaded() const { return gpu_ != nullptr; }
    // The leaves of `s` (none: nothing appended), blended `alpha` of the way from their last update, drawn with `view`
    // (world to eye).
    void add_items(std::vector<render::DrawItem>& items, const sim::exact::Sparks& s, float alpha, const render::Mat4& view,
                   const LeafLight& light);

private:
    static constexpr size_t kShapes = 8;
    std::vector<scene::Vertex> verts_; // kShapes x kPool quads: shape k's from k x kPool
    std::unique_ptr<render::GpuModel> gpu_;
};

} // namespace gt3r::race
