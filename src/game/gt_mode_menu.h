#pragma once

// Binds GT Mode (gt_mode.h) to the interactive menus through
// menu::flow::GtModeHooks: the GT mode menu's actions, its
// "pre-check" popups, the code-filled text widgets (credits, days, prices,
// license times, status page...), the garage list, race requests and
// results. docs/dev/GT_MODE.md describes the mapping. The implementation is in
// game/gt_mode_menu/, one file per concern around the private internal.h.
//
// install_gt_mode_menu() is called once by the app (src/app/screen_gtmode.cpp).
// The game is loaded from the current save of the save library (save_library.h,
// <user data>/saves/) when the GT-mode control opens (new game when there is none) and
// saved into it after every race; the save page (menu/flow/save_page.h) loads, saves,
// renames, copies, deletes, imports and exports saves.
//
// Environment (development / screenshots):
//   GT3R_GTMODE_SAVE=<path>   play this save file (its folder is the library; the
//                             current save is not remembered)
//   GT3R_GTMODE_NOSAVE=1      never write the save

#include "game/gt_mode.h"
#include "menu/flow/game_hooks.h"
#include "menu/flow/text_box.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gt3r::game {

class GtModeSession;
class SaveLibrary;

void install_gt_mode_menu();
// A license test run judged by the race (medal 0 gold .. 2 bronze, -1 none): written to the
// save at once, so the medal is kept even if the game ends before the race screen returns
// to the menus (the menus record it again from the GameResult, with the presentations).
void save_license_run(const std::string& code, int medal, double seconds);
// A licence test run with a medal (race code, seconds): entered in the test's records at
// once; when it makes the ranking the name entry is queued as a presentation (run it with
// GameFlow::start_presentations()). Returns the rank (-1 = not ranked / no session).
int add_license_record(const std::string& code, double seconds);
// A finished machine-test run (M400 / M1000: its time in seconds; MMAX: its top speed in
// km/h) with the ride car, recorded right after the run as the original does (gt_mode §17):
// its ranking and the best of the test; a run that makes the ranking queues its name entry
// as above. Returns the rank (-1 = not ranked / no session). Post the session's result with
// GameResult::records_done.
int add_machine_test_record(const std::string& code, double value);
// A finished arcade time-trial challenge run (code AT<nn>0001, its best lap in seconds), the
// same way (gt_mode §16): its ranking with the challenge's car and the name entry.
int add_time_trial_record(const std::string& code, double seconds);
// A free run (arcade time attack, GT Mode free run) that ended with a best lap (seconds) on
// `course` in `car` (a car label), the same way (gt_mode §17): the course's ranking
// ("ct:<course>") and the name entry.
int add_course_time_trial_record(const std::string& course, double best_lap, const std::string& car);
// A GT race's or championship round's prize page (the event's cup for places 1-3, else the
// consolation prize; place 0-based), queued for GameFlow::start_presentations() as the
// original shows it straight after the results, before the replay (presentation §F). False
// when the race has none (no trophy class / money). Post the result with
// GameResult::prize_shown.
bool queue_race_prize(const std::string& code, int place);
// An arcade single race won (race code "A<difficulty>..."): the difficulty's cup page, queued
// the same way (presentation §F: right after the race, before its replay). False without a
// session or a difficulty. Post the result with GameResult::prize_shown.
bool queue_arcade_trophy(const std::string& race_code);
// A finished GT single race (not a championship round): its place, prize and best result
// written to the save at once (a copy of the game; the menus record it from the GameResult
// when the race screen closes), as the original records a race before its replay.
void save_race_run(const menu::flow::GameResult& r);
// A championship round's "Save game" (result quick menu): a snapshot save
// (begin_snapshot_save) of the game with this round recorded (a copy: the menus record the
// round from the GameResult when the race screen closes); end_race_game_save() drops it.
// False without a session.
bool begin_race_game_save(const menu::flow::GameResult& r);
void end_race_game_save();
// The running session (nullptr until the GT-mode menus opened with disc data).
GtModeSession* gt_mode_session();
// The car a "car_render_*" widget shows on a page: the dealer's focus car with
// the chosen paint, the focused garage car, or the ride car (shops). False = none.
// `dirt` (0..0.8): the car's dirt (the showroom floor reflects 1 - dirt of it); during a car
// wash the dirt the car had, until the wash is over.
bool car_for_widget(const std::string& page, const std::string& widget_class, std::string& car, int& paint,
                    std::string* wheel = nullptr, float* dirt = nullptr);
