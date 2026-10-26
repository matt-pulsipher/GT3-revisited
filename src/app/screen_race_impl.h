#pragma once

// Private to the race screen: the RaceScreen class, split over the screen_race*.cpp files
// (one file per concern; the method declarations below are grouped by the file that
// defines them). The race screen code map is in docs/dev/RACE.md; the public entry points
// are in screen_race.h and screen_online.h.

#include "app/app.h"
#include "app/screen_gtmode.h"
#include "app/screen_touch_overlay.h"
#include "app/screen_registry.h"
#include "app/screen_online.h"
#include "app/screen_race.h"
#include "menu/flow/game_hooks.h"
#include "menu/flow/options.h"
#include "menu/flow/menu_sound.h"
#include "menu/flow/screen_transition.h"
#include "audio/engine_sound.h"
#include "audio/motor_sound.h"
#include "base/file.h"
#include "base/log.h"
#include "base/hitch.h"
#include "formats/model/wheel_spec.h"
#include "ai/ai_driver.h"
#include "ai/ai_rules.h"
#include "game/gt_mode_menu.h"
#include "game/gt_save.h"
#include "sim/models.h"
#include "game/data/car_database.h"
#include "race/backend.h"
#include "race/camera_rig.h"
#include "race/controls.h"
#include "race/field.h"
#include "race/car_sfx.h"
#include "race/ghost.h"
#include "audio/course_ambience.h"
#include "formats/sound/crss.h"
#include "race/demo_replay.h"
#include "render/prize_view.h"
#include "race/license.h"
#include "race/machine_test.h"
#include "track/track.h"
#include "race/pit.h"
#include "race/post_run.h"
#include "race/billboards.h"
#include "race/lamp_glows.h"
#include "race/race_panels.h"
#include "race/sun_glare.h"
#include "race/results.h"
#include "race/pause_menu.h"
#include "race/paint.h"
#include "race/pool_draw.h"
#include "race/hud.h"
#include "race/hud_clocks.h"
#include "race/interp.h"
#include "race/race_audio.h"
#include "race/race_scene.h"
#include "race/arcade_car.h"
#include "race/point_lights.h"
#include "formats/text/mltx.h"
#include "race/pylons.h"
#include "race/tyre_smoke.h"
#include "race/wet_road.h"
#include "race/road_shade.h"
#include "race/replay.h"
#include "race/replay_options.h"
#include "race/replay_focus.h"
#include "race/analyzer.h"
#include "race/replay_library.h"
#include "race/net_race.h"
#include "game/net_car.h"
#include "net/session.h"
#include "race/replay_cameras.h"
#include "formats/course/course_file.h"
#include "render/view3d.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gt3r::race_screen {

using namespace race;

// GT3 keeps each player's last change-view choice for the next race (the options object,
// written every frame by the camera manager); a new game starts in the in-car view.
struct LastView {
    race::CameraMode mode = race::CameraMode::Bumper;
    bool mirror = true;
};
extern LastView g_last_view[2];

// The menus' logical keys from a pad (menus §2): confirm = Circle or Cross,
// cancel = Triangle or Square, the left stick doubles the D-pad.
u16 menu_keys(const PadState& pad);

struct CourseEntry {
    std::string label, name;
};
struct CarEntry {
    std::string label, name, model;
};

struct DebugPreset {
    std::string course, car;
    int cam = -1, paint = 0;
    bool autodrive = false, manual = false, have = false, hud = true, perf = false, gpusync = false, sheen = true, pause = false;
    int pause_sel = 0;       // pause=1: the cursor on this button (screenshots)
    bool prerace = false;    // open GT mode's pre-race quick menu (screenshots)
    bool gt_result = false;  // gtq=1: the GT-mode result quick menu (screenshots)
    bool watch = false, save = false, ghost = true, music = true;
    double warp = 0, quit = 0, rwarp = 0;
    double lic_finish = 0;   // tests: a running licence test finishes at this elapsed time (forced result)
    bool demo_drive = false; // tests: the player's run driven by the disc's demo inputs (judged as a real run)
    double leave = 0; // tests: back to the menus this many seconds after the finish (posts the result)
    int exit_laps = 0; // screenshots: a run ends as the pause menu's Exit once it has this many laps
    std::string replay; // file to watch ("last" = newest)
    std::string event;  // race against the event's opponents (e.g. AEAT0001)
    bool two_player = false; // 2P battle (split screen, pad 2)
    bool quick = false;      // after warp: open the post-race quick menu (if finished)
    int laps = 0;            // time trial: end after N laps (results), 0 = endless
    std::string split;       // 2P: "h" (top/bottom) or "v" (side by side); empty = config
    std::string car2;        // player 2's car (default: player 1's)
    std::string tt_event;    // time trial with this event's start (flying starts: licence tests, time attack)
    // Scripted player input (tests / screenshots): "thr:1@0;steer:-1@3" sets throttle to 1 at
    // 0 s and steering to full left at 3 s (seconds since the run started; brk, steer, thr).
    std::string script;
    int prize = -1;          // licence result: show this medal (0 gold .. 2 bronze), for screenshots
    bool demo = false;       // start the demonstration (licence: the disc's demo run)
    bool pit = false;        // auto-drive takes the pit lane (and stops) at the next entry marker
    std::string qm;          // quick menu: choose this item when it opens ("option", "record", "data_logger", ...)
    int ropts = -1;          // replays (screenshots): open REPLAY OPTIONS with the cursor on this row
    int rmode = -1;          // replays (screenshots): this view mode (not stored)
    int rtarget = -1;        // replays (screenshots): this TARGET for the view mode (not stored)
    int hud_mode = -1;       // screenshots: draw the HUD in this layout (race::HudMode number)
    int ritems = -1;         // replays (screenshots): these REPLAY OPTIONS ITEM bits (not stored)
    int rcam = -1;           // replays (screenshots): this camera mode instead of the replay cameras (2: in-car)
    float lcursor = -1;      // the Analyzer (screenshots): its cursor at this distance (m); lpanel=1: EXPRESSION open
    float oyaw = 0.6f, opitch = 0.25f, odist = 7.0f;
    float at = -1, at_kmh = 150; // time trial: start moving at this distance past the line (debug shots)
    float turn = 0;              // debug shots: the standing start turned about +Y by this many degrees
};

