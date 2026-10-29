// GT-mode menu bindings: the art of widgets filled by code: image widgets drawn from the page's
// hidden art (license cups, league result marks, license marks, logos, the oil gauge, the gear
// chart), widgets whose own art is not drawn (hide_art), the oil change's and the car wash's
// animations, and the car a 3D car widget shows (car_for_widget, car_wash_spin; drawn by the
// app).

#include "game/gt_mode_menu/internal.h"

#include "game/gt_setting.h"
#include "menu/menu_renderer.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace gt3r::game {

namespace gt_menu {

// Image widgets filled from the page's hidden-layer art (they have no parts of
// their own): license cups, league result marks, required-license marks, car logos.
void widget_draws(const MenuController& ctl, const Element& w, std::vector<menu::ElementDraw>& out) {
    GtModeSession* s = S();
    if (!s || !w.widget_class) return;
    const std::string& cls = *w.widget_class;
    menu::flow::MenuSet::PageRef ref = ctl.page();
    const GtMode& gt = s->gt();
    // The page layer holding widget `w` (the fade-in clock of what it draws).
    auto layer_of = [&](const Element& e) {
        for (size_t li = 0; li < ref.layout().layers.size(); ++li)
            for (const Element& x : ref.layout().layers[li].widgets)
                if (&x == &e) return int(li);
        return -1;
    };
    auto put = [&](const Element* e) {
        if (!e) return;
        menu::ElementDraw d;
        d.scene = ref.scene;
        d.element = e;
        d.has_box = true;
        d.box = {w.x, w.y, w.w, w.h};
        d.z = w.z;
        out.push_back(d);
    };
    // Arcade Clear Status (ar/goodies): per-course logo / name once the course is
    // open, Easy / Normal / Hard clear marks (a harder win counts for the easier
    // marks; no Professional mark) and per-group prize icons.
    if (ctl.page_name() == "ar/goodies") {
        auto num = [&](size_t from) { return std::atoi(cls.c_str() + from) - 1; };
        auto list = gt.data().arcade_courses.find("SINGLE_RACE");
        if (list == gt.data().arcade_courses.end()) return;
        const auto& courses = list->second;
        auto course_at = [&](int i) -> const GtData::ArcadeCourse* {
            return i >= 0 && i < int(courses.size()) ? &courses[size_t(i)] : nullptr;
        };
        if (starts(cls, "course_logo_") || starts(cls, "course_name_")) {
            const GtData::ArcadeCourse* c = course_at(num(12));
            if (c && gt.arcade_course_open(c->course))
                put(hidden_element(ref.layout(), c->course + (starts(cls, "course_logo_") ? "_mini" : "_doc")));
            return;
        }
        static const char* const kDiff[3] = {"easy", "normal", "hard"};
        for (int d = 0; d < 3; ++d) {
            std::string mark = std::string("mark_") + kDiff[d] + "_";
            if (starts(cls, mark.c_str())) {
                if (gt.arcade_won_at_least(num(mark.size()), d))
                    put(hidden_element(ref.layout(), std::string("clear_") + kDiff[d]));
                return;
            }
            std::string prize = std::string("prize_") + kDiff[d] + "_";
            if (starts(cls, prize.c_str()) && cls.size() == prize.size() + 1) {
                if (gt.arcade_group_won(cls.back() - 'a', d))
                    put(hidden_element(ref.layout(), std::string("prize_item_") + kDiff[d]));
                return;
            }
        }
        return;
    }
    if (starts(cls, "disp_license_cup_")) {
        std::string id = cls.substr(17);
        if (id.size() < 2) return;
        int test = id.back() - '1';
        id.pop_back();
        int lic = id == "r" ? 5 : int(license_from_menu_id(id));
        if (lic < 0 || test < 0 || test > 7) return;
        Medal m = gt.state().license_tests[size_t(lic * 8 + test)].medal;
        if (m != Medal::None) put(hidden_element(ref.layout(), std::string("cup_") + medal_name(m)));
        return;
    }
    // Series race pages (gt_mode §22.1): "event_status_buttonNN" belongs
    // to item NN of the page's button list (mbl order, 00 the race information); its own box is
    // not used, nor a widget past the list. It shows the best result under the item's own code
    // (a round, or the championship's "…_0500", place 0 only for a champion) as mark_<place>,
    // then the course logo of a race (the hidden element named after its course; none for the
    // championship, which has no race record), each at its layout position taken as if the
    // button's top-left corner were the page's; half bright on the buttons without the focus.
    if (starts(cls, "event_status_button")) {
        const int n = std::atoi(cls.c_str() + 19);
        SDL_FRect box, clip;
        if (n <= 0 || n >= int(ctl.items().size())) return;
        const MenuController::ItemState& it = ctl.items()[size_t(n)];
        if (!it.flow || !ctl.item_box(n, box, &clip)) return;
        auto put_on_button = [&](const Element* e) {
            if (!e) return;
            menu::ElementDraw d;
            d.scene = ref.scene;
            d.element = e;
            d.has_box = true;
            d.box = {box.x + e->x, box.y + box.h + e->y - menu::kMenuHeight, e->w, e->h};
            d.has_clip = true;
            d.clip = clip;
            d.brightness = n == ctl.focus() ? 1.0f : 0.5f;
            d.alpha = ctl.layer_fade_alpha(layer_of(w), *e);
            d.z = w.z;
            out.push_back(d);
        };
        const int best = gt.state().best_position(it.flow->name);
        if (best >= 0 && best < 6) put_on_button(hidden_element(ref.layout(), "mark_" + std::to_string(best + 1)));
        if (const data::Event* ev = gt.db().event(it.flow->name)) put_on_button(hidden_element(ref.layout(), ev->course));
        return;
    }
    if (cls == "event_status" || cls == "need_license") {
        // League pages (gt_mode §22.1): "event_status7" / "need_license7"
        // belong to the item whose focus layer is "s_7", named after the series' first code
        // ("Gsunday_e_0001", or the championship's own "Gtt_e_0500").
        const std::string layer = "s_" + w.name.substr(12);
        const MblItem* item = nullptr;
        if (ref.flow)
            for (const MblItem& it : ref.flow->items)
                if (it.str80 == layer) item = &it;
        if (!item || item->name.size() < 4) return;
        const std::string& name = item->name;
        // Each element at its own size, centred on (x, the widget's centre), fading in with
        // the focus layer.
        const float cx = w.x + w.w * 0.5f, cy = w.y + w.h * 0.5f;
        auto put_centred = [&](const Element* e, float x, float dz) {
            if (!e) return;
            menu::ElementDraw d;
            d.scene = ref.scene;
            d.element = e;
            d.has_box = true;
            d.box = {x - e->w * 0.5f, cy - e->h * 0.5f, e->w, e->h};
            d.alpha = ctl.layer_fade_alpha(layer_of(w), *e);
            d.z = w.z + dz;
            out.push_back(d);
        };
        if (cls == "need_license") {
            // The required licence of the series' first race (a championship's: its round 1).
            std::string code = name;
            if (code.back() == '0') ++code.back();
            auto r = gt.rules(code);
            if (r && r->required_license > 0) {
                std::string l = license_menu_id(License(r->required_license - 1));
                put_centred(hidden_element(ref.layout(), "lc_mark_" + (l == "d" ? std::string("r") : l)), cx, 0);
            }
            return;
        }
        // The codes from the item's on (its last two digits counting up) while they are result
        // slots; a championship (…_0500 / _0700 / _1000) is its own code, then its rounds.
        const std::string prefix = name.substr(0, name.size() - 2);
        const int first = std::atoi(name.c_str() + name.size() - 2);
        const bool champ = name[name.size() - 4] != '0' || name[name.size() - 3] != '0';
        std::vector<std::string> codes;
        for (int k = first; k < 100; ++k) {
            char two[4];
            std::snprintf(two, sizeof two, "%02d", k);
            if (std::find(gt.data().race_codes.begin(), gt.data().race_codes.end(), prefix + two) ==
                gt.data().race_codes.end())
                break;
            codes.push_back(prefix + two);
        }
        if (codes.empty()) return;
        const Element* frame = hidden_element(ref.layout(), "frame");
        const Element* arrow = hidden_element(ref.layout(), "frame_arrow");
        const float f = frame ? frame->w : 0, a = arrow ? arrow->w : 0, n = float(codes.size());
        auto mark = [&](const std::string& code, float x) {
            int best = gt.state().best_position(code);
            if (best >= 0 && best < 6) put_centred(hidden_element(ref.layout(), "mark_" + std::to_string(best + 1)), x, 0.01f);
        };
        if (!champ) {
            // A series of races: a frame each, f + 8 apart, the row's left edge n (f + 4) / 2
            // left of the centre.
            const float left = cx - n * (f + 4) * 0.5f;
            for (size_t i = 0; i < codes.size(); ++i) {
                const float x = left + f * 0.5f + float(i) * (f + 8);
                put_centred(frame, x, 0);
                mark(codes[i], x);
            }
            return;
        }
        // A championship: its own frame and mark, a frame's width of gap, an arrow frame per
        // round (the mark over its disc, f / 2 in), the last round a plain frame; the row
        // 2f + (n - 1) a wide about the centre.
        const float left = cx - (2 * f + (n - 1) * a) * 0.5f;
        put_centred(frame, left + f * 0.5f, 0);
        mark(codes[0], left + f * 0.5f);
        float x = left + 2 * f;
        for (size_t k = 1; k < codes.size(); ++k) {
            const bool last = k + 1 == codes.size();
            put_centred(last ? frame : arrow, x + (last ? f : a) * 0.5f, 0);
            mark(codes[k], x + f * 0.5f);
            x += a;
        }
        return;
    }
    // Status page: the highest license held, and the rally license.
    if (cls == "license_mark") {
        // lc_mark1..5 are in letter order (A, B, IA, IB, S), as the images show.
        static const int kMark[5] = {2, 1, 4, 3, 5}; // B, A, IB, IA, S
        if (int lic = gt.highest_license(); lic >= 0 && lic < 5)
            put(hidden_element(ref.layout(), "lc_mark" + std::to_string(kMark[lic])));
        return;
    }
    if (cls == "license_mark_rally") {
        if (gt.license_held(5)) put(hidden_element(ref.layout(), "lc_mark6"));
        return;
    }
    // License violation popup: the license the selected race needs.
    if (cls == "violated_license") {
        if (auto r = gt.rules(s->race_code); r && r->required_license > 0) {
            std::string n = r->required_license == 6 ? "DIRT" : license_name(License(r->required_license - 1));
            put(hidden_element(ref.layout(), n));
        }
        return;
    }
    // Tune shop: the maker logo of the ride car ("ASTON MARTIN" -> "astonmartin", "MG MINI" -> "mg_mini").
    if (cls == "tuner_logo") {
        const GarageCar* ride = gt.state().ride_car();
        const data::Car* car = ride ? gt.db().car(ride->car) : nullptr;
        if (!car) return;
        std::string joined, underscored;
        for (char ch : car->maker) {
            char l = char(std::tolower(u8(ch)));
            if (std::isalnum(u8(l))) joined.push_back(l), underscored.push_back(l);
            else if (ch == ' ' || ch == '-') underscored.push_back('_');
        }
        const Element* logo = hidden_element(ref.layout(), joined);
        put(logo ? logo : hidden_element(ref.layout(), underscored));
        return;
    }
    if (cls == "gear_chart") {
        // Gear chart: engine speed (up) against road speed (right) for each gear, a
        // sawtooth from the rpm after each upshift to the rev limit; dotted lines.
        GarageCar* work = setting_work();
        if (!work) return;
        std::optional<sim::CarSpec> spec = gt.car_spec(*work);
        if (!spec || spec->gearbox.ratios.empty()) return;
        const auto& g = spec->gearbox.ratios;
        const float rev = spec->engine.rev_limit_rpm, fd = spec->gearbox.final_drive;
        const float radius = spec->axle[1].tire_radius;
        auto kmh = [&](float rpm, float ratio) { return rpm * 0.10472f * radius / (ratio * fd) * 3.6f; };
        const float vmax = kmh(rev, g.back()) * 1.05f;
        if (vmax <= 0 || rev <= 0) return;
        auto dot = [&](float v, float rpm, SDL_FColor c, float size) {
            menu::ElementDraw d;
            d.kind = menu::ElementDraw::Kind::Fill;
            d.has_box = true;
            float x = w.x + 4 + (w.w - 8) * std::clamp(v / vmax, 0.0f, 1.0f);
            float y = w.y + 4 + (w.h - 8) * std::clamp(rpm / (rev * 1.05f), 0.0f, 1.0f);
            d.box = {x - size / 2, y - size / 2, size, size};
            d.fill = c;
            d.z = w.z;
            out.push_back(d);
        };
        // Rev limit line.
        for (int k = 0; k <= 60; ++k) dot(vmax * float(k) / 60, rev, {0.6f, 0.15f, 0.1f, 0.8f}, 1.2f);
        for (size_t i = 0; i < g.size(); ++i) {
            float r0 = i == 0 ? 0.0f : rev * g[i] / g[i - 1];
            float v0 = kmh(r0, g[i]), v1 = kmh(rev, g[i]);
            for (int k = 0; k <= 40; ++k) {
                float t = float(k) / 40;
                dot(v0 + (v1 - v0) * t, r0 + (rev - r0) * t, {1.0f, 0.68f, 0.0f, 1}, 2.0f);
            }
        }
        return;
    }
    if (cls == "car_logo") {
        std::string car = s->focus_car;
        if (starts(ctl.page_name(), "hm/") && s->garage_focus >= 0 && s->garage_focus < int(gt.state().garage.size()))
            car = gt.state().garage[size_t(s->garage_focus)].car;
        const Element* e = hidden_element(ref.layout(), car);
        if (!e)
            if (const DealerCar* d = gt.data().catalog.dealer_car(car)) e = hidden_element(ref.layout(), d->item);
        put(e);
    }
}

// Widgets whose own art is not drawn (code fills their place).
bool hide_art(const std::string& page, const Element& el) {
    if (!el.widget_class) return false;
    const std::string& c = *el.widget_class;
    // The prize screen's "render_model" marker: its place shows the 3D car instead.
    if ((page == "car_get" || page == "license_get" || page == "trophy_get" || is_mini_prize(page)) &&
        c == "render_model")
        return true;
    // The car renders' own image is the showroom's reflection map (car_select_env), not
    // art to draw: the 3D car is drawn in its place.
    if (starts(c, "car_render_")) return true;
    // The paint column is drawn by code (extra_draws); the widget's own white box is
    // not shown (reference screens).
    if (c == "car_color_box") return true;
    // Arcade Clear Status: the end button / message once a GT league is complete
    // or every arcade course is open.
    if (page == "ar/goodies" && (c == "end_button" || c == "end_message") && S()) {
        const GtMode& gt = S()->gt();
        return !(gt.league_complete("beginner") || gt.league_complete("amateur") || gt.league_complete("pro") ||
                 gt.state().arcade.course_level >= 6);
    }
    // Settings menu: "attach_*" marks the fitted part, "disable_*" covers a locked setting.
    if (starts(c, "attach_") && S())
        if (GarageCar* work = setting_work()) return !setting_attached(S()->gt(), *work, c);
    if (starts(c, "disable_") && S())
        if (GarageCar* real = setting_real()) return !setting_locked(S()->gt(), *real, c);
    return false;
}

// --- The oil page's gauge ("oil_change_cylinder", gt_mode §13.2) ---------------------------------
// A cylinder always full of oil whose colour shows how used the oil is: each corner of the
// page's hidden "clean" colouring blended into its "dirty" one by GtMode::oil_age (fresh oil:
// clean, 300 km on it: dirty). An oil change drains and refills it (oil_cylinder_at); the oil
// takes the clean colour when the new oil is in.

OilCylinder oil_cylinder_at(u32 frames) {
    OilCylinder c;
    int state = 0, counter = 0;
    for (u32 f = 1; f <= frames && !c.done; ++f) {
        switch (state) {
        case 0: counter = 60, state = 1; break;
        case 1:
            if (--counter < 0) state = 2, ++c.sounds;
            break;
        case 2:
            c.level -= 0.01f;
            if (c.level < 0) c.level = 0, counter = 60, state = 3;
            break;
        case 3:
            if (--counter < 0) state = 4, c.clean = true, ++c.sounds;
            break;
        default:
            c.level += 0.01f;
            if (c.level > 1) c.level = 1, c.done = true;
            break;
        }
    }
    return c;
}

namespace {

using PageRef = menu::flow::MenuSet::PageRef;

// The gauge widget on screen and the page it belongs to: on the base page, or on the page a
// popup goes back to (the oil change's progress popup sits over the oil page).
const Element* oil_gauge(const MenuController& ctl, PageRef& owner) {
    PageRef ref = ctl.page();
    if (!ref) return nullptr;
    if (ctl.depth() > 0) {
        if (!ref.flow || ref.flow->back.empty()) return nullptr;
        ref = ctl.set().page(ref.flow->back);
        if (!ref) return nullptr;
    }
    for (const menu::Layer& l : ref.layout().layers)
        if (l.name == "defaultLayer")
            for (const Element& w : l.widgets)
                if (w.widget_class && *w.widget_class == "oil_change_cylinder") {
                    owner = ref;
                    return &w;
                }
    return nullptr;
}

u32 frames_since(const MenuController& ctl, u32 start) { return ctl.frame() >= start ? ctl.frame() - start : 0; }

} // namespace

std::string step_oil_change(MenuController& ctl) {
    OilChange& oc = menu_state().oil_change;
    if (!oc.on) return {};
    PageRef owner;
    if (!oil_gauge(ctl, owner)) { // left the page: nothing more plays
        oc = {};
        return {};
    }
    const OilCylinder c = oil_cylinder_at(frames_since(ctl, oc.start));
    std::string se;
    if (oc.sounds < c.sounds) {
        ++oc.sounds;
        se = "oil";
    }
    if (c.done) {
        oc = {};
        ctl.give_control(); // the progress popup's "Complete" button comes up
    }
    return se;
}

// --- The car wash (gt_mode §13.2) -------------------------------------------------------------
// The car is washed spinning: after a moment the wash's effect plays and the car's showroom
// turntable is pushed round once a frame for 181 frames (car_wash_spin); then the car is shown
// clean and the progress popup gives the controls back.

WashSpin wash_spin_at(u32 frames) {
    WashSpin w;
    int state = 0, counter = 30;
    for (u32 f = 1; f <= frames && !w.done; ++f) {
        if (state == 0) {
            if (--counter < 0) state = 1, counter = 180, w.sound = true;
        } else {
            ++w.pushes;
            if (--counter < 0) w.done = true;
        }
    }
    return w;
}

std::string step_car_wash(MenuController& ctl) {
    CarWash& w = menu_state().wash;
    if (!w.on) return {};
    const WashSpin t = wash_spin_at(frames_since(ctl, w.start));
    w.pushes = t.pushes;
    std::string se;
    if (t.sound && !w.sound) {
        w.sound = true;
        se = "car_wash";
    }
    if (t.done) {
        w.on = false; // the car is shown clean from now on
        ctl.give_control();
    }
    return se;
}

void oil_gauge_draws(const MenuController& ctl, std::vector<menu::ElementDraw>& out) {
    GtModeSession* s = S();
    PageRef owner;
    const Element* w = s ? oil_gauge(ctl, owner) : nullptr;
    const GarageCar* ride = s ? s->gt().state().ride_car() : nullptr;
    if (!w || !ride) return;
    const Element* clean = hidden_element(owner.layout(), "clean");
    const Element* dirty = hidden_element(owner.layout(), "dirty");
    if (!clean || !dirty || !clean->colors || !dirty->colors) return;
    float age = GtMode::oil_age(ride->cond.oil_m), level = 1;
    if (const OilChange& oc = menu_state().oil_change; oc.on) {
        const OilCylinder c = oil_cylinder_at(frames_since(ctl, oc.start));
        level = c.level;
        age = c.clean ? 0 : oc.age;
    }
    menu::ElementDraw d;
    d.scene = owner.scene;
    d.element = dirty;
    d.has_box = true;
    d.box = {dirty->x, dirty->y, dirty->w, dirty->h * level}; // the oil sinks to the bottom (y up)
    d.has_colors = true;
    for (int i = 0; i < 4; ++i) {
        const menu::Rgba &a = (*clean->colors)[i], &b = (*dirty->colors)[i];
        auto mix = [&](u8 x, u8 y) { return (float(x) * (1 - age) + float(y) * age) / 255.0f; };
        d.colors[i] = {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
    }
    // At the widget's depth on the base page: under the cylinder's glass and the page's frame.
    d.z = w->z;
    out.insert(std::find_if(out.begin(), out.end(), [&](const menu::ElementDraw& e) { return e.z > d.z; }), d);
}

} // namespace gt_menu

using namespace gt_menu;

CarWashSpin car_wash_spin() {
    const CarWash& w = menu_state().wash;
    return {w.serial, w.pushes};
}

bool car_for_widget(const std::string& page, const std::string& cls, std::string& car, int& paint,
                    std::string* wheel, float* dirt) {
    const MenuState& ms = menu_state();
    GtModeSession* s = S();
    if (!s) return false;
    if (wheel) wheel->clear();
    if (dirt) *dirt = 0;
    if (cls == "render_model" && page == "car_get" && ms.show) {
        // Roulette: the set's candidates flash by for 2.5 s, then the car won.
        const auto& c = ms.show->candidates;
        if (c.size() > 1 && ms.show_frames < 150) {
            car = c[size_t(ms.show_frames / 6) % c.size()];
            paint = 0;
        } else {
            car = ms.show->car;
            paint = ms.show->paint;
        }
        return true;
    }
    if (!starts(cls, "car_render_")) return false;
    const GameState& st = s->gt().state();
    if (starts(page, "cd/") || cls == "car_render_from" || cls == "car_render_buy") {
        if (s->focus_car.empty()) return false;
        car = s->focus_car;
        paint = s->color_cursor;
        return true;
    }
    const GarageCar* g = nullptr;
    const GameState& lst = s->list_gt().state(); // the guest garage during a trade
    if (starts(page, "hm/") && s->garage_focus >= 0 && s->garage_focus < int(lst.garage.size()))
        g = &lst.garage[size_t(s->garage_focus)];
    if (!g) g = st.ride_car();
    if (!g) return false;
    car = g->car;
    paint = g->color;
    // A car being washed keeps the dirt it came in with until the wash is over.
    const float d = ms.wash.on && g == st.ride_car() ? ms.wash.dirt : float(g->cond.dirt);
    if (dirt) *dirt = std::clamp(d, 0.0f, 0.8f);
    if (wheel) {
        *wheel = g->wheel;
        // Wheel shop: the wheel being looked at goes on the car (a preview).
        if (starts(page, "at/ws/ws_") && cls == "car_render_wheel_shop")
            if (GtModeSession* ss = S(); ss && !ss->focus_wheel_preview.empty()) *wheel = ss->focus_wheel_preview;
        // The garage's wheel list: the entry under the cursor, the car's own wheels for the
        // stock-wheel button.
        if (page == "hm/hm_wheel") *wheel = wheel_list_entry(st, s->wheel_cursor);
    }
    return true;
}

} // namespace gt3r::game