// The prize car page's 3D scene (car_get, gt_mode §10.1; car_get_roulette.h): the candidates
// on their carousel, each in its own paint at its place (m, y up) and heading (degrees, about
// y), the ones the roulette is not on dropped by `y`; under each its floor light (the page's
// hit-mark image on a square of half size `light_half` at `light_y` below the car, opacity
// `light`); once the others have dropped away only the car won. The camera: eye, look-at point
// and up vector (a 40-degree horizontal field over the whole 640 x 448 frame). False when the
// page is not showing a prize car.
struct CarGetScene {
    struct Car {
        std::string car;
        int paint = 0;
        float x = 0, y = 0, z = 0, heading = 0, light = 0;
        bool lit = false; // the car the roulette is on: drawn last, over the page's background
    };
    std::vector<Car> cars;
    float eye[3]{}, look[3]{}, up[3]{0, 1, 0};
    float light_half = 0, light_y = 0;
    // What stays of the cars drawn before the lit one (and their lights): the page background is
    // put back over them at 1 - others (they fade out while the camera waits to fly in).
    float others = 1;
    // The cars' colour scale: 0 = black silhouettes (the whole carousel), then the car won
    // alone lighting up (0..1).
    float shade = 0;
    static constexpr float kHalfFov = 20.0f; // degrees, horizontal (= 0.3490659 rad)
};
bool car_get_scene(CarGetScene& out);
// The car wash spins the car (gt_mode §13.2): for 181 frames its showroom turntable gets an extra
// kWashSpinPush of yaw acceleration (degrees / s², on top of the camera's own) once a frame.
// `serial` is new for every wash; `pushes` counts the frames pushed so far, so a showroom
// stepping in real time applies each push exactly once.
struct CarWashSpin {
    int serial = 0;
    int pushes = 0;
};
CarWashSpin car_wash_spin();
constexpr float kWashSpinPush = 10000.0f;
// The prize model (".prm" path in the VOL) of the presentation on `page`: the
// license card on "license_get", the championship trophy on "trophy_get"; ""
// when none (or not yet faded in). `serial` changes with every new presentation
// (restart the animation); `fade` is its opacity 0..1.
std::string presentation_model(const std::string& page, int* serial = nullptr, float* fade = nullptr);
// The car an arcade picker entry shows ("nsx_s_zero_97" picture -> base car
// label for the 3D model), "" when unknown.
std::string arcade_entry_car(const std::string& entry);
// Arcade garage classes: the car page entries "hg<i>" (home garage) / "gg<i>" (guest
// garage), i = the garage list index. Returns the garage car (nullptr for other entries).
const GarageCar* arcade_garage_car(const std::string& entry);
// The race information's Cars Permitted list (page 00cmn/car_reguration) for a race code:
// each car of the event's regulation as "<name> <second name>", a string listed once (at
// its last place); empty when the race has no list ("No Limitations").
std::vector<std::string> permitted_car_names(const GtMode& gt, const std::string& code);
// Prepares the car settings menu (menu/setting.mbl) for the ride car: a working
// copy for the sliders; `event` (race code, may be empty) is checked on every
// change (settings that would break its rules are refused). False without a
// GT-mode session or ride car.
bool begin_setting(const std::string& event);
// Prepares the records menu (menu/records.mbl) for a ranking key: "lic:<test>",
// "mt:M400" / "mt:M1000" / "mt:MMAX", "at:<arcade time trial code>", "ct:<course>".
bool begin_records(const std::string& key);
// Creates the GT-mode session (disc data + save) if none exists yet, e.g. when
// the settings menu is opened on its own. `read` loads disc files.
bool ensure_gt_mode_session(const std::function<std::optional<Bytes>(const std::string&)>& read);
// The save library (GT3R_GTMODE_SAVE's folder, or <user data>/saves/ with the saves of
// earlier versions migrated on first use) and the save to play: GT3R_GTMODE_SAVE, the
// remembered current save, the newest save, or <library>/gtmode.sav for a new game.
SaveLibrary& save_library();
std::string current_save_path();
// The next save page in SaveGame mode saves `s` instead of the running game (e.g. a
// championship between rounds, with the round just finished), and the running game and
// its current save stay as they are. Cleared when that page closes.
void begin_snapshot_save(GameState s);
// The snapshot waiting for the save page (nullptr = none; tests).
const GameState* snapshot_save();
// Tests only: forgets everything this module keeps between calls (the loaded disc data and
// its reader, the session, queued presentations / sounds / popups / movies, the map pointer,
// the save library and the snapshot), so the next GT-mode menus start as on a fresh launch,
// e.g. with another disc or a synthetic data set. Hooks stay installed.
void reset_gt_mode_menu_for_tests();
// The disc closes (App::close_disc): forgets the same, so the next disc loads its own data,
// reader and region's save library.
void close_gt_mode();
// The mods changed (App::rescan_mods): the next GT-mode start loads the disc data again, and
// the dealers their mod cars, with the mods as they are now. A running session keeps the
// data it has.
void reload_gt_mode_data();

