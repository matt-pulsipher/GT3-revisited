// CPU cars on an online grid (docs/dev/ONLINE.md "CPU cars"): the host's machine runs them as opponents of its own race and
// sends their states; every other machine poses them as it poses a player's car, with a prediction shadow the race's AI drives
// with the car's driver overrides. Every machine is a real race::ExactField in one process; states go through the wire format,
// late; contacts, finishes and ghosting take the race screen's paths (screen_race_online.cpp) through the field's calls. Needs
// the disc (GT3R_DISC).

#include "test_framework.h"
#include "exact_drive.h"

#include "disc/core_image.h"
#include "disc/volume.h"
#include "game/data/car_database.h"
#include "game/net_car.h"
#include "net/protocol.h"
#include "race/backend.h"
#include "race/exact_field.h"
#include "race/exact_setup.h"
#include "race/net_race.h"
#include "sim/car_spec.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/race.h"
#include "sim/exact/race_inputs.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
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

// The arcade race's driver overrides for a skill, as the lobby takes them from the host's disc (A{E,N,H,P}ST0001).
std::optional<std::array<u8, 16>> skill_driver(char skill) {
    const data::Event* ev = database()->event(std::string("A") + skill + "ST0001");
    if (!ev || ev->bytes.size() < 0x1F4) return std::nullopt;
    return ex::driver_overrides(ex::race_config(ev->bytes));
}

