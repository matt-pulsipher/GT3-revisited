#include "race/replay_post.h"

#include "base/ee_float.h"
#include "race/sun_glare.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt3r::race {

namespace {

// The lens shape: 0.58 (half-width of the top / bottom edges, x S), 0.5
// (half-height, an immediate), 0.45 (their texels' height), 1.6 (the side
// texels: 1.6 x 0.58 S = 0.928 S).
constexpr float kEdge = 0.58f, kHalfHeight = 0.5f, kTexelHeight = 0.45f, kSideTexel = 1.6f;
// The frame the original draws the lenses in: 640 x 448, the display buffers 640 x 224 (one
// field): heights after the projection are field lines, two of the frame's.
constexpr float kFrameW = 640.0f, kFrameH = 448.0f, kFieldLine = 2.0f;

} // namespace

float haze_wrap(float d, float L) {
    // 1 / L, times d, truncated towards zero, times L; then + L for d < 0.
    const float q = 1.0f / L;
    float r = d - std::trunc(d * q) * L;
    if (d < 0) r += L;
    return r - L * 0.5f;
}

std::array<HazeVertex, 6> haze_hexagon(float x, float y, float s) {
    // Positions and texels (frame pixels; y down) in the strip's order: (+, +), (-, +), (+side, 0),
    // (-side, 0), (+, -), (-, -). Vertical offsets are the field's lines (x 2 in the frame).
    const float ex = kEdge * s, hy = kHalfHeight * s * kFieldLine, side = 2.0f * ex;
    const float ty = kTexelHeight * s * kFieldLine, tside = kSideTexel * ex;
    std::array<HazeVertex, 6> v{};
    v[0] = {x + ex, y + hy, x + ex, y + ty};
    v[1] = {std::max(x - ex, 0.0f), y + hy, std::max(x - ex, 0.0f), y + ty};
    v[2] = {x + side, y, x + tside, y};
    v[3] = {std::max(x - side, 0.0f), y, std::max(x - tside, 0.0f), y};
    // (The original keeps this vertex's y at or below the frame's left edge's x, a mix-up of its
    // two edge values that changes nothing while S is under 192 pixels; its texel's v >= 0.)
    v[4] = {x + ex, y - hy, x + ex, std::max(y - ty, 0.0f)};
    v[5] = {std::max(x - ex, 0.0f), std::max(y - hy, 0.0f), std::max(x - ex, 0.0f), std::max(y - ty, 0.0f)};
    return v;
}

void HeatHaze::setup(const SunParams& p) {
    particles_.clear();
    if (!p.haze || p.haze_count == 0) return;
    const u32 n = std::min(p.haze_count, kMaxLenses);
    // The EE's arithmetic for the draws ((r - 1) (hi - lo) + lo, r in [1, 2) from the
    // generator's low 23 bits), so the field starts where the original's does.
    u32 seed = kSeed;
    auto draw = [&](ee::F lo, ee::F hi) {
        seed = seed * kMultiplier + 1u;
        const ee::F r = ee::F::raw(0x3F800000u | (seed & 0x7FFFFFu));
        return (r - ee::F(1.0f)) * (hi - lo) + lo;
    };
    // For every particle, the last first: x, y, z, vx, vz drawn; vy the constant.
    auto fill = [&](ee::F box, ee::F cap, ee::F vx, ee::F vy, ee::F vz) {
        const ee::F half = ee::F(0.5f);
        for (u32 i = n; i-- > 0;) {
            Particle& q = particles_[i];
            q.x = draw(-box, box).host();
            q.y = draw(-cap * half, cap * half).host();
            q.z = draw(-box, box).host();
            q.vx = draw(-vx, vx).host();
            q.vy = vy.host();
            q.vz = draw(-vz, vz).host();
        }
    };
    particles_.resize(n);
    // The defaults (box 32, cap 8, drift 0.5 / 0.01 / 0.5), then the program
    // with Env0 +0xB8 / +0xBC / +0xC4 / +0xC8 / +0xC4.
    fill(ee::F(32.0f), ee::F(8.0f), ee::F(0.5f), ee::F(0.01f), ee::F(0.5f));
    fill(ee::F(p.haze_box), ee::F(p.haze_cap), ee::F(p.haze_drift_xz), ee::F(p.haze_drift_y), ee::F(p.haze_drift_xz));
    size_ = p.haze_size, box_ = p.haze_box, cap_ = p.haze_cap, ahead_ = p.haze_ahead;
}

void HeatHaze::step() {
    // p += v per component (in the EE's float rounding).
    for (Particle& q : particles_) {
        q.x = (ee::F(q.x) + ee::F(q.vx)).host();
        q.y = (ee::F(q.y) + ee::F(q.vy)).host();
        q.z = (ee::F(q.z) + ee::F(q.vz)).host();
    }
}

