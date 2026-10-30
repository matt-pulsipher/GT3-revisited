// GT-mode menu bindings: the code-driven lists and the per-frame input (on_frame): the GT Mode
// map pointer, the garage list and its sorting, the Cars Permitted list, the wheel shop's tiles
// and the wheel inventory, the dealer's paint picker, the settings sliders; their geometry for
// touch and a hovering pointer.

#include "game/gt_mode_menu/internal.h"

#include "game/gt_setting.h"

#include <algorithm>
#include <cmath>

namespace gt3r::game {

namespace gt_menu {

namespace {

void map_centre(const MenuController& ctl, int i, float& x, float& y) {
    const Element* e = i >= 0 && i < int(ctl.items().size()) ? ctl.items()[size_t(i)].element : nullptr;
    x = e ? e->x + e->w * 0.5f : 320;
    y = e ? e->y + e->h * 0.5f : 224;
}

// GT Mode map: the pointer eases to its building, which takes the focus on arrival.
void ease_map_pointer(MenuController& ctl) {
    MenuState& ms = menu_state();
    if (ctl.page_name() == "map" && ctl.depth() == 0 && ms.map.active && ms.map.target >= 0) {
        // Focus set by other means (taps, returning to the page): the pointer follows it.
        if (!ms.map.moving && ctl.focus() >= 0) ms.map.target = ctl.focus();
        float tx, ty;
        map_centre(ctl, ms.map.target, tx, ty);
        ms.map.x += (tx - ms.map.x) * 0.3f;
        ms.map.y += (ty - ms.map.y) * 0.3f;
        float ex = tx - ms.map.x, ey = ty - ms.map.y;
        if (ex * ex + ey * ey < 144 && ms.map.moving) {
            ms.map.moving = false;
            if (ctl.focus() != ms.map.target && ctl.items()[size_t(ms.map.target)].flow)
                ctl.set_focus(ctl.items()[size_t(ms.map.target)].flow->name);
        }
    } else if (ctl.page_name() != "map") {
        ms.map.active = false;
    }
}

bool list_rows(const MenuController& ctl, const char* window, int n, int cursor, ListRows& g) {
    menu::flow::MenuSet::PageRef ref = ctl.page();
    if (!ref || ctl.items().empty() || !ctl.items()[0].element) return false;
    g.tmpl = ctl.items()[0].element;
    g.win = find_widget_named(ref.layout(), window);
    if (!g.win) return false;
    g.n = n;
    g.cursor = std::clamp(cursor, 0, std::max(0, n - 1));
    g.target = menu::flow::column_layout(std::vector<float>(size_t(std::max(0, n)), g.tmpl->h), float(g.cursor),
                                         g.win->h, g.y, g.total);
    MenuState& ms = menu_state();
    g.off = ms.list_off.page == ctl.page_name() ? ms.list_off.off : g.target;
    if (ms.list_pin.page == ctl.page_name() && ms.list_pin.focus != g.cursor) ms.list_pin = {}; // moved by key
    if (ms.list_pin.page == ctl.page_name()) g.off = ms.list_pin.off;
    return true;
}

} // namespace

bool garage_rows(const MenuController& ctl, GtModeSession& s, ListRows& g) {
    const std::string& page = ctl.page_name();
    if (ctl.depth() != 0 || (page != "hm/hm_gr" && page != "hm/hm_gr_trade")) return false;
    return list_rows(ctl, page == "hm/hm_gr_trade" ? "menu_garage_trade" : "menu_garage",
                     int(s.list_gt().state().garage.size()), s.garage_cursor, g);
}

// The race information's Cars Permitted popup.
bool regulated_rows(const MenuController& ctl, GtModeSession& s, int n, ListRows& g) {
    return ctl.page_name() == "00cmn/car_reguration" && list_rows(ctl, "menu_regulated_car", n, s.regulated_cursor, g);
}

bool records_rows(const MenuController& ctl, GtModeSession& s, int n, ListRows& g) {
    return ctl.page_name() == "records_free_tt" && list_rows(ctl, "menu_records", n, s.records_cursor, g);
}

int records_places(const GtModeSession& s) { return starts(s.records_key, "lic:") ? 6 : 12; }

bool wheel_row(const MenuController& ctl, WheelRow& r) {
    if (!ctl.has_page() || ctl.depth() != 0 || !starts(ctl.page_name(), "at/ws/ws_")) return false;
    r.win = find_widget_named(ctl.page().layout(), "menu_wheel_shop");
    if (!r.win) return false;
    r.n = int(ctl.items().size());
    r.tile = ctl.items().empty() || !ctl.items()[0].element ? 52.0f : ctl.items()[0].element->w;
    r.step = r.tile + menu::flow::kRowGap; // GT3's button list gap (4) after each 52-unit tile: 56
    const int f = std::max(0, ctl.focus());
    std::vector<float> xs;
    float total = 0;
    r.target = menu::flow::row_layout(std::vector<float>(size_t(r.n), r.tile), float(f), r.win->w, xs, total);
    MenuState& ms = menu_state();
    r.off = ms.shop_row.page == ctl.page_name() ? ms.shop_row.off : r.target;
    if (ms.wheel_pin.page == ctl.page_name() && ms.wheel_pin.focus != f) ms.wheel_pin = {}; // moved by key
    if (ms.wheel_pin.page == ctl.page_name()) r.off = ms.wheel_pin.off;
    return true;
}

std::string initial_item(const std::string& page) {
    // GT3 compares each of the brand's wheels with the ride car's wheel (a car on its own wheels
    // never matches: its stock wheel is named after its body model) and starts on the match,
    // else on the first wheel.
    const GtModeSession* s = S();
    const GarageCar* ride = s ? s->gt().state().ride_car() : nullptr;
    if (!starts(page, "at/ws/ws_") || !ride || ride->wheel.empty()) return {};
    return ride->wheel + "_link";
}

std::string wheel_list_entry(const GameState& st, int i) {
    return i >= 1 && i <= int(st.wheels.size()) ? st.wheels[size_t(i - 1)] : std::string();
}

int worn_wheel_entry(const GameState& st, const GarageCar& c) {
    // GT3 compares each entry's name with the car's wheel: the placeholder's empty name matches
    // a car on its own wheels, a bought wheel's name the wheel the car wears.
    if (c.wheel.empty()) return 0;
    auto it = std::find(st.wheels.begin(), st.wheels.end(), c.wheel);
    return it == st.wheels.end() ? -1 : int(it - st.wheels.begin()) + 1;
}

const GarageCar* wheel_page_car(const GtModeSession& s) {
    const GameState& st = s.gt().state();
    return s.garage_focus >= 0 && s.garage_focus < int(st.garage.size()) ? &st.garage[size_t(s.garage_focus)] : nullptr;
}

bool wheel_list(const MenuController& ctl, const GtModeSession& s, WheelList& r) {
    // (Not under the "Equip this wheel?" popup, whose black background covers the page.)
    if (!ctl.has_page() || ctl.depth() != 0 || ctl.page_name() != "hm/hm_wheel" || ctl.items().empty() ||
        !ctl.items()[0].element)
        return false;
    r.win = find_widget_named(ctl.page().layout(), "menu_wheel");
    if (!r.win) return false;
    r.button = ctl.items()[0].element;
    r.n = int(s.gt().state().wheels.size()) + 1;
    const std::vector<float> widths(size_t(r.n), r.button->w);
    r.target = menu::flow::row_layout(widths, float(std::clamp(s.wheel_cursor, 0, r.n - 1)), r.win->w, r.x, r.total);
    r.off = s.wheel_row_set ? s.wheel_row_off : r.target;
    MenuState& ms = menu_state();
    if (ms.wheel_pin.page == ctl.page_name() && ms.wheel_pin.focus != s.wheel_cursor) ms.wheel_pin = {}; // moved by key
    if (ms.wheel_pin.page == ctl.page_name()) r.off = ms.wheel_pin.off;
    return true;
}

namespace {

// The button under a point (inside the window), or -1.
int wheel_button_at(const WheelList& r, float x, float y) {
    if (x < r.win->x || x > r.win->x + r.win->w) return -1;
    for (int i = 0; i < r.n; ++i) {
        SDL_FRect b = r.box(i);
        if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) return i;
    }
    return -1;
}

// The tile under a point (inside the window), or -1.
int wheel_tile_at(const WheelRow& r, float x, float y) {
    if (x < r.win->x || x > r.win->x + r.win->w) return -1;
    for (int i = 0; i < r.n; ++i) {
        SDL_FRect b = r.box(i);
        if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) return i;
    }
    return -1;
}

// The top page's vertical list (the garage or trade list, Cars Permitted, the records) and its
// cursor, or nullptr.
int* page_list(const MenuController& ctl, GtModeSession& s, ListRows& g) {
    if (garage_rows(ctl, s, g)) return &s.garage_cursor;
    if (regulated_rows(ctl, s, int(permitted_car_names(s.gt(), s.race_code).size()), g)) return &s.regulated_cursor;
    if (records_rows(ctl, s, records_places(s), g)) return &s.records_cursor;
    return nullptr;
}

// The list's row under a point (inside the window: the rows are clipped to it), or -1.
int list_row_at(const ListRows& g, float x, float y) {
    if (x < g.win->x || x > g.win->x + g.win->w || y < g.win->y || y > g.win->y + g.win->h) return -1;
    for (int i = 0; i < g.n; ++i) {
        const SDL_FRect b = g.box(i);
        if (y >= b.y && y <= b.y + b.h) return i;
    }
    return -1;
}

} // namespace

