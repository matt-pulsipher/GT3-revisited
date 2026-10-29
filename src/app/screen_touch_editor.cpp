// Settings -> Touch -> Customize layout: the on-screen controls on the whole screen as in a
// race (or the game's menus), to drag where the thumbs want them, resize (pinch, the mouse
// wheel or - / +) and hide. Each finished change is applied and saved to config.ini at once
// ([touch] drive.* / menu.* keys, TouchPlacement in input/touch_controls.h).

#include "app/app.h"
#include "app/screen_touch_overlay.h"
#include "app/ui.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace gt3r {

namespace {

struct ControlName {
    const char* key;
    const char* name;
};
constexpr ControlName kNames[] = {
    {"drive.gas", "Gas pedal"},       {"drive.brake", "Brake pedal"},   {"drive.up", "Shift up"},
    {"drive.down", "Shift down"},     {"drive.handbrake", "Handbrake"}, {"drive.pause", "Pause"},
    {"drive.camera", "Camera"},       {"drive.steer", "Steering"},      {"menu.dpad", "D-pad"},
    {"menu.buttons", "Face buttons"}, {"menu.l1", "L1"},                {"menu.r1", "R1"},
    {"menu.select", "Select"},        {"menu.start", "Start"},
};

std::string_view control_name(std::string_view key) {
    for (const ControlName& n : kNames)
        if (key == n.key) return n.name;
    return key;
}

constexpr float kScaleStep = 0.1f; // - / +
constexpr float kDragStart = 6;    // units a press moves before it drags (a tap selects)
constexpr float kSnap = 5;         // a control's centre this close to the centre line snaps onto it

float snap_scale(float s) {
    return std::clamp(std::round(s * 20) / 20, TouchPlacement::kMinScale, TouchPlacement::kMaxScale);
}

Color alpha(Color c, float a) { return {c.r, c.g, c.b, u8(std::lround(std::clamp(a, 0.0f, 1.0f) * 255))}; }

class TouchEditorScreen : public Screen {
public:
    explicit TouchEditorScreen(std::function<void(App&)> to_game) : to_game_(std::move(to_game)) {}

    void update(App& app, double) override {
        init(app);
        const MenuInput& m = app.menu();
        if (m.pressed(PadButton::Triangle)) { // Esc, Android back
            const bool first = !left_;
            leave(app);
            if (first && to_game_) to_game_(app); // opened from the game's menus: straight back to them
            return;
        }
        if (m.pressed(PadButton::L1) || m.pressed(PadButton::R1))
            switch_layout(layout_ == TouchLayout::Driving ? TouchLayout::Menu : TouchLayout::Driving);
    }

    void on_event(App& app, const SDL_Event& e) override {
        init(app);
        // Fingers lifted while the app is in the background never report "up".
        if (e.type == SDL_EVENT_WILL_ENTER_BACKGROUND || e.type == SDL_EVENT_DID_ENTER_BACKGROUND ||
            e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            ptrs_.clear();
            mode_ = Mode::None;
            snapped_ = false;
            commit(app);
            return;
        }
        if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            const float steps = e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f);
            std::string_view key = ctl_.key_at(e.wheel.mouse_x, e.wheel.mouse_y);
            if (!key.empty()) sel_ = key;
            if (!sel_.empty() && steps != 0) {
                TouchPlacement p = cfg_.placement(sel_);
                p.scale = snap_scale(p.scale + (steps > 0 ? 0.05f : -0.05f));
                edit(p);
                commit(app);
            }
            return;
        }
        const PointerEvent pe = pointer_event(e);
        switch (pe.kind) {
        case PointerEvent::Kind::Down: down(pe); break;
        case PointerEvent::Kind::Move: move(pe); break;
        case PointerEvent::Kind::Up: up(app, pe); break;
        case PointerEvent::Kind::None: break;
        }
    }

    void render(App& app, Ui& ui) override {
        init(app);
        app.touch().draw_whole_window(ui, ctl_, [&](float dx, float dy) { draw(ui, dx, dy); });
    }

    TouchLayout touch_layout() const override { return TouchLayout::None; } // the controls here are the ones edited

