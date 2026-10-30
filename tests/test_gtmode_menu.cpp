// GT Mode through the interactive menus (menu::flow::GameFlow + the GT-mode
// hooks of game/gt_mode_menu.cpp), on the real menus. Never writes a save.

#include "test_framework.h"

#include "disc/region.h"
#include "formats/db/gt_db.h"
#include "formats/text/mltx.h"
#include "game/gt_mode_menu.h"
#include "game/gt_save.h"
#include "menu/flow/game_flow.h"
#include "menu/flow/gt_mode_hooks.h"
#include "vfs/vfs.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

using namespace gt3r;
using namespace gt3r::menu::flow;

namespace {

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

// The race-prize presentations a result shows first (arcade / GT-mode cups, the
// consolation prize): checked and confirmed once OK is accepted. Returns the pages seen.
std::vector<std::string> skip_race_prizes(GameFlow& f) {
    std::vector<std::string> seen;
    for (int k = 0; k < 4 && f.controller(); ++k) {
        std::string p = f.controller()->page_name();
        if (p != "arcade_mini" && p != "gtmode_mini" && p != "consolation_prize") break;
        seen.push_back(p);
        for (int i = 0; i < 300; ++i) f.tick(0);
        press(f, MenuKey::Confirm);
        settle(f);
    }
    return seen;
}

// Focus an item by name and confirm it.
void choose(GameFlow& f, const char* item) {
    CHECK(f.controller()->set_focus(item));
    press(f, MenuKey::Confirm);
}

std::string widget_text(const GameFlow& f, const char* cls) {
    for (const menu::ElementDraw& d : f.draw_list())
        if (d.element && d.element->widget_class && *d.element->widget_class == cls && d.text) return *d.text;
    return "<none>";
}

// setenv is POSIX-only; the Windows CRT has _putenv_s (same effect for std::getenv).
void set_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unset_env(const char* name) {
#ifdef _WIN32
    _putenv_s(name, ""); // an empty value removes it from the CRT environment
#else
    unsetenv(name);
#endif
}

} // namespace

TEST(gtmode_menu_disc_buy_and_race) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    CHECK(s->gt().state().credits == 18000); // no save: a new game

    // Title -> GT mode map: the status bar shows the credits.
    choose(f, "gt_mode");
    settle(f);
    CHECK(f.controller()->page_name() == "map");
    CHECK(widget_text(f, "user_money") == " 18,000"); // a space after the Cr. icon

    // No car yet: the tune shop refuses with the "ride car" popup.
    choose(f, "ts_ts");
    CHECK(f.controller()->depth() == 1 && f.controller()->page_name() == "00cmn/ride_car");
    press(f, MenuKey::Cancel);

    // Dealer: Japan -> first dealer -> a car; too expensive -> no_money popup.
    choose(f, "cd_cd");
    CHECK(f.controller()->page_name() == "cd/cd");
    choose(f, "jp");
    choose(f, f.controller()->items()[0].flow->name.c_str()); // first maker
    std::string maker = f.controller()->page_name();
    choose(f, f.controller()->items()[0].flow->name.c_str()); // first car (set_car_code)
    const std::string car = s->focus_car;
    CHECK(!car.empty());
    CHECK(!widget_text(f, "car_buy_price").empty()); // the number next to a Cr. icon
    s->gt().state().credits = 0;
    choose(f, "buy");
    CHECK(f.controller()->depth() == 1 && f.controller()->page_name() == "00cmn/no_money");
    press(f, MenuKey::Cancel);
    // With money: buy it; the first car becomes the ride car.
    s->gt().state().credits = 1000000;
    CHECK(!(s->gt().car_flags(car) & 1)); // the first Japanese dealer car is for sale
    choose(f, "buy");
    CHECK(f.controller()->page_name() == "00cmn/cd_buy_sure");
    choose(f, "yes1");
    CHECK(s->gt().state().garage.size() == 1 && s->gt().state().ride == 0);
    CHECK(s->gt().state().credits == 1000000 - s->gt().car_price(car));
    CHECK(f.controller()->page_name() == "00cmn/cd_bought");
    choose(f, "yes1");
    settle(f);
    CHECK(f.controller()->page_name() == "cd/cd");

    // Race: beginner -> Sunday Cup -> race 1 -> go race.
    press(f, MenuKey::Cancel);
    settle(f);
    CHECK(f.controller()->page_name() == "map");
    u32 day = s->gt().state().day;
    choose(f, "r_r");
    choose(f, "beginner");
    choose(f, "Gsunday_e_0001");
    CHECK(f.controller()->page_name() == "r/beginner/gsunday_e");
    choose(f, "Gsunday_e_0001");
    CHECK(f.controller()->page_name() == "r/beginner/gsunday_e_entry");
    choose(f, "menu1");
    for (int i = 0; i < 80 && !f.waiting_for_game(); ++i) f.tick(0);
    std::optional<GameRequest> r = f.take_request();
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->mode == "go_race" && r->race_code == "Gsunday_e_0001" && r->event == "Gsunday_e_0001");
    CHECK(r->car_parts.has_value() && r->car == car);
    CHECK(r->opponents.size() == 5);
    CHECK(s->gt().state().day == day + 1);
    // Win it.
    s64 credits = s->gt().state().credits;
    GameResult res;
    res.mode = r->mode;
    res.race_code = r->race_code;
    res.event = r->event;
    res.finished = true;
    res.position = 1;
    res.cars = 6;
    // The run drove 4.5 km: the engine condition the race hands back (physics §10.3).
    CHECK(r->car_condition && r->garage_car == 0);
    {
        const sim::EngineCondition end = game::GtMode::driven({r->car_odometer, r->car_wear_m, r->car_oil_m}, 4500);
        res.car_condition = true;
        res.car_odometer = end.odometer, res.odometer_added = end.odometer - r->car_odometer;
        res.car_wear_m = end.wear_m, res.car_oil_m = end.oil_m;
        res.garage_car = r->garage_car;
    }
    post_game_result(res);
    f.game_finished();
    settle(f);
    // The original shows the event's cup (trophy class 5: race/g_easy_gold.prm) with the
    // prize money counting into the credits before the menu comes back.
    CHECK(f.controller() && f.controller()->page_name() == "gtmode_mini");
    {
        std::string model = game::presentation_model("gtmode_mini");
        CHECK(model == "race/g_easy_gold.prm");
    }
    CHECK(skip_race_prizes(f).size() == 1);
    CHECK(s->gt().state().credits == credits + 1000);
    CHECK(s->gt().state().best_position("Gsunday_e_0001") == 0);
    CHECK(s->gt().state().garage[0].cond.odometer_m == 4500.0);
    CHECK(f.controller() && f.controller()->page_name() == "r/beginner/gsunday_e");

    // Winning the last missing Sunday Cup race gives the series prize car: the
    // car_get presentation runs before the menu comes back.
    s->gt().state().race_best["Gsunday_e_0002"] = 0;
    s->gt().state().race_best["Gsunday_e_0003"] = 0;
    choose(f, "Gsunday_e_0001");
    choose(f, "menu1");
    for (int i = 0; i < 80 && !f.waiting_for_game(); ++i) f.tick(0);
    r = f.take_request();
    CHECK(r.has_value());
    size_t cars = s->gt().state().garage.size();
    post_game_result(res);
    f.game_finished();
    settle(f);
    CHECK(skip_race_prizes(f).size() == 1);
    CHECK(s->gt().state().garage.size() == cars + 1);
    CHECK(f.gt_active() && f.control() == "car_get" && f.controller()->page_name() == "car_get");
    // The page keeps the controls until its roulette has stopped on the car won and the
    // camera has flown in on it (one candidate: 210 frames held, then the drop of the others).
    CHECK(f.controller()->ignoring_control());
    press(f, MenuKey::Confirm);
    CHECK(f.control() == "car_get");
    for (int i = 0; i < 600 && f.controller()->ignoring_control(); ++i) f.tick(0);
    CHECK(!f.controller()->ignoring_control());
    press(f, MenuKey::Confirm);
    settle(f);
    CHECK(f.control() == "gt_mode" && f.controller()->page_name() == "r/beginner/gsunday_e");
    (void)maker;
}

TEST(gtmode_menu_disc_championship_resume) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    // A save with a championship suspended after round 2.
    s->gt().state().credits = 1000000;
    CHECK(s->gt().buy_car("altezza_rs200_98") == 0);
    CHECK(s->gt().start_championship("Galtezza_e_0501"));
    s->gt().record_round(0);
    s->gt().record_round(1);
    // Entering GT mode resumes it with round 3.
    choose(f, "gt_mode");
    for (int i = 0; i < 80 && !f.waiting_for_game(); ++i) f.tick(0);
    std::optional<GameRequest> r = f.take_request();
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->mode == "go_race" && r->race_code == "Galtezza_e_0503");
    CHECK(r->champ_round == 3 && r->champ_rounds == 5 && !r->champ_last);
    CHECK(r->opponents == s->gt().state().championship.entrants);
}

// Tune shop: Purchase? -> Install parts now? -> Installed are popups, and a popup item
// confirmed inside a popup replaces it (the page manager keeps one popup level), so every
// end of the chain lands back on the part's page. The Installed page shows the power before
// and with the part.
TEST(gtmode_menu_disc_tune_shop_flow) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    gt.state().credits = 1000000;
    CHECK(gt.buy_car("altezza_rs200_98") == 0);
    choose(f, "gt_mode");
    settle(f);
    choose(f, "ts_ts");
    CHECK(f.controller()->page_name() == "ts/ts");
    choose(f, "bg2");
    CHECK(f.controller()->page_name() == "ts/ts_eg");

    // Bought and installed (muf_sports, goods 37).
    choose(f, "item1");
    CHECK(f.controller()->page_name() == "ts/ts_eg_muf_sp");
    CHECK(gt.can_buy_part(37) == game::Check::Ok);
    const float before = gt.ride_power_ps();
    choose(f, "muf_sports");
    CHECK(f.controller()->depth() == 1 && f.controller()->page_name() == "ts/ts_buy_sure1");
    choose(f, "yes1");
    CHECK(gt.owns(37));
    CHECK(f.controller()->depth() == 1 && f.controller()->page_name() == "ts/ts_fit_sure1");
    choose(f, "yes1");
    CHECK(f.controller()->depth() == 1 && f.controller()->page_name() == "ts/ts_fit_end1");
    CHECK(gt.ride_power_ps() > before);
    // (The widgets' classes say torque; the original prints the power in both, "%d %s".)
    CHECK(widget_text(f, "car_previous_torque") == gt.power_text(before, " "));
    CHECK(widget_text(f, "car_current_torque") == gt.power_text(gt.ride_power_ps(), " "));
    choose(f, "yes1");
    CHECK(f.controller()->depth() == 0 && f.controller()->page_name() == "ts/ts_eg_muf_sp");
    CHECK(widget_text(f, "price_muf_sports") == "Purchased");
    // Bought again: refused.
    choose(f, "muf_sports");
    CHECK(f.controller()->depth() == 1 && f.controller()->page_name() == "ts/already_you_have");
    press(f, MenuKey::Cancel);
    CHECK(f.controller()->depth() == 0 && f.controller()->page_name() == "ts/ts_eg_muf_sp");
    press(f, MenuKey::Cancel);
    CHECK(f.controller()->page_name() == "ts/ts_eg");

    // Bought, not installed: No on "Install parts now?" also returns to the part's page.
    choose(f, "item2");
    CHECK(f.controller()->page_name() == "ts/ts_eg_muf_semi");
    const float fitted = gt.ride_power_ps();
    choose(f, "muf_semi_racing");
    choose(f, "yes1");
    CHECK(gt.owns(38));
    CHECK(f.controller()->depth() == 1 && f.controller()->page_name() == "ts/ts_fit_sure1");
    choose(f, "no1");
    CHECK(f.controller()->depth() == 0 && f.controller()->page_name() == "ts/ts_eg_muf_semi");
    CHECK(gt.ride_power_ps() == fitted);
    press(f, MenuKey::Cancel);
    CHECK(f.controller()->page_name() == "ts/ts_eg");
}

// Race information -> Cars Permitted: the event's regulation list (car name and second
// name, each string once), a cursor row scrolling through it; "No Limitations" without one.
TEST(gtmode_menu_disc_permitted_cars) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    std::vector<std::string> names = game::permitted_car_names(gt, "G_80sports_e_0001");
    CHECK(names.size() >= 10);
    CHECK(std::find(names.begin(), names.end(), std::string()) == names.end());
    for (size_t i = 0; i < names.size(); ++i)
        CHECK(std::count(names.begin(), names.end(), names[i]) == 1);
    CHECK(game::permitted_car_names(gt, "Gsunday_e_0001").empty());

    auto rows = [&] {
        std::vector<std::string> out;
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && d.text && (d.element->name == "link_menu" || d.element->name == "active_menu"))
                out.push_back(*d.text);
        return out;
    };
    auto focused_row = [&] {
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && d.text && d.element->name == "active_menu") return *d.text;
        return std::string("<none>");
    };
    choose(f, "gt_mode");
    settle(f);
    choose(f, "r_r");
    choose(f, "beginner");
    choose(f, "G_80sports_e_0001");
    CHECK(f.controller()->page_name() == "r/beginner/g_80sports_e");
    choose(f, "G_80sports_e_0001_info");
    CHECK(f.controller()->page_name() == "00cmn/race_info");
    choose(f, "car_regulation");
    CHECK(f.controller()->depth() == 1 && f.controller()->page_name() == "00cmn/car_reguration");
    std::vector<std::string> shown = rows();
    CHECK(!shown.empty() && shown.size() < names.size()); // the box holds fewer rows than the list
    CHECK(shown.size() >= 1 && shown[0] == names[0]);
    CHECK(focused_row() == names[0]);
    for (size_t i = 1; i < names.size() + 2; ++i) press(f, MenuKey::Down);
    CHECK(focused_row() == names.back()); // clamped at the end
    shown = rows();
    CHECK(!shown.empty() && shown.back() == names.back());
    press(f, MenuKey::Cancel);
    CHECK(f.controller()->depth() == 0 && f.controller()->page_name() == "00cmn/race_info");

    // A race without a list.
    press(f, MenuKey::Cancel);
    settle(f);
    press(f, MenuKey::Cancel);
    settle(f);
    choose(f, "Gsunday_e_0001");
    choose(f, "Gsunday_e_0001_info");
    CHECK(f.controller()->page_name() == "00cmn/race_info");
    choose(f, "car_regulation");
    CHECK(f.controller()->page_name() == "00cmn/car_reguration");
    shown = rows();
    CHECK(shown.empty());
    bool none = false;
    for (const menu::ElementDraw& d : f.draw_list())
        none = none || (d.element && d.element->name == "no_regulated_car");
    CHECK(none);
}

