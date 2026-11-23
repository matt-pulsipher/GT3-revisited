// The AI course object's line queries in EE arithmetic (src/sim/exact/ai_course.h) against the original's own functions on the
// disc's courses, bit for bit (model_ai_course): for each course the original's AI course setup 0x1F9B98 / 0x1F92F8 fills the
// object K, which is read into sim::exact::AiCourse; then at many race distances (random ones over the lap and beyond it, the
// points' own distances and their neighbours, the lap's ends) and search hints, on every line present:
//   - the line position 0x1FA018 (outputs untouched when no span is found), the stop line's 0x1FA368, the span search 0x1F9F08;
//   - the side map 0x1FA530, the fallback 0x1F9BF8, the passing lines 0x1F9C80 / 0x1F9CB0 and the lane lookups 0x1F9CE0 /
//     0x1F9D38; the lap length 0x18FA10.
// Then the same on synthetic objects (the last course's road, random lines: unordered distances, every kind, arcs either way,
// absent lines), for the paths the disc's lines do not reach (no span found, the stop line's search running out).
// The real courses' objects are ours (sim::exact::ai_course_setup from our parse of the file), each compared point by point with
// the original's first. GT3R_AICOURSE_ALL=1 runs every course file of courses/US (default: a selection with pit lines, side maps stored in either
// order, licence files whose lines carry no road segments, dirt).

#include "ai_exact_probe.h"
#include "harness.h"
#include "race_setup.h"
#include "start_setup.h"

#include "disc/disc.h"
#include "disc/volume.h"
#include "sim/exact/ai_course.h"
#include "sim/exact/tick.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::diff;

