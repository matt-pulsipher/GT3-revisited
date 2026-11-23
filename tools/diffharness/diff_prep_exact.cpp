// Phase 2's leaf routines (src/sim/exact/tick_prep.cpp, tick_table.cpp) against the original's code on random and edge
// operands, bit for bit: what the races of race_tick_exact do not reach (knots a lookup hits exactly, unsorted and repeated
// knots, every segment of the engine condition curve and of wear curves other than the shipped one, the fade's edges). The
// software double they use is checked in libgcc_double_exact (diff_libm.cpp).

#include "car_exact_probe.h"
#include "harness.h"

#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

#include <cmath>
#include <cstdio>
#include <iterator>
#include <vector>

using namespace gt3r;
using namespace gt3r::diff;

namespace {

namespace ex = sim::exact;
namespace ce = car_exact;
using ee::F;

// NTSC-U addresses (pa() at the test's start translates them).
constexpr u32 kLookupU = 0x001D8FB8, kLookupBisectU = 0x001D8E50, kConditionU = 0x001DAD28, kWearGripU = 0x001E9E80;
constexpr u32 kSlipAngleU = 0x001EA348;
constexpr u32 kConditionBlockU = 0x002D2B00, kConditionBytesU = 0x002D2950, kConditionBuiltU = 0x002D294C;

std::string hex(u64 v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llX", (unsigned long long)v);
    return b;
}

// A table {s16 n; float* x; float* y} in the interpreter's memory: knots mostly increasing, some repeated, some unsorted.
struct EeTable {
    u32 head = 0;
    s16 n = 0;
    std::vector<F> x, y;
};

EeTable rand_table(DiffContext& ctx, int max_n) {
    Rng& r = ctx.rng;
    EeTable t;
    t.n = s16(r.range(1, max_n));
    const int mode = int(r.range(0, 9));
    float v = r.uniform(-50, 50);
    for (int k = 0; k < t.n; ++k) {
        if (mode == 0) v = r.uniform(-100, 100);  // unsorted
        else if (mode != 1 || !r.chance(0.3)) v += r.uniform(0.01f, 40); // mode 1: some knots repeated
        t.x.push_back(F(v));
        t.y.push_back(r.chance(0.1) ? F() : F(r.uniform(-500, 500)));
    }
    const u32 xs = ctx.ee.alloc(4 * u32(t.n)), ys = ctx.ee.alloc(4 * u32(t.n));
    t.head = ctx.ee.alloc(12);
    for (u32 k = 0; k < u32(t.n); ++k) ctx.ee.w32(xs + 4 * k, t.x[k].bits), ctx.ee.w32(ys + 4 * k, t.y[k].bits);
    ctx.ee.w16(t.head, u16(t.n));
    ctx.ee.w32(t.head + 4, xs);
    ctx.ee.w32(t.head + 8, ys);
    return t;
}

// Around a set of knots: a knot, a knot's neighbour (bit patterns), or uniform in [lo, hi].
F near(Rng& r, const std::vector<F>& knots, float lo, float hi) {
    const F k = knots[size_t(r.range(0, s64(knots.size()) - 1))];
    switch (r.range(0, 3)) {
    case 0: return k;
    case 1: return F::raw(k.bits + u32(r.range(-3, 3)));
    default: return F(r.uniform(lo, hi));
    }
}

} // namespace

