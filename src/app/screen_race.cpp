// The race screen: every race, solo run, replay and demonstration (on the exact core, race/exact_field.h). The
// race steps at a fixed rate (RaceField::tick_hz) decoupled from the frame rate; rendering interpolates between the last
// two steps. On its own (--screen race): choose a course and a car, then a time trial.
//
// Driving controls come from each player's race pad (InputManager::race_pad: the race
// layout, modern or GT3's own, with buttons in GT3's meaning): Cross throttle, Square brake
// (pressure), Circle handbrake, Triangle reverse, left stick / d-pad steer, R2/L2 shift
// (manual), R1 camera, L1 look back, Start pause (Restart /
// camera / gearbox / exit). See race/controls.h and docs/INPUT.md.
//
// GT3R_RACE="course=trialmt,car=r34_gtr_v_99,cam=0,auto=1,warp=6,mt=0,quit=20,paint=0"
// skips the selection: auto=1 lets the race's AI drive the player's car, warp=N
// simulates N seconds before the first frame (driving screenshots with
// --shot), quit=N exits after N seconds (frame-time measurements).
//
// The screen core: construction (menu, online, --screen race), the Screen interface
// (update / render dispatch between the selection and the drive), init, the toast and the
// perf log, and the public entry points (menu request handler, make_*_race_screen, the
// --screen registration).
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"
#include "game/language_rules.h"