void map_input(MenuController& ctl, menu::flow::MenuKeys held) {
    MenuState& ms = menu_state();
    if (!ms.map.active) {
        ms.map = MapPointer{};
        ms.map.active = true;
        ms.map.target = ctl.focus();
    }
    menu::flow::MenuKeys fired = held & ~ms.map.prev;
    ms.map.prev = held;
    float dx = (fired & key_bit(MenuKey::Right)) ? 1.0f : (fired & key_bit(MenuKey::Left)) ? -1.0f : 0.0f;
    float dy = (fired & key_bit(MenuKey::Up)) ? 1.0f : (fired & key_bit(MenuKey::Down)) ? -1.0f : 0.0f;
    if ((dx != 0 || dy != 0) && ms.map.target >= 0) {
        float cx, cy;
        map_centre(ctl, ms.map.target, cx, cy);
        float dl = std::sqrt(dx * dx + dy * dy), best = 0;
        int pick = -1;
        for (int i = 0; i < int(ctl.items().size()); ++i) {
            if (i == ms.map.target || !ctl.items()[size_t(i)].element) continue;
            float bx, by;
            map_centre(ctl, i, bx, by);
            float vx = bx - cx, vy = by - cy, dist = std::sqrt(vx * vx + vy * vy);
            if (dist < 1e-3f) continue;
            float cosv = (vx * dx + vy * dy) / (dist * dl);
            if (cosv < 0) continue;
            float score = cosv * cosv / dist;
            if (score > best) best = score, pick = i;
        }
        if (pick >= 0) {
            ms.map.target = pick;
            ms.map.moving = true;
            ms.sounds.push_back("cursor");
        }
    }
}