namespace {

namespace ex = sim::exact;
namespace ae = ai_exact;
using ee::F;

constexpr Pa kSetup = 0x001F9B98, kLapLength = 0x0018FA10, kResolve = 0x001F9BF8, kSpan = 0x001F9F08;
constexpr Pa kEvaluate = 0x001FA018, kEvaluateStop = 0x001FA368, kSideMap = 0x001FA530;
constexpr Pa kLineB = 0x001F9C80, kLineA = 0x001F9CB0, kLaneLeft = 0x001F9CE0, kLaneRight = 0x001F9D38;
constexpr u32 kSentinel = 0x7FBADBAD;

std::string fmt(const char* f, ...) {
    char s[400];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(s, sizeof s, f, ap);
    va_end(ap);
    return s;
}

struct Counts {
    long evals = 0, missed = 0, beyond = 0, stops = 0, spans = 0, lanes = 0;
    std::map<std::string, long> cover;
};

// Every query on the AI course object at k (in the original's memory) against ours, `samples` times.
void probe_course(DiffContext& ctx, const std::string& name, u32 k, const ex::AiCourse& ours, int samples, Counts& n) {
    Ee& ee = ctx.ee;
    const u32 table = ee.r32(k + 4), out = ee.alloc(0x20), car = ee.alloc(0x400);
    const F lap = ours.lap_length;
    ctx.check(ee.call(kLapLength, {Arg::ptr(ee.r32(k))}).f0 == lap.bits, name + ": lap length");
    std::vector<int> present;
    for (int l = 0; l < 8; ++l)
        if (!ours.lines[size_t(l)].empty()) present.push_back(l);
    if (present.empty()) return ctx.check(false, name + ": no AI lines");
    for (int l : present) {
        int arcs = 0;
        for (const ex::AiPoint& p : ours.lines[size_t(l)]) arcs += p.kind != 0, ++n.cover[fmt("point kind %d", p.kind)];
        n.cover[fmt("line %d with arcs", l)] += arcs > 0;
    }
    // the driving lines' point counts: the blend's old line is searched from the current line's next point, which reads past a
    // line shorter than about 2/3 of the current one
    {
        size_t lo = 0, hi = 0;
        for (int l : {1, 2, 3, 6, 7})
            if (const size_t c = ours.lines[size_t(l)].size()) lo = lo ? std::min(lo, c) : c, hi = std::max(hi, c);
        if (lo && 2 * hi >= 3 * lo && name.rfind("synthetic", 0) != 0) {
            std::string counts;
            for (int l : {1, 2, 3, 6, 7}) counts += fmt(" %d:%d", l, int(ours.lines[size_t(l)].size()));
            ++n.cover["driving lines' point counts 1.5 times apart or more: " + name + counts];
        }
    }
    // the fallback and the passing lines for every request (a later build's compiler put the passing lines inside the lane
    // lookups: no routine of their own to call there; the lane lookups below check them)
    const bool passing = has_pa(kLineB.ntsc_u) && has_pa(kLineA.ntsc_u);
    if (!passing) ++n.cover["passing lines inlined in the lane lookups (not called on their own)"];
    for (int line = -1; line <= 9; ++line) {
        ctx.expect_eq(u32(ee.call(kResolve, {Arg::ptr(k), Arg::integer(line)}).v0.lo), u32(ours.resolve(line)),
                      [&] { return name + fmt(": resolve(%d)", line); });
        if (!passing) continue;
        ctx.expect_eq(u32(ee.call(kLineB, {Arg::ptr(table), Arg::integer(line)}).v0.lo), u32(ours.line_b(line)),
                      [&] { return name + fmt(": line_b(%d)", line); });
        ctx.expect_eq(u32(ee.call(kLineA, {Arg::ptr(table), Arg::integer(line)}).v0.lo), u32(ours.line_a(line)),
                      [&] { return name + fmt(": line_a(%d)", line); });
    }
    auto pick_s = [&](int line) -> F {
        const std::vector<ex::AiPoint>& p = ours.lines[size_t(line)];
        const F d = p[size_t(ctx.rng.range(0, s64(p.size()) - 1))].dist;
        switch (ctx.rng.range(0, 9)) {
        case 0: return d;
        case 1: return F::raw(d.bits + 1);
        case 2: return F::raw(d.bits - 1);
        case 3: return ctx.rng.chance(0.5) ? lap : F::raw(lap.bits - 1);
        case 4: return ctx.rng.chance(0.5) ? F() : F::raw(0x80000000);
        case 5: return F(ctx.rng.uniform(-0.15f, 1.15f) * lap.host());
        default: return F(ctx.rng.uniform(0, 1) * lap.host());
        }
    };
    for (int it = 0; it < samples; ++it) {
        const int line = present[size_t(ctx.rng.range(0, s64(present.size()) - 1))];
        const int count = int(ours.lines[size_t(line)].size());
        const F s = pick_s(ctx.rng.chance(0.2) ? present[size_t(ctx.rng.range(0, s64(present.size()) - 1))] : line);
        const int hint = ctx.rng.chance(0.9) ? int(ctx.rng.range(0, count)) : int(ctx.rng.range(-3, 3 * count));
        auto inputs = [&] { return name + fmt(": line %d (%d points) hint %d s 0x%08X (%.3f)", line, count, hint, s.bits, double(s.host())); };
        // the span search (a hint that makes it read past the line is skipped: the original reads what follows there)
        {
            int to = -7, from = -7;
            bool far = false;
            const int c = ours.span(line, hint, s, to, from, &far);
            if (far) {
                ++n.beyond;
            } else {
                ee.w32(out, kSentinel), ee.w32(out + 4, kSentinel);
                const u32 r = u32(ee.call(kSpan, {Arg::ptr(table), Arg::integer(line), Arg::integer(hint), Arg::ptr(out), Arg::ptr(out + 4),
                                                  Arg::fbits(s.bits)}).v0.lo);
                ctx.expect_eq(r, u32(c), [&] { return "span: " + inputs(); });
                ctx.expect_eq(ee.r32(out), c ? u32(to) : kSentinel, [&] { return "span to: " + inputs(); });
                ctx.expect_eq(ee.r32(out + 4), c ? u32(from) : kSentinel, [&] { return "span from: " + inputs(); });
                ++n.cover[fmt("span case %d", c)];
                ++n.spans;
            }
        }
        // the line position (the search sees s wrapped into the lap)
        {
            F ws = s;
            if (ws < F()) ws = ws + lap;
            else if (lap <= ws) ws = ws - lap;
            int to = 0, from = 0;
            bool far = false;
            (void)ours.span(line, hint, ws, to, from, &far);
            if (far) {
                ++n.beyond;
            } else {
                F x = F::raw(kSentinel), y = F::raw(kSentinel), h = F::raw(kSentinel);
                const bool ok = ours.evaluate(line, hint, s, x, y, h);
                for (u32 o = 0; o < 12; o += 4) ee.w32(out + o, kSentinel);
                ee.call(kEvaluate, {Arg::ptr(k), Arg::integer(line), Arg::integer(hint), Arg::ptr(out), Arg::ptr(out + 4), Arg::ptr(out + 8),
                                    Arg::fbits(s.bits)});
                ctx.expect_eq(ee.r32(out), x.bits, [&] { return "evaluate x: " + inputs(); });
                ctx.expect_eq(ee.r32(out + 4), y.bits, [&] { return "evaluate y: " + inputs(); });
                ctx.expect_eq(ee.r32(out + 8), h.bits, [&] { return "evaluate h: " + inputs(); });
                if (!ok) ++n.missed;
                else ++n.cover[fmt("evaluate on kind %d", ours.lines[size_t(line)][size_t(from)].kind)];
                ++n.evals;
            }
        }
        // the stop line's evaluation (on any line: the code takes the line it is given)
        {
            F x = F::raw(kSentinel), y = F::raw(kSentinel), h = F::raw(kSentinel);
            const int r = ours.evaluate_stop(line, s, x, y, h);
            for (u32 o = 0; o < 12; o += 4) ee.w32(out + o, kSentinel);
            const u32 v = u32(ee.call(kEvaluateStop, {Arg::ptr(k), Arg::integer(line), Arg::integer(hint), Arg::ptr(out), Arg::ptr(out + 4),
                                                     Arg::ptr(out + 8), Arg::fbits(s.bits)}).v0.lo);
            ctx.expect_eq(v, u32(r), [&] { return "evaluate_stop: " + inputs(); });
            ctx.expect_eq(ee.r32(out), x.bits, [&] { return "evaluate_stop x: " + inputs(); });
            ctx.expect_eq(ee.r32(out + 4), y.bits, [&] { return "evaluate_stop y: " + inputs(); });
            ctx.expect_eq(ee.r32(out + 8), h.bits, [&] { return "evaluate_stop h: " + inputs(); });
            if (r < 0) ++n.cover["evaluate_stop outside"];
            else if (x.bits == ours.lines[size_t(line)][size_t(r)].x.bits && r + 2 == count) ++n.cover["evaluate_stop at a span's start"];
            else ++n.cover["evaluate_stop inside"];
            ++n.stops;
        }
        // the side map and the lane lookups from this line
        {
            const u32 v = u32(ee.call(kSideMap, {Arg::ptr(k), Arg::integer(hint), Arg::fbits(s.bits)}).v0.lo);
            const bool sw = ours.side_swapped(hint, s);
            ctx.expect_eq(v, u32(sw), [&] { return "side map: " + inputs(); });
            if (sw) ++n.cover["side map swapped"];
            const u32 lv = u32(ee.call(kLaneLeft, {Arg::ptr(k), Arg::integer(line), Arg::integer(hint), Arg::fbits(s.bits)}).v0.lo);
            const u32 rv = u32(ee.call(kLaneRight, {Arg::ptr(k), Arg::integer(line), Arg::integer(hint), Arg::fbits(s.bits)}).v0.lo);
            ctx.expect_eq(lv, u32(ours.lane_left(line, hint, s)), [&] { return "lane left: " + inputs(); });
            ctx.expect_eq(rv, u32(ours.lane_right(line, hint, s)), [&] { return "lane right: " + inputs(); });
            ++n.lanes;
        }
        // the target point (0x1F7658) of a car near the line at a short look-ahead, so that it grows
        {
            ex::Car c;
            c.f3e4 = s;
            F x, y, h;
            const int near = line == 4 && !ours.lines[6].empty() ? 6 : line;
            if ((near != 4 && ours.evaluate(near, 0, s + F(ctx.rng.uniform(-3, 3)), x, y, h)) || ctx.rng.chance(0.2)) {
                c.pos.x = x + F(ctx.rng.uniform(-4, 4)), c.pos.y = y + F(ctx.rng.uniform(-4, 4));
                const F look = F(ctx.rng.uniform(0, 8));
                const int th = int(ctx.rng.range(0, count));
                F dx, dy;
                bool miss = false;
                const F l = ex::ai_target(c, ours, line, th, look, dx, dy, &miss);
                if (!miss) {
                    ee.w32(car, c.pos.x.bits), ee.w32(car + 4, c.pos.y.bits), ee.w32(car + f_off(0x3E4), s.bits); // (F: the later builds' race progress is 4 higher)
                    const u32 f = ee.call(pa(0x001F7658), {Arg::ptr(k), Arg::ptr(car), Arg::integer(line), Arg::integer(th), Arg::ptr(out),
                                                       Arg::ptr(out + 4), Arg::fbits(look.bits)}).f0;
                    ctx.expect_eq(f, l.bits, [&] { return "target look-ahead: " + inputs(); });
                    ctx.expect_eq(ee.r32(out), dx.bits, [&] { return "target dx: " + inputs(); });
                    ctx.expect_eq(ee.r32(out + 4), dy.bits, [&] { return "target dy: " + inputs(); });
                    ++n.cover[l == look ? "target at the look-ahead" : "target: look-ahead extended"];
                }
            }
        }
        ctx.sample();
    }
}

// A synthetic AI course object on the course object `course` (its road gives the lap length): up to 7 random lines.
u32 synthetic_course(DiffContext& ctx, u32 course) {
    Ee& ee = ctx.ee;
    const u32 k = ee.alloc(0x10), table = ee.alloc(0x28);
    ee.w32(k, course), ee.w32(k + 4, table), ee.w32(k + 8, 0);
    for (u32 o = 0; o < 0x28; o += 4) ee.w32(table + o, 0);
    const float lap = ee.rf(ee.r32(ee.r32(course) + 0x70) + 0x18);
    for (u32 line = 1; line < 8; ++line) {
        if (ctx.rng.chance(0.25)) continue;
        const u32 n = u32(ctx.rng.range(1, 12)), l = ee.alloc(4 + ae::kPointSize * n);
        ee.w32(table + 8 + 4 * line, l);
        ee.w32(l, n);
        std::vector<float> d(n);
        for (float& x : d) x = ctx.rng.uniform(-0.1f, 1.1f) * lap;
        if (ctx.rng.chance(0.5)) std::sort(d.begin(), d.end());
        for (u32 i = 0; i < n; ++i) {
            const u32 p = l + 4 + ae::kPointSize * i;
            for (u32 o = 0; o < ae::kPointSize; o += 4) ee.w32(p + o, 0);
            ee.w8(p, u8(ctx.rng.range(0, 2)));
            ee.wf(p + 0x04, ctx.rng.uniform(-800, 800)), ee.wf(p + 0x08, ctx.rng.uniform(-800, 800)), ee.wf(p + 0x0C, ctx.rng.uniform(-20, 80));
            ee.wf(p + 0x14, d[i]);
            ee.wf(p + 0x18, (ctx.rng.chance(0.5) ? 1 : -1) * ctx.rng.uniform(15, 400));
            ee.wf(p + 0x24, ctx.rng.uniform(-3.2f, 3.2f));
            if (ctx.rng.chance(0.05)) ee.w8(p, 0), ee.wf(p + 0x18, 0);
        }
    }
    return k;
}

} // namespace

