// The car's parameter block P (car + 0x10) built by sim::exact (params_build.h) against the original's builders, byte for byte
// (model_car_params_exact).
//
// 1. Every car of table 30, every variant an event races (table 31), every arcade car (table 35), then every car again with
//    random upgrade parts (stock settings) and with random parts and random settings: a race entry from the user's disc
//    (CarEquipments 0x2312B0 / 0x231BC0 / 0x231DB0, part overrides re-derived by the set-part functions as race_setup's
//    build_entry does), then the original's spec builder 0x1DA308 and converter 0x1DBFD8 with the race's arguments (a cleared
//    record, control source 2, the rule block, the entry's +0x2104 block) and a spread of the others (control source 0, rule
//    bytes, condition words, mode 2 and the tyre override of 0x1DA308, the physics globals as the program image has them and as
//    0x1DAE70 recomputes them, a record with random previous content), then ours on the same inputs. Compared: the equipment
//    ours derives from the database (stock_equipment) against the original's CarEquipments, the spec bytes (from the original's
//    equipment and from ours), every byte of P (car + 0x10 .. car + 0xEC0) and the spec bytes the converter edits.
// 2. Whole race initialisations (race_setup.h: the conductor's 0x1F3628 for CPU races): at the race's own calls of 0x1DA308
//    (in 0x1F3628) and 0x1DBFD8 (in 0x1DDAD0) ours runs from the call's inputs and must give the call's spec and P, and
//    race_params from the race's equipment and arguments (its own physics globals) must give the same P.
//
// 1b. The converter alone on specs with random values in the fields it reads (paths the database never takes).
//
// Options: GT3R_PARAMS_ONLY=<label substring>, GT3R_PARAMS_RANDOM=<random runs per car, default 1>, GT3R_PARAMS_PERTURB=<runs of
// 1b, default 600>, GT3R_PARAMS_EVENTS=<event
// labels for part 2, "-" for none>, GT3R_PARAMS_INIT_MAP=1 (part 2 also lists, per function the race's initialisation calls for
// a car, the record bytes it changes).

#include "harness.h"
#include "ps2float.h"
#include "race_setup.h"
#include "start_setup.h"

#include "disc/disc.h"
#include "disc/volume.h"
#include "formats/db/gt_db.h"
#include "game/data/car_database.h"
#include "sim/car_spec.h"
#include "sim/exact/params_build.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::diff;

namespace {
namespace rs = race_setup;
namespace ex = sim::exact;

constexpr Pa kSpecBuild = 0x001DA308, kConverter = 0x001DBFD8, kPhysicsGlobals = 0x001DAE70, kInitName = 0x0033DB38;
constexpr Pa kSpecSite = 0x001F377C, kConvertSite = 0x001DDC10; // the race's calls (in 0x1F3628 and 0x1DDAD0)
constexpr const Pa (&kSetPart)[10] = rs::kSetParts;
constexpr Pa kAirDensity = 0x00352920, kRollTable = 0x002D2CE0, kRollScale = 0x002D2CF0;

std::string hex(u64 v) {
    char b[24];
    std::snprintf(b, sizeof b, "%llX", (unsigned long long)v);
    return b;
}

ex::Equipment read_eq(Ee& ee, u32 eq) {
    ex::Equipment e;
    e.car = ee.r64(eq);
    for (size_t i = 0; i < 27; ++i)
        if (u32 slot = rs::eq_slot_for(i)) e.part[size_t(data::kCarPartTables[i])] = ee.r64(eq + slot);
    for (u32 k = 0; k < e.setting.size(); ++k) e.setting[k] = s32(ee.r32(eq + 0xF0 + 4 * k));
    return e;
}

// The converter's inputs from the interpreter at a call: the record base, control source, rule block, condition block and the
// physics globals.
// The program's tables (ProgramData from the disc's CORE.GT3), set when the test starts.
const ex::ProgramData* g_data = nullptr;

void read_inputs(Ee& ee, u32 p, s32 control, u32 rules, u32 cond, ex::ConvertInputs& in) {
    in.data = g_data;
    in.base = p;
    in.control = control;
    for (u32 k = 0; k < 12; ++k) in.rules[k] = rules ? ee.r8(rules + k) : 0;
    for (u32 k = 0; k < 8; ++k) in.condition[k] = cond ? ee.r32(cond + 4 * k) : 0;
    in.air_density = ee::F::raw(ee.r32(kAirDensity));
    in.roll_n = s16(ee.r16(kRollTable));
    const u32 rx = ee.r32(kRollTable + 4), ry = ee.r32(kRollTable + 8);
    for (u32 k = 0; k < 8; ++k) in.roll_x[k] = ee::F::raw(ee.r32(rx + 4 * k)), in.roll_y[k] = ee::F::raw(ee.r32(ry + 4 * k));
    in.roll_scale = ee::F::raw(ee.r32(kRollScale));
}

std::string first_diff(const u8* a, const u8* b, size_t n, const char* what) {
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) {
            const size_t w = i & ~size_t(3);
            u32 wa = 0, wb = 0;
            for (size_t k = 0; k < 4 && w + k < n; ++k) wa |= u32(a[w + k]) << (8 * k), wb |= u32(b[w + k]) << (8 * k);
            char s[128];
            std::snprintf(s, sizeof s, "%s +0x%zX: ours %08X, original %08X", what, w, wa, wb);
            return s;
        }
    return {};
}

