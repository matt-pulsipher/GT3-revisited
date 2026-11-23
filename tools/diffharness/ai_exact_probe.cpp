// The original's AI course object as sim::exact::AiCourse; see ai_exact_probe.h.

#include "ai_exact_probe.h"
#include "program_map.h"

#include <cstdarg>
#include <cstdio>

namespace gt3r::diff::ai_exact {

namespace {

std::string fmt(const char* f, ...) {
    char s[200];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(s, sizeof s, f, ap);
    va_end(ap);
    return s;
}

} // namespace

ex::AiPoint read_point(Ee& ee, u32 a) {
    ex::AiPoint p;
    auto f = [&](u32 o) { return ee::F::raw(ee.r32(a + o)); };
    p.kind = ee.r8(a), p.brake_adjust = s8(ee.r8(a + 1)), p.speed_adjust = s8(ee.r8(a + 2)), p.x03 = ee.r8(a + 3);
    p.x = f(0x04), p.y = f(0x08), p.height = f(0x0C);
    p.seg = s16(ee.r16(a + 0x10)), p.surface = s8(ee.r8(a + 0x12)), p.x13 = ee.r8(a + 0x13);
    p.dist = f(0x14), p.radius = f(0x18), p.slope = f(0x1C), p.bank = f(0x20), p.heading = f(0x24);
    return p;
}

ex::AiCourse read_course(Ee& ee, u32 k) {
    ex::AiCourse c;
    c.behaviour = disc_behaviour();
    const u32 course = ee.r32(k), table = ee.r32(k + 4);
    c.lap_length = ee::F::raw(ee.r32(ee.r32(ee.r32(course) + 0x70) + 0x18));
    c.table = table != 0;
    c.pit_lines = ee.r32(k + 8) != 0;
    for (u32 line = 0; table && line < 8; ++line) {
        const u32 l = ee.r32(table + 8 + 4 * line);
        if (!l) continue;
        const u32 n = ee.r32(l);
        for (u32 i = 0; i < n && i < 1000; ++i) c.lines[line].push_back(read_point(ee, l + 4 + kPointSize * i));
    }
    for (u32 r = 0; r < 8; ++r)
        for (u32 j = 0; j < 6; ++j) c.fallback[r][j] = s8(ee.r8(kFallback + 6 * r + j));
    return c;
}

std::string course_difference(const ex::AiCourse& a, const ex::AiCourse& b) {
    if (a.lap_length.bits != b.lap_length.bits) return fmt("lap length 0x%08X vs 0x%08X", a.lap_length.bits, b.lap_length.bits);
    if (a.table != b.table || a.pit_lines != b.pit_lines) return "line table / pit-lines flag";
    if (a.fallback != b.fallback) return "fallback table";
    for (int l = 0; l < 8; ++l) {
        const auto &x = a.lines[size_t(l)], &y = b.lines[size_t(l)];
        if (x.size() != y.size()) return fmt("line %d: %d points vs %d", l, int(x.size()), int(y.size()));
        for (size_t i = 0; i < x.size(); ++i) {
            const ex::AiPoint &p = x[i], &q = y[i];
            const struct {
                const char* name;
                u32 u, v;
            } f[] = {{"kind", p.kind, q.kind},
                     {"brake_adjust", u8(p.brake_adjust), u8(q.brake_adjust)},
                     {"speed_adjust", u8(p.speed_adjust), u8(q.speed_adjust)},
                     {"x03", p.x03, q.x03},
                     {"x", p.x.bits, q.x.bits},
                     {"y", p.y.bits, q.y.bits},
                     {"height", p.height.bits, q.height.bits},
                     {"seg", u16(p.seg), u16(q.seg)},
                     {"surface", u8(p.surface), u8(q.surface)},
                     {"x13", p.x13, q.x13},
                     {"dist", p.dist.bits, q.dist.bits},
                     {"radius", p.radius.bits, q.radius.bits},
                     {"slope", p.slope.bits, q.slope.bits},
                     {"bank", p.bank.bits, q.bank.bits},
                     {"heading", p.heading.bits, q.heading.bits}};
            for (const auto& e : f)
                if (e.u != e.v) return fmt("line %d point %d %s 0x%08X vs 0x%08X", l, int(i), e.name, e.u, e.v);
        }
    }
    return {};
}

} // namespace gt3r::diff::ai_exact
