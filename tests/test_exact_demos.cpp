// The disc's demo runs on the exact core (race/exact_demo.h): each demo's race record built from its save, stepped on an
// ExactRace from its frames, its result against the time the save records (GT3's own result for that run, recorded on a PS2).
// Needs GT3R_DISC (any region: every disc carries the NTSC-U runs).

#include "test_framework.h"

#include "disc/core_image.h"
#include "disc/disc.h"
#include "disc/volume.h"
#include "game/data/car_database.h"
#include "game/language_rules.h"
#include "race/backend.h"
#include "race/exact_demo.h"
#include "race/exact_field.h"
#include "race/exact_setup.h"
#include "sim/exact/params_build.h"
#include "sim/exact/race.h"
#include "sim/exact/race_record.h"
#include "vfs/vfs.h"

#include <cstdlib>
#include <memory>

using namespace gt3r;
namespace ex = gt3r::sim::exact;

namespace {

struct DemoRun {
    u32 time = 0; // the licence's judged time, the time trial's first lap (0 none)
    s32 result = 0;
    u32 steps = 0;
};

DemoRun run_demo(const ex::RaceRecord& rec, const ex::ProgramData& d, const data::CarDatabase& db, ByteSpan course_file,
                 size_t frames) {
    DemoRun out;
    ex::ProgramData dd = d; // the record's rules, as playback runs it (the NTSC-U runs every disc carries: NTSC-U's)
    if (!(dd.behaviour() == rec.behaviour) && dd.can_behave(rec.behaviour)) dd.set_behaviour(rec.behaviour);
    const ex::RaceSetup s = rec.setup(dd, db, ex::RaceCourse::load(course_file, dd));
    ex::ExactRace race(s);
    ex::RacePlayback play(&rec);
    while (!play.done()) {
        play.step(race);
        ++out.steps;
        if (rec.kind == ex::Conductor::License && !out.result && race.flow().result != 0 && race.flow().result != -2)
            out.result = race.flow().result, out.time = race.flow().result_time;
        if (rec.kind == ex::Conductor::TimeTrial && !out.time && race.cars()[0].best_lap != ex::kNoTime) out.time = race.cars()[0].best_lap;
        if ((out.result || out.time) && out.steps > frames) break;
    }
    return out;
}

} // namespace

TEST(exact_demo_records) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    CHECK(db.load(disc->volume()));
    std::optional<Bytes> core = disc->volume().read_program();
    CHECK(core.has_value());
    if (!core) return;
    const ex::ProgramData d = ex::ProgramData::load(CoreImage::decode(*core));
    int runs = 0, timed = 0, recorded = 0;
    std::string others;
    for (const std::string& code : disc->volume().list_dir("demo")) {
        const data::Event* ev = db.event(code);
        const Volume::Entry* de = disc->volume().find("demo/" + code);
        if (!ev || !de) continue;
        std::optional<race::DemoReplay> demo = race::DemoReplay::parse(disc->volume().read(*de));
        CHECK(demo.has_value());
        if (!demo) continue;
        std::string err;
        std::optional<ex::RaceRecord> rec = race::demo_record(*demo, *ev, db, d.fingerprint(), 600, &err);
        CHECK(rec.has_value());
        if (!rec) continue;
        // Through a replay file's record.
        const ex::RaceRecord back = ex::RaceRecord::parse(rec->serialize());
        CHECK(back.frames == rec->frames && back.entries.size() == 1);
        const Volume::Entry* ce = disc->volume().find(vfs.region().course_path(ev->course, vfs.language()));
        CHECK(ce != nullptr);
        if (!ce) continue;
        const DemoRun r = run_demo(back, d, db, disc->volume().read(*ce), demo->size());
        ++runs;
        if (r.time) ++timed;
        if (r.time && r.time == demo->time_ms) ++recorded;
        else others += " " + code + " " + std::to_string(r.time) + "/" + std::to_string(demo->time_ms);
    }
    std::printf("  %d demos on the exact core, %d timed, %d with the recorded time; the others (ours / recorded):%s\n", runs, timed,
                recorded, others.c_str());
    // Every one ends on its recorded time; on the PAL disc's data all but the AT02 time trial (which leaves the course there).
    const bool pal = disc->info().region == Region::Europe;
    CHECK(runs == 58 && recorded == (pal ? runs - 1 : runs));
    CHECK(!pal || others.rfind(" AT020001 ", 0) == 0);
}

