// The cars a race fields, as the original builds them before the race:
//
// 1. race_course_tires_exact: RaceBase::vf40 (0x17CC80, the tyres for the course) over every car of tables 30, 31 and 35 on a
//    dirt, a wet and a road course, against sim::exact::course_tires on the same tyres.
// 2. gt_field_exact: GT Mode's field (gt_mode §20.4) end to end: RaceGTmodeInformation's constructor 0x18F3E0, the event
//    (0x179C48) and its virtual 12 (0x18F540: the pool draw, virtual 14 and 13 0x179D70, each drawn car built by 0x238550 from its
//    record's parts and settings) on a race of six entries, for every GT Mode race of more than one car and three draw states;
//    each drawn car's spec against race::draw_opponents and race::exact_equipment's (the spec as the race builds it).

#include "harness.h"
#include "race_setup.h"
#include "start_setup.h"

#include "base/game_random.h"
#include "disc/disc.h"
#include "disc/volume.h"
#include "game/data/car_database.h"
#include "race/exact_setup.h"
#include "race/pool_draw.h"
#include "sim/exact/params_build.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::diff;

namespace {

namespace ex = sim::exact;
namespace rs = race_setup;

constexpr Pa kCourseTires = 0x0017CC80; // RaceBase::vf40 (this): this+4 the race information
constexpr Pa kInfoCtor = 0x0018F3E0, kInfoEvent = 0x00179C48, kInfoField = 0x0018F540, kPaints = 0x00176FD0;
constexpr Pa kNewArray = 0x0027C2D8, kDelete = 0x0027C0C0;
constexpr Pa kGlobalDb = 0x002EBD88, kRaceState = 0x00351DE0, kLanguage = 0x00352FD8;

std::string fmt(const char* f, auto... a) {
    char b[512];
    std::snprintf(b, sizeof b, f, a...);
    return b;
}

// A fresh machine with the C runtime, the class check and the heap (operator new[] / delete); the database at `gtar`.
u32 fresh(Ee& ee, const Bytes& gtar_bytes) {
    ee.reset();
    ee.clear_hle();
    ee.clear_probes();
    ee.hle_libc_defaults();
    ee.hle(rs::kClassCheck, [](Ee& e) { e.set_v0(1); }, "class check");
    ee.hle(kNewArray, [](Ee& e) { e.set_v0(e.alloc(std::max<u32>(u32(e.a(0)), 16))); }, "operator new[]");
    ee.hle(kDelete, [](Ee&) {}, "operator delete");
    return ee.alloc_bytes(gtar_bytes);
}

// An empty race entry (its CarEquipments constructed and initialised on the database).
u32 empty_entry(Ee& ee, u32 gtar) {
    const u32 entry = ee.alloc(rs::kEntrySize, 64), eq = entry + rs::kEntryEq;
    ee.call(rs::kEqCtor, {Arg::ptr(eq + 0x1D0)}, 100000);
    rs::set_database(ee, eq + 0x1D0, gtar);
    ee.call(rs::kEqInit, {Arg::ptr(eq)}, 1000000);
    return entry;
}

ex::Equipment entry_equipment(Ee& ee, u32 entry) {
    std::vector<u8> img(ex::kEquipmentImageSize);
    ee.read_bytes(entry + rs::kEntryEq, img.data(), u32(img.size()));
    return ex::equipment_image(ByteSpan(img.data(), img.size()));
}

bool load(DiffContext& ctx, data::CarDatabase& db, Bytes& gtar) {
    Volume& vol = ctx.disc->volume();
    if (!db.load(vol)) {
        ctx.check(false, "database load: " + db.error());
        return false;
    }
    const Volume::Entry* e = vol.find(db_path(vol));
    if (!e) {
        ctx.check(false, db_path(vol) + " not found");
        return false;
    }
    gtar = vol.read(*e);
    return true;
}

} // namespace

