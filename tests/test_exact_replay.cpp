// Replays of races on the exact core (replay format v11, sim/exact/race_record.h): a scripted race through the game's race
// code (race::ExactField), its record through a replay file, and the replay's race (ExactFieldOptions::playback) stepped
// beside the live one's recording: every car record, the race order, clock, collision bodies and random register equal after
// every step (by race_hash; every record byte compared every 300 steps and at the end). They need the program's tables and
// the courses from the disc (GT3R_DISC).

#include "test_framework.h"
#include "exact_drive.h"

#include "disc/core_image.h"
#include "disc/volume.h"
#include "game/data/car_database.h"
#include "race/backend.h"
#include "race/exact_car.h"
#include "race/exact_field.h"
#include "race/exact_setup.h"
#include "race/replay.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/race.h"
#include "sim/exact/race_record.h"
#include "vfs/vfs.h"

#include <cmath>
#include <cstring>
#include <functional>
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

void hold(PadState& p, PadButton b, u8 pressure) {
    p.buttons = u16(p.buttons | (1u << unsigned(b)));
    p.pressure[size_t(b)] = pressure;
}

// A driver toward the car's AI line ahead: the stick's x (positive steer = left), and throttle or brake.
struct Aim {
    float steer = 0; // -1..1, left positive
    bool throttle = false, brake = false;
};
Aim aim(const ex::Car& c, const ex::RaceCourse& course) {
    ex::F dx, dy;
    const int line = c.ai.line ? c.ai.line : 6;
    ex::ai_target(c, course.ai, line, c.ai.next, ex::F(12.0f) + ex::F(0.6f) * c.speed, dx, dy);
    const ex::F angle = test::heading_angle(c, dx, dy); // (EE floats: the same aim on every platform)
    Aim a;
    a.steer = std::clamp(angle.host() * 2.5f, -1.0f, 1.0f);
    a.throttle = c.speed * ex::F(3.6f) < ex::F(200.0f) - ex::F(260.0f) * ee::abs(angle), a.brake = !a.throttle;
    return a;
}

ex::Record image(const ex::Car& c) {
    ex::Record r{};
    ex::store_image(c, r.data());
    return r;
}

struct Scenario {
    std::string name, event, car;
    ex::Conductor kind = ex::Conductor::SinglePlayer;
    int steps = 6000;
    u8 config = 0;
    bool pad = true;           // player 1 on the race pad (the pad read), else ControlInputs
    bool second_pad = false;   // a battle's player 2 on the race pad
    int take_over = -1;        // the AI takes player 1's car over at this step (a demonstration)
    int gearbox = -1;          // the pause menu's AT / MT changes at this step (while the pad drives)
    bool condition = false;    // the screen seeds an engine condition other than the entry's before the first step
};

struct Result {
    int steps = 0, differing = 0, first = -1, frames = 0, changes = 0;
    size_t bytes = 0;
    bool matched = false;
};

