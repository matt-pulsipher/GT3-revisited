// GT-mode menu bindings: the code-filled text widgets, by widget class: settings values, status
// bar, presentations' counters, dealer, garage, tune shop, wheel shop / wash / oil prices,
// licenses, race information, status page, spec pages (docs/dev/GT_MODE.md lists them).

#include "game/gt_mode_menu/internal.h"

#include "game/gt_setting.h"
#include "sim/car_spec.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gt3r::game::gt_menu {

std::optional<std::string> widget_text(const std::string& page, const Element& e) {
    const MenuState& ms = menu_state();
    GtModeSession* s = S();
    if (!s || !e.widget_class) return std::nullopt;
    const std::string& c = *e.widget_class;
    const GtMode& gt = s->gt();
    const GameState& st = gt.state();
    const GarageCar* ride = st.ride_car();
    // The garage pages show the guest garage during a trade.
    const GameState& lst = s->list_gt().state();
    const GarageCar* focus_g =
        s->garage_focus >= 0 && s->garage_focus < int(lst.garage.size()) ? &lst.garage[size_t(s->garage_focus)] : nullptr;
    char buf[96];

    if (is_setting_page(page) || page == "root" || page == "change_parts" || page == "parts_setting") {
        GarageCar* real = setting_real();
        GarageCar* work = setting_work();
        if (real && work) {
            if (auto t = setting_text(gt, *real, *work, c)) return t;
            // "%d %s": the setting car's computed power (parts, condition), truncated.
            if (c == "car_current_power") return gt.power_text(gt.power_ps(*work), " ");
        }
        // The change-parts rows: the part fitted in each slot (the working copy's parts).
        if (starts(c, "parts_")) {
            auto r = s->setting_event.empty() ? std::nullopt : gt.rules(s->setting_event);
            if (auto t = setting_parts_text(gt, real && work ? work : nullptr, c, r && r->dirt_tires)) return t;
        }
    }
    if (c == "text_box" && page == "name_entry") {
        RankEntry* r = named_entry();
        return r ? r->name : std::string();
    }
    // Race prizes and the championship trophy: the place, the prize money and the credits
    // counting up by it. The prize counter's items: the game's "Cr. %s" (the balance) and
    // "%s Cr. %s" with the language's prefix ("Prize Cr. 991", what is left, counting down; it
    // fades out after the count, see remain_alpha).
    if ((is_mini_prize(page) || page == "trophy_get") && ms.show) {
        if (c == "rank_disp") return ms.show->rank;
        const Show::Counter& k = ms.show->counter;
        s64 base = ms.show->credited ? st.credits - ms.show->money : st.credits;
        s64 moved = k.started ? k.cur - base : 0;
        if (c == "get_money_disp") return prize_remain_text(gt.data().prize_prefix, ms.show->money - moved);
        if (c == "count" || c == "count_shadow") return credits_text(base + moved);
    }
    if (c == "user_money") return " " + format_credits(st.credits); // a space after the Cr. icon (reference)
    if (c == "user_day") return std::to_string(st.day);
    // The status bar's day counter: the unit widget's own template filled with the day
    // ("day %d" -> "day 12"; the NTSC-U layout hides "user_day"). A template without "%d" is a
    // label beside the "user_day" number and stays as it is (the Japanese layout's "day" kanji).
    if (c == "user_day_unit") {
        std::string t = e.text ? e.text->text : std::string();
        size_t at = t.find("%d");
        if (at == std::string::npos) return t;
        return t.substr(0, at) + std::to_string(st.day) + t.substr(at + 2);
    }
    if (c == "eventname") return std::string();     // drawn from the hidden reason element (extra_draws)
    if (c == "user_carname" || c == "user_carname_full") return ride ? car_name(gt, ride->car) : std::string();
    if (c == "focused_carname") {
        if (starts(page, "hm/") && focus_g) return car_name(gt, focus_g->car);
        return s->focus_car.empty() ? std::string() : car_name(gt, s->focus_car);
    }
    if (c == "goods_car_price") {
        // Widget "<car>_price[N]" on the dealer pages: "Cr. %s", "----" for a car not
        // for sale (flag bit 0).
        std::string car = e.name.substr(0, e.name.rfind("_price"));
        if (const DealerCar* d = gt.data().catalog.dealer_car(car)) car = d->car;
        if (!gt.db().car(car)) return std::string();
        return gt.car_flags(car) & 1 ? std::string("----") : cr_pre(gt.car_price(car));
    }
    if (c == "car_buy_price") {
        if (starts(page, "hm/") && focus_g) return cr_num(gt.car_price(focus_g->car));
        // The dealer car page: the price, "----" for a car not for sale (flag bit 0).
        // After the Cr. icon one space in (the reference dealer car pages: " 17,000").
        if (s->focus_car.empty()) return std::string();
        return gt.car_flags(s->focus_car) & 1 ? std::string("----") : " " + cr_num(gt.car_price(s->focus_car));
    }
    if (c == "car_sell_price") {
        // (The garage car page insets its texts instead: extra_draws.)
        const std::string pad = starts(page, "hm/hm_car") ? "" : " ";
        return focus_g ? (gt.car_flags(focus_g->car) & 2 ? std::string("----") : pad + cr_num(gt.sell_price(*focus_g)))
                       : std::string();
    }
    if (starts(c, "price_")) {
        std::string item = c.substr(6);
        // After a Cr. icon: " 3,000" (the reference shop pages).
        if (item == "wheel") return " " + cr_num(gt.data().wheel_price());
        if (item == "car_wash") return " " + cr_num(gt.data().car_wash_price());
        if (item == "oil_change") return " " + cr_num(gt.data().oil_change_price());
        int g = goods_of_item(item);
        if (g < 0 || !ride) return std::string("----");
        if (gt.owns(g)) return std::string("Purchased");
        auto o = gt.part_offer(g);
        return o ? " " + cr_num(o->price) : std::string("----");
    }
    if (c == "car_previous_torque" || c == "car_current_torque") {
        // The Installed page (ts_fit_end1 / 3): despite the class names, the power before the
        // part (kept when it was fitted) and now, "%d %s" ("281 HP").
        if (!ride) return std::string();
        return gt.power_text(c == "car_previous_torque" ? s->part_power_before : gt.ride_power_ps(), " ");
    }
    if (c == "disp_ps_befor" || c == "disp_ps_after") {
        if (!ride) return std::string();
        int goods = s->focus_goods;
        const auto& pages = gt.data().catalog.shop_item_of_page;
        if (auto it = pages.find(page); it != pages.end()) goods = goods_of_item(it->second);
        float ps = c == "disp_ps_befor" || goods < 0 ? gt.ride_power_ps() : gt.power_with(goods);
        return gt.power_text(ps); // "%d%s"
    }
    if (c == "focused_partsname") return std::string();
    if (c == "color_name_box" || c == "car_color_name") {
        if (starts(page, "hm/") && focus_g) return gt.data().color_name(gt.data().color_id(focus_g->car, focus_g->color));
        if (s->focus_car.empty()) return std::string();
        // Dealer car pages: the reference screens start the name one space in (as the price).
        return " " + gt.data().color_name(gt.data().color_id(s->focus_car, s->color_cursor));
    }
    if (c == "disp_time_gold" || c == "disp_time_silver" || c == "disp_time_bronze") {
        std::string code = e.name.substr(0, e.name.find('_'));
        auto r = gt.rules(code);
        if (!r) return std::string();
        int m = c == "disp_time_gold" ? 0 : c == "disp_time_silver" ? 1 : 2;
        return format_time_ms(r->license_ms[size_t(m)]);
    }
    if (c == "license_car_name") {
        std::string code = e.name.substr(0, e.name.find('_'));
        auto r = gt.rules(code);
        if (!r || r->entries.empty()) return std::string();
        const data::CarVariant* v = gt.db().variant(r->entries[0]);
        return car_name(gt, v ? v->car : r->entries[0]);
    }
    if (c == "license_launch_speed") {
        std::string code = e.name.substr(0, e.name.find('_'));
        const data::Event* ev = gt.db().event(code);
        if (!ev || ev->bytes.size() < 0x199) return std::string();
        // The start speed (also 0, as "0mph" on the B-1 page); NTSC-U in mph.
        if (gt.data().imperial()) std::snprintf(buf, sizeof buf, "%dmph", int(std::lround(ev->bytes[0x198] * 0.621371)));
        else std::snprintf(buf, sizeof buf, "%dkm/h", ev->bytes[0x198]);
        return std::string(buf);
    }
    if (starts(c, "race_prize_")) {
        // The race information page's prizes: the place's prize, "0" included.
        int p = std::atoi(c.c_str() + 11) - 1;
        auto r = gt.rules(s->race_code);
        if (!r || p < 0 || p > 5) return std::string();
        return cr_num(r->prize[size_t(p)]);
    }
    if (starts(c, "regulation_")) {
        auto r = gt.rules(s->race_code);
        if (!r) return std::string();
        // No rule: "No Limitations" (the original's race information page).
        const std::string none = "No Limitations";
        if (c == "regulation_license")
            return r->required_license ? std::string(license_name(License(r->required_license - 1))) : none;
        if (c == "regulation_dr_type") {
            static const char* const n[] = {"", "FF", "FR", "4WD", "MR", "RR"};
            return r->drivetrain > 0 && r->drivetrain <= 5 ? std::string(n[r->drivetrain]) : none;
        }
        if (c == "regulation_aspiration") return r->aspiration == 1 ? "NA" : r->aspiration == 2 ? "TURBO" : none;
        if (c == "regulation_tire") return r->dirt_tires ? std::string("DIRT") : none;
        if (c == "regulation_tuning")
            return r->car_class == 1 ? "RACE CAR" : r->car_class == 2 ? "TUNED" : r->car_class == 3 ? "NORMAL" : none;
        if (c == "regulation_car") return r->regulation.empty() ? none : std::string("LIMITED");
    }
    if (starts(c, "status_")) {
        int gold = 0, silver = 0, bronze = 0;
        for (const LicenseTestRecord& t : st.license_tests) {
            gold += t.medal == Medal::Gold;
            silver += t.medal == Medal::Silver;
            bronze += t.medal == Medal::Bronze;
        }
        s64 value = 0;
        for (const GarageCar& g : st.garage) value += gt.car_price(g.car);
        if (c == "status_ttl_wins") return std::to_string(st.wins);
        if (c == "status_ttl_races") return std::to_string(st.races);
        if (c == "status_avg_win") {
            // No race yet: "0 %" (the reference status page).
            if (!st.races) return std::string("0 %");
            std::snprintf(buf, sizeof buf, "%.1f %%", 100.0 * st.wins / st.races);
            return std::string(buf);
        }
        if (c == "status_ttl_milage") {
            // NTSC-U: miles (the reference status page shows "0.0 miles").
            if (gt.data().imperial()) std::snprintf(buf, sizeof buf, "%.1f miles", st.distance_m / 1609.344);
            else std::snprintf(buf, sizeof buf, "%.1f km", st.distance_m / 1000.0);
            return std::string(buf);
        }
        if (c == "status_prize_money") return " " + cr_num(st.prize_money); // after the Cr. icon
        if (c == "status_no_trophy") return std::to_string(st.trophies);
        if (c == "status_no_cars") return std::to_string(st.garage.size());
        if (c == "status_ttl_car_price") return " " + cr_num(value);
        if (c == "status_ave_prize_car") return std::to_string(st.prize_cars);
        if (c == "status_no_lc_g") return std::to_string(gold);
        if (c == "status_no_lc_s") return std::to_string(silver);
        if (c == "status_no_lc_b") return std::to_string(bronze);
        if (c == "status_complete") return gt.completion_text();
    }
    // Car pages: the focused garage car at home, else the dealer's focus car.
    const std::string car = starts(page, "hm/") && focus_g ? focus_g->car : s->focus_car;
    if (starts(c, "spec_") || starts(c, "car_")) {
        const data::Car* dc = gt.db().car(car);
        if (!dc) return std::nullopt;
        data::CarDatabase::Summary sum = gt.db().summarize(*dc);
        auto num = [&](const char* fmt, double v) {
            std::snprintf(buf, sizeof buf, fmt, v);
            return std::string(buf);
        };
        // Power. car_max_power and
        // spec_max_power_real: the built car's computed peak "%d%s/%drpm" ("325HP/6800rpm" on
        // NTSC-U): a garage car with its parts and condition, a dealer car stock. spec_max_power:
        // the catalogue figure and rpm text. All truncated after the unit conversion.
        auto peak_text = [&](const PowerPeak& p) {
            return gt.power_text(p.ps) + "/" + std::to_string(p.rpm) + "rpm";
        };
        if ((c == "car_max_power" || c == "spec_max_power_real") && starts(page, "hm/") && focus_g)
            return peak_text(gt.power_peak(*focus_g));
        if (c == "car_max_power" || c == "spec_max_power_real") {
            if (auto spec = sim::build_car_spec(gt.db(), car)) return peak_text(spec_power_peak(*spec));
            return std::string("-");
        }
        if (c == "spec_max_power")
            return gt.power_text(float(sum.power_ps)) + "/" + sum.power_rpm;
        if (c == "spec_max_torque" || c == "spec_max_torque_real") return num("%.1f kgfm", double(sum.torque_kgfm));
        if (c == "spec_displacement" || c == "spec_displacement_real") return sum.displacement;
        if (c == "spec_weight") return num("%.0f kg", sum.weight_kg);
        if (c == "spec_height") return num("%.0f mm", sum.height_mm);
        if (c == "spec_length") return num("%.0f mm", sum.length_mm);
        if (c == "spec_width") return num("%.0f mm", sum.width_mm);
        if (c == "spec_wheel_base") return num("%.0f mm", sum.wheelbase_mm);
        if (c == "spec_drive_type" || c == "car_drive_type") return sum.drive;
        if (c == "spec_engine_type") return sum.layout;
        if (c == "spec_aspiration") return sum.aspiration;
        if (c == "spec_tread_f" || c == "spec_tread_r") {
            const data::Car* dcar = gt.db().car(car);
            auto body = dcar ? gt.db().body(dcar->parts[5]) : std::nullopt;
            if (!body) return std::string("-");
            return num("%.0f mm", c == "spec_tread_f" ? body->front_tread_mm : body->rear_tread_mm);
        }
        if (c == "spec_t_w_ratio" || c == "spec_t_w_ratio_real")
            return sum.torque_kgfm > 0 ? num("%.2f kg/kgfm", double(sum.weight_kg) / sum.torque_kgfm) : std::string("-");
        if (c == "spec_p_w_ratio" || c == "spec_p_w_ratio_real")
            return sum.power_ps ? num("%.2f kg/PS", double(sum.weight_kg) / sum.power_ps) : std::string("-");
        if (c == "car_kilometrage")
            return gt.data().imperial()
                       ? (focus_g ? num("%.1fmiles", focus_g->cond.odometer_m / 1609.344) : std::string("0.0miles"))
                       : (focus_g ? num("%.1f km", focus_g->cond.odometer_m / 1000.0) : std::string("0.0 km"));
    }
    return std::nullopt;
}

} // namespace gt3r::game::gt_menu