// The first difference in the words the spec builder reads (not EQ+0x118 front torque, +0x1AC the stock final drive, +0x1B4, nor
// the gear words past the gear count, which the builder overwrites with -1).
std::string eq_diff(const ex::Equipment& a, const ex::Equipment& b, const data::CarDatabase& db) {
    if (a.car != b.car) return "car id " + hex(a.car) + " vs " + hex(b.car);
    for (size_t t = 0; t < a.part.size(); ++t)
        if (a.part[t] != b.part[t]) return std::string("part ") + data::table_name(data::TableId(t)) + " " + hex(a.part[t]) + " vs " + hex(b.part[t]);
    const ByteSpan* g = db.record(data::TableId::Gear, b.part[size_t(data::TableId::Gear)]);
    const u32 gears = g && g->size() > 0x11 ? (*g)[0x11] : 7;
    for (size_t k = 0; k < a.setting.size(); ++k) {
        const u32 off = 0xF0 + 4 * u32(k);
        if (off == 0x118 || off == 0x1AC || off == 0x1B4 || (off < 0x110 && k > gears)) continue;
        if (a.setting[k] != b.setting[k]) return "setting EQ+0x" + hex(off) + " " + std::to_string(a.setting[k]) + " vs " + std::to_string(b.setting[k]);
    }
    return {};
}

// Random settings words inside (and a little outside) the ranges the part records give [physics §6.6].
void random_settings(Rng& rng, Ee& ee, u32 eq, const ex::Equipment& e, const data::CarDatabase& db) {
    auto rec = [&](data::TableId t) { return db.record(t, e.part[size_t(t)]); };
    auto put = [&](u32 off, s64 v) { ee.w32(eq + off, u32(s32(v))); };
    auto idx = [&](int steps) { return rng.range(0, std::max(steps, 1) + 1); }; // 0 and steps + 1: just outside
    if (const ByteSpan* g = rec(data::TableId::Gear); g && g->size() > 0x2B) {
        put(0x110, rng.range(rd16(*g, 0x26) ? rd16(*g, 0x26) : 2500, rd16(*g, 0x24) > rd16(*g, 0x26) ? rd16(*g, 0x24) : 5500));
        for (u32 i = 0; i < 8; ++i)
            if (s32(ee.r32(eq + 0xF0 + 4 * i)) > 0) put(0xF0 + 4 * i, s32(ee.r32(eq + 0xF0 + 4 * i)) * rng.range(90, 110) / 100);
    }
    if (const ByteSpan* b = rec(data::TableId::BrakeController); b && b->size() > 0x15) put(0x11C, idx((*b)[0x11])), put(0x120, idx((*b)[0x15]));
    put(0x124, rng.range(0, 100)), put(0x128, rng.range(0, 100));
    for (u32 i = 0; i < 6; ++i)
        if (u32 v = ee.r32(eq + 0x12C + 4 * i)) put(0x12C + 4 * i, std::clamp<s64>(s64(v) * rng.range(75, 125) / 100, 1, 255));
    put(0x144, rng.range(0, 120)), put(0x148, rng.range(0, 100));
    put(0x14C, rng.range(60, 200)), put(0x150, rng.range(60, 200));
    put(0x154, rng.range(100, 156)), put(0x158, rng.range(100, 156));
    put(0x15C, rng.range(10, 250)), put(0x160, rng.range(10, 250));
    if (rng.chance(0.3)) put(0x164, rng.range(60, 100)), put(0x168, rng.range(60, 100));
    if (const ByteSpan* su = rec(data::TableId::Suspension); su && su->size() >= 0x7C) {
        const int f = (*su)[0x2B], r1 = (*su)[0x39], r2 = (*su)[0x40];
        for (u32 i = 0; i < 2; ++i) put(0x16C + 4 * i, idx(std::min(f, int((*su)[0x32]))));
        for (u32 i = 0; i < 2; ++i) put(0x17C + 4 * i, idx(r1)), put(0x184 + 4 * i, idx(r2));
        put(0x18C, idx((*su)[0x47])), put(0x190, idx((*su)[0x4B]));
    }
    for (u32 i = 0; i < 6; ++i) put(0x194 + 4 * i, rng.range(0, 60));
    if (const ByteSpan* t = rec(data::TableId::Tcsc); t && t->size() > 0x12) put(0x1B0, idx((*t)[0x12]));
    if (const ByteSpan* a = rec(data::TableId::Ascc); a && a->size() > 0x26)
        put(0x1BC, idx((*a)[0x11])), put(0x1C0, idx((*a)[0x18])), put(0x1C4, idx((*a)[0x1E])), put(0x1C8, idx((*a)[0x23]));
    if (rng.chance(0.2)) put(0x1B8, rng.range(800, 1200)); // the torque-scale override word
}

// One run's inputs (besides the entry) and the original's outputs.
struct Run {
    std::string label, base;
    std::array<u64, 27> parts{}; // Car::parts order
    bool tune = false;           // random settings words written after set-car
    int flag = 0, mode = 0;
    bool tire_override = false, recompute_globals = false;
    s32 control = 2;
    std::array<u8, 12> rules{};
    std::array<u32, 8> condition{};
    bool has_condition = true;
    u64 prior_seed = 0;          // nonzero: the record starts with random bytes
    float air_density = 1.2f;
    // the original's side
    ex::Equipment eq_before, eq_after; // CarEquipments before / after 0x1DA308
    ex::Spec spec{}, spec_after{};     // after 0x1DA308 / after 0x1DBFD8
    ex::ConvertInputs in;
    ex::ParamImage prior{}, p{};
    ex::ConvertInputs globals;         // the globals 0x1DAE70 left (recompute_globals)
};

