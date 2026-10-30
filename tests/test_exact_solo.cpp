// The solo modes on the exact core through the game's race code (race::ExactField with the time trial, licence, machine test
// and free run conductors): each run ends as its conductor decides, the free run's lap handlers keep its best lap and put
// the ghost car back on it, and the run's record replays to the same race (the free run's ghost from its records again).
// They need the program's tables and the courses from the disc (GT3R_DISC).

#include "test_framework.h"

#include "disc/core_image.h"
#include "disc/volume.h"
#include "game/data/car_database.h"
#include "race/backend.h"
#include "race/exact_field.h"
#include "race/analyzer.h"
#include "race/license.h"
#include "race/replay.h"
#include "race/results.h"
#include "sim/exact/race.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/race_record.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

std::optional<Bytes> course_file(const std::string& label) {
    Disc* d = test::test_disc();
    const Volume::Entry* e = d ? d->volume().find("courses/US/" + label) : nullptr;
    if (!e) return std::nullopt;
    return d->volume().read(*e);
}

// Aim at the car's AI line ahead; full throttle below a speed that falls with the aim's angle, the brake above it (its top:
// g_line_pace km/h).
float g_line_pace = 150.0f;
ex::PadFrame line_driver(const ex::Car& c, const ex::RaceCourse& course) {
    ex::PadFrame f;
    f.flags = 0x1F;
    f.kind = 2;
    ex::F dx, dy;
    const int line = c.ai.line ? c.ai.line : 6;
    ex::ai_target(c, course.ai, line, c.ai.next, ex::F(12.0f) + ex::F(0.6f) * c.speed, dx, dy);
    const float fx = c.fwd.x.host(), fy = c.fwd.y.host(), x = dx.host(), y = dy.host();
    const float angle = std::atan2(fx * y - fy * x, fx * x + fy * y);
    f.steer = s16(std::lround(std::clamp(angle * 2.5f, -1.0f, 1.0f) * 4096.0f));
    const float kmh = c.speed.host() * 3.6f, limit = g_line_pace - 300.0f * std::fabs(angle);
    if (kmh < limit) f.throttle = 4096;
    else f.brake = 2048;
    return f;
}

sim::ControlInputs inputs_of(const ex::PadFrame& f) {
    sim::ControlInputs in;
    in.steer = -float(f.steer) / 4096.0f;
    in.throttle = float(f.throttle) / 4096.0f;
    in.brake = float(f.brake) / 4096.0f;
    in.controller_kind = 2;
    return in;
}

struct SoloRun {
    std::shared_ptr<const ex::RaceRecord> record; // the run's
    int steps = 0, laps = 0, ghost_restarts = 0, ghost_moving = 0;
    bool finished = false, best = false, ghost = false;
    bool restart_before_ghost = false; // the ghost car restarted before this run made a ghost (a ghost from the runs before)
    bool record_has_ghost = false;     // the run's record starts with a ghost (RaceRecord::recorders)
    s32 result = 0;
    u32 result_time = 0;
    u32 best_lap = ex::kNoTime;
    std::vector<s32> events;    // the race's posts over the run (types)
    std::optional<bool> replay; // the run's record played again matched the race
};

