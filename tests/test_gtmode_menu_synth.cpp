// GT Mode menus (game/gt_mode_menu/ through menu::flow::GameFlow) on a small invented
// world (gtmode_synth.h): characterization tests that pin the current behaviour without a
// disc. Never writes a save (GT3R_GTMODE_NOSAVE) and leaves no module state behind.

#include "test_framework.h"

#include "gtmode_synth.h"

#include "game/gt_mode_menu.h"
#include "game/gt_save.h"
#include "game/gt_setting.h"
#include "game/prize_count_up.h"
#include "game/save_library.h"
#include "menu/flow/game_flow.h"
#include "menu/flow/gt_mode_hooks.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::menu::flow;
namespace synth = gt3r::test::gtsynth;
namespace w = gt3r::test::gtsynth::world;

namespace {

void set_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unset_env(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// Each test starts from a fresh GT-mode module (no data, no session, nothing queued) on
// the synthetic files, and puts the module and the environment back as it found them:
// other tests in the process (the disc tests) load their own data afterwards.
class World {
public:
    World() : read(synth::gtmode_reader()) {
        for (const char* v : kVars) {
            const char* old = std::getenv(v);
            saved_.push_back(old ? std::optional<std::string>(old) : std::nullopt);
            unset_env(v);
        }
        set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
        set_env("GT3R_GTMODE_NOSAVE", "1");
        game::reset_gt_mode_menu_for_tests();
        take_game_result();
        game::install_gt_mode_menu();
    }
    ~World() {
        game::reset_gt_mode_menu_for_tests();
        take_game_result();
        for (size_t i = 0; i < std::size(kVars); ++i) {
            if (saved_[i]) set_env(kVars[i], saved_[i]->c_str());
            else unset_env(kVars[i]);
        }
    }
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    FileReader read;

private:
    static constexpr const char* kVars[] = {"GT3R_GTMODE_SAVE", "GT3R_GTMODE_NOSAVE", "GT3R_GTMODE_FAKE_RACE",
                                            "GT3R_GTMODE_FAKE_TROPHY", "GT3R_GTMODE_PRIZE_FRAME"};
    std::vector<std::optional<std::string>> saved_;
};

void settle(GameFlow& f) {
    for (int i = 0; i < 300; ++i) {
        if (!f.switching() && f.controller() && !f.controller()->busy()) break;
        f.tick(0);
    }
    f.tick(0);
}

void press(GameFlow& f, MenuKey k) {
    f.tick(key_bit(k));
    f.tick(0);
    settle(f);
}

void choose(GameFlow& f, const std::string& item) {
    CHECK(f.controller() && f.controller()->set_focus(item));
    press(f, MenuKey::Confirm);
}

std::string page(const GameFlow& f) { return f.controller() ? f.controller()->page_name() : std::string("<none>"); }

// The text a widget class is drawn with ("<none>" when not drawn).
std::string text_of(const GameFlow& f, const std::string& cls) {
    for (const menu::ElementDraw& d : f.draw_list())
        if (d.element && d.element->widget_class && *d.element->widget_class == cls && d.text) return *d.text;
    return "<none>";
}

// The text an element (by name) is drawn with ("<none>" when not drawn).
std::string text_named(const GameFlow& f, const std::string& name) {
    for (const menu::ElementDraw& d : f.draw_list())
        if (d.element && d.element->name == name && d.text) return *d.text;
    return "<none>";
}

bool has_sound(const std::vector<std::string>& v, const char* s) { return std::find(v.begin(), v.end(), s) != v.end(); }

bool box_is(const menu::ElementDraw& d, float x, float y, float w, float h) {
    auto near = [](float a, float b) { return std::abs(a - b) < 0.01f; };
    return d.has_box && near(d.box.x, x) && near(d.box.y, y) && near(d.box.w, w) && near(d.box.h, h);
}

// Draws of elements with this name (the hidden art the GT-mode code places).
std::vector<menu::ElementDraw> draws_named(const GameFlow& f, const std::string& name) {
    std::vector<menu::ElementDraw> out;
    for (const menu::ElementDraw& d : f.draw_list())
        if (d.element && d.element->name == name) out.push_back(d);
    return out;
}

std::optional<GameRequest> wait_request(GameFlow& f) {
    for (int i = 0; i < 120 && !f.waiting_for_game(); ++i) f.tick(0);
    return f.take_request();
}

// The game comes back with a result; the flow runs its presentations.
void finish_game(GameFlow& f, const GameResult& r) {
    post_game_result(r);
    f.game_finished();
    settle(f);
}

// The run drove 4 km: a request that carries the engine condition gets it back advanced
// (physics §10.3), as the race reports it.
void drove_4km(const GameRequest& r, GameResult& res) {
    if (!r.car_condition) return;
    const sim::EngineCondition end = game::GtMode::driven({r.car_odometer, r.car_wear_m, r.car_oil_m}, 4000);
    res.car_condition = true;
    res.car_odometer = end.odometer, res.odometer_added = end.odometer - r.car_odometer;
    res.car_wear_m = end.wear_m, res.car_oil_m = end.oil_m;
    res.garage_car = r.garage_car;
}

GameResult win(const GameRequest& r, int position = 1) {
    GameResult res;
    res.mode = r.mode;
    res.race_code = r.race_code;
    res.event = r.event;
    res.course = r.course;
    res.finished = true;
    res.position = position;
    res.cars = int(r.opponents.size()) + 1;
    drove_4km(r, res);
    return res;
}

// The title page: opening the GT-mode control creates the session (a new game).
game::GtModeSession* start_title(GameFlow& f) {
    f.start();
    settle(f);
    return game::gt_mode_session();
}

} // namespace

// The synthetic files load through the real parsers into the GT-mode data.
TEST(gtmode_synth_world_loads) {
    World world;
    auto data = std::make_shared<game::GtData>();
    game::GtData& d = *data;
    CHECK(d.load(world.read));
    CHECK(d.error().empty());
    CHECK(d.db.cars().size() == 3);
    const data::Car* coupe = d.db.car(w::kCoupe);
    CHECK(coupe && coupe->name == "Synthetic Coupe" && coupe->maker == "ACME MOTORS" && coupe->price == 12000);
    CHECK(d.model(w::kCoupe) == "zz0001");
    CHECK(d.color_count(w::kCoupe) == 2 && d.color_count(w::kWagon) == 3 && d.color_count(w::kRacer) == 1);
    CHECK(d.color_name(d.color_id(w::kWagon, 2)) == "Chalk White");
    CHECK(d.color_rgb(d.color_id(w::kCoupe, 1)) == 0x002020C0);
    CHECK(d.starting_credits() == w::kStartCredits && d.car_wash_price() == 60 && d.wheel_price() == 700);
    CHECK(d.title_idle_seconds == w::kTitleIdleSeconds);
    CHECK(d.catalog.dealers.size() == 3);
    const game::DealerCar* racer = d.catalog.dealer_car("_syn_racer_03");
    CHECK(racer && racer->car == w::kRacer && racer->region == "jp");
    CHECK(d.catalog.series.size() == 3);
    const game::RaceSeries* champ = d.catalog.series_for("Gsynch_n_0501");
    CHECK(champ && champ->championship() && champ->races.size() == 2 && champ->tier == "amateur");
    CHECK(d.catalog.licenses.size() == 2 && d.catalog.license("b") && d.catalog.license("b")->tests.size() == 8);
    CHECK(d.catalog.shop_item_of_page.at("ts/ts_eg_muf_sp") == "muf_sports");
    CHECK(d.prizes.size() == 4 && d.prize_set("Gsyn_e_0000") && d.prize_set("Gsyn_e_0000")->cars[0].car == w::kRacer);
    const game::PrizeSet* all = d.prize_set("gtmode_complete");
    CHECK(all && all->cars.size() == 1 && all->cars[0].car == w::kCoupe && all->cars[0].color_id == -1);
    CHECK(d.race_codes.size() == 6 && d.unlock_level.at("Gsynf_s_0001") == 1); // (the championship code too)
    CHECK(d.demos.size() == 2 && d.demos[1].event == "demo_syn_2" && d.demos[1].course == "syn_hill");
    CHECK(d.arcade_courses.at("SINGLE_RACE").size() == 3 && d.arcade_courses.at("SINGLE_RACE")[2].level == 1);
    CHECK(d.arcade_classes.at("CLASS_C").size() == 2 && d.arcade_classes.at("CLASS_C")[0].image() == w::kCoupe);
    CHECK(d.name_entry_chars("US").size() == 5 && d.name_entry_chars("US").at("key_b") == "B"); // (every item with a text)
    game::GtMode gt(data);
    auto rules = gt.rules("Gsyn_e_0001");
    CHECK(rules && rules->course == "syn_ring" && rules->prize[0] == 1500 && rules->trophy_class == 5 &&
          rules->cars == 6 && rules->entries.size() == 2);
    auto lic = gt.rules("L0B0003");
    CHECK(lic && lic->license_ms[0] == 30000 && lic->license_ms[1] == 32500 && lic->entries[0] == "syv_lic_b");
    CHECK(gt.car_flags(w::kRacer) == 3 && gt.car_category(w::kRacer) == 1);
    // The prize pages' word before "Cr." from the realtime text, in the language.
    CHECK(d.prize_prefix == "Prize");
}

// The game config's sums come from the disc: a game config without one of them is refused (the
// program has no fallback), whatever the others hold; in Japanese they are x100.
TEST(gtmode_synth_game_config_sums) {
    World world;
    for (const char* name : {"initial", "car_wash", "oil_change", "wheel"}) {
        FileReader read = [&world, name](const std::string& p) -> std::optional<Bytes> {
            std::optional<Bytes> b = world.read(p);
            if (!b || p != "gameconf/product.gcf") return b;
            // Rename the entry ("wheel" -> "wheeX"): its value is no longer found.
            const std::string key = std::string(1, '\0') + name + '\0';
            auto at = std::search(b->begin(), b->end(), key.begin(), key.end());
            if (at != b->end()) *(at + std::ptrdiff_t(key.size()) - 2) = 'X';
            return b;
        };
        game::GtData d;
        CHECK(!d.load(read) && d.error().find(std::string("'") + name + "'") != std::string::npos);
    }
    FileReader japanese = [&world](const std::string& p) -> std::optional<Bytes> {
        if (p == Vfs::kLanguageFile) return Bytes{'J', 'P'};
        return world.read(p);
    };
    game::GtData jp;
    CHECK(jp.load(japanese) && jp.money_scale == 100);
    CHECK(jp.starting_credits() == 100 * u32(w::kStartCredits) && jp.car_wash_price() == 6000 &&
          jp.oil_change_price() == 30000 && jp.wheel_price() == 70000);
    CHECK(jp.prize_prefix == "\xBE\xDE\xB6\xE2"); // the Japanese pool of the realtime text
}

// The dealer region pages leave out the Opel button in British English, the Vauxhall one in
// the other languages (docs/dev/GT_MODE.md "Language"); other pages list every item.
TEST(gtmode_synth_dealer_buttons_by_language) {
    for (const char* lang : {"US", "BR"}) {
        World world;
        FileReader read = [&world, lang](const std::string& p) -> std::optional<Bytes> {
            if (p == Vfs::kLanguageFile) return Bytes(lang, lang + 2);
            return world.read(p);
        };
        GameFlow f(read, "US");
        CHECK(start_title(f) != nullptr);
        const GtModeHooks& h = gt_mode_hooks();
        menu::MblItem opel, vauxhall;
        opel.name = "opel", vauxhall.name = "vauxhall";
        const bool br = std::string(lang) == "BR";
        CHECK(h.listed("cd/cd_ger", opel) == !br && h.listed("cd/cd_eng", vauxhall) == br);
        CHECK(h.listed("hm/hm", opel) && h.listed("hm/hm", vauxhall));
    }
}

// install_gt_mode_menu(): every hook the GT-mode code provides is set; opening the GT-mode
// control (the title) attaches the session: a new game from the gcf's figures.
TEST(gtmode_synth_install_and_attach) {
    World world;
    const GtModeHooks& h = gt_mode_hooks();
    CHECK(h.attach && h.on_event && h.intercept && h.enabled && h.listed && h.widget_text && h.hide_art && h.on_frame &&
          h.map_input && h.prepare_request && h.on_result && h.next_request && h.extra_draws && h.tap && h.hover &&
          h.take_popup && h.name_backspace && h.take_movie && h.attract_idle_frames && h.attract_demo &&
          h.on_arcade_result && h.arcade_parts && h.arcade_gauges && h.arcade_colors && h.arcade_image &&
          h.arcade_specs && h.arcade_prepare && h.save_list && h.save_run && h.save_closed && h.arcade_disabled &&
          h.arcade_entries && h.presentation && h.resume_request);
    CHECK(!h.widget_draws); // image widgets are filled from extra_draws
    CHECK(menu_patches().flow && menu_patches().layout); // mod cars at the dealers / arcade
    // Before the menus open there is no session and the hooks do nothing.
    CHECK(game::gt_mode_session() == nullptr);
    CHECK(h.attract_idle_frames() == 0 && !h.presentation() && h.take_popup().empty() && h.take_movie().empty());
    std::vector<std::string> log;
    CHECK(!h.on_event(MenuEvent{}, log) && log.empty());
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    CHECK(s->save_path() == "/nonexistent-gt3r-test/gtmode.sav");
    CHECK(s->gt().state().credits == w::kStartCredits && s->gt().state().day == 1 && s->gt().state().garage.empty());
    CHECK(h.attract_idle_frames() == w::kTitleIdleSeconds * 60);
    CHECK(!h.name_backspace() && !h.next_request() && !h.resume_request());
    // hide_art: the 3D model markers, the car renders and the paint box are drawn by code.
    menu::Element e;
    auto hidden = [&](const char* pg, const char* cls) {
        e.widget_class = cls;
        return h.hide_art(pg, e);
    };
    CHECK(hidden("car_get", "render_model") && hidden("gtmode_mini", "render_model") &&
          hidden("trophy_get", "render_model"));
    CHECK(!hidden("map", "render_model") && !hidden("map", "user_money"));
    CHECK(hidden("cd/acme_coupe_car", "car_render_from") && hidden("hm/hm_car", "car_color_box"));
    CHECK(hidden("ar/goodies", "end_button")); // no league complete, no course open beyond level 0
    CHECK(!hidden("map", "end_button"));
}

// The disc data loads at the first title and stays for the game's later starts, until the
// mods change (App::rescan_mods): the next start then loads it with the mods' files as they
// are (a mod installed after the first title was missing from GT mode and arcade).
TEST(gtmode_synth_data_reloads_after_mods_change) {
    World world;
    auto files = std::make_shared<std::map<std::string, Bytes>>(synth::gtmode_files());
    const FileReader read = [files](const std::string& p) -> std::optional<Bytes> {
        auto it = files->find(p);
        if (it == files->end()) return std::nullopt;
        return it->second;
    };
    const GtModeHooks& h = gt_mode_hooks();
    std::shared_ptr<const game::GtData> first;
    {
        GameFlow f(read, "US");
        game::GtModeSession* s = start_title(f);
        CHECK(s != nullptr);
        if (!s) return;
        first = s->gt().data_ptr();
        CHECK(h.attract_idle_frames() == w::kTitleIdleSeconds * 60);
    }
    // A mod's product.gcf: the title idle time (section 0, the first) is 7 s.
    Bytes& gcf = (*files)["gameconf/product.gcf"];
    synth::p32(gcf, rd32(gcf, 12), 7);
    {
        GameFlow f(read, "US"); // back to the title: the data it had
        game::GtModeSession* s = start_title(f);
        CHECK(s && s->gt().data_ptr() == first);
        CHECK(h.attract_idle_frames() == w::kTitleIdleSeconds * 60);
    }
    game::reload_gt_mode_data();
    {
        GameFlow f(read, "US");
        game::GtModeSession* s = start_title(f);
        CHECK(s && s->gt().data_ptr() != first);
        CHECK(h.attract_idle_frames() == 7 * 60);
    }
}

// The title (GameFlow start) and GT mode's map: the status bar, the map pointer moving
// between the buildings, the status page, and back to the title.
TEST(gtmode_synth_title_and_map) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s && f.control() == "gt_mode" && page(f) == "root" && f.controller()->focus() == 0);
    if (!s) return;
    CHECK(f.controller()->items().size() == 7);
    CHECK(f.take_music() == std::string() && f.take_movie() == std::string("stream/title_full.pss"));
    press(f, MenuKey::Down);
    CHECK(f.controller()->focus() == 1);
    press(f, MenuKey::Confirm); // go_sitemap: the map, a cut in the same control
    CHECK(f.control() == "gt_mode" && page(f) == "map" && !f.switching());
    CHECK(f.take_music() == std::string("gtmenu01"));
    // Status bar (the import page 00cmn/status).
    CHECK(text_of(f, "user_money") == " 25,000");
    CHECK(text_of(f, "user_day_unit") == "day 1");
    CHECK(text_of(f, "user_carname").empty());
    {
        auto unit = draws_named(f, "user_day_unit");
        // Right-aligned 10 units before the 24-unit box's right edge, 96 wide.
        CHECK(unit.size() == 1 && box_is(unit[0], 560 + 24 - 10 - 96, 400, 96, 16));
    }
    // The pointer eases onto the focused building (cd_cd, centre 130, 320).
    for (int i = 0; i < 60; ++i) f.tick(0);
    {
        auto ptr = draws_named(f, "pointer");
        CHECK(ptr.size() == 1 && box_is(ptr[0], 130, 320 - 16, 16, 16));
    }
    f.take_sounds();
    // Right: the building to the right (hm_hm) takes the focus when the pointer arrives.
    press(f, MenuKey::Right);
    CHECK(has_sound(f.take_sounds(), "cursor"));
    for (int i = 0; i < 40; ++i) f.tick(0);
    CHECK(f.controller()->focused()->flow->name == "hm_hm");
    press(f, MenuKey::Down);
    for (int i = 0; i < 40; ++i) f.tick(0);
    CHECK(f.controller()->focused()->flow->name == "lc_lc");
    press(f, MenuKey::Left);
    for (int i = 0; i < 40; ++i) f.tick(0);
    CHECK(f.controller()->focused()->flow->name == "ts_ts");
    // The status page.
    choose(f, "st_st");
    CHECK(page(f) == "status");
    CHECK(text_of(f, "status_ttl_wins") == "0" && text_of(f, "status_ttl_races") == "0");
    CHECK(text_of(f, "status_avg_win") == "0 %" && text_of(f, "status_ttl_milage") == "0.0 miles");
    CHECK(text_of(f, "status_prize_money") == " 0" && text_of(f, "status_no_cars") == "0");
    CHECK(text_of(f, "status_ttl_car_price") == " 0" && text_of(f, "status_no_lc_g") == "0");
    CHECK(text_of(f, "status_complete") == "0.0 %");
    CHECK(draws_named(f, "lc_mark1").empty() && draws_named(f, "lc_mark2").empty()); // no licence
    CHECK(text_of(f, "user_money") == " 25,000"); // the status bar here too
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "map" && f.controller()->focused()->flow->name == "st_st");
    // Cancel on the map: back to the title, focus on GT mode.
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "root" && f.controller()->focus() == 1);
    // The replay theater page and back: its back action (set_focus_replay_theater) moves the
    // title focus to Replay Theater before the title is built.
    choose(f, "replay");
    CHECK(page(f) == "replay_theater" && f.controller()->items().size() == 3);
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "root" && f.controller()->focus() == 2);
}

