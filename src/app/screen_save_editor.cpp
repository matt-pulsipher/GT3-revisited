// Save editor (Home -> Save editor, gt3r --screen saveeditor): changes a GT Mode save of the
// disc's save library (game/save_library.h). First the library's saves, then the chosen one on
// tabbed pages laid out as Settings: Career (credits, prize money, the day, races and wins, the
// endings seen), Garage (the cars: paint, odometer, engine, oil, dirt, the active car - the
// game's ride car; add any car of the game with the online lobby's car page, or remove one),
// Licenses (each test's medal), Races (each race's best place, championships won) and Arcade
// (course groups and car levels open). The changes (game/save_edit.h) stay in a copy until Save
// (Start, or the top bar's button) writes them; leaving with changes asks first. The save being
// played is edited as the game has it, which can be a race ahead of its file, and the game takes
// the saved changes (game::store_edited_save).
//
// Input as in Settings, without on / off switches (a two-way choice names both sides): Up/Down
// picks a row, Left/Right changes it, L1/R1 or Tab switches page, Triangle/Esc goes back; mouse
// and touch tap and drag. Numbers change by a step with Left/Right, and Cross (or a tap on the
// number) opens a number pad that takes the keyboard's digits too (Backspace deletes, Enter
// confirms).

#include "app/app.h"
#include "app/gt_mode_titles.h"
#include "app/online_pickers.h"
#include "app/screen_registry.h"

#include "formats/text/charset.h"
#include "game/gt_mode_menu.h"
#include "game/save_edit.h"
#include "game/save_library.h"
#include "menu/flow/menu_set.h"
#include "sim/engine_condition.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace gt3r {

namespace {

using game::GameState;
using game::GtMode;
using game::Medal;

enum Tab { kCareer, kGarage, kLicenses, kRaces, kArcade, kTabCount };
const char* const kTabNames[kTabCount] = {"Career", "Garage", "Licenses", "Races", "Arcade"};

const char* const kLicenseTitles[game::kLicenseCount] = {"B License",  "A License", "IB License",
                                                         "IA License", "S License", "R License (rally)"};
// The medal choices, worst first, as Medal values.
const char* const kMedalNames[4] = {"No medal", "Bronze", "Silver", "Gold"};
const Medal kMedals[4] = {Medal::None, Medal::Bronze, Medal::Silver, Medal::Gold};

struct Row {
    enum class Kind { Heading, Info, Choice, Slider, Number, Action };
    Kind kind = Kind::Info;
    std::string label, desc, value;
    float frac = 0;      // Slider 0..1
    bool enabled = true; // false: greyed, nothing happens
    bool accent = false; // Action: its value in the accent colour (the active car)
    bool chip = false;   // Choice: a paint chip of `rgb` before the value
    u32 rgb = 0;
    std::function<void(int)> change;     // Left/Right
    std::function<void(float)> set_frac; // slider pointer position 0..1
    std::function<void()> activate;      // Cross / tap

