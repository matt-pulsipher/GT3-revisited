// The collision step in EE arithmetic (src/sim/collision_exact.h, src/sim/course_collision.h) against the original's own
// functions, bit for bit:
//   model_collide_exact_build   the body build 0x1FAE38 (+ the chamfer of 0x1FA740) and the pair tables 0x1FB0D8
//   model_collide_exact_step    random scenes on a synthetic course: the all-bodies step 0x1FA8F8 and the walls-only step
//                               0x1FACB8, several steps in a row (so contact flags carry over), every record field compared
//   model_collide_course        the disc's courses: the wall grid walk 0x228848, the segment links 0x228B68 and the road
//                               segment query 0x2280C8 against sim::CourseCollision built from our parse of the same files
// race_tick_exact (diff_tick_exact.cpp) runs the step in real races.
//
// The synthetic course lives in interpreter memory in the program's layout (road block: +0x34 segment table, +0x3C wall
// points, +0x4C wall grid; the course pointer -> container -> +0x70 road block) and, the same, as a course::Road for ours: two
// closed wall loops (the track boundary, counter-clockwise, and an island, clockwise), corner flags on about half the points
// and a few sensor edges (none on the US disc, but the code has them), one grid cell, two linked segments. Scenes: one to three
// bodies near the walls and each other, at speed or crawling with steering (the stuck nudge; a quarter of them computer
// cars, which the later builds' nudge leaves alone), some ghosts, some spun past 8
// turns or 20 pi rad/s; the bodies keep the step's results between steps and are pushed on towards the walls.

#include "collide_exact_probe.h"
#include "harness.h"
#include "race_setup.h"

#include "asm.h"
#include "disc/disc.h"
#include "disc/volume.h"
#include "formats/course/course_file.h"
#include "formats/course/road.h"
#include "sim/course_collision.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>

using namespace gt3r;
using namespace gt3r::diff;

