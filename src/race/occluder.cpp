#include "race/occluder.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gt3r::race {

using render::Vec3;

void Occluder::add(const scene::Model& model) {
    for (const scene::Mesh& m : model.meshes) {
        if (m.lod != 0) continue;
        const auto& t = m.transform; // column-major
        auto xf = [&](const scene::Vertex& v) {
            const float* p = v.pos;
            return Vec3{t[0] * p[0] + t[4] * p[1] + t[8] * p[2] + t[12], t[1] * p[0] + t[5] * p[1] + t[9] * p[2] + t[13],
                        t[2] * p[0] + t[6] * p[1] + t[10] * p[2] + t[14]};
        };
        for (const scene::Primitive& pr : m.primitives) {
            if (pr.flags & (scene::kAdditive | scene::kNoDepthWrite)) continue; // glows, sheen passes
            for (u32 i = pr.first_index; i + 2 < pr.first_index + pr.index_count && i + 2 < m.indices.size(); i += 3) {
                u32 a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
                if (a >= m.vertices.size() || b >= m.vertices.size() || c >= m.vertices.size()) continue;
                Vec3 pa = xf(m.vertices[a]), pb = xf(m.vertices[b]), pc = xf(m.vertices[c]);
                tris_.push_back({pa, pb - pa, pc - pa});
            }
        }
    }
}

void Occluder::build(float cell) {
    cells_.clear();
    if (tris_.empty()) return;
    cell_ = cell;
    float x1 = -1e30f, z1 = -1e30f;
    x0_ = z0_ = 1e30f;
    max_y_ = -1e30f;
    for (const Tri& t : tris_)
        for (const Vec3& p : {t.a, t.a + t.e1, t.a + t.e2}) {
            x0_ = std::min(x0_, p.x), z0_ = std::min(z0_, p.z);
            x1 = std::max(x1, p.x), z1 = std::max(z1, p.z);
            max_y_ = std::max(max_y_, p.y);
        }
    nx_ = std::max(1, int((x1 - x0_) / cell_) + 1);
    nz_ = std::max(1, int((z1 - z0_) / cell_) + 1);
    // Very large extents (distant backdrops): coarser cells keep the grid small.
    while (size_t(nx_) * size_t(nz_) > 4'000'000) {
        cell_ *= 2;
        nx_ = std::max(1, int((x1 - x0_) / cell_) + 1);
        nz_ = std::max(1, int((z1 - z0_) / cell_) + 1);
    }
    cells_.assign(size_t(nx_) * size_t(nz_), {});
    for (u32 i = 0; i < tris_.size(); ++i) {
        const Tri& t = tris_[i];
        Vec3 b = t.a + t.e1, c = t.a + t.e2;
        int ix0 = int((std::min({t.a.x, b.x, c.x}) - x0_) / cell_), ix1 = int((std::max({t.a.x, b.x, c.x}) - x0_) / cell_);
        int iz0 = int((std::min({t.a.z, b.z, c.z}) - z0_) / cell_), iz1 = int((std::max({t.a.z, b.z, c.z}) - z0_) / cell_);
        for (int z = std::max(0, iz0); z <= std::min(nz_ - 1, iz1); ++z)
            for (int x = std::max(0, ix0); x <= std::min(nx_ - 1, ix1); ++x) cells_[size_t(z) * size_t(nx_) + size_t(x)].push_back(i);
    }
    stamp_.assign(tris_.size(), 0);
}

bool Occluder::blocked(const Vec3& o, const Vec3& d, float max_dist) const {
    if (cells_.empty()) return false;
    // The stretch of the ray over the grid (a cell's margin around it: a point up to a cell short
    // of the first row still counts as in it); none: nothing to hit.
    float t0 = 0, t1 = max_dist;
    auto slab = [&](float p, float v, float lo, float hi) {
        if (v == 0) return p >= lo && p <= hi;
        float a = (lo - p) / v, b = (hi - p) / v;
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a), t1 = std::min(t1, b);
        return t0 <= t1;
    };
    if (!slab(o.x, d.x, x0_ - cell_, x0_ + float(nx_ + 1) * cell_) || !slab(o.z, d.z, z0_ - cell_, z0_ + float(nz_ + 1) * cell_))
        return false;
    ++frame_;
    // March along the ray in half-cell steps; stop above the highest triangle or out of the grid.
    float step = cell_ * 0.5f;
    for (float s = std::floor(t0 / step) * step; s < max_dist; s += step) {
        Vec3 p = o + d * s;
        if (p.y > max_y_ && d.y >= 0) return false;
        int ix = int((p.x - x0_) / cell_), iz = int((p.z - z0_) / cell_);
        if (ix < 0 || iz < 0 || ix >= nx_ || iz >= nz_) {
            if ((ix < 0 && d.x <= 0) || (ix >= nx_ && d.x >= 0) || (iz < 0 && d.z <= 0) || (iz >= nz_ && d.z >= 0)) return false;
            continue;
        }
        for (u32 ti : cells_[size_t(iz) * size_t(nx_) + size_t(ix)]) {
            if (stamp_[ti] == frame_) continue;
            stamp_[ti] = frame_;
            const Tri& t = tris_[ti];
            // Moller-Trumbore.
            Vec3 pv = cross(d, t.e2);
            float det = dot(t.e1, pv);
            if (std::fabs(det) < 1e-8f) continue;
            float inv = 1.0f / det;
            Vec3 tv = o - t.a;
            float u = dot(tv, pv) * inv;
            if (u < 0 || u > 1) continue;
            Vec3 qv = cross(tv, t.e1);
            float v = dot(d, qv) * inv;
            if (v < 0 || u + v > 1) continue;
            float dist = dot(t.e2, qv) * inv;
            if (dist > 0.05f && dist < max_dist) return true;
        }
    }
    return false;
}

} // namespace gt3r::race