// A licence test's car as a live run builds it (the test's car through race::exact_equipment: its record's parts and settings,
// then the course's tyres as the race fits them) against the car the test's demo was recorded with: the same spec in every test,
// so the demo's inputs drive a live run as they drove the recording. The rally tests run on their dirt tyres, S-5 and IB-1 /
// IB-2 on wet ones; B-8, IB-6 and IB-7 have settings of their own. Needs the NTSC-U disc (its licence cars and demo names).
TEST(exact_demo_licence_cars) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs an NTSC-U GT3R_DISC");
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    CHECK(db.load(disc->volume()));
    std::optional<Bytes> core = disc->volume().read_program();
    CHECK(core.has_value());
    if (!core) return;
    const ex::ProgramData d = ex::ProgramData::load(CoreImage::decode(*core));
    int tests = 0, same = 0, fitted = 0;
    std::string others;
    for (const std::string& code : disc->volume().list_dir("demo")) {
        const data::Event* ev = db.event(code);
        const Volume::Entry* de = disc->volume().find("demo/" + code);
        if (!ev || !de || code[0] != 'L') continue;
        std::optional<race::DemoReplay> demo = race::DemoReplay::parse(disc->volume().read(*de));
        CHECK(demo.has_value() && demo->car_record.size() >= ex::kEquipmentImageSize);
        if (!demo || demo->car_record.size() < ex::kEquipmentImageSize) continue;
        std::optional<ex::Equipment> eq = race::exact_equipment(db, race::ExactCarSource{game::license_car(db, *ev, vfs.language()).entry});
        CHECK(eq.has_value());
        if (!eq) continue;
        const u8 category = race::exact_course_category(db, ev->course);
        fitted += ex::course_tires(db, *eq, category);
        ex::Equipment recorded = ex::equipment_image(demo->car_record);
        ex::Spec ours{}, theirs{};
        ex::build_spec(db, d, *eq, 0, category, false, ours);
        ex::build_spec(db, d, recorded, 0, category, false, theirs);
        ++tests;
        if (ours == theirs) ++same;
        else others += " " + code;
    }
    std::printf("  %d licence tests: %d cars with their demo's spec (%d on the course's tyres); the others:%s\n", tests, same,
                fitted, others.c_str());
    CHECK(tests == 48 && same == tests);
    CHECK(fitted == 11); // R-1 .. R-8 (dirt), S-5, IB-1, IB-2 (wet)
    // Through the race's own set-up (race::make_exact_field, as the licence screen builds a run): R-4's car on its dirt tyres.
    const data::Event* ev = db.event("L0R0004");
    std::optional<Bytes> file = ev ? vfs.read(course_file_path(vfs, ev->course)) : std::nullopt;
    CHECK(file.has_value());
    if (!file) return;
    std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, ev->course);
    race::FieldConfig cfg;
    cfg.event = ev->label;
    cfg.max_opponents = 0;
    cfg.exact.data = std::make_shared<const ex::ProgramData>(d);
    cfg.exact.course_file = std::make_shared<const Bytes>(*file);
    cfg.exact.course = ev->course;
    cfg.exact.kind = ex::Conductor::License;
    cfg.exact.player.car = {game::license_car(db, *ev, vfs.language()).entry};
    std::string err;
    std::unique_ptr<race::RaceField> field = track ? race::make_exact_field(*track, db, cfg, &err) : nullptr;
    CHECK(field != nullptr);
    std::shared_ptr<const ex::RaceRecord> rec = field ? race::exact_recording(*field) : nullptr;
    CHECK(rec && !rec->entries.empty());
    if (rec && !rec->entries.empty())
        CHECK(db.label(rec->entries[0].eq.part[size_t(data::TableId::FrontTire)]) == "ft_celica_wrc_00_m");
}
