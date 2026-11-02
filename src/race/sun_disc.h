#pragma once

// The sun disc (presentation §I6): with the sun on (Env0 flag 0x400) and a disc size d
// (Env0 +0x9D4) above 0, the race view draws the flare system's 14-segment disc mesh
// (race::Flare::disc_mesh) right after the sky dome and before the backdrop, so the backdrop
// and the scenery cover it. It sits 10 m out along the sun direction from the camera, parallel
// to the screen, radius 10 d (angular radius atan d), coloured by the flare vertex program:
// vertex RGB x sun RGB (+0x9C0) x disc gain (+0x9D8) x 0.5 (clamped to 255), vertex alpha at
// full visibility, added as Cs As / 128 + Cd; no depth test or write, no fog, no alpha test. Only
// the main view and the 2P halves draw it: not the rear-view mirror or the car reflection map.

#include "render/gpu_model.h"
#include "render/math3d.h"
#include "render/view3d.h"
#include "scene/model.h"

#include <memory>
#include <vector>

namespace gt3r::race {

struct SunParams;

class SunDisc {
public:
    // The disc's model for a course's sun (no meshes when it draws nothing): the disc mesh at
    // radius 1 with its vertex colours, one additive, unlit, double-sided primitive without
    // depth writes. The GS's As / 128 weight (up to 255 / 128) is two of the renderer's additive
    // passes (Cs As + Cd, As up to 1): SunDisc::add_items adds the item twice.
    static scene::Model build(const SunParams& p);
    void load(render::GpuContext& ctx, const SunParams& p);
    bool active() const { return gpu_ != nullptr; }
    // Background items for a camera at `eye` with these right / up axes (world): the disc at
    // eye + 10 x the sun direction, in the camera's screen plane.
    void add_items(std::vector<render::DrawItem>& items, const render::Vec3& eye, const render::Vec3& right,
                   const render::Vec3& up) const;
    // The disc's world transform for that camera (radius 1 mesh -> world).
    static render::Mat4 transform(const render::Vec3& eye, const render::Vec3& sun_dir, float disc_size,
                                  const render::Vec3& right, const render::Vec3& up);

private:
    std::unique_ptr<render::GpuModel> gpu_;
    render::Vec3 dir_{0, 1, 0};
    float size_ = 0;
};

} // namespace gt3r::race
