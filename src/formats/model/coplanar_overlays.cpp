#include "formats/model/coplanar_overlays.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <vector>

namespace gt3r::gtm1 {

namespace {

using P3 = std::array<float, 3>;
using Corners = std::array<P3, 3>;

constexpr float kPlaneTolerance = 0.005f; // m: GT3's Z merges faces this close at race distances
constexpr float kOverlapMargin = 0.001f;  // m: more than touching along an edge
constexpr float kSameFacing = 0.9995f;    // cos of the largest angle between the two faces

struct Tri {
    Corners p;
    P3 n;
    float d = 0;
    P3 lo, hi;
    Corners key; // the corners sorted (the same triangle in any order)
};

P3 sub(const P3& a, const P3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
float dot(const P3& a, const P3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// The two triangles projected on the plane of `a` overlap by more than kOverlapMargin along every
// separating axis (the six edge normals).
bool overlaps(const Tri& a, const Tri& b) {
    int ax = 0;
    for (int k = 1; k < 3; ++k)
        if (std::fabs(a.n[size_t(k)]) > std::fabs(a.n[size_t(ax)])) ax = k;
    const int u = ax == 0 ? 1 : 0, v = ax == 2 ? 1 : 2;
    std::array<std::array<float, 2>, 3> A, B;
    for (size_t k = 0; k < 3; ++k) {
        A[k] = {a.p[k][size_t(u)], a.p[k][size_t(v)]};
        B[k] = {b.p[k][size_t(u)], b.p[k][size_t(v)]};
    }
    for (const auto* t : {&A, &B})
        for (size_t e = 0; e < 3; ++e) {
            const auto& p0 = (*t)[e];
            const auto& p1 = (*t)[(e + 1) % 3];
            float nx = p0[1] - p1[1], ny = p1[0] - p0[0];
            const float len = std::sqrt(nx * nx + ny * ny);
            if (len < 1e-6f) return false;
            nx /= len, ny /= len;
            float amin = 1e30f, amax = -1e30f, bmin = 1e30f, bmax = -1e30f;
            for (size_t k = 0; k < 3; ++k) {
                const float pa = A[k][0] * nx + A[k][1] * ny, pb = B[k][0] * nx + B[k][1] * ny;
                amin = std::min(amin, pa), amax = std::max(amax, pa);
                bmin = std::min(bmin, pb), bmax = std::max(bmax, pb);
            }
            if (std::min(amax, bmax) - std::max(amin, bmin) <= kOverlapMargin) return false;
        }
    return true;
}

} // namespace

int mark_coplanar_overlays(scene::Mesh& mesh) {
    // Every triangle with its primitive, then the pairs whose boxes meet (sorted by their
    // lowest x and swept: no grid, so a kilometre-wide backdrop triangle costs no more).
    std::vector<Tri> tris;
    std::vector<u32> prim_of;
    for (u32 pi = 0; pi < mesh.primitives.size(); ++pi) {
        const scene::Primitive& prim = mesh.primitives[pi];
        for (u32 i = prim.first_index; i + 2 < prim.first_index + prim.index_count; i += 3) {
            Tri t;
            for (size_t k = 0; k < 3; ++k) {
                const float* q = mesh.vertices[mesh.indices[i + k]].pos;
                t.p[k] = {q[0], q[1], q[2]};
            }
            const P3 e1 = sub(t.p[1], t.p[0]), e2 = sub(t.p[2], t.p[0]);
            P3 n{e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
            const float len = std::sqrt(dot(n, n));
            if (len < 1e-6f) continue; // degenerate (strip joins)
            t.n = {n[0] / len, n[1] / len, n[2] / len};
            t.d = dot(t.n, t.p[0]);
            for (size_t k = 0; k < 3; ++k) {
                t.lo[k] = std::min({t.p[0][k], t.p[1][k], t.p[2][k]}) - kPlaneTolerance;
                t.hi[k] = std::max({t.p[0][k], t.p[1][k], t.p[2][k]}) + kPlaneTolerance;
            }
            t.key = t.p;
            std::sort(t.key.begin(), t.key.end());
            tris.push_back(t);
            prim_of.push_back(pi);
        }
    }
    // later[j]: primitive j lies on an earlier depth-writing primitive's surface.
    std::vector<bool> later(mesh.primitives.size(), false);
    auto lies_on = [](const Tri& t, const Tri& s) { // t on s's plane, facing the same way, overlapping
        if (s.key == t.key || dot(s.n, t.n) < kSameFacing) return false; // (same vertices: equal depth)
        for (const P3& q : t.p)
            if (std::fabs(dot(s.n, q) - s.d) > kPlaneTolerance) return false;
        return overlaps(s, t);
    };
    // The pairs, swept along x on a compact copy (box, normal, primitive).
    struct Box {
        float lo[3], hi[3], n[3];
        u32 prim, tri;
    };
    std::vector<Box> boxes(tris.size());
    for (u32 i = 0; i < tris.size(); ++i) {
        Box& b = boxes[i];
        for (size_t k = 0; k < 3; ++k) b.lo[k] = tris[i].lo[k], b.hi[k] = tris[i].hi[k], b.n[k] = tris[i].n[k];
        b.prim = prim_of[i], b.tri = i;
    }
    std::sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) { return a.lo[0] < b.lo[0]; });
    std::vector<const Box*> active;
    for (const Box& a : boxes) {
        size_t w = 0; // drop the boxes that end before this one starts
        for (const Box* b : active)
            if (b->hi[0] >= a.lo[0]) active[w++] = b;
        active.resize(w);
        for (const Box* b : active) {
            if (b->prim == a.prim || a.n[0] * b->n[0] + a.n[1] * b->n[1] + a.n[2] * b->n[2] < kSameFacing ||
                a.hi[1] < b->lo[1] || b->hi[1] < a.lo[1] || a.hi[2] < b->lo[2] || b->hi[2] < a.lo[2])
                continue;
            const Box& i = a.prim < b->prim ? a : *b; // drawn first
            const Box& j = &i == &a ? *b : a;
            if (later[j.prim] || (mesh.primitives[i.prim].flags & scene::kNoDepthWrite)) continue;
            if (lies_on(tris[j.tri], tris[i.tri])) later[j.prim] = true;
        }
        active.push_back(&a);
    }
    // In draw order: the primitives found above, and the passes over a marked one's vertices.
    std::set<Corners> marked_tris;
    int marked = 0;
    size_t t = 0;
    for (u32 pi = 0; pi < mesh.primitives.size(); ++pi) {
        const size_t first = t;
        while (t < tris.size() && prim_of[t] == pi) ++t;
        bool overlay = later[pi];
        for (size_t k = first; k < t && !overlay; ++k) overlay = marked_tris.count(tris[k].key) > 0;
        if (!overlay) continue;
        mesh.primitives[pi].flags |= scene::kCoplanarOverlay;
        ++marked;
        for (size_t k = first; k < t; ++k) marked_tris.insert(tris[k].key);
    }
    return marked;
}

} // namespace gt3r::gtm1
