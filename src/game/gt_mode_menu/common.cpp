// GT-mode menu bindings: the module's state (MenuState, internal.h) and the helpers its files
// share.

#include "game/gt_mode_mods.h"
#include "game/gt_mode_menu/internal.h"

#include "base/log.h"

#include <cstdlib>

namespace gt3r::game {


namespace gt_menu {

MenuState& menu_state() {
    static MenuState state;
    return state;
}

std::string cr(s64 v) { return format_credits(v) + " Cr."; } // log lines (English, not shown in the game)
std::string cr_pre(s64 v) { return credits_text(v); }
std::string cr_num(s64 v) { return format_credits(v); }

std::string car_name(const GtMode& gt, const std::string& label) {
    const data::Car* c = gt.db().car(label);
    if (!c) return label;
    return c->name.empty() ? label : c->name;
}

int goods_of_item(const std::string& item) {
    const ShopItem* s = shop_item(item);
    return s ? s->goods : -1;
}

std::string wheel_of_item(const std::string& item) {
    size_t p = item.rfind('_');
    return p == std::string::npos ? item : item.substr(0, p);
}

std::string ordinal(int place) { // 0-based
    static const char* const kSuffix[] = {"th", "st", "nd", "rd"};
    int n = place + 1, m = n % 100;
    return std::to_string(n) + ((m >= 11 && m <= 13) || n % 10 > 3 ? kSuffix[0] : kSuffix[n % 10]);
}

bool is_mini_prize(const std::string& page) {
    return page == "arcade_mini" || page == "gtmode_mini" || page == "consolation_prize";
}

RankEntry* named_entry() {
    const MenuState& ms = menu_state();
    if (!S() || !ms.show || ms.show->record_key.empty()) return nullptr;
    auto& r = S()->gt().state().rankings;
    auto it = r.find(ms.show->record_key);
    if (it == r.end() || ms.show->record_rank < 0 || ms.show->record_rank >= int(it->second.size())) return nullptr;
    return &it->second[size_t(ms.show->record_rank)];
}

GarageCar* setting_real() { return S() ? S()->gt().state().ride_car() : nullptr; }
GarageCar* setting_work() {
    GtModeSession* s = S();
    GarageCar* real = setting_real();
    if (!s || !real) return nullptr;
    if (!s->setting_work) s->setting_work = *real;
    return &*s->setting_work;
}
bool is_setting_page(const std::string& page) { return starts(page, "set/") || starts(page, "chg/"); }

bool saving_enabled() {
    const char* p = std::getenv("GT3R_GTMODE_NOSAVE");
    return !(p && *p && *p != '0');
}

void autosave(std::vector<std::string>* log) {
    if (!S() || !saving_enabled()) return;
    std::string err;
    if (!S()->save(&err)) {
        LOG_WARN("gt mode: save failed: %s", err.c_str());
        if (log) log->push_back("save failed: " + err);
    }
}

std::vector<std::string> to_log_file(const std::vector<std::string>& lines) {
    for (const std::string& m : lines) LOG_INFO("gt mode: %s", m.c_str());
    return {};
}

const Element* find_widget_named(const menu::Page& p, const std::string& name) {
    for (const menu::Layer& l : p.layers)
        for (const Element& e : l.widgets)
            if (e.name == name) return &e;
    return nullptr;
}

const Element* hidden_element(const menu::Page& p, const std::string& name) {
    for (const menu::Layer& l : p.layers)
        if (l.name == "hidden")
            for (const Element& e : l.elements)
                if (e.name == name) return &e;
    return nullptr;
}

} // namespace gt_menu

// Everything is in MenuState, so a fresh one forgets it all.
void reset_gt_mode_menu_for_tests() { gt_menu::menu_state() = gt_menu::MenuState{}; }
void close_gt_mode() { gt_menu::menu_state() = gt_menu::MenuState{}; }
void reload_gt_mode_data() {
    gt_menu::menu_state().data.reset();
    forget_mod_menu_plan(); // an updated mod can keep its file names
}

} // namespace gt3r::game