namespace gt3r::race_screen {

LastView g_last_view[2];

// The menus' logical keys from a pad (menus §2): confirm = Circle or Cross,
// cancel = Triangle or Square, the left stick doubles the D-pad.
u16 menu_keys(const PadState& pad) {
    using menu::flow::MenuKey;
    u16 k = 0;
    auto set = [&](MenuKey key, bool on) {
        if (on) k |= menu::flow::key_bit(key);
    };
    set(MenuKey::Up, pad.down(PadButton::Up) || pad.axis(PadAxis::LY) < 0x40);
    set(MenuKey::Down, pad.down(PadButton::Down) || pad.axis(PadAxis::LY) > 0xC0);
    set(MenuKey::Left, pad.down(PadButton::Left) || pad.axis(PadAxis::LX) < 0x40);
    set(MenuKey::Right, pad.down(PadButton::Right) || pad.axis(PadAxis::LX) > 0xC0);
    set(MenuKey::Confirm, pad.down(PadButton::Circle) || pad.down(PadButton::Cross));
    set(MenuKey::Cancel, pad.down(PadButton::Triangle) || pad.down(PadButton::Square));
    set(MenuKey::Start, pad.down(PadButton::Start));
    set(MenuKey::Select, pad.down(PadButton::Select));
    set(MenuKey::L1, pad.down(PadButton::L1));
    set(MenuKey::R1, pad.down(PadButton::R1));
    return k;
}

RaceScreen::RaceScreen(MenuLaunch l) : preset_(read_preset()), launch_(std::move(l)) {
    from_menu_ = true;
    loading_ = menu::flow::race_loading_picture(); // what the menu left on screen (menus §6)
    scripted_ = preset_.have; // GT3R_RACE given (screenshots / tests)
    preset_.have = true;
    // Replay Theater (docs/dev/RACE.md "Replay Theater"): a saved replay, or the disc's
    // demonstration of an event (run as its licence test / arcade time trial with the
    // demo's inputs), played once; then back to the theater with no result.
    menu::flow::GameRequest& rq = launch_.request;
    if (rq.mode == "theater_replay") {
        theater_ = true;
        preset_.replay = rq.replay_file;
    } else if (rq.mode == "logger") { // the Analyzer: track A's replay (B from replay_file2)
        theater_ = logger_mode_ = true;
        preset_.replay = rq.replay_file;
    } else if (rq.mode == "theater_demo") {
        theater_ = true;
        theater_demo_ = rq.event;
        preset_.demo = true;
        if (rq.event.size() == 8 && rq.event.rfind("AT", 0) == 0) { // arcade time trial NN
            rq.mode = "time_trial";
            rq.course = "tt" + rq.event.substr(2, 2);
            rq.event.clear(), rq.race_code.clear();
        } else {
            rq.mode = "go_license";
            rq.practice = true; // nothing goes into the GT Mode save
        }
    }
}

// Online battle (docs/dev/ONLINE.md): the session's race; leaving returns to the lobby.
RaceScreen::RaceScreen(const OnlineLaunch& l) : preset_(read_preset()) {
    online_ = true;
    preset_.have = true;
    preset_.course = l.course;
    preset_.car = l.car;
    preset_.paint = l.paint;
    preset_.manual = l.manual;
    preset_.laps = l.laps;
    preset_.event.clear(), preset_.tt_event.clear(), preset_.replay.clear();
    preset_.two_player = preset_.watch = preset_.save = false;
    preset_.warp = 0;
    net_.car_info.assign(l.car_info.begin(), l.car_info.end());
}

RaceScreen::~RaceScreen() {
    if (loading_own_) SDL_DestroyTexture(loading_own_);
    if (!app_) return;
    app_->remember_race_scale();
    silence(*app_);
    music_.stop(app_->audio());
    ambience_.reset(app_->audio().voices());
}

void RaceScreen::update(App& app, double dt) {
    // Launched from the menus or online: the loading picture is drawn before each blocking
    // part of the load, the set-up and then the course and cars (menus §6).
    if (!init_) {
        if ((from_menu_ || online_) && !loading_drawn_) return; // the first frame: the picture only
        init(app);
        if (begin_pending_) return;
    } else if (begin_pending_) {
        begin_pending_ = false;
        begin(app);
        return;
    }
    if (launch_failed_) { // the message, then back to the menus with any button
        const MenuInput& m = app.menu();
        for (PadButton b : {PadButton::Cross, PadButton::Circle, PadButton::Triangle, PadButton::Start})
            if (m.pressed(b)) {
                app.pop();
                return;
            }
        return;
    }
    if (stage_ == Stage::Select) update_select(app);
    else if (stage_ == Stage::Drive) update_drive(app, dt);
    // Screenshots: the pause menu's Exit in a live run once it has driven exit_laps laps.
    if (preset_.exit_laps > 0 && stage_ == Stage::Drive && !replaying_ && !paused_ && !quick_ && exit_transition_ < 0 &&
        track_ && track_->laps_completed() >= preset_.exit_laps) {
        preset_.exit_laps = 0;
        pause_exit();
    }
    // Tests: leave for the menus a set time after the finish, as the quick menu's Exit.
    if (preset_.leave > 0 && from_menu_ && stage_ == Stage::Drive && finished_ && !replaying_) {
        leave_clock_ += dt;
        if (leave_clock_ > preset_.leave) {
            leave_clock_ = 0;
            preset_.leave = 0;
            quick_ = false;
            leave(app);
            return;
        }
    }
    if (preset_.quit > 0 && (clock_ += dt) > preset_.quit) {
        log_perf(true);
        app.quit();
    }
}

void RaceScreen::on_event(App& app, const SDL_Event& e) {
    if (stage_ == Stage::Drive) camera_.handle_event(e);
    // Mobile: going to the background pauses the race, like the console.
    if (e.type == SDL_EVENT_DID_ENTER_BACKGROUND && stage_ == Stage::Drive && !paused_) open_pause(app);
}

// Phones: driving touch controls while racing, menu controls otherwise (the quick menus
// run over the race stage: a GT race's field behind them is not a replay).
TouchLayout RaceScreen::touch_layout() const {
    return stage_ == Stage::Drive && !paused_ && !replaying_ && !quick_ ? TouchLayout::Driving : TouchLayout::Menu;
}

bool RaceScreen::follows_aspect() const {
    return launch_failed_ || stage_ == Stage::Drive || ((from_menu_ || online_) && error_.empty());
}

void RaceScreen::render(App& app, Ui& ui) {
    if (launch_failed_) {
        ui.fill(0, 0, ui.width(), ui.height(), Color{0, 0, 0, 255});
        ui.text_center(ui.height() / 2 - 16, "The race cannot start.", colors::kText);
        ui.text_center(ui.height() / 2, Ui::clip(error_, ui.width() - 40.0f), colors::kDim);
        ui.footer("Press any button to return");
        return;
    }
    if (stage_ == Stage::Drive) render_drive(app, ui);
    else if ((from_menu_ || online_) && error_.empty()) render_loading(app, ui);
    else render_select(app, ui);
}

// The loading screen (menus §6): the game keeps drawing the transition it left the menu with
// over the menu's last frame, every frame however long the load takes (with the "still
// loading" square from 3 s after the menu was left); the race then appears as a cut. Ours
// catches up the frames our blocking load took.
void RaceScreen::render_loading(App& app, Ui& ui) {
    SDL_Renderer* r = app.renderer();
    if (!loading_.valid() && !loading_own_) { // nothing handed over: the Fadeout on black
        loading_own_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 640, 448);
        if (loading_own_) {
            SDL_Texture* prev = SDL_GetRenderTarget(r);
            SDL_SetRenderTarget(r, loading_own_);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderClear(r);
            SDL_SetRenderTarget(r, prev);
            loading_.frame = loading_own_;
            loading_.transition.start(menu::flow::TransitionType::Fadeout);
        }
    }
    const u64 now = SDL_GetTicksNS();
    const int frames = loading_drawn_ ? menu::flow::LoadingPicture::frames_for(now - loading_ns_) : 1;
    loading_ns_ = now;
    loading_drawn_ = true;
    ui.fill(0, 0, ui.width(), ui.height(), Color{0, 0, 0, 255});
    if (!loading_.valid()) return;
    SDL_Texture* prev = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, loading_.frame);
    for (int i = 0; i < frames; ++i) loading_.transition.draw_frame(r, SDL_FRect{0, 0, 640, 448});
    SDL_SetRenderTarget(r, prev);
    const UiRect pic = ui.picture43(); // the menu's 4:3 picture in the middle
    const SDL_FRect dst{pic.x, pic.y, pic.w, pic.h};
    SDL_RenderTexture(r, loading_.frame, nullptr, &dst);
}