// The title's one focus (menus §7.1): the title always opens on it; coming back from a title
// command sets it to that command's item, go_top sets GT mode, the Replay Theater page's back
// action sets Replay Theater. Every way back to the title the flow can drive here.
TEST(gtmode_synth_title_focus_on_return) {
    CHECK(title_focus_after("go_arcade_menu") == 0 && title_focus_after("go_replay_theater") == 2);
    CHECK(title_focus_after("go_load_game_menu") == 3 && title_focus_after("go_save_game_menu") == 4);
    CHECK(title_focus_after("go_file_operation") == 5 && title_focus_after("go_option") == 6);
    CHECK(title_focus_after("go_demonstration") == 0 && title_focus_after("go_logger") == 0);
    CHECK(title_focus_after("go_race") == 0 && title_focus_after("") == 0);
    World world;
    GameFlow f(world.read, "US");
    CHECK(start_title(f) != nullptr);
    auto title_focus = [&] { return f.control() == "gt_mode" && page(f) == "root" ? f.controller()->focus() : -1; };
    CHECK(title_focus() == 0); // boot: Arcade
    // Replay Theater and back, wherever the title was: Replay Theater.
    press(f, MenuKey::Down);
    press(f, MenuKey::Down);
    press(f, MenuKey::Down);
    press(f, MenuKey::Down); // (Save: moving the focus does not change the title focus)
    choose(f, "replay");
    press(f, MenuKey::Cancel);
    CHECK(title_focus() == 2);
    // GT mode and back (go_top): GT mode; then Replay Theater again: Replay Theater.
    choose(f, "gt_mode");
    press(f, MenuKey::Cancel);
    CHECK(title_focus() == 1);
    choose(f, "replay");
    press(f, MenuKey::Cancel);
    CHECK(title_focus() == 2);
    // A Replay Theater command and back to its page, then Cancel: Replay Theater.
    choose(f, "replay");
    choose(f, "theater");
    settle(f);
    CHECK(f.control() == "memorycard" && f.theater());
    press(f, MenuKey::Cancel);
    settle(f);
    CHECK(f.control() == "gt_mode" && page(f) == "replay_theater");
    press(f, MenuKey::Cancel);
    settle(f);
    CHECK(title_focus() == 2);
    // Arcade, Save, File operation and Options: back on their own item.
    choose(f, "arcade");
    settle(f);
    CHECK(f.control() == "arcade" && page(f) == "ar/top");
    press(f, MenuKey::Cancel);
    settle(f);
    CHECK(title_focus() == 0);
    const std::pair<const char*, int> modes[] = {{"save", 4}, {"file", 5}, {"load", 3}, {"option", 6}};
    for (const auto& [item, focus] : modes) {
        choose(f, item);
        settle(f);
        CHECK(f.control() != "gt_mode");
        press(f, MenuKey::Cancel);
        settle(f);
        CHECK(title_focus() == focus);
    }
}

// The dealer: prices and names (widget_text), the paint column (extra_draws), the
// pre-check popups (no money, not for sale), buying (on_event buy_car / ride_car).
TEST(gtmode_synth_dealer) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    choose(f, "gt_mode");
    choose(f, "cd_cd");
    choose(f, "jp");
    choose(f, "acme");
    CHECK(page(f) == "cd/cd_jp_acme");
    CHECK(text_named(f, "syn_coupe_01_price") == "Cr. 12,000" && text_named(f, "syn_wagon_02_price") == "Cr. 30,000");
    choose(f, w::kCoupe); // set_car_code
    CHECK(page(f) == "cd/acme_coupe_car" && s->focus_car == w::kCoupe && s->color_cursor == 0);
    CHECK(text_of(f, "focused_carname") == "Synthetic Coupe");
    CHECK(text_of(f, "car_buy_price") == " 12,000");
    CHECK(text_of(f, "color_name_box") == " Sky Mist");
    // The paint column: per paint two shadows and the swatch, the chosen one marked.
    auto fills = [&] {
        std::vector<menu::ElementDraw> out;
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.kind == menu::ElementDraw::Kind::Fill) out.push_back(d);
        return out;
    };
    auto marked_at = [&](float y) {
        for (const menu::ElementDraw& d : fills())
            if (box_is(d, 560 - 12, y, 8, 10)) return true;
        return false;
    };
    {
        auto fl = fills();
        CHECK(fl.size() == 7);
        bool first = false;
        for (const menu::ElementDraw& d : fl)
            if (box_is(d, 560, 125, 16, 10))
                first = std::abs(d.fill.r - 0x80 / 255.0f) < 1e-3f && std::abs(d.fill.b - 0xC0 / 255.0f) < 1e-3f;
        CHECK(first);
        CHECK(marked_at(125) && !marked_at(112.5f));
    }
    // The car logo (hidden art named after the car) in the car_logo widget's box.
    {
        auto logo = draws_named(f, w::kCoupe);
        CHECK(logo.size() == 1 && box_is(logo[0], 40, 60, 80, 40));
    }
    std::string car;
    int paint = -1;
    CHECK(game::car_for_widget("cd/acme_coupe_car", "car_render_from", car, paint) && car == w::kCoupe && paint == 0);
    // Down picks the next paint (and wraps).
    f.take_sounds();
    press(f, MenuKey::Down);
    CHECK(s->color_cursor == 1 && has_sound(f.take_sounds(), "cursor"));
    CHECK(text_of(f, "color_name_box") == " Ember Red" && marked_at(112.5f));
    press(f, MenuKey::Down);
    press(f, MenuKey::Down);
    CHECK(s->color_cursor == 1);
    // No money: the popup instead of the purchase.
    gt.state().credits = 0;
    f.take_sounds();
    choose(f, "buy");
    CHECK(page(f) == "00cmn/no_money" && f.controller()->depth() == 1 && has_sound(f.take_sounds(), "disable"));
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "cd/acme_coupe_car" && f.controller()->depth() == 0);
    // Bought (in the chosen paint); the first car becomes the ride car.
    gt.state().credits = w::kStartCredits;
    choose(f, "buy");
    CHECK(page(f) == "00cmn/cd_buy_sure");
    choose(f, "yes1");
    CHECK(gt.state().garage.size() == 1 && gt.state().ride == 0 && gt.state().garage[0].color == 1);
    CHECK(gt.state().credits == w::kStartCredits - 12000);
    CHECK(page(f) == "00cmn/cd_bought" && f.controller()->depth() == 1);
    choose(f, "yes1"); // one car: no "ride it?"
    CHECK(page(f) == "cd/acme_coupe_car" && f.controller()->depth() == 0);
    CHECK(text_of(f, "user_carname") == "Synthetic Coupe" && text_of(f, "user_money") == " 13,000");
    // A second car: "ride the new car?" (yes: the new car, at the head of the garage).
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "cd/cd_jp_acme" && f.controller()->focus() == 0);
    choose(f, w::kWagon);
    CHECK(text_of(f, "car_buy_price") == " 30,000" && text_of(f, "color_name_box") == " Moss Green");
    choose(f, "buy");
    CHECK(page(f) == "00cmn/no_money");
    press(f, MenuKey::Cancel);
    gt.state().credits = 100000;
    choose(f, "buy");
    choose(f, "yes1");
    choose(f, "yes1"); // check_ride_car
    CHECK(page(f) == "00cmn/cd_ride_sure");
    CHECK(gt.state().garage.size() == 2 && gt.state().garage[0].car == w::kWagon && gt.state().ride == 1);
    choose(f, "yes1");
    CHECK(gt.state().ride == 0 && page(f) == "cd/acme_wagon_car" && f.controller()->depth() == 0);
    CHECK(text_of(f, "user_carname") == "Synthetic Wagon");
    // A prize-only car ("_label" item at its dealer): not for sale.
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    choose(f, "beta");
    choose(f, "_syn_racer_03");
    CHECK(s->focus_car == w::kRacer && text_of(f, "focused_carname") == "Synthetic Racer");
    CHECK(text_of(f, "car_buy_price") == "----"); // no price for a car not for sale
    choose(f, "buy");
    CHECK(page(f) == "00cmn/not_for_buy" && gt.state().garage.size() == 2);
}

// The dealer's list (gt_mode §7): each car's price as "Cr. 12,000", "----" for a car whose
// not-for-sale flag (bit 0) is set, here the prize-only racer at its dealer; x100 in Japanese
// (gt_mode §2).
TEST(gtmode_synth_dealer_list_not_for_sale) {
    for (const char* lang : {"US", "JP"}) {
        World world;
        FileReader read = [&world, lang](const std::string& p) -> std::optional<Bytes> {
            if (p == Vfs::kLanguageFile) return Bytes(lang, lang + 2);
            return world.read(p);
        };
        GameFlow f(read, "US");
        game::GtModeSession* s = start_title(f);
        CHECK(s != nullptr);
        if (!s) return;
        const game::GtMode& gt = s->gt();
        const bool jp = std::string(lang) == "JP";
        CHECK(gt.data().money_scale == (jp ? 100 : 1));
        choose(f, "gt_mode");
        choose(f, "cd_cd");
        choose(f, "jp");
        choose(f, "acme");
        CHECK(page(f) == "cd/cd_jp_acme");
        CHECK(text_named(f, "syn_coupe_01_price") == (jp ? "Cr. 1,200,000" : "Cr. 12,000"));
        CHECK(text_named(f, "syn_wagon_02_price") == (jp ? "Cr. 3,000,000" : "Cr. 30,000"));
        press(f, MenuKey::Cancel);
        choose(f, "beta");
        CHECK(page(f) == "cd/cd_jp_beta");
        // The racer has a catalogue price, but the list shows none.
        CHECK(gt.car_price(w::kRacer) == (jp ? 9000000 : 90000) && (gt.car_flags(w::kRacer) & 1));
        CHECK(text_named(f, "_syn_racer_03_price") == "----");
    }
}

// The wheel inventory (gt_mode §7): 200 slots with the stock-wheel placeholder in one, so 199
// wheels can be bought. With no slot free the shop's confirm page still opens (its pre-check
// refuses only a wheel already held or too little money), buying adds nothing and charges
// nothing, and "fit it?" still puts the wheel on the ride car, for free and outside the
// inventory, as GT3's shop does on a full inventory. The synthetic wheel price is 700 (x100 in
// Japanese, gt_mode §2).
TEST(gtmode_synth_wheel_inventory_full) {
    for (const char* lang : {"US", "JP"}) {
        World world;
        FileReader read = [&world, lang](const std::string& p) -> std::optional<Bytes> {
            if (p == Vfs::kLanguageFile) return Bytes(lang, lang + 2);
            return world.read(p);
        };
        GameFlow f(read, "US");
        game::GtModeSession* s = start_title(f);
        CHECK(s != nullptr);
        if (!s) return;
        game::GtMode& gt = s->gt();
        const s64 price = std::string(lang) == "JP" ? 70000 : 700;
        CHECK(gt.data().wheel_price() == price);
        const GtModeHooks& h = gt_mode_hooks();
        // The wheel shop's confirm page for a wheel: "" (it opens) or the refusal popup.
        auto buy_sure = [&](const std::string& wheel) {
            menu::MblItem it;
            it.name = wheel + "_link";
            it.link = "00cmn/ws_buy_sure";
            return h.intercept("at/ws/ws_x", it);
        };
        std::vector<std::string> log;
        auto action = [&](const std::string& a, const std::string& item = {}) {
            MenuEvent e;
            e.kind = MenuEvent::Kind::Action;
            e.action = a;
            e.item = item;
            e.page = a == "buy_wheel" ? "00cmn/ws_buy_sure" : "at/ws/ws_x";
            return h.on_event(e, log);
        };
        gt.state().credits = 100000000;
        CHECK(gt.buy_car(w::kCoupe) == 0 && gt.state().wheels.empty());
        s64 credits = gt.state().credits;
        for (int i = 0; i < 198; ++i) CHECK(gt.buy_wheel("syn_wheel_" + std::to_string(i)));
        CHECK(gt.state().wheels.size() == 198 && gt.state().credits == credits - 198 * price);
        // The 199th, through the shop's pre-check and actions: bought and charged.
        credits = gt.state().credits;
        CHECK(buy_sure("syn_wheel_198").empty());
        action("set_focus_wheel_code", "syn_wheel_198_link");
        action("buy_wheel");
        CHECK(gt.state().wheels.size() == 199 && gt.state().wheels.back() == "syn_wheel_198");
        CHECK(gt.state().credits == credits - price);
        action("fit_wheel");
        CHECK(gt.state().ride_car()->wheel == "syn_wheel_198" && gt.state().credits == credits - price);
        // The 200th: no slot is free.
        credits = gt.state().credits;
        CHECK(buy_sure("syn_wheel_199").empty() && gt.can_buy_wheel("syn_wheel_199") == game::Check::Ok);
        action("set_focus_wheel_code", "syn_wheel_199_link");
        CHECK(s->focus_wheel == "syn_wheel_199");
        action("buy_wheel");
        CHECK(gt.state().wheels.size() == 199 && gt.state().wheels.back() == "syn_wheel_198");
        CHECK(gt.state().credits == credits);
        CHECK(!gt.buy_wheel("syn_wheel_199") && gt.state().wheels.size() == 199 && gt.state().credits == credits);
        // "Fit it?" yes: the ride car wears the wheel the inventory had no slot for; nothing is charged.
        action("fit_wheel");
        CHECK(gt.state().ride_car()->wheel == "syn_wheel_199" && gt.state().credits == credits);
        CHECK(gt.state().wheels.size() == 199 &&
              std::find(gt.state().wheels.begin(), gt.state().wheels.end(), "syn_wheel_199") == gt.state().wheels.end());
        // The garage's car page shows the car on it, and the save keeps it.
        std::string shown, wheel;
        int paint = -1;
        CHECK(game::car_for_widget("hm/hm_car", "car_render_garage", shown, paint, &wheel));
        CHECK(shown == w::kCoupe && wheel == "syn_wheel_199");
        const game::GameState saved = game::load_from_text(game::save_to_text(gt.state()));
        CHECK(saved.garage.size() == 1 && saved.garage[0].wheel == "syn_wheel_199" && saved.wheels == gt.state().wheels);
        // Once changed, it cannot be put back: the garage offers only the inventory's wheels.
        CHECK(gt.fit_wheel(gt.state().ride, "") && !gt.fit_wheel(gt.state().ride, "syn_wheel_199"));
        // The usual refusals still come first.
        CHECK(buy_sure("syn_wheel_0") == "00cmn/already_you_have");
        gt.state().credits = price - 1;
        CHECK(buy_sure("syn_wheel_199") == "00cmn/no_money");
    }
}

// The garage's wheel list (gt_mode §7; extra_draws, on_frame): one button per wheel inventory
// entry in a row centred in the "menu_wheel" window, 4 units apart, the stock-wheel button first
// ("default_wheel" tile), then the bought wheels with their own tiles in the "wheel" frame; the
// "mark" on the worn entry, the cursor starting there (the first button, unmarked, for a wheel
// the inventory does not hold); Left / Right move it and the car shows the wheel; "Equip this
// wheel?" yes fits it (the stock-wheel button: the car's own wheels). The scroll bar shows only
// while the row is longer than the window, with the pinch over the visible share. The tile named
// after a car model is never drawn. The wheel shop's brand page starts on the worn wheel.
TEST(gtmode_synth_garage_wheel_list) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    gt.state().credits = 1000000;
    CHECK(gt.buy_car(w::kCoupe) == 0 && gt.buy_wheel(w::kRimA) && gt.buy_wheel(w::kRimB));
    const game::GarageCar& car = gt.state().garage[0];
    choose(f, "gt_mode");
    choose(f, "hm_hm");
    choose(f, "hm_gr");
    choose(f, "car1");
    choose(f, "wheel");
    CHECK(page(f) == "hm/hm_wheel");
    auto boxes = [&](const std::string& name) {
        std::vector<float> xs;
        for (const menu::ElementDraw& d : draws_named(f, name))
            if (d.has_box) xs.push_back(d.box.x);
        std::sort(xs.begin(), xs.end());
        xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
        return xs;
    };
    auto preview = [&] {
        std::string c, wheel = "?";
        int paint = -1;
        game::car_for_widget(page(f), "car_render_wheel", c, paint, &wheel);
        return wheel;
    };
    auto settle_scroll = [&] {
        for (int i = 0; i < 60; ++i) f.tick(0);
    };
    settle_scroll();
    // Three buttons (188 units) centred in the 620-unit window from x 10: frames at 226, 290, 354.
    const std::vector<float> frames = boxes("wheel");
    CHECK((frames == std::vector<float>{226, 290, 354}));
    CHECK((boxes("default_wheel") == std::vector<float>{230}) && (boxes(w::kRimA) == std::vector<float>{294}) &&
          (boxes(w::kRimB) == std::vector<float>{358}));
    CHECK((boxes("mark") == std::vector<float>{226}) && s->wheel_cursor == 0 && preview().empty());
    CHECK(draws_named(f, "zz0001").empty() && draws_named(f, "scroll_bar").empty() && draws_named(f, "pinch").empty());
    // A hovering pointer moves the cursor to the button under it, a tap on another button too,
    // and a tap on the cursor's button asks "Equip this wheel?" (here: no).
    CHECK(f.hover(354 + 30, 95 + 30) && s->wheel_cursor == 2 && preview() == w::kRimB);
    CHECK(f.tap(226 + 30, 95 + 30) && s->wheel_cursor == 0);
    CHECK(f.tap(226 + 30, 95 + 30));
    f.tick(0); // (the tap's confirm goes in on the next frame)
    settle(f);
    CHECK(page(f) == "hm/hm_change_wheel_sure");
    choose(f, "no1");
    CHECK(page(f) == "hm/hm_wheel" && s->wheel_cursor == 0 && car.wheel.empty());
    // Right: the first bought wheel on the car; Right again stops at the end.
    f.take_sounds();
    press(f, MenuKey::Right);
    CHECK(s->wheel_cursor == 1 && preview() == w::kRimA && has_sound(f.take_sounds(), "cursor"));
    press(f, MenuKey::Right);
    press(f, MenuKey::Right);
    CHECK(s->wheel_cursor == 2 && preview() == w::kRimB);
    press(f, MenuKey::Left);
    // Equip it.
    press(f, MenuKey::Confirm);
    CHECK(page(f) == "hm/hm_change_wheel_sure");
    choose(f, "yes1");
    CHECK(car.wheel == w::kRimA && page(f) == "hm/hm_wheel_changed");
    choose(f, "buy3");
    CHECK(page(f) == "hm/hm_car");
    choose(f, "wheel");
    settle_scroll();
    CHECK(s->wheel_cursor == 1 && (boxes("mark") == std::vector<float>{290}));
    // The stock-wheel button: back on the car's own wheels.
    press(f, MenuKey::Left);
    press(f, MenuKey::Confirm);
    choose(f, "yes1");
    CHECK(car.wheel.empty());
    choose(f, "buy3");
    // A wheel the inventory does not hold: no mark, the first button.
    gt.state().garage[0].wheel = "syn_rim_c";
    choose(f, "wheel");
    settle_scroll();
    CHECK(s->wheel_cursor == 0 && boxes("mark").empty());
    press(f, MenuKey::Cancel);
    gt.state().garage[0].wheel.clear();
    // Ten more wheels (13 buttons, 828 units): left-aligned near the start, the scroll bar and
    // the pinch as wide as the visible share (620 / 828 of the bar).
    for (int i = 0; i < 10; ++i) CHECK(gt.buy_wheel("syn_more_" + std::to_string(i)));
    choose(f, "wheel");
    settle_scroll();
    CHECK(boxes("wheel").front() == 10 && draws_named(f, "scroll_bar").size() == 1);
    {
        auto p = draws_named(f, "pinch");
        CHECK(p.size() == 1 && std::abs(p[0].box.x - 10) < 0.01f && std::abs(p[0].box.w - 620.0f * 620.0f / 828.0f) < 0.01f);
    }
    // In the middle the cursor's button is centred (button 6: offset 310 - (384 + 30) = -104),
    // the pinch at 10 + 104 x 620 / 828.
    for (int i = 0; i < 6; ++i) press(f, MenuKey::Right);
    settle_scroll();
    {
        auto p = draws_named(f, "pinch");
        CHECK(s->wheel_cursor == 6 && (boxes("wheel").size() >= 7));
        CHECK(p.size() == 1 && std::abs(p[0].box.x - (10 + 104.0f * 620.0f / 828.0f)) < 0.01f);
    }
    // At the far end the row is right-aligned and the pinch at the bar's end.
    for (int i = 0; i < 6; ++i) press(f, MenuKey::Right);
    settle_scroll();
    {
        auto p = draws_named(f, "pinch");
        CHECK(s->wheel_cursor == 12 && boxes("wheel").back() == 630 - 60);
        CHECK(p.size() == 1 && std::abs(p[0].box.x + p[0].box.w - 630) < 0.01f);
    }
    // The wheel shop's brand page starts on the wheel the ride car wears.
    gt.state().garage[0].wheel = w::kRimB;
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "map");
    choose(f, "at_at");
    choose(f, "ws");
    choose(f, "syn");
    CHECK(page(f) == "at/ws/ws_syn" && f.controller()->focus() == 1);
    // Its two 52-unit tiles 4 apart (108 units) centred in the 600-unit window from x 20.
    CHECK(draws_named(f, w::kRimA).size() == 1 && draws_named(f, w::kRimA)[0].box.x == 20 + 246);
    CHECK(draws_named(f, w::kRimB).size() == 1 && draws_named(f, w::kRimB)[0].box.x == 20 + 246 + 56);
    gt.state().garage[0].wheel.clear();
    press(f, MenuKey::Cancel);
    choose(f, "syn");
    CHECK(page(f) == "at/ws/ws_syn" && f.controller()->focus() == 0);
}