    bool selectable() const { return kind != Kind::Heading && kind != Kind::Info; }
};

std::string credits(s64 v) { return game::format_credits(v) + " Cr."; }

std::string ordinal(int place) { // 0-based
    const int n = place + 1;
    const char* suf = n % 100 >= 11 && n % 100 <= 13 ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    return std::to_string(n) + suf;
}

class SaveEditorScreen : public Screen {
public:
    void on_event(App& app, const SDL_Event& e) override {
        if (picking_) {
            picker_.event(e);
            return;
        }
        if (pad_.active) {
            pad_event(app, e);
            return;
        }
        if (confirm_.active) {
            if (gesture_.feed(e) == Gesture::Result::Tap)
                for (size_t i = 0; i < confirm_.rects.size(); ++i)
                    if (confirm_.rects[i].contains(gesture_.x(), gesture_.y())) answer_confirm(int(i));
            return;
        }
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_TAB && !e.key.repeat && editing()) {
            switch_tab((e.key.mod & SDL_KMOD_SHIFT) ? -1 : 1);
            return;
        }
        if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            scroll_ -= e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f) * lay_.row_h;
            follow_sel_ = false;
            return;
        }
        switch (gesture_.feed(e)) {
        case Gesture::Result::Down: {
            const float x = gesture_.x(), y = gesture_.y();
            press_ = Press{};
            if (lay_.back.contains(x, y)) press_.button = kBackButton;
            else if (editing() && lay_.save.contains(x, y)) press_.button = kSaveButton;
            else if (int t = tab_at(x, y); t >= 0) press_.tab = t;
            else if (lay_.list.contains(x, y)) {
                press_.in_list = true;
                press_.scroll = scroll_;
                int i = row_at(y);
                if (i >= 0 && rows_[size_t(i)].selectable()) {
                    press_.row = i;
                    Row& r = rows_[size_t(i)];
                    if (r.kind == Row::Kind::Slider && r.enabled && r.set_frac && x >= lay_.val_x - 12) {
                        sel_ = i;
                        focus_tabs_ = false;
                        dragging_ = i;
                        r.set_frac((x - lay_.val_x) / lay_.slider_w);
                    }
                }
            }
            break;
        }
        case Gesture::Result::DragStart:
        case Gesture::Result::Drag:
            if (dragging_ >= 0 && dragging_ < int(rows_.size()) && rows_[size_t(dragging_)].set_frac)
                rows_[size_t(dragging_)].set_frac((gesture_.x() - lay_.val_x) / lay_.slider_w);
            else if (press_.in_list) {
                scroll_ = press_.scroll - (gesture_.y() - gesture_.start_y());
                follow_sel_ = false;
                press_.row = -1;
            } else {
                press_ = Press{};
            }
            break;
        case Gesture::Result::Tap: {
            Press p = press_;
            press_ = Press{};
            const bool was_slider = dragging_ >= 0;
            dragging_ = -1;
            const float x = gesture_.x(), y = gesture_.y();
            if (p.button == kBackButton && lay_.back.contains(x, y)) {
                back(app);
                return;
            }
            if (p.button == kSaveButton && lay_.save.contains(x, y)) {
                save();
                return;
            }
            if (p.tab >= 0) {
                if (p.tab != tab_) switch_tab(p.tab - tab_);
                focus_tabs_ = false;
                return;
            }
            if (p.row < 0 || p.row >= int(rows_.size()) || was_slider) return;
            sel_ = p.row;
            focus_tabs_ = false;
            Row& r = rows_[size_t(p.row)];
            if (!r.enabled) return;
            const float sx = gesture_.start_x();
            const bool on_value = sx >= lay_.val_x - 12;
            const float arrow = ui_touch_ ? 26.0f : 14.0f; // a Number's -/+ ends
            switch (r.kind) {
            case Row::Kind::Slider:
                if (on_value && r.set_frac) r.set_frac((sx - lay_.val_x) / lay_.slider_w);
                break;
            case Row::Kind::Choice:
                if (on_value && r.change) r.change(sx < lay_.val_x + lay_.val_w / 2 ? -1 : 1);
                break;
            case Row::Kind::Number:
                if (on_value && r.change && sx < lay_.val_x + arrow) r.change(-1);
                else if (on_value && r.change && sx >= lay_.val_x + lay_.val_w - arrow) r.change(1);
                else if (r.activate) r.activate();
                break;
            case Row::Kind::Action:
                if (r.activate) r.activate();
                break;
            default: break;
            }
            break;
        }
        case Gesture::Result::Up:
            press_ = Press{};
            dragging_ = -1;
            break;
        case Gesture::Result::None: break;
        }
    }

    void update(App& app, double dt) override {
        if (!init_) init(app);
        note_left_ -= dt;
        if (picking_) {
            update_picker(app);
            return;
        }
        build(app);
        const MenuInput& m = app.menu();
        if (pad_.active) {
            update_pad(m);
            return;
        }
        if (confirm_.active) {
            if (int h = m.horizontal()) confirm_.focus = std::clamp(confirm_.focus + h, 0, int(confirm_.buttons.size()) - 1);
            if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) answer_confirm(0); // the first: Cancel
            else if (m.pressed(PadButton::Cross)) answer_confirm(confirm_.focus);
            return;
        }
        if (m.pressed(PadButton::Triangle)) {
            back(app);
            return;
        }
        if (editing()) {
            if (m.pressed(PadButton::Start)) save();
            if (m.pressed(PadButton::L1)) switch_tab(-1);
            if (m.pressed(PadButton::R1)) switch_tab(1);
            if (focus_tabs_) {
                if (int h = m.horizontal()) switch_tab(h);
                if (m.vertical() > 0 || m.pressed(PadButton::Cross)) {
                    focus_tabs_ = false;
                    sel_ = first_selectable();
                    follow_sel_ = true;
                }
                return;
            }
        }
        if (int v = m.vertical()) {
            move(v);
            follow_sel_ = true;
        }
        if (sel_ < 0 || sel_ >= int(rows_.size())) return;
        Row& r = rows_[size_t(sel_)];
        if (!r.enabled) return;
        if (int h = m.horizontal(); h && r.change) r.change(h);
        if (m.pressed(PadButton::Cross) && r.activate) r.activate();
    }

    void render(App& app, Ui& ui) override {
        if (!init_) init(app); // (a screen pushed from another's update renders first)
        if (picking_) {
            ui.clear();
            picker_.render(app, ui);
            return;
        }
        build(app);
        ui_touch_ = ui.touch();
        compute_layout(ui);
        const UiRect& S = ui.safe();
        const bool touch = ui.touch();

        // Top bar: back, title (the save's name when editing), Save.
        ui.fill(0, 0, ui.width(), lay_.bar_bottom, colors::kPanel);
        ui.button(lay_.back, "< Back", false, press_.button == kBackButton && gesture_.active());
        const float ty = std::floor(lay_.back.y + (lay_.back.h - Ui::kChar) / 2);
        float tx = lay_.back.right() + 12;
        ui.text(tx, ty, "SAVE EDITOR", colors::kAccent);
        tx += Ui::text_width("SAVE EDITOR") + 12;
        if (editing()) {
            ui.button(lay_.save, dirty_ ? "Save" : "Saved", false, press_.button == kSaveButton && gesture_.active(), dirty_);
            const float room = lay_.save.x - 12 - tx;
            std::string title = name_ + (dirty_ ? "  (unsaved changes)" : "");
            ui.text(tx, ty, Ui::elide(title, room), dirty_ ? colors::kText : colors::kDim);
            for (int t = 0; t < kTabCount; ++t)
                ui.tab(lay_.tabs[t].x, lay_.tabs[t].y, lay_.tabs[t].h, kTabNames[t], t == tab_, focus_tabs_ && t == tab_);
            ui.fill(0, lay_.tab_bottom, ui.width(), 1, colors::kOutline);
        } else {
            ui.fill(0, lay_.bar_bottom, ui.width(), 1, colors::kOutline);
        }

        // Rows (scrollable).
        const int n = int(rows_.size());
        const UiRect& L = lay_.list;
        const float content = n ? row_top(n) : 0;
        if (follow_sel_ && sel_ >= 0 && sel_ < n && !focus_tabs_) {
            float top = row_top(sel_), bot = top + row_h(sel_);
            if (sel_ > 0 && rows_[size_t(sel_ - 1)].kind == Row::Kind::Heading) top = row_top(sel_ - 1);
            if (top < scroll_) scroll_ = top;
            if (bot > scroll_ + L.h) scroll_ = bot - L.h;
        }
        scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, content - L.h));
        if (focus_tabs_) scroll_ = 0;
        SDL_Rect clip{int(L.x), int(L.y), int(std::ceil(L.w)), int(L.h)};
        SDL_SetRenderClipRect(ui.renderer(), &clip);
        for (int i = 0; i < n; ++i) {
            float y = L.y + row_top(i) - scroll_;
            if (y + row_h(i) < L.y || y > L.bottom()) continue;
            draw_row(ui, rows_[size_t(i)], y, row_h(i), i == sel_ && !focus_tabs_, i == press_.row && gesture_.active());
        }
        SDL_SetRenderClipRect(ui.renderer(), nullptr);
        ui.scrollbar({L.x, L.y, S.right() - L.x - 1, L.h}, content, scroll_);

        // Description of the highlighted row (or a note: saved, added...).
        const float dy = lay_.desc_y;
        ui.fill(0, dy, ui.width(), ui.height() - dy, Color{22, 24, 31});
        ui.fill(0, dy, ui.width(), 1, colors::kOutline);
        std::string desc = note_left_ > 0 ? note_
                         : focus_tabs_    ? "Left/Right: switch page    Down: rows"
                         : sel_ >= 0 && sel_ < n ? rows_[size_t(sel_)].desc
                                                 : "";
        std::vector<std::string> lines = Ui::wrap(desc, S.w - 16);
        if (lines.size() > 2) {
            for (size_t k = 2; k < lines.size(); ++k) lines[1] += " " + lines[k];
            lines.resize(2);
            lines[1] = Ui::clip(lines[1], S.w - 16);
        }
        for (size_t i = 0; i < lines.size(); ++i)
            ui.text(S.x + 8, dy + 4 + float(i) * 11, lines[i], note_left_ > 0 ? colors::kAccent : colors::kText);
        if (!touch) ui.footer(footer_hint());
        if (confirm_.active) draw_confirm(ui);
        if (pad_.active) draw_pad(ui);
    }

    TouchLayout touch_layout() const override { return picking_ ? TouchLayout::Menu : TouchLayout::None; }
    bool adaptive_ui() const override { return !picking_; }

