// The vehicle tick's ground contact in EE arithmetic (src/sim/exact/course_ground.h, tick.h) against the original's own
// functions on the disc's courses, bit for bit (model_ground_course):
//   - the course ground query 0x18FB58 (0x2280C8) on four query records at a time, every byte of the records compared:
//     points inside triangles of every material and flags class, on their edges and vertices, just outside, near the
//     walls, on grid breakpoints, anywhere over the grid and off it; record 0's segment hint the triangle's own, a
//     neighbour, another segment or none; stale record contents (a miss keeps and renormalises the last plane);
//   - the ground contact 0x1E8B68 on a car record over the same points (body position and hub offsets around them,
//     speed ramp, F +0x535, both answers of the conductor's virtual 39), the whole record compared;
//   - the road bump tables 0x1DAE70 builds against sim::exact::surface_bumps() (from the program's data, race_inputs.h).
// The course is our parse of the same file the original relocates (race_setup::load_course); race_tick_exact runs the
// same ports in races.

#include "car_exact_probe.h"
#include "harness.h"
#include "race_setup.h"

#include "disc/disc.h"
#include "disc/volume.h"
#include "formats/course/course_file.h"
#include "formats/course/road.h"
#include "sim/exact/course_ground.h"
#include "sim/exact/race_inputs.h"
#include "sim/exact/tick.h"

#include <cmath>
#include <cstdio>
#include <map>

using namespace gt3r;
using namespace gt3r::diff;

namespace {

namespace ce = car_exact;
namespace ex = sim::exact;
using ee::F;

constexpr Pa kQuery = 0x0018FB58, kContact = 0x001E8B68, kBumpTables = 0x001DAE70;
constexpr Pa kVf39Base = 0x001D9FD8, kVf39FreeRun = 0x001F1A20, kVf40Single = 0x001F2918;

std::string fmt(const char* f, double a = 0, double b = 0, double c = 0) {
    char s[256];
    std::snprintf(s, sizeof s, f, a, b, c);
    return s;
}

struct TriRef {
    int seg, tri;
};

// A point of the course to test: (x, z) and the segment of the triangle it was taken from (-1 none).
struct Pt {
    float x, z, y;
    int seg;
};

} // namespace