DIFF_TEST_ALL(model_ai_course, "AI course queries 0x1FA018 / 0x1FA368 / 0x1F9F08 / 0x1FA530 / 0x1F9BF8 / lane lookups on the disc's courses vs sim::exact::AiCourse, bit for bit") {
    Volume& vol = ctx.disc->volume();
    std::vector<std::string> names = {"trialmt", "laguna", "super", "midfield", "r_midfield", "apricot", "r_apricot", "grandvalley",
                                      "route5_dry", "route11", "smtsouth", "tahitimaze", "deepforest", "akasaka", "seattle",
                                      "L_seattle1", "L_rome1", "L_apricot1", "L_grandvalley2", "L_tahitidirt1", "L_testcourse1"};
    if (std::getenv("GT3R_AICOURSE_ALL") && std::atoi(std::getenv("GT3R_AICOURSE_ALL")) != 0) {
        names.clear();
        for (const std::string& f : vol.list_dir(course_dir(vol)))
            if (f.find('/') == std::string::npos) names.push_back(f);
    }
    long courses = 0, built = 0;
    Counts real, synth;
    std::string perr;
    const ex::ProgramData* pd = start_setup::program_data(ctx, perr);
    ctx.check(pd != nullptr, "the program's tables: " + perr);
    std::string missing;
    u32 last_course = 0;
    for (const std::string& name : names) {
        const Volume::Entry* e = vol.find(course_path(vol, name));
        if (!e) {
            missing += " " + name;
            continue;
        }
        const Bytes file = vol.read(*e);
        Ee& ee = ctx.ee;
        ee.reset();
        ee.hle_libc_defaults();
        std::string err;
        const u32 container = race_setup::load_course(ee, file, err);
        if (!container) {
            ctx.check(false, name + ": " + err);
            continue;
        }
        const u32 course = ee.alloc(0x40), k = ee.alloc(0x10);
        ee.w32(course, container);
        ee.call(kSetup, {Arg::ptr(k), Arg::ptr(course)}, 2'000'000'000);
        ++courses;
        // our course setup (sim::exact::ai_course_setup) from our parse of the file against the original's object, then the
        // queries on ours
        start_setup::Course sc;
        if (!pd || !start_setup::load_course(file, *pd, sc, err)) {
            ctx.check(false, name + ": our course setup: " + err);
            continue;
        }
        const std::string d = ae::course_difference(sc.ai, ae::read_course(ee, k));
        ctx.check(d.empty(), name + ": our course setup: " + d);
        built += d.empty();
        try {
            probe_course(ctx, name, k, sc.ai, ctx.iterations * 4, real);
        } catch (const EeTrap& x) {
            ctx.check(false, name + ": " + x.what());
        }
        last_course = course;
    }
    for (int i = 0; last_course && i < 40; ++i) {
        try {
            const u32 k = synthetic_course(ctx, last_course);
            probe_course(ctx, fmt("synthetic %d", i), k, ae::read_course(ctx.ee, k), ctx.iterations / 4, synth);
        } catch (const EeTrap& x) {
            ctx.check(false, fmt("synthetic %d: ", i) + x.what());
        }
    }
    for (const Counts* n : {&real, &synth}) {
        std::string cov;
        for (const auto& [key, v] : n->cover) cov += fmt("%s%s %ld", cov.empty() ? "" : "; ", key.c_str(), v);
        ctx.note(fmt("%s: %ld line positions (%ld without a span), %ld stop-line positions, %ld span searches, %ld side-map / lane "
                     "lookups, %ld samples skipped (the search would read past the line); ",
                     n == &real ? fmt("%ld courses (missing:%s; %ld set up by ai_course_setup exactly as the original)", courses,
                                      missing.empty() ? " none" : missing.c_str(), built).c_str()
                                : "40 synthetic courses",
                     n->evals, n->missed, n->stops, n->spans, n->lanes, n->beyond) +
                 cov);
    }
    ctx.check(courses > 0, "no course found");
}
