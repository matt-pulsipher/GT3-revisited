// The race loop: update_drive() (one frame: menus vs driving, pause, replay keys,
// the fixed-step ticks, finish -> results flow, cameras, sound placement, rumble) and
// tick() (one race step: the player's pad, scripted or demo inputs, laps, machine test
// marks, the finish, countdown), plus the per-step helpers (gear hint, dirt, pylons, tyre
// smoke, force feedback).
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"

namespace gt3r::race_screen {

sim::ControlInputs RaceScreen::scripted_inputs(double t, bool& reverse) const {
    sim::ControlInputs in;
    reverse = false;
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
        else if (f == "rev") reverse = v != 0; // the reverse request (the back input)
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

void RaceScreen::tick(App& app, const PadState& pad) {
    double dt = step_.step();
    animate_course(float(dt)); // the course's animations run with the race
    sim::ControlInputs in;
    // The race pad when it drives this step (the car reads it itself: RaceVehicle::set_pad); player 2's.
    const PadState* live_pad = nullptr;
    bool live_pad2 = false;
    // The display's clocks (the title, the replay mark) advance with each race step, while a
    // replay has frames (presentation §B3, §G7).
    if (!replaying_ || !race::replay_over(backdrop_ ? race::PlayMode::Backdrop : race::PlayMode::Replay, player_.done()))
        title_clock_.step(), mark_timer_.step();
    if (replaying_) {
        // A replay stops at the end of its recording; the menu backdrop runs on with a cleared
        // input frame (no steering, throttle or brake; race/post_run.h). The race plays its record; the recorded inputs
        // are the HUD's.
        const bool done = player_.done();
        if (race::replay_over(backdrop_ ? race::PlayMode::Backdrop : race::PlayMode::Replay, done)) return;
        in = player_.next_inputs();
        if (done && !replay_.inputs.empty()) in.automatic = replay_.inputs.back().automatic; // (the car's gearbox)
    } else {
        // The race's AI drives the player's car in the run-ups (a rolling start's first 5 s, a flying start up to the line),
        // the pit lane and after the finish (AUTO DRIVE), and with the test autopilot (auto=1, configuration 3): the pad is
        // not read then (its frame decoded, not used).
        const bool ai_drives = preset_.autodrive || finished_ || rolling_runup() || flying_runup() || demo_;
        sim::ControlInputs want = ai_drives ? sim::ControlInputs{} : controls_.update(pad, dt);
        if (!ai_drives) live_pad = &pad;
        // The player's reverse request (the back input at least the throttle; the shift logic's
        // reverse request, physics §2.6), recorded with the tick.
        bool pad_reverse = !ai_drives && controls_.reverse();
        int kind = ai_drives ? 0 : controls_.kind();
        if (!preset_.script.empty() && !ai_drives) {
            want = scripted_inputs(script_time_, pad_reverse);
            live_pad = nullptr;
        }
        script_time_ += dt;
        // A demonstration: the disc's recorded run (its race record plays it; without one, its inputs).
        if (demo_ && disc_demo_) {
            want = demo_inputs(demo_tick_);
            kind = disc_demo_->kind(demo_tick_);
            live_pad = nullptr;
        }
        // Tests (GT3R_RACE demo_drive=1): the disc's recorded inputs drive the player's own,
        // judged run (its tick counter runs from the start of the run).
        if (preset_.demo_drive && !demo_ && disc_demo_ && !finished_ && !flying_runup()) {
            want = demo_inputs(demo_drive_tick_);
            kind = disc_demo_->kind(demo_drive_tick_);
            ++demo_drive_tick_;
            live_pad = nullptr;
        }
        if (demo_) ++demo_tick_;
        vehicle_->set_reverse_request(pad_reverse);
        want.controller_kind = u8(kind);
        in = recorder_.record(want);
        recorder_.set_reverse(pad_reverse);
    }
    // A standing start's hold (the race holds the car itself).
    const bool holding = tt_hold_ > 0;
    prev_ = vehicle_->state();
    last_in_ = in;
    bool p2 = two_player_ && field_ && field_->has_second();
    if (p2) {
        if (replaying_) {
            last_in2_ = player_.second_inputs();
        } else {
            const bool ai2 = preset_.autodrive || finished2_;
            sim::ControlInputs w2 = ai2 ? sim::ControlInputs{} : controls2_.update(app.input().race_pad(1), dt);
            field_->set_second_pad(ai2 ? nullptr : &app.input().race_pad(1));
            live_pad2 = !ai2;
            const bool pad_reverse2 = !ai2 && controls2_.reverse();
            field_->set_second_reverse(pad_reverse2);
            w2.controller_kind = u8(ai2 ? 0 : controls2_.kind());
            last_in2_ = recorder_.record_second(w2);
            recorder_.set_second_reverse(pad_reverse2);
        }
        field_->set_second_inputs(last_in2_);
    }
    // Brake lamps (GT3: a 4-frame history of "brake pedal > 0" per car; lamp factor k / 4).
    brake_lamps_.push(0, vehicle_->brake_pedal());
    if (p2) brake_lamps_.push(1, field_->second_brake());
    if (const RaceField* f = exact_field()) // (the opponents on the course: a race's, a free practice's)
        for (int i = 0; i < f->opponents(); ++i) brake_lamps_.push(2 + size_t(i), f->opponent_brake(i));
    if (online_) online_before_step(dt);
    vehicle_->set_pad(live_pad);
    vehicle_->step(in, dt);
    if (!replaying_) recorder_.set_driver_kind(vehicle_->driver_kind());
    // The car read the race pad itself (its record keeps the frame, which a replay plays): the replay's inputs, for its HUD,
    // are the ones whose frame (exact_inputs_frame) is the one it read (the handbrake and the back axis as on / off).
    if (!replaying_)
        if (const std::vector<sim::exact::PadFrame>* fr = exact_frames_of(*exact_field()); fr && !fr->empty()) {
            auto inputs = [](const sim::exact::PadFrame& f, sim::ControlInputs r) {
                r.steer = -float(f.steer) / 4096.0f;
                r.throttle = float(f.throttle) / 4096.0f;
                r.brake = float(f.brake) / 4096.0f;
                r.handbrake = f.third > 0;
                r.shift_up = f.shift > 0, r.shift_down = f.shift < 0;
                r.controller_kind = f.kind;
                return r;
            };
            if (live_pad) {
                last_in_ = recorder_.replace_last(inputs((*fr)[0], in));
                recorder_.set_reverse((*fr)[0].back > 0);
            }
            if (live_pad2 && fr->size() > 1) {
                last_in2_ = recorder_.replace_last_second(inputs((*fr)[1], last_in2_));
                recorder_.set_second_reverse((*fr)[1].back > 0);
            }
        }
    // A replay of a solo run: the computer took over once the timing had begun (the end of the run; not a flying start's
    // run-up): the run's car has ended, and its crossings post nothing more (presentation §D6; races: the race stops a
    // finished car's timer).
    if (replaying_ && vehicle_->driver_kind() != 0 && track_->lap_started()) replay_run_ended_ = true;
    if (online_) online_after_step();
    trace_tick();
    if (RaceField* f = exact_field())
        for (int i : f->take_pit_entries()) {
            // (-1 / -2: the players' cars)
            const std::string label = i == -1                         ? car_.label
                                      : i == -2 && preset_.car2.empty() ? car_.label
                                      : i == -2                         ? preset_.car2
                                                                        : f->opponent_label(i);
            const std::string name = i == -1 ? car_.name : car_display_name(label); // game text (the event display)
            post_info(name + " Pitting in...");
            LOG_INFO("Race: %s pitting in (t %.1f)", label.c_str(), race_time_);
        }
    // The turbo sounds read the car's processed throttle (0 through an upshift), sound §7.5.
    if (const sim::VehicleTelemetry* t = vehicle_->telemetry())
        extra_.step(CarExtraSounds::step_inputs(*t, vehicle_->facts().value_or(CarFacts{}).turbo));
    if (p2)
        if (const sim::VehicleTelemetry* t2 = field_->second_telemetry())
            extra2_.step(CarExtraSounds::step_inputs(*t2, field_->second_facts().value_or(CarFacts{}).turbo));
    if (p2) {
        const sim::VehicleState& s2 = field_->second_state();
        RumbleModel::Events ev2;
        for (const sim::WheelState& w : s2.wheels)
            if (w.grounded && track_->is_kerb(w.surface)) ev2.kerb = true;
        ev2.offroad = track_->is_offroad(track_->query().probe(s2.position.x, s2.position.z, s2.position.y + 2.0f));
        ev2.impact = field_->second_impact();
        rumble2_.update(s2, field_->second_telemetry(), ev2, last_in2_, dt);
        if (!finished2_ && field_->second_finish() >= 0) { // the race's finish
            finished2_ = true;
            finish_position2_ = field_->second_position();
            if (!finished_) p2_won_ = true;
        }
    }
    if (debug_ && (++debug_ticks_ % 30) == 0) {
        const sim::VehicleState& v = vehicle_->state();
        LOG_INFO("t=%.1f pos(%.1f %.1f %.1f) %.1f km/h gear %d rpm %.0f in(st %.2f th %.2f br %.2f) lap %.3f reverb %.3f "
                 "turbo %d amb(air %.2f city %.2f crowd %.2f sea %.2f) driver %d",
                 race_time_, v.position.x, v.position.y, v.position.z, v.speed_kmh, v.gear, v.engine_rpm, in.steer,
                 in.throttle, in.brake, track_->lap_progress(), ambience_.depth(), extra_.turbo().level(),
                 crss_.channel(0).l + crss_.channel(0).r, crss_.channel(1).l + crss_.channel(1).r,
                 crss_.channel(3).l + crss_.channel(3).r, crss_.channel(4).l + crss_.channel(4).r, vehicle_->driver_kind());
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
    // A solo run: the race's clock and start hold, and its end as the race flow decides it.
    if (exact_solo()) exact_solo_step();
    if (std::exchange(practice_pit_exit_, false)) flying_start_signal(app); // free practice: START out of the pit lane
    // The pre-race shot's cuts, and its cut to the player's view, dissolve over 1 s
    // (presentation §B3).
    if (live_view() && race::start_shot_dissolve(frames_to_signal())) start_dissolve(1.0f);
    // Races: the race's clock (0 = GO, the same for every car).
    bool lap_done = !holding && track_->lap_update(s.position, field_ ? std::max(field_->race_time(), 0.0) : race_time_);
    const bool timer_updated = !holding;
    u32 tick_index = replaying_ ? player_.tick() : recorder_.tick(); // ticks simulated so far

    // The lap's ticks (the logger's laps, a free run's best lap).
    bool started = track_->lap_started();
    if (started && !was_started_) {
        logger_.begin_lap();
        lap_begin_tick_ = tick_index;
        lap_ticks_ = 0;
        if (!license_event_.empty() && !license_.rules().standing) license_clock_start_ = race_time_; // (a flying test's clock)
    }
    was_started_ = started;
    if (started) {
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
        // Free runs (presentation §G11): a lap strictly under the session's best lap (its recording under 36,000 ticks)
        // is the best lap; the run that drove it is the one SAVE GHOST writes (the lap's ghost is the race's own: the free
        // run's lap handlers).
        if (!replaying_ && !demo_ && lap_ghost() && preset_.ghost) {
            const long long ms = std::llround(t * 1000.0);
            const long long best_ms = best_run_time_ < 0 ? -1 : std::llround(best_run_time_ * 1000.0);
            if (race::takes_best_lap(ms, race::ghost_recording_kept(lap_ticks_), best_ms)) best_run_time_ = t, best_lap_in_run_ = true;
        }
        lap_begin_tick_ = tick_index;
        lap_ticks_ = 1;
    }

    // The run's top speed, until its finish (the machine tests' record).
    if (!replaying_ && !demo_ && !holding && !finished_) top_speed_kmh_ = std::max(top_speed_kmh_, s.speed_kmh);
    // The HUD's own highest speed: every tick, past the finish too (presentation §D4); in
    // replays too (the top-speed test's pop-up at the goal, §D6).
    if (!demo_) hud_top_kmh_ = std::max(hud_top_kmh_, s.speed_kmh);
    // Machine tests (presentation §B7): the split points and the goal, track distances from the
    // control line, timed within the tick from GO, for the pop-ups (the race's split line ends the run: exact_solo_step).
    machine_marks_.clear();
    if (machine_ && !holding && ((!finished_ && !replaying_ && !demo_) || (replaying_ && !machine_->done())))
        machine_marks_ =
            machine_->update(dist_before, track_->race_distance(), track_->length(), clock_before, race_time_);
    // A demonstration: the race's own licence judge and end of the run (its record: race::demo_record).
    if (exact_demo() && !demo_logged_)
        if (const sim::exact::ExactRace* er = exact_race_of(*exact_field()); er && er->flow().result != 0 && er->flow().result != -2) {
            demo_logged_ = true;
            LOG_INFO("Licence demo: result %d, %.3f s (recorded %.3f s; step %zu)", er->flow().result,
                     er->flow().result_time / 1000.0, disc_demo_->time_ms / 1000.0, demo_tick_);
        }
    // Races: the race's finish (the race's laps, the leader has finished, or the first crossing after a time limit); the
    // place is the race order's.
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

    const sim::SurfaceHit h = track_->query().probe(s.position.x, s.position.z, s.position.y + 2.0f); // rumble, ambience
    {
        RumbleModel::Events ev;
        for (const sim::WheelState& w : s.wheels)
            if (w.grounded && track_->is_kerb(w.surface)) ev.kerb = true;
        ev.offroad = track_->is_offroad(h);
        ev.impact = vehicle_->last_impact();
        rumble_.update(s, vehicle_->telemetry(), ev, in, dt);
    }
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
    // (No respawns, as in GT3: a car stays where it is.)
}

// GT3R_RACE_TRACE=<file>: one line per race step with the player's car after it, for comparing a
// run with its replay ("L" live, "R" replay; the ticks simulated so far, the driver kind the step
// ran with, position and orientation as exact hex floats, the speed).
void RaceScreen::trace_tick() {
    static FILE* f = [] {
        const char* p = SDL_getenv("GT3R_RACE_TRACE");
        return p && *p ? std::fopen(p, "w") : nullptr;
    }();
    if (!f) return;
    const sim::VehicleState& s = vehicle_->state();
    std::fprintf(f, "%c %u %d %a %a %a %a %a %a %a %.3f\n", replaying_ ? 'R' : 'L',
                 replaying_ ? player_.tick() : recorder_.tick(), vehicle_->driver_kind(),
                 double(s.position.x), double(s.position.y), double(s.position.z), double(s.orientation.w),
                 double(s.orientation.x), double(s.orientation.y), double(s.orientation.z), double(s.speed_kmh));
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
    if (online_ && !online_update(app, dt)) return;
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
    if (setting_pending_) { // back from GT mode's settings, in the menu SETTING opened from: the ride car as now set up
        setting_pending_ = false;
        if (game::GtModeSession* gs = game::gt_mode_session())
            if (const game::GarageCar* gc = gs->gt().state().ride_car()) {
                launch_source_ = ExactCarSource{gc->car, gc->parts, gc->settings};
                // Before the first run the grid takes the car at once (the run state's reset closes the menu: open it
                // again; START then starts the race, which counts); after a run (machine test, free run) the next
                // START builds it and the last run stays to replay.
                if (prerace_ && build_vehicle(app)) {
                    restart();
                    quick_ = true;
                }
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
                demo_tick_ = 0, demo_logged_ = false;
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
    // GT3's buttons: R1 change view, L1 look back (held), R3 our performance overlay; see
    // race/controls.h. The overlay (a developer tool) is not on a controller by default, and
    // under the "gt3" layout the race pad is the menu pad, whose R3 does nothing in a race
    // (bind it in a custom race layout; GT3R_RACE=perf=1 starts with the overlay on).
    bool own_race_pad = !app.input().race_uses_menu_pad(0);
    if (replaying_ || (theater_ && demo_)) {
        // Replays: the original's replay pad (screen_race_replay.cpp, presentation §G3).
        replay_pad(m);
        camera_.set_look_back(false);
    } else {
        if (rpressed(PadButton::R1)) next_view(camera_, 0);
        camera_.set_look_back(pad.down(PadButton::L1));
    }
    // Runs with a ghost: the controller's own Select (the menu pad's) shows / hides it for the
    // rest of the visit, as in the original (presentation §G11).
    if ((whole_run_ghost() || lap_ghost()) && !replaying_ && !demo_ && m.pressed(PadButton::Select))
        ghost_shown_ = !ghost_shown_;
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
        if (std::optional<bool> m = exact_field() ? exact_playback_matches(*exact_field()) : std::nullopt) exact = *m;
        LOG_INFO("Replay: end, final %s the recording", exact ? "matches" : "DIFFERS FROM");
    }
    render_state_ = interpolate(prev_, vehicle_->state(), step_.alpha());
    if (two_player_ && field_ && field_->has_second()) {
        PadState pad2 = app.input().race_pad(1);
        auto pressed2 = [&](PadButton b) { return pad2.down(b) && !(pad2_prev_ & (1u << unsigned(b))); };
        if (pressed2(PadButton::R1)) next_view(camera2_, 1);
        camera2_.set_look_back(pad2.down(PadButton::L1));
        pad2_prev_ = pad2.buttons;
        render_state2_ = interpolate(field_->second_prev_state(), field_->second_state(), step_.alpha());
        camera2_.set_intro(live_view() && race::start_shot_on(frames_to_signal()), race::start_shot_t(frames_to_signal()));
        camera2_.set_track_distance(track_->distance_of(render_state2_.position));
        camera2_.set_lap(field_->second_laps().laps);
        camera2_.update(render_state2_, field_->second_setup(), pad2, dt);
        app.input().set_rumble(1, rumble2_on_ && !replaying_ ? rumble2_.rumble(app.input().gt3_rumble(1)) : PadRumble{});
        if (rumble2_on_ && !replaying_) app.input().set_trigger_rumble(1, rumble2_.trigger_left(), rumble2_.trigger_right());
        else app.input().set_trigger_rumble(1, 0, 0);
        if (se_on() && engine2_.loaded()) {
            sim::Vec3 off2 = render_state2_.position - to_sim(camera2_.eye());
            if (camera2_.onboard()) engine2_.set_position(0.0f, 1.0f);
            else engine2_.set_position(off2.dot(cam_right(camera2_)), off2.dot(cam_forward(camera2_)));
            engine2_.set_view(camera2_.onboard() ? EngineSound::View::OnboardOwn : EngineSound::View::ChaseOwn);
            engine2_.update(app.audio().voices(), render_state2_.engine_rpm, render_state2_.throttle, dt);
        }
        if (const sim::VehicleTelemetry* t2 = field_->second_telemetry()) {
            sim::Vec3 o2 = render_state2_.position - to_sim(camera2_.eye());
            if (se_on()) extra2_.update(sfx_, app.audio().voices(),
                           extra_frame(field_->second_facts().value_or(CarFacts{}), *t2, render_state2_, sound_car2_,
                                       camera2_.onboard(), o2.dot(cam_right(camera2_)), o2.dot(cam_forward(camera2_)),
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
    app.input().set_rumble(0, feel ? rumble_.rumble(app.input().gt3_rumble(0)) : PadRumble{});
    if (!feel) app.input().set_trigger_rumble(0, 0, 0);
    else app.input().set_trigger_rumble(0, rumble_.trigger_left(), rumble_.trigger_right());
    if (const RaceField* f = exact_field(); f && f->opponents() > 0) {
        std::vector<const sim::VehicleState*> st;
        for (int i = 0; i < f->opponents(); ++i) st.push_back(&f->opponent_state(i));
        render::Vec3 e = camera_.eye();
        opp_engines_.view = camera_.onboard() ? EngineSound::View::OnboardOther
                            : replaying_ || camera_.mode() == CameraMode::Tv ? EngineSound::View::Replay
                                                                             : EngineSound::View::ChaseOther;
        std::vector<float> slip;
        for (int i = 0; i < f->opponents(); ++i) {
            const sim::VehicleTelemetry* t = f->opponent_telemetry(i);
            slip.push_back(t ? t->slipstream : 0.0f);
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
    if (const sim::VehicleTelemetry* t = vehicle_->telemetry()) {
        sim::Vec3 o = render_state_.position - to_sim(camera_.eye());
        SoundView view = camera_.onboard() ? SoundView::InCarTarget
                         : camera_.mode() == CameraMode::Tv || replaying_ ? SoundView::Other
                                                                           : SoundView::ChaseTarget;
        if (se_on()) extra_.update(sfx_, app.audio().voices(),
                      extra_frame(vehicle_->facts().value_or(CarFacts{}), *t, render_state_, sound_car_, camera_.onboard(),
                                  o.dot(cam_right(camera_)), o.dot(cam_forward(camera_)), view));
    }
    update_course_ambience(app);
    // Controller force feedback (triggers, wheels) while someone is driving.
    if (!replaying_ && !demo_ && rumble_on_) {
        if (!finished_)
            if (const sim::VehicleTelemetry* t = vehicle_->telemetry())
                app.input().set_force_feedback(0, feedback(vehicle_->state(), *t, last_in_, ffb_wall_, ffb_car_));
        if (field_ && field_->has_second() && !finished2_)
            if (const sim::VehicleTelemetry* t2 = field_->second_telemetry())
                app.input().set_force_feedback(1, feedback(field_->second_state(), *t2, last_in2_, ffb_wall2_, ffb_car2_));
    }
    ffb_wall_ = ffb_car_ = ffb_wall2_ = ffb_car2_ = 0;
}

// The steering force is the one GT3 gives a force-feedback wheel (car +0x16D6, 0x80 centred; positive turns the wheel
// left, as the pad frame's steering).
ForceFeedbackState RaceScreen::feedback(const sim::VehicleState& s, const sim::VehicleTelemetry& t, const sim::ControlInputs& in,
                                        float wall, float car_hit) {
    ForceFeedbackState f;
    f.speed_kmh = s.speed_kmh;
    f.brake = in.brake;
    f.abs_active = t.abs_active;
    f.steer_force = t.wheel_force >= 0 ? std::clamp(-float(t.wheel_force - 0x80) / 127.0f, -1.0f, 1.0f) : 0.0f;
    f.jolt = std::min(1.0f, std::max(wall, car_hit) / 5000.0f);
    return f;
}

// The recommended-gear hint: the one the car's own AI driver object keeps (+0xAC / +0xAD, its gear and whether it blinks
// in a braking zone), live runs only.
void RaceScreen::update_gear_hint(const sim::VehicleState& s) {
    (void)s;
    hint_gear_ = -1, hint_blink_ = false;
    if (vehicle_ && !replaying_) hint_gear_ = vehicle_->gear_hint(hint_blink_);
}

// Dirt (GT3: the car's condition word, 0..0.8, weakens the reflection passes as 1 - dirt): the car's own, as its tyre
// effects keep it (physics §8.3b); the wet Route 5 shows none.
void RaceScreen::update_dirt(double dt) {
    (void)dt;
    const sim::VehicleTelemetry* tp = vehicle_ ? vehicle_->telemetry() : nullptr;
    if (!tp || replaying_) return;
    for (const data::Course& c : db_.courses())
        if (c.label == course_label_for_dirt_ && c.surface == 1) dirt_dims_ = false; // the wet Route 5: no dimming
    if (tp->dirt >= 0) car_dirt_ = tp->dirt;
}

// Pylons (GT3's rigid-body pylons, every race mode): each car's box knocks them about; a
// touch by the player's car fails a licence.
void RaceScreen::update_pylons(double dt) {
    if (pylons_.count() == 0) return;
    std::vector<Pylons::Car> cars;
    auto add = [&](const sim::VehicleState& st, const std::optional<CarFacts>& f) {
        if (!f) return;
        Pylons::Car c;
        c.pos = st.position, c.vel = st.velocity, c.orientation = st.orientation;
        c.yaw_rate = st.angular_velocity.y;
        c.front = f->front_extent;
        c.rear = f->rear_extent;
        c.half_width = f->width * 0.5f;
        cars.push_back(c);
    };
    if (vehicle_) add(vehicle_->state(), vehicle_->facts()); // car 0 = the player
    if (field_ && field_->has_second()) add(field_->second_state(), field_->second_facts());
    if (const RaceField* f = exact_field())
        for (int i = 0; i < f->opponents(); ++i) add(f->opponent_state(i), f->opponent_facts(i));
    const track::Track* rt = track_->runtime();
    unsigned hits = pylons_.step(track_->query(), cars, dt, rt ? std::span<const track::WallLine>(rt->wall_lines()) : std::span<const track::WallLine>{});
    // The conductor's hit rule for every touch (kind 2: licences and time trials fail on it). GT3 runs the
    // course's rigid bodies after the frame's race step (the race's frame update: the step), so a touch the step
    // made reaches the car before the next step, as it does here (pylons after the step, the hit before the next one).
    if ((hits & 1u) && solo_field_ && !replaying_ && !demo_) exact_report_hit(*solo_field_, 0, 2);
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
    auto car = [&](const sim::VehicleState& st, const sim::VehicleTelemetry* tp) {
        if (!tp) return;
        const sim::VehicleTelemetry& t = *tp;
        for (size_t i = 0; i < 4; ++i) {
            const sim::WheelState& w = st.wheels[i];
            if (!w.grounded || t.smoke_emit[i] <= 0.001f) continue; // the wheel's smoke emission (physics §8.3b)
            float sun = 1;
            if (rt) {
                RoadShade rs = road_shade(rt->road(), w.contact.x, w.contact.z, w.contact.y + 1.0f);
                if (rs.hit) sun = rs.a;
            }
            smoke_.emit({w.contact.x, w.contact.y, w.contact.z}, sim::surface_index(w.surface), t.smoke_emit[i], sun,
                        (i & 1) != 0);
        }
    };
    if (vehicle_) car(vehicle_->state(), vehicle_->telemetry());
    if (field_ && field_->has_second()) car(field_->second_state(), field_->second_telemetry());
    if (const RaceField* f = exact_field())
        for (int i = 0; i < f->opponents(); ++i) car(f->opponent_state(i), f->opponent_telemetry(i));
    smoke_.update(dt);
}

} // namespace gt3r::race_screen
