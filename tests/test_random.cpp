// The game's random generator and the draws built on it.
// These tests pin the algorithm and the
// behaviour around it.

#include "test_framework.h"
#include "replay_fixtures.h"

#include "base/game_random.h"
#include "disc/disc.h"
#include "game/gt_mode.h"
#include "game/gt_save.h"
#include "race/pool_draw.h"
#include "race/replay.h"

#include <algorithm>
#include <cstring>
#include <set>

using namespace gt3r;
using namespace gt3r::game;

namespace {

std::shared_ptr<const GtData> disc_data() {
    static std::shared_ptr<GtData> data;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (Disc* disc = test::test_disc()) {
            auto d = std::make_shared<GtData>();
            const Volume& vol = disc->volume();
            bool ok = d->load([&](const std::string& p) -> std::optional<Bytes> {
                const Volume::Entry* e = vol.find(p);
                if (!e) return std::nullopt;
                return vol.read(*e);
            });
            if (ok) data = d;
        }
    }
    return data;
}

u32 fixed_hardware() { return 7; }

// Steps between two states (up to `limit`), -1 if not reached.
int steps_between(u32 from, u32 to, int limit) {
    for (int i = 0; i <= limit; ++i) {
        if (from == to) return i;
        GameRandom::next(from);
    }
    return -1;
}

} // namespace

TEST(random_algorithm) {
    GameRandom g;
    CHECK(g.x == 0x87654321u);
    CHECK(g.next() == 0x88FB88FBu && g.x == 0xFDB97542u);
    CHECK(g.next() == 0x10231023u && g.x == 0xD950C973u);
    // unit: the low 23 output bits over 2^23.
    u32 x = 0x87654321u;
    float f = GameRandom::rand01(x);
    CHECK(f == float(0x88FB88FBu & 0x7FFFFFu) / 8388608.0f);
    for (int i = 0; i < 10000; ++i) {
        float u = g.rand01();
        CHECK(u >= 0 && u < 1);
    }
    GameRandom a{123}, b{123};
    a.skip(5);
    for (int i = 0; i < 5; ++i) b.next();
    CHECK(a.x == b.x);
    GameRandom m{0xFFFFFFFFu};
    for (int i = 0; i < 1000; ++i) {
        int v = m.int31();
        CHECK(v >= 0 && v <= 2147483392);
    }
}

TEST(random_vu0_register) {
    // Seed 0.1: R = 1.m with 0.1's mantissa (0x4CCCCD); each draw shifts m left with bit 4 ^ bit 22
    // (0 ^ 1) fed in at the bottom: 0x19999B, i.e. 1.2000000477 - 1.
    Vu0Random r;
    r.init(Vu0Random::kRunSeed);
    CHECK(r.r == 0x3FCCCCCDu);
    float f = r.next();
    CHECK(r.r == 0x3F99999Bu);
    u32 bits = 0x3F99999Bu;
    float expect;
    std::memcpy(&expect, &bits, 4);
    CHECK(f == expect - 1.0f);
    // Always [0, 1); a zero mantissa stays zero (as the hardware's feedback register).
    bool in_range = true;
    std::set<u32> seen;
    for (int i = 0; i < 10000; ++i) {
        float v = r.next();
        in_range = in_range && v >= 0 && v < 1;
        seen.insert(r.r);
    }
    CHECK(in_range && seen.size() == 10000);
    Vu0Random z;
    z.r = 0x3F800000u;
    CHECK(z.next() == 0 && z.next() == 0);
}

TEST(random_stir_range) {
    // (hardware random value + shared int) mod 13, 0 -> 1: always 1..12 steps.
    GameRandom shared{0x2468ACE0};
    std::set<int> seen;
    for (u32 hw = 0; hw < 48; ++hw)
        for (int k = 0; k < 50; ++k) {
            u32 x = 0x13579BDFu, before = x;
            GameRandom::stir(x, hw, shared);
            int n = steps_between(before, x, 13);
            CHECK(n >= 1 && n <= 12);
            seen.insert(n);
        }
    CHECK(seen.size() == 12);
}

