// Fields bigger than GT3's (race::FieldConfig::made_grid, sim::exact::kMaxCars): Quick race's custom race, mod events and
// online races of up to 32 cars, the grid places past the course's own made along its main line. A race of the course's own
// grid size races as before with the switch on; a 32-car field starts with its cars apart and on the road on every course, races, and practises.

#include "test_framework.h"

#include "disc/core_image.h"
#include "disc/volume.h"
#include "game/data/car_database.h"
#include "race/backend.h"
#include "race/exact_field.h"
#include "race/hud.h"
#include "race/results.h"
#include "sim/exact/race.h"
#include "sim/exact/race_record.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <memory>

using namespace gt3r;
namespace ex = gt3r::sim::exact;

namespace {

data::CarDatabase* database() {
    static data::CarDatabase db;
    static bool tried = false, ok = false;
    if (!tried) {
        tried = true;
        if (Disc* d = test::test_disc()) ok = db.load(d->volume());
    }
    return ok ? &db : nullptr;
}

std::shared_ptr<const ex::ProgramData> program() {
    static std::shared_ptr<const ex::ProgramData> d;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (Disc* disc = test::test_disc())
            if (std::optional<Bytes> core = disc->volume().read_program())
                d = std::make_shared<const ex::ProgramData>(ex::ProgramData::load(CoreImage::decode(*core)));
    }
    return d;
}

struct BigField {
    std::unique_ptr<race::RaceTrack> track;
    std::unique_ptr<race::RaceField> field;
    const ex::ExactRace* race = nullptr;
};

// The event's race (on `course`; "": its own) with `opponents` CPU cars (its entries in turn), the player behind them; `made`:
// FieldConfig::made_grid. `grid`: FieldConfig::grid_slots. `remote`: other players' cars on the places behind (online).
BigField make_field(const std::string& event, int opponents, bool made, std::string course = "",
                    ex::Conductor kind = ex::Conductor::SinglePlayer, std::vector<int> grid = {}, int remote = 0) {
    BigField out;
    data::CarDatabase& db = *database();
    const data::Event* ev = db.event(event);
    CHECK(ev != nullptr);
    if (!ev || ev->entries.empty()) return out;
    if (course.empty()) course = ev->course;
    Disc* disc = test::test_disc();
    const Volume::Entry* e = disc->volume().find("courses/US/" + course);
    CHECK(e != nullptr);
    if (!e) return out;
    const Bytes file = disc->volume().read(*e);
    Vfs vfs(&disc->volume());
    out.track = race::make_track(vfs, file, course);
    race::FieldConfig cfg;
    cfg.event = event;
    for (int k = 0; k < opponents; ++k) cfg.opponents.push_back(ev->entries[size_t(k) % ev->entries.size()]);
    cfg.max_opponents = opponents;
    cfg.made_grid = made;
    cfg.grid_slots = std::move(grid);
    for (int k = 0; k < remote; ++k) cfg.remote.push_back({"r34_gtr_v_99", opponents + 1 + k, {}, {}, std::nullopt});
    race::ExactFieldOptions o;
    o.data = program();
    o.course_file = std::make_shared<const Bytes>(file);
    o.course = course;
    o.kind = kind;
    o.laps = kind == ex::Conductor::FreePractice ? 0 : ev->laps;
    o.player.car = {"r34_gtr_v_99"};
    cfg.exact = o;
    std::string err;
    out.field = race::RaceField::create(*out.track, db, cfg, &err);
    if (!out.field) std::printf("  %s: %s\n", event.c_str(), err.c_str());
    CHECK(out.field != nullptr);
    if (out.field) out.race = race::exact_race_of(*out.field);
    return out;
}

// Steps the race with the player's car standing (the CPU cars drive): `steps` frames of 1/60 s.
void run(BigField& f, int steps) {
    for (int k = 0; k < steps; ++k) f.field->player().step(sim::ControlInputs{}, 1.0 / 60.0);
}

} // namespace

// A race of the course's own size races exactly as before with the switch on (no grid place is made).
TEST(big_field_small_race_unchanged) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    BigField a = make_field("Gsunday_e_0001", 5, false), b = make_field("Gsunday_e_0001", 5, true);
    CHECK(a.race && b.race);
    if (!a.race || !b.race) return;
    CHECK(a.race->size() == 6 && b.race->size() == 6);
    CHECK(ex::race_hash(*a.race) == ex::race_hash(*b.race));
    run(a, 1800), run(b, 1800);
    CHECK(ex::race_hash(*a.race) == ex::race_hash(*b.race));
}

