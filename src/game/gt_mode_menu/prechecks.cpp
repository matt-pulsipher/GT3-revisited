// GT-mode menu bindings: the pre-check popups (a popup page instead of following the item,
// gt_mode §3.3 / gt_mode §6.2: no money, garage full, no license, regulation...) and the enable rules of
// items (presentations' OK, locked series, settings status boxes, ...).

#include "game/gt_mode_menu/internal.h"

#include "game/gt_setting.h"
#include "game/language_rules.h"
#include "vfs/vfs.h"

#include <cstdlib>

namespace gt3r::game::gt_menu {

namespace {

std::string popup_for(Check c, int license = -1) {
    if (c == Check::NoLicense && license >= 0) return std::string("lc/no_license_") + license_menu_id(License(license));
    if (c == Check::AlreadyHave) return "ts/already_you_have";
    return c == Check::Ok || c == Check::Unknown ? std::string() : std::string(check_name(c));
}

} // namespace

std::string intercept(const std::string& page, const MblItem& it) {
    GtModeSession* s = S();
    if (!s) return {};
    // Trade: buying the guest car is refused for prize-only models, without the money,
    // with a full garage.
    if (page == "hm/hm_car_trade" && it.link == "hm/hm_buy_sure") {
        const GameState& gs = s->list_gt().state();
        if (!s->trade || s->garage_focus < 0 || s->garage_focus >= int(gs.garage.size())) return "00cmn/not_for_buy";
        switch (s->gt().can_buy_guest_car(gs.garage[size_t(s->garage_focus)])) {
        case Check::Ok: return {};
        case Check::NoMoney: return "00cmn/no_money";
        case Check::MaxCar: return "00cmn/max_car";
        default: return "00cmn/not_for_buy";
        }
    }
    if (starts(page, "ar/")) {
        // Clear Status "Goal": the endings need a complete GT league or every arcade
        // course won at one difficulty.
        if (page == "ar/goodies" && it.link == "ar/select_ending" && !s->gt().arcade_ending_open())
            return "ar/cannot_ending";
        return {};
    }
    if (is_setting_page(page) || page == "change_parts") {
        GarageCar* real = setting_real();
        if (!real) return {};
        // Part change: refused when the event's rules would be broken.
        if (int goods = setting_goods(it.action); goods >= 0 && !s->setting_event.empty()) {
            GarageCar copy = *real;
            s->gt().fit_goods(copy, goods);
            if (!s->gt().check_entry(s->setting_event, copy).ok()) return "not_to_change";
        }
        // Tyres cannot be changed for a dirt event.
        if ((it.link == "chg/change_tir_front" || it.link == "chg/change_tir_rear") && !s->setting_event.empty())
            if (auto r = s->gt().rules(s->setting_event); r && r->dirt_tires) return "dirt_tire_only";
        return {};
    }
    GtMode& gt = s->gt();
    const std::string& link = it.link;
    const std::string& act = it.action;
    if (link == "00cmn/cd_buy_sure") return popup_for(gt.can_buy_car(s->focus_car));
    if (link == "hm/hm_sell_sure") return popup_for(gt.can_sell(s->garage_focus));
    if (link == "at/at" || link == "ts/ts" || link == "ts/ts_bs" || link == "ts/ts_dr" || link == "ts/ts_eg" ||
        link == "ts/ts_ot" || link == "ts/ts_tr" || link == "ts/ts_tb" || link == "mt/mt" || link == "hm/hm_fr" ||
        act == "go_machine_test")
        return gt.state().ride_car() ? std::string() : popup_for(Check::NoRideCar);
    if (link == "at/ws/ws") {
        if (!gt.state().ride_car()) return popup_for(Check::NoRideCar);
        return gt.car_flags(gt.state().ride_car()->car) & 4 ? popup_for(Check::NotForChangeWheel) : std::string();
    }
    if (link == "hm/hm_wheel") {
        if (s->garage_focus < 0 || s->garage_focus >= int(gt.state().garage.size())) return {};
        return gt.car_flags(gt.state().garage[size_t(s->garage_focus)].car) & 4 ? popup_for(Check::NotForChangeWheel)
                                                                                 : std::string();
    }
    if (starts(link, "ts/ts_buy_sure_all")) return popup_for(gt.can_buy_all_tires());
    if (starts(link, "ts/ts_buy_sure")) {
        int g = goods_of_item(it.name);
        if (g < 0) g = s->focus_goods;
        return popup_for(gt.can_buy_part(g));
    }
    if (link == "00cmn/ws_buy_sure") {
        Check c = gt.can_buy_wheel(wheel_of_item(it.name));
        return c == Check::AlreadyHave ? std::string("00cmn/already_you_have") : popup_for(c);
    }
    if (act == "go_free_run") {
        if (!gt.state().ride_car()) return popup_for(Check::NoRideCar);
        for (const data::Course& c : gt.db().courses())
            if (c.label == it.name && c.surface == 2 && !gt.state().ride_car()->owned.test(66))
                return popup_for(Check::NeedDirtTire);
        return {};
    }
    if (act == "go_race") return popup_for(gt.can_enter(s->race_code));
    if (act == "go_license") return popup_for(gt.can_take_test(it.name));
    if (starts(link, "lc/lc_")) {
        int lic = int(license_from_menu_id(link.substr(6)));
        return popup_for(gt.can_open_license_page(lic), lic);
    }
    if (act == "oil_change") return popup_for(gt.can_change_oil());
    if (act == "wash_car") return popup_for(gt.can_wash());
    if (act == "check_ride_car" && gt.state().garage.size() > 1 && page == "00cmn/cd_bought")
        return "00cmn/cd_ride_sure";
    (void)page;
    return {};
}

bool listed(const std::string& page, const MblItem& it) {
    if (!starts(page, "cd/")) return true;
    const MenuState& ms = menu_state();
    const Language lang = ms.data ? ms.data->language : ms.reader ? language_of(ms.reader) : Language::US;
    return dealer_item_listed(it.name, lang);
}

bool enabled(const std::string& page, const MblItem& it) {
    const MenuState& ms = menu_state();
    GtModeSession* s = S();
    if (!s) return true;
    // Presentations: OK is accepted once the timeline is over.
    if (page == "license_get") return ms.show_frames >= kLicOk;
    if (page == "trophy_get") return trophy_ok();
    if (page == "car_get") return car_get_ok();
    if (page == "arcade_mini") return ms.show_frames >= kArcadeMiniOk;
    if (page == "gtmode_mini" || page == "consolation_prize") return ms.show && counter_done(*ms.show);
    if (starts(page, "ar/")) {
        // The long ending: a complete amateur / pro league or all courses won at
        // Normal, Hard or Professional.
        if (page == "ar/select_ending" && it.action == "go_ending_normal") return s->gt().arcade_long_ending_open();
        return true;
    }
    if (starts(page, "set/adjast_")) {
        // Status boxes ("status_box_sus3" = the 3rd setting of the page) open only when unlocked.
        GarageCar* real = setting_real();
        size_t d = it.name.find_last_not_of("0123456789");
        if (!real || !starts(it.name, "status_box_") || d + 1 >= it.name.size()) return true;
        return setting_page_item_enabled(s->gt(), *real, page, std::atoi(it.name.c_str() + d + 1) - 1);
    }
    if (starts(page, "chg/")) {
        GarageCar* real = setting_real();
        int goods = setting_owned_goods(it.action);
        return !real || goods < 0 || real->owned.test(size_t(goods));
    }
    if (starts(page, "r/") && starts(it.name, "G")) return s->gt().series_unlocked(it.name);
    return true;
}

} // namespace gt3r::game::gt_menu