// The GT3R_RACE debug preset (have = false when the variable is not set).
DebugPreset read_preset();

// The course file of a course label (any region's folder), or "".
std::string course_path(const Vfs& vfs, const std::string& label);

// Rolling frame statistics, logged every few seconds.
struct Perf {
    double acc_frame = 0, acc_draw = 0, max_frame = 0, window = 0;
    int frames = 0;
    double avg_frame = 0, avg_draw = 0, peak_frame = 0;
    void add(double frame_ms, double draw_ms) {
        acc_frame += frame_ms, acc_draw += draw_ms, max_frame = std::max(max_frame, frame_ms), ++frames;
        window += frame_ms;
    }
    bool roll() {
        if (window < 3000 || frames == 0) return false;
        avg_frame = acc_frame / frames, avg_draw = acc_draw / frames, peak_frame = max_frame;
        acc_frame = acc_draw = max_frame = window = 0;
        frames = 0;
        return true;
    }
};

// Brake-lamp histories (GT3: a 4-frame history of "brake pedal > 0" per car; lamp factor k / 4):
// [0] player, [1] player 2, [2 + i] opponent i.
struct BrakeLamps {
    std::vector<std::array<u8, 4>> hist;
    void push(size_t car, float brake) {
        if (hist.size() <= car) hist.resize(car + 1, std::array<u8, 4>{});
        std::array<u8, 4>& h = hist[car];
        h[0] = h[1], h[1] = h[2], h[2] = h[3], h[3] = brake > 0 ? 1 : 0;
    }
    bool now(size_t car) const { return car < hist.size() && hist[car][3] != 0; }
    float factor(size_t car) const {
        if (car >= hist.size()) return 0;
        const std::array<u8, 4>& h = hist[car];
        return float(h[0] + h[1] + h[2] + h[3]) / 4.0f;
    }
    void clear() { hist.clear(); }
};

// A race started from the interactive menus (menu::flow::GameRequest).
struct MenuLaunch {
    menu::flow::GameRequest request;
};

class RaceScreen : public Screen {
public:
    RaceScreen() : preset_(read_preset()) {}
    explicit RaceScreen(MenuLaunch l);
    // Online battle (docs/dev/ONLINE.md): the session's race; leaving returns to the lobby.
    explicit RaceScreen(const OnlineLaunch& l);
    ~RaceScreen() override;

    void update(App& app, double dt) override;
    void on_event(App& app, const SDL_Event& e) override;
    // Phones: driving touch controls while racing, menu controls otherwise.
    TouchLayout touch_layout() const override;
    void render(App& app, Ui& ui) override;

private:
    // ===== Types =====

    enum class Stage { Select, Drive };
    enum class Panel { None, Options, Settings, Records, Logger }; // quick-menu panels
    struct SoundCar {
        float cd = 0.35f;   // body (racing modify, table 5) record +0x39 / 100
        bool whine = false; // gear part (table 17) +0x10 != 0: non-stock gearbox
    };
    struct BestLicense {
        int medal = -1;   // 0 gold .. 2 bronze, -1 finished without a medal
        double time = -1; // judged time (s)
    };
    using FieldLap = RaceField::LapInfo;
    enum class NetWait { None, Loading, Starting }; // online: the start handshake

    // ===== Methods, by the file that defines them =====

    // Screen core (screen_race.cpp)
    void init(App& app);
    // The run's start after init (launched from the menus: on the next frame, so the loading
    // picture is on screen first).
    void begin(App& app);
    // The loading screen (menus §6): the menu's last frame with its transition running on,
    // the "still loading" square after 3 s; black when no menu frame was handed over.
    void render_loading(App& app, Ui& ui);
    void toast(std::string s);
    void log_perf(bool force);

    // Launching (screen_race_launch.cpp)
    void resolve_online();
    void resolve_launch();

    // Run setup (screen_race_setup.cpp)
    TrackPose flying_pose(float metres_before) const;
    void start(App& app, const Replay* replay = nullptr);
    void warp(App& app, double seconds);
    void build_tv_cameras();
    void load_saved_ghost();
    void restart();
    void drop_vehicle();
    bool build_vehicle(App& app);
    bool build_vehicle_inner(App& app);
    ReplayBattle live_battle(App& app, const std::string& car2, int course_category) const;
    void reset_run_state();
    // Engine condition (physics §10.3): every live run starts with run_condition_ (the
    // request's garage car words, then what the previous live run left) and hands its words
    // back when it ends; replays run with the condition their run started with.
    void take_live_condition();
    void seed_vehicle_condition(const sim::EngineCondition& c, bool live);
    sim::EngineCondition live_condition() const;
    static std::string respawn_mode(App& app);
    std::optional<Bytes> read_car(App& app, const std::string& model) const;
    // Flying start (physics §9.1, 9.4: licence tests and time trials with the event's
    // distance and speed): the computer drives the player's car until the first line crossing.
    bool flying_runup() const { return !race_mode_ && tt_start_.flying() && track_ && !track_->lap_started(); }
    // The player's car's lap counter (race::lap_counter: 0 before the start line), as the
    // replay cameras take it.
    int lap_count() const { return race::lap_counter(track_->lap_started(), track_->laps_completed()); }
    // Frames left to the start signal of a standing-start hold (0: none, or past it).
    int frames_to_signal() const {
        const double s = field_ ? -field_->race_time() : tt_hold_;
        return s > 0 ? int(std::lround(s * 60.0)) : 0;
    }
    // A live view: not a replay, demonstration, attract demo or menu backdrop.
    bool live_view() const { return !replaying_ && !demo_ && !attract_ && !backdrop_; }
    // Rolling start: the computer drives the player's car for the first 300 ticks (counted in
    // ticks, so the hand-over cannot drift by a frame).
    bool rolling_runup() const { return rolling_ && field_ && std::llround(field_->race_time() * 60.0) < 300; }
    // A flying start's start signal (presentation §B2): the line crossing that ends the
    // computer's run-up starts the run as GO starts a standing one.
    void flying_start_signal(App& app);
    // Machine tests (presentation §B7): the time tests (0-400 m, 0-1000 m) rank the time to the
    // goal, the top-speed test the top speed.
    bool machine_time_test() const { return machine_ && machine_->timed(); }
    bool machine_test_top_speed() const { return machine_ && !machine_->timed(); }
    // An arcade or GT Mode free run (the course alone, no laps to run; not the free run before
    // a GT race).
    bool solo_free_run() const {
        return from_menu_ && !field_ && (launch_.request.mode == "free_run" || launch_.request.mode == "go_free_run");
    }

