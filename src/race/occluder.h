#pragma once

// CPU ray test against a model's opaque triangles: the course's scenery and backdrop, for the
// sun glare's and the lamp glows' visibility, and each car's body (in its own frame), for the
// lamp glows' (GT3 depth-tests a box of pixels around the point against the finished
// frame; we cast rays through the same box). Triangles are binned in an XZ grid; a ray walks the
// grid from where it meets it until it passes above the highest triangle.

#include "render/math3d.h"
#include "scene/model.h"

#include <cmath>
#include <vector>

namespace gt3r::race {

class Occluder {
public:
    // Adds a model's opaque, non-additive LOD-0 triangles (mesh transforms applied).
    void add(const scene::Model& model);
    void build(float cell = 16.0f);
    bool empty() const { return tris_.empty(); }
    // True if the ray from `o` along unit `d` hits a triangle within `max_dist`.
    bool blocked(const render::Vec3& o, const render::Vec3& d, float max_dist) const;
    bool segment_blocked(const render::Vec3& a, const render::Vec3& b) const {
        render::Vec3 d = b - a;
        float len = std::sqrt(dot(d, d));
        return len > 1e-3f && blocked(a, d * (1.0f / len), len);
    }
    size_t triangles() const { return tris_.size(); }

private:
    struct Tri {
        render::Vec3 a, e1, e2;
    };
    std::vector<Tri> tris_;
    std::vector<std::vector<u32>> cells_;
    float x0_ = 0, z0_ = 0, cell_ = 16, max_y_ = 0;
    int nx_ = 0, nz_ = 0;
    mutable std::vector<u32> stamp_;
    mutable u32 frame_ = 0;
};

} // namespace gt3r::race