void HeatHaze::lenses(const render::Mat4& view, const render::Mat4& proj, std::vector<HazeLens>& out) const {
    out.clear();
    if (particles_.empty()) return;
    // m: the projection's x scale (the original reads it from its projection-only matrix).
    const float m = proj.at(0, 0);
    if (!(m > 0)) return;
    const float B = (1.0f / m) * (box_ + box_);        // the box's edge across (div.s, then x 2 b)
    const float S = size_ * std::sqrt(std::fabs(m));   // sqrt.s takes |m|
    const float Ly = cap_ < B ? cap_ : B;
    // The centre: (0, 0, -ahead) through the inverted view rotation (translation removed).
    const render::Vec3 c = render::Vec3{-view.at(2, 0), -view.at(2, 1), -view.at(2, 2)} * ahead_;
    // The lenses' matrix: the view's rotation (its translation zeroed) then the projection. The
    // original reads m from the projection alone, then the program rebuilds the draw composite
    // (the modelview, dirty since its translation was zeroed, times the projection)
    // before projecting the lenses, so the box is always ahead of the camera, whatever its heading.
    render::Mat4 rot = view;
    rot.at(0, 3) = rot.at(1, 3) = rot.at(2, 3) = 0;
    const render::Mat4 M = proj * rot;
    for (size_t i = particles_.size(); i-- > 0;) {
        const Particle& q = particles_[i];
        const float x = c.x + haze_wrap(q.x - c.x, B);
        const float z = c.z + haze_wrap(q.z - c.z, B);
        const float y = c.y + haze_wrap(q.y - c.y, Ly);
        const float cx = M.at(0, 0) * x + M.at(0, 1) * y + M.at(0, 2) * z + M.at(0, 3);
        const float cy = M.at(1, 0) * x + M.at(1, 1) * y + M.at(1, 2) * z + M.at(1, 3);
        const float cz = M.at(2, 0) * x + M.at(2, 1) * y + M.at(2, 2) * z + M.at(2, 3);
        const float cw = M.at(3, 0) * x + M.at(3, 1) * y + M.at(3, 2) * z + M.at(3, 3);
        // Inside every clip plane (a negative w is always out).
        if (!(cw > 0) || std::fabs(cx) > cw || std::fabs(cy) > cw || cz < 0 || cz > cw) continue;
        HazeLens l;
        l.x = kFrameW * 0.5f * (1.0f + cx / cw);
        l.y = kFrameH * 0.5f * (1.0f - cy / cw);
        l.size = S;
        l.depth = cz / cw;
        out.push_back(l);
    }
}

void add_haze_lenses(render::ScreenBatch& b, const std::vector<HazeLens>& lenses) {
    b.rect = {0, 0, kFrameW, kFrameH};
    if (lenses.empty()) return;
    std::vector<render::ScreenBatch::Vertex> verts;
    std::vector<u32> idx;
    verts.reserve(lenses.size() * 6);
    idx.reserve(lenses.size() * 12);
    for (const HazeLens& l : lenses) {
        const u32 base = u32(verts.size());
        for (const HazeVertex& h : haze_hexagon(l.x, l.y, l.size)) {
            render::ScreenBatch::Vertex v;
            v.x = h.x, v.y = h.y;
            // CLAMP: REGION_CLAMP to the frame.
            v.u = std::clamp(h.u, 0.0f, kFrameW), v.v = std::clamp(h.v, 0.0f, kFrameH);
            v.depth = l.depth;
            verts.push_back(v);
        }
        // The strip's four triangles.
        for (u32 t = 0; t < 4; ++t)
            for (u32 k = 0; k < 3; ++k) idx.push_back(base + t + k);
    }
    // ALPHA 0x64, FIX 64: (Cs - Cd) x 0.5 + Cd. View3D draws the batch sampling the picture.
    b.add(nullptr, verts.data(), verts.size(), idx.data(), idx.size(), 1, render::BlendMode::Alpha);
}

std::array<VignetteVertex, 64> vignette_ring(const SunParams& p) {
    // RGB x 255, A x 128, + 0.5, clamped to [0, 255], truncated.
    auto bytes = [](const std::array<float, 4>& c, u8 out[4]) {
        for (size_t i = 0; i < 4; ++i) {
            const float k = i < 3 ? 255.0f : 128.0f;
            out[i] = u8(std::min(std::max(c[i] * k, 0.0f) + 0.5f, 255.0f));
        }
    };
    std::array<VignetteVertex, 64> v{};
    constexpr float kTwoPi = 6.28318548f;
    for (int i = 0; i < 32; ++i) {
        const float a = (i == 31 ? 0.0f : float(i) * kTwoPi) / 31.0f;
        const float s = std::sin(a), co = std::cos(a);
        VignetteVertex& o = v[size_t(2 * i)];
        VignetteVertex& n = v[size_t(2 * i + 1)];
        o.x = co * p.vignette_outer, o.y = s * p.vignette_outer;
        n.x = co * p.vignette_inner, n.y = s * p.vignette_inner;
        bytes(p.vignette_outer_rgba, o.rgba);
        bytes(p.vignette_inner_rgba, n.rgba);
    }
    return v;
}

void add_vignette(render::ScreenBatch& b, const SunParams& p) {
    b.rect = {0, 0, kFrameW, kFrameH};
    const std::array<VignetteVertex, 64> ring = vignette_ring(p);
    render::ScreenBatch::Vertex verts[64];
    for (size_t i = 0; i < ring.size(); ++i) {
        // Ortho -320..320 x -224..224 about the frame's centre, y up.
        verts[i].x = kFrameW * 0.5f + ring[i].x;
        verts[i].y = kFrameH * 0.5f - ring[i].y;
        for (int k = 0; k < 3; ++k) verts[i].rgba[k] = float(ring[i].rgba[k]) / 255.0f;
        verts[i].rgba[3] = std::min(float(ring[i].rgba[3]) / 128.0f, 1.0f); // GS alpha: 128 = 1.0
    }
    u32 idx[62 * 3];
    for (u32 t = 0; t < 62; ++t)
        for (u32 k = 0; k < 3; ++k) idx[t * 3 + k] = t + k;
    // ALPHA 0x44: (Cs - Cd) As + Cd, Gouraud, untextured.
    b.add(nullptr, verts, 64, idx, 62 * 3, 1, render::BlendMode::Alpha);
}

} // namespace gt3r::race