// A player's driver on the pad path: aim at the AI line ahead, full throttle below a speed that falls with the aim's angle,
// the brake above it, a slow steering wave; or the race's AI (the test autopilot) when `ai`.
ex::PadFrame line_driver(const ex::Car& c, const ex::RaceCourse& course, int step) {
    ex::PadFrame f;
    f.flags = 0x1F;
    f.kind = 2;
    ex::F dx, dy;
    const int line = c.ai.line ? c.ai.line : 6;
    ex::ai_target(c, course.ai, line, c.ai.next, ex::F(12.0f) + ex::F(0.6f) * c.speed, dx, dy);
    const ex::F angle = test::heading_angle(c, dx, dy) + ex::F(0.05f) * ee::sin(ex::F(float(step) * 0.05f));
    f.steer = s16(std::lround(std::clamp(angle.host() * 2.5f, -1.0f, 1.0f) * 4096.0f));
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

// A place on the grid: a player's car (its machine), or one of the host's CPU cars (machine -1) with its skill.
struct Seat {
    std::string car;
    int slot = 0;
    int machine = -1;
    char skill = 'N';
};

// One machine, as the race screen sets it up (configure_online_field): its car 0, the host's CPU cars as its own opponents
// (host) or posed cars (everyone else), the other players' cars posed.
struct Machine {
    int id = 0, my_seat = -1;
    std::unique_ptr<race::RaceTrack> track;
    std::unique_ptr<race::RaceField> field;
    std::vector<int> opp_of; // by seat: the field's opponent index (-1: this machine's own car)
    std::vector<char> posed; // by seat
    std::vector<race::RemoteCar> remote;
    std::vector<race::Plausibility> checks;
    std::vector<std::deque<net::CarSnapshot>> inbox;
    net::PadInput inputs[net::kInputHistory];
    int ninputs = 0;
    u32 tick = 0;
    std::vector<u32> cpu_tick;
    const ex::ExactRace& race() const { return *race::exact_race_of(*field); }
    int seat_of_opp(int opp) const {
        for (size_t s = 0; s < opp_of.size(); ++s)
            if (opp_of[s] == opp) return int(s);
        return -1;
    }
};

struct Options {
    std::string course = "trialmt";
    int laps = 1;
    int latency = 6; // steps, each way
    int loss_pct = 0;
    bool humans_ai = true; // the race's AI drives the players' cars too (race pace, wheel to wheel)
};

bool make_machine(Machine& m, const std::vector<Seat>& seats, int id, const Options& o) {
    data::CarDatabase& db = *database();
    std::optional<Bytes> file = course_file(o.course);
    if (!file) return false;
    Disc* disc = test::test_disc();
    Vfs vfs(&disc->volume());
    m.id = id;
    m.track = race::make_track(vfs, *file, o.course);
    const int n = int(seats.size());
    m.opp_of.assign(size_t(n), -1), m.posed.assign(size_t(n), 0);
    m.remote.resize(size_t(n)), m.checks.resize(size_t(n)), m.inbox.resize(size_t(n));
    m.cpu_tick.assign(size_t(n), 0);
    for (int s = 0; s < n; ++s)
        if (seats[size_t(s)].machine == id) m.my_seat = s;
    race::FieldConfig cfg;
    cfg.max_opponents = 0;
    cfg.player_slot = seats[size_t(m.my_seat)].slot;
    std::vector<int> ours, theirs;
    for (int s = 0; s < n; ++s) {
        if (s == m.my_seat) continue;
        if (seats[size_t(s)].machine < 0 && id == 0) ours.push_back(s);
        else theirs.push_back(s);
    }
    if (!ours.empty()) cfg.grid_slots.push_back(cfg.player_slot);
    for (int s : ours) {
        const Seat& st = seats[size_t(s)];
        cfg.opponents.push_back(st.car);
        cfg.opponent_sources.push_back(race::ExactCarSource{st.car});
        cfg.opponent_drivers.push_back(skill_driver(st.skill));
        cfg.grid_slots.push_back(st.slot);
        ++cfg.max_opponents;
    }
    for (int s : theirs) {
        const Seat& st = seats[size_t(s)];
        race::FieldConfig::Remote rm;
        rm.car = st.car, rm.slot = st.slot, rm.source.label = st.car;
        if (st.machine < 0) rm.driver = skill_driver(st.skill);
        cfg.remote.push_back(rm);
    }
    race::ExactFieldOptions x;
    x.data = program();
    x.course_file = std::make_shared<const Bytes>(*file);
    x.course = o.course;
    x.kind = ex::Conductor::BattleMP;
    x.config = o.humans_ai ? 3 : 0;
    x.laps = o.laps;
    x.player.car.label = seats[size_t(m.my_seat)].car;
    cfg.exact = x;
    std::string err;
    m.field = race::RaceField::create(*m.track, db, cfg, &err);
    if (!m.field) {
        std::printf("  machine %d: %s\n", id, err.c_str());
        return false;
    }
    int k = 0;
    for (int s : ours) m.opp_of[size_t(s)] = k++;
    for (int s : theirs) m.opp_of[size_t(s)] = k++, m.posed[size_t(s)] = 1;
    for (int s : theirs)
        if (std::optional<sim::CarSpec> sp = sim::build_car_spec(db, seats[size_t(s)].car))
            m.checks[size_t(s)].set_car(game::geared_top_speed(*sp));
    return true;
}

net::CarSnapshot through_wire(const net::CarSnapshot& s) {
    net::Writer w;
    net::write_snapshot(w, s);
    net::Reader r(w.data);
    net::CarSnapshot back;
    CHECK(net::read_snapshot(r, back));
    return back;
}

struct Result {
    bool ok = false;
    double cpu_se = 0, cpu_max = 0;
    u64 cpu_n = 0;
    int flags = 0;
    std::vector<double> finish;          // by seat, as its owner had it (-1: not finished)
    std::vector<std::vector<double>> seen; // [machine][seat]: the finish each machine took (its own or the owner's claim)
    int cpu_hits = 0;                    // the host's CPU cars' contacts with posed players' cars (RemoteHit::from)
    int hits_on_cpu = 0, applied_on_cpu = 0;
    u64 order_same = 0, order_all = 0;
    double cpu_rms() const { return cpu_n ? std::sqrt(cpu_se / double(cpu_n)) : 0; }
};

Result run(const std::vector<Seat>& seats, const Options& o, int max_steps = 60 * 300) {
    Result out;
    const int n_seats = int(seats.size());
    int n_machines = 0;
    for (const Seat& s : seats) n_machines = std::max(n_machines, s.machine + 1);
    std::vector<Machine> ms(static_cast<size_t>(n_machines));
    for (int i = 0; i < n_machines; ++i)
        if (!make_machine(ms[size_t(i)], seats, i, o)) {
            CHECK(false);
            return out;
        }
    out.finish.assign(size_t(n_seats), -1);
    out.seen.assign(size_t(n_machines), std::vector<double>(size_t(n_seats), -1));
    auto owner = [&](int seat) { return std::max(0, seats[size_t(seat)].machine); };
    auto is_cpu = [&](int seat) { return seats[size_t(seat)].machine < 0; };
    struct State {
        int to, seat;
        net::CarSnapshot s;
    };
    struct Hit {
        int to, target, from;
        race::RaceField::RemoteHit h;
    };
    struct Fin {
        int to, seat;
        double t;
    };
    std::multimap<int, State> states;
    std::multimap<int, Hit> hits;
    std::multimap<int, Fin> fins;
    std::vector<char> done(size_t(n_seats), 0);
    auto send_state = [&](int k, int from, int seat, const net::CarSnapshot& s) {
        for (int to = 0; to < n_machines; ++to) {
            if (to == from) continue;
            const u32 rnd = (u32(k) * 2654435761u) ^ (u32(seat) * 40503u) ^ (u32(to) * 2166136261u);
            if (o.loss_pct && int((rnd >> 9) % 100) < o.loss_pct) continue;
            states.insert({k + 1 + o.latency, {to, seat, s}});
        }
    };
    auto finish = [&](int k, int seat, double t) {
        if (done[size_t(seat)]) return;
        done[size_t(seat)] = 1;
        out.finish[size_t(seat)] = t;
        for (int to = 0; to < n_machines; ++to)
            if (to != owner(seat)) fins.insert({k + 1 + o.latency, {to, seat, t}});
    };
    int k = 0, end = -1;
    for (; k < max_steps; ++k) {
        for (auto it = states.begin(); it != states.end() && it->first <= k; it = states.erase(it))
            ms[size_t(it->second.to)].inbox[size_t(it->second.seat)].push_back(it->second.s);
        for (auto it = fins.begin(); it != fins.end() && it->first <= k; it = fins.erase(it)) {
            Machine& m = ms[size_t(it->second.to)];
            m.field->set_remote_finish(m.opp_of[size_t(it->second.seat)], it->second.t);
            out.seen[size_t(m.id)][size_t(it->second.seat)] = it->second.t;
        }
        // Contacts (the race screen's K::Contact): the host answers for its CPU cars, a player's machine for its car; only a
        // hit its own machine missed, from a car that is no ghost.
        for (auto it = hits.begin(); it != hits.end() && it->first <= k; it = hits.erase(it)) {
            const Hit& h = it->second;
            Machine& m = ms[size_t(h.to)];
            const double now = m.field->race_time();
            const int from = m.opp_of[size_t(h.from)];
            if (from < 0 || m.field->opponent_ghost(from) || m.field->opponent_retired(from)) continue;
            if (is_cpu(h.target)) {
                ++out.hits_on_cpu;
                const int cpu = m.opp_of[size_t(h.target)];
                if (m.field->opponent_ghost(cpu) || m.field->last_contact_between(cpu, from) >= now - 0.2) continue;
                m.field->apply_opponent_impulse(cpu, h.h.point_x, h.h.point_z, h.h.jx, h.h.jz);
                ++out.applied_on_cpu;
            } else if (m.field->last_contact_with(from) < now - 0.2) {
                m.field->apply_player_impulse(h.h.point_x, h.h.point_z, h.h.jx, h.h.jz);
            }
        }
        for (Machine& m : ms) {
            race::RaceField& f = *m.field;
            const double now = f.race_time(), dt = 1.0 / f.tick_hz();
            for (int s = 0; s < n_seats; ++s) {
                if (!m.posed[size_t(s)]) continue;
                const int opp = m.opp_of[size_t(s)];
                race::RemoteCar& rc = m.remote[size_t(s)];
                for (net::CarSnapshot st : m.inbox[size_t(s)]) {
                    race::complete_snapshot(st);
                    if (!rc.attached())
                        if (std::unique_ptr<ex::ExactRace> sh = race::exact_shadow(f, opp, st.entry)) rc.attach(std::move(sh), st.entry);
                    rc.receive(st, now);
                    const bool contact = f.last_local_contact(opp) > now - 1.0;
                    if (m.checks[size_t(s)].on_state(st, contact)) ++out.flags;
                    if (rc.physics() && m.checks[size_t(s)].on_prediction_error(rc.last_error(), rc.last_error_horizon(), contact))
                        ++out.flags;
                }
                m.inbox[size_t(s)].clear();
                f.set_remote_ghost(opp, now < 3.0);
                if (rc.has()) f.set_remote_pose(opp, rc.predict(now + dt));
            }
            const ex::PadFrame frame = line_driver(m.race().cars()[0], m.race().course(), k);
            f.player().step(inputs_of(frame), dt);
            std::copy_backward(m.inputs, m.inputs + net::kInputHistory - 1, m.inputs + net::kInputHistory);
            const ex::PadFrame& g = (*race::exact_frames_of(f))[0];
            m.inputs[0] = {g.flags, g.kind, g.shift, g.steer, g.throttle, g.brake, g.back, g.third};
            m.ninputs = std::min(m.ninputs + 1, net::kInputHistory);
            const ex::Car& c0 = m.race().cars()[0];
            send_state(k, m.id, m.my_seat,
                       through_wire(race::make_snapshot(c0, *race::exact_player_entry(f), ++m.tick, f.race_time(),
                                                        std::max(0, int(c0.laps) - int(c0.lap_base) - 1), m.inputs, m.ninputs)));
            // The host's CPU cars, 30 states a second each, staggered (RaceScreen::online_cpu_cars).
            if (m.id == 0)
                for (int s = 0; s < n_seats; ++s) {
                    if (!is_cpu(s) || (m.tick + u32(m.opp_of[size_t(s)])) % 2 != 0) continue;
                    const ex::Car& c = m.race().cars()[size_t(1 + m.opp_of[size_t(s)])];
                    net::CarEntry e = race::car_entry(c, true, true);
                    e.control = 1;
                    send_state(k, 0, s, through_wire(race::make_snapshot(c, e, ++m.cpu_tick[size_t(s)], f.race_time(),
                                                                         std::max(0, int(c.laps) - int(c.lap_base) - 1), nullptr, 0)));
                }
            // Our cars' hits on posed cars: to the struck car's owner, from the car that struck it.
            for (const race::RaceField::RemoteHit& h : f.take_remote_hits()) {
                const int target = m.seat_of_opp(h.opponent), from = h.from < 0 ? m.my_seat : m.seat_of_opp(h.from);
                if (target < 0 || from < 0) continue;
                if (h.from >= 0) {
                    CHECK(m.id == 0 && is_cpu(from) && !is_cpu(target));
                    ++out.cpu_hits;
                }
                hits.insert({k + 1 + o.latency, {owner(target), target, from, h}});
            }
            if (f.player_finish() >= 0) finish(k, m.my_seat, f.player_finish());
            if (m.id == 0)
                for (const race::RaceField::Standing& st : f.standings())
                    if (st.finished && st.car >= 0)
                        if (const int s = m.seat_of_opp(st.car); s >= 0 && is_cpu(s)) finish(k, s, st.time);
        }
        // Every machine's pose of each CPU car against the car itself (the host's), at the same step.
        if (ms[0].field->race_time() > 0)
            for (size_t i = 1; i < ms.size(); ++i)
                for (int s = 0; s < n_seats; ++s) {
                    if (!is_cpu(s) || done[size_t(s)] || !ms[i].remote[size_t(s)].has()) continue;
                    const ex::Car& posed = ms[i].race().cars()[size_t(1 + ms[i].opp_of[size_t(s)])];
                    const ex::Car& real = ms[0].race().cars()[size_t(1 + ms[0].opp_of[size_t(s)])];
                    const double e = std::hypot(double(posed.pos.x.host() - real.pos.x.host()), double(posed.pos.y.host() - real.pos.y.host()));
                    out.cpu_se += e * e, ++out.cpu_n, out.cpu_max = std::max(out.cpu_max, e);
                }
        // The race order on every machine against the host's.
        if (ms[0].field->race_time() > 3) {
            auto order = [&](const Machine& m) {
                std::vector<int> seat_at;
                for (const race::RaceField::Standing& st : m.field->standings())
                    seat_at.push_back(st.car == -1 ? m.my_seat : m.seat_of_opp(st.car));
                return seat_at;
            };
            const std::vector<int> ref = order(ms[0]);
            for (size_t i = 1; i < ms.size(); ++i) {
                ++out.order_all;
                if (order(ms[i]) == ref) ++out.order_same;
            }
        }
        // Everyone done: on until the last finish has reached every machine.
        if (end < 0 && std::all_of(done.begin(), done.end(), [](char d) { return d != 0; })) end = k + o.latency + 2;
        if (end >= 0 && k >= end) break;
    }
    // What each machine took for its own car's and the host for its CPU cars' finishes.
    for (Machine& m : ms) {
        out.seen[size_t(m.id)][size_t(m.my_seat)] = out.finish[size_t(m.my_seat)];
        if (m.id == 0)
            for (int s = 0; s < n_seats; ++s)
                if (is_cpu(s)) out.seen[0][size_t(s)] = out.finish[size_t(s)];
    }
    // A finished CPU car is a ghost on the host as on everyone else's machine.
    for (int s = 0; s < n_seats; ++s)
        if (is_cpu(s) && done[size_t(s)]) CHECK(ms[0].field->opponent_ghost(ms[0].opp_of[size_t(s)]));
    out.ok = true;
    std::printf("    %d steps; CPU cars posed %.1f cm rms, %.2f m max; %d warnings; %d CPU car hits on players, %d hits on CPU "
                "cars (%d applied); order agreed %.1f %%\n",
                k, out.cpu_rms() * 100, out.cpu_max, out.flags, out.cpu_hits, out.hits_on_cpu, out.applied_on_cpu,
                out.order_all ? 100.0 * double(out.order_same) / double(out.order_all) : 100.0);
    return out;
}

bool have_disc() {
    if (!database() || !program() || !course_file("trialmt")) {
        std::printf("    skip: no disc (GT3R_DISC)\n");
        return false;
    }
    return true;
}

} // namespace