// A solo run of `event` under `kind` with the line driver on the pad, until car 0 finishes (or `max_steps`), then its
// record played back.
SoloRun solo(const std::string& event, ex::Conductor kind, int max_steps, int min_laps = 0,
             std::shared_ptr<ex::SoloRecorders> visit = nullptr, std::string car_label = {}) {
    SoloRun out;
    data::CarDatabase& db = *database();
    const data::Event* ev = db.event(event);
    CHECK(ev != nullptr);
    if (!ev) return out;
    const std::string car = !car_label.empty() ? car_label : ev->entries.empty() ? "r34_gtr_v_99" : ev->entries[0];
    std::optional<Bytes> file = course_file(ev->course);
    CHECK(file.has_value());
    if (!file) return out;
    Vfs vfs(&test::test_disc()->volume());
    std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, ev->course);
    race::FieldConfig cfg;
    cfg.event = event;
    race::ExactFieldOptions o;
    o.data = program();
    o.course_file = *file;
    o.course = ev->course;
    o.kind = kind;
    o.laps = ev->laps;
    o.player.car = {car};
    o.free_run = visit;
    const race::LicenseTimes lt = race::license_times(*ev);
    for (size_t k = 0; k < 3; ++k) o.licence_times[k] = u32(std::max(0, lt.ms[k]));
    cfg.exact = o;
    std::string err;
    std::unique_ptr<race::RaceField> field = race::RaceField::create(*track, db, cfg, &err);
    if (!field) std::printf("  %s: %s\n", event.c_str(), err.c_str());
    CHECK(field != nullptr);
    if (!field) return out;
    const ex::ExactRace& r = *race::exact_race_of(*field);
    // (The free run's ghost car is not an opponent; free practice runs the event's field.)
    CHECK(kind == ex::Conductor::FreePractice ? field->opponents() > 0 : field->opponents() == 0);
    for (int k = 0; k < max_steps; ++k) {
        field->player().step(inputs_of(line_driver(r.cars()[0], r.course())), 1.0 / 60.0);
        ++out.steps;
        for (const ex::RaceEvent& e : r.events()) out.events.push_back(e.type);
        for (const ex::RecorderEvent& e : r.flow().recorder) {
            out.ghost_restarts += e.kind == ex::RecorderEvent::ghost_restart;
            out.restart_before_ghost = out.restart_before_ghost || (e.kind == ex::RecorderEvent::ghost_restart && !out.ghost);
            out.best = out.best || e.kind == ex::RecorderEvent::best;
            out.ghost = out.ghost || e.kind == ex::RecorderEvent::ghost;
        }
        if (r.size() > 1 && out.ghost_restarts > 0 && r.cars()[1].speed.host() > 5.0f) ++out.ghost_moving;
        out.laps = std::max(0, int(r.cars()[0].laps) - 1);
        if (r.cars()[0].finished != 0 && !out.finished) out.finished = true, k = std::max(k, max_steps - 120); // (two seconds on)
        if (min_laps > 0 && out.laps >= min_laps) break;
    }
    out.result = r.flow().result, out.result_time = r.flow().result_time;
    out.best_lap = r.cars()[0].best_lap;
    // The run's record played back: the same race (its hash at the end).
    std::shared_ptr<const ex::RaceRecord> rec = race::exact_recording(*field);
    CHECK(rec != nullptr);
    if (!rec) return out;
    out.record_has_ghost = rec->recorders && !rec->recorders->ghost_empty;
    out.record = rec;
    rec = std::make_shared<const ex::RaceRecord>(ex::RaceRecord::parse(rec->serialize())); // (as a replay file keeps it)
    race::FieldConfig pc = cfg;
    pc.exact.playback = rec;
    std::unique_ptr<race::RaceField> play = race::RaceField::create(*track, db, pc, &err);
    CHECK(play != nullptr);
    if (!play) return out;
    for (u32 k = 0; k < rec->frames; ++k) play->player().step({}, 1.0 / 60.0);
    out.replay = race::exact_playback_matches(*play);
    std::printf("  %s (%s): %d steps, %d laps, finished %s, result %d, best lap %u, ghost restarts %d (moving %d), replay %s\n",
                event.c_str(), ex::conductor_name(kind), out.steps, out.laps, out.finished ? "yes" : "no", int(out.result),
                out.best_lap, out.ghost_restarts, out.ghost_moving,
                out.replay ? (*out.replay ? "matches" : "DIFFERS") : "not done");
    return out;
}

// A solo run's record played headless to its end (LOAD GHOST's way): its recorders as the race's end leaves them.
std::optional<ex::SoloRecorders> played_recorders(const std::string& event, std::shared_ptr<const ex::RaceRecord> rec) {
    data::CarDatabase& db = *database();
    const data::Event* ev = db.event(event);
    std::optional<Bytes> file = ev ? course_file(ev->course) : std::nullopt;
    if (!file || !rec) return std::nullopt;
    Vfs vfs(&test::test_disc()->volume());
    std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, ev->course);
    race::FieldConfig cfg;
    cfg.event = event;
    race::ExactFieldOptions o;
    o.data = program();
    o.course_file = *file;
    o.course = ev->course;
    o.kind = rec->kind;
    o.playback = rec;
    cfg.exact = o;
    std::string err;
    std::unique_ptr<race::RaceField> play = race::RaceField::create(*track, db, cfg, &err);
    if (!play || !race::exact_solo_recorders(*play)) return std::nullopt;
    for (u32 k = 0; k < rec->frames; ++k) play->player().step({}, 1.0 / 60.0);
    ex::SoloRecorders done = *race::exact_solo_recorders(*play);
    done.end();
    return done;
}

