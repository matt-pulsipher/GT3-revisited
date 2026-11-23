// Differential tests for GT3's random number generator and the draws that use
// it (docs/specs/gt_mode.md §20): the generator primitives, the GT-mode state's
// skip / draw / stir, the championship trophy pick, the prize-car pick and
// paint roll (code slices of the car_get control), and the opponent pool draw
// of the race setup. Ours: base/game_random.h, game/gt_mode.h, race/pool_draw.h.

#include "harness.h"

#include "base/game_random.h"
#include "game/gt_mode.h"
#include "race/pool_draw.h"

#include <cstring>

using namespace gt3r;
using namespace gt3r::diff;

namespace {
constexpr u32 kStep = 0x0026C118;       // u32 (u32* state): one step, output word
constexpr u32 kUnit = 0x0026C178;       // float (u32* state): [0, 1)
constexpr u32 kSharedInt = 0x0026C0C0;  // int (): shared state, int(unit x 2^31)
constexpr u32 kSharedState = 0x003532E0;
constexpr u32 kGtSkip = 0x00211AC0;     // void (int n): n steps of the GT-mode state
constexpr u32 kGtDraw = 0x00211B00;     // float (int n): n - 1 steps, then unit
constexpr u32 kGtStir = 0x00211A68;     // void (): COP0 Random + shared int, mod 13 (0 -> 1) steps
constexpr u32 kGtState = 0x00353250;
constexpr u32 kTrophyPick = 0x001390D0; // void (out*, int forced): out+4 = trophy (forced >= 10: random)
constexpr u32 kGameStatus = 0x00218660, kCompletion = 0x00219918;
constexpr u32 kTrophyTableInit = 0x00139CA0; // static initializer of the trophy table (weights at +4)
constexpr u32 kPoolDraw = 0x00179D70;   // RaceInformation virtual 13 (this, race, slots*, count)
constexpr u32 kRaceState = 0x00351DE0;
constexpr u32 kPoolCount = 0x002384E0, kPoolEntry = 0x002384C8, kPoolUnavailable = 0x0023ABF0,
              kPoolAssign = 0x00238550, kPaints = 0x00176FD0;
constexpr u32 kPrizePickBegin = 0x0013D138, kPrizePickEnd = 0x0013D22C, kPrizeItem = 0x0013C8F0;
constexpr u32 kPaintRollBegin = 0x0013EA28, kPaintRollEnd = 0x0013EA84;

u32 fbits(float f) {
    u32 b;
    std::memcpy(&b, &f, 4);
    return b;
}
std::string hex(u32 v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}
u32 state_sample(DiffContext& ctx, int i) {
    // Edge states first, then random ones.
    static const u32 kEdge[] = {0, 1, 0xFFFFFFFFu, GameRandom::kBootSeed, 0x80000000u, 0x7FFFFFFFu};
    return i < int(std::size(kEdge)) ? kEdge[i] : ctx.rng.u32_();
}
} // namespace

DIFF_TEST(rng_step, "generator step 0x26C118 vs GameRandom::next") {
    u32 p = 0;
    for (int i = 0; i < ctx.iterations * 20; ++i) {
        u32 s = state_sample(ctx, i);
        ctx.ee.reset();
        p = ctx.ee.alloc(16);
        ctx.ee.w32(p, s);
        u32 out = u32(ctx.ee.call(kStep, {Arg::ptr(p)}).v0.lo);
        u32 x = s, ours = GameRandom::next(x);
        ctx.expect_eq(u64(out) << 32 | ctx.ee.r32(p), u64(ours) << 32 | x, [&] { return "state=" + hex(s); });
    }
}

DIFF_TEST(rng_unit, "unit draw 0x26C178 vs GameRandom::rand01 (bits)") {
    for (int i = 0; i < ctx.iterations * 20; ++i) {
        u32 s = state_sample(ctx, i);
        ctx.ee.reset();
        u32 p = ctx.ee.alloc(16);
        ctx.ee.w32(p, s);
        u32 f = ctx.ee.call(kUnit, {Arg::ptr(p)}).f0;
        u32 x = s;
        u32 ours = fbits(GameRandom::rand01(x));
        ctx.expect_eq(u64(f) << 32 | ctx.ee.r32(p), u64(ours) << 32 | x, [&] { return "state=" + hex(s); });
    }
}

