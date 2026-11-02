// Online battle glue (docs/dev/ONLINE.md): the field from the session's grid, network
// events, the synchronised start and tick pacing, remote car prediction, plausibility
// reports and ghosting.
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"

#include "base/config.h"
#include "sim/exact/race.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace gt3r::race_screen {

// Every player runs only its own car on the exact core; the others are posed from their snapshots (RemoteCar prediction)
// in the field's race (race::exact_shadow, ExactRace::pose). All players start the countdown at the same wall-clock instant
// and pace their steps from it.

namespace {

constexpr u32 kGoDelayMs = 1500;          // host: Go -> countdown start
constexpr double kClockEase = 0.05;       // racing: a start-time correction runs our race clock at most 5% fast or slow
constexpr double kContactWindow = 0.2;    // s: our own contact this recent makes theirs a duplicate
constexpr double kStartGhost = 3.0;       // s after the start signal

// [voice] section of config.ini (Settings → Online → Proximity voice).
struct VoiceSettings {
    bool open_mic = true;
    bool muted = false;
    float volume = 1.0f;
    float full_m = 15.0f;
    float max_m = 80.0f;
};

VoiceSettings load_voice_settings(const IniDocument& cfg) {
    VoiceSettings v;
    v.open_mic = to_lower(cfg.get("voice", "open_mic").value_or("on")) != "off";
    v.muted = to_lower(cfg.get("voice", "muted").value_or("off")) == "on";
    if (auto s = cfg.get("voice", "volume")) {
        try {
            v.volume = std::clamp(std::stof(*s), 0.0f, 1.0f);
        } catch (...) {
        }
    }
    if (auto s = cfg.get("voice", "full_m")) {
        try {
            v.full_m = std::clamp(std::stof(*s), 1.0f, 100.0f);
        } catch (...) {
        }
    }
    if (auto s = cfg.get("voice", "max_m")) {
        try {
            v.max_m = std::clamp(std::stof(*s), 5.0f, 300.0f);
        } catch (...) {
        }
    }
    if (v.max_m < v.full_m + 1.0f) v.max_m = v.full_m + 1.0f;
    return v;
}

void save_voice_bool(App& app, const char* key, bool on) {
    app.config().set("voice", key, on ? "on" : "off");
    app.save_config();
}

} // namespace

