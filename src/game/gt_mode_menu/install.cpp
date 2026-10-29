// GT-mode menu bindings: install_gt_mode_menu() registers the GtModeHooks, each one handled by
// the file of its concern (internal.h lists them), and attach() opens the game when the GT-mode
// control opens.

#include "game/gt_mode_menu/internal.h"

#include "base/log.h"

#include <cstdlib>
#include <utility>

namespace gt3r::game {

void install_mod_menu_patches(); // gt_mode_mods.cpp

using namespace gt_menu;

namespace {

// The GT-mode control is opened: the disc data and the current save of the save library (a new
// game when there is none), the original's boot-time memory-card load.
void attach(const menu::flow::FileReader& read, const std::string& lang) {
    menu_state().reader = read;
    menu_state().lang = lang;
    std::string why;
    bool loaded = false;
    if (!start_session(read, &why, &loaded)) {
        LOG_WARN("gt mode: no game data: %s", why.c_str());
        return;
    }
    // Development aid (screenshots, tests of the result path): every race is won at once.
    if (const char* fake = std::getenv("GT3R_GTMODE_FAKE_RACE"); fake && *fake && *fake != '0')
        menu::flow::set_game_request_handler([](const GameRequest& rq, App&) {
            GameResult res;
            res.mode = rq.mode;
            res.race_code = rq.race_code;
            res.event = rq.event;
            res.finished = true;
            res.position = 1;
            res.cars = int(rq.opponents.size()) + 1;
            res.total_time = 60;
            if (rq.car_condition) { // 3 km driven
                const sim::EngineCondition end =
                    GtMode::driven({rq.car_odometer, rq.car_wear_m, rq.car_oil_m}, 3000);
                res.car_condition = true;
                res.car_odometer = end.odometer, res.odometer_added = end.odometer - rq.car_odometer;
                res.car_wear_m = end.wear_m, res.car_oil_m = end.oil_m;
                res.garage_car = rq.garage_car;
            }
            if (rq.mode == "go_license") res.license_medal = 0; // gold
            menu::flow::post_game_result(res);
            return true;
        });
    if (loaded) LOG_INFO("gt mode: loaded %s", S()->save_path().c_str());
}

} // namespace

void install_gt_mode_menu() {
    install_mod_menu_patches();
    menu::flow::GtModeHooks h;
    h.attach = attach;
    h.on_event = [](const MenuEvent& ev, std::vector<std::string>&) {
        std::vector<std::string> lines;
        bool consumed = on_event(ev, lines);
        to_log_file(lines);
        return consumed;
    };
    h.intercept = intercept;
    h.enabled = enabled;
    h.listed = listed;
    h.initial_item = initial_item;
    h.widget_text = widget_text;
    h.hide_art = hide_art;
    h.on_frame = on_frame;
    h.on_tick = on_tick;
    h.map_input = map_input;
    h.prepare_request = prepare_request;
    h.on_result = on_result;
    h.next_request = next_request;
    h.extra_draws = extra_draws;
    h.tap = garage_tap;
    h.hover = garage_hover;
    h.take_popup = []() { return std::exchange(menu_state().popup, std::string()); };
    h.name_backspace = name_backspace;
    h.name_cursor = name_cursor;
    h.take_movie = []() {
        std::vector<std::string>& q = menu_state().movies;
        if (q.empty()) return std::string();
        std::string mv = std::move(q.front());
        q.erase(q.begin());
        return mv;
    };
    h.attract_idle_frames = attract_idle_frames;
    h.attract_demo = attract_demo;
    h.on_arcade_result = on_arcade_result;
    h.arcade_parts = arcade_parts;
    h.arcade_gauges = arcade_gauges;
    h.arcade_colors = arcade_colors;
    h.arcade_image = arcade_image;
    h.arcade_specs = arcade_specs;
    h.arcade_prepare = arcade_prepare;
    h.save_list = save_page_files;
    h.save_run = save_run;
    h.save_closed = save_closed;
    h.arcade_disabled = arcade_disabled;
    h.arcade_entries = arcade_entries;
    h.presentation = next_presentation;
    h.resume_request = resume_request;
    menu::flow::set_gt_mode_hooks(std::move(h));
}

} // namespace gt3r::game