DIFF_TEST(rng_shared_int, "shared-state integer draw 0x26C0C0 vs GameRandom::int31") {
    for (int i = 0; i < ctx.iterations * 10; ++i) {
        u32 s = state_sample(ctx, i);
        ctx.ee.reset();
        ctx.ee.w32(kSharedState, s);
        u32 v = u32(ctx.ee.call(kSharedInt).v0.lo);
        GameRandom g{s};
        u32 ours = u32(g.int31());
        ctx.expect_eq(u64(v) << 32 | ctx.ee.r32(kSharedState), u64(ours) << 32 | g.x,
                      [&] { return "state=" + hex(s); });
    }
}

DIFF_TEST(rng_gt_skip_draw, "GT-mode state skip 0x211AC0 / draw 0x211B00 vs GameRandom::skip / rand01") {
    for (int i = 0; i < ctx.iterations * 5; ++i) {
        u32 s = state_sample(ctx, i);
        int n = int(ctx.rng.range(0, 20));
        ctx.ee.reset();
        ctx.ee.w32(kGtState, s);
        ctx.ee.call(kGtSkip, {Arg::integer(n)});
        u32 x = s;
        GameRandom::skip(x, n);
        ctx.expect_eq(ctx.ee.r32(kGtState), x, [&] { return "skip state=" + hex(s) + " n=" + std::to_string(n); });

        n = int(ctx.rng.range(1, 20));
        ctx.ee.reset();
        ctx.ee.w32(kGtState, s);
        u32 f = ctx.ee.call(kGtDraw, {Arg::integer(n)}).f0;
        x = s;
        GameRandom::skip(x, n - 1);
        u32 ours = fbits(GameRandom::rand01(x));
        ctx.expect_eq(u64(f) << 32 | ctx.ee.r32(kGtState), u64(ours) << 32 | x,
                      [&] { return "draw state=" + hex(s) + " n=" + std::to_string(n); });
    }
}

DIFF_TEST(rng_gt_stir, "GT-mode stir 0x211A68 (COP0 Random + shared draw) vs GameRandom::stir") {
    for (int i = 0; i < ctx.iterations * 5; ++i) {
        u32 s = state_sample(ctx, i), sh = ctx.rng.u32_(), hw = u32(ctx.rng.range(0, 47));
        if (i < 48) hw = u32(i), sh = GameRandom::kBootSeed;
        ctx.ee.reset();
        ctx.ee.cop0_random = hw;
        ctx.ee.w32(kGtState, s);
        ctx.ee.w32(kSharedState, sh);
        ctx.ee.call(kGtStir);
        u32 x = s;
        GameRandom shared{sh};
        GameRandom::stir(x, hw, shared);
        ctx.expect_eq(u64(ctx.ee.r32(kGtState)) << 32 | ctx.ee.r32(kSharedState), u64(x) << 32 | shared.x,
                      [&] { return "state=" + hex(s) + " shared=" + hex(sh) + " cop0=" + std::to_string(hw); });
    }
    ctx.ee.cop0_random = 0;
}

DIFF_TEST(rng_trophy_pick, "championship trophy pick 0x1390D0 vs game::pick_trophy_index") {
    float completion = 0;
    ctx.ee.clear_hle();
    ctx.ee.hle_libc_defaults();
    ctx.ee.hle(kGameStatus, [](Ee& e) { e.set_v0(0x01000000); }, "game_status");
    ctx.ee.hle(kCompletion, [&](Ee& e) { e.fpr[0] = fbits(completion); }, "completion");
    for (int i = 0; i < ctx.iterations * 10; ++i) {
        u32 s = state_sample(ctx, i);
        // Completion: the status page's ratio (k / 524, at most 0.99, or 1), plus random values.
        int k = int(ctx.rng.range(0, 524));
        completion = i % 3 == 0 ? ctx.rng.uniform(0, 1) : k == 524 ? 1.0f : std::min(0.99f, float(k) / 524.0f);
        ctx.ee.reset();
        ctx.ee.call(kTrophyTableInit);
        ctx.ee.w32(kGtState, s);
        u32 out = ctx.ee.alloc(16);
        ctx.ee.call(kTrophyPick, {Arg::ptr(out), Arg::integer(10)});
        u32 x = s;
        int ours = game::pick_trophy_index(completion, x);
        ctx.expect_eq(u64(ctx.ee.r32(out + 4)) << 32 | ctx.ee.r32(kGtState), u64(u32(ours)) << 32 | x,
                      [&] { return "state=" + hex(s) + " completion=" + std::to_string(completion); });
    }
    ctx.ee.clear_hle();
    ctx.ee.hle_libc_defaults();
}