// The garage list (code-drawn rows in extra_draws), its cursor (on_frame), the sort panel,
// the garage car page and selling (on_event sell_car, intercept not_for_sell).
TEST(gtmode_synth_garage) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    gt.state().credits = 100000;
    CHECK(gt.buy_car(w::kCoupe, 0) == 0);
    CHECK(gt.buy_car(w::kWagon, 2) == 0); // newest first: wagon 0, coupe 1 (the ride car)
    choose(f, "gt_mode");
    choose(f, "hm_hm");
    choose(f, "hm_gr");
    CHECK(page(f) == "hm/hm_gr");
    // Rows in the "menu_garage" window (y up: from its top at 400, 24 apart: the 20-unit rows
    // and GT3's button list gap of 4), date order, the cursor row in the template's focused look;
    // the MAKE column; the ride car's mark.
    auto rows = [&] {
        std::vector<std::pair<std::string, menu::ElementDraw>> out;
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && d.text && d.element->name == "car1") out.push_back({*d.text, d});
        return out;
    };
    {
        auto r = rows();
        CHECK(r.size() == 2);
        if (r.size() == 2) {
            CHECK(r[0].first == "Synthetic Wagon" && box_is(r[0].second, 40, 380, 300, 20));
            CHECK(r[1].first == "Synthetic Coupe" && box_is(r[1].second, 40, 356, 300, 20));
            CHECK(r[0].second.element == &f.controller()->items()[0].element->alt[0]); // the cursor row
        }
        CHECK(draws_named(f, "maker").size() == 1 && draws_named(f, "price").empty());
        auto mark = draws_named(f, "mark");
        CHECK(mark.size() == 1 && box_is(mark[0], 412, 360, 12, 12));
        CHECK(text_named(f, "info_a") == "ACME MOTORS" && text_named(f, "info") == "ACME MOTORS");
    }
    f.take_sounds();
    press(f, MenuKey::Down);
    CHECK(s->garage_cursor == 1 && has_sound(f.take_sounds(), "cursor"));
    press(f, MenuKey::Down); // clamped
    CHECK(s->garage_cursor == 1);
    // START: the sort panel; by name.
    press(f, MenuKey::Start);
    CHECK(page(f) == "hm/hm_gr_panel" && f.controller()->depth() == 1);
    choose(f, "sort_name");
    CHECK(s->sort_key == 1 && s->garage_cursor == 0 && page(f) == "hm/hm_gr");
    {
        auto r = rows();
        CHECK(r.size() == 2 && r[0].first == "Synthetic Coupe" && r[1].first == "Synthetic Wagon");
    }
    // The car under the cursor: the garage car page.
    choose(f, "car1");
    CHECK(page(f) == "hm/hm_car" && s->garage_focus == 1);
    CHECK(text_of(f, "focused_carname") == "Synthetic Coupe");
    CHECK(text_of(f, "car_sell_price") == "3,000"); // (no space: the page insets its texts)
    CHECK(text_of(f, "car_kilometrage") == "0.0miles" && text_of(f, "color_name_box") == "Sky Mist");
    std::string car;
    int paint = -1;
    float dirt = -1;
    CHECK(game::car_for_widget("hm/hm_car", "car_render_garage", car, paint, nullptr, &dirt));
    CHECK(car == w::kCoupe && paint == 0 && dirt == 0);
    // Sold for a quarter of the price.
    choose(f, "sell");
    CHECK(page(f) == "hm/hm_sell_sure");
    choose(f, "yes1");
    CHECK(page(f) == "hm/hm_gr" && gt.state().garage.size() == 1 && gt.state().credits == 100000 - 42000 + 3000);
    CHECK(gt.state().ride == -1 && s->garage_focus == -1);
    // A prize car cannot be sold.
    CHECK(gt.add_prize_car(w::kRacer, -1, "test") == 0);
    choose(f, "car1"); // name order: the racer first
    CHECK(page(f) == "hm/hm_car" && text_of(f, "focused_carname") == "Synthetic Racer");
    CHECK(text_of(f, "car_sell_price") == "----");
    choose(f, "sell");
    CHECK(page(f) == "00cmn/not_for_sell" && gt.state().garage.size() == 2);
}

// The garage list as GT3's vertical button list (menus §4.6, §9.7; gt_mode §5, §19): the 20-unit
// rows 24 apart from the
// window's top; a list that fits stays there, with no scroll bar at all (track included); a
// longer one keeps the cursor row centred, held at the window's top and bottom near the ends,
// its offset easing 30 % a frame; the scroll bar's pinch is window h x bar h / list h high, its
// top at bar top - offset x bar h / list h (top-down). The rows are clipped to the window; a
// hovering pointer moves the cursor without scrolling, a tap moves it and a second confirms.
// The trade list (the guest garage) is the same list.
TEST(gtmode_synth_garage_list_scroll) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    gt.state().credits = 100000000;
    CHECK(gt.buy_car(w::kCoupe, 0) == 0);
    CHECK(gt.buy_car(w::kWagon, 0) == 0);
    choose(f, "gt_mode");
    choose(f, "hm_hm");
    choose(f, "hm_gr");
    CHECK(page(f) == "hm/hm_gr");
    // The list's rows (the code-drawn ones, clipped to the window) top to bottom, and the
    // cursor row's.
    auto rows = [&] {
        std::vector<menu::ElementDraw> out;
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && d.text && d.element->name == "car1" && d.has_clip) out.push_back(d);
        std::sort(out.begin(), out.end(), [](const menu::ElementDraw& a, const menu::ElementDraw& b) { return a.box.y > b.box.y; });
        return out;
    };
    auto cursor_y = [&] {
        for (const menu::ElementDraw& d : rows())
            if (d.element == &f.controller()->items()[0].element->alt[0]) return d.box.y;
        return -1000.0f;
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
    {
        auto r = rows();
        CHECK(r.size() == 2 && box_is(r[0], 40, 380, 300, 20) && box_is(r[1], 40, 356, 300, 20));
        for (const menu::ElementDraw& d : r)
            CHECK(near(d.clip.x, 40) && near(d.clip.y, 100) && near(d.clip.w, 420) && near(d.clip.h, 300));
        CHECK(draws_named(f, "scroll_bar").empty() && draws_named(f, "pinch").empty());
    }
    // Twenty cars: 20 x 20 + 19 x 4 = 476 units in the 300-unit window.
    press(f, MenuKey::Cancel);
    for (int i = 0; i < 18; ++i) CHECK(gt.buy_car(w::kCoupe, 0) == 0);
    choose(f, "hm_gr");
    CHECK(page(f) == "hm/hm_gr" && gt.state().garage.size() == 20 && s->garage_cursor == 0);
    const float total = 476, ph = 300 * 300 / total;
    // The cursor row at offset `off` (row c's top 400 - off - 24 c), the scroll bar's track and
    // its pinch.
    auto at = [&](int c, float off) {
        auto p = draws_named(f, "pinch");
        return s->garage_cursor == c && near(cursor_y(), 400 - off - 24.0f * float(c) - 20) &&
               draws_named(f, "scroll_bar").size() == 1 && p.size() == 1 &&
               box_is(p[0], 20, 400 + off * 300 / total - ph, 8, ph);
    };
    auto settle_list = [&] {
        for (int i = 0; i < 60; ++i) f.tick(0);
    };
    CHECK(at(0, 0));
    CHECK(rows().size() == 13 && near(rows().back().box.y, 92)); // rows 0-12, the last one in part
    // Down: the cursor sound; the list stays at the top while the cursor row starts less than
    // 150 units down (row 6 at 144).
    f.take_sounds();
    press(f, MenuKey::Down);
    CHECK(s->garage_cursor == 1 && has_sound(f.take_sounds(), "cursor"));
    s->garage_cursor = 6;
    settle_list();
    CHECK(at(6, 0));
    // Row 10 centred: offset 150 - (240 + 10); rows 4 to 16 shown, the two ends in part.
    s->garage_cursor = 10;
    settle_list();
    CHECK(at(10, -100));
    {
        auto r = rows();
        CHECK(r.size() == 13 && near(r.front().box.y, 384) && near(r.back().box.y, 96));
    }
    // One frame towards row 11's -124: 30 % of the way (-107.2), then -112.24.
    s->garage_cursor = 11;
    f.tick(0);
    CHECK(at(11, -107.2f));
    f.tick(0);
    CHECK(at(11, -112.24f));
    settle_list();
    CHECK(at(11, -124));
    // Near the end the list is held at the window's bottom (300 - 476); Down stops at the last.
    s->garage_cursor = 13;
    settle_list();
    CHECK(at(13, -176));
    for (int i = 0; i < 8; ++i) press(f, MenuKey::Down);
    settle_list();
    CHECK(at(19, -176) && near(cursor_y(), 100));
    // A hovering pointer: over row 15 (its top at 400 + 176 - 360) the cursor moves there and the
    // list holds still; a key lets it ease again.
    s->garage_cursor = 10;
    settle_list();
    CHECK(f.hover(100, 400 + 100 - 24 * 12 - 10)); // row 12, 10 units into it
    settle_list();
    CHECK(at(12, -100));
    CHECK(f.hover(100, 400 + 100 - 24 * 12 - 10) && s->garage_cursor == 12); // still over it
    CHECK(!f.hover(100, 420)); // above the window
    press(f, MenuKey::Down);
    settle_list();
    CHECK(at(13, -176));
    // A tap on another row moves the cursor; on the cursor row it opens the car.
    CHECK(f.tap(100, 400 + 176 - 24 * 14 - 10));
    f.tick(0);
    settle(f);
    settle_list();
    CHECK(at(14, -176) && page(f) == "hm/hm_gr");
    CHECK(f.tap(100, 400 + 176 - 24 * 14 - 10));
    f.tick(0);
    settle(f);
    CHECK(page(f) == "hm/hm_car");
    // Back on the list: placed at once on the cursor row's offset.
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "hm/hm_gr" && at(14, -176));
    // The trade list: the guest garage, the same list.
    s->guest = std::make_unique<game::GtMode>(gt.data_ptr());
    s->guest->state().credits = 100000000;
    for (int i = 0; i < 3; ++i) CHECK(s->guest->buy_car(w::kWagon, 0) == 0);
    press(f, MenuKey::Cancel);
    CHECK(f.controller()->open("hm/hm_gr_trade"));
    settle(f);
    CHECK(page(f) == "hm/hm_gr_trade" && s->trade && s->garage_cursor == 0);
    {
        auto r = rows();
        CHECK(r.size() == 3 && near(r[0].box.y, 380) && near(r[1].box.y, 356) && near(r[2].box.y, 332));
        CHECK(draws_named(f, "scroll_bar").empty() && draws_named(f, "pinch").empty());
    }
    s->trade = false;
    s->guest.reset();
}

// The tune shop: no ride car, buying and fitting a part through the popup chain, the part
// page's texts and the maker logo, already owned.
TEST(gtmode_synth_tune_shop) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    choose(f, "gt_mode");
    choose(f, "ts_ts");
    CHECK(page(f) == "00cmn/ride_car" && f.controller()->depth() == 1);
    press(f, MenuKey::Cancel);
    CHECK(gt.buy_car(w::kCoupe) == 0);
    choose(f, "ts_ts");
    choose(f, "bg2");
    choose(f, "item1");
    CHECK(page(f) == "ts/ts_eg_muf_sp");
    CHECK(text_of(f, "price_muf_sports") == " 1,500");
    CHECK(text_of(f, "disp_ps_befor") == "0HP" && text_of(f, "disp_ps_after") == "0HP"); // (no engine data)
    {
        auto logo = draws_named(f, "acmemotors"); // "ACME MOTORS"
        CHECK(logo.size() == 1 && box_is(logo[0], 40, 60, 80, 40));
    }
    choose(f, "muf_sports");
    CHECK(page(f) == "ts/ts_buy_sure1" && s->focus_goods == 37);
    choose(f, "yes1");
    CHECK(gt.owns(37) && gt.state().credits == w::kStartCredits - 12000 - w::kMufflerPrice);
    CHECK(page(f) == "ts/ts_fit_sure1" && f.controller()->depth() == 1);
    choose(f, "yes1");
    CHECK(page(f) == "ts/ts_fit_end1" && f.controller()->depth() == 1);
    CHECK(gt.state().ride_car()->parts[21] == db::id_hash("mu_syn_coupe_01_1"));
    CHECK(text_of(f, "car_previous_torque") == "0 HP" && text_of(f, "car_current_torque") == "0 HP");
    choose(f, "yes1");
    CHECK(page(f) == "ts/ts_eg_muf_sp" && f.controller()->depth() == 0);
    CHECK(text_of(f, "price_muf_sports") == "Purchased");
    choose(f, "muf_sports");
    CHECK(page(f) == "ts/already_you_have");
}

// The race pages' focus (menus §4.2): a race series or licence page opens on the item of the
// stored race code, and every build of such a page clears the code.
TEST(gtmode_synth_race_page_focus) {
    using H = RacePageHook;
    auto items = [](std::initializer_list<const char*> actions) {
        std::vector<menu::MblItem> v;
        for (const char* a : actions) v.emplace_back().action = a;
        return v;
    };
    const auto series = items({"set_race_code_info", "set_race_code", "set_race_code"});
    const auto champ = items({"set_race_code_info", "set_race_code_champ", "set_race_code"});
    CHECK(race_page_hook("r/beginner/gsunday_e", series) == H::Series);
    CHECK(race_page_hook("r/amateur/ggtworld_n", champ) == H::ChampionshipSeries);
    CHECK(race_page_hook("r/beginner/gsunday_e_entry", series) == H::None);
    CHECK(race_page_hook("r/end/Eakasaka_n", series) == H::None); // (the list holds it in lower case)
    CHECK(race_page_hook("r/beginner/beginner", items({"", ""})) == H::None);
    CHECK(race_page_hook("lc/lc_b", items({"go_license"})) == H::License && race_page_hook("lc/lc", {}) == H::None);
    CHECK(race_page_focus(H::Series, "Gsunday_e_0002", 4) == 2 && race_page_focus(H::Series, "", 4) == 0);
    CHECK(race_page_focus(H::ChampionshipSeries, "Ggtworld_n_1010", 4) == 11); // a race: after the championship
    CHECK(race_page_focus(H::ChampionshipSeries, "Ggtworld_n_1001", 2) == 1);  // the championship item
    CHECK(race_page_focus(H::ChampionshipSeries, "", 2) == 0 && race_page_focus(H::ChampionshipSeries, "7", 4) == 0);
    CHECK(race_page_focus(H::License, "L0B0003", 0) == 2 && race_page_focus(H::License, "L0B0009", 0) == 0);
    CHECK(race_page_focus(H::License, "M400", 0) == 0 && race_page_focus(H::License, "", 0) == 0);
    CHECK(race_page_focus(H::None, "Gsunday_e_0002", 4) == -1);

    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    CHECK(s->gt().buy_car(w::kCoupe) == 0);
    auto focused = [&] { return f.controller()->focused() ? f.controller()->focused()->flow->name : std::string(); };
    choose(f, "gt_mode");
    choose(f, "r_r");
    choose(f, "beginner");
    // First visit (nothing stored): the first item, the race information.
    choose(f, "Gsyn_e_0001");
    CHECK(page(f) == "r/beginner/gsyn_e" && focused() == "Gsyn_e_0002_info");
    // Back out of the entry page: the race chosen (not the first item leading to the entry page).
    choose(f, "Gsyn_e_0002");
    CHECK(page(f) == "r/beginner/gsyn_e_entry");
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "r/beginner/gsyn_e" && focused() == "Gsyn_e_0002");
    // That build cleared the code: from the tier page again, the first item.
    press(f, MenuKey::Cancel);
    choose(f, "Gsyn_e_0001");
    CHECK(page(f) == "r/beginner/gsyn_e" && focused() == "Gsyn_e_0002_info");
    // The race information stores its race; closing the popup builds nothing, so the next
    // build of a race page reads it (here the same series, entered again).
    choose(f, "Gsyn_e_0002_info");
    CHECK(page(f) == "00cmn/race_info");
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "r/beginner");
    choose(f, "Gsyn_e_0001");
    CHECK(focused() == "Gsyn_e_0002");
    // Back after a race: the race just run.
    choose(f, "Gsyn_e_0002");
    choose(f, "menu1");
    std::optional<GameRequest> r = wait_request(f);
    CHECK(r.has_value());
    if (!r) return;
    finish_game(f, win(*r));
    CHECK(page(f) == "gtmode_mini");
    for (int i = 0; i < 300; ++i) f.tick(0);
    press(f, MenuKey::Confirm);
    CHECK(f.control() == "gt_mode" && page(f) == "r/beginner/gsyn_e" && focused() == "Gsyn_e_0002");
    // A championship series: a single race after the championship item.
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    choose(f, "amateur");
    choose(f, "Gsynch_n_0500");
    CHECK(page(f) == "r/amateur/gsynch_n" && focused() == "Gsynch_n_0501_info");
    choose(f, "Gsynch_n_0502");
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "r/amateur/gsynch_n" && focused() == "Gsynch_n_0502");
    choose(f, "Gsynch_n_0500");
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "r/amateur/gsynch_n" && focused() == "Gsynch_n_0500");
    // A licence page: first visit the first test; after a test that test.
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "map");
    choose(f, "lc_lc");
    choose(f, "lc_b");
    CHECK(page(f) == "lc/lc_b" && focused() == "L0B0001");
    choose(f, "L0B0003");
    r = wait_request(f);
    CHECK(r && r->race_code == "L0B0003");
    if (!r) return;
    GameResult res;
    res.mode = r->mode;
    res.race_code = r->race_code;
    res.event = r->event;
    finish_game(f, res); // (not finished: no medal, nothing to show)
    CHECK(f.control() == "gt_mode" && page(f) == "lc/lc_b" && focused() == "L0B0003");
    press(f, MenuKey::Cancel);
    choose(f, "lc_b");
    CHECK(page(f) == "lc/lc_b" && focused() == "L0B0001");
}

