// GT Mode's starting grid (docs/specs/gt_mode.md §8.6) against the original's (gt_grid_exact):
//
// 1. The showroom build 0x1D93C0 (the menus' car; the grids build every entry with it, with a fresh engine) of every database
//    car (table 30) and every car of a GT Mode race's pool: the spec's mass (+0x2A), the peak 0x1E14E8 (+0x1294 PS, +0x1298
//    kgf m, +0x129C power rpm, +0x129E torque rpm) bit for bit, and the whole power 0x1D9680 shows for it in PS and in HP (the
//    units word 0x2F59D8 + 4 = 0 / 1), against sim::exact::showroom_build and shown_power on race::exact_equipment. NTSC-J's
//    0x1D9680 has no conversion (PS whatever the word; its one language shows PS): there "HP" is compared with PS.
// 2. The grids: GT Mode's race grid 0x12E670 for every race of menu/gt_mode.mbl's series and, for the six-car ones, the
//    championship grid 0x18E190 (whose 0x18DF70 copies the places into the entries), each entry's grid slot (+0x20E0) against
//    gt_grid_places over race::exact_grid_figure, in PS and in HP. The opponents are pool cars in a random order (an equal
//    figure keeps entry order), the player's car (entry 0) a random pool car. The grids' showroom builds are the original's
//    from part 1 (0x1D93C0 replaced by the words it wrote for that car: the mass and the peak), so each grid runs only the
//    original's division, ranking and places.

#include "harness.h"
#include "race_setup.h"
#include "start_setup.h"

#include "disc/disc.h"
#include "disc/volume.h"
#include "formats/menu/mbl.h"
#include "game/data/car_database.h"
#include "game/gt_catalog.h"
#include "race/exact_setup.h"
#include "race/field.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::diff;