DIFF_TEST(rng_prize_pick, "prize-car weighted pick (car_get render_model slice 0x13D138..0x13D22C) vs game::pick_weighted") {
    ctx.ee.clear_hle();
    ctx.ee.hle_libc_defaults();
    ctx.ee.hle(kPrizeItem, [](Ee&) {}, "prize_item");
    // The slice calls out (ra is overwritten): the end stub returns to the harness.
    ctx.ee.hle(kPrizePickEnd, [](Ee& e) { e.gpr[31].lo = Ee::kReturnAddr; }, "end");
    for (int i = 0; i < ctx.iterations * 5; ++i) {
        u32 s = state_sample(ctx, i);
        int n = int(ctx.rng.range(1, 8));
        std::vector<int> w;
        for (int j = 0; j < n; ++j) w.push_back(int(ctx.rng.range(i % 4 == 0 ? -2 : 1, i % 2 ? 1 : 9)));
        ctx.ee.reset();
        ctx.ee.w32(kGtState, s);
        u32 a = ctx.ee.alloc(0x1100), b = ctx.ee.alloc(0xE40 + 0x14 * 8 + 0x80), wp = ctx.ee.alloc(64),
            out = ctx.ee.alloc(32);
        for (int j = 0; j < n; ++j) ctx.ee.wf(wp + 4 * u32(j), std::max(float(w[size_t(j)]), 1.0f)); // as the ctor stores them
        ctx.ee.w32(a + 0x102C, b);
        ctx.ee.w8(a + 0x1044, u8(n));
        ctx.ee.w32(b + 0xE9C, wp);
        ctx.ee.w32(out + 0x14, 0xDEAD);
        const u32 sp = Ee::kStackTop;
        ctx.ee.w32(sp + 0x14, out), ctx.ee.w32(sp + 0x1C, out), ctx.ee.w32(sp + 0x18, u32(n));
        ctx.ee.set32(20, a); // s4
        ctx.ee.set32(3, b);  // v1
        ctx.ee.fpr[20] = 0;  // f20: the weight sum
        ctx.ee.call(kPrizePickBegin);
        u32 x = s;
        int ours = game::pick_weighted(w, x);
        ctx.expect_eq(u64(ctx.ee.r32(out + 0x14)) << 32 | ctx.ee.r32(kGtState), u64(u32(ours)) << 32 | x, [&] {
            std::string d = "state=" + hex(s) + " weights=";
            for (int v : w) d += std::to_string(v) + " ";
            return d;
        });
    }
    ctx.ee.clear_hle();
    ctx.ee.hle_libc_defaults();
}

DIFF_TEST(rng_prize_paint, "prize-car paint roll (car_get slice 0x13EA28..0x13EA84) vs game::prize_paint_index") {
    u32 got = 0;
    ctx.ee.clear_hle();
    ctx.ee.hle_libc_defaults();
    ctx.ee.hle(kPaintRollEnd, [&](Ee& e) { // s0 = the paint index; back to the harness
        got = e.g32(16);
        e.gpr[31].lo = Ee::kReturnAddr;
    }, "set_paint");
    for (int i = 0; i < ctx.iterations * 10; ++i) {
        u32 s = state_sample(ctx, i);
        int n = int(ctx.rng.range(0, 40));
        ctx.ee.reset();
        ctx.ee.w32(kGtState, s);
        ctx.ee.set32(17, u32(n));          // s1: the model's colour count
        ctx.ee.set32(18, 0xFFFFFFFFu);     // s2: colour id -1
        ctx.ee.set32(16, 0);
        ctx.ee.call(kPaintRollBegin);
        u32 x = s;
        int ours = game::prize_paint_index(n, x);
        ctx.expect_eq(u64(got) << 32 | ctx.ee.r32(kGtState), u64(u32(ours)) << 32 | x,
                      [&] { return "state=" + hex(s) + " count=" + std::to_string(n); });
    }
    ctx.ee.clear_hle();
    ctx.ee.hle_libc_defaults();
}

