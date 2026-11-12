#include "race/occluder.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gt3r::race {

using render::Vec3;

namespace {

// A triangle's box and centre, for the build.
struct Ref {
    float lo[3], hi[3], c[3];
    u32 tri;
};

constexpr u32 kLeafTris = 4;
constexpr int kBins = 16;
// Below this depth the lists are halved instead (a balanced tree under it), so that no ray's
// stack of deferred nodes passes kStack.
constexpr u32 kSahDepth = 48;
constexpr int kStack = 128;

struct Box {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    void grow(const float l[3], const float h[3]) {
        for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], l[k]), hi[k] = std::max(hi[k], h[k]);
    }
    float area() const {
        if (lo[0] > hi[0]) return 0;
        const float x = hi[0] - lo[0], y = hi[1] - lo[1], z = hi[2] - lo[2];
        return x * y + y * z + z * x;
    }
};

} // namespace

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

// A binned surface-area split per node (16 bins on the axis where the centres spread most),
// down to leaves of up to four triangles.
void Occluder::build() {
    nodes_.clear();
    if (tris_.empty()) return;
    std::vector<Ref> refs(tris_.size());
    for (u32 i = 0; i < tris_.size(); ++i) {
        const Tri& t = tris_[i];
        const Vec3 b = t.a + t.e1, c = t.a + t.e2;
        const float p[3][3] = {{t.a.x, t.a.y, t.a.z}, {b.x, b.y, b.z}, {c.x, c.y, c.z}};
        Ref& r = refs[i];
        r.tri = i;
        for (int k = 0; k < 3; ++k) {
            const float v0 = p[0][k], v1 = p[1][k], v2 = p[2][k];
            r.lo[k] = std::min({v0, v1, v2}), r.hi[k] = std::max({v0, v1, v2});
            r.c[k] = 0.5f * (r.lo[k] + r.hi[k]);
        }
    }
    nodes_.reserve(2 * refs.size() / kLeafTris + 1);
    struct Task {
        u32 node, first, count, depth;
    };
    std::vector<Task> todo;
    nodes_.emplace_back();
    todo.push_back({0, 0, u32(refs.size()), 0});
    while (!todo.empty()) {
        const Task t = todo.back();
        todo.pop_back();
        Box box, cbox;
        for (u32 i = t.first; i < t.first + t.count; ++i) box.grow(refs[i].lo, refs[i].hi), cbox.grow(refs[i].c, refs[i].c);
        // Padded a little: a flat triangle's box has no thickness, which the ray's slab test could
        // miss in rounding.
        for (int k = 0; k < 3; ++k) nodes_[t.node].lo[k] = box.lo[k] - 1e-3f, nodes_[t.node].hi[k] = box.hi[k] + 1e-3f;
        int axis = 0;
        for (int k = 1; k < 3; ++k)
            if (cbox.hi[k] - cbox.lo[k] > cbox.hi[axis] - cbox.lo[axis]) axis = k;
        const float span = cbox.hi[axis] - cbox.lo[axis];
        u32 split = 0; // the first ref of the right half; 0: a leaf
        if (t.count > kLeafTris && span > 0 && t.depth < kSahDepth) {
            Box bins[kBins];
            u32 counts[kBins] = {};
            const float scale = float(kBins) / span;
            auto bin_of = [&](const Ref& r) { return std::min(kBins - 1, int((r.c[axis] - cbox.lo[axis]) * scale)); };
            for (u32 i = t.first; i < t.first + t.count; ++i) {
                const int b = bin_of(refs[i]);
                bins[b].grow(refs[i].lo, refs[i].hi), ++counts[b];
            }
            float right_cost[kBins] = {};
            Box acc;
            u32 n = 0;
            for (int b = kBins - 1; b > 0; --b) {
                acc.grow(bins[b].lo, bins[b].hi), n += counts[b];
                right_cost[b] = acc.area() * float(n);
            }
            Box left;
            u32 nl = 0;
            float best = 1e30f;
            int best_b = 0;
            for (int b = 1; b < kBins; ++b) {
                left.grow(bins[b - 1].lo, bins[b - 1].hi), nl += counts[b - 1];
                const float cost = left.area() * float(nl) + right_cost[b];
                if (nl > 0 && nl < t.count && cost < best) best = cost, best_b = b;
            }
            if (best_b > 0) {
                Ref* mid = std::partition(refs.data() + t.first, refs.data() + t.first + t.count,
                                          [&](const Ref& r) { return bin_of(r) < best_b; });
                split = u32(mid - refs.data());
            }
        }
        if (split == 0 && t.count > kLeafTris) { // deep in the tree, or no split found: halve it
            std::nth_element(refs.data() + t.first, refs.data() + t.first + t.count / 2, refs.data() + t.first + t.count,
                             [&](const Ref& a, const Ref& b) { return a.c[axis] < b.c[axis]; });
            split = t.first + t.count / 2;
        }
        if (split == 0) {
            nodes_[t.node].first = t.first, nodes_[t.node].count = t.count;
            continue;
        }
        // Children: a pair, the left one at `first`.
        const u32 left_node = u32(nodes_.size()), right_node = left_node + 1;
        nodes_.emplace_back(), nodes_.emplace_back();
        nodes_[t.node].first = left_node, nodes_[t.node].count = 0;
        todo.push_back({right_node, split, t.first + t.count - split, t.depth + 1});
        todo.push_back({left_node, t.first, split - t.first, t.depth + 1});
    }
    std::vector<Tri> sorted(tris_.size());
    for (size_t i = 0; i < refs.size(); ++i) sorted[i] = tris_[refs[i].tri];
    tris_ = std::move(sorted);
}

bool Occluder::blocked(const Vec3& o, const Vec3& d, float max_dist) const {
    if (nodes_.empty()) return false;
    // 1 / d with no infinities (a zero component would give 0 x inf = NaN on a box face).
    const float id[3] = {1.0f / (d.x != 0 ? d.x : 1e-30f), 1.0f / (d.y != 0 ? d.y : 1e-30f), 1.0f / (d.z != 0 ? d.z : 1e-30f)};
    const float org[3] = {o.x, o.y, o.z};
    auto enters = [&](const Node& n) {
        float t0 = 0, t1 = max_dist;
        for (int k = 0; k < 3; ++k) {
            float a = (n.lo[k] - org[k]) * id[k], b = (n.hi[k] - org[k]) * id[k];
            if (a > b) std::swap(a, b);
            t0 = std::max(t0, a), t1 = std::min(t1, b);
        }
        return t0 <= t1;
    };
    u32 stack[kStack];
    int top = 0;
    u32 at = 0;
    if (!enters(nodes_[0])) return false;
    for (;;) {
        const Node& n = nodes_[at];
        if (n.count == 0) {
            const u32 l = n.first, r = n.first + 1;
            const bool hl = enters(nodes_[l]), hr = enters(nodes_[r]);
            if (hl && hr) {
                stack[top++] = r;
                at = l;
                continue;
            }
            if (hl || hr) {
                at = hl ? l : r;
                continue;
            }
        } else {
            for (u32 i = n.first; i < n.first + n.count; ++i) {
                const Tri& t = tris_[i];
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
        if (top == 0) return false;
        at = stack[--top];
    }
}

} // namespace gt3r::race