    // Drive loop (screen_race_drive.cpp)
    sim::ControlInputs scripted_inputs(double t) const;
    void next_view(CameraRig& cam, int player);
    void respawn();
    void apply_respawn(const TrackPose& p);
    sim::ControlInputs autodrive(const sim::VehicleState& s);
    // The player's driver parameters (ai §9.1): the course's set for the car's driveline class
    // (else the course category's built-in set); with `event`, the race / licence event's
    // overrides on top (ai §1.1: re-applied at the end of a run).
    ai::DriverParams player_params(bool event) const;
    // The player's AI driver for the pit lane / the end of the run (created on first use).
    ai::AiDriver& player_auto_driver();
    // How the run ended, for the end-of-run driver (ai::end_of_run).
    ai::EndOfRun player_end_of_run() const;
    void tick(App& app, const PadState& pad);
    void update_drive(App& app, double dt);
    static ForceFeedbackState feedback(const sim::Vehicle& v, const sim::ControlInputs& in, float wall, float car);
    void update_gear_hint(const sim::VehicleState& s);
    void update_dirt(double dt);
    void update_pylons(double dt);
    void update_smoke(double dt);

#if GT3R_XR
    // VR headsets (screen_race_vr.cpp): while driving, the race in stereo from the head pose,
    // seated in the car (in-car views) or floating behind it (chase views); the HUD, mirror and
    // menus on the panel in front.
    bool vr_stereo(App& app, bool split) const;
    render::Mat4 vr_base(const xr::Settings& st) const; // the seated space's origin -> world
    void render_vr_eyes(App& app, Ui& ui, const render::DrawOptions& o);
#endif

    // In-race pop-ups (screen_race_events.cpp): the event displays fed from the lap timers'
    // split-point and line crossings, the race gaps and the time trials' best lap; in replays
    // the re-run's, for the followed car.
    void reset_lap_events();
    void reset_session_best();
    void lap_events(bool timer_updated);
    HudRun hud_run() const; // the run's kind for the layout and the pop-ups (presentation §D4, §D6)
    void post_info(std::string text) { pending_info_.push_back(std::move(text)); }
    std::optional<int> car_at_position(int position) const; // Standing::car of race position 1..
    void draw_event_icons(App& app, int half);

    // Drawing the race (screen_race_render.cpp)
    bool update_env_cube();
    void draw_mirror(App& app, Ui& ui, const SDL_FRect& frame, const render::DrawOptions& main, bool p2 = false,
                     bool half = false);
    CarShadowParams shadow_params() const;
    void draw_car_icon(App& app, int car, const SDL_FRect& rc, size_t slot, bool on_bar = false);
    CarLook car_look(const sim::VehicleState& s, bool player) const;
    static TyreSmoke::View smoke_view(const CameraRig& c, float aspect, float fovy);
    // `mirror`: the rear-view mirror's pass (its own sky; CourseScene::add_items).
    void collect_items(const render::Vec3& eye, const sim::Vec3& right = {1, 0, 0}, bool player = true,
                       bool second = true, const TyreSmoke::View& smoke_v = {}, bool mirror = false);
    size_t player_items_[2] = {0, 0}; // collect_items: the player's car is items_[first, end)
    size_t scenery_items_ = 0;          // collect_items: the course's items are items_[0, end)
    HudData hud_data(int player);
    float sun_visibility(const render::Vec3& eye, float fovy, float rect_h);
    bool split_horizontal(App& app) const;
    static sim::Vec3 cam_right(const CameraRig& c);
    static sim::Vec3 cam_forward(const CameraRig& c);
    render::DrawOptions draw_options() const;
    void render_drive(App& app, Ui& ui);
    size_t add_wet_road(const render::Vec3& eye, float aspect, const render::DrawOptions& o) {
        return add_wet_road(eye, camera_.view(), camera_.projection(aspect),
                            render::Frustum::symmetric(camera_.fovy(), aspect), o);
    }
    void remove_wet_road(size_t n) {
        auto first = items_.begin() + std::ptrdiff_t(scenery_items_);
        items_.erase(first, first + std::ptrdiff_t(n));
    }
    // Returns the number of frame items inserted at items_[scenery_items_] (remove them after the draw).
    size_t add_wet_road(const render::Vec3& eye, const render::Mat4& view, const render::Mat4& proj,
                        const render::Frustum& fr, const render::DrawOptions& o);
    std::vector<CarLampGlows::Car> lamp_cars() const;
    static sim::Vec3 to_sim(const render::Vec3& v) { return {v.x, v.y, v.z}; }
    FieldLap field_lap2() const { return field_ && field_->has_second() ? field_->second_laps() : FieldLap{}; }