Result round_trip(const Scenario& sc) {
    Result out;
    data::CarDatabase& db = *database();
    const data::Event* ev = db.event(sc.event);
    CHECK(ev != nullptr);
    if (!ev) return out;
    std::optional<Bytes> file = course_file(ev->course);
    CHECK(file.has_value());
    if (!file) return out;
    Vfs vfs(&test::test_disc()->volume());
    std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, ev->course);
    const bool battle = sc.kind == ex::Conductor::Battle2P;
    const std::string car = sc.car.empty() && !ev->entries.empty() ? ev->entries[0] : sc.car;
    race::FieldConfig cfg;
    cfg.event = sc.event;
    cfg.max_opponents = battle ? 0 : std::max(0, std::min(int(ev->cars_in_race) - 1, 5));
    race::ExactFieldOptions o;
    o.data = program();
    o.course_file = std::make_shared<const Bytes>(*file);
    o.course = ev->course;
    o.kind = sc.kind;
    o.config = sc.config;
    o.laps = battle ? 2 : ev->laps;
    o.player.car = {car};
    o.player.pedal_style = 0;
    if (battle) o.second.car = {car};
    cfg.exact = o;
    std::string err;
    std::unique_ptr<race::RaceField> live = race::RaceField::create(*track, db, cfg, &err);
    if (!live) std::printf("  %s: %s\n", sc.name.c_str(), err.c_str());
    CHECK(live != nullptr);
    if (!live) return out;
    const ex::ExactRace& a = *race::exact_race_of(*live);
    if (sc.condition) live->player().set_engine_condition({123456, 2500.0f, 900.0f});
    // The live run: its hash after every step, its records every 300 steps.
    std::vector<u64> hashes;
    std::vector<std::vector<ex::Record>> images;
    auto snap = [](const ex::ExactRace& r) {
        std::vector<ex::Record> v;
        for (const ex::Car& c : r.cars()) v.push_back(image(c));
        return v;
    };
    bool automatic = true;
    for (int k = 0; k < sc.steps; ++k) {
        const Aim a0 = aim(a.cars()[0], a.course());
        sim::ControlInputs in;
        in.steer = -a0.steer;
        in.throttle = a0.throttle ? 1.0f : 0.0f;
        in.brake = a0.brake ? 0.6f : 0.0f;
        in.controller_kind = 2;
        in.shift_up = k % 600 == 599;
        if (sc.gearbox >= 0 && k == sc.gearbox) automatic = !automatic;
        in.automatic = automatic;
        PadState pad;
        pad.connected = true;
        pad.axes[size_t(PadAxis::LX)] = u8(std::clamp(int(128 - a0.steer * 127), 0, 255));
        if (a0.throttle) hold(pad, PadButton::Cross, (k / 200) % 3 == 0 ? 40 : 255);
        if (a0.brake) hold(pad, PadButton::Square, 140);
        if (k % 900 > 860) hold(pad, PadButton::Left, 255);
        if (k > 1500 && k < 1540) hold(pad, PadButton::Triangle, 255);
        if (k % 700 == 699) hold(pad, PadButton::R2, 255);
        PadState pad2;
        if (battle) {
            const Aim a1 = aim(a.cars()[1], a.course());
            pad2.connected = true;
            pad2.axes[size_t(PadAxis::LX)] = u8(std::clamp(int(128 - a1.steer * 127), 0, 255));
            if (a1.throttle) hold(pad2, PadButton::Cross, 255);
            if (a1.brake) hold(pad2, PadButton::Square, 128);
            if (sc.second_pad) live->set_second_pad(&pad2);
            else {
                sim::ControlInputs i2;
                i2.steer = -a1.steer, i2.throttle = a1.throttle ? 1.0f : 0.0f, i2.brake = a1.brake ? 0.5f : 0.0f;
                live->set_second_inputs(i2);
            }
        }
        if (k == sc.take_over) CHECK(live->player().ai_take_over());
        if (sc.pad) live->player().set_pad(&pad);
        live->player().step(in, 1.0 / 60.0);
        hashes.push_back(ex::race_hash(a));
        if (k % 300 == 299 || k + 1 == sc.steps) images.push_back(snap(a));
    }
    // Through a replay file.
    race::Replay r;
    r.course = ev->course, r.car = car, r.event = sc.event;
    r.inputs.resize(size_t(sc.steps));
    r.exact = race::exact_recording(*live);
    CHECK(r.exact != nullptr);
    if (!r.exact) return out;
    const Bytes file_bytes = r.serialize();
    out.bytes = file_bytes.size();
    const race::Replay q = race::Replay::parse(file_bytes);
    CHECK(q.exact && q.exact->final_hash == hashes.back());
    if (!q.exact) return out;
    out.frames = int(q.exact->frames), out.changes = int(q.exact->changes.size());
    // The replay's race: its own pads and inputs are not used.
    race::FieldConfig pcfg = cfg;
    pcfg.exact.playback = q.exact;
    pcfg.exact.player.car = {car};
    std::unique_ptr<race::RaceField> play = race::RaceField::create(*track, db, pcfg, &err);
    if (!play) std::printf("  %s playback: %s\n", sc.name.c_str(), err.c_str());
    CHECK(play != nullptr);
    if (!play) return out;
    const ex::ExactRace& b = *race::exact_race_of(*play);
    size_t img = 0;
    for (int k = 0; k < sc.steps; ++k) {
        PadState junk;
        junk.connected = true;
        hold(junk, PadButton::Square, 255);
        play->player().set_pad(&junk);
        sim::ControlInputs in;
        in.steer = 1, in.brake = 1;
        play->player().step(in, 1.0 / 60.0);
        ++out.steps;
        bool same = ex::race_hash(b) == hashes[size_t(k)];
        if (k % 300 == 299 || k + 1 == sc.steps) {
            const std::vector<ex::Record> now = snap(b);
            same = same && img < images.size() && now == images[img];
            ++img;
        }
        if (!same && out.differing++ == 0) out.first = k;
    }
    out.matched = race::exact_playback_matches(*play).value_or(false);
    // Recorded on other data or another program: refused, never played.
    for (int k = 0; k < 2; ++k) {
        auto other = std::make_shared<ex::RaceRecord>(*q.exact);
        (k == 0 ? other->data : other->program) ^= 1;
        race::FieldConfig ocfg = pcfg;
        ocfg.exact.playback = other;
        std::string why;
        const bool built = race::RaceField::create(*track, db, ocfg, &why) != nullptr;
        if (built || why.find("recorded with") == std::string::npos) std::printf("  refusal %d: built %d, %s\n", k, int(built), why.c_str());
        CHECK(!built && why.find("recorded with") != std::string::npos);
    }
    std::printf("  %s: %d steps, %d changes, replay %zu bytes, %d differing (first %d), end %s\n", sc.name.c_str(), out.steps,
                out.changes, out.bytes, out.differing, out.first, out.matched ? "matches" : "DIFFERS");
    return out;
}

} // namespace

