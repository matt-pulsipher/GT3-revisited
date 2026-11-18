// Gran Turismo Concept's race rules on the exact core (the Concept spec): the course license's conductor as the later
// builds have it (what is out of the course, what a contact does) and the test with the pace car (the Training conductor).
// The rules alone need no disc; the runs need the Concept discs (GT3R_DISC_CONCEPT, images separated by ';').

#include "test_framework.h"

#include "concept_discs.h"
#include "disc/core_image.h"
#include "disc/disc.h"
#include "game/data/car_database.h"
#include "race/backend.h"
#include "race/exact_field.h"
#include "race/exact_setup.h"
#include "race/license.h"
#include "sim/exact/race.h"
#include "sim/exact/race_flow.h"
#include "sim/exact/race_inputs.h"
#include "sim/exact/race_record.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <memory>

using namespace gt3r;
namespace ex = gt3r::sim::exact;
using sim::Program;

// What the conductors answer: GT3's licence conductor, the later builds' (soft ground is not out of the course, pit mode
// 0), and the pace-car test's five own answers.
TEST(concept_licence_conductor_answers) {
    using K = ex::Conductor;
    CHECK(ex::licence_kind(K::License) && ex::licence_kind(K::Training) && !ex::licence_kind(K::TimeTrial));
    // GT3: the pit stretch and, off dirt, soft ground.
    const ex::CourseOut gt3 = ex::course_out_rule(K::License, 0, Program::NtscU);
    CHECK(gt3.on && gt3.pit == 1 && gt3.soft == 1);
    CHECK(ex::course_out_rule(K::License, 2, Program::Pal).soft == 0);
    CHECK(ex::pit_mode(K::License, 0, Program::NtscJ) == 3);
    for (Program p : {Program::ConceptTokyo, Program::ConceptSeoul, Program::ConceptSeoul2, Program::ConceptGenevaEu,
                      Program::ConceptGenevaAsia})
        for (K k : {K::License, K::Training}) {
            const ex::CourseOut r = ex::course_out_rule(k, 0, p);
            CHECK(r.on && r.pit == 1 && r.soft == 0);
            CHECK(ex::pit_mode(k, 0, p) == 0);
        }
    // The time trial's is GT3's in every build's table here (not read for the later builds: nothing reaches it).
    CHECK(ex::course_out_rule(K::TimeTrial, 0, Program::ConceptGenevaEu).soft == 1);
    // The pace-car test: the event's rubber band and its pass as a single race, no pits.
    ex::RaceConfig cfg{};
    cfg[0x22] = 1;
    CHECK(ex::rubber_band_answer(K::Training, cfg, 0) && !ex::rubber_band_answer(K::License, cfg, 0));
    CHECK(ex::rubber_band_mode(K::Training, cfg) == 1 && ex::rubber_band_mode(K::License, cfg) == -1);
    CHECK(!ex::has_pits(K::Training) && !ex::has_pits(K::License));
    CHECK(!ex::rubber_band_cap(K::Training));
    // A hit sets the flag for car 0 alone under both (a wall hit over 10 m/s of speed change, any hit of kind 2; never a car's).
    ex::Car c{};
    c.p.inv_mass = ex::F(0.001f);
    ex::hit_course_out(c, K::Training, 1, ex::F(1.0e6f));
    CHECK(c.course_out == 0);
    ex::hit_course_out(c, K::Training, 0, ex::F(20000.0f));
    CHECK(c.course_out == 1);
    ex::Car other{};
    other.index = 1, other.p.inv_mass = ex::F(0.001f);
    ex::hit_course_out(other, K::Training, 0, ex::F(20000.0f));
    CHECK(other.course_out == 0);
}