    // Race audio (screen_race_audio.cpp)
    void music_cues(App& app);
    void update_course_ambience(App& app);
    // Every car's tyre loops and the followed cars' collision one-shots, each physics step (sound §7.6).
    void update_tyre_sounds(App& app, const sim::VehicleState& s);
    SoundCar car_sound_facts(const std::string& label) const;
    static CarExtraSounds::Frame extra_frame(const sim::Vehicle& v, const sim::VehicleState& s, const SoundCar& car,
                                             bool onboard, float x, float z, SoundView view);
    void silence(App& app);
    // The original's global SE gain: race SE in live play, replay SE in replays and demos.
    // (The race behind a quick menu is silent: ours, its sound behind the menu not traced.)
    bool se_on() const { return backdrop_ ? false : replaying_ || demo_ || attract_ ? replay_se_ : race_se_; }

    // Selection (screen_race_select.cpp)
    void update_select(App& app);
    bool select_entries(const std::string& course, const std::string& car);
    void load_replay_file(App& app, const ReplayFile& f);
    void render_select(App&, Ui& ui);

    // Quick menu (screen_race_quick_menu.cpp)
    void start_free_run(App& app);
    void end_free_run(App& app);
    std::string quick_menu_layout(bool before) const;
    bool menu_before_run() const;
    void fill_license_menu(App& app);
    std::string records_key() const;
    bool course_time_trial() const;
    // The run's stored records (presentation §B7): the values of its ranking, best first;
    // empty when it has none (no GT-mode session, Quick race practice, debug runs).
    std::vector<double> stored_records() const;
    // A time trial, machine test or licence test finished with `value` (a time, or the
    // top-speed test's km/h): its place in the stored records, and the session record check
    // and RETRY lock (presentation §E3; `result`: licence 1..3 the medal, 0 none, else -2).
    void rank_finish(double value, bool speed, int result = -2);
    // The live run just over, into its ranking at once (licence with a medal, finished machine
    // test or time-trial challenge; gt_mode §17); true when it ranked and its name entry was
    // queued (make_presentation_screen) to run before the quick menu.
    bool record_run();
    void fill_top3();
    // The quick menu's item switches (menus §11): the visit's runs' and this run kind's, handed
    // to the menu shown (again after they change while it is up).
    void update_quick_menu_items();
    bool dirt_course() const;
    void open_prerace_menu(App& app);
    void open_quick_menu(App& app);
    void update_quick_menu(App& app, double dt);
    std::vector<OptionRow> option_rows() const;
    void change_option(App& app, int row, int dir);
    void update_panel(App& app);
    void open_records(App& app);
    void draw_panel(App& app, const SDL_FRect& dst);
    bool results_panel_up() const { return field_ && finished_ && !replaying_ && !two_player_ && finish_wait_ >= 5.0; }
    bool championship() const { return from_menu_ && launch_.request.champ_round > 0; }

    // Pause (screen_race_pause.cpp)
    std::vector<std::string> pause_items() const;
    PauseMenu::Kind pause_kind() const;
    void open_pause(App& app);
    void update_pause(App& app, double dt);
    void scan_user_music(App& app);
    void change_user_music(App& app, int delta);
    void online_pause_note(Ui& ui);
    void render_pause(Ui& ui);

    // Results (screen_race_results.cpp)
    static std::string ascii(const std::string& s);
    std::string car_display_name(const std::string& label, bool full = false) const;
    std::vector<ResultRow> result_rows(bool full_names = false) const;
    std::vector<ResultRow> result_menu_rows() const;
    void latch_result();
    menu::flow::GameResult build_result() const;
    void leave(App& app);
    int presented_medal() const;
    std::string race_text(const char* key, const char* fallback);
    // Tells the menus how the requested race went (GT mode awards prizes from it).
    void post_result() { menu::flow::post_game_result(result_latch_.for_leave(build_result())); }
    // Frames since the result was decided (the presenter's clock).
    int result_frame() const { return int(std::lround(finish_wait_ * 60.0)); }

    // Replays (screen_race_replay.cpp)
    Replay finished_recording() const;
    ReplayKind replay_kind() const;
    void end_replay(App& app);
    void watch_replay();
    void start_playback(const Replay& r);
    void stop_playback();
    void save_current_replay();
    void draw_replay_items(App& app, Ui& ui, const SDL_FRect& rect);
    // The title and replay-mark clocks from a run's or replay's start (presentation §B3):
    // `replay` 0 a live run, 1 a replay, 2 the attract demo / menu backdrop (no title).
    void start_hud_clocks(int replay, float start_speed);
    bool long_title(float start_speed) const;
    void apply_replay_settings(App& app, const ReplaySettings& rs);
    // The view settings in force: the attract demo's and the menu backdrop's own pick (play
    // mode 2), else REPLAY OPTIONS'.
    const ReplaySettings& view_settings() const { return attract_ || backdrop_ ? attract_view_ : replay_settings_; }
    // A new replay or demonstration: no shot, the director's generator seeded from the clock.
    void restart_director();
    void pick_attract_view();
    void apply_replay_presets();
    // The replay camera manager's 60 Hz frames (presentation §G14): the music's markers, the
    // shot length, view mode 1's handler (avant_garde_frame), the next cut request.
    void update_camera_manager(double dt);
    void avant_garde_frame();
    // The standing-start hold (the cars held on the grid before the signal).
    bool start_hold() const;
    // A dissolve from the last picture over `seconds` (presentation §G16), and its 60 Hz step.
    void start_dissolve(float seconds);
    void step_dissolve();
    // The followed car and its battle partner (presentation §G3): the pad (Up / Down, Cross; view
    // mode 1's cut buttons), TARGET, then the camera's aim (screen_race_replay.cpp).
    void replay_pad(const MenuInput& m);
    void begin_replay_focus();
    void update_replay_focus(double dt);
    void step_replay_focus();
    void follow(int car, int partner, int frames);
    std::vector<race::ReplayFocus::Car> focus_cars() const;
    void aim_replay_camera();
    void update_shot_filter();
    // The post-process pass (presentation §G6, §G16): the grade (the shot's levels / filter over
    // the course's Env0 values), the dissolve and the trail.
    void apply_post_pass(render::DrawOptions& o) const;
    std::string replay_dir(App& app) const { return app.data_dir() + "replays"; }

