// Settings: every user-facing config.ini option on tabbed pages (Display,
// Audio, Controls, Rumble & feedback, Touch, Game disc, Online). Changes apply at once
// where the engine can (volume, vsync, speed units, MSAA, anisotropic filtering, 3D
// resolution, bindings, force feedback, touch layout) and are saved to config.ini
// immediately. Only keys the engine reads are offered; see README "Settings" for
// the key list.

#include "app/app.h"
#include "app/screen_online.h"
#include "app/screen_registry.h"
#include "app/updates.h"

#include "base/file.h"
#include "disc/region.h"
#include "menu/flow/menu_sound.h"
#include "net/session.h"
#include "platform/recent_discs.h"
#include "platform/render_scale.h"
#include "race/exact_setup.h"
#include "sim/exact/program_data.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gt3r {

namespace {

#if GT3R_XR
enum Tab { kDisplay, kAudio, kControls, kFfb, kTouch, kDisc, kOnline, kVr, kTabCount };
const char* const kTabNames[kTabCount] = {"Display", "Audio", "Controls", "Rumble & feedback", "Touch", "Game disc", "Online", "VR"};
#else
enum Tab { kDisplay, kAudio, kControls, kFfb, kTouch, kDisc, kOnline, kTabCount };
const char* const kTabNames[kTabCount] = {"Display", "Audio", "Controls", "Rumble & feedback", "Touch", "Game disc", "Online"};
#endif

struct Row {
    enum class Kind { Toggle, Choice, Slider, Action, Info, Heading, Preview, Binding };
    Kind kind = Kind::Info;
    std::string label, desc, value;
    bool on = false;       // Toggle
    float frac = 0;        // Slider position 0..1
    bool enabled = true;   // false: shown greyed, changes ignored
    bool dim = false;      // greyed but still adjustable (touch settings on desktop)
    bool restart = false;  // takes effect after a restart (and differs from the running state)
    bool columns = false;  // Heading over binding rows: their column titles
    std::function<void(int)> change;     // Left/Right
    std::function<void(float)> set_frac; // slider pointer position 0..1
    std::function<void()> activate;      // Cross / tap
    std::function<void()> remove;        // Square (recent discs)
    // Binding: one control of a player's menu pad or race pad, with its live meter, the
    // controller's inputs (`value`) and the keyboard's (`keys`). Square adds an input, Select
    // clears the highlighted column.
    int player = 0;
    bool race = false;
    PadTarget target;
    std::string keys;
    std::function<void()> append, clear;

    bool selectable() const { return kind != Kind::Info && kind != Kind::Heading && kind != Kind::Preview; }
};

std::string percent(float v) { return std::to_string(int(std::lround(v * 100))) + "%"; }

float snap(float v, float lo, float hi, float step) {
    v = std::clamp(v, lo, hi);
    return std::clamp(lo + std::round((v - lo) / step) * step, lo, hi);
}

bool disc_available(const std::string& path) {
    return path.find("://") != std::string::npos || host_path_exists(path);
}

class SettingsScreen : public Screen {
public:
    SettingsScreen(int tab, bool to_game) : tab_(std::clamp(tab, 0, kTabCount - 1)), to_game_(to_game) {
        // Screenshots / testing: GT3R_SETTINGS_TAB=<0..6> opens that page.
        if (const char* t = SDL_getenv("GT3R_SETTINGS_TAB"); t && *t) tab_ = std::clamp(std::atoi(t), 0, kTabCount - 1);
    }

