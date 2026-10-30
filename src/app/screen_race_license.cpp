// Licence tests and demonstrations: the test's rules and its result as the race flow judges it (LicenseRun), the medal,
// and the disc's demonstration runs (quick menu DEMO, the licence menu's preview box).
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"
#include "game/language_rules.h"

namespace gt3r::race_screen {

sim::ControlInputs RaceScreen::demo_inputs(size_t i) const {
    sim::ControlInputs in = disc_demo_->inputs(i);
    in.steer = kDemoSteerSign * in.steer;
    return in;
}

void RaceScreen::start_demo() {
    if (!track_ || !vehicle_) return;
    pre_demo_run_ = finished_recording();
    quick_ = false;
    demo_ = true; // (before the race is built: the exact core plays the demonstration's record, with NTSC-U's rules)
    restart();
    demo_tick_ = 0, demo_logged_ = false;
    // Without the disc's demonstration: the race's own AI drives the player's car.
    if (vehicle_ && !disc_demo_) vehicle_->ai_take_over();
    drive_camera_ = camera_.mode() == CameraMode::Tv ? drive_camera_ : camera_.mode();
    camera_.set_mode(CameraMode::Tv);
    restart_director();
}

// Licence quick menu: the demonstration runs live in the preview box behind the menu.
void RaceScreen::start_preview() {
    if (!track_ || !vehicle_ || preview_) return;
    // After a run, GT3's licence menu replays that run in the box, looping
    // (presentation.md A7); with no run, or after one left with EXIT (the driver loads the
    // demonstration in its place, §A8), the disc's demonstration.
    if (finish_menu_shown_ && recorder_.tick() > 0 && last_run_ended_) {
        pre_demo_run_ = finished_recording();
        preview_ = run_preview_ = true;
        start_playback(*pre_demo_run_);
        quick_ = finish_menu_shown_ = true; // (the playback resets the run state)
        return;
    }
    if (!disc_demo_) return;
    pre_demo_run_ = finished_recording();
    demo_ = true, preview_ = true; // (before the race is built, as start_demo)
    restart();
    quick_ = true;
    demo_tick_ = 0, demo_logged_ = false;
    drive_camera_ = camera_.mode() == CameraMode::Tv ? drive_camera_ : camera_.mode();
    camera_.set_mode(CameraMode::Tv);
    restart_director();
}

void RaceScreen::stop_preview(App& app) {
    if (!preview_) return;
    if (run_preview_) {
        run_preview_ = preview_ = false;
        replaying_ = false;
        bool q = quick_;
        restart();
        quick_ = q;
        camera_.set_mode(drive_camera_);
        restore_pre_demo_run();
        silence(app);
        return;
    }
    preview_ = false;
    demo_ = false;
    bool q = quick_;
    restart();
    quick_ = q;
    camera_.set_mode(drive_camera_);
    restore_pre_demo_run();
    silence(app);
}

void RaceScreen::end_demo(App& app) {
    demo_ = false;
    restart();
    camera_.set_mode(drive_camera_);
    restore_pre_demo_run();
    silence(app);
    quick_ = true;
}

// "L0B0003" -> "B-3", "LIA0007" -> "IA-7".
std::string RaceScreen::license_title(const std::string& code) {
    if (code.size() < 7) return code;
    std::string cls = code.substr(1, 2);
    if (cls[0] == '0') cls = cls.substr(1);
    return cls + "-" + std::to_string(std::atoi(code.substr(3).c_str()));
}

void RaceScreen::begin_license() {
    license_times_ = {};
    const std::string& demo_code = license_event_.empty() ? theater_demo_ : license_event_;
    if (!demo_code.empty() && !disc_demo_tried_ && app_ && app_->vfs()) { // the disc's demonstration run
        disc_demo_tried_ = true;
        std::string err;
        // The language's demonstration (docs/dev/GT_MODE.md "Language": S-1's in British English).
        const Vfs& vfs = *app_->vfs();
        const std::string path =
            game::demo_replay_path(demo_code, vfs.language(), [&](const std::string& p) { return vfs.exists(p); });
        if (std::optional<Bytes> b = vfs.read(path)) disc_demo_ = DemoReplay::parse(*b, &err);
        if (disc_demo_) LOG_INFO("Demo %s: %zu frames, %.3f s", demo_code.c_str(), disc_demo_->size(),
                                 disc_demo_->time_ms / 1000.0);
        else LOG_WARN("Demo %s: no demo run (%s)", demo_code.c_str(), err.c_str());
        // The race GT3's replay playback builds from the save, played like a replay.
        const data::Event* dev = db_.event(demo_code);
        if (disc_demo_ && dev && load_program_data(*app_))
            if (std::optional<sim::exact::RaceRecord> r = race::demo_record(*disc_demo_, *dev, db_, exact_data_->fingerprint(), 180, &err))
                demo_record_ = std::make_shared<const sim::exact::RaceRecord>(std::move(*r));
    }
    if (license_event_.empty()) return;
    const data::Event* ev = db_.event(license_event_);
    if (!ev) return;
    license_times_ = license_times(*ev);
    LicenseRun::Rules r;
    r.kind = license_times_.kind;
    r.laps = std::max<int>(1, ev->laps);
    r.standing = !tt_start_.flying();
    license_.begin(r);
}

// The licence run is over (finished with `time` and `medal`, -1 none, or failed): the result, the medal's model, GT mode's
// save.
void RaceScreen::license_end(LicenseRun::State r, double time, int medal, const std::string& failure) {
    license_.end(r, time, failure);
    finished_ = true;
    finish_position_ = r == LicenseRun::State::Finished ? 1 : 0;
    music_license_end_s_ = race_time_, music_license_failed_ = race::license_fail_jingle(r, medal); // the replay's end cue
    if (r == LicenseRun::State::Finished) {
        LOG_INFO("Licence %s: finished in %.3f s, medal %d", license_event_.c_str(), time, medal);
        rank_finish(time, false, medal >= 0 ? medal + 1 : 0); // a record with a medal locks RETRY (presentation §E3)
        // The session's best judged run is what GT mode records (a retry or a replay
        // afterwards must not lose a medal already earned).
        auto rank = [](int m) { return m >= 0 ? m : 3; };
        if (!demo_ && (!best_license_ || rank(medal) < rank(best_license_->medal) ||
                       (rank(medal) == rank(best_license_->medal) && time < best_license_->time)))
            best_license_ = BestLicense{medal, time};
        // GT mode: the medal goes into the save at once (also kept if the game is closed
        // before returning to the menus).
        if (!demo_ && from_menu_ && launch_.request.mode == "go_license" && !launch_.request.practice && medal >= 0)
            game::save_license_run(license_event_, medal, time);
        if (preset_.prize >= 0) medal = std::min(preset_.prize, 2); // screenshots: show this medal
        shown_medal_ = medal;
        // The medal model (race/gold|silver|bronze.prm) flies in on the result screen.
        static const char* const kPrm[] = {"race/gold.prm", "race/silver.prm", "race/bronze.prm"};
        prize_.reset();
        if (medal >= 0 && app_ && app_->gpu() && app_->vfs())
            if (std::optional<Bytes> b = app_->vfs()->read(kPrm[medal])) {
                prize_ = std::make_unique<render::PrizeView>(*app_->gpu());
                if (!prize_->load(*b, kPrm[medal])) prize_.reset();
            }
    }
    else LOG_INFO("Licence %s: FAIL (%s)", license_event_.c_str(), failure.c_str());
}

} // namespace gt3r::race_screen
