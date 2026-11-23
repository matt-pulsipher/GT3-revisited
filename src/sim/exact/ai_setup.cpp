// The AI course object's setup; see ai_setup.h. 0x1F9B98 and the line loader 0x1F92F8 with its ground probes 0x1F8BA0,
// 0x1F8EC8, the segment tracer 0x1F8BF8, the stop line's first segment 0x1F91E8 and the road frame under a point 0x1F8F40.

#include "sim/exact/ai_setup.h"

#include "sim/exact/build_math.h"

#include "base/ee_math.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

const F k0 = F::raw(0), kHalf = F::raw(0x3F000000), k1 = F::raw(0x3F800000), k10 = F::raw(0x41200000), k100 = F::raw(0x42C80000);
const F kFixed = F::raw(0x39800000);                            // 1 / 4096
const F kTwoPi = F::raw(0x40C90FDB), kPi = F::raw(0x40490FDB); // 0x35068C / 0x350690, 0x35067C / 0x350684
const F kTenDeg = F::raw(0x3E32B8C2);                           // 0x350680 / 0x350688: 10 degrees in radians

// 0x1F8BA0: the road segment under (x, y) from the hint, -1 off the road.
s32 segment_at(const CourseQuery& q, s32 hint, F x, F y) {
    Ground g;
    g.x = x, g.h = k100, g.z = -y, g.seg = hint;
    q.ground(g);
    return g.miss ? -1 : g.seg;
}

F height_at(const CourseQuery& q, s32& seg, bool& miss, F x, F y, F h) { return road_height(q, seg, miss, x, y, h); }

// 0x1F8BF8: point i's segment, traced from point i - 1's segment along the segment between them (straights every 10 m, arcs
// every 10 degrees); an arc's kind byte becomes 1 (left, radius > 0) or 2 (right), 1 on the side map (line 5).
s32 trace(const Behaviour& bh, const CourseQuery& q, std::vector<AiPoint>& p, int i, s32 seg, int line) {
    AiPoint& a = p[size_t(i - 1)];
    const AiPoint& b = p[size_t(i)];
    const F ax = a.x, ay = a.y, by = b.y;
    if (a.kind == 0) {
        const F dx = b.x - ax, dy = by - ay;
        s32 n = ee::to_int(ee::sqrt((dx * dx) + (dy * dy)) / k10);
        if (n == 0) n = 1;
        for (s32 k = 1; k <= n; ++k) {
            const F t = ee::int_to_f(k) / ee::int_to_f(n);
            const F y = ay + t * dy, x = ax + t * dx;
            seg = segment_at(q, seg, x, y);
        }
        return seg;
    }
    const F heading = a.heading;
    const F s = -ee::sin(heading);
    const F c = ee::cos(heading);
    F r = a.radius;
    const F cx = ax - c * r, cy = ay + s * r;
    F a0 = atan2(bh, cx - ax, ay - cy);
    F a1 = atan2(bh, cx - b.x, by - cy);
    const F two_pi = folded(bh, kTwoPi.bits, 0x40C90FDA), ten_deg = folded(bh, kTenDeg.bits, 0x3E32B8C1); // (Europe 0x1F85A8)
    F steps;
    if (k0 < r) {
        a.kind = 1;
        if (a1 < a0) a1 = a1 + two_pi;
        steps = (a1 - a0) / ten_deg;
    } else {
        a.kind = 2;
        r = -r;
        if (a0 < a1) a0 = a0 + two_pi;
        steps = (a0 - a1) / ten_deg;
    }
    s32 n = ee::to_int(steps);
    if (n == 0) n = 1;
    if (line == 5) a.kind = 1;
    const F sweep = a1 - a0, fn = ee::int_to_f(n);
    for (s32 k = 1; k <= n; ++k) {
        const F angle = a0 + (ee::int_to_f(k) / fn) * sweep;
        const F x = cx - ee::sin(angle) * r;
        const F y = cy + ee::cos(angle) * r;
        seg = segment_at(q, seg, x, y);
    }
    return seg;
}

// 0x1F91E8: the stop line's first point's segment: probed with the hints of the main line's (6) points from its last one
// back until one finds the road; false when there is no main line or none does.
bool stop_line_segment(const CourseQuery& q, const AiCourse& k, s32& seg, F x, F y) {
    const int line = k.resolve(6);
    if (line < 0) return false;
    const std::vector<AiPoint>& p = k.lines[size_t(line)];
    for (int i = int(p.size()) - 1; i >= 0; --i) {
        s32 hint = p[size_t(i)].seg;
        bool miss = false;
        height_at(q, hint, miss, x, y, k100);
        if (!miss) {
            seg = hint;
            return true;
        }
    }
    return false;
}