namespace {

namespace ce = collide_exact;
namespace ex = sim::collide::exact;
using ex::F;

constexpr Pa kBuild = 0x001FAE38, kPairTables = 0x001FB0D8, kStepAll = 0x001FA8F8, kStepWalls = 0x001FACB8;
constexpr u32 kCarBase = 0xC8;
constexpr double kPi = 3.14159265358979323846;

std::string fmt(const char* f, double a = 0, double b = 0, double c = 0) {
    char s[256];
    std::snprintf(s, sizeof s, f, a, b, c);
    return s;
}

F fh(double v) { return F(float(v)); }

u32 zalloc(Ee& ee, u32 n) {
    u32 a = ee.alloc(n);
    for (u32 i = 0; i < n; i += 4) ee.w32(a + i, 0);
    return a;
}

// The sound / pad vibration calls of the responses and the effect pass: no-ops (they reach game state the tests do not build).
void patch_effects(Ee& ee) {
    ee.protect_text = false;
    for (u32 a : {pa(0x001DFEB8), pa(0x001DFD98)}) {
        ee.w32(a, enc::jr_ra);
        ee.w32(a + 4, enc::nop);
    }
    ee.protect_text = true;
}

struct Scene {
    course::Road road;
    std::unique_ptr<sim::CourseCollision> course;
    u32 course_ptr = 0, walls = 0;
    std::vector<std::array<double, 2>> loop0; // the boundary's points (plane X, Y)
};

// Two wall loops and their grid, in memory and as a Road.
void make_course(Ee& ee, Rng& rng, Scene& sc) {
    struct Pt {
        double x, y;
        u16 flags;
        int seg;
    };
    std::vector<std::vector<Pt>> loops(2);
    const int n0 = int(rng.range(7, 14)), n1 = int(rng.range(4, 7));
    const double r0 = rng.uniform(9, 16), r1 = rng.uniform(1.5, 3.0), ox = rng.uniform(-4, 4), oy = rng.uniform(-4, 4);
    for (int k = 0; k < n0; ++k) { // counter-clockwise: the track inside
        double a = 2 * kPi * (k + rng.uniform(-0.3f, 0.3f)) / n0, r = r0 * rng.uniform(0.8f, 1.2f);
        loops[0].push_back({r * std::cos(a), r * std::sin(a), 0, k < n0 / 2 ? 0 : 1});
    }
    for (int k = 0; k < n1; ++k) { // clockwise: the island's outside is the track
        double a = -2 * kPi * (k + rng.uniform(-0.2f, 0.2f)) / n1, r = r1 * rng.uniform(0.8f, 1.2f);
        loops[1].push_back({ox + r * std::cos(a), oy + r * std::sin(a), 0, 1});
    }
    for (auto& l : loops)
        for (Pt& p : l) p.flags = u16((rng.chance(0.5) ? 1 : 0) | (rng.chance(0.08) ? 2 : 0));
    // records: x, height, z, link (count on the first, -count on the last), flags
    std::vector<Pt> all;
    std::vector<int> links;
    for (auto& l : loops)
        for (size_t k = 0; k < l.size(); ++k) {
            all.push_back(l[k]);
            links.push_back(k == 0 ? int(l.size()) : k + 1 == l.size() ? -int(l.size()) : 0);
        }
    const u32 nw = u32(all.size());
    sc.walls = zalloc(ee, 16 * nw);
    sc.road = {};
    for (u32 i = 0; i < nw; ++i) {
        F x = fh(all[i].x), z = fh(-all[i].y);
        ee.w32(sc.walls + 16 * i, x.bits);
        ee.w32(sc.walls + 16 * i + 4, 0);
        ee.w32(sc.walls + 16 * i + 8, z.bits);
        u32 tag = (u32(u16(s16(links[i])))) | (u32(all[i].flags) << 16);
        ee.w32(sc.walls + 16 * i + 12, tag);
        sc.road.wall_points.push_back({{x.host(), 0, z.host()}, tag});
    }
    // segments: 0 <-> 1, and the words before the table (segment -1's list): count 0
    const u32 segs_mem = zalloc(ee, 0x40 + 2 * 0x40) + 0x40;
    for (u32 s = 0; s < 2; ++s) {
        ee.w16(segs_mem + 0x40 * s + 0x16, 1);
        ee.w16(segs_mem + 0x40 * s + 0x18, u16(1 - s));
        course::RoadSegment rs;
        rs.neighbours = {u16(1 - s)};
        sc.road.segments.push_back(rs);
    }
    // the wall grid: one cell, a group per segment (ids ascending)
    const u32 grid = zalloc(ee, 0x40), cells = zalloc(ee, 16), list = zalloc(ee, 4 * (nw + 8));
    ee.w32(grid, cells);
    ee.w16(grid + 4, 2);
    ee.w16(grid + 6, 2);
    for (u32 i = 0; i < 4; ++i) ee.w32(grid + 8 + 4 * i, fh(i % 2 ? 200.0 : -200.0).bits);
    ee.w32(cells, list);
    course::RoadGrid& g = sc.road.wall_grid;
    g.xs = {-200, 200};
    g.zs = {-200, 200};
    g.cell_begin = {0};
    u32 p = list;
    for (u16 s = 0; s < 2; ++s) {
        std::vector<u16> ids;
        for (u32 i = 0; i < nw; ++i)
            if (all[i].seg == s) ids.push_back(u16(i));
        ee.w16(p, u16(ids.size()));
        ee.w16(p + 2, s);
        p += 4;
        for (u16 id : ids) {
            ee.w16(p, id);
            p += 2;
            g.refs.push_back({s, id});
        }
    }
    ee.w16(p, 0);
    g.cell_begin.push_back(u32(g.refs.size()));
    const u32 rb = zalloc(ee, 0x60), container = zalloc(ee, 0x80);
    ee.w32(rb + 0x34, segs_mem);
    ee.w32(rb + 0x3C, sc.walls);
    ee.w32(rb + 0x4C, grid);
    ee.w32(container + 0x70, rb);
    sc.course_ptr = zalloc(ee, 0x10);
    ee.w32(sc.course_ptr, container);
    sc.course = std::make_unique<sim::CourseCollision>(sc.road);
    sc.loop0.clear();
    for (const Pt& q : loops[0]) sc.loop0.push_back({q.x, q.y});
}

// A body for car i, placed near the boundary (and near body 0 for the others), as the copy-in leaves it.
ex::Body make_body(Rng& rng, const Scene& sc, int i, const std::vector<ex::Body>& before) {
    ex::Body b;
    b.behaviour = disc_behaviour(); // the disc's release (program_map.h)
    ex::build(b, fh(rng.uniform(1.4f, 2.6f)), fh(rng.uniform(1.4f, 2.6f)), fh(rng.uniform(1.6f, 2.0f)), fh(rng.uniform(900, 1600)),
              fh(rng.uniform(900, 3000)), i);
    double X, Y, h = rng.uniform(-3.2f, 3.2f), vx, vy;
    if (i > 0 && rng.chance(0.7)) { // near body 0: car contact
        const ex::Body& o = before[0];
        double a = rng.uniform(-3.2f, 3.2f), d = rng.uniform(2.0f, 5.0f);
        X = o.cx.value() + d * std::cos(a);
        Y = o.cy.value() + d * std::sin(a);
        vx = o.cvx.value() - std::cos(a) * rng.uniform(0, 25);
        vy = o.cvy.value() - std::sin(a) * rng.uniform(0, 25);
    } else { // inside the boundary, near edge k
        size_t k = size_t(rng.range(0, s64(sc.loop0.size()) - 1));
        auto pa = sc.loop0[k], pb = sc.loop0[(k + 1) % sc.loop0.size()];
        double ex_ = pb[0] - pa[0], ey = pb[1] - pa[1], len = std::hypot(ex_, ey), s = rng.uniform(0.1f, 0.9f);
        double nx = -ey / len, ny = ex_ / len, d = rng.uniform(0.3f, 3.0f); // left of a -> b: inside
        X = pa[0] + ex_ * s + nx * d;
        Y = pa[1] + ey * s + ny * d;
        double sp = rng.uniform(0, 40), a = rng.uniform(-1.2f, 1.2f); // towards the wall, +-70 deg
        vx = -sp * (nx * std::cos(a) - ny * std::sin(a));
        vy = -sp * (nx * std::sin(a) + ny * std::cos(a));
    }
    double w = rng.uniform(-3, 3);
    if (rng.chance(0.1)) vx = rng.uniform(-0.9f, 0.9f), vy = rng.uniform(-0.9f, 0.9f), w = 0; // crawling: the stuck nudge
    if (rng.chance(0.05)) w = rng.chance(0.5) ? 70 : -70;                                     // past the yaw clamp
    if (rng.chance(0.05)) h += (rng.chance(0.5) ? 1 : -1) * 2 * kPi * rng.range(8, 10);        // past the heading wrap
    b.cx = fh(X), b.cy = fh(Y), b.cz = fh(rng.uniform(-0.5f, 0.5f));
    b.cvx = fh(vx), b.cvy = fh(vy), b.cvz = fh(rng.uniform(-1, 1));
    b.ch = fh(h), b.cw = fh(w);
    b.hint = rng.chance(0.8) ? int(rng.range(0, 1)) : -1;
    b.course = sc.course.get();
    b.dt_in = fh(rng.uniform(0.9f, 1.1f) / 60.0);
    b.steer = s8(rng.range(-100, 100));
    b.ghost = rng.chance(0.1);
    b.control = rng.chance(0.25) ? s8(rng.range(1, 2)) : 0; // (the later builds' stuck nudge: a pad car's only)
    return b;
}

// The manager in memory: conductor (its cars carry the step length, steering byte, ghost flag and control source) and the
// bodies.
u32 make_manager(Ee& ee, const Scene& sc, const std::vector<ex::Body>& bodies) {
    const int n = int(bodies.size());
    const u32 cond = zalloc(ee, kCarBase + car_stride() * 8), cm = zalloc(ee, ce::kBody0 + ce::kBodySize * 8);
    ee.w32(cm, cond);
    ee.w32(cm + 4, u32(n));
    ee.w32(cm + 8, sc.course_ptr);
    for (int i = 0; i < n; ++i) {
        const u32 car = cond + kCarBase + car_stride() * u32(i), b = ce::body_at(cm, i);
        ee.w32(car + 0xEC0, bodies[size_t(i)].dt_in.bits);
        ee.w8(car + car_off(0x14A7), u8(bodies[size_t(i)].steer));
        ee.w8(car + car_off(0x136E), bodies[size_t(i)].ghost ? 1 : 0);
        ee.w8(car + car_off(0x136D), u8(bodies[size_t(i)].control));
        ce::write_body(ee, b, bodies[size_t(i)], sc.walls);
        ee.w32(b + 0x450, car);
        ee.w32(b + 0x478, sc.course_ptr);
        ee.w32(b + 0x498, ce::body_at(cm, 0));
    }
    return cm;
}

} // namespace