class GtModeSession {
public:
    GtModeSession(std::shared_ptr<const GtData> data, std::string save_path);

    GtMode& gt() { return gt_; }
    const GtMode& gt() const { return gt_; }
    const std::string& save_path() const { return save_path_; }
    // The save the game is written to from now on (the library's current save).
    void set_save_path(std::string p) { save_path_ = std::move(p); }
    bool save(std::string* error = nullptr);
    // False (a new game) when there is no readable save at save_path(); one that is there
    // but cannot be read is moved aside first (set_aside_save, unless GT3R_GTMODE_NOSAVE),
    // so the new game's saves do not replace it.
    bool load(std::string* error = nullptr);

    // Menu state the original keeps in its session block.
    std::string focus_car;     // dealer car (set_car_code)
    int garage_cursor = 0;     // garage list cursor (hm/hm_gr), index into the display order
    int sort_key = 0;          // garage sort (0 date .. 8 aspiration)
    int color_cursor = 0;      // dealer car page: paint index
    int garage_focus = -1;     // garage car chosen (set_garage_car)
    int focus_goods = -1;      // tune-shop goods (set_focus_parts)
    float part_power_before = 0; // tune shop: the ride car's power before the last part was fitted
    int regulated_cursor = 0;  // 00cmn/car_reguration: the permitted-car list cursor
    std::string focus_wheel;   // wheel shop (set_focus_wheel_code)
    std::string focus_wheel_preview; // wheel shop: the wheel under the cursor (shown on the car)
    // hm/hm_wheel, the garage's wheel list: the focused button (0 = the stock-wheel placeholder,
    // k = GameState::wheels[k - 1]) and the row's offset in its window, easing towards the
    // focused button's (none yet: placed at once).
    int wheel_cursor = 0;
    float wheel_row_off = 0;
    bool wheel_row_set = false;
    std::string race_code;     // set_race_code*
    bool champ = false;        // set_race_code_champ
    std::string last_page;
    // Car settings menu.
    std::optional<GarageCar> setting_work;          // working copy (sliders edit it)
    std::string setting_event;                      // race whose rules apply ("" = none)
    std::map<std::string, int> setting_entry;       // slider positions when the page opened (pinch_prev)
    bool setting_committed = false;                 // a commit hides pinch_prev until the page is rebuilt
    std::string records_key;                        // records menu: the ranking shown
    int records_cursor = 0;                         // records menu: the place under the cursor
    menu::flow::TextBox name_box;                   // record name entry: the name and its cursor
    // Guest garage (a friend's save loaded for Trade and the arcade guest class) and
    // whether the garage pages show it (the trade session).
    std::unique_ptr<GtMode> guest;
    bool trade = false;
    GtMode& list_gt() { return trade && guest ? *guest : gt_; }
private:
    GtMode gt_;
    std::string save_path_;
};

} // namespace gt3r::game