// 0x1F8F40: the road under a point: its height, surface index, pit-lane flag and the slope along / bank across the heading
// from four probes half a metre away (one side alone when its opposite misses); false off the road.
struct RoadFrame {
    F height, slope, bank;
    s32 surface = 0, x32 = 0, flags = 0;
};
bool road_frame(const Behaviour& bh, const CourseQuery& q, F x, F y, F heading, s32 seg, RoadFrame& out) {
    Ground g;
    g.x = x, g.h = k100, g.z = -y, g.seg = seg;
    q.ground(g);
    s32 hint = g.seg;
    out.bank = k0, out.slope = k0;
    if (g.miss) {
        out.flags = 0, out.surface = 0, out.height = k0, out.x32 = 0;
        return false;
    }
    const F centre = g.height;
    out.height = centre;
    out.flags = g.flags & 1;
    out.surface = surface_index(g);
    out.x32 = g.x32;
    const F s = ee::sin(heading) * kHalf;
    const F c = ee::cos(heading) * kHalf;
    bool m1 = false, m2 = false, m3 = false, m4 = false;
    const F h1 = height_at(q, hint, m1, x - s, y + c, k100);
    const F h2 = height_at(q, hint, m2, x + s, y - c, k100);
    const F h3 = height_at(q, hint, m3, x - c, y - s, k100);
    const F h4 = height_at(q, hint, m4, x + c, y + s, k100);
    if ((m1 && m2) || (m3 && m4)) return false;
    if (m1) out.slope = atan2(bh, centre - h2, kHalf);
    else if (!m2) out.slope = atan2(bh, h1 - h2, k1);
    else out.slope = atan2(bh, h1 - centre, kHalf);
    if (m3) out.bank = atan2(bh, centre - h4, kHalf);
    else if (!m4) out.bank = atan2(bh, h3 - h4, k1);
    else out.bank = atan2(bh, h3 - centre, kHalf);
    return true;
}

} // namespace

F road_height(const CourseQuery& q, s32& seg, bool& miss, F x, F y, F h) {
    Ground g;
    g.x = x, g.h = h, g.z = -y, g.seg = seg;
    q.ground(g);
    miss = g.miss != 0;
    if (miss) return k0;
    seg = g.seg;
    return g.height;
}

AiCourse ai_course_setup(const CourseQuery& q, const course::AiLines* lines, const ProgramData& d) {
    AiCourse k;
    k.behaviour = d.behaviour();
    k.lap_length = q.lap_length();
    for (u32 l = 0; l < 8; ++l)
        for (u32 j = 0; j < 6; ++j) k.fallback[l][j] = s8(d.b(ProgramData::Table::LineFallback, 6 * l + j));
    std::array<u8, 8> order;
    for (u32 j = 0; j < 8; ++j) order[j] = d.b(ProgramData::Table::LineOrder, j);
    k.table = lines != nullptr;
    if (!lines) return k;
    // The file's points into the lines (game line g = file line g - 1; a line of no points is a null slot), converted from
    // the file's fixed point as the first setup of the section does.
    for (size_t g = 1; g < 8; ++g) {
        if (g - 1 >= lines->lines.size()) continue;
        for (const course::AiPoint& f : lines->lines[g - 1].points) {
            AiPoint p;
            p.kind = u8(f.flags), p.brake_adjust = s8(f.flags >> 8), p.speed_adjust = s8(f.flags >> 16), p.x03 = u8(f.flags >> 24);
            p.heading = ((ee::int_to_f(s32(f.packed)) * folded(k.behaviour, kTwoPi.bits, 0x40C90FDA)) * kFixed) +
                        folded(k.behaviour, kPi.bits, 0x40490FDA); // (2002 Tokyo-Geneva's: Europe 0x1F8D18)
            p.x = (-ee::int_to_f(f.raw_x)) * kFixed;
            p.radius = ee::int_to_f(f.raw_radius) * kFixed;
            p.y = (-ee::int_to_f(f.raw_z)) * kFixed;
            p.seg = s16(f.segment);
            k.lines[g].push_back(p);
        }
    }
    // Segments from the file when any point (lines in the loader's order) has one; otherwise traced.
    bool file_segments = false;
    for (u8 g : order) {
        for (const AiPoint& p : k.lines[g & 7])
            if (p.seg != 0) {
                file_segments = true;
                break;
            }
        if (file_segments) break;
    }
    for (u8 g8 : order) {
        const int g = g8 & 7;
        std::vector<AiPoint>& p = k.lines[size_t(g)];
        bool past_half = false;
        s32 seg = -1;
        for (size_t i = 0; i < p.size(); ++i) {
            AiPoint& a = p[i];
            const F x = a.x, y = a.y;
            if (file_segments) seg = a.seg;
            else if (g == 4) {
                if (i == 0) {
                    s32 s = seg;
                    if (stop_line_segment(q, k, s, x, y)) seg = s;
                } else seg = segment_at(q, seg, x, y);
            } else if (i == 0) seg = segment_at(q, seg, x, y);
            else seg = trace(k.behaviour, q, p, int(i), seg, g);
            a.seg = s16(seg);
            a.dist = q.progress(x, k100, -y, seg);
            a.height = k0, a.slope = k0, a.bank = k0, a.surface = 0, a.x13 = 0;
            if (g != 5) {
                RoadFrame r;
                if (road_frame(k.behaviour, q, x, y, a.heading, seg, r)) {
                    a.height = r.height, a.slope = r.slope, a.bank = r.bank;
                    a.surface = s8(r.surface), a.x13 = u8(r.x32);
                }
            }
            if (g == 4) {
                if (a.dist < k.lap_length * kHalf) past_half = true;
                if (past_half) a.dist = a.dist + k.lap_length;
            }
        }
    }
    k.pit_lines = !k.lines[3].empty() && !k.lines[4].empty();
    return k;
}

} // namespace gt3r::sim::exact