DIFF_TEST_ALL(model_collide_exact_build, "collision body build 0x1FAE38 (chamfer 0x1FA740) and pair tables 0x1FB0D8 vs collide::exact::build / pair_tables, bit for bit") {
    for (int it = 0; it < ctx.iterations; ++it) {
        ctx.ee.free_all();
        const int n = int(ctx.rng.range(1, 8));
        std::vector<ex::Body> ours(static_cast<size_t>(n));
        const u32 cm = ctx.ee.alloc(ce::kBody0 + ce::kBodySize * 8);
        for (u32 a = 0; a < ce::kBody0 + ce::kBodySize * 8; a += 4) ctx.ee.w32(cm + a, 0);
        for (int i = 0; i < n; ++i) {
            F a = fh(ctx.rng.uniform(0.8f, 3.0f)), r = fh(ctx.rng.uniform(0.8f, 3.0f)), w = fh(ctx.rng.uniform(1.2f, 2.4f)),
              m = fh(ctx.rng.uniform(500, 2500)), iz = fh(ctx.rng.uniform(300, 5000));
            // the caller's chamfers (0x1FA740: width x the constant at 0x35069C)
            F k = F::raw(ctx.ee.r32(pa(0x0035069C))), d1 = w * k;
            ctx.ee.call(kBuild, {Arg::ptr(ce::body_at(cm, i)), Arg::integer(i), Arg::ptr(0), Arg::ptr(0), Arg::fbits(a.bits), Arg::fbits(r.bits),
                                 Arg::fbits(w.bits), Arg::fbits(d1.bits), Arg::fbits(d1.bits), Arg::fbits(m.bits), Arg::fbits(iz.bits)});
            ours[size_t(i)].behaviour = disc_behaviour(); // (the disc's build's chamfer: Rule::BodyBuild)
            ex::build(ours[size_t(i)], a, r, w, m, iz, i);
        }
        ctx.ee.w32(cm + 4, u32(n));
        for (int i = 0; i < n; ++i)
            ctx.ee.call(kPairTables, {Arg::ptr(ce::body_at(cm, i)), Arg::ptr(ce::body_at(cm, 0)), Arg::integer(n)});
        ex::pair_tables(ours);
        std::vector<ex::Body> theirs = ce::read_bodies(ctx.ee, cm, 0);
        for (ex::Body& b : theirs) b.hint = -1; // not written by the build (ours: -1 by default)
        const std::string d = ce::first_difference(ours, theirs);
        ctx.check(d.empty(), "build: " + d);
        ctx.sample();
    }
}