bool posted(const SoloRun& s, s32 type) { return std::find(s.events.begin(), s.events.end(), type) != s.events.end(); }

} // namespace

TEST(exact_game_solo_modes) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    // The time trial (Trial Mountain): the AI's run-up, then one timed lap ends the run (a course-out would: event 10).
    SoloRun tt = solo("AT020001", ex::Conductor::TimeTrial, 12000);
    CHECK(tt.finished);
    CHECK(posted(tt, 4)); // the run-up's hand-back
    CHECK(posted(tt, 5) && tt.best_lap != ex::kNoTime && !posted(tt, 10)); // (this driver keeps it on the road)
    CHECK(tt.replay && *tt.replay);
    // A licence test (Grand Valley): judged by the conductor (a medal, failed, or out of the course).
    SoloRun lic = solo("L0A0001", ex::Conductor::License, 12000);
    CHECK(lic.finished);
    CHECK(lic.result != 0 || posted(lic, 9) || posted(lic, 5));
    CHECK(lic.replay && *lic.replay);
    // The 0-400 m machine test: the split line ends it with the time to it.
    SoloRun mt = solo("M400", ex::Conductor::MachineTest, 3000);
    CHECK(mt.finished);
    CHECK(mt.best_lap != ex::kNoTime && mt.best_lap < 30000);
    CHECK(mt.replay && *mt.replay);
    // The free run (Deep Forest): laps on and on; the first good lap becomes the best lap and the ghost, the ghost car is put
    // back on its start at each crossing after it and driven from its recording; the replay drives the ghost again.
    SoloRun fr = solo("AT040001", ex::Conductor::FreeRun, 40000, 4);
    CHECK(!fr.finished);
    CHECK(fr.laps >= 3);
    CHECK(fr.best && fr.ghost);
    CHECK(fr.ghost_restarts >= 2);
    CHECK(fr.ghost_moving > 600); // (the ghost car runs its lap)
    CHECK(fr.replay && *fr.replay);
    // GT mode's free run before a race (free practice, Sunday Cup's Super Speedway): laps on, the session's best.
    SoloRun fp = solo("Gsunday_e_0001", ex::Conductor::FreePractice, 30000, 2);
    CHECK(!fp.finished && fp.laps >= 2 && fp.best_lap != ex::kNoTime);
    CHECK(fp.replay && *fp.replay);
}

