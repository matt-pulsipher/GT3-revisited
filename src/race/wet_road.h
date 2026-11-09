#pragma once

// GT3's wet-road reflection pass (docs/dev/RACE.md "Wet-road reflection"):
// courses with a course-file slot 9 model (route5 / route11 families, the wet circle tests).
//   1. The frame alpha is cleared to 0, then slot 13 (alpha-only packets, depth-tested against
//      the scenery) writes the reflectivity mask: texel x vertex alpha.
//   2. A small offscreen buffer (Env0 +0xC24 x +0xC28, 64 x 128), colour and depth cleared to 0,
//      gets slot 9 (the scenery stored pre-mirrored below the road) with the main view and
//      projection and its own fog (Env0 +0xC40 block, the +0x60 layout); with Env0 flag 0x10000
//      the cars mirrored about the road plane under each (body and wheels); with 0x20000 the
//      car lamps' streaks (below).
//   3. Env0 +0xC2C blur passes: the buffer copied onto itself, bilinear, offset -0.5 / +0.5 texel
//      alternately.
//   4. The buffer stretched over the frame (bilinear, net -0.5 texel shift) with Env0 +0xC20:
//      0x54 (Cs - Cd) Ad + Cd (wet route5) or 0x58 Cs Ad + Cd (route5_dry, route11).
// It runs after the scenery and before the cars (which therefore cover it).
// Streaks (Env0 +0xE60..+0xE7C): per car the headlamps (I = 1), brake lamps (while braking: k
// for flag-bit-0 records, else 1 while the pedal is down) and tail lamps (0.3), at the lamp's
// mirror image: c = cos(lamp axis, eye -> lamp) (skipped when < 0 or the lamp is behind the
// eye), a camera-facing quad pulled 0.5 m towards the eye, half width (E74 + (E70 - E74) c) x
// lamp size (+0x40), from min(E78, lamp height) above the image (the road surface) to E7C below
// it, race/reflection.img (bright and wide at the top, tapering), colour lamp RGB x I x c x
// E60..E68 x E6C, added (Cs + Cd), depth-tested against the buffer, no depth write.

#include "base/bytes.h"
#include "formats/course/visibility.h"
#include "race/clut_animations.h"
#include "race/lamp_glows.h"
#include "render/view3d.h"
#include "scene/model.h"

#include <array>
#include <memory>
#include <vector>

namespace gt3r {
class Vfs;
}

namespace gt3r::render {
class GpuContext;
class GpuModel;
} // namespace gt3r::render

namespace gt3r::race {

struct WetRoadParams {
    int mode = 0x54;              // +0xC20 composite ALPHA mode
    int width = 64, height = 128; // +0xC24 / +0xC28
    int blur = 1;                 // +0xC2C
    std::array<float, 3> fog_rgb{}; // +0xC40 block: RGB, +0xC50 distance A, +0xC54 minimum visibility b
    float fog_a = 0, fog_b = 1;
    bool cars = false, streaks = false; // Env0 +0x10 bits 0x10000 / 0x20000
    std::array<float, 3> streak_rgb{1, 1, 1}; // +0xE60..+0xE68
    float streak_gain = 0, width_head_on = 0, width_side = 0, cap = 0, length = 0; // E6C, E70, E74, E78, E7C
};

class WetRoad {
public:
    WetRoad();
    ~WetRoad();
    // `course` = the course file, `env0` = its slot 32. Inactive without a slot 9 model.
    bool load(render::GpuContext& ctx, const Vfs& vfs, ByteSpan course, ByteSpan env0);
    bool active() const { return reflection_ != nullptr; }
    const WetRoadParams& params() const { return p_; }
    // CLUT animations of the slot 9 texture sets.
    ClutAnimations& clut_animations() { return cluts_; }

    // A car for the streaks: its lamps, model frame -> world (the body as drawn), the road
    // mirror under it (CarVisual::road_mirror), the lamp factor k/4 and the brake pedal now.
    struct Car {
        const CarLampSet* lamps = nullptr;
        render::Mat4 model, mirror;
        float k = 0;
        bool brake_now = false;
    };
    // The buffer's scene (the caller adds the mirrored cars): slot 9 (the nodes of slot 10's list
    // for `eye`) and the streaks for `view`.
    void add_buffer_items(std::vector<render::DrawItem>& items, const render::Mat4& view, const render::Vec3& eye,
                          const std::vector<Car>& cars);
    // The buffer's draw options from the main view's.
    render::DrawOptions buffer_options(const render::DrawOptions& main) const;
    // Renders the buffer and its blur passes; returns the texture to composite (null on failure).
    SDL_GPUTexture* render(render::View3D& view3d, const render::Mat4& view, const render::Mat4& proj,
                           const render::Vec3& eye, const std::vector<render::DrawItem>& items,
                           const render::DrawOptions& main);
    // The frame's items, after the scenery: the alpha clear, the slot 13 mask (the nodes of slot
    // 14's list for the camera) and the composite of `buffer` (a camera-space quad just past the
    // near plane; `fovy` vertical, `aspect` w / h).
    void add_frame_items(std::vector<render::DrawItem>& items, const render::Mat4& view, float fovy, float aspect,
                         SDL_GPUTexture* buffer) {
        add_frame_items(items, view, render::Frustum::symmetric(fovy, aspect), buffer);
    }
    // Any frustum (VR eyes: one quad pair per eye is kept).
    void add_frame_items(std::vector<render::DrawItem>& items, const render::Mat4& view, const render::Frustum& fr,
                         SDL_GPUTexture* buffer);

private:
    struct FrameQuads {
        render::Frustum fr;
        std::unique_ptr<render::GpuModel> clear, frame;
        u64 used = 0;
    };
    FrameQuads& frame_quads(const render::Frustum& fr);
    render::GpuContext* ctx_ = nullptr;
    WetRoadParams p_;
    ClutAnimations cluts_;
    std::unique_ptr<render::GpuModel> reflection_, mask_;
    course::Visibility reflection_vis_, mask_vis_; // slots 10 and 14
    FrameQuads quads_[2];                       // the frame's alpha clear and composite, per view frustum
    u64 quad_uses_ = 0;
    std::unique_ptr<render::GpuModel> blur_quad_[2]; // blur: -0.5 / +0.5 texel
    std::unique_ptr<render::GpuModel> streaks_;
    std::vector<scene::Vertex> streak_verts_;
    static constexpr int kMaxStreaks = 128;
};

} // namespace gt3r::race