DIFF_TEST_ALL(model_collide_exact_step, "collision steps 0x1FA8F8 / 0x1FACB8 on random scenes (synthetic course: walls, corners, sensors) vs collide::exact::step_all / step_walls, bit for bit") {
    patch_effects(ctx.ee);
    struct Path {
        Pa pc; // NTSC-U (paths missing from the disc's program are left out)
        const char* name;
        long n = 0;
    };
    static Path paths[] = {{0x001FCEF8, "separation"}, {0x001FBED4, "persist velocity"}, {0x001FD0B4, "hold walks on"},
                           {0x001FD028, "hold walks back"}, {0x001FD1E8, "hold"}, {0x001FD6F4, "corner crossing"},
                           {0x001FD4C8, "sensor crossing"}, {0x001FDEC0, "point response"}, {0x001FE3A0, "corner response"},
                           {0x001FC5C0, "pair response"}, {0x001FDD64, "push-out"}, {0x001FDD18, "push-out slide"},
                           {0x001FB7A0, "stuck nudge"}, {0x001FB6B0, "yaw clamp"}, {0x001FB658, "heading wrap"}};
    for (Path& p : paths) {
        p.n = 0;
        if (has_pa(p.pc.ntsc_u)) ctx.ee.probe(p.pc, [&p](Ee&) { ++p.n; });
    }
    long steps = 0, bad = 0;
    for (int it = 0; it < ctx.iterations && bad < 5; ++it) {
        ctx.ee.free_all();
        Scene sc;
        make_course(ctx.ee, ctx.rng, sc);
        const int n = int(ctx.rng.range(1, 3));
        std::vector<ex::Body> ours;
        for (int i = 0; i < n; ++i) ours.push_back(make_body(ctx.rng, sc, i, ours));
        ex::pair_tables(ours);
        const u32 cm = make_manager(ctx.ee, sc, ours);
        const bool walls_only = ctx.rng.chance(0.25);
        for (int s = 0; s < 4 && bad < 5; ++s) {
            const std::vector<u8> pre = ce::snapshot(ctx.ee, cm);
            if (walls_only) {
                for (int i = 0; i < n; ++i) {
                    ctx.ee.call(kStepWalls, {Arg::ptr(cm), Arg::integer(i)});
                    ex::step_walls(ours, i);
                }
            } else {
                ctx.ee.call(kStepAll, {Arg::ptr(cm)});
                ex::step_all(ours);
            }
            ++steps;
            ctx.sample();
            std::string d = ce::first_difference(ours, ce::read_bodies(ctx.ee, cm, sc.walls, sc.course.get()));
            if (!d.empty()) {
                ++bad;
                std::string where = walls_only ? std::string() : "; function by function: " + ce::drill_step(ctx.ee, cm, pre, sc.course.get());
                ctx.check(false, fmt("scene %.0f step %.0f", it, s) + (walls_only ? " walls-only: " : ": ") + d + where);
                break;
            }
            // push on: the same velocity change on both sides (the committed state is the next step's input)
            for (int i = 0; i < n; ++i) {
                ex::Body& b = ours[size_t(i)];
                F kx = fh(ctx.rng.uniform(-2, 2)), ky = fh(ctx.rng.uniform(-2, 2));
                b.cvx = b.cvx + kx;
                b.cvy = b.cvy + ky;
                const u32 B = ce::body_at(cm, i);
                ctx.ee.w32(B + 0x460, b.cvx.bits);
                ctx.ee.w32(B + 0x464, b.cvy.bits);
            }
        }
    }
    std::string cov = "original paths taken:";
    for (const Path& p : paths) cov += " " + std::string(p.name) + " " + std::to_string(p.n) + ",";
    cov.pop_back();
    ctx.note(cov);
    ctx.note(std::to_string(steps) + " steps, " + std::to_string(bad) + " differing");
}