    // After a run (screen_race_post_run.cpp; race/post_run.h): the race-exit transition, the
    // prize / trophy and name-entry presentations, the automatic replay, the quick menu and
    // what it shows behind it, the pause window's EXIT.
    race::RunKind run_kind() const;
    // The pause window's EXIT in a live run: the run ends (the exit transition), or, in a free
    // run with a lap and the top-speed test once moving, it is the run's finish.
    void pause_exit();
    void exit_as_finish(double overlay_delay);
    // The race-exit transition over the frame on screen; `then` runs at its end.
    enum class ExitThen { RunEnd, Menu };
    void begin_exit_transition(ExitThen then);
    void after_run(App& app);        // the live run's transition is over
    void start_auto_replay(App& app); // the run's replay, by itself
    void return_to_menu(App& app);   // a replay's (or the licence demonstration's) transition is over
    // The quick menu's backdrop (race::menu_backdrop): the race in play mode 2, the licence
    // preview box, or nothing.
    void start_menu_backdrop(bool before_run);
    void stop_backdrop(App& app);
    void update_backdrop(App& app, double dt);
    // A live run is over and lost to the replays that re-run it: its laps for the saved replay.
    void keep_run_laps() { run_laps_ = lap_times_; }

    // Analyzer (screen_race_analyzer.cpp)
    void load_analyzer(App& app);
    void open_session_analyzer(App& app);
    void setup_logger(App& app);
    AnalyzerTrack sample_replay(App& app, const Replay& r);
    AnalyzerTrack sample_other_replay(App& app, const Replay& r);
    void logger_seek(App& app, float d);
    bool update_logger(App& app, double dt);

    // Online (screen_race_online.cpp)
    void configure_online_field(FieldConfig& cfg);
    int net_index(u8 player) const;
    void online_retire(int i);
    void online_event(App& app, const net::Event& e);
    bool online_update(App& app);
    int online_ticks(int n);
    void online_before_step(double dt);
    void online_after_step();
    void online_ghosts(const net::Session& ns);
    int online_finish_position() const;

    // Licence tests (screen_race_license.cpp)
    sim::ControlInputs demo_inputs(size_t i) const;
    void start_demo();
    void start_preview();
    void stop_preview(App& app);
    void end_demo(App& app);
    static std::string license_title(const std::string& code);
    void begin_license();
    void update_license(const sim::VehicleState& s, double dt);
    // The disc demo's steering is +-4096 with positive to the left; ours is positive right.
    static inline float kDemoSteerSign = std::getenv("GT3R_DEMO_SIGN") ? float(std::atof(std::getenv("GT3R_DEMO_SIGN"))) : -1.0f;

    // ===== State =====