TEST(gtmode_menu_disc_touch_taps) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    // Title (a scrolling selector): tapping the focused item in the window confirms it.
    MenuController* c = f.controller();
    CHECK(c->focus() == 0);
    CHECK(c->set_focus("gt_mode"));
    settle(f);
    int hit = -1;
    for (float y = 0; y < 448 && hit != c->focus(); y += 2)
        for (float x = 0; x < 640 && hit != c->focus(); x += 4)
            if (c->hit_test(x, y) == c->focus()) {
                hit = c->focus();
                CHECK(!f.hover(x + 1, y + 1)); // (no hover focus on a scrolling selector)
                CHECK(f.tap(x + 1, y + 1));
            }
    CHECK(hit == c->focus());
    settle(f);
    CHECK(f.controller()->page_name() == "map");
    // Map: a tap on another item focuses it, a second tap confirms it.
    c = f.controller();
    int dealer = -1;
    for (int i = 0; i < int(c->items().size()); ++i)
        if (c->items()[size_t(i)].flow->name == "cd_cd") dealer = i;
    CHECK(dealer >= 0);
    const menu::Element* e = c->items()[size_t(dealer)].element;
    float x = e->x + e->w / 2, y = e->y + e->h / 2;
    CHECK(c->hit_test(x, y) == dealer);
    CHECK(c->hit_test(-10, -10) == -1);
    // A hovering pointer (mouse, VR controller) focuses the item it is over at once (the map's
    // items stay put); then the taps below work as before.
    const int other = dealer == 0 ? 1 : 0;
    const menu::Element* eo = c->items()[size_t(other)].element;
    const float ox = eo->x + eo->w / 2, oy = eo->y + eo->h / 2;
    if (c->hit_test(ox, oy) == other) {
        CHECK(f.hover(ox, oy));
        CHECK(c->focus() == other);
    }
    CHECK(f.tap(x, y));
    settle(f);
    CHECK(f.controller()->focus() == dealer && f.controller()->page_name() == "map");
    CHECK(f.tap(x, y));
    settle(f);
    CHECK(f.controller()->page_name() == "cd/cd");
}

// A hovering pointer in the shops: the tune shop's parts and the wheel shop's makers are
// scrolling rows (they hold still while the pointer moves the cursor, so one click chooses);
// the wheel shop's tiles are drawn by GT-mode code (their own hit test).
TEST(gtmode_menu_disc_pointer_shops) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    s->gt().state().credits = 1000000;
    CHECK(s->gt().buy_car("altezza_rs200_98") == 0);
    choose(f, "gt_mode");
    settle(f);
    auto centre = [&](int i, float& x, float& y) {
        SDL_FRect b{};
        if (!f.controller()->item_box(i, b)) return false;
        x = b.x + b.w / 2, y = b.y + b.h / 2;
        return true;
    };
    // Tune shop, engine parts: a horizontal row in a window.
    choose(f, "ts_ts");
    choose(f, "bg2");
    CHECK(f.controller()->page_name() == "ts/ts_eg");
    float x = 0, y = 0;
    CHECK(centre(6, x, y));
    CHECK(f.hover(x, y));
    CHECK(f.controller()->focus() == 6);
    for (int i = 0; i < 30; ++i) f.tick(0);
    float x2 = 0, y2 = 0;
    CHECK(centre(6, x2, y2) && x2 == x && y2 == y); // the row held still (it scrolls for 6 by key)
    // One click chooses, with the row still held (the choice's feedback must not wait for a
    // scroll that never comes: it hung the menu).
    CHECK(f.tap(x, y));
    f.tick(0);
    settle(f);
    CHECK(f.controller()->page_name() == "ts/ts_eg_port" && !f.controller()->busy());
    press(f, MenuKey::Cancel);
    CHECK(f.controller()->page_name() == "ts/ts_eg");
    CHECK(centre(1, x, y) && f.hover(x, y) && f.controller()->focus() == 1); // (the row came back scrolled)
    CHECK(f.tap(x, y)); // the semi-racing muffler
    f.tick(0);
    settle(f);
    CHECK(f.controller()->page_name() == "ts/ts_eg_muf_semi");
    press(f, MenuKey::Cancel);
    CHECK(f.controller()->page_name() == "ts/ts_eg");
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    CHECK(f.controller()->page_name() == "map");
    // Wheel shop: the makers' row, then a maker's wheels.
    choose(f, "at_at");
    choose(f, "sl");
    CHECK(f.controller()->page_name() == "at/ws/ws");
    CHECK(centre(0, x, y) && f.hover(x, y) && f.controller()->focus() == 0);
    CHECK(f.tap(x, y));
    f.tick(0);
    settle(f);
    CHECK(f.controller()->page_name() == "at/ws/ws_bridgestone");
    for (int i = 0; i < 30; ++i) f.tick(0);
    auto tile = [&](const char* wheel, float& tx, float& ty) {
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && d.has_box && d.element->name == wheel) {
                tx = d.box.x + d.box.w / 2, ty = d.box.y + d.box.h / 2;
                return true;
            }
        return false;
    };
    CHECK(tile("brw002_s", x, y));
    CHECK(f.hover(x, y));
    CHECK(f.controller()->focus() == 2);
    CHECK(f.tap(x, y)); // one click: the purchase question
    f.tick(0);
    settle(f);
    CHECK(f.controller()->depth() == 1);
}

// The wheel shop's highlights (menus §5.3, §9.7). On the GT Auto page the Wheel Shop button's
// highlight sits on the button, although the layout puts it 10 units to the right (the
// playtest saw the pulsing normal look flash beside it). The makers' row highlights the focused
// maker in the row. On each maker's page the focused tile alone is washed, once, on that tile,
// and nothing of the page's own (parked) items shows on the screen; a hovering pointer still
// moves the wash.
TEST(gtmode_menu_disc_wheel_shop_highlights) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    s->gt().state().credits = 1000000;
    CHECK(s->gt().buy_car("altezza_rs200_98") == 0);
    choose(f, "gt_mode");
    settle(f);
    choose(f, "at_at");
    CHECK(f.controller()->page_name() == "at/at");
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
    // Where a draw puts its element (its replacement box, else the element's own place).
    auto origin = [](const menu::ElementDraw& d, float& x, float& y) {
        x = d.has_box ? d.box.x : d.element->x + d.dx;
        y = d.has_box ? d.box.y : d.element->y + d.dy;
    };
    // The focused button's looks over a pulse period: the highlight once a frame and the normal
    // look over it, both where the list puts the button.
    auto button_ok = [&]() {
        const MenuController& c = *f.controller();
        const MenuController::ItemState* it = c.focused();
        SDL_FRect box{};
        if (!it || !it->element || it->element->alt.empty() || !c.item_box(c.focus(), box)) return false;
        bool ok = true;
        int pulses = 0;
        for (int i = 0; i < 30; ++i) {
            f.tick(0);
            int highlights = 0;
            for (const menu::ElementDraw& d : f.draw_list()) {
                if (d.kind != menu::ElementDraw::Kind::Element) continue;
                if (d.element != it->element && d.element != &it->element->alt[0]) continue;
                float x, y;
                origin(d, x, y);
                ok = ok && near(x, box.x) && near(y, box.y);
                if (d.element != it->element) ++highlights;
                else if (d.alpha > 0) ++pulses;
            }
            ok = ok && highlights == 1;
        }
        return ok && pulses > 0;
    };
    CHECK(f.controller()->set_focus("sl"));
    const menu::Element& sl = *f.controller()->focused()->element;
    CHECK(!sl.alt.empty() && near(sl.alt[0].x - sl.x, 10)); // the layout's highlight, beside the button
    CHECK(button_ok());
    choose(f, "sl");
    CHECK(f.controller()->page_name() == "at/ws/ws");
    // Each maker's tiles: the wash on the focused tile only.
    auto tiles_ok = [&]() {
        const MenuController& c = *f.controller();
        const MenuController::ItemState* it = c.focused();
        if (!it || !it->flow) return false;
        const std::string wheel = it->flow->name.substr(0, it->flow->name.rfind('_'));
        std::vector<menu::ElementDraw> list = f.draw_list();
        const menu::ElementDraw* tile = nullptr;
        for (const menu::ElementDraw& d : list)
            if (d.element && d.has_box && d.element->name == wheel) tile = &d;
        if (!tile) return false;
        int washes = 0;
        bool ok = true;
        for (const menu::ElementDraw& d : list) {
            if (d.kind == menu::ElementDraw::Kind::Fill && d.has_box && d.fill.r == 1 && d.fill.g == 1 &&
                d.fill.b == 1 && d.fill.a < 1) {
                ++washes;
                ok = ok && near(d.box.x, tile->box.x) && near(d.box.y, tile->box.y) && near(d.box.w, tile->box.w) &&
                     near(d.box.h, tile->box.h);
            }
            for (const MenuController::ItemState& item : c.items())
                if (d.element && item.element &&
                    (d.element == item.element || (!item.element->alt.empty() && d.element == &item.element->alt[0]))) {
                    float x, y;
                    origin(d, x, y);
                    ok = ok && x >= 640; // parked: the page's own button art never shows
                }
        }
        return ok && washes == 1;
    };
    int makers = 0, makers_ok = 0;
    for (int m = 0; m < int(f.controller()->items().size()); ++m) {
        f.controller()->set_focus(m);
        CHECK(button_ok()); // the makers' row
        press(f, MenuKey::Confirm);
        if (f.controller()->page_name().rfind("at/ws/ws_", 0) != 0) {
            CHECK(false);
            break;
        }
        ++makers;
        for (int i = 0; i < 30; ++i) f.tick(0);
        bool ok = tiles_ok();
        press(f, MenuKey::Right);
        press(f, MenuKey::Right);
        ok = ok && tiles_ok();
        // A hovering pointer over the first tile moves the wash there.
        float x = 0, y = 0;
        for (const menu::ElementDraw& d : f.draw_list()) {
            const std::string& first = f.controller()->items()[0].flow->name;
            if (d.element && d.has_box && d.element->name == first.substr(0, first.rfind('_')))
                x = d.box.x + d.box.w / 2, y = d.box.y + d.box.h / 2;
        }
        ok = ok && f.hover(x, y) && f.controller()->focus() == 0 && tiles_ok();
        if (ok) ++makers_ok;
        else std::printf("  %s: the wash is not on the focused tile alone\n", f.controller()->page_name().c_str());
        press(f, MenuKey::Cancel);
        CHECK(f.controller()->page_name() == "at/ws/ws");
    }
    CHECK(makers == 9 && makers_ok == makers);
}

// The dealer's list (gt_mode §7): the focused car's price as "Cr. 785,000", or "----" for a car
// whose not-for-sale flag (car +0xF0 bit 0) is set. The Nissan dealer lists the Pennzoil Nismo
// GT-R (785,000 Cr.), then the Z Concept, the NISMO GT-R LM road car and the R390 GT1, the last
// three not for sale. Any region's disc, in each of its languages; x100 in Japanese (gt_mode §2).
TEST(gtmode_menu_disc_dealer_list_not_for_sale) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    const RegionData rd = vfs.region();
    for (Language lang : rd.languages()) {
        // A fresh GT-mode module per language: it keeps the game data it loaded first.
        game::reset_gt_mode_menu_for_tests();
        vfs.set_language(lang);
        GameFlow f(vfs_reader(vfs), text::language_code(lang));
        f.start();
        settle(f);
        game::GtModeSession* s = game::gt_mode_session();
        CHECK(s != nullptr);
        if (!s) break;
        const game::GtMode& gt = s->gt();
        const s64 k = lang == Language::JP ? 100 : 1;
        CHECK(gt.data().money_scale == int(k));
        choose(f, "gt_mode");
        settle(f);
        choose(f, "cd_cd");
        choose(f, "jp");
        choose(f, "dealer5");
        CHECK(f.controller()->page_name() == "cd/cd_jpn_nissan");
        // Each car's layer holds its price widget ("<car>_price"): the focused car's is drawn.
        std::map<std::string, std::string> price;
        int not_for_sale = 0, for_sale = 0;
        for (int i = 0; i < int(f.controller()->items().size()); ++i) {
            const std::string car = f.controller()->items()[size_t(i)].flow->name;
            CHECK(f.controller()->set_focus(car));
            for (int t = 0; t < 30; ++t) f.tick(0); // (the layers switch on the next frames)
            std::vector<std::string> drawn;
            for (const menu::ElementDraw& d : f.draw_list())
                if (d.element && d.element->widget_class && *d.element->widget_class == "goods_car_price" && d.text) {
                    CHECK(d.element->name == car + "_price");
                    drawn.push_back(*d.text);
                }
            CHECK(drawn.size() == 1);
            if (drawn.size() != 1) continue;
            price[car] = drawn[0];
            if (gt.car_flags(car) & 1) {
                ++not_for_sale;
                CHECK(drawn[0] == "----");
            } else {
                ++for_sale;
                CHECK(drawn[0] == "Cr. " + game::format_credits(gt.car_price(car)));
            }
        }
        CHECK(not_for_sale == 3 && for_sale == 14);
        CHECK(price["penzoil_gtr_99"] == (k == 100 ? "Cr. 78,500,000" : "Cr. 785,000"));
        CHECK(price["z_detroitver_01"] == "----" && gt.car_price("z_detroitver_01") == 300000 * k);
        CHECK(price["nismo_gtr_lm_00"] == "----" && price["r390_race_98"] == "----");
        CHECK(price["s14_ks_aero_96"] == (k == 100 ? "Cr. 2,395,000" : "Cr. 23,950"));
        // The Z Concept's own page: no price either, and no sale.
        choose(f, "z_detroitver_01");
        CHECK(f.controller()->page_name() == "cd/cd_jpn_nissan_car" && s->focus_car == "z_detroitver_01");
        CHECK(widget_text(f, "car_buy_price") == "----");
        s->gt().state().credits = 10000000 * k;
        choose(f, "buy");
        CHECK(f.controller()->page_name() == "00cmn/not_for_buy" && s->gt().state().garage.empty());
    }
    game::reset_gt_mode_menu_for_tests();
}