// Races: the tier and series pages' code-drawn art (event status frames and marks, course
// logos), a locked series, the race information popup and its Cars Permitted list,
// entering a race (the GameRequest GT mode prepares), the result (the race prize page
// counting the prize into the credits) and the series prize car.
TEST(gtmode_synth_race_and_prizes) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    gt.state().credits = 100000;
    CHECK(gt.buy_car(w::kCoupe) == 0);
    choose(f, "gt_mode");
    choose(f, "r_r");
    choose(f, "beginner");
    CHECK(page(f) == "r/beginner");
    // event_status: a frame per race of the series (the result slots from the item's code on),
    // f + 8 apart, the row's left edge n (f + 4) / 2 left of the widget's centre.
    {
        auto frames = draws_named(f, "frame");
        CHECK(frames.size() == 2 && box_is(frames[0], 226, 300, 30, 30) && box_is(frames[1], 264, 300, 30, 30));
        CHECK(draws_named(f, "mark_1").empty() && draws_named(f, "lc_mark_b").empty());
    }
    // The pro series needs the S licence (gcf unlock level 1): disabled.
    press(f, MenuKey::Cancel);
    choose(f, "pro");
    CHECK(page(f) == "r/pro" && !f.controller()->item_enabled(0));
    f.take_sounds();
    press(f, MenuKey::Confirm);
    CHECK(page(f) == "r/pro" && has_sound(f.take_sounds(), "disable"));
    press(f, MenuKey::Cancel);
    choose(f, "beginner");
    choose(f, "Gsyn_e_0001");
    CHECK(page(f) == "r/beginner/gsyn_e");
    // (First visit: the race information item; no course name for it.)
    CHECK(f.controller()->focus() == 0 && draws_named(f, "syn_ring_doc").empty());
    CHECK(f.controller()->set_focus("Gsyn_e_0001"));
    // Each race button gets its course logo from its event_status_button widget, at the logo's
    // layout place (4, 413) taken from the button's top-left corner, its own size, half bright
    // off the focus (the third widget is past the list: nothing); the focused race's course name
    // in the text bar.
    {
        auto logos = draws_named(f, "syn_ring");
        CHECK(logos.size() == 2 && box_is(logos[0], 44, 305, 55, 42) && box_is(logos[1], 264, 305, 55, 42));
        CHECK(logos.size() == 2 && logos[0].brightness == 1.0f && logos[1].brightness == 0.5f);
        auto doc = draws_named(f, "syn_ring_doc");
        CHECK(doc.size() == 1 && box_is(doc[0], 0, 20, 640, 24));
        CHECK(draws_named(f, "mark_1").empty());
    }
    // Race information: race 2's prizes and rules (a regulation list), Cars Permitted.
    choose(f, "Gsyn_e_0002_info");
    CHECK(page(f) == "00cmn/race_info" && s->race_code == "Gsyn_e_0002");
    CHECK(text_of(f, "race_prize_1") == "2,000" && text_of(f, "race_prize_6") == "300");
    CHECK(text_of(f, "regulation_license") == "No Limitations" && text_of(f, "regulation_dr_type") == "No Limitations");
    CHECK(text_of(f, "regulation_car") == "LIMITED");
    choose(f, "permitted");
    CHECK(page(f) == "00cmn/car_reguration" && f.controller()->depth() == 1);
    auto permitted = [&] {
        std::vector<menu::ElementDraw> out;
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.text && d.text_dx == menu::kTextInset && d.element && d.element->name == "link_menu")
                out.push_back(d);
        return out;
    };
    {
        auto rows = permitted();
        CHECK(rows.size() == 2);
        if (rows.size() == 2) {
            CHECK(*rows[0].text == "Synthetic Coupe" && box_is(rows[0], 60, 300, 300, 20));
            CHECK(*rows[1].text == "Synthetic Wagon Touring" && box_is(rows[1], 60, 276, 300, 20));
            CHECK(rows[0].element == &f.controller()->items()[0].element->alt[0]); // the cursor row
        }
        // (Rows 24 apart from the window's top, GT3's vertical button list; the list fits: no
        // scroll bar, track included.)
        CHECK(draws_named(f, "scroll_bar").empty() && draws_named(f, "pinch").empty());
    }
    press(f, MenuKey::Down);
    CHECK(s->regulated_cursor == 1);
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "r/beginner/gsyn_e" && f.controller()->depth() == 0);
    // Entering race 1: the request GT mode prepared; a day passes.
    choose(f, "Gsyn_e_0001");
    CHECK(page(f) == "r/beginner/gsyn_e_entry");
    choose(f, "menu1");
    std::optional<GameRequest> r = wait_request(f);
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->mode == "go_race" && r->race_code == "Gsyn_e_0001" && r->event == "Gsyn_e_0001");
    CHECK(r->page == "r/beginner/gsyn_e_entry");
    CHECK(r->car == w::kCoupe && r->car_model == "zz0001" && r->car_color == 0 && r->car_parts && !r->racing_gearbox);
    CHECK(r->opponents.size() == 5 && r->champ_round == 0);
    for (const std::string& o : r->opponents) CHECK(o == "syv_coupe_a" || o == "syv_wagon_b");
    CHECK(gt.state().day == 2);
    // The run starts with the ride car's engine condition (a new car: running in).
    CHECK(r->car_condition && r->garage_car == 0 && r->car_odometer == 0 && r->car_wear_m == -300000 &&
          r->car_oil_m == 0);
    // Won: the race prize page (the event's cup) counts the prize money into the credits.
    finish_game(f, win(*r));
    CHECK(f.control() == "gtmode_mini" && page(f) == "gtmode_mini" && f.gt_active());
    CHECK(gt.state().credits == 88000 + 1500 && gt.state().best_position("Gsyn_e_0001") == 0);
    CHECK(gt.state().garage[0].cond.odometer_m == 4000.0 && gt.state().wins == 1 && gt.state().races == 1);
    CHECK(gt.state().garage[0].cond.engine_wear_m == -296000 && gt.state().distance_m == 4000.0);
    CHECK(game::presentation_model("gtmode_mini") == "race/g_easy_gold.prm");
    CHECK(text_of(f, "rank_disp") == "1st");
    CHECK(text_of(f, "count") == "Cr. 88,000" && text_of(f, "get_money_disp") == "Prize Cr. 1,500");
    CHECK(!f.controller()->item_enabled(0) && draws_named(f, "ok").empty()); // OK once counted
    for (int i = 0; i < 300; ++i) f.tick(0);
    CHECK(text_of(f, "count") == "Cr. 89,500" && text_of(f, "get_money_disp") == "Prize Cr. 0");
    CHECK(f.controller()->item_enabled(0) && !draws_named(f, "ok").empty());
    press(f, MenuKey::Confirm);
    CHECK(f.control() == "gt_mode" && page(f) == "r/beginner/gsyn_e"); // the entry page's parent
    {
        // The race's best result in the button's round slot (mark_1 at (61, 412) from the
        // button's top-left corner), whatever the button's width.
        auto mark = draws_named(f, "mark_1");
        CHECK(mark.size() == 1 && box_is(mark[0], 101, 304, 32, 32));
    }
    // Every race of the series won: the series prize car (car_get) after the cup.
    gt.state().race_best["Gsyn_e_0002"] = 0;
    choose(f, "Gsyn_e_0001");
    choose(f, "menu1");
    r = wait_request(f);
    CHECK(r.has_value());
    if (!r) return;
    finish_game(f, win(*r));
    CHECK(page(f) == "gtmode_mini");
    for (int i = 0; i < 300; ++i) f.tick(0);
    press(f, MenuKey::Confirm);
    CHECK(f.control() == "car_get" && page(f) == "car_get");
    CHECK(gt.state().garage.size() == 2 && gt.state().garage[0].car == w::kRacer);
    CHECK(gt.state().garage[0].origin == "Gsyn_e_0000" && gt.state().prize_cars == 1);
    {
        auto why = draws_named(f, "solo_race_clear"); // the reason line, in the event_message box
        CHECK(why.size() == 1 && box_is(why[0], 100, 400, 400, 24));
    }
    std::string car;
    int paint = -1;
    CHECK(game::car_for_widget("car_get", "render_model", car, paint) && car == w::kRacer && paint == 0);
    press(f, MenuKey::Confirm);
    // The beginner league is complete (its only series won): the short ending, once.
    r = wait_request(f);
    CHECK(r && r->kind == GameRequest::Kind::PlayMovie && r->course == "stream/ending_short_full.pss");
    CHECK(gt.state().ending_short_seen && !gt.state().ending_long_seen);
    f.game_finished();
    settle(f);
    CHECK(f.control() == "gt_mode" && page(f) == "r/beginner/gsyn_e");
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "r/beginner" && draws_named(f, "mark_1").size() == 2 && draws_named(f, "frame").size() == 2);
    // The status page after two wins.
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    choose(f, "st_st");
    CHECK(page(f) == "status");
    CHECK(text_of(f, "status_ttl_wins") == "2" && text_of(f, "status_avg_win") == "100.0 %");
    CHECK(text_of(f, "status_ttl_milage") == "5.0 miles" && text_of(f, "status_prize_money") == " 3,000");
    CHECK(text_of(f, "status_no_cars") == "2" && text_of(f, "status_ttl_car_price") == " 102,000");
}

// gt_mode §23: the whole game's prize set (gtmode_complete) is checked after a GT-mode win
// with its code: with every GT-mode result slot won (gcf section 4) its one car comes once, in a
// random paint; one result short gives nothing, and an arcade check never gives it.
TEST(gtmode_synth_game_complete_prize) {
    World world;
    auto data = std::make_shared<game::GtData>();
    CHECK(data->load(world.read));
    auto origin_count = [](const game::GtMode& g) {
        int n = 0;
        for (const game::GarageCar& c : g.state().garage) n += c.origin == "gtmode_complete";
        return n;
    };
    {
        game::GtMode gt(data);
        gt.state().credits = 100000;
        CHECK(gt.buy_car(w::kWagon) == 0);
        gt.state().prizes_given.insert("Gsyn_e_0000"); // the series set already given
        // Four of the five results won: nothing.
        for (const std::string& c : data->race_codes)
            if (c != "Gsyn_e_0002") gt.state().race_best[c] = 0;
        CHECK(!gt.all_races_won());
        game::Award a = gt.record_race("Gsyn_e_0001", 0);
        CHECK(a.new_cars.empty() && origin_count(gt) == 0);
        // An arcade check with everything won: nothing either.
        gt.state().race_best["Gsyn_e_0002"] = 0;
        CHECK(gt.all_races_won());
        CHECK(gt.award_prizes("ANCT0001").empty() && gt.award_prizes("arcade_compulsory_complete").empty());
        CHECK(origin_count(gt) == 0);
        // The last win: the set's car, at the head of the garage, in one of its two paints.
        a = gt.record_race("Gsyn_e_0002", 0);
        CHECK(a.new_cars.size() == 1 && origin_count(gt) == 1);
        if (a.new_cars.size() == 1) {
            const game::GarageCar& c = gt.state().garage[size_t(a.new_cars[0])];
            CHECK(c.car == w::kCoupe && c.color >= 0 && c.color < 2 && c.cond.engine_wear_m == -300000);
        }
        CHECK(gt.state().prizes_given.count("gtmode_complete") == 1 && gt.state().prize_cars == 1);
        // Once: more wins (a race, a licence set) give it no more.
        CHECK(gt.record_race("Gsyn_e_0001", 0).new_cars.empty());
        CHECK(gt.award_prizes("L0B0000").empty() && origin_count(gt) == 1);
    }
    {
        // A licence all-gold check counts as a GT-mode result too.
        game::GtMode gt(data);
        for (const std::string& c : data->race_codes) gt.state().race_best[c] = 0;
        std::vector<int> got = gt.award_prizes("L0B0000");
        CHECK(origin_count(gt) == 1);
        (void)got;
    }
    // Through the menus: the last race won, then the car_get page names the reason.
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    gt.state().credits = 100000;
    CHECK(gt.buy_car(w::kCoupe) == 0);
    gt.state().prizes_given.insert("Gsyn_e_0000");
    for (const std::string& c : data->race_codes)
        if (c != "Gsyn_e_0001") gt.state().race_best[c] = 0;
    choose(f, "gt_mode");
    choose(f, "r_r");
    choose(f, "beginner");
    choose(f, "Gsyn_e_0001");
    CHECK(f.controller()->set_focus("Gsyn_e_0001"));
    choose(f, "Gsyn_e_0001");
    choose(f, "menu1");
    std::optional<GameRequest> r = wait_request(f);
    CHECK(r.has_value());
    if (!r) return;
    finish_game(f, win(*r));
    CHECK(page(f) == "gtmode_mini");
    for (int i = 0; i < 300; ++i) f.tick(0);
    press(f, MenuKey::Confirm);
    CHECK(f.control() == "car_get" && page(f) == "car_get");
    CHECK(gt.state().garage.size() == 2 && gt.state().garage[0].origin == "gtmode_complete");
    CHECK(draws_named(f, "gtmode_complete").size() == 1 && draws_named(f, "solo_race_clear").empty());
}

namespace {

// Ticks `frames` frames; for each condition the frame (0 = now) on which it first held, -1
// when it never did.
std::vector<int> first_frames(GameFlow& f, int frames, const std::vector<std::function<bool()>>& conds) {
    std::vector<int> at(conds.size(), -1);
    for (int i = 0; i < frames; ++i) {
        for (size_t k = 0; k < conds.size(); ++k)
            if (at[k] < 0 && conds[k]()) at[k] = i;
        f.tick(0);
    }
    return at;
}

bool layer_on(const GameFlow& f, const std::string& name) {
    const MenuController* c = f.controller();
    if (!c || !c->page()) return false;
    const MenuSet::PageRef ref = c->page();
    const auto& layers = ref.layout().layers;
    for (size_t i = 0; i < layers.size(); ++i)
        if (layers[i].name == name) return c->layer_visible(int(i));
    return false;
}

} // namespace

// A championship: the champ item starts it at round 1, each round's prize page, the next
// round runs straight away, the title gives the trophy presentation (the bonus counted
// in on its timeline) and the championship's prize car.
TEST(gtmode_synth_championship) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    gt.state().credits = 100000;
    CHECK(gt.buy_car(w::kCoupe) == 0);
    choose(f, "gt_mode");
    choose(f, "r_r");
    choose(f, "amateur");
    choose(f, "Gsynch_n_0500");
    CHECK(page(f) == "r/amateur/gsynch_n");
    choose(f, "Gsynch_n_0500"); // set_race_code_champ: round 1
    CHECK(s->race_code == "Gsynch_n_0501" && s->champ);
    choose(f, "menu1");
    std::optional<GameRequest> r = wait_request(f);
    CHECK(r.has_value());
    if (!r) return;
    CHECK(gt.state().championship.active() && gt.state().championship.races.size() == 2);
    CHECK(r->race_code == "Gsynch_n_0501" && r->champ_round == 1 && r->champ_rounds == 2 && !r->champ_last);
    CHECK(r->opponents == gt.state().championship.entrants && r->opponents.size() == 5);
    finish_game(f, win(*r));
    CHECK(page(f) == "gtmode_mini" && game::presentation_model("gtmode_mini") == "race/g_normal_gold.prm");
    CHECK(gt.state().credits == 88000 + 3000);
    for (int i = 0; i < 300; ++i) f.tick(0);
    press(f, MenuKey::Confirm);
    // Round 2 without the menus.
    r = wait_request(f);
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->race_code == "Gsynch_n_0502" && r->champ_round == 2 && r->champ_last);
    finish_game(f, win(*r));
    CHECK(!gt.state().championship.active() && gt.state().best_position("Gsynch_n_0500") == 0);
    CHECK(gt.state().credits == 88000 + 3000 + 3000 + 5000); // the rounds and the title bonus
    CHECK(page(f) == "gtmode_mini");
    for (int i = 0; i < 300; ++i) f.tick(0);
    press(f, MenuKey::Confirm);
    // The trophy: "mess0" for 180 frames, the model fading in from 180, the bonus box from
    // 270; the count from 360; OK (and "mess1") 15 frames after the count.
    CHECK(f.control() == "trophy_get" && page(f) == "trophy_get");
    CHECK(layer_on(f, "mess0") && !layer_on(f, "get_mess") && !layer_on(f, "mess1"));
    CHECK(game::presentation_model("trophy_get").empty());
    CHECK(text_of(f, "count") == "Cr. 94,000" && text_named(f, "get_money_disp") == "<none>");
    // Frames counted from the page's first idle frame (its timeline's frame 2).
    std::vector<int> at = first_frames(
        f, 800,
        {[&] { return !game::presentation_model("trophy_get").empty(); }, [&] { return !layer_on(f, "mess0"); },
         [&] { return layer_on(f, "get_mess") && text_named(f, "get_money_disp") == "Prize Cr. 5,000"; },
         [&] { return text_of(f, "count") != "Cr. 94,000"; }, [&] { return text_of(f, "count") == "Cr. 99,000"; },
         [&] { return layer_on(f, "mess1"); }, [&] { return f.controller()->item_enabled(0); }});
    CHECK((at == std::vector<int>{178, 178, 268, 365, 468, 483, 483}));
    CHECK(at[6] - at[4] == 15); // OK 15 frames after the count
    {
        int serial = -1;
        std::string model = game::presentation_model("trophy_get", &serial);
        CHECK(model.size() == 17 && model.rfind("menu/champ_0", 0) == 0 && model.substr(13) == ".prm" && serial > 0);
    }
    press(f, MenuKey::Confirm);
    // The championship's prize car (its fixed paint, Chalk White = index 2).
    CHECK(page(f) == "car_get" && gt.state().garage.size() == 2 && gt.state().garage[0].car == w::kWagon);
    CHECK(gt.state().garage[0].color == 2 && gt.state().garage[0].origin == "Gsynch_n_0500");
    CHECK(draws_named(f, "championship_win").size() == 1);
    press(f, MenuKey::Confirm);
    // The amateur league complete: the long ending, then the series page again.
    r = wait_request(f);
    CHECK(r && r->kind == GameRequest::Kind::PlayMovie && r->course == "stream/ending_long_full.pss");
    f.game_finished();
    settle(f);
    // (On the championship item: the stored code is still the championship's.)
    CHECK(f.control() == "gt_mode" && page(f) == "r/amateur/gsynch_n");
    CHECK(f.controller()->focused()->flow->name == "Gsynch_n_0500");
    // The championship's button (SERIES RACE) shows its own result: the champion's mark_1 in the
    // laurel's slot, no course logo; each round's button its round's best result, the last
    // (152 wide) in the same slot as the others.
    {
        auto marks = draws_named(f, "mark_1");
        CHECK(marks.size() == 3 && box_is(marks[0], 101, 304, 32, 32) && box_is(marks[1], 201, 304, 32, 32) &&
              box_is(marks[2], 301, 304, 32, 32));
        CHECK(marks.size() == 3 && marks[0].brightness == 1.0f && marks[2].brightness == 0.5f);
        auto logos = draws_named(f, "syn_ring");
        CHECK(logos.size() == 2 && box_is(logos[0], 144, 305, 55, 42) && box_is(logos[1], 244, 305, 55, 42));
    }
    // The league page: the championship's frame and mark, a frame's width of gap, an arrow frame
    // per round but the last (a plain frame), each round's mark over its disc.
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "r/amateur" && f.controller()->focused()->flow->name == "Gsynch_n_0500");
    {
        auto frames = draws_named(f, "frame");
        auto arrows = draws_named(f, "frame_arrow");
        auto marks = draws_named(f, "mark_1");
        CHECK(frames.size() == 2 && box_is(frames[0], 190, 300, 30, 30) && box_is(frames[1], 290, 300, 30, 30));
        CHECK(arrows.size() == 1 && box_is(arrows[0], 250, 300, 40, 30));
        CHECK(marks.size() == 3 && box_is(marks[0], 190, 300, 30, 30) && box_is(marks[1], 250, 300, 30, 30) &&
              box_is(marks[2], 290, 300, 30, 30));
    }
}