    void on_event(App& app, const SDL_Event& e) override {
        build(app);
        if (confirm_.active) { // a tap answers the replace-Custom question
            if (gesture_.feed(e) == Gesture::Result::Tap) {
                if (confirm_.cancel_rect.contains(gesture_.x(), gesture_.y())) answer_confirm(false);
                else if (confirm_.replace_rect.contains(gesture_.x(), gesture_.y())) answer_confirm(true);
            }
            return;
        }
        if (capture_.state != Capture::State::Idle) { // the input goes to the binding
            // A tap or click, Esc or Android Back cancels it (InputManager doesn't capture those).
            const bool back_key = e.type == SDL_EVENT_KEY_DOWN &&
                                  (e.key.scancode == SDL_SCANCODE_ESCAPE || e.key.scancode == SDL_SCANCODE_AC_BACK);
            if (back_key || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_FINGER_DOWN)
                end_capture(app);
            return;
        }
        // Delete clears a binding column (the keyboard's Select, Tab, switches sections).
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_DELETE && !e.key.repeat && !focus_tabs_ &&
            sel_ >= 0 && sel_ < int(rows_.size()) && rows_[sel_].enabled && rows_[sel_].clear) {
            rows_[sel_].clear();
            return;
        }
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_TAB && !e.key.repeat) {
            switch_tab(app, (e.key.mod & SDL_KMOD_SHIFT) ? -1 : 1);
            return;
        }
        if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            float dy = e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f);
            scroll_ -= dy * lay_.row_h;
            follow_sel_ = false;
            return;
        }
        switch (gesture_.feed(e)) {
        case Gesture::Result::Down: {
            const float x = gesture_.x(), y = gesture_.y();
            press_ = Press{};
            if (lay_.back.contains(x, y)) press_.back = true;
            else if (int t = tab_at(x, y); t >= 0) press_.tab = t;
            else if (lay_.list.contains(x, y)) {
                press_.in_list = true;
                press_.scroll = scroll_;
                int i = row_at(y);
                if (i >= 0 && rows_[i].selectable()) {
                    press_.row = i;
                    Row& r = rows_[i];
                    // Sliders follow the finger straight away.
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
            if (dragging_ >= 0 && dragging_ < int(rows_.size()) && rows_[dragging_].set_frac && rows_[dragging_].enabled)
                rows_[dragging_].set_frac((gesture_.x() - lay_.val_x) / lay_.slider_w);
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
            bool was_slider = dragging_ >= 0;
            dragging_ = -1;
            if (p.back && lay_.back.contains(gesture_.x(), gesture_.y())) {
                leave(app);
                app.pop();
                return;
            }
            if (p.tab >= 0) {
                if (p.tab != tab_) switch_tab(app, p.tab - tab_);
                focus_tabs_ = false;
                return;
            }
            if (p.row < 0 || p.row >= int(rows_.size()) || was_slider) return;
            sel_ = p.row;
            focus_tabs_ = false;
            Row& r = rows_[p.row];
            if (!r.enabled) return;
            const float x = gesture_.start_x();
            const bool on_value = x >= lay_.val_x - 12;
            switch (r.kind) {
            case Row::Kind::Slider:
                if (on_value && r.set_frac) r.set_frac((x - lay_.val_x) / lay_.slider_w);
                break;
            case Row::Kind::Choice:
                if (on_value && r.change) r.change(x < lay_.val_x + lay_.val_w / 2 ? -1 : 1);
                else if (r.activate) r.activate();
                break;
            case Row::Kind::Toggle:
                if (on_value && r.change) r.change(1); // the switch
                break;
            case Row::Kind::Action:
                if (r.activate) r.activate();
                break;
            case Row::Kind::Binding:
                if (on_value) column_ = x < key_column_x() ? 0 : 1;
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
        build(app);
        // A test sound on the new output device once a switch has landed (sound §1.1).
        if (const SDL_AudioDeviceID d = app.audio().output_device_id(); d != heard_device_) {
            if (heard_device_ && tab_ == kAudio) play_se(app, "cursor");
            heard_device_ = d;
        }
        note_left_ -= dt;
        if (rumble_left_ > 0) { // Test rumble
            rumble_left_ -= dt;
            app.input().set_rumble(rumble_player_, rumble_left_ > 0 ? PadRumble{true, 200} : PadRumble{});
        }
        if (capture_.state != Capture::State::Idle) {
            update_capture(app, dt);
            return;
        }
        const MenuInput& m = app.menu();
        if (confirm_.active) {
            update_confirm(m);
            return;
        }
        if (m.pressed(PadButton::Triangle)) {
            leave(app);
            app.pop();
            return;
        }
        if (m.pressed(PadButton::L1)) switch_tab(app, -1);
        if (m.pressed(PadButton::R1)) switch_tab(app, 1);

        if (focus_tabs_) {
            if (int h = m.horizontal()) switch_tab(app, h);
            if (m.vertical() > 0 || m.pressed(PadButton::Cross)) {
                focus_tabs_ = false;
                sel_ = first_selectable();
                follow_sel_ = true;
            }
            return;
        }
        if (int v = m.vertical()) {
            move(v);
            follow_sel_ = true;
        }
        if (sel_ < 0 || sel_ >= int(rows_.size())) return;
        Row& r = rows_[sel_];
        if (!r.enabled) return;
        if (int h = m.horizontal(); h && r.change) r.change(h);
        if (m.pressed(PadButton::Cross) && r.activate) r.activate();
        if (m.pressed(PadButton::Square) && r.remove) r.remove();
        if (m.pressed(PadButton::Square) && r.append) r.append();
        if (m.pressed(PadButton::Select) && r.clear) r.clear();
    }

    void render(App& app, Ui& ui) override {
        compute_layout(ui);
        const UiRect& S = ui.safe();
        const bool touch = ui.touch();

        // Top bar: back button, title, keyboard hint.
        ui.fill(0, 0, ui.width(), lay_.bar_bottom, colors::kPanel);
        ui.button(lay_.back, back_label(), false, press_.back && gesture_.active());
        ui.text(lay_.back.right() + 12, std::floor(lay_.back.y + (lay_.back.h - Ui::kChar) / 2), "SETTINGS", colors::kAccent);
        if (!touch)
            ui.text_right(S.right() - 8, std::floor(lay_.back.y + (lay_.back.h - Ui::kChar) / 2), "L1/R1 or Tab: section",
                          colors::kDim);

        // Tab bar.
        for (int t = 0; t < kTabCount; ++t)
            ui.tab(lay_.tabs[t].x, lay_.tabs[t].y, lay_.tabs[t].h, tab_name(t), t == tab_, focus_tabs_ && t == tab_);
        ui.fill(0, lay_.tab_bottom, ui.width(), 1, colors::kOutline);

        // Rows (scrollable).
        const int n = int(rows_.size());
        const UiRect& L = lay_.list;
        const float content = n ? row_top(n) : 0;
        if (follow_sel_ && sel_ >= 0 && sel_ < n && !focus_tabs_) {
            float top = row_top(sel_), bot = top + row_h(sel_);
            // Keep a heading visible with its first row.
            if (sel_ > 0 && rows_[sel_ - 1].kind == Row::Kind::Heading) top = row_top(sel_ - 1);
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
            draw_row(app, ui, rows_[i], y, row_h(i), i == sel_ && !focus_tabs_, i == press_.row && gesture_.active());
        }
        SDL_SetRenderClipRect(ui.renderer(), nullptr);
        ui.scrollbar({L.x, L.y, S.right() - L.x - 1, L.h}, content, scroll_);

        // Description of the highlighted option.
        const float dy = lay_.desc_y;
        ui.fill(0, dy, ui.width(), ui.height() - dy, Color{22, 24, 31});
        ui.fill(0, dy, ui.width(), 1, colors::kOutline);
        std::string desc = note_left_ > 0 ? note_
                         : focus_tabs_         ? "Left/Right: switch section    Down: options"
                         : sel_ >= 0 && sel_ < n ? rows_[sel_].desc
                                                 : "";
        std::vector<std::string> lines = Ui::wrap(desc, S.w - 16);
        if (lines.size() > 2) {
            for (size_t k = 2; k < lines.size(); ++k) lines[1] += " " + lines[k];
            lines.resize(2);
            lines[1] = Ui::clip(lines[1], S.w - 16);
        }
        for (size_t i = 0; i < lines.size(); ++i) ui.text(S.x + 8, dy + 4 + float(i) * 11, lines[i], colors::kText);
        if (!touch) ui.footer(footer_hint());
        if (capture_.state != Capture::State::Idle) draw_capture(ui);
        if (confirm_.active) draw_confirm(ui);
    }

    TouchLayout touch_layout() const override { return TouchLayout::None; } // tap and drag work directly
    bool adaptive_ui() const override { return true; }
    // Capturing a binding: no menu input at all (the press is the binding's).
    bool wants_menu_input() const override { return capture_.state == Capture::State::Idle; }
    bool menu_input_any_device() const override { return true; } // any controller choice can be undone

private:
    // Layout, recomputed each frame from the Ui's size (pointer handling uses
    // the last frame's).
    struct Layout {
        UiRect back, list;
        UiRect tabs[kTabCount];
        float bar_bottom = 20, tab_bottom = 40, desc_y = 318;
        float row_h = 18, heading_h = 18, preview_h = 110;
        float label_x = 24, val_x = 330, val_w = 288, slider_w = 200;
        bool short_names = false;
    };
    struct Press {
        bool back = false, in_list = false;
        int tab = -1, row = -1;
        float scroll = 0;
    };

    const char* tab_name(int t) const { return lay_.short_names && t == kFfb ? "Feedback" : kTabNames[t]; }

    void compute_layout(const Ui& ui) {
        const UiRect& S = ui.safe();
        const bool touch = ui.touch();
        Layout& l = lay_;
        const float bar_h = touch ? 30.0f : 20.0f;
        l.bar_bottom = S.y + bar_h;
        l.back = {S.x + 6, S.y + (touch ? 3 : 2), std::max(touch ? 72.0f : 64.0f, ui.tab_width(back_label())),
                  bar_h - (touch ? 6 : 4)};
        const float th = touch ? 28.0f : 16.0f, ty = l.bar_bottom + 4;
        float total = 0;
        l.short_names = false;
        for (int pass = 0; pass < 2; ++pass) {
            total = 0;
            for (int t = 0; t < kTabCount; ++t) total += ui.tab_width(tab_name(t)) + 4;
            if (total <= S.w - 8) break;
            l.short_names = true;
        }
        float tx = S.x + 6;
        for (int t = 0; t < kTabCount; ++t) {
            float w = ui.tab_width(tab_name(t));
            l.tabs[t] = {tx, ty, w, th};
            tx += w + 4;
        }
        l.tab_bottom = ty + th + 2;
        const float desc_h = 26, footer_h = touch ? 0.0f : 16.0f;
        l.desc_y = ui.height() - footer_h - desc_h - (touch ? ui.height() - S.bottom() : 0);
        l.list = {S.x, l.tab_bottom + 3, S.w, l.desc_y - l.tab_bottom - 4};
        l.row_h = touch ? 32.0f : 18.0f;
        l.heading_h = touch ? 26.0f : 18.0f;
        l.preview_h = touch ? 104.0f : 96.0f;
        l.label_x = S.x + (touch ? 18 : 16);
        l.val_x = std::floor(S.x + S.w * (touch ? 0.5f : 0.5f));
        l.val_w = S.right() - 16 - l.val_x;
        l.slider_w = std::max(60.0f, l.val_w - (touch ? 64 : 88));
        if (touch) l.val_x += 8, l.slider_w -= 8, l.val_w -= 8; // room for the slider knob
    }

    float row_h(int i) const {
        switch (rows_[i].kind) {
        case Row::Kind::Heading: return lay_.heading_h;
        case Row::Kind::Preview: return lay_.preview_h;
        default: return lay_.row_h;
        }
    }
    float row_top(int i) const { // offset of row i from the top of the list
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
        for (int t = 0; t < kTabCount; ++t) {
            UiRect r = lay_.tabs[t];
            r.y -= 2, r.h += 4; // a little slack for fingers
            if (r.contains(x, y)) return t;
        }
        return -1;
    }

    // --- row builders ------------------------------------------------------

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

    // Toggles, choices and sliders change with Left/Right or a tap on their switch, arrows or track;
    // Cross never changes a setting (a stray press once flipped Analog response).
    Row& toggle(std::string label, std::string desc, bool on, std::function<void(bool)> set) {
        Row& r = add(Row::Kind::Toggle, std::move(label), std::move(desc));
        r.on = on;
        r.change = [set, on](int) { set(!on); };
        return r;
    }

    // `names` are shown; `idx` is the current one. Clamped (not wrapping).
    Row& choice(std::string label, std::string desc, std::vector<std::string> names, int idx,
                std::function<void(int)> set) {
        Row& r = add(Row::Kind::Choice, std::move(label), std::move(desc));
        int n = int(names.size());
        r.value = idx >= 0 && idx < n ? names[idx] : "?";
        r.change = [set, idx, n](int d) {
            int to = std::clamp(idx + d, 0, n - 1);
            if (to != idx) set(to);
        };
        return r;
    }

    Row& slider(std::string label, std::string desc, float v, float lo, float hi, float step, std::string text,
                std::function<void(float)> set) {
        Row& r = add(Row::Kind::Slider, std::move(label), std::move(desc));
        r.frac = (v - lo) / (hi - lo);
        r.value = std::move(text);
        r.change = [=](int d) {
            float to = snap(v + d * step, lo, hi, step);
            if (to != v) set(to);
        };
        r.set_frac = [=](float f) {
            float to = snap(lo + std::clamp(f, 0.0f, 1.0f) * (hi - lo), lo, hi, step);
            if (to != v) set(to);
        };
        return r;
    }

    Row& action(std::string label, std::string desc, std::string value, std::function<void()> fn) {
        Row& r = add(Row::Kind::Action, std::move(label), std::move(desc));
        r.value = std::move(value);
        r.activate = std::move(fn);
        return r;
    }

    // --- pages ---------------------------------------------------------------

    void build(App& app) {
        rows_.clear();
        switch (tab_) {
        case kDisplay: build_display(app); break;
        case kAudio: build_audio(app); break;
        case kControls: build_controls(app); break;
        case kFfb: build_ffb(app); break;
        case kTouch: build_touch(app); break;
        case kDisc: build_disc(app); break;
        case kOnline: build_online(app); break;
#if GT3R_XR
        case kVr: build_vr(app); break;
#endif
        default: break;
        }
        if (sel_ < 0 || sel_ >= int(rows_.size()) || !rows_[sel_].selectable()) sel_ = first_selectable();
    }

    void build_display(App& app) {
        App* a = &app;
        IniDocument& cfg = app.config();
        if (!platform::is_mobile())
            toggle("Fullscreen", "Fill the whole screen; off: a resizable window. F11 also toggles.", app.fullscreen(),
                   [a](bool v) { a->set_fullscreen(v); });
        else
            info("Fullscreen", "Always on this device");

        // [video] aspect: the game's screens (menus, races, movies); ours keep their own layout.
        // Not in VR: the headset's virtual screen is 16:9 and the stereo views have its own FOV.
        bool vr = false;
#if GT3R_XR
        vr = app.xr() != nullptr;
#endif
        if (!vr)
            choice("Aspect ratio",
                   "The shape of the game's picture. 4:3: like on a 4:3 TV. Fill screen: races use the whole "
                   "screen; menus and movies stay 4:3.",
                   {"16:9", "4:3", "Fill screen"}, int(app.aspect()),
                   [a](int i) { a->set_aspect(platform::Aspect(i)); });

        bool vsync = cfg.get("video", "vsync").value_or("1") != "0";
        toggle("VSync", "Wait for the display refresh: no tearing. Off: lower input latency.", vsync, [a](bool v) {
            a->config().set("video", "vsync", v ? "1" : "0");
            SDL_SetRenderVSync(a->renderer(), v ? 1 : 0);
            a->save_config();
        });
        toggle("Limit to 60 fps",
               "On 120 Hz and faster displays, draw about 60 frames a second (the game runs at 60 Hz). "
               "Steadier when the computer is busy.",
               app.limit_60fps(), [a](bool v) { a->set_limit_60fps(v); });

        // [race] units: the disc's own (its language's: mph on the North American disc, km/h on
        // the European and Japanese ones) unless one is picked; menus and races follow it.
        const std::optional<bool> unit = speed_unit();
        std::string disc_unit = "Disc default";
        if (app.vfs()) disc_unit += language_units(app.vfs()->language()).mph ? " (mph)" : " (km/h)";
        choice("Speed units",
               "Speeds in races and menus; distances follow (miles or km). Disc default: mph on the North "
               "American disc, km/h on the European and Japanese ones.",
               {disc_unit, "km/h", "mph"}, unit ? (*unit ? 2 : 1) : 0, [a](int i) {
                   if (i == 0) a->config().remove("race", "units");
                   else a->config().set("race", "units", i == 2 ? "mph" : "kmh");
                   set_speed_unit(i == 0 ? std::nullopt : std::optional<bool>(i == 2));
                   a->save_config();
               });

        // The levels this GPU offers (Mali: off, 4x, 8x), the one in use selected.
        const std::vector<int> levels = app.gpu() ? app.gpu()->msaa_levels() : std::vector<int>{1, 2, 4, 8};
        const int in_use = app.gpu() ? app.gpu()->msaa()
                           : cfg.get("video", "msaa") ? std::atoi(cfg.get("video", "msaa")->c_str())
                                                      : platform::default_msaa(platform::is_mobile());
        int idx = 0;
        std::vector<std::string> names;
        for (size_t i = 0; i < levels.size(); ++i) {
            names.push_back(levels[i] > 1 ? std::to_string(levels[i]) + "x" : "Off");
            if (in_use >= levels[i]) idx = int(i);
        }
        Row& msaa = choice("Anti-aliasing (MSAA)", "Smooths jagged edges of 3D models. Higher costs more GPU time.", names, idx,
                           [a, levels](int i) {
                               a->config().set("video", "msaa", std::to_string(levels[i]));
                               if (a->gpu()) a->gpu()->set_msaa(levels[i]);
                               a->save_config();
                           });
        msaa.enabled = app.gpu() != nullptr;
        if (!msaa.enabled) msaa.desc = "Needs the 3D renderer (turn it on and restart).";

        static const int kAniso[] = {1, 2, 4, 8, 16};
        const int aniso = app.gpu() ? app.gpu()->anisotropy()
                          : cfg.get("video", "anisotropy") ? std::atoi(cfg.get("video", "anisotropy")->c_str())
                                                           : 4;
        int aidx = 0;
        for (int i = 0; i < 5; ++i)
            if (aniso >= kAniso[i]) aidx = i;
        Row& af = choice("Anisotropic filtering",
                         "Keeps textures sharp at shallow angles, like the road ahead. Higher costs a little more GPU time.",
                         {"Off", "2x", "4x", "8x", "16x"}, aidx, [a](int i) {
                             a->config().set("video", "anisotropy", std::to_string(kAniso[i]));
                             if (a->gpu()) a->gpu()->set_anisotropy(kAniso[i]);
                             a->save_config();
                         });
        af.enabled = msaa.enabled && app.gpu()->anisotropy_supported();
        if (!msaa.enabled) af.desc = msaa.desc;
        else if (!af.enabled) af.desc = "This GPU has no anisotropic filtering.";

        // Auto sits among the fixed sizes at the scale it picks, so Left is always lower.
        int win_w = 0, win_h = 0;
        SDL_GetWindowSizeInPixels(app.window(), &win_w, &win_h);
        const float auto_scale = platform::auto_render_scale(win_w, win_h, platform::is_mobile());
        // With dynamic resolution, Auto climbs to the screen's full resolution when the GPU keeps up.
        const float auto_top = app.dynamic_resolution() ? std::max(auto_scale, platform::dynamic_ceiling(win_w, win_h))
                                                        : auto_scale;
        // Above 100% (supersampling): the sizes this screen allows (platform::supersample_limit).
        static const char* const kScales[] = {"0.5", "0.6", "0.7", "0.75", "0.8", "0.9",
                                              "1.0", "1.25", "1.5", "2.0", "3.0", "4.0"};
        const float limit = platform::supersample_limit(float(win_w), float(win_h));
        std::vector<std::string> svalues, snames;
        for (const char* v : kScales) {
            const float f = std::strtof(v, nullptr);
            if (f > limit + 1e-3f) break;
            if (auto_scale <= f && std::find(svalues.begin(), svalues.end(), "auto") == svalues.end())
                svalues.push_back("auto"), snames.push_back(auto_top > auto_scale + 0.001f
                                                                ? "Auto (up to " + percent(auto_top) + ")"
                                                                : "Auto (" + percent(auto_scale) + ")");
            svalues.push_back(v), snames.push_back(percent(f));
        }
        std::optional<float> rs = platform::parse_render_scale(cfg.get("video", "render_scale").value_or("auto"));
        int sidx = int(std::find(svalues.begin(), svalues.end(), "auto") - svalues.begin());
        if (rs) {
            float best = 9;
            for (int i = 0; i < int(svalues.size()); ++i)
                if (svalues[size_t(i)] != "auto")
                    if (float d = std::fabs(std::strtof(svalues[size_t(i)].c_str(), nullptr) - *rs); d < best)
                        best = d, sidx = i;
        }
        std::string scale_desc = "Resolution of 3D views vs. the screen. Lower is faster on phones.";
        if (limit > 1.0f)
            scale_desc += " Above 100% renders larger and scales down: smoother edges and sharper textures for much "
                          "more GPU time and memory.";
        Row& scale = choice("3D resolution", scale_desc, snames, sidx, [a, svalues](int i) {
            a->config().set("video", "render_scale", svalues[size_t(i)]);
            a->apply_render_scale();
            a->save_config();
        });
        scale.enabled = app.gpu() != nullptr;
        if (!scale.enabled) scale.desc = msaa.desc;

        std::string dyn_desc = "Lowers the 3D resolution in heavy scenes (to 50% at most) to keep the frame rate and "
                               "raises it while the GPU keeps up: to 3D resolution above, or with Auto the screen's "
                               "full resolution.";
        if (const render::DynamicScale* d = app.dynamic_scale()) dyn_desc += " Now " + percent(d->scale()) + ".";
        Row& dyn = toggle("Dynamic resolution", dyn_desc, app.dynamic_resolution(),
                          [a](bool v) { a->set_dynamic_resolution(v); });
        dyn.enabled = scale.enabled;
        if (!dyn.enabled) dyn.desc = msaa.desc;

        Row& detail = toggle("Always show full car detail",
                             "Every car with its sharpest model and textures, however far away. Off: distant cars "
                             "switch to GT3's simpler models.",
                             app.full_car_detail(), [a](bool v) {
                                 a->config().set("video", "full_car_detail", v ? "on" : "off");
                                 a->save_config();
                             });
        detail.enabled = scale.enabled;
        if (!detail.enabled) detail.desc = msaa.desc;

        bool gpu_cfg = cfg.get("video", "gpu").value_or("1") != "0";
        Row& gpu = toggle("3D renderer (SDL_GPU)", "Off: 2D-only fallback for systems without a working GPU driver.",
                          gpu_cfg, [a](bool v) {
                              a->config().set("video", "gpu", v ? "1" : "0");
                              a->save_config();
                          });
        gpu.restart = gpu_cfg != (app.gpu() != nullptr);
        if (gpu.restart) gpu.desc = "Takes effect after restarting GT3 Revisited.";
        if (app.gpu()) info("Graphics driver", app.gpu()->driver_name());
        info("2D renderer", SDL_GetRendererName(app.renderer()));
    }

#if GT3R_XR
    // [vr]: the headset (xr::Settings).
    void build_vr(App& app) {
        App* a = &app;
        xr::Session* x = app.xr();
        if (!x) {
            info("Headset", "Not in use", "No VR headset found when the game started; it runs in a window.");
            return;
        }
        const xr::Settings s = x->settings();
        auto apply = [a](const xr::Settings& n) { a->set_xr_settings(n); };
        toggle("Races in 3D", "Drive in stereo from the car's seat or behind it. Off: races on the screen, like menus.",
               s.stereo, [apply, s](bool v) {
                   xr::Settings n = s;
                   n.stereo = v;
                   apply(n);
               });
        toggle("Level horizon", "In-car view: follow the car's heading only, not its pitch and roll (gentler).",
               s.level_horizon, [apply, s](bool v) {
                   xr::Settings n = s;
                   n.level_horizon = v;
                   apply(n);
               });
        toggle("Car body in the in-car view", "See your car's bonnet and body around you. Off: GT3's camera alone.",
               s.show_car, [apply, s](bool v) {
                   xr::Settings n = s;
                   n.show_car = v;
                   apply(n);
               });
        choice("Steering", "Wheel: squeeze both grips and turn the controllers like a steering wheel.",
               {"Thumbstick", "Wheel"}, s.wheel ? 1 : 0, [apply, s](int i) {
                   xr::Settings n = s;
                   n.wheel = i == 1;
                   apply(n);
               });
        static const int kDegrees[] = {180, 270, 360, 540, 900};
        int di = 0;
        for (int i = 0; i < 5; ++i)
            if (s.wheel_degrees >= kDegrees[i]) di = i;
        choice("Wheel rotation", "Degrees from full left to full right lock.", {"180", "270", "360", "540", "900"},
               di, [apply, s](int i) {
                   xr::Settings n = s;
                   n.wheel_degrees = kDegrees[i];
                   apply(n);
               }).enabled = s.wheel;
        auto metres = [](float v) {
            char b[16];
            std::snprintf(b, sizeof(b), "%.1f m", double(v));
            return std::string(b);
        };
        slider("Screen size", "Width of the virtual screen for menus and replays.", s.screen_width, 1.2f, 6.0f, 0.2f,
               metres(s.screen_width), [apply, s](float v) {
                   xr::Settings n = s;
                   n.screen_width = v;
                   apply(n);
               });
        slider("Screen distance", "How far away the virtual screen is.", s.screen_distance, 1.0f, 6.0f, 0.2f,
               metres(s.screen_distance), [apply, s](float v) {
                   xr::Settings n = s;
                   n.screen_distance = v;
                   apply(n);
               });
        slider("HUD size", "Width of the race display panel in front of the car.", s.hud_width, 0.6f, 3.0f, 0.1f,
               metres(s.hud_width), [apply, s](float v) {
                   xr::Settings n = s;
                   n.hud_width = v;
                   apply(n);
               });
        slider("HUD distance", "How far ahead the race display panel floats.", s.hud_distance, 0.6f, 3.0f, 0.1f,
               metres(s.hud_distance), [apply, s](float v) {
                   xr::Settings n = s;
                   n.hud_distance = v;
                   apply(n);
               });
        const std::vector<float>& rates = x->refresh_rates();
        if (!rates.empty()) {
            std::vector<std::string> names;
            int ri = 0;
            for (size_t i = 0; i < rates.size(); ++i) {
                names.push_back(std::to_string(int(std::lround(rates[i]))) + " Hz");
                if (std::fabs(rates[i] - float(s.refresh_hz)) < std::fabs(rates[size_t(ri)] - float(s.refresh_hz))) ri = int(i);
            }
            std::vector<float> rv = rates;
            choice("Refresh rate", "Higher is smoother and needs more from the headset.", names, ri, [apply, s, rv](int i) {
                xr::Settings n = s;
                n.refresh_hz = int(std::lround(rv[size_t(i)]));
                apply(n);
            });
        }
        static const float kScales[] = {0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f};
        int si = 0;
        for (int i = 0; i < 6; ++i)
            if (std::fabs(kScales[i] - s.eye_scale) < std::fabs(kScales[si] - s.eye_scale)) si = i;
        Row& res = choice("3D resolution", "Of the headset's recommended size. Takes effect after a restart.",
                          {"70%", "80%", "90%", "100%", "110%", "120%"}, si, [apply, s](int i) {
                              xr::Settings n = s;
                              n.eye_scale = kScales[i];
                              apply(n);
                          });
        (void)res;
        action("Recentre view", "Puts the seat and the screen where you are looking now.", "", [x] { x->recenter(); });
        info("Eyes", std::to_string(x->eye(0).width) + " x " + std::to_string(x->eye(0).height) + " pixels");
    }
#endif

    void build_audio(App& app) {
        App* a = &app;
        AudioEngine& au = app.audio();
        bool disc = app.vfs() != nullptr;
        // Phones and headsets send the sound where the system says (SDL lists only the
        // system device there).
        if (platform::is_mobile()) {
            info("Output device", "Chosen by the system");
        } else if (au.ok()) {
            std::vector<std::string> devs = {""}; // "": the system default
            for (const std::string& d : AudioEngine::playback_devices()) devs.push_back(d);
            const std::string& want = au.output_device();
            int idx = int(std::find(devs.begin(), devs.end(), want) - devs.begin());
            const bool missing = idx == int(devs.size());
            if (missing) devs.push_back(want); // remembered device that isn't connected right now
            std::vector<std::string> shown = devs;
            const char* def = SDL_GetAudioDeviceName(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
            shown[0] = def && *def ? std::string("System default: ") + def : "System default";
            if (missing) shown[size_t(idx)] += " (not connected)";
            choice("Output device",
                   missing ? "Not connected: playing on the system default until it is."
                           : "Speakers or headphones for all sound. System default follows the system's choice.",
                   shown, idx, [a, devs](int i) {
                       a->audio().set_output_device(devs[size_t(i)]);
                       a->save_config();
                   });
        }
        slider("Music volume",
               disc ? "Soundtrack and menu music. Changing it plays a preview track." : "Soundtrack and menu music.",
               au.music_volume(), 0, 1, 0.05f, percent(au.music_volume()), [this, a](float v) {
                   a->audio().set_music_volume(v);
                   start_preview(*a);
                   a->save_config();
               });
        slider("Sound effects", "Engines, tyres, crashes and menu sounds.", au.sfx_volume(), 0, 1, 0.05f,
               percent(au.sfx_volume()), [this, a](float v) {
                   a->audio().set_sfx_volume(v);
                   play_se(*a, "cursor");
                   a->save_config();
               });
        toggle("Surround cue", "GT3's rear-speaker phase trick for Pro Logic decoders. Off for stereo.",
               au.voices().surround_cue(), [a](bool v) {
                   a->audio().voices().set_surround_cue(v);
                   a->config().set("audio", "surround_cue", v ? "on" : "off");
                   a->save_config();
               });
        if (!au.ok()) info("Audio device", "Not available", "No audio output device was found.");
    }

    void build_controls(App& app) {
        App* a = &app;
        InputManager& in = app.input();
        for (int p = 0; p < InputManager::kMaxPlayers; ++p) {
            heading(p == 0 ? "Player 1" : "Player 2 (split screen)");
            {
                // Auto, each connected pad or wheel, a remembered one that isn't connected, Keyboard only.
                std::vector<std::string> filters = {"auto"};
                for (const std::string& d : in.device_names())
                    if (std::find(filters.begin(), filters.end(), d) == filters.end()) filters.push_back(d);
                const std::string& filter = in.device_filter(p);
                // A name from the config that isn't a connected device's whole name: kept as it is.
                const bool named = !in.keyboard_only(p) && filter != "auto" &&
                                   std::find(filters.begin(), filters.end(), filter) == filters.end();
                if (named) filters.push_back(filter);
                // Keyboard only needs a keyboard (none on a headset, a Steam Deck, a couch PC).
#if GT3R_XR
                const bool keyboard_only = false;
#else
                const bool keyboard_only = SDL_HasKeyboard();
#endif
                if (keyboard_only || in.keyboard_only(p)) filters.push_back(InputManager::kKeyboardOnly);
                int idx = 0;
                for (size_t i = 0; i < filters.size(); ++i)
                    if (filters[i] == filter) idx = int(i);
                std::vector<std::string> shown = filters;
                shown.front() = "Auto: " + in.device_name(p);
                if (filters.back() == InputManager::kKeyboardOnly) shown.back() = "Keyboard only";
                if (named && in.kind(p) == DeviceKind::Keyboard)
                    for (std::string& s : shown)
                        if (s == filter) s += " (not connected)";
                choice("Controller",
                       "Left/Right. Auto takes a connected steering wheel, otherwise the first free controller. The "
                       "buttons below follow the device in use (controller, wheel or keyboard).",
                       shown, idx, [a, p, filters](int i) {
                           a->input().set_device_filter(p, filters[size_t(i)]);
                           a->save_config();
                       });
            }

            {
                // Modern, GT3 original, and Custom while custom race controls are kept: choosing a
                // layout keeps them for later.
                static const char* const kLayouts[] = {"modern", "gt3"};
                const bool kept = in.has_custom(p, true), in_use = in.is_race_custom(p);
                std::vector<std::string> rnames = {"Modern", "GT3 original"};
                if (kept) rnames.push_back("Custom");
                const int ridx = in_use ? 2 : in.race_layout(p) == "gt3" ? 1 : 0;
                std::string desc = in_use ? "Custom race controls are in use (below). Modern or GT3 original keeps them "
                                            "for later."
                                          : "Modern: triggers gas/brake, Cross/Square shift, Triangle view. GT3 original: "
                                            "the race reads the menu buttons. Back paddles shift in both.";
                if (kept && !in_use) desc += " Your custom set is kept: choose Custom to use it.";
                choice("Race controls", desc, rnames, ridx, [a, p](int i) {
                    if (i == 2) a->input().use_custom(p, true);
                    else a->input().set_race_layout(p, kLayouts[i]);
                    a->save_config();
                });
            }

            if (!in.keyboard_only(p))
                toggle("Keyboard",
                       in.keyboard_fallback(p)
                           ? "Off, but no controller drives player 1, so the keyboard still does."
                           : "The keyboard also drives this player (alongside the controller).",
                       in.keyboard_enabled(p),
                       [a, p](bool v) {
                           a->input().set_keyboard(p, v);
                           a->save_config();
                       });
            binding_rows(app, p);
        }
        heading("All players");
        {
            bool gt3_views = to_lower(app.config().get("race", "views").value_or("all")) == "gt3";
            choice("Race views", "Change view cycles bumper, bumper without mirror, chase, far chase; GT3: bumper and chase.",
                   {"All four", "GT3 original"}, gt3_views ? 1 : 0, [a](int i) {
                       a->config().set("race", "views", i == 1 ? "gt3" : "all");
                       a->save_config();
                   });
            bool original = to_lower(app.config().get("race", "analog_response").value_or("linear")) == "original";
            choice("Analog response",
                   "Linear: sticks, triggers, wheels, touch and tilt steer and press exactly as far as you do. "
                   "Original: GT3's DualShock 2 curves (a dead zone in the stick's centre, pedals full from a light press).",
                   {"Linear", "Original"}, original ? 1 : 0, [a](int i) {
                       a->config().set("race", "analog_response", i == 1 ? "original" : "linear");
                       a->save_config();
                   });
            bool shake = to_lower(app.config().get("race", "camera_shake").value_or("on")) != "off";
            toggle("In-car camera shake", "GT3's in-car vibration: grows with tyre slip and speed.", shake, [a](bool v) {
                a->config().set("race", "camera_shake", v ? "on" : "off");
                a->save_config();
            });
        }
    }

    // A player's controls, inline under their settings: the race actions in driving order, then
    // the DualShock 2's buttons and sticks (the menu pad, which the game's menus read).
    void binding_rows(App& app, int p) {
        App* a = &app;
        InputManager& in = app.input();
        const std::string who = p == 0 ? "Player 1 " : "Player 2 ";
        auto button = [](PadButton b) {
            PadTarget t;
            t.kind = PadTarget::Kind::Button;
            t.button = b;
            return t;
        };
        auto axis = [](PadAxis x) {
            PadTarget t;
            t.kind = PadTarget::Kind::Axis;
            t.axis = x;
            return t;
        };
        const bool gt3 = in.race_uses_menu_pad(p);
        heading(who + "race controls").columns = true;
        for (const PadTarget& t : {button(PadButton::Cross), button(PadButton::Square), button(PadButton::R2),
                                   button(PadButton::L2), button(PadButton::Circle), button(PadButton::Triangle),
                                   button(PadButton::R1), button(PadButton::L1), button(PadButton::Start),
                                   axis(PadAxis::LX), button(PadButton::Left), button(PadButton::Right),
                                   button(PadButton::R3)}) {
            Row& r = binding(app, p, true, t);
            if (!gt3) continue;
            // GT3 original: the race reads the menu pad, so the menu button's inputs drive it.
            r.label += " (" + target_name(t) + ")";
            r.desc = "GT3 original: the race reads the menu buttons, so this is " + target_name(t) +
                     "'s. Editing it makes custom race controls from GT3 original plus the change.";
        }
        heading(who + "menu buttons").columns = true;
        {
            static const char* const kKinds[] = {"Controller layout", "Wheel layout", "Keyboard layout",
                                                 "Joystick (bind its inputs)"};
            const bool kept = in.has_custom(p, false), in_use = in.is_custom(p);
            std::vector<std::string> names = {kKinds[size_t(in.kind(p))]};
            if (kept) names.push_back("Custom");
            choice("Menu buttons",
                   in_use  ? "Custom menu buttons are in use (below). The layout keeps them for later."
                   : kept ? "The DualShock 2's buttons, as the game's menus read them. Your custom set is kept: "
                            "choose Custom to use it."
                          : "The DualShock 2's buttons, as the game's menus read them.",
                   names, in_use ? 1 : 0, [a, p](int i) {
                       if (i == 1) a->input().use_custom(p, false);
                       else a->input().reset_bindings(p);
                       a->save_config();
                   });
        }
        for (size_t i = 0; i < kPadButtonCount; ++i) binding(app, p, false, button(PadButton(i)));
        for (size_t i = 0; i < kPadAxisCount; ++i) binding(app, p, false, axis(PadAxis(i)));
    }

    static std::string target_name(const PadTarget& t) {
        static const char* const kAxes[] = {"Left stick X", "Left stick Y", "Right stick X", "Right stick Y"};
        if (t.kind == PadTarget::Kind::Axis) return kAxes[size_t(t.axis)];
        return std::string(pad_button_name(t.button));
    }

    // A control's inputs in one set, as the player's device names them ("Cross", "Right paddle").
    static std::string inputs(const InputManager& in, int player, const Profile& prof, const PadTarget& t) {
        std::string out;
        for (const Binding& b : prof.bindings) {
            const bool match = b.target.kind == t.kind &&
                               (t.kind == PadTarget::Kind::Button ? b.target.button == t.button : b.target.axis == t.axis);
            if (!match) continue;
            if (!out.empty()) out += ", ";
            if (b.target.kind == PadTarget::Kind::Axis && b.target.half != InputSource::Half::Full)
                out += b.target.half == InputSource::Half::Positive ? "+" : "-";
            out += in.describe(player, b.source);
        }
        return out;
    }

    Row& binding(App& app, int p, bool race, const PadTarget& t) {
        App* a = &app;
        InputManager& in = app.input();
        const std::string label = race ? std::string(race_action_name(t)) : target_name(t);
        Row& r = add(Row::Kind::Binding, label,
                     "Cross, then the new input: a key replaces the keyboard's, anything else the controller's "
                     "(Esc, Back or a tap cancels). Square adds one. Select clears the highlighted column (Left/Right).");
        r.player = p;
        r.race = race;
        r.target = t;
        r.value = in.kind(p) == DeviceKind::Keyboard ? "" : inputs(in, p, in.bindings(p, race, BindingSet::Controller), t);
        r.keys = inputs(in, p, in.bindings(p, race, BindingSet::Keyboard), t);
        r.activate = [a, this, p, race, t, label] {
            guard_edit(*a, p, race, [this, p, race, t, label] { start_capture(p, race, t, false, label); });
        };
        r.append = [a, this, p, race, t, label] {
            guard_edit(*a, p, race, [this, p, race, t, label] { start_capture(p, race, t, true, label); });
        };
        r.change = [this](int d) { column_ = d > 0 ? 1 : 0; };
        r.clear = [a, this, p, race, t] {
            const BindingSet set = column_ == 0 ? BindingSet::Controller : BindingSet::Keyboard;
            guard_edit(*a, p, race, [a, p, race, t, set] {
                if (race) a->input().clear_race_target(p, t, set);
                else a->input().clear_target(p, t, set);
                a->save_config();
            });
        };
        return r;
    }

    // An edit while a layout is chosen makes new custom bindings from it plus the edit. When custom
    // ones are kept, that replaces them: ask first, Cancel focused.
    void guard_edit(App& app, int p, bool race, std::function<void()> edit) {
        InputManager& in = app.input();
        const bool in_use = race ? in.is_race_custom(p) : in.is_custom(p);
        if (in_use || !in.has_custom(p, race)) {
            edit();
            return;
        }
        static const char* const kKinds[] = {"the controller layout", "the wheel layout", "the keyboard layout",
                                             "no buttons"};
        confirm_.text = race ? std::string("Replace your custom race controls with ") +
                                   (in.race_layout(p) == "gt3" ? "GT3 original" : "Modern") + " plus this change?"
                             : std::string("Replace your custom menu buttons with ") + kKinds[size_t(in.kind(p))] +
                                   " plus this change?";
        confirm_.yes = std::move(edit);
        confirm_.replace = false; // Cancel focused
        confirm_.active = true;
    }

    // The replace-Custom question: Left/Right and Cross, Triangle cancels, or a tap on a button.
    void update_confirm(const MenuInput& m) {
        if (m.horizontal()) confirm_.replace = m.horizontal() > 0;
        if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) confirm_.active = false;
        if (m.pressed(PadButton::Cross)) answer_confirm(confirm_.replace);
    }

    void answer_confirm(bool replace) {
        confirm_.active = false;
        if (replace && confirm_.yes) confirm_.yes();
        confirm_.yes = nullptr;
    }

    void draw_confirm(Ui& ui) {
        const float w = 440, h = 78, x = std::floor((ui.width() - w) / 2), y = std::floor((ui.height() - h) / 2);
        ui.fill(x, y, w, h, colors::kPanel);
        ui.frame(x, y, w, h, colors::kAccent);
        ui.text_center_in(x, w, y + 14, Ui::elide(confirm_.text, w - 16), colors::kText);
        const float bw = 120, bh = 22, by = y + h - bh - 12;
        confirm_.cancel_rect = {x + w / 2 - bw - 8, by, bw, bh};
        confirm_.replace_rect = {x + w / 2 + 8, by, bw, bh};
        ui.button(confirm_.cancel_rect, "Cancel", !confirm_.replace, false);
        ui.button(confirm_.replace_rect, "Replace", confirm_.replace, false);
    }

    // --- binding capture ---------------------------------------------------------------------

    struct Capture {
        enum class State { Idle, WaitRelease, Capturing };
        State state = State::Idle;
        int player = 0;
        bool race = false, append = false;
        PadTarget target;
        std::string what;
        double timer = 0;
    };

    void start_capture(int player, bool race, const PadTarget& t, bool append, const std::string& what) {
        capture_ = Capture{Capture::State::WaitRelease, player, race, append, t, what, 1.5};
    }

    void update_capture(App& app, double dt) {
        InputManager& in = app.input();
        capture_.timer -= dt;
        if (capture_.state == Capture::State::WaitRelease) {
            // The press that started it is let go first (on whichever device Settings reads); a
            // stick resting off centre doesn't hold it up for long.
            auto at_rest = [](const PadState& s) {
                bool rest = s.buttons == 0;
                for (u8 v : s.axes) rest &= std::abs(int(v) - 128) < 40;
                return rest;
            };
            if ((at_rest(in.any_device_pad()) && at_rest(in.pad(capture_.player))) || capture_.timer <= 0) {
                in.begin_capture(capture_.player); // another device than theirs is named on the binding
                capture_.state = Capture::State::Capturing;
                capture_.timer = 6.0;
            }
            return;
        }
        if (auto src = in.poll_capture()) {
            const bool bound = capture_.race ? in.bind_race(capture_.player, capture_.target, *src, !capture_.append)
                                             : in.bind(capture_.player, capture_.target, *src, !capture_.append);
            if (!bound) {
                note_ = capture_.race ? "A button or key can't steer both ways: bind it to Steer left or Steer right."
                                      : "A button or key can't move a stick both ways: use the D-pad rows.";
                note_left_ = 4;
            }
        } else if (capture_.timer > 0 && in.capturing()) {
            return; // still waiting (Esc or Back ends the capture: capturing() turns false)
        }
        end_capture(app);
    }

    void end_capture(App& app) {
        app.input().cancel_capture();
        capture_.state = Capture::State::Idle;
        app.menu().suppress(); // the captured (or cancelling) press isn't also a menu press
        app.save_config();
    }

    void draw_capture(Ui& ui) const {
        const float w = 400, h = 70, x = std::floor((ui.width() - w) / 2), y = std::floor((ui.height() - h) / 2);
        ui.fill(x, y, w, h, colors::kPanel);
        ui.frame(x, y, w, h, colors::kAccent);
        if (capture_.state == Capture::State::WaitRelease) {
            ui.text_center_in(x, w, y + 20, "Release all inputs...", colors::kText);
            return;
        }
        ui.text_center_in(x, w, y + 12, (capture_.append ? "Add input for " : "Press input for ") + capture_.what,
                          colors::kAccent);
        ui.text_center_in(x, w, y + 28, "Key, button, trigger, stick or wheel axis", colors::kDim);
        ui.text_center_in(x, w, y + 44,
                          "Cancels in " + std::to_string(int(std::ceil(capture_.timer))) + "s (Esc, Back or a tap: now)",
                          colors::kDim);
    }

    float key_column_x() const { return lay_.val_x + std::floor(lay_.val_w * 0.58f); }

    // Each player's rumble, then the trigger and wheel effects (all players).
    void build_ffb(App& app) {
        App* a = &app;
        InputManager& in = app.input();
        for (int p = 0; p < InputManager::kMaxPlayers; ++p) {
            heading(p == 0 ? "Player 1" : "Player 2 (split screen)");
            {
                // Auto shows what it plays on the controller in use.
                static const char* const kModes[] = {"auto", "modern", "gt3"};
                const std::string& mode = in.rumble_mode(p);
                const int midx = mode == "modern" ? 1 : mode == "gt3" ? 2 : 0;
                const char* const autos = gt3_rumble("auto", in.dualsense(p)) ? "Auto: GT3 original" : "Auto: Modern";
                choice("Rumble",
                       "Modern: wall and car hits, curbs, off-road, wheelspin and slides. GT3 original: GT3's own "
                       "rumble, which most controllers play much stronger than a DualShock 2. Auto: GT3 original on a "
                       "DualSense.",
                       {autos, "Modern", "GT3 original"}, midx, [a, p](int i) {
                           a->input().set_rumble_mode(p, kModes[i]);
                           a->save_config();
                       });
            }
            if (p == 0 && InputManager::phone_vibration_available())
                toggle("Phone vibration",
                       "Rumble on this device's own motor when the controls in use have none (built-in controls, "
                       "touch). A controller with rumble always rumbles itself.",
                       in.phone_vibration(), [a](bool v) {
                           a->input().set_phone_vibration(v);
                           a->save_config();
                       });
            action("Test rumble", "Rumbles this player's controller for half a second.", "", [this, p] {
                rumble_player_ = p;
                rumble_left_ = 0.5;
            });
        }
        heading("Triggers and wheels");
        ForceFeedbackConfig c = app.input().ffb_config();
        auto apply = [a](const ForceFeedbackConfig& n) {
            a->input().set_ffb_config(n);
            a->save_config();
        };
        toggle("Force feedback", "Master switch for trigger effects and wheel forces.", c.enabled, [apply, c](bool v) {
            ForceFeedbackConfig n = c;
            n.enabled = v;
            apply(n);
        });
        size_t base = rows_.size();
        toggle("Trigger effects", "DualSense brake resistance and ABS pulse; Xbox impulse triggers.", c.triggers,
                          [apply, c](bool v) {
                              ForceFeedbackConfig n = c;
                              n.triggers = v;
                              apply(n);
                          });
        slider("Trigger resistance", "How hard the DualSense brake trigger pushes back.", c.trigger_force, 0, 1,
                          0.05f, percent(c.trigger_force), [apply, c](float v) {
                              ForceFeedbackConfig n = c;
                              n.trigger_force = v;
                              apply(n);
                          });
        toggle("Wheel force feedback", "Steering force and centring spring on force-feedback wheels.", c.wheel,
                          [apply, c](bool v) {
                              ForceFeedbackConfig n = c;
                              n.wheel = v;
                              apply(n);
                          });
        slider("Wheel strength", "Overall wheel force (100% = default, up to 200%).", c.strength, 0, 2, 0.05f,
                          percent(c.strength), [apply, c](float v) {
                              ForceFeedbackConfig n = c;
                              n.strength = v;
                              apply(n);
                          });
        slider("Centring spring", "Self-centring spring of the wheel at speed.", c.spring, 0, 1, 0.05f,
                          percent(c.spring), [apply, c](float v) {
                              ForceFeedbackConfig n = c;
                              n.spring = v;
                              apply(n);
                          });
        toggle("Invert wheel force", "Flip the steering force if your wheel pulls the wrong way.", c.invert,
                          [apply, c](bool v) {
                              ForceFeedbackConfig n = c;
                              n.invert = v;
                              apply(n);
                          });
        // base+0 triggers, +1 resistance, +2 wheel, +3 strength, +4 spring, +5 invert.
        for (size_t i = base; i < base + 6; ++i) rows_[i].enabled = c.enabled;
        if (!c.triggers) rows_[base + 1].enabled = false;
        if (!c.wheel) rows_[base + 3].enabled = rows_[base + 4].enabled = rows_[base + 5].enabled = false;
        info("Device (player 1)", app.input().device_name(0),
             "Effects are felt while driving.");
    }

    void build_touch(App& app) {
        App* a = &app;
        TouchConfig c = app.touch().controls().config();
        auto apply = [a](const TouchConfig& n) {
            a->touch().controls().configure(n);
            a->save_config();
        };
        bool desktop = touch_hidden();
        if (desktop) info("On this computer", "Only used with a touchscreen");
        add(Row::Kind::Preview, "Preview", "");
        size_t first = rows_.size();
        const bool custom = c.customized(TouchLayout::Driving) || c.customized(TouchLayout::Menu);
        action("Customize layout...", "Drag each control where you want it, resize it with a pinch or +/-, or hide it.",
               custom ? "Custom" : "Default", [a, this] { a->push(make_touch_editor_screen(exit_to_game())); });
        choice("On-screen controls", "Auto: shown after a touch, hidden on pad or keyboard input.",
               {"Auto", "Always", "Off"}, int(c.mode), [apply, c](int i) {
                   TouchConfig n = c;
                   n.mode = TouchConfig::Mode(i);
                   apply(n);
               });
        choice("Steering", "Drag: finger on the left half. Tilt: turn the device.", {"Drag", "Tilt"}, int(c.steering),
               [apply, c](int i) {
                   TouchConfig n = c;
                   n.steering = TouchConfig::Steering(i);
                   apply(n);
               });
        choice("Pedals", "Position: higher = more pressure. Touch area: screen pressure. Full: 100%.",
               {"Position", "Touch area", "Full"}, int(c.pedals), [apply, c](int i) {
                   TouchConfig n = c;
                   n.pedals = TouchConfig::Pedals(i);
                   apply(n);
               });
        slider("Pedal minimum", "Pressure at the bottom of a pedal (position mode).", c.pedal_min, 0, 1, 0.05f,
               percent(c.pedal_min), [apply, c](float v) {
                   TouchConfig n = c;
                   n.pedal_min = v;
                   apply(n);
               });
        slider("Steering range", "Drag distance for full lock, as a share of the screen width.", c.steer_range, 0.03f,
               0.5f, 0.01f, percent(c.steer_range), [apply, c](float v) {
                   TouchConfig n = c;
                   n.steer_range = v;
                   apply(n);
               });
        slider("Tilt deadzone", "Tilt steering ignores this much turn around the centre.", c.tilt_deadzone, 0, 0.5f,
               0.01f, percent(c.tilt_deadzone), [apply, c](float v) {
                   TouchConfig n = c;
                   n.tilt_deadzone = v;
                   apply(n);
               });
        slider("Opacity", "How solid the on-screen controls are drawn.", c.opacity, 0.05f, 1, 0.05f, percent(c.opacity),
               [apply, c](float v) {
                   TouchConfig n = c;
                   n.opacity = v;
                   apply(n);
               });
        slider("Size", "Size of the on-screen controls.", c.size, 0.6f, 1.6f, 0.05f, percent(c.size),
               [apply, c](float v) {
                   TouchConfig n = c;
                   n.size = v;
                   apply(n);
               });
        toggle("Left-handed", "Mirror the layout: pedals on the left, steering on the right.", c.left_handed,
               [apply, c](bool v) {
                   TouchConfig n = c;
                   n.left_handed = v;
                   apply(n);
               });
        if (desktop)
            for (size_t i = first; i < rows_.size(); ++i) rows_[i].dim = true;
    }

    void build_disc(App& app) {
        App* a = &app;
        IniDocument& cfg = app.config();
        std::string current = app.disc() ? cfg.get("game", "disc").value_or("") : "";
        if (app.disc()) {
            const DiscInfo& d = app.disc()->info();
            info("Current disc", d.description);
            if (!d.problem.empty()) info("Disc image", "Incomplete", d.problem);
            info("", d.serial + "  v" + d.version + "  " + region_name(d.region));
            // The race physics (race_physics: the disc's own, or GT3's standing in for a program it does not know).
            std::string why;
            if (const auto exact = race::exact_program_data(*app.vfs(), &why); !exact) {
                info("Race physics", "Unavailable", "This disc's program can't be read (details in the log).");
            } else {
                const race::RacePhysics how = race::race_physics(*exact);
                if (how.own)
                    info("Race physics", race::race_core_name(how.core));
                else
                    info("Race physics", std::string(race::race_core_name(how.core)) + " (this disc's own physics isn't supported yet)",
                         "Races on this disc use Gran Turismo 3's physics: parts of its program are not like Gran Turismo 3's "
                         "(details in the log).");
            }
            info("File", platform::disc_file_name(current), "");
            // PAL: the language chosen at the first start.
            const std::vector<Language> langs = app.vfs()->region().languages();
            if (langs.size() > 1) {
                std::vector<std::string> names;
                int cur = 0;
                for (size_t i = 0; i < langs.size(); ++i) {
                    names.push_back(text::language_name(langs[i]));
                    if (langs[i] == app.vfs()->language()) cur = int(i);
                }
                choice("Language", "Menus, race screens and units follow it.", names, cur, [a, langs](int i) {
                    a->set_language(text::language_code(langs[size_t(i)]));
                });
            }
        } else {
            info("Current disc", "None");
        }
        action("Browse for a disc image...", "Pick a .iso made from your own Gran Turismo 3 A-Spec disc.", "",
               [a] { a->show_open_dialog(); });

        std::vector<std::string> recent = app.recent_discs();
        if (!recent.empty()) heading("Recent disc images");
        for (const std::string& p : recent) {
            bool is_current = !current.empty() && platform::same_disc_path(p, current);
            bool ok = disc_available(p);
            std::string where = is_current ? "In use" : ok ? p : "Missing: " + p;
            Row& r = action(platform::disc_file_name(p),
                            is_current ? "This disc is open. Square: remove from the list."
                            : ok       ? "Cross: open this disc (restarts the game). Square: remove."
                                       : "Not found (moved or drive not connected). Square: remove.",
                            where, [a, p, is_current] {
                                if (!is_current) a->request_disc(p);
                            });
            r.enabled = ok;
            r.dim = is_current;
            r.remove = [a, p] { a->forget_recent_disc(p); };
        }
        if (!app.last_error().empty() && !app.failed_disc_path().empty())
            info("Last error", app.last_error(), elide_middle(app.failed_disc_path(), 77));

        heading("Startup");
        bool hub = to_lower(cfg.get("app", "start").value_or("")) == "hub";
        choice("Start in", "Game: opening movie and title menu. Developer options: the tools list.",
               {"Game", "Developer options"}, hub ? 1 : 0, [a](int i) {
                   a->config().set("app", "start", i ? "hub" : "game");
                   a->save_config();
               });
        info("Settings file", app.data_dir() + "config.ini");
    }

    // The update check (app/updates.h) and the relay server it asks.
    void build_online(App& app) {
        App* a = &app;
        Updates& up = updates();
        heading("Updates");
        toggle("Check for updates at startup",
               "Sends one small message with this game's version and platform to the relay server when the game "
               "starts. Off: only when you choose Check now.",
               up.enabled(app), [a](bool v) { updates().set_enabled(*a, v); });
        action("Check now", "Asks the relay server for the latest release (a few seconds at most).", up.status_text(),
               [a] { updates().check_now(*a); });
        info("This version", net::this_game_version().to_string());
        info("Latest release", (up.latest().known() ? up.latest().number() : std::string("Unknown")) +
                                   (up.checked().empty() ? "" : "  (as of " + up.checked() + ")"));
        // Sessions through the relay server need it; offline play and Host / Join by address never do.
        info("Online play needs", up.min_online().known() ? "Version " + up.min_online().number() + " or later" : "Any version");
        net::UpdateLinks links = up.links();
        Row& dl = action("Download update",
                         up.prompt() == net::UpdatePrompt::None ? "This version is up to date (as far as the last check knows)."
                                                                : "Opens the update dialog: " + links.download,
                         up.prompt() == net::UpdatePrompt::None ? "Up to date" : "Version " + up.latest().number(),
                         [] { updates().open_dialog(); });
        dl.enabled = up.prompt() != net::UpdatePrompt::None;
        Row& notes = action("Release notes", links.notes.empty() ? "The relay server has not named a release page yet." : links.notes,
                            links.notes.empty() ? "Unknown" : "", [] { updates().open_notes(); });
        notes.enabled = !links.notes.empty();
        heading("Online battle");
        std::string server = app.config().get("online", "server").value_or(kDefaultServer);
        info("Relay server", server.empty() ? kDefaultServer : server,
             "Change it in the online lobby (Relay server). The update check asks the same server.");
    }

    // --- behaviour ---------------------------------------------------------

    static bool touch_hidden() { return !platform::is_mobile(); }

    int first_selectable() const {
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].selectable()) return int(i);
        return -1;
    }

    void move(int dir) {
        if (focus_tabs_) {
            if (dir > 0) {
                focus_tabs_ = false;
                sel_ = first_selectable();
            }
            return;
        }
        int n = int(rows_.size());
        for (int i = sel_ + dir; i >= 0 && i < n; i += dir)
            if (rows_[i].selectable()) {
                sel_ = i;
                return;
            }
        if (dir < 0) {
            focus_tabs_ = true; // up past the first option: the tab bar
            scroll_ = 0;
        }
    }

    void switch_tab(App& app, int d) {
        if (tab_ == kAudio) stop_preview(app);
        tab_ = (tab_ + d + kTabCount) % kTabCount;
        sel_ = -1;
        scroll_ = 0;
        follow_sel_ = true;
        dragging_ = -1;
        press_ = Press{};
        build(app);
    }

    void leave(App& app) { stop_preview(app); }

    void start_preview(App& app) {
        if (!app.vfs() || app.audio().music_playing()) return;
        AudioEngine& au = app.audio();
        // The menu music at its own level, so the Music slider is heard as in the menus.
        if (au.play_sequence(*app.vfs(), std::string("gtmenu01"), bgm::kMenuVolume) ||
            au.play_sequence(*app.vfs(), 0, bgm::kMenuVolume))
            preview_ = true;
    }

    void stop_preview(App& app) {
        if (!preview_) return;
        app.audio().stop_music();
        preview_ = false;
    }

    void play_se(App& app, const char* name) {
        if (!app.vfs()) return;
        if (!se_tried_) {
            se_tried_ = true;
            sounds_.load(*app.vfs());
        }
        sounds_.play(app.audio().voices(), name);
    }

    // Opened from the game's menus (make_settings_screen's to_game), back goes to the game, from
    // the screens opened here too: they pop themselves, then this closes Settings.
    const char* back_label() const { return to_game_ ? "< Back to game" : "< Back"; }
    std::function<void(App&)> exit_to_game() {
        if (!to_game_) return {};
        return [this](App& app) {
            leave(app);
            app.pop();
        };
    }
    std::string footer_hint() const {
        const std::string back = to_game_ ? "    Triangle/Esc: back to game" : "    Triangle/Esc: back";
        if (sel_ < 0 || sel_ >= int(rows_.size()) || focus_tabs_) return "Left/Right: section" + back;
        const Row& r = rows_[sel_];
        switch (r.kind) {
        case Row::Kind::Toggle: return "Left/Right: switch" + back;
        case Row::Kind::Choice: return "Left/Right: change" + back;
        case Row::Kind::Slider: return "Left/Right: adjust (or drag)" + back;
        case Row::Kind::Binding:
            return r.enabled ? "Cross: rebind  Square: add  Select/Del: clear  Left/Right: column" + back : back.substr(4);
        default: return (r.remove ? "Cross: open    Square: remove" : "Cross: open") + back;
        }
    }

    void draw_row(App& app, Ui& ui, const Row& r, float y, float h, bool selected, bool pressed) {
        const UiRect& S = ui.safe();
        const bool touch = ui.touch();
        const float ty = std::floor(y + (h - Ui::kChar) / 2), cy = y + h / 2;
        const float x0 = S.x + 6, x1 = S.right() - 8;
        if (r.kind == Row::Kind::Heading) {
            float hy = std::floor(y + h - 12);
            ui.text(lay_.label_x - 8, hy, r.label, colors::kAccent);
            float lx = lay_.label_x - 8 + Ui::text_width(r.label) + 8;
            ui.fill(lx, hy + 4, (r.columns ? lay_.val_x - 8 : x1) - lx, 1, colors::kOutline);
            if (r.columns) {
                ui.text(lay_.val_x, hy, "Controller", colors::kDim);
                ui.text(key_column_x() + 8, hy, "Keyboard", colors::kDim);
            }
            return;
        }
        if (r.kind == Row::Kind::Preview) {
            ui.text(lay_.label_x, y + 8, r.label, colors::kDim);
            const float px = lay_.val_x - (touch ? 40 : 60);
            float ly = y + 22;
            for (const std::string& line :
                 Ui::wrap("Driving layout with these settings and your custom layout.", px - lay_.label_x - 12))
                ui.text(lay_.label_x, ly, line, colors::kDim), ly += 11;
            const bool inside = y >= lay_.list.y - 0.5f && y + h <= lay_.list.bottom() + 0.5f;
            app.touch().draw_preview(ui, {px, y + 4, x1 - px, h - 8}, inside);
            return;
        }
        if (selected || pressed) {
            ui.fill_round(x0, y + 1, x1 - x0, h - 2, touch ? 6.0f : 3.0f, pressed ? Color{62, 70, 100} : colors::kHighlight);
            ui.fill_round(x0, y + 4, 3, h - 8, 1.5f, colors::kAccent);
        } else if (touch && r.selectable()) {
            ui.fill(x0 + 8, y + h - 1, x1 - x0 - 16, 1, Color{34, 37, 48}); // row separators
        }
        const bool grey = !r.enabled || r.dim;
        Color label = r.kind == Row::Kind::Info ? colors::kDim : grey ? colors::kDim : selected ? colors::kAccent : colors::kText;
        const float meter_w = r.kind == Row::Kind::Binding ? 60 : 0; // its live meter, left of the value
        std::string lbl = Ui::elide(r.label, lay_.val_x - lay_.label_x - 12 - meter_w);
        ui.text(lay_.label_x, ty, lbl, label);
        if (r.restart && lay_.label_x + Ui::text_width(lbl) + 16 + Ui::text_width("RESTART") < lay_.val_x)
            ui.tag(lay_.label_x + Ui::text_width(lbl) + 8, ty, "RESTART", colors::kAccent);

        const float vx = lay_.val_x, vw = lay_.val_w;
        Color val = grey ? colors::kDim : colors::kText;
        switch (r.kind) {
        case Row::Kind::Toggle: ui.toggle_pill(vx, cy, r.on, !grey); break;
        case Row::Kind::Choice: {
            Color arrow = !grey ? (selected ? colors::kAccent : colors::kText) : colors::kOutline;
            if (touch) { // tap targets for the two halves
                const float bw = 26, bh = h - 8;
                ui.fill_round(vx, y + 4, bw, bh, 5, Color{40, 43, 54});
                ui.fill_round(vx + vw - bw, y + 4, bw, bh, 5, Color{40, 43, 54});
                ui.text_center_in(vx, bw, ty, "<", arrow);
                ui.text_center_in(vx + vw - bw, bw, ty, ">", arrow);
            } else {
                ui.text(vx, ty, "<", arrow);
                ui.text_right(vx + vw, ty, ">", arrow);
            }
            ui.text_center_in(vx, vw, ty, Ui::elide(r.value, vw - (touch ? 60 : 32)), val);
            break;
        }
        case Row::Kind::Slider:
            ui.slider(vx, cy, lay_.slider_w, r.frac, selected, !grey);
            ui.text_right(vx + vw, ty, r.value, val);
            break;
        case Row::Kind::Action:
            ui.text(vx, ty, Ui::elide(r.value, vw - 16), r.enabled ? colors::kDim : colors::kBad);
            if (r.enabled && !r.dim) ui.text_right(vx + vw, ty, ">", selected ? colors::kAccent : colors::kOutline);
            break;
        case Row::Kind::Info: ui.text(vx, ty, Ui::elide(r.value, vw), colors::kText); break;
        case Row::Kind::Binding: {
            const InputManager& in = app.input();
            const PadState& pad = r.race ? in.race_pad(r.player) : in.pad(r.player);
            const float mx = vx - meter_w, mw = 44, my = std::floor(cy - 3);
            if (r.target.kind == PadTarget::Kind::Button) {
                ui.bar(mx, my, mw, 6, pad.press(r.target.button) / 255.0f, pad.down(r.target.button) ? colors::kGood : colors::kDim);
            } else {
                const float v = std::clamp(pad.axis_f(r.target.axis), -1.0f, 1.0f), c = mx + mw / 2, w = mw / 2 * v;
                ui.fill(mx, my, mw, 6, colors::kPanel);
                ui.fill(w >= 0 ? c : c + w, my, std::fabs(w), 6, colors::kAccent);
                ui.frame(mx, my, mw, 6, colors::kHighlight);
            }
            const float kx = key_column_x();
            auto column = [&](float x, float w, const std::string& text, bool active) {
                const Color c = text.empty() ? colors::kOutline : !r.enabled ? colors::kDim
                                : selected && active ? colors::kAccent : colors::kText;
                ui.text(x, ty, Ui::elide(text.empty() ? "-" : text, w), c);
            };
            column(vx, kx - vx - 8, r.value, column_ == 0);
            ui.text(kx, ty, "|", colors::kOutline);
            column(kx + 8, vx + vw - kx - 8, r.keys, column_ == 1);
            break;
        }
        default: break;
        }
    }

    std::vector<Row> rows_;
    int tab_ = 0;
    bool to_game_ = false;
    int sel_ = -1;
    float scroll_ = 0;
    bool follow_sel_ = true; // keep the selection in view (keyboard / pad); off while scrolling by touch
    int dragging_ = -1;      // slider row being dragged
    bool focus_tabs_ = false;
    bool preview_ = false;
    bool se_tried_ = false;
    SDL_AudioDeviceID heard_device_ = 0; // the output device as of the last frame
    Gesture gesture_;
    Press press_;
    Layout lay_;
    Capture capture_;    // a binding being captured (Controls)
    struct Confirm {
        bool active = false, replace = false; // replace: the focused answer (Cancel first)
        std::string text;
        std::function<void()> yes;
        UiRect cancel_rect, replace_rect;
    } confirm_;
    int column_ = 0;     // binding rows: 0 the controller's column, 1 the keyboard's
    int rumble_player_ = 0;
    double rumble_left_ = 0; // Test rumble
    std::string note_;       // shown in the description area for note_left_ seconds
    double note_left_ = 0;
    menu::flow::MenuSounds sounds_;
};


} // namespace

std::unique_ptr<Screen> make_settings_screen(int tab, bool to_game) {
    return std::make_unique<SettingsScreen>(tab, to_game);
}

GT3R_REGISTER_PLAYER_SCREEN_NO_DISC(settings, "settings", "Settings",
                             "Display, audio, controls, force feedback, touch and disc settings.", 3,
                             [] { return make_settings_screen(); });

} // namespace gt3r