// The wheel inventory (gt_mode §7): 200 slots, one of them the stock-wheel placeholder every new
// game starts with, so 199 wheels can be bought. With no slot free the shop adds nothing and
// charges nothing, and its "fit it?" still puts the wheel on the ride car, for free and outside
// the inventory. Every disc's shop sells 179 wheels: the test buys them all but two, fills the
// inventory up to 198 bought wheels directly (more than the shop offers), then through the shop's
// pages buys the 199th (charged the game config's wheel price) and tries a 200th, answering yes
// to fitting it, with the same outcome as GT3's shop on a full inventory. Any region's disc; x100
// in Japanese (gt_mode §2).
TEST(gtmode_menu_disc_wheel_inventory_full) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::reset_gt_mode_menu_for_tests();
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    const Language lang = vfs.region().default_language();
    vfs.set_language(lang);
    GameFlow f(vfs_reader(vfs), text::language_code(lang));
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    const s64 k = lang == Language::JP ? 100 : 1;
    const s64 price = gt.data().wheel_price();
    CHECK(price == 500 * k);
    CHECK(gt.state().wheels.empty()); // a new game: only the placeholder
    gt.state().credits = 1000000 * k;
    CHECK(gt.buy_car("r34_gtr_v_99") == 0 && !(gt.car_flags("r34_gtr_v_99") & 4));
    choose(f, "gt_mode");
    settle(f);
    choose(f, "at_at");
    choose(f, "sl");
    CHECK(f.controller()->page_name() == "at/ws/ws");
    // The shop's wheels, maker by maker ("<wheel>_link" items).
    std::vector<std::string> shop;
    const int makers = int(f.controller()->items().size());
    for (int m = 0; m < makers; ++m) {
        f.controller()->set_focus(m);
        press(f, MenuKey::Confirm);
        for (const MenuController::ItemState& it : f.controller()->items())
            if (it.flow->action == "set_focus_wheel_code")
                shop.push_back(it.flow->name.substr(0, it.flow->name.rfind('_')));
        press(f, MenuKey::Cancel);
    }
    std::sort(shop.begin(), shop.end());
    CHECK(makers == 9 && shop.size() == 179 && std::unique(shop.begin(), shop.end()) == shop.end());
    if (shop.size() < 2) return;
    // The first maker's first two wheels are kept for the shop's pages.
    f.controller()->set_focus(0);
    press(f, MenuKey::Confirm);
    const std::string page = f.controller()->page_name();
    std::vector<std::string> keep;
    for (const MenuController::ItemState& it : f.controller()->items())
        if (it.flow->action == "set_focus_wheel_code" && keep.size() < 2) keep.push_back(it.flow->name);
    CHECK(keep.size() == 2);
    if (keep.size() != 2) return;
    s64 credits = gt.state().credits;
    for (const std::string& w : shop) {
        if (w + "_link" == keep[0] || w + "_link" == keep[1]) continue;
        CHECK(gt.buy_wheel(w) && gt.state().credits == credits - price);
        credits = gt.state().credits;
    }
    CHECK(gt.state().wheels.size() == 177);
    for (int i = 0; gt.state().wheels.size() < 198; ++i)
        gt.state().wheels.push_back("extra_wheel_" + std::to_string(i));
    // The 199th: bought and charged.
    choose(f, keep[0].c_str());
    CHECK(f.controller()->page_name() == "00cmn/ws_buy_sure");
    choose(f, "yes1");
    CHECK(gt.state().wheels.size() == 199 && gt.state().wheels.back() + "_link" == keep[0]);
    CHECK(gt.state().credits == credits - price);
    CHECK(f.controller()->page_name() == "00cmn/ws_fit_sure");
    choose(f, "no1");
    CHECK(f.controller()->page_name() == page);
    // The 200th: no slot is free. The shop asks as usual, then adds nothing and charges nothing.
    credits = gt.state().credits;
    choose(f, keep[1].c_str());
    CHECK(f.controller()->page_name() == "00cmn/ws_buy_sure");
    choose(f, "yes1");
    CHECK(gt.state().wheels.size() == 199 && gt.state().credits == credits);
    CHECK(gt.state().wheels.back() + "_link" == keep[0]);
    const std::string last = keep[1].substr(0, keep[1].rfind('_'));
    CHECK(gt.can_buy_wheel(last) == game::Check::Ok && !gt.buy_wheel(last) && gt.state().credits == credits);
    // "Fit it?" yes: the ride car wears the wheel, which the inventory does not hold; nothing is
    // charged.
    CHECK(f.controller()->page_name() == "00cmn/ws_fit_sure");
    choose(f, "yes1");
    CHECK(f.controller()->page_name() == page);
    CHECK(gt.state().ride_car()->wheel == last && gt.state().credits == credits);
    CHECK(gt.state().wheels.size() == 199 &&
          std::find(gt.state().wheels.begin(), gt.state().wheels.end(), last) == gt.state().wheels.end());
    // The garage's car page shows the car on it.
    std::string shown, wheel;
    int paint = -1;
    CHECK(game::car_for_widget("hm/hm_car", "car_render_garage", shown, paint, &wheel) && wheel == last);
    // Saves keep it: the native save, and the PS2 game file (the car's wheel by id, after the 0x130
    // bytes of the game's figures; 200 wheel entries, the placeholder included).
    const game::GameState native = game::load_from_text(game::save_to_text(gt.state()));
    CHECK(native.garage.size() == 1 && native.garage[0].wheel == last && native.wheels == gt.state().wheels);
    const Bytes file = game::export_ps2_save(gt.state(), gt.data());
    Bytes payload(file.begin() + 0x40, file.begin() + 0x40 + rd32(file, 4));
    for (u8& b : payload) b = u8(~b);
    CHECK(rd32(payload, 0x34) == 200 && rd64(payload, 0x130 + 0xE8) == db::id_hash(last));
    const game::GameState ps2 = game::import_ps2_save(file, gt.data());
    CHECK(ps2.garage.size() == 1 && ps2.garage[0].wheel == last && ps2.wheels == gt.state().wheels);
    // Once changed, it cannot be put back: the garage offers only the inventory's wheels.
    CHECK(gt.fit_wheel(gt.state().ride, "") && !gt.fit_wheel(gt.state().ride, last));
    game::reset_gt_mode_menu_for_tests();
}

// The garage's wheel list (gt_mode §7): Garage > car > Wheel shows one button per entry of the
// wheel inventory in a row centred in the page's "menu_wheel" window, 4 units apart: the
// stock-wheel button first with the page's "default_wheel" tile, then the bought wheels in the
// order they were bought, each with its own tile in the page's 60 x 60 "wheel" frame. The "mark"
// frame is over the entry the car wears and the cursor starts there (on the stock-wheel button
// for a car on its own wheels; no mark and the first button for a wheel the inventory does not
// hold). Left / Right move the cursor (held at the ends) and the car shows the wheel under it;
// "Equip this wheel?" yes puts it on the car, the stock-wheel button the car's own wheels. The
// scroll bar shows only while the row is longer than the window. None of the page's tiles named
// after car models is drawn. The wheel shop's brand pages start on the wheel the ride car wears.
// Any region's disc.
TEST(gtmode_menu_disc_garage_wheel_list) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::reset_gt_mode_menu_for_tests();
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    const Language lang = vfs.region().default_language();
    vfs.set_language(lang);
    GameFlow f(vfs_reader(vfs), text::language_code(lang));
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    gt.state().credits = 100000000;
    CHECK(gt.buy_car("r34_gtr_v_99") == 0);
    const std::vector<std::string> bought = {"bbw005_g", "duw001_g", "bbw004_g"};
    for (const std::string& w : bought) CHECK(gt.buy_wheel(w));
    const game::GarageCar& car = gt.state().garage[0];
    choose(f, "gt_mode");
    settle(f);
    choose(f, "hm_hm");
    choose(f, "gr");
    choose(f, "car1");
    CHECK(f.controller()->page_name() == "hm/hm_car");
    choose(f, "wheel");
    CHECK(f.controller()->page_name() == "hm/hm_wheel");
    const menu::Page& lay = f.controller()->page().layout();
    const menu::Element* win = nullptr;
    for (const menu::Layer& l : lay.layers)
        for (const menu::Element& e : l.widgets)
            if (e.name == "menu_wheel") win = &e;
    CHECK(win != nullptr);
    if (!win) return;
    // The drawn tiles (52 x 52, inside the window), frames and marks, left to right.
    struct Drawn {
        std::string name;
        SDL_FRect box;
    };
    auto drawn = [&](auto pick) {
        std::vector<Drawn> out;
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && d.has_box && pick(*d.element)) out.push_back({d.element->name, d.box});
        std::sort(out.begin(), out.end(), [](const Drawn& a, const Drawn& b) { return a.box.x < b.box.x; });
        return out;
    };
    auto tiles = [&] { return drawn([](const menu::Element& e) { return e.w == 52 && e.h == 52; }); };
    // The buttons' frames, one per button (the focused one: its highlight with the plain look over it).
    auto frames = [&] {
        std::vector<Drawn> all = drawn([](const menu::Element& e) { return e.name == "wheel" || e.name == "wheel1"; });
        all.erase(std::unique(all.begin(), all.end(), [](const Drawn& a, const Drawn& b) { return a.box.x == b.box.x; }),
                  all.end());
        return all;
    };
    auto marks = [&] { return drawn([](const menu::Element& e) { return e.name == "mark"; }); };
    auto focused_frame = [&] {
        for (const Drawn& d : drawn([](const menu::Element& e) { return e.name == "wheel1"; })) return d.box.x;
        return -1.0f;
    };
    auto preview = [&] {
        std::string c, w = "?";
        int paint = -1;
        game::car_for_widget(f.controller()->page_name(), "car_render_wheel", c, paint, &w);
        return w;
    };
    auto settle_scroll = [&] {
        for (int i = 0; i < 60; ++i) f.tick(0);
    };
    settle_scroll();
    // A car on its own wheels: the stock-wheel button is marked and has the cursor.
    {
        const std::vector<Drawn> t = tiles(), fr = frames(), m = marks();
        CHECK(t.size() == 4 && fr.size() == 4 && m.size() == 1);
        if (t.size() == 4 && fr.size() == 4 && m.size() == 1) {
            CHECK(t[0].name == "default_wheel" && t[1].name == bought[0] && t[2].name == bought[1] &&
                  t[3].name == bought[2]);
            const float total = 4 * 60 + 3 * 4, left = win->x + (win->w - total) * 0.5f;
            for (int i = 0; i < 4; ++i) {
                CHECK(std::fabs(fr[size_t(i)].box.x - (left + 64.0f * float(i))) < 0.01f);
                CHECK(std::fabs(t[size_t(i)].box.x - (fr[size_t(i)].box.x + 4)) < 0.01f);
                CHECK(std::fabs(fr[size_t(i)].box.y - (win->y + 6)) < 0.01f);
            }
            CHECK(std::fabs(m[0].box.x - fr[0].box.x) < 0.01f && std::fabs(focused_frame() - fr[0].box.x) < 0.01f);
        }
        CHECK(s->wheel_cursor == 0 && preview().empty());
        // No scroll bar while the row fits.
        bool bar_drawn = false;
        for (const menu::ElementDraw& d : f.draw_list())
            bar_drawn = bar_drawn || (d.element && (d.element->name == "scroll_bar" || d.element->name == "pinch"));
        CHECK(!bar_drawn);
    }
    // Right twice: the second bought wheel under the cursor, on the car; at the end it stays.
    press(f, MenuKey::Right);
    press(f, MenuKey::Right);
    settle_scroll();
    CHECK(s->wheel_cursor == 2 && preview() == bought[1]);
    {
        const std::vector<Drawn> fr = frames(), m = marks();
        CHECK(fr.size() == 4 && m.size() == 1);
        if (fr.size() == 4 && m.size() == 1)
            CHECK(std::fabs(focused_frame() - fr[2].box.x) < 0.01f && std::fabs(m[0].box.x - fr[0].box.x) < 0.01f);
    }
    for (int i = 0; i < 4; ++i) press(f, MenuKey::Right);
    CHECK(s->wheel_cursor == 3);
    press(f, MenuKey::Left);
    CHECK(s->wheel_cursor == 2);
    // Equip it.
    press(f, MenuKey::Confirm);
    settle(f);
    CHECK(f.controller()->page_name() == "hm/hm_change_wheel_sure");
    choose(f, "yes1");
    CHECK(car.wheel == bought[1] && f.controller()->page_name() == "hm/hm_wheel_changed");
    choose(f, "buy3");
    CHECK(f.controller()->page_name() == "hm/hm_car");
    // Back on the page: the cursor and the mark on the wheel the car wears.
    choose(f, "wheel");
    settle_scroll();
    CHECK(s->wheel_cursor == 2);
    {
        const std::vector<Drawn> fr = frames(), m = marks();
        if (fr.size() == 4 && m.size() == 1) CHECK(std::fabs(m[0].box.x - fr[2].box.x) < 0.01f);
        else CHECK(false);
    }
    // The stock-wheel button puts the car back on its own wheels.
    press(f, MenuKey::Left);
    press(f, MenuKey::Left);
    press(f, MenuKey::Confirm);
    settle(f);
    choose(f, "yes1");
    CHECK(car.wheel.empty());
    choose(f, "buy3");
    // A row longer than the window (13 buttons, 828 units): left-aligned near the start, the
    // cursor's button centred in the middle, right-aligned at the end; the scroll bar and its
    // pinch at bar x - offset x bar w / row w, window w x bar w / row w wide (GT3's ScrollBar).
    for (const char* w : {"bbw006_s", "bbw007_s", "bbw008_s", "bbw009_s", "bbw010_s", "bbw011_s", "bbw012_s",
                          "bbw013_s", "duw001_s"})
        CHECK(gt.buy_wheel(w));
    choose(f, "wheel");
    settle_scroll();
    {
        const menu::Element* sbar = nullptr;
        for (const menu::Layer& l : lay.layers)
            for (const menu::Element& e : l.widgets)
                if (e.name == "scroll_bar") sbar = &e;
        CHECK(sbar != nullptr && int(gt.state().wheels.size()) + 1 == 13);
        const float total = 13 * 60 + 12 * 4;
        auto row_at = [&](int cursor, float off) {
            while (s->wheel_cursor < cursor) press(f, MenuKey::Right);
            settle_scroll();
            const std::vector<Drawn> fr = frames(), p = drawn([](const menu::Element& e) { return e.name == "pinch"; });
            int tracks = 0; // (the track is drawn where the layout puts it, without a box)
            for (const menu::ElementDraw& d : f.draw_list()) tracks += d.element == sbar;
            return s->wheel_cursor == cursor && !fr.empty() && sbar && tracks == 1 && p.size() == 1 &&
                   std::fabs(focused_frame() - (win->x + off + 64.0f * float(cursor))) < 0.01f &&
                   std::fabs(p[0].box.x - (sbar->x - off * sbar->w / total)) < 0.01f &&
                   std::fabs(p[0].box.w - win->w * sbar->w / total) < 0.01f;
        };
        CHECK(row_at(0, 0));
        CHECK(row_at(6, win->w * 0.5f - (6 * 64 + 30)));
        CHECK(row_at(12, win->w - total));
    }
    press(f, MenuKey::Cancel);
    CHECK(f.controller()->page_name() == "hm/hm_car");
    // A wheel the inventory does not hold (fitted by the shop with no slot free): no mark, the
    // cursor on the first button.
    gt.state().garage[0].wheel = "enw010_s";
    choose(f, "wheel");
    settle_scroll();
    CHECK(s->wheel_cursor == 0 && marks().empty());
    // Only the default_wheel and the bought wheels' tiles: none named after a car model.
    for (const Drawn& t : tiles())
        CHECK(t.name == "default_wheel" ||
              std::find(gt.state().wheels.begin(), gt.state().wheels.end(), t.name) != gt.state().wheels.end());
    // The wheel shop: a brand page opens on the wheel the ride car wears, another on its first.
    gt.state().garage[0].wheel = bought[0];
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    CHECK(f.controller()->page_name() == "map");
    choose(f, "at_at");
    choose(f, "sl");
    CHECK(f.controller()->page_name() == "at/ws/ws");
    choose(f, "ts");
    CHECK(f.controller()->page_name() == "at/ws/ws_bbs" && f.controller()->focused() &&
          f.controller()->focused()->flow->name == bought[0] + "_link");
    // Its tiles: GT3's button row, 52-unit tiles 4 apart (56 from tile to tile), placed for the
    // focused tile (14 tiles, 780 units in the 600-unit window: tile 5 starts at 280, under half
    // the window, so the row is left-aligned).
    {
        const menu::Element* shop_win = nullptr;
        for (const menu::Layer& l : f.controller()->page().layout().layers)
            for (const menu::Element& e : l.widgets)
                if (e.name == "menu_wheel_shop") shop_win = &e;
        std::vector<float> xs;
        for (const MenuController::ItemState& it : f.controller()->items()) {
            const std::string code = it.flow->name.substr(0, it.flow->name.rfind('_'));
            for (const Drawn& d : drawn([&](const menu::Element& e) { return e.name == code; })) xs.push_back(d.box.x);
        }
        CHECK(shop_win && f.controller()->focus() == 5 && xs.size() >= 10);
        for (size_t i = 0; shop_win && i < xs.size(); ++i) CHECK(std::fabs(xs[i] - (shop_win->x + 56.0f * float(i))) < 0.01f);
    }
    press(f, MenuKey::Cancel);
    choose(f, "hm");
    CHECK(f.controller()->page_name() == "at/ws/ws_bridgestone" && f.controller()->focus() == 0);
    game::reset_gt_mode_menu_for_tests();
}