bool run_original(Ee& ee, const Bytes& gtar_bytes, const data::CarDatabase& db, Run& r, Rng* tune, std::string& err) {
    try {
        ee.reset();
        ee.clear_hle();
        ee.clear_probes();
        ee.hle_libc_defaults();
        ee.hle(rs::kClassCheck, [](Ee& x) { x.set_v0(1); }, "class check");
        ee.wf(kAirDensity, r.air_density);
        const u32 gtar = ee.alloc_bytes(gtar_bytes);
        const u32 entry = ee.alloc(rs::kEntrySize, 64), eq = entry + rs::kEntryEq;
        ee.call(rs::kEqCtor, {Arg::ptr(eq + 0x1D0)}, 100000);
        rs::set_database(ee, eq + 0x1D0, gtar);
        ee.call(rs::kEqInit, {Arg::ptr(eq)}, 1000000);
        if (ee.call(rs::kEqSetCar, {Arg::ptr(eq), Arg::integer(s64(db::id_hash(r.base)))}, 10000000).v0_s32() != 1) {
            err = r.label + ": set car failed";
            return false;
        }
        const data::Car* car = db.car(r.base);
        bool changed = false;
        for (size_t i = 0; i < 27; ++i)
            if (r.parts[i] != 0 && r.parts[i] != car->parts[i])
                if (u32 slot = rs::eq_slot_for(i)) ee.w64(eq + slot, r.parts[i]), changed = true;
        if (changed)
            for (u32 fn : kSetPart) ee.call(fn, {Arg::ptr(eq)}, 100000000);
        if (tune) random_settings(*tune, ee, eq, read_eq(ee, eq), db);
        r.eq_before = read_eq(ee, eq);
        for (u32 k = 0; k < 8; ++k) ee.w32(entry + entry_off(0x2104) + 4 * k, r.condition[k]);
        const u32 rules = ee.alloc(0x40, 16);
        for (u32 k = 0; k < 12; ++k) ee.w8(rules + k, r.rules[k]);
        const u32 spec = ee.alloc(0x400, 64);
        ee.call(kSpecBuild, {Arg::ptr(spec), Arg::ptr(eq), Arg::integer(r.flag), Arg::integer(r.mode), Arg::integer(r.tire_override ? 1 : 0),
                             Arg::ptr(kInitName)},
                100'000'000);
        ee.read_bytes(spec, r.spec.data(), ex::kSpecSize);
        r.eq_after = read_eq(ee, eq);
        if (r.recompute_globals) {
            ee.call(kPhysicsGlobals, {}, 10'000'000);
            read_inputs(ee, 0, 0, 0, 0, r.globals);
        }
        const u32 rec = ee.alloc(0x1700, 64); // cleared, as 0x1EAA88 leaves it
        if (r.prior_seed) {
            Rng fill(r.prior_seed);
            for (u32 k = 0; k < 0x16DC; k += 4) ee.w32(rec + k, fill.u32_());
        }
        ee.read_bytes(rec + 0x10, r.prior.data(), ex::kParamsSize);
        const u32 cond = r.has_condition ? entry + entry_off(0x2104) : 0;
        read_inputs(ee, rec + 0x10, r.control, rules, cond, r.in);
        ee.call(kConverter, {Arg::ptr(rec + 0x10), Arg::ptr(spec), Arg::integer(r.control), Arg::integer(0), Arg::ptr(rules), Arg::ptr(cond)},
                400'000'000);
        ee.read_bytes(spec, r.spec_after.data(), ex::kSpecSize);
        ee.read_bytes(rec + 0x10, r.p.data(), ex::kParamsSize);
        return true;
    } catch (const EeTrap& t) {
        char b[160];
        std::snprintf(b, sizeof b, "%s: trap at pc=%08X: %s", r.label.c_str(), t.pc, t.what());
        err = b;
        return false;
    }
}

struct Count {
    long n = 0, bad = 0;
    std::string first;
    void add(const std::string& label, const std::string& d) {
        ++n;
        if (!d.empty() && ++bad == 1) first = label + ": " + d;
    }
    std::string line(const char* what) const {
        return std::string(what) + ": " + std::to_string(bad) + " of " + std::to_string(n) + " differ" + (first.empty() ? "" : "; first: " + first);
    }
};

// How many runs differ in each word of an image (the words most often off, with the run count), for the notes.
struct WordTally {
    std::map<size_t, long> n;
    void add(const u8* a, const u8* b, size_t size) {
        for (size_t w = 0; w + 4 <= size; w += 4)
            if (std::memcmp(a + w, b + w, 4) != 0) ++n[w];
    }
    std::string line(const char* what, size_t most = 16) const {
        std::vector<std::pair<long, size_t>> v;
        for (const auto& [w, c] : n) v.push_back({-c, w});
        std::sort(v.begin(), v.end());
        std::string s = std::string(what) + ": " + std::to_string(n.size()) + " words differ";
        for (size_t i = 0; i < v.size() && i < most; ++i) {
            char b[32];
            std::snprintf(b, sizeof b, "%s+0x%zX (%ld)", i ? ", " : "; ", v[i].second, -v[i].first);
            s += b;
        }
        return s;
    }
};

