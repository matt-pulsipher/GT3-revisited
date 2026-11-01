// Online races on the exact core (docs/dev/ONLINE.md): two players' machines in one process, each its own race::ExactField
// with its car stepped and the other's posed from the other machine's snapshots (through the wire format, late), the
// prediction shadows, contacts and their impulses, the plausibility checks. They need the program's tables and the courses
// from the disc (GT3R_DISC).

#include "test_framework.h"
#include "exact_drive.h"

#include "disc/core_image.h"
#include "disc/volume.h"
#include "game/data/car_database.h"
#include "game/gt_parts.h"
#include "game/gt_state.h"
#include "game/net_car.h"
#include "net/protocol.h"
#include "net/session.h"
#include "race/backend.h"
#include "race/exact_car.h"
#include "race/exact_field.h"
#include "race/exact_setup.h"
#include "race/net_race.h"
#include "sim/car_spec.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/race.h"
#include "vfs/vfs.h"

#include <chrono>
#include <cmath>
#include <deque>
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

// A test driver on the pad path: aim at the AI line ahead, full throttle below a speed that falls with the aim's angle,
// the brake above it; `weave` adds a slow steering wave (the inputs keep changing, as a player's do).
ex::PadFrame line_driver(const ex::Car& c, const ex::RaceCourse& course, int step, float weave = 0) {
    ex::PadFrame f;
    f.flags = 0x1F;
    f.kind = 2;
    ex::F dx, dy;
    const int line = c.ai.line ? c.ai.line : 6;
    ex::ai_target(c, course.ai, line, c.ai.next, ex::F(12.0f) + ex::F(0.6f) * c.speed, dx, dy);
    // (EE floats: the same frames on every platform)
    const ex::F angle = test::heading_angle(c, dx, dy) + ex::F(weave) * ee::sin(ex::F(float(step) * 0.05f));
    f.steer = s16(std::lround(std::clamp(angle.host() * 2.5f, -1.0f, 1.0f) * 4096.0f));
    // (never below 10 km/h: a car stopped across its line drives on)
    if (c.speed * ex::F(3.6f) < ee::max(ex::F(200.0f) - ex::F(260.0f) * ee::abs(angle), ex::F(10.0f))) f.throttle = 4096;
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

// One player's machine: its field (its car 0 on its grid slot, the other player's car posed), its view of the other car.
struct Machine {
    std::unique_ptr<race::RaceTrack> track;
    std::unique_ptr<race::RaceField> field;
    race::RemoteCar remote;
    std::deque<net::CarSnapshot> inbox; // states from the other machine, delivered `latency` steps late
    net::PadInput inputs[net::kInputHistory];
    int ninputs = 0;
    u32 tick = 0;
    const ex::ExactRace& race() const { return *race::exact_race_of(*field); }
};

bool make_machine(Machine& m, const std::string& course, const std::string& mine, int my_slot, const std::string& theirs,
                  int their_slot, int laps, u8 config = 0) {
    data::CarDatabase& db = *database();
    std::optional<Bytes> file = course_file(course);
    if (!file) return false;
    Disc* disc = test::test_disc();
    Vfs vfs(&disc->volume());
    m.track = race::make_track(vfs, *file, course);
    race::FieldConfig cfg;
    cfg.max_opponents = 0;
    cfg.player_slot = my_slot;
    race::FieldConfig::Remote rm;
    rm.car = theirs, rm.slot = their_slot, rm.source.label = theirs;
    cfg.remote.push_back(rm);
    race::ExactFieldOptions o;
    o.data = program();
    o.course_file = *file;
    o.course = course;
    o.kind = ex::Conductor::BattleMP;
    o.config = config; // 3: the race's AI drives the player's car (GT3R_RACE auto=1)
    o.laps = laps;
    o.player.car.label = mine;
    cfg.exact = o;
    std::string err;
    m.field = race::RaceField::create(*m.track, db, cfg, &err);
    if (!m.field) std::printf("  field: %s\n", err.c_str());
    return m.field != nullptr;
}

// The machine's step: its car from `frame`, the other car posed from the newest state that has arrived (predicted to the
// end of the coming step), then its own state out through the wire format.
net::CarSnapshot machine_step(Machine& m, const ex::PadFrame& frame, bool physics) {
    race::RaceField& f = *m.field;
    const double now = f.race_time(), dt = 1.0 / f.tick_hz();
    while (!m.inbox.empty()) {
        net::CarSnapshot s = m.inbox.front();
        m.inbox.pop_front();
        race::complete_snapshot(s);
        if (!m.remote.attached())
            if (std::unique_ptr<ex::ExactRace> sh = race::exact_shadow(f, 0, s.entry)) m.remote.attach(std::move(sh), s.entry, physics);
        m.remote.receive(s, now);
    }
    if (m.remote.has()) f.set_remote_pose(0, m.remote.predict(now + dt));
    f.player().step(inputs_of(frame), dt);
    std::copy_backward(m.inputs, m.inputs + net::kInputHistory - 1, m.inputs + net::kInputHistory);
    const ex::PadFrame& g = (*race::exact_frames_of(f))[0];
    m.inputs[0] = {g.flags, g.kind, g.shift, g.steer, g.throttle, g.brake, g.back, g.third};
    m.ninputs = std::min(m.ninputs + 1, net::kInputHistory);
    const ex::Car& c = m.race().cars()[0];
    const int laps = std::max(0, int(c.laps) - int(c.lap_base) - 1);
    net::CarSnapshot s = race::make_snapshot(c, *race::exact_player_entry(f), ++m.tick, f.race_time(), laps, m.inputs, m.ninputs);
    net::Writer w;
    net::write_snapshot(w, s);
    net::Reader r(w.data);
    net::CarSnapshot back;
    CHECK(net::read_snapshot(r, back));
    return back;
}

struct Duel {
    int steps = 0, contacts_a = 0, contacts_b = 0, hits_sent = 0;
    double rms = 0, max = 0;        // A's pose of B's car against B's own, after each step (m)
    bool finished_a = false, finished_b = false;
    double finish_a = -1, finish_b = -1, observed_b = -1;
    int flags = 0;                  // plausibility warnings A raised about B
    double max_hit = 0;             // the strongest hit either machine saw (N s)
};

// B's driver that rams A: steer at A's car as B's machine has it posed, full throttle.
ex::PadFrame ram_driver(const ex::Car& me, const ex::Car& target) {
    ex::PadFrame f;
    f.flags = 0x1F;
    f.kind = 2;
    const ex::F angle = test::heading_angle(me, target.pos.x - me.pos.x, target.pos.y - me.pos.y);
    f.steer = s16(std::lround(std::clamp(angle.host() * 3.0f, -1.0f, 1.0f) * 4096.0f));
    f.throttle = 4096;
    return f;
}

// Two machines racing each other on `course`: A's car on slot 0, B's on slot 1, each driven by the line driver (B with a
// weave; with `ram_s` B drives at A's car for that long after the start), states `latency` steps late each way; contacts each
// machine saw go to the other as impulses.
Duel duel(const std::string& course, const std::string& car_a, const std::string& car_b, int latency, bool physics, int max_steps,
          float weave_b = 0.25f, int laps = 1, double ram_s = 0, bool ai = false, u8 config = 0, int jitter = 0,
          bool b_ahead = false) {
    Duel out;
    Machine a, b;
    if (!make_machine(a, course, car_a, 0, car_b, 1, laps, config) || !make_machine(b, course, car_b, 1, car_a, 0, laps, config)) {
        CHECK(false);
        return out;
    }
    race::Plausibility check;
    if (std::optional<sim::CarSpec> sp = sim::build_car_spec(*database(), car_b)) check.set_car(game::geared_top_speed(*sp));
    std::deque<std::pair<int, net::CarSnapshot>> to_a, to_b; // (deliver at step, state)
    std::deque<std::pair<int, net::Event::Hit>> hits_to_a, hits_to_b;
    double se = 0;
    int n = 0;
    for (int k = 0; k < max_steps; ++k) {
        while (!to_a.empty() && to_a.front().first <= k) a.inbox.push_back(to_a.front().second), to_a.pop_front();
        while (!to_b.empty() && to_b.front().first <= k) b.inbox.push_back(to_b.front().second), to_b.pop_front();
        auto apply = [&](Machine& m, std::deque<std::pair<int, net::Event::Hit>>& q) {
            while (!q.empty() && q.front().first <= k) {
                const net::Event::Hit& h = q.front().second;
                // As the race screen: only when this machine saw no contact with that car lately.
                if (m.field->last_contact_with(0) < m.field->race_time() - 0.2)
                    m.field->apply_player_impulse(h.point_x, h.point_z, h.jx, h.jz);
                q.pop_front();
            }
        };
        apply(a, hits_to_a), apply(b, hits_to_b);
        // After the ram (or from the start: `ai`) the race's AI drives both cars on (it backs out of walls; the shadows
        // follow the driver kind).
        if ((ram_s > 0 || ai) && k > 0 && a.field->race_time() >= ram_s && a.race().cars()[0].driver_kind == 0) {
            CHECK(a.field->player().ai_take_over());
            CHECK(b.field->player().ai_take_over());
        }
        // (while B rams, A waits on the grid with its brake on)
        ex::PadFrame fa = line_driver(a.race().cars()[0], a.race().course(), k);
        if (a.field->race_time() < ram_s) fa.throttle = 0, fa.steer = 0, fa.brake = 4096;
        const ex::PadFrame fb = b.field->race_time() < ram_s ? ram_driver(b.race().cars()[0], b.race().cars()[1])
                                                             : line_driver(b.race().cars()[0], b.race().course(), k, weave_b);
        // (`b_ahead`: B runs each step first and its state reaches A before A's step: one step ahead of A's race clock)
        std::optional<net::CarSnapshot> sb_first;
        if (b_ahead) a.inbox.push_back(*(sb_first = machine_step(b, fb, physics)));
        // A's view of B: the state arrives and is checked as the screen does.
        for (const net::CarSnapshot& s0 : a.inbox) {
            net::CarSnapshot s = s0;
            race::complete_snapshot(s);
            if (check.on_state(s, a.field->last_contact_with(0) > a.field->race_time() - 1.0)) ++out.flags;
        }
        net::CarSnapshot sa = machine_step(a, fa, physics);
        if (a.remote.physics() &&
            check.on_prediction_error(a.remote.last_error(), a.remote.last_error_horizon(),
                                      a.field->last_contact_with(0) > a.field->race_time() - 1.0))
            ++out.flags;
        net::CarSnapshot sb = sb_first ? *sb_first : machine_step(b, fb, physics);
        // (`jitter`: each state up to that many steps later still, out of order)
        u32 rnd = u32(k) * 2654435761u;
        to_b.push_back({k + 1 + latency + (jitter ? int((rnd >> 7) % u32(jitter + 1)) : 0), sa});
        if (!b_ahead) to_a.push_back({k + 1 + latency + (jitter ? int((rnd >> 17) % u32(jitter + 1)) : 0), sb});
        std::stable_sort(to_a.begin(), to_a.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        std::stable_sort(to_b.begin(), to_b.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        for (const race::RaceField::RemoteHit& h : a.field->take_remote_hits()) {
            hits_to_b.push_back({k + 1 + latency, {u32(k), h.point_x, h.point_z, h.jx, h.jz}}), ++out.hits_sent, ++out.contacts_a;
            out.max_hit = std::max(out.max_hit, double(std::hypot(h.jx, h.jz)));
            if (std::getenv("GT3R_DUEL_DEBUG")) std::printf("  t %.2f: A saw a hit, %.0f N s\n", a.field->race_time(), std::hypot(h.jx, h.jz));
        }
        for (const race::RaceField::RemoteHit& h : b.field->take_remote_hits()) {
            hits_to_a.push_back({k + 1 + latency, {u32(k), h.point_x, h.point_z, h.jx, h.jz}}), ++out.hits_sent, ++out.contacts_b;
            out.max_hit = std::max(out.max_hit, double(std::hypot(h.jx, h.jz)));
            if (std::getenv("GT3R_DUEL_DEBUG")) std::printf("  t %.2f: B saw a hit, %.0f N s\n", b.field->race_time(), std::hypot(h.jx, h.jz));
        }
        ++out.steps;
        if (std::getenv("GT3R_DUEL_DEBUG") && k % 60 == 0) {
            const ex::Car &ca = a.race().cars()[0], &cb = b.race().cars()[0];
            std::printf("  t %.1f: A (%.1f %.1f) %.1f km/h laps %d | B (%.1f %.1f) %.1f km/h laps %d | A sees B (%.1f %.1f)\n",
                        a.field->race_time(), ca.pos.x.host(), ca.pos.y.host(), ca.speed.host() * 3.6f, int(ca.laps),
                        cb.pos.x.host(), cb.pos.y.host(), cb.speed.host() * 3.6f, int(cb.laps), a.race().cars()[1].pos.x.host(),
                        a.race().cars()[1].pos.y.host());
        }
        // A's pose of B (the posed record, the end of this step) against B's car.
        if (a.remote.has() && a.field->race_time() > 0) {
            const ex::Car& posed = a.race().cars()[1];
            const ex::Car& real = b.race().cars()[0];
            const double e = std::hypot(double(posed.pos.x.host() - real.pos.x.host()), double(posed.pos.y.host() - real.pos.y.host()));
            se += e * e, ++n;
            out.max = std::max(out.max, e);
        }
        // Finishes: each owner's, sent to the other as a claim.
        if (!out.finished_a && a.field->player_finish() >= 0) {
            out.finished_a = true, out.finish_a = a.field->player_finish();
            b.field->set_remote_finish(0, out.finish_a);
        }
        if (!out.finished_b && b.field->player_finish() >= 0) {
            out.finished_b = true, out.finish_b = b.field->player_finish();
            a.field->set_remote_finish(0, out.finish_b);
        }
        if (out.observed_b < 0) out.observed_b = a.field->remote_observed_finish(0);
        if (out.finished_a && out.finished_b && out.observed_b >= 0) break;
    }
    if (!out.finished_b) {
        const ex::Car& c = b.race().cars()[0];
        std::printf("  B not finished: laps %d progress %.1f speed %.1f km/h kind %d control %d\n", int(c.laps), c.f3e4.host(),
                    c.speed.host() * 3.6f, int(c.driver_kind), int(c.control));
    }
    out.rms = n ? std::sqrt(se / n) : 0;
    return out;
}

} // namespace

TEST(net_exact_posed_car_contact) {
    if (!program() || !database()) SKIP("needs GT3R_DISC");
    // Our car parked on slot 0, the other player's car posed so that it drives into it from behind at 8 m/s: our car is
    // pushed forward, the hit reported for its owner pushes the remote car back, against its motion. The posed car's
    // record keeps the pose (its own phases do not run, the collision's result for it is thrown away).
    Machine m;
    CHECK(make_machine(m, "trialmt", "r34_gtr_v_99", 0, "supra_rz_97", 1, 1));
    if (!m.field) return;
    race::RaceField& f = *m.field;
    // The countdown runs out first (the cars are held on the grid until GO).
    while (f.race_time() < 0) f.player().step({}, 1.0 / 60);
    const ex::ExactRace& race = m.race();
    const ex::Car me = race.cars()[0];
    // The remote car's pose: behind ours along our heading, driving into it.
    ex::Car pose = race.cars()[1];
    const ex::F back = ex::F(9.0f), v = ex::F(8.0f);
    pose.pos = {me.pos.x - (me.fwd.x * back), me.pos.y - (me.fwd.y * back), me.pos.z};
    pose.fwd = me.fwd, pose.lat = me.lat, pose.up = me.up;
    pose.heading = me.heading, pose.pitch = me.pitch, pose.roll = me.roll;
    pose.vel = {me.fwd.x * v, me.fwd.y * v, ex::F()};
    pose.yaw_rate = ex::F();
    pose.road_seg = me.road_seg;
    pose.dt = me.dt, pose.time_scale = me.time_scale;
    bool hit = false;
    float pushed = 0;
    for (int k = 0; k < 120 && !hit; ++k) {
        pose.pos = {pose.pos.x + (pose.vel.x * pose.dt), pose.pos.y + (pose.vel.y * pose.dt), pose.pos.z};
        race::RemoteCar::Pose p;
        p.car = &pose;
        f.set_remote_pose(0, p);
        f.player().step({}, 1.0 / 60);
        const ex::Car& posed = race.cars()[1];
        CHECK(posed.pos.x == pose.pos.x && posed.pos.y == pose.pos.y && posed.vel.x == pose.vel.x); // the pose, kept
        for (const race::RaceField::RemoteHit& h : f.take_remote_hits()) {
            hit = true;
            // The impulse on the remote car (course frame: x, z = -y) points against its motion.
            const float along = h.jx * me.fwd.x.host() + h.jz * -me.fwd.y.host();
            CHECK(along < 0);
            CHECK(f.last_contact_with(0) >= f.race_time() - 1e-9);
        }
        const ex::Car& now = race.cars()[0];
        pushed = now.vel.x.host() * me.fwd.x.host() + now.vel.y.host() * me.fwd.y.host();
    }
    CHECK(hit);
    std::printf("  posed car hit: our car pushed to %.2f m/s along its heading\n", pushed);
    CHECK(pushed > 0.5f);
    // A ghost has no contact: the pose drives through our car.
    f.set_remote_ghost(0, true);
    CHECK(f.opponent_ghost(0));
    for (int k = 0; k < 60; ++k) {
        race::RemoteCar::Pose p;
        p.car = &pose;
        f.set_remote_pose(0, p);
        f.player().step({}, 1.0 / 60);
        CHECK(f.take_remote_hits().empty());
    }
}

TEST(net_exact_lobby_digest_is_the_race_params) {
    if (!program() || !database()) SKIP("needs GT3R_DISC");
    // The block the lobby digests for a descriptor (game::online_car_params) is the one the online race builds for that
    // player's car: a disc car and a garage car with a part and its engine condition, posed in a race here.
    data::CarDatabase& db = *database();
    game::OnlineCar disc_car = game::online_car_from_db(db, "supra_rz_97", 0);
    game::GarageCar g;
    g.car = "supra_rz_97";
    if (const data::Car* c = db.car(g.car)) g.parts = c->parts;
    if (std::optional<game::PartOffer> part = game::find_part(db, g.car, 6)) // (a goods of the tune shop)
        if (const int slot = game::part_slot(game::goods_info(6).table); slot >= 0) g.parts[size_t(slot)] = part->id;
    g.settings = sim::stock_settings(db, g.car, g.parts);
    g.cond.engine_wear_m = -150000;
    game::OnlineCar garage = game::online_car_from_garage(db, g);
    for (const game::OnlineCar& oc : {disc_car, garage}) {
        Machine m;
        std::optional<Bytes> file = course_file("trialmt");
        Disc* disc = test::test_disc();
        Vfs vfs(&disc->volume());
        m.track = race::make_track(vfs, *file, "trialmt");
        race::FieldConfig cfg;
        cfg.max_opponents = 0;
        cfg.player_slot = 0;
        race::FieldConfig::Remote rm;
        rm.car = oc.car, rm.slot = 1;
        rm.source = race::exact_online_source(db, oc, "trialmt", &rm.condition);
        cfg.remote.push_back(rm);
        race::ExactFieldOptions o;
        o.data = program();
        o.course_file = *file;
        o.course = "trialmt";
        o.kind = ex::Conductor::BattleMP;
        o.laps = 1;
        o.player.car.label = "r34_gtr_v_99";
        cfg.exact = o;
        m.field = race::RaceField::create(*m.track, db, cfg);
        CHECK(m.field != nullptr);
        if (!m.field) return;
        const std::optional<ex::ParamImage> lobby = game::online_car_params(db, *program(), oc);
        CHECK(lobby.has_value());
        if (!lobby) return;
        // Both through the record mirror (car_fields.h): the race's car, and the lobby's block loaded into a record.
        ex::Record in_race{}, from_lobby{}, img{};
        ex::store_image(m.race().cars()[1], in_race.data());
        std::copy(lobby->begin(), lobby->end(), img.begin() + ex::kParams);
        ex::Car t;
        ex::load_image(t, img.data());
        ex::store_image(t, from_lobby.data());
        CHECK(std::equal(in_race.begin() + ex::kParams, in_race.begin() + ex::kParams + ex::kParamsSize, from_lobby.begin() + ex::kParams));
    }
}

TEST(net_exact_contact_impulse) {
    if (!program() || !database()) SKIP("needs GT3R_DISC");
    Machine m;
    CHECK(make_machine(m, "trialmt", "r34_gtr_v_99", 0, "supra_rz_97", 1, 1));
    if (!m.field) return;
    race::RaceField& f = *m.field;
    const ex::ExactRace& race = m.race();
    const ex::Car c0 = race.cars()[0];
    const sim::collide::exact::Body& b = race.bodies()[0];
    // Through the CoG (course frame x, z = -y): pure translation, dv = J / m.
    f.apply_player_impulse(c0.pos.x.host(), -c0.pos.y.host(), 1200, 0);
    const ex::Car& c1 = race.cars()[0];
    CHECK(c1.vel.x == c0.vel.x + ex::F(1200.0f) * b.im && c1.yaw_rate == c0.yaw_rate);
    // Off the CoG: it also turns the car, as the collision step's response does: (r x J) / (10 I_z).
    const ex::Car c2 = race.cars()[0];
    f.apply_player_impulse(c0.pos.x.host() + 1.5f, -c0.pos.y.host(), 0, -1200); // course z -1200 = physics y +1200
    const ex::Car& c3 = race.cars()[0];
    CHECK(c3.yaw_rate.host() > c2.yaw_rate.host()); // r = (+1.5, 0), J = (0, +1200): counter-clockwise (Z up)
    // A non-finite impulse is ignored.
    const ex::Car c4 = race.cars()[0];
    f.apply_player_impulse(c0.pos.x.host(), 0, std::nanf(""), 0);
    CHECK(race.cars()[0].vel.x == c4.vel.x);
}

TEST(net_exact_duel) {
    if (!program() || !database()) SKIP("needs GT3R_DISC");
    // Two machines race a lap of Trial Mountain, 100 ms apart each way: each sees the other's car where its owner has it to
    // within centimetres, contacts reach both, both finish, and their finish claims agree with what the other machine saw.
    Duel d = duel("trialmt", "r34_gtr_v_99", "supra_rz_97", 6, true, 60 * 200);
    std::printf("  duel, 100 ms late: %d steps, prediction rms %.3f m (max %.2f m), contacts %d / %d, hits sent %d, finishes "
                "%.3f / %.3f s (B seen at %.3f s), %d warnings\n",
                d.steps, d.rms, d.max, d.contacts_a, d.contacts_b, d.hits_sent, d.finish_a, d.finish_b, d.observed_b, d.flags);
    CHECK(d.finished_a && d.finished_b);
    CHECK(d.observed_b >= 0 && std::fabs(d.observed_b - d.finish_b) < race::Plausibility::kFinishSlack);
    CHECK(d.rms < 0.5);
    CHECK(d.flags == 0); // honest driving raises no warning
}

TEST(net_exact_duel_contact) {
    if (!program() || !database()) SKIP("needs GT3R_DISC");
    // B drives into A for the first 6 s, 100 ms apart: both machines see contacts, each pushes its own car and sends the
    // other the impulse its car gave; both still finish, the prediction recovers.
    Duel d = duel("trialmt", "r34_gtr_v_99", "supra_rz_97", 6, true, 60 * 200, 0.25f, 1, 3.0);
    std::printf("  duel with contact, 100 ms late: %d steps, prediction rms %.3f m (max %.2f m), contacts A %d / B %d, hits "
                "sent %d, finishes %.3f / %.3f s, %d warnings\n",
                d.steps, d.rms, d.max, d.contacts_a, d.contacts_b, d.hits_sent, d.finish_a, d.finish_b, d.flags);
    CHECK(d.contacts_a > 0 && d.contacts_b > 0);
    CHECK(d.finished_a && d.finished_b);
    CHECK(d.flags == 0);
}

TEST(net_exact_duel_ai) {
    if (!program() || !database()) SKIP("needs GT3R_DISC");
    // Both cars driven by the race's AI from the start (as GT3R_RACE auto=1 online), 17 ms late each way: they race side
    // by side; the hits each machine sees are of the same size as the other's.
    Duel d = duel("trialmt", "r34_gtr_v_99", "supra_rz_97", 1, true, 60 * 200, 0, 1, 0, true);
    std::printf("  AI duel, 17 ms late: %d steps, prediction rms %.3f m (max %.2f m), contacts A %d / B %d, strongest hit "
                "%.0f N s, finishes %.3f / %.3f s, %d warnings\n",
                d.steps, d.rms, d.max, d.contacts_a, d.contacts_b, d.max_hit, d.finish_a, d.finish_b, d.flags);
    CHECK(d.finished_a && d.finished_b);
    CHECK(d.flags == 0);
}

TEST(net_exact_duel_autopilot) {
    if (!program() || !database()) SKIP("needs GT3R_DISC");
    // As two games with GT3R_RACE auto=1 on one machine: the race's AI drives both players' cars from the grid (the race's
    // configuration 3), the states arrive 0 to 2 steps late, some out of order.
    Duel d = duel("trialmt", "r34_gtr_v_99", "supra_rz_97", 0, true, 60 * 200, 0, 1, 0, false, 3, 2);
    std::printf("  autopilot duel, 0-33 ms late: %d steps, prediction rms %.3f m (max %.2f m), contacts A %d / B %d, strongest "
                "hit %.0f N s, finishes %.3f / %.3f s, %d warnings\n",
                d.steps, d.rms, d.max, d.contacts_a, d.contacts_b, d.max_hit, d.finish_a, d.finish_b, d.flags);
    CHECK(d.finished_a && d.finished_b);
    CHECK(d.flags == 0);
}

TEST(net_exact_stalled_machine) {
    // A's frames stall now and then (every 20 steps it runs none for three frames, then four at once), as a busy machine's
    // do: B's states then arrive from ahead of A's race clock. A poses B where B was at A's race time (the state waits), not
    // where B is already.
    if (!program() || !database()) SKIP("needs GT3R_DISC");
    Machine a, b;
    CHECK(make_machine(a, "trialmt", "r34_gtr_v_99", 0, "supra_rz_97", 1, 1, 3));
    CHECK(make_machine(b, "trialmt", "supra_rz_97", 1, "r34_gtr_v_99", 0, 1, 3));
    if (!a.field || !b.field) return;
    std::vector<ex::Vec3> b_at(1, ex::Vec3{}); // B's car after each of its steps (index: its tick)
    std::deque<net::CarSnapshot> to_a;
    double se = 0, worst = 0;
    int n = 0, owed = 0;
    for (int k = 0; k < 60 * 40; ++k) {
        for (const net::CarSnapshot& s : to_a) a.inbox.push_back(s);
        to_a.clear();
        ++owed;
        std::vector<std::pair<u32, ex::Vec3>> poses; // A's pose of B after each of its steps (its tick)
        if (k % 20 < 3 || k % 20 > 5) // A stalls these frames; it runs its owed steps on the next
            for (; owed > 0; --owed) {
                machine_step(a, {}, true);
                if (a.field->race_time() > 1) poses.push_back({a.tick, a.race().cars()[1].pos});
            }
        to_a.push_back(machine_step(b, {}, true));
        b_at.push_back(b.race().cars()[0].pos);
        for (const auto& [tick, p] : poses) {
            if (tick >= b_at.size()) continue;
            const ex::Vec3& real = b_at[tick];
            const double e = std::hypot(double(p.x.host() - real.x.host()), double(p.y.host() - real.y.host()));
            se += e * e, ++n;
            worst = std::max(worst, e);
        }
    }
    const double rms = n ? std::sqrt(se / n) : 0;
    std::printf("  A stalling 3 of every 20 steps: its pose of B rms %.3f m (max %.2f m) at A's race time\n", rms, worst);
    CHECK(n > 1000);
    CHECK(rms < 0.05);
}

TEST(net_exact_duel_one_step_ahead) {
    if (!program() || !database()) SKIP("needs GT3R_DISC");
    // B's machine runs a step ahead of A's (its state reaches A before A's own step of that time), two laps on the race's
    // AI: A poses B from a step of its own every step (the newest state waits a step), B's laps and finish in A's race are
    // B's own, no warning.
    Duel d = duel("trialmt", "r34_gtr_v_99", "supra_rz_97", 0, true, 60 * 300, 0, 2, 0, false, 3, 0, true);
    std::printf("  B a step ahead: %d steps, prediction rms %.3f m (max %.2f m), contacts A %d / B %d, finishes %.3f / %.3f s "
                "(B seen at %.3f s), %d warnings\n",
                d.steps, d.rms, d.max, d.contacts_a, d.contacts_b, d.finish_a, d.finish_b, d.observed_b, d.flags);
    CHECK(d.finished_a && d.finished_b);
    CHECK(d.observed_b >= 0 && std::fabs(d.observed_b - d.finish_b) < 0.1);
    CHECK(d.flags == 0);
}

TEST(net_exact_prediction_beats_dead_reckoning) {
    if (!program() || !database()) SKIP("needs GT3R_DISC");
    // The prediction shadow (the remote car run on with its owner's newest frame) against dead reckoning (the synced record
    // carried along its velocity), over 40 s of weaving, braking and cornering, 100 and 150 ms late.
    for (int latency : {6, 9}) {
        Duel dead = duel("trialmt", "r34_gtr_v_99", "supra_rz_97", latency, false, 60 * 40, 0.4f);
        Duel phys = duel("trialmt", "r34_gtr_v_99", "supra_rz_97", latency, true, 60 * 40, 0.4f);
        std::printf("  %3d ms late: dead reckoning rms %.3f m (max %.2f), prediction rms %.3f m (max %.2f)\n", latency * 1000 / 60,
                    dead.rms, dead.max, phys.rms, phys.max);
        CHECK(phys.rms < dead.rms * 0.6);
    }
}

TEST(net_exact_shadow_cost) {
    if (!program() || !database()) SKIP("needs GT3R_DISC");
    // A shadow step (a one-car race step without tyre effects): one per remote car and frame, plus a catch-up of the
    // latency when a state arrives.
    Machine m;
    CHECK(make_machine(m, "trialmt", "r34_gtr_v_99", 0, "supra_rz_97", 1, 1));
    if (!m.field) return;
    net::CarEntry e;
    std::unique_ptr<ex::ExactRace> sh = race::exact_shadow(*m.field, 0, e);
    CHECK(sh != nullptr);
    if (!sh) return;
    ex::PadFrame f;
    f.flags = 0x1F, f.kind = 2, f.throttle = 4096, f.steer = 600;
    const int n = 2000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) sh->step(std::span<const ex::PadFrame>(&f, 1));
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / n;
    std::printf("  shadow step %.1f us: 5 remote cars x (1 + 6 catch-up) steps = %.2f ms per frame\n", us, us * 35 / 1000);
    CHECK(us * 35 < 8000); // inside a frame even in a debug build
}
