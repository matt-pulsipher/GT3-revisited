// The course's single-point queries; see course_query.h. The ground query with its triangle tests
// (the inside test, the height), the scalar interpolation, the cell lookup (RoadCells,
// course_ground.h) and the neighbour test; the race progress query with the checkpoint test.

#include "sim/exact/course_query.h"

namespace gt3r::sim::exact {

namespace {

const F k0 = F::raw(0), k1 = F::raw(0x3F800000), kLowest = F::raw(0xFF7FFFFF), k65535 = F::raw(0x477FFF00);

} // namespace

CourseQuery::CourseQuery(const course::Road& road)
    : length_(F(road.length)), wraps_(u8(road.header_flag)), cells_(road.grid_index) {
    for (const course::GridSlot& s : road.grid) grid_.push_back({F(s.pos.x), F(s.pos.y), F(s.pos.z), F(s.yaw)});
    grid_words_ = road.grid_words, grid_pointers_ = road.grid_pointers, base_ = road.base;
    const course::RoadGrid& g = road.grid_index;
    if (!g.empty()) {
        cell_group_begin_ = g.cell_group_begin;
        groups_ = g.groups;
        refs_ = g.refs;
    }
    for (const course::RoadSegment& s : road.segments) {
        Segment o;
        for (size_t k = 0; k < s.vertices.size(); ++k) {
            const u32 attrs = k < s.vertex_attrs.size() ? s.vertex_attrs[k] : 0;
            o.v.push_back({F(s.vertices[k].x), F(s.vertices[k].y), F(s.vertices[k].z), u16(attrs), u16(attrs >> 16)});
        }
        for (const course::RoadTriangle& t : s.triangles) o.t.push_back({t.a, t.b, t.c, t.material, t.flags});
        o.neighbours = s.neighbours;
        o.checkpoints = s.checkpoint_ids;
        segs_.push_back(std::move(o));
    }
    for (const course::Checkpoint& c : road.checkpoints)
        cps_.push_back({F(c.x0), F(c.z0), F(c.x1), F(c.z1), F(c.distance), F(c.dir_x), F(c.dir_z), F(c.unknown)});
    for (const course::RoadMarker& m : road.markers) markers_.push_back({u16(m.flags), u16(m.flags >> 16), F(m.distance)});
}

void CourseQuery::ground(Ground& g, u16* material) const {
    // its own record: the scalars start at 1, the bytes at 0.
    g.attr_a = k1, g.attr_b = k1;
    g.material = 0, g.flags = 0, g.x32 = 0;
    g.height = g.h;
    const F x = g.x, z = g.z;
    const int c = cells_.cell(x, z);
    if (c < 0) {
        g.miss = 1;
        return;
    }
    const s32 hint = g.seg;
    g.seg = -1;
    const Segment* best_seg = nullptr;
    const Tri* best = nullptr;
    s32 best_id = -1;
    F best_h = kLowest;
    for (u32 k = cell_group_begin_[size_t(c)]; k < cell_group_begin_[size_t(c) + 1]; ++k) {
        const course::RoadGridGroup& grp = groups_[k];
        const int seg = grp.segment;
        if (seg != hint && hint >= 0) { // on the hint's neighbour list (a hint past the road's segments: none)
            bool found = false;
            if (size_t(hint) < segs_.size())
                for (u16 n : segs_[size_t(hint)].neighbours) found = found || n == seg;
            if (!found) continue;
        }
        const Segment& s = segs_[size_t(seg)];
        const Tri* in_group = nullptr;
        F h_group = kLowest;
        for (u32 r = grp.begin; r < grp.end; ++r) {
            const Tri& t = s.t[refs_[r].triangle];
            const Vertex &a = s.v[t.a], &b = s.v[t.b], &cv = s.v[t.c];
            // The point inside or on the edges (the three cross products all <= 0 or all >= 0).
            const F az = a.z - z, ax = a.x - x, bz = b.z - z, bx = b.x - x;
            const F e0 = (ax * bz) - (bx * az);
            const F cx = cv.x - x, cz = cv.z - z;
            const F e2 = (cx * az) - (ax * cz);
            const F e1 = (bx * cz) - (cx * bz);
            const bool inside = (e0 <= k0 && e1 <= k0 && e2 <= k0) || (k0 <= e0 && k0 <= e1 && k0 <= e2);
            if (!inside) continue;
            // The plane's height at (x, z), clamped to the corners' heights.
            F lo = ee::min(a.y, b.y), hi = ee::max(a.y, b.y);
            lo = ee::min(cv.y, lo), hi = ee::max(cv.y, hi);
            const F ux = b.x - a.x, uz = b.z - a.z, vx = cv.x - a.x, vz = cv.z - a.z;
            const F det = (ux * vz) - (vx * uz);
            const F px = a.x - x, pz = a.z - z;
            const F uy = b.y - a.y, vy = cv.y - a.y;
            F h;
            if (det == k0) h = lo;
            else {
                const F s1 = (vx * pz) - (px * vz);
                const F s2 = (px * uz) - (ux * pz);
                h = (((a.y * det) + (uy * s1)) + (vy * s2)) / det;
            }
            h = ee::min(ee::max(h, lo), hi);
            if (h_group < h) h_group = h, in_group = &t;
        }
        if (in_group && !(h_group < best_h)) best_h = h_group, best = in_group, best_seg = &s, best_id = seg;
    }
    if (!best) {
        g.miss = 1;
        return;
    }
    g.miss = 0;
    g.height = best_h;
    g.seg = best_id;
    g.material = u8(best->material), g.flags = u8(best->flags);
    if (material) *material = best->material;
    const Vertex &a = best_seg->v[best->a], &b = best_seg->v[best->b], &cv = best_seg->v[best->c];
    {
        // The point's barycentric weights times the corners' scalars, clamped to the corners' range.
        const F p_x = x, p_z = z;
        F f8 = a.x * b.z;
        const F f17 = b.x - a.x;
        F f1 = b.x * a.z;
        F f4 = b.x * cv.z;
        const F f15 = cv.x * a.z;
        f8 = f8 - f1;
        const F f16 = a.x * cv.z;
        F f10 = cv.z - a.z;
        const F f13 = cv.x * b.z;
        f1 = f8 + f4;
        F f0 = b.z - cv.z;
        const F f9 = a.z - b.z;
        f10 = p_x * f10;
        F f3 = a.x - cv.x;
        F f7 = f15 - f16;
        f0 = p_x * f0;
        f4 = f4 - f13;
        f1 = f1 - f13;
        F f2 = cv.x - b.x;
        f3 = p_z * f3;
        const F f6x = p_x * f9;
        f7 = f7 + f10;
        f2 = p_z * f2;
        f4 = f4 + f0;
        f1 = f1 + f15;
        f8 = f8 + f6x;
        f7 = f7 + f3;
        f1 = f1 - f16;
        f4 = f4 + f2;
        const F f11 = p_z * f17;
        const F sa = ee::int_to_f(a.a), sb = ee::int_to_f(b.a);
        f4 = f4 / f1; // a's weight
        f8 = f8 + f11;
        f7 = f7 / f1; // b's weight
        const F qa = sa / k65535, qb = sb / k65535;
        const F sc = ee::int_to_f(cv.a);
        f8 = f8 / f1; // c's weight
        const F qc = sc / k65535;
        F v = ((f4 * qa) + (f7 * qb)) + (f8 * qc);
        const F lo = ee::min(ee::min(qa, qb), qc), hi = ee::max(ee::max(qa, qb), qc);
        g.attr_a = ee::min(ee::max(v, lo), hi);
        const F rb = ee::int_to_f(b.b) / k65535, ra = ee::int_to_f(a.b) / k65535, rc = ee::int_to_f(cv.b) / k65535;
        const F wb = f7 * rb, wa = f4 * ra, wc = f8 * rc;
        F w = (wa + wb) + wc;
        const F lo2 = ee::min(ee::min(ra, rb), rc), hi2 = ee::max(ee::max(ra, rb), rc);
        g.attr_a_raw = a.a, g.attr_b_raw = a.b;
        g.attr_b = ee::min(ee::max(w, lo2), hi2);
    }
    // The triangle's unit normal (b - a) x (c - a) in the course frame and its plane.
    const F ez = b.z - a.z, fz = cv.z - a.z, ex = b.x - a.x, fx = cv.x - a.x, ey = b.y - a.y, fy = cv.y - a.y;
    const F p9 = ex * fz, p2 = ez * fx, p7 = ez * fy, p4 = ey * fz, p3 = ex * fy, p1 = ey * fx;
    const F nx = p4 - p7, ny = p2 - p9, nz = p3 - p1;
    const F r = ee::rsqrt(k1, ((nx * nx) + (ny * ny)) + (nz * nz));
    g.normal.x = nx * r, g.normal.y = ny * r, g.normal.z = nz * r;
    g.plane_d = -(((g.normal.x * a.x) + (g.normal.y * a.y)) + (g.normal.z * a.z));
}

// The point between one of segment seg's checkpoints and the next (on the front side of the first, the back
// side of the next and between their end lines): the distance there, interpolated by the planes.
bool CourseQuery::in_checkpoints(F x, F z, int seg, F& out) const {
    const Segment& s = segs_[size_t(seg)];
    for (u16 id : s.checkpoints) {
        if (id >= cps_.size()) continue; // not a checkpoint of this course (another game's data)
        const Checkpoint& a = cps_[id];
        const F pa = ((a.dx * x) + (a.dz * z)) + a.d;
        if (k0 < pa) continue;
        u32 next = u32(id) + 1;
        if (next == cps_.size()) {
            if (!wraps_) continue;
            next = 0;
        }
        const Checkpoint& b = cps_[next];
        const F pb = ((b.dx * x) + (b.dz * z)) + b.d;
        if (pb < k0) continue;
        if (((b.x0 - a.x0) * (z - a.z0)) - ((b.z0 - a.z0) * (x - a.x0)) < k0) continue;
        if (k0 < ((b.x1 - a.x1) * (z - a.z1)) - ((b.z1 - a.z1) * (x - a.x1))) continue;
        F db = b.dist;
        if (db < a.dist) db = db + length_;
        F d = a.dist + ((db - a.dist) * pa) / (pa - pb);
        if (length_ <= d) d = d - length_;
        out = d;
        return true;
    }
    return false;
}

F CourseQuery::progress(F x, F h, F z, s32 seg) const {
    (void)h;
    F out = k0;
    if (u32(seg) < segs_.size()) {
        if (in_checkpoints(x, z, seg, out)) return out;
        for (u16 n : segs_[size_t(seg)].neighbours)
            if (in_checkpoints(x, z, n, out)) return out;
        return out;
    }
    for (size_t k = 0; k < segs_.size(); ++k)
        if (in_checkpoints(x, z, int(k), out)) return out;
    return out;
}

CourseQuery::Slot CourseQuery::slot(int i, u32 road) const {
    if (i < 0 || i >= 8) return {}; // (start_car refuses such a slot)
    auto word = [&](int k) {
        const u32 w = grid_words_[size_t(4 * i + k)];
        return F::raw((grid_pointers_ >> (4 * i + k)) & 1 ? w + (road - base_) : w);
    };
    return {word(0), word(1), word(2), word(3)};
}

int CourseQuery::sectors() const {
    int n = 0;
    for (const Marker& m : markers_)
        if (m.group == 0 && m.type == 2) ++n;
    return n + 1 < 5 ? n + 1 : 4;
}

F CourseQuery::sector_start(int i) const {
    if (sectors() - 2 < i) return F::raw(0xBF800000);
    int k = 0;
    for (const Marker& m : markers_)
        if (m.group == 0 && m.type == 2 && k++ == i) return m.dist;
    return k0;
}

int CourseQuery::logger_marks() const {
    int n = 0;
    for (const Marker& m : markers_)
        if (m.group == 1 && m.type == 2) ++n;
    return n;
}

F CourseQuery::logger_mark(int k) const {
    int n = 0;
    for (const Marker& m : markers_)
        if (m.group == 1 && m.type == 2 && n++ == k) return m.dist;
    return k0;
}

} // namespace gt3r::sim::exact