TEST(random_pool_draw) {
    // No repeats until the pool is used up, then plain random indices.
    for (u32 seed = 1; seed < 200; ++seed) {
        GameRandom g{seed};
        std::vector<int> d = race::draw_pool(g, std::vector<bool>(8, false), 12);
        CHECK(d.size() == 12);
        std::set<int> first(d.begin(), d.begin() + 8);
        CHECK(first.size() == 8);
        for (int k : d) CHECK(k >= 0 && k < 8);
        // Same state, same draw.
        GameRandom h{seed};
        CHECK(race::draw_pool(h, std::vector<bool>(8, false), 12) == d && h.x == g.x);
        // One step per draw.
        CHECK(steps_between(seed, g.x, 12) == 12);
    }
    // Unavailable entries are never drawn while others are left.
    std::vector<bool> un = {true, false, true, false, false, true};
    for (u32 seed = 1; seed < 200; ++seed) {
        GameRandom g{seed};
        std::vector<int> d = race::draw_pool(g, un, 3);
        std::set<int> s(d.begin(), d.end());
        CHECK(s.size() == 3);
        for (int k : d) CHECK(!un[size_t(k)]);
    }
    // More draws than available entries (the original would hang): ours ends, repeating.
    GameRandom g{99};
    CHECK(race::draw_pool(g, un, 6).size() == 6);
    GameRandom e{5};
    CHECK(race::draw_pool(e, {}, 3).size() == 3 || race::draw_pool(e, {}, 3).empty());
}

TEST(random_prize_and_trophy_draws) {
    // Paint index: int(count x unit + 0.5), count -> 0; uniform over the count.
    int hist[5] = {};
    u32 x = 1;
    for (int i = 0; i < 50000; ++i) {
        int k = prize_paint_index(5, x);
        CHECK(k >= 0 && k < 5);
        if (k >= 0 && k < 5) ++hist[k];
    }
    for (int h : hist) CHECK(h > 9000 && h < 11000);
    CHECK(prize_paint_index(0, x) == 0);
    // Weighted pick: weight 3 against two of 1 (weights below 1 count as 1).
    int w[3] = {};
    for (int i = 0; i < 50000; ++i) ++w[pick_weighted({0, 3, 1}, x)];
    CHECK(w[1] > 28000 && w[1] < 32000 && w[0] > 9000 && w[2] > 9000);
    CHECK(pick_weighted({1}, x) == 0);
    // Trophies at completion 1: every weight + 165.333, the rare ones become likely.
    int t[10] = {};
    for (int i = 0; i < 20000; ++i) ++t[pick_trophy_index(1.0f, x)];
    CHECK(t[4] > 600 && t[6] > 600); // about 4 % each (from 0.1 % and 0.04 %)
    int t0[10] = {};
    for (int i = 0; i < 20000; ++i) ++t0[pick_trophy_index(0.0f, x)];
    CHECK(t0[6] < 30 && t0[7] > t0[0]);
}

TEST(random_replay_paint_seed) {
    race::Replay r;
    r.course = "trialmt", r.car = "r34_gtr_v_99";
    r.inputs.resize(3);
    r.exact = test::race_record_stub();
    r.paint_seed = 0xCAFEBABEu;
    race::Replay p = race::Replay::parse(r.serialize());
    CHECK(p.paint_seed == 0xCAFEBABEu);
    // Unset: the boot seed (the generator's state at power-on).
    race::Replay d;
    d.exact = test::race_record_stub();
    CHECK(d.paint_seed == GameRandom::kBootSeed);
    CHECK(race::Replay::parse(d.serialize()).paint_seed == GameRandom::kBootSeed);
}