// The free practice before a GT race (gt_mode §19), as GT3 runs it: the race's field on the course (car 0 from its pit box,
// the others flying from the line behind), every car keeping its best lap; the grid after it by those laps when the player
// set one (race::practice_grid), which the race then starts on; a second practice starts each car's best lap from its
// entry's of the first (entry + 0x11D8, FieldConfig::entry_best).
TEST(exact_game_free_practice_field) {
    // (Every region: the disc's own database, program and course files.)
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    static data::CarDatabase db;
    static std::shared_ptr<const ex::ProgramData> data;
    if (!data) {
        CHECK(db.load(disc->volume()));
        std::optional<Bytes> core = disc->volume().read_program();
        CHECK(core.has_value());
        if (!core) return;
        data = std::make_shared<const ex::ProgramData>(ex::ProgramData::load(CoreImage::decode(*core)));
    }
    const std::string event = "Gsunday_e_0001"; // Sunday Cup race 1, Super Speedway
    const data::Event* ev = db.event(event);
    CHECK(ev != nullptr && !ev->entries.empty());
    if (!ev || ev->entries.empty()) return;
    Vfs vfs(&disc->volume());
    const std::string path = course_file_path(vfs, ev->course);
    std::optional<Bytes> file = path.empty() ? std::nullopt : vfs.read(path);
    CHECK(file.has_value());
    if (!file) return;
    std::vector<std::string> opponents;
    for (size_t k = 1; k < ev->entries.size() && opponents.size() < 5; ++k)
        if (!ev->entries[k].empty() && ev->entries[k] != "-") opponents.push_back(ev->entries[k]);
    std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, ev->course);
    auto config = [&](ex::Conductor kind) {
        race::FieldConfig cfg;
        cfg.event = event;
        cfg.opponents = opponents;
        cfg.max_opponents = int(opponents.size());
        cfg.grid_by_power = true;
        race::ExactFieldOptions o;
        o.data = data;
        o.course_file = *file;
        o.course = ev->course;
        o.kind = kind;
        o.laps = kind == ex::Conductor::FreePractice ? 0 : ev->laps;
        o.player.car = {ev->entries[0]};
        cfg.exact = o;
        return cfg;
    };
    std::string err;
    race::FieldConfig pc = config(ex::Conductor::FreePractice);
    std::unique_ptr<race::RaceField> practice = race::RaceField::create(*track, db, pc, &err);
    if (!practice) std::printf("  free practice: %s\n", err.c_str());
    CHECK(practice != nullptr);
    if (!practice) return;
    CHECK(practice->opponents() == int(opponents.size()));
    const ex::ExactRace& r = *race::exact_race_of(*practice);
    for (int k = 0; k < r.size(); ++k) CHECK(r.cars()[size_t(k)].best_lap == ex::kNoTime);
    CHECK(practice->race_time() >= 0); // no countdown (start type -1): car 0 waits only for its pit-box start
    // The player along the line until two laps are timed; the others are driven by the AI.
    // The player's car leaves its box after the 2 s pit start (the computer drives it out: driver kind 4), and the pit lane
    // posts its exit (6, the START picture) for it.
    int steps = 0, left_box = -1;
    bool pit_exit = false;
    for (; steps < 40000 && std::max(0, int(r.cars()[0].laps) - 1) < 2; ++steps) {
        practice->player().step(inputs_of(line_driver(r.cars()[0], r.course())), 1.0 / 60.0);
        if (left_box < 0 && r.cars()[0].driver_kind == 4) left_box = steps;
        for (const ex::PitEvent& p : r.pit_events()) pit_exit = pit_exit || (p.type == 6 && p.car == 0);
    }
    CHECK(left_box >= 100 && left_box <= 140);
    CHECK(pit_exit);
    const race::ExactBestLaps b = race::exact_best_laps(*practice);
    CHECK(b.car.size() == opponents.size() + 1 && b.entry.size() == b.car.size());
    std::string laps;
    int clean = 0;
    for (size_t k = 0; k < b.car.size(); ++k) {
        laps += " " + std::to_string(b.car[k]) + " / " + std::to_string(b.entry[k]);
        clean += b.entry[k] != ex::kNoTime;
    }
    std::printf("  free practice: %d steps, best laps (car / entry)%s\n", steps, laps.c_str());
    for (size_t k = 0; k < b.car.size(); ++k) {
        CHECK(b.car[k] != ex::kNoTime); // every car set a lap (the AI's flying from behind the line)
        CHECK(b.car[k] <= b.entry[k]);  // (the car's counts every lap, the entry's the clean ones)
    }
    CHECK(clean > 0); // (the records are on: configuration 0, a live run)
    CHECK(race::practice_regrids(b.car));
    const std::vector<int> grid = race::practice_grid(b.car);
    // The race on that grid: entry k on slot grid[k].
    race::FieldConfig rc = config(ex::Conductor::SinglePlayer);
    rc.grid_slots = grid;
    std::unique_ptr<race::RaceField> racef = race::RaceField::create(*track, db, rc, &err);
    CHECK(racef != nullptr);
    if (!racef) return;
    std::shared_ptr<const ex::RaceRecord> rec = race::exact_recording(*racef);
    CHECK(rec && rec->entries.size() == grid.size());
    if (rec && rec->entries.size() == grid.size())
        for (size_t k = 0; k < grid.size(); ++k) CHECK(rec->entries[k].grid_slot == grid[k]);
    // The pre-race list in that order: slot 0 the fastest.
    const std::vector<race::RaceField::Standing> st = racef->standings();
    CHECK(st.size() == grid.size());
    for (size_t p = 0; p < st.size(); ++p) CHECK(grid[size_t(st[p].car + 1)] == int(p));
    // A grid that does not fit the field is not used: the event's.
    rc.grid_slots = {0, 0, 1};
    std::unique_ptr<race::RaceField> other = race::RaceField::create(*track, db, rc, &err);
    CHECK(other && race::exact_recording(*other)->entries[0].grid_slot == int(opponents.size()));
    // A second practice on that grid: each car's best lap from its entry's at the start (the restart).
    pc.grid_slots = grid;
    pc.entry_best = b.entry;
    std::unique_ptr<race::RaceField> again = race::RaceField::create(*track, db, pc, &err);
    CHECK(again != nullptr);
    if (!again) return;
    const ex::ExactRace& r2 = *race::exact_race_of(*again);
    for (size_t k = 0; k < b.entry.size() && k < size_t(r2.size()); ++k) CHECK(r2.cars()[k].best_lap == b.entry[k]);
    // Without the entries' best laps every car starts with none.
    pc.entry_best.clear();
    std::unique_ptr<race::RaceField> fresh = race::RaceField::create(*track, db, pc, &err);
    CHECK(fresh != nullptr);
    if (fresh)
        for (const ex::Car& c : race::exact_race_of(*fresh)->cars()) CHECK(c.best_lap == ex::kNoTime);
}