DIFF_TEST_ALL(model_ground_course, "ground query 0x18FB58 / 0x2280C8 and ground contact 0x1E8B68 on the disc's courses vs sim::exact, bit for bit") {
    Volume& vol = ctx.disc->volume();
    const char* names[] = {"trialmt", "laguna", "super", "route5", "route11", "smtsouth", "tahitimaze", "deepforest",
                           "rome", "akasaka", "grandvalley", "seattle", "L_testcourse1"};
    long queries = 0, contacts = 0, missing = 0, courses = 0;
    long rec_hits = 0, rec_misses = 0, overlaps = 0, flagged = 0, soft4 = 0, marked = 0;
    std::map<int, long> surfaces;
    std::string missing_names;
    for (const char* name : names) {
        const Volume::Entry* e = vol.find(course_path(vol, name));
        if (!e) {
            ++missing;
            missing_names += std::string(" ") + name;
            continue;
        }
        Bytes file = vol.read(*e);
        ctx.ee.reset();
        ctx.ee.hle_libc_defaults();
        std::string err;
        const u32 container = race_setup::load_course(ctx.ee, file, err);
        if (!container) {
            ctx.check(false, std::string(name) + ": " + err);
            continue;
        }
        ++courses;
        const u32 course_ptr = ctx.ee.alloc(0x10);
        ctx.ee.w32(course_ptr, container);
        ByteSpan rs = course::CourseFile::parse(file).section(file, 28);
        const course::Road road = course::Road::parse(rs);
        const ex::CourseGround ours(road);
        const int nseg = int(road.segments.size());
        // the parse keeps 20 neighbours (the room in the segment record); the program reads all of them
        for (int s = 0; s < nseg; ++s)
            ctx.check(rd16(rs, rd32(rs, 0x34) + 0x40 * u32(s) + 0x16) <= 20, std::string(name) + fmt(": segment %.0f has more than 20 neighbours", s));

        // the road bump tables
        ctx.ee.call(kBumpTables, {}, 10'000'000);
        const ex::SurfaceBumps sb = ex::surface_bumps(car_exact::program_data(*ctx.disc));
        for (u32 s = 0; s < 16; ++s) {
            const bool same = ctx.ee.r32(pa(0x002D2D30) + 4 * s) == sb.amp[s].bits && ctx.ee.r32(pa(0x002D2D70) + 4 * s) == sb.freq[s].bits &&
                              s8(ctx.ee.r8(pa(0x002D2DB0) + s)) == sb.scaled[s] && s8(ctx.ee.r8(pa(0x002D2DC0) + s)) == sb.mode[s];
            ctx.check(same, std::string(name) + fmt(": road bump table row %.0f", s));
        }

        // triangles by class: material, then each flags bit
        std::map<int, std::vector<TriRef>> classes;
        for (int s = 0; s < nseg; ++s)
            for (int t = 0; t < int(road.segments[size_t(s)].triangles.size()); ++t) {
                const course::RoadTriangle& tr = road.segments[size_t(s)].triangles[size_t(t)];
                classes[u8(tr.edge_flags)].push_back({s, t});
                for (int b = 0; b < 8; ++b)
                    if (u8(tr.surface) & (1 << b)) classes[0x100 << b].push_back({s, t});
            }
        std::vector<int> class_keys;
        for (auto& [k, v] : classes) class_keys.push_back(k);
        const course::RoadGrid& grid = road.grid_index;
        auto point = [&]() -> Pt {
            const int kind = int(ctx.rng.range(0, 9));
            if (kind <= 5 && !class_keys.empty()) {
                const std::vector<TriRef>& c = classes[class_keys[size_t(ctx.rng.range(0, s64(class_keys.size()) - 1))]];
                const TriRef r = c[size_t(ctx.rng.range(0, s64(c.size()) - 1))];
                const course::RoadSegment& s = road.segments[size_t(r.seg)];
                const course::RoadTriangle& t = s.triangles[size_t(r.tri)];
                const course::Vec3 &a = s.vertices[t.a], &b = s.vertices[t.b], &cc = s.vertices[t.c];
                double u = ctx.rng.uniform(0, 1), v = ctx.rng.uniform(0, 1);
                if (u + v > 1) u = 1 - u, v = 1 - v;
                if (kind == 2) u = 0;                                     // on edge a-c
                if (kind == 3) v = 1 - u;                                 // on edge b-c
                if (kind == 4) { // a vertex
                    const int k = int(ctx.rng.range(0, 2));
                    u = k == 1, v = k == 2;
                }
                if (kind == 5) u = -0.02 * ctx.rng.uniform(0, 1);         // just outside
                const double x = a.x + u * (b.x - a.x) + v * (cc.x - a.x), z = a.z + u * (b.z - a.z) + v * (cc.z - a.z);
                const double y = a.y + u * (b.y - a.y) + v * (cc.y - a.y);
                return {float(x), float(z), float(y), r.seg};
            }
            if (kind == 6 && !road.wall_points.empty()) { // near a wall
                const course::Vec3& w = road.wall_points[size_t(ctx.rng.range(0, s64(road.wall_points.size()) - 1))].pos;
                return {w.x + ctx.rng.uniform(-3, 3), w.z + ctx.rng.uniform(-3, 3), w.y, -1};
            }
            if (!grid.empty()) {
                const float x0 = grid.xs.front(), x1 = grid.xs.back(), z0 = grid.zs.front(), z1 = grid.zs.back();
                float x = ctx.rng.uniform(x0, x1), z = ctx.rng.uniform(z0, z1);
                if (kind == 7) { // on breakpoints, the grid's ends included
                    if (ctx.rng.chance(0.7)) x = grid.xs[size_t(ctx.rng.range(0, s64(grid.xs.size()) - 1))];
                    if (ctx.rng.chance(0.7)) z = grid.zs[size_t(ctx.rng.range(0, s64(grid.zs.size()) - 1))];
                }
                if (kind == 9) x = ctx.rng.chance(0.5) ? x0 - ctx.rng.uniform(0, 50) : x1 + ctx.rng.uniform(0, 50); // off the grid
                return {x, z, 0, -1};
            }
            return {ctx.rng.uniform(-500, 500), ctx.rng.uniform(-500, 500), 0, -1};
        };
        auto hint_for = [&](int seg) -> s32 {
            const double r = ctx.rng.uniform(0, 1);
            if (seg >= 0 && r < 0.45) return seg;
            if (seg >= 0 && r < 0.65 && !road.segments[size_t(seg)].neighbours.empty()) {
                const std::vector<u16>& n = road.segments[size_t(seg)].neighbours;
                return n[size_t(ctx.rng.range(0, s64(n.size()) - 1))];
            }
            if (r < 0.85) return -1;
            return s32(ctx.rng.range(0, nseg - 1));
        };
        // a car record with stale ground records, its conductor and course pointers
        const u32 car = ctx.ee.alloc(ce::kCarSize + 16), cond = ctx.ee.alloc(cond_off(0xE3A8) + 8), vt = ctx.ee.alloc(sim::concept_program(program()) ? 0x170 : 0x150);
        ctx.ee.w32(car + 4, cond);
        ctx.ee.w32(car + 8, course_ptr);
        ctx.ee.w32(cond + cond_off(0xE3A8), vt);
        ctx.ee.w32(vt + 8 * u32(conductor_slot(40)) + 4, kVf40Single); // the single race conductor's course-out hook (a no-op)
        auto fill = [&]() {
            for (u32 o = 0x10; o < ce::kCarSize; o += 4) ctx.ee.w32(car + o, u32(ctx.rng.next()));
            for (u32 i = 0; i < 4; ++i) {
                const u32 G = car + car_off(ce::kGround0) + ce::kGroundSize * i;
                auto f = [&](u32 o, float lo, float hi) { ctx.ee.w32(G + o, F(ctx.rng.uniform(lo, hi)).bits); };
                if (ctx.rng.chance(0.9)) {
                    f(0x10, -50, 300), f(0x14, 0, 1), f(0x18, 0, 1), f(0x1C, -0.3f, 0.3f), f(0x20, 0.9f, 1), f(0x24, -0.3f, 0.3f), f(0x28, -300, 300);
                }
            }
        };
        for (int it = 0; it < ctx.iterations; ++it) {
            // four query points: around record 0's or anywhere
            Pt p[4];
            p[0] = point();
            for (int r = 1; r < 4; ++r) {
                if (ctx.rng.chance(0.75)) p[r] = {p[0].x + ctx.rng.uniform(-2.5f, 2.5f), p[0].z + ctx.rng.uniform(-2.5f, 2.5f), p[0].y, p[0].seg};
                else p[r] = point();
            }
            const s32 hint = hint_for(p[0].seg);
            // the query alone
            fill();
            for (u32 r = 0; r < 4; ++r) {
                const u32 G = car + car_off(ce::kGround0) + ce::kGroundSize * r;
                ctx.ee.w32(G + 0, F(p[r].x).bits);
                ctx.ee.w32(G + 8, F(p[r].z).bits);
            }
            ctx.ee.w32(car + car_off(ce::kGround0) + 0xC, u32(hint));
            {
                const std::vector<u8> pre = ce::snapshot(ctx.ee, car);
                ex::Car c = ce::read_car(ctx.ee, car);
                ctx.ee.call(kQuery, {Arg::ptr(course_ptr), Arg::ptr(car + car_off(ce::kGround0))}, 50'000'000);
                ours.query(c.g, c.behaviour);
                const ex::Car theirs = ce::read_car(ctx.ee, car);
                const std::string d = ce::first_difference(c, theirs);
                ctx.check(d.empty(), std::string(name) + fmt(": query at (%.3f, %.3f) hint %.0f: ", p[0].x, p[0].z, hint) + d);
                ctx.check(ce::unmirrored_changes(pre, ce::snapshot(ctx.ee, car)).empty(), std::string(name) + ": query changed words outside the field table");
                for (const ex::Ground& g : theirs.g) (g.miss ? rec_misses : rec_hits)++;
                // more than one triangle under record 0's point (overpasses, overlapping verges): from our parse, host floats
                if (!grid.empty() && !theirs.g[0].miss) {
                    int n = 0;
                    for (const course::RoadSegment& s : road.segments)
                        for (const course::RoadTriangle& t : s.triangles) {
                            const course::Vec3 &a = s.vertices[t.a], &b = s.vertices[t.b], &cc = s.vertices[t.c];
                            const double det = (b.x - a.x) * (cc.z - a.z) - (cc.x - a.x) * (b.z - a.z);
                            if (det == 0) continue;
                            const double u = ((p[0].x - a.x) * (cc.z - a.z) - (cc.x - a.x) * (p[0].z - a.z)) / det;
                            const double v = ((b.x - a.x) * (p[0].z - a.z) - (p[0].x - a.x) * (b.z - a.z)) / det;
                            n += u > 0 && v > 0 && u + v < 1;
                        }
                    overlaps += n > 1;
                }
                ++queries;
                ctx.sample();
            }
            // the whole ground contact: the body over the points, hubs around it
            fill();
            const bool marks = ctx.rng.chance(0.7);
            ctx.ee.w32(vt + 8 * u32(conductor_slot(39)) + 4, marks ? kVf39Base : kVf39FreeRun);
            ctx.ee.w16(vt + 0x138, 0);
            ctx.ee.w16(vt + 0x140, 0);
            const float bx = p[0].x, by = -p[0].z;
            ctx.ee.w32(car + ce::kF + 0, F(bx).bits);
            ctx.ee.w32(car + ce::kF + 4, F(by).bits);
            ctx.ee.w32(car + ce::kF + 8, F(p[0].y + ctx.rng.uniform(0, 1)).bits);
            for (u32 w = 0; w < 4; ++w) {
                const u32 W = car + ce::kWheel0 + ce::kWheelSize * w;
                // hubs: the other points relative to the body (physics frame: x, y = -z)
                ctx.ee.w32(W + 0x3C, F(p[w].x - bx).bits);
                ctx.ee.w32(W + 0x40, F(-p[w].z - by).bits);
                ctx.ee.w32(W + 0x44, F(ctx.rng.uniform(-0.5f, 0)).bits);
                ctx.ee.w32(W + 0x6C, F(ctx.rng.chance(0.2) ? 1.0f : ctx.rng.uniform(0, 1)).bits);
            }
            ctx.ee.w32(car + car_off(ce::kGround0) + 0xC, u32(hint));
            {
                const std::vector<u8> pre = ce::snapshot(ctx.ee, car);
                ex::Car c = ce::read_car(ctx.ee, car);
                ctx.ee.call(kContact, {Arg::ptr(car)}, 50'000'000);
                const ex::GroundCounts n = ex::ground_contact(c, ours, sb, marks);
                const ex::Car theirs = ce::read_car(ctx.ee, car);
                const std::string d = ce::first_difference(c, theirs);
                ctx.check(d.empty(), std::string(name) + fmt(": contact at (%.3f, %.3f) hint %.0f: ", p[0].x, p[0].z, hint) + d);
                const std::vector<u32> un = ce::unmirrored_changes(pre, ce::snapshot(ctx.ee, car));
                ctx.check(un.empty(), std::string(name) + fmt(": contact changed +0x%X outside the field table", un.empty() ? 0 : un[0]));
                flagged += n.flagged > 0;
                soft4 += n.soft == 4;
                marked += (theirs.x535 >> 4) & 1;
                for (size_t w = 0; w < 4; ++w)
                    if (!theirs.g[w].miss) ++surfaces[theirs.w[w].surface];
                ++contacts;
                ctx.sample();
            }
        }
    }
    std::string surf;
    for (auto& [s, n] : surfaces) surf += fmt(" %.0f:", s) + std::to_string(n);
    ctx.note(std::to_string(courses) + " courses (missing:" + (missing ? missing_names : std::string(" none")) + "), " +
             std::to_string(queries) + " queries (" + std::to_string(rec_hits) + " records on the road, " +
             std::to_string(rec_misses) + " missed, " + std::to_string(overlaps) + " points over overlapping triangles), " +
             std::to_string(contacts) + " ground contacts (" + std::to_string(flagged) + " with a flags-bit-0 wheel, " +
             std::to_string(marked) + " marked, " + std::to_string(soft4) + " all four on soft ground); surfaces" + surf);
}