namespace {

// The top page's per-frame code: code-driven lists, sliders, presentation timelines. Returns
// the effect it plays on this frame, or "".
std::string page_frame(MenuController& ctl) {
    GtModeSession* s = S();
    if (!s || !ctl.has_page()) return {};
    const std::string& page = ctl.page_name();
    // Code-driven lists live on base pages; the settings sliders and the permitted cars
    // are popups.
    if (ctl.depth() != 0 && !starts(page, "set/adjast_") && page != "00cmn/car_reguration") return {};
    // Presentations step their timelines on every frame (presentations.cpp); the prize
    // counter's beep plays on the frame of its step.
    if (std::string se; presentation_frame(ctl, *s, page, se)) return se;
    // Wheel shop: the focused wheel is previewed on the car; the row eases towards it (held
    // still while a hovering pointer moved the cursor).
    if (starts(page, "at/ws/ws_")) {
        const MenuController::ItemState* it = ctl.focused();
        s->focus_wheel_preview = it && it->flow ? wheel_of_item(it->flow->name) : std::string();
        if (WheelRow r; wheel_row(ctl, r)) {
            MenuState& ms = menu_state();
            if (ms.shop_row.page != page) ms.shop_row = {page, r.target};
            else if (ms.wheel_pin.page != page) ms.shop_row.off = menu::flow::ease_scroll(ms.shop_row.off, r.target);
            else ms.shop_row.off = ms.wheel_pin.off;
        }
    } else if (!s->focus_wheel_preview.empty()) {
        s->focus_wheel_preview.clear();
    }
    menu::flow::MenuKeys f = ctl.fired();
    int d = (f & key_bit(MenuKey::Down)) ? 1 : (f & key_bit(MenuKey::Up)) ? -1 : 0;
    // Settings sliders: Left/Right one step, L1/R1 ten steps, clamped (no wrap); SE "param"
    // on a change, "disable" at either end or when the change is refused.
    if (starts(page, "set/adjast_"))
        if (const MenuController::ItemState* it = ctl.focused(); it && it->flow && starts(it->flow->name, "slider_")) {
            int step = (f & key_bit(MenuKey::Right)) ? 1 : (f & key_bit(MenuKey::Left)) ? -1
                       : (f & key_bit(MenuKey::R1)) ? 10 : (f & key_bit(MenuKey::L1)) ? -10 : 0;
            GarageCar* real = setting_real();
            GarageCar* work = setting_work();
            if (!step || !real || !work) return {};
            std::optional<SliderRange> r = slider_range(s->gt(), *real, *work, it->flow->name);
            if (!r || !slider_enabled(s->gt(), *real, it->flow->name)) return "disable";
            int v = std::clamp(r->value + step, r->lo(), std::max(r->lo(), r->hi()));
            if (r->fixed() || v == r->value) return "disable";
            return slider_set(s->gt(), *real, *work, it->flow->name, v, s->setting_event) ? "param" : "disable";
        }
    // Dealer car page: Up/Down pick the paint (the page's buttons move with Left/Right).
    if (starts(page, "cd/") && page.size() > 4 && page.compare(page.size() - 4, 4, "_car") == 0) {
        int n = s->gt().data().color_count(s->focus_car);
        if (d && n > 1) {
            s->color_cursor = (s->color_cursor + d + n) % n;
            return "cursor";
        }
        return {};
    }
    if (page == "hm/hm_gr" || page == "hm/hm_gr_trade" || page == "00cmn/car_reguration" || page == "records_free_tt") {
        // START opens the garage's sort panel.
        if ((page == "hm/hm_gr" || page == "hm/hm_gr_trade") && (ctl.pressed() & key_bit(MenuKey::Start))) {
            ctl.popup(page == "hm/hm_gr" ? "hm/hm_gr_panel" : "hm/hm_gr_panel_trade");
            return {};
        }
        // Up / Down move the cursor, held at the ends (no wrap); the list then eases towards the
        // cursor row (not while a hovering pointer holds it). The garage lists play "cursor"
        // (GarageButtonList), the records the plain button list's "select".
        std::string se;
        ListRows g;
        if (int* cursor = page_list(ctl, *s, g); cursor && d && g.n > 0) {
            int c = std::clamp(*cursor + d, 0, g.n - 1);
            if (c != *cursor) {
                *cursor = c;
                se = page == "records_free_tt" ? "select" : "cursor";
            }
        }
        if (page_list(ctl, *s, g)) {
            MenuState& ms = menu_state();
            if (ms.list_off.page != page) ms.list_off = {page, g.target};
            else ms.list_off.off = ms.list_pin.page == page ? ms.list_pin.off : menu::flow::ease_scroll(ms.list_off.off, g.target);
        }
        return se;
    } else if (page == "hm/hm_wheel") {
        // The garage's wheel list: Left / Right move along the row, held at the ends (no wrap);
        // the cursor sound only when it moves. The row's scroll eases towards the cursor (not
        // while a hovering pointer holds it).
        int dx = (f & key_bit(MenuKey::Right)) ? 1 : (f & key_bit(MenuKey::Left)) ? -1 : 0;
        const int n = int(s->gt().state().wheels.size()) + 1;
        s->wheel_cursor = std::clamp(s->wheel_cursor, 0, n - 1);
        std::string se;
        if (dx) {
            int c = std::clamp(s->wheel_cursor + dx, 0, n - 1);
            if (c != s->wheel_cursor) {
                s->wheel_cursor = c;
                se = "cursor";
            }
        }
        if (WheelList r; wheel_list(ctl, *s, r)) {
            if (!s->wheel_row_set) s->wheel_row_off = r.target, s->wheel_row_set = true;
            else if (menu_state().wheel_pin.page != page) s->wheel_row_off = menu::flow::ease_scroll(s->wheel_row_off, r.target);
            else s->wheel_row_off = menu_state().wheel_pin.off;
        }
        return se;
    }
    return {};
}

} // namespace