// The free run's best lap and ghost stay from run to run of a visit (GT3 keeps the race object, with its best lap's store and
// the ghost's playback, until the player leaves): a restart's ghost car is put on the last run's ghost at its first crossing,
// and its replay (its record keeps the recorders it started with) is the same race again.
TEST(exact_game_free_run_visit) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    auto visit = std::make_shared<ex::SoloRecorders>(true);
    SoloRun first = solo("AT040001", ex::Conductor::FreeRun, 40000, 3, visit);
    CHECK(first.ghost && !first.restart_before_ghost && !first.record_has_ghost);
    CHECK(first.replay && *first.replay);
    CHECK(!visit->ghost_empty && visit->snapshot[2].time != ex::kNoTime);
    const u32 best = visit->snapshot[2].time;
    const ex::SoloRecorders after_first = *visit;
    SoloRun again = solo("AT040001", ex::Conductor::FreeRun, 20000, 1, visit); // the restart
    CHECK(again.record_has_ghost);
    CHECK(again.restart_before_ghost); // (the first crossing: the visit's ghost)
    CHECK(again.ghost_moving > 300);
    CHECK(again.replay && *again.replay);
    CHECK(visit->snapshot[2].time <= best);
    // A visit's first run on a fresh field has none.
    SoloRun fresh = solo("AT040001", ex::Conductor::FreeRun, 20000, 1);
    CHECK(!fresh.restart_before_ghost && !fresh.record_has_ghost);
    // LOAD GHOST: the first run's record played headless gives its recorders as the run left them (the record memory byte
    // for byte, the best lap), and its best lap becomes a fresh visit's ghost, which the next run's ghost car plays.
    std::optional<ex::SoloRecorders> loaded = played_recorders("AT040001", first.record);
    CHECK(loaded.has_value());
    if (!loaded) return;
    CHECK(loaded->pool1 == after_first.pool1 && loaded->pool0 == after_first.pool0);
    CHECK(loaded->best_time() == best);
    auto other = std::make_shared<ex::SoloRecorders>(true);
    CHECK(other->ghost_from_best(*loaded));
    CHECK(!other->ghost_empty && other->snapshot[0].time == best);
    CHECK(!ex::SoloRecorders(true).ghost_from_best(ex::SoloRecorders(true))); // (no best lap: nothing)
    SoloRun with = solo("AT040001", ex::Conductor::FreeRun, 20000, 1, other);
    CHECK(with.record_has_ghost && with.restart_before_ghost);
    CHECK(with.ghost_moving > 300);
    CHECK(with.replay && *with.replay);
}