// The judge (Rule::LicenceJudge): GT3 ends the test on the hit flag; the later builds clear it and go on. A computer car (the pace car) is never
// judged: out of the course it is left alone.
TEST(concept_licence_judge) {
    auto judge = [](ex::Conductor k, Program build, u8 flags, s8 control, std::vector<ex::RaceEvent>& events, s32& result) {
        std::vector<ex::Car> cars(1);
        cars[0].course_out = flags, cars[0].control = control;
        ex::RaceFlowInputs in;
        in.kind = k, in.behaviour = {build, 60}; // (Rule::LicenceJudge: the build's)
        in.config = {100, 1, 3};
        ex::RaceFlow flow;
        flow.result = -2;
        ex::race_mode_clock(cars, in, flow, events, {});
        result = flow.result;
        return cars[0];
    };
    std::vector<ex::RaceEvent> ev;
    s32 result = 0;
    // GT3: a hit fails the test (event 9, result -1).
    ex::Car c = judge(ex::Conductor::License, Program::NtscU, 1, 0, ev, result);
    CHECK(c.finished != 0 && result == -1 && ev.size() == 1 && ev[0].type == 9);
    // The later builds: the flag is cleared, the run goes on.
    for (ex::Conductor k : {ex::Conductor::License, ex::Conductor::Training}) {
        ev.clear();
        c = judge(k, Program::ConceptGenevaEu, 1, 0, ev, result);
        CHECK(c.finished == 0 && c.course_out == 0 && result == -2 && ev.empty());
        // Out of the course (the pit stretch) still ends it.
        c = judge(k, Program::ConceptGenevaEu, 2, 0, ev, result);
        CHECK(c.finished != 0 && result == -1 && ev.size() == 1 && ev[0].type == 9);
    }
    // The pace car out of the course: nothing.
    ev.clear();
    c = judge(ex::Conductor::Training, Program::ConceptGenevaEu, 2, 2, ev, result);
    CHECK(c.finished == 0 && result == -2 && ev.empty());
}

// A test with the pace car on each Concept disc: the pace car is a computer car one second ahead at the start speed; with
// the player's car left alone it laps the course, ends its run and nothing else (no result, no failed or finished event
// for it); the player's car is the one judged; the run's record plays back to the same race.
TEST(concept_disc_pace_car_run) {
    const std::vector<std::string> discs = test::concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> disc = Disc::open(path);
        Vfs vfs(&disc->volume());
        data::CarDatabase db;
        CHECK(db.load(vfs));
        std::optional<Bytes> core = disc->volume().read_program();
        CHECK(core.has_value());
        if (!core) continue;
        auto data = std::make_shared<const ex::ProgramData>(ex::ProgramData::load(CoreImage::decode(*core)));
        CHECK(sim::concept_program(data->program()));
        // The first license test: the disc's own code (LCeu0000, LCtw0000 or LC000000), the one whose pool has a pace car.
        const data::Event* ev = nullptr;
        std::string code;
        for (const char* c : {"LCeu0000", "LCtw0000", "LC000000"}) {
            const data::Event* e = db.event(c);
            if (e && e->entries.size() >= 3 && vfs.read(course_file_path(vfs, e->course))) {
                // (the Tokyo-Geneva discs hold each other's rows: the program's own is the first its gcf names; any of them
                // runs the same rules)
                ev = e, code = c;
                break;
            }
        }
        CHECK(ev != nullptr);
        if (!ev) continue;
        std::optional<Bytes> file = vfs.read(course_file_path(vfs, ev->course));
        std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, ev->course);
        race::FieldConfig cfg;
        cfg.event = code;
        cfg.opponents = {ev->entries[2]};
        cfg.max_opponents = 1;
        cfg.player_slot = 0;
        race::ExactFieldOptions o;
        o.data = data;
        o.course_file = std::make_shared<const Bytes>(*file);
        o.course = ev->course;
        o.kind = ex::Conductor::Training;
        o.laps = ev->laps;
        o.player.car = {ev->entries[0]};
        const race::LicenseTimes lt = race::license_times(*ev);
        for (size_t k = 0; k < 3; ++k) o.licence_times[k] = u32(std::max(0, lt.ms[k]));
        cfg.exact = o;
        std::string err;
        std::unique_ptr<race::RaceField> field = race::RaceField::create(*track, db, cfg, &err);
        if (!field) std::printf("    %s %s: %s\n", disc->info().serial.c_str(), code.c_str(), err.c_str());
        CHECK(field != nullptr);
        if (!field) continue;
        const ex::ExactRace& r = *race::exact_race_of(*field);
        CHECK(r.kind() == ex::Conductor::Training && r.size() == 2 && field->opponents() == 1);
        if (r.size() != 2) continue;
        // The start: the event's flying start, the pace car trunc(speed / 3.6) m further on at the same speed.
        const int speed = ev->bytes[0x198];
        const int ahead = int(float(speed) / 3.6f);
        const ex::Car& me = r.cars()[0];
        const ex::Car& pace = r.cars()[1];
        const float lap = r.course().query->lap_length().host();
        float gap = pace.f3e4.host() - me.f3e4.host();
        if (gap < -lap / 2) gap += lap;
        std::printf("    %s %s on %s: start speed %d km/h, the pace car %.1f m ahead (rule: %d m)\n", disc->info().serial.c_str(),
                    code.c_str(), ev->course.c_str(), speed, double(gap), ahead);
        CHECK(speed > 0 && std::fabs(gap - float(ahead)) < 1.5f);
        CHECK(me.control == 0 && pace.control == 2 && me.ghost == 0 && pace.ghost == 0);
        CHECK(std::fabs(me.speed.host() - pace.speed.host()) < 0.01f && me.speed.host() > 1.0f);
        // The player's car left alone (it rolls to a stop on the road); the pace car laps the course, slowly: the event's rubber
        // band holds it back while it is ahead.
        bool pace_event = false, any_end = false;
        int steps = 0;
        for (; steps < 60 * 900 && pace.finished == 0; ++steps) {
            field->player().step({}, 1.0 / 60.0);
            for (const ex::RaceEvent& e : r.events()) {
                any_end = any_end || e.type == 5 || e.type == 9;
                pace_event = pace_event || ((e.type == 5 || e.type == 9) && e.car == 1);
            }
        }
        std::printf("      the pace car's lap: %d steps; the player's car: finished %d, result %d\n", steps, int(me.finished),
                    int(r.flow().result));
        CHECK(pace.finished != 0);         // (its run is over: the judge's finish for a computer car)
        CHECK(!pace_event && !any_end);    // no finished / failed event
        CHECK(me.finished == 0 && r.flow().result == -2); // the player's test goes on, no result yet
        // The record (a replay, not a ghost) plays back to the same race.
        std::shared_ptr<const ex::RaceRecord> rec = race::exact_recording(*field);
        CHECK(rec != nullptr);
        if (!rec) continue;
        CHECK(rec->kind == ex::Conductor::Training && rec->entries.size() == 2);
        rec = std::make_shared<const ex::RaceRecord>(ex::RaceRecord::parse(rec->serialize()));
        race::FieldConfig pc = cfg;
        pc.exact.playback = rec;
        std::unique_ptr<race::RaceField> play = race::RaceField::create(*track, db, pc, &err);
        CHECK(play != nullptr);
        if (!play) continue;
        for (u32 k = 0; k < rec->frames; ++k) play->player().step({}, 1.0 / 60.0);
        const std::optional<bool> same = race::exact_playback_matches(*play);
        CHECK(same && *same);
    }
}

