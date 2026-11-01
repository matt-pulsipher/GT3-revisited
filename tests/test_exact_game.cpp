// The game's races on the exact core (race::ExactField): the pad read, and whole races through the game's race code equal to
// sim::exact::ExactRace driven directly from the same setup and frames. They need the program's tables and the courses from
// the disc (GT3R_DISC).

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
#include "sim/exact/pad_read.h"
#include "sim/exact/race.h"
#include "vfs/vfs.h"

#include <cmath>
#include <cstring>
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

PadState neutral_pad() {
    PadState p;
    p.connected = true;
    return p;
}
void hold(PadState& p, PadButton b, u8 pressure) {
    p.buttons = u16(p.buttons | (1u << unsigned(b)));
    p.pressure[size_t(b)] = pressure;
}

// The test's own driver for a car on the pad path: aim at its AI line ahead (the AI course's own evaluation), full throttle
// below a speed that falls with the aim's angle, the brake above it; a frame in whole pad units.
ex::PadFrame line_driver(const ex::Car& c, const ex::RaceCourse& course) {
    ex::PadFrame f;
    f.flags = 0x1F;
    f.kind = 2;
    ex::F dx, dy;
    const int line = c.ai.line ? c.ai.line : 6;
    ex::ai_target(c, course.ai, line, c.ai.next, ex::F(12.0f) + ex::F(0.6f) * c.speed, dx, dy);
    const ex::F angle = test::heading_angle(c, dx, dy); // left positive (Z up; EE floats: the same frames on every platform)
    f.steer = s16(std::lround(std::clamp(angle.host() * 2.5f, -1.0f, 1.0f) * 4096.0f));
    if (c.speed * ex::F(3.6f) < ex::F(210.0f) - ex::F(260.0f) * ee::abs(angle)) f.throttle = 4096;
    else f.brake = 2048;
    return f;
}

// The ControlInputs whose frame (race::exact_inputs_frame) is f.
sim::ControlInputs inputs_of(const ex::PadFrame& f) {
    sim::ControlInputs in;
    in.steer = -float(f.steer) / 4096.0f;
    in.throttle = float(f.throttle) / 4096.0f;
    in.brake = float(f.brake) / 4096.0f;
    in.controller_kind = 2;
    return in;
}

bool same_record(const ex::Car& a, const ex::Car& b) {
    ex::Record ia{}, ib{};
    ex::store_image(a, ia.data());
    ex::store_image(b, ib.data());
    return ia == ib;
}

struct LockstepRun {
    int steps = 0, differing = 0, finished = 0, first_diff = -1;
    bool player_finished = false;
    double race_time = 0;
};