// The code-drawn vertical lists on the disc's pages as GT3's vertical button list (menus §4.6,
// §9.7; gt_mode §5, §8, §17.2, §19):
// the garage and trade lists (the 24-unit "car1" rows 28 apart in the 304-unit window), Cars
// Permitted (the 20-unit rows 24 apart in 232) and the records (the 56-unit bars 60 apart in
// 356: a licence test's six places fill it). From the window's top; a list that fits stays
// there with no scroll bar, track included; a longer one keeps the cursor row centred, held at
// the window's top and bottom near the ends, its offset easing 30 % a frame, and its scroll
// bar's pinch is window h x bar h / list h high, at bar top - offset x bar h / list h
// (top-down).
TEST(gtmode_menu_disc_vertical_lists) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::reset_gt_mode_menu_for_tests();
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    const Language lang = vfs.region().default_language();
    vfs.set_language(lang);
    GameFlow f(vfs_reader(vfs), text::language_code(lang));
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    gt.state().credits = 1000000000;
    for (int i = 0; i < 3; ++i) CHECK(gt.buy_car("r34_gtr_v_99") == 0);
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
    auto widget = [&](const char* name) -> const menu::Element* {
        for (const menu::Layer& l : f.controller()->page().layout().layers)
            for (const menu::Element& e : l.widgets)
                if (e.name == name) return &e;
        return nullptr;
    };
    auto named = [&](const char* name) {
        std::vector<menu::ElementDraw> out;
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && d.element->name == name) out.push_back(d);
        return out;
    };
    // The list's rows (code-drawn, clipped to the window) and the cursor row's top edge (y up).
    auto rows = [&](const char* tmpl, const char* focus) {
        std::vector<menu::ElementDraw> out;
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && d.has_clip && (d.element->name == tmpl || d.element->name == focus)) out.push_back(d);
        std::sort(out.begin(), out.end(), [](const menu::ElementDraw& a, const menu::ElementDraw& b) { return a.box.y > b.box.y; });
        return out;
    };
    auto cursor_top = [&](const char* tmpl, const char* focus) {
        for (const menu::ElementDraw& d : rows(tmpl, focus))
            if (d.element->name == focus) return d.box.y + d.box.h;
        return -1000.0f;
    };
    auto settle_list = [&] {
        for (int i = 0; i < 60; ++i) f.tick(0);
    };
    // The cursor row `c` at offset `off` (rows `step` apart) and the scroll bar: none while
    // `total` fits it, else the track and the pinch.
    auto at = [&](const char* tmpl, const char* focus, const menu::Element* win, float step, int c, float off,
                  float total) {
        const menu::Element* bar = widget("scroll_bar");
        auto p = named("pinch");
        const float top = win->y + win->h;
        bool ok = bar && near(cursor_top(tmpl, focus), top - off - step * float(c));
        if (total <= bar->h) return ok && named("scroll_bar").empty() && p.empty();
        const float ph = win->h * bar->h / total;
        return ok && named("scroll_bar").size() == 1 && p.size() == 1 &&
               near(p[0].box.x, bar->x) && near(p[0].box.w, bar->w) && near(p[0].box.h, ph) &&
               near(p[0].box.y + ph, bar->y + bar->h + off * bar->h / total);
    };

    // The garage: three cars at the window's top, 28 apart.
    choose(f, "gt_mode");
    settle(f);
    choose(f, "hm_hm");
    choose(f, "gr");
    CHECK(f.controller()->page_name() == "hm/hm_gr");
    const menu::Element* win = widget("menu_garage");
    CHECK(win && near(win->h, 304) && f.controller()->items()[0].element->h == 24);
    if (!win) return;
    {
        auto r = rows("car1", "car_a1");
        CHECK(r.size() == 3);
        for (size_t i = 0; i < r.size(); ++i) CHECK(near(r[i].box.y + 24, win->y + win->h - 28.0f * float(i)));
        CHECK(at("car1", "car_a1", win, 28, 0, 0, 3 * 28 - 4));
    }
    // Twenty: 556 units. The top, the cursor row centred (row 10: 152 - (280 + 12)), the bottom
    // (304 - 556); one frame of easing.
    press(f, MenuKey::Cancel);
    for (int i = 0; i < 17; ++i) CHECK(gt.buy_car("r34_gtr_v_99") == 0);
    choose(f, "gr");
    win = widget("menu_garage");
    CHECK(f.controller()->page_name() == "hm/hm_gr" && win && gt.state().garage.size() == 20);
    if (!win) return;
    const float total = 20 * 28 - 4;
    CHECK(at("car1", "car_a1", win, 28, 0, 0, total));
    s->garage_cursor = 10;
    settle_list();
    CHECK(at("car1", "car_a1", win, 28, 10, -140, total));
    s->garage_cursor = 11;
    f.tick(0);
    CHECK(at("car1", "car_a1", win, 28, 11, -140 - 28 * 0.3f, total));
    for (int i = 0; i < 12; ++i) press(f, MenuKey::Down);
    settle_list();
    CHECK(s->garage_cursor == 19 && at("car1", "car_a1", win, 28, 19, 304 - total, total));
    // The trade page (a friend's garage of four): the same list in its own window.
    s->guest = std::make_unique<game::GtMode>(gt.data_ptr());
    s->guest->state().credits = 1000000000;
    for (int i = 0; i < 4; ++i) CHECK(s->guest->buy_car("r34_gtr_v_99") == 0);
    press(f, MenuKey::Cancel);
    CHECK(f.controller()->open("hm/hm_gr_trade"));
    settle(f);
    const menu::Element* twin = widget("menu_garage_trade");
    CHECK(f.controller()->page_name() == "hm/hm_gr_trade" && s->trade && twin);
    if (twin) CHECK(rows("car1", "car_a1").size() == 4 && at("car1", "car_a1", twin, 28, 0, 0, 4 * 28 - 4));
    s->trade = false;
    s->guest.reset();
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    CHECK(f.controller()->page_name() == "map");

    // Cars Permitted: the 80's sports list (longer than the 232-unit window).
    const std::vector<std::string> names = game::permitted_car_names(gt, "G_80sports_e_0001");
    CHECK(names.size() >= 10);
    choose(f, "r_r");
    choose(f, "beginner");
    choose(f, "G_80sports_e_0001");
    choose(f, "G_80sports_e_0001_info");
    choose(f, "car_regulation");
    CHECK(f.controller()->page_name() == "00cmn/car_reguration");
    const menu::Element* rwin = widget("menu_regulated_car");
    CHECK(rwin && near(rwin->h, 232) && f.controller()->items()[0].element->h == 20);
    if (rwin && names.size() >= 10) {
        const float rtotal = float(names.size()) * 24 - 4;
        CHECK(at("link_menu", "active_menu", rwin, 24, 0, 0, rtotal));
        auto r = rows("link_menu", "active_menu");
        CHECK(r.size() >= 2 && near(r[0].box.y - r[1].box.y, 24));
        const int n = int(names.size());
        for (int i = 1; i < n; ++i) press(f, MenuKey::Down);
        settle_list();
        CHECK(s->regulated_cursor == n - 1 && at("link_menu", "active_menu", rwin, 24, n - 1, 232 - rtotal, rtotal));
    }
    press(f, MenuKey::Cancel);

    // The records: twelve places (716 units, so the list scrolls) and a licence test's six.
    CHECK(game::begin_records("mt:M400"));
    GameFlow rec(vfs_reader(vfs), text::language_code(lang));
    rec.start("records", "records_free_tt");
    settle(rec);
    CHECK(rec.controller() && rec.controller()->page_name() == "records_free_tt");
    auto bars = [&] {
        std::vector<float> out; // the bars' top edges, top to bottom; the cursor's first
        float cursor = -1000;
        for (const menu::ElementDraw& d : rec.draw_list())
            if (d.element && d.has_clip && (d.element->name == "record_bar" || d.element->name == "record_bar1")) {
                out.push_back(d.box.y + d.box.h);
                if (d.element->name == "record_bar1") cursor = d.box.y + d.box.h;
            }
        std::sort(out.rbegin(), out.rend());
        out.insert(out.begin(), cursor);
        return out;
    };
    {
        std::vector<float> b = bars(); // window 4..360
        CHECK(b.size() == 7 && near(b[0], 360));
        for (size_t i = 1; i < b.size(); ++i) CHECK(near(b[i], 360 - 60.0f * float(i - 1)));
    }
    for (int i = 0; i < 12; ++i) press(rec, MenuKey::Down);
    for (int i = 0; i < 60; ++i) rec.tick(0);
    {
        std::vector<float> b = bars(); // the last place at the window's bottom: offset 356 - 716
        CHECK(s->records_cursor == 11 && b.size() == 7 && near(b[0], 4 + 56) && near(b.back(), 4 + 56));
    }
    CHECK(game::begin_records("lic:L0B0001"));
    GameFlow lic(vfs_reader(vfs), text::language_code(lang));
    lic.start("records", "records_free_tt");
    settle(lic);
    int places = 0;
    for (const menu::ElementDraw& d : lic.draw_list())
        places += d.element && d.has_clip && (d.element->name == "record_bar" || d.element->name == "record_bar1");
    CHECK(places == 6);
    game::reset_gt_mode_menu_for_tests();
}

// The car wash and the oil change on the disc's pages (gt_mode §13.2): after the button's confirm
// feedback their progress popup ("Washing . . .", "Changing oil . . .") holds the controls, with
// no "Complete" button and no key doing anything, until the wash (the car spun for 181 frames)
// or the oil change (drained and refilled) is over; then "Complete" leads back to the shop.
TEST(gtmode_menu_disc_wash_and_oil) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    s->gt().state().credits = 1000000;
    CHECK(s->gt().buy_car("altezza_rs200_98") == 0);
    game::GarageCar* ride = s->gt().state().ride_car();
    CHECK(ride != nullptr);
    if (!ride) return;
    ride->cond.dirt = 0.5f;
    ride->cond.oil_m = 200000;
    choose(f, "gt_mode");
    settle(f);
    choose(f, "at_at");
    CHECK(f.controller()->page_name() == "at/at");
    auto drawn = [&](const char* name) {
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && d.element->name == name) return true;
        return false;
    };
    // Confirms the shop's button and runs until the controls come back: the frames the popup
    // took them (after the button's confirm feedback) and gave them back, the frames its effect
    // played, and whether the popup held them (no button, keys ignored).
    struct Run {
        int up = -1, back = -1;
        std::vector<int> sounds;
        bool held = true;
    };
    auto run = [&](const char* button, const char* popup, const char* sound) {
        Run r;
        CHECK(f.controller()->set_focus(button));
        f.take_sounds();
        f.tick(key_bit(MenuKey::Confirm));
        for (int t = 1; t <= 600 && r.back < 0; ++t) {
            const bool presses = t >= 60 && t < 160 && t % 10 == 0; // Confirm / Cancel while it runs
            f.tick(presses ? key_bit(t % 20 ? MenuKey::Confirm : MenuKey::Cancel) : MenuKeys(0));
            for (const std::string& se : f.take_sounds())
                if (se == sound) r.sounds.push_back(t);
            if (r.up < 0 && f.controller()->ignoring_control()) r.up = t;
            if (r.up < 0) continue;
            if (!f.controller()->ignoring_control()) r.back = t;
            else if (f.controller()->page_name() != popup || drawn("end") || !drawn("base1")) r.held = false;
        }
        CHECK(f.controller()->page_name() == popup && drawn("end")); // "Complete"
        return r;
    };
    choose(f, "gr");
    CHECK(f.controller()->page_name() == "at/cw/cw");
    Run wash = run("wash", "at/cw/cw_now", "car_wash");
    CHECK(wash.up == 30 && wash.held && wash.back == 212 && (wash.sounds == std::vector<int>{31}));
    CHECK(ride->cond.dirt == 0);
    press(f, MenuKey::Confirm);
    CHECK(f.controller()->page_name() == "at/at");
    choose(f, "st");
    CHECK(f.controller()->page_name() == "at/ot/ot");
    Run oil = run("wash", "at/ot/ot_now", "oil");
    CHECK(oil.up == 30 && oil.held && oil.back == 325 && (oil.sounds == std::vector<int>{62, 224}));
    CHECK(ride->cond.oil_m == -300000);
    press(f, MenuKey::Confirm);
    CHECK(f.controller()->page_name() == "at/at");
    unset_env("GT3R_GTMODE_SAVE");
    unset_env("GT3R_GTMODE_NOSAVE");
}

TEST(gtmode_menu_disc_settings_sliders) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow title(vfs_reader(vfs), "US");
    title.start(); // creates the GT-mode session
    settle(title);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    game::GtMode& gt = s->gt();
    gt.state().credits = 5000000;
    CHECK(gt.buy_car("r34_gtr_v_99") == 0);
    for (int g : {53, 22, 34, 3})
        if (gt.buy_part(g)) gt.fit_part(g);
    CHECK(game::begin_setting(""));
    GameFlow f(vfs_reader(vfs), "US");
    f.start("setting", "root");
    settle(f);
    CHECK(f.controller()->page_name() == "root");
    choose(f, "item2");
    CHECK(f.controller()->page_name() == "parts_setting");
    choose(f, "item1");
    CHECK(f.controller()->page_name() == "set/adjast_sus");
    // Shock absorbers (single damper level) are locked with the racing kit.
    CHECK(!f.controller()->item_enabled(2));
    choose(f, "status_box_sus1");
    CHECK(f.controller()->page_name() == "set/adjast_sus_spring");
    int before = gt.state().ride_car()->settings.spring[0];
    CHECK(f.controller()->set_focus("slider_rate_f"));
    settle(f);
    f.take_sounds();
    press(f, MenuKey::Right);
    std::vector<std::string> se = f.take_sounds();
    CHECK(std::find(se.begin(), se.end(), "param") != se.end());
    CHECK(s->setting_work->settings.spring[0] == before + 1);
    CHECK(gt.state().ride_car()->settings.spring[0] == before); // not committed yet
    choose(f, "ok"); // commit_slider
    CHECK(gt.state().ride_car()->settings.spring[0] == before + 1);
    auto spec = gt.car_spec(*gt.state().ride_car());
    CHECK(spec.has_value());
}