// Two players and four CPU cars of mixed skill on Trial Mountain, 100 ms apart, every car on the race's AI: everyone finishes,
// the client sees the CPU cars where the host has them, everyone takes the same finishes, no warnings.
TEST(net_cpu_cars_race) {
    if (!have_disc()) return;
    const std::vector<Seat> seats = {
        {"skyline_gt_r_v_spec_ii_r34_99", 0, -1, 'H'}, {"supra_rz_97", 1, -1, 'N'}, {"r34_gtr_v_99", 2, 0},
        {"nsx_type_r_92", 3, -1, 'E'},                {"supra_rz_97", 4, 1},       {"r34_gtr_v_99", 5, -1, 'P'},
    };
    std::vector<Seat> s = seats;
    for (Seat& x : s)
        if (!database()->car(x.car)) x.car = "r34_gtr_v_99"; // (labels differ between releases: any car will do)
    Options o;
    o.loss_pct = 5;
    const Result r = run(s, o);
    CHECK(r.ok);
    if (!r.ok) return;
    for (size_t i = 0; i < s.size(); ++i) {
        CHECK(r.finish[i] > 0);
        for (size_t m = 0; m < r.seen.size(); ++m) CHECK(std::fabs(r.seen[m][i] - r.finish[i]) < 1e-6);
    }
    CHECK(r.cpu_rms() < 0.10);
    CHECK(r.cpu_max < 3.0);
    CHECK(r.flags == 0);
    CHECK(r.order_all > 0 && double(r.order_same) >= 0.95 * double(r.order_all));
}