// A race through race::RaceField (the game's) and the same race on an ExactRace built here from the field's documented
// set-up (car 0 the player behind the opponents in the event's order, car 1 player 2 in a battle), stepped with the same
// frames: every record, the race order and clock compared after every step; the field's outputs against the race's.
LockstepRun lockstep(const std::string& event, std::string car, ex::Conductor kind, int max_steps, bool second_pad) {
    LockstepRun out;
    data::CarDatabase& db = *database();
    const data::Event* ev = db.event(event);
    CHECK(ev != nullptr);
    if (!ev) return out;
    if (car.empty() && !ev->entries.empty()) car = ev->entries[0]; // the event's own car
    std::optional<Bytes> file = course_file(ev->course);
    CHECK(file.has_value());
    if (!file) return out;
    Disc* disc = test::test_disc();
    Vfs vfs(&disc->volume());
    std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, ev->course);
    const bool battle = kind == ex::Conductor::Battle2P;
    const int laps = battle ? 2 : ev->laps;
    race::FieldConfig cfg;
    cfg.event = event;
    cfg.max_opponents = battle ? 0 : std::max(0, std::min(int(ev->cars_in_race) - 1, 5));
    race::ExactFieldOptions o;
    o.data = program();
    o.course_file = *file;
    o.course = ev->course;
    o.kind = kind;
    o.laps = laps;
    o.player.car = {car};
    if (battle) o.second.car = {car};
    o.second.original_response = true; // (player 2 is compared with pad_read's own frames)
    cfg.exact = o;
    std::string err;
    std::unique_ptr<race::RaceField> field = race::RaceField::create(*track, db, cfg, &err);
    if (!field) std::printf("  %s: %s\n", event.c_str(), err.c_str());
    CHECK(field != nullptr);
    if (!field) return out;
    const ex::ExactRace* fr = race::exact_race_of(*field);
    CHECK(fr != nullptr);
    if (!fr) return out;

    // The same race, set up here.
    ex::RaceSetup rs;
    rs.data = o.data.get();
    rs.db = &db;
    rs.course = ex::RaceCourse::load(*file, *o.data);
    rs.event = ev->bytes;
    rs.course_category = race::exact_course_category(db, ev->course);
    rs.kind = kind;
    rs.laps = laps;
    rs.start_type = battle ? -1 : ev->bytes[0x1F3];
    rs.countdown = ev->bytes[0x1F2];
    if (battle) rs.battle_laps = laps;
    std::vector<std::string> opps;
    if (!battle)
        for (size_t i = 0; i < ev->entries.size() && int(i) < cfg.max_opponents; ++i) opps.push_back(ev->entries[i]);
    const int humans = battle ? 2 : 1;
    for (int h = 0; h < humans; ++h) {
        ex::StartEntry e;
        e.eq = *race::exact_equipment(db, {car});
        e.human = 1;
        e.player = true;
        e.grid_slot = int(opps.size()) + h;
        e.best_lap = 0x157529FF;
        rs.entries.push_back(e);
    }
    for (size_t i = 0; i < opps.size(); ++i) {
        ex::StartEntry e;
        e.eq = *race::exact_equipment(db, {opps[i]});
        e.grid_slot = int(i);
        e.best_lap = 0x157529FF;
        rs.entries.push_back(e);
    }
    ex::ExactRace direct(rs);
    direct.start_countdown();
    CHECK(direct.size() == fr->size());
    if (direct.size() != fr->size()) return out;

    for (int k = 0; k < max_steps; ++k) {
        std::vector<ex::PadFrame> frames(size_t(direct.size()));
        frames[0] = line_driver(direct.cars()[0], direct.course());
        PadState pad2 = neutral_pad();
        if (battle) {
            // Player 2 on the race pad: Cross and the stick's x from its own line driver (through the pad read).
            const ex::PadFrame want = line_driver(direct.cars()[1], direct.course());
            const int lx = std::clamp(128 - want.steer / 64, 0, 255);
            pad2.axes[size_t(PadAxis::LX)] = u8(lx);
            if (want.throttle) hold(pad2, PadButton::Cross, 255);
            if (want.brake) hold(pad2, PadButton::Square, 128);
            ex::PadReading r;
            ex::type_a_channels(r);
            r.buttons = u16((want.throttle ? 0x400 : 0) | (want.brake ? 0x800 : 0));
            r.channel[2] = u8(lx);
            r.channel[10] = want.throttle ? 255 : 0;
            r.channel[11] = want.brake ? 128 : 0;
            frames[1] = second_pad ? ex::pad_read(r, *o.data) : want;
            if (second_pad) field->set_second_pad(&pad2);
            else field->set_second_inputs(inputs_of(want));
        }
        field->player().step(inputs_of(frames[0]), 1.0 / 60.0);
        direct.step(frames);
        ++out.steps;
        bool same = fr->order() == direct.order() && fr->flow().clock == direct.flow().clock &&
                    fr->start_count() == direct.start_count();
        for (int i = 0; i < direct.size(); ++i) same = same && same_record(fr->cars()[size_t(i)], direct.cars()[size_t(i)]);
        if (!same) {
            ++out.differing;
            if (out.first_diff < 0) out.first_diff = k;
        }
        // The field's view: the player's state from its record, the race clock, the finish.
        sim::VehicleState s;
        race::exact_state(direct.cars()[0], race::exact_setup(direct.cars()[0]), s);
        same = std::memcmp(&s.position, &field->player().state().position, sizeof s.position) == 0 &&
               field->player_position() == int(direct.cars()[0].rank);
        if (!same && out.first_diff < 0) out.first_diff = k, ++out.differing;
        int done = 0;
        for (const ex::Car& c : direct.cars()) done += c.finished != 0;
        out.finished = done;
        out.player_finished = field->player_finish() >= 0;
        if (out.player_finished)
            CHECK(std::fabs(field->player_finish() - double(direct.cars()[0].x530) / 1000.0) < 1e-9);
        if (done == direct.size()) break;
    }
    out.race_time = field->race_time();
    std::printf("  %s: %d steps, %d of %d cars finished (player %s), race time %.2f s, %d differing (first %d)\n",
                event.c_str(), out.steps, out.finished, direct.size(), out.player_finished ? "yes" : "no", out.race_time,
                out.differing, out.first_diff);
    return out;
}

} // namespace

