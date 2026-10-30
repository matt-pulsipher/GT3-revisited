// After a run, as the original's race drivers go on after every run (presentation §A8, §C4, §C5,
// §E5, §F; menus §11; the rules in race/post_run.h): the race-exit transition, the prize /
// win trophy and the record's name entry, the run's replay by itself, the quick menu and the
// race behind it (play mode 2), and the pause window's EXIT that counts as a finish.
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"
#include "sim/exact/race.h"

namespace gt3r::race_screen {

race::RunKind RaceScreen::run_kind() const {
    using race::RunKind;
    if (online_ || attract_ || theater_) return RunKind::Other;
    if (free_run_) return RunKind::FreePractice;
    if (two_player_) return RunKind::TwoPlayer;
    if (!license_event_.empty()) return RunKind::Licence;
    if (machine_) return RunKind::MachineTest;
    if (field_) {
        if (championship()) return RunKind::Championship;
        // (Screenshots: prerace=1 / gtq=1 show the GT race's menus.)
        if ((from_menu_ && launch_.request.mode == "go_race") || preset_.prerace || preset_.gt_result)
            return RunKind::GtRace;
        return RunKind::ArcadeRace;
    }
    return race_laps_ > 0 ? RunKind::TimeTrial : RunKind::FreeRun;
}

void RaceScreen::pause_exit() {
    race::PauseExitRun r;
    r.kind = run_kind();
    r.top_speed_test = machine_test_top_speed();
    r.finished = finished_;
    r.laps_completed = track_ ? track_->laps_completed() : 0;
    r.top_speed_mps = top_speed_kmh_ / 3.6f;
    if (race::pause_exit_finishes(r)) exit_as_finish(race::exit_finish_overlay_delay(r));
    else begin_exit_transition(ExitThen::RunEnd);
}

// Pause EXIT as the run's finish (a free run with a lap, the top-speed test once moving): the
// record overlay over the running race (the course's ranking, or the top speed), the end-of-run
// driver, and the recording closed at the press, so the replay ends there.
void RaceScreen::exit_as_finish(double overlay_delay) {
    finished_ = true;
    finish_position_ = 1;
    exit_finished_ = true;
    overlay_delay_ = overlay_delay;
    track_->lap_freeze(); // nothing is timed after the finish
    recorder_.assign(Replay(recorder_.replay()));
    // The race's record too (the replay is its record), and the race object's own EXIT: its recordings closed, the car's
    // end of run (the end-of-run driver drives on).
    if (RaceField* f = exact_field()) exact_exit_finish(*f, machine_.has_value());
    latch_run_replay(); // (the car drives on: the replay ends here)
    if (machine_) rank_finish(top_speed_kmh_, true);
    else rank_finish(track_->best_lap_time(), false);
    LOG_INFO("Race: pause EXIT is the run's finish (best lap %.3f s, top %.1f km/h)", track_->best_lap_time(),
             double(top_speed_kmh_));
}

void RaceScreen::begin_exit_transition(ExitThen then) {
    exit_then_ = then;
    exit_transition_ = 0;
}

// The live run's race-exit transition is over (presentation §C4): its result latched as GT
// mode records it, then per mode (race::post_run) the prize page / win trophy and the
// record's name entry (presentations over the race), the run's replay, the quick menu.
void RaceScreen::after_run(App& app) {
    if (online_ || free_run_) { // online: back to the lobby; the free practice: its pre-race menu
        leave(app);
        return;
    }
    silence(app);
    latch_result();
    latch_run_replay();
    keep_run_laps();
    last_run_ended_ = finished_;
    const race::RunKind kind = run_kind();
    const menu::flow::GameRequest& rq = launch_.request;
    solo_run_ghost(); // (before the switches: a recording too long to keep sets no record)
    qm_items_.after_run(kind, finished_, run_session_record_, championship() && rq.champ_last);
    race::RunOutcome o;
    o.finished = finished_ && !tt_failed_;
    o.place = field_ && finished_ && finish_position_ > 0 ? finish_position_ - 1 : -1;
    bool shows = false;
    if (race::post_run(kind, o).prize && from_menu_ && !rq.practice) {
        const bool queued = kind == race::RunKind::ArcadeRace
                                ? rq.mode == "single_race" && game::queue_arcade_trophy(rq.race_code)
                                : game::queue_race_prize(rq.event.empty() ? rq.race_code : rq.event, o.place);
        if (queued) prize_shown_ = shows = true;
    }
    // The run's record (licence medal, machine test, time trial, free run) into its ranking at
    // once; one that makes it queues its name entry (gt_mode §17).
    o.ranked = record_run();
    const race::PostRun p = race::post_run(kind, o);
    shows = shows || p.name_entry;
    if (p.replay) {
        replay_pending_ = true; // started once the presentations close, else at once
        if (shows) app.push(make_presentation_screen());
        else start_auto_replay(app);
        return;
    }
    open_quick_menu(app);
    if (shows) {
        top3_pending_ = p.name_entry; // the Top 3 again once the name is in
        app.push(make_presentation_screen());
    }
}

// The original's ghosts (presentation §G11): licence tests, time trials and machine tests run
// the session's best run as their ghost, free runs its best lap (solo race's runs; the free
// practice before a GT race has none).
bool RaceScreen::whole_run_ghost() const {
    const race::RunKind k = run_kind();
    return k == race::RunKind::Licence || k == race::RunKind::TimeTrial || k == race::RunKind::MachineTest;
}

bool RaceScreen::lap_ghost() const { return run_kind() == race::RunKind::FreeRun; }

// The run's time the ghost rules compare: the judged licence time, the time trial's (best)
// lap, the 0-400 m / 0-1000 m time to the goal; none after a FAIL or EXIT, nor in the
// top-speed test (presentation §G11).
double RaceScreen::ghost_run_time() const {
    switch (run_kind()) {
    case race::RunKind::Licence: return license_.state() == LicenseRun::State::Finished ? license_.time() : -1;
    case race::RunKind::MachineTest: return machine_ && machine_->timed() && machine_time_ > 0 ? machine_time_ : -1;
    case race::RunKind::TimeTrial: return tt_failed_ ? -1 : track_->best_lap_time();
    default: return -1;
    }
}

// After a live solo run (presentation §G11): a run that set the session record (at or under
// the session's best; a licence test with a medal or not) becomes the session's best run, SAVE
// GHOST's file; a run with a time under the ghost's (or with no ghost yet) gives the ghost the
// best run: at once with the Ghost option on, else after "Update Ghost?" on the next quick
// menu. A recording of 10 minutes or more is not kept: no record, no ghost. A free run's lap
// ghost is its race's own (the free run's lap handlers); here the run that drove the best lap
// is kept for SAVE GHOST, and a lap that beat the ghost with the Ghost option off (race info
// + 0x48, left set: the lap is not the ghost yet) asks "Update Ghost?".
void RaceScreen::solo_run_ghost() {
    if (replaying_ || demo_ || !preset_.ghost) return;
    if (lap_ghost()) {
        if (best_lap_in_run_) best_run_replay_ = finished_recording();
        best_lap_in_run_ = false;
        if (const RaceField* f = exact_field())
            if (const sim::exact::ExactRace* r = exact_race_of(*f); r && r->flow().ghost != 0) ghost_ask_ = true;
        return;
    }
    if (!whole_run_ghost()) return;
    const bool kept = race::ghost_recording_kept(recorder_.tick());
    if (!kept) run_session_record_ = false;
    const double t = ghost_run_time();
    if (run_session_record_ && t > 0) {
        best_run_time_ = t;
        best_run_replay_ = finished_recording();
        best_run_record_ = best_run_replay_->exact;
    }
    const long long ms = t > 0 ? std::llround(t * 1000.0) : -1;
    const long long ghost_ms = !ghost_record_ ? -1 : std::llround(ghost_time_ * 1000.0);
    if (!race::replaces_ghost(ms, kept, ghost_ms) || !best_run_record_) return;
    if (ghost_on_) take_best_run_ghost();
    else ghost_ask_ = true;
}

void RaceScreen::start_auto_replay(App& app) {
    replay_pending_ = false;
    silence(app);
    music_.stop(app.audio()); // the run's music ended with it; the replay starts its own (music §2.5)
    watch_replay();
    replay_to_menu_ = replaying_;
    if (!replaying_) open_quick_menu(app);
}

// A replay's transition is over: the quick menu cuts in.
void RaceScreen::return_to_menu(App& app) {
    replaying_ = false;
    camera_.set_mode(drive_camera_);
    replay_to_menu_ = false;
    lap_times_ = run_laps_; // the live run's (a saved replay's laps)
    silence(app);
    open_quick_menu(app);
}

// The race behind the quick menu (race::menu_backdrop). Play mode 2: the last run (a run alone)
// replayed from its start with records off (it is a replay) and a random view mode, picked as
// for the attract demo (presentation §G13); before any run the empty recording: the car at its
// start with no input. A field stands on its grid (ours: not simulated; with no recording the
// original's cars get no input).
void RaceScreen::start_menu_backdrop(bool before_run) {
    if (backdrop_) { // (again: from the start)
        backdrop_ = false;
        replaying_ = false;
        camera_.set_mode(drive_camera_);
    }
    switch (race::menu_backdrop(run_kind(), before_run)) {
    case race::MenuBackdrop::None: return;
    case race::MenuBackdrop::Preview: start_preview(); return;
    case race::MenuBackdrop::Race: break;
    }
    if (!track_ || !vehicle_ || preview_ || demo_) return;
    backdrop_ = true;
    if (!field_) {
        const bool q = quick_, shown = finish_menu_shown_;
        start_playback(finished_recording());
        quick_ = q, finish_menu_shown_ = shown; // (the playback resets the run state)
    } else {
        drive_camera_ = camera_.mode() == CameraMode::Tv ? drive_camera_ : camera_.mode();
        camera_.set_mode(CameraMode::Tv);
    }
    pick_attract_view();
    render_state_ = vehicle_->state();
    camera_.snap(render_state_);
}

void RaceScreen::stop_backdrop(App& app) {
    if (!backdrop_) return;
    backdrop_ = false;
    replaying_ = false;
    camera_.set_mode(drive_camera_);
    lap_times_ = run_laps_;
    silence(app);
}

void RaceScreen::update_backdrop(App& app, double dt) {
    if (replaying_) {
        const int n = step_.advance(dt);
        for (int i = 0; i < n; ++i) tick(app, PadState{});
        render_state_ = interpolate(prev_, vehicle_->state(), step_.alpha());
    }
    camera_.set_intro(false, 0);
    camera_.set_track_distance(track_->distance_of(render_state_.position));
    camera_.set_lap(lap_count());
    director_.set_view_mode(view_settings().mode);
    update_camera_manager(dt);
    if (car_visual_) camera_.set_replay_mounts(&car_visual_->mounts());
    camera_.update(render_state_, vehicle_->setup(), PadState{}, dt);
    update_shot_filter();
}

} // namespace gt3r::race_screen