private:
    enum Button { kDriving, kMenus, kResetLayout, kDone, kSmaller, kBigger, kHide, kResetOne };
    struct ToolButton {
        UiRect r;
        Button id;
        std::string label;
        bool primary = false;
    };
    struct Pointer {
        u64 id = 0;
        float x = 0, y = 0;
    };
    enum class Mode : u8 { None, Press, Move, Pinch, Tool, Panel, Ignore };

    void init(App& app) {
        if (ready_) return;
        ready_ = true;
        cfg_ = app.touch().controls().config();
        rebuild();
    }

    // The controls as edited, always shown and solid enough to see.
    void rebuild() {
        TouchConfig shown = cfg_;
        shown.mode = TouchConfig::Mode::On;
        shown.opacity = std::max(shown.opacity, 0.6f);
        ctl_.configure(shown);
        ctl_.set_layout(layout_);
    }

    void edit(const TouchPlacement& p) {
        cfg_.set_placement(sel_, p);
        rebuild();
        dirty_ = true;
    }

    // Applies the edited layout and saves config.ini.
    void commit(App& app) {
        if (!dirty_) return;
        dirty_ = false;
        app.touch().controls().configure(cfg_);
        app.save_config();
    }

    void leave(App& app) {
        if (left_) return;
        left_ = true;
        commit(app);
        app.pop();
    }

    void switch_layout(TouchLayout l) {
        if (l == layout_) return;
        layout_ = l;
        sel_.clear();
        rebuild();
    }

    const ToolButton* tool_at(float x, float y) const {
        if (!panel_shown_) return nullptr;
        for (const ToolButton& b : tools_)
            if (b.r.contains(x, y)) return &b;
        return nullptr;
    }

    Pointer* find(u64 id) {
        for (Pointer& p : ptrs_)
            if (p.id == id) return &p;
        return nullptr;
    }

    float spread() const { return std::hypot(ptrs_[1].x - ptrs_[0].x, ptrs_[1].y - ptrs_[0].y); }

    void down(const PointerEvent& pe) {
        std::erase_if(ptrs_, [&](const Pointer& p) { return p.id == pe.id; });
        ptrs_.push_back({pe.id, pe.x, pe.y});
        if (ptrs_.size() == 2) {
            // A second finger: pinch the selected control (also when the first finger was
            // dragging it), nothing over the toolbar.
            if (!sel_.empty() && (mode_ == Mode::Press || mode_ == Mode::Move || mode_ == Mode::None)) {
                mode_ = Mode::Pinch;
                pinch_d0_ = std::max(spread(), 8.0f);
                pinch_s0_ = cfg_.placement(sel_).scale;
            } else {
                mode_ = Mode::Ignore;
            }
            return;
        }
        if (ptrs_.size() > 2) {
            if (mode_ != Mode::Pinch) mode_ = Mode::Ignore;
            return;
        }
        start_x_ = pe.x, start_y_ = pe.y;
        if (const ToolButton* b = tool_at(pe.x, pe.y)) {
            mode_ = Mode::Tool;
            tool_ = b->id;
            return;
        }
        if (panel_shown_ && panel_.contains(pe.x, pe.y)) { // the toolbar's background moves it (never the control under it)
            mode_ = Mode::Panel;
            grab_dx_ = panel_.x - pe.x, grab_dy_ = panel_.y - pe.y;
            return;
        }
        const std::string_view key = ctl_.key_at(pe.x, pe.y);
        mode_ = Mode::Press;
        pressed_key_ = std::string(key);
        if (!key.empty()) {
            sel_ = std::string(key);
            const TouchRect b = ctl_.group_bounds(sel_);
            grab_dx_ = b.x + b.w / 2 - pe.x, grab_dy_ = b.y + b.h / 2 - pe.y;
        }
    }

    void move(const PointerEvent& pe) {
        Pointer* p = find(pe.id);
        if (!p) return;
        p->x = pe.x, p->y = pe.y;
        if (mode_ == Mode::Pinch && ptrs_.size() >= 2) {
            TouchPlacement pl = cfg_.placement(sel_);
            const float s = snap_scale(pinch_s0_ * spread() / pinch_d0_);
            if (s != pl.scale) {
                pl.scale = s;
                edit(pl);
            }
            return;
        }
        if (ptrs_.size() != 1) return;
        if (mode_ == Mode::Panel) {
            if (!panel_moved_ && std::hypot(pe.x - start_x_, pe.y - start_y_) <= kDragStart) return;
            panel_moved_ = true; // stays where it is put from now on
            panel_.x = pe.x + grab_dx_, panel_.y = pe.y + grab_dy_;
            return;
        }
        if (mode_ == Mode::Press && !pressed_key_.empty() && std::hypot(pe.x - start_x_, pe.y - start_y_) > kDragStart)
            mode_ = Mode::Move;
        if (mode_ != Mode::Move) return;
        float cx = pe.x + grab_dx_, cy = pe.y + grab_dy_;
        const TouchRect sc = ctl_.screen();
        snapped_ = std::fabs(cx - (sc.x + sc.w / 2)) < kSnap;
        if (snapped_) cx = sc.x + sc.w / 2;
        const TouchPlacement pl = ctl_.placement_at(sel_, cx, cy);
        if (pl != cfg_.placement(sel_)) edit(pl);
    }

    void up(App& app, const PointerEvent& pe) {
        Pointer* p = find(pe.id);
        if (!p) return;
        const float x = pe.x, y = pe.y;
        std::erase_if(ptrs_, [&](const Pointer& q) { return q.id == pe.id; });
        if (!ptrs_.empty()) return; // the gesture ends with its last finger
        const Mode mode = std::exchange(mode_, Mode::None);
        snapped_ = false;
        switch (mode) {
        case Mode::Tool:
            if (const ToolButton* b = tool_at(x, y); b && b->id == tool_) press_tool(app, tool_);
            break;
        case Mode::Press:
            if (pressed_key_.empty()) sel_.clear(); // a tap on nothing: deselect
            break;
        default: break;
        }
        commit(app);
    }

    void press_tool(App& app, Button id) {
        TouchPlacement p = cfg_.placement(sel_);
        switch (id) {
        case kDriving: switch_layout(TouchLayout::Driving); break;
        case kMenus: switch_layout(TouchLayout::Menu); break;
        case kResetLayout:
            cfg_.reset_placements(layout_);
            rebuild();
            dirty_ = true;
            break;
        case kDone: leave(app); break;
        case kSmaller: p.scale = snap_scale(p.scale - kScaleStep), edit(p); break;
        case kBigger: p.scale = snap_scale(p.scale + kScaleStep), edit(p); break;
        case kHide: p.hidden = !p.hidden, edit(p); break;
        case kResetOne: edit(TouchPlacement{}); break;
        }
    }

    // --- drawing (UI units; everything shifted by dx, dy) -------------------------------------

    void draw(Ui& ui, float dx, float dy) {
        const TouchRect sc = ctl_.screen();
        // A stand-in for the race: sky, ground and road over the whole screen.
        const float x0 = sc.x + dx, y0 = sc.y + dy, w = sc.w, h = sc.h, hy = dy + 360 * 0.45f;
        ui.fill(x0, y0, w, hy - y0, Color{46, 62, 84});
        ui.fill(x0, hy, w, y0 + h - hy, Color{42, 50, 40});
        const float cx = dx + 320;
        SDL_FColor road{0.2f, 0.21f, 0.23f, 1};
        SDL_Vertex v[4] = {{{cx - 4, hy}, road, {0, 0}},
                           {{cx + 4, hy}, road, {0, 0}},
                           {{cx + (y0 + h - hy) * 0.95f, y0 + h}, road, {0, 0}},
                           {{cx - (y0 + h - hy) * 0.95f, y0 + h}, road, {0, 0}}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(ui.renderer(), nullptr, v, 4, idx, 6);
        ui.frame(dx, dy, 640, 360, Color{255, 255, 255, 36}); // the game area

        // The steering zone (drag steering): the controls over it take their touches first.
        for (const TouchControls::Widget& wd : ctl_.widgets())
            if (wd.kind == TouchControls::Widget::Kind::Steering && cfg_.steering == TouchConfig::Steering::Drag)
                ui.fill(wd.x + dx, wd.y + dy, wd.w, wd.h, Color{255, 255, 255, 12});

        if (snapped_) ui.fill(sc.x + sc.w / 2 + dx - 0.5f, y0, 1, h, alpha(colors::kAccent, 0.5f));
        draw_touch_controls(ui, ctl_, dx, dy);
        draw_hidden(ui, dx, dy);
        if (!sel_.empty()) draw_selection(ui, dx, dy);

        layout_panel();
        if (panel_shown_) draw_panel(ui, dx, dy);
    }

    void draw_hidden(Ui& ui, float dx, float dy) {
        std::vector<std::string_view> done;
        for (const TouchControls::Widget& wd : ctl_.widgets()) {
            if (!wd.hidden) continue;
            const bool round = wd.shape == TouchControls::Widget::Shape::Circle;
            const float x = (round ? wd.x - wd.w : wd.x) + dx, y = (round ? wd.y - wd.h : wd.y) + dy;
            const float bw = round ? 2 * wd.w : wd.w, bh = round ? 2 * wd.h : wd.h;
            ui.fill_round(x, y, bw, bh, round ? wd.w : 6, Color{0, 0, 0, 50});
            ui.frame_round(x, y, bw, bh, round ? wd.w : 6, 1, Color{255, 255, 255, 70});
            if (std::find(done.begin(), done.end(), std::string_view(wd.key)) != done.end()) continue;
            done.push_back(wd.key);
            const TouchRect b = ctl_.group_bounds(wd.key);
            ui.text_center_in(b.x + dx - 20, b.w + 40, std::floor(b.y + b.h / 2 + dy - Ui::kChar / 2.0f), "HIDDEN",
                              Color{255, 255, 255, 140});
        }
    }

    void draw_selection(Ui& ui, float dx, float dy) {
        const TouchRect b = ctl_.group_bounds(sel_);
        if (b.w <= 0) return;
        const float x = b.x + dx - 4, y = b.y + dy - 4, w = b.w + 8, h = b.h + 8;
        ui.frame_round(x, y, w, h, 6, 1.5f, colors::kAccent);
        for (float hx : {x, x + w})
            for (float hy : {y, y + h}) ui.fill_round(hx - 3, hy - 3, 6, 6, 1.5f, colors::kAccent);
        // Size while resizing, above the control (below it near the top of the screen).
        if (mode_ == Mode::Pinch || mode_ == Mode::Move) {
            const std::string s = mode_ == Mode::Pinch ? pct(cfg_.placement(sel_).scale) : std::string(control_name(sel_));
            const float tw = Ui::text_width(s) + 12, ty = y - 18 < ctl_.screen().y + dy ? y + h + 4 : y - 18;
            ui.fill_round(x + w / 2 - tw / 2, ty, tw, 14, 4, Color{20, 20, 26, 220});
            ui.text_center_in(x + w / 2 - tw / 2, tw, ty + 3, s, colors::kAccent);
        }
    }

    static std::string pct(float s) { return std::to_string(int(std::lround(s * 100))) + "%"; }

    // The toolbar: the free spot of the game area nearest its middle. When the controls
    // leave none, the spot covering least, never most of a control (the selected one least
    // of all), so every control can still be grabbed. Once dragged by its background it
    // stays where it was put. Hidden while a control is being dragged or pinched.
    void layout_panel() {
        constexpr float kW = 344, kRow = 32, kPad = 6, kGap = 4;
        constexpr float kH = 2 * kRow + 3 * kPad;
        constexpr float kEdge = 6, kClear = 4, kStep = 4;
        panel_shown_ = mode_ != Mode::Move && mode_ != Mode::Pinch;
        if (panel_moved_) {
            const TouchRect sc = ctl_.screen();
            panel_ = {std::clamp(panel_.x, sc.x + kEdge, std::max(sc.x + kEdge, sc.x + sc.w - kEdge - kW)),
                      std::clamp(panel_.y, sc.y + kEdge, std::max(sc.y + kEdge, sc.y + sc.h - kEdge - kH)), kW, kH};
        } else {
            struct Block {
                TouchRect r;
                bool selected;
            };
            std::vector<Block> blocks;
            for (const TouchControls::Widget& w : ctl_.widgets())
                if (*w.key) blocks.push_back({ctl_.group_bounds(w.key), sel_ == w.key});
            auto cost = [&](float x, float y) {
                float c = 0;
                for (const Block& b : blocks) {
                    const float ox = std::min(x + kW + kClear, b.r.x + b.r.w) - std::max(x - kClear, b.r.x);
                    const float oy = std::min(y + kH + kClear, b.r.y + b.r.h) - std::max(y - kClear, b.r.y);
                    if (ox <= 0 || oy <= 0) continue;
                    const float area = ox * oy;
                    c += b.selected ? 4 * area : area;
                    if (area > 0.5f * b.r.w * b.r.h) c += 1e6f; // mostly hidden: hard to grab
                }
                return c;
            };
            const float cx = 320 - kW / 2, cy = 180 - kH / 2;
            float best_x = cx, best_y = cy, best = cost(cx, cy), best_d = 0;
            for (float y = kEdge; y <= 360 - kEdge - kH; y += kStep)
                for (float x = kEdge; x <= 640 - kEdge - kW; x += kStep) {
                    const float c = cost(x, y), d = std::hypot(x - cx, y - cy);
                    if (c < best - 0.5f || (std::fabs(c - best) <= 0.5f && d < best_d)) best = c, best_d = d, best_x = x, best_y = y;
                }
            panel_ = {best_x, best_y, kW, kH};
        }

        tools_.clear();
        float x = panel_.x + kPad, y = panel_.y + kPad;
        auto add = [&](float& at, float w, Button id, std::string label, bool primary = false, bool right = false) {
            UiRect r{right ? at - w : at, y, w, kRow};
            tools_.push_back({r, id, std::move(label), primary});
            at = right ? at - w - kGap : at + w + kGap;
        };
        add(x, 66, kDriving, "Driving", layout_ == TouchLayout::Driving);
        add(x, 66, kMenus, "Menus", layout_ == TouchLayout::Menu);
        float rx = panel_.right() - kPad;
        add(rx, 56, kDone, "Done", false, true);
        add(rx, 90, kResetLayout, "Reset layout", false, true);
        if (sel_.empty()) return;
        y += kRow + kPad, rx = panel_.right() - kPad;
        add(rx, 50, kResetOne, "Reset", false, true);
        add(rx, 50, kHide, cfg_.placement(sel_).hidden ? "Show" : "Hide", false, true);
        add(rx, 30, kBigger, "+", false, true);
        rx -= 42 + kGap; // the size between - and +
        add(rx, 30, kSmaller, "-", false, true);
    }

    void draw_panel(Ui& ui, float dx, float dy) {
        const UiRect p{panel_.x + dx, panel_.y + dy, panel_.w, panel_.h};
        const bool dragging = mode_ == Mode::Panel && panel_moved_;
        ui.fill_round(p.x, p.y, p.w, p.h, 8, Color{18, 20, 27, 236});
        ui.frame_round(p.x, p.y, p.w, p.h, 8, dragging ? 1.5f : 1, dragging ? colors::kAccent : colors::kOutline);
        // A grip between the tabs and the page buttons: the background drags the toolbar.
        float gx0 = p.x, gx1 = p.right();
        for (const ToolButton& b : tools_) {
            if (b.id == kMenus) gx0 = b.r.right() + dx;
            if (b.id == kResetLayout) gx1 = b.r.x + dx;
        }
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 2; ++j)
                ui.fill_circle((gx0 + gx1) / 2 + (float(i) - 1) * 5, p.y + 6 + 16 + (float(j) - 0.5f) * 5, 1.2f,
                               dragging ? colors::kAccent : colors::kOutline);
        const bool held = mode_ == Mode::Tool;
        for (const ToolButton& b : tools_) {
            const UiRect r{b.r.x + dx, b.r.y + dy, b.r.w, b.r.h};
            const bool pressed = held && tool_ == b.id && ptrs_.size() == 1 && b.r.contains(ptrs_[0].x, ptrs_[0].y);
            ui.button(r, b.label, false, pressed, b.primary);
        }
        const float row2 = p.y + 6 + 32 + 6, ty = std::floor(row2 + (32 - Ui::kChar) / 2.0f);
        if (sel_.empty()) {
            ui.text_center_in(p.x, p.w, ty,
                              ui.touch() ? "Tap a control to edit it. Drag to move, pinch to resize."
                                         : "Click a control to edit it. Drag to move, scroll to resize.",
                              colors::kDim);
            return;
        }
        ui.text(p.x + 10, ty, Ui::clip(control_name(sel_), 92), colors::kAccent);
        float x0 = 0, x1 = 0; // the size goes between - and +
        for (const ToolButton& b : tools_) {
            if (b.id == kSmaller) x0 = b.r.right();
            if (b.id == kBigger) x1 = b.r.x;
        }
        ui.text_center_in(x0 + dx, x1 - x0, ty, pct(cfg_.placement(sel_).scale), colors::kText);
    }

    TouchConfig cfg_;   // the layout being edited (mode etc. as saved)
    TouchControls ctl_; // cfg_ on the screen, always shown
    TouchLayout layout_ = TouchLayout::Driving;
    bool ready_ = false, dirty_ = false, left_ = false;
    std::function<void(App&)> to_game_; // make_touch_editor_screen (Done still returns to Settings)
    std::string sel_;           // selected control's key
    std::string pressed_key_;   // control under the press (empty: none)
    std::vector<Pointer> ptrs_; // pointers down
    Mode mode_ = Mode::None;
    Button tool_ = kDone;
    float start_x_ = 0, start_y_ = 0, grab_dx_ = 0, grab_dy_ = 0;
    float pinch_d0_ = 1, pinch_s0_ = 1;
    bool snapped_ = false;
    std::vector<ToolButton> tools_;
    UiRect panel_;
    bool panel_shown_ = true;
    bool panel_moved_ = false; // dragged by its background: no longer placed automatically
};

} // namespace

std::unique_ptr<Screen> make_touch_editor_screen(std::function<void(App&)> to_game) {
    return std::make_unique<TouchEditorScreen>(std::move(to_game));
}

} // namespace gt3r