void RaceScreen::init(App& app) {
    init_ = true;
    app_ = &app;
    if (!app.vfs()) {
        error_ = "No disc loaded";
        return;
    }
    db_ok_ = db_.load(*app.vfs());
    if (!colors_.load(*app.vfs())) LOG_WARN("Race: database/carcolor.db not loaded (opponents in default paint)");
    if (!db_ok_) error_ = db_.player_error();
    for (const data::Course& c : db_.courses()) {
        if (c.label.rfind("L_", 0) == 0 || course_path(*app.vfs(), c.label).empty()) continue;
        courses_.push_back({c.label, c.name.empty() ? c.label : c.name});
    }
    if (courses_.empty())
        for (const char* l : {"trialmt", "deepforest", "grandvalley", "laguna", "midfield", "seattle", "rome"})
            if (!course_path(*app.vfs(), l).empty()) courses_.push_back({l, l});
    for (const data::Car& c : db_.cars()) {
        std::string model = db_.model_of(c);
        if (model.empty() || !app.vfs()->exists("cars/day/" + model)) continue;
        cars_.push_back({c.label, c.name.empty() ? c.label : c.name, model});
    }
    std::sort(cars_.begin(), cars_.end(), [](const CarEntry& a, const CarEntry& b) { return a.name < b.name; });
    auto find_course = [&](const std::string& l) {
        for (size_t i = 0; i < courses_.size(); ++i)
            if (courses_[i].label == l) return int(i);
        return -1;
    };
    auto find_car = [&](const std::string& l) {
        for (size_t i = 0; i < cars_.size(); ++i)
            if (cars_[i].label == l || cars_[i].model == l) return int(i);
        return -1;
    };
    if (from_menu_) resolve_launch();
    if (online_) resolve_online();
    if (!preset_.tt_event.empty())
        if (const data::Event* te = db_.event(preset_.tt_event)) { // the event's own layout (licence sections)
            preset_.course = te->course;
            solo_event_ = preset_.tt_event;
            if (license_times(*te).valid()) license_event_ = preset_.tt_event;
            if (preset_.tt_event == "M400" || preset_.tt_event == "M1000" || preset_.tt_event == "MMAX")
                machine_.emplace(int(te->laps)); // the event's test byte (presentation §B7)
            if (preset_.car.empty() && !te->entries.empty()) { // licence tests: the event's car
                // (gt_mode §21; in the game's language: docs/dev/GT_MODE.md "Language")
                const bool lic = license_times(*te).valid();
                const game::LicenseCar lc = game::license_car(db_, *te, db_.language());
                const std::string e0 = lic ? lc.entry : te->entries[0];
                if (lic) event_paint_id_ = lc.color;
                launch_source_ = ExactCarSource{e0};
                if (db_.car(e0)) preset_.car = e0;
                else if (const data::CarVariant* v = db_.variant(e0)) preset_.car = v->car;
            }
            bool listed = false;
            for (const CourseEntry& c : courses_) listed = listed || c.label == te->course;
            if (!listed) courses_.push_back({te->course, te->course});
        }
    course_sel_ = std::max(0, find_course(preset_.course.empty() ? "trialmt" : preset_.course));
    car_sel_ = std::max(0, find_car(preset_.car.empty() ? "r34_gtr_v_99" : preset_.car));
    controls_.config().automatic = !preset_.manual;
    // Title Options menu ([options]): Race BGM and 1P vibration.
    music_on_ = preset_.music && menu::flow::option_value(&app.config(), "race_bgm") != "off";
    // The quick menus' music follows the Menu BGM switch (music §3.3).
    menu_music_on_ = preset_.music && menu::flow::option_value(&app.config(), "menu_bgm") != "off";
    // The race playlist (the options' music list, music §2.3), kept in [options].
    music_.load_playlist(app.audio(), *app.vfs(), app.config().get("options", "music_playlist"));
    rumble_on_ = menu::flow::option_value(&app.config(), "vib1") != "off";
    rumble2_on_ = menu::flow::option_value(&app.config(), "vib2") != "off";
    // Options (the original's options record): race / replay SE, ghost, wide mode (2P split).
    race_se_ = menu::flow::option_value(&app.config(), "race_se") != "off";
    replay_se_ = menu::flow::option_value(&app.config(), "replay_se") != "off";
    // REPLAY OPTIONS (view mode, target, HUD items, sound; presentation §G).
    replay_settings_ = ReplaySettings::load(app.config());
    replay_se_ = replay_settings_.se;
    ghost_on_ = menu::flow::option_value(&app.config(), "ghost") != "off";
    wide_mode_ = menu::flow::option_value(&app.config(), "wide_mode") == "on";
    if (!preset_.event.empty()) race_event_ = preset_.event;
    if (!preset_.tt_event.empty())
        if (const data::Event* te = db_.event(preset_.tt_event)) tt_start_ = event_start(*te);
    if (preset_.two_player) two_player_ = true;
    if (preset_.laps > 0) race_laps_ = preset_.laps;
    camera_.set_orbit(preset_.oyaw, preset_.opitch, preset_.odist);
    show_perf_ = preset_.perf;
    camera_.set_view(g_last_view[0].mode, g_last_view[0].mirror);
    camera2_.set_view(g_last_view[1].mode, g_last_view[1].mirror);
    drive_camera_ = g_last_view[0].mode;
    if (preset_.cam >= 0) camera_.set_mode(CameraMode(std::clamp(preset_.cam, 0, int(CameraMode::Count) - 1)));
    if (from_menu_ || online_) begin_pending_ = true; // the course and cars on the next frame
    else begin(app);
}