// Licences: the licence pages' checks (an earlier licence, the last test), the times and
// car texts, a test's result (the medal, a record -> the name entry), the licence card
// (its timeline) and the all-gold prize car; the cup on the page afterwards.
TEST(gtmode_synth_licenses_and_name_entry) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    choose(f, "gt_mode");
    choose(f, "lc_lc");
    choose(f, "lc_a");
    CHECK(page(f) == "lc/no_license_a" && f.controller()->depth() == 1);
    press(f, MenuKey::Cancel);
    choose(f, "lc_b");
    CHECK(page(f) == "lc/lc_b");
    CHECK(text_named(f, "L0B0001_gold") == "0'30.000" && text_named(f, "L0B0001_bronze") == "0'35.000");
    CHECK(text_named(f, "L0B0001_car") == "Synthetic Coupe" && text_named(f, "L0B0001_speed") == "31mph");
    CHECK(draws_named(f, "cup_gold").empty());
    choose(f, "L0B0008"); // needs tests 1-7
    CHECK(page(f) == "lc/no_last_exam");
    press(f, MenuKey::Cancel);
    // Tests 2-8 gold already; test 1 through the menus.
    for (int t = 1; t < 8; ++t) gt.state().license_tests[size_t(t)].medal = game::Medal::Gold;
    choose(f, "L0B0001");
    std::optional<GameRequest> r = wait_request(f);
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->mode == "go_license" && r->race_code == "L0B0001" && r->event == "L0B0001" && r->car == "syv_lic_b");
    CHECK(r->opponents.empty() && gt.state().day == 2);
    // The race enters the run's record as it ends (gt_mode §17); here its name entry waits in
    // the queue ahead of the result's presentations (the race screen shows it over the race).
    CHECK(game::add_license_record("L0B0001", 29.5) == 0);
    GameResult res = win(*r);
    res.total_time = 29.5;
    res.license_medal = 0;
    res.records_done = true;
    finish_game(f, res);
    CHECK(gt.state().license_tests[0].medal == game::Medal::Gold && gt.license_held(0) && gt.license_all_gold(0));
    CHECK((gt.state().trophy_flags & 1) != 0);
    // A record: the name entry first (the key's character appended, Back Space, OK).
    CHECK(f.control() == "name_entry" && page(f) == "name_entry");
    CHECK(gt.state().rankings["lic:L0B0001"].size() == 1 && gt.state().rankings["lic:L0B0001"][0].value == 29.5);
    CHECK(text_of(f, "text_box").empty());
    auto key = [&](int k) {
        CHECK(f.controller()->set_focus("free_cursor"));
        f.controller()->set_choice("free_cursor", k);
        press(f, MenuKey::Confirm);
    };
    key(1);
    key(0);
    key(2);
    CHECK(gt.state().player_name == "BAC" && text_of(f, "text_box") == "BAC");
    f.take_sounds();
    press(f, MenuKey::Cancel); // Back Space
    CHECK(gt.state().player_name == "BA" && has_sound(f.take_sounds(), "cursor") && page(f) == "name_entry");
    auto caret_x = [&] {
        auto caret = draws_named(f, "cursor"); // after the text before the cursor, 12 units in
        CHECK(caret.size() == 1 && caret[0].has_box);
        return caret.empty() ? 0.0f : caret[0].box.x;
    };
    const float end_x = caret_x();
    CHECK(end_x > 40 + 12);
    // L1 / R1 move the text box's cursor; a key's character goes in there, Back Space steps
    // left and deletes the character there (at the start: the first one).
    press(f, MenuKey::L1);
    CHECK(s->name_box.cursor == 1 && has_sound(f.take_sounds(), "cursor") && caret_x() < end_x);
    key(2); // C
    CHECK(gt.state().player_name == "BCA" && s->name_box.cursor == 2);
    press(f, MenuKey::Cancel);
    CHECK(gt.state().player_name == "BA" && s->name_box.cursor == 1);
    press(f, MenuKey::L1);
    f.take_sounds();
    press(f, MenuKey::L1); // already at the start
    CHECK(s->name_box.cursor == 0 && !has_sound(f.take_sounds(), "cursor") && caret_x() == 40 + 12 + 1);
    press(f, MenuKey::Cancel);
    CHECK(gt.state().player_name == "A" && s->name_box.cursor == 0);
    key(1); // B
    press(f, MenuKey::R1);
    press(f, MenuKey::R1);
    CHECK(gt.state().player_name == "BA" && s->name_box.cursor == 2 && caret_x() == end_x);
    choose(f, "free_command");
    CHECK(gt.state().rankings["lic:L0B0001"][0].name == "BA");
    // The licence card: "<x>_mess0" for 240 frames, the model from 120, "<x>_mess1" from
    // 360, OK from 420 (counted here from the page's first idle frame, its timeline's frame 2).
    CHECK(f.control() == "lc_get" && page(f) == "license_get");
    CHECK(layer_on(f, "b_mess0") && !layer_on(f, "b_mess1") && !f.controller()->item_enabled(0));
    CHECK(game::presentation_model("license_get").empty());
    std::vector<int> at = first_frames(f, 600,
                                       {[&] { return game::presentation_model("license_get") == "menu/lic_b.prm"; },
                                        [&] { return !layer_on(f, "b_mess0"); }, [&] { return layer_on(f, "b_mess1"); },
                                        [&] { return f.controller()->item_enabled(0); }});
    CHECK((at == std::vector<int>{118, 238, 358, 418}));
    press(f, MenuKey::Confirm);
    // All gold: the licence's prize car (Night Blue = index 1).
    CHECK(page(f) == "car_get" && draws_named(f, "license_b_all_gold").size() == 1);
    CHECK(gt.state().garage.size() == 1 && gt.state().garage[0].car == w::kWagon && gt.state().garage[0].color == 1);
    press(f, MenuKey::Confirm);
    CHECK(f.control() == "gt_mode" && page(f) == "lc/lc_b");
    {
        auto cup = draws_named(f, "cup_gold");
        CHECK(cup.size() == 1 && box_is(cup[0], 200, 400, 30, 20));
    }
    // The status page: the B licence mark (lc_mark2: the marks are in letter order).
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    choose(f, "st_st");
    CHECK(draws_named(f, "lc_mark2").size() == 1 && draws_named(f, "lc_mark1").empty());
    CHECK(text_of(f, "status_no_lc_g") == "8");
}

// The prize counter itself (prize_count_up.h, presentation §F), frame by frame from the
// page's first frame: the single-precision steps, beeps and Confirm.
TEST(gtmode_synth_prize_counter_steps) {
    // Runs a count of `money` (no presses): the frame it finishes on, its step count and the
    // steps that beep.
    struct Run {
        int done_frame = -1, steps = 0;
        std::vector<int> beeps;
    };
    auto run = [](s64 money) {
        game::PrizeCountUp c;
        c.cur = 50000;
        c.target = c.cur + money;
        Run r;
        for (int frame = 0; frame < 2000 && !c.done(); ++frame) {
            if (game::step_prize_count_up(c, false)) r.beeps.push_back(c.n);
            if (c.done()) r.done_frame = frame, r.steps = c.n;
        }
        return r;
    };
    // 500 Cr: the first step on frame 122, done on the 99th step (frame 220). A beep every
    // 5th step of the creep (5/21 Cr a step), every 3rd once accelerating, none on the step
    // that finishes.
    Run r = run(500);
    std::vector<int> beeps;
    for (int k = 5; k <= 90; k += 5) beeps.push_back(k);
    beeps.push_back(93);
    beeps.push_back(96);
    CHECK(r.done_frame == 220 && r.steps == 99 && r.beeps == beeps);
    r = run(1000);
    CHECK(r.done_frame == 222 && r.steps == 101 && r.beeps.size() == 21);
    r = run(5000);
    CHECK(r.done_frame == 229 && r.steps == 108 && r.beeps.size() == 23);
    // The creep in single precision: 5 Cr only on the 22nd step (exactly 5/21 a step would
    // give it on the 21st).
    game::PrizeCountUp c;
    c.target = 1000000000;
    c.delay = 0;
    while (c.n < 21) game::step_prize_count_up(c, false);
    CHECK(c.cur == 4 && c.state == 1);
    game::step_prize_count_up(c, false);
    CHECK(c.cur == 5);
    // 91 creep steps; then the acceleration (its cap is tested before adding: one frame over).
    while (c.n < 91) game::step_prize_count_up(c, false);
    CHECK(c.state == 2 && c.a == 0.0f && c.v == 0.2380952388048172f);
    game::step_prize_count_up(c, false);
    CHECK(c.a == 5.2720417976379395f && c.v == 0.2380952388048172f + 5.2720417976379395f);
    float amax = 0;
    while (c.n < 200) game::step_prize_count_up(c, false), amax = std::max(amax, c.a);
    CHECK(c.a == 474.4837646484375f && amax > c.a && amax < c.a + 5.3f);
    // The velocity cap: the acceleration drops to 0, every step is the cap.
    while (c.a != 0.0f && c.n < 1000) game::step_prize_count_up(c, false);
    s64 before = c.cur;
    game::step_prize_count_up(c, false);
    CHECK(c.n < 1000 && c.v == 142345.125f && c.a == 0.0f && c.cur - before >= 142345 && c.cur - before <= 142346);
    // Confirm while accelerating: speed 3 from that frame on, the tripled step stored back
    // as the velocity (it triples again on the next frame); the second press completes the
    // count on its frame, without a beep.
    game::PrizeCountUp t;
    t.target = 1000000000;
    t.delay = 0;
    while (t.n < 100) game::step_prize_count_up(t, false);
    float v = t.v, a = t.a;
    game::step_prize_count_up(t, true);
    CHECK(t.speed == 3.0f && t.a == a + 5.2720417976379395f && t.v == (v + t.a) * 3.0f);
    v = t.v, a = t.a;
    game::step_prize_count_up(t, false);
    CHECK(t.v == (v + t.a) * 3.0f && t.v > 3.0f * v && t.cur < t.target);
    CHECK(!game::step_prize_count_up(t, true) && t.done() && t.cur == t.target);
    // Presses during the wait are ignored (Confirm is read only while counting).
    game::PrizeCountUp w;
    w.target = 500;
    for (int i = 0; i < 122; ++i) game::step_prize_count_up(w, true);
    CHECK(w.n == 0 && w.presses == 0 && w.speed == 1.0f);
}

// The race prize page shown from the race (queue_race_prize + start_presentations): the
// prize counter's timeline frame by frame (presentation §F: a 120-frame wait, a slow
// creep, then the acceleration; the balance not yet credited counts up), and Confirm.
TEST(gtmode_synth_prize_counter_timeline) {
    World world;
    GameFlow menus(world.read, "US");
    game::GtModeSession* s = start_title(menus);
    CHECK(s != nullptr);
    if (!s) return;
    s->gt().state().credits = 50000;
    CHECK(!game::queue_race_prize("Gsyn_e_0002", 6) && !game::queue_race_prize("L0B0001", 0));
    CHECK(game::queue_race_prize("Gsyn_e_0002", 3)); // 4th: the consolation prize, 500 Cr.
    GameFlow f(world.read, "US");
    CHECK(f.start_presentations());
    CHECK(f.control() == "consolation_prize" && page(f) == "consolation_prize");
    CHECK(game::presentation_model("consolation_prize") == "race/consolation.prm");
    std::vector<std::string> counts;
    std::vector<std::string> left;
    int sounds = 0;
    std::vector<int> beep_frames;
    for (int i = 0; i < 400; ++i) {
        f.tick(0);
        counts.push_back(text_of(f, "count"));
        left.push_back(text_of(f, "get_money_disp"));
        for (const std::string& se : f.take_sounds())
            if (se == "cursor") ++sounds, beep_frames.push_back(i);
    }
    CHECK(text_of(f, "rank_disp") == "4th");
    auto first = [&](const std::string& v) {
        return int(std::find(counts.begin(), counts.end(), v) - counts.begin());
    };
    CHECK(counts.front() == "Cr. 50,000" && left.front() == "Prize Cr. 500");
    CHECK(counts.back() == "Cr. 50,500" && left.back() == "Prize Cr. 0");
    CHECK(s->gt().state().credits == 50000); // the race's result credits it later
    // Frames counted from the page's opening (10 settle frames first): the counter steps on
    // every frame (its first step is the page's frame 122) and finishes on its 99th step, the
    // page's frame 220; as in the original game, a beep plays on the frame of a step and never
    // holds the count.
    CHECK(first("Cr. 50,001") == 135 && first("Cr. 50,500") == 229 && sounds == 20);
    CHECK(beep_frames.front() == 135 && beep_frames[1] == 140 && beep_frames.back() == 226); // steps 5, 10, 96
    CHECK(counts[220] == "Cr. 50,021" && left[220] == "Prize Cr. 479");
    for (size_t i = 1; i < counts.size(); ++i) CHECK(counts[i] >= counts[i - 1]); // (same width: text order)
    // Confirm while counting: the first press triples the speed, the second completes it.
    CHECK(game::queue_race_prize("Gsyn_e_0002", 3));
    GameFlow g(world.read, "US");
    CHECK(g.start_presentations());
    for (int i = 0; i < 140; ++i) g.tick(0);
    std::string before = text_of(g, "count");
    press(g, MenuKey::Confirm);
    CHECK(text_of(g, "count") != "Cr. 50,500" && page(g) == "consolation_prize");
    press(g, MenuKey::Confirm);
    CHECK(text_of(g, "count") == "Cr. 50,500" && before != "Cr. 50,500");
    for (int i = 0; i < 2; ++i) g.tick(0);
    CHECK(g.controller()->item_enabled(0));
    press(g, MenuKey::Confirm);
    for (int i = 0; i < 60 && !g.exited(); ++i) g.tick(0);
    CHECK(g.exited());
}

// Machine test: the request (its race code is the item), a record run -> the name entry,
// the machine records, and the records menu's code-drawn ranking rows.
TEST(gtmode_synth_machine_test_and_records) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    choose(f, "gt_mode");
    choose(f, "mt_mt");
    CHECK(page(f) == "00cmn/ride_car");
    press(f, MenuKey::Cancel);
    CHECK(gt.buy_car(w::kCoupe) == 0);
    choose(f, "mt_mt");
    choose(f, "M400");
    std::optional<GameRequest> r = wait_request(f);
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->mode == "go_machine_test" && r->race_code == "M400" && r->event == "M400" && r->car == w::kCoupe);
    // The race records the run as it ends (gt_mode §17); here its name entry is still queued
    // when the menus resume.
    CHECK(game::add_machine_test_record("M400", 12.345) == 0);
    GameResult res = win(*r);
    res.total_time = 12.345;
    res.records_done = true;
    finish_game(f, res);
    CHECK(gt.state().machine.t400 == 12.345 && gt.state().machine.car400 == w::kCoupe);
    CHECK(f.control() == "name_entry" && page(f) == "name_entry");
    CHECK(f.controller()->set_focus("free_cursor"));
    f.controller()->set_choice("free_cursor", 2);
    press(f, MenuKey::Confirm);
    choose(f, "free_command");
    CHECK(f.control() == "gt_mode" && page(f) == "mt/mt");
    // A second, slower entry (no name entry: added directly), stamped 2026-10-16.
    const u32 stamp0 = gt.state().rankings["mt:M400"][0].stamp;
    CHECK(stamp0 != 0);
    CHECK(gt.add_record("mt:M400", 13.5, w::kWagon, true, 12) == 1);
    gt.state().rankings["mt:M400"][1].stamp = 0x3264FD64;
    CHECK(game::begin_records("mt:M400") && s->records_key == "mt:M400");
    GameFlow rec(world.read, "US");
    rec.start("records", "records_free_tt");
    settle(rec);
    CHECK(rec.control() == "records" && page(rec) == "records_free_tt" && rec.gt_active());
    // Every place of the table, 28 apart from the window's top (the 24-unit bars and the gap of
    // 4; the twelve fill the window): the bar and the texts; the empty ones with no name, the
    // time and date placeholders and no car.
    auto texts = [&](const char* name) {
        std::vector<std::string> out;
        for (const menu::ElementDraw& d : rec.draw_list())
            if (d.element && d.element->name == name && d.text) out.push_back(*d.text);
        return out;
    };
    auto places = [](std::vector<std::string> first, const std::string& rest) {
        first.resize(12, rest);
        return first;
    };
    CHECK((texts("record_rank") == std::vector<std::string>{"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"}));
    CHECK(texts("record_name") == places({"C", "C"}, "")); // (the last name entered)
    CHECK(texts("record_car") == places({"Synthetic Coupe ", "Synthetic Wagon Touring"}, ""));
    CHECK(texts("record_data") == places({"0'12.345", "0'13.500"}, "--'--.---"));
    CHECK(texts("record_date") == places({game::record_date_text(stamp0), "16/10/26"}, "--/--/--"));
    CHECK(draws_named(rec, "record_car_pict").size() == 12);
    int bars = 0;
    for (const menu::ElementDraw& d : rec.draw_list())
        if (d.element && d.element->name == "record_bar")
            bars += box_is(d, 40, 336, 560, 24) || box_is(d, 40, 56, 560, 24);
    CHECK(bars == 2);
    // The first place has the cursor: its bar in the item's focused look.
    int focused = 0;
    for (const menu::ElementDraw& d : rec.draw_list())
        focused += d.element == &rec.controller()->items()[0].element->alt[0] && d.has_clip && box_is(d, 40, 364, 560, 24);
    CHECK(focused == 1);
    for (const menu::ElementDraw& d : rec.draw_list())
        if (d.element && d.element->name == "record_name" && d.text)
            CHECK(d.box.w == 200); // left-aligned: room to the right
    // Up / Down move the cursor over the places (the plain list's "select"; held at the ends);
    // a hovering pointer moves it too.
    auto cursor_bar = [&] {
        for (const menu::ElementDraw& d : rec.draw_list())
            if (d.element == &rec.controller()->items()[0].element->alt[0] && d.has_clip) return d.box.y;
        return -1.0f;
    };
    rec.take_sounds();
    press(rec, MenuKey::Down);
    CHECK(s->records_cursor == 1 && cursor_bar() == 336 && has_sound(rec.take_sounds(), "select"));
    press(rec, MenuKey::Up);
    press(rec, MenuKey::Up);
    CHECK(s->records_cursor == 0 && cursor_bar() == 364);
    CHECK(rec.hover(300, 56 + 10) && s->records_cursor == 11 && cursor_bar() == 56);
    press(rec, MenuKey::Down);
    CHECK(s->records_cursor == 11);
    press(rec, MenuKey::Cancel);
    CHECK(rec.exited());
    // A licence test's page: its 6 places, no car.
    CHECK(game::begin_records("lic:L0B0001"));
    GameFlow lic(world.read, "US");
    lic.start("records", "records_free_tt");
    settle(lic);
    CHECK(draws_named(lic, "record_rank").size() == 6 && draws_named(lic, "record_date").size() == 6);
    CHECK(draws_named(lic, "record_car_pict").empty() && draws_named(lic, "record_car").empty());
    // A value equal to a stored one ranks ahead of it (the first place not better).
    CHECK(gt.add_record("mt:M400", 13.5, w::kCoupe, true, 12) == 1);
    CHECK(gt.state().rankings["mt:M400"][1].car == w::kCoupe && gt.state().rankings["mt:M400"][2].car == w::kWagon);
    // The top-speed test: highest first, ties ahead too, and the best of the test.
    CHECK(gt.record_machine_test("MMAX", 250.0, w::kCoupe) == 0 && gt.record_machine_test("MMAX", 260.0, w::kWagon) == 0);
    CHECK(gt.record_machine_test("MMAX", 250.0, w::kWagon) == 1 && gt.record_machine_test("MMAX", 0, w::kWagon) == -1);
    CHECK(gt.state().machine.top_speed == 260.0 && gt.state().machine.car_top == w::kWagon);
    CHECK(gt.record_machine_test("M1000", 25.0, w::kCoupe) == 0 && gt.state().machine.t1000 == 25.0);
}

// Machine tests record every finished run as it ends (the race screen's
// add_machine_test_record): one that makes the ranking runs its name entry right there,
// starting with the last name entered; the session's result adds nothing (the menus never
// record from it), and the whole session costs one day.
TEST(gtmode_synth_machine_test_runs) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    CHECK(gt.buy_car(w::kCoupe) == 0);
    choose(f, "gt_mode");
    choose(f, "mt_mt");
    choose(f, "M400");
    std::optional<GameRequest> r = wait_request(f);
    CHECK(r.has_value() && r->race_code == "M400");
    if (!r) return;
    const u32 day = gt.state().day;
    auto name_entry = [&](const char* expect_start, int key) {
        GameFlow p(world.read, "US");
        CHECK(p.start_presentations());
        settle(p);
        CHECK(p.control() == "name_entry" && page(p) == "name_entry" && text_of(p, "text_box") == expect_start);
        if (key >= 0) {
            CHECK(p.controller()->set_focus("free_cursor"));
            p.controller()->set_choice("free_cursor", key);
            press(p, MenuKey::Confirm);
        }
        choose(p, "free_command");
        for (int i = 0; i < 60 && !p.exited(); ++i) p.tick(0);
        CHECK(p.exited());
    };
    CHECK(game::add_machine_test_record("M400", 26.5) == 0);
    name_entry("", 0); // A
    CHECK(game::add_machine_test_record("M400", 25.75) == 0);
    name_entry("A", 1); // AB
    CHECK(game::add_machine_test_record("M400", 30.0) == 2);
    name_entry("AB", -1);
    const std::vector<game::RankEntry>& rank = gt.state().rankings["mt:M400"];
    CHECK(rank.size() == 3 && rank[0].value == 25.75 && rank[0].name == "AB" && rank[1].name == "A");
    CHECK(rank[0].car == w::kCoupe && rank[0].stamp != 0 && gt.state().machine.t400 == 25.75);
    // The session ends: its result (here a faster time than any run's) is not recorded, no
    // name entry.
    GameResult res = win(*r);
    res.total_time = 20.0;
    finish_game(f, res);
    CHECK(f.control() == "gt_mode" && page(f) == "mt/mt");
    CHECK(gt.state().rankings["mt:M400"].size() == 3 && gt.state().machine.t400 == 25.75 && gt.state().day == day);
}

