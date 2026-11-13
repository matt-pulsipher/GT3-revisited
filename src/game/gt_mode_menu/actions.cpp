// GT-mode menu bindings: the GT-mode control's actions (the original GT mode menu's actions):
// dealer, garage, trade, tune shop, wheels, car wash and oil, race codes, the settings menu's
// sliders and part changes, the record name entry; the actions that change the game autosave.

#include "game/gt_mode_menu/internal.h"

#include "game/gt_setting.h"

#include <algorithm>

namespace gt3r::game::gt_menu {

bool on_event(const MenuEvent& e, std::vector<std::string>& log) {
    MenuState& ms = menu_state();
    GtModeSession* s = S();
    if (!s) return false;
    GtMode& gt = s->gt();
    using K = MenuEvent::Kind;
    if (e.kind == K::PageChanged) {
        s->last_page = e.page;
        // The trade session: the guest garage's list and car pages.
        if (e.page == "hm/hm_gr_trade") {
            if (!s->trade) s->garage_cursor = 0;
            s->trade = s->guest != nullptr;
        } else if (e.page == "hm/hm" || e.page == "hm/hm_gr" || e.page == "map") {
            if (s->trade) s->garage_cursor = 0, s->garage_focus = -1;
            s->trade = false;
        }
        if (e.page == "00cmn/car_reguration") s->regulated_cursor = 0;
        // The vertical lists are placed at once when their page opens (ListRows).
        if (e.page == "hm/hm_gr" || e.page == "hm/hm_gr_trade" || e.page == "00cmn/car_reguration" ||
            e.page == "records_free_tt")
            ms.list_off = {}, ms.list_pin = {};
        // The garage's wheel list starts on the entry the car wears (the stock-wheel button for a
        // car on its own wheels), else on its first button (gt_mode §7; WheelButtonList).
        if (starts(e.page, "at/ws/ws_")) ms.shop_row = {}; // a brand page's row is placed at once
        if (e.page == "hm/hm_wheel") {
            const GarageCar* car = wheel_page_car(*s);
            s->wheel_cursor = std::max(0, car ? worn_wheel_entry(gt.state(), *car) : 0);
            s->wheel_row_set = false; // (the row placed at once)
            ms.wheel_pin = {};
        }
        if (starts(e.page, "set/adjast_")) {
            // Slider markers: the value each slider had when the page opened.
            s->setting_entry.clear();
            s->setting_committed = false;
            // The gear sliders' ranges are built as the page opens (gt_mode §14).
            if (GarageCar* work = setting_work()) work->range_ref_final = 0;
            if (GarageCar* work = setting_work())
                for (const char* item : {"slider_rate_f", "slider_rate_r", "slider_height_f", "slider_height_r",
                                         "slider_damper_f", "slider_damper_r", "slider_bound_f", "slider_bound_r",
                                         "slider_rebound_f", "slider_rebound_r", "slider_camber_f", "slider_camber_r",
                                         "slider_toe_f", "slider_toe_r", "slider_stabilizer_f", "slider_stabilizer_r",
                                         "slider_brake_f", "slider_brake_r", "slider_lsd_init_f", "slider_lsd_init_r",
                                         "slider_lsd_accl_f", "slider_lsd_accl_r", "slider_lsd_decl_f",
                                         "slider_lsd_decl_r", "slider_gear_ratio_1", "slider_gear_ratio_2",
                                         "slider_gear_ratio_3", "slider_gear_ratio_4", "slider_gear_ratio_5",
                                         "slider_gear_ratio_6", "slider_gear_ratio_7", "slider_gear_ratio_final",
                                         "slider_gear_auto_level", "slider_aero_f", "slider_aero_r",
                                         "slider_elec_aycc", "slider_elec_ascc", "slider_elec_tcsc",
                                         "slider_elec_vcdc"})
                    if (auto r = slider_range(s->gt(), *setting_real(), *work, item)) s->setting_entry[item] = r->value;
        }
        return false;
    }
    if (e.kind == K::Action && e.action == "input_text" && e.page == "name_entry") {
        // Name entry (gt_mode §17.1): the chosen key's character goes in at the text box's cursor
        // (at most 15 characters; the box started with the last name entered).
        if (RankEntry* r = named_entry()) {
            const std::map<std::string, std::string>& chars = gt.data().name_entry_chars(ms.lang);
            auto ch = chars.find(e.item);
            if (s->name_box.insert(ch != chars.end() ? ch->second : std::string())) {
                ms.sounds.push_back("cursor");
                r->name = gt.state().player_name = s->name_box.text;
            }
        }
        return false;
    }
    if (e.kind == K::BackAction && e.action == "retrieve_slider") {
        // Back from a slider page: every uncommitted change is discarded.
        if (GarageCar* real = setting_real()) s->setting_work = *real;
        return false;
    }
    if (e.kind == K::Action && e.action == "commit_slider") {
        if (GarageCar* real = setting_real(); real && s->setting_work) {
            *real = *s->setting_work;
            real->range_ref_final = 0;
            s->setting_committed = true;
            autosave(&log);
        }
        return false;
    }
    if (e.kind == K::Action && setting_goods(e.action) >= 0 && is_setting_page(e.page)) {
        // Part change: fit the owned part at once (no OK needed). GT3's part-change action plays
        // "ok" when the car owns the part and "disable" when it does not; with no car, nothing.
        // It runs for every set_* item but set_brk_racing, which has an action of its own that
        // plays none.
        GarageCar* real = setting_real();
        int goods = setting_goods(e.action);
        const bool sound = e.action != "set_brk_racing";
        if (real && real->owned.test(size_t(setting_owned_goods(e.action)))) {
            gt.fit_goods(*real, goods);
            s->setting_work = *real;
            autosave(&log);
            if (sound) ms.event_sounds.push_back("ok");
        } else if (real && sound) {
            ms.event_sounds.push_back("disable");
        }
        return false;
    }
    if (e.kind == K::Action) {
        const std::string& a = e.action;
        if (a == "set_car_code") {
            const DealerCar* d = gt.data().catalog.dealer_car(e.item);
            s->focus_car = d ? d->car : e.item;
            s->color_cursor = 0; // the paint resets when the focus car changes
        } else if (a == "clear_focus_car") {
            s->focus_car.clear();
        } else if (a == "buy_car") {
            // The purchase's "buy" effect comes a moment later (SoundTimer), whatever the result.
            ms.sound_timers.push_back({kSoundTimerFrames, "buy"});
            int i = gt.buy_car(s->focus_car, s->color_cursor);
            if (i >= 0) {
                log.push_back("bought " + car_name(gt, s->focus_car) + " for " + cr(gt.car_price(s->focus_car)));
                autosave(&log);
            }
        } else if (a == "ride_car") {
            // From the dealer ("ride the new car?"): the new car is at the head of the list.
            int i = e.page == "00cmn/cd_ride_sure" ? 0 : s->garage_focus;
            if (e.item != "no1") gt.ride(i);
        } else if (a == "sell_car") {
            s64 v = gt.sell_car(s->garage_focus);
            if (v > 0) log.push_back("sold for " + cr(v));
            s->garage_cursor = std::min(s->garage_cursor, std::max(0, int(gt.state().garage.size()) - 1));
            s->garage_focus = -1;
            autosave(&log);
        } else if (a == "buy_guest_car") {
            // Trade: a copy of the friend's car (the friend keeps it), for the catalogue price.
            const GameState& gs = s->list_gt().state();
            if (s->trade && s->garage_focus >= 0 && s->garage_focus < int(gs.garage.size())) {
                const GarageCar c = gs.garage[size_t(s->garage_focus)];
                if (gt.buy_guest_car(c) >= 0) {
                    log.push_back("bought " + car_name(gt, c.car) + " for " + cr(gt.car_price(c.car)));
                    autosave(&log);
                }
            }
        } else if (a == "set_garage_car" || a == "set_garage_car_trade") {
            std::vector<int> order = s->list_gt().garage_order(s->sort_key);
            s->garage_focus = s->garage_cursor >= 0 && s->garage_cursor < int(order.size())
                                  ? order[size_t(s->garage_cursor)]
                                  : -1;
        } else if (a == "set_focus_parts") {
            s->focus_goods = goods_of_item(e.item);
        } else if (a == "buy_parts" || a == "buy_fit_parts") {
            // (The Installed page shows the power before and with the part.)
            if (a == "buy_fit_parts") s->part_power_before = gt.ride_power_ps();
            if (gt.buy_part(s->focus_goods)) {
                log.push_back("bought part for " + cr(gt.part_offer(s->focus_goods) ? gt.part_offer(s->focus_goods)->price : 0));
                if (a == "buy_fit_parts") gt.fit_part(s->focus_goods);
            }
        } else if (a == "fit_parts") {
            s->part_power_before = gt.ride_power_ps();
            gt.fit_part(s->focus_goods);
        } else if (a == "buy_all_tires") {
            gt.buy_all_tires();
        } else if (a == "set_focus_wheel_code") {
            s->focus_wheel = wheel_of_item(e.item);
        } else if (a == "buy_wheel") {
            gt.buy_wheel(s->focus_wheel);
        } else if (a == "fit_wheel") {
            ms.sound_timers.push_back({kSoundTimerFrames, "wheel"}); // (as change_wheel's)
            gt.fit_shop_wheel(s->focus_wheel);
        } else if (a == "set_focus_wheel") {
            // The chosen list entry (wheel_cursor) for the focused garage car.
        } else if (a == "change_wheel") {
            // GT3's change_wheel: the car takes the chosen entry's wheel, the
            // placeholder putting it back on its own wheels; its "wheel" effect comes a moment later.
            ms.sound_timers.push_back({kSoundTimerFrames, "wheel"});
            gt.fit_wheel(s->garage_focus, wheel_list_entry(gt.state(), s->wheel_cursor));
        } else if (a == "wash_car") {
            // The car is then washed spinning, its effect coming with the spin (widget_art.cpp).
            const GarageCar* ride = gt.state().ride_car();
            const float dirt = ride ? float(ride->cond.dirt) : 0;
            if (gt.wash()) ms.wash = {true, ms.frame, dirt, false, ms.wash.serial + 1, 0};
        } else if (a == "oil_change") {
            // The page's gauge then drains and refills (its effects come with it; widget_art.cpp).
            const GarageCar* ride = gt.state().ride_car();
            const float age = ride ? GtMode::oil_age(ride->cond.oil_m) : 0;
            if (gt.change_oil()) ms.oil_change = {true, ms.frame, age, 0};
        } else if (a == "set_race_code" || a == "set_race_code_info" || a == "set_race_code_spot") {
            s->race_code = a == "set_race_code_info" && e.item.size() > 5 ? e.item.substr(0, e.item.size() - 5) : e.item;
            s->champ = false;
        } else if (a == "set_race_code_champ") {
            s->race_code = e.item;
            if (!s->race_code.empty()) ++s->race_code.back();
            s->champ = true;
        } else if (a == "clear_race_code") {
            s->race_code.clear();
            s->champ = false;
        } else if (starts(a, "sort_")) {
            static const char* const keys[] = {"date", "name", "maker", "color", "price",
                                               "distance", "power", "drive", "aspiration"};
            for (int k = 0; k < 9; ++k)
                if (a.substr(5) == keys[k]) s->sort_key = k;
            // The list is made again in the new order, on its first car and placed at once
            // (as GT3's garage list rebuilds itself).
            s->garage_cursor = 0;
            ms.list_off = {}, ms.list_pin = {};
        }
        return false;
    }
    // Finish go_save / go_load (settings root): the flow opens the save page over the
    // car's setting files (save_list / save_run below).
    if (e.kind == K::Finish) {
        const std::string& a = e.action;
        if (a == "go_race") {
            gt.enter();
            if (s->champ) gt.start_championship(s->race_code);
        } else if (a == "go_license" || a == "go_machine_test" || a == "go_free_run") {
            gt.enter();
            if (a == "go_free_run") ms.free_run_course = e.item;
        }
        // go_save_in_home / go_save_game_menu / go_load_game_menu / go_trade: the flow
        // opens the save page (save_list / save_run below).
    }
    return false;
}

} // namespace gt3r::game::gt_menu