// A CPU car's prediction drives with its own skill: the client's shadow of it starts with the host's CPU car's driver
// (throttle, brake and corner speed from the skill's overrides), not the race's.
TEST(net_cpu_cars_shadow_skill) {
    if (!have_disc() || !skill_driver('E') || !skill_driver('P')) return;
    std::vector<Seat> seats = {{"r34_gtr_v_99", 0, 0}, {"r34_gtr_v_99", 1, -1, 'E'}, {"r34_gtr_v_99", 2, -1, 'P'}, {"r34_gtr_v_99", 3, 1}};
    Options o;
    o.humans_ai = false;
    Machine host, client;
    CHECK(make_machine(host, seats, 0, o));
    CHECK(make_machine(client, seats, 1, o));
    if (!host.field || !client.field) return;
    for (int s : {1, 2}) {
        const ex::Car& real = host.race().cars()[size_t(1 + host.opp_of[size_t(s)])];
        net::CarEntry e = race::car_entry(real, true, true);
        e.control = 1;
        std::unique_ptr<ex::ExactRace> sh = race::exact_shadow(*client.field, client.opp_of[size_t(s)], e);
        CHECK(sh != nullptr);
        if (!sh) continue;
        const ex::Driver& a = real.ai;
        const ex::Driver& b = sh->cars()[0].ai;
        CHECK(a.throttle_scale == b.throttle_scale);
        CHECK(a.brake_factor == b.brake_factor);
        CHECK(a.speed_scale == b.speed_scale);
        CHECK(a.speed_offset == b.speed_offset);
    }
    // Easy and Professional drive differently (the overrides reached the cars at all).
    const ex::Driver& easy = host.race().cars()[size_t(1 + host.opp_of[1])].ai;
    const ex::Driver& pro = host.race().cars()[size_t(1 + host.opp_of[2])].ai;
    CHECK(!(easy.speed_scale == pro.speed_scale) || !(easy.throttle_scale == pro.throttle_scale) ||
          !(easy.brake_factor == pro.brake_factor));
}