// The data logger's pass over a replay (GT3's Analyzer): the run's record played with race info + 0x70 / + 0x74 set records car
// 0's table at the course's logger marks (an entry a sector, timed from the lap's start to its end), the pass is the same
// race but for the logger's mark count (F +0x493), and its bins (LoggerBinner) cover the lap from the line.
TEST(exact_game_logger_pass) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    SoloRun tt = solo("AT020001", ex::Conductor::TimeTrial, 12000);
    CHECK(tt.finished && tt.record != nullptr);
    if (!tt.record) return;
    data::CarDatabase& db = *database();
    const data::Event* ev = db.event("AT020001");
    std::optional<Bytes> file = course_file(ev->course);
    Vfs vfs(&test::test_disc()->volume());
    std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, ev->course);
    race::FieldConfig cfg;
    cfg.event = "AT020001";
    race::ExactFieldOptions o;
    o.data = program();
    o.course_file = *file;
    o.course = ev->course;
    o.kind = ex::Conductor::TimeTrial;
    o.player.car = {ev->entries[0]};
    o.playback = tt.record;
    o.logger = o.logger_record = true;
    cfg.exact = o;
    std::string err;
    std::unique_ptr<race::RaceField> f = race::RaceField::create(*track, db, cfg, &err);
    CHECK(f != nullptr);
    if (!f) return;
    const ex::ExactRace& r = *race::exact_race_of(*f);
    race::LoggerBinner bins(r.course().query->lap_length());
    race::FieldConfig pc = cfg;
    pc.exact.logger = pc.exact.logger_record = false;
    std::unique_ptr<race::RaceField> plain = race::RaceField::create(*track, db, pc, &err);
    bool same = true;
    for (u32 k = 0; k < tt.record->frames; ++k) {
        f->player().step({}, 1.0 / 60.0);
        plain->player().step({}, 1.0 / 60.0);
        bins.add(r.cars()[0], double(r.flow().clock) / 3000.0, k + 1);
        if (same) { // (the logger's own mark count aside: the race is the same)
            ex::Car a = r.cars()[0];
            a.x493 = race::exact_race_of(*plain)->cars()[0].x493;
            const std::string d = ex::first_difference(a, race::exact_race_of(*plain)->cars()[0]);
            if (!d.empty()) std::printf("  step %u: %s\n", k, d.c_str()), same = false;
        }
    }
    bins.finish();
    CHECK(same); // (the logger only counts its marks, times and records them)
    const auto* tables = race::exact_logger_tables(*f);
    CHECK(tables && !tables->empty());
    if (!tables || tables->empty()) return;
    const int marks = r.course().query->logger_marks();
    const race::AnalyzerTrack t = race::logger_track(bins, &(*tables)[0], 60.0);
    std::printf("  logger pass: %zu entries (%d marks), %zu sectors timed, lap %.3f s (best lap %u ms), %zu bins of %.3f m\n",
                (*tables)[0].size(), marks, t.sector_times.size(), t.trace.time, tt.best_lap, bins.bins().size(),
                double(bins.width().host()));
    CHECK(int(t.sector_times.size()) == marks + 1);
    CHECK(std::fabs(t.trace.time - double(tt.best_lap) / 1000.0) < 0.0015); // (the lap's entry ends at the line)
    const float lap = r.course().query->lap_length().host();
    CHECK(float(bins.bins().size()) * bins.width().host() > 0.98f * lap); // (the lap from the line)
    CHECK(t.trace.speed_kmh[bins.bins().size() / 2] > 30.0f);
}