DIFF_TEST_ALL(race_course_tires_exact, "the course's tyres RaceBase::vf40 0x17CC80 vs sim::exact::course_tires, every car") {
    data::CarDatabase db;
    Bytes gtar_bytes;
    if (!load(ctx, db, gtar_bytes)) return;
    std::vector<std::string> labels;
    for (const data::Car& c : db.cars()) labels.push_back(c.label);
    for (const data::CarVariant& v : db.variants()) labels.push_back(v.label);
    for (const data::CarVariant& v : db.arcade_cars()) labels.push_back(v.label);
    Ee& ee = ctx.ee;
    for (u8 category : {u8(2), u8(1), u8(0)}) {
        std::string course;
        for (const data::Course& c : db.courses())
            if (course.empty() && race::exact_course_category(db, c.label) == category) course = c.label;
        if (course.empty()) {
            ctx.check(false, fmt("no course of category %d", int(category)));
            continue;
        }
        int cars = 0, refitted = 0, differ = 0, failed = 0;
        for (size_t first = 0; first < labels.size(); first += 64) {
            const u32 gtar = fresh(ee, gtar_bytes);
            // The race information: +0x18 its entry list {count, +8 entries}, +0x28 the event's database holder (+0x20 the
            // course id, the course record's +8 its category: 0x2389A0).
            const u32 holder = ee.alloc(0x40), info = ee.alloc(0x100), race = ee.alloc(0x40), list = ee.alloc(0x20);
            rs::set_database(ee, holder, gtar);
            ee.w64(holder + holder_off(0x20), db::id_hash(course));
            ee.w32(info + 0x28, holder);
            ee.w32(info + 0x18, list);
            ee.w32(race + 4, info);
            std::vector<std::pair<std::string, u32>> built;
            for (size_t i = first; i < labels.size() && i < first + 64; ++i) {
                const u32 entry = ee.alloc(rs::kEntrySize, 64);
                std::string err;
                if (rs::build_entry(ee, gtar, db, labels[i], entry, err)) built.push_back({labels[i], entry});
                else ++failed;
            }
            const u32 arr = ee.alloc(4 * u32(built.size() + 1));
            for (size_t k = 0; k < built.size(); ++k) ee.w32(arr + 4 * u32(k), built[k].second);
            ee.w32(list + 0, u32(built.size()));
            ee.w32(list + 8, arr);
            std::vector<ex::Equipment> before;
            for (auto& [l, e] : built) before.push_back(entry_equipment(ee, e));
            ee.call(kCourseTires, {Arg::ptr(race)}, 2'000'000'000);
            for (size_t k = 0; k < built.size(); ++k) {
                const ex::Equipment orig = entry_equipment(ee, built[k].second);
                ex::Equipment ours = before[k];
                ex::course_tires(db, ours, category);
                ++cars;
                refitted += orig.part != before[k].part;
                const auto f = size_t(data::TableId::FrontTire), r = size_t(data::TableId::RearTire);
                const bool same = ours.part[f] == orig.part[f] && ours.part[r] == orig.part[r];
                ctx.sample();
                if (!same && ++differ <= 10)
                    ctx.check(false, fmt("%s on %s: the original %s / %s, ours %s / %s", built[k].first.c_str(), course.c_str(),
                                         db.label(orig.part[f]).c_str(), db.label(orig.part[r]).c_str(),
                                         db.label(ours.part[f]).c_str(), db.label(ours.part[r]).c_str()));
            }
        }
        ctx.check(failed == 0, fmt("%d cars could not be built", failed));
        ctx.note(fmt("%s (category %d): %d cars, %d refitted, %d differ", course.c_str(), int(category), cars, refitted, differ));
    }
}

DIFF_TEST_ALL(gt_field_exact, "GT Mode's field: RaceGTmodeInformation vf12 (the draw, 0x238550) vs race::draw_opponents, exact_equipment") {
    data::CarDatabase db;
    Bytes gtar_bytes;
    if (!load(ctx, db, gtar_bytes)) return;
    std::string err;
    const ex::ProgramData* tables = start_setup::program_data(ctx, err);
    if (!tables) {
        ctx.check(false, err);
        return;
    }
    Ee& ee = ctx.ee;
    int races = 0, cars = 0, differ = 0, above = 0;
    for (const data::Event& ev : db.events()) {
        if (ev.label.empty() || ev.label[0] != 'G' || ev.cars_in_race < 2) continue;
        ++races;
        for (u32 seed : {GameRandom::kBootSeed, 0x01234567u, 0xCAFEF00Du}) {
            const u32 gtar = fresh(ee, gtar_bytes);
            ee.hle(kPaints, [](Ee&) {}, "paints"); // (after the build: the paint colours)
            ee.w32(kGlobalDb, gtar);
            ee.w32(kLanguage, u32(db.language()));
            const u32 info = ee.alloc(0x1000, 64);
            ee.call(kInfoCtor, {Arg::ptr(info)}, 10'000'000);
            ee.call(kInfoEvent, {Arg::ptr(info), Arg::integer(s64(db::id_hash(ev.label)))}, 10'000'000);
            const u32 n = 6, race = ee.alloc(0x100), list = ee.alloc(0x20), arr = ee.alloc(4 * n);
            ee.w32(race + 0x18, list), ee.w32(list + 0, n), ee.w32(list + 8, arr);
            std::vector<u32> entries;
            for (u32 i = 0; i < n; ++i) entries.push_back(empty_entry(ee, gtar)), ee.w32(arr + 4 * i, entries.back());
            ee.w32(kRaceState, seed);
            ee.call(kInfoField, {Arg::ptr(info), Arg::ptr(race)}, 2'000'000'000);
            GameRandom g{seed};
            const std::vector<std::string> drawn = race::draw_opponents(db, ev, int(n) - 1, g);
            for (u32 i = 1; i < n; ++i) {
                ex::Equipment orig = entry_equipment(ee, entries[i]);
                const std::string label = i - 1 < drawn.size() ? drawn[i - 1] : std::string();
                std::optional<ex::Equipment> ours = label.empty() ? std::nullopt : race::exact_equipment(db, {label});
                ++cars;
                above += orig.word(0x1B8) > 1000;
                bool same = ours && ours->car == orig.car && ours->part == orig.part && ours->word(0x1B8) == orig.word(0x1B8);
                if (same) { // the spec the race builds from each (the words it never reads aside)
                    ex::Spec a{}, b{};
                    ex::build_spec(db, *tables, *ours, 0, 0, false, a);
                    ex::build_spec(db, *tables, orig, 0, 0, false, b);
                    same = a == b;
                }
                ctx.sample();
                if (!same && ++differ <= 10)
                    ctx.check(false, fmt("%s, draw state %08X, slot %u: ours %s differs from the original's car", ev.label.c_str(), seed, i,
                                         label.c_str()));
            }
        }
    }
    ctx.note(fmt("%d races x 3 draws: %d drawn cars, %d differ; %d with an engine output factor above 1000", races, cars, differ,
                 above));
}