// Course time trials (gt_mode §17): a free run's best lap goes into its course's ranking
// ("ct:<course>", the courses of the arcade TIME_ATTACK list) with the run's car, and one that
// ranks asks for the name: GT Mode's free run (the ride car) and the arcade time attack (the
// arcade car), both entered by the race screen as each run ends (add_course_time_trial_record);
// the results posted on leaving record nothing. The records page shows each entry's car.
TEST(gtmode_synth_course_time_trials) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    const GtModeHooks& h = gt_mode_hooks();
    CHECK(gt.course_time_trial_index("syn_ring") == 0 && gt.course_time_trial_index("syn_hill") < 0);
    CHECK(gt.buy_car(w::kCoupe) == 0);
    // GT Mode's free run in the ride car: entered at once, with its name entry.
    CHECK(game::add_course_time_trial_record("syn_ring", 61.25, w::kCoupe) == 0);
    std::optional<GtModeHooks::Presentation> p = h.presentation();
    CHECK(p && p->set == "name_entry" && !h.presentation());
    const std::vector<game::RankEntry>& rank = gt.state().rankings["ct:syn_ring"];
    CHECK(rank.size() == 1 && rank[0].car == w::kCoupe);
    // Its result (a faster lap than the run's, a lap the race did not enter): nothing.
    GameResult fr;
    fr.mode = "go_free_run";
    fr.course = "syn_ring";
    fr.finished = true;
    fr.best_lap = 60.0;
    h.on_result(fr);
    CHECK(rank.size() == 1 && !h.presentation());
    // The arcade time attack (a free run, ATAF0001) with an arcade car: the tie goes ahead.
    CHECK(game::add_course_time_trial_record("syn_ring", 61.25, w::kWagon) == 0);
    CHECK(rank.size() == 2 && rank[0].car == w::kWagon && rank[1].car == w::kCoupe);
    p = h.presentation();
    CHECK(p && p->set == "name_entry" && !h.presentation());
    GameResult ta;
    ta.mode = "free_run";
    ta.course = "syn_ring";
    ta.finished = true;
    ta.best_lap = 60.0;
    h.on_arcade_result(ta);
    CHECK(rank.size() == 2 && !h.presentation());
    // A course without a ranking, a run without a lap: nothing.
    CHECK(game::add_course_time_trial_record("syn_hill", 61.0, w::kWagon) < 0);
    CHECK(game::add_course_time_trial_record("syn_ring", -1, w::kWagon) < 0);
    CHECK(!gt.state().rankings.count("ct:syn_hill") && rank.size() == 2 && !h.presentation());
    CHECK(game::add_course_time_trial_record("syn_ring", 59.5, w::kCoupe) == 0 && h.presentation().has_value());
    // The records page: the course's 12 places, each entry's car after the icon.
    CHECK(game::begin_records("ct:syn_ring"));
    GameFlow rec(world.read, "US");
    rec.start("records", "records_free_tt");
    settle(rec);
    std::vector<std::string> cars, data;
    for (const menu::ElementDraw& d : rec.draw_list()) {
        if (d.element && d.element->name == "record_car" && d.text) cars.push_back(*d.text);
        if (d.element && d.element->name == "record_data" && d.text) data.push_back(*d.text);
    }
    CHECK(cars.size() == 12 && cars[0] == "Synthetic Coupe " && cars[1] == "Synthetic Wagon Touring" && cars[3].empty());
    CHECK(data.size() == 12 && data[0] == "0'59.500" && data[2] == "1'01.250" && data[3] == "--'--.---");
    CHECK(draws_named(rec, "record_car_pict").size() == 12);
}

// The car settings menu (control "setting"): the status boxes' locks, a slider (on_frame:
// steps, clamping, refusals), its markers (extra_draws: the value and the value the page
// opened with), commit and discard; the setting files through the save page backend.
TEST(gtmode_synth_settings_and_setting_files) {
    World world;
    GameFlow menus(world.read, "US");
    game::GtModeSession* s = start_title(menus);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    CHECK(!game::begin_setting("")); // no ride car
    CHECK(gt.buy_car(w::kCoupe) == 0);
    CHECK(game::begin_setting(""));
    game::GarageCar& real = *gt.state().ride_car();
    CHECK(real.settings.spring[0] == 5);
    GameFlow f(world.read, "US");
    f.start("setting", "root");
    settle(f);
    CHECK(f.control() == "setting" && page(f) == "root" && f.gt_active());
    choose(f, "item2");
    choose(f, "item1");
    CHECK(page(f) == "set/adjast_sus");
    CHECK(f.controller()->item_enabled(0) && !f.controller()->item_enabled(1)); // springs yes, dampers no (stock)
    choose(f, "status_box_sus1");
    CHECK(page(f) == "set/adjast_sus_spring" && s->setting_entry.at("slider_rate_f") == 5);
    // The marker: values 3..9 over the 200-unit bar (less the marker's 8).
    auto marker = [&](const char* name) -> float {
        auto d = draws_named(f, name);
        return d.size() == 1 ? d[0].box.x : -1.0f;
    };
    CHECK(marker("pinch") == 100 + 2 * 32 && marker("pinch_prev") < 0 && marker("pinch_fix") < 0);
    // The page opens on its first slider (no control hook, no item links anywhere: item 0).
    CHECK(f.controller()->focused()->flow->name == "slider_rate_f");
    f.take_sounds();
    press(f, MenuKey::Right);
    CHECK(s->setting_work->settings.spring[0] == 6 && real.settings.spring[0] == 5);
    CHECK(has_sound(f.take_sounds(), "param"));
    CHECK(marker("pinch") == 100 + 3 * 32 && marker("pinch_prev") == 100 + 2 * 32);
    press(f, MenuKey::R1); // ten steps, clamped
    CHECK(s->setting_work->settings.spring[0] == 9 && has_sound(f.take_sounds(), "param"));
    press(f, MenuKey::R1);
    CHECK(s->setting_work->settings.spring[0] == 9 && has_sound(f.take_sounds(), "disable"));
    // OK (commit_slider, kind 3) commits and goes back like Cancel: its back action
    // (retrieve_slider) then copies the committed car back into the working copy, which
    // changes nothing; the suspension page again.
    CHECK(f.controller()->set_focus("ok"));
    press(f, MenuKey::Confirm);
    // (Every set/adjast_ page build takes new opening values and clears the commit mark.)
    CHECK(real.settings.spring[0] == 9 && s->setting_work->settings.spring[0] == 9);
    CHECK(!s->setting_committed && page(f) == "set/adjast_sus");
    // Back without OK discards.
    choose(f, "status_box_sus1");
    CHECK(f.controller()->focused()->flow->name == "slider_rate_f");
    CHECK(marker("pinch") == 100 + 6 * 32 && marker("pinch_prev") < 0);
    press(f, MenuKey::Left);
    CHECK(s->setting_work->settings.spring[0] == 8);
    press(f, MenuKey::Cancel); // back action retrieve_slider
    CHECK(page(f) == "set/adjast_sus" && s->setting_work->settings.spring[0] == 9 && real.settings.spring[0] == 9);
    // Setting files (Save / Load Settings): the backend the save page runs.
    using SP = SavePage;
    const GtModeHooks& h = gt_mode_hooks();
    std::vector<SP::File> files = h.save_list(SP::Mode::SaveSettings);
    CHECK(files.size() == 1 && files[0].kind == SP::File::Kind::New);
    CHECK(h.save_run(SP::Mode::SaveSettings, SP::OpSave, files[0], "") == SP::Ok);
    CHECK(gt.state().setting_files.size() == 1 && gt.state().setting_files[0].car == w::kCoupe);
    files = h.save_list(SP::Mode::SaveSettings);
    CHECK(files.size() == 2 && files[1].kind == SP::File::Kind::Setting && files[1].id == "0" && files[1].own);
    CHECK(files[1].texts.size() == 3 && files[1].texts[0].first == "s_pt_car" &&
          files[1].texts[0].second == "Synthetic Coupe");
    CHECK(h.save_run(SP::Mode::SaveSettings, SP::OpRename, files[1], "GRIP") == SP::Ok);
    CHECK(h.save_run(SP::Mode::SaveSettings, SP::OpCopy, files[1], "") == SP::Ok);
    CHECK(gt.state().setting_files.size() == 2 && gt.state().setting_files[1].comment == "GRIP");
    files = h.save_list(SP::Mode::LoadSettings); // the ride car's files, newest first
    CHECK(files.size() == 2 && files[0].id == "1" && files[0].name == "GRIP");
    // Loading: the settings back; a part changed since -> "load_warning" for the flow.
    real.settings.spring[0] = 4;
    CHECK(h.save_run(SP::Mode::LoadSettings, SP::OpLoad, files[1], "") == SP::Ok);
    CHECK(real.settings.spring[0] == 9 && h.take_popup().empty());
    real.settings.spring[0] = 4;
    real.parts[13] = 0; // another drivetrain
    CHECK(h.save_run(SP::Mode::LoadSettings, SP::OpLoad, files[1], "") == SP::Ok);
    CHECK(real.settings.spring[0] == 9 && s->setting_work->settings.spring[0] == 9);
    CHECK(h.take_popup() == "load_warning" && h.take_popup().empty());
    CHECK(h.save_run(SP::Mode::SaveSettings, SP::OpDelete, files[0], "") == SP::Ok);
    CHECK(gt.state().setting_files.size() == 1);
    SP::File bad;
    bad.kind = SP::File::Kind::Setting;
    bad.id = "9";
    CHECK(h.save_run(SP::Mode::LoadSettings, SP::OpLoad, bad, "") == SP::ReadError);
    CHECK(h.save_run(SP::Mode::SaveSettings, SP::OpRename, bad, "X") == SP::WriteError);
    // The settings root's Save: the save page over the car's files, back to the root.
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "root");
    choose(f, "save");
    CHECK(f.control() == "memorycard" && page(f) == "file_operation");
    CHECK(f.save_page() && f.save_page()->mode() == SP::Mode::SaveSettings && f.save_page()->files().size() == 2);
    press(f, MenuKey::Cancel);
    settle(f);
    CHECK(f.control() == "setting" && page(f) == "root" && !f.save_page());
}

// The save page for games (save_page_files / save_page_run) with saving off: the lists,
// a new save, a snapshot offered for one save, a new game; the title's Load and the
// home Save open the page and come back.
TEST(gtmode_synth_save_page_games) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    using SP = SavePage;
    const GtModeHooks& h = gt_mode_hooks();
    CHECK(game::save_library().dir() == "/nonexistent-gt3r-test/" && !game::save_library().tracked());
    CHECK(game::current_save_path() == "/nonexistent-gt3r-test/gtmode.sav");
    std::vector<SP::File> files = h.save_list(SP::Mode::LoadGame);
    CHECK(files.size() == 1 && files[0].kind == SP::File::Kind::New && files[0].name == "GAME");
    CHECK(h.save_list(SP::Mode::SaveGame).size() == 1 && h.save_list(SP::Mode::Files).empty());
    // A new save: named, played from now on (nothing written).
    gt.state().credits = 777;
    CHECK(h.save_run(SP::Mode::SaveGame, SP::OpSave, files[0], "MY RUN") == SP::Ok);
    CHECK(gt.state().file_comment == "MY RUN" && s->save_path() == "/nonexistent-gt3r-test/MY RUN.sav");
    // A snapshot (e.g. between championship rounds) is saved instead, once; the game stays.
    game::GameState snap = gt.state();
    snap.credits = 1;
    game::begin_snapshot_save(snap);
    CHECK(game::snapshot_save() && game::snapshot_save()->credits == 1);
    CHECK(h.save_run(SP::Mode::SaveGame, SP::OpSave, files[0], "SNAP") == SP::Ok);
    CHECK(!game::snapshot_save() && gt.state().file_comment == "MY RUN");
    CHECK(s->save_path() == "/nonexistent-gt3r-test/MY RUN.sav");
    game::begin_snapshot_save(snap);
    h.save_closed();
    CHECK(!game::snapshot_save());
    // The save being played is never deleted; renames and copies do nothing without saving.
    SP::File cur;
    cur.id = s->save_path();
    cur.current = true;
    CHECK(h.save_run(SP::Mode::Files, SP::OpDelete, cur, "") == SP::WriteError);
    CHECK(h.save_run(SP::Mode::Files, SP::OpRename, cur, "X") == SP::Ok && gt.state().file_comment == "MY RUN");
    SP::File missing;
    missing.id = "/nonexistent-gt3r-test/none.sav";
    CHECK(h.save_run(SP::Mode::LoadGame, SP::OpLoad, missing, "") == SP::ReadError);
    // New game: a fresh state under its own name.
    CHECK(gt.buy_car(w::kCoupe) < 0); // (777 Cr.)
    CHECK(h.save_run(SP::Mode::LoadGame, SP::OpNewGame, files[0], "SECOND") == SP::Ok);
    CHECK(gt.state().credits == w::kStartCredits && gt.state().file_comment == "SECOND");
    CHECK(s->save_path() == "/nonexistent-gt3r-test/SECOND.sav");
    // Title Load: the save page (a transition), back to the title on its item.
    choose(f, "load");
    settle(f);
    CHECK(f.control() == "memorycard" && page(f) == "file_operation");
    CHECK(f.save_page() && f.save_page()->mode() == SP::Mode::LoadGame && f.save_page()->files().size() == 1);
    press(f, MenuKey::Cancel);
    settle(f);
    CHECK(f.control() == "gt_mode" && page(f) == "root" && f.controller()->focus() == 3);
    // Home Save.
    choose(f, "gt_mode");
    choose(f, "hm_hm");
    choose(f, "save");
    settle(f);
    CHECK(f.save_page() && f.save_page()->mode() == SP::Mode::SaveGame);
    press(f, MenuKey::Cancel);
    settle(f);
    CHECK(f.control() == "gt_mode" && page(f) == "hm/hm");
}

namespace {

void enter_arcade(GameFlow& f) {
    CHECK(f.controller()->set_focus("arcade"));
    press(f, MenuKey::Confirm);
    CHECK(f.take_transition() == TransitionType::Fadeout);
    settle(f);
}

} // namespace

// Arcade (GameFlow's arcade control with the GT-mode hooks): Single Race through the
// level page and the code-drawn pickers (open courses, class refusals, the car's paints
// and the AT / MT, RACING / DRIFT steps), the request, the result's cup and progress, and
// the Clear Status page.
TEST(gtmode_synth_arcade_single_race) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    enter_arcade(f);
    CHECK(f.control() == "arcade" && page(f) == "ar/top" && f.controller()->focus() == 0);
    CHECK(f.take_movie() == std::string("stream/arcade_menu_full.pss"));
    choose(f, "single_race");
    CHECK(page(f) == "ar/level");
    choose(f, "normal");
    CHECK(page(f) == "ar/select_course" && f.carousel());
    if (!f.carousel()) return;
    // Only the open courses (gcf level 0); the course's parts filled by the game.
    CHECK((f.carousel()->entries == std::vector<std::string>{"syn_ring", "syn_dirt"}) && f.carousel()->index == 0);
    CHECK(draws_named(f, "syn_ring_logo").size() == 1 && draws_named(f, "syn_ring_doc").size() == 1);
    CHECK(draws_named(f, "syn_ring_mini").size() == 1 && draws_named(f, "syn_hill_mini").empty());
    CHECK(text_named(f, "length") == "1.243miles" && draws_named(f, "area_a").size() == 1);
    CHECK(draws_named(f, "tarmac").size() == 1 && draws_named(f, "dirt").empty());
    CHECK(draws_named(f, "arrow_l").empty() && draws_named(f, "arrow_r").size() == 1);
    f.take_sounds();
    press(f, MenuKey::Right);
    CHECK(f.carousel()->index == 1 && has_sound(f.take_sounds(), "select"));
    press(f, MenuKey::Right);
    CHECK(f.carousel()->index == 1 && has_sound(f.take_sounds(), "disable"));
    press(f, MenuKey::Left);
    press(f, MenuKey::Confirm);
    CHECK(page(f) == "ar/select_class" && f.carousel());
    if (!f.carousel()) return;
    // The class order of the game; Dirt refused on a road course, the empty garage too.
    CHECK((f.carousel()->entries == std::vector<std::string>{"class_c", "class_b", "class_dirt", "home_garage"}));
    CHECK(draws_named(f, "x").size() == 2);
    press(f, MenuKey::Right);
    press(f, MenuKey::Right);
    CHECK(f.carousel()->entries[size_t(f.carousel()->index)] == "class_dirt");
    CHECK(draws_named(f, "disable_dirt_doc").size() == 1 && draws_named(f, "disabled").size() == 1);
    f.take_sounds();
    press(f, MenuKey::Confirm);
    CHECK(page(f) == "ar/select_class" && has_sound(f.take_sounds(), "disable"));
    press(f, MenuKey::Right);
    press(f, MenuKey::Right); // wraps to C
    CHECK(f.carousel()->index == 0);
    press(f, MenuKey::Confirm);
    CHECK(page(f) == "ar/select_car" && f.carousel());
    if (!f.carousel()) return;
    CHECK((f.carousel()->entries == std::vector<std::string>{w::kCoupe, w::kWagon}));
    CHECK(draws_named(f, w::kCoupe).size() == 1); // the car's picture rides on the cross
    press(f, MenuKey::Down); // the second paint
    CHECK(f.car_paint() == 1 && text_named(f, "color_name") == "Ember Red");
    press(f, MenuKey::Down);
    CHECK(f.car_paint() == 1);
    press(f, MenuKey::Confirm); // AUTOMATIC / MANUAL
    CHECK(f.carousel()->step == 2);
    press(f, MenuKey::Right);
    press(f, MenuKey::Confirm); // RACING / DRIFT
    CHECK(f.carousel()->step == 3);
    press(f, MenuKey::Confirm);
    std::optional<GameRequest> r = wait_request(f);
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->mode == "single_race" && r->race_code == "ANCT0001" && r->difficulty == 1 && r->page == "ar/select_car");
    CHECK(r->course == "syn_ring" && r->car == w::kCoupe && r->car_class == "class_c" && r->car_color == 1);
    CHECK(r->transmission == 1 && r->tire_mode == 0 && !r->car_parts);
    // An arcade car runs with a fresh engine; its distance still counts in the total.
    CHECK(r->car_condition && r->garage_car == -1 && r->car_odometer == 0 && r->car_wear_m == 0);
    // Won: the difficulty's cup (OK after 180 frames), the course recorded.
    finish_game(f, win(*r));
    CHECK(s->gt().state().distance_m == 4000.0);
    CHECK(f.control() == "arcade_mini" && page(f) == "arcade_mini");
    CHECK(game::presentation_model("arcade_mini") == "race/a_normal_gold.prm" && text_of(f, "rank_disp") == "1st");
    // Normal, course 0: record 1 x the SINGLE_RACE list's three courses + 0.
    CHECK(s->gt().arcade_best(1, 0) == 0 && s->gt().state().arcade.best[3] == 0 && s->gt().state().trophies == 1);
    CHECK(!f.controller()->item_enabled(0));
    for (int i = 0; i < 180; ++i) f.tick(0);
    CHECK(f.controller()->item_enabled(0));
    press(f, MenuKey::Confirm);
    CHECK(f.control() == "arcade" && page(f) == "ar/top" && f.controller()->focus() == 0);
    // Clear Status: the course's logo and its Easy mark (won at Normal); no ending yet.
    choose(f, "goodies");
    CHECK(page(f) == "ar/goodies");
    {
        auto logo = draws_named(f, "syn_ring_mini");
        auto mark = draws_named(f, "clear_easy");
        CHECK(logo.size() == 1 && box_is(logo[0], 40, 300, 60, 30));
        CHECK(mark.size() == 1 && box_is(mark[0], 110, 300, 20, 20));
    }
    choose(f, "goal");
    CHECK(page(f) == "ar/cannot_ending" && f.controller()->depth() == 1);
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "ar/top");
    press(f, MenuKey::Cancel);
    settle(f);
    CHECK(f.control() == "gt_mode" && page(f) == "root" && f.controller()->focus() == 0);
}