// GT3's Analyzer race (exact_field.h ExactFieldOptions::analysis): two time trial runs, track A's replay with track B's car as its
// ghost car playing B's recording, the logger recording both cars' tables. Car 0 drives track A's run again (B's ghost car does
// not touch it; only the flap pendulums, which draw the race's random register as the other car's tyre effects do, differ);
// each car's table has its lap's entries; the REPLAY view's jumps (exact_logger_jump) put car 0 back where
// the pass had it at the entry's frame, and on from there it drives the pass's lap again.
TEST(exact_game_analysis) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    const std::string car = "r34_gtr_v_99";
    SoloRun a = solo("AT020001", ex::Conductor::TimeTrial, 12000, 0, nullptr, car);
    g_line_pace = 125.0f; // (track B slower)
    SoloRun b = solo("AT020001", ex::Conductor::TimeTrial, 14000, 0, nullptr, car);
    g_line_pace = 150.0f;
    CHECK(a.record && b.record && !a.record->run_recording.empty());
    if (!a.record || !b.record) return;
    data::CarDatabase& db = *database();
    const data::Event* ev = db.event("AT020001");
    std::optional<Bytes> file = course_file(ev->course);
    Vfs vfs(&test::test_disc()->volume());
    std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, ev->course);
    race::FieldConfig cfg;
    cfg.event = "AT020001";
    race::ExactFieldOptions o;
    o.data = program();
    o.course_file = *file;
    o.course = ev->course;
    o.kind = ex::Conductor::TimeTrial;
    o.player.car = {car};
    o.playback = a.record;
    o.logger = o.logger_record = true;
    o.analysis = true, o.analysis_b = b.record;
    cfg.exact = o;
    std::string err;
    std::unique_ptr<race::RaceField> f = race::RaceField::create(*track, db, cfg, &err);
    race::FieldConfig pc = cfg;
    pc.exact.logger = pc.exact.logger_record = pc.exact.analysis = false;
    pc.exact.analysis_b = nullptr;
    std::unique_ptr<race::RaceField> plain = race::RaceField::create(*track, db, pc, &err); // track A's replay alone
    CHECK(f != nullptr && plain != nullptr);
    if (!f || !plain) return;
    const ex::ExactRace& r = *race::exact_race_of(*f);
    CHECK(r.size() == 2 && r.ghost_car() == 1 && r.cars()[1].ghost != 0);
    std::map<u32, ex::Car> pass; // car 0 by its device's frame
    bool same = true;
    u32 steps = 0;
    for (; steps < a.record->frames + 600 && !r.recorders()->player.rec.done; ++steps) {
        f->player().step({}, 1.0 / 60.0);
        if (steps < a.record->frames) plain->player().step({}, 1.0 / 60.0);
        pass[race::exact_device_frame(*f, 0)] = r.cars()[0];
        if (same && steps + 1 < a.record->frames) { // (but its last frame: the playback's is empty)
            const ex::Car &c = r.cars()[0], &p = race::exact_race_of(*plain)->cars()[0];
            if (!(c.pos.x.bits == p.pos.x.bits && c.pos.y.bits == p.pos.y.bits && c.pos.z.bits == p.pos.z.bits && c.vel.x.bits == p.vel.x.bits &&
                  c.vel.y.bits == p.vel.y.bits && c.laps == p.laps))
                std::printf("  step %u: car 0's motion differs\n", steps), same = false;
        }
    }
    CHECK(same);
    const auto* tables = race::exact_logger_tables(*f);
    CHECK(tables && tables->size() >= 2 && !(*tables)[0].empty() && !(*tables)[1].empty());
    if (!tables || tables->size() < 2 || (*tables)[0].empty()) return;
    std::printf("  the Analyzer's race: %u steps, track A %zu entries, track B %zu\n", steps, (*tables)[0].size(), (*tables)[1].size());
    race::exact_logger_record(*f, false);
    int jumps = 0, on = 0;
    for (const auto& [k, e] : (*tables)[0]) {
        if (k < 0 || k % 4 != 0) continue;
        CHECK(race::exact_logger_jump(*f, k, 0));
        ++jumps;
        for (int j = 0; j < 300; ++j) {
            f->player().step({}, 1.0 / 60.0);
            const auto it = pass.find(race::exact_device_frame(*f, 0));
            if (it == pass.end()) break;
            const ex::Car& c = r.cars()[0];
            if (!(c.pos.x.bits == it->second.pos.x.bits && c.pos.y.bits == it->second.pos.y.bits && c.vel.x.bits == it->second.vel.x.bits)) {
                std::printf("  the jump to entry %d: off the pass after %d steps\n", k, j);
                break;
            }
            ++on;
        }
    }
    std::printf("  %d jumps, %d steps after them on the pass's line\n", jumps, on);
    CHECK(jumps > 0 && on > 250 * jumps);
    // GT3R_ANALYZER_REPLAYS=<dir>: both runs saved as replays there (for the Replay Theater's Analyzer in the app).
    if (const char* dir = std::getenv("GT3R_ANALYZER_REPLAYS"))
        for (const SoloRun* s : {&a, &b}) {
            race::Replay r;
            r.course = ev->course, r.car = car, r.event.clear();
            r.kind = race::ReplayKind::TimeTrial, r.laps = 1;
            r.tick_hz = 60.0;
            r.inputs.assign(s->record->frames, sim::ControlInputs{});
            r.exact = s->record;
            const std::optional<std::string> path = race::save_replay(r, dir);
            std::printf("  saved %s\n", path ? path->c_str() : "(failed)");
        }
}