// 32 cars: every one on the grid apart from the others, on the road, facing on along the lap; after a minute of racing every
// CPU car has driven on (none stuck in a wall at its start) and the ranking holds all 32.
TEST(big_field_32_cars) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    for (const char* event : {"Gsunday_e_0001", "Dr_tahitimaze_0001"}) {
        BigField f = make_field(event, 31, true);
        CHECK(f.race != nullptr);
        if (!f.race) continue;
        CHECK(f.race->size() == 32);
        const std::span<const ex::Car> cars = f.race->cars();
        float closest = 1e9f;
        for (size_t i = 0; i < cars.size(); ++i) {
            CHECK(std::isfinite(cars[i].pos.z.host()));
            for (size_t j = i + 1; j < cars.size(); ++j)
                closest = std::min(closest, std::hypot(cars[i].pos.x.host() - cars[j].pos.x.host(),
                                                       cars[i].pos.y.host() - cars[j].pos.y.host()));
        }
        std::printf("  %s: 32 cars, closest two %.2f m apart at the start\n", event, double(closest));
        CHECK(closest > 2.5f);
        std::vector<ex::Vec3> start;
        for (const ex::Car& c : cars) start.push_back(c.pos);
        run(f, 60 * 75); // (the countdown and a minute)
        int moved = 0;
        float least = 1e9f;
        for (size_t i = 1; i < cars.size(); ++i) {
            const float d = std::hypot(cars[i].pos.x.host() - start[i].x.host(), cars[i].pos.y.host() - start[i].y.host());
            least = std::min(least, d);
            moved += d > 150.0f;
            CHECK(std::isfinite(cars[i].speed.host()));
        }
        std::printf("  %s: %d of 31 CPU cars 150 m on or more after a minute (least %.0f m)\n", event, moved, double(least));
        CHECK(moved == 31);
        std::vector<bool> seen(32, false);
        for (size_t k = 0; k < 32; ++k) {
            const s32 car = f.race->order()[k];
            CHECK(car >= 0 && car < 32);
            if (car >= 0 && car < 32) seen[size_t(car)] = true;
        }
        CHECK(std::all_of(seen.begin(), seen.end(), [](bool s) { return s; }));
    }
}

// Every race course's 32-car grid: each made place behind the table's and the one before it, short of the line and on its first
// lap (on courses with long straights or a part of the lap beside the grid too), and clear of the walls: nothing pushes a car
// in the two seconds it stands there. On the courses whose grid runs past a pit lane's mouth every CPU car drives away from it.
TEST(big_field_grid_every_course) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    const std::vector<std::string> drive = {"apricot", "deepforest", "midfield", "route11", "r_apricot", "r_midfield", "r_route11"};
    int courses = 0;
    for (const Volume::Entry& e : test::test_disc()->volume().entries()) {
        if (e.path.rfind("courses/US/", 0) != 0) continue;
        const std::string name = e.path.substr(11);
        if (name.empty() || name[0] == '.' || name.find('/') != std::string::npos || name.rfind("L_", 0) == 0) continue; // (a licence test's)
        BigField f = make_field("Gsunday_e_0001", 31, true, name);
        if (!f.race) continue;
        ++courses;
        CHECK(f.race->size() == 32);
        const std::span<const ex::Car> cars = f.race->cars();
        const float lap = f.race->course().query->lap_length().host();
        const size_t table = f.race->course().query->grid().size();
        std::vector<ex::Vec3> start;
        for (const ex::Car& c : cars) start.push_back(c.pos);
        // (the cars by grid slot: the opponents on slots 0 .. 30, the player on the last)
        auto behind = [&](size_t slot) { return lap - cars[slot == 31 ? 0 : slot + 1].f3e4.host(); };
        bool fine = true;
        for (size_t s = 0; s < 32; ++s) {
            const ex::Car& c = cars[s == 31 ? 0 : s + 1];
            fine = fine && c.laps == 0 && behind(s) > 0 && behind(s) < 1000;
            if (s >= table) fine = fine && behind(s) > behind(s - 1) + 2;
        }
        if (!fine) std::printf("  %s: the grid is not in line behind the start\n", name.c_str());
        CHECK(fine);
        const bool drives = std::find(drive.begin(), drive.end(), name) != drive.end();
        run(f, 120);
        for (size_t i = 0; i < 32; ++i) {
            const float moved = std::hypot(cars[i].pos.x.host() - start[i].x.host(), cars[i].pos.y.host() - start[i].y.host());
            if (moved > 0.02f) std::printf("  %s: car %zu pushed %.2f m where it stands\n", name.c_str(), i, double(moved));
            CHECK(moved <= 0.02f);
        }
        if (!drives) continue;
        run(f, 60 * 28);
        for (size_t i = 1; i < 32; ++i) {
            const float moved = std::hypot(cars[i].pos.x.host() - start[i].x.host(), cars[i].pos.y.host() - start[i].y.host());
            if (moved < 50) std::printf("  %s: car %zu only %.0f m from its place after 30 s\n", name.c_str(), i, double(moved));
            CHECK(moved >= 50);
        }
    }
    std::printf("  %d courses\n", courses);
    CHECK(courses > 30);
}

