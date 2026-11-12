#pragma once

// CPU ray test against a model's opaque triangles: the course's scenery and backdrop, for the
// sun glare's and the lamp glows' visibility, and each car's body (in its own frame), for the
// lamp glows' (GT3 depth-tests a box of pixels around the point against the finished
// frame; we cast rays through the same box). The triangles sit in a bounding volume tree, so a
// ray tests only the few whose boxes it crosses: dense city blocks (Seattle's downtown packs
// over 1,500 triangles into some 16 m squares) cost no more than open country.

#include "render/math3d.h"
#include "scene/model.h"

#include <cmath>
#include <vector>

namespace gt3r::race {

class Occluder {
public:
    // Adds a model's opaque, non-additive LOD-0 triangles (mesh transforms applied).
    void add(const scene::Model& model);
    // Builds the tree over the triangles added so far.
    void build();
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
    // A tree node: its box; a leaf holds tris_[first, first + count), an inner node (count 0)
    // has its children at `first` and `first` + 1.
    struct Node {
        float lo[3], hi[3];
        u32 first = 0, count = 0;
    };
    std::vector<Tri> tris_;
    std::vector<Node> nodes_;
};

} // namespace gt3r::race