TEST(exact_game_pad_read) {
    std::shared_ptr<const ex::ProgramData> d = program();
    if (!d) SKIP("needs GT3R_DISC");
    ex::PadReading r;
    ex::type_a_channels(r);
    // At rest: every axis analog (the pressure pad), the stick's kind, nothing pressed.
    ex::PadFrame f = ex::pad_read(r, *d);
    CHECK(f.flags == 0x1F && f.kind == 2 && f.steer == 0 && f.throttle == 0 && f.brake == 0 && f.shift == 0);
    // Cross all the way: full throttle; a light press: 2 x 20 x 100 / 35 = 114 (x 16).
    r.buttons = 0x400, r.channel[10] = 255;
    CHECK(ex::pad_read(r, *d).throttle == 4096);
    r.channel[10] = 20;
    CHECK(ex::pad_read(r, *d).throttle == 114 * 16);
    // The stick through the response curve: full left 4096 (left positive), the centre's dead band 0.
    r = {};
    ex::type_a_channels(r);
    r.channel[2] = 0;
    CHECK(ex::pad_read(r, *d).steer == 4096);
    r.channel[2] = 140;
    CHECK(ex::pad_read(r, *d).steer == 0);
    // The d-pad's pressure: kind 3, full lock to the left.
    r.channel[2] = 0x80, r.channel[5] = 255, r.buttons = 0x100;
    f = ex::pad_read(r, *d);
    CHECK(f.kind == 3 && f.steer == 4096 && (f.flags & 1));
    // R2 / L2: the shift request (L2 wins).
    r = {};
    ex::type_a_channels(r);
    r.buttons = 0x4000;
    CHECK(ex::pad_read(r, *d).shift == 1);
    r.buttons = 0xC000;
    CHECK(ex::pad_read(r, *d).shift == -1);
    // The steering wheel: kind 4, (0x200 - pos) x 9 within +-4096, the pedals beyond their dead travel.
    r = {};
    r.type = 0, r.wheel = 1, r.wheel_pos = 0x200 - 100, r.wheel_throttle = 0, r.wheel_brake = 0xFF;
    f = ex::pad_read(r, *d);
    CHECK(f.kind == 4 && f.steer == 900 && f.throttle == 4096 && (f.flags & 2) && f.brake == 0);

    // The race pad with the linear response (the default): the stick straight to the steering, each pressure to the frame
    // value whose shaped value (phase 3: x + c (1 - x), c the program's 0.1) is the pressure's fraction; the original
    // response is the pad read as it is.
    const ex::PadConstants k = ex::pad_constants(*d);
    auto shaped = [&](u16 v) { return v == 0 ? 0.0f : (ex::F(float(v) / 4096.0f) + k.analog_jump * (ex::F(1.0f) - ex::F(float(v) / 4096.0f))).host(); };
    race::ExactHuman lin, orig;
    orig.original_response = true;
    PadState pad = neutral_pad();
    pad.axes[size_t(PadAxis::LX)] = 112; // a little left of centre: inside the original curve's dead band
    hold(pad, PadButton::Cross, 128);
    hold(pad, PadButton::Square, 64);
    f = race::exact_pad_frame(pad, lin, *d);
    CHECK(f.kind == 2 && f.steer == 512 && f.flags == 0x1F);
    CHECK(std::fabs(shaped(f.throttle) - 128.0f / 255.0f) < 0.001f && std::fabs(shaped(f.brake) - 64.0f / 255.0f) < 0.001f);
    CHECK(f.third == 0 && f.back == 0);
    ex::PadFrame g = race::exact_pad_frame(pad, orig, *d);
    CHECK(g.steer == 0 && g.throttle == 4096); // the dead band; a half press is all the way
    pad.axes[size_t(PadAxis::LX)] = 0;
    CHECK(race::exact_pad_frame(pad, lin, *d).steer == 4096);
    pad = neutral_pad();
    hold(pad, PadButton::Cross, 10); // below the shaping's least step: its least value
    CHECK(race::exact_pad_frame(pad, lin, *d).throttle == 1);
}