TEST(exact_replay_round_trip) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    std::vector<Scenario> scenarios;
    // A GT-mode race on the race pad (the pad read), the gearbox changed in the pause menu, an engine condition seeded.
    {
        Scenario s{"Sunday Cup on the pad", "Gsunday_e_0001", "r34_gtr_v_99"};
        s.steps = 9000, s.gearbox = 2400, s.condition = true;
        scenarios.push_back(s);
    }
    // A rally on dirt from ControlInputs (scripts, the autopilot).
    {
        Scenario s{"Tahiti Maze rally from inputs", "Dr_tahitimaze_0001", ""};
        s.pad = false;
        scenarios.push_back(s);
    }
    // A split-screen battle: player 1 from inputs, player 2 on the pad.
    {
        Scenario s{"2P battle", "Gsunday_e_0003", "r34_gtr_v_99", ex::Conductor::Battle2P};
        s.pad = false, s.second_pad = true;
        scenarios.push_back(s);
    }
    // A race's demonstration: the AI takes the player's car over.
    {
        Scenario s{"demonstration (AI take-over)", "Gspider_e_0001", "r34_gtr_v_99"};
        s.take_over = 1200;
        scenarios.push_back(s);
    }
    // The test autopilot: the race's AI drives the player's car with its pad decoded (configuration byte 3).
    {
        Scenario s{"autopilot (configuration 3)", "G_4wd_e_0001", ""};
        s.config = 3, s.steps = 4000;
        scenarios.push_back(s);
    }
    for (const Scenario& s : scenarios) {
        const Result r = round_trip(s);
        CHECK(r.steps == s.steps && r.frames == s.steps && r.differing == 0 && r.matched);
    }
}