private:
    enum Button { kNoButton, kBackButton, kSaveButton };
    struct Layout {
        UiRect back, save, list;
        UiRect tabs[kTabCount];
        float bar_bottom = 20, tab_bottom = 40, desc_y = 318;
        float row_h = 18, heading_h = 18;
        float label_x = 24, val_x = 330, val_w = 288, slider_w = 200;
    };
    struct Press {
        int button = kNoButton;
        bool in_list = false;
        int tab = -1, row = -1;
        float scroll = 0;
    };

    bool editing() const { return gt_ != nullptr; }

    // --- data ----------------------------------------------------------------------------

    void init(App& app) {
        init_ = true;
        if (!app.vfs()) {
            error_ = "No disc is open.";
            return;
        }
        if (!game::ensure_gt_mode_session(menu::flow::vfs_reader(*app.vfs())) || !game::gt_mode_session()) {
            error_ = "This disc's GT Mode data cannot be read.";
            return;
        }
        data_ = game::gt_mode_session()->gt().data_ptr();
        if (std::optional<Bytes> j = app.vfs()->read("font/jis2uni.dat"); j && text::JisTable::is_utbl(*j)) {
            try {
                jis_ = text::JisTable::parse(*j);
            } catch (const std::exception&) {
            }
        }
        titles_ = game_menu_titles(*app.vfs(), app.language());
        series_ = ordered_series(*app.vfs(), data_->catalog);
        refresh_saves();
    }

    void refresh_saves() {
        saves_ = game::save_library().list();
        const game::GtModeSession* s = game::gt_mode_session();
        playing_ = s ? s->save_path() : std::string();
        summaries_.clear();
        for (const game::SaveInfo& f : saves_) {
            if (!f.state) {
                summaries_.emplace_back();
                continue;
            }
            GtMode view(data_);
            view.state() = *f.state;
            char b[160];
            std::snprintf(b, sizeof b, "Day %u   %s   %zu car%s   %s", f.state->day, credits(f.state->credits).c_str(),
                          f.state->garage.size(), f.state->garage.size() == 1 ? "" : "s", view.completion_text().c_str());
            summaries_.push_back(b);
        }
    }

    void open_save(const game::SaveInfo& info) {
        std::string err;
        std::optional<GameState> st = game::editable_save(info.path, &err);
        if (!st) {
            show_note("This save cannot be read: " + err);
            return;
        }
        gt_ = std::make_unique<GtMode>(data_);
        gt_->state() = std::move(*st);
        ++rev_;
        path_ = info.path;
        name_ = info.name;
        dirty_ = false;
        tab_ = kCareer;
        car_ = -1;
        list_sel_ = sel_;
        reset_list();
    }

    void close_save() {
        gt_.reset();
        car_ = -1;
        refresh_saves();
        reset_list();
        sel_ = list_sel_;
    }

    void save() {
        if (!gt_ || !dirty_) return;
        std::string err;
        if (game::store_edited_save(path_, gt_->state(), &err)) {
            dirty_ = false;
            show_note("Saved " + name_ + ".");
        } else {
            show_note("Could not save: " + err);
        }
    }

    void changed() {
        dirty_ = true;
        ++rev_;
    }

    void show_note(std::string s) {
        note_ = std::move(s);
        note_left_ = 3;
    }

    // Triangle / Back: the car page -> the garage; the save -> the list (asking about unsaved
    // changes); the list -> the main menu.
    void back(App& app) {
        if (editing() && tab_ == kGarage && car_ >= 0) {
            sel_ = car_row_;
            car_ = -1;
            follow_sel_ = true;
            return;
        }
        if (editing() && dirty_) {
            ask("Save the changes to " + name_ + "?", {"Cancel", "Discard", "Save"}, 2, [this](int a) {
                if (a == 2) save();
                if (a == 2 && dirty_) return; // (not saved: stay)
                if (a >= 1) close_save();
            });
            return;
        }
        if (editing()) close_save();
        else app.pop();
    }

    // Game text (car and paint names) for the UI font.
    std::string ui_text(const std::string& s) const { return text::game_text_for_ui(s, jis_.empty() ? nullptr : &jis_); }

    std::string car_name(const std::string& label) const {
        const data::Car* c = gt_ ? gt_->db().car(label) : nullptr;
        if (!c) return label;
        return ui_text(c->latin_name.empty() ? c->label : c->latin_name);
    }

    // --- rows ------------------------------------------------------------------------------

    Row& add(Row::Kind k, std::string label, std::string desc) {
        Row r;
        r.kind = k;
        r.label = std::move(label);
        r.desc = std::move(desc);
        rows_.push_back(std::move(r));
        return rows_.back();
    }
    Row& heading(std::string label) { return add(Row::Kind::Heading, std::move(label), ""); }
    Row& info(std::string label, std::string value, std::string desc = "") {
        Row& r = add(Row::Kind::Info, std::move(label), std::move(desc));
        r.value = std::move(value);
        return r;
    }
    // Yes / no settings say what each side means (`off`, `on`) instead of a switch.
    Row& either(std::string label, std::string desc, const char* off, const char* on, bool v, std::function<void(bool)> set) {
        return choice(std::move(label), std::move(desc), {off, on}, v ? 1 : 0, [set](int k) { set(k == 1); });
    }
    // `names` shown, `idx` the current one; clamped (no wrap).
    Row& choice(std::string label, std::string desc, std::vector<std::string> names, int idx, std::function<void(int)> set) {
        Row& r = add(Row::Kind::Choice, std::move(label), std::move(desc));
        const int n = int(names.size());
        r.value = idx >= 0 && idx < n ? names[size_t(idx)] : "?";
        r.change = [this, set, idx, n](int d) {
            int to = std::clamp(idx + d, 0, n - 1);
            if (to == idx) return;
            set(to);
            changed();
        };
        return r;
    }
    // Left/Right: +-`step`; Cross: the number pad.
    Row& number(std::string label, std::string desc, s64 v, s64 lo, s64 hi, s64 step, std::string text,
                std::function<void(s64)> set) {
        Row& r = add(Row::Kind::Number, label, std::move(desc));
        r.value = std::move(text);
        auto apply = [this, set, v](s64 to) {
            if (to == v) return;
            set(to);
            changed();
        };
        r.change = [apply, v, lo, hi, step](int d) { apply(std::clamp(v + d * step, lo, hi)); };
        r.activate = [this, apply, label, v, lo, hi] { open_pad(label, v, lo, hi, apply); };
        return r;
    }
    Row& slider(std::string label, std::string desc, float frac, std::string text, std::function<void(float)> set) {
        Row& r = add(Row::Kind::Slider, std::move(label), std::move(desc));
        r.frac = frac;
        r.value = std::move(text);
        r.change = [this, set, frac](int d) {
            float to = std::clamp(std::round(frac * 20 + d) / 20, 0.0f, 1.0f);
            if (to == frac) return;
            set(to);
            changed();
        };
        r.set_frac = [this, set, frac](float f) {
            float to = std::clamp(std::round(f * 20) / 20, 0.0f, 1.0f);
            if (to == frac) return;
            set(to);
            changed();
        };
        return r;
    }
    Row& action(std::string label, std::string desc, std::string value, std::function<void()> fn) {
        Row& r = add(Row::Kind::Action, std::move(label), std::move(desc));
        r.value = std::move(value);
        r.activate = std::move(fn);
        return r;
    }

    void build(App& app) {
        rows_.clear();
        if (!error_.empty()) info(error_, "");
        else if (!editing()) build_saves();
        else {
            switch (tab_) {
            case kCareer: build_career(); break;
            case kGarage: car_ >= 0 ? build_car() : build_garage(app); break;
            case kLicenses: build_licenses(); break;
            case kRaces: build_races(); break;
            case kArcade: build_arcade(); break;
            default: break;
            }
        }
        if (sel_ < 0 || sel_ >= int(rows_.size()) || !rows_[size_t(sel_)].selectable()) sel_ = nearest_selectable(sel_);
    }

    void build_saves() {
        heading("GT Mode saves");
        if (saves_.empty())
            info("No saves yet", "", "GT Mode saves its game here once it has been played (title menu: GT Mode).");
        for (size_t i = 0; i < saves_.size(); ++i) {
            const game::SaveInfo& s = saves_[i];
            const bool playing = game::same_save_file(s.path, playing_);
            if (!s.state) {
                Row& r = info(s.name, "Cannot be read", "This save cannot be read (" + s.error + "); it is left as it is.");
                r.enabled = false;
                continue;
            }
            Row& r = action(s.name + (playing ? "  (playing)" : ""),
                            playing ? "The game being played: the game takes the changes when they are saved."
                                    : "Edit this save.",
                            summaries_[i], [this, i] { open_save(saves_[i]); });
            r.accent = playing;
        }
    }

    void build_career() {
        GameState& s = gt_->state();
        heading("Money");
        number("Credits", "The money you have to spend. Left/Right: 10,000 Cr. at a time; Cross: type a number.",
               s.credits, 0, game::kEditMaxMoney, 10000, credits(s.credits), [&s](s64 v) { s.credits = v; });
        heading("Progress");
        number("Day", "GT Mode's day count: a day passes for every race or test you enter.", s.day, 1, game::kEditMaxDays, 1,
               game::format_credits(s.day), [&s](s64 v) { s.day = u32(v); });
        number("Races entered", "Every race entered, championship rounds included. Never fewer than the races won.",
               s.races, 0, game::kEditMaxCount, 1, game::format_credits(s.races), [&s](s64 v) { game::set_races(s, u32(v)); });
        number("Races won", "More wins than races entered raises the races entered too.", s.wins, 0, game::kEditMaxCount,
               1, game::format_credits(s.wins), [&s](s64 v) { game::set_wins(s, u32(v)); });
        number("Prize money won", "All the prize money won so far, as GT Mode's Status page shows it. Not money to spend.",
               s.prize_money, 0, game::kEditMaxMoney, 10000, credits(s.prize_money), [&s](s64 v) { s.prize_money = v; });
        info("Completion", gt_->completion_text(),
             "How much of the game is done: races won, license tests, arcade results. It follows the other pages.");
        heading("Endings");
        either("Short ending seen", "No: the short ending plays again after a win once the Beginner League is complete.",
               "No", "Yes", s.ending_short_seen, [&s](bool on) { s.ending_short_seen = on; });
        either("Long ending seen",
               "No: the long ending plays again after a win once the Amateur or Professional League is complete.", "No",
               "Yes", s.ending_long_seen, [&s](bool on) { s.ending_long_seen = on; });
    }

    void build_garage(App& app) {
        GameState& s = gt_->state();
        const bool full = int(s.garage.size()) >= game::kMaxGarageCars;
        char b[64];
        std::snprintf(b, sizeof b, "%zu of %d cars", s.garage.size(), game::kMaxGarageCars);
        Row& add_row = action("Add a car...",
                              full ? "The garage is full (200 cars)."
                                   : "Any car of the game, in the paint chosen, with its stock parts and a new engine.",
                              b, [this, &app] { open_picker(app); });
        add_row.enabled = !full;
        heading("Cars, newest first");
        if (s.garage.empty()) info("No cars", "");
        if (garage_rev_ != rev_) { // (a car's power takes its spec: once per change, not per frame)
            garage_rev_ = rev_;
            garage_values_.clear();
            for (const game::GarageCar& c : s.garage)
                garage_values_.push_back(gt_->power_text(gt_->power_ps(c), " ") + "   " + distance(c.cond.odometer_m));
        }
        for (size_t i = 0; i < s.garage.size(); ++i) {
            const game::GarageCar& c = s.garage[i];
            const bool ride = int(i) == s.ride;
            std::string value = i < garage_values_.size() ? garage_values_[i] : std::string();
            if (ride) value = "Active car   " + value;
            Row& r = action(car_name(c.car), "Change this car, or remove it.", value, [this, i] {
                car_row_ = sel_;
                car_ = int(i);
                sel_ = -1;
                scroll_ = 0;
                follow_sel_ = true;
            });
            r.accent = ride;
        }
    }

    void build_car() {
        GameState& s = gt_->state();
        if (car_ >= int(s.garage.size())) {
            car_ = -1;
            return;
        }
        game::GarageCar& c = s.garage[size_t(car_)];
        const int idx = car_;
        heading(car_name(c.car));
        action("All cars", "Back to the garage.", "", [this] {
            sel_ = car_row_;
            car_ = -1;
            follow_sel_ = true;
        });
        if (idx == s.ride) {
            info("Active car", "This car", "The car you drive in GT Mode's races.");
        } else {
            action("Active car", "Make this the car you drive in GT Mode's races.", "Make it the active car", [this, idx] {
                gt_->ride(idx);
                changed();
            });
        }
        // Paint: the model's list (carcolor.db), with its names.
        const game::GtData& d = *data_;
        std::vector<std::string> paints;
        for (int k = 0, n = d.color_count(c.car); k < n; ++k) {
            // (names starting with '-' show none, as on the car pages: menus §9.5)
            std::string nm = ui_text(d.color_name_latin(d.color_id(c.car, k)));
            paints.push_back(nm.empty() || nm[0] == '-' ? "Paint " + std::to_string(k + 1) : nm);
        }
        if (paints.empty()) paints.push_back("Paint 1");
        const int paint = std::clamp(c.color, 0, int(paints.size()) - 1);
        Row& pr = choice("Paint", "The car's paint, from its model's colors.", paints, paint, [&c](int k) { c.color = k; });
        if (const u32 id = d.color_id(c.car, paint); id != 0xFFFFFFFFu) pr.chip = true, pr.rgb = d.color_rgb(id);
        const u32 odo = game::odometer_units(d, c.cond.odometer_m);
        number("Odometer", "The distance the car has been driven.", odo, 0, game::odometer_units_max(d), 100,
               distance(c.cond.odometer_m), [&c, &d](s64 v) { c.cond.odometer_m = game::odometer_metres(d, u32(v)); });
        static const char* const kEngine[game::kEngineStates] = {"New", "Run in", "Worn"};
        std::vector<std::string> engines;
        for (int k = 0; k < game::kEngineStates; ++k) {
            const float f = sim::engine_condition_factor(game::engine_wear(game::EngineState(k)), 0);
            engines.push_back(std::string(kEngine[k]) + " (" + std::to_string(int(std::lround(f * 100))) + "% power)");
        }
        choice("Engine", "A new engine gives 97% of its power until it is run in (300 km); a worn one 98% (from 600 km).",
               engines, int(game::engine_state(c.cond.engine_wear_m)),
               [&c](int k) { c.cond.engine_wear_m = game::engine_wear(game::EngineState(k)); });
        choice("Oil", "Fresh oil (just changed) adds power for its first 300 km; used oil lights the oil lamp in races.",
               {"Fresh", "Used"}, int(game::oil_state(c.cond.oil_m)),
               [&c](int k) { c.cond.oil_m = game::oil_value(game::OilState(k)); });
        const float dirt = std::clamp(c.cond.dirt / 0.8f, 0.0f, 1.0f);
        slider("Dirt", "How dirty the car looks (a car wash cleans it).", dirt,
               dirt == 0 ? "Clean" : std::to_string(int(std::lround(dirt * 100))) + "%",
               [&c](float f) { c.cond.dirt = f * 0.8f; });
        info("Power", gt_->power_text(gt_->power_ps(c), " "), "With its parts and engine condition.");
        action("Remove from the garage", "The car leaves the garage (not sold: no credits).", "", [this, idx] {
            ask("Remove the " + car_name(gt_->state().garage[size_t(idx)].car) + " from the garage?", {"Cancel", "Remove"}, 0,
                [this, idx](int a) {
                    if (a != 1) return;
                    gt_->remove_car(idx);
                    changed();
                    car_ = -1;
                    sel_ = car_row_;
                    follow_sel_ = true;
                });
        });
    }

    void build_licenses() {
        action("Gold in every test", "Every license test won with gold (the prize cars for all gold are not given).", "",
               [this] {
                   game::set_every_license_medal(*gt_, Medal::Gold);
                   changed();
               });
        action("Clear every test", "Every license test back to no medal: the licenses are not held.", "", [this] {
            game::set_every_license_medal(*gt_, Medal::None);
            changed();
        });
        std::vector<std::string> medals(std::begin(kMedalNames), std::end(kMedalNames));
        for (int l = 0; l < game::kLicenseCount; ++l) {
            heading(std::string(kLicenseTitles[l]) + (gt_->license_held(l) ? "  - held" : ""));
            for (int t = 0; t < game::kTestsPerLicense; ++t) {
                const std::string code = game::license_test_code(*gt_, l, t);
                const game::LicenseTestRecord& rec = gt_->state().license_tests[size_t(l * game::kTestsPerLicense + t)];
                int idx = 0;
                for (int k = 0; k < 4; ++k)
                    if (kMedals[k] == rec.medal) idx = k;
                std::string label = license_test_label(code);
                if (auto it = titles_.find(code); it != titles_.end()) label += "  " + it->second;
                std::vector<std::string> names = medals;
                if (rec.best_time > 0)
                    names[size_t(idx)] += "   " + game::format_time_ms(u32(std::llround(rec.best_time * 1000)));
                choice(label,
                       "The test's medal. Its best time becomes the medal's limit unless the time it has earns that medal.",
                       names, idx, [this, l, t](int k) { game::set_license_medal(*gt_, l, t, kMedals[k]); });
            }
        }
    }

    // A race's best place: not raced, or 1st to its field's last place.
    void place_row(const std::string& name, const std::string& code) {
        auto f = field_.find(code);
        if (f == field_.end()) {
            std::optional<game::EventRules> r = gt_->rules(code);
            f = field_.emplace(code, std::clamp(r ? r->cars : 6, 1, game::kNoPosition - 1)).first;
        }
        const int cars = f->second;
        std::vector<std::string> names{"Not raced"};
        for (int p = 0; p < cars; ++p) names.push_back(ordinal(p));
        const int best = gt_->state().best_position(code);
        const int idx = best >= 0 && best < cars ? best + 1 : 0;
        if (best >= cars && best < game::kNoPosition) names[0] = ordinal(best); // (a place this field cannot have)
        choice(name, "The best place in this race (1st: won).", names, idx, [this, code](int k) {
            game::set_race_best(gt_->state(), code, k == 0 ? game::kNoPosition : k - 1);
        });
    }

    void build_races() {
        const GameState& s = gt_->state();
        const std::vector<std::string>& codes = data_->race_codes;
        int won = 0;
        for (const std::string& c : codes) won += s.best_position(c) == 0;
        char b[64];
        std::snprintf(b, sizeof b, "%d of %zu won", won, codes.size());
        action("Win every race", "Every race and championship won (their prize cars are not given).", b, [this] {
            game::win_every_race(*gt_);
            changed();
        });
        action("Clear every result", "Every race back to not raced.", "", [this] {
            game::clear_race_results(gt_->state());
            changed();
        });
        std::set<std::string> listed;
        for (const League& lg : kLeagues) {
            bool head = false;
            for (const game::RaceSeries* sr : series_) {
                if (sr->tier != lg.tier) continue;
                if (!head) heading(lg.title), head = true;
                auto t = titles_.find(sr->page);
                const std::string title = t != titles_.end() ? t->second : sr->id;
                for (size_t i = 0; i < sr->races.size(); ++i) {
                    std::string name = title;
                    if (sr->races.size() > 1) name += (sr->championship() ? " - Round " : " - Race ") + std::to_string(i + 1);
                    place_row(name, sr->races[i]);
                    listed.insert(sr->races[i]);
                }
                if (sr->championship()) {
                    const std::string champ = game::championship_code(*sr);
                    listed.insert(champ);
                    either(title + " - Championship", "The championship's title (won on points), which completes the series.",
                           "Not won", "Won", s.best_position(champ) == 0, [this, champ](bool on) {
                               game::set_race_best(gt_->state(), champ, on ? 0 : game::kNoPosition);
                           });
                }
            }
        }
        bool head = false;
        for (const std::string& c : codes) {
            if (listed.count(c)) continue;
            if (!head) heading("Other results"), head = true;
            place_row(c, c);
        }
    }

    void build_arcade() {
        game::ArcadeProgress& a = gt_->state().arcade;
        action("Open every course and car", "Every arcade course group and car level open.", "", [this] {
            game::open_arcade(*gt_);
            changed();
        });
        heading("Courses");
        const int max_level = game::arcade_course_level_max(*data_);
        std::vector<int> levels; // SINGLE_RACE course levels
        if (auto it = data_->arcade_courses.find("SINGLE_RACE"); it != data_->arcade_courses.end())
            for (const game::GtData::ArcadeCourse& c : it->second) levels.push_back(c.level);
        std::vector<std::string> names;
        for (int k = 0; k <= max_level; ++k) {
            const int open = int(std::count_if(levels.begin(), levels.end(), [k](int l) { return l <= k; }));
            names.push_back(std::to_string(open) + " of " + std::to_string(levels.size()) + " courses");
        }
        choice("Courses open", "The arcade's courses open in groups: each group as the one before it is won.", names,
               std::clamp(a.course_level, 0, max_level), [&a](int k) { a.course_level = k; });
        heading("Car levels");
        std::map<int, int> cars; // level -> arcade cars
        for (const auto& [cls, list] : data_->arcade_classes)
            for (const game::GtData::ArcadeCar& c : list) ++cars[c.level];
        for (int k = 1; k <= game::kArcadeCarLevels; ++k) {
            char b[64];
            const int n = cars.count(k) ? cars[k] : 0;
            std::snprintf(b, sizeof b, "Level %d: %d car%s", k, n, n == 1 ? "" : "s");
            either(b, "Unlocked: the arcade's cars of this level can be chosen.", "Locked", "Unlocked",
                   a.car_flags[size_t(k - 1)], [&a, k](bool on) { a.car_flags[size_t(k - 1)] = on; });
        }
    }

    std::string distance(double metres) const {
        return game::format_credits(game::odometer_units(*data_, metres)) + (data_->imperial() ? " mi" : " km");
    }

    // --- the car picker (the online lobby's car page) ------------------------------------------

    void open_picker(App& app) {
        if (cars_.empty()) {
            for (const data::Car& c : gt_->db().cars()) {
                std::string model = gt_->db().model_of(c);
                if (model.empty() || !app.vfs()->exists("cars/day/" + model)) continue; // as the race lists them
                PickerCar e;
                e.label = c.label;
                e.name = ui_text(c.latin_name.empty() ? c.label : c.latin_name);
                e.model = model;
                e.maker = c.maker;
                e.mod = gt_->db().mod_car(c.label) != nullptr;
                cars_.push_back(std::move(e));
            }
            std::sort(cars_.begin(), cars_.end(), [](const PickerCar& a, const PickerCar& b) { return a.name < b.name; });
        }
        if (cars_.empty()) {
            show_note("No cars found on the disc.");
            return;
        }
        picker_.open(app, art_, gt_->db(), cars_, 0, 0, [](int, int) { return std::string(); });
        picker_.show_groups(); // (the makers first)
        picking_ = true;
    }

    void update_picker(App& app) {
        CarPicker::Result r = picker_.update(app);
        if (r == CarPicker::Result::None) return;
        picking_ = false;
        app.menu().suppress();
        if (r != CarPicker::Result::Chosen) return;
        const PickerCar& c = cars_[size_t(picker_.car())];
        if (gt_->add_car(c.label, picker_.paint()) < 0) {
            show_note("The car cannot be added.");
            return;
        }
        changed();
        show_note("Added the " + c.name + ".");
        sel_ = -1; // (the new car is the first)
        build(app);
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].kind == Row::Kind::Heading) {
                sel_ = int(i) + 1;
                break;
            }
        follow_sel_ = true;
    }

    // --- the number pad ---------------------------------------------------------------------------

    // Keys: digits 1..9 on rows 0..2, then Del 0 Max, then Cancel OK.
    static constexpr int kPadKeys = 14;
    enum PadKey { kDel = 9, kZero = 10, kMax = 11, kCancel = 12, kOk = 13 };
    struct NumberPad {
        bool active = false;
        std::string title, digits;
        bool fresh = true; // showing the value it opened with: the first digit replaces it
        s64 lo = 0, hi = 0;
        int focus = kOk;
        std::function<void(s64)> done;
        UiRect keys[kPadKeys];
    } pad_;

    void open_pad(const std::string& title, s64 v, s64 lo, s64 hi, std::function<void(s64)> done) {
        pad_ = NumberPad{};
        pad_.active = true;
        pad_.title = title;
        pad_.digits = std::to_string(v);
        pad_.lo = lo, pad_.hi = hi;
        pad_.done = std::move(done);
    }

    s64 pad_value() const {
        s64 v = 0;
        for (char c : pad_.digits) v = std::min(v * 10 + (c - '0'), pad_.hi);
        return std::clamp(v, pad_.lo, pad_.hi);
    }

    void pad_press(int key) {
        const size_t max_digits = std::to_string(pad_.hi).size();
        if (key < 9 || key == kZero) {
            if (pad_.fresh) pad_.digits.clear();
            pad_.fresh = false;
            if (pad_.digits == "0") pad_.digits.clear();
            if (pad_.digits.size() < max_digits) pad_.digits += char('0' + (key == kZero ? 0 : key + 1));
        } else if (key == kDel) {
            if (pad_.fresh) pad_.digits.clear();
            else if (!pad_.digits.empty()) pad_.digits.pop_back();
            pad_.fresh = false;
        } else if (key == kMax) {
            pad_.digits = std::to_string(pad_.hi);
            pad_.fresh = true;
        } else if (key == kCancel) {
            pad_.active = false;
        } else if (key == kOk) {
            pad_.active = false;
            if (pad_.done) pad_.done(pad_value());
        }
    }

    void pad_event(App& app, const SDL_Event& e) {
        if (e.type == SDL_EVENT_KEY_DOWN) {
            // Digits by key position (the number row and the keypad), whatever the layout.
            const SDL_Scancode k = e.key.scancode;
            if (k >= SDL_SCANCODE_1 && k <= SDL_SCANCODE_9) pad_press(int(k - SDL_SCANCODE_1));
            else if (k >= SDL_SCANCODE_KP_1 && k <= SDL_SCANCODE_KP_9) pad_press(int(k - SDL_SCANCODE_KP_1));
            else if (k == SDL_SCANCODE_0 || k == SDL_SCANCODE_KP_0) pad_press(kZero);
            else if (k == SDL_SCANCODE_BACKSPACE || k == SDL_SCANCODE_DELETE) {
                pad_press(kDel);
                app.menu().suppress(); // (Backspace is also the menus' back)
            } else if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER) {
                pad_press(kOk);
                app.menu().suppress(); // (Enter is also the menus' Cross)
            }
            return;
        }
        if (gesture_.feed(e) == Gesture::Result::Tap)
            for (int i = 0; i < kPadKeys; ++i)
                if (pad_.keys[i].contains(gesture_.x(), gesture_.y())) {
                    pad_.focus = i;
                    pad_press(i);
                    return;
                }
    }

    void update_pad(const MenuInput& m) {
        // Focus: a 3 x 4 grid, then the two buttons under it.
        int row = pad_.focus < kCancel ? pad_.focus / 3 : 4, col = pad_.focus < kCancel ? pad_.focus % 3 : pad_.focus - kCancel;
        if (int v = m.vertical()) {
            const int to = std::clamp(row + v, 0, 4);
            if (to == 4 && row < 4) col = col < 2 ? 0 : 1; // Cancel under the left two, OK under the right
            else if (row == 4 && to < 4) col = col == 0 ? 0 : 2;
            row = to;
        }
        if (int h = m.horizontal()) col = std::clamp(col + h, 0, row == 4 ? 1 : 2);
        pad_.focus = row == 4 ? kCancel + col : row * 3 + col;
        if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) pad_press(kCancel);
        else if (m.pressed(PadButton::Start)) pad_press(kOk);
        else if (m.pressed(PadButton::Square)) pad_press(kDel);
        else if (m.pressed(PadButton::Cross)) pad_press(pad_.focus);
    }

    void draw_pad(Ui& ui) {
        const bool touch = ui.touch();
        const float kw = touch ? 64 : 56, kh = touch ? 36 : 22, gap = 6;
        const float w = 3 * kw + 2 * gap + 24, h = 54 + 4 * (kh + gap) + kh + 18;
        const float x = std::floor((ui.width() - w) / 2), y = std::floor((ui.height() - h) / 2);
        ui.fill(0, 0, ui.width(), ui.height(), Color{0, 0, 0, 120});
        ui.fill_round(x, y, w, h, 6, colors::kPanel);
        ui.frame_round(x, y, w, h, 6, 1, colors::kAccent);
        ui.text(x + 12, y + 10, pad_.title, colors::kAccent);
        const std::string range = game::format_credits(pad_.lo) + " to " + game::format_credits(pad_.hi);
        ui.text_right(x + w - 12, y + 10, range, colors::kDim);
        // The value, as typed (a value above the limit shows the limit it will be).
        const UiRect box{x + 12, y + 24, w - 24, 22};
        ui.fill_round(box.x, box.y, box.w, box.h, 4, colors::kBackground);
        ui.frame_round(box.x, box.y, box.w, box.h, 4, 1, pad_.fresh ? colors::kOutline : colors::kAccent);
        const std::string shown = pad_.digits.empty() ? "" : game::format_credits(pad_value());
        ui.text_right(box.right() - 8, std::floor(box.y + (box.h - Ui::kChar) / 2), shown + (pad_.fresh ? "" : "_"),
                      pad_.fresh ? colors::kDim : colors::kText);
        static const char* const kLabels[kPadKeys] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "Del", "0", "Max", "Cancel", "OK"};
        const float gx = x + 12, gy = box.bottom() + 8;
        for (int i = 0; i < kCancel; ++i) {
            const int row = i / 3, col = i % 3;
            pad_.keys[i] = {gx + float(col) * (kw + gap), gy + float(row) * (kh + gap), kw, kh};
        }
        const float bw = (3 * kw + 2 * gap - gap) / 2, by = gy + 4 * (kh + gap) + 4;
        pad_.keys[kCancel] = {gx, by, bw, kh};
        pad_.keys[kOk] = {gx + bw + gap, by, bw, kh};
        for (int i = 0; i < kPadKeys; ++i) ui.button(pad_.keys[i], kLabels[i], i == pad_.focus, false, i == kOk);
    }

    // --- the confirm dialog --------------------------------------------------------------------

    struct Confirm {
        bool active = false;
        std::string text;
        std::vector<std::string> buttons; // the first is Cancel (Triangle answers it)
        int focus = 0;
        std::function<void(int)> answer;
        std::vector<UiRect> rects;
    } confirm_;

    void ask(std::string text, std::vector<std::string> buttons, int focus, std::function<void(int)> answer) {
        confirm_ = Confirm{};
        confirm_.active = true;
        confirm_.text = std::move(text);
        confirm_.buttons = std::move(buttons);
        confirm_.focus = focus;
        confirm_.answer = std::move(answer);
    }

    void answer_confirm(int i) {
        confirm_.active = false;
        if (confirm_.answer) confirm_.answer(i);
    }

    void draw_confirm(Ui& ui) {
        const bool touch = ui.touch();
        const float bw = 110, bh = touch ? 34 : 22, gap = 10;
        const float n = float(confirm_.buttons.size());
        const float w = std::min(ui.width() - 16, std::max(320.0f, n * bw + (n - 1) * gap + 32)), h = 48 + bh + 12;
        const float x = std::floor((ui.width() - w) / 2), y = std::floor((ui.height() - h) / 2);
        ui.fill(0, 0, ui.width(), ui.height(), Color{0, 0, 0, 120});
        ui.fill(x, y, w, h, colors::kPanel);
        ui.frame(x, y, w, h, colors::kAccent);
        ui.text_center_in(x, w, y + 16, Ui::elide(confirm_.text, w - 16), colors::kText);
        confirm_.rects.clear();
        float bx = x + (w - (n * bw + (n - 1) * gap)) / 2;
        for (size_t i = 0; i < confirm_.buttons.size(); ++i, bx += bw + gap) {
            confirm_.rects.push_back({bx, y + h - bh - 12, bw, bh});
            ui.button(confirm_.rects.back(), confirm_.buttons[i], int(i) == confirm_.focus, false);
        }
    }

    // --- layout and drawing ------------------------------------------------------------------

    void compute_layout(const Ui& ui) {
        const UiRect& S = ui.safe();
        const bool touch = ui.touch();
        Layout& l = lay_;
        const float bar_h = touch ? 30.0f : 20.0f;
        l.bar_bottom = S.y + bar_h;
        l.back = {S.x + 6, S.y + (touch ? 3 : 2), std::max(touch ? 72.0f : 64.0f, ui.tab_width("< Back")), bar_h - (touch ? 6 : 4)};
        const float sw = std::max(touch ? 72.0f : 64.0f, ui.tab_width("Saved"));
        l.save = {S.right() - 6 - sw, l.back.y, sw, l.back.h};
        float top = l.bar_bottom;
        if (editing()) {
            const float th = touch ? 28.0f : 16.0f, ty = l.bar_bottom + 4;
            float tx = S.x + 6;
            for (int t = 0; t < kTabCount; ++t) {
                const float w = ui.tab_width(kTabNames[t]);
                l.tabs[t] = {tx, ty, w, th};
                tx += w + 4;
            }
            l.tab_bottom = ty + th + 2;
            top = l.tab_bottom;
        }
        const float desc_h = 26, footer_h = touch ? 0.0f : 16.0f;
        l.desc_y = ui.height() - footer_h - desc_h - (touch ? ui.height() - S.bottom() : 0);
        l.list = {S.x, top + 3, S.w, l.desc_y - top - 4};
        l.row_h = touch ? 32.0f : 18.0f;
        l.heading_h = touch ? 26.0f : 18.0f;
        l.label_x = S.x + (touch ? 18 : 16);
        l.val_x = std::floor(S.x + S.w * 0.5f);
        l.val_w = S.right() - 16 - l.val_x;
        l.slider_w = std::max(60.0f, l.val_w - (touch ? 64 : 88));
        if (touch) l.val_x += 8, l.slider_w -= 8, l.val_w -= 8;
    }

    float row_h(int i) const { return rows_[size_t(i)].kind == Row::Kind::Heading ? lay_.heading_h : lay_.row_h; }
    float row_top(int i) const {
        float y = 0;
        for (int k = 0; k < i && k < int(rows_.size()); ++k) y += row_h(k);
        return y;
    }
    int row_at(float y) const {
        float off = y - lay_.list.y + scroll_, top = 0;
        for (int i = 0; i < int(rows_.size()); ++i) {
            if (off >= top && off < top + row_h(i)) return i;
            top += row_h(i);
        }
        return -1;
    }
    int tab_at(float x, float y) const {
        if (!editing()) return -1;
        for (int t = 0; t < kTabCount; ++t) {
            UiRect r = lay_.tabs[t];
            r.y -= 2, r.h += 4;
            if (r.contains(x, y)) return t;
        }
        return -1;
    }

    int first_selectable() const {
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].selectable()) return int(i);
        return -1;
    }
    // The selectable row at or after `from` (rows rebuilt with fewer, or a heading there), else
    // the last one before it.
    int nearest_selectable(int from) const {
        for (int i = std::max(from, 0); i < int(rows_.size()); ++i)
            if (rows_[size_t(i)].selectable()) return i;
        for (int i = std::min(from, int(rows_.size())) - 1; i >= 0; --i)
            if (rows_[size_t(i)].selectable()) return i;
        return -1;
    }

    void move(int dir) {
        const int n = int(rows_.size());
        for (int i = sel_ + dir; i >= 0 && i < n; i += dir)
            if (rows_[size_t(i)].selectable()) {
                sel_ = i;
                return;
            }
        if (dir < 0 && editing()) {
            focus_tabs_ = true; // up past the first row: the tab bar
            scroll_ = 0;
        }
    }

    void switch_tab(int d) {
        tab_ = (tab_ + d + kTabCount) % kTabCount;
        car_ = -1;
        reset_list();
    }

    void reset_list() {
        sel_ = -1;
        scroll_ = 0;
        follow_sel_ = true;
        dragging_ = -1;
        focus_tabs_ = false;
        press_ = Press{};
    }

    std::string footer_hint() const {
        const std::string back = editing() ? "    Triangle/Esc: back    Start/P: save" : "    Triangle/Esc: back";
        if (focus_tabs_) return "Left/Right: page" + back;
        if (sel_ < 0 || sel_ >= int(rows_.size())) return back.substr(4);
        switch (rows_[size_t(sel_)].kind) {
        case Row::Kind::Choice: return "Left/Right: change" + back;
        case Row::Kind::Slider: return "Left/Right: adjust (or drag)" + back;
        case Row::Kind::Number: return "Left/Right: change    Cross/Enter: type a number" + back;
        default: return "Cross/Enter: choose" + back;
        }
    }

    void draw_row(Ui& ui, const Row& r, float y, float h, bool selected, bool pressed) {
        const UiRect& S = ui.safe();
        const bool touch = ui.touch();
        const float ty = std::floor(y + (h - Ui::kChar) / 2), cy = y + h / 2;
        const float x0 = S.x + 6, x1 = S.right() - 8;
        if (r.kind == Row::Kind::Heading) {
            const float hy = std::floor(y + h - 12);
            ui.text(lay_.label_x - 8, hy, r.label, colors::kAccent);
            const float lx = lay_.label_x - 8 + Ui::text_width(r.label) + 8;
            ui.fill(lx, hy + 4, x1 - lx, 1, colors::kOutline);
            return;
        }
        if (selected || pressed) {
            ui.fill_round(x0, y + 1, x1 - x0, h - 2, touch ? 6.0f : 3.0f, pressed ? Color{62, 70, 100} : colors::kHighlight);
            ui.fill_round(x0, y + 4, 3, h - 8, 1.5f, colors::kAccent);
        } else if (touch && r.selectable()) {
            ui.fill(x0 + 8, y + h - 1, x1 - x0 - 16, 1, Color{34, 37, 48});
        }
        const bool grey = !r.enabled;
        const Color label = r.kind == Row::Kind::Info || grey ? colors::kDim : selected ? colors::kAccent : colors::kText;
        ui.text(lay_.label_x, ty, Ui::elide(r.label, lay_.val_x - lay_.label_x - 12), label);
        const float vx = lay_.val_x, vw = lay_.val_w;
        const Color val = grey ? colors::kDim : colors::kText;
        switch (r.kind) {
        case Row::Kind::Choice:
        case Row::Kind::Number: {
            const Color arrow = !grey ? (selected ? colors::kAccent : colors::kText) : colors::kOutline;
            const char* lo = r.kind == Row::Kind::Number ? "-" : "<";
            const char* hi = r.kind == Row::Kind::Number ? "+" : ">";
            if (touch) {
                const float bw = 26, bh = h - 8;
                ui.fill_round(vx, y + 4, bw, bh, 5, Color{40, 43, 54});
                ui.fill_round(vx + vw - bw, y + 4, bw, bh, 5, Color{40, 43, 54});
                ui.text_center_in(vx, bw, ty, lo, arrow);
                ui.text_center_in(vx + vw - bw, bw, ty, hi, arrow);
            } else {
                ui.text(vx, ty, lo, arrow);
                ui.text_right(vx + vw, ty, hi, arrow);
            }
            const std::string v = Ui::elide(r.value, vw - (touch ? 60 : 32) - (r.chip ? 20 : 0));
            if (r.chip) {
                const float sx = std::floor(vx + (vw - Ui::text_width(v) - 20) / 2); // chip, gap, name
                draw_paint_chip(ui, sx, std::floor(cy - 5), 14, 10, r.rgb, false);
                ui.text(sx + 20, ty, v, val);
            } else {
                ui.text_center_in(vx, vw, ty, v, val);
            }
            break;
        }
        case Row::Kind::Slider:
            ui.slider(vx, cy, lay_.slider_w, r.frac, selected, !grey);
            ui.text_right(vx + vw, ty, r.value, val);
            break;
        case Row::Kind::Action:
            ui.text(vx, ty, Ui::elide(r.value, vw - 16), grey ? colors::kOutline : r.accent ? colors::kAccent : colors::kDim);
            if (!grey) ui.text_right(vx + vw, ty, ">", selected ? colors::kAccent : colors::kOutline);
            break;
        case Row::Kind::Info: ui.text(vx, ty, Ui::elide(r.value, vw), grey ? colors::kBad : colors::kText); break;
        default: break;
        }
    }

    bool init_ = false;
    std::string error_;
    std::shared_ptr<const game::GtData> data_;
    text::JisTable jis_; // the disc's (Japanese names)
    std::map<std::string, std::string> titles_;
    std::vector<const game::RaceSeries*> series_;
    std::map<std::string, int> field_; // race code -> its field's size (the places it has)
    std::vector<game::SaveInfo> saves_;
    std::vector<std::string> summaries_; // each save's day, credits, cars and completion
    std::string playing_;                // the session's save

    // The save being edited (none: the list).
    std::unique_ptr<GtMode> gt_;
    std::string path_, name_;
    bool dirty_ = false;
    u64 rev_ = 0;        // counts the changes (and opens): the garage list's figures follow it
    u64 garage_rev_ = ~u64(0);
    std::vector<std::string> garage_values_; // each car's power and odometer
    int tab_ = kCareer;
    int car_ = -1;     // the garage car whose page is open
    int car_row_ = -1; // its row on the garage page
    int list_sel_ = -1;

    std::vector<Row> rows_;
    int sel_ = -1;
    float scroll_ = 0;
    bool follow_sel_ = true;
    int dragging_ = -1;
    bool focus_tabs_ = false;
    bool ui_touch_ = false;
    Gesture gesture_;
    Press press_;
    Layout lay_;
    std::string note_;
    double note_left_ = 0;

    bool picking_ = false;
    std::vector<PickerCar> cars_;
    PickerArt art_;
    CarPicker picker_;
};

} // namespace

GT3R_REGISTER_PLAYER_SCREEN(save_editor, "saveeditor", "Save editor",
                            "Change a GT Mode save: credits, days, the garage, licenses, race results, arcade unlocks.", 11,
                            [] { return std::make_unique<SaveEditorScreen>(); });

} // namespace gt3r