namespace {

namespace ex = sim::exact;
namespace rs = race_setup;

constexpr Pa kShowroom = 0x001D93C0, kPowerText = 0x001D9680, kUnits = 0x002F59D8;
constexpr Pa kRaceGrid = 0x0012E670, kChampGrid = 0x0018E190;
constexpr Pa kNewArray = 0x0027C2D8, kDelete = 0x0027C0C0;
constexpr u32 kShowSize = 0x3000, kPeak = 0x1294;
constexpr u32 kChampStride = 0x1F8, kChampSize = 0x1000;
// The championship's places (0x18DF70: +0x484 a stride per entry); from 2002 Tokyo-Seoul v2.00 on 8 higher, as its database
// holder's record (holder_off).
u32 champ_places() { return holder_off(0x484); }

std::string fmt(const char* f, auto... a) {
    char b[512];
    std::snprintf(b, sizeof b, f, a...);
    return b;
}

// A fresh machine with the C runtime, the class check and the grids' heap (operator new[] 0x27C2D8, delete 0x27C0C0).
void fresh(Ee& ee) {
    ee.reset();
    ee.clear_hle();
    ee.clear_probes();
    ee.hle_libc_defaults();
    ee.hle(rs::kClassCheck, [](Ee& e) { e.set_v0(1); }, "class check");
    ee.hle(kNewArray, [](Ee& e) { e.set_v0(e.alloc(std::max<u32>(u32(e.a(0)), 16))); }, "operator new[]");
    ee.hle(kDelete, [](Ee&) {}, "operator delete");
    ee.wf(rs::kAirDensityGlobal, 1.2f);
}

struct OrigShowroom {
    s16 mass = 0;
    u32 ps = 0, kgfm = 0;
    int power_rpm = 0, torque_rpm = 0;
    int shown[2] = {}; // units word 0 (PS), 1 (HP)
};

bool original_showroom(Ee& ee, const data::CarDatabase& db, const Bytes& gtar_bytes, const std::string& label, OrigShowroom& o,
                       std::string& err) {
    try {
        fresh(ee);
        const u32 gtar = ee.alloc_bytes(gtar_bytes);
        const u32 entry = ee.alloc(rs::kEntrySize, 64), show = ee.alloc(kShowSize), env = ee.alloc(0x20), rpm = ee.alloc(4);
        if (!rs::build_entry(ee, gtar, db, label, entry, err)) return false;
        ee.call(kShowroom, {Arg::ptr(show), Arg::ptr(entry + rs::kEntryEq), Arg::ptr(env)}, 200'000'000);
        o.mass = s16(ee.r16(show + 0x2A));
        o.ps = ee.r32(show + kPeak), o.kgfm = ee.r32(show + kPeak + 4);
        o.power_rpm = ee.r16(show + kPeak + 8), o.torque_rpm = ee.r16(show + kPeak + 0xA);
        for (u32 u = 0; u < 2; ++u) {
            ee.w32(kUnits + 4, u);
            o.shown[u] = ee.call(kPowerText, {Arg::ptr(show), Arg::ptr(rpm), Arg::integer(0x7073), Arg::integer(0)}, 100000).v0_s32();
        }
        return true;
    } catch (const EeTrap& t) {
        err = fmt("trap at pc=%08X: %s", t.pc, t.what());
        return false;
    }
}

// The machine for the grids: fresh(), with the showroom build writing the recorded words of the entry's car (*cars, by its EQ's
// first word: the index).
void grid_machine(Ee& ee, const std::vector<const OrigShowroom*>* cars) {
    fresh(ee);
    ee.hle(kShowroom, [cars](Ee& e) {
        const u32 show = u32(e.a(0)), eq = u32(e.a(1));
        const OrigShowroom& o = *cars->at(e.r32(eq));
        e.w16(show + 0x2A, u16(o.mass));
        e.w32(show + kPeak, o.ps), e.w32(show + kPeak + 4, o.kgfm);
        e.w16(show + kPeak + 8, u16(o.power_rpm)), e.w16(show + kPeak + 0xA, u16(o.torque_rpm));
    }, "showroom build (recorded)");
}

// The original's grid slots of entries with the showroom words `cars` (entry 0 the player's) with the units word `units`: the race
// grid, or the championship's (six entries); on grid_machine's machine over `cars`, its heap emptied first.
bool original_grid(Ee& ee, const std::vector<const OrigShowroom*>& cars, u32 units, bool championship, std::vector<int>& slots,
                   std::string& err) {
    try {
        ee.free_all();
        ee.w32(kUnits + 4, units);
        const u32 n = u32(cars.size());
        const u32 info = ee.alloc(0x100), list = ee.alloc(0x20), arr = ee.alloc(4 * 8);
        ee.w32(list + 0, n);
        ee.w32(list + 8, arr);
        ee.w32(info + 0x18, list);
        std::vector<u32> entries;
        for (u32 i = 0; i < n; ++i) {
            const u32 entry = ee.alloc(rs::kEntrySize, 64);
            ee.w32(entry + rs::kEntryEq, i);
            ee.w32(entry + entry_off(0x20E0), 0xFFFFFFFF);
            ee.w32(arr + 4 * i, entry);
            entries.push_back(entry);
        }
        if (championship) {
            const u32 champ = ee.alloc(kChampSize);
            ee.call(kChampGrid, {Arg::ptr(champ), Arg::ptr(info)}, 2'000'000'000);
            for (u32 i = 0; i < n; ++i)
                if (ee.r32(champ + champ_places() + kChampStride * i) != ee.r32(entries[i] + entry_off(0x20E0))) {
                    err = fmt("entry %u: the championship's place %d, the entry's %d", i, s32(ee.r32(champ + champ_places() + kChampStride * i)),
                              s32(ee.r32(entries[i] + entry_off(0x20E0))));
                    return false;
                }
        } else {
            ee.call(kRaceGrid, {Arg::ptr(info)}, 2'000'000'000);
        }
        slots.clear();
        for (u32 e : entries) slots.push_back(s32(ee.r32(e + entry_off(0x20E0))));
        return true;
    } catch (const EeTrap& t) {
        err = fmt("trap at pc=%08X: %s", t.pc, t.what());
        return false;
    }
}

std::string join(const std::vector<int>& v) {
    std::string s;
    for (int x : v) s += (s.empty() ? "" : " ") + std::to_string(x);
    return s;
}

} // namespace

DIFF_TEST_ALL(gt_grid_exact, "GT Mode's grid: showroom 0x1D93C0 (mass, peak 0x1E14E8, 0x1D9680) and the grids 0x12E670 / 0x18E190 vs "
                             "sim::exact::showroom_build, race::gt_grid_places") {
    data::CarDatabase db;
    Volume& vol = ctx.disc->volume();
    if (!db.load(vol)) {
        ctx.check(false, "database load: " + db.error());
        return;
    }
    const Volume::Entry* dbe = vol.find(db_path(vol));
    if (!dbe) {
        ctx.check(false, db_path(vol) + " not found");
        return;
    }
    const Bytes gtar = vol.read(*dbe);
    std::string err;
    const ex::ProgramData* tables = start_setup::program_data(ctx, err);
    if (!tables) {
        ctx.check(false, err);
        return;
    }
    // GT Mode's races (menu/gt_mode.mbl's series) and their pools.
    std::vector<const data::Event*> races;
    std::set<std::string> champ_rounds;
    if (const Volume::Entry* me = vol.find("menu/gt_mode.mbl")) {
        const Bytes mbl = vol.read(*me);
        for (const game::RaceSeries& s : game::Catalog::from_mbl(menu::MblFile::parse(mbl)).series)
            for (const std::string& r : s.races)
                if (const data::Event* e = db.event(r)) {
                    races.push_back(e);
                    if (s.championship()) champ_rounds.insert(r);
                }
    }
    // (a disc without GT Mode's races, 2002 Tokyo-Geneva's: the showroom of every database car alone)
    if (races.empty()) ctx.note("no GT Mode races in menu/gt_mode.mbl: the showroom only");
    auto pool = [](const data::Event& e) {
        std::vector<std::string> p;
        for (const std::string& l : e.entries)
            if (!l.empty() && l != "-") p.push_back(l);
        return p;
    };

    // 1. The showroom build of every car and every pool car.
    std::set<std::string> labels;
    for (const data::Car& c : db.cars())
        if (c.label.rfind("sample", 0) != 0) labels.insert(c.label);
    for (const data::Event* e : races)
        for (const std::string& l : pool(*e)) labels.insert(l);
    const bool converts = program() != Program::NtscJ; // the units word converts the power (not on NTSC-J)
    std::map<std::string, int> figure_ps, figure_hp; // ours, for part 2
    std::map<std::string, OrigShowroom> orig;          // the original's, for part 2
    size_t built = 0, bad = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (const std::string& l : labels) {
        std::optional<ex::Equipment> eq = race::exact_equipment(db, {l}, &err);
        if (!eq) {
            ctx.check(false, l + ": exact_equipment: " + err);
            continue;
        }
        OrigShowroom o;
        if (!original_showroom(ctx.ee, db, gtar, l, o, err)) {
            ctx.check(false, l + ": original: " + err);
            continue;
        }
        const ex::Showroom s = ex::showroom_build(db, *tables, *eq);
        const int ps = ex::shown_power(s.peak.ps, false), hp = ex::shown_power(s.peak.ps, converts);
        figure_ps[l] = race::exact_grid_figure(db, *tables, *eq, false);
        figure_hp[l] = race::exact_grid_figure(db, *tables, *eq, converts);
        orig[l] = o;
        ++built;
        ctx.sample();
        if (s.mass() != o.mass || s.peak.ps.bits != o.ps || s.peak.kgfm.bits != o.kgfm || s.peak.power_rpm != o.power_rpm ||
            s.peak.torque_rpm != o.torque_rpm || ps != o.shown[0] || hp != o.shown[1]) {
            ++bad;
            ctx.check(false, fmt("%s: original %d kg, %s PS @%d, %s kgfm @%d, shown %d PS / %d HP; ours %d kg, %s PS @%d, %s kgfm @%d, %d / %d",
                                 l.c_str(), o.mass, hexf(o.ps).c_str(), o.power_rpm, hexf(o.kgfm).c_str(), o.torque_rpm, o.shown[0],
                                 o.shown[1], s.mass(), hexf(s.peak.ps.bits).c_str(), s.peak.power_rpm, hexf(s.peak.kgfm.bits).c_str(),
                                 s.peak.torque_rpm, ps, hp));
        }
    }
    ctx.note(fmt("showroom: %zu cars built, %zu differ (%.0f s)", built, bad,
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()));
    if (races.empty()) {
        ctx.check(built > 0, "no cars built");
        return;
    }

    // 2. The grids of every GT Mode race, in PS and in HP; championship rounds also by the championship's grid.
    size_t grids = 0, wrong = 0, ties = 0;
    const int draws = std::max(1, ctx.iterations / 500);
    std::vector<const OrigShowroom*> shown;
    grid_machine(ctx.ee, &shown);
    struct Unstub { // the recorded showroom stub reads `shown`: gone with it (the later tests build cars with the original's)
        Ee& ee;
        ~Unstub() { ee.clear_hle(), ee.hle_libc_defaults(); }
    } unstub{ctx.ee};
    for (const data::Event* e : races) {
        const std::vector<std::string> p = pool(*e);
        const size_t cars = size_t(std::clamp(int(e->cars_in_race), 2, 6));
        if (p.size() + 1 < cars) continue;
        for (int d = 0; d < draws; ++d) {
            // The opponents: cars - 1 pool cars in a random order; the player's car a random pool car.
            std::vector<std::string> order = p;
            for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[size_t(ctx.rng.range(0, s64(i) - 1))]);
            std::vector<std::string> entries = {p[size_t(ctx.rng.range(0, s64(p.size()) - 1))]};
            entries.insert(entries.end(), order.begin(), order.begin() + std::ptrdiff_t(cars - 1));
            shown.clear();
            for (const std::string& l : entries)
                if (auto it = orig.find(l); it != orig.end()) shown.push_back(&it->second);
            if (shown.size() != entries.size()) continue; // (a car part 1 could not build: reported there)
            for (u32 units : {0u, 1u})
                for (bool champ : {false, true}) {
                    if (champ && !(champ_rounds.count(e->label) && cars == 6)) continue;
                    std::vector<int> orig_slots;
                    if (!original_grid(ctx.ee, shown, units, champ, orig_slots, err)) {
                        ctx.check(false, e->label + ": original grid: " + err);
                        continue;
                    }
                    std::vector<int> fig;
                    for (size_t i = 1; i < entries.size(); ++i) fig.push_back((units ? figure_hp : figure_ps)[entries[i]]);
                    std::vector<int> ours = race::gt_grid_places(fig);
                    ours.insert(ours.begin(), int(entries.size()) - 1); // the player behind the opponents
                    ties += std::set<int>(fig.begin(), fig.end()).size() < fig.size();
                    ++grids;
                    ctx.sample();
                    if (ours != orig_slots) {
                        ++wrong;
                        std::string names;
                        for (size_t i = 1; i < entries.size(); ++i) names += " " + entries[i] + "=" + std::to_string(fig[i - 1]);
                        ctx.check(false, fmt("%s (%s, %s): original slots %s, ours %s;%s", e->label.c_str(), units ? "HP" : "PS",
                                             champ ? "championship" : "race", join(orig_slots).c_str(), join(ours).c_str(), names.c_str()));
                    }
                }
        }
    }
    ctx.note(fmt("grids: %zu (%zu races, %zu championship rounds), %zu with equal figures, %zu differ", grids, races.size(),
                 champ_rounds.size(), ties, wrong));
}