std::string ranges(const std::vector<bool>& w) {
    std::string s;
    for (size_t i = 0; i < w.size();) {
        if (!w[i]) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < w.size() && w[j]) ++j;
        s += " +0x" + hex(i) + (j - i > 1 ? "..0x" + hex(j - 1) : "");
        i = j;
    }
    return s;
}

// A spec with random values in the fields the converter reads, for the paths the database never takes (drive types and AWD
// systems, differential types, zero masses, areas, inertias, brakes and tyre sizes, the turbo shapes, torque words above 0x7FFF,
// surface classes >= 3, the tyre-block selection by spec 0x12, the steer-coupled camber). Counts stay inside their arrays.
void perturb(Rng& g, ex::Spec& s) {
    auto b = [&](u32 o, s64 lo, s64 hi) { s[o] = u8(g.range(lo, hi)); };
    auto h = [&](u32 o, s64 lo, s64 hi) {
        const u16 v = u16(g.range(lo, hi));
        s[o] = u8(v), s[o + 1] = u8(v >> 8);
    };
    auto maybe = [&](double p) { return g.chance(p); };
    if (maybe(0.5)) b(0x2C, 30, 70);
    if (maybe(0.3)) h(0x24, 2000, 3200);
    if (maybe(0.3)) h(0x26, 1300, 1800), h(0x28, 1300, 1800), b(0x68, -20, 40), b(0x69, -20, 40);
    if (maybe(0.3)) h(0x1C, 0, 2500), h(0x1E, 0, 2500), h(0x20, 1500, 2000), h(0x22, 1000, 1500);
    if (maybe(0.3)) h(0x2A, maybe(0.2) ? 0 : 600, 2000);
    for (u32 o : {0x2Eu, 0x2Fu, 0x30u, 0x31u, 0x32u, 0x33u, 0x34u, 0x35u, 0x36u, 0x37u, 0x38u, 0x39u})
        if (maybe(0.2)) b(o, maybe(0.3) ? 0 : 1, 255);
    if (maybe(0.3)) h(0x16, 0, maybe(0.3) ? 0 : 30000);
    for (u32 o = 0x3A; o <= 0x57; ++o)
        if (maybe(0.15)) b(o, 0, 255);
    for (u32 o = 0x5A; o <= 0x67; ++o)
        if (maybe(0.15)) b(o, 0, 255);
    if (maybe(0.3)) h(0x6A, 0, 600), h(0x6C, 0, 3000);
    if (maybe(0.3)) b(0x6E, 5, 120), b(0x6F, 0, 110), b(0x70, 0, 100);
    if (maybe(0.3)) {
        const s64 n = g.range(2, 16);
        s[0x71] = u8(n);
        s64 rpm = g.range(5, 20);
        for (u32 i = 0; i < 16; ++i) {
            s[0x72 + i] = u8(i < n ? std::min<s64>(rpm, 255) : 0xFF);
            rpm += g.range(1, 12);
            h(0x82 + 2 * i, 0, maybe(0.1) ? 0xFFFF : 6000);
        }
        if (maybe(0.3)) s[0x82 + 2 * u32(n - 1)] = 0, s[0x83 + 2 * u32(n - 1)] = 0;
    }
    if (maybe(0.3)) h(0xA2, 0, 1200), h(0xA4, 0, 1300), b(0xA6, 0, 130);
    if (maybe(0.3))
        for (u32 o = 0xA7; o <= 0xAF; ++o) b(o, o == 0xA8 || o == 0xA9 ? 0 : (maybe(0.2) ? 0 : 1), o == 0xA8 || o == 0xA9 ? 4 : 150);
    if (maybe(0.4)) b(0xB2, 0, 6), b(0xB3, 0, 8), b(0xB4, 0, 100), b(0xB5, 0, 40);
    if (maybe(0.3)) {
        b(0xC8, 1, 7);
        for (u32 i = 0; i < 8; ++i) h(0xB6 + 2 * i, 500, 4500);
        h(0xC6, 2500, 5500);
    }
    if (maybe(0.2)) b(0xC9, 0, 80), b(0xCD, 0, 50);
    if (maybe(0.3)) h(0xCE, 0, 500), h(0xD0, 0, 500), h(0xD2, maybe(0.2) ? 0 : 1, 500), h(0xD4, maybe(0.2) ? 0 : 1, 500);
    static const char kTypes[] = "frnmhvytx";
    if (maybe(0.4)) s[0xD6] = u8(kTypes[g.range(0, 8)]), s[0xD7] = u8(kTypes[g.range(0, 8)]);
    for (u32 o = 0xD8; o <= 0xE0; ++o)
        if (maybe(0.2)) b(o, 0, 255);
    for (u32 o = 0x109; o <= 0x117; ++o)
        if (maybe(0.2)) b(o, 0, 255);
    if (maybe(0.1)) b(0x18, 1, 90);
    for (u32 o = 0x11A; o <= 0x11F; ++o)
        if (maybe(0.15)) b(o, 0, 80);
    for (u32 o = 0x122; o <= 0x131; ++o)
        if (maybe(0.15)) b(o, 0, 255);
    for (u32 o = 0x132; o <= 0x141; ++o)
        if (maybe(0.15)) b(o, 0, 4);
    if (maybe(0.15)) s[0x12] = 0;
    for (u32 k = 0; k < 6; ++k) { // tyre blocks (the counts keep their own values)
        const u32 o = 0x142 + 0x70 * k;
        for (u32 i = 0; i < 0x70; ++i)
            if (i != 0x02 && i != 0x13 && i != 0x26 && i != 0x39 && i != 0x5E && maybe(0.05)) b(o + i, 0, 255);
    }
}

} // namespace