TEST(gtmode_menu_disc_attract_mode) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    CHECK(f.controller()->page_name() == "root");
    // 20 s idle on the title (gcf section 0) starts a demo race.
    std::optional<GameRequest> r;
    for (int i = 0; i < 20 * 60 + 120 && !r; ++i) {
        f.tick(0);
        r = f.take_request();
    }
    CHECK(r.has_value() && f.attract());
    if (!r) return;
    CHECK(r->mode == "attract_demo" && r->event.rfind("demo_", 0) == 0 && !r->course.empty());
    // Five demos that end on their own, then the opening movie, then the title.
    int demos = 1;
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
        if (!r) break;
        if (r->kind == GameRequest::Kind::PlayMovie) break;
        ++demos;
    }
    CHECK(demos == 5);
    CHECK(r && r->kind == GameRequest::Kind::PlayMovie && r->course == "stream/opening.pss");
    f.game_finished();
    settle(f);
    CHECK(!f.attract() && f.controller() && f.controller()->page_name() == "root");
    // A button during a demo ends attract mode at once.
    for (int i = 0; i < 20 * 60 + 120 && !r; ++i) f.tick(0);
    r.reset();
    for (int i = 0; i < 20 * 60 + 200 && !r; ++i) {
        f.tick(0);
        r = f.take_request();
    }
    CHECK(r.has_value());
    GameResult quit;
    quit.finished = false;
    post_game_result(quit);
    f.game_finished();
    settle(f);
    CHECK(!f.attract() && f.controller()->page_name() == "root");
}

// The race prize page shown from the race (before the replay): the prize counts down while the
// balance (not yet credited) counts up; OK only once the count is done.
TEST(gtmode_menu_disc_race_prize_page) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow menus(vfs_reader(vfs), "US");
    menus.start();
    settle(menus);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    s64 credits = s->gt().state().credits;
    CHECK(game::queue_race_prize("Gsunday_e_0001", 0));
    GameFlow f(vfs_reader(vfs), "US");
    CHECK(f.start_presentations());
    f.tick(0);
    CHECK(f.controller() && f.controller()->page_name() == "gtmode_mini");
    CHECK(game::presentation_model("gtmode_mini") == "race/g_easy_gold.prm");
    CHECK(widget_text(f, "rank_disp") == "1st");
    CHECK(widget_text(f, "count") == "Cr. " + game::format_credits(credits));
    CHECK(widget_text(f, "get_money_disp") == "Prize Cr. 1,000");
    for (int i = 0; i < 300; ++i) f.tick(0);
    CHECK(widget_text(f, "count") == "Cr. " + game::format_credits(credits + 1000));
    // The prize line stays at 0 for 61 frames after the count, then fades out (60 frames).
    CHECK(widget_text(f, "get_money_disp") == "Prize Cr. 0");
    press(f, MenuKey::Confirm);
    for (int i = 0; i < 60 && !f.exited(); ++i) f.tick(0);
    CHECK(f.exited());
}

// A licence run with a medal: the race enters the record itself and runs the name entry
// as a stand-alone presentation (the original asks right after the run); the result
// posted later records nothing.
TEST(gtmode_menu_disc_license_run_name_entry) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow menus(vfs_reader(vfs), "US");
    menus.start();
    settle(menus);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    s->gt().state().rankings.erase("lic:L0B0001");
    CHECK(game::add_license_record("L0B0001", 35.2) == 0);
    GameFlow f(vfs_reader(vfs), "US");
    CHECK(f.start_presentations());
    settle(f);
    CHECK(f.controller() && f.controller()->page_name() == "name_entry");
    CHECK(f.controller()->set_focus("free_command"));
    press(f, MenuKey::Confirm); // OK
    for (int i = 0; i < 60 && !f.exited(); ++i) f.tick(0);
    CHECK(f.exited());
    CHECK(s->gt().state().rankings["lic:L0B0001"].size() == 1);
    GameFlow empty(vfs_reader(vfs), "US");
    CHECK(!empty.start_presentations() && empty.exited());
}

TEST(gtmode_menu_disc_record_name_entry) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    const std::string path = test::temp_path("record_name.sav").string();
    std::remove(path.c_str());
    set_env("GT3R_GTMODE_SAVE", path.c_str());
    set_env("GT3R_GTMODE_NOSAVE", "0");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    s->gt().state().credits = 1000000;
    CHECK(s->gt().buy_car("r34_gtr_v_99") == 0);
    choose(f, "gt_mode");
    settle(f);
    choose(f, "mt_mt");
    CHECK(f.controller()->page_name() == "mt/mt");
    choose(f, "M400");
    for (int i = 0; i < 80 && !f.waiting_for_game(); ++i) f.tick(0);
    std::optional<GameRequest> r = f.take_request();
    CHECK(r.has_value() && r->race_code == "M400");
    // The race records the run as it ends (gt_mode §17); here its name entry is still queued
    // when the menus resume.
    CHECK(game::add_machine_test_record("M400", 12.345) == 0);
    GameResult res;
    res.mode = "go_machine_test";
    res.race_code = res.event = "M400";
    res.finished = true;
    res.total_time = 12.345;
    res.records_done = true;
    post_game_result(res);
    f.game_finished();
    settle(f);
    // A new 0-400 m record: the name entry page.
    CHECK(f.control() == "name_entry" && f.controller()->page_name() == "name_entry");
    MenuController* c = f.controller();
    // One key cursor over the whole grid (free button rows), the column kept across rows.
    {
        auto highlighted = [&] {
            int n = 0;
            const MenuSet::PageRef ref = c->page();
            const menu::Page& pg = ref.layout();
            for (const menu::ElementDraw& d : f.draw_list())
                for (const menu::Element& it : pg.items)
                    if (it.name.size() <= 5 && !it.alt.empty() && d.element == &it.alt[0]) ++n;
            return n;
        };
        CHECK(c->page_name() == "name_entry");
        CHECK(highlighted() == 1);
        for (int i = 0; i < 3; ++i) press(f, MenuKey::Right);
        press(f, MenuKey::Down);
        CHECK(c->focused() && c->focused()->flow->name == "free_cursor1");
        CHECK(c->choice("free_cursor1") == 3); // A..P col 3 -> T
        CHECK(highlighted() == 1);
        press(f, MenuKey::Up);
        press(f, MenuKey::Up); // wraps to the OK row
        CHECK(c->focused() && c->focused()->flow->name == "free_command");
        press(f, MenuKey::Down); // back to row 0: it keeps its key
        CHECK(c->choice("free_cursor") == 3);
        CHECK(highlighted() == 1);
        c->set_focus("free_cursor");
        c->set_choice("free_cursor", 0);
    }
    auto type = [&](const char* row, int child) {
        CHECK(c->set_focus(row));
        c->set_choice(row, child);
        press(f, MenuKey::Confirm);
    };
    type("free_cursor", 6);  // G
    type("free_cursor1", 3); // T (row Q..Z)
    type("free_cursor", 2);  // C
    f.tick(key_bit(MenuKey::Cancel)); // delete the C
    f.tick(0);
    type("free_cursor1", 1); // R
    const auto& rank = s->gt().state().rankings["mt:M400"];
    CHECK(rank.size() == 1 && rank[0].name == "GTR" && rank[0].value == 12.345);
    CHECK(s->gt().state().player_name == "GTR");
    // L1 / R1 move the text box's cursor: the caret (hidden "cursor") follows it.
    auto caret_x = [&] {
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && d.element->name == "cursor" && d.has_box) return d.box.x;
        return -1.0f;
    };
    const float end_x = caret_x();
    press(f, MenuKey::L1);
    const float mid_x = caret_x();
    press(f, MenuKey::R1);
    CHECK(end_x > 0 && mid_x > 0 && mid_x < end_x && caret_x() == end_x && s->name_box.cursor == 3);
    CHECK(c->set_focus("free_command"));
    press(f, MenuKey::Confirm); // OK
    settle(f);
    CHECK(f.control() == "gt_mode" && f.controller()->page_name() == "mt/mt");
    // The name is saved with the record once the page closes (the result was saved before it).
    std::ifstream in(path);
    std::string line;
    bool record = false, player = false;
    while (std::getline(in, line)) {
        // "rank <key> <value> <car> <day> <name> <stamp>"
        if (line.rfind("rank mt:M400 12.345 ", 0) == 0) {
            const size_t name_at = line.find(" GTR ");
            record = record || (name_at != std::string::npos && std::strtoul(line.c_str() + name_at + 5, nullptr, 10) != 0);
        }
        player = player || line == "player GTR";
    }
    CHECK(record && player);
    std::remove(path.c_str());
    set_env("GT3R_GTMODE_NOSAVE", "1");
}

// Arcade progression through the arcade menus: the pickers offer only the open
// courses / cars, Professional is Hard with L1 + R1, a Single Race win is
// recorded, and Clear Status shows the marks and refuses the endings.
TEST(gtmode_menu_disc_arcade_progression) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    press(f, MenuKey::Confirm); // title: Arcade
    settle(f);
    CHECK(f.control() == "arcade" && f.controller()->page_name() == "ar/top");
    choose(f, "item1"); // Single Race
    CHECK(f.controller()->page_name() == "ar/level");
    // Professional: confirm Hard while L1 and R1 are held.
    const MenuKeys lr = key_bit(MenuKey::L1) | key_bit(MenuKey::R1);
    CHECK(f.controller()->set_focus("item8"));
    auto drawn_now = [&](const char* n) {
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && d.element->name == n) return true;
        return false;
    };
    for (int i = 0; i < 30; ++i) f.tick(0);
    CHECK(!drawn_now("pro_a") && drawn_now("text_hard"));
    f.tick(lr);
    CHECK(drawn_now("pro_a") && drawn_now("text_pro") && !drawn_now("text_hard")); // Hard shows the "pro" art
    f.tick(lr | key_bit(MenuKey::Confirm));
    for (int i = 0; i < 300 && (f.switching() || f.controller()->busy() || f.controller()->page_name() == "ar/level");
         ++i)
        f.tick(lr);
    settle(f);
    CHECK(f.controller()->page_name() == "ar/select_course");
    // New game: the six courses of group 0, in the game's order.
    const std::vector<std::string> group0 = {"super", "midfield", "smtnorth", "smtsouth", "trialmt", "r_midfield"};
    CHECK(f.carousel() && f.carousel()->entries == group0);
    // Pointer: a tap on another icon of the strip moves there, a tap on the focused one chooses it.
    auto icon_centre = [&](int k, float& x, float& y) {
        const std::string name = group0[size_t(k)] + "_mini";
        for (const menu::ElementDraw& d : f.draw_list()) {
            if (!d.element || !d.has_box || d.additive || d.element->name != name) continue;
            x = d.box.x + d.box.w / 2, y = d.box.y + d.box.h / 2;
            return true;
        }
        return false;
    };
    float ix = 0, iy = 0;
    for (int i = 0; i < 40; ++i) f.tick(0); // the strip glides in
    CHECK(icon_centre(2, ix, iy) && f.tap(ix, iy));
    f.tick(0);
    CHECK(f.carousel() && f.carousel()->index == 2 && f.controller()->page_name() == "ar/select_course");
    for (int i = 0; i < 40; ++i) f.tick(0);
    CHECK(icon_centre(0, ix, iy) && f.tap(ix, iy));
    f.tick(0);
    CHECK(f.carousel() && f.carousel()->index == 0);
    for (int i = 0; i < 40; ++i) f.tick(0);
    CHECK(icon_centre(0, ix, iy) && f.tap(ix, iy)); // Super Speedway
    f.tick(0); // (the tap's Confirm is applied on this tick)
    settle(f);
    CHECK(f.controller()->page_name() == "ar/select_class");
    CHECK(f.carousel() && f.carousel()->entries.size() > 1 && f.carousel()->entries[1] == "class_b");
    press(f, MenuKey::Right);
    press(f, MenuKey::Confirm); // class B
    CHECK(f.controller()->page_name() == "ar/select_car");
    const GameFlow::Carousel* cars = f.carousel();
    CHECK(cars && cars->entries.size() == 10);
    if (cars) {
        auto has = [&](const char* n) {
            return std::find(cars->entries.begin(), cars->entries.end(), n) != cars->entries.end();
        };
        CHECK(has("s2000_typev_00") && has("opel_speedster_00") && !has("tt_00") && !has("vauxhall_vx220_00"));
    }
    // A paint, then AUTOMATIC / MANUAL and RACING / DRIFT, then Back twice: the page keeps
    // taking input (it used to stop: the page's own cancel had ended the controller).
    press(f, MenuKey::Down);
    press(f, MenuKey::Confirm);
    CHECK(f.carousel() && f.carousel()->step == 2);
    press(f, MenuKey::Confirm);
    CHECK(f.carousel() && f.carousel()->step == 3);
    press(f, MenuKey::Cancel);
    CHECK(f.carousel() && f.carousel()->step == 2 && f.controller()->page_name() == "ar/select_car");
    press(f, MenuKey::Cancel);
    CHECK(f.carousel() && f.carousel()->step == 1 && f.controller()->page_name() == "ar/select_car");
    {
        int before = f.carousel()->index;
        press(f, MenuKey::Right);
        CHECK(f.carousel() && f.carousel()->index == before + 1); // input still accepted
        press(f, MenuKey::Left);
        CHECK(f.carousel() && f.carousel()->index == before);
    }
    press(f, MenuKey::Up); // back to the first paint
    // OK: AUTOMATIC / MANUAL (choose MANUAL), then RACING / DRIFT, then the race.
    press(f, MenuKey::Confirm);
    CHECK(f.carousel() && f.carousel()->step == 2 && !f.waiting_for_game());
    press(f, MenuKey::Right);
    press(f, MenuKey::Confirm);
    CHECK(f.carousel() && f.carousel()->step == 3);
    press(f, MenuKey::Confirm);
    for (int i = 0; i < 80 && !f.waiting_for_game(); ++i) f.tick(0);
    std::optional<GameRequest> r = f.take_request();
    CHECK(r && r->transmission == 1 && r->tire_mode == 0);
    CHECK(r && r->mode == "single_race" && r->difficulty == 3 && r->race_code == "APBT0001" && r->course == "super");
    GameResult res;
    res.mode = "single_race";
    res.race_code = "APBT0001";
    res.finished = true;
    res.position = 1;
    post_game_result(res);
    f.game_finished();
    settle(f);
    // A win shows the difficulty's cup (Professional: race/a_special_gold.prm), OK after 3 s.
    CHECK(f.controller()->page_name() == "arcade_mini");
    CHECK(game::presentation_model("arcade_mini") == "race/a_special_gold.prm");
    CHECK(skip_race_prizes(f).size() == 1);
    CHECK(f.controller()->page_name() == "ar/top");
    const game::ArcadeProgress& a = s->gt().state().arcade;
    CHECK(a.best[size_t(3 * s->gt().arcade_course_count())] == 0 && s->gt().arcade_course_count() == 34 &&
          a.awards == 0x8000 && a.course_level == 0);
    // Clear Status: a Pro win has no mark; an Easy win shows the Easy mark.
    s->gt().arcade_single_race("AECT0001", "super", 0);
    choose(f, "item5");
    CHECK(f.controller()->page_name() == "ar/goodies");
    auto drawn = [&](const char* n) {
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && d.element->name == n) return true;
        return false;
    };
    CHECK(drawn("super_mini") && drawn("clear_easy") && !drawn("clear_normal") && !drawn("akasaka_mini"));
    // No ending yet: "Goal" shows the refusal.
    choose(f, "Goal");
    CHECK(f.controller()->page_name() == "ar/cannot_ending");
}

