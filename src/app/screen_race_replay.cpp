// Replays: the run's recording, playback (watch / restart / stop / save), the replay view
// mode and REPLAY OPTIONS (items, target), the replay pad, the followed car and its battle
// partner, and the director's per-shot colour filter.
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"

#include "sim/exact/race_record.h"

namespace gt3r::race_screen {

// The last run's replay: once the run has ended, the one latched then (latch_run_replay); during the run, from the live
// race.
Replay RaceScreen::finished_recording() const {
    if (run_replay_) return *run_replay_;
    Replay r = recorder_.replay();
    r.final_position = vehicle_->state().position;
    // What the theater's info panels show, and the car itself (replay format v6).
    r.kind = replay_kind();
    r.laps = int(lap_times_.size());
    double sum = 0;
    for (double t : lap_times_) sum += t;
    r.total_time = !license_event_.empty() ? (license_.state() == LicenseRun::State::Finished ? license_.time() : -1)
                   : lap_times_.empty()    ? -1
                   : field_                ? sum
                                           : lap_times_.back();
    if (field_) r.grid_by_power = field_grid_by_power_; // the field it ran in
    if (two_player_ && field_ && field_->has_second()) r.battle = battle_;
    r.car_model = launch_model_;
    if (from_menu_) r.wheel = launch_.request.car_wheel;
    // The race's record (the setup, every frame's pads and the game's changes; format v11), which its playback runs again.
    if (exact_field()) r.exact = exact_recording(*exact_field());
    if (r.exact && r.exact->frames != r.ticks())
        LOG_WARN("Replay: the race has %u frames, the run %u ticks", r.exact->frames, r.ticks());
    // The power the menus show: the car's curve (a garage car's with its parts and settings) x the engine condition factor
    // it started with.
    const float f = sim::engine_condition_factor(r.condition.wear_m, r.condition.oil_m);
    const ExactCarSource src = launch_source_.value_or(ExactCarSource{car_.label});
    std::optional<sim::CarSpec> sp =
        src.parts ? sim::build_car_spec(db_, src.label, *src.parts, src.settings.value_or(sim::stock_settings(db_, src.label, *src.parts)))
                  : sim::build_car_spec(db_, src.label);
    if (sp) r.power_ps = game::spec_power_ps(*sp, f);
    return r;
}

// The live run has ended (after_run; a pause EXIT that finishes it, at the press, where its recording closes): its replay
// as it is now, for REPLAY, SAVE REPLAY, the ghost and the menu's backdrop, which rebuild the field (the licence preview
// restarts the race): the field is then no longer the run's.
void RaceScreen::latch_run_replay() {
    if (replaying_ || attract_ || demo_ || run_replay_) return;
    run_replay_ = finished_recording();
}

// Back from a demonstration or the licence preview (the race restarted): the run before it as the recording again, and
// as the run's latched replay.
void RaceScreen::restore_pre_demo_run() {
    if (pre_demo_run_) {
        recorder_.assign(*pre_demo_run_);
        if (pre_demo_run_->ticks() > 0) run_replay_ = std::move(*pre_demo_run_);
    }
    pre_demo_run_.reset();
}

ReplayKind RaceScreen::replay_kind() const {
    if (online_) return ReplayKind::Online;
    if (two_player_) return ReplayKind::TwoPlayer;
    if (!license_event_.empty()) return ReplayKind::License;
    if (!from_menu_) return field_ ? ReplayKind::ArcadeRace : ReplayKind::TimeTrial;
    const std::string& m = launch_.request.mode;
    if (launch_.request.practice || m == "go_free_run" || m == "go_machine_test") return ReplayKind::Practice;
    if (m.rfind("go_", 0) == 0) return ReplayKind::GtRace;
    return m == "single_race" ? ReplayKind::ArcadeRace : ReplayKind::TimeTrial;
}

// Leaves a replay (the replay pause's EXIT): the automatic replay and the REPLAY item's end at
// once with the race-exit transition, then the quick menu (screen_race_post_run.cpp); else
// drive again.
void RaceScreen::end_replay(App&) {
    if (replay_to_menu_) begin_exit_transition(ExitThen::Menu);
    else stop_playback();
}

void RaceScreen::watch_replay() {
    if (replaying_ || recorder_.tick() == 0) return;
    if (finished_) latch_result(); // the replay re-runs the field: keep the live run's result
    start_playback(finished_recording());
}

std::shared_ptr<const sim::exact::RaceRecord> RaceScreen::exact_playback() const {
    if (replaying_) return replay_.exact;
    if (demo_) return demo_record_;
    return nullptr;
}

std::shared_ptr<const sim::exact::RaceRecord> RaceScreen::exact_ghost_source() const {
    if (replaying_ || demo_ || !preset_.ghost || !whole_run_ghost()) return nullptr;
    return ghost_record_;
}

bool RaceScreen::can_play(const Replay& r, std::string* why) {
    if (!r.exact || !app_ || !load_program_data(*app_)) {
        if (why) *why = "This replay cannot be played on this disc.";
        return false;
    }
    if (r.exact->program != exact_data_->fingerprint()) {
        if (why) *why = "This replay was recorded with another version of the game and cannot be played.";
        return false;
    }
    return true;
}

void RaceScreen::start_playback(const Replay& r) {
    if (std::string why; !can_play(r, &why)) {
        LOG_WARN("Replay: %s", why.c_str());
        toast(why);
        return;
    }
    take_live_condition(); // the live run's engine condition, before the replay's replaces it
    replay_ = r;
    player_ = ReplayPlayer(&replay_);
    replaying_ = true;
    if (race_mode_) { // a race's replay: its field (the opponents' paints drawn again from the race's state)
        race_event_ = replay_.event;
        race_opponents_ = replay_.opponents;
        paint_seed_override_ = replay_.paint_seed;
    }
    // The recorded race, from its record.
    if (!app_ || !build_vehicle(*app_)) { // (the recorded race could not be built: nothing is played)
        replaying_ = false;
        restart();
        return;
    }
    seed_vehicle_condition(replay_.condition, false);
    reset_run_state();
    replay_finish_.reset();
    start_hud_clocks(attract_ || backdrop_ ? 2 : 1, replay_.start_speed);
    if (camera_.mode() != CameraMode::Tv && !driver_view_) drive_camera_ = camera_.mode();
    camera_.set_mode(CameraMode::Tv);
    restart_director();
    LOG_INFO("Replay: %s / %s, %u ticks (%.1f s)", replay_.course.c_str(), replay_.car.c_str(), replay_.ticks(),
             replay_.duration());
}

void RaceScreen::stop_playback() {
    replaying_ = false;
    camera_.set_mode(drive_camera_);
    restart();
}

void RaceScreen::save_current_replay() {
    if (!app_) return;
    Replay r = replaying_ ? replay_ : finished_recording();
    if (r.ticks() == 0) return;
    std::optional<std::string> path = save_replay(r, replay_dir(*app_));
    toast(path ? "Replay saved" : "Could not save the replay");
}

// REPLAY OPTIONS ITEM: the HUD parts shown during replays (default none), on the replay layout
// (presentation §D9): easy meter (the in-car cluster, or the speed and gear on a small panel
// outside the car), car name, time (the lap count, the current lap and the pop-ups of the
// followed car, §D6), map, steering, tyres and accel / brake.
// The course name over the replay's start is drawn whatever ITEM says (presentation §B3).
void RaceScreen::draw_replay_items(App& app, Ui& ui, const SDL_FRect& rect) {
    HudData d = hud_data(0);
    d.live = false;
    d.wrong_way = d.auto_drive = false;
    d.countdown = d.finish_t = -1;
    d.four_three = original_res_;
    const SDL_FRect* vp = original_res_ ? &rect : nullptr;
    if (const u8 it = replay_settings_.items) {
        HudData items = d;
        items.items = it;
        items.intro_title.clear();
        hud_.draw(ui.renderer(), items, vp);
        draw_event_icons(app, 0); // a gap's car picture
    }
    hud_.draw_title(ui.renderer(), d, vp);
}

// A run's or replay's start (presentation §B3): the title's clock (the long form for a run from
// rest under the race rules, else the short one; none in the attract demo or the menu backdrop,
// nor for a course named "-...") and the replay mark's.
void RaceScreen::start_hud_clocks(int replay, float start_speed) {
    if (replay == 2 || title_name_.empty() || title_name_[0] == '-') title_clock_.stop();
    else title_clock_.start(long_title(start_speed));
    mark_timer_.start();
}

// The long form: standing-start races, licence and machine tests from rest, 2P; the short one:
// moving starts and the time trial, free run and free practice rules (their own conductors).
bool RaceScreen::long_title(float start_speed) const {
    if (start_speed > 0) return false;
    switch (run_kind()) {
    case race::RunKind::TimeTrial:
    case race::RunKind::FreeRun:
    case race::RunKind::FreePractice: return false;
    case race::RunKind::Other: return field_ || !license_event_.empty(); // (a theater file: a race or a test)
    default: return true;
    }
}

// REPLAY OPTIONS changed: store them and apply sound and the view mode. Every apply resets the
// camera manager (presentation §G14): the cut countdown, the mode-1 level and music-timed cuts
// from the settings, no shot look, and the view's reset (view mode 1: a cut at once). A new MODE
// or TARGET sets the target as the original does (presentation §G3, §G4): a new shot, the
// interest scores restarted for the target and, in view modes 0, 1 and 3, the cameras on the
// followed car alone.
void RaceScreen::apply_replay_settings(App& app, const ReplaySettings& rs) {
    const bool mode_changed = rs.mode != replay_settings_.mode;
    const bool target_changed = mode_changed || rs.current_target() != replay_settings_.current_target();
    replay_settings_ = rs;
    replay_settings_.save(app.config());
    app.save_config();
    replay_se_ = rs.se;
    cut_clock_.reset(rs.level, attract_ || backdrop_ ? 0.0f : rs.music_cuts);
    director_.clear_look();
    director_.reset();
    last_shot_ = -2;
    if (target_changed && field_ && !attract_ && !backdrop_) {
        const std::vector<ReplayFocus::Car> cars = focus_cars();
        if (rs.mode != 2) follow(replay_focus_ + 1, -1, 1);
        focus_.reset_interest(replay_focus_ + 1, rs.current_target(), cars, u32(SDL_GetPerformanceCounter()));
    }
}

// A replay's (or demonstration's) set-up: the director's generator from a clock value, no look;
// the camera manager's request 1 and the view's reset (the first frame cuts, on the car alone),
// its countdown, level and music-timed cuts (the attract demo and the menu backdrops: off,
// presentation §G3); the start's dissolve from the last picture (§G16).
void RaceScreen::restart_director() {
    director_.setup();
    director_.seed(u32(SDL_GetPerformanceCounter())); // the game seeds it from a clock value
    director_.clear_look();
    cut_clock_.setup(replay_settings_.level, attract_ || backdrop_ ? 0.0f : replay_settings_.music_cuts);
    cut_acc_ = 0;
    last_shot_ = -2;
    start_dissolve(0.5f);
    begin_replay_focus();
}

// The attract demo picks its own view mode and target (presentation §G13), counting the demos
// since the last avant-garde one over the whole session.
namespace {
int g_attract_since_avant_garde = 0;
} // namespace

void RaceScreen::pick_attract_view() {
    restart_director();
    AttractView v = race::pick_attract_view(director_.rng().rand01(), g_attract_since_avant_garde);
    attract_view_ = replay_settings_;
    attract_view_.mode = v.mode;
    attract_view_.target[size_t(v.mode)] = v.target;
    begin_replay_focus(); // (with the picked target)
    LOG_INFO("Attract demo: view mode %d, target %d", v.mode, v.target);
}

// The replay camera manager, every 60 Hz frame (presentation §G14), live and in replays: the
// race music's markers (its object steps before the cameras), the shot length, view mode 1's
// handler, then the next frame's cut request; and the dissolve's clock.
void RaceScreen::update_camera_manager(double dt) {
    cut_acc_ += dt * 60.0;
    for (int i = 0; cut_acc_ >= 1.0 - 1e-9 && i < 12; ++i) {
        cut_acc_ -= 1.0;
        step_dissolve();
        if (app_) music_.step_markers(app_->audio());
        const MusicMarkers* m = &music_.markers();
        const int mode = director_.view_mode();
        cut_clock_.update_shot_length(m, mode);
        director_.set_cut_gate(cut_clock_.shot_length(), cut_clock_.request() > 0); // (modes 0, 2, 3)
        if (mode == 1 && gtrc_ && camera_.mode() == CameraMode::Tv) avant_garde_frame();
        cut_clock_.update_request(m, mode, start_hold(), director_.rng());
    }
    if (cut_acc_ > 1.0) cut_acc_ = 0;
    if (director_.view_mode() != 1 || camera_.mode() != CameraMode::Tv) level_step_ = music_cut_step_ = 0;
}

// View mode 1's handler, one frame (presentation §G2, §G3): Up / Down the level, Right / Left
// the music-timed cuts (stored with the options), then a cut on the request (none during the
// standing-start hold), the view's reset or the pad (Cross, Circle, L1, R1). With TARGET AUTO in
// a field the cut picks the car it follows, aimed with its battle partner (alone for the
// countdown's request when the car had a course shot).
void RaceScreen::avant_garde_frame() {
    const bool pad = std::exchange(focus_cut_, false);
    const int ud = std::exchange(level_step_, 0), lr = std::exchange(music_cut_step_, 0);
    if (ud != 0 || lr != 0) {
        if (ud != 0) cut_clock_.step_level(ud);
        if (lr != 0 && cut_clock_.step_music_cuts(lr)) // (its indicator is never shown)
            LOG_INFO("Replay: music-timed cuts %s", cut_clock_.music_cuts_on(1) ? "on" : "off");
        if (!attract_ && !backdrop_) {
            replay_settings_.level = cut_clock_.level();
            replay_settings_.music_cuts = cut_clock_.music_cuts();
            if (app_) {
                replay_settings_.save(app_->config());
                app_->save_config();
            }
        }
    }
    const bool hold = start_hold();
    const int request = hold ? 0 : cut_clock_.request();
    if (!director_.avant_garde_due(request, hold, pad)) return;
    const bool field = field_ && !two_player_ && (replaying_ || attract_);
    std::vector<ReplayDirector::FieldCar> cars;
    const int own_mounts = car_visual_ ? int(car_visual_->mounts().size()) : 0;
    cars.push_back({track_->distance_of(render_state_.position), lap_count(), render_state_.speed_kmh / 3.6f, own_mounts});
    if (field)
        for (int i = 0; i < field_->opponents(); ++i) {
            const sim::VehicleState& o = field_->opponent_state(i);
            const CarVisual* v = size_t(i) < opp_visuals_.size() ? opp_visuals_[size_t(i)].get() : nullptr;
            cars.push_back({track_->distance_of(o.position), field_->opponent_lap_count(i), o.speed_kmh / 3.6f,
                            v ? int(v->mounts().size()) : 0});
        }
    const ReplayDirector::Cut cut =
        director_.avant_garde_cut(cars, field ? replay_focus_ + 1 : 0,
                                  field && view_settings().current_target() == ReplayFocus::Auto,
                                  cut_clock_.shot_length(), request, hold, u32(SDL_GetPerformanceCounter()));
    if (field) follow(cut.car, cut.pair ? focus_.partner(cut.car) : -1, 1);
    if (cut.dissolve > 0) start_dissolve(cut.dissolve);
    if (debug_)
        LOG_INFO("avant-garde cut: request %d x %.2f car %d procedural %d dissolve %.1f", request,
                 cut_clock_.shot_length(), cut.car, director_.procedural_shot().kind, cut.dissolve);
}

bool RaceScreen::start_hold() const { return field_ ? field_->race_time() < 0 : tt_hold_ > 0; }

void RaceScreen::start_dissolve(float seconds) { dissolve_.start(seconds); }

void RaceScreen::step_dissolve() { dissolve_.step(); }

// The pad during a replay (presentation §G3; the original's race input maps only Up, Down,
// Left, Right, Cross, Circle, L1 and R1 there). View modes 0, 3 and 2 with TARGET MANUAL: Up /
// Down the next / previous car; modes 0, 2 and 3: Cross the driver's view on / off (the view
// the player last drove with, on the followed car); mode 1: Cross, Circle, L1 and R1 a new cut,
// Up / Down the level of the music's marked sections, Right / Left the music-timed cuts on /
// off (§G3). Nothing changes the view mode: REPLAY OPTIONS does. The buttons are the pad's
// own (the menu pad, a DualShock 2's), not the race layout's actions: a trigger bound to
// accelerate is still R2 here.
void RaceScreen::replay_pad(const MenuInput& m) {
    const ReplaySettings& vs = view_settings();
    if (vs.mode == 1) {
        if (m.pressed(PadButton::Cross) || m.pressed(PadButton::Circle) || m.pressed(PadButton::L1) ||
            m.pressed(PadButton::R1))
            focus_cut_ = true;
        if (m.pressed(PadButton::Up)) level_step_ = 1;
        else if (m.pressed(PadButton::Down)) level_step_ = -1;
        if (m.pressed(PadButton::Right)) music_cut_step_ = 1;
        else if (m.pressed(PadButton::Left)) music_cut_step_ = -1;
        return;
    }
    if (field_ && !two_player_ && vs.current_target() == ReplayFocus::Manual) {
        if (m.pressed(PadButton::Up)) focus_step_ = 1;
        else if (m.pressed(PadButton::Down)) focus_step_ = -1;
    }
    if (m.pressed(PadButton::Cross) && replaying_) {
        driver_view_ = !driver_view_;
        camera_.set_mode(driver_view_ ? drive_camera_ : CameraMode::Tv);
        if (!driver_view_) { // back to the replay cameras: a new shot
            director_.reset();
            last_shot_ = -2;
        }
    }
}

// A new replay (or attract demo): no battles, every car held 40 frames, the cameras on the
// player's car (alone), the interest scores from the target, the replay cameras (not the
// driver's view).
void RaceScreen::begin_replay_focus() {
    focus_.begin();
    replay_focus_ = -1;
    replay_partner_ = -1;
    aim_from_ = aim_from_partner_ = -1;
    aim_left_ = aim_total_ = 0;
    focus_acc_ = 0;
    focus_step_ = 0;
    focus_cut_ = false;
    driver_view_ = false;
    attract_car_frames_ = 0;
    if (field_) focus_.reset_interest(0, view_settings().current_target(), focus_cars(), u32(SDL_GetPerformanceCounter()));
}

// The field for the focus: the player's car first, then the opponents, with their race places.
std::vector<ReplayFocus::Car> RaceScreen::focus_cars() const {
    std::vector<ReplayFocus::Car> cars;
    if (!field_) return cars;
    const int n = std::min(field_->opponents() + 1, ReplayFocus::kMaxCars);
    cars.resize(size_t(n));
    auto pos = [](const sim::Vec3& p) { return render::Vec3{p.x, p.y, p.z}; };
    cars[0].pos = pos(vehicle_->state().position);
    for (int i = 1; i < n; ++i) cars[size_t(i)].pos = pos(field_->opponent_state(i - 1).position);
    const std::vector<RaceField::Standing> st = field_->standings();
    int rank = 0;
    for (const RaceField::Standing& s : st) {
        if (s.car < -1) continue; // (a second player)
        if (s.car + 1 < n) cars[size_t(s.car + 1)].rank = rank;
        ++rank;
    }
    return cars;
}

// The cameras follow `car` (0 the player's, i the opponent i - 1) and aim between it and
// `partner` (-1 none), moving there from the last target over min(frames, 60) frames.
void RaceScreen::follow(int car, int partner, int frames) {
    if (frames > 1) {
        aim_from_ = replay_focus_ + 1, aim_from_partner_ = replay_partner_;
        aim_total_ = aim_left_ = std::min(frames, 60);
    } else {
        aim_total_ = aim_left_ = 0;
    }
    if (car - 1 != replay_focus_ || partner != replay_partner_)
        LOG_INFO("Replay cameras: car %d, partner %d (race time %.1f s)", car, partner, field_ ? field_->race_time() : 0.0);
    replay_focus_ = car - 1;
    replay_partner_ = partner;
    director_.target_set();
}

// One 60 Hz frame of the followed car's logic by view mode (presentation §G3).
void RaceScreen::step_replay_focus() {
    const ReplaySettings& vs = view_settings();
    const int target = vs.current_target();
    const std::vector<ReplayFocus::Car> cars = focus_cars();
    const int n = int(cars.size());
    if (n == 0) return;
    const int followed = std::clamp(replay_focus_ + 1, 0, n - 1);
    const bool settled = camera_.mode() != CameraMode::Tv || !director_.settling();
    focus_.step(cars, followed, target, field_->race_time() >= 0, settled);
    if (aim_left_ > 0) --aim_left_;
    const int step = std::exchange(focus_step_, 0);
    int next = -1;
    bool manual = step != 0;
    switch (vs.mode) {
    case 1: // the cuts pick the car (update_avant_garde); a battle beginning or ending re-aims
        if (focus_.battle_changed(followed)) follow(followed, focus_.partner(followed), 120);
        return;
    case 2:
        if (target == ReplayFocus::Manual) {
            if (step != 0) next = ReplayFocus::next_car(followed, step, n);
        } else if (!director_.camera_fresh() && focus_.pending() >= 0 && focus_.hold(followed) <= 0 &&
                   director_.switch_gate()) {
            next = focus_.take_pending(n); // TOP / AUTO
        }
        if (next >= 0 && next != followed) {
            follow(next, focus_.partner(next), 120);
            focus_.set_hold(next, 120);
            if (manual) director_.car_changed(120.0 / 60.0, true); // a new shot for the new car
            else director_.target_switched();
        } else if (focus_.battle_changed(followed)) {
            follow(followed, focus_.partner(followed), 120);
            focus_.set_hold(followed, 120);
        }
        return;
    default: // 0, 3: MANUAL only, no partner; the attract demo moves to the next car every 300 frames
             // (once the shot's gate is open)
        if ((attract_ || backdrop_) && ++attract_car_frames_ > 299 && director_.shot_gate()) {
            next = ReplayFocus::next_car(followed, 1, n);
            attract_car_frames_ = 0;
            manual = true;
        }
        if (step != 0) next = ReplayFocus::next_car(followed, step, n);
        if (next >= 0 && next != followed) {
            follow(next, -1, 15);
            focus_.set_hold(next, 15);
            if (manual) { // a new shot for the new car, with a 1 s dissolve (presentation §G16)
                director_.car_changed(step != 0 ? 15.0 / 60.0 : 0.0, step == 0);
                start_dissolve(1.0f);
            }
        }
        return;
    }
}

// Every frame of a replay or attract demo: the focus logic at 60 Hz, view mode 1's cuts, the
// driver's view (Cross), and a new shot after a manual car change.
void RaceScreen::update_replay_focus(double dt) {
    const bool focus = field_ && !two_player_;
    if (focus) {
        focus_acc_ += dt * 60.0;
        for (int i = 0; focus_acc_ >= 1.0 - 1e-9 && i < 12; ++i) {
            focus_acc_ -= 1.0;
            step_replay_focus();
        }
        if (focus_acc_ > 1.0) focus_acc_ = 0;
    }
    if (driver_view_ && replaying_) { // (view mode 1 keeps its own cameras)
        const CameraMode want = view_settings().mode != 1 ? drive_camera_ : CameraMode::Tv;
        if (camera_.mode() != want) camera_.set_mode(want);
    }
}

// The TV camera's aim: the followed car, or between it and its battle partner, moving from the
// last target over its frames (3 t^2 - 2 t^3); the pair for the zoom and mode 2's on-board cut.
void RaceScreen::aim_replay_camera() {
    if (!field_ || two_player_) return;
    const int n = field_->opponents() + 1;
    auto state = [&](int car) {
        if (car <= 0 || car >= n) return render_state_;
        return interpolate(field_->opponent_prev_state(car - 1), field_->opponent_state(car - 1), step_.alpha());
    };
    auto pos = [&](int car) {
        const sim::Vec3 p = state(car).position;
        return render::Vec3{p.x, p.y, p.z};
    };
    auto aim_of = [&](int car, int partner) { return partner >= 0 ? (pos(car) + pos(partner)) * 0.5f : pos(car); };
    const int car = replay_focus_ + 1, partner = replay_partner_ >= 0 && replay_partner_ < n ? replay_partner_ : -1;
    render::Vec3 aim = aim_of(car, partner);
    if (aim_left_ > 0 && aim_total_ > 0 && aim_from_ >= 0 && aim_from_ < n) {
        const float t = float(aim_left_) / float(aim_total_), e = t * t * (3.0f - 2.0f * t);
        const int from_partner = aim_from_partner_ >= 0 && aim_from_partner_ < n ? aim_from_partner_ : -1;
        aim = aim * (1.0f - e) + aim_of(aim_from_, from_partner) * e;
    }
    director_.set_pair(partner >= 0);
    if (partner < 0) {
        camera_.set_replay_aim(aim, nullptr, nullptr, 0);
        return;
    }
    const ReplayDirector::Pair pair{pos(car), pos(partner)};
    const sim::VehicleState ps = state(partner);
    const float length = partner == 0 ? vehicle_->setup().length : field_->opponent_setup(partner - 1).length;
    camera_.set_replay_aim(aim, &pair, &ps, length);
}

// View mode 1's look (presentation §G6) belongs to its cuts (ReplayDirector::roll_look); our
// fallback trackside cameras (a course without a camera script) roll it at their own cuts.
// Other view modes have none.
void RaceScreen::update_shot_filter() {
    const bool tv = director_.view_mode() == 1 && camera_.mode() == CameraMode::Tv;
    if (!tv) {
        director_.clear_look();
        last_shot_ = -2;
        return;
    }
    if (gtrc_) return;
    const int shot = 1000 + camera_.tv_index();
    if (shot == last_shot_) return;
    last_shot_ = shot;
    if (director_.roll_look()) start_dissolve(0.5f);
}

// The grade pass (presentation §G6): in a replay or demonstration only for a shot with levels
// or the colour filter (view mode 1), the course's Env0 values standing in for the half the
// shot does not set; in a live run on the course's grade flag, with its Env0 values.
void RaceScreen::apply_post_pass(render::DrawOptions& o) const {
    const SunParams& e = sun_.params();
    const bool replay = replaying_ || attract_ || demo_ || backdrop_;
    const ShotLook& f = director_.look();
    const bool levels = replay && f.levels, filter = replay && f.filter;
    render::DrawOptions::PostGrade& g = o.post_grade;
    g.on = replay ? levels || filter : e.has_env && e.grade;
    if (!g.on) return;
    g.black = levels ? f.black : e.has_env ? e.grade_black : 0.0f;
    g.white = levels ? f.white : e.has_env ? e.grade_white : 1.0f;
    for (int i = 0; i < 3; ++i) g.curve[i] = filter ? f.curve[i] : e.has_env ? e.grade_g[size_t(i)] : 0.5f;
    g.saturation = filter ? f.saturation : e.has_env ? e.saturation : 1.0f;
}

} // namespace gt3r::race_screen