DIFF_TEST_ALL(prep_leaves_exact, "0x1D8FB8 / 0x1D8E50 lookups, 0x1DAD28 condition factor (0x1DAB50 block), 0x1E9E80 wear grip, 0x1EA348 slip angle vs sim::exact, bit for bit") {
    Rng& r = ctx.rng;
    Ee& ee = ctx.ee;
    const u32 kLookup = pa(kLookupU), kLookupBisect = pa(kLookupBisectU), kCondition = pa(kConditionU), kWearGrip = pa(kWearGripU);
    const u32 kSlipAngle = pa(kSlipAngleU), kConditionBlock = pa(kConditionBlockU), kConditionBytes = pa(kConditionBytesU),
              kConditionBuilt = pa(kConditionBuiltU);
    // The lookups.
    long knot_hits = 0;
    for (int i = 0; i < ctx.iterations / 2; ++i) {
        ee.free_all();
        const EeTable t = rand_table(ctx, 16);
        for (int q = 0; q < 16; ++q) {
            const F v = near(r, t.x, -120, 650);
            for (const u32 fn : {kLookup, kLookupBisect}) {
                const u32 orig = ee.call(fn, {Arg::ptr(t.head), Arg::fbits(v.bits)}).f0;
                const F ours = fn == kLookup ? ex::lookup(t.n, t.x.data(), t.y.data(), v) : ex::lookup_bisect(t.n, t.x.data(), t.y.data(), v);
                ctx.expect_eq(orig, ours.bits, [&] {
                    std::string s = hex(fn) + " n " + std::to_string(t.n) + " v " + hex(v.bits) + " x";
                    for (const F& x : t.x) s += " " + hex(x.bits);
                    return s;
                });
            }
            for (const F& k : t.x) knot_hits += k == v;
        }
    }
    ctx.note("lookups: " + std::to_string(knot_hits) + " queries on a knot");
    // The condition factor, with the block built from random static bytes each time (the shipped bytes one in four).
    for (int i = 0; i < ctx.iterations * 2; ++i) {
        ee.free_all();
        std::array<u8, 9> b = {30, 30, 50, 10, 20, 20, 50, 10, 30};
        if (i % 4) for (u8& x : b) x = u8(r.range(0, 255));
        for (u32 k = 0; k < 9; ++k) ee.w8(kConditionBytes + k, b[k]);
        ee.w8(kConditionBuilt, 0);
        const ex::Condition k = ex::condition_params(b, ex::toward_zero(disc_behaviour()));
        const F e = near(r, {k.e1, F(), k.e2, k.e3}, -3e6f, 3e6f), o = near(r, {k.o1, F(), k.omax}, -3e6f, 3e6f);
        const u32 counters = ee.alloc(16);
        ee.w32(counters + 4, e.bits);
        ee.w32(counters + 8, o.bits);
        const u32 orig = ee.call(kCondition, {Arg::ptr(kConditionBlock), Arg::ptr(counters)}).f0;
        auto in = [&] {
            std::string s = "bytes";
            for (u8 x : b) s += " " + std::to_string(x);
            return s + " E " + hex(e.bits) + " O " + hex(o.bits);
        };
        // (the disc's build's factor, Rule::EngineCondition: the later builds' is 1 and builds no block)
        const ex::Behaviour how = disc_behaviour();
        ctx.expect_eq(orig, ex::condition_factor(how, k, e, o).bits, in);
        if (!ex::gt3_program(how.rule(ex::Rule::EngineCondition))) continue;
        const F block[9] = {k.e1, k.c1, k.e2, k.e3, k.c2, k.o1, k.co, k.omax, k.x20};
        for (u32 j = 0; j < 9; ++j) ctx.expect_eq(ee.r32(kConditionBlock + 4 * j), block[j].bits, [&] { return "block word " + std::to_string(j) + " " + in(); });
    }
    // Wear grip on random wear curves (the shipped one, one in four) and wheels in every segment, at the knots and just below
    // them; the pit flag and wear off sometimes.
    long active = 0;
    for (int i = 0; i < ctx.iterations * 2; ++i) {
        ee.free_all();
        const u32 car = ee.alloc(car_stride(), 64), cp = ee.alloc(0x40);
        ee.write_bytes(car, std::vector<u8>(car_stride(), 0));
        const u32 Fb = car + ce::kF, P = car + ce::kParams;
        ex::WearParams w = {F(-20000.0f), F(0.06f), F(40000.0f), F(), F(150000.0f), F(0.05f), F(210000.0f), F(0.30f), F(), F()};
        if (i % 4) {
            w.fresh = r.chance(0.1) ? F() : F(-r.uniform(0, 60000));
            w.knee1 = F(r.uniform(0, 1e5f));
            w.knee2 = r.chance(0.1) ? w.knee1 : F(w.knee1.host() + r.uniform(0, 1e5f));
            w.max = F(w.knee2.host() + r.uniform(0, 1e5f));
            if (r.chance(0.05)) w.max = F();
            w.fresh_loss = F(r.uniform(0, 0.5f)), w.loss1 = F(r.uniform(0, 0.5f)), w.loss2 = F(r.uniform(0, 0.5f)), w.max_loss = F(r.uniform(0, 0.9f));
        }
        // A curve no event builds (knees are bytes x 10000 >= 0): a large negative knee below a small upper one, so the
        // chopped differences can make the segment's t exactly 1 (its "end of segment" level).
        const int contrived = i % 8 == 1 ? 1 : i % 8 == 5 ? 2 : 0;
        if (contrived) {
            const F lo(-r.uniform(1e6f, 1.6e7f)), hi(r.uniform(1e-3f, 1.0f));
            w.fresh = F(-4e7f);
            if (contrived == 1) w.knee1 = F(-3e7f), w.knee2 = lo, w.max = hi;
            else w.knee1 = lo, w.knee2 = hi, w.max = F(1e8f);
        }
        const F words[8] = {w.fresh, w.fresh_loss, w.knee1, w.loss1, w.knee2, w.loss2, w.max, w.max_loss};
        for (u32 j = 0; j < 8; ++j) ee.w32(cp + 4 * j, words[j].bits);
        ee.w8(Fb + f_off(0x535), u8(r.chance(0.1) ? 0x10 | r.range(0, 255) : r.range(0, 255) & ~0x10));
        for (u32 a = 0; a < 2; ++a)
            for (u32 s = 0; s < 16; ++s) ee.w8(P + 0xDD0 + 16 * a + s, u8(r.range(0, 5)));
        for (u32 t = 0; t < 6; ++t) ee.w32(P + 0x398 + 0x1B4 * t + 0x34, F(r.chance(0.3) ? 1.0f : r.uniform(0.5f, 1.0f)).bits);
        for (u32 j = 0; j < 4; ++j) {
            const u32 W = car + ce::kWheel0 + ce::kWheelSize * j;
            ee.w8(W + 0x24, u8(r.range(0, 15)));
            ee.w32(W + 0x58, F(r.uniform(0, 1)).bits);
            ee.w8(W + 0x5D, u8(r.range(0, 255)));
            const F x = contrived ? F((contrived == 1 ? w.max : w.knee2).host() * r.uniform(0, 1))
                                  : near(r, {w.fresh, F(), w.knee1, w.knee2, w.max}, w.fresh.host() * 1.5f, w.max.host() * 1.2f + 1000);
            ee.w32(W + 0xA0, x.bits);
        }
        const ex::Car pre = ce::read_car(ee, car);
        const u64 orig = ee.call(kWearGrip, {Arg::ptr(Fb), Arg::ptr(P), Arg::ptr(cp)}).v0.lo & 0xFFFFFFFF;
        ex::Car ours = pre;
        const bool on = ex::wear_grip(ours, w);
        active += on;
        const std::string d = ce::first_difference(ours, ce::read_car(ee, car));
        auto in = [&] {
            std::string s = "curve";
            for (const F& x : words) s += " " + hex(x.bits);
            for (const ex::Wheel& x : pre.w) s += " wear " + hex(x.wear.bits);
            return s + (d.empty() ? "" : ": " + d);
        };
        ctx.expect_eq(orig, on ? 1 : 0, in);
        ctx.expect_eq(d.empty() ? 0 : 1, 0, in);
    }
    ctx.note("wear grip: " + std::to_string(active) + " calls with wear on");
    // The slip angle: random hub velocities, speeds at the fade's edges, x = -y (atan2f's shortcut), zeros.
    for (int i = 0; i < ctx.iterations * 4; ++i) {
        ee.free_all();
        float vx = r.uniform(-6, 6), vy = r.uniform(-6, 6);
        if (i % 4 == 1) {
            const float a = r.uniform(-3.2f, 3.2f), s = (r.chance(0.5) ? F::raw(0x3E8E38E4) : F::raw(0x4031C71D)).host();
            vx = s * std::cos(a), vy = s * std::sin(a);
        } else if (i % 4 == 2) vy = r.chance(0.5) ? -vx : 0.0f;
        else if (i % 16 == 3) vx = vy = 0;
        const F fx(vx), fy(vy);
        const u32 out = ee.alloc(4);
        const u32 orig = ee.call(kSlipAngle, {Arg::ptr(out), Arg::fbits(fx.bits), Arg::fbits(fy.bits)}).f0;
        F fade;
        const F ours = ex::slip_angle(disc_behaviour(), fx, fy, fade); // (the disc's build's: Rule::SlipAngle)
        auto in = [&] { return "vx " + hex(fx.bits) + " vy " + hex(fy.bits); };
        ctx.expect_eq(orig, ours.bits, in);
        ctx.expect_eq(ee.r32(out), fade.bits, in);
    }
}