// A run on the race pad (the pad read) and its replay: the recorded inputs, made from the frames the pad read gave as the
// race screen records them, give the same race when stepped as inputs (race::exact_inputs_frame).
TEST(exact_game_pad_replay) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    data::CarDatabase& db = *database();
    const data::Event* ev = db.event("Gsunday_e_0002");
    CHECK(ev != nullptr);
    if (!ev) return;
    std::optional<Bytes> file = course_file(ev->course);
    CHECK(file.has_value());
    if (!file) return;
    Vfs vfs(&test::test_disc()->volume());
    std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, ev->course);
    race::FieldConfig cfg;
    const std::string cfg_player = "r34_gtr_v_99";
    cfg.event = ev->label;
    race::ExactFieldOptions o;
    o.data = program();
    o.course_file = *file;
    o.course = ev->course;
    o.laps = ev->laps;
    o.player.car.label = cfg_player;
    cfg.exact = o;
    std::unique_ptr<race::RaceField> live = race::RaceField::create(*track, db, cfg);
    std::unique_ptr<race::RaceField> replay = race::RaceField::create(*track, db, cfg);
    CHECK(live && replay);
    if (!live || !replay) return;
    const ex::ExactRace& a = *race::exact_race_of(*live);
    const ex::ExactRace& b = *race::exact_race_of(*replay);
    int differing = 0, reverse_steps = 0, dpad_steps = 0;
    for (int k = 0; k < 4000; ++k) {
        // The pad: the stick toward the line, Cross pressed part of the way or fully, Square in the braking zones, the
        // d-pad now and then, Triangle (back) for a moment, R2 / L2 taps.
        const ex::PadFrame want = line_driver(a.cars()[0], a.course());
        PadState pad = neutral_pad();
        pad.axes[size_t(PadAxis::LX)] = u8(std::clamp(128 - want.steer / 40, 0, 255));
        if (want.throttle) hold(pad, PadButton::Cross, (k / 200) % 3 == 0 ? 20 : 255);
        if (want.brake) hold(pad, PadButton::Square, 90);
        if (k % 700 > 650) hold(pad, PadButton::Left, 255), ++dpad_steps;
        if (k > 900 && k < 960) hold(pad, PadButton::Triangle, 255), ++reverse_steps;
        if (k % 500 == 499) hold(pad, PadButton::R2, 255);
        live->player().set_pad(&pad);
        live->player().step({}, 1.0 / 60.0);
        // As the screen records it.
        const ex::PadFrame& f = (*race::exact_frames_of(*live))[0];
        sim::ControlInputs r;
        r.steer = -float(f.steer) / 4096.0f;
        r.throttle = float(f.throttle) / 4096.0f;
        r.brake = float(f.brake) / 4096.0f;
        r.handbrake = f.third > 0;
        r.shift_up = f.shift > 0, r.shift_down = f.shift < 0;
        r.controller_kind = f.kind;
        const sim::ControlInputs q = race::quantize(r);
        replay->player().set_reverse_request(f.back > 0);
        replay->player().step(q, 1.0 / 60.0);
        bool same = a.order() == b.order() && a.flow().clock == b.flow().clock;
        for (int i = 0; i < a.size(); ++i) same = same && same_record(a.cars()[size_t(i)], b.cars()[size_t(i)]);
        if (!same && differing++ == 0) std::printf("  pad replay: first difference at step %d\n", k);
    }
    std::printf("  pad replay: 4000 steps (%d with the d-pad, %d with the back axis), %d differing\n", dpad_steps, reverse_steps,
                differing);
    CHECK(differing == 0);
}

