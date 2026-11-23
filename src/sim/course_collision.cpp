// The course for the collision step; see course_collision.h. The grid walks are the program's wall walk (0x228848) and
// road walk (0x2280C8), with its breakpoint search 0x2273F0, clamps 0x227558 / 0x227578 and cell lookup 0x2274A0.

#include "sim/course_collision.h"

#include <climits>

namespace gt3r::sim {

using collide::exact::F;

namespace {

constexpr int kBelow = INT_MIN + 1, kAbove = INT_MAX - 1;

// 0x2273F0: the cell index of v along sorted breakpoints: the last i with bp[i] <= v; kBelow / kAbove off the ends.
int search(const std::vector<F>& bp, F v) {
    int hi = int(bp.size()) - 1;
    if (v < bp[0]) return kBelow;
    if (bp[size_t(hi)] <= v) return kAbove;
    int lo = 0;
    while (lo < hi) {
        int mid = (lo + hi) >> 1;
        if (v < bp[size_t(mid)]) hi = mid;
        else lo = mid + 1;
    }
    return lo - 1;
}

// 0x227558 / 0x227578: clamped to the cells.
int clamp_cell(int i, size_t n) {
    if (i < 0) return 0;
    int last = int(n) - 2;
    return last < i ? last : i;
}

} // namespace

CourseCollision::CourseCollision(const course::Road& road) {
    walls_.reserve(road.wall_points.size());
    for (const course::WallPoint& w : road.wall_points)
        walls_.push_back({F(w.pos.x), F(w.pos.y), F(w.pos.z), s16(w.tag & 0xFFFF), u16(w.tag >> 16)});
    // A road built in code: polylines only (open lines), and no wall grid: every segment is a candidate.
    if (road.wall_points.empty())
        for (const std::vector<course::Vec3>& line : road.walls) {
            const int first = int(walls_.size()), n = int(line.size());
            for (int k = 0; k < n; ++k) {
                if (k + 1 < n) every_.push_back(int(walls_.size()));
                walls_.push_back({F(line[size_t(k)].x), F(line[size_t(k)].y), F(line[size_t(k)].z), 0, 0});
            }
            if (n > 1) walls_[size_t(first)].link = s16(n), walls_.back().link = s16(-n);
        }
    else if (road.wall_grid.empty())
        for (int i = 0; i < int(walls_.size()); ++i) every_.push_back(i);
    auto grid = [](const course::RoadGrid& g, Grid& out) {
        for (float x : g.xs) out.xs.push_back(F(x));
        for (float z : g.zs) out.zs.push_back(F(z));
        out.cell_begin = g.cell_begin;
        out.refs = g.refs;
    };
    grid(road.wall_grid, wall_grid_);
    grid(road.grid_index, road_grid_);
    for (const course::RoadSegment& s : road.segments) {
        neighbours_.push_back(s.neighbours);
        std::vector<Tri> ts;
        for (const course::RoadTriangle& t : s.triangles) {
            const course::Vec3 &a = s.vertices[t.a], &b = s.vertices[t.b], &c = s.vertices[t.c];
            ts.push_back({F(a.x), F(a.y), F(a.z), F(b.x), F(b.y), F(b.z), F(c.x), F(c.y), F(c.z)});
        }
        tris_.push_back(std::move(ts));
    }
    before_ = road.before_segments;
}

// The group filter of both walks: the hint's segment, its neighbours, or everything without a hint.
bool CourseCollision::passes(int seg, int hint) const {
    if (seg == hint || hint < 0) return true;
    if (size_t(hint) >= neighbours_.size()) return false;
    for (u16 n : neighbours_[size_t(hint)])
        if (n == seg) return true;
    return false;
}

// 0x228848: the cells of the box grown by 8 m (clamped into the grid), rows then columns, the groups of each cell in
// stored order.
void CourseCollision::wall_candidates(F x0, F z0, F x1, F z1, int hint, std::vector<int>& out) const {
    const Grid& g = wall_grid_;
    if (g.empty()) {
        out.insert(out.end(), every_.begin(), every_.end());
        return;
    }
    const F k8 = F::raw(0x41000000);
    int ix0 = clamp_cell(search(g.xs, x0 - k8), g.xs.size()), iz0 = clamp_cell(search(g.zs, z0 - k8), g.zs.size());
    int ix1 = clamp_cell(search(g.xs, x1 + k8), g.xs.size()), iz1 = clamp_cell(search(g.zs, z1 + k8), g.zs.size());
    const size_t nx = g.xs.size() - 1;
    for (int iz = iz0; iz <= iz1; ++iz)
        for (int ix = ix0; ix <= ix1; ++ix) {
            size_t c = size_t(iz) * nx + size_t(ix);
            for (u32 r = g.cell_begin[c]; r < g.cell_begin[c + 1]; ++r)
                if (passes(g.refs[r].segment, hint)) out.push_back(g.refs[r].triangle);
        }
}

// 0x228B68: b in a's neighbour list. Segment -1 reads the words before the segment table (Road::before_segments).
bool CourseCollision::linked(int a, int b) const {
    const std::vector<u16>* list = a >= 0 && size_t(a) < neighbours_.size() ? &neighbours_[size_t(a)] : a == -1 ? &before_ : nullptr;
    if (!list) return false;
    for (u16 n : *list)
        if (int(n) == b) return true;
    return false;
}

// 0x2280C8 (one query point): the grid cell (none off the grid), then each passing triangle: barycentric u, v strictly
// inside, its height highest so far (from -FLT_MAX).
int CourseCollision::road_segment(F x, F z, int hint) const {
    const Grid& g = road_grid_;
    if (g.empty()) return -1;
    int ix = search(g.xs, x), iz = search(g.zs, z);
    if (ix == kBelow || ix == kAbove || iz == kBelow || iz == kAbove) return -1;
    const F zero = F::raw(0), one = F::raw(0x3F800000);
    F best = F::raw(0xFF7FFFFF);
    int seg_out = -1;
    size_t c = size_t(iz) * (g.xs.size() - 1) + size_t(ix);
    for (u32 r = g.cell_begin[c]; r < g.cell_begin[c + 1]; ++r) {
        int seg = g.refs[r].segment;
        if (!passes(seg, hint)) continue;
        const Tri& t = tris_[size_t(seg)][g.refs[r].triangle];
        F bx = t.bx - t.ax, bz = t.bz - t.az, cz = t.cz - t.az, cx = t.cx - t.ax;
        F det = (bx * cz) - (cx * bz);
        F px = x - t.ax, pz = z - t.az;
        F nu = (px * cz) - (cx * pz);
        // u = nu x (1 / det) has the sign of nu xor det's (a zero nu gives a signed zero): u > 0 needs equal signs, so
        // the division is left out where it cannot pass (the same outcome as the program's).
        if (!((nu.bits >> 23) & 0xFF) || ((nu.bits ^ det.bits) & 0x80000000u)) continue;
        F inv = one / det;
        F u = nu * inv;
        if (!(zero < u)) continue;
        F v = ((bx * pz) - (px * bz)) * inv;
        if (!(zero < v)) continue;
        if (!(u + v < one)) continue;
        F h = (((t.by - t.ay) * u) + ((t.cy - t.ay) * v)) + t.ay;
        if (!(best < h)) continue;
        best = h;
        seg_out = seg;
    }
    return seg_out;
}

} // namespace gt3r::sim
