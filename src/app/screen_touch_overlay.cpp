#include "app/screen_touch_overlay.h"

#include "app/ui.h"
#include "base/config.h"
#include "input/input_manager.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>

namespace gt3r {

namespace {

using Widget = TouchControls::Widget;

// Translucent PS-style controls: a dark glassy body, a light rim, the
// DualShock symbol colours, amber when held (the frontend's accent).
constexpr Color kBody{14, 16, 22}, kRim{225, 230, 240}, kHot{255, 196, 40};
constexpr Color kGas{90, 210, 120}, kBrake{240, 90, 80};

Color with_alpha(Color c, float a) { return {c.r, c.g, c.b, u8(std::lround(std::clamp(a, 0.0f, 1.0f) * 255))}; }

// A line `t` units thick as a quad.
void thick_line(SDL_Renderer* r, float x0, float y0, float x1, float y1, float t, Color c, bool extend = true) {
    float dx = x1 - x0, dy = y1 - y0, len = std::hypot(dx, dy);
    if (len < 1e-4f) return;
    float nx = -dy / len * t / 2, ny = dx / len * t / 2;
    // Extend by half the thickness so joined strokes meet without notches.
    float ex = extend ? dx / len * t / 2 : 0, ey = extend ? dy / len * t / 2 : 0;
    SDL_FColor fc{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
    SDL_Vertex v[4] = {{{x0 + nx - ex, y0 + ny - ey}, fc, {0, 0}},
                       {{x0 - nx - ex, y0 - ny - ey}, fc, {0, 0}},
                       {{x1 - nx + ex, y1 - ny + ey}, fc, {0, 0}},
                       {{x1 + nx + ex, y1 + ny + ey}, fc, {0, 0}}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

void triangle(SDL_Renderer* r, float x0, float y0, float x1, float y1, float x2, float y2, Color c) {
    SDL_FColor fc{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
    SDL_Vertex v[3] = {{{x0, y0}, fc, {0, 0}}, {{x1, y1}, fc, {0, 0}}, {{x2, y2}, fc, {0, 0}}};
    SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
}

// DualShock face symbols (the debug font has no glyphs for them).
void face_symbol(Ui& ui, PadButton b, float cx, float cy, float s, float alpha) {
    SDL_Renderer* r = ui.renderer();
    const float t = std::max(1.2f, s * 0.24f);
    switch (b) {
    case PadButton::Cross: {
        Color c = with_alpha({124, 178, 240}, alpha);
        float k = s * 0.8f;
        thick_line(r, cx - k, cy - k, cx + k, cy + k, t, c);
        thick_line(r, cx - k, cy + k, cx + k, cy - k, t, c);
        break;
    }
    case PadButton::Circle: {
        Color c = with_alpha({250, 104, 104}, alpha);
        ui.frame_round(cx - s, cy - s, 2 * s, 2 * s, s, t, c);
        break;
    }
    case PadButton::Square: {
        Color c = with_alpha({236, 140, 214}, alpha);
        float k = s * 0.82f;
        ui.frame_round(cx - k, cy - k, 2 * k, 2 * k, t * 0.4f, t, c);
        break;
    }
    case PadButton::Triangle: {
        Color c = with_alpha({80, 214, 172}, alpha);
        float top = cy - s * 0.95f, bot = cy + s * 0.7f, hw = s * 0.98f;
        thick_line(r, cx, top, cx + hw, bot, t, c);
        thick_line(r, cx + hw, bot, cx - hw, bot, t, c);
        thick_line(r, cx - hw, bot, cx, top, t, c);
        break;
    }
    default: break;
    }
}

void chevron(SDL_Renderer* r, float cx, float cy, float s, bool up, Color c) {
    float d = up ? -1.0f : 1.0f, t = std::max(1.5f, s * 0.32f);
    thick_line(r, cx - s, cy - d * s * 0.45f, cx, cy + d * s * 0.45f, t, c);
    thick_line(r, cx, cy + d * s * 0.45f, cx + s, cy - d * s * 0.45f, t, c);
}

void label(Ui& ui, float cx, float cy, std::string_view s, Color c) {
    ui.text(std::round(cx - Ui::text_width(s) / 2), std::round(cy - Ui::kChar / 2.0f), s, c);
}

// A round or rounded-rect control body with its rim.
void body(Ui& ui, const Widget& w, float a, float radius) {
    const bool round = w.shape == Widget::Shape::Circle;
    const float x = round ? w.x - w.w : w.x, y = round ? w.y - w.h : w.y;
    const float bw = round ? 2 * w.w : w.w, bh = round ? 2 * w.h : w.h, rad = round ? w.w : radius;
    ui.fill_round(x, y, bw, bh, rad, with_alpha(w.active ? Color{110, 86, 20} : kBody, a * (w.active ? 0.8f : 0.55f)));
    // Soft top highlight: the glassy look of the PS buttons.
    ui.fill_round(x + bw * 0.12f, y + 1.5f, bw * 0.76f, bh * 0.38f, std::min(rad, bh * 0.19f), with_alpha(kRim, a * 0.08f));
    ui.frame_round(x, y, bw, bh, rad, 1.2f, with_alpha(w.active ? kHot : kRim, a * (w.active ? 1.0f : 0.7f)));
}

void draw_controls(Ui& ui, const TouchControls& controls, float ox, float oy) {
    SDL_Renderer* r = ui.renderer();
    const TouchConfig& cfg = controls.config();
    const float a = cfg.opacity, ink = std::min(1.0f, a * 1.7f);

    for (Widget w : controls.widgets()) {
        if (w.hidden) continue;
        w.x += ox, w.y += oy, w.origin_x += ox, w.origin_y += oy, w.knob_x += ox;
        const Color fg = with_alpha(w.active ? kHot : Color{240, 242, 246}, ink);
        switch (w.kind) {
        case Widget::Kind::Button:
            body(ui, w, a, std::min(w.w, w.h) / 2);
            if (w.shape == Widget::Shape::Rect) {
                label(ui, w.x + w.w / 2, w.y + w.h / 2, w.label, fg);
            } else if (w.label.empty()) {
                face_symbol(ui, w.button, w.x, w.y, w.w * 0.42f, ink);
            } else if (w.label == "UP" || w.label == "DOWN") {
                chevron(r, w.x, w.y - (w.label == "UP" ? 4 : -4) * w.w / 22, w.w * 0.32f, w.label == "UP", fg);
                label(ui, w.x, w.y + (w.label == "UP" ? 7 : -7) * w.w / 22, w.label == "UP" ? "+" : "-", fg);
            } else if (w.label == "||") {
                float h = w.w * 0.42f, bw = std::max(1.6f, w.w * 0.16f);
                ui.fill_round(w.x - bw * 1.6f, w.y - h / 2, bw, h, bw / 3, fg);
                ui.fill_round(w.x + bw * 0.6f, w.y - h / 2, bw, h, bw / 3, fg);
            } else {
                label(ui, w.x, w.y, w.label, fg);
            }
            break;
        case Widget::Kind::Pedal: {
            const bool gas = w.button == PadButton::Cross;
            const Color tint = gas ? kGas : kBrake;
            const float rad = std::min(10.0f, w.w * 0.18f);
            ui.fill_round(w.x, w.y, w.w, w.h, rad, with_alpha(kBody, a * 0.55f));
            ui.fill_round(w.x, w.y, w.w, w.h, rad, with_alpha(tint, a * 0.12f));
            if (w.active) {
                float fh = std::max(2 * rad, w.h * w.value);
                ui.fill_round(w.x, w.y + w.h - fh, w.w, fh, rad, with_alpha(tint, a * 0.75f));
            }
            // Rubber grip ridges, and pressure ticks along the side.
            const float gx = w.x + w.w * 0.22f, gw = w.w * 0.56f;
            for (int i = 0; i < 5; ++i) {
                float y = w.y + w.h * (0.18f + 0.11f * float(i));
                ui.fill_round(gx, y, gw, 2.5f, 1.25f, with_alpha(kRim, a * 0.35f));
            }
            for (int i = 1; i < 4; ++i) {
                float y = w.y + w.h - w.h * 0.8f * float(i) / 4.0f;
                ui.fill(w.x + 3, y, 5, 1, with_alpha(kRim, a * 0.5f));
            }
            ui.frame_round(w.x, w.y, w.w, w.h, rad, 1.4f,
                           with_alpha(w.active ? kHot : tint, std::min(1.0f, a * (w.active ? 1.6f : 1.2f))));
            label(ui, w.x + w.w / 2, w.y + w.h - 13, w.label, fg);
            break;
        }
        case Widget::Kind::DPad: {
            const float cx = w.x + w.w / 2, cy = w.y + w.h / 2, arm = w.w / 2, t = w.w * 0.18f;
            const Color fillc = with_alpha(kBody, a * 0.55f), rim = with_alpha(kRim, a * 0.7f);
            ui.fill(cx - arm, cy - t, 2 * arm, 2 * t, fillc);
            ui.fill(cx - t, cy - arm, 2 * t, arm - t, fillc);
            ui.fill(cx - t, cy + t, 2 * t, arm - t, fillc);
            // Outline of the cross as one polygon (overlapping translucent
            // shapes would show their edges through each other).
            const SDL_FPoint pts[12] = {{cx - t, cy - arm}, {cx + t, cy - arm}, {cx + t, cy - t}, {cx + arm, cy - t},
                                        {cx + arm, cy + t}, {cx + t, cy + t}, {cx + t, cy + arm}, {cx - t, cy + arm},
                                        {cx - t, cy + t},   {cx - arm, cy + t}, {cx - arm, cy - t}, {cx - t, cy - t}};
            for (int i = 0; i < 12; ++i)
                thick_line(r, pts[i].x, pts[i].y, pts[(i + 1) % 12].x, pts[(i + 1) % 12].y, 1.2f, rim, false);
            struct Arm {
                PadButton b;
                float dx, dy;
            };
            const Arm arms[] = {{PadButton::Up, 0, -1}, {PadButton::Down, 0, 1}, {PadButton::Left, -1, 0}, {PadButton::Right, 1, 0}};
            for (const Arm& ar : arms) {
                const bool on = w.active && w.button == ar.b;
                const float px = cx + ar.dx * arm * 0.62f, py = cy + ar.dy * arm * 0.62f, s = t * 0.55f;
                if (on) ui.fill_round(px - t, py - t, 2 * t, 2 * t, t * 0.35f, with_alpha(kHot, a * 0.45f));
                // Arrow pointing outwards.
                const float tx = px + ar.dx * s, ty = py + ar.dy * s;
                const float bx = px - ar.dx * s * 0.6f, by = py - ar.dy * s * 0.6f;
                triangle(r, tx, ty, bx + ar.dy * s, by - ar.dx * s, bx - ar.dy * s, by + ar.dx * s,
                         with_alpha(on ? kHot : kRim, on ? ink : a * 1.1f));
            }
            break;
        }
        case Widget::Kind::Steering: {
            const float range = cfg.steer_range * 640.0f;
            if (cfg.steering == TouchConfig::Steering::Tilt) {
                // Tilt meter along the bottom of the zone.
                const float cx = w.x + w.w / 2, y = w.y + w.h - 26;
                ui.fill_round(cx - range, y, 2 * range, 6, 3, with_alpha(kBody, a * 0.6f));
                float k = cx + w.value * range;
                ui.fill_round(std::min(cx, k), y, std::max(6.0f, std::fabs(k - cx)), 6, 3, with_alpha(kHot, a));
                ui.fill(cx - 0.5f, y - 3, 1, 12, with_alpha(kRim, a));
                label(ui, cx, y - 10, "TILT", with_alpha(kRim, ink));
            } else if (w.active) {
                const float y = w.origin_y;
                ui.fill_round(w.origin_x - range, y - 4, 2 * range, 8, 4, with_alpha(kBody, a * 0.6f));
                ui.frame_round(w.origin_x - range, y - 4, 2 * range, 8, 4, 1, with_alpha(kRim, a * 0.6f));
                float k = w.knob_x;
                ui.fill_round(std::min(w.origin_x, k), y - 2, std::fabs(k - w.origin_x), 4, 2, with_alpha(kHot, a * 0.8f));
                ui.fill(w.origin_x - 0.75f, y - 9, 1.5f, 18, with_alpha(kRim, ink));
                ui.fill_circle(k, y, 15, with_alpha(kBody, a * 0.7f));
                ui.frame_round(k - 15, y - 15, 30, 30, 15, 1.6f, with_alpha(kHot, ink));
                ui.fill_circle(k, y, 5, with_alpha(kHot, ink));
            } else {
                // Idle hint: a double arrow low in the zone.
                const float cx = w.x + w.w / 2, y = w.y + w.h * 0.66f, hw = 44;
                const Color c = with_alpha(kRim, a * 0.7f);
                thick_line(r, cx - hw + 6, y, cx + hw - 6, y, 1.4f, c);
                triangle(r, cx - hw, y, cx - hw + 9, y - 5, cx - hw + 9, y + 5, c);
                triangle(r, cx + hw, y, cx + hw - 9, y - 5, cx + hw - 9, y + 5, c);
                label(ui, cx, y + 12, "STEER", c);
            }
            break;
        }
        }
    }
}

// The letterboxed UI's window (the game area or 640x360): its corners in UI units, its cutout
// insets, and the render scale and offset that draw UI units across all of it (letterbox bars
// included).
struct WindowSpace {
    bool logical = false; // a letterboxed logical presentation is set
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    float k = 1, ox = 0, oy = 0; // window units per UI unit; UI (0, 0) drawn at (ox, oy)
    bool screen = false;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // window corners, UI units
    bool insets = false;
    float il = 0, it = 0, ir = 0, ib = 0; // cutout / rounded corners, UI units from the edges
};

WindowSpace window_space(SDL_Renderer* r, const SDL_FRect& safe_window) {
    WindowSpace w;
    SDL_FRect lr{};
    w.logical = SDL_GetRenderLogicalPresentation(r, &w.lw, &w.lh, &w.mode) && w.mode != SDL_LOGICAL_PRESENTATION_DISABLED &&
                w.lw > 0 && SDL_GetRenderLogicalPresentationRect(r, &lr) && lr.w > 0;
    if (!w.logical) return w;
    w.k = lr.w / float(w.lw);
    w.ox = lr.x / w.k, w.oy = lr.y / w.k;
    // Window corners in UI units: the same mapping touch events go through.
    int ww = 0, wh = 0;
    if (SDL_Window* win = SDL_GetRenderWindow(r); win && SDL_GetWindowSize(win, &ww, &wh) &&
        SDL_RenderCoordinatesFromWindow(r, 0, 0, &w.x0, &w.y0) &&
        SDL_RenderCoordinatesFromWindow(r, float(ww), float(wh), &w.x1, &w.y1)) {
        w.screen = true;
        // Display cutout / rounded corners, as insets from the window edges.
        float sx0 = w.x0, sy0 = w.y0, sx1 = w.x1, sy1 = w.y1;
        const SDL_FRect& sa = safe_window;
        if (sa.w > 0 && sa.h > 0 && SDL_RenderCoordinatesFromWindow(r, sa.x, sa.y, &sx0, &sy0) &&
            SDL_RenderCoordinatesFromWindow(r, sa.x + sa.w, sa.y + sa.h, &sx1, &sy1)) {
            w.insets = true;
            w.il = sx0 - w.x0, w.it = sy0 - w.y0, w.ir = w.x1 - sx1, w.ib = w.y1 - sy1;
        }
    }
    return w;
}

void lay_out(TouchControls& c, const WindowSpace& w) {
    if (w.logical) c.set_game_area(float(w.lw), float(w.lh));
    if (w.screen) c.set_screen(w.x0, w.y0, w.x1, w.y1);
    if (w.insets) c.set_insets(w.il, w.it, w.ir, w.ib);
}

// Draw in UI units across the whole window: the logical presentation (which clips to the
// game area) off while `draw` runs.
template <class F> void with_whole_window(SDL_Renderer* r, const WindowSpace& w, F&& draw) {
    if (w.logical) {
        SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        SDL_SetRenderScale(r, w.k, w.k);
    }
    draw(w.ox, w.oy);
    if (w.logical) {
        SDL_SetRenderScale(r, 1, 1);
        SDL_SetRenderLogicalPresentation(r, w.lw, w.lh, w.mode);
    }
}

} // namespace

void draw_touch_controls(Ui& ui, const TouchControls& controls, float dx, float dy) { draw_controls(ui, controls, dx, dy); }

void TouchOverlay::load(const IniDocument& config) {
    controls_.configure(TouchConfig::load(config));
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    // Phones and tablets start with the controls shown unless a pad is connected.
    if (!SDL_HasGamepad()) controls_.note_touch();
#endif
    // Screenshots on desktop: GT3R_TOUCH_SHOW=1 shows the controls as if the
    // screen had been touched (auto mode; nothing is written to config.ini).
    if (const char* v = SDL_getenv("GT3R_TOUCH_SHOW"); v && *v == '1') controls_.note_touch();
}

bool TouchOverlay::event(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_FINGER_DOWN:
        if (e.tfinger.touchID == SDL_MOUSE_TOUCHID) return false; // synthesized from a mouse
        return controls_.finger_down(e.tfinger.fingerID, e.tfinger.x, e.tfinger.y, e.tfinger.pressure);
    case SDL_EVENT_FINGER_MOTION:
        if (e.tfinger.touchID != SDL_MOUSE_TOUCHID)
            controls_.finger_move(e.tfinger.fingerID, e.tfinger.x, e.tfinger.y, e.tfinger.pressure);
        return false;
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        if (e.tfinger.touchID != SDL_MOUSE_TOUCHID) controls_.finger_up(e.tfinger.fingerID);
        return false;
    // SDL also turns the first finger into mouse events. Swallow those that
    // start on a control so the screen underneath doesn't see a click.
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.which != SDL_TOUCH_MOUSEID) return false;
        swallow_mouse_ = controls_.hit(e.button.x, e.button.y);
        return swallow_mouse_;
    case SDL_EVENT_MOUSE_MOTION: return e.motion.which == SDL_TOUCH_MOUSEID && swallow_mouse_;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.which != SDL_TOUCH_MOUSEID || !swallow_mouse_) return false;
        swallow_mouse_ = false;
        return true;
    // Any other input device hides the controls (auto mode).
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN: controls_.note_other_input(); return false;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (std::abs(int(e.gaxis.value)) > 20000) controls_.note_other_input();
        return false;
    case SDL_EVENT_KEY_DOWN:
        // The Android back key and volume keys exist on every phone.
        if (!e.key.repeat && e.key.scancode != SDL_SCANCODE_AC_BACK && e.key.scancode != SDL_SCANCODE_VOLUMEUP &&
            e.key.scancode != SDL_SCANCODE_VOLUMEDOWN && e.key.scancode != SDL_SCANCODE_MUTE)
            controls_.note_other_input();
        return false;
    // Fingers lifted while the app is in the background never report "up".
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        controls_.release_all();
        swallow_mouse_ = false;
        return false;
    default: return false;
    }
}

void TouchOverlay::update(double dt, TouchLayout layout, InputManager& input) {
    controls_.set_layout(layout);
    controls_.update(dt, input.tilt(0));
    input.set_overlay(0, controls_.pad());
}


void TouchOverlay::render(Ui& ui) {
    if (controls_.layout() == TouchLayout::None) return; // our own screens: other layout space, no controls
    // The UI is letterboxed to its game area. Work out the whole window
    // in UI units so controls can sit in the bars, then draw with the logical
    // presentation (which clips to the game area) switched off.
    const WindowSpace w = window_space(ui.renderer(), safe_window_);
    lay_out(controls_, w);
    if (!controls_.visible()) return;
    with_whole_window(ui.renderer(), w, [&](float ox, float oy) { draw_controls(ui, controls_, ox, oy); });
}

void TouchOverlay::draw_whole_window(Ui& ui, TouchControls& c, const std::function<void(float, float)>& draw) const {
    const WindowSpace w = window_space(ui.renderer(), safe_window_);
    lay_out(c, w);
    with_whole_window(ui.renderer(), w, draw);
}

void TouchOverlay::draw_whole_window(Ui& ui, const std::function<void(float, float)>& draw) const {
    with_whole_window(ui.renderer(), window_space(ui.renderer(), safe_window_), draw);
}

void TouchOverlay::draw_preview(Ui& ui, const UiRect& box, bool controls) const {
    // A 20:9 phone screen: the 640x360 game area plus side bars.
    constexpr float kX0 = -70, kX1 = 710, kH = 360;
    TouchControls preview;
    preview.configure(controls_.config());
    preview.set_layout(TouchLayout::Driving);
    preview.set_screen(kX0, 0, kX1, kH);
    SDL_Renderer* r = ui.renderer();
    const float k = std::min(box.w / (kX1 - kX0), box.h / kH);
    const float w = (kX1 - kX0) * k, h = kH * k, x = box.x + (box.w - w) / 2, y = box.y + (box.h - h) / 2;
    // A stand-in for the race: sky, road and the 16:9 game area.
    ui.fill_round(x - 3, y - 3, w + 6, h + 6, 8, Color{6, 7, 10});
    ui.fill(x, y, w, h * 0.45f, Color{58, 78, 104});
    ui.fill(x, y + h * 0.45f, w, h * 0.55f, Color{52, 60, 48});
    const float cx = x + w / 2, hy = y + h * 0.45f;
    SDL_FColor road{0.24f, 0.25f, 0.27f, 1};
    SDL_Vertex v[4] = {{{cx - 4, hy}, road, {0, 0}}, {{cx + 4, hy}, road, {0, 0}}, {{cx + w * 0.3f, y + h}, road, {0, 0}},
                       {{cx - w * 0.3f, y + h}, road, {0, 0}}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
    ui.frame(x + (0 - kX0) * k, y, 640 * k, h, Color{255, 255, 255, 40});
    if (!controls) return;

    // The clip rect is scaled with the render scale: drop it while scaled.
    SDL_Rect old_clip{};
    const bool had_clip = SDL_RenderClipEnabled(r) && SDL_GetRenderClipRect(r, &old_clip);
    SDL_SetRenderClipRect(r, nullptr);
    float sx = 1, sy = 1;
    SDL_GetRenderScale(r, &sx, &sy);
    SDL_SetRenderScale(r, sx * k, sy * k);
    draw_controls(ui, preview, x / k - kX0, y / k);
    SDL_SetRenderScale(r, sx, sy);
    SDL_SetRenderClipRect(r, had_clip ? &old_clip : nullptr);
}

} // namespace gt3r