// Arcade class page refusals and the time-trial name entry.
TEST(gtmode_menu_disc_arcade_class_and_time_trial) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    CHECK(s->gt().arcade_course_dirt("tahitidirt") && !s->gt().arcade_course_dirt("super"));
    auto drawn = [&](const char* n) {
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && d.element->name == n) return true;
        return false;
    };
    press(f, MenuKey::Confirm); // Arcade
    choose(f, "item1");          // Single Race
    choose(f, "item1");          // Easy
    press(f, MenuKey::Confirm);  // Super Speedway (road)
    CHECK(f.controller()->page_name() == "ar/select_class");
    // The game's class order (Single Race: no guest garage).
    const std::vector<std::string> classes = {"class_c", "class_b", "class_a", "class_s", "class_dirt", "home_garage"};
    CHECK(f.carousel() && f.carousel()->entries == classes);
    CHECK(!drawn("disabled") && drawn("class_c_doc"));
    for (int i = 0; i < 4; ++i) press(f, MenuKey::Right);
    // The dirt class on a road course: refused with its reason.
    CHECK(drawn("disabled") && drawn("disable_dirt_doc") && !drawn("class_dirt_doc"));
    press(f, MenuKey::Confirm);
    CHECK(f.controller()->page_name() == "ar/select_class");
    press(f, MenuKey::Right); // home_garage: the GT garage is empty on a new game
    CHECK(drawn("disable_no_car_doc"));
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    press(f, MenuKey::Cancel);
    CHECK(f.controller()->page_name() == "ar/top");
    // Time trial 1 (gt_mode §17): a run under the record goes in when it ends, from the race
    // screen (add_time_trial_record), and its name entry runs over the race; the result posted
    // on leaving records nothing, so the arcade menu comes straight back.
    auto time_trial = [&] {
        choose(f, "item2");
        CHECK(f.controller()->page_name() == "ar/select_compulsory");
        press(f, MenuKey::Confirm);
        for (int i = 0; i < 80 && !f.waiting_for_game(); ++i) f.tick(0);
        std::optional<GameRequest> r = f.take_request();
        CHECK(r && r->mode == "time_trial" && r->course == "tt01");
    };
    auto leave = [&](bool records_done) {
        GameResult res;
        res.mode = "time_trial";
        res.finished = true;
        res.position = 1;
        res.total_time = res.best_lap = 79.0;
        res.records_done = records_done;
        post_game_result(res);
        f.game_finished();
        settle(f);
        CHECK(f.control() == "arcade" && f.controller()->page_name() == "ar/top");
    };
    time_trial();
    CHECK(game::add_time_trial_record("AT010001", 80.0) == 0);
    {
        GameFlow p(vfs_reader(vfs), "US");
        CHECK(p.start_presentations());
        settle(p);
        CHECK(p.controller() && p.controller()->page_name() == "name_entry");
        MenuController* c = p.controller();
        CHECK(c->set_focus("free_cursor1"));
        c->set_choice("free_cursor1", 9); // "Z" (row Q..Z)
        press(p, MenuKey::Confirm);
        CHECK(c->set_focus("free_command"));
        press(p, MenuKey::Confirm); // OK
        for (int i = 0; i < 60 && !p.exited(); ++i) p.tick(0);
        CHECK(p.exited());
    }
    leave(true);
    const auto& rank = s->gt().state().rankings["at:AT010001"];
    CHECK(rank.size() == 4 && rank[0].value == 80.0 && rank[0].name == "Z" &&
          rank[1].name == game::GtMode::time_trial_seed_name(0, 0));
    CHECK(s->gt().state().arcade.at_best_rank[0] == 0);
    if (!rank.empty()) CHECK(rank[0].car == "motersport_elise_00");
    // A visit the race recorded nothing from (an EXIT before the lap): its result neither.
    time_trial();
    leave(false);
    CHECK(rank.size() == 4 && rank[0].value == 80.0);
}

// A championship win shows the trophy presentation (a random champ_NN.prm, the bonus
// counting up, OK after the timeline), then the prize car.
TEST(gtmode_menu_disc_championship_trophy) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    unset_env("GT3R_GTMODE_PRIZE_FRAME");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    s->gt().state().credits = 1000000;
    CHECK(s->gt().buy_car("altezza_rs200_98") == 0);
    CHECK(s->gt().start_championship("Galtezza_e_0501"));
    for (int i = 0; i < 4; ++i) s->gt().record_round(0);
    choose(f, "gt_mode"); // resumes with the last round
    for (int i = 0; i < 80 && !f.waiting_for_game(); ++i) f.tick(0);
    std::optional<GameRequest> r = f.take_request();
    CHECK(r && r->champ_last);
    s64 before = s->gt().state().credits;
    GameResult res;
    res.mode = "go_race";
    res.race_code = r ? r->race_code : "";
    res.event = r ? r->event : "";
    res.finished = true;
    res.position = 1;
    post_game_result(res);
    f.game_finished();
    settle(f);
    skip_race_prizes(f);
    CHECK(f.controller()->page_name() == "trophy_get");
    float fade = 1;
    std::string model = game::presentation_model("trophy_get", nullptr, &fade);
    CHECK(model.empty() && fade == 0); // fades in from 3 s
    for (int i = 0; i < 300; ++i) f.tick(0);
    model = game::presentation_model("trophy_get", nullptr, &fade);
    CHECK(model.size() == 17 && model.rfind("menu/champ_0", 0) == 0 && fade == 1.0f);
    CHECK(s->gt().state().trophy_flags != 0);
    s64 bonus = s->gt().state().credits - before;
    CHECK(bonus > 0);
    // OK is refused until the count-up is over.
    press(f, MenuKey::Confirm);
    CHECK(f.controller()->page_name() == "trophy_get");
    // The prize counter starts 360 frames in: the bonus box shows what is left of the bonus
    // (its two text widgets, drawn from the forced get_mess layer).
    for (int i = 0; i < 70; ++i) f.tick(0);
    // (`bonus` also holds the last round's prize money, shown before.)
    int boxes = 0;
    for (const menu::ElementDraw& d : f.draw_list())
        if (d.text && d.element && d.element->widget_class && *d.element->widget_class == "get_money_disp") {
            ++boxes;
            std::string digits;
            for (char ch : *d.text)
                if (ch >= '0' && ch <= '9') digits += ch;
            CHECK(d.text->rfind("Prize Cr. ", 0) == 0 && !digits.empty());
            s64 left = digits.empty() ? 0 : std::stoll(digits);
            CHECK(left > 0 && left < bonus);
            CHECK(widget_text(f, "count") == "Cr. " + game::format_credits(s->gt().state().credits - left));
        }
    CHECK(boxes == 2);
    for (int i = 0; i < 700; ++i) f.tick(0);
    CHECK(widget_text(f, "count") == "Cr. " + game::format_credits(s->gt().state().credits));
    CHECK(widget_text(f, "get_money_disp") == "Prize Cr. 0");
    press(f, MenuKey::Confirm);
    settle(f);
    CHECK(f.controller()->page_name() != "trophy_get");
}

// The arcade pickers as the original lays them out: every available entry in the icon
// strip, the course list clamped at its ends (arrows hidden), the class list wrapping, and
// the entry's art sliding with the cross.
TEST(gtmode_menu_disc_arcade_picker_strip) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    auto count = [&](const std::string& suffix) {
        int n = 0;
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && !d.additive && d.element->name.size() > suffix.size() &&
                d.element->name.compare(d.element->name.size() - suffix.size(), suffix.size(), suffix) == 0)
                ++n;
        return n;
    };
    auto drawn = [&](const char* name, float* dx = nullptr) {
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && d.element->name == name) {
                if (dx) *dx = d.dx;
                return true;
            }
        return false;
    };
    press(f, MenuKey::Confirm); // Arcade
    choose(f, "item1");         // Single Race
    choose(f, "item1");         // Easy
    CHECK(f.controller()->page_name() == "ar/select_course");
    for (int i = 0; i < 40; ++i) f.tick(0);
    CHECK(count("_mini") == 6); // the six open courses, all in the strip
    CHECK(!drawn("arrow_l") && drawn("arrow_r"));
    CHECK(drawn("super_logo") && drawn("tarmac") && drawn("area_a") && drawn("length"));
    (void)f.take_sounds();
    press(f, MenuKey::Left); // clamped
    CHECK(f.carousel()->index == 0);
    std::vector<std::string> se = f.take_sounds();
    CHECK(std::find(se.begin(), se.end(), "disable") != se.end());
    f.tick(key_bit(MenuKey::Right));
    f.tick(0);
    // Mid-slide: the new art comes from the right, the old leaves to the left.
    float dx_new = 0, dx_old = 0;
    CHECK(drawn("midfield_logo", &dx_new) && drawn("super_logo", &dx_old));
    CHECK(dx_new > 300 && dx_old < 0);
    for (int i = 0; i < 40; ++i) f.tick(0);
    CHECK(drawn("midfield_logo", &dx_new) && std::fabs(dx_new) < 2 && !drawn("super_logo"));
    for (int i = 0; i < 6; ++i) press(f, MenuKey::Right);
    CHECK(f.carousel()->index == 5 && drawn("arrow_l") && !drawn("arrow_r"));
    // Class list: wraps.
    press(f, MenuKey::Confirm);
    CHECK(f.controller()->page_name() == "ar/select_class");
    int n = int(f.carousel()->entries.size());
    int start = f.carousel()->index;
    press(f, MenuKey::Left);
    CHECK(f.carousel()->index == (start + n - 1) % n);
    CHECK(drawn("arrow_l") && drawn("arrow_r") && count("_mini") == n);
}

// 2 Player Battle: the course on pad 1, then both players choose class, car, AT / MT and
// RACING / DRIFT at once on their own pads; the race starts when both are ready.
TEST(gtmode_menu_disc_arcade_two_player) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    press(f, MenuKey::Confirm); // Arcade
    choose(f, "item4");         // 2 Player Battle
    CHECK(f.controller()->page_name() == "ar/select_course");
    press(f, MenuKey::Confirm); // Super Speedway
    CHECK(f.controller()->page_name() == "ar/select_car_2p");
    for (int i = 0; i < 40; ++i) f.tick(0);
    CHECK(f.two_player(0) && f.two_player(1) && f.two_player(0)->state == 2 && f.two_player(1)->state == 2);
    // Both pads at once: player 1 class A (Right x 2), player 2 class C.
    auto both = [&](MenuKeys k1, MenuKeys k2) {
        f.set_player2_keys(k2);
        f.tick(k1);
        f.set_player2_keys(0);
        f.tick(0);
        f.tick(0);
    };
    const MenuKeys R = key_bit(MenuKey::Right), OK = key_bit(MenuKey::Confirm), D = key_bit(MenuKey::Down);
    both(R, 0);
    both(R, 0);
    CHECK(f.two_player(0)->cls == 2 && f.two_player(1)->cls == 0);
    both(OK, OK);
    CHECK(f.two_player(0)->state == 4 && f.two_player(1)->state == 4);
    CHECK(!f.two_player(0)->cars.empty() && !f.two_player(1)->cars.empty());
    both(R, D); // P1 next car, P2 next paint
    CHECK(f.two_player(0)->car == 1 && f.two_player(1)->paint == 1);
    both(OK, OK); // AT / MT
    both(0, R);   // P2 manual
    both(OK, OK); // RACING / DRIFT
    CHECK(f.two_player(0)->state == 8 && f.two_player(1)->state == 8);
    both(OK, 0);
    CHECK(f.two_player(0)->state == 9 && !f.waiting_for_game());
    both(0, OK);
    for (int i = 0; i < 80 && !f.waiting_for_game(); ++i) f.tick(0);
    std::optional<GameRequest> r = f.take_request();
    CHECK(r && r->mode == "two_player_battle" && r->car_class == "class_a" && r->car2_class == "class_c");
    CHECK(r && !r->car.empty() && !r->car2.empty() && r->transmission == 0 && r->transmission2 == 1 && r->car2_color == 1);
}