TEST(random_gtmode_disc) {
    std::shared_ptr<const GtData> data = disc_data();
    if (!data) SKIP("needs GT3R_DISC");
    set_hardware_random_source(fixed_hardware);
    GtMode gt(data);
    // New game keeps the running state; saves carry it (+0xCC / "rng").
    gt.state().rng = 0xABCDEF01u;
    gt.new_game();
    CHECK(gt.state().rng == 0xABCDEF01u);
    CHECK(load_from_text(save_to_text(gt.state())).rng == 0xABCDEF01u);
    CHECK(import_ps2_save(export_ps2_save(gt.state(), *data), *data).rng == 0xABCDEF01u);
    // The menus step it once per frame.
    gt.tick_random();
    CHECK(steps_between(0xABCDEF01u, gt.state().rng, 1) == 1);

    // Pool availability on this (US) disc (none is region-locked here).
    int pools = 0, unavailable = 0, gaps = 0;
    for (const data::Event& ev : data->db.events()) {
        std::vector<std::string> pool = race::event_pool(data->db, ev);
        gaps += pool != ev.entries; // entries after a "-" are not in the pool
        if (pool.empty()) continue;
        ++pools;
        int avail = 0;
        for (const std::string& l : pool) avail += race::pool_entry_available(data->db, l);
        unavailable += int(pool.size()) - avail;
        // With an unavailable entry the original hangs if it must draw more cars than are left.
        if (avail < int(pool.size()) && ev.cars_in_race > 1) CHECK(avail >= int(ev.cars_in_race) - 1);
    }
    CHECK(pools > 300);
    std::printf("  %d event pools, %d entries unavailable in the US region, %d with entries past a \"-\"\n", pools,
                unavailable, gaps);

    // Opponents: the race module's generator, one step per car, no repeats.
    GameRandom r1{0x5555AAAAu}, r2{0x5555AAAAu};
    std::vector<std::string> a = gt.pick_opponents("Gsunday_e_0001", r1), b = gt.pick_opponents("Gsunday_e_0001", r2);
    CHECK(a == b && a.size() == 5 && r1.x == r2.x);
    CHECK(std::set<std::string>(a.begin(), a.end()).size() == a.size());
    // A drawn car has its pool entry's paint (+0x110 + 4 k) in whatever slot it lands: the Trial Mountain demo gives every
    // entry one.
    if (const data::Event* demo = data->db.event("demo_tm")) {
        GameRandom r3{0x5555AAAAu};
        std::vector<u32> ids;
        const std::vector<std::string> field = race::draw_opponents(data->db, *demo, 6, r3, &ids);
        const std::vector<std::string> pool = race::event_pool(data->db, *demo);
        CHECK(field.size() == 6 && ids.size() == 6 && field != pool);
        for (size_t i = 0; i < field.size() && i < ids.size(); ++i) {
            const auto at = std::find(pool.begin(), pool.end(), field[i]);
            CHECK(at != pool.end() && ids[i] == race::pool_color_id(*demo, int(at - pool.begin())) && ids[i] != 0xFFFFFFFFu);
        }
    }
    CHECK(steps_between(0x5555AAAAu, r1.x, 5) == 5);

    // Prize cars: with the same generator states (and the hardware source fixed) the
    // award is the same.
    auto award = [&](u32 seed) {
        GtMode g(data);
        g.state().credits = 100000000;
        g.state().rng = seed;
        shared_random().x = 0x11111111u;
        Award w;
        g.start_championship("Galtezza_e_0501");
        for (size_t i = 0; i < g.state().championship.races.size(); ++i) w = g.record_round(0);
        CHECK(w.champion && w.trophy >= 0 && w.trophy < 10 && w.new_cars.size() == 1);
        const GarageCar& c = g.state().garage[size_t(w.new_cars[0])];
        return std::make_tuple(w.trophy, c.car, c.color, g.state().rng);
    };
    CHECK(award(0x0BADF00Du) == award(0x0BADF00Du));
    std::set<std::string> cars;
    for (u32 s = 1; s < 60; ++s) cars.insert(std::get<1>(award(s * 0x9E3779B9u)));
    CHECK(cars.size() > 1); // a multi-candidate set rolls
    set_hardware_random_source(nullptr);
}
