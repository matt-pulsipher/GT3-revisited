// The tick's built inputs (src/sim/exact/race_inputs.h) on synthetic program images, disc-free: a hash of everything the
// builders produce from random tables, pinned so a build that changes their results (host float or std:: math creeping in,
// another architecture) fails; the version check of ProgramData; the race flow's ranking and gap matrices on hand-made races.

#include "sim/exact/race_flow.h"
#include "sim/exact/race_inputs.h"
#include "test_framework.h"
#include "sim/exact/program_tables.h"

#include <random>
#include <stdexcept>
#include <vector>

using namespace gt3r;
namespace ex = gt3r::sim::exact;

namespace {

using T = ex::ProgramData::Table;

void put32(ex::ProgramData& d, T t, u32 off, u32 v) {
    for (u32 i = 0; i < 4; ++i) d.poke(t, off + i, u8(v >> (8 * i)));
}

// Random tables (ProgramData::synthetic, filled by id).
ex::ProgramData program(std::mt19937& rng) {
    ex::ProgramData d = ex::ProgramData::synthetic();
    auto fill = [&](T t) {
        for (u32 i = 0; i < ex::ProgramData::size(t); ++i) d.poke(t, i, u8(rng() % 4 == 0 ? 0 : rng()));
    };
    fill(T::Static);
    fill(T::Effects);
    fill(T::Controller);
    fill(T::SurfaceClass);
    for (u32 i = 0; i < 16; ++i) put32(d, T::Soft, 4 * i, rng() % 2);
    d.poke(T::Static, 0x0B, u8(1 + rng() % 8)); // the rolling table's point count
    // The race start's and the car spec's tables (their own generator, so the builders' tables above stay as they were).
    std::mt19937 start(11);
    for (T t : {T::DriverSets, T::LineFallback, T::InitialLines, T::LineOrder, T::GearKinds, T::SpecCurves, T::SpecSurfaceClass,
                T::LsdTypes, T::MachineSectors})
        for (u32 i = 0; i < ex::ProgramData::size(t); ++i) {
            const u8 v = u8(start());
            d.poke(t, i, v);
            if (t == T::SpecCurves && i >= 0x10 && i - 0x10 < ex::ProgramData::size(T::SpecCurveB)) d.poke(T::SpecCurveB, i - 0x10, v);
        }
    return d;
}

struct Hash {
    u64 h = 1469598103934665603ull;
    void add(u32 v) { h = (h ^ v) * 1099511628211ull; }
    void add(ex::F f) { add(f.bits); }
    template <class T, size_t N> void add(const std::array<T, N>& a) {
        for (const T& x : a) add(x);
    }
    template <size_t N> void add(const ex::Table<N>& t) {
        add(u32(t.n));
        for (s32 k = 0; k < t.n; ++k) add(t.x[size_t(k)]), add(t.y[size_t(k)]);
    }
    void add(const ex::PedalCurve& c) { add(u32(c.type)), add(c.a), add(c.b), add(c.c); }
};
void add(Hash& h, s8 v) { h.add(u32(v)); }

u64 race_inputs_hash() {
    std::mt19937 rng(7);
    Hash h;
    for (int run = 0; run < 48; ++run) {
        const ex::ProgramData d = program(rng);
        std::vector<u8> ev(0x200);
        for (u8& b : ev) b = u8(rng() % 3 == 0 ? 0 : rng() % 64);
        ex::RaceConfig cfg = ex::race_config(ByteSpan(ev.data(), ev.size()));
        const auto kind = ex::Conductor(run % 8);
        const u8 cat = u8(rng() % 3);
        const bool band = ex::rubber_band_answer(kind, cfg, s32(rng() % 3));
        const bool wear = ex::wear_answer(kind, cfg, cat, s32(rng() % 2));
        const ex::RaceInputs r = ex::race_inputs(d, kind, cfg, wear, cat);
        const ex::RubberBand b = ex::rubber_band_params(cfg, band);
        for (u8 c : cfg) h.add(u32(c));
        h.add(u32(band)), h.add(u32(wear)), h.add(u32(ex::rubber_band_mode(kind, cfg))), h.add(u32(ex::rubber_band_cap(kind)));
        h.add(u32(ex::has_pits(kind))), h.add(u32(ex::pit_mode(kind, cat)));
        h.add(u32(r.course_out.on)), h.add(u32(r.course_out.pit)), h.add(u32(r.course_out.soft));
        h.add(r.prep.slip.drag_share), h.add(r.prep.slip.downforce_share), h.add(r.prep.slip.k_up), h.add(r.prep.slip.k_down);
        const ex::WearParams& w = r.prep.wear;
        for (ex::F f : {w.fresh, w.fresh_loss, w.knee1, w.loss1, w.knee2, w.loss2, w.max, w.max_loss, w.worn, w.slip_cap}) h.add(f);
        const ex::Condition& k = r.prep.condition;
        for (ex::F f : {k.e1, k.c1, k.e2, k.e3, k.c2, k.o1, k.co, k.omax, k.x20}) h.add(f);
        h.add(u32(r.aids.course_category)), h.add(r.aids.over_angle), h.add(r.aids.over_gain), h.add(u32(r.aids.dirt_steer)),
            h.add(r.aids.dirt_cut);
        h.add(r.statics.slip_fade), h.add(r.statics.slip_gain_on), h.add(r.statics.slip_gain_base), h.add(r.statics.slip_gain_knee);
        h.add(r.statics.rolling_curve), h.add(r.statics.rolling_mu);
        h.add(u32(r.gears.mode)), h.add(u32(r.gears.down_throttle)), h.add(u32(r.gears.down_lift)), h.add(u32(r.gears.down_brake));
        h.add(r.brake_heat.c0), h.add(r.brake_heat.c1), h.add(r.brake_heat.c2), h.add(r.brake_heat.c3);
        h.add(r.bumps.amp), h.add(r.bumps.freq), h.add(r.bumps.soft);
        for (size_t s = 0; s < 16; ++s) add(h, r.bumps.scaled[s]), add(h, r.bumps.mode[s]);
        const ex::SteerFeel& f = r.feel;
        for (ex::F x : {f.align_gain, f.lat_gain, f.pad_gain, f.steer_gain, f.steer_max, f.slip_gain, f.slip_max}) h.add(x);
        h.add(u32(f.use_x420)), h.add(u32(f.mode)), h.add(f.curve);
        for (size_t ch = 0; ch < 3; ++ch)
            for (size_t cl = 0; cl < 3; ++cl) {
                const ex::EffectCurves& e = r.effects.set[ch][cl];
                h.add(e.a), h.add(e.b), h.add(e.base), h.add(e.r0), h.add(e.g);
                h.add(u32(r.effects.by_lat[ch][cl])), h.add(u32(r.effects.by_slip[ch][cl]));
            }
        for (ex::F x : {r.effects.kerb_scale, r.effects.kerb_x0, r.effects.kerb_y0, r.effects.kerb_x1, r.effects.kerb_y1}) h.add(x);
        h.add(r.sound_scales);
        const ex::PadConstants& p = r.pad;
        for (ex::F x : {p.throttle_up, p.throttle_down, p.brake_up, p.brake_down, p.third_up, p.third_down, p.k1, p.k2, p.k3,
                        p.wheel_slew, p.analog_jump})
            h.add(x);
        h.add(p.wheel_lock), h.add(p.rate_out), h.add(p.rate_back), h.add(p.rate), h.add(p.wheel_throttle), h.add(p.wheel_brake);
        h.add(p.surface_class);
        for (ex::F x : {b.slow, b.slow_start, b.slow_full, b.fast, b.fast_start, b.fast_full, b.global}) h.add(x);
    }
    return h.h;
}

} // namespace