// 2001 Tokyo, 2002 Tokyo-Seoul (v1.02, v2.00) and 2002 Tokyo-Geneva (Europe, Asia) race on their own physics: a single race runs
// under the disc's own rules (2001 Tokyo's with its special car and a car with a stepless gearbox: the later builds' own words
// of the car record live; Tokyo-Geneva's, without demo_tmv_g, an event of its own with special car 1 and a winged car in
// it), and its record (a replay) plays back to the same race; a record of another program (one made while the disc raced
// on GT3's core has another fingerprint) is refused.
TEST(concept_disc_own_core_race_and_replay) {
    const std::vector<std::string> discs = test::concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    bool ran = false;
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> disc = Disc::open(path);
        std::optional<Bytes> core = disc->volume().read_program();
        CHECK(core.has_value());
        if (!core) continue;
        auto data = std::make_shared<const ex::ProgramData>(ex::ProgramData::load(CoreImage::decode(*core)));
        const sim::Program prog = data->program();
        const bool geneva = prog == sim::Program::ConceptGenevaEu || prog == sim::Program::ConceptGenevaAsia;
        if (prog != sim::Program::ConceptTokyo && prog != sim::Program::ConceptSeoul && prog != sim::Program::ConceptSeoul2 && !geneva) continue;
        const bool tokyo = prog == sim::Program::ConceptTokyo;
        ran = true;
        CHECK(data->known() && data->behaviour().rules == prog && race::race_physics(*data).own);
        Vfs vfs(&disc->volume());
        data::CarDatabase db;
        CHECK(db.load(vfs));
        const char* event = geneva ? "A_cien_dmv_02_0003" : "demo_tmv_g";
        const data::Event* ev = db.event(event);
        CHECK(ev != nullptr && (!tokyo || db.car("fit_w_ff_01") != nullptr));
        if (!ev || (tokyo && !db.car("fit_w_ff_01"))) continue;
        // (2002 Tokyo-Seoul: the event's first car on the pad)
        std::string mine = "fit_w_ff_01";
        if (!tokyo)
            for (const std::string& e : ev->entries)
                if (!e.empty() && e != "-") {
                    mine = e;
                    break;
                }
        std::optional<Bytes> file = vfs.read(course_file_path(vfs, ev->course));
        CHECK(file.has_value());
        if (!file) continue;
        std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, ev->course);
        race::FieldConfig cfg;
        cfg.event = event;
        for (const std::string& e : ev->entries)
            if (!e.empty() && e != "-" && cfg.opponents.size() < 2) cfg.opponents.push_back(e);
        if (geneva) cfg.opponents = {"gtcrr_pod_tmv_01", "gtcrr_crossfire_dmv_02"}; // (special car 1 and a winged one)
        cfg.max_opponents = int(cfg.opponents.size());
        cfg.player_slot = 0;
        race::ExactFieldOptions o;
        o.data = data;
        o.course_file = std::make_shared<const Bytes>(*file);
        o.course = ev->course;
        o.kind = ex::Conductor::SinglePlayer;
        o.laps = 1;
        o.player.car = {mine};
        cfg.exact = o;
        std::string err;
        std::unique_ptr<race::RaceField> field = race::RaceField::create(*track, db, cfg, &err);
        if (!field) std::printf("    %s: %s\n", disc->info().serial.c_str(), err.c_str());
        CHECK(field != nullptr);
        if (!field) continue;
        const ex::ExactRace& r = *race::exact_race_of(*field);
        int pod = -1, kind7 = -1;
        for (size_t i = 0; i < size_t(r.size()); ++i) {
            CHECK(r.cars()[i].behaviour.rules == prog);
            if (r.cars()[i].p.special == 1) pod = int(i);
            if (r.cars()[i].p.special == 7) kind7 = int(i);
        }
        if (geneva) CHECK(pod > 0); // (special car 1's mood live)
        if (geneva) std::printf("    a car of special kind 7: %s\n", kind7 > 0 ? "yes" : "no");
        const ex::Car& me = r.cars()[0];
        if (tokyo) {
            CHECK(me.p.d.kind == 1 || me.p.d.kind == 2); // the stepless gearbox
            CHECK(pod > 0);
            if (pod > 0) CHECK(r.cars()[size_t(pod)].later.special[2] == 0x3C); // the special car's start state
        }
        // Full throttle for 20 seconds: the stepless gearbox's ratio word lives (1 with the clutch open, below 1 at speed).
        sim::ControlInputs in;
        in.throttle = 1.0f;
        bool moved = false;
        float fastest = 0, lowest = 1;
        for (int k = 0; k < 60 * 20; ++k) {
            field->player().step(in, 1.0 / 60.0);
            moved = moved || (me.later.ratio.bits != 0 && me.later.ratio.bits != 0x3F800000);
            fastest = std::max(fastest, me.speed.host());
            if (me.later.ratio.bits != 0) lowest = std::min(lowest, me.later.ratio.host());
        }
        std::printf("    %s %s: %zu cars, %s up to %.1f km/h, its ratio word down to %.3f\n", disc->info().serial.c_str(), event,
                    size_t(r.size()), mine.c_str(), double(fastest * 3.6f), double(lowest));
        CHECK(fastest > 10.0f && (!tokyo || (moved && lowest < 0.9f)));
        // The record plays back to the same race, the later words in its snapshots and hash.
        std::shared_ptr<const ex::RaceRecord> rec = race::exact_recording(*field);
        CHECK(rec != nullptr);
        if (!rec) continue;
        CHECK(rec->program == data->fingerprint() && rec->behaviour.rules == prog);
        rec = std::make_shared<const ex::RaceRecord>(ex::RaceRecord::parse(rec->serialize()));
        CHECK(race::record_program_data(data, rec->program) == data);
        CHECK(race::record_program_data(data, rec->program ^ 0x5Eu) == nullptr); // another program's record
        race::FieldConfig pc = cfg;
        pc.exact.playback = rec;
        std::unique_ptr<race::RaceField> play = race::RaceField::create(*track, db, pc, &err);
        CHECK(play != nullptr);
        if (!play) continue;
        for (u32 k = 0; k < rec->frames; ++k) play->player().step({}, 1.0 / 60.0);
        const std::optional<bool> same = race::exact_playback_matches(*play);
        CHECK(same && *same);
    }
    if (!ran) SKIP("needs a Concept 2001 Tokyo, 2002 Tokyo-Seoul or 2002 Tokyo-Geneva disc in GT3R_DISC_CONCEPT");
}