    DebugPreset preset_;
    MenuLaunch launch_;
    std::shared_ptr<const sim::CarSpec> launch_spec_;
    std::string launch_model_;
    std::vector<double> lap_times_;
    double finish_wait_ = 0, all_done_at_ = -1;
    bool finish_menu_shown_ = false;
    bool p1_won_ = false, p2_won_ = false; // 2P: who crossed first
    int finish_record_rank_ = -1;
    bool retry_locked_ = false; // the run set a record: RETRY greyed out until the next run (presentation §E3)
    bool quick_ = false;
    bool results_loaded_ = false;
    Panel panel_ = Panel::None;
    int panel_sel_ = 0;
    LapLogger logger_;
    bool imperial_ = true; // speeds in mph
    std::shared_ptr<ai::AiCourse> hint_course_;   // recommended-gear hint (passive AI bookkeeping)
    std::unique_ptr<ai::AiDriver> hint_driver_;
    std::shared_ptr<ai::AiCourse> runup_course_;  // rolling / flying run-up: the CPU AI driver
    std::unique_ptr<ai::AiDriver> runup_driver_;
    // The AI driving the player's car in the pit lane and after the run (ai §1.1, ai §13).
    std::unique_ptr<ai::AiDriver> auto_driver_;
    bool end_of_run_ = false; // the end-of-run driver has taken over
    int hint_gear_ = -1;
    bool hint_blink_ = false;
    // The request's engine condition (has_condition_: the run hands it back to GT mode),
    // the one the next live run starts with, and whether the vehicle holds a live run's words.
    bool has_condition_ = false;
    sim::EngineCondition start_condition_, run_condition_;
    bool live_condition_ = false;
    double car_dirt_ = 0;
    int launch_tire_stage_ = -1;
    std::shared_ptr<const sim::CarSpec> launch_spec2_; // 2P: player 2's arcade car (own parts, tyres)
    int car2_paint_ = -1, manual2_ = -1;               // 2P: player 2's paint and gearbox (-1 = default) // arcade car: tyre stage used (10 racing, 11 drift, ...); -1 = none
    bool dirt_dims_ = true;               // false on the wet Route 5 (GT3 uses 0 there)
    std::string course_label_for_dirt_;   // course label (course table category)
    CoursePointLights point_lights_;
    BrakeLamps brake_lamps_;
    std::string car_folder_ = "cars/day/"; // the course's car time of day (day / eve / night)
    CarLampGlows car_lamps_;
    WetRoad wet_;                                // the wet-road reflection pass (course slots 9 / 13)
    std::vector<render::DrawItem> wet_items_;    // its buffer's scene
    Pylons pylons_;
    bool pylon_hit_ = false; // the player's car touched a pylon (licence: FAIL)
    TyreSmoke smoke_;                             // tyre smoke / dirt dust (course slots 26 / 32)
    // Per-car light sets this frame (stable addresses; filled while collecting the draw items).
    mutable std::deque<render::ItemLights> item_lights_;
    bool env_cube_on_ = true; // [video] car_reflections
    bool shadows_on_ = true;  // [video] car_shadows
    std::vector<render::DrawItem> env_items_;
    bool env_gt3_scene_ = false; // the map holds GT3's reflection scene: use GT3's lookup
    std::vector<RecordEntry> records_;
    std::vector<std::string> record_names_;
    RecordsPanel records_panel_;
    bool records_loaded_ = false;
    bool demo_ = false;                  // quick menu "demonstration": the computer drives
    std::optional<Replay> pre_demo_run_; // the player's run, kept for replay / save
    // After a run (screen_race_post_run.cpp): what the exit transition leads to, the replay
    // whose end (or EXIT) goes back to the quick menu (the automatic one, the REPLAY item), the
    // replay due once the run's presentations close, the menu's race in play mode 2.
    ExitThen exit_then_ = ExitThen::RunEnd;
    bool replay_to_menu_ = false, replay_pending_ = false, backdrop_ = false;
    // The quick menus' item switches for this visit (race::QuickMenuItems), and whether the
    // live run so far set a session record (SAVE GHOST, DATA LOGGER; reset at every run start).
    race::QuickMenuItems qm_items_;
    bool run_session_record_ = false;
    bool last_run_ended_ = false; // the last live run ended (finished, failed, or EXIT as its finish)
    bool exit_finished_ = false;  // pause EXIT was this run's finish
    double overlay_delay_ = race::kRecordOverlayDelay; // finish -> record overlay (s)
    std::vector<double> run_laps_; // the last live run's laps (the replays re-run lap_times_)
    bool abandoned_ = false;
    bool attract_ = false, attract_interrupted_ = false; // title-screen attract demo
    bool theater_ = false;     // started by the Replay Theater (theater_replay / theater_demo / logger)
    // The Analyzer (data logger): the theater's (logger_mode_) or the quick menu's.
    Analyzer analyzer_;
    bool analyzer_loaded_ = false, logger_mode_ = false;
    u32 logger_begin_ = 0, logger_end_ = 0; // track A's lap in its replay (ticks)
    double logger_acc_ = 0;
    // REPLAY OPTIONS and the replay view mode (presentation §G).
    ReplaySettings replay_settings_;
    ReplaySettings attract_view_; // the attract demo: its picked view mode and target (not stored)
    ReplayOptions replay_opts_;
    bool replay_opts_open_ = false, replay_opts_loaded_ = false;
    double replay_opts_acc_ = 0;
    int replay_focus_ = -1;     // followed car in field replays: -1 the player, else an opponent
    // The followed car's battle partner in view modes 1 and 2 (0 the player's car, i the opponent
    // i - 1; -1 none), the battles and interest scores, the camera's aim moving from the last
    // car (pair, same numbering) over `aim_left_` of `aim_total_` frames, the pad's car step (Up
    // 1 / Down -1) for the next focus frame, Cross's driver's view, the attract demo's car clock
    // (modes 0 / 3).
    int replay_partner_ = -1;
    race::ReplayFocus focus_;
    double focus_acc_ = 0;
    int aim_from_ = -1, aim_from_partner_ = -1, aim_left_ = 0, aim_total_ = 0;
    int focus_step_ = 0;
    bool focus_cut_ = false; // a pad cut (view mode 1)
    int level_step_ = 0, music_cut_step_ = 0; // view mode 1's Up / Down, Right / Left (+1 / -1)
    race::CutClock cut_clock_;               // the camera manager's requests and shot length
    double cut_acc_ = 0;
    race::DissolveClock dissolve_; // the post pass's dissolve
    race::FinishCue replay_finish_; // a field replay's finish jingle (the followed car's finish)
    int hidden_opponent_ = -1;      // collect_items: an opponent not drawn (an on-board replay view)
    bool driver_view_ = false;
    int attract_car_frames_ = 0;
    std::string theater_demo_; // theater_demo: the demonstration's event code
    static constexpr double kAttractSeconds = 1800.0 / 60.0; // RaceArcadeDemo's 1800-step timer
    float demo_start_kmh_ = 0;    // demo events: every car starts at this speed
    bool setting_pending_ = false;
    bool respawn_all_ = false;           // [race] respawn = all: the player's car too
    bool tt_failed_ = false;
    SunGlare sun_;
    bool no_course_fog_ = SDL_getenv("GT3R_NO_COURSE_FOG") != nullptr; // compare with the old linear fog
    LampGlows lamps_;
    int sun_log_ = 0;
    Billboards billboards_;
    bool original_res_ = false, original_nearest_ = true;
    double script_time_ = 0;
    double leave_clock_ = 0; // preset leave: seconds since the finish
    std::optional<BestLicense> best_license_; // the best finished licence run of this visit
    int demo_drive_tick_ = 0; // preset demo_drive: ticks since the run started
    bool preview_ = false;               // licence menu: the demonstration in the preview box
    bool run_preview_ = false;           // ... or the player's own run replaying (after a test)
    int exit_transition_ = -1;           // frames into the race -> menu transition (-1: none)
    static constexpr int kExitHold = 10, kExitWash = 8;
    bool prerace_ = false;
    bool free_run_ = false;              // free run before a GT race (back to the pre-race menu)
    int free_run_laps_ = 0;               // the pre-race quick menu (before the race starts)
    bool music_on_ = true, menu_music_on_ = true, rumble_on_ = true, rumble2_on_ = true;
    bool aids_options_apply_ = false, asm_on_ = true, tcs_on_ = true;
    bool race_se_ = true, replay_se_ = true, ghost_on_ = true, wide_mode_ = false;
    QuickMenu quick_menu_;
    RaceResultLatch result_latch_;
    std::optional<std::vector<ResultRow>> latched_rows_; // the result menu's rows, kept with the latched result
    PauseMenu pause_menu_;
    bool pause_loaded_ = false;
    menu::flow::MenuSounds quick_sounds_; // the menus' sound effects (quick / pause menus)
    bool quick_sounds_tried_ = false;
    ResultsOverlay results_;
    TimeTrialOverlay tt_overlay_;
    LicenseResultOverlay license_overlay_;
    std::unique_ptr<render::PrizeView> prize_; // licence medal model
    int shown_medal_ = -1;
    bool run_recorded_ = false;     // this run went into the records (record_run)
    bool top3_pending_ = false;     // its name entry is up: the Top 3 again once it closes
    bool prize_shown_ = false;      // the race showed its prize page / arcade trophy (GameResult::prize_shown)
    bool any_run_recorded_ = false; // GameResult::records_done
    std::optional<text::StringTable> race_strings_;
    bool race_strings_tried_ = false;
    double odometer_m_ = 0;
    bool from_menu_ = false;
    bool scripted_ = false; // a menu launch with GT3R_RACE set (no pre-run quick menu unless qm=)
    App* app_ = nullptr;
    bool debug_ = SDL_getenv("GT3R_RACE_DEBUG") != nullptr;
    int debug_ticks_ = 0;
    bool show_perf_ = false;
    u16 race_prev_ = 0; // race pad buttons of the last frame (edges)
    bool init_ = false, db_ok_ = false, hud_loaded_ = false, paused_ = false;
    menu::flow::LoadingPicture loading_; // the menu's frame and transition (or our own black one)
    SDL_Texture* loading_own_ = nullptr;  // the black frame when no menu frame was handed over
    u64 loading_ns_ = 0;                  // when the loading picture was last drawn
    bool loading_drawn_ = false, begin_pending_ = false;
    Stage stage_ = Stage::Select;
    std::string error_, course_name_;
    std::string title_name_; // the course table's name for the start title (presentation §B3; may be empty)
    data::CarDatabase db_;
    std::vector<CourseEntry> courses_;
    std::vector<CarEntry> cars_;
    int course_sel_ = 0, car_sel_ = 0, column_ = 0, pause_sel_ = 0;
    bool pause_more_ = false;
    std::vector<std::string> user_music_files_;
    std::vector<std::string> race_music_files_;
    int user_music_index_ = -1;
    u16 music_chord_prev_ = 0;
    int scripted_kind_ = 0;
    race::TitleClock title_clock_;
    race::ReplayMarkTimer mark_timer_;
    int last_shot_ = -2; // the director's cut the shot filter was rolled for
    u32 paint_seed_ = GameRandom::kBootSeed;   // race_info_random() before this race's paint draw
    std::optional<u32> paint_seed_override_;     // the next build re-runs a replay's paint draw
    SDL_FRect frame_rect_{0, 0, 0, 0}; // the 3D frame's rectangle last drawn (UI units)

