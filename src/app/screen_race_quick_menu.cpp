// GT3's quick menus around a run (pre-race and result: layout per mode, entry list,
// Top 3, licence page) and their item actions, the free run before a GT race, and our
// panels opened from them (options, settings, records, the session's data logger).
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"
#include "sim/exact/race.h"

namespace gt3r::race_screen {

// Free run before a GT race: the course alone (no opponents, no laps); leaving it
// ("Exit" in the pause menu) returns to the pre-race menu with the field rebuilt.
void RaceScreen::start_free_run(App& app) {
    // Free practice starts the player's car in its pit box on the course's stop line (GT3's free practice start). GT3 offers
    // it only where there is one: its pre-race menu turns FREE RUN off on a dirt course (QuickMenuItems::visit), and every
    // course of GT mode's races without a stop line is one; a course without a pit box (none of GT mode's) refuses it here.
    {
        std::shared_ptr<const sim::exact::RaceCourse> c;
        if (load_program_data(app)) try {
                c = exact_course(course_bytes_, courses_[size_t(course_sel_)].label, *exact_data_);
            } catch (const std::exception& e) {
                LOG_WARN("Race: free run: %s", e.what());
            }
        if (!c || !exact_has_pit_box(*c)) {
            LOG_WARN("Race: free run refused: the course's stop line has no pit box");
            toast("Free run is not available on this course.");
            return;
        }
    }
    free_run_ = true;
    quick_ = prerace_ = false;
    race_mode_ = false;
    int laps = race_laps_;
    build_vehicle(app);
    race_laps_ = 0;
    free_run_laps_ = laps;
    restart();
}

void RaceScreen::end_free_run(App& app) {
    free_run_ = false;
    race_mode_ = true;
    build_vehicle(app);
    race_laps_ = free_run_laps_;
    if (field_) field_->set_laps(race_laps_);
    restart();
    open_prerace_menu(app);
}

// The quick-menu layout of this run, as the original picks it per mode (arcade
// race, free run, time trial, machine test, licence, GT race, championship, 2P;
// menus §11).
// `before`: the menu before the first run (licence, machine test, GT race, time trial,
// free run); the arcade race and 2P battle run first.
std::string RaceScreen::quick_menu_layout(bool before) const {
    const menu::flow::GameRequest& rq = launch_.request;
    if (!license_event_.empty()) return "qm_gt_license1.pmb";
    if (two_player_) return "qm_arc_2p_result1.pmb";
    if (from_menu_ && rq.mode == "go_machine_test")
        return rq.race_code == "M1000" ? "qm_gt_mt_10001.pmb" : rq.race_code == "MMAX" ? "qm_gt_mt_max1.pmb"
                                                                                        : "qm_gt_mt_4001.pmb";
    if (!field_) return race_laps_ > 0 ? "qm_arc_tt_ltd1.pmb" : "qm_arc_tt_free1.pmb";
    if (before) return championship() ? "qm_gt_event_champ1.pmb" : "qm_gt_event1.pmb";
    if (championship()) return "qm_gt_result_champ1.pmb";
    if ((from_menu_ && rq.mode.rfind("go_", 0) == 0) || preset_.gt_result) return "qm_gt_result1.pmb";
    return "qm_arc_result1.pmb";
}

// Whether this run opens with its quick menu (quick_menu_layout(true)).
bool RaceScreen::menu_before_run() const {
    const std::string& m = launch_.request.mode;
    return m == "go_race" || m == "go_machine_test" || m == "go_license" || m == "go_free_run" ||
           m == "time_trial" || m == "free_run";
}

// Licence quick menu: the test's name, target times and description panel, and the
// preview box (the disc's demonstration before a run, the run after it).
void RaceScreen::fill_license_menu(App& app) {
    start_preview();
    quick_menu_.set_text("qm_license_name", license_title(license_event_));
    std::string page = license_event_;
    for (char& c : page) c = char(std::tolower(static_cast<unsigned char>(c)));
    quick_menu_.add_page_layout(*app.vfs(), app.renderer(), page, lang_); // the test's description panel
    static const char* const kW[] = {"qm_license_time_gold", "qm_license_time_silver", "qm_license_time_bronz"};
    for (int i = 0; i < 3; ++i)
        if (license_times_.ms[size_t(i)] > 0) quick_menu_.set_text(kW[i], menu_time_text(u32(license_times_.ms[size_t(i)])));
}

// The menus' free runs on a course (the arcade time attack, GT Mode's free run): the original's
// free-run driver, whose runs go into the course's time-trial ranking.
bool RaceScreen::course_time_trial() const {
    const std::string& m = launch_.request.mode;
    return from_menu_ && !field_ && (m == "free_run" || m == "go_free_run");
}

// The GT Mode ranking this run's records go to (licence, machine test, arcade time trial, the
// course's time trial for the arcade time attack and GT Mode's free run), or "" (Quick race
// practice, races).
std::string RaceScreen::records_key() const {
    const menu::flow::GameRequest& rq = launch_.request;
    if (!from_menu_ || rq.practice) return {};
    if (!license_event_.empty()) return "lic:" + license_event_;
    if (rq.mode == "go_machine_test") return "mt:" + rq.race_code;
    if (course_time_trial()) return "ct:" + courses_[size_t(course_sel_)].label;
    if (rq.mode == "time_trial") {
        // Arcade time trials: the challenge's code, or the one of its course ("tt01" -> AT010001).
        std::string code = rq.race_code;
        if (game::GtMode::time_trial_index(code) < 0)
            if (int i = game::GtMode::time_trial_index(rq.course); i >= 0) {
                char b[16];
                std::snprintf(b, sizeof b, "AT%02d0001", i + 1);
                code = b;
            }
        if (!code.empty()) return "at:" + code;
    }
    return {};
}

std::vector<double> RaceScreen::stored_records() const {
    std::vector<double> out;
    const std::string key = records_key();
    game::GtModeSession* gs = game::gt_mode_session();
    if (key.empty() || !gs) return out;
    for (const game::RankEntry& e : gs->gt().ranking_top(key, size_t(kRecordPlaces))) out.push_back(e.value);
    return out;
}

// At the finish (presentation §B7, §E3): time trials and machine tests take their place
// in the stored records for the result overlay (a run without stored records ranks as in an
// empty table; ours: debug runs and Quick race practice keep none). The session record check:
// a time at or under the session's best lap before it greys RETRY out until the next run
// (licence tests only with a medal); licence and machine-test times then count as the
// session's laps (time trials' come with their laps, lap_events). `result`: licence tests
// 1..3 the medal, 0 none; other runs -2.
void RaceScreen::rank_finish(double value, bool speed, int result) {
    const bool license = !license_event_.empty();
    if (!license) finish_record_rank_ = record_rank(stored_records(), value, speed);
    RecordFinish rf;
    rf.replay = replaying_ || demo_;
    rf.failed = tt_failed_;
    rf.time = speed ? -1.0 : value; // the top-speed test is never checked
    rf.session_best = best_splits_.best_lap() >= 0 ? double(best_splits_.best_lap()) / 1000.0 : -1.0;
    rf.result = result;
    if (locks_retry(rf)) retry_locked_ = true;
    // The quick menu's SAVE GHOST / DATA LOGGER (menus §11; a free run's laps count in
    // lap_events: its EXIT as the finish checks nothing).
    if (sets_record(rf) && run_kind() != RunKind::FreeRun) run_session_record_ = true;
    if ((license || machine_) && !rf.replay && rf.time >= 0) best_splits_.lap_done(std::llround(rf.time * 1000.0));
    LOG_INFO("Race: finish %.3f, record place %d%s%s", value, finish_record_rank_ + 1,
             sets_record(rf) ? ", session record" : "", retry_locked_ ? ", RETRY locked" : "");
}

bool RaceScreen::record_run() {
    const menu::flow::GameRequest& rq = launch_.request;
    if (!from_menu_ || rq.practice || run_recorded_ || replaying_ || demo_) return false;
    int rank = -1;
    if (!license_event_.empty()) { // a licence run with a medal (presentation §A5)
        if (rq.mode != "go_license" || license_.state() != LicenseRun::State::Finished || shown_medal_ < 0) return false;
        rank = game::add_license_record(rq.race_code, license_.time());
    } else if (machine_ && rq.mode == "go_machine_test") { // the value rank_finish placed
        // (The top-speed test also when its pause EXIT was the finish: no time to the goal.)
        const double v = machine_->timed() ? machine_time_ : double(top_speed_kmh_);
        if (!finished_ || v <= 0 || (machine_time_ <= 0 && !exit_finished_)) return false;
        rank = game::add_machine_test_record(rq.race_code, v);
    } else if (rq.mode == "time_trial" && !field_ && race_laps_ > 0) { // a challenge: its best lap
        const std::string key = records_key();
        if (!finished_ || tt_failed_ || track_->best_lap_time() <= 0 || key.rfind("at:", 0) != 0) return false;
        rank = game::add_time_trial_record(key.substr(3), track_->best_lap_time());
    } else if (course_time_trial()) { // a free run, however it ended: its best lap, if any (gt_mode §17)
        if (track_->best_lap_time() <= 0) return false;
        rank = game::add_course_time_trial_record(courses_[size_t(course_sel_)].label, track_->best_lap_time(),
                                                  cars_[size_t(car_sel_)].label);
    } else {
        return false;
    }
    run_recorded_ = any_run_recorded_ = true;
    return rank >= 0;
}

// Time trial / machine test quick menus: "Top 3" = entries 0..2 of the ranking, read again
// before every quick menu, so the list shows before the first run and after each (menus §11).
// The three rows are always filled: a ranking with fewer entries shows empty ones (no name,
// no time, a speed of 0), as the original's tables hold 12 entries from a new game.
void RaceScreen::fill_top3() {
    const menu::flow::GameRequest& rq = launch_.request;
    if (!license_event_.empty() || (rq.mode != "go_machine_test" && rq.mode != "time_trial" && !course_time_trial()))
        return;
    std::string key = records_key();
    game::GtModeSession* gs = game::gt_mode_session();
    if (key.empty() || !gs) return;
    const game::GtMode& gt = gs->gt();
    std::vector<game::RankEntry> top = gt.ranking_top(key, 3);
    for (size_t k = 0; k < 3; ++k) {
        const std::string n = std::to_string(k + 1);
        const game::RankEntry* e = k < top.size() ? &top[k] : nullptr;
        // Speed: "%4.1f %s" in the Options unit (rounded; " 0.0 km/h" for an empty entry).
        const float kmh = e ? float(e->value) : 0.0f;
        char speed[32];
        std::snprintf(speed, sizeof speed, "%4.1f %s", double(imperial_ ? kmh * 0.62139f : kmh), imperial_ ? "mph" : "km/h");
        quick_menu_.set_class_text("qm_rank_name" + n, e ? gt.ranking_name(*e) : std::string());
        // (The free time trial's list: the entry's car, "---" without one.)
        quick_menu_.set_class_text("qm_list_car_name" + n, e && !e->car.empty() ? car_display_name(e->car) : "---");
        quick_menu_.set_class_text("qm_rank_time" + n, e ? menu_time_text(e->value) : std::string(kNoTimeText));
        quick_menu_.set_class_text("qm_rank_speed" + n, speed);
    }
}

bool RaceScreen::dirt_course() const {
    for (const data::Course& c : db_.courses())
        if (c.label == courses_[size_t(course_sel_)].label) return c.surface == 2;
    return false;
}

void RaceScreen::update_quick_menu_items() {
    race::QuickMenuItems q = qm_items_;
    q.visit(run_kind(), launch_.request.car_parts.has_value(), dirt_course());
    quick_menu_.set_items(q);
}

void RaceScreen::open_prerace_menu(App& app) {
    quick_ = true;
    prerace_ = true;
    silence(app);
    std::string pmb = quick_menu_layout(true);
    if (!quick_menu_.load(*app.vfs(), app.renderer(), hud_.fonts(), pmb, lang_)) {
        quick_ = prerace_ = false; // no layout: straight to the race
        return;
    }
    update_quick_menu_items();
    if (!license_event_.empty()) {
        fill_license_menu(app);
        return;
    }
    // The entry list (the capture of GT3's RACE MENU): every car, no times yet ("no time").
    if (field_) {
        std::vector<ResultRow> rows = result_rows();
        for (ResultRow& r : rows) r.time = kNoTimeText;
        quick_menu_.set_rows(std::move(rows));
    }
    if (race_laps_ > 0) quick_menu_.set_text("laps", std::to_string(race_laps_) + (race_laps_ == 1 ? " Lap" : " Laps"));
    fill_top3();
    start_menu_backdrop(true);
}

void RaceScreen::open_quick_menu(App& app) {
    prerace_ = false;
    finish_menu_shown_ = true;
    quick_ = true;
    silence(app);
    std::string pmb = quick_menu_layout(false);
    if (!quick_menu_.load(*app.vfs(), app.renderer(), hud_.fonts(), pmb, lang_)) quick_menu_.load(*app.vfs(), app.renderer(), hud_.fonts(), "qm_arc_result1.pmb", lang_);
    update_quick_menu_items();
    // The live race's rows: after a replay (a GT race's own, or the 2P one) the field is the
    // replay's.
    std::vector<ResultRow> rows = latched_rows_ ? *latched_rows_ : result_menu_rows();
    for (const ResultRow& r : rows)
        LOG_INFO("Race result: %-40s %s", text::game_text_to_utf8(r.name, hud_.fonts().jis()).c_str(), r.time.c_str());
    quick_menu_.set_rows(std::move(rows));
    if (!license_event_.empty()) fill_license_menu(app); // the test's name and target times
    fill_top3();
    start_menu_backdrop(false);
}

void RaceScreen::update_quick_menu(App& app, double dt) {
    if (panel_ != Panel::None) {
        update_panel(app);
        return;
    }
    // Back from an item that kept the menu (save, load ghost, a dialog): the original's driver
    // loop starts the race behind it again.
    if (!backdrop_ && !preview_) start_menu_backdrop(prerace_);
    if (!quick_sounds_tried_) {
        quick_sounds_tried_ = true;
        quick_sounds_.load(*app.vfs());
        quick_sounds_.set_enabled(menu::flow::option_value(&app.config(), "menu_se") != "off");
    }
    // A run (a free run's lap) better than the ghost with the Ghost option off: the menu opens
    // on "Update Ghost?" (presentation §G11; once its root page can take the popup).
    if (ghost_ask_ && quick_menu_.on_root() && quick_menu_.open_page("ghost_dlg")) ghost_ask_ = false;
    QuickMenu::Choice choice = quick_menu_.update(menu_keys(app.input().pad(0)), dt);
    for (const std::string& se : quick_menu_.take_sounds()) quick_sounds_.play(app.audio().voices(), se);
    if (!preset_.qm.empty() && quick_menu_.focus(preset_.qm)) { // scripted (screenshots)
        if (quick_menu_.item_enabled(preset_.qm)) {
            choice = {preset_.qm, "root", quick_menu_.action(preset_.qm)};
        } else { // refused as a confirm on it would be (menus §11)
            quick_sounds_.play(app.audio().voices(), "disable");
            LOG_INFO("Quick menu: %s is disabled", preset_.qm.c_str());
        }
    }
    preset_.qm.clear();
    if (choice.empty()) return;
    const std::string& a = choice.item;
    const QmAction act = quick_menu_action(choice.action);
    if (choice.page == "root" && act != QmAction::Option && act != QmAction::Records && act != QmAction::Logger) {
        if (preview_) stop_preview(app);
        stop_backdrop(app); // the race behind the menu
    }
    if (choice.page != "root") { // yes/no dialogs (the popup has closed itself)
        if (choice.page == "exit_sure" && act == QmAction::Exit) { // "Retire from championship?" Yes
            abandoned_ = true;
            quick_ = false;
            leave(app);
        } else if (choice.page == "ghost_dlg" && act == QmAction::Yes) { // "Update Ghost?"
            take_best_run_ghost();
        }
        return;
    }
    switch (act) {
    case QmAction::Start:
        quick_ = false;
        prerace_ = false;
        restart();
        break;
    case QmAction::FreeRun: start_free_run(app); break;
    case QmAction::Replay:
        quick_ = false;
        watch_replay();
        replay_to_menu_ = replaying_; // its end (or EXIT): the transition, then this menu again
        if (!replaying_) quick_ = true;
        break;
    case QmAction::Demo: // its run replaces the last one: no REPLAY until the next run
        qm_items_.after_demonstration();
        update_quick_menu_items();
        start_demo();
        break;
    case QmAction::SaveReplay: save_current_replay(); break;
    case QmAction::SaveGhost:
        // The session's best run (a free run: the run of its best lap), the ghost's source, not
        // the last run.
        if (best_run_replay_) {
            std::optional<std::string> path = save_replay(*best_run_replay_, replay_dir(app));
            toast(path ? "Replay saved" : "Could not save the replay");
        } else {
            save_current_replay();
        }
        break;
    case QmAction::LoadGhost: // at once, no question (the original's LOAD GHOST)
        toast(load_saved_ghost() ? "Ghost: " + Hud::format_time(ghost_time_)
                                 : whole_run_ghost() ? "No saved run" : "No saved best lap");
        break;
    case QmAction::Records: {
        // The game's records (GT mode keeps them from the results) for licences, machine
        // tests and arcade time trials; else the best laps of the saved replays.
        std::string key = records_key(); // (Quick race practice: not the GT Mode save's)
        if (!key.empty()) {
            app.push(make_records_screen(key));
            return;
        }
        open_records(app);
        break;
    }
    case QmAction::Logger:
        open_session_analyzer(app);
        panel_ = Panel::Logger;
        break;
    case QmAction::Option:
        panel_ = Panel::Options;
        panel_sel_ = 0;
        break;
    case QmAction::Setting:
        if (from_menu_ && launch_.request.mode.rfind("go_", 0) == 0 && launch_.request.car_parts && !launch_.request.practice) {
            // GT mode: the game's car settings menu for the ride car; rebuilt when it closes.
            app.push(make_gt_setting_screen(launch_.request.event.empty() ? launch_.request.race_code
                                                                          : launch_.request.event));
            setting_pending_ = true;
            return;
        }
        panel_ = Panel::Settings;
        panel_sel_ = 0;
        break;
    case QmAction::NextSession:
        quick_ = false;
        leave(app); // GT mode chains the next round
        break;
    case QmAction::SaveGame:
        // The save page (Save game); the save includes this round (the original
        // stores the championship's progress around its save).
        if (from_menu_ && championship() && !launch_.request.practice) {
            app.push(make_game_save_screen(result_latch_.for_leave(build_result())));
            return;
        }
        break;
    case QmAction::Exit: {
        std::string link = quick_menu_.link(a);
        if (!link.empty() && quick_menu_.open_page(link)) return; // "Retire from championship?"
        quick_ = false;
        leave(app);
        break;
    }
    default:
        // An item whose .mbl link opens a page of the layout (championship Exit ->
        // "exit_sure" when chosen by a script).
        if (std::string link = quick_menu_.link(a); !link.empty() && quick_menu_.open_page(link)) return;
        LOG_WARN("Quick menu: item %s has no action (%s)", a.c_str(), choice.action.c_str());
        break;
    }
}

std::vector<OptionRow> RaceScreen::option_rows() const {
    std::vector<OptionRow> rows;
    rows.push_back({"Transmission", controls_.config().automatic ? "AT" : "MT"});
    if (panel_ == Panel::Settings) return rows;
    rows.push_back({"Camera", camera_mode_name(drive_camera_)});
    rows.push_back({"Music", music_on_ ? "ON" : "OFF"});
    rows.push_back({"Vibration", rumble_on_ ? "ON" : "OFF"});
    rows.push_back({"Display", preset_.hud ? "ON" : "OFF"});
    if (two_player_) rows.push_back({"2P split", split_horizontal(*app_) ? "Horizontal" : "Vertical"});
    return rows;
}

void RaceScreen::change_option(App& app, int row, int dir) {
    switch (row) {
    case 0: controls_.config().automatic = !controls_.config().automatic; break;
    case 1: {
        int n = int(CameraMode::Tv); // drive cameras: Chase .. Orbit
        drive_camera_ = CameraMode((int(drive_camera_) + dir + n) % n);
        if (!replaying_) camera_.set_mode(drive_camera_);
        break;
    }
    case 2:
        music_on_ = !music_on_; // music_cues() starts / stops the music
        break;
    case 3: rumble_on_ = !rumble_on_; break;
    case 4: preset_.hud = !preset_.hud; break;
    case 5: preset_.split = split_horizontal(app) ? "vertical" : "horizontal"; break;
    default: break;
    }
}

void RaceScreen::update_panel(App& app) {
    const MenuInput& m = app.menu();
    if (panel_ == Panel::Logger) { // the Analyzer (GRAPH) over the session's laps
        bool open = analyzer_.tick(menu_keys(app.input().pad(0)));
        for (const std::string& se : analyzer_.take_sounds()) quick_sounds_.play(app.audio().voices(), se);
        if (!open) panel_ = Panel::None;
        return;
    }
    if (m.pressed(PadButton::Circle) || m.pressed(PadButton::Triangle) ||
        (panel_ == Panel::Records && m.pressed(PadButton::Cross))) {
        panel_ = Panel::None;
        return;
    }
    if (panel_ == Panel::Options || panel_ == Panel::Settings) {
        int n = int(option_rows().size());
        panel_sel_ = std::clamp(panel_sel_ + m.vertical(), 0, n - 1);
        int h = m.horizontal();
        if (h || m.pressed(PadButton::Cross)) change_option(app, panel_sel_, h ? h : 1);
    }
}

void RaceScreen::open_records(App& app) {
    records_ = course_records(replay_dir(app), courses_[size_t(course_sel_)].label);
    record_names_.clear();
    for (const RecordEntry& r : records_) record_names_.push_back(car_display_name(r.car));
    if (!records_loaded_) records_loaded_ = records_panel_.load(*app.vfs(), app.renderer(), hud_.fonts(), lang_) || true;
    panel_ = Panel::Records;
}

void RaceScreen::draw_panel(App& app, const SDL_FRect& dst) {
    SDL_Renderer* r = app.renderer();
    switch (panel_) {
    case Panel::Records:
        // The course table's name is game text ("C\x81\xF4te d\xA1\xC7Azur").
        records_panel_.draw(r, hud_.fonts(), records_, record_names_,
                            text::game_text_to_utf8(course_name_, hud_.fonts().jis()), dst);
        break;
    case Panel::Logger:
        analyzer_.draw(r, dst);
        break;
    case Panel::Options:
    case Panel::Settings:
        draw_option_list(r, hud_.fonts(), panel_ == Panel::Options ? "OPTIONS" : "SETTINGS", option_rows(), panel_sel_,
                         "Up/Down select   Left/Right change   Circle back", dst);
        break;
    case Panel::None: break;
    }
}

} // namespace gt3r::race_screen