// The pod race in the game's race (physics §14.5, concept §9.2: APOD0001, test kind 5) on every Concept disc: the stop zone
// is set up from the program's two constants, the computer's pods stop in it once a lap and the rule counts their stops; the
// player's idle pod (on the grid) owes its stop and is shown no prompt off the zone.
TEST(concept_disc_pod_race) {
    const std::vector<std::string> discs = test::concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> disc = Disc::open(path);
        std::optional<Bytes> core = disc->volume().read_program();
        CHECK(core.has_value());
        if (!core) continue;
        auto data = std::make_shared<const ex::ProgramData>(ex::ProgramData::load(CoreImage::decode(*core)));
        if (!sim::concept_program(data->program()) || !data->known()) continue;
        Vfs vfs(&disc->volume());
        data::CarDatabase db;
        CHECK(db.load(vfs));
        const data::Event* ev = db.event("APOD0001");
        CHECK(ev != nullptr);
        if (!ev) continue;
        CHECK(ev->bytes.size() > 0x19A && ev->bytes[0x19A] == 5);
        std::string pod;
        for (const std::string& e : ev->entries)
            if (!e.empty() && e != "-" && pod.empty()) pod = e;
        CHECK(!pod.empty());
        std::optional<Bytes> file = vfs.read(course_file_path(vfs, ev->course));
        CHECK(file.has_value());
        if (pod.empty() || !file) continue;
        std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, ev->course);
        race::FieldConfig cfg;
        cfg.event = "APOD0001";
        cfg.opponents.assign(5, pod);
        cfg.max_opponents = 5;
        cfg.player_slot = 0;
        race::ExactFieldOptions o;
        o.data = data;
        o.course_file = std::make_shared<const Bytes>(*file);
        o.course = ev->course;
        o.kind = ex::Conductor::SinglePlayer;
        o.laps = 3;
        o.player.car = {pod};
        cfg.exact = o;
        std::string err;
        std::unique_ptr<race::RaceField> field = race::RaceField::create(*track, db, cfg, &err);
        if (!field) std::printf("    %s: %s\n", disc->info().serial.c_str(), err.c_str());
        CHECK(field != nullptr);
        if (!field) continue;
        const ex::ExactRace& r = *race::exact_race_of(*field);
        const ex::StopZone& z = r.flow().zone;
        CHECK(z.active == 1 && z.base.bits == ee::F(608.0f).bits && z.spacing.bits == ee::F(33.0f).bits && z.cars == 6);
        for (int i = 0; i < r.size(); ++i) CHECK(r.cars()[size_t(i)].p.special == 1);
        int prompts = 0;
        for (int k = 0; k < 60 * 120; ++k) {
            field->player().step({}, 1.0 / field->tick_hz());
            prompts += field->car_message(1) == 0x10 || field->car_message(1) == 0x11;
        }
        int stopped = 0, stops = 0;
        for (int i = 1; i < r.size(); ++i) stopped += z.stops[size_t(i)] != 0, stops += z.stops[size_t(i)];
        std::printf("    %s: the stop zone at %.0f m, %.0f m long; %d of %d computer pods stopped (%d stops) in 120 s\n",
                    disc->info().serial.c_str(), double(z.base.host()), double(z.spacing.host()), stopped, r.size() - 1, stops);
        CHECK(stopped == r.size() - 1 && z.stops[0] == 0 && prompts == 0 && field->take_pod_notes().empty());
    }
}
