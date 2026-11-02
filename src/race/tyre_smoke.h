#pragma once

// Tyre smoke and dirt dust (GT3's race smoke system; facts in docs/dev/RACE.md "Tyre smoke").
// A pool of 256 puffs shared by every car: each tick every wheel with smoke-emission intensity
// (VehicleTelemetry::smoke_emit, physics §8.3b; not the slip intensity) > 0.001
// on a surface class the course enables emits one puff at its contact point (no horizontal
// velocity); puffs grow, rise, spin and fade linearly and are drawn as camera-facing,
// back-to-front sorted, alpha-blended quads with the course's smoke texture (slot 26, four
// 32x64 frames). Per class (Env0 +0x4D0, 6 x 0x44 bytes): RGB, opacity (0 = off), size, growth,
// rise, spin, fade, gravity, alphamix, fade spread n. Env0 +0x910: shade floor for the colour.

#include "base/bytes.h"
#include "render/gpu_model.h"
#include "render/view3d.h"
#include "scene/model.h"

#include <array>
#include <memory>
#include <vector>

namespace gt3r::race {

// The view tyre smoke is drawn for (GT3's per-puff cull): `forward` = camera view direction,
// `proj_x` = the projection's x scale (1 / tan(horizontal half-FOV); 0 = no size test).
// The default (zero forward) culls nothing.
struct SmokeView {
    render::Vec3 forward{0, 0, 0};
    float proj_x = 0;
    float near_m = 0.1f, far_m = 1e9f;
};

class TyreSmoke {
public:
    // `env0` = course slot 32, `tex` = course slot 26 (Tex1). False when the course has none.
    bool load(render::GpuContext& ctx, ByteSpan env0, ByteSpan tex);
    bool loaded() const { return gpu_ != nullptr; }
    void clear();
    // One wheel, one 1/60 s tick: `surface_index` = sim::surface_index of the wheel,
    // `intensity` = VehicleTelemetry::smoke_emit, `sun` = road shade A under the wheel, `right_side`
    // = wheels 1 / 3.
    void emit(const render::Vec3& contact, int surface_index, float intensity, float sun, bool right_side);
    void update(double dt);
    using View = SmokeView;
    // GT3 drops a puff when its centre depth d is not inside (near, far) or when it would be
    // more than two screen widths across (proj_x * size / d >= 2): the camera never sees a
    // puff it is inside of or right next to (no near clipping exists for them).
    static bool visible(const View& v, float depth, float size) {
        if (v.forward.x == 0 && v.forward.y == 0 && v.forward.z == 0) return true;
        return depth > v.near_m && depth < v.far_m && !(v.proj_x > 0 && v.proj_x * size >= 2.0f * depth);
    }
    // Writes the quads facing the camera, sorted back to front, and appends the item.
    // `mirror`: the rear-view mirror's pass (GT3 draws every puff whole there, without the
    // ground-line split, alpha-blended on every course: presentation §H2).
    void add_items(std::vector<render::DrawItem>& items, const render::Vec3& eye, const render::Vec3& cam_right,
                   const render::Vec3& cam_up, const View& view = {}, bool mirror = false);
    size_t live() const;

private:
    struct Class {
        float rgb[3] = {1, 1, 1};
        float opacity = 0, size = 0.3f, growth = 2.4f, vy = 0.9f, spin = 114, fade = 0.6f, gravity = 0, alphamix = 1;
        u32 n = 0;
    };
    struct Puff {
        bool live = false;
        int k = 0, cls = 0;
        bool right = false;
        float rgb[3] = {1, 1, 1};
        float size = 0, strength = 0, weight = 1, rot = 0, vy = 0, fade_m = 1, grow_m = 1;
        render::Vec3 pos, ground;
    };
    float rand01();
    std::array<Class, 6> classes_{};
    float shade_floor_ = 0.42f;
    bool additive_ = false; // GT3's reduced smoke buffer (Env0 +0x914 / +0x91C): added to the frame
    std::vector<Puff> puffs_ = std::vector<Puff>(256);
    static constexpr size_t kVerts = 10; // per puff: two polygon fans of 5 (above / below the ground line)
    std::vector<scene::Vertex> verts_;
    std::unique_ptr<render::GpuModel> gpu_;
    std::unique_ptr<render::GpuModel> gpu_blend_; // additive courses: the mirror's alpha-blended copy
    u32 rng_ = 0x12345678u;
};

} // namespace gt3r::race