// The save page (save_page.h) over a fake library: no slot choice and no "working" /
// "Complete" windows; the operation menu, one confirm for delete / overwrite, the name
// entry for new saves and renames, the export's format window, the file dialog for
// imports, exports and guest garages, rows scrolling past twelve files, the error window.
TEST(save_page_flow) {
    struct Fake {
        std::string id, name;
        bool current = false;
    };
    std::vector<Fake> saves = {{"a", "A", true}, {"b", "B"}};
    std::vector<std::tuple<int, std::string, std::string>> ran; // op, file id, arg
    int fail = 0;
    SavePage::Backend b;
    b.list = [&](SavePage::Mode m) {
        std::vector<SavePage::File> out;
        if (m == SavePage::Mode::LoadGame || m == SavePage::Mode::SaveGame) {
            SavePage::File n;
            n.kind = SavePage::File::Kind::New;
            n.name = "GAME";
            out.push_back(n);
        }
        for (const Fake& f : saves) {
            if (m == SavePage::Mode::LoadGuest && f.current) continue;
            SavePage::File g;
            g.id = f.id;
            g.name = f.name;
            g.current = f.current;
            out.push_back(g);
        }
        return out;
    };
    b.run = [&](SavePage::Mode, SavePage::Op op, const SavePage::File& f, const std::string& arg) {
        ran.emplace_back(int(op), f.id, arg);
        if (fail) return fail;
        if (op == SavePage::OpDelete)
            saves.erase(std::remove_if(saves.begin(), saves.end(), [&](const Fake& x) { return x.id == f.id; }), saves.end());
        if (op == SavePage::OpCopy || op == SavePage::OpImport) saves.insert(saves.begin(), {"c", "C"});
        return 0;
    };
    std::optional<std::string> dialog_answer;
    std::vector<int> dialogs;
    std::string dialog_name;
    FileDialogs d;
    d.show = [&](FileDialogs::Kind k, const std::string& name) {
        dialogs.push_back(int(k));
        dialog_name = name;
    };
    d.poll = [&]() { return std::exchange(dialog_answer, std::nullopt); };
    set_file_dialogs(d);
    std::vector<std::string> snd;
    auto press = [&](SavePage& p, MenuKey k) {
        int r = p.tick(key_bit(k), snd);
        return r ? r : p.tick(0, snd);
    };
    // Load: the cursor starts on the save being played; its menu has no Delete.
    SavePage p;
    p.open(SavePage::Mode::LoadGame, b);
    p.tick(0, snd);
    CHECK(p.files().size() == 3 && p.files(1).size() == 1 && p.cursor() == 1);
    press(p, MenuKey::Confirm);
    CHECK(p.phase() == SavePage::Phase::Operation);
    CHECK((p.ops() == std::vector<SavePage::Op>{SavePage::OpLoad, SavePage::OpRename, SavePage::OpCopy, SavePage::OpExport}));
    press(p, MenuKey::Cancel);
    // The other save: Delete asks once (CANCEL first), OK deletes and the list stays.
    press(p, MenuKey::Right);
    press(p, MenuKey::Confirm);
    CHECK(p.ops().size() == 5 && p.ops()[3] == SavePage::OpDelete);
    for (int i = 0; i < 3; ++i) press(p, MenuKey::Down);
    press(p, MenuKey::Confirm);
    CHECK(p.phase() == SavePage::Phase::Confirm);
    press(p, MenuKey::Confirm); // CANCEL
    CHECK(p.phase() == SavePage::Phase::Operation && ran.empty());
    press(p, MenuKey::Confirm);
    press(p, MenuKey::Left);
    CHECK(press(p, MenuKey::Confirm) == 0 && p.phase() == SavePage::Phase::List && saves.size() == 1);
    CHECK(std::get<0>(ran.back()) == SavePage::OpDelete && std::get<1>(ran.back()) == "b");
    // Rename: the name entry, then done (cut to 16 characters, no confirm).
    press(p, MenuKey::Left);
    press(p, MenuKey::Right); // onto "A"
    press(p, MenuKey::Confirm);
    press(p, MenuKey::Down);
    press(p, MenuKey::Confirm);
    CHECK(p.wants_name() && p.name_start() == "A");
    p.name_done(true, "Championship run 2");
    CHECK(p.phase() == SavePage::Phase::List && std::get<2>(ran.back()) == "Championship run");
    // Import: the file dialog (cancel = nothing), then the file.
    press(p, MenuKey::Right);
    press(p, MenuKey::Right);
    press(p, MenuKey::Right);
    CHECK(p.panel() == 1);
    press(p, MenuKey::Confirm);
    CHECK(p.phase() == SavePage::Phase::Dialog && dialogs.back() == int(FileDialogs::Kind::OpenSave));
    dialog_answer = "";
    p.tick(0, snd);
    CHECK(p.phase() == SavePage::Phase::List);
    press(p, MenuKey::Confirm);
    dialog_answer = "/x/BASCUS-97102GAMEDATA.psu";
    p.tick(0, snd);
    CHECK(std::get<0>(ran.back()) == SavePage::OpImport && std::get<2>(ran.back()) == "/x/BASCUS-97102GAMEDATA.psu");
    CHECK(p.panel() == 0 && p.cursor() == 1 && p.focused()->id == "c"); // the imported save
    // New game: the "New Save" entry asks for a name and ends the page.
    press(p, MenuKey::Left);
    press(p, MenuKey::Confirm);
    CHECK(p.wants_name() && p.name_start() == "GAME");
    p.name_done(true, "SECOND");
    CHECK(p.tick(0, snd) == 1 && std::get<0>(ran.back()) == SavePage::OpNewGame && std::get<2>(ran.back()) == "SECOND");

    // Save: "Save" over the current save at once; over another one after "Overwrite OK?".
    saves = {{"a", "A", true}, {"b", "B"}};
    SavePage q;
    q.open(SavePage::Mode::SaveGame, b);
    q.tick(0, snd);
    press(q, MenuKey::Confirm);
    CHECK(q.ops().front() == SavePage::OpOverwrite);
    CHECK(press(q, MenuKey::Confirm) == 1 && std::get<0>(ran.back()) == SavePage::OpOverwrite && std::get<1>(ran.back()) == "a");
    q.open(SavePage::Mode::SaveGame, b);
    q.tick(0, snd);
    press(q, MenuKey::Right);
    press(q, MenuKey::Confirm);
    press(q, MenuKey::Confirm);
    CHECK(q.phase() == SavePage::Phase::Confirm);
    CHECK(press(q, MenuKey::Confirm) == 1 && std::get<1>(ran.back()) == "b");
    // Export: the format window (.psu first; Cancel back to the operation menu), then the save
    // dialog starting on the file's name with the format's extension (cancelled: back to the
    // formats); the format chosen runs whatever the file is called; then back to the list.
    q.open(SavePage::Mode::Files, b);
    q.tick(0, snd);
    CHECK(q.files().size() == 2 && q.files(1).size() == 1);
    press(q, MenuKey::Confirm);
    for (int i = 0; i < 3; ++i) press(q, MenuKey::Down);
    const size_t dialogs_before = dialogs.size();
    press(q, MenuKey::Confirm);
    CHECK(q.phase() == SavePage::Phase::Format && q.format() == 0 && dialogs.size() == dialogs_before);
    press(q, MenuKey::Cancel);
    CHECK(q.phase() == SavePage::Phase::Operation && q.op() == 2); // (Edit file name, Copy, Export)
    press(q, MenuKey::Confirm);
    press(q, MenuKey::Up); // stays on the first
    for (int i = 0; i < 3; ++i) press(q, MenuKey::Down);
    CHECK(q.phase() == SavePage::Phase::Format && q.format() == SavePage::kExportFormatCount - 1);
    press(q, MenuKey::Confirm);
    CHECK(q.phase() == SavePage::Phase::Dialog && dialogs.back() == int(FileDialogs::Kind::ExportPs2));
    CHECK(dialog_name == "A.raw");
    dialog_answer = "";
    q.tick(0, snd);
    CHECK(q.phase() == SavePage::Phase::Format && q.format() == 2);
    press(q, MenuKey::Up);
    press(q, MenuKey::Confirm);
    CHECK(dialog_name == "A.ps2");
    dialog_answer = "content://docs/document/42";
    q.tick(0, snd);
    CHECK(std::get<0>(ran.back()) == SavePage::OpExportCard && std::get<1>(ran.back()) == "a");
    CHECK(std::get<2>(ran.back()) == "content://docs/document/42");
    CHECK(q.phase() == SavePage::Phase::Done && q.done_text() == "Exported as 42"); // (no FileDialogs::name)
    press(q, MenuKey::Confirm);
    CHECK(q.phase() == SavePage::Phase::List);
    for (const SavePage::ExportFormat& e : SavePage::kExportFormats)
        CHECK(e.op != SavePage::OpExport && e.ext[0] == '.' && std::strlen(e.label) <= 15); // (the button)
    // A failing operation: the error window, OK back to the list.
    fail = SavePage::WriteError;
    press(q, MenuKey::Confirm);
    press(q, MenuKey::Down);
    press(q, MenuKey::Confirm); // Copy
    CHECK(q.phase() == SavePage::Phase::Error);
    CHECK(press(q, MenuKey::Confirm) == 0 && q.phase() == SavePage::Phase::List);
    fail = 0;
    CHECK(press(q, MenuKey::Cancel) == -1);

    // Guest garage: a save loads at once (not the current one), or a file from elsewhere.
    SavePage g;
    g.open(SavePage::Mode::LoadGuest, b, "Trade");
    g.tick(0, snd);
    CHECK(g.files().size() == 1 && g.files()[0].id == "b");
    CHECK(press(g, MenuKey::Confirm) == 1 && std::get<0>(ran.back()) == SavePage::OpLoad && std::get<1>(ran.back()) == "b");
    g.open(SavePage::Mode::LoadGuest, b);
    g.tick(0, snd);
    press(g, MenuKey::Right);
    press(g, MenuKey::Confirm);
    dialog_answer = "/x/card.ps2";
    CHECK(g.tick(0, snd) == 1 && std::get<0>(ran.back()) == SavePage::OpLoad && std::get<2>(ran.back()) == "/x/card.ps2");

    // Many saves: rows scroll (three shown).
    saves.clear();
    for (int i = 0; i < 20; ++i) saves.push_back({std::to_string(i), "S" + std::to_string(i)});
    SavePage m;
    m.open(SavePage::Mode::Files, b);
    m.tick(0, snd);
    for (int i = 0; i < 4; ++i) press(m, MenuKey::Down);
    CHECK(m.cursor() == 16 && m.top_row() == 2);
    press(m, MenuKey::Down);
    CHECK(m.cursor() == 16); // the last row holds 16..19
    for (int i = 0; i < 4; ++i) press(m, MenuKey::Up);
    CHECK(m.cursor() == 0 && m.top_row() == 0);
    // Without file dialogs: no right panel, no Export.
    set_file_dialogs({});
    m.open(SavePage::Mode::Files, b);
    m.tick(0, snd);
    press(m, MenuKey::Confirm);
    CHECK(m.files(1).empty() && m.ops().back() == SavePage::OpDelete);
}

// The save page while a file dialog is open: either key (the waiting window's CANCEL) stops
// waiting, back to the formats (export) or the list (import), and gives the dialog up; its
// answer coming later never runs. A dialog that never answers can be left. An export ends
// on "Exported as" with the file written (a name without an extension gets the format's).
TEST(save_page_dialog_cancel) {
    std::vector<std::tuple<int, std::string, std::string>> ran; // op, file id, arg
    SavePage::Backend b;
    b.list = [](SavePage::Mode) {
        SavePage::File g;
        g.id = "a";
        g.name = "A";
        return std::vector<SavePage::File>{g};
    };
    b.run = [&](SavePage::Mode, SavePage::Op op, const SavePage::File& f, const std::string& arg) {
        ran.emplace_back(int(op), f.id, arg);
        return 0;
    };
    // A fake platform: an answer set by the test waits until polled (or until cancel drops it,
    // as the app's does; `keep_late` leaves it, as a dialog would that cannot be told).
    std::optional<std::string> answer;
    int shown = 0, cancelled = 0;
    bool keep_late = false;
    FileDialogs d;
    d.show = [&](FileDialogs::Kind, const std::string&) { ++shown; };
    d.poll = [&]() { return std::exchange(answer, std::nullopt); };
    d.cancel = [&]() {
        ++cancelled;
        if (!keep_late) answer.reset();
    };
    set_file_dialogs(d);
    std::vector<std::string> snd;
    auto press = [&](SavePage& p, MenuKey k) {
        int r = p.tick(key_bit(k), snd);
        return r ? r : p.tick(0, snd);
    };
    SavePage p;
    p.open(SavePage::Mode::Files, b);
    p.tick(0, snd);
    // Export -> the formats -> the dialog; Cancel goes back to the formats and gives it up.
    press(p, MenuKey::Confirm);
    for (size_t i = 1; i < p.ops().size(); ++i) press(p, MenuKey::Down);
    press(p, MenuKey::Confirm);
    press(p, MenuKey::Down); // PS2 card (.ps2)
    press(p, MenuKey::Confirm);
    CHECK(p.phase() == SavePage::Phase::Dialog && shown == 1);
    for (int i = 0; i < 600; ++i) p.tick(0, snd); // no answer: still waiting
    CHECK(p.phase() == SavePage::Phase::Dialog);
    snd.clear();
    press(p, MenuKey::Cancel);
    CHECK(p.phase() == SavePage::Phase::Format && p.format() == 1 && cancelled == 1 && snd == std::vector<std::string>{"cancel"});
    // A late answer of the dialog given up runs nothing, now or when the next one opens.
    keep_late = true;
    answer = "/x/late.ps2";
    for (int i = 0; i < 10; ++i) p.tick(0, snd);
    CHECK(ran.empty() && p.phase() == SavePage::Phase::Format);
    press(p, MenuKey::Confirm);
    CHECK(p.phase() == SavePage::Phase::Dialog && shown == 2);
    p.tick(0, snd);
    CHECK(ran.empty() && p.phase() == SavePage::Phase::Dialog);
    press(p, MenuKey::Confirm); // Confirm is the window's CANCEL too
    CHECK(p.phase() == SavePage::Phase::Format && cancelled == 2 && ran.empty());
    keep_late = false;
    // Answered: written under the format's extension, then "Exported as" until either key.
    press(p, MenuKey::Confirm);
    answer = "/x/dir.v2/My export";
    p.tick(0, snd);
    CHECK(std::get<0>(ran.back()) == SavePage::OpExportCard && std::get<2>(ran.back()) == "/x/dir.v2/My export");
    CHECK(p.phase() == SavePage::Phase::Done && p.done_text() == "Exported as My export.ps2");
    press(p, MenuKey::Cancel);
    CHECK(p.phase() == SavePage::Phase::List);
    CHECK(SavePage::export_path("/x/a.b/save", SavePage::OpExportRaw) == "/x/a.b/save.raw");
    CHECK(SavePage::export_path("/x/save.bin", SavePage::OpExportPsu) == "/x/save.bin");
    CHECK(SavePage::export_path("content://d/7", SavePage::OpExportPsu) == "content://d/7");
    // Import: a dialog that never answers is left with Cancel (back to the list); a late
    // answer imports nothing.
    ran.clear();
    press(p, MenuKey::Right);
    CHECK(p.panel() == 1);
    press(p, MenuKey::Confirm);
    CHECK(p.phase() == SavePage::Phase::Dialog && shown == 4);
    for (int i = 0; i < 600; ++i) p.tick(0, snd);
    press(p, MenuKey::Cancel);
    CHECK(p.phase() == SavePage::Phase::List && p.panel() == 1 && cancelled == 3);
    keep_late = true;
    answer = "/x/late.sav";
    for (int i = 0; i < 10; ++i) p.tick(0, snd);
    CHECK(ran.empty() && p.phase() == SavePage::Phase::List);
    CHECK(press(p, MenuKey::Cancel) == -1); // and the page can be left
    // Without poll() a dialog cannot be waited for: straight back.
    FileDialogs show_only;
    show_only.show = d.show;
    set_file_dialogs(show_only);
    p.open(SavePage::Mode::Files, b);
    p.tick(0, snd);
    press(p, MenuKey::Right);
    press(p, MenuKey::Confirm);
    p.tick(0, snd);
    CHECK(p.phase() == SavePage::Phase::List);
    set_file_dialogs({});
}

// Settings files on the save page: the car's files (Load), "New Save" first (Save) and the
// other cars' files dimmed; no slots.
TEST(save_page_settings) {
    SavePage::Backend b;
    std::vector<int> ran;
    b.list = [&](SavePage::Mode m) {
        std::vector<SavePage::File> out;
        if (m == SavePage::Mode::SaveSettings) out.push_back({SavePage::File::Kind::New, "", true, false, false, "", {}});
        out.push_back({SavePage::File::Kind::Setting, "1", true, false, false, "grip", {}});
        if (m == SavePage::Mode::SaveSettings) out.push_back({SavePage::File::Kind::Setting, "0", false, false, false, "other", {}});
        return out;
    };
    b.run = [&](SavePage::Mode, SavePage::Op op, const SavePage::File&, const std::string&) {
        ran.push_back(int(op));
        return 0;
    };
    std::vector<std::string> snd;
    auto press = [&](SavePage& p, MenuKey k) {
        int r = p.tick(key_bit(k), snd);
        return r ? r : p.tick(0, snd);
    };
    SavePage p;
    p.open(SavePage::Mode::LoadSettings, b);
    p.tick(0, snd);
    CHECK(p.files().size() == 1 && p.files(1).empty());
    press(p, MenuKey::Confirm);
    CHECK(p.ops().front() == SavePage::OpLoad);
    CHECK(press(p, MenuKey::Confirm) == 1 && ran.back() == SavePage::OpLoad);
    p.open(SavePage::Mode::SaveSettings, b);
    p.tick(0, snd);
    CHECK(p.cursor() == 1); // the first file, not "New Save"
    press(p, MenuKey::Right);
    press(p, MenuKey::Confirm);
    CHECK(p.ops().front() == SavePage::OpRename); // another car's file: no Overwrite
    press(p, MenuKey::Cancel);
    press(p, MenuKey::Left);
    press(p, MenuKey::Left);
    CHECK(press(p, MenuKey::Confirm) == 1 && ran.back() == SavePage::OpSave); // "New Save": at once
}

