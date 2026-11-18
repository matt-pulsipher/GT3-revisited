// The course's ground query; see course_ground.h. With its cell lookup (RoadCells), breakpoint search
// and neighbour test. The program groups the records by grid cell and tests each triangle of a cell against
// every record in it; each record's result depends only on its own point, so here each record walks its cell alone.

#include "sim/exact/course_ground.h"

#include <bit>
#include <climits>
#include <cmath>
#include <limits>

namespace gt3r::sim::exact {

namespace {

constexpr int kBelow = INT_MIN + 1, kAbove = INT_MAX - 1;

// The last i with bp[i] <= v; kBelow / kAbove off the ends.
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

} // namespace

RoadCells::RoadCells(const course::RoadGrid& g) {
    if (g.empty()) return;
    for (float x : g.xs) xs_.push_back(F(x));
    for (float z : g.zs) zs_.push_back(F(z));
}

int RoadCells::cell(F x, F z) const {
    if (xs_.size() < 2 || zs_.size() < 2) return -1;
    const int ix = search(xs_, x), iz = search(zs_, z);
    if (ix == kBelow || ix == kAbove || iz == kBelow || iz == kAbove) return -1;
    return int(xs_.size() - 1) * iz + ix;
}

CourseGround::CourseGround(const course::Road& road) : cells_(road.grid_index) {
    const F one = F::raw(0x3F800000), k65535 = F::raw(0x477FFF00);
    // Every triangle's terms, segment by segment (the parse checked the vertex indices).
    std::vector<u32> first(road.segments.size() + 1, 0);
    for (size_t si = 0; si < road.segments.size(); ++si) {
        const course::RoadSegment& s = road.segments[si];
        first[si + 1] = first[si] + u32(s.triangles.size());
        neighbours_.push_back(s.neighbours);
        auto attrs = [&](size_t k) { return k < s.vertex_attrs.size() ? s.vertex_attrs[k] : 0u; };
        for (const course::RoadTriangle& t : s.triangles) {
            const course::Vec3 &a = s.vertices[t.a], &b = s.vertices[t.b], &c = s.vertices[t.c];
            const F ax = F(a.x), ay = F(a.y), az = F(a.z);
            const F bx = F(b.x) - ax, bz = F(b.z) - az, cz = F(c.z) - az, cx = F(c.x) - ax;
            const F inv = one / ((bx * cz) - (cx * bz));
            const F by = F(b.y) - ay, cy = F(c.y) - ay;
            // The determinant's exact value (by inv's sign) plus its bound (maybe_inside), rounded up to a float.
            const double dbx = ee::exact_value(bx.bits), dbz = ee::exact_value(bz.bits), dcx = ee::exact_value(cx.bits),
                         dcz = ee::exact_value(cz.bits), sd = (inv.bits >> 31) ? -1.0 : 1.0;
            const double det = sd * ((dbx * dcz) - (dcx * dbz)), td = (std::fabs(dbx * dcz) + std::fabs(dcx * dbz)) * 0x1p-14 + 0x1p-100;
            float upper = std::numeric_limits<float>::infinity();
            if (det > td && det + td < 0x1p100) upper = std::nextafter(float(det + td), upper);
            test_.push_back({ax, az, bx, bz, cx, cz, inv, ay, by, cy, upper});
            for (F x : {ax, az, bx, bz, cx, cz}) sides_ = sides_ && ee::side_range(x.bits);
            TriHit h;
            const u32 va = attrs(t.a), vb = attrs(t.b), vc = attrs(t.c);
            auto scalar = [&](u16 x) { return ee::int_to_f(x) / k65535; };
            const F fa = scalar(u16(va)), fb = scalar(u16(vb)), fc = scalar(u16(vc));
            h.a0 = fa, h.a1 = fb - fa, h.a2 = fc - fa;
            const F ga = scalar(u16(va >> 16)), gb = scalar(u16(vb >> 16)), gc = scalar(u16(vc >> 16));
            h.b0 = ga, h.b1 = gb - ga, h.b2 = gc - ga;
            h.dz = ((bx * cy) - (cx * by)) * inv, h.dx = ((cz * by) - (bz * cy)) * inv;
            h.plane_d = -(((h.dx * ax) + (h.dz * az)) - ay);
            h.attr_a_raw = u16(va), h.attr_b_raw = u16(va >> 16);
            h.material = u8(t.edge_flags), h.flags = u8(t.surface);
            hit_.push_back(h);
        }
    }
    // The cells' references in order, in runs of one segment.
    const course::RoadGrid& g = road.grid_index;
    if (g.empty()) return;
    cell_runs_.push_back(0);
    for (size_t cell = 0; cell + 1 < g.cell_begin.size(); ++cell) {
        for (u32 k = g.cell_begin[cell]; k < g.cell_begin[cell + 1]; ++k) {
            const course::RoadGridRef& r = g.refs[k];
            if (runs_.size() == cell_runs_.back() || runs_.back().segment != r.segment)
                runs_.push_back({r.segment, u32(tris_.size()), u32(tris_.size())});
            tris_.push_back(first[r.segment] + r.triangle);
            runs_.back().end = u32(tris_.size());
        }
        cell_runs_.push_back(u32(runs_.size()));
    }
}

// The records of `lanes` (bit r: record r) whose point may lie in the triangle: the others certainly fail the containment
// test below, from the exact values (host floats: the points' and the triangle's terms are floats, exponent-0 values zeros)
// and bounds on the chopped operations' errors. The side tests nu and nv are within 2^-19 (|a| max(|b|, |c|) + |d| max(|e|,
// |f|)) of their exact values and the determinant within 2^-21 (|bx cz| + |cx bz|) of its (ee::side_sign); each counts as certain
// beyond 2^-14 times its terms (32 times the bound, far beyond the float evaluation's own error): nu or nv not of inv's sign
// (u = nu x inv or v not above 0), or both of its sign and u + v at least 1 beyond its bound (the chopped u, v and sum each
// below their exact values by less than 2^-20 of them; from the determinant's exact value plus its bound, `upper`, with a
// margin of 2^-13). One pass over the four records' points (vectorised).
unsigned CourseGround::maybe_inside(const TriTest& t, const float (&qx)[4], const float (&qz)[4], unsigned lanes) {
    const float ax = ee::fast::host(t.ax.bits), az = ee::fast::host(t.az.bits), bx = ee::fast::host(t.bx.bits),
                bz = ee::fast::host(t.bz.bits), cx = ee::fast::host(t.cx.bits), cz = ee::fast::host(t.cz.bits);
    const float s = (t.inv.bits >> 31) ? -1.0f : 1.0f, upper = t.upper * (1.0f + 0x1p-13f);
    unsigned maybe = 0;
    for (int l = 0; l < 4; ++l) {
        const float px = qx[l] - ax, pz = qz[l] - az;
        const float mx = std::fmax(std::fabs(qx[l]), std::fabs(ax)), mz = std::fmax(std::fabs(qz[l]), std::fabs(az));
        const float nu = s * ((cz * px) - (cx * pz)), tu = (std::fabs(cz) * mx + std::fabs(cx) * mz) * 0x1p-14f + 0x1p-100f;
        const float nv = s * ((bx * pz) - (bz * px)), tv = (std::fabs(bx) * mz + std::fabs(bz) * mx) * 0x1p-14f + 0x1p-100f;
        const bool out = (nu < -tu) | (nv < -tv) | ((nu > tu) & (nv > tv) & (((nu - tu) + (nv - tv)) >= upper));
        maybe |= unsigned(!out) << l;
    }
    return maybe & lanes;
}

// The group filter: the hint's segment, its neighbours (seg + 0x16: u16 count, ids), or everything without a hint.
bool CourseGround::passes(u32 seg, int hint) const {
    if (hint < 0 || seg == u32(hint)) return true;
    if (size_t(hint) >= neighbours_.size()) return false;
    for (u16 n : neighbours_[size_t(hint)])
        if (n == seg) return true;
    return false;
}

void CourseGround::query(std::array<Ground, 4>& g, const Behaviour& how) const {
    const F lowest = F::raw(gt3_program(how.rule(Rule::GroundMiss)) ? 0xFF7FFFFF : 0xFFFFFFFF);
    const int hint = g[0].seg;
    const F zero = F::raw(0), one = F::raw(0x3F800000);
    std::array<int, 4> cells;
    for (size_t r = 0; r < 4; ++r) {
        g[r].height = lowest;
        g[r].seg = -1;
        cells[r] = cells_.cell(g[r].x, g[r].z);
    }
    // Each record's highest triangle; the records in one cell walk it together (each in the cell's order: the same result).
    std::array<const TriHit*, 4> best{};
    std::array<F, 4> bu, bv;
    float qx[4] = {}, qz[4] = {};
    unsigned ranged = 0; // the records whose point is in maybe_inside's range (the others take the full test)
    for (size_t r = 0; r < 4; ++r)
        if (sides_ && ee::side_operand(g[r].x, qx[r]) && ee::side_operand(g[r].z, qz[r])) ranged |= 1u << r;
    unsigned done = 0;
    for (size_t r = 0; r < 4; ++r) {
        if (cells[r] < 0 || (done >> r & 1)) continue;
        unsigned lanes = 0;
        for (size_t o = r; o < 4; ++o) lanes |= unsigned(cells[o] == cells[r]) << o;
        done |= lanes;
        for (u32 k = cell_runs_[size_t(cells[r])]; k < cell_runs_[size_t(cells[r]) + 1]; ++k) {
            const Run& run = runs_[k];
            if (!passes(run.segment, hint)) continue;
            for (u32 j = run.begin; j < run.end; ++j) {
                const TriTest& t = test_[tris_[j]];
                for (unsigned m = maybe_inside(t, qx, qz, lanes & ranged) | (lanes & ~ranged); m; m &= m - 1) {
                    const int l = std::countr_zero(m);
                    Ground& q = g[size_t(l)];
                    // u = nu x inv has the sign of nu xor inv's and is a signed zero for a zero nu, so it cannot pass where
                    // they differ or nu is zero: the test stops there (the same outcome); v likewise.
                    const F px = q.x - t.ax, pz = q.z - t.az;
                    const F nu = (px * t.cz) - (t.cx * pz);
                    if (!((nu.bits >> 23) & 0xFF) || ((nu.bits ^ t.inv.bits) & 0x80000000u)) continue;
                    const F u = nu * t.inv;
                    if (!(zero < u)) continue;
                    const F nv = (t.bx * pz) - (px * t.bz);
                    if (!((nv.bits >> 23) & 0xFF) || ((nv.bits ^ t.inv.bits) & 0x80000000u)) continue;
                    const F v = nv * t.inv;
                    if (!(zero < v) || !(u + v < one)) continue;
                    const F h = ((t.by * u) + (t.cy * v)) + t.ay;
                    if (!(q.height < h)) continue;
                    q.height = h;
                    q.seg = s32(run.segment);
                    best[size_t(l)] = &hit_[tris_[j]], bu[size_t(l)] = u, bv[size_t(l)] = v;
                }
            }
        }
    }
    for (size_t r = 0; r < 4; ++r) {
        const TriHit* h = best[r];
        if (!h) continue;
        Ground& q = g[r]; // the highest triangle's interpolated scalars, plane and bytes
        q.attr_a = ((h->a1 * bu[r]) + (h->a2 * bv[r])) + h->a0;
        q.attr_b = ((h->b1 * bu[r]) + (h->b2 * bv[r])) + h->b0;
        q.normal.z = h->dz;
        q.normal.x = h->dx;
        q.plane_d = h->plane_d;
        q.attr_a_raw = h->attr_a_raw;
        q.attr_b_raw = h->attr_b_raw;
        q.material = h->material;
        q.flags = h->flags;
    }
    // (-dh/dx, 1, -dh/dz) / length: the slopes times -1 / length, the y component 1 / length.
    for (Ground& q : g) {
        const F len2 = ((q.normal.x * q.normal.x) + one) + (q.normal.z * q.normal.z);
        const F r = ee::rsqrt(F::raw(0xBF800000), len2);
        q.x32 = 0;
        q.h = q.height;
        q.miss = u8(u32(q.seg) >> 31);
        q.normal.x = q.normal.x * r;
        q.normal.z = q.normal.z * r;
        q.plane_d = q.plane_d * r;
        q.normal.y = -r;
    }
}

} // namespace gt3r::sim::exact