    CarSetup car_;
    std::unique_ptr<RaceTrack> track_;
    std::unique_ptr<RaceVehicle> own_vehicle_;
    RaceVehicle* vehicle_ = nullptr; // own_vehicle_ or the field's player
    std::unique_ptr<RaceField> field_;
    std::vector<std::shared_ptr<CarVisual>> opp_visuals_;
    bool race_mode_ = false;
    // --- online battle (docs/dev/ONLINE.md) ---
    bool online_ = false;
    // The online race's state (screen_race_online.cpp; only read in online races).
    struct Online {
        NetWait wait = NetWait::None;
        double start_at = 0;         // net::now_ms() of the countdown start
        long long ticks = 0;         // ticks run since then
        std::vector<int> player;     // opponent index -> player id
        std::vector<int> paint;      // opponent index -> paint
        std::vector<std::string> names;
        std::vector<RemoteCar> cars;
        bool finish_sent = false, lost = false, loaded_sent = false, left = false;
        u32 state_tick = 0;
        Bytes car_info; // our game::OnlineCar
        bool physics = SDL_getenv("GT3R_NET_PREDICT") == nullptr || SDL_getenv("GT3R_NET_PREDICT")[0] != 'd'; // "dead" = dead reckoning
        bool dirt = false;
        int contacts_applied = 0; // hits only the other machine saw
        std::vector<bool> ghost;   // remote cars currently ghosted (any rule)
        std::vector<Plausibility> checks;
        std::vector<double> claims; // finish claims not yet checked against what we saw
        int respawn_ticks = 0;      // our states carry "respawned" this many more ticks
        net::InputFrame inputs[net::kInputHistory];
        int input_count = 0;
    } net_;
    // 2P battle (split screen): player 2 drives the field's second car with pad 2.
    bool two_player_ = false;
    OpponentEngines opp_engines_;
    RaceSfx sfx_;
    CarSfx car_sfx_, car_sfx2_;
    std::vector<CarSfx> opp_sfx_; // every other car's tyre loops (sound §7.6)
    float ffb_wall_ = 0, ffb_car_ = 0, ffb_wall2_ = 0, ffb_car2_ = 0; // strongest hits since the last frame
    CarExtraSounds extra_{"p1_"}, extra2_{"p2_"};
    SoundCar sound_car_, sound_car2_;
    CourseAmbience crss_;
    CourseAmbiencePlayer crss_player_;
    bool crss_ok_ = false;
    int last_count_ = -1;
    bool flying_runup_ = false; // last tick was in the flying run-up
    double flying_go_t_ = -1;   // race_time_ of the flying start's signal (< 0: not yet)
    CarColors colors_;
    ControlMapper controls2_;
    CameraRig camera2_;
    RumbleModel rumble2_;
    EngineSound engine2_;
    sim::ControlInputs last_in2_;
    sim::VehicleState render_state2_;
    std::unique_ptr<render::View3D> view2_;
    std::unique_ptr<render::View3D> mirror_view_, mirror_view2_;
#if GT3R_XR
    std::unique_ptr<render::View3D> vr_views_[2]; // the eyes (screen_race_vr.cpp)
#endif
    std::vector<std::unique_ptr<render::View3D>> icon_views_; // 2P results panel car pictures
    std::unique_ptr<CarVisual> car2_visual_;
    bool finished2_ = false;
    int finish_position2_ = 0;
    u16 pad2_prev_ = 0;
    std::string race_event_;
    std::vector<std::string> race_opponents_;
    bool field_grid_by_power_ = false, field_cpu_respawn_ = true; // the field's rules (recorded in replays)
    ReplayBattle battle_; // 2P battle: the field's set-up (recorded in replays)
    int race_laps_ = 0;
    bool rolling_ = false;   // rolling start (GT endurance)
    PitLane pit_;
    // The event displays (presentation §D6), one per player half: lap times, splits, gaps,
    // "+N Laps" and "%s Pitting in..."; the race gaps' passing order; time trials' best lap
    // of the visit (kept across retries).
    std::array<EventDisplay, 2> event_display_{};
    PassingOrder passing_;
    BestLapSplits best_splits_;
    std::vector<std::string> pending_info_; // this tick's "Pitting in..." messages
    std::array<int, 2> event_icon_car_{-1, -1}; // the gap's car picture per half (set by hud_data)
    // Replays of solo runs: the run has ended (its car's crossings post nothing more), and a
    // licence test's time at its end has been posted.
    bool replay_run_ended_ = false, replay_license_posted_ = false;
    // The HUD's lap-time list and Total per player half (presentation §D4): the live run's
    // laps (the last 8 kept), how many, their sum; and the visit's fastest lap of every car
    // (races; kept across retries).
    struct LapList {
        std::vector<double> recent;
        int count = 0;
        double sum = 0;
        void add(double t);
    };
    std::array<LapList, 2> lap_lists_{};
    long long session_best_ms_ = -1;
    std::string license_event_;   // licence test event ("" = not a licence test)
    u32 event_paint_id_ = 0xFFFFFFFFu; // licence test: the event's slot-0 colour id (0xFFFFFFFF = none)
    // Machine tests (presentation §B7): the test (the event's test byte) and the run's timing
    // points; machine_time_ is the time to the goal (s since GO), -1 before it.
    std::optional<MachineTest> machine_;
    std::vector<MachineTest::Crossing> machine_marks_; // this tick's timing points (lap_events)
    double machine_time_ = -1;
    float top_speed_kmh_ = 0;     // best speed of the run until its finish (GameResult::top_speed_kmh)
    float hud_top_kmh_ = 0;       // the HUD's highest speed so far (the top-speed test's "Best" line)
    LicenseRun license_, demo_judge_;
    double license_dist_ = 0;          // the judge's previous unwrapped track distance
    bool license_dist_valid_ = false;
    std::optional<DemoReplay> disc_demo_; // demo/<event>: the game's demonstration run
    bool disc_demo_tried_ = false;
    size_t demo_tick_ = 0;
    bool demo_off_ = false; // the demonstration's car put two wheels off the road: the AI drives on
    LicenseTimes license_times_;
    float license_progress_ = 0;  // metres since the start line at the last tick
    double tt_hold_ = 0;     // seconds of standing-start hold left (time trial / licence)
    EventStart tt_start_;    // time trial / licence event start (flying start when initial speed > 0)
    double time_limit_ = 0;  // time-limited race (s), 0 = laps
    bool finished_ = false;
    int finish_position_ = 0;
    std::unique_ptr<CourseScene> course_scene_;
    // The course's animations (CourseScene: CLUT animations and moving nodes; the wet road's and
    // the billboards' CLUT animations): restarted with every run, advanced by the race steps,
    // uploaded once per drawn frame.
    void restart_course_animations(float countdown) {
        if (course_scene_) course_scene_->restart_animations(countdown);
        wet_.clut_animations().restart(countdown);
        billboards_.clut_animations().restart(countdown);
    }
    void animate_course(float seconds) {
        if (course_scene_) course_scene_->animate(seconds);
        wet_.clut_animations().animate(seconds);
        billboards_.clut_animations().animate(seconds);
    }
    void upload_course_animations() {
        if (course_scene_) course_scene_->upload_animations();
        wet_.clut_animations().upload();
        billboards_.clut_animations().upload();
    }
    std::unique_ptr<CarVisual> car_visual_;
    std::unique_ptr<render::View3D> view_;
    std::vector<render::DrawItem> items_;