void RaceScreen::begin(App& app) {
    // Mali Bifrost: what the render thread's warm-up has not built yet, in the load rather than
    // as stutters in the race.
    if (app.gpu()) app.gpu()->finish_warming();
    if (!preset_.replay.empty()) {
        std::vector<ReplayFile> files = list_replays(replay_dir(app));
        for (const ReplayFile& f : files)
            if (preset_.replay == "last" || f.name == preset_.replay || f.path == preset_.replay) {
                load_replay_file(app, f);
                if (logger_mode_ && stage_ == Stage::Drive) setup_logger(app);
                return;
            }
        error_ = "replay not found: " + preset_.replay;
        return;
    }
    if (preset_.have && !courses_.empty() && !cars_.empty()) start(app);
    if ((from_menu_ || online_) && stage_ != Stage::Drive) { // (it says why, then back to the menus)
        LOG_WARN("Race: could not start the menu's race: %s", error_.c_str());
        if (error_.empty()) error_ = "The race could not be set up.";
        launch_failed_ = true;
    }
}

void RaceScreen::toast(std::string s) {
    toast_ = std::move(s);
    toast_time_ = 3.0;
}

void RaceScreen::log_perf(bool force) {
    if (!course_scene_) return;
    if (force && perf_.frames > 0) perf_.window = 1e9;
    if (!perf_.roll()) return;
    const render::DrawStats& ds = view_ ? view_->stats() : render::DrawStats{};
    LOG_INFO("Race perf [%s]: frame %.2f ms avg (%.0f fps), worst %.2f ms; scene draw (CPU) %.2f ms; "
             "%d meshes drawn, %d culled, %d draw calls, %zu triangles; %d voices",
             course_name_.c_str(), perf_.avg_frame, 1000.0 / std::max(perf_.avg_frame, 0.01), perf_.peak_frame,
             perf_.avg_draw, ds.meshes_drawn, ds.meshes_culled, ds.draw_calls, ds.triangles,
             app_ ? app_->audio().voices().active_voices() : 0);
}

} // namespace gt3r::race_screen

