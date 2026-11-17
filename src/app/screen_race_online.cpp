// Online battle glue (docs/dev/ONLINE.md): the field from the session's grid, network
// events, the synchronised start and tick pacing, remote car prediction, plausibility
// reports and ghosting.
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"

#include "sim/exact/race.h"

namespace gt3r::race_screen {

// Every player runs only its own car on the exact core; the others are posed from their snapshots (RemoteCar prediction)
// in the field's race (race::exact_shadow, ExactRace::pose). All players start the countdown at the same wall-clock instant
// and pace their steps from it.

namespace {

constexpr u32 kGoDelayMs = 1500;          // host: Go -> countdown start
constexpr double kClockEase = 0.05;       // racing: a start-time correction runs our race clock at most 5% fast or slow
constexpr double kContactWindow = 0.2;    // s: our own contact this recent makes theirs a duplicate
constexpr double kStartGhost = 3.0;       // s after the start signal

} // namespace

void RaceScreen::configure_online_field(FieldConfig& cfg) {
    net::Session& ns = net::online();
    const net::Settings& st = ns.settings();
    cfg.opponents.clear();
    cfg.opponent_sources.clear(), cfg.opponent_drivers.clear(), cfg.opponent_skills.clear();
    cfg.grid_slots.clear();
    cfg.max_opponents = 0;
    cfg.event.clear();
    cfg.battle_wear = ns.settings().tire_wear; // the host's (the lobby refuses the start while a player's game lacks it)
    net_.player.clear(), net_.paint.clear(), net_.names.clear(), net_.top_speed.clear();
    net_.cpu.clear(), net_.local.clear(), net_.cpu_tick.clear(), net_.cpu_finish_sent.clear();
    // The field's opponents: the CPU cars we run (the host's), then the other cars (players' and, for everyone but the host,
    // the host's CPU cars), each on its grid slot (pole first).
    struct Seat {
        u8 id;
        int slot;
    };
    std::vector<Seat> ours, theirs;
    const std::vector<u8>& grid = ns.grid();
    for (size_t slot = 0; slot < grid.size(); ++slot) {
        const u8 id = grid[slot];
        if (int(id) == ns.my_id()) cfg.player_slot = int(slot);
        else if (net::is_cpu_id(id) && ns.is_host()) ours.push_back({id, int(slot)});
        else theirs.push_back({id, int(slot)});
    }
    // A car's descriptor as this machine races it: its label, the car as raced, its top speed (plausibility).
    struct Car {
        std::string label;
        ExactCarSource source;
        sim::EngineCondition condition;
        float top_speed = 150.0f; // (m/s: unknown gearing, a generous bound)
        std::string name;         // the car's name (a CPU car's name)
    };
    auto car_of = [&](u8 id, const std::string& fallback, const std::string& who) {
        Car c;
        c.label = fallback;
        // Their car as they race it (garage parts and settings, arcade tyres: weight, size and
        // grip count in contact and prediction). The lobby has checked that it races here as
        // its owner races it; one that does not is the stock car, or a stand-in when unknown.
        if (const Bytes* info = ns.car_info(id))
            if (std::optional<game::OnlineCar> oc = game::decode_online_car(*info)) {
                std::string base = game::online_car_base(db_, *oc); // an arcade car's twin on this disc
                c.label = base.empty() ? oc->car : base;
                c.name = oc->name.empty() ? c.label : oc->name;
                std::string err;
                if (std::optional<sim::CarSpec> sp = game::online_car_spec(db_, *oc, &err, preset_.course))
                    c.top_speed = game::geared_top_speed(*sp);
                else LOG_WARN("Online: %s's car: %s", who.c_str(), err.c_str());
                if (!base.empty()) c.source = exact_online_source(db_, *oc, preset_.course, &c.condition);
            }
        return c;
    };
    auto cpu_of = [&](u8 id) -> const net::Settings::CpuCar* {
        const size_t k = size_t(id - net::kCpuId);
        return net::is_cpu_id(id) && k < st.cpu.size() ? &st.cpu[k] : nullptr;
    };
    auto add = [&](u8 id, int paint, const std::string& name, float top_speed, bool cpu, bool local) {
        net_.player.push_back(id);
        net_.paint.push_back(paint);
        net_.names.push_back(name);
        net_.top_speed.push_back(top_speed);
        net_.cpu.push_back(cpu);
        net_.local.push_back(local);
    };
    // Our CPU cars (the host's): opponents of this race, driven by its AI with the skill's driver overrides.
    if (!ours.empty()) cfg.grid_slots.push_back(cfg.player_slot);
    for (const Seat& s : ours) {
        const net::Settings::CpuCar* cc = cpu_of(s.id);
        const std::string who = "CPU car " + std::to_string(s.id - net::kCpuId + 1);
        Car c = car_of(s.id, "", who);
        if (!cc || c.label.empty()) {
            LOG_WARN("Online: %s has no car; its place stays empty", who.c_str());
            continue;
        }
        cfg.opponents.push_back(c.label);
        cfg.opponent_sources.push_back(c.source);
        cfg.opponent_drivers.push_back(cc->driver);
        cfg.grid_slots.push_back(s.slot);
        ++cfg.max_opponents;
        add(s.id, cc->paint, c.name, c.top_speed, true, true);
    }
    if (cfg.grid_slots.size() < 2) cfg.grid_slots.clear();
    for (const Seat& s : theirs) {
        const net::Player* p = ns.player(s.id);
        const net::Settings::CpuCar* cc = cpu_of(s.id);
        if (!p && !cc) continue; // left before the race
        const std::string who = p ? p->name : "CPU car " + std::to_string(s.id - net::kCpuId + 1);
        Car c = car_of(s.id, p ? p->car : std::string(), who);
        if (c.label.empty()) {
            LOG_WARN("Online: %s has no car; its place stays empty", who.c_str());
            continue;
        }
        FieldConfig::Remote rm{c.label, s.slot, c.source, c.condition, std::nullopt};
        if (cc) rm.driver = cc->driver; // (the host's CPU car: its prediction's AI drives with the same skill)
        cfg.remote.push_back(rm);
        add(s.id, p ? p->paint : cc->paint, cc ? c.name : who, c.top_speed, cc != nullptr, false);
    }
    net_.cpu_tick.assign(net_.player.size(), 0);
    net_.reported_contact.assign(net_.player.size(), -1e18);
    net_.cpu_finish_sent.assign(net_.player.size(), false);
    net_.cars.clear();
    net_.cars.resize(net_.player.size());
    net_.wait = ns.joined() ? NetWait::Loading : NetWait::None;
    net_.lost = !ns.joined();
    net_.ticks = 0;
    net_.state_tick = 0;
    net_.input_count = 0;
    net_.ghost.clear();
    net_.checks.clear();
    net_.claims.clear();
    net_.finish_sent = net_.loaded_sent = false;
}

int RaceScreen::net_index(u8 player) const {
    for (size_t i = 0; i < net_.player.size(); ++i)
        if (net_.player[i] == int(player)) return int(i);
    return -1;
}

void RaceScreen::online_retire(int i) {
    if (!field_ || i < 0 || i >= field_->opponents() || field_->opponent_retired(i)) return;
    field_->set_remote_retired(i);
    if (size_t(i) < opp_visuals_.size()) opp_visuals_[size_t(i)].reset(); // no longer drawn
}

void RaceScreen::online_event(App& app, const net::Event& e) {
    using K = net::Event::Kind;
    switch (e.kind) {
    case K::Go:
        net_.start_at = e.start_at_ms;
        if (net_.wait != NetWait::None) net_.wait = NetWait::Starting;
        break;
    case K::Finish:
        if (int i = net_index(e.player); i >= 0 && field_) {
            field_->set_remote_finish(i, e.time);
            net_.claims.resize(net_.player.size(), -1);
            net_.claims[size_t(i)] = e.time;
            LOG_INFO("Online: %s finished in %.3f s", net_.names[size_t(i)].c_str(), e.time);
        }
        break;
    case K::Contact:
        // That car was in a contact (the plausibility checks allow it a shove), whether or not ours saw it.
        if (int i = net_index(e.player); i >= 0 && field_ && size_t(i) < net_.reported_contact.size())
            net_.reported_contact[size_t(i)] = field_->race_time();
        // Host: a player's machine saw their car hit one of our CPU cars. The host answers for it as a player's machine
        // does for its own car: only a hit it missed.
        if (net::is_cpu_id(e.target)) {
            const int cpu = net_index(e.target), from = net_index(e.player);
            if (cpu < 0 || from < 0 || !field_ || !net_.local[size_t(cpu)] || field_->opponent_ghost(from) ||
                field_->opponent_retired(from) || field_->opponent_ghost(cpu))
                break;
            if (field_->last_contact_between(cpu, from) >= field_->race_time() - kContactWindow) break;
            field_->apply_opponent_impulse(cpu, e.contact.point_x, e.contact.point_z, e.contact.jx, e.contact.jz);
            ++net_.contacts_applied;
            LOG_INFO("Online: %s's machine saw a hit on CPU car %d ours missed (%.0f N s), applied", net_.names[size_t(from)].c_str(),
                     int(e.target - net::kCpuId) + 1, std::hypot(e.contact.jx, e.contact.jz));
            break;
        }
        // Another player's machine saw their car (or the host's CPU car) hit ours. If ours saw the contact too (within
        // 0.2 s) it has already been solved here; a ghosted car never touches us.
        if (int i = net_index(e.player); i >= 0 && field_ && !field_->opponent_ghost(i) && !field_->opponent_retired(i)) {
            if (field_->last_contact_with(i) >= field_->race_time() - kContactWindow) break;
            field_->apply_player_impulse(e.contact.point_x, e.contact.point_z, e.contact.jx, e.contact.jz);
            ++net_.contacts_applied;
            LOG_INFO("Online: %s's machine saw a hit ours missed (%.0f N s), applied", net_.names[size_t(i)].c_str(),
                     std::hypot(e.contact.jx, e.contact.jz));
        }
        break;
    case K::Flag: { // host: a warning about a player
        const net::Session& ns = net::online();
        const net::Player* sp = ns.player(e.player);
        const net::Player* rp = ns.player(e.flag.reporter);
        toast(std::string("Warning: ") + (sp ? sp->name : "?") + " " + net::flag_text(e.flag.kind) + " (seen by " +
              (rp ? rp->name : "?") + ")");
        break;
    }
    case K::Retire:
        if (int i = net_index(e.player); i >= 0) {
            online_retire(i);
            toast(net_.names[size_t(i)] + " left the race");
        }
        break;
    case K::Disconnected:
    case K::Rejected:
        net_.lost = true;
        for (int i = 0; i < int(net_.player.size()); ++i)
            if (!net_.local[size_t(i)]) online_retire(i); // (our CPU cars race on with us)
        if (net_.wait != NetWait::None) net_.wait = NetWait::None; // drive on alone
        toast("Connection lost: " + e.text);
        break;
    case K::Lobby: // the host went back to the lobby: the race is over for everyone
        net_.left = true;
        leave(app);
        break;
    default: break; // roster / settings: the lobby reads the session when it resumes
    }
}

// Every frame: network in/out and the start handshake. false = skip the rest of the frame.
bool RaceScreen::online_update(App& app, double dt) {
    net::Session& ns = net::online();
    ns.poll();
    while (std::optional<net::Event> e = ns.next_event()) {
        online_event(app, *e);
        if (net_.left) return false;
    }
    if (net_.wait == NetWait::Loading) {
        if (!net_.loaded_sent) ns.loaded(), net_.loaded_sent = true;
        if (ns.is_host() && ns.all_loaded()) {
            ns.go(kGoDelayMs);
            while (std::optional<net::Event> e = ns.next_event()) online_event(app, *e);
        }
        if (net_.wait == NetWait::Loading) toast("Waiting for the other players...");
    }
    // The start time as the clock sync refines it. Once racing, a correction is eased in: in one
    // jump, the few ms the host clock's estimate moves by over a long link moved everything drawn
    // by part of a tick at once.
    if (const double refined = ns.race_start_ms(); refined > 0) {
        const double ease = net_.wait == NetWait::None && net_.start_at > 0 ? kClockEase * dt * 1000.0 : 1e300;
        net_.start_at = std::clamp(refined, net_.start_at - ease, net_.start_at + ease);
    }
    if (net_.wait == NetWait::Starting) {
        if (ns.now() >= net_.start_at) net_.wait = NetWait::None, step_.reset();
        else toast("Get ready!");
    }
    online_ghosts(ns);
    if (finished_ && !net_.finish_sent && field_) {
        net_.finish_sent = true;
        double t = race_time_;
        for (const RaceField::Standing& st : field_->standings())
            if (st.car == -1) t = st.time;
        ns.send_finish(t, track_->best_lap_time());
        LOG_INFO("Online: finished in %.3f s; contacts: %d hits sent, %d applied from the others", t, net_.contacts_sent,
                 net_.contacts_applied);
        for (size_t i = 0; i < net_.cars.size() && i < net_.names.size(); ++i)
            if (!net_.local[i]) // (our CPU cars: run here)
                LOG_INFO("Online: %s: %u states, prediction %s, last error %.3f m", net_.names[i].c_str(),
                     ns.remote(u8(net_.player[i])) ? ns.remote(u8(net_.player[i]))->count : 0u,
                     net_.cars[i].physics() ? "exact shadow" : "dead reckoning", net_.cars[i].last_error());
    }
    return true;
}

// Ticks to run this frame: none before the start, then as many as the wall clock since
// the synchronised start asks for (a stalled frame catches up instead of falling behind).
int RaceScreen::online_ticks(int n) {
    if (net_.wait != NetWait::None) {
        step_.reset();
        return 0;
    }
    if (net_.lost || net_.start_at <= 0) return n;
    // The drawn blend between ticks follows the same clock: blended by the frame times instead,
    // above 60 fps the picture stepped half a tick back on every other frame.
    const double per_ms = (field_ ? field_->tick_hz() : sim::kDefaultTickHz) / 1000.0;
    return step_.follow((net::online().now() - net_.start_at) * per_ms, net_.ticks, 30);
}

void RaceScreen::online_before_step(double dt) {
    if (!field_) return;
    net::Session& ns = net::online();
    double now = field_->race_time();
    for (size_t i = 0; i < net_.player.size() && int(i) < field_->opponents(); ++i) {
        if (net_.local[i]) continue; // (our CPU car: our race runs it)
        RemoteCar& rc = net_.cars[i];
        const net::Session::Remote* r = ns.remote(u8(net_.player[i]));
        // The prediction shadow: their car alone on a one-car race of our course, built for their entry (ASM / TCS, the
        // steering wheel, the AI's autopilot) once their first state is here.
        auto built_for = [](const net::CarEntry& a, const net::CarEntry& b) {
            return a.asm_on == b.asm_on && a.tcs_on == b.tcs_on && a.wheel_type == b.wheel_type && (a.control == 1) == (b.control == 1);
        };
        if (r && (!rc.attached() || !built_for(rc.shadow_entry(), r->snap.entry)))
            if (std::unique_ptr<sim::exact::ExactRace> sh = exact_shadow(*field_, int(i), r->snap.entry))
                rc.attach(std::move(sh), r->snap.entry, net_.physics);
        if (r && (!rc.has() || r->snap.tick > rc.latest().tick)) {
            net::CarSnapshot snap = r->snap;
            complete_snapshot(snap);
            rc.receive(snap, now);
            // Plausibility (what their car can physically do); the host hears about it.
            if (net_.checks.size() < net_.player.size()) {
                net_.checks.resize(net_.player.size());
                for (size_t k = 0; k < net_.checks.size() && k < net_.top_speed.size(); ++k) net_.checks[k].set_car(net_.top_speed[k]);
            }
            bool contact = field_->last_local_contact(int(i)) > now - 1.0 ||
                           (i < net_.reported_contact.size() && net_.reported_contact[i] > now - 1.0);
            auto report = [&](std::optional<Plausibility::Flag> f) {
                if (!f) return;
                if (net_.cpu[i]) // (the host's own CPU car: nobody to warn about; a sign of a fault)
                    LOG_WARN("Online: %s (CPU car) %s (%.2f)", net_.names[i].c_str(), net::flag_text(f->kind), f->value);
                else ns.report_flag(u8(net_.player[i]), f->kind, f->value);
            };
            report(net_.checks[i].on_state(snap, contact));
            if (rc.physics()) report(net_.checks[i].on_prediction_error(rc.last_error(), rc.last_error_horizon(), contact));
        }
        // Its finish claim against when we saw it cross the line.
        if (i < net_.claims.size() && net_.claims[i] >= 0 && field_->remote_observed_finish(int(i)) >= 0 &&
            i < net_.checks.size()) {
            if (auto f = net_.checks[i].on_finish(net_.claims[i], field_->remote_observed_finish(int(i)))) {
                if (net_.cpu[i]) LOG_WARN("Online: %s (CPU car) %s (%.2f)", net_.names[i].c_str(), net::flag_text(f->kind), f->value);
                else ns.report_flag(u8(net_.player[i]), f->kind, f->value);
            }
            net_.claims[i] = -1;
        }
        if (rc.has()) {
            const RemoteCar::Pose p = rc.predict(now + dt);
            field_->set_remote_pose(int(i), p);
            // GT3R_NET_DEBUG=1: our car, the pose and the newest state twice a second.
            static const bool debug = SDL_getenv("GT3R_NET_DEBUG") != nullptr;
            if (debug && p.car && (net_.state_tick % 30) == 0)
                if (const sim::exact::ExactRace* race = exact_race_of(*field_)) {
                    const sim::exact::Car& me = race->cars()[0];
                    LOG_INFO("Online debug: t %.3f us (%.2f %.2f) v (%.2f %.2f); %s posed (%.2f %.2f) v (%.2f %.2f) for t %.3f; "
                             "state t %.3f (%.2f %.2f)",
                             now, me.pos.x.host(), me.pos.y.host(), me.vel.x.host(), me.vel.y.host(), net_.names[i].c_str(),
                             p.car->pos.x.host(), p.car->pos.y.host(), p.car->vel.x.host(), p.car->vel.y.host(), now + dt,
                             rc.latest().race_time, rc.latest().state.position.x, -rc.latest().state.position.z);
                }
        }
    }
}

void RaceScreen::online_after_step() {
    net::Session& ns = net::online();
    const sim::exact::ExactRace* race = field_ ? exact_race_of(*field_) : nullptr;
    const std::vector<sim::exact::PadFrame>* frames = field_ ? exact_frames_of(*field_) : nullptr;
    const std::optional<net::CarEntry> entry = field_ ? exact_player_entry(*field_) : std::nullopt;
    if (!race || !frames || frames->empty() || !entry || !ns.joined()) return;
    // This step's frame in front of the last ones (each state repeats them: no input is lost with a datagram).
    std::copy_backward(net_.inputs, net_.inputs + net::kInputHistory - 1, net_.inputs + net::kInputHistory);
    const sim::exact::PadFrame& f = (*frames)[0];
    net_.inputs[0] = {f.flags, f.kind, f.shift, f.steer, f.throttle, f.brake, f.back, f.third};
    net_.input_count = std::min(net_.input_count + 1, net::kInputHistory);
    ns.send_state(make_snapshot(race->cars()[0], *entry, ++net_.state_tick, field_->race_time(), track_->laps_completed(),
                                net_.inputs, net_.input_count));
    // Hits our machine saw: their owners apply them if their machines missed them.
    for (const RaceField::RemoteHit& h : field_->take_remote_hits())
        if (size_t(h.opponent) < net_.player.size()) {
            net::Event::Hit hit{net_.state_tick, h.point_x, h.point_z, h.jx, h.jz};
            const bool by_cpu = h.from >= 0 && size_t(h.from) < net_.player.size() && net_.local[size_t(h.from)];
            ns.send_contact(u8(net_.player[size_t(h.opponent)]), hit, by_cpu ? net_.player[size_t(h.from)] : -1);
            ++net_.contacts_sent;
            if (by_cpu) {
                LOG_INFO("Online: %s (CPU car) hit %s's (%.0f N s) at %.2f s", net_.names[size_t(h.from)].c_str(),
                         net_.names[size_t(h.opponent)].c_str(), std::hypot(h.jx, h.jz), field_->race_time());
                continue;
            }
            const sim::exact::Car& me = race->cars()[0];
            const sim::exact::Car& them = race->cars()[size_t(1 + h.opponent)];
            LOG_INFO("Online: our car hit %s's (%.0f N s) at %.2f s: ours at (%.2f %.2f) %.1f km/h, theirs posed at (%.2f %.2f) "
                     "%.1f km/h",
                     net_.names[size_t(h.opponent)].c_str(), std::hypot(h.jx, h.jz), field_->race_time(), me.pos.x.host(),
                     me.pos.y.host(), me.speed.host() * 3.6f, them.pos.x.host(), them.pos.y.host(), them.speed.host() * 3.6f);
        }
    online_cpu_cars(*race);
    ns.poll(); // out at once
}

// Host: our CPU cars' states, 30 a second each (staggered: half of them each step), and their finishes. Their states carry
// no pad frames and the AI's control source (1), so every other machine's prediction is the race's AI driving the car with
// the CPU car's driver overrides (race::exact_shadow; docs/dev/ONLINE.md "CPU cars").
void RaceScreen::online_cpu_cars(const sim::exact::ExactRace& race) {
    net::Session& ns = net::online();
    if (!ns.is_host()) return;
    std::vector<RaceField::Standing> st;
    for (size_t i = 0; i < net_.player.size(); ++i) {
        if (!net_.local[i] || 1 + i >= size_t(race.size())) continue;
        const sim::exact::Car& c = race.cars()[1 + i]; // (opponent i is car 1 + i: one player, no ghost car)
        if ((net_.state_tick + u32(i)) % 2 == 0) {
            net::CarEntry e = car_entry(c, true, true);
            e.control = 1;
            net::CarSnapshot snap = make_snapshot(c, e, ++net_.cpu_tick[i], field_->race_time(),
                                                  std::max(0, int(c.laps) - int(c.lap_base) - 1), nullptr, 0);
            snap.player = u8(net_.player[i]);
            ns.send_cpu_state(snap);
        }
        if (net_.cpu_finish_sent[i]) continue;
        if (st.empty()) st = field_->standings();
        for (const RaceField::Standing& s : st)
            if (s.car == int(i) && s.finished) {
                net_.cpu_finish_sent[i] = true;
                ns.send_cpu_finish(u8(net_.player[i]), s.time, c.best_lap == sim::exact::kNoTime ? -1.0 : c.best_lap / 1000.0);
                LOG_INFO("Online: %s (CPU car) finished in %.3f s", net_.names[i].c_str(), s.time);
            }
    }
}

// Lobby setting: a car whose round trip to us is above the threshold drives as a ghost
// (drawn translucent, no contact): late positions make contact unfair.
void RaceScreen::online_ghosts(const net::Session& ns) {
    if (!field_) return;
    u16 limit = ns.settings().ghost_ping_ms;
    // Race distances (player and opponents) for the lapped-car rule. A car still behind the start line (its lap counter 0,
    // as the cars on the back of the grid start) is that far short of it, not a lap's progress ahead: else a car on the grid
    // was "lapped" by every car that had crossed the line, for the first seconds after the start.
    const double lap = track_ ? track_->length() : 0;
    double mine = 0;
    std::vector<double> dist(size_t(field_->opponents()), 0.0);
    for (const RaceField::Standing& st : field_->standings()) {
        const bool started = st.car == -1 ? track_ && track_->lap_started() : st.car >= 0 && field_->opponent_lap_count(st.car) > 0;
        const double d = st.distance - (started ? 0.0 : lap);
        if (st.car == -1) mine = d;
        else if (st.car >= 0 && size_t(st.car) < dist.size()) dist[size_t(st.car)] = d;
    }
    net_.ghost.resize(net_.player.size(), false);
    for (size_t i = 0; i < net_.player.size() && int(i) < field_->opponents(); ++i) {
        if (net_.local[i]) continue; // (our CPU car: its finish ghosts it, in the field)
        net::Link l = ns.link(u8(net_.player[i]));
        const char* why = nullptr;
        // Late positions make contact unfair (lobby setting).
        if (limit > 0 && l.known && l.rtt_ms > double(limit)) why = "round trip above the lobby's limit";
        // The start: everyone launches at once and small timing differences would decide the
        // first corner.
        else if (field_->race_time() < kStartGhost) why = "start";
        // A car being lapped (or lapping us): the leader should not be held up by a backmarker,
        // nor the backmarker punted.
        else if (lap > 0 && std::fabs(dist[i] - mine) > lap * 0.9) why = "lapped";
        bool ghost = why != nullptr;
        if (ghost != net_.ghost[i]) {
            net_.ghost[i] = ghost;
            field_->set_remote_ghost(int(i), ghost);
            LOG_INFO("Online: %s %s%s%s (round trip %.0f ms)", net_.names[i].c_str(), ghost ? "ghosted: " : "solid again",
                     ghost ? why : "", "", l.rtt_ms);
        }
    }
}

int RaceScreen::online_finish_position() const {
    std::vector<RaceField::Standing> st = field_->standings();
    for (size_t i = 0; i < st.size(); ++i)
        if (st[i].car == -1) return int(i) + 1;
    return 1;
}

} // namespace gt3r::race_screen