// 2 Player Battle: the course on pad 1, then both players choose class, car, AT / MT and
// RACING / DRIFT on their own pads; the race starts when both are ready.
TEST(gtmode_synth_arcade_two_player) {
    World world;
    GameFlow f(world.read, "US");
    CHECK(start_title(f) != nullptr);
    enter_arcade(f);
    choose(f, "battle");
    CHECK(page(f) == "ar/select_course" && f.carousel() && f.carousel()->entries.size() == 1);
    press(f, MenuKey::Confirm);
    CHECK(page(f) == "ar/select_car_2p" && f.two_player(0) && f.two_player(1));
    if (!f.two_player(0)) return;
    CHECK(f.two_player(0)->state == 2 && f.two_player(1)->state == 2);
    auto p2 = [&](MenuKey k) {
        f.set_player2_keys(key_bit(k));
        f.tick(0);
        f.set_player2_keys(0);
        f.tick(0);
    };
    press(f, MenuKey::Confirm); // P1: class C
    CHECK(f.two_player(0)->state == 4 && f.two_player(0)->cars.size() == 2);
    p2(MenuKey::Right); // P2: class B
    p2(MenuKey::Confirm);
    CHECK(f.two_player(1)->state == 4 && (f.two_player(1)->cars == std::vector<std::string>{w::kWagon}));
    press(f, MenuKey::Right); // P1: the wagon
    CHECK(f.two_player(0)->car == 1);
    for (int i = 0; i < 3; ++i) press(f, MenuKey::Confirm);
    CHECK(f.two_player(0)->state == 9);
    p2(MenuKey::Down); // P2: the second paint
    CHECK(f.two_player(1)->paint == 1);
    p2(MenuKey::Confirm);
    p2(MenuKey::Confirm);
    p2(MenuKey::Confirm);
    std::optional<GameRequest> r = wait_request(f);
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->mode == "two_player_battle" && r->race_code == "A2P0001" && r->course == "syn_ring");
    CHECK(r->car == w::kWagon && r->car_class == "class_c" && r->car2 == w::kWagon && r->car2_class == "class_b");
    CHECK(r->car2_color == 1 && r->transmission == 0 && r->tire_mode == 0);
    CHECK(!r->car_condition); // 2 Player Battle: fresh engines, nothing handed back
}

// The title's other sections: Options (a choice stored in [options], kept when the menu
// opens again), Replay Theater (the file list, a replay request and back) and the
// Analyzer (its buttons, loading a track through the theater page, its request).
TEST(gtmode_synth_options_theater_logger) {
    World world;
    GameFlow f(world.read, "US");
    IniDocument cfg;
    int changed = 0;
    f.set_options(&cfg, [&] { ++changed; });
    CHECK(start_title(f) != nullptr);
    // Options.
    choose(f, "option");
    settle(f);
    // (The option pages have no BGM: the title's silence goes on.)
    CHECK(f.control() == "option" && page(f) == "option" && f.take_music().value_or("") == "");
    choose(f, "view");
    CHECK(page(f) == "view/view" && f.controller()->choice("multi_wide_mode") == 0);
    press(f, MenuKey::Right);
    CHECK(f.controller()->choice("multi_wide_mode") == 1 && option_value(&cfg, "wide_mode") == "on" && changed == 1);
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "option");
    press(f, MenuKey::Cancel);
    settle(f);
    CHECK(f.control() == "gt_mode" && page(f) == "root" && f.controller()->focus() == 6);
    choose(f, "option");
    settle(f);
    choose(f, "view");
    CHECK(f.controller()->choice("multi_wide_mode") == 1);
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    settle(f);
    // Replay Theater: the app's files on the memorycard set's demo_loader page.
    TheaterBackend tb;
    tb.list = [](int kind) {
        std::vector<TheaterEntry> v(2);
        v[0].id = kind ? "demo_syn_1" : "/replays/a.rpl";
        v[0].texts = {{"course", "Synthetic Ring"}, {"course_id", "syn_ring"}};
        v[1].id = kind ? "demo_syn_2" : "/replays/b.rpl";
        v[1].texts = {{"course_id", "syn_hill"}};
        return v;
    };
    set_theater_backend(tb);
    choose(f, "replay");
    choose(f, "theater");
    settle(f);
    CHECK(f.control() == "memorycard" && page(f) == "demo_loader" && f.theater() && !f.save_page());
    CHECK(f.theater() && f.theater()->entries().size() == 2 && f.theater()->mode() == TheaterPage::Mode::LoadReplay);
    press(f, MenuKey::Right);
    press(f, MenuKey::Confirm);
    std::optional<GameRequest> r = wait_request(f);
    CHECK(r && r->mode == "theater_replay" && r->replay_file == "/replays/b.rpl" && r->page == "replay_theater");
    f.game_finished();
    settle(f);
    CHECK(f.control() == "gt_mode" && page(f) == "replay_theater" && f.controller()->focus() == 0);
    choose(f, "demo");
    settle(f);
    CHECK(f.theater() && f.theater()->mode() == TheaterPage::Mode::Demonstration);
    press(f, MenuKey::Confirm);
    r = wait_request(f);
    CHECK(r && r->mode == "theater_demo" && r->race_code == "demo_syn_1" && r->event == "demo_syn_1");
    f.game_finished();
    settle(f);
    CHECK(page(f) == "replay_theater");
    // The Analyzer: REPLAY / GRAPH need a track; LOAD A through the theater page.
    choose(f, "logger");
    settle(f);
    CHECK(f.control() == "logger" && page(f) == "top" && f.logger_focus() == 0);
    CHECK(draws_named(f, "RawListItemFixed_Replay").size() == 3); // the page's, the code's, the focus pulse
    f.take_sounds();
    press(f, MenuKey::Confirm);
    CHECK(has_sound(f.take_sounds(), "disable") && !f.waiting_for_game());
    press(f, MenuKey::Right);
    press(f, MenuKey::Right);
    CHECK(f.logger_focus() == 2 && has_sound(f.take_sounds(), "cursor"));
    {
        bool dim = false;
        for (const menu::ElementDraw& d : draws_named(f, "RawListItemFixed_Replay")) dim = dim || d.brightness == 0.35f;
        CHECK(dim); // no track: REPLAY dimmed
    }
    press(f, MenuKey::Confirm);
    settle(f);
    CHECK(f.theater() && f.theater()->mode() == TheaterPage::Mode::LoadTrack);
    press(f, MenuKey::Confirm);
    settle(f);
    CHECK(f.control() == "logger" && f.logger_track(0).path == "/replays/a.rpl" && f.logger_track(1).path.empty());
    press(f, MenuKey::Left);
    press(f, MenuKey::Left);
    press(f, MenuKey::Confirm);
    r = wait_request(f);
    CHECK(r && r->mode == "logger" && r->replay_file == "/replays/a.rpl" && r->replay_file2.empty());
    CHECK(r && r->logger_view == 0);
    f.game_finished();
    settle(f);
    CHECK(f.control() == "logger" && page(f) == "top");
    for (int i = 0; i < 4; ++i) press(f, MenuKey::Right);
    CHECK(f.logger_focus() == 4);
    press(f, MenuKey::Confirm); // EXIT
    settle(f);
    CHECK(f.control() == "gt_mode" && page(f) == "replay_theater" && f.controller()->focus() == 2);
    set_theater_backend({});
}

// The Options music list (menus §10.1) through the menus: the top page's view of the stored
// list, the list page's keys (it took none before: the page has no items) and preview, the
// commit on Cancel into [options] music_playlist (what the race reads), a lifted song's
// Cancel dropping the edit, and the pointer.
TEST(gtmode_synth_options_music_list) {
    World world;
    GameFlow f(world.read, "US");
    IniDocument cfg;
    int changed = 0;
    f.set_options(&cfg, [&] { ++changed; });
    CHECK(start_title(f) != nullptr);
    choose(f, "option");
    settle(f);
    // The top page: the list's view under the music icon (no cursor, no preview).
    CHECK(f.controller()->set_focus("music"));
    f.tick(0);
    const std::string first = std::string(w::kSongTitles[0]) + " / " + w::kSongArtist;
    CHECK(text_named(f, "label_box") == first);
    CHECK(!f.take_preview() && !f.music_list());
    // The page: the first song's intro plays at once.
    press(f, MenuKey::Confirm);
    CHECK(page(f) == "music/music" && f.music_list() != nullptr);
    if (!f.music_list()) return;
    const MusicListPage& m = *f.music_list();
    CHECK(f.take_preview() == 0 && m.rows() == 4 && m.focus() == 0);
    f.take_sounds();
    press(f, MenuKey::Up);
    CHECK(has_sound(f.take_sounds(), "disable") && page(f) == "music/music");
    press(f, MenuKey::Down);
    CHECK(m.focus() == 1 && f.take_preview() == 1 && has_sound(f.take_sounds(), "cursor"));
    // Out of the list: the preview stops.
    press(f, MenuKey::Confirm);
    CHECK(!m.songs()[1].listed && f.take_preview() == -1 && has_sound(f.take_sounds(), "ok"));
    // Order.
    press(f, MenuKey::Down);
    press(f, MenuKey::Down);
    press(f, MenuKey::Left);
    CHECK(m.focus() == 3 && !m.shuffle());
    // Cancel keeps the edit (and stops the music), back on the top page.
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "option" && changed == 1 && f.take_preview() == -1 && !f.music_list());
    std::optional<std::string> stored = cfg.get("options", "music_playlist");
    std::optional<race::RacePlaylist> p = stored ? race::RacePlaylist::from_text(*stored) : std::nullopt;
    CHECK(p && p->songs().size() == 3 && !p->shuffle() && p->cursor() == 0);
    if (p) {
        CHECK(p->songs()[0].listed && !p->songs()[1].listed && p->songs()[2].listed);
        CHECK(p->songs()[0].id == db::id_hash(w::kSongFiles[0]));
        CHECK(p->next() == 0u && p->next() == 2u); // the race's songs: in order, the second skipped
    }
    // Again: the stored list; a lifted song's Cancel drops the edit.
    choose(f, "music");
    CHECK(f.music_list() && !f.music_list()->songs()[1].listed && !f.music_list()->shuffle());
    CHECK(f.take_preview() == 0);
    press(f, MenuKey::Right);
    press(f, MenuKey::Confirm);
    press(f, MenuKey::Down);
    CHECK(f.music_list() && f.music_list()->lifted() && f.music_list()->focus() == 1);
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "option" && changed == 1 && cfg.get("options", "music_playlist") == stored);
    // The pointer over the page's list (the widget's box, 24-unit rows from its top).
    choose(f, "music");
    const float x = 28 + 300, top = 44 + 308;
    CHECK(f.hover(x, top - 24 * 2 - 12) && f.music_list() && f.music_list()->focus() == 2);
    CHECK(f.take_preview() == 2);
    CHECK(f.tap(x, top - 24 * 2 - 12) && f.music_list() && f.music_list()->lifted());
    CHECK(f.tap(x, top - 12) && f.music_list()->focus() == 0);
    CHECK(f.tap(x, top - 12) && !f.music_list()->lifted());
    press(f, MenuKey::Cancel);
    p = race::RacePlaylist::from_text(cfg.get("options", "music_playlist").value_or(""));
    CHECK(p && p->songs()[0].id == db::id_hash(w::kSongFiles[2]) && changed == 2);
}

// Attract mode: the gcf's idle time on the title starts the demos (cycling the gcf's demo
// list), five demos, the opening movie, the title again.
TEST(gtmode_synth_attract_mode) {
    World world;
    GameFlow f(world.read, "US");
    CHECK(start_title(f) != nullptr);
    // (Back from Options first: the title on Options.)
    CHECK(f.controller()->set_focus("option"));
    press(f, MenuKey::Confirm);
    settle(f);
    press(f, MenuKey::Cancel);
    settle(f);
    CHECK(page(f) == "root" && f.controller()->focus() == 6);
    std::optional<GameRequest> r;
    int frames = 0;
    for (; frames < 400 && !r; ++frames) {
        f.tick(0);
        r = f.take_request();
    }
    CHECK(r.has_value() && f.attract());
    if (!r) return;
    CHECK(frames > w::kTitleIdleSeconds * 60 && frames < w::kTitleIdleSeconds * 60 + 60);
    CHECK(r->mode == "attract_demo" && r->page == "root" && r->car.empty());
    CHECK((r->event == "demo_syn_1" && r->course == "syn_ring") ||
          (r->event == "demo_syn_2" && r->course == "syn_hill"));
    std::vector<std::string> events{r->event};
    for (int k = 0; k < 10; ++k) {
        GameResult res;
        res.mode = r->mode;
        res.finished = true;
        post_game_result(res);
        f.game_finished();
        r.reset();
        for (int i = 0; i < 80 && !r; ++i) {
            f.tick(0);
            r = f.take_request();
        }
        if (!r || r->kind == GameRequest::Kind::PlayMovie) break;
        events.push_back(r->event);
    }
    CHECK(events.size() == 5);
    for (size_t i = 1; i < events.size(); ++i) CHECK(events[i] != events[i - 1]); // the two demos in turn
    CHECK(r && r->kind == GameRequest::Kind::PlayMovie && r->course == "stream/opening.pss");
    f.game_finished();
    settle(f);
    // The title idled without a command: back on Arcade.
    CHECK(!f.attract() && page(f) == "root" && f.controller()->focus() == 0);
}

// The hooks called directly for the branches the walks above do not reach: pre-checks,
// enable rules, actions, results, the arcade helpers.
TEST(gtmode_synth_hook_branches) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    const GtModeHooks& h = gt_mode_hooks();
    auto item = [](const std::string& name, const std::string& action, const std::string& link) {
        menu::MblItem it;
        it.name = name;
        it.action = action;
        it.link = link;
        return it;
    };
    // intercept
    CHECK(h.intercept("map", item("at_at", "", "at/at")) == "00cmn/ride_car");
    CHECK(h.intercept("map", item("x", "go_machine_test", "")) == "00cmn/ride_car");
    CHECK(h.intercept("hm/hm_car_trade", item("buy", "", "hm/hm_buy_sure")) == "00cmn/not_for_buy");
    CHECK(gt.buy_car(w::kCoupe) == 0);
    CHECK(h.intercept("map", item("at_at", "", "at/at")).empty());
    CHECK(h.intercept("at/at", item("ws", "", "at/ws/ws")).empty());
    CHECK(h.intercept("hm/hm_car", item("w", "", "hm/hm_wheel")).empty()); // (no garage car chosen)
    CHECK(h.intercept("ts/ts_tir", item("all", "", "ts/ts_buy_sure_all")) == "ts/already_you_have_all_tires");
    CHECK(h.intercept("ts/ts_x", item("unknown", "", "ts/ts_buy_sure2")) == "ts/not_exist");
    CHECK(h.intercept("at/ws/ws_x", item("bbw_syn_link", "", "00cmn/ws_buy_sure")).empty());
    CHECK(h.intercept("map", item("syn_dirt", "go_free_run", "")) == "hm/need_dirt_tire");
    CHECK(h.intercept("map", item("syn_ring", "go_free_run", "")).empty());
    CHECK(h.intercept("r/x", item("L0A0002", "go_license", "")) == "lc/no_license");
    CHECK(h.intercept("ar/goodies", item("goal", "", "ar/select_ending")) == "ar/cannot_ending");
    // enabled
    CHECK(!h.enabled("arcade_mini", item("ok", "", "")) && !h.enabled("license_get", item("ok", "", "")));
    CHECK(!h.enabled("ar/select_ending", item("long", "go_ending_normal", "")));
    CHECK(h.enabled("ar/select_ending", item("short", "go_ending_short", "")) && h.enabled("map", item("x", "", "")));
    // Actions: wash (its sound plays on the next frames) and oil, wheels, codes, sorts.
    std::vector<std::string> log;
    auto action = [&](const std::string& a, const std::string& it = {}, const std::string& pg = "at/at") {
        MenuEvent e;
        e.kind = MenuEvent::Kind::Action;
        e.action = a;
        e.item = it;
        e.page = pg;
        return h.on_event(e, log);
    };
    const s64 c0 = gt.state().credits;
    gt.state().ride_car()->cond.dirt = 0.5f;
    CHECK(!action("wash_car") && gt.state().ride_car()->cond.dirt == 0 && gt.state().credits == c0 - 60);
    CHECK(!action("oil_change") && gt.state().ride_car()->cond.oil_m == -300000 && gt.state().credits == c0 - 360);
    std::vector<std::string> se;
    for (int i = 0; i < 40; ++i) {
        f.tick(0);
        for (std::string& x : f.take_sounds()) se.push_back(x);
    }
    // (The wash's effect comes as its spin starts, the oil change's with its gauge's animation,
    // gtmode_synth_car_wash_and_touch; with no gauge on screen there is none.)
    CHECK((se == std::vector<std::string>{"car_wash"}));
    action("set_focus_wheel_code", "bbw_syn_link");
    CHECK(s->focus_wheel == "bbw_syn");
    action("buy_wheel");
    CHECK(gt.state().wheels.size() == 1 && gt.state().credits == c0 - 360 - 700);
    CHECK(h.intercept("at/ws/ws_x", item("bbw_syn_link", "", "00cmn/ws_buy_sure")) == "00cmn/already_you_have");
    action("fit_wheel");
    CHECK(gt.state().ride_car()->wheel == "bbw_syn");
    s->garage_focus = 0;
    s->wheel_cursor = -1;
    action("change_wheel");
    CHECK(gt.state().ride_car()->wheel.empty());
    action("set_race_code_spot", "Gsyn_e_0002");
    CHECK(s->race_code == "Gsyn_e_0002" && !s->champ);
    action("clear_race_code");
    CHECK(s->race_code.empty());
    action("sort_power");
    CHECK(s->sort_key == 6);
    action("sort_aspiration");
    CHECK(s->sort_key == 8 && s->garage_cursor == 0);
    action("set_car_code", "_syn_racer_03");
    CHECK(s->focus_car == w::kRacer);
    action("clear_focus_car");
    CHECK(s->focus_car.empty());
    // A race entry the car breaks the rules of (not in the regulation list).
    CHECK(gt.add_prize_car(w::kRacer, -1, "test") == 0);
    gt.ride(0);
    action("set_race_code", "Gsyn_e_0002", "r/beginner/gsyn_e");
    CHECK(h.intercept("r/beginner/gsyn_e_entry", item("menu1", "go_race", "")) == "00cmn/regulation_violation");
    gt.ride(1);
    // A free run: a day, the course on the request.
    {
        MenuEvent e;
        e.kind = MenuEvent::Kind::Finish;
        e.action = "go_free_run";
        e.item = "syn_ring";
        e.page = "map";
        const u32 day = gt.state().day;
        h.on_event(e, log);
        CHECK(gt.state().day == day + 1);
        GameRequest rq;
        rq.mode = "go_free_run";
        rq.race_code = "ATAF0001";
        h.prepare_request(rq);
        CHECK(rq.course == "syn_ring" && rq.event == "ATAF0001" && rq.car == w::kCoupe);
        GameRequest pv;
        pv.mode = "go_preview";
        s->race_code = "Gsyn_e_0001";
        h.prepare_request(pv);
        CHECK(pv.race_code == "Gsyn_e_0001" && pv.event == "Gsyn_e_0001");
    }
    // Results: a retirement (a race counted, nothing paid), a licence test judged by GT mode
    // (31 s: silver), a top speed record, the dirt the race left.
    GameResult res;
    res.mode = "go_race";
    res.race_code = res.event = "Gsyn_e_0001";
    res.finished = false;
    res.car_dirt = 0.9;
    const s64 c1 = gt.state().credits;
    CHECK(h.on_result(res).empty());
    CHECK(gt.state().races == 1 && gt.state().credits == c1 && gt.state().ride_car()->cond.dirt == 0.8f);
    CHECK(!h.presentation() && !h.next_request());
    GameResult lic;
    lic.mode = "go_license";
    lic.race_code = lic.event = "L0B0002";
    lic.finished = true;
    lic.total_time = 31.0;
    h.on_result(lic);
    CHECK(gt.state().license_tests[1].medal == game::Medal::Silver);
    // The rankings are the race screen's (gt_mode §17): no record or name entry from a result.
    CHECK(gt.state().rankings["lic:L0B0002"].empty() && !h.presentation());
    GameResult top;
    top.mode = "go_machine_test";
    top.race_code = "MMAX";
    top.finished = true;
    top.top_speed_kmh = 251.5;
    h.on_result(top);
    CHECK(gt.state().machine.top_speed < 0 && gt.state().rankings["mt:MMAX"].empty() && !h.presentation());
    std::optional<GtModeHooks::Presentation> p;
    // A prize car with a full garage: "Garage is full" after the cup.
    game::GarageCar spare = gt.state().garage[0];
    gt.state().garage.resize(size_t(game::kMaxGarageCars), spare);
    gt.state().race_best["Gsyn_e_0002"] = 0;
    GameResult won;
    won.mode = "go_race";
    won.race_code = won.event = "Gsyn_e_0001";
    won.finished = true;
    won.position = 1;
    h.on_result(won);
    p = h.presentation();
    CHECK(p && p->set == "gtmode_mini");
    p = h.presentation();
    CHECK(p && p->set == "not_get_car" && p->page == "not_car_get" && !h.presentation());
    gt.state().garage.resize(2);
    // Championship: an abandoned one ends; a round won asks for the next one.
    CHECK(gt.start_championship("Gsynch_n_0501"));
    GameResult quit;
    quit.mode = "go_race";
    quit.abandoned = true;
    h.on_result(quit);
    CHECK(!gt.state().championship.active());
    CHECK(gt.start_championship("Gsynch_n_0501"));
    GameRequest first;
    first.mode = "go_race";
    h.prepare_request(first);
    CHECK(first.race_code == "Gsynch_n_0501" && first.champ_round == 1);
    GameResult round = won;
    round.race_code = round.event = "Gsynch_n_0501";
    h.on_result(round);
    while (h.presentation()) {
    }
    std::optional<GameRequest> next = h.next_request();
    CHECK(next && next->race_code == "Gsynch_n_0502" && next->champ_last && !h.next_request());
    std::optional<GameRequest> resume = h.resume_request();
    CHECK(resume && resume->page == "map" && resume->race_code == "Gsynch_n_0502");
    // Arcade helpers.
    CHECK(game::arcade_entry_car(w::kCoupe) == w::kCoupe && game::arcade_entry_car("hg1") == w::kCoupe);
    CHECK(game::arcade_garage_car("hg1") && !game::arcade_garage_car("gg0") && !game::arcade_garage_car("hg9"));
    CHECK(h.arcade_image("hg1") == w::kCoupe && h.arcade_image(w::kCoupe).empty());
    CHECK(h.arcade_colors(w::kCoupe).size() == 2 && h.arcade_colors(w::kCoupe)[1].first == "Ember Red");
    CHECK(h.arcade_colors("hg1").size() == 1);
    CHECK((h.arcade_gauges(w::kCoupe) == std::array<float, 3>{0.7f, 0.7f, 0.7f})); // (no car spec)
    auto specs = h.arcade_specs(w::kCoupe);
    CHECK(specs.size() == 4 && specs[2].first == "Width" && specs[2].second == "1720mm");
    std::vector<std::string> entries;
    h.arcade_entries("ar/select_car", "single_race", "home_garage", entries);
    CHECK((entries == std::vector<std::string>{"hg0", "hg1"}));
    CHECK(h.arcade_disabled("ar/select_class", "syn_dirt", "class_c") == "disable_road_doc");
    CHECK(h.arcade_disabled("ar/select_class", "syn_dirt", "class_dirt").empty());
    CHECK(h.arcade_disabled("ar/select_class", "syn_ring", "guest_garage") == "disable_no_car_doc");
    CHECK(h.arcade_disabled("ar/select_car", "syn_dirt", "hg1") == "disable_dirt_doc");
    GameRequest ar;
    ar.mode = "single_race";
    ar.race_code = "ANCT0001";
    ar.course = "syn_ring";
    ar.car = "hg1";
    h.arcade_prepare(ar);
    CHECK(ar.race_code == "ANCT0002" && ar.car == w::kCoupe && ar.car_model == "zz0001"); // (no spec: class C + 1)
    ar.race_code = "ANCT0001";
    ar.course = "syn_dirt";
    ar.car = "hg1";
    h.arcade_prepare(ar);
    CHECK(ar.race_code == "ANAD0001");
    // A time trial run as the race screen enters it: its ranking (no seeds without the event)
    // and the name entry; its result records nothing.
    CHECK(game::add_time_trial_record("AT010001", 61.5) == 0);
    CHECK(gt.state().rankings["at:AT010001"].size() == 1 && gt.state().arcade.at_best_rank[0] == 0);
    p = h.presentation();
    CHECK(p && p->set == "name_entry");
    GameResult tt;
    tt.mode = "time_trial";
    tt.race_code = "AT010001";
    tt.finished = true;
    tt.total_time = 60.0;
    h.on_arcade_result(tt);
    CHECK(gt.state().rankings["at:AT010001"].size() == 1 && !h.presentation());
}