namespace gt3r {

using race_screen::MenuLaunch;
using race_screen::RaceScreen;

// Races requested by the interactive menus start the time trial directly; the
// menus resume when the race screen is popped.
static const bool race_menu_hook = [] {
    menu::flow::set_game_request_handler([](const menu::flow::GameRequest& r, App& app) {
        if (r.kind != menu::flow::GameRequest::Kind::StartRace) return false;
        if (r.mode == "online_battle") { // i.LINK Battle: the online lobby (docs/dev/ONLINE.md)
            app.push(make_online_lobby_screen());
            return true;
        }
        static const char* const kSupported[] = {"time_trial", "free_run",       "single_race",     "two_player_battle",
                                                 "go_race",    "go_free_run",    "go_license",      "go_machine_test",
                                                 "go_preview",  "attract_demo",   "theater_replay",  "theater_demo",
                                                 "logger"};
        bool ok = false;
        for (const char* m : kSupported) ok = ok || r.mode == m;
        if (!ok) return false;
        app.push(std::make_unique<RaceScreen>(MenuLaunch{r}));
        return true;
    });
    return true;
}();

std::unique_ptr<Screen> make_online_race_screen(const OnlineLaunch& l) { return std::make_unique<RaceScreen>(l); }

std::unique_ptr<Screen> make_menu_race_screen(const menu::flow::GameRequest& r) {
    return std::make_unique<RaceScreen>(MenuLaunch{r});
}

GT3R_REGISTER_PLAYER_SCREEN(race, "race", "Time trial", "Any car on any course, nothing to unlock; saved replays too.", 8,
                     []() -> std::unique_ptr<Screen> {
                         // GT3R_RACE="theater=AT010001": the Replay Theater's demonstration of an event.
                         if (const char* env = SDL_getenv("GT3R_RACE")) {
                             std::string s = std::string(",") + env + ",";
                             size_t at = s.find(",theater=");
                             if (at != std::string::npos) {
                                 menu::flow::GameRequest r;
                                 r.kind = menu::flow::GameRequest::Kind::StartRace;
                                 r.mode = "theater_demo";
                                 r.event = s.substr(at + 9, s.find(',', at + 9) - at - 9);
                                 return std::make_unique<RaceScreen>(MenuLaunch{r});
                             }
                         }
                         return std::make_unique<RaceScreen>();
                     });

} // namespace gt3r