std::string on_tick(MenuController& ctl) {
    MenuState& ms = menu_state();
    ms.frame = ctl.frame();
    std::string se = step_car_wash(ctl);
    if (std::string oil = step_oil_change(ctl); se.empty()) se = std::move(oil);
    return se;
}

std::string on_frame(MenuController& ctl) {
    MenuState& ms = menu_state();
    // The menus step the GT-mode random generator once per frame (gt_mode §20.2).
    if (GtModeSession* rs = S()) rs->gt().tick_random();
    // (Ours: a page that took the controls away with nothing running to give them back, e.g.
    // one opened directly, gives them back once it has settled.)
    // (The prize car page keeps them while its roulette runs: car_get_frame gives them back.)
    if (ctl.ignoring_control() && !ms.wash.on && !ms.oil_change.on && !car_get_holding()) ctl.give_control();
    ease_map_pointer(ctl);
    // The page's code runs on every frame; a queued effect (the map pointer's) plays on the
    // next frame the page plays none.
    std::string se = page_frame(ctl);
    if (se.empty() && !ms.sounds.empty()) {
        se = ms.sounds.front();
        ms.sounds.erase(ms.sounds.begin());
    }
    return se;
}

int garage_hover(MenuController& ctl, float x, float y) {
    if (GtModeSession* s = S())
        if (WheelList r; wheel_list(ctl, *s, r)) {
            int i = wheel_button_at(r, x, y);
            if (i < 0) return 0;
            if (i == s->wheel_cursor) return 1;
            const float off = r.off;
            s->wheel_cursor = i;
            menu_state().wheel_pin = {ctl.page_name(), i, off};
            return 2;
        }
    if (WheelRow r; wheel_row(ctl, r)) {
        int i = wheel_tile_at(r, x, y);
        if (i < 0) return 0;
        if (i == ctl.focus()) return 1;
        const float off = r.off;
        ctl.set_focus(ctl.items()[size_t(i)].flow->name);
        menu_state().wheel_pin = {ctl.page_name(), i, off};
        return 2;
    }
    GtModeSession* s = S();
    ListRows g;
    int* cursor = s ? page_list(ctl, *s, g) : nullptr;
    int i = cursor ? list_row_at(g, x, y) : -1;
    if (i < 0) return 0;
    if (*cursor == i) return 1;
    // (The list holds still while the pointer moves the cursor, so the row stays under it.)
    *cursor = i;
    menu_state().list_pin = {ctl.page_name(), i, g.off};
    return 2;
}