// Pause EXIT as a free run's finish (presentation §E5), as GT3's free run makes it: the race's record is closed at the press
// while the race runs on, so the replay ends there (its frames are the run's ticks at the press, and played back it ends on
// the race as it was then); the race's own recordings are closed and the end-of-run driver takes the car (end of run, mode
// 4: driver kind 1 off the pit surface), which drives on with no input from the pad.
TEST(exact_game_exit_as_finish) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    data::CarDatabase& db = *database();
    const data::Event* ev = db.event("AT040001");
    CHECK(ev != nullptr);
    if (!ev) return;
    std::optional<Bytes> file = course_file(ev->course);
    if (!file) return;
    Vfs vfs(&test::test_disc()->volume());
    std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, ev->course);
    race::FieldConfig cfg;
    cfg.event = "AT040001";
    race::ExactFieldOptions o;
    o.data = program();
    o.course_file = *file;
    o.course = ev->course;
    o.kind = ex::Conductor::FreeRun;
    o.player.car = {ev->entries.empty() ? "r34_gtr_v_99" : ev->entries[0]};
    cfg.exact = o;
    std::string err;
    std::unique_ptr<race::RaceField> f = race::RaceField::create(*track, db, cfg, &err);
    CHECK(f != nullptr);
    if (!f) return;
    const ex::ExactRace& r = *race::exact_race_of(*f);
    u32 steps = 0;
    bool vibrated = false; // the pad drives the car: the race sends its vibration
    for (; steps < 40000 && int(r.cars()[0].laps) - 1 < 1; ++steps) {
        f->player().step(inputs_of(line_driver(r.cars()[0], r.course())), 1.0 / 60.0);
        if (const sim::VehicleTelemetry* t = f->player().telemetry()) vibrated |= t->pad_output && t->pad_rumble > 0;
    }
    CHECK(int(r.cars()[0].laps) - 1 >= 1);
    CHECK(vibrated);
    CHECK(race::exact_exit_finish(*f, false)); // the press
    CHECK(r.cars()[0].finished != 0 && r.cars()[0].driver_kind == 1);
    CHECK(r.recorders() && r.recorders()->player.rec.done && r.recorders()->lap.rec.done);
    float slowest = 1e9f;
    bool sent = false; // a car the AI drives sends no vibration
    for (int k = 0; k < 900; ++k) { // the run on: 15 s, the AI driving
        f->player().step({}, 1.0 / 60.0);
        if (k >= 120) slowest = std::min(slowest, r.cars()[0].speed.host());
        if (const sim::VehicleTelemetry* t = f->player().telemetry()) sent |= t->pad_output;
    }
    CHECK(slowest > 5.0f);
    CHECK(!sent);
    std::shared_ptr<const ex::RaceRecord> rec = race::exact_recording(*f);
    CHECK(rec != nullptr);
    if (!rec) return;
    CHECK(rec->frames == steps);
    race::FieldConfig pc = cfg;
    pc.exact.playback = std::make_shared<const ex::RaceRecord>(ex::RaceRecord::parse(rec->serialize()));
    std::unique_ptr<race::RaceField> play = race::RaceField::create(*track, db, pc, &err);
    CHECK(play != nullptr);
    if (!play) return;
    for (u32 k = 0; k < rec->frames; ++k) play->player().step({}, 1.0 / 60.0);
    const std::optional<bool> m = race::exact_playback_matches(*play);
    CHECK(m && *m);
    std::printf("  free run: EXIT at frame %u (after a lap), 900 steps on (the AI driving, at least %.1f m/s), record %u frames, "
                "replay %s\n",
                steps, double(slowest), rec->frames, m ? (*m ? "matches" : "DIFFERS") : "not done");
}

// A licence run slower than bronze, as GT3 judges and presents it: the race flow fails it (event 9) with the result 0 and its
// time (race info + 0x40 / + 0x44), and the licence display shows TIME EXPIRED with that time (race::license_outcome: a
// finished run, no medal, the time); a run out of the course would be -1, FAIL with no time.
TEST(exact_game_licence_over_the_times) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    const data::Event* ev = database()->event("L0A0004");
    CHECK(ev != nullptr);
    if (!ev) return;
    const race::LicenseTimes lt = race::license_times(*ev);
    g_line_pace = 60.0f; // (well under the pace a medal needs)
    SoloRun slow = solo("L0A0004", ex::Conductor::License, 30000);
    g_line_pace = 150.0f;
    CHECK(slow.finished);
    CHECK(slow.result == 0 && posted(slow, 9));
    CHECK(slow.result_time > u32(lt.ms[2])); // slower than bronze
    const race::LicenseOutcome o = race::license_outcome(slow.result, slow.result_time, posted(slow, 9), -1);
    CHECK(o.state == race::LicenseRun::State::Finished && o.medal < 0);
    CHECK(std::fabs(o.time - double(slow.result_time) / 1000.0) < 1e-9);
    std::printf("  L0A0004 at %.0f km/h: result %d, time %.3f s (bronze %.3f s): TIME EXPIRED with the time\n", 60.0,
                int(slow.result), o.time, lt.ms[2] / 1000.0);
}