// A whole-run ghost (ExactFieldOptions::ghost): a time trial's run, then the next run with that run as its ghost car. The
// ghost car (car 1, flagged a ghost by the time trial's conductor) drives the first run again from its frames: its state is
// the first run's car 0's after every step; the player's car is not touched by it (the time trial's collision step is
// walls-only); a replay of the run with its ghost plays to the same race.
TEST(exact_replay_ghost) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    data::CarDatabase& db = *database();
    const data::Event* ev = db.event("AT020001");
    CHECK(ev != nullptr);
    if (!ev) return;
    std::optional<Bytes> file = course_file(ev->course);
    CHECK(file.has_value());
    if (!file) return;
    Vfs vfs(&test::test_disc()->volume());
    std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, ev->course);
    race::FieldConfig cfg;
    const std::string cfg_player = ev->entries[0];
    cfg.event = ev->label;
    cfg.max_opponents = 0;
    race::ExactFieldOptions o;
    o.data = program();
    o.course_file = std::make_shared<const Bytes>(*file);
    o.course = ev->course;
    o.kind = ex::Conductor::TimeTrial;
    o.laps = 1;
    o.player.car = {cfg_player};
    cfg.exact = o;
    auto drive = [&](race::RaceField& f, int k, double w) {
        const ex::ExactRace& r = *race::exact_race_of(f);
        const Aim a = aim(r.cars()[0], r.course());
        sim::ControlInputs in;
        in.steer = -a.steer * float(w);
        in.throttle = a.throttle ? 1.0f : 0.0f;
        in.brake = a.brake ? 0.7f : 0.0f;
        in.controller_kind = 2;
        in.shift_up = k % 500 == 499;
        f.player().step(in, 1.0 / 60.0);
    };
    const int steps = 3000;
    std::unique_ptr<race::RaceField> first = race::RaceField::create(*track, db, cfg);
    CHECK(first != nullptr);
    if (!first) return;
    std::vector<sim::VehicleState> run;
    std::vector<bool> own; // the run's car on its pad or the run-up's AI (not the end of the run's driver)
    for (int k = 0; k < steps; ++k) {
        drive(*first, k, 1.0), run.push_back(first->player().state());
        const ex::Car& c = race::exact_race_of(*first)->cars()[0];
        own.push_back(!c.finished && c.driver_kind <= 1);
    }
    std::shared_ptr<const ex::RaceRecord> ghost = race::exact_recording(*first);
    CHECK(ghost && ghost->frames == u32(steps));
    if (!ghost) return;
    // The next run with the ghost; the player drives the same way, then differently.
    race::FieldConfig gcfg = cfg;
    gcfg.exact.ghost = ghost;
    std::unique_ptr<race::RaceField> second = race::RaceField::create(*track, db, gcfg);
    CHECK(second != nullptr);
    if (!second) return;
    const ex::ExactRace& b = *race::exact_race_of(*second);
    CHECK(b.size() == 2 && b.cars()[1].ghost != 0 && second->opponents() == 0);
    int ghost_differ = 0, player_differ = 0, drawn = 0, first_diff = -1, compared = 0;
    for (int k = 0; k < steps; ++k) {
        drive(*second, k, k < steps / 2 ? 1.0 : 0.8);
        sim::VehicleState g;
        if (race::exact_ghost(*second, 1.0f, g)) ++drawn; // (drawn: not while the computer drives it)
        race::exact_state(b.cars()[1], race::exact_setup(b.cars()[1]), g);
        const sim::VehicleState& want = run[size_t(k)];
        if (own[size_t(k)]) ++compared;
        if ((own[size_t(k)] && std::memcmp(&g.position, &want.position, sizeof g.position) != 0) ||
            (own[size_t(k)] && std::memcmp(&g.orientation, &want.orientation, sizeof g.orientation) != 0))
            if (ghost_differ++ == 0) first_diff = k;
        if (k < steps / 2 && std::memcmp(&second->player().state().position, &want.position, sizeof(sim::Vec3)) != 0) ++player_differ;
    }
    std::printf("  ghost: %d steps, drawn on %d; compared on the first run's own %d (to the end of its run): %d differing (from "
                "%d); the player (same driving) differs on %d\n",
                steps, drawn, compared, ghost_differ, first_diff, player_differ);
    CHECK(ghost_differ == 0 && player_differ == 0 && drawn > 0 && compared > 600);
    // A replay of the run with its ghost: the same race.
    std::shared_ptr<const ex::RaceRecord> rec = race::exact_recording(*second);
    race::FieldConfig pcfg = cfg;
    pcfg.exact.playback = std::make_shared<const ex::RaceRecord>(ex::RaceRecord::parse(rec->serialize()));
    std::unique_ptr<race::RaceField> play = race::RaceField::create(*track, db, pcfg);
    CHECK(play != nullptr);
    if (!play) return;
    for (int k = 0; k < steps; ++k) play->player().step({}, 1.0 / 60.0);
    CHECK(race::exact_playback_matches(*play).value_or(false));
    CHECK(race::exact_race_of(*play)->size() == 2 && play->opponents() == 0);
}
