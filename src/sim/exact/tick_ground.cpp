// Phase 12's ground contact; see tick.h.

#include "base/ee_math.h"
#include "sim/exact/course_ground.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

int surface_index(const Ground& g) {
    if (!(g.flags & 2)) return g.material;
    switch (g.material) {
    case 0: case 7: return 12;
    case 1: return 13;
    case 9: return 14;
    default: return g.material;
    }
}

bool soft_material(const Ground& g, const SurfaceBumps& sb) { return sb.soft[g.material & 15] != 0; }

F bump_wave(int mode, F x, F z, F freq) {
    F w = ee::sin(freq * x);
    w = w + ee::cos(freq * z);
    if (mode != 1) return w;
    F sq = w * w;
    if (w < F::raw(0)) sq = -sq;
    return sq;
}

GroundCounts ground_contact(Car& c, const CourseGround& course, const SurfaceBumps& sb, bool mark_flagged) {
    for (size_t i = 0; i < 4; ++i) {
        const Wheel& w = c.w[i];
        Ground& g = c.g[i];
        const F y = c.pos.y + w.hub.y;
        g.x = c.pos.x + w.hub.x;
        g.h = c.pos.z + w.hub.z;
        g.z = -y;
    }
    course.query(c.g, c.behaviour);
    for (size_t i = 0; i < 4; ++i) {
        Wheel& w = c.w[i];
        const Ground& g = c.g[i];
        if (g.miss) {
            w.bump = F::raw(0);
            continue;
        }
        w.ground0 = g.height;
        w.ground = g.height;
        w.surface = u8(surface_index(g));
        w.x25 = g.x32;
        w.surface_flags = g.flags;
        // the tables have 16 rows (surfaces 0..14 on the disc)
        const size_t s = w.surface & 15;
        const F amp = sb.amp[s];
        if (amp == F::raw(0)) {
            w.bump = F::raw(0);
            continue;
        }
        F b = bump_wave(sb.mode[s], g.x, g.z, sb.freq[s]);
        if (sb.scaled[s]) b = b * w.speed_ramp;
        w.bump = b * amp;
    }
    const GroundCounts n = ground_counts(c, sb);
    c.x535 = u8(c.x535 & 0xEF);
    c.road_seg = c.g[0].seg;
    if (n.flagged && mark_flagged) c.x535 = u8(c.x535 | 0x10);
    return n;
}

GroundCounts ground_counts(const Car& c, const SurfaceBumps& sb) {
    GroundCounts n;
    for (const Ground& g : c.g) {
        if (g.miss) continue;
        n.flagged += g.flags & 1;
        if (soft_material(g, sb)) ++n.soft;
    }
    return n;
}

bool in_pit_stretch(const CourseOut& r, F p) {
    if (!r.stretch) return false;
    if (p < r.lap * F::raw(0x3F000000)) p = p + r.lap;
    return r.lo < p && p < r.hi;
}

void course_out(Car& c, const CourseOut& r, const GroundCounts& n) {
    if (!r.on || c.course_out != 0 || c.control != 0) return;
    s32 a = 0;
    if (n.flagged == 4 && in_pit_stretch(r, c.f3e4) && 0 < r.pit) a = r.pit;
    if (n.soft == 4 && a < r.soft) a = r.soft;
    if (a == 1) ++c.off_count;
    else if (a == 2) c.off_count = 0xFF;
    else c.off_count = 0;
    if (c.off_count != 0) c.course_out = 2;
}

std::array<TyreEffect, 4> tyre_effect_requests(const Car& c) {
    std::array<TyreEffect, 4> out{};
    for (size_t i = 0; i < 4; ++i) {
        const Wheel& w = c.w[i];
        const Ground& g = c.g[i];
        TyreEffect& e = out[i];
        e.wheel = s32(i);
        e.surface = w.surface;
        e.x = c.pos.x + w.hub.x;
        e.h = w.ground0;
        e.z = -(c.pos.y + w.hub.y);
        e.yaw = c.heading + w.steer;
        e.attr_a = g.attr_a;
        e.normal = g.normal;
        e.plane_d = g.plane_d - (g.normal.y * folded(c.behaviour, 0x3D4CCCCD, 0x3D4CCCCC)); // 0.05 (2002 Tokyo-Geneva's: Europe 0x1ED4B8)
        e.smoke_emit = w.smoke_emit;
        e.slip_ratio = w.slip_ratio;
        e.hub_vx = w.hub_vx;
        e.rim_speed = w.rim_speed;
        e.ground_flags = g.flags;
    }
    return out;
}

} // namespace gt3r::sim::exact