    FixedStep step_{sim::kDefaultTickHz};
    sim::VehicleState prev_, render_state_;
    sim::ControlInputs last_in_;
    ControlMapper controls_;
    RumbleModel rumble_;
    CameraRig camera_;
    Hud hud_;
    EngineSound engine_;
    MotorBanks motor_; // synthesised banks when the player's car is an electric mod car
    ReplayRecorder recorder_;
    Replay replay_;
    ReplayPlayer player_;
    bool replaying_ = false;
    CameraMode drive_camera_ = CameraMode::Bumper; // GT3 starts every race in the in-car view
    GhostLap ghost_, cur_lap_;
    double ghost_time_ = -1;
    u32 lap_ticks_ = 0, lap_begin_tick_ = 0;
    bool was_started_ = false;
    std::unique_ptr<GtrcScript> gtrc_;
    ReflectionDiscMesh reflection_discs_; // point-light courses: the lamps' discs in the reflection map
    ReplayDirector director_;
    ReverbAmbience ambience_;
    RaceMusic music_;
    int music_start_type_ = 2;          // the field event's start type (start jingle; music §2)
    double music_license_end_s_ = -1;   // the live licence run's end (race time), for its replay's finish cue
    bool music_license_failed_ = false; // the live licence run failed (its replay's fail jingle; music §3.1)
    std::string toast_;
    double toast_time_ = 0;
    bool show_replays_ = false;
    std::vector<ReplayFile> replay_files_;
    int replay_sel_ = 0;
    int auto_shift_ = 0;
    double stuck_time_ = 0, best_progress_ = -1;
    double race_time_ = 0, offroad_time_ = 0, clock_ = 0, frame_ms_ = 0;
    float lowest_ = -1000;
    Uint64 last_counter_ = 0;
    Perf perf_;
};

} // namespace gt3r::race_screen