// Free practice (QUALIFY) with a bigger field: every one of the race's cars runs it, apart on the line behind (on a lap too
// short for 200 m a car as well), and the grid its times leave (race::practice_grid) fits the race.
TEST(big_field_practice) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    for (const int opponents : {9, 31}) {
        BigField f = make_field("Gsunday_e_0001", opponents, true, "", ex::Conductor::FreePractice); // (Super Speedway: 2,404 m)
        CHECK(f.race != nullptr);
        if (!f.race) continue;
        const size_t n = size_t(opponents) + 1;
        CHECK(size_t(f.race->size()) == n);
        const std::span<const ex::Car> cars = f.race->cars();
        float closest = 1e9f;
        for (size_t i = 0; i < n; ++i)
            for (size_t j = i + 1; j < n; ++j)
                closest = std::min(closest, std::hypot(cars[i].pos.x.host() - cars[j].pos.x.host(), cars[i].pos.y.host() - cars[j].pos.y.host()));
        std::printf("  practice of %zu cars: the closest two %.1f m apart\n", n, double(closest));
        CHECK(closest > 20.0f);
        run(f, 60 * 150);
        std::vector<u32> best = race::exact_best_laps(*f.field).car;
        CHECK(best.size() == n);
        int timed = 0;
        for (size_t k = 1; k < best.size(); ++k) timed += best[k] < race::kPracticeNoTime;
        std::printf("  %d of %d CPU cars with a lap after 150 s\n", timed, opponents);
        CHECK(timed >= opponents - 1);
        if (best.size() != n) continue;
        best[0] = 1; // (the player fastest)
        CHECK(race::practice_regrids(best));
        BigField r = make_field("Gsunday_e_0001", opponents, true, "", ex::Conductor::SinglePlayer, race::practice_grid(best));
        CHECK(r.race != nullptr);
        if (r.race) CHECK(r.race->order()[0] == 0);
    }
    // A field GT3 could run practises as before with the switch on.
    BigField a = make_field("Gsunday_e_0001", 5, false, "", ex::Conductor::FreePractice), b = make_field("Gsunday_e_0001", 5, true, "", ex::Conductor::FreePractice);
    CHECK(a.race && b.race);
    if (!a.race || !b.race) return;
    run(a, 1800), run(b, 1800);
    CHECK(ex::race_hash(*a.race) == ex::race_hash(*b.race));
}

// An online race (the i.LINK battle's conductor, issue 453) of 32 cars: the CPU cars on the first places, the player and two
// other players behind them past the course's grid.
TEST(big_field_online) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    for (const char* course : {"midfield", "route11"}) {
        BigField f = make_field("Gsunday_e_0001", 29, true, course, ex::Conductor::BattleMP, {}, 2);
        CHECK(f.race != nullptr);
        if (!f.race) continue;
        CHECK(f.race->size() == 32);
        for (const ex::Car& c : f.race->cars()) {
            CHECK(c.laps == 0);
            CHECK(f.race->course().query->lap_length().host() - c.f3e4.host() < 400.0f);
        }
        // As the same places of a single race: every machine of an online race, whatever its own car, builds one grid.
        BigField s = make_field("Gsunday_e_0001", 31, true, course);
        CHECK(s.race != nullptr);
        if (s.race) CHECK(std::abs(s.race->cars()[0].f3e4.host() - f.race->cars()[31].f3e4.host()) < 0.5f);
    }
}

// A race record of a big field: its cars and the made grid survive the file. One without a grid made as today's races make it
// keeps version 7, which v0.8.0 reads.
TEST(big_field_race_record) {
    ex::RaceRecord r;
    r.entries.resize(20);
    for (size_t i = 0; i < r.entries.size(); ++i) r.entries[i].grid_slot = int(i), r.entries[i].eq.car = 0x1000 + i;
    r.pads.assign(r.entries.size(), {});
    for (const ex::MadeGrid made : {ex::MadeGrid::Off, ex::MadeGrid::V080, ex::MadeGrid::On}) {
        r.made_grid = made;
        const Bytes file = r.serialize();
        CHECK(file[4] == (made == ex::MadeGrid::On ? 8 : 7));
        const ex::RaceRecord y = ex::RaceRecord::parse(file);
        CHECK(y.entries.size() == 20 && y.made_grid == made);
        CHECK(y.entries[19].grid_slot == 19 && y.entries[19].eq.car == 0x1000 + 19);
    }
}

// The HUD's place past GT3's twelve strings, made of the same glyphs.
TEST(big_field_rank_ordinal) {
    CHECK(race::rank_ordinal(1) == "1st" && race::rank_ordinal(2) == "2nd" && race::rank_ordinal(3) == "3rd");
    CHECK(race::rank_ordinal(11) == "11th" && race::rank_ordinal(12) == "12th" && race::rank_ordinal(13) == "13th");
    CHECK(race::rank_ordinal(21) == "21st" && race::rank_ordinal(22) == "22nd" && race::rank_ordinal(23) == "23rd");
    CHECK(race::rank_ordinal(31) == "31st" && race::rank_ordinal(32) == "32nd");
}