void RaceScreen::configure_online_field(FieldConfig& cfg) {
    net::Session& ns = net::online();
    cfg.opponents.clear();
    cfg.max_opponents = 0;
    cfg.event.clear();
    net_.player.clear(), net_.paint.clear(), net_.names.clear(), net_.top_speed.clear();
    const std::vector<u8>& grid = ns.grid();
    for (size_t slot = 0; slot < grid.size(); ++slot) {
        u8 id = grid[slot];
        if (int(id) == ns.my_id()) {
            cfg.player_slot = int(slot);
            continue;
        }
        const net::Player* p = ns.player(id);
        if (!p) continue; // left before the race
        FieldConfig::Remote rm{p->car, int(slot), {}, {}};
        float top_speed = 150.0f; // (m/s: unknown gearing, a generous bound)
        // Their car as they race it (garage parts and settings, arcade tyres: weight, size and
        // grip count in contact and prediction). The lobby has checked that it races here as
        // its owner races it; one that does not is the stock car, or a stand-in when unknown.
        if (const Bytes* info = ns.car_info(id))
            if (std::optional<game::OnlineCar> oc = game::decode_online_car(*info)) {
                std::string base = game::online_car_base(db_, *oc); // an arcade car's twin on this disc
                rm.car = base.empty() ? oc->car : base;
                std::string err;
                if (std::optional<sim::CarSpec> sp = game::online_car_spec(db_, *oc, &err, preset_.course))
                    top_speed = game::geared_top_speed(*sp);
                else LOG_WARN("Online: %s's car: %s", p->name.c_str(), err.c_str());
                if (!base.empty()) rm.source = exact_online_source(db_, *oc, preset_.course, &rm.condition);
            }
        net_.top_speed.push_back(top_speed);
        cfg.remote.push_back(rm);
        net_.player.push_back(id);
        net_.paint.push_back(p->paint);
        net_.names.push_back(p->name);
    }
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
        // Another player's machine saw their car hit ours. If ours saw the contact too (within
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
        for (int i = 0; i < int(net_.player.size()); ++i) online_retire(i);
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
            LOG_INFO("Online: %s: %u states, prediction %s, last error %.3f m", net_.names[i].c_str(),
                     ns.remote(u8(net_.player[i])) ? ns.remote(u8(net_.player[i]))->count : 0u,
                     net_.cars[i].physics() ? "exact shadow" : "dead reckoning", net_.cars[i].last_error());
    }

    // Proximity voice ([voice] settings; M mute, B mode, V force-talk).
    const VoiceSettings vset = load_voice_settings(app.config());
    if (!voice_) {
        voice_ = std::make_unique<net::VoiceChat>();
        voice_->set_open_mic(vset.open_mic);
        voice_->set_muted(vset.muted);
        voice_->set_volume(vset.volume);
        if (voice_->start()) {
            toast(vset.muted ? "Voice: muted (M to unmute)"
                             : (vset.open_mic ? "Voice: open-mic on (M mute, B mode)"
                                              : "Voice: push-to-talk (hold V)"));
        }
    }
    if (voice_ && voice_->active()) {
        // Volume can change from Settings while a race is open.
        voice_->set_volume(vset.volume);

        int nkeys = 0;
        const bool* keys = SDL_GetKeyboardState(&nkeys);
        auto key_down = [&](SDL_Scancode sc) -> bool {
            return keys && nkeys > static_cast<int>(sc) && keys[sc];
        };

        // Edge-triggered mode / mute (static latches local to this race).
        static bool m_was = false, b_was = false;
        const bool m_down = key_down(SDL_SCANCODE_M);
        const bool b_down = key_down(SDL_SCANCODE_B);
        if (m_down && !m_was) {
            voice_->set_muted(!voice_->muted());
            save_voice_bool(app, "muted", voice_->muted());
            toast(voice_->muted() ? "Voice: muted" : "Voice: unmuted");
        }
        if (b_down && !b_was) {
            voice_->set_open_mic(!voice_->open_mic());
            save_voice_bool(app, "open_mic", voice_->open_mic());
            toast(voice_->open_mic() ? "Voice: open-mic (VAD)" : "Voice: push-to-talk (hold V)");
        }
        m_was = m_down;
        b_was = b_down;

        // V: force transmit (PTT). In open-mic mode this also punches through quiet VAD frames.
        voice_->set_talking(key_down(SDL_SCANCODE_V));

        for (const net::VoiceFrame& f : voice_->take_outgoing()) {
            const u8* samples = f.data.data();
            ns.send_voice(f.seq, ByteSpan(samples, static_cast<size_t>(net::kVoiceFrameSamples)));
        }

        // Gains from our car to each remote (horizontal distance on the track).
        const float full_m = vset.full_m;
        const float max_m = vset.max_m;
        const sim::Vec3 my_pos = vehicle_ ? vehicle_->state().position : render_state_.position;
        for (size_t i = 0; i < net_.player.size(); ++i) {
            float gain = 0.0f;
            if (i < net_.cars.size() && net_.cars[i].has()) {
                const sim::Vec3& their = net_.cars[i].latest().state.position;
                const float dx = their.x - my_pos.x;
                const float dz = their.z - my_pos.z;
                const float dist = std::sqrt(dx * dx + dz * dz);
                if (dist <= full_m) gain = 1.0f;
                else if (dist >= max_m) gain = 0.0f;
                else gain = 1.0f - (dist - full_m) / (max_m - full_m);
            }
            voice_->set_gain(u8(net_.player[i]), gain);
        }
        for (const net::Session::VoiceRx& vx : ns.take_voice())
            voice_->push_remote(vx.player, vx.seq, vx.data);
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
            bool contact = field_->last_contact_with(int(i)) > now - 1.0;
            auto report = [&](std::optional<Plausibility::Flag> f) {
                if (f) ns.report_flag(u8(net_.player[i]), f->kind, f->value);
            };
            report(net_.checks[i].on_state(snap, contact));
            if (rc.physics()) report(net_.checks[i].on_prediction_error(rc.last_error(), rc.last_error_horizon(), contact));
        }
        // Its finish claim against when we saw it cross the line.
        if (i < net_.claims.size() && net_.claims[i] >= 0 && field_->remote_observed_finish(int(i)) >= 0 &&
            i < net_.checks.size()) {
            if (auto f = net_.checks[i].on_finish(net_.claims[i], field_->remote_observed_finish(int(i))))
                ns.report_flag(u8(net_.player[i]), f->kind, f->value);
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
            ns.send_contact(u8(net_.player[size_t(h.opponent)]), hit);
            ++net_.contacts_sent;
            const sim::exact::Car& me = race->cars()[0];
            const sim::exact::Car& them = race->cars()[size_t(1 + h.opponent)];
            LOG_INFO("Online: our car hit %s's (%.0f N s) at %.2f s: ours at (%.2f %.2f) %.1f km/h, theirs posed at (%.2f %.2f) "
                     "%.1f km/h",
                     net_.names[size_t(h.opponent)].c_str(), std::hypot(h.jx, h.jz), field_->race_time(), me.pos.x.host(),
                     me.pos.y.host(), me.speed.host() * 3.6f, them.pos.x.host(), them.pos.y.host(), them.speed.host() * 3.6f);
        }
    ns.poll(); // out at once
}

// Lobby setting: a car whose round trip to us is above the threshold drives as a ghost
// (drawn translucent, no contact): late positions make contact unfair.
void RaceScreen::online_ghosts(const net::Session& ns) {
    if (!field_) return;
    u16 limit = ns.settings().ghost_ping_ms;
    // Race distances (player and opponents) for the lapped-car rule.
    double mine = 0;
    std::vector<double> dist(size_t(field_->opponents()), 0.0);
    for (const RaceField::Standing& st : field_->standings()) {
        if (st.car == -1) mine = st.distance;
        else if (st.car >= 0 && size_t(st.car) < dist.size()) dist[size_t(st.car)] = st.distance;
    }
    const double lap = track_ ? track_->length() : 0;
    net_.ghost.resize(net_.player.size(), false);
    for (size_t i = 0; i < net_.player.size() && int(i) < field_->opponents(); ++i) {
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
