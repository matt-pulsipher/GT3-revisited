// The race loop: update_drive() (one frame: menus vs driving, pause, replay keys,
// the fixed-step ticks, finish -> results flow, cameras, sound placement, rumble) and
// tick() (one physics step: player / auto / scripted / demo inputs, pit lane, laps,
// machine and time-trial judging, countdown, fail-safe respawns), plus the per-tick
// helpers (autopilot, gear hint, dirt, pylons, tyre smoke, force feedback).
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"

namespace gt3r::race_screen {

sim::ControlInputs RaceScreen::scripted_inputs(double t) const {
    sim::ControlInputs in;
    in.automatic = controls_.config().automatic;
    size_t pos = 0;
    const std::string& sc = preset_.script;
    while (pos < sc.size()) {
        size_t end = sc.find(';', pos);
        if (end == std::string::npos) end = sc.size();
        std::string item = sc.substr(pos, end - pos);
        pos = end + 1;
        size_t c = item.find(':'), at = item.find('@');
        if (c == std::string::npos || at == std::string::npos || at < c) continue;
        if (std::atof(item.substr(at + 1).c_str()) > t) continue;
        std::string f = item.substr(0, c);
        float v = float(std::atof(item.substr(c + 1, at - c - 1).c_str()));
        if (f == "steer") in.steer = v;
        else if (f == "thr") in.throttle = v;
        else if (f == "brk") in.brake = v;
    }
    in.controller_kind = 2;
    return in;
}

// Change view (R1), remembered for the player's next race like GT3 does.
void RaceScreen::next_view(CameraRig& cam, int player) {
    cam.next_mode();
    if (player == 0) drive_camera_ = cam.mode();
    if (cam.mode() == CameraMode::Bumper || cam.mode() == CameraMode::Chase || cam.mode() == CameraMode::ChaseFar)
        g_last_view[player] = {cam.mode(), cam.mirror()};
}

void RaceScreen::respawn() {
    TrackPose p = track_->respawn_pose(vehicle_->state().position);
    apply_respawn(p);
    recorder_.respawn(p);
}

void RaceScreen::apply_respawn(const TrackPose& p) {
    net_.respawn_ticks = 30; // online: the jump is announced (no teleport warning)
    vehicle_->reset(p);
    prev_ = vehicle_->state();
    track_->lap_teleport(p.position);
    camera_.snap(vehicle_->state());
    offroad_time_ = 0;
}

sim::ControlInputs RaceScreen::autodrive(const sim::VehicleState& s) {
    // Test autopilot with pit=1: from 250 m before the pit entry it follows the pit lane
    // line, so it reaches the entry marker on the pit-lane surface like a player would.
    if (preset_.pit && pit_.available() && !pit_.driving() && pit_.to_entry(s.position) < 250.0f) {
        sim::ControlInputs pin = pit_.lane_inputs(s, 60.0f);
        pin.automatic = true;
        return pin;
    }
    sim::ControlInputs in;
    sim::Vec3 ahead = track_->point_ahead(s.position, 9.0f + s.speed_kmh * 0.12f);
    sim::Vec3 fwd = rotate(s.orientation, {0, 0, -1}), right = rotate(s.orientation, {1, 0, 0});
    sim::Vec3 d = ahead - s.position;
    float ang = std::atan2(d.dot(right), d.dot(fwd));
    in.steer = std::clamp(ang * 1.7f, -1.0f, 1.0f);
    // Manual gearbox driven here: the automatic would select reverse after a stop.
    in.automatic = false;
    const sim::VehicleSetup& su = vehicle_->setup();
    if (++auto_shift_ >= 15) { // at most one shift every 1/4 s
        if (s.gear < 1 || (s.engine_rpm > su.rev_limit_rpm * 0.96f && s.gear < su.forward_gears && s.speed_kmh > 15.0f))
            in.shift_up = true; // (not while held on the grid with the engine revving)
        else if (s.gear > 1 && s.engine_rpm < su.rev_limit_rpm * 0.5f) in.shift_down = true;
        if (in.shift_up || in.shift_down) auto_shift_ = 0;
    }
    // Speed limit from the curvature ahead: v = sqrt(a_lat / k) at each sample, reachable
    // from here with a_brake over the distance to it.
    constexpr float kLat = 7.0f, kBrake = 6.0f, kStep = 6.0f;
    float vmax = 70.0f; // m/s
    sim::Vec3 p0 = track_->point_ahead(s.position, 0.0f), p1 = track_->point_ahead(s.position, kStep);
    for (int i = 2; i <= 24; ++i) {
        sim::Vec3 p2 = track_->point_ahead(s.position, kStep * float(i));
        float h1 = std::atan2(p1.x - p0.x, p1.z - p0.z), h2 = std::atan2(p2.x - p1.x, p2.z - p1.z);
        float dh = std::fabs(std::remainder(h2 - h1, 6.2831853f));
        float seg = std::max(1.0f, std::hypot(p2.x - p1.x, p2.z - p1.z));
        float k = dh / seg;
        float v_corner = k > 1e-4f ? std::sqrt(kLat / k) : 70.0f;
        float dist = kStep * float(i - 1);
        vmax = std::min(vmax, std::sqrt(v_corner * v_corner + 2 * kBrake * dist));
        p0 = p1, p1 = p2;
    }
    float target = std::max(40.0f, vmax * 3.6f);
    if (s.speed_kmh < target) in.throttle = std::min(1.0f, (target - s.speed_kmh) / 10.0f + 0.2f);
    else if (s.speed_kmh > target + 3) in.brake = std::min(1.0f, (s.speed_kmh - target) / 15.0f);
    return in;
}

void RaceScreen::tick(App& app, const PadState& pad) {
    double dt = step_.step();
    animate_course(float(dt)); // the course's animations run with the race
    sim::ControlInputs in;
    int kind = 2; // pad record kind of this step (physics §4.4)
    // The display's clocks (the title, the replay mark) advance with each race step, while a
    // replay has frames (presentation §B3, §G7).
    if (!replaying_ || !race::replay_over(backdrop_ ? race::PlayMode::Backdrop : race::PlayMode::Replay, player_.done()))
        title_clock_.step(), mark_timer_.step();
    if (replaying_) {
        // A replay stops at the end of its recording; the menu backdrop runs on with a cleared
        // input frame (no steering, throttle or brake; race/post_run.h).
        const bool done = player_.done();
        if (race::replay_over(backdrop_ ? race::PlayMode::Backdrop : race::PlayMode::Replay, done)) return;
        while (const ReplayEvent* e = player_.pending_event())
            if (e->kind == ReplayEvent::Respawn) apply_respawn(e->pose);
            else if (e->kind == ReplayEvent::Respawn2 && field_ && field_->has_second()) field_->reset_second(e->pose);
        in = player_.next_inputs();
        if (done && !replay_.inputs.empty()) in.automatic = replay_.inputs.back().automatic; // (the car's gearbox)
        kind = in.controller_kind;
    } else {
        // After the finish GT3 drives the player's car (AUTO DRIVE): the end-of-run driver (ai §1.1).
        // Rolling start: the computer drives the player's car for the first 5 s.
        bool rolling_drive = rolling_runup();
        // Flying licence / time-trial start: the computer drives up to the start line.
        bool flying_drive = flying_runup();
        bool auto_drive = preset_.autodrive || finished_ || demo_ || rolling_drive || flying_drive;
        sim::ControlInputs want = auto_drive ? autodrive(vehicle_->state()) : controls_.update(pad, dt);
        if (!replaying_) vehicle_->set_reverse_request(!auto_drive && controls_.reverse());
        // Rolling / flying run-up (physics §9.4): the regular CPU AI driver on its line until
        // the hand-over, with the automatic box (physics runs the scripted aids path, kind 1).
        const sim::Vehicle* rsv = vehicle_->sim_vehicle();
        // (Also in demonstrations: their recorded frames are consumed but ignored until the line.)
        // Demonstrations: our physics is not bit-exact with the original's, so a long open-loop
        // run drifts off the original's line (the fast arcade time trials 3-22 s after the
        // line); once two wheels leave the road or the run would fail (course-out, wall), the
        // same AI driver takes over to the end (ours).
        bool demo_takeover = demo_ && disc_demo_ && (demo_off_ || demo_judge_.state() == LicenseRun::State::Failed);
        if ((rolling_drive || flying_drive || demo_takeover) && !preset_.autodrive && !finished_ && rsv && track_->runtime()) {
            if (!runup_driver_) {
                if (!runup_course_) runup_course_ = std::make_shared<ai::AiCourse>(*track_->runtime());
                // The course's set for the car's class (else the category's built-in set), no
                // event overrides (ai §9.1).
                runup_driver_ = std::make_unique<ai::AiDriver>(runup_course_, player_params(false));
                runup_driver_->reset(vehicle_->state());
                runup_driver_->set_kind(ai::kKindAi);
            }
            const sim::VehicleState& vs = vehicle_->state();
            ai::CarSnapshot me{vs.position, vs.orientation, vs.velocity};
            want = runup_driver_->update(*rsv, {race_time_, std::span<const ai::CarSnapshot>(&me, 1), 0});
            want.automatic = true;
            vehicle_->set_reverse_request(runup_driver_->reverse_request());
        } else {
            runup_driver_.reset();
        }
        // The end of the run (ai §1.1): kind 1 drives on along the main line (slower after a race,
        // time trial or machine test), kind 6 stops along it, kind 7 stops with the wheels
        // straight; parked (kind 8) once the car stands still. Not while the pit lane drives.
        const bool end_drive = finished_ && !demo_ && !preset_.autodrive && rsv && track_->runtime() && !pit_.driving();
        if (end_drive) {
            if (!end_of_run_) {
                ai::EndOfRun e = player_end_of_run();
                ai::AiDriver& d = player_auto_driver();
                d.reset(vehicle_->state());
                ai::DriverParams p = player_params(true);
                d.set_params(e.slow ? ai::after_finish(p) : p);
                if (e.main_line) d.set_line(ai::AiCourse::kMain);
                d.set_kind(e.kind);
                end_of_run_ = true;
                LOG_INFO("Race: end of run, driver kind %d%s", e.kind, e.slow ? " (slow run-on)" : "");
            }
            const sim::VehicleState& vs = vehicle_->state();
            ai::CarSnapshot me{vs.position, vs.orientation, vs.velocity};
            const int was = auto_driver_->kind();
            want = auto_driver_->update(*rsv, {race_time_, std::span<const ai::CarSnapshot>(&me, 1), 0});
            want.automatic = true;
            vehicle_->set_reverse_request(auto_driver_->reverse_request());
            if (was != ai::kKindParked && auto_driver_->kind() == ai::kKindParked)
                LOG_INFO("Race: parked (driver kind 8) at %.2f s", race_time_);
        }
        if (!preset_.script.empty() && !auto_drive) want = scripted_inputs(script_time_);
        script_time_ += dt;
        // Demonstration of a licence test / time attack: the disc's recorded run (until the
        // takeover above).
        kind = auto_drive ? 0 : controls_.kind();
        // GT3's scripted-driver kind (ai §1, the "driver kind"): 1 for the rolling / flying
        // run-up until the hand-over, the end-of-run driver's after the finish (1, 6, 7, then 8
        // parked); pit auto drive overrides it below. Demos and the attract mode stay 0.
        scripted_kind_ = end_drive && auto_driver_ ? auto_driver_->kind()
                         : (rolling_drive || flying_drive || demo_takeover) ? ai::kKindAi
                                                                             : ai::kKindNormal;
        if (demo_ && disc_demo_ && !flying_drive && !demo_takeover) {
            want = demo_inputs(demo_tick_);
            kind = disc_demo_->kind(demo_tick_);
        }
        // Tests (GT3R_RACE demo_drive=1): the disc's recorded inputs drive the player's own,
        // judged run (its tick counter runs from the start of the run).
        if (preset_.demo_drive && !demo_ && disc_demo_ && !finished_ && !flying_drive) {
            want = demo_inputs(demo_drive_tick_);
            kind = disc_demo_->kind(demo_drive_tick_);
            ++demo_drive_tick_;
        }
        if (demo_) ++demo_tick_;
        // (Without the simulation vehicle, e.g. the stand-in, a stopped run is held with the brake.)
        if (finished_ && !end_drive && !demo_ && !pit_.driving() &&
            (tt_failed_ || (!license_event_.empty() &&
                            (license_.state() == LicenseRun::State::Failed || license_.rules().kind == 1))))
            want = sim::ControlInputs{}, want.brake = 1;
        want.controller_kind = u8(kind);
        in = recorder_.record(want);
        recorder_.set_driver_kind(scripted_kind_);
    }
    // Standing start of a licence test / machine test (start type 1 / 0): the car is held by the
    // race hold (physics §9.3: in place, brakes off, the engine can rev) and the clock starts at the signal.
    bool holding = tt_hold_ > 0;
    float lamp_pedal = in.brake; // the brake lamps follow the pedal, not the start hold
    if (!vehicle_->set_race_hold(holding) && holding) in.brake = 1, in.handbrake = true;
    // Pit lane: the computer drives from the entry marker to the exit (live and in
    // replays, so the stop is re-run); the box stop is the vehicle's own 5 s hold.
    // Replays use the recorded scripted-driver kind (it changes the physics: aids, gearbox).
    if (replaying_) {
        scripted_kind_ = player_.driver_kind();
        // The end-of-run driver took over once the timing had begun (not the flying run-up's
        // driver): the run's car has ended, and its crossings post nothing more (presentation
        // §D6; races: the field stops a finished car's timer).
        if (scripted_kind_ != ai::kKindNormal && track_->lap_started()) replay_run_ended_ = true;
    }
    if (!(pit_.available() && field_)) vehicle_->set_driver_kind(scripted_kind_);
    if (pit_.available() && field_) {
        PitLane::In pi;
        pi.state = vehicle_->state();
        const sim::Vehicle* sv = vehicle_->sim_vehicle();
        if (sv) {
            const sim::VehicleTelemetry& t = sv->telemetry();
            pi.on_pit_surface = t.on_pit_surface;
            for (int i = 0; i < 4; ++i) pi.want_service = pi.want_service || t.tire_wear[size_t(i)] > sv->tire_wear_params().fresh;
            pi.want_service = pi.want_service && sv->tire_wear_params().on();
            pi.stopping = sv->spawn_hold_active();
        }
        pi.race_over = finished_;
        if (preset_.pit && !replaying_) pi.want_service = sv && sv->tire_wear_params().on();
        PitLane::Out po = pit_.update(pi);
        if (po.entered) {
            post_info(ascii(car_.name) + " Pitting in...");
            LOG_INFO("Race: %s pitting in (t %.1f)", car_.name.c_str(), race_time_);
            if (!replaying_ && sv) player_auto_driver().reset(vehicle_->state());
        }
        if (po.begin_stop) vehicle_->begin_pit_stop(po.box), LOG_INFO("Race: pit stop (t %.1f)", race_time_);
        // The AI driver drives the pit lane (ai §13): the state's line and driver kind (2 lane,
        // 3 stop line, 4 leaving), back to the main line at the exit.
        if (!replaying_ && sv && (po.line || po.auto_drive)) {
            ai::AiDriver& d = player_auto_driver();
            if (po.line) d.set_line(po.line, po.blend);
            if (po.exited) d.reset_recovery();
            d.set_kind(po.kind);
        }
        vehicle_->set_driver_kind(po.auto_drive ? po.kind : scripted_kind_);
        if (po.auto_drive && !replaying_) {
            // The recorded input is what the simulation sees (replays reproduce the stop).
            sim::ControlInputs pin;
            if (po.hold || !sv) {
                pin.brake = 1; // the 5 s stop
            } else {
                const sim::VehicleState& vs = vehicle_->state();
                ai::CarSnapshot me{vs.position, vs.orientation, vs.velocity};
                pin = auto_driver_->update(*sv, {race_time_, std::span<const ai::CarSnapshot>(&me, 1), 0});
                vehicle_->set_reverse_request(auto_driver_->reverse_request());
            }
            pin.automatic = true;
            pin.controller_kind = 0;
            in = recorder_.replace_last(pin);
        }
    }
    prev_ = vehicle_->state();
    last_in_ = in;
    bool p2 = two_player_ && field_ && field_->has_second();
    if (p2) {
        if (replaying_) {
            last_in2_ = player_.second_inputs();
        } else {
            bool ai2 = preset_.autodrive || finished2_;
            sim::ControlInputs w2 = ai2 ? autodrive(field_->second_state()) : controls2_.update(app.input().race_pad(1), dt);
            field_->set_second_reverse(!ai2 && controls2_.reverse());
            w2.controller_kind = u8(ai2 ? 0 : controls2_.kind());
            last_in2_ = recorder_.record_second(w2);
        }
        field_->set_second_inputs(last_in2_);
    }
    (void)kind;
    // Brake lamps (GT3: a 4-frame history of "brake pedal > 0" per car; lamp factor k / 4).
    brake_lamps_.push(0, lamp_pedal);
    if (p2) brake_lamps_.push(1, last_in2_.brake);
    if (field_)
        for (int i = 0; i < field_->opponents(); ++i) brake_lamps_.push(2 + size_t(i), field_->opponent_brake(i));
    if (online_) online_before_step(dt);
    vehicle_->step(in, dt);
    if (online_) online_after_step();
    if (field_)
        for (int i : field_->take_pit_entries()) {
            post_info(car_display_name(field_->opponent_label(i)) + " Pitting in...");
            LOG_INFO("Race: %s pitting in (t %.1f)", field_->opponent_label(i).c_str(), race_time_);
        }
    // The turbo sounds read the car's processed throttle (0 through an upshift), sound §7.5.
    if (const sim::Vehicle* sv = vehicle_->sim_vehicle()) extra_.step(CarExtraSounds::step_inputs(*sv));
    if (p2)
        if (const sim::Vehicle* sv2 = field_->second_vehicle()) extra2_.step(CarExtraSounds::step_inputs(*sv2));
    if (p2) {
        const sim::VehicleState& s2 = field_->second_state();
        RumbleModel::Events ev2;
        for (const sim::WheelState& w : s2.wheels)
            if (w.grounded && track_->is_kerb(w.surface)) ev2.kerb = true;
        ev2.offroad = track_->is_offroad(track_->query().probe(s2.position.x, s2.position.z, s2.position.y + 2.0f));
        ev2.impact = field_->second_impact();
        if (const sim::Vehicle* sv2 = field_->second_vehicle())
            for (size_t i = 0; i < s2.wheels.size(); ++i)
                if (s2.wheels[i].grounded) ev2.tire_slip = std::max(ev2.tire_slip, sv2->telemetry().smoke[i]);
        rumble2_.update(s2, ev2, last_in2_, dt);
        if (!finished2_ && field_->second_finish() >= 0) { // the field's finish rule (race::finishes_at_line)
            finished2_ = true;
            finish_position2_ = field_->second_position();
            if (!finished_) p2_won_ = true;
        }
    }
    if (debug_ && (++debug_ticks_ % 30) == 0) {
        const sim::VehicleState& v = vehicle_->state();
        LOG_INFO("t=%.1f pos(%.1f %.1f %.1f) %.1f km/h gear %d rpm %.0f in(st %.2f th %.2f br %.2f) lap %.3f reverb %.3f "
                 "turbo %d amb(air %.2f city %.2f crowd %.2f sea %.2f) pit %d",
                 race_time_, v.position.x, v.position.y, v.position.z, v.speed_kmh, v.gear, v.engine_rpm, in.steer,
                 in.throttle, in.brake, track_->lap_progress(), ambience_.depth(), extra_.turbo().level(),
                 crss_.channel(0).l + crss_.channel(0).r, crss_.channel(1).l + crss_.channel(1).r,
                 crss_.channel(3).l + crss_.channel(3).r, crss_.channel(4).l + crss_.channel(4).r, int(pit_.state()));
    }
    const sim::VehicleState& s = vehicle_->state();
    // Machine tests: the track distance and the clock before this tick (presentation §B7).
    const double dist_before = track_->race_distance(), clock_before = race_time_;
    if (holding) {
        tt_hold_ -= dt;
        if (tt_hold_ <= 1e-9) { // the signal: the lap clock starts here
            tt_hold_ = 0;
            race_time_ = 0;
            track_->lap_reset(0);
            track_->lap_update(s.position, 0);
        }
    } else {
        race_time_ += dt;
    }
    // The pre-race shot's cuts, and its cut to the player's view, dissolve over 1 s
    // (presentation §B3).
    if (live_view() && race::start_shot_dissolve(frames_to_signal())) start_dissolve(1.0f);
    // Races: the field's race clock (0 = GO, the same for every car).
    bool lap_done = !holding && track_->lap_update(s.position, field_ ? std::max(field_->race_time(), 0.0) : race_time_);
    const bool timer_updated = !holding;
    u32 tick_index = replaying_ ? player_.tick() : recorder_.tick(); // ticks simulated so far

    // Lap bookkeeping for the ghost: frame k = state k ticks after the lap started.
    bool started = track_->lap_started();
    if (started && !was_started_) {
        cur_lap_.clear();
        logger_.begin_lap();
        lap_begin_tick_ = tick_index;
        lap_ticks_ = 0;
    }
    was_started_ = started;
    if (started) {
        cur_lap_.add(s);
        ++lap_ticks_;
        if (!replaying_ && !demo_)
            logger_.sample(track_->lap_progress() * track_->length(), s.speed_kmh, in.throttle, in.brake, s.position.x,
                           s.position.z);
    }
    odometer_m_ += double(s.speed_kmh) / 3.6 * dt;
    update_gear_hint(s);
    update_dirt(dt);
    update_smoke(dt);
    update_pylons(dt);
    if (lap_done && tt_failed_) lap_done = false; // a failed time trial records no lap
    if (lap_done) {
        double t = track_->last_lap_time();
        lap_times_.push_back(t);
        LOG_INFO("Race: lap %zu %s %.3f s", lap_times_.size(), car_.label.c_str(), t);
        if (!replaying_) recorder_.lap(t, lap_begin_tick_, tick_index);
        if (!replaying_ && !demo_) logger_.finish_lap(t);
        if (!replaying_ && !demo_ && !race_mode_ && (ghost_time_ < 0 || t < ghost_time_)) {
            ghost_ = cur_lap_;
            ghost_time_ = t;
            LOG_INFO("Race: new best lap %.3f s, ghost updated (%zu frames)", t, ghost_.size());
        }
        cur_lap_.clear();
        cur_lap_.add(s);
        lap_begin_tick_ = tick_index;
        lap_ticks_ = 1;
    }

    // The run's top speed, until its finish (the machine tests' record).
    if (!replaying_ && !demo_ && !holding && !finished_) top_speed_kmh_ = std::max(top_speed_kmh_, s.speed_kmh);
    // The HUD's own highest speed: every tick, past the finish too (presentation §D4); in
    // replays too (the top-speed test's pop-up at the goal, §D6).
    if (!demo_) hud_top_kmh_ = std::max(hud_top_kmh_, s.speed_kmh);
    // Machine tests (presentation §B7): the split points and the goal, track distances from the
    // control line, timed within the tick from GO; line crossings count for nothing. The goal
    // ends the run: the time tests rank their time to it, the top-speed test its top speed.
    machine_marks_.clear();
    if (machine_ && !holding && !finished_ && !replaying_ && !demo_) {
        machine_marks_ =
            machine_->update(dist_before, track_->race_distance(), track_->length(), clock_before, race_time_);
        for (const MachineTest::Crossing& c : machine_marks_) {
            if (!c.goal) continue;
            machine_time_ = double(c.ms) / 1000.0;
            finished_ = true;
            finish_position_ = 1;
            track_->lap_freeze(); // nothing is timed after the finish
            LOG_INFO("Machine test: %.0f m in %.3f s (top %.1f km/h)", double(machine_->goal_m()), machine_time_,
                     top_speed_kmh_);
            if (machine_->timed()) rank_finish(machine_time_, false);
            else rank_finish(top_speed_kmh_, true);
        }
    } else if (machine_ && !holding && replaying_ && !machine_->done()) {
        // Replays: the timing points again, for the pop-ups only (the result is the live run's).
        machine_marks_ =
            machine_->update(dist_before, track_->race_distance(), track_->length(), clock_before, race_time_);
    }
    // Time trial course-out (physics §11): the run fails, no lap is recorded, the car stops.
    bool time_trial = !free_run_ && (!from_menu_ || launch_.request.mode == "time_trial"); // not free run
    if (time_trial && !field_ && license_event_.empty() && !machine_ && !finished_ && !replaying_ && !demo_ &&
        !preset_.autodrive && track_->lap_started()) {
        LicenseRun::Step st;
        float mass = vehicle_->sim_vehicle() ? vehicle_->sim_vehicle()->spec().mass : 1200.0f;
        st.wall_dv = vehicle_->last_impact(); // speed change this step (no other cars here)
        (void)mass;
        for (int i = 0; i < 4; ++i) st.grounded[i] = s.wheels[size_t(i)].grounded, st.surface[i] = s.wheels[size_t(i)].surface;
        const char* why = course_out(st);
        if (!why && pylon_hit_) why = "pylon"; // time trial stops on a rigid-body hit too (failure bit 0)
        if (why) {
            tt_failed_ = true;
            finished_ = true;
            finish_position_ = 0;
            LOG_INFO("Time trial: FAIL (%s) at %.2f s, %.0f km/h, wall dv %.1f m/s", why, race_time_, s.speed_kmh, st.wall_dv);
        }
    }
    // Licence tests: judged every tick (stop box, section goal, laps, failures).
    if (!license_event_.empty() && !finished_ && !replaying_ && !demo_) update_license(s, dt);
    if (demo_ && disc_demo_ && demo_judge_.state() == LicenseRun::State::Running) { // the demonstration's own judge
        std::swap(license_, demo_judge_);
        bool fin = finished_;
        update_license(s, dt);
        finished_ = fin;
        std::swap(license_, demo_judge_);
        // (A course-out in the computer-driven run-up of a flying start does not count: the
        // run is the recording's from the line on.)
        if (demo_judge_.state() == LicenseRun::State::Failed && flying_runup()) demo_judge_.begin(license_.rules());
        // (Ours: the takeover comes as soon as two wheels are off the road, before the run
        // would fail, so the demonstration hardly leaves the track.)
        if (!demo_off_ && !flying_runup()) {
            int off = 0;
            for (const sim::WheelState& w : s.wheels) {
                u8 m = track::surface_material(w.surface);
                off += w.grounded && (m == 2 || m == 3 || m == 4 || m == 6);
            }
            if (off >= 2) demo_off_ = true, LOG_INFO("Demo: AI takeover at step %zu (wheels off the road)", demo_tick_);
        }
        if (demo_judge_.state() != LicenseRun::State::Running)
            LOG_INFO("Licence demo: %s %.3f s (recorded %.3f s; step %zu, %s)",
                     demo_judge_.state() == LicenseRun::State::Finished ? "finished" : "FAILED", demo_judge_.time(),
                     disc_demo_->time_ms / 1000.0, demo_tick_, demo_judge_.failure().c_str());
    }
    // Time trials end at the line once their laps are done and rank their best lap; nothing is
    // timed after it. The arcade time trial ends at its first completed lap (presentation §B7);
    // debug runs after their `laps=N`.
    if (license_event_.empty() && !machine_ && lap_done && !field_ && !finished_ && !replaying_ && !demo_ &&
        solo_run_finished(track_->laps_completed(), race_laps_)) {
        finished_ = true;
        finish_position_ = 1;
        track_->lap_freeze();
        rank_finish(track_->best_lap_time(), false);
    }
    // Races: the field's finish rule (race::finishes_at_line: the race's laps, the leader has
    // finished, or the first crossing after a time limit); the place is the race order's.
    if (field_ && !finished_ && field_->player_finish() >= 0) {
        finished_ = true;
        track_->lap_freeze(); // no more laps after the finish (the HUD's lap time runs on)
        finish_position_ = field_->player_position();
        if (online_) finish_position_ = online_finish_position();
        if (two_player_ && !finished2_) p1_won_ = true;
        LOG_INFO("Race: finished %d of %d after %d laps (%.3f s)", finish_position_, field_->opponents() + 1,
                 race_laps_, race_time_);
    }

    // The pop-ups in the middle of the screen: lap times, splits, gaps (presentation §D6).
    lap_events(timer_updated);

    if ((finished_ || finished2_) && !replaying_) finish_wait_ += dt; // seconds since the finish (results timing)
    // The medal's animation runs from the frame its model starts to fade in.
    if (prize_ && finished_ && result_frame() >= LicenseResultOverlay::model_start(shown_medal_)) prize_->tick(dt);

    RumbleModel::Events ev;
    for (const sim::WheelState& w : s.wheels)
        if (w.grounded && track_->is_kerb(w.surface)) ev.kerb = true;
    sim::SurfaceHit h = track_->query().probe(s.position.x, s.position.z, s.position.y + 2.0f);
    ev.offroad = track_->is_offroad(h);
    ev.impact = vehicle_->last_impact();
    if (const sim::Vehicle* sv = vehicle_->sim_vehicle())
        for (size_t i = 0; i < s.wheels.size(); ++i)
            if (s.wheels[i].grounded) ev.tire_slip = std::max(ev.tire_slip, sv->telemetry().smoke[i]);
    rumble_.update(s, ev, in, dt);
    {
        if (se_on()) update_tyre_sounds(app, s);
        // Force feedback is sent once per frame: keep the strongest hits of the frame's steps.
        ffb_wall_ = std::max(ffb_wall_, std::fabs(vehicle_->wall_impulse()));
        ffb_car_ = std::max(ffb_car_, std::fabs(vehicle_->car_impulse()));
        if (field_ && field_->has_second()) {
            ffb_wall2_ = std::max(ffb_wall2_, std::fabs(field_->second_wall_impulse()));
            ffb_car2_ = std::max(ffb_car2_, std::fabs(field_->second_car_impulse()));
        }
    }
    // Start countdown beeps: count_d at 3, 2, 1, count_s at the start signal.
    if ((field_ || tt_start_.countdown > 0) && !replaying_) {
        double t = field_ ? field_->race_time() : (tt_hold_ > 0 ? -tt_hold_ : race_time_ + 1.0);
        int c = t < 0 ? int(std::ceil(-t)) : 0;
        if (rolling_) c = t < kRollingAutoDrive ? 4 : 0; // START (count_s) at the hand-over only
        if (c != last_count_ && c <= 3) {
            if (c > 0) sfx_.play(app.audio().voices(), "count_d");
            else if (last_count_ > 0) sfx_.play(app.audio().voices(), "count_s");
        }
        last_count_ = c;
    }
    // A flying start's signal: at the line crossing that ends the run-up (live runs only, as
    // the countdown beeps).
    const bool runup = flying_runup();
    if (flying_runup_ && !runup && !replaying_ && !demo_ && !attract_) flying_start_signal(app);
    flying_runup_ = runup;
    ambience_.tick(app.audio().voices(), h.hit ? (h.surface >> 8) : 0);

    if (replaying_) return; // respawns come from the replay's events
    // The original never respawns a car. Non-original fail-safes: falling out of the
    // world (always), and with [race] respawn = all a player car stuck off the road; the
    // test autopilot (auto=1) gives up on a stuck car.
    bool lost = !std::isfinite(s.position.x) || !std::isfinite(s.position.y) || s.position.y < lowest_ - 50;
    bool player_failsafe = (respawn_all_ && !online_) || preset_.autodrive; // never online
    offroad_time_ = h.hit || !player_failsafe ? 0.0 : offroad_time_ + dt;
    // Auto-drive gives up on a stuck car (crashed into a wall) and respawns.
    double progress = track_->laps_completed() + double(track_->lap_progress());
    if (progress > best_progress_ + 1e-4) best_progress_ = progress, stuck_time_ = 0;
    else if (preset_.autodrive && !(field_ && field_->race_time() < 0)) stuck_time_ += dt; // not while held on the grid
    if (pit_.driving()) stuck_time_ = offroad_time_ = 0; // the pit lane drive and the stop
    if (lost || offroad_time_ > 8.0 || stuck_time_ > 4.0) {
        respawn();
        stuck_time_ = 0;
        best_progress_ = -1;
    }
}

// A flying start's signal (presentation §B2): the start-line crossing that ends the computer's
// run-up hands the car over and starts the run as GO starts a standing one: count_s, the START
// image for 1 s (hud_data) and the race music (music_cues: the run-up holds it). In time
// trials, flying licence tests and free runs alike (not the free run before a GT race, which
// is not flying). A free run adds its two notes in the event display: "Timer starts at
// start/finish line" 2 s after the signal, then "Press START button to end session", 3 s each.
void RaceScreen::flying_start_signal(App& app) {
    flying_go_t_ = race_time_;
    sfx_.play(app.audio().voices(), "count_s");
    LOG_INFO("Race: flying start signal at %.3f s (count_s)", race_time_);
    if (run_kind() != RunKind::FreeRun) return;
    EventDisplay& ev = event_display_[0];
    EventDisplay::Message ready, help;
    ready.text = race_text("RACEDISPLAY_FreeRunReady", "Timer starts at start/finish line");
    help.text = race_text("RACEDISPLAY_FreeRunHelp", "Press START button to end session");
    ev.after(std::move(ready), 2.0f);
    ev.queue(std::move(help));
}

void RaceScreen::update_drive(App& app, double dt) {
    const MenuInput& m = app.menu();
    if (online_ && !online_update(app)) return;
    if (logger_mode_ && update_logger(app, dt)) return;
    if (replay_pending_) { // back from the run's presentations: its replay (screen_race_post_run.cpp)
        start_auto_replay(app);
        return;
    }
    // Race -> menu (presentation §C1, §C4): the last frame of the live run or of a replay
    // stays up (nothing is simulated) for 10 frames, then a grey wash (0.1) at alpha 0.5 per
    // frame, then what comes next cuts in.
    if (exit_transition_ >= 0) {
        if (++exit_transition_ > kExitHold + kExitWash) {
            exit_transition_ = -1;
            if (exit_then_ == ExitThen::Menu) return_to_menu(app);
            else after_run(app);
        }
        return;
    }
    if (top3_pending_) { // back from the run's name entry: the Top 3 with the name entered
        top3_pending_ = false;
        if (quick_) fill_top3();
    }
    if (attract_) { // any button returns to the title; the demo ends a few seconds after the finish
        bool any = false;
        for (PadButton b : {PadButton::Cross, PadButton::Circle, PadButton::Triangle, PadButton::Square,
                            PadButton::Start, PadButton::Select})
            any = any || m.pressed(b);
        // The demo ends by itself after 1800 steps (the original's timer) or at the finish.
        if (any || (finished_ && finish_wait_ > 5.0) || race_time_ > kAttractSeconds) {
            attract_interrupted_ = any;
            leave(app);
            return;
        }
    }
    if (setting_pending_) { // back from GT mode's settings: the ride car as now set up
        setting_pending_ = false;
        if (std::shared_ptr<const sim::CarSpec> sp = gt_ride_car_spec()) {
            launch_spec_ = sp;
            car_.spec = sp;
            if (build_vehicle(app)) restart();
            toast("Settings applied");
        }
    }
    toast_time_ = std::max(0.0, toast_time_ - dt);
    lamps_.tick(dt);
    if (app.vfs()) music_cues(app);
    if (quick_) {
        if (preview_) { // the demonstration runs behind the menu, looping
            int n = step_.advance(dt);
            for (int i = 0; i < n; ++i) tick(app, PadState{});
            render_state_ = interpolate(prev_, vehicle_->state(), step_.alpha());
            camera_.set_track_distance(track_->distance_of(render_state_.position));
            camera_.set_lap(lap_count());
            director_.set_view_mode(view_settings().mode);
            update_camera_manager(dt);
            if (car_visual_) camera_.set_replay_mounts(&car_visual_->mounts());
            camera_.update(render_state_, vehicle_->setup(), PadState{}, dt);
            if (run_preview_) {
                if (player_.done() && pre_demo_run_) { // loop the run
                    start_playback(*pre_demo_run_);
                    quick_ = finish_menu_shown_ = true;
                }
            } else if (demo_tick_ > disc_demo_->size() + 180) {
                restart();
                quick_ = true;
                demo_tick_ = 0;
                demo_judge_.begin(license_.rules());
                demo_off_ = false;
            }
        } else if (backdrop_ && panel_ == Panel::None) {
            update_backdrop(app, dt); // the race behind the menu (play mode 2)
        }
        update_quick_menu(app, dt);
        return;
    }
    if (theater_ && demo_ && !paused_ && disc_demo_ && demo_tick_ > disc_demo_->size() + 180) {
        leave(app); // the theater's demonstration ran out: back to the theater
        return;
    }
    if (demo_ && !theater_ &&
        (m.pressed(PadButton::Start) || m.pressed(PadButton::Cross) || m.pressed(PadButton::Circle))) {
        end_demo(app);
        return;
    }
    // The race pad (the player's race layout, buttons in GT3's meaning): its presses this frame.
    const PadState pad = app.input().race_pad(0);
    const u16 race_pressed = u16(pad.buttons & ~race_prev_);
    race_prev_ = pad.buttons;
    auto rpressed = [&](PadButton b) { return (race_pressed & (1u << unsigned(b))) != 0; };
    // (Replays too: only Start opens their pause, presentation §G; nothing else skips one.)
    if (!paused_ && (m.pressed(PadButton::Start) || rpressed(PadButton::Start))) {
        open_pause(app);
        return;
    } else if (paused_ && !replay_opts_open_ && (m.pressed(PadButton::Start) || rpressed(PadButton::Start))) {
        paused_ = false; // (ours: Start also continues)
        return;
    }
    if (paused_) {
        if (online_) { // the others race on: so does our car (no input) behind the menu
            int n = online_ticks(step_.advance(dt));
            for (int i = 0; i < n; ++i) tick(app, PadState{});
            render_state_ = interpolate(prev_, vehicle_->state(), step_.alpha());
            // ... and the camera keeps following it (offline the frozen view is the pause).
            camera_.set_look_back(false);
            camera_.set_track_distance(track_->distance_of(render_state_.position));
            camera_.set_lap(lap_count());
            camera_.update(render_state_, vehicle_->setup(), PadState{}, dt);
        }
        update_pause(app, dt);
        return;
    }
    // GT3's buttons: R1 change view, L1 look back (held), Select respawn (ours), R3 our
    // performance overlay; see race/controls.h. Neither is on a controller by default: the
    // presets bind respawn to the keyboard (and the touch overlay) only and leave the
    // overlay (a developer tool) unbound, and under the "gt3" layout the race pad is the
    // menu pad, whose Select / R3 do nothing in a race (bind them in a custom race layout;
    // GT3R_RACE=perf=1 starts with the overlay on).
    bool own_race_pad = !app.input().race_uses_menu_pad(0);
    if (replaying_ || (theater_ && demo_)) {
        // Replays: the original's replay pad (screen_race_replay.cpp, presentation §G3).
        replay_pad(m);
        camera_.set_look_back(false);
    } else {
        if (rpressed(PadButton::R1)) next_view(camera_, 0);
        camera_.set_look_back(pad.down(PadButton::L1));
    }
    // Never online: a respawn there would skip track (respawn is offline only).
    if (!replaying_ && !online_ && own_race_pad && rpressed(PadButton::Select)) respawn();
    if (own_race_pad && rpressed(PadButton::R3)) show_perf_ = !show_perf_;

    Uint64 now = SDL_GetPerformanceCounter();
    if (last_counter_) frame_ms_ = double(now - last_counter_) * 1000.0 / double(SDL_GetPerformanceFrequency());
    last_counter_ = now;

    int n = step_.advance(dt);
    if (online_) n = online_ticks(n);
    Uint64 h_t0 = SDL_GetTicksNS();
    for (int i = 0; i < n; ++i) tick(app, pad);
    hitch::ticks = n, hitch::ticks_ms = double(SDL_GetTicksNS() - h_t0) / 1e6;
    hitch::smoke = int(smoke_.live()), hitch::x = render_state_.position.x, hitch::z = render_state_.position.z;
    hitch::race_t = float(race_time_);
    // After the finish: results overlay, then the quick menu (GT3's flow).
    if ((finished_ || finished2_) && !replaying_ && !finish_menu_shown_) {
        bool all_done = true;
        if (field_)
            for (const RaceField::Standing& st : field_->standings()) all_done = all_done && st.finished;
        if (all_done && all_done_at_ < 0) all_done_at_ = finish_wait_;
        double shown = field_ ? ResultsOverlay::fully_shown_time(size_t(field_->opponents() + 1 + (field_->has_second() ? 1 : 0)))
                              : TimeTrialOverlay::kShowDelay;
        // The result screens' confirm: Circle / Cross (presentation §B6, §B7).
        const bool confirm = app.menu().pressed(PadButton::Cross) || app.menu().pressed(PadButton::Circle);
        // Races: once the panel is fully shown; time trials, machine tests and free runs: the
        // record overlay's 5 s (0.1 s after the top-speed test's EXIT), then 10 s more.
        bool leave = field_ ? finish_wait_ > shown && (confirm || (all_done && finish_wait_ - all_done_at_ > 15.0))
                            : TimeTrialOverlay::ends_race(finish_wait_ + TimeTrialOverlay::kShowDelay - overlay_delay_, confirm);
        // Licence results and the time-trial FAIL: GT3's presenter timeline (frames from
        // the result); confirm only in its last hold. Leaving takes 2 more frames (event 1).
        if (!field_ && (!license_event_.empty() || tt_failed_)) {
            bool tt = license_event_.empty();
            int medal = tt ? -1 : presented_medal();
            int f = result_frame(), skip = LicenseResultOverlay::skippable_from(medal, tt);
            leave = f >= LicenseResultOverlay::end_frame(medal, tt) + 2 || (skip >= 0 && f >= skip && confirm);
        }
        if (two_player_ && finish_wait_ > shown + 15.0) leave = true;
        // The race loop ends: GT3's loader transition, then what follows the run
        // (screen_race_post_run.cpp).
        if (leave) begin_exit_transition(ExitThen::RunEnd);
    }
    if (replaying_ && replay_to_menu_ && !paused_ && race::replay_over(race::PlayMode::Replay, player_.done())) {
        begin_exit_transition(ExitThen::Menu); // over the replay's last frame, then the quick menu
    } else if (replaying_ && player_.done() && !paused_ && theater_) {
        leave(app); // the theater's replay ended: back to the theater
        return;
    } else if (replaying_ && player_.done() && !paused_) {
        open_pause(app); // end of the replay: show the menu
        toast("End of replay");
        const sim::Vec3& a = vehicle_->state().position;
        bool exact = a.x == replay_.final_position.x && a.y == replay_.final_position.y && a.z == replay_.final_position.z;
        LOG_INFO("Replay: end, final position %s the recording", exact ? "matches" : "DIFFERS FROM");
    }
    render_state_ = interpolate(prev_, vehicle_->state(), step_.alpha());
    if (two_player_ && field_ && field_->has_second()) {
        PadState pad2 = app.input().race_pad(1);
        auto pressed2 = [&](PadButton b) { return pad2.down(b) && !(pad2_prev_ & (1u << unsigned(b))); };
        if (pressed2(PadButton::R1)) next_view(camera2_, 1);
        camera2_.set_look_back(pad2.down(PadButton::L1));
        if (!replaying_ && !online_ && !app.input().race_uses_menu_pad(1) && pressed2(PadButton::Select)) {
            TrackPose p = track_->respawn_pose(field_->second_state().position);
            field_->reset_second(p);
            recorder_.respawn_second(p);
            camera2_.snap(field_->second_state());
        }
        pad2_prev_ = pad2.buttons;
        render_state2_ = interpolate(field_->second_prev_state(), field_->second_state(), step_.alpha());
        camera2_.set_intro(live_view() && race::start_shot_on(frames_to_signal()), race::start_shot_t(frames_to_signal()));
        camera2_.set_track_distance(track_->distance_of(render_state2_.position));
        camera2_.set_lap(field_->second_laps().laps);
        camera2_.update(render_state2_, field_->second_setup(), pad2, dt);
        app.input().set_rumble(1, rumble2_on_ && !replaying_ ? rumble2_.rumble() : PadRumble{});
        if (rumble2_on_ && !replaying_) app.input().set_trigger_rumble(1, rumble2_.trigger_left(), rumble2_.trigger_right());
        else app.input().set_trigger_rumble(1, 0, 0);
        if (se_on() && engine2_.loaded()) {
            sim::Vec3 off2 = render_state2_.position - to_sim(camera2_.eye());
            if (camera2_.onboard()) engine2_.set_position(0.0f, 1.0f);
            else engine2_.set_position(off2.dot(cam_right(camera2_)), off2.dot(cam_forward(camera2_)));
            engine2_.set_view(camera2_.onboard() ? EngineSound::View::OnboardOwn : EngineSound::View::ChaseOwn);
            engine2_.update(app.audio().voices(), render_state2_.engine_rpm, render_state2_.throttle, dt);
        }
        if (const sim::Vehicle* sv2 = field_->second_vehicle()) {
            sim::Vec3 o2 = render_state2_.position - to_sim(camera2_.eye());
            if (se_on()) extra2_.update(sfx_, app.audio().voices(),
                           extra_frame(*sv2, render_state2_, sound_car2_, camera2_.onboard(), o2.dot(cam_right(camera2_)),
                                       o2.dot(cam_forward(camera2_)),
                                       camera2_.onboard() ? SoundView::InCarTarget : SoundView::ChaseTarget));
        }
    }
    camera_.set_track_distance(track_->distance_of(render_state_.position));
    camera_.set_lap(lap_count());
    // GT3's pre-race shot (presentation §B3): every live view with a standing-start hold plays
    // start.cam until one second before the signal (C >= 60 frames, t = 1 - (C - 60) / 600).
    camera_.set_intro(live_view() && race::start_shot_on(frames_to_signal()), race::start_shot_t(frames_to_signal()));
    // The replay director's view mode (presentation §G2): REPLAY OPTIONS', or the attract
    // demo's pick; mode 1 cuts on its own timer.
    director_.set_view_mode(view_settings().mode);
    if (replaying_ || attract_) update_replay_focus(dt); // the followed car (TARGET), the pad's views and cuts
    update_camera_manager(dt);                           // the music's cut requests, view mode 1's cuts
    if ((replaying_ || attract_) && field_) {
        aim_replay_camera();
        if (replay_focus_ >= 0 && replay_focus_ < field_->opponents()) {
            sim::VehicleState fs = interpolate(field_->opponent_prev_state(replay_focus_), field_->opponent_state(replay_focus_),
                                               step_.alpha());
            camera_.set_track_distance(track_->distance_of(fs.position));
            camera_.set_lap(field_->opponent_lap_count(replay_focus_));
            const CarVisual* v = size_t(replay_focus_) < opp_visuals_.size() ? opp_visuals_[size_t(replay_focus_)].get() : nullptr;
            if (v) camera_.set_replay_mounts(&v->mounts());
            camera_.update(fs, field_->opponent_setup(replay_focus_), pad, dt);
        } else {
            if (car_visual_) camera_.set_replay_mounts(&car_visual_->mounts());
            camera_.update(render_state_, vehicle_->setup(), pad, dt);
        }
    } else {
        if (car_visual_) camera_.set_replay_mounts(&car_visual_->mounts());
        camera_.update(render_state_, vehicle_->setup(), pad, dt);
    }
    update_shot_filter();
    if (const float d = director_.take_dissolve(); d > 0) start_dissolve(d); // a course record's own

    bool feel = rumble_on_ && !replaying_ && !demo_;
    app.input().set_rumble(0, feel ? rumble_.rumble() : PadRumble{});
    if (!feel) app.input().set_trigger_rumble(0, 0, 0);
    else app.input().set_trigger_rumble(0, rumble_.trigger_left(), rumble_.trigger_right());
    if (field_ && field_->opponents() > 0) {
        std::vector<const sim::VehicleState*> st;
        for (int i = 0; i < field_->opponents(); ++i) st.push_back(&field_->opponent_state(i));
        render::Vec3 e = camera_.eye();
        opp_engines_.view = camera_.onboard() ? EngineSound::View::OnboardOther
                            : replaying_ || camera_.mode() == CameraMode::Tv ? EngineSound::View::Replay
                                                                             : EngineSound::View::ChaseOther;
        std::vector<float> slip;
        for (int i = 0; i < field_->opponents(); ++i) {
            float y = 0;
            [&](const auto& t) {
                if constexpr (requires { t.slipstream; }) y = t.slipstream;
            }(field_->opponent_vehicle(i)->telemetry());
            slip.push_back(y);
        }
        if (se_on()) opp_engines_.update(app.audio().voices(), {e.x, e.y, e.z}, render_state_.velocity, cam_right(camera_),
                            cam_forward(camera_), st, dt, &slip);
    }
    if (se_on() && engine_.loaded()) {
        // Stereo placement: centred on board, else the car's offset from the camera.
        sim::Vec3 off = render_state_.position - to_sim(camera_.eye());
        if (camera_.onboard()) engine_.set_position(0.0f, 1.0f);
        else engine_.set_position(off.dot(cam_right(camera_)), off.dot(cam_forward(camera_)));
        engine_.set_view(replaying_ ? EngineSound::View::Replay
                         : camera_.onboard() ? EngineSound::View::OnboardOwn
                                             : EngineSound::View::ChaseOwn);
        engine_.update(app.audio().voices(), render_state_.engine_rpm, render_state_.throttle, dt);
    }
    if (const sim::Vehicle* sv = vehicle_->sim_vehicle()) {
        sim::Vec3 o = render_state_.position - to_sim(camera_.eye());
        SoundView view = camera_.onboard() ? SoundView::InCarTarget
                         : camera_.mode() == CameraMode::Tv || replaying_ ? SoundView::Other
                                                                           : SoundView::ChaseTarget;
        if (se_on()) extra_.update(sfx_, app.audio().voices(),
                      extra_frame(*sv, render_state_, sound_car_, camera_.onboard(), o.dot(cam_right(camera_)),
                                  o.dot(cam_forward(camera_)), view));
    }
    update_course_ambience(app);
    // Controller force feedback (triggers, wheels) while someone is driving.
    if (!replaying_ && !demo_ && rumble_on_) {
        if (!finished_)
            if (const sim::Vehicle* sv = vehicle_->sim_vehicle())
                app.input().set_force_feedback(0, feedback(*sv, last_in_, ffb_wall_, ffb_car_));
        if (field_ && field_->has_second() && !finished2_)
            if (const sim::Vehicle* sv2 = field_->second_vehicle())
                app.input().set_force_feedback(
                    1, feedback(*sv2, last_in2_, ffb_wall2_, ffb_car2_));
    }
    ffb_wall_ = ffb_car_ = ffb_wall2_ = ffb_car2_ = 0;
}

// Tyre fy is along the wheel's right axis (forward x up), so in a right-hand corner the
// front tyres push right (fy > 0) and the wheel is pulled left (negative force).
ForceFeedbackState RaceScreen::feedback(const sim::Vehicle& v, const sim::ControlInputs& in, float wall, float car) {
    const sim::VehicleTelemetry& t = v.telemetry();
    ForceFeedbackState f;
    f.speed_kmh = v.state().speed_kmh;
    f.brake = in.brake;
    f.abs_active = t.abs_active;
    float front_load = std::max(1.0f, v.spec().mass * v.spec().front_weight * 9.81f);
    f.steer_force = std::clamp(-(t.fy[0] + t.fy[1]) / front_load, -1.0f, 1.0f);
    f.jolt = std::min(1.0f, std::max(wall, car) / 5000.0f);
    return f;
}

ai::DriverParams RaceScreen::player_params(bool event) const {
    const sim::Vehicle* sv = vehicle_ ? vehicle_->sim_vehicle() : nullptr;
    const track::Track* rt = track_ ? track_->runtime() : nullptr;
    if (!sv || !rt) return ai::DriverParams::builtin(driver_param_set(db_, course_label_for_dirt_));
    const int cls = ai::driveline_class(sv->spec());
    ai::DriverParams p = ai::course_params(rt->driver_sets(), driver_param_set(db_, course_label_for_dirt_), cls);
    if (event) {
        const std::string& label = !license_event_.empty() ? license_event_ : race_event_;
        if (const data::Event* ev = label.empty() ? nullptr : db_.event(label)) ai::apply_event(p, *ev, cls);
    }
    return p;
}

ai::AiDriver& RaceScreen::player_auto_driver() {
    if (!auto_driver_) {
        if (!runup_course_) runup_course_ = std::make_shared<ai::AiCourse>(*track_->runtime());
        auto_driver_ = std::make_unique<ai::AiDriver>(runup_course_, player_params(false));
        auto_driver_->reset(vehicle_->state());
    }
    return *auto_driver_;
}

ai::EndOfRun RaceScreen::player_end_of_run() const {
    const sim::Vehicle* sv = vehicle_ ? vehicle_->sim_vehicle() : nullptr;
    const bool pit_surface = sv && sv->telemetry().on_pit_surface;
    if (field_) return ai::end_of_run(ai::RunEnd::RaceFinish);
    if (machine_) return ai::end_of_run(ai::RunEnd::MachineTest);
    if (!license_event_.empty())
        return license_.state() == LicenseRun::State::Failed ? ai::end_of_run(ai::RunEnd::LicenceFail)
                                                             : ai::end_of_run(ai::RunEnd::LicenceFinish, license_.rules().kind);
    if (tt_failed_) return ai::end_of_run(ai::RunEnd::TimeTrialFail);
    // (A free run's end: pause EXIT after a lap, race/post_run.h.)
    if (free_run_ || run_kind() == race::RunKind::FreeRun) return ai::end_of_run(ai::RunEnd::FreeRunEnd, 0, pit_surface);
    return ai::end_of_run(ai::RunEnd::TimeTrialFinish);
}

// Recommended-gear hint (GT3: the player's car runs the AI's line bookkeeping passively;
// when faster than the coming section's target speed T, the hint is the gear the automatic
// box would pick for T, blinking in braking zones). T = the next point's speed, or in an
// arc's braking zone min(current, next).
void RaceScreen::update_gear_hint(const sim::VehicleState& s) {
    hint_gear_ = -1, hint_blink_ = false;
    const sim::Vehicle* sv = vehicle_ ? vehicle_->sim_vehicle() : nullptr;
    const track::Track* rt = track_ ? track_->runtime() : nullptr;
    if (!sv || !rt || replaying_) return;
    if (!hint_driver_) {
        if (!hint_course_) hint_course_ = std::make_shared<ai::AiCourse>(*rt);
        hint_driver_ = std::make_unique<ai::AiDriver>(hint_course_, player_params(false));
        hint_driver_->reset(s);
    }
    ai::CarSnapshot me{s.position, s.orientation, s.velocity};
    hint_driver_->update(*sv, {race_time_, std::span<const ai::CarSnapshot>(&me, 1), 0});
    const ai::AiDebug& d = hint_driver_->debug();
    float t = d.mode == 4 ? std::min(d.target_speed, d.next_speed) : d.next_speed;
    float v = s.speed_kmh / 3.6f;
    if (t <= 0 || v < t) return;
    const sim::CarSpec& sp = sv->spec();
    float r = sp.drivetrain.drive == sim::Drive::FF ? sv->setup().wheels[0].radius : sv->setup().wheels[2].radius;
    hint_gear_ = sim::model::gear_for_speed(sp.gearbox, t, r, sv->setup().upshift_rpm);
    hint_blink_ = d.mode == 3 || d.mode == 4;
    static int last = -1;
    if (debug_ && hint_gear_ != last)
        LOG_INFO("gear hint %d%s (v %.1f T %.1f mode %d)", hint_gear_, hint_blink_ ? " blink" : "", v, t, d.mode);
    last = hint_gear_;
}

// Dirt (GT3: the car's condition word, 0..0.8, weakens the reflection passes as 1 - dirt):
// every step it grows by S x (8 on tarmac courses, else 1) x 0.001 x dt, S = the sum over the
// wheels on loose surfaces (indices 2-6, 12-14) of the smoke-emission intensity (physics §8.3b).
void RaceScreen::update_dirt(double dt) {
    const sim::Vehicle* sv = vehicle_ ? vehicle_->sim_vehicle() : nullptr;
    if (!sv || replaying_) return;
    const sim::VehicleState& st = sv->state();
    const sim::VehicleTelemetry& t = sv->telemetry();
    float sum = 0;
    for (size_t i = 0; i < 4; ++i) {
        int si = sim::model::surface_index(st.wheels[i].surface);
        bool loose = (si >= 2 && si <= 6) || (si >= 12 && si <= 14);
        if (loose && st.wheels[i].grounded) sum += t.smoke_emit[i]; // the wheel's smoke emission (already <= 1)
    }
    int category = 0;
    for (const data::Course& c : db_.courses())
        if (c.label == course_label_for_dirt_) category = c.surface;
    car_dirt_ = std::min(0.8, car_dirt_ + double(sum) * (category == 0 ? 8.0 : 1.0) * 0.001 * dt);
    if (category == 1) dirt_dims_ = false; // the wet Route 5: no dimming
}

// Pylons (GT3's rigid-body pylons, every race mode): each car's box knocks them about; a
// touch by the player's car fails a licence.
void RaceScreen::update_pylons(double dt) {
    if (pylons_.count() == 0) return;
    std::vector<Pylons::Car> cars;
    auto add = [&](const sim::Vehicle* v) {
        if (!v) return;
        const sim::VehicleState& st = v->state();
        const sim::CarSpec& sp = v->spec();
        Pylons::Car c;
        c.pos = st.position, c.vel = st.velocity, c.orientation = st.orientation;
        c.yaw_rate = st.angular_velocity.y;
        c.front = sp.front_extent > 0 ? sp.front_extent : sp.length * 0.5f;
        c.rear = sp.rear_extent > 0 ? sp.rear_extent : sp.length * 0.5f;
        c.half_width = sp.width * 0.5f;
        cars.push_back(c);
    };
    add(vehicle_ ? vehicle_->sim_vehicle() : nullptr); // car 0 = the player
    if (field_) {
        if (field_->has_second()) add(field_->second_vehicle());
        for (int i = 0; i < field_->opponents(); ++i) add(field_->opponent_vehicle(i));
    }
    unsigned hits = pylons_.step(track_->query(), cars, dt, &track_->world());
    if ((hits & 1u) && !pylon_hit_) {
        pylon_hit_ = true;
        const sim::Vec3& p = vehicle_->state().position;
        LOG_INFO("Race: pylon touched at (%.1f %.1f %.1f), %.0f km/h", p.x, p.y, p.z, vehicle_->state().speed_kmh);
    }
}

// Tyre smoke / dust: every car's wheels emit each tick (their smoke emission, physics §8.3b), then the puffs move.
void RaceScreen::update_smoke(double dt) {
    if (!smoke_.loaded()) return;
    const track::Track* rt = track_ ? track_->runtime() : nullptr;
    auto car = [&](const sim::Vehicle* v) {
        if (!v) return;
        const sim::VehicleState& st = v->state();
        const sim::VehicleTelemetry& t = v->telemetry();
        for (size_t i = 0; i < 4; ++i) {
            const sim::WheelState& w = st.wheels[i];
            if (!w.grounded || t.smoke_emit[i] <= 0.001f) continue; // the wheel's smoke emission (physics §8.3b)
            float sun = 1;
            if (rt) {
                RoadShade rs = road_shade(rt->road(), w.contact.x, w.contact.z, w.contact.y + 1.0f);
                if (rs.hit) sun = rs.a;
            }
            smoke_.emit({w.contact.x, w.contact.y, w.contact.z}, sim::model::surface_index(w.surface), t.smoke_emit[i], sun,
                        (i & 1) != 0);
        }
    };
    car(vehicle_ ? vehicle_->sim_vehicle() : nullptr);
    if (field_) {
        if (field_->has_second()) car(field_->second_vehicle());
        for (int i = 0; i < field_->opponents(); ++i) car(field_->opponent_vehicle(i));
    }
    smoke_.update(dt);
}

} // namespace gt3r::race_screen