TEST(exact_game_race_lockstep) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    // A GT-mode race (Sunday Cup, 6 cars), a rally (the Tahiti Maze, dirt), a split-screen battle with player 2 on the pad.
    LockstepRun a = lockstep("Gsunday_e_0001", "r34_gtr_v_99", ex::Conductor::SinglePlayer, 30000, false);
    CHECK(a.steps > 0 && a.differing == 0);
    CHECK(a.finished > 1); // the field's race ran to the opponents' finishes
    LockstepRun b = lockstep("Dr_tahitimaze_0001", "", ex::Conductor::SinglePlayer, 30000, false);
    CHECK(b.steps > 0 && b.differing == 0);
    LockstepRun c = lockstep("Gsunday_e_0003", "r34_gtr_v_99", ex::Conductor::Battle2P, 30000, true);
    CHECK(c.steps > 0 && c.differing == 0);
}

TEST(exact_game_rules) {
    if (!database() || !program()) SKIP("needs GT3R_DISC");
    // Whose rules a race follows: the disc's (unset), NTSC-U's on every disc (online, the game's demos); another release's
    // rules than NTSC-U's or the disc's are refused. Every race steps 60 times a second.
    data::CarDatabase& db = *database();
    const data::Event* ev = db.event("Gsunday_e_0001");
    CHECK(ev != nullptr);
    if (!ev) return;
    std::optional<Bytes> file = course_file(ev->course);
    CHECK(file.has_value());
    if (!file) return;
    Disc* disc = test::test_disc();
    Vfs vfs(&disc->volume());
    std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, ev->course);
    auto build = [&](std::optional<sim::Program> rules, std::string* err) {
        race::FieldConfig cfg;
        const std::string cfg_player = "r34_gtr_v_99";
        cfg.event = ev->label;
        cfg.max_opponents = 1;
        race::ExactFieldOptions o;
        o.data = program();
        o.rules = rules;
        o.course_file = *file;
        o.course = ev->course;
        o.kind = ex::Conductor::SinglePlayer;
        o.laps = ev->laps;
        o.player.car = {cfg_player};
        cfg.exact = o;
        return race::RaceField::create(*track, db, cfg, err);
    };
    const sim::Program disc_rules = program()->program();
    const std::optional<sim::Program> kept[] = {std::nullopt, sim::Program::NtscU, disc_rules};
    for (const std::optional<sim::Program>& r : kept) {
        std::string err;
        std::unique_ptr<race::RaceField> f = build(r, &err);
        CHECK(f != nullptr && race::exact_race_of(*f) != nullptr);
        if (f) CHECK(f->tick_hz() == 60.0);
    }
    for (int p = 0; p < sim::kPrograms; ++p) {
        const sim::Program other = sim::Program(p);
        if (other == sim::Program::NtscU || other == disc_rules) continue;
        std::string err;
        CHECK(build(other, &err) == nullptr && !err.empty()); // (not this disc's release)
    }
}