DIFF_TEST_ALL(model_collide_course, "course walls for the collision step on the disc's courses: grid walk 0x228848, segment links 0x228B68, road segment 0x2280C8 vs sim::CourseCollision") {
    Volume& vol = ctx.disc->volume();
    std::vector<std::string> names;
    for (const char* c : {"trialmt", "laguna", "super", "route11", "smtsouth", "montecarlo", "midfield", "deepforest", "tahitimaze", "rome"})
        names.push_back(c);
    static constexpr Pa kWalk = 0x00228848, kLinked = 0x00228B68, kRoad = 0x002280C8, kSweepCb = 0x001FD368;
    std::vector<int> seen;
    long walks = 0, links = 0, roads = 0, missing = 0;
    for (const std::string& name : names) {
        const Volume::Entry* e = vol.find(course_path(vol, name));
        if (!e) {
            ++missing;
            continue;
        }
        Bytes file = vol.read(*e);
        ctx.ee.reset();
        ctx.ee.hle_libc_defaults();
        std::string err;
        const u32 container = race_setup::load_course(ctx.ee, file, err);
        if (!container) {
            ctx.check(false, name + ": " + err);
            continue;
        }
        const u32 course_ptr = ctx.ee.alloc(0x10);
        ctx.ee.w32(course_ptr, container);
        const u32 rb = ce::road_block(ctx.ee, course_ptr), walls = ce::wall_table(ctx.ee, course_ptr);
        course::CourseFile cf = course::CourseFile::parse(file);
        ByteSpan rs = cf.section(file, 28);
        if (rs.empty()) {
            ctx.check(false, name + ": no road block");
            continue;
        }
        std::optional<course::Road> road = course::Road::parse(rs);
        const sim::CourseCollision ours(*road);
        const int nseg = int(road->segments.size());
        // segment links, every pair from a few rows (a = -1 included)
        for (int a : {-1, 0, nseg / 3, nseg / 2, nseg - 1})
            for (int b = -1; b < nseg; ++b) {
                bool theirs = ctx.ee.call(kLinked, {Arg::ptr(rb), Arg::integer(a), Arg::integer(b)}).v0.lo != 0;
                ctx.expect_eq(theirs, ours.linked(a, b), [&] { return name + fmt(" linked(%.0f, %.0f)", a, b); });
                ++links;
            }
        // grid walks: the sweep callback slot replaced by a recorder
        ctx.ee.hle(kSweepCb, [&seen, walls](Ee& ee) { seen.push_back(int((u32(ee.a(0)) - walls) / 16)); }, "candidate recorder");
        const auto& wp = road->wall_points;
        for (int i = 0; i < ctx.iterations / 4; ++i) {
            const auto& c = wp[size_t(ctx.rng.range(0, s64(wp.size()) - 1))].pos;
            float x0 = c.x + ctx.rng.uniform(-6, 6), z0 = c.z + ctx.rng.uniform(-6, 6);
            float x1 = x0 + ctx.rng.uniform(0, 8), z1 = z0 + ctx.rng.uniform(0, 8);
            if (i % 50 == 0) x0 -= 5000, z1 += 5000; // off the grid
            const int hint = ctx.rng.chance(0.15) ? -1 : int(ctx.rng.range(0, nseg - 1));
            seen.clear();
            ctx.ee.call(kWalk, {Arg::ptr(rb), Arg::ptr(kSweepCb), Arg::ptr(0), Arg::integer(hint), Arg::fbits(F(x0).bits), Arg::fbits(F(z0).bits),
                                Arg::fbits(F(x1).bits), Arg::fbits(F(z1).bits)});
            std::vector<int> mine;
            ours.wall_candidates(F(x0), F(z0), F(x1), F(z1), hint, mine);
            ctx.expect_eq(1, u64(mine == seen), [&] {
                return name + fmt(" walk (%.2f, %.2f) +", x0, z0) + fmt("(%.2f, %.2f)", x1 - x0, z1 - z0) + " hint " + std::to_string(hint) +
                       ": " + std::to_string(mine.size()) + " vs " + std::to_string(seen.size()) + " candidates";
            });
            ++walks;
        }
        ctx.ee.clear_hle();
        ctx.ee.hle_libc_defaults();
        // road segment queries: four query records (0x34 bytes: x, height, z, segment / hint, best height, ...); record 0 decides
        const u32 rec = ctx.ee.alloc(4 * 0x34 + 0x40);
        for (int i = 0; i < ctx.iterations / 4; ++i) {
            const course::RoadSegment& s = road->segments[size_t(ctx.rng.range(0, nseg - 1))];
            if (s.vertices.empty()) continue;
            const course::Vec3& v = s.vertices[size_t(ctx.rng.range(0, s64(s.vertices.size()) - 1))];
            float x = v.x + ctx.rng.uniform(-3, 3), z = v.z + ctx.rng.uniform(-3, 3);
            if (i % 40 == 0) x = v.x, z = v.z; // on a vertex: no strict inside
            const int hint = ctx.rng.chance(0.2) ? -1 : int(ctx.rng.range(0, nseg - 1));
            for (u32 r = 0; r < 4; ++r) {
                const u32 q = rec + 0x34 * r;
                for (u32 o = 0; o < 0x34; o += 4) ctx.ee.w32(q + o, 0);
                ctx.ee.w32(q, F(r ? x + 30.0f * float(r) : x).bits);
                ctx.ee.w32(q + 8, F(z).bits);
                ctx.ee.w32(q + 0xC, u32(r ? -1 : hint));
            }
            ctx.ee.call(kRoad, {Arg::ptr(rb), Arg::ptr(rec)});
            const int theirs = s32(ctx.ee.r32(rec + 0xC));
            ctx.expect_eq(u64(s64(theirs)), u64(s64(ours.road_segment(F(x), F(z), hint))),
                          [&] { return name + fmt(" road (%.3f, %.3f)", x, z) + " hint " + std::to_string(hint); });
            ++roads;
        }
        ctx.sample();
    }
    ctx.note(std::to_string(names.size() - size_t(missing)) + " courses: " + std::to_string(walks) + " grid walks, " + std::to_string(links) +
             " link tests, " + std::to_string(roads) + " road queries");
}