DIFF_TEST_ALL(model_car_params_exact, "spec builder 0x1DA308 + converter 0x1DBFD8 (P = car+0x10) vs sim::exact, byte for byte, every car") {
    data::CarDatabase db;
    if (!db.load(ctx.disc->volume())) {
        ctx.check(false, "database load: " + db.error());
        return;
    }
    const Volume::Entry* ge = ctx.disc->volume().find(db_path(ctx.disc->volume()));
    if (!ge) {
        ctx.check(false, db_path(ctx.disc->volume()) + " not found");
        return;
    }
    const Bytes gtar = ctx.disc->volume().read(*ge);
    std::string derr;
    g_data = start_setup::program_data(ctx, derr);
    if (!g_data) {
        ctx.check(false, derr);
        return;
    }
    const char* only = std::getenv("GT3R_PARAMS_ONLY");
    const int random_runs = std::getenv("GT3R_PARAMS_RANDOM") ? std::atoi(std::getenv("GT3R_PARAMS_RANDOM")) : 1;

    // ---- 1. the builders on every car -----------------------------------------------------------------------------------------
    std::vector<Run> jobs;
    auto add = [&](const std::string& label, const std::string& base, const std::array<u64, 27>& parts, bool tune) {
        Run r;
        r.label = label, r.base = base, r.parts = parts, r.tune = tune;
        jobs.push_back(std::move(r));
    };
    for (const data::Car& c : db.cars()) add(c.label, c.label, c.parts, false);
    auto add_variant = [&](const data::CarVariant& v) {
        const data::Car* b = db.car(v.car);
        if (!b) return;
        std::array<u64, 27> parts = b->parts;
        for (size_t i = 0; i < 27; ++i)
            if (v.parts[i]) parts[i] = v.parts[i];
        add(v.label, v.car, parts, false);
    };
    std::set<std::string> raced;
    for (const data::Event& ev : db.events())
        for (const std::string& e : ev.entries)
            if (const data::CarVariant* v = db.variant(e); v && raced.insert(e).second) add_variant(*v);
    for (const data::CarVariant& v : db.arcade_cars()) add_variant(v);
    for (int k = 0; k < random_runs; ++k)
        for (const bool tune : {false, true})
            for (const data::Car& c : db.cars()) {
                std::array<u64, 27> parts = c.parts;
                for (size_t i = 0; i < 27; ++i) {
                    const std::vector<data::PartInfo> opts = db.parts_for(data::TableId(data::kCarPartTables[i]), c.label);
                    if (!opts.empty() && ctx.rng.chance(0.5)) parts[i] = db::id_hash(opts[size_t(ctx.rng.range(0, s64(opts.size()) - 1))].label);
                }
                add(c.label + (tune ? " random parts + settings #" : " random parts #") + std::to_string(k), c.label, parts, tune);
            }
    Count eq_db, spec_eq, spec_db, conv, edits, globals;
    WordTally conv_words;
    long overridden = 0; // runs where 0x1DA308 replaced the tyre parts
    size_t v = 0;
    for (Run& r : jobs) {
        if (only && r.label.find(only) == std::string::npos) continue;
        const size_t k = v++;
        Rng tune_rng(0x5EED0000u + k);
        // The race's arguments for a CPU entry (control source 2, cleared record) with a spread of the other inputs.
        r.control = k % 4 == 3 ? 0 : 2;
        r.mode = k % 7 == 5 ? 2 : 0;
        r.flag = int(k % 3 == 1);
        r.tire_override = k % 11 == 7;
        if (k % 2)
            r.rules = {0, 0, 0, u8(k % 5 == 1 ? 0 : 20 + k % 200), u8(80 + k % 40), u8(k % 120), u8(90 + k % 20), 100, u8(70 + k % 50), u8(k % 9), 100, 95};
        if (k % 3 == 2) { // the condition words across their limits (0x2D2B00: -300000 / 0 / 500000 / 600000 and -100000 / 0)
            Rng c(0xC0D0u + k);
            r.condition[1] = ps2f::from_host(c.uniform(-400000.0f, 700000.0f));
            r.condition[2] = ps2f::from_host(c.uniform(-150000.0f, 50000.0f));
        }
        r.has_condition = k % 13 != 3;
        r.recompute_globals = k % 5 == 2;
        if (k % 8 == 6) r.air_density = 1.0f + float(k % 40) * 0.01f;
        if (k % 9 == 4) r.prior_seed = 0x9E3779B9u + k;
        std::string err;
        if (!run_original(ctx.ee, gtar, db, r, r.tune ? &tune_rng : nullptr, err)) {
            ctx.check(false, err);
            continue;
        }
        if (!r.tune) {
            const ex::Equipment ours = ex::stock_equipment(db, r.base, r.parts);
            eq_db.add(r.label, eq_diff(ours, r.eq_before, db));
            ex::Equipment e = ours;
            ex::Spec s{};
            ex::build_spec(db, *g_data, e, r.flag, r.mode, r.tire_override, s);
            spec_db.add(r.label, first_diff(s.data(), r.spec.data(), ex::kSpecSize, "spec"));
        }
        ex::Equipment e = r.eq_before;
        ex::Spec s{};
        ex::build_spec(db, *g_data, e, r.flag, r.mode, r.tire_override, s);
        std::string d = first_diff(s.data(), r.spec.data(), ex::kSpecSize, "spec");
        if (d.empty()) d = eq_diff(e, r.eq_after, db);
        spec_eq.add(r.label, d);
        overridden += r.eq_after.part != r.eq_before.part;
        if (r.recompute_globals) {
            ex::ConvertInputs g;
            g.data = g_data;
            g.physics_globals();
            std::string gd;
            if (g.air_density.bits != r.globals.air_density.bits) gd = "air density";
            if (g.roll_n != r.globals.roll_n || g.roll_scale.bits != r.globals.roll_scale.bits) gd = "rolling-resistance count / scale";
            for (size_t i = 0; i < 8; ++i)
                if (g.roll_x[i].bits != r.globals.roll_x[i].bits || g.roll_y[i].bits != r.globals.roll_y[i].bits) gd = "rolling-resistance point " + std::to_string(i);
            globals.add(r.label, gd);
        }
        ex::Spec cs = r.spec;
        ex::ParamImage p = r.prior;
        ex::convert_params(cs, r.in, p);
        conv.add(r.label, first_diff(p.data(), r.p.data(), ex::kParamsSize, "P"));
        conv_words.add(p.data(), r.p.data(), ex::kParamsSize);
        edits.add(r.label, first_diff(cs.data(), r.spec_after.data(), ex::kSpecSize, "spec after the converter"));
        ctx.sample();
    }
    ctx.note(std::to_string(v) + " runs: cars, raced variants, arcade cars, random parts, random parts + settings");
    ctx.note(eq_db.line("equipment from the database (stock_equipment)"));
    ctx.note(spec_eq.line("spec (0x1DA308) from the original's equipment"));
    ctx.note(spec_db.line("spec from our equipment"));
    ctx.note("the tyre override replaced the tyre parts in " + std::to_string(overridden) + " runs");
    ctx.note(conv.line("P (0x1DBFD8)"));
    if (conv.bad) ctx.note(conv_words.line("P words differing (runs)"));
    ctx.note(edits.line("spec bytes the converter edits"));
    ctx.note(globals.line("physics globals (0x1DAE70)"));
    for (const Count* c : {&eq_db, &spec_eq, &spec_db, &conv, &edits, &globals}) ctx.check(c->bad == 0, c->first);
    ctx.check(conv.n > 0, "no runs");

    // ---- 1b. the converter on perturbed specs ---------------------------------------------------------------------------------
    Count perturbed;
    const int nperturb = std::getenv("GT3R_PARAMS_PERTURB") ? std::atoi(std::getenv("GT3R_PARAMS_PERTURB")) : 600;
    for (int k = 0; k < nperturb && !jobs.empty() && !only; ++k) {
        const Run& base = jobs[size_t(k * 7919) % jobs.size()];
        ex::Spec spec = base.spec;
        Rng g(0xBADC0DEu + u64(k));
        perturb(g, spec);
        Ee& ee = ctx.ee;
        ex::Spec theirs_s{};
        ex::ParamImage theirs_p{};
        ex::ConvertInputs in;
        try {
            ee.reset();
            ee.clear_hle();
            ee.clear_probes();
            ee.hle_libc_defaults();
            ee.wf(kAirDensity, 1.2f);
            if (k % 3 == 1) ee.call(kPhysicsGlobals, {}, 10'000'000);
            const u32 rules = ee.alloc(0x40, 16), cond = ee.alloc(0x40, 16), sp = ee.alloc(0x400, 64), rec = ee.alloc(0x1700, 64);
            for (u32 i = 0; i < 12; ++i) ee.w8(rules + i, base.rules[i]);
            for (u32 i = 0; i < 8; ++i) ee.w32(cond + 4 * i, base.condition[i]);
            ee.write_bytes(sp, ByteSpan(spec.data(), spec.size()));
            const s32 control = k % 4 == 3 ? 0 : 2;
            read_inputs(ee, rec + 0x10, control, rules, cond, in);
            ee.call(kConverter, {Arg::ptr(rec + 0x10), Arg::ptr(sp), Arg::integer(control), Arg::integer(0), Arg::ptr(rules), Arg::ptr(cond)},
                    400'000'000);
            ee.read_bytes(sp, theirs_s.data(), ex::kSpecSize);
            ee.read_bytes(rec + 0x10, theirs_p.data(), ex::kParamsSize);
        } catch (const EeTrap& t) {
            char b[160];
            std::snprintf(b, sizeof b, "perturbed #%d: trap at pc=%08X: %s", k, t.pc, t.what());
            ctx.check(false, b);
            continue;
        }
        ex::ParamImage p{};
        ex::convert_params(spec, in, p);
        std::string d = first_diff(p.data(), theirs_p.data(), ex::kParamsSize, "P");
        if (d.empty()) d = first_diff(spec.data(), theirs_s.data(), ex::kSpecSize, "spec after the converter");
        perturbed.add(base.label + " perturbed #" + std::to_string(k), d);
        ctx.sample();
    }
    ctx.note(perturbed.line("P from perturbed specs"));
    ctx.check(perturbed.bad == 0, perturbed.first);

    // ---- 2. the race's own calls ----------------------------------------------------------------------------------------------
    std::string events = std::getenv("GT3R_PARAMS_EVENTS") ? std::getenv("GT3R_PARAMS_EVENTS") : "Gsunday_e_0003,Gevolution_e_0001,Ggtworld_n_1010,Dr_tahitimaze_0001";
    // A database without GT3's events (the 2002 builds of Gran Turismo Concept): its own first races on four courses.
    if (!std::getenv("GT3R_PARAMS_EVENTS") && !db.event("Gsunday_e_0003")) {
        events.clear();
        std::set<std::string> courses;
        for (const data::Event& e : db.events()) {
            if (e.entries.size() < 2 || !ctx.disc->volume().find(course_path(ctx.disc->volume(), e.course))) continue;
            if (courses.size() < 4 && courses.insert(e.course).second) events += (events.empty() ? "" : ",") + e.label;
        }
        ctx.note("the database has none of GT3's events; races: " + events);
    }
    const bool map = std::getenv("GT3R_PARAMS_INIT_MAP") != nullptr;
    Count race_spec, race_conv, race_edits, race_whole;
    std::map<std::string, std::vector<bool>> written; // init map: callee -> record bytes changed
    for (size_t pos = 0; events != "-" && pos < events.size();) {
        size_t end = events.find(',', pos);
        if (end == std::string::npos) end = events.size();
        const std::string label = events.substr(pos, end - pos);
        pos = end + 1;
        const data::Event* ev = db.event(label);
        const Volume::Entry* ce = ev ? ctx.disc->volume().find(course_path(ctx.disc->volume(), ev->course)) : nullptr;
        if (!ce) {
            ctx.check(false, label + ": no such event or course");
            continue;
        }
        const Bytes course = ctx.disc->volume().read(*ce);
        std::vector<std::string> labels;
        for (const std::string& e : ev->entries)
            if (!e.empty() && e != "-" && labels.size() < 6) labels.push_back(e);
        struct SpecCall {
            u32 spec = 0, eq_addr = 0;
            s32 flag = 0, mode = 0;
            bool override = false;
            ex::Equipment eq;
        } sc;
        struct ConvCall {
            u32 p = 0, spec = 0;
            ex::Spec spec_before{};
            ex::ParamImage prior{};
            ex::ConvertInputs in;
            bool asm_on = true, tcs_on = true;
        } cc;
        int car_index = 0;
        u32 car = 0;
        std::map<u32, std::vector<u8>> before; // init map: call site -> record before
        auto install = [&](Ee& ee, const rs::OrigRace&) {
            ee.probe(kSpecBuild, [&](Ee& e) {
                if (u32(e.gpr[31].lo) != kSpecSite + 8) return;
                sc.spec = u32(e.a(0)), sc.flag = s32(e.a(2)), sc.mode = s32(e.a(3)), sc.override = e.gpr[8].lo != 0;
                sc.eq_addr = u32(e.a(1));
                sc.eq = read_eq(e, sc.eq_addr);
            });
            ee.probe(kConverter, [&](Ee& e) {
                if (u32(e.gpr[31].lo) != kConvertSite + 8) return;
                cc.p = u32(e.a(0)), cc.spec = u32(e.a(1));
                e.read_bytes(cc.spec, cc.spec_before.data(), ex::kSpecSize);
                e.read_bytes(cc.p, cc.prior.data(), ex::kParamsSize);
                read_inputs(e, cc.p, s32(e.a(2)), u32(e.gpr[8].lo), u32(e.gpr[9].lo), cc.in);
                const u32 entry = sc.eq_addr - rs::kEntryEq; // the entry whose spec was built last (this car's)
                cc.asm_on = e.r32(entry + entry_off(0x20F0)) != 0, cc.tcs_on = e.r32(entry + entry_off(0x20F4)) != 0;
            });
            // The call sites in the race's initialisation: for the map, the callee of each (the car: 0x1DDAD0's a0, or the
            // loop's car i of 0x1F3628).
            static const std::vector<std::pair<Pa, const char*>> kSites = {
                {0x1F3720, "0x1EAA88 car init (clear, conductor / race-info links, driver block)"},
                {0x1F3844, "0x1DDAD0 whole car set-up"},
                {0x1F385C, "0x234EC0, then car+0x16D0 / +0x16D1"},
                {0x1F3890, "car+0x1374 = 0"},
                {0x1F3898, "0x1EADB8 phase 1 (previous-tick slots)"},
                {0x1F38B0, "0x1EAFC0"},
                {0x1DDB4C, "0x1D9AF0 (clears car+0x137C)"},
                {0x1DDB60, "0x1EA8C0"},
                {0x1DDB98, "0x1E4468"},
                {0x1DDBEC, "0x1DD0C8 (F+0x3E9 control source, F+0x534)"},
                {0x1DDC10, "0x1DBFD8 converter (P)"},
                {0x1DDC30, "0x1F5FD8 driver set"},
                {0x1DDC48, "0x1F6380 event overrides"},
                {0x1DDC58, "0x1F6540"},
                {0x1DDC64, "0x1F6550 driver downforce term"},
                {0x1DDC74, "0x1DD0D8 (F+0x461)"},
                {0x1DDCA4, "0x1F2DB0 grid placement"},
                {0x1DDCF8, "0x1F9E30"},
                {0x1DDD78, "0x1DD210 pose, wheels on the ground"},
                {0x1DDD80, "0x1E0740"},
                {0x1DDD88, "0x1DD5F0 standing start"},
                {0x1DDD90, "0x1DD110"},
                {0x1DDD98, "0x1ECFC8 per-car step"},
                {0x1DDE94, "0x1EC4B0"},
                {0x1DDEB4, "0x1F2EA8"},
                {0x1DDEEC, "0x1F8780 driver"},
                {0x1DDEF4, "0x1F6588"},
                {0x1DDF0C, "0x1E3D60"},
                {0x1DDF40, "0x1E3C78"},
                {0x1DDFAC, "0x1DD8A8 moving start"},
            };
            ee.probe(kSpecSite + 8, [&](Ee& e) {
                ex::Spec orig{}, ours{};
                e.read_bytes(sc.spec, orig.data(), ex::kSpecSize);
                ex::Equipment eq = sc.eq;
                ex::build_spec(db, *g_data, eq, sc.flag, sc.mode, sc.override, ours);
                race_spec.add(label, first_diff(ours.data(), orig.data(), ex::kSpecSize, "spec"));
            });
            ee.probe(kConvertSite + 8, [&](Ee& e) {
                ex::Spec s = cc.spec_before, orig_s{};
                ex::ParamImage p = cc.prior, orig_p{};
                ex::convert_params(s, cc.in, p);
                e.read_bytes(cc.p, orig_p.data(), ex::kParamsSize);
                e.read_bytes(cc.spec, orig_s.data(), ex::kSpecSize);
                race_conv.add(label, first_diff(p.data(), orig_p.data(), ex::kParamsSize, "P"));
                race_edits.add(label, first_diff(s.data(), orig_s.data(), ex::kSpecSize, "spec after the converter"));
                const ex::ParamImage whole = ex::race_params(db, *g_data, sc.eq, sc.mode, cc.asm_on, cc.tcs_on, cc.in);
                race_whole.add(label, first_diff(whole.data(), orig_p.data(), ex::kParamsSize, "P"));
                ctx.sample();
                if (map) { // the map's own probe at this return address
                    std::vector<u8> after(car_stride());
                    e.read_bytes(cc.p - 0x10, after.data(), car_stride());
                    std::vector<bool>& w = written["0x1DBFD8 converter (P)"];
                    w.resize(car_stride());
                    for (size_t i = 0; i < after.size(); ++i)
                        if (after[i] != before[kConvertSite][i]) w[i] = true;
                }
            });
            if (!map) return;
            ee.probe(pa(0x1DDAD0), [&](Ee& e) { car = u32(e.a(0)); });
            ee.probe(pa(0x1F36F8), [&](Ee& e) { car_index = s32(e.gpr[21].lo); }); // the per-car loop (s5 = i)
            // One probe per address: a call's return address can be the next call's site.
            std::map<u32, std::pair<std::vector<size_t>, std::vector<size_t>>> at; // address -> (sites ending, sites starting)
            for (size_t i = 0; i < kSites.size(); ++i) {
                at[kSites[i].first].second.push_back(i);
                if (kSites[i].first != kConvertSite) at[kSites[i].first + 8].first.push_back(i);
            }
            for (const auto& [addr, lists] : at) {
                ee.probe(addr, [&, lists](Ee& e) {
                    for (size_t i : lists.first) {
                        const u32 site = kSites[i].first;
                        const u32 rec = kSites[i].first.ntsc_u >= 0x1F3000 ? u32(e.gpr[20].lo) + rs::kCarBase + car_stride() * u32(car_index) : car;
                        std::vector<u8> after(car_stride());
                        e.read_bytes(rec, after.data(), car_stride());
                        std::vector<bool>& w = written[kSites[i].second];
                        w.resize(car_stride());
                        for (size_t k = 0; k < after.size(); ++k)
                            if (after[k] != before[site][k]) w[k] = true;
                    }
                    for (size_t i : lists.second) {
                        const u32 site = kSites[i].first;
                        const u32 rec = kSites[i].first.ntsc_u >= 0x1F3000 ? u32(e.gpr[20].lo) + rs::kCarBase + car_stride() * u32(car_index) : car;
                        std::vector<u8>& b = before[site];
                        b.resize(car_stride());
                        e.read_bytes(rec, b.data(), car_stride());
                    }
                });
            }
        };
        rs::OrigRace race;
        std::string err;
        if (!rs::setup_original_race(ctx.ee, db, gtar, *ev, course, labels, 2, 0, race, err, install)) ctx.check(false, label + ": " + err);
    }
    ctx.note(race_spec.line("race calls of 0x1DA308"));
    ctx.note(race_conv.line("race calls of 0x1DBFD8"));
    ctx.note(race_edits.line("race calls of 0x1DBFD8, spec edits"));
    ctx.note(race_whole.line("race_params (equipment to P) at the race calls"));
    for (const Count* c : {&race_spec, &race_conv, &race_edits, &race_whole}) ctx.check(c->bad == 0, c->first);
    if (events != "-") ctx.check(race_conv.n > 0, "no race calls of the converter");
    for (const auto& [name, w] : written) ctx.note("init map: " + name + ":" + ranges(w));
}