// Options ([options] in config.ini): values, defaults and the preview widgets.
TEST(options_values_and_previews) {
    IniDocument cfg;
    CHECK(option_value(&cfg, "wide_mode") == "off" && option_value(&cfg, "menu_bgm") == "on");
    CHECK(option_value(&cfg, "split_boost") == "low" && option_laps(&cfg) == 2 && option_handicap(&cfg) == 0);
    // Four-widget choices: the lit art of the current value only.
    CHECK(option_preview_visible(&cfg, "wide_mode_off_on") && !option_preview_visible(&cfg, "wide_mode_off_off"));
    CHECK(!option_preview_visible(&cfg, "wide_mode_on_on") && option_preview_visible(&cfg, "wide_mode_on_off"));
    CHECK(option_preview_visible(&cfg, "assist_type1_on_1p") && !option_preview_visible(&cfg, "assist_type2_on_2p"));
    CHECK(option_preview_visible(&cfg, "split_boost_low_on") && !option_preview_visible(&cfg, "split_boost_none_on"));
    // On/off pairs: the widget of the current state.
    CHECK(option_preview_visible(&cfg, "gear_auto_on") && !option_preview_visible(&cfg, "gear_auto_off"));
    CHECK(option_preview_visible(&cfg, "force_feedback_on_2p") && !option_preview_visible(&cfg, "force_feedback_off_2p"));
    // Other classes are not previews.
    CHECK(option_preview_visible(&cfg, "song_list") && option_preview_visible(&cfg, "slider_split_race_laps"));
    // Changing a value.
    const GameOption* o = find_game_option_by_item("view/view", "multi_wide_mode");
    CHECK(o != nullptr);
    if (o) set_option(cfg, *o, 1);
    CHECK(option_value(&cfg, "wide_mode") == "on" && option_preview_visible(&cfg, "wide_mode_on_on"));
    cfg.set("options", "menu_se", "garbage");
    CHECK(option_value(&cfg, "menu_se") == "on"); // invalid -> default
}

// The save page in the real menus (title File Operation, the disc's art) over a fake library
// and fake file dialogs: Export to PS2 -> a format -> the dialog cancelled, and Import ->
// cancelled; the page still takes the keys afterwards and can be left.
TEST(gtmode_menu_disc_save_page_dialogs) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    const GtModeHooks installed = gt_mode_hooks();
    GtModeHooks h = installed;
    std::vector<std::tuple<int, std::string, std::string>> ran; // op, file id, arg
    h.save_list = [](SavePage::Mode) {
        std::vector<SavePage::File> out;
        for (const char* n : {"A", "B"}) {
            SavePage::File g;
            g.id = n;
            g.name = n;
            out.push_back(g);
        }
        return out;
    };
    h.save_run = [&](SavePage::Mode, SavePage::Op op, const SavePage::File& f, const std::string& arg) {
        ran.emplace_back(int(op), f.id, arg);
        return 0;
    };
    set_gt_mode_hooks(h);
    std::optional<std::string> answer;
    int shown = 0;
    FileDialogs d;
    d.show = [&](FileDialogs::Kind, const std::string&) { ++shown; };
    d.poll = [&]() { return std::exchange(answer, std::nullopt); };
    set_file_dialogs(d);
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    choose(f, "file_operation");
    settle(f);
    const SavePage* p = f.save_page();
    CHECK(p && p->mode() == SavePage::Mode::Files && p->files().size() == 2);
    if (!p) return;
    // Export: Confirm, Down x2 (Edit file name, Copy, Delete, Export), Confirm -> the formats.
    press(f, MenuKey::Confirm);
    CHECK(p->phase() == SavePage::Phase::Operation && p->ops().back() == SavePage::OpExport);
    for (size_t i = 1; i < p->ops().size(); ++i) press(f, MenuKey::Down);
    press(f, MenuKey::Confirm);
    CHECK(p->phase() == SavePage::Phase::Format);
    press(f, MenuKey::Confirm);
    CHECK(p->phase() == SavePage::Phase::Dialog && shown == 1);
    for (int i = 0; i < 30; ++i) f.tick(0);
    answer = "";
    f.tick(0);
    CHECK(p->phase() == SavePage::Phase::Format && ran.empty());
    // The keys still work: Down, Confirm (the dialog again), Cancel back to the list.
    press(f, MenuKey::Down);
    CHECK(p->format() == 1);
    press(f, MenuKey::Confirm);
    CHECK(p->phase() == SavePage::Phase::Dialog && shown == 2);
    answer = "";
    f.tick(0);
    press(f, MenuKey::Cancel);
    CHECK(p->phase() == SavePage::Phase::Operation);
    press(f, MenuKey::Cancel);
    CHECK(p->phase() == SavePage::Phase::List);
    // Import: Right to the right panel, Confirm, the dialog cancelled -> the list, keys work.
    press(f, MenuKey::Right);
    press(f, MenuKey::Right);
    CHECK(p->panel() == 1);
    press(f, MenuKey::Confirm);
    CHECK(p->phase() == SavePage::Phase::Dialog && shown == 3);
    answer = "";
    f.tick(0);
    CHECK(p->phase() == SavePage::Phase::List);
    press(f, MenuKey::Left);
    CHECK(p->panel() == 0);

    // The same by mouse / touch, on the disc's art: icons 64 x 64 at a 70 pitch in the left
    // view (28, 174, 284 x 214); the operation / format window (256 x 140) kept in that view
    // at (28, 248), its buttons (128 x 26) at x 144 from y 352 down by 32; the waiting and
    // "Exported" windows' button at (270, 184, 100 x 26).
    auto tap = [&](float x, float y) {
        const bool used = f.tap(x, y);
        f.tick(0);
        f.tick(0);
        return used;
    };
    auto drawn = [&](const char* text) {
        for (const menu::ElementDraw& e : f.draw_list())
            if (e.text && *e.text == text) return true;
        return false;
    };
    CHECK(tap(66, 350) && p->cursor() == 0 && p->phase() == SavePage::Phase::List);  // A: focused
    CHECK(tap(136, 350) && p->cursor() == 1 && p->phase() == SavePage::Phase::List); // B: focused
    CHECK(tap(136, 350) && p->phase() == SavePage::Phase::Operation);               // B again: its menu
    CHECK(tap(208, 269) && p->op() == 3 && p->phase() == SavePage::Phase::Operation); // Export to PS2
    CHECK(tap(208, 269) && p->phase() == SavePage::Phase::Format);
    CHECK(f.hover(208, 301) && p->format() == 2); // a mouse over Raw save
    CHECK(tap(208, 301) && p->phase() == SavePage::Phase::Dialog && shown == 4);
    CHECK(drawn("Waiting for the file window...") && drawn("CANCEL"));
    CHECK(!tap(500, 100) && p->phase() == SavePage::Phase::Dialog); // (only its button)
    CHECK(tap(320, 197) && p->phase() == SavePage::Phase::Format);  // CANCEL
    CHECK(tap(500, 100) && p->phase() == SavePage::Phase::Operation); // outside the window: back
    CHECK(tap(500, 100) && p->phase() == SavePage::Phase::List);
    // Exported: the window says where; a tap closes it.
    press(f, MenuKey::Confirm);
    tap(208, 269);
    tap(208, 269);
    tap(208, 301);
    CHECK(tap(208, 301) && p->phase() == SavePage::Phase::Dialog && shown == 5);
    answer = "/x/out";
    f.tick(0);
    CHECK(p->phase() == SavePage::Phase::Done && std::get<0>(ran.back()) == SavePage::OpExportRaw);
    CHECK(drawn("Exported as out.raw") && drawn("OK"));
    CHECK(tap(500, 100) && p->phase() == SavePage::Phase::List);
    // The pointer's back (a right click) leaves the page.
    f.back();
    for (int i = 0; i < 120 && f.save_page(); ++i) f.tick(0);
    settle(f);
    CHECK(!f.save_page() && f.control() == "gt_mode");
    set_gt_mode_hooks(installed);
    set_file_dialogs({});
}

// License B end to end through the menus: the license page raises the GameRequest, the
// race's judged result (gold) comes back, GT mode records the medal, writes the save, the
// B page shows the cup; test 8 opens after tests 1-7; all 8 -> the license card.
TEST(gtmode_menu_disc_license_b_progress) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    const std::string path = test::temp_path("license_b.sav").string();
    std::remove(path.c_str());
    set_env("GT3R_GTMODE_SAVE", path.c_str());
    set_env("GT3R_GTMODE_NOSAVE", "0");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    choose(f, "gt_mode");
    settle(f);
    choose(f, "lc_lc");
    settle(f);
    CHECK(f.controller()->page_name() == "lc/lc");
    choose(f, "b");
    settle(f);
    CHECK(f.controller()->page_name() == "lc/lc_b");
    auto run_test = [&](const char* code, int medal, double t) {
        CHECK(f.controller()->set_focus(code));
        press(f, MenuKey::Confirm);
        for (int i = 0; i < 120 && !f.waiting_for_game(); ++i) f.tick(0);
        std::optional<GameRequest> rq = f.take_request();
        CHECK(rq.has_value());
        if (!rq) return;
        CHECK(rq->mode == "go_license" && rq->race_code == code);
        // What the race posts for a judged run (src/app/screen_race.cpp post_result).
        GameResult res;
        res.mode = rq->mode;
        res.race_code = rq->race_code;
        res.event = rq->event;
        res.finished = medal >= 0;
        res.position = medal >= 0 ? 1 : 0;
        res.cars = 1;
        res.total_time = medal >= 0 ? t : -1;
        res.license_medal = medal;
        post_game_result(res);
        f.game_finished();
        settle(f);
        // A new record asks for a name: confirm it (OK) and come back to the page.
        if (f.controller() && f.controller()->page_name() == "name_entry") {
            // The original's controls: Triangle / Square only delete (never leave), Start jumps
            // to the OK row, OK ends the page; OK is drawn inside the "menu" window (on screen).
            press(f, MenuKey::Cancel);
            press(f, MenuKey::Cancel);
            CHECK(f.controller()->page_name() == "name_entry");
            press(f, MenuKey::Start);
            const auto* it = f.controller()->focused();
            CHECK(it && it->flow && it->flow->name == "free_command");
            bool ok_shown = false;
            for (const menu::ElementDraw& d : f.draw_list())
                if (d.element && d.element->text && d.element->text->text == "OK" && d.element->y + d.dy >= 30 &&
                    d.element->y + d.dy + d.element->h <= 448)
                    ok_shown = true;
            CHECK(ok_shown);
            press(f, MenuKey::Confirm);
            settle(f);
        }
    };
    run_test("L0B0001", 0, 34.9);
    std::printf("  after the result: control %s page %s\n", f.control().c_str(),
                f.controller() ? f.controller()->page_name().c_str() : "-");
    const auto& lt = s->gt().state().license_tests;
    CHECK(lt[0].medal == game::Medal::Gold && std::fabs(lt[0].best_time - 34.9) < 1e-6);
    CHECK(f.controller()->page_name() == "lc/lc_b");
    // The B page shows the gold cup for test 1.
    {
        bool cup = false;
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.element && d.element->name == "cup_gold") cup = true;
        CHECK(cup);
    }
    // The save was written with the medal.
    {
        std::optional<game::GameState> saved = game::read_save(path);
        CHECK(saved.has_value());
        if (saved) CHECK(saved->license_tests[0].medal == game::Medal::Gold);
    }
    // Test 8 is refused until tests 1-7 hold a medal.
    CHECK(s->gt().can_take_test("L0B0008") == game::Check::NoLastExam);
    const char* codes[] = {"L0B0002", "L0B0003", "L0B0004", "L0B0005", "L0B0006", "L0B0007"};
    for (const char* c : codes) run_test(c, 2, 60.0);
    CHECK(s->gt().can_take_test("L0B0008") == game::Check::Ok);
    CHECK(!s->gt().license_held(0));
    run_test("L0B0008", 1, 60.0);
    CHECK(s->gt().license_held(0));
    std::remove(path.c_str());
    set_env("GT3R_GTMODE_NOSAVE", "1");
}

// Arcade garage classes: the car page shows the garage car's own picture (its label), and
// both players race their garage cars as tuned (2 Player Battle: player 2's spec, model and
// wheel too). A championship round's "Save game" writes the game with the round recorded.
TEST(gtmode_menu_disc_arcade_garage_cars) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    set_env("GT3R_GTMODE_SAVE", "/nonexistent-gt3r-test/gtmode.sav");
    set_env("GT3R_GTMODE_NOSAVE", "1");
    game::install_gt_mode_menu();
    Vfs vfs(&disc->volume());
    GameFlow f(vfs_reader(vfs), "US");
    f.start();
    settle(f);
    game::GtModeSession* s = game::gt_mode_session();
    CHECK(s != nullptr);
    if (!s) return;
    s->gt().state().credits = 1000000;
    CHECK(s->gt().buy_car("r34_gtr_v_99") == 0);
    CHECK(s->gt().buy_car("altezza_rs200_98") == 0);
    const GtModeHooks& h = gt_mode_hooks();
    CHECK(h.arcade_image && h.arcade_prepare);
    if (!h.arcade_image || !h.arcade_prepare) return;
    const game::GarageCar* g0 = game::arcade_garage_car("hg0");
    const game::GarageCar* g1 = game::arcade_garage_car("hg1");
    CHECK(g0 && g1);
    if (!g0 || !g1) return;
    CHECK(h.arcade_image("hg1") == g1->car);
    CHECK(h.arcade_image("class_c").empty());
    GameRequest r;
    r.mode = "two_player_battle";
    r.car = "hg0";
    r.car2 = "hg1";
    h.arcade_prepare(r);
    CHECK(r.car == g0->car && r.car_parts.has_value());
    CHECK(r.car2 == g1->car && r.car2_parts.has_value() && !r.car2_model.empty() && r.car2_color == g1->color);
    CHECK(!r.racing_gearbox && !r.racing_gearbox2); // stock gearboxes: no whine
    // Player 2's garage car with a non-stock transmission gets the gear whine as player 1's does.
    {
        game::GarageCar& p2 = s->gt().state().garage[1];
        const int gearbox = game::goods_id(17, 3); // full-custom transmission
        CHECK(gearbox >= 0 && s->gt().fit_goods(p2, gearbox));
        GameRequest r2;
        r2.mode = "two_player_battle";
        r2.car = "hg0";
        r2.car2 = "hg1";
        h.arcade_prepare(r2);
        CHECK(!r2.racing_gearbox && r2.racing_gearbox2);
        r2 = GameRequest{};
        r2.mode = "two_player_battle";
        r2.car = "hg1";
        r2.car2 = "hg0";
        h.arcade_prepare(r2);
        CHECK(r2.racing_gearbox && !r2.racing_gearbox2);
        CHECK(s->gt().fit_goods(p2, game::goods_id(17, 0))); // back to stock
    }

    // Save game during a championship: the saved state has the round.
    CHECK(s->gt().start_championship("Galtezza_e_0501"));
    GameResult res;
    res.mode = "go_race";
    res.finished = true;
    res.position = 1;
    CHECK(game::begin_race_game_save(res));
    CHECK(game::snapshot_save() && game::snapshot_save()->championship.next == 1);
    CHECK(s->gt().state().championship.next == 0); // the running game is unchanged
    game::end_race_game_save();
    CHECK(!game::snapshot_save());

    // The save page on its own in Save game mode (the race screen's make_game_save_screen):
    // Cancel ends it, and closing it drops the snapshot.
    CHECK(game::begin_race_game_save(res));
    GameFlow g(vfs_reader(vfs), "US");
    g.start_save_game();
    settle(g);
    CHECK(g.controller() && g.controller()->page_name() == "file_operation" && !g.exited());
    CHECK(g.save_page() && g.save_page()->mode() == SavePage::Mode::SaveGame);
    press(g, MenuKey::Cancel);
    for (int i = 0; i < 60 && !g.exited(); ++i) g.tick(0);
    CHECK(g.exited() && !game::snapshot_save());
    game::end_race_game_save();
}