// The car wash and the oil change (gt_mode §13.2), and touch on the garage list (gt_mode §5; garage_tap /
// garage_hover: a row moves the cursor, the cursor row opens the car). Each runs under a
// progress popup that takes the controls away until it is over: no input and no "Complete"
// button until then. The car is washed spinning (its turntable pushed round, car_wash_spin) and
// shown as dirty as it came in until the wash is over; the oil gauge (drawn by code) drains and
// refills.
// GT3's result rules (gt_mode §3.4, §22.2, §10): a race left from its pre-race menu is not
// one; a championship round that was run counts even when the player then retires; a round
// left by pause EXIT scores the others by their places and the player nothing; a championship
// won on points (its last round not won) opens the prize check, where the endings are checked;
// a league's prize key only with a code of that league.
TEST(gtmode_synth_result_rules) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    const GtModeHooks& h = gt_mode_hooks();
    CHECK(gt.buy_car(w::kCoupe) == 0);
    while (!h.take_movie().empty()) {
    }
    auto drain = [&] {
        while (h.presentation()) {
        }
    };
    auto race = [](const std::string& code, int position, std::vector<int> order) {
        GameResult r;
        r.mode = "go_race";
        r.race_code = r.event = code;
        r.finished = position > 0;
        r.position = position;
        r.order = std::move(order);
        return r;
    };
    // Left from the pre-race menu: nothing (no race counted).
    const u32 races = gt.state().races;
    const s64 credits = gt.state().credits;
    GameResult left = race("Gsyn_e_0001", 0, {});
    left.started = false;
    h.on_result(left);
    CHECK(gt.state().races == races && gt.state().credits == credits && gt.state().best_position("Gsyn_e_0001") == game::kNoPosition);
    // A championship round's pre-race menu left: the championship ends, nothing recorded.
    CHECK(gt.start_championship("Gsynch_n_0501"));
    GameResult quit = left;
    quit.race_code = quit.event = "Gsynch_n_0501";
    quit.abandoned = true;
    h.on_result(quit);
    CHECK(!gt.state().championship.active() && gt.state().races == races && gt.state().credits == credits);
    // Retiring after a round that was run: the round keeps its place, prize and race count.
    CHECK(gt.start_championship("Gsynch_n_0501"));
    GameResult second = race("Gsynch_n_0501", 2, {0, -1, 1, 2, 3, 4});
    second.abandoned = true;
    h.on_result(second);
    drain();
    CHECK(!gt.state().championship.active() && gt.state().races == races + 1);
    CHECK(gt.state().best_position("Gsynch_n_0501") == 1 && gt.state().credits == credits + 1800);
    CHECK(gt.state().best_position("Gsynch_n_0500") == game::kNoPosition);
    // A round left by pause EXIT (not finished, the player 2nd on the road): the cars ahead and
    // behind keep the points of their places, the player scores none.
    CHECK(gt.start_championship("Gsynch_n_0501"));
    h.on_result(race("Gsynch_n_0501", 0, {0, -1, 1, 2, 3, 4}));
    drain();
    {
        const game::Championship& ch = gt.state().championship;
        CHECK(ch.active() && ch.next == 1 && ch.points.size() == 6);
        CHECK(ch.points.size() == 6 && ch.points[0] == 0 && ch.points[1] == 10 && ch.points[2] == 4 &&
              ch.points[3] == 3 && ch.points[4] == 2 && ch.points[5] == 1);
    }
    CHECK(gt.state().races == races + 2 && gt.state().best_position("Gsynch_n_0501") == 1);
    gt.state().championship = {};
    // Won on points with the last round 2nd (10 + 6 against 4 + 10): the title, its prize car,
    // and the league's ending from that check.
    CHECK(!gt.state().ending_long_seen && gt.start_championship("Gsynch_n_0501"));
    h.on_result(race("Gsynch_n_0501", 1, {-1, 0, 1, 2, 3, 4}));
    drain();
    h.on_result(race("Gsynch_n_0502", 2, {1, -1, 0, 2, 3, 4}));
    drain();
    CHECK(gt.state().best_position("Gsynch_n_0500") == 0 && gt.state().best_position("Gsynch_n_0502") == 1);
    CHECK(gt.state().ending_long_seen && h.take_movie() == "stream/ending_long_full.pss" && h.take_movie().empty());
    // League keys only with a code of their league.
    CHECK(game::GtMode::code_league("Gtt_e_0505") == "beginner" && game::GtMode::code_league("Gtt_n_0500") == "amateur");
    CHECK(game::GtMode::code_league("Gf1_h_1000") == "pro" && game::GtMode::code_league("E24h_0001") == "end");
    CHECK(game::GtMode::code_league("D_tahiti_0001") == "dirt" && game::GtMode::code_league("L0B0001").empty());
}

TEST(gtmode_synth_car_wash_and_touch) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    gt.state().credits = 100000;
    CHECK(gt.buy_car(w::kCoupe) == 0 && gt.buy_car(w::kWagon) == 0);
    game::GarageCar& ride = *gt.state().ride_car();
    ride.cond.oil_m = 150000; // half worn
    ride.cond.dirt = 0.4f;
    choose(f, "gt_mode");
    choose(f, "at_at");
    CHECK(page(f) == "at/at");
    // While a progress popup holds the controls: its text, no button, and keys do nothing.
    auto held = [&](const char* popup) {
        return page(f) == popup && f.controller()->ignoring_control() && !draws_named(f, "base1").empty() &&
               draws_named(f, "end").empty();
    };
    auto ignores_keys = [&](const char* popup) {
        for (MenuKey k : {MenuKey::Confirm, MenuKey::Cancel, MenuKey::Left, MenuKey::Start}) {
            f.tick(key_bit(k));
            if (!held(popup) || f.controller()->busy()) return false;
        }
        return true;
    };
    auto shown_dirt = [&]() {
        std::string car;
        int paint = -1;
        float dirt = -1;
        CHECK(game::car_for_widget(page(f), "car_render_car_wash", car, paint, nullptr, &dirt) && car == ride.car);
        return dirt;
    };
    auto near = [](float a, float b) { return std::abs(a - b) < 0.01f; };

    // The car wash: 31 frames after the action its effect plays and the car's turntable is
    // pushed round, once a frame for 181 frames; the last push ends the wash: the car is shown
    // clean and "Complete" comes up (back to the shop).
    choose(f, "cw");
    CHECK(page(f) == "at/cw/cw" && !f.controller()->ignoring_control() && near(shown_dirt(), 0.4f));
    CHECK(f.controller()->set_focus("wash"));
    f.take_sounds();
    const int serial = game::car_wash_spin().serial;
    f.tick(key_bit(MenuKey::Confirm)); // the action's frame
    CHECK(near(ride.cond.dirt, 0) && gt.state().credits == 100000 - 42000 - 60);
    CHECK(game::car_wash_spin().serial == serial + 1 && game::car_wash_spin().pushes == 0);
    int sound_at = -1, back_at = -1, pushes_at_sound = -1;
    bool dirty_while_washed = true, pushed_each_frame = true;
    for (int t = 1; t <= 300 && back_at < 0; ++t) {
        const int pushes = game::car_wash_spin().pushes;
        f.tick(0);
        if (has_sound(f.take_sounds(), "car_wash")) sound_at = t, pushes_at_sound = game::car_wash_spin().pushes;
        if (sound_at > 0 && t > sound_at && game::car_wash_spin().pushes != pushes + 1) pushed_each_frame = false;
        if (!f.controller()->ignoring_control()) back_at = t;
        else if (!near(shown_dirt(), 0.4f)) dirty_while_washed = false;
        if (t == 20) CHECK(held("at/cw/cw_now"));
        if (t == 100) {
            CHECK(ignores_keys("at/cw/cw_now")); // (4 frames of the wash too)
            t += 4;
        }
    }
    CHECK(sound_at == 31 && pushes_at_sound == 0 && pushed_each_frame && dirty_while_washed);
    CHECK(back_at == 212 && game::car_wash_spin().pushes == 181 && near(shown_dirt(), 0));
    CHECK(page(f) == "at/cw/cw_now" && !draws_named(f, "end").empty());
    for (int i = 0; i < 30; ++i) f.tick(0);
    CHECK(game::car_wash_spin().pushes == 181 && !has_sound(f.take_sounds(), "car_wash"));
    press(f, MenuKey::Confirm); // Complete
    CHECK(page(f) == "at/at");

    // The oil change. The oil gauge (gt_mode §13.2): the cylinder's oil (the "dirty" art with code
    // colours), its height and its red (0 clean .. 0x80 dirty in the synthetic art), drawn under
    // the glass.
    choose(f, "ot");
    CHECK(page(f) == "at/ot/ot");
    struct Gauge {
        float h = -1, red = -1;
        bool under_glass = false;
    };
    auto gauge = [&]() {
        Gauge g;
        int at = -1;
        std::vector<menu::ElementDraw> list = f.draw_list();
        for (size_t i = 0; i < list.size(); ++i) {
            const menu::ElementDraw& d = list[i];
            if (d.element && d.element->name == "glass") g.under_glass = at >= 0;
            if (!d.element || d.element->name != "dirty" || !d.has_colors || !box_is(d, 500, 100, 20, d.box.h)) continue;
            at = int(i);
            g.h = d.box.h;
            g.red = d.colors[0].r * 255.0f;
        }
        return g;
    };
    Gauge g = gauge();
    CHECK(near(g.h, 100) && near(g.red, 64) && g.under_glass); // full, half used (150 of 300 km)
    ride.cond.oil_m = 300000; // worn
    CHECK(near(gauge().h, 100) && near(gauge().red, 128));
    ride.cond.oil_m = 0; // a new car's oil
    CHECK(near(gauge().h, 100) && near(gauge().red, 0));
    ride.cond.oil_m = 150000;
    // The oil drains (still the old oil), the new oil goes in and fills back up; "oil" plays as
    // the draining and the filling start. Keys pressed once it has drained change nothing.
    CHECK(f.controller()->set_focus("oil"));
    f.take_sounds();
    f.tick(key_bit(MenuKey::Confirm)); // the action's frame
    CHECK(ride.cond.oil_m == -300000 && gt.state().credits == 100000 - 42000 - 60 - 300);
    int first_oil = -1, second_oil = -1, empty_at = -1, full_at = -1;
    bool old_while_draining = true, clean_after = true;
    for (int t = 1; t <= 600 && full_at < 0; ++t) {
        f.tick(0);
        Gauge now = gauge();
        if (has_sound(f.take_sounds(), "oil")) (first_oil < 0 ? first_oil : second_oil) = t;
        if (empty_at < 0 && now.h == 0) empty_at = t;
        if (second_oil < 0 && !near(now.red, 64)) old_while_draining = false;
        if (second_oil >= 0 && !near(now.red, 0)) clean_after = false;
        if (!f.controller()->ignoring_control()) full_at = t;
        if (t == 20) CHECK(held("at/ot/ot_now"));
        if (t == 200) {
            CHECK(empty_at > 0 && ignores_keys("at/ot/ot_now")); // drained: still changing
            t += 4;
        }
    }
    CHECK(first_oil == 62);              // 61 frames' wait
    CHECK(empty_at - first_oil == 101);  // 0.01 a frame
    CHECK(second_oil - empty_at == 61);  // 61 frames' wait
    CHECK(full_at - second_oil == 101);  // the controls come back as it is full
    CHECK(old_while_draining && clean_after && near(gauge().h, 100) && near(gauge().red, 0));
    CHECK(page(f) == "at/ot/ot_now" && !draws_named(f, "end").empty()); // "Complete"
    for (int i = 0; i < 10; ++i) f.tick(0);
    CHECK(!has_sound(f.take_sounds(), "oil") && near(gauge().h, 100) && near(gauge().red, 0));
    press(f, MenuKey::Confirm); // Complete
    CHECK(page(f) == "at/at");
    choose(f, "ot");
    gt.state().credits = 10;
    choose(f, "oil");
    CHECK(page(f) == "00cmn/no_money");
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    CHECK(page(f) == "map");
    choose(f, "hm_hm");
    choose(f, "hm_gr");
    CHECK(page(f) == "hm/hm_gr" && s->garage_cursor == 0);
    // Rows: 378..398 (row 0), 356..376 (row 1) in the window 40..460.
    CHECK(!f.tap(500, 360)); // outside the window
    CHECK(f.tap(100, 360) && s->garage_cursor == 1 && page(f) == "hm/hm_gr");
    f.take_sounds();
    CHECK(f.hover(100, 385) && s->garage_cursor == 0 && has_sound(f.take_sounds(), "select"));
    CHECK(f.hover(100, 385) && s->garage_cursor == 0); // over the cursor row
    CHECK(f.tap(100, 385)); // the cursor row: confirmed on the next tick
    f.tick(0);
    settle(f);
    CHECK(page(f) == "hm/hm_car" && s->garage_focus == 0 && text_of(f, "focused_carname") == "Synthetic Wagon");
}

// The race shows an arcade win's cup and a championship round's prize page itself, right
// after the race and before its replay (presentation §F); the result it posts then says
// prize_shown and the menus show neither again.
TEST(gtmode_synth_prizes_before_the_replay) {
    World world;
    GameFlow f(world.read, "US");
    game::GtModeSession* s = start_title(f);
    CHECK(s != nullptr);
    if (!s) return;
    const GtModeHooks& h = gt_mode_hooks();
    // The arcade cup: the difficulty from the race code's second letter.
    CHECK(!game::queue_arcade_trophy("A") && !game::queue_arcade_trophy("AXCT0001"));
    CHECK(game::queue_arcade_trophy("AHCT0001"));
    std::optional<GtModeHooks::Presentation> p = h.presentation();
    CHECK(p && p->page == "arcade_mini" && game::presentation_model("arcade_mini") == "race/a_hard_gold.prm");
    CHECK(!h.presentation());
    // The win's result after it: recorded, no second cup; without prize_shown, the cup.
    GameResult won;
    won.mode = "single_race";
    won.race_code = "AHCT0001";
    won.course = "syn_ring";
    won.finished = true;
    won.position = 1;
    won.cars = 2;
    won.prize_shown = true;
    const u32 trophies = s->gt().state().trophies;
    h.on_arcade_result(won);
    CHECK(!h.presentation() && s->gt().state().trophies == trophies + 1);
    won.prize_shown = false;
    h.on_arcade_result(won);
    p = h.presentation();
    CHECK(p && p->page == "arcade_mini" && !h.presentation());
    // A championship round's prize page (its own place prize, the cup of its trophy class).
    CHECK(s->gt().start_championship("Gsynch_n_0501"));
    CHECK(game::queue_race_prize("Gsynch_n_0501", 1));
    p = h.presentation();
    CHECK(p && p->page == "gtmode_mini" && game::presentation_model("gtmode_mini") == "race/g_normal_silver.prm");
    CHECK(!h.presentation());
}