// A tap on a list row moves the cursor there; a tap on the row under the cursor confirms it
// (the garage opens the car, the records their delete panel). The permitted-car rows only move
// the cursor.
int garage_tap(MenuController& ctl, float x, float y) {
    if (GtModeSession* s = S())
        if (WheelList r; wheel_list(ctl, *s, r)) {
            int i = wheel_button_at(r, x, y);
            if (i < 0) return 0;
            if (i == s->wheel_cursor) return 2;
            s->wheel_cursor = i;
            return 1;
        }
    if (WheelRow r; wheel_row(ctl, r)) {
        int i = wheel_tile_at(r, x, y);
        if (i < 0) return 0;
        if (i == ctl.focus()) return 2;
        ctl.set_focus(ctl.items()[size_t(i)].flow->name);
        return 1;
    }
    GtModeSession* s = S();
    ListRows g;
    int* cursor = s ? page_list(ctl, *s, g) : nullptr;
    int i = cursor ? list_row_at(g, x, y) : -1;
    if (i < 0) return 0;
    if (i == *cursor) return cursor == &s->regulated_cursor ? 1 : 2;
    *cursor = i;
    return 1;
}

} // namespace gt_menu

using namespace gt_menu;

std::vector<std::string> permitted_car_names(const GtMode& gt, const std::string& code) {
    std::vector<std::string> all;
    std::optional<EventRules> r = gt.rules(code);
    if (!r || r->regulation.empty()) return all;
    for (const data::Regulation& g : gt.db().regulations()) {
        if (g.label != r->regulation) continue;
        for (const std::string& label : g.cars) {
            std::string n = car_name(gt, label);
            if (const data::Car* c = gt.db().car(label); c && c->name_alt.find_first_not_of(' ') != std::string::npos)
                n += " " + c->name_alt;
            all.push_back(std::move(n));
        }
        break;
    }
    // A car and its variants share a name: each string once, at its last place.
    std::vector<std::string> out;
    for (size_t i = 0; i < all.size(); ++i)
        if (std::find(all.begin() + std::ptrdiff_t(i) + 1, all.end(), all[i]) == all.end()) out.push_back(all[i]);
    return out;
}

} // namespace gt3r::game