DIFF_TEST(rng_pool_draw, "opponent pool draw RaceInformation vf13 0x179D70 vs race::draw_pool") {
    int pool = 0;
    std::vector<bool> unavailable;
    std::vector<int> assigned;
    ctx.ee.clear_hle();
    ctx.ee.hle_libc_defaults();
    ctx.ee.hle(kPoolCount, [&](Ee& e) { e.set_v0(u64(pool)); }, "pool_count");
    ctx.ee.hle(kPoolEntry, [](Ee& e) { e.set_v0(e.a(1)); }, "pool_entry"); // the "label" is the index
    ctx.ee.hle(kPoolUnavailable, [&](Ee& e) {
        u64 k = e.a(0);
        e.set_v0(k < unavailable.size() && unavailable[size_t(k)] ? 1 : 0);
    }, "unavailable");
    ctx.ee.hle(kPoolAssign, [&](Ee& e) { assigned.push_back(int(e.a(1))); }, "assign");
    ctx.ee.hle(kPaints, [](Ee&) {}, "paints");
    for (int i = 0; i < ctx.iterations * 5; ++i) {
        u32 s = state_sample(ctx, i);
        pool = int(ctx.rng.range(1, 32));
        unavailable.assign(size_t(pool), false);
        int avail = pool;
        if (i % 3 == 1)
            for (int k = 0; k < pool; ++k)
                if (ctx.rng.chance(0.3) && avail > 1) unavailable[size_t(k)] = true, --avail;
        // More draws than entries only without unavailable ones (the original then loops forever).
        int draws = avail == pool ? int(ctx.rng.range(0, pool + 6)) : int(ctx.rng.range(0, avail));
        assigned.clear();
        ctx.ee.reset();
        ctx.ee.w32(kRaceState, s);
        u32 self = ctx.ee.alloc(0x100), race = ctx.ee.alloc(0x40), table = ctx.ee.alloc(0x20),
            cars = ctx.ee.alloc(4 * 40), slots = ctx.ee.alloc(4 * 40);
        ctx.ee.w32(race + 0x18, table);
        ctx.ee.w32(table + 0, u32(draws + 1));
        ctx.ee.w32(table + 8, cars);
        for (int k = 0; k < 40; ++k) ctx.ee.w32(cars + 4 * u32(k), ctx.ee.alloc(0x40));
        for (int k = 0; k < draws; ++k) ctx.ee.w32(slots + 4 * u32(k), u32(k + 1));
        ctx.ee.call(kPoolDraw, {Arg::ptr(self), Arg::ptr(race), Arg::ptr(slots), Arg::integer(draws)});
        GameRandom g{s};
        std::vector<int> ours = race::draw_pool(g, unavailable, draws);
        u64 ho = ctx.ee.r32(kRaceState), hm = g.x;
        for (int k : assigned) ho = ho * 1000003u + u64(k);
        for (int k : ours) hm = hm * 1000003u + u64(k);
        ctx.expect_eq(ho, hm, [&] {
            std::string d = "state=" + hex(s) + " pool=" + std::to_string(pool) + " draws=" + std::to_string(draws) +
                            " orig=";
            for (int k : assigned) d += std::to_string(k) + ",";
            d += " ours=";
            for (int k : ours) d += std::to_string(k) + ",";
            return d;
        });
    }
    ctx.ee.clear_hle();
    ctx.ee.hle_libc_defaults();
}
