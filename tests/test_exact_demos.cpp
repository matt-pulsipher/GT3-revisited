// The disc's demo runs on the exact core (race/exact_demo.h): each demo's race record built from its save, stepped on an
// ExactRace from its frames, its result against the time the save records (GT3's own result for that run, recorded on a PS2).
// Needs GT3R_DISC (any region: every disc carries the NTSC-U runs).

#include "test_framework.h"

#include "disc/core_image.h"
#include "disc/disc.h"
#include "disc/volume.h"
#include "game/data/car_database.h"
#include "race/exact_demo.h"
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
