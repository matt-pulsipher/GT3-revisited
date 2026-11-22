// Fields bigger than GT3's (race::FieldConfig::made_grid, sim::exact::kMaxCars): Quick race's custom race, mod events and
// online races of up to 32 cars, the grid places past the course's own made along its main line. A race of the course's own
// grid size races as before with the switch on; a 32-car field starts with its cars apart and on the road, and races.

#include "test_framework.h"

#include "disc/core_image.h"
#include "disc/volume.h"
#include "game/data/car_database.h"
#include "race/backend.h"
#include "race/exact_field.h"
#include "race/hud.h"
#include "sim/exact/race.h"
#include "sim/exact/race_record.h"
#include "vfs/vfs.h"

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

// The event's race with `opponents` CPU cars (its entries in turn), the player behind them; `made`: FieldConfig::made_grid.
BigField make_field(const std::string& event, int opponents, bool made) {
    BigField out;
    data::CarDatabase& db = *database();
    const data::Event* ev = db.event(event);
    CHECK(ev != nullptr);
    if (!ev || ev->entries.empty()) return out;
    Disc* disc = test::test_disc();
    const Volume::Entry* e = disc->volume().find("courses/US/" + ev->course);
    CHECK(e != nullptr);
    if (!e) return out;
    const Bytes file = disc->volume().read(*e);
    Vfs vfs(&disc->volume());
    out.track = race::make_track(vfs, file, ev->course);
    race::FieldConfig cfg;
    cfg.event = event;
    for (int k = 0; k < opponents; ++k) cfg.opponents.push_back(ev->entries[size_t(k) % ev->entries.size()]);
    cfg.max_opponents = opponents;
    cfg.made_grid = made;
    race::ExactFieldOptions o;
    o.data = program();
    o.course_file = std::make_shared<const Bytes>(file);
    o.course = ev->course;
    o.kind = ex::Conductor::SinglePlayer;
    o.laps = ev->laps;
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

// A race record of a big field (version 7): its cars and the made grid survive the file.
TEST(big_field_race_record) {
    ex::RaceRecord r;
    r.entries.resize(20);
    for (size_t i = 0; i < r.entries.size(); ++i) r.entries[i].grid_slot = int(i), r.entries[i].eq.car = 0x1000 + i;
    r.pads.assign(r.entries.size(), {});
    r.made_grid = true;
    const ex::RaceRecord y = ex::RaceRecord::parse(r.serialize());
    CHECK(y.entries.size() == 20 && y.made_grid);
    CHECK(y.entries[19].grid_slot == 19 && y.entries[19].eq.car == 0x1000 + 19);
    r.made_grid = false;
    CHECK(!ex::RaceRecord::parse(r.serialize()).made_grid);
}

// The HUD's place past GT3's twelve strings, made of the same glyphs.
TEST(big_field_rank_ordinal) {
    CHECK(race::rank_ordinal(1) == "1st" && race::rank_ordinal(2) == "2nd" && race::rank_ordinal(3) == "3rd");
    CHECK(race::rank_ordinal(11) == "11th" && race::rank_ordinal(12) == "12th" && race::rank_ordinal(13) == "13th");
    CHECK(race::rank_ordinal(21) == "21st" && race::rank_ordinal(22) == "22nd" && race::rank_ordinal(23) == "23rd");
    CHECK(race::rank_ordinal(31) == "31st" && race::rank_ordinal(32) == "32nd");
}
