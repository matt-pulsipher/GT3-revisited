// The exact core's ground query (sim::exact::CourseGround, the program's 0x18FB58) against a plain copy of the program's
// walk: every triangle of the point's cell computed in full, in the program's operations. Synthetic roads with slivers,
// overlapping and degenerate triangles, query points on and next to edges and vertices: every record field bit for bit (the
// query leaves out work whose outcome is certain).

#include "sim/exact/course_ground.h"
#include "test_framework.h"

#include <random>

using namespace gt3r;
namespace ex = gt3r::sim::exact;
using ex::F;

namespace {

// The program's walk as the port first had it: per record, every reference of its cell that passes the segment filter.
void reference_query(const course::Road& road, std::array<ex::Ground, 4>& g) {
    const ex::RoadCells cells(road.grid_index);
    const int hint = g[0].seg;
    const F zero = F::raw(0), one = F::raw(0x3F800000), k65535 = F::raw(0x477FFF00);
    auto passes = [&](int seg) {
        if (seg == hint || hint < 0) return true;
        if (size_t(hint) >= road.segments.size()) return false;
        for (u16 n : road.segments[size_t(hint)].neighbours)
            if (n == seg) return true;
        return false;
    };
    std::array<int, 4> cell;
    for (size_t r = 0; r < 4; ++r) {
        g[r].height = F::raw(0xFF7FFFFF);
        g[r].seg = -1;
        cell[r] = cells.cell(g[r].x, g[r].z);
    }
    for (size_t r = 0; r < 4; ++r) {
        if (cell[r] < 0) continue;
        ex::Ground& q = g[r];
        for (u32 k = road.grid_index.cell_begin[size_t(cell[r])]; k < road.grid_index.cell_begin[size_t(cell[r]) + 1]; ++k) {
            const int seg = road.grid_index.refs[k].segment;
            if (!passes(seg)) continue;
            const course::RoadSegment& s = road.segments[size_t(seg)];
            const course::RoadTriangle& t = s.triangles[road.grid_index.refs[k].triangle];
            const course::Vec3 &va = s.vertices[t.a], &vb = s.vertices[t.b], &vc = s.vertices[t.c];
            const F ax = F(va.x), ay = F(va.y), az = F(va.z), bx_ = F(vb.x), by_ = F(vb.y), bz_ = F(vb.z);
            const F cx_ = F(vc.x), cy_ = F(vc.y), cz_ = F(vc.z);
            const F bx = bx_ - ax, bz = bz_ - az, cz = cz_ - az, cx = cx_ - ax;
            const F inv = one / ((bx * cz) - (cx * bz));
            const F px = q.x - ax, pz = q.z - az;
            const F u = ((px * cz) - (cx * pz)) * inv;
            const F v = ((bx * pz) - (px * bz)) * inv;
            if (!(zero < u) || !(zero < v) || !(u + v < one)) continue;
            const F h = (((by_ - ay) * u) + ((cy_ - ay) * v)) + ay;
            if (!(q.height < h)) continue;
            q.height = h;
            q.seg = seg;
            auto attr = [&](size_t i) { return i < s.vertex_attrs.size() ? s.vertex_attrs[i] : 0u; };
            auto lerp = [&](u16 sa, u16 sb, u16 sc) {
                const F fa = ee::int_to_f(sa) / k65535, fb = ee::int_to_f(sb) / k65535, fc = ee::int_to_f(sc) / k65535;
                return (((fb - fa) * u) + ((fc - fa) * v)) + fa;
            };
            const u32 wa = attr(t.a), wb = attr(t.b), wc = attr(t.c);
            q.attr_a = lerp(u16(wa), u16(wb), u16(wc));
            q.attr_b = lerp(u16(wa >> 16), u16(wb >> 16), u16(wc >> 16));
            const F by = by_ - ay, cy = cy_ - ay;
            const F dz = ((bx * cy) - (cx * by)) * inv, dx = ((cz * by) - (bz * cy)) * inv;
            q.normal.z = dz;
            q.normal.x = dx;
            q.plane_d = -(((dx * ax) + (dz * az)) - ay);
            q.attr_a_raw = u16(wa);
            q.attr_b_raw = u16(wa >> 16);
            q.material = u8(t.edge_flags);
            q.flags = u8(t.surface);
        }
    }
    for (ex::Ground& q : g) {
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

bool same(const ex::Ground& a, const ex::Ground& b) {
    return a.x.bits == b.x.bits && a.h.bits == b.h.bits && a.z.bits == b.z.bits && a.seg == b.seg && a.height.bits == b.height.bits &&
           a.attr_a.bits == b.attr_a.bits && a.attr_b.bits == b.attr_b.bits && a.normal.x.bits == b.normal.x.bits &&
           a.normal.y.bits == b.normal.y.bits && a.normal.z.bits == b.normal.z.bits && a.plane_d.bits == b.plane_d.bits &&
           a.attr_a_raw == b.attr_a_raw && a.attr_b_raw == b.attr_b_raw && a.miss == b.miss && a.material == b.material &&
           a.x32 == b.x32 && a.flags == b.flags;
}

// A road of `nseg` segments over [-50, 50] m (offset by `base`), random triangles (slivers, near-degenerate and overlapping
// ones among them), a 3 x 3 grid whose every cell lists every triangle, each segment's neighbours the next two.
course::Road random_road(std::mt19937& rng, int nseg, float base) {
    std::uniform_real_distribution<float> pos(-50, 50), h(-5, 5);
    course::Road road;
    for (int s = 0; s < nseg; ++s) {
        course::RoadSegment seg;
        for (int t = 0; t < 12; ++t) {
            const u16 i = u16(seg.vertices.size());
            course::Vec3 a{base + pos(rng), h(rng), base + pos(rng)}, b{base + pos(rng), h(rng), base + pos(rng)}, c;
            switch (rng() % 4) {
            case 0: c = {a.x + (b.x - a.x) * 0.5f + 1e-3f, h(rng), a.z + (b.z - a.z) * 0.5f}; break; // a sliver
            case 1: c = {a.x + (b.x - a.x) * 0.25f, h(rng), a.z + (b.z - a.z) * 0.25f}; break;       // (nearly) collinear
            default: c = {base + pos(rng), h(rng), base + pos(rng)}; break;
            }
            for (const course::Vec3& v : {a, b, c}) seg.vertices.push_back(v), seg.vertex_attrs.push_back(rng());
            course::RoadTriangle tri{};
            tri.a = i, tri.b = u16(i + 1), tri.c = u16(i + 2);
            tri.edge_flags = rng() & 0xFF, tri.surface = rng() & 0xFF;
            seg.triangles.push_back(tri);
        }
        seg.neighbours = {u16((s + 1) % nseg), u16((s + 2) % nseg)};
        road.segments.push_back(std::move(seg));
    }
    course::RoadGrid& g = road.grid_index;
    g.xs = {base - 60, base - 10, base + 10, base + 60};
    g.zs = {base - 60, base, base + 20, base + 60};
    g.cell_begin.push_back(0);
    for (int cell = 0; cell < 9; ++cell) {
        for (int s = 0; s < nseg; ++s)
            for (u16 t = 0; t < 12; ++t) g.refs.push_back({u16(s), t});
        g.cell_begin.push_back(u32(g.refs.size()));
    }
    return road;
}

} // namespace

TEST(exact_ground_query_matches_full_walk) {
    std::mt19937 rng(17);
    int bad = 0, hits = 0, queries = 0;
    for (int round = 0; round < 6 && bad < 5; ++round) {
        const course::Road road = random_road(rng, 5, round < 3 ? 0.0f : 700.0f); // also far from the origin (larger ulps)
        const ex::CourseGround ground(road);
        std::uniform_real_distribution<float> unit(0, 1);
        for (int i = 0; i < 20000 && bad < 5; ++i) {
            std::array<ex::Ground, 4> a{}, b{};
            const int hint = int(rng() % 7) - 1; // none, a segment, or past the segments
            for (size_t r = 0; r < 4; ++r) {
                const course::RoadSegment& s = road.segments[rng() % road.segments.size()];
                const course::RoadTriangle& t = s.triangles[rng() % s.triangles.size()];
                const course::Vec3 &v0 = s.vertices[t.a], &v1 = s.vertices[t.b], &v2 = s.vertices[t.c];
                float x, z;
                switch (rng() % 4) {
                case 0: { // on an edge, then a few ulps either way
                    const float k = unit(rng);
                    const course::Vec3& p = rng() % 2 ? v1 : v2;
                    x = v0.x + (p.x - v0.x) * k, z = v0.z + (p.z - v0.z) * k;
                    break;
                }
                case 1: { // on the third edge
                    const float k = unit(rng);
                    x = v1.x + (v2.x - v1.x) * k, z = v1.z + (v2.z - v1.z) * k;
                    break;
                }
                case 2: x = v0.x, z = v0.z; break; // a vertex
                default: x = v0.x + (unit(rng) - 0.5f) * 30, z = v0.z + (unit(rng) - 0.5f) * 30; break;
                }
                F fx = F(x), fz = F(z);
                fx.bits = u32(s32(fx.bits) + s32(rng() % 9) - 4), fz.bits = u32(s32(fz.bits) + s32(rng() % 9) - 4);
                for (ex::Ground* g : {&a[r], &b[r]}) {
                    g->x = fx, g->z = fz;
                    g->seg = hint;
                    g->normal = {F(0.1f * float(r)), F(1), F(-0.2f)}; // the last plane (kept on a miss)
                    g->plane_d = F(0.5f);
                }
            }
            ground.query(a);
            reference_query(road, b);
            ++queries;
            for (size_t r = 0; r < 4; ++r) {
                hits += a[r].seg >= 0;
                if (!same(a[r], b[r])) ++bad;
            }
        }
    }
    CHECK(bad == 0);
    CHECK(hits > queries); // (points on the road among them)
}