TEST(race_inputs_platform_hash) { CHECK(race_inputs_hash() == 0xa4bcb16f366249ecull); }

// A pedal curve from its bytes, as the original builds the steering wheel's throttle curve from type 1, 50.
TEST(race_inputs_pedal_curve) {
    const ex::PedalCurve k = ex::pedal_curve_record(1, 50);
    CHECK(k.type == 1 && k.a.bits == 0xBF000001 && k.b.bits == 0xBE000001 && k.c.bits == 0x3EFFFFFF);
    const ex::PedalCurve none = ex::pedal_curve_record(3, 50);
    CHECK(none.type == 3 && none.a.bits == 0 && none.b.bits == 0 && none.c.bits == 0);
}

// A program none of whose routines or tables are recognised: no exception, every table and rule reported, not complete.
TEST(race_inputs_unknown_program) {
    std::mt19937 rng(1);
    std::vector<u8> img(0x300000);
    for (u8& c : img) c = u8(rng() % 4 == 0 ? 0 : rng());
    bool threw = false;
    ex::ProgramData d;
    try {
        d = ex::ProgramData::load(ByteSpan(img.data(), img.size()), 0x00100000);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(!threw && !d.known());
    // GT3's values serve every table (none of this image's matches)
    for (int i = 0; i < ex::ProgramData::kTables; ++i) CHECK(!d.table_matched(T(i)) && d.table_release(T(i)) == ex::Program::NtscU);
    CHECK(d.b(T::Static, 0x0B) == ex::gt3_table(T::Static)[0x0B]);
    size_t missing = 0, rules = 0;
    for (const std::string& s : d.issues()) {
        if (s.find(": not found") != std::string::npos) ++missing;
        if (s.find("NTSC-U's rules") != std::string::npos) ++rules;
    }
    CHECK(missing + 4 >= size_t(ex::ProgramData::kTables) && rules == size_t(ex::kRules)); // (a few tables by their shape)
    for (int r = 0; r < ex::kRules; ++r) CHECK(!d.recognised(ex::Rule(r)) && d.behaviour().rule(ex::Rule(r)) == ex::Program::NtscU);
}

// A synthetic program's tables by id, its release's rules.
TEST(race_inputs_program_versions) {
    for (int v = 0; v < ex::kPrograms; ++v) {
        const auto p = ex::Program(v);
        ex::ProgramData d = ex::ProgramData::synthetic(p);
        for (u32 i = 0; i < ex::ProgramData::size(T::Static); ++i) d.poke(T::Static, i, u8(i + v));
        CHECK(d.program() == p && d.behaviour() == (ex::Behaviour{p, 60}));
        CHECK(d.b(T::Static, 5) == u8(5 + v) && d.h(T::Static, 4) == u16((4 + v) | (5 + v) << 8));
    }
}

// The behaviour: the disc's release's rules at 60 steps a second by default; NTSC-U's rules or the release's whole own
// behaviour (PAL at 50) on request, nothing else; at 60 steps a second a PAL program's HUD message times (50 a second) read at
// 60 a second.
TEST(race_inputs_program_behaviour) {
    const auto pal = ex::Program::Pal;
    ex::ProgramData d = ex::ProgramData::synthetic(pal);
    put32(d, T::Messages, 8 * 6 + 4, 100), put32(d, T::Messages, 8 * 13 + 4, 0xFF);
    CHECK(d.behaviour() == (ex::Behaviour{pal, 60})); // the release's rules at 60 steps a second
    CHECK(ex::hud_messages(d)[6] == 120 && ex::hud_messages(d)[13] == 0xFF && ex::hud_messages(d)[0] == 0);
    d.set_behaviour(ex::release_behaviour(pal));
    CHECK(ex::hud_messages(d)[6] == 100 && ex::hud_messages(d)[13] == 0xFF);
    CHECK(ex::steps_per_second(d.behaviour()) == 50 && ex::steps_per_second(ex::release_behaviour(ex::Program::NtscJ)) == 60);
    d.set_behaviour({ex::Program::NtscU, 60});
    CHECK(ex::hud_messages(d)[6] == 120);
    bool refused = false;
    try {
        d.set_behaviour({ex::Program::NtscJ, 60});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    bool refused_rate = false;
    try {
        d.set_behaviour({ex::Program::NtscU, 50});
    } catch (const std::invalid_argument&) {
        refused_rate = true;
    }
    CHECK(refused && refused_rate && d.behaviour() == (ex::Behaviour{ex::Program::NtscU, 60}));
}

// The ranking and the gap matrices on hand-made races: more laps (less the lap base) ahead; at equal
// laps a car not finished ahead of a finished one (the program's order), else the greater progress, two finished cars by the
// earlier crossing; free practice by the smaller best lap. The matrices: car i behind car j, the lap taken over when laps
// and metres disagree, the laps clamped to -127 .. 127.
TEST(race_flow_ranking_and_gaps) {
    std::vector<ex::Car> cars(4);
    auto set = [&](size_t i, s16 laps, u8 base, float progress, u8 finished, u32 crossed, u32 best) {
        ex::Car& c = cars[i];
        c.laps = laps, c.lap_base = base, c.f3e4 = ex::F(progress), c.finished = finished, c.x530 = crossed, c.best_lap = best;
    };
    set(0, 2, 0, 100.0f, 0, 0, 70000);
    set(1, 3, 1, 500.0f, 0, 0, 65000);
    set(2, 3, 0, 10.0f, 1, 900, 80000);
    set(3, 3, 0, 20.0f, 1, 800, 60000);
    ex::RaceOrder order{};
    ex::rank_reset(cars, order);
    ex::race_ranking(cars, order, ex::Conductor::SinglePlayer);
    CHECK(order[0] == 3 && order[1] == 2 && order[2] == 1 && order[3] == 0 && order[4] == -1);
    CHECK(cars[3].rank == 1 && cars[2].rank == 2 && cars[1].rank == 3 && cars[0].rank == 4);
    ex::race_ranking(cars, order, ex::Conductor::FreePractice);
    CHECK(order[0] == 3 && order[1] == 1 && order[2] == 0 && order[3] == 2);
    CHECK(cars[3].rank == 1 && cars[1].rank == 2 && cars[0].rank == 3 && cars[2].rank == 4);

    ex::GapMatrix m;
    ex::gap_matrix(cars, ex::F(1000.0f), m);
    CHECK(m.laps[0][1] == 0 && m.gap[0][1].bits == ex::F(400.0f).bits);  // level on laps, 400 m behind
    CHECK(m.laps[0][2] == 0 && m.gap[0][2].bits == ex::F(910.0f).bits);  // a lap up but behind on the road: 910 m
    CHECK(m.laps[2][0] == 0 && m.gap[2][0].bits == ex::F(-910.0f).bits);
    CHECK(m.laps[0][0] == 0 && m.gap[0][0].bits == 0);                    // the diagonal untouched

    std::vector<ex::Car> two(2);
    two[0].laps = 300, two[0].f3e4 = ex::F(50.0f);
    two[1].laps = 0, two[1].f3e4 = ex::F(10.0f);
    ex::gap_matrix(two, ex::F(1000.0f), m);
    CHECK(m.laps[1][0] == 127 && m.laps[0][1] == -127);
    // equal laps: the one not finished ahead of the finished one
    two[0].laps = 3, two[0].finished = 1, two[0].f3e4 = ex::F(50.0f);
    two[1].laps = 3, two[1].finished = 0, two[1].f3e4 = ex::F(40.0f);
    ex::RaceOrder o2{};
    ex::rank_reset(two, o2);
    ex::race_ranking(two, o2, ex::Conductor::SinglePlayer);
    CHECK(o2[0] == 1 && o2[1] == 0 && two[1].rank == 1);
}
