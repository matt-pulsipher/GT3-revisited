#pragma once

#include "app/ui_font.h"
#include "input/virtual_pad.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_render.h>

#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gt3r {

// Tiny immediate-mode drawing helpers for the frontend (our own screens, not
// the game's menus). Text uses the embedded Barlow font (see ui_font.h), or
// SDL's 8x8 debug font if that cannot be loaded.
struct Color {
    u8 r, g, b, a = 255;
};

namespace colors {
constexpr Color kText{230, 230, 235};
constexpr Color kDim{140, 140, 150};
constexpr Color kAccent{255, 196, 40};
constexpr Color kGood{90, 210, 120};
constexpr Color kBad{240, 90, 80};
constexpr Color kPanel{28, 30, 38};
constexpr Color kHighlight{52, 58, 82};
constexpr Color kBackground{14, 15, 20};
constexpr Color kOutline{78, 86, 118};
} // namespace colors

// Rectangle in UI units.
struct UiRect {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

class Ui {
public:
    static constexpr int kWidth = 640; // logical resolution of the game and tool screens
    static constexpr int kHeight = 360;
    // Height of a line of text: text(x, y) puts capitals in y .. y + kChar
    // (descenders reach ~2 units lower). Also the debug font's glyph width;
    // measure proportional text with text_width().
    static constexpr int kChar = 8;
    static constexpr float kBodyEm = 11;  // body text size (units per em)
    static constexpr float kTitleCap = 7; // text_big(): capital height per scale step

    // Blending is enabled so colours with alpha < 255 draw translucent
    // (e.g. panels over a 3D view).
    explicit Ui(SDL_Renderer* r);
    SDL_Renderer* renderer() const { return r_; }

    // Layout of the frame being drawn. Tool screens get the fixed 640x360
    // letterboxed space; the game's screens (Screen::follows_aspect) the game
    // area, 360 tall and as wide as Settings -> Display -> Aspect ratio makes
    // it (640 at 16:9, 480 at 4:3, the window's shape with fill). Screens with
    // Screen::adaptive_ui() (setup, hub, settings) get the whole window at an
    // integer pixel scale chosen for a readable text size, and lay themselves
    // out with width()/height() and safe() (the part clear of display cutouts
    // and rounded corners).
    void set_layout(float w, float h, UiRect safe, bool touch) { w_ = w, h_ = h, safe_ = safe, touch_ = touch; }
    float width() const { return w_; }
    float height() const { return h_; }
    // The game's 4:3 picture (its menus, movies) in the middle of the layout, full height.
    UiRect picture43() const { return {(w_ - h_ * 4 / 3) / 2, 0, h_ * 4 / 3, h_}; }
    const UiRect& safe() const { return safe_; }
    // Finger-sized targets (phones and tablets).
    bool touch() const { return touch_; }

    void clear();
    void text(float x, float y, std::string_view s, Color c = colors::kText);
    void text_right(float right, float y, std::string_view s, Color c = colors::kText);
    void text_center(float y, std::string_view s, Color c = colors::kText);       // centred in width()
    void text_center_in(float x, float w, float y, std::string_view s, Color c = colors::kText);
    // Title type (Barlow Condensed) with capitals in y .. y + kChar * scale.
    void text_big(float x, float y, std::string_view s, int scale, Color c = colors::kText);
    // Fixed-pitch text (hex dumps, tables): each character centred in a
    // kMonoCell-wide cell.
    static constexpr float kMonoCell = 7;
    void text_mono(float x, float y, std::string_view s, Color c = colors::kText);
    // Width of text() (scale 1) or text_big() at `scale`.
    static float text_width(std::string_view s, int scale = 1);
    // `s` split into lines at most `width` units wide (at spaces; words longer
    // than a line are cut).
    static std::vector<std::string> wrap(std::string_view s, float width);
    // `s` fitted into `width` units by replacing its middle with "..." (keeps
    // the start and the file name end of long paths).
    static std::string elide(std::string_view s, float width);
    // `s` fitted into `width` units by cutting its end ("...").
    static std::string clip(std::string_view s, float width);
    // Rounded rectangle, filled or as an outline `t` units thick.
    void fill_round(float x, float y, float w, float h, float r, Color c);
    void frame_round(float x, float y, float w, float h, float r, float t, Color c);
    void fill_circle(float cx, float cy, float r, Color c);
    // A push button: `primary` is the accent-filled main action.
    void button(const UiRect& r, std::string_view label, bool focused, bool pressed, bool primary = false);
    // Thin vertical scroll indicator for a list `view` showing `offset`..+view.h of `content` height.
    void scrollbar(const UiRect& view, float content, float offset);
    void fill(float x, float y, float w, float h, Color c);
    void frame(float x, float y, float w, float h, Color c);
    // Horizontal 0..1 bar.
    void bar(float x, float y, float w, float h, float v, Color c);
    void header(std::string_view title, std::string_view hint);
    void footer(std::string_view hint);
    // Settings-style widgets. Sizes follow touch(): finger-sized on phones.
    // A tab of a tab bar at (x, y), `h` tall; returns its width.
    float tab(float x, float y, float h, std::string_view label, bool active, bool focused);
    float tab_width(std::string_view label) const { return std::ceil(text_width(label)) + (touch_ ? 20 : 16); }
    // ON/OFF switch vertically centred on `cy`; toggle_width() wide.
    void toggle_pill(float x, float cy, bool on, bool enabled = true);
    float toggle_width() const { return touch_ ? 52.0f : 46.0f; }
    // Slider track with a fill and a knob, vertically centred on `cy`; `v` 0..1.
    void slider(float x, float cy, float w, float v, bool focused, bool enabled = true);
    // Small outlined tag ("RESTART") at (x, y); returns its width.
    float tag(float x, float y, std::string_view label, Color c);
    // Draws a list with a highlighted row; scrolls to keep `selected` visible.
    // Returns the index of the first visible row.
    int list_window(int count, int selected, int rows);

private:
    SDL_Renderer* r_;
    std::unique_ptr<UiFontRenderer> font_;
    float w_ = kWidth, h_ = kHeight;
    UiRect safe_{0, 0, kWidth, kHeight};
    bool touch_ = false;
};

// `s` split into lines of at most `max_chars` characters at spaces (words
// longer than a line are cut). Ui::wrap() measures the drawn width instead.
std::vector<std::string> wrap_text(std::string_view s, size_t max_chars);

// One pointer (mouse with the left button, or a finger) as press / move /
// release, for tap-and-drag handling. Mouse events SDL synthesizes from
// touches and touch events synthesized from the mouse are ignored, so each
// physical action is seen once. `e` must be in render coordinates.
struct PointerEvent {
    enum class Kind : u8 { None, Down, Move, Up } kind = Kind::None;
    float x = 0, y = 0;
    u64 id = 0; // 0: mouse, else finger id + 1
};
PointerEvent pointer_event(const SDL_Event& e);

// Tap-or-drag gesture on a list: a press turns into a tap on release, or
// into a drag once the pointer moves more than a few units (then the list
// scrolls and the release is not a tap). Only the first pointer counts.
class Gesture {
public:
    enum class Result : u8 { None, Down, Tap, DragStart, Drag, Up };
    Result feed(const SDL_Event& e);
    bool active() const { return active_; }
    bool dragging() const { return dragging_; }
    float x() const { return x_; }
    float y() const { return y_; }
    float start_x() const { return sx_; }
    float start_y() const { return sy_; }
    void cancel() { active_ = dragging_ = false; }

private:
    bool active_ = false, dragging_ = false;
    u64 id_ = 0;
    float x_ = 0, y_ = 0, sx_ = 0, sy_ = 0;
};

// Clickable areas of a page, recorded while it draws (UI units), for pointer input.
struct HitArea {
    UiRect r;
    int id = 0, arg = 0;
};
class HitList {
public:
    void clear() { areas_.clear(); }
    void add(const UiRect& r, int id, int arg = 0) { areas_.push_back({r, id, arg}); }
    // The area under (x, y); the one added last (drawn on top) first. Null when none.
    const HitArea* at(float x, float y) const;

private:
    std::vector<HitArea> areas_;
};

// Pointer input for pages with clickable areas: the mouse, fingers, and the VR controller's
// pointer (which App::xr_pointer sends as mouse events). Fed from Screen::on_event (UI
// coordinates) and read in update():
//  - Hover: a mouse or VR pointer moved (no button held). Pages focus what it is over, so one
//    click chooses. A finger has no hover: a tap on something not focused focuses it and the
//    next tap chooses (as in the game's menus, MenuController::tap).
//  - Click: a press and release that did not drag.
//  - Drag / DragEnd: the press moved more than a few units (now, and where it started).
//  - Wheel: mouse wheel steps (+ = up / away from the user) at the pointer.
//  - Back: the right mouse button.
class PointerInput {
public:
    struct Action {
        enum class Kind : u8 { Hover, Click, Drag, DragEnd, Wheel, Back } kind = Kind::Hover;
        float x = 0, y = 0;   // where (Drag: the pointer now)
        float sx = 0, sy = 0; // Drag: where the press started
        float wheel = 0;      // Wheel: steps
        bool finger = false;  // a touch
    };
    void event(const SDL_Event& e);
    std::vector<Action> take() { return std::exchange(actions_, {}); }
    void clear() {
        actions_.clear();
        gesture_.cancel();
    }
    // Where the mouse / VR pointer last was (hover highlights); false after a touch, or when it
    // left the window.
    bool hover_pos(float& x, float& y) const {
        x = hx_, y = hy_;
        return hover_known_;
    }

private:
    Gesture gesture_;
    bool finger_ = false;
    std::vector<Action> actions_;
    bool hover_known_ = false;
    float hx_ = 0, hy_ = 0;
};

// Tap/click position in UI coordinates if `e` is a primary press (left mouse
// button or a finger; mouse events synthesized from touches are ignored).
// `e` must already be converted to render coordinates (App does this).
bool pointer_down(const SDL_Event& e, float& x, float& y);

// `s` shortened to at most `max_chars` characters by replacing its middle
// with "..." (keeps the start and the file name end of long paths). Never
// splits a UTF-8 sequence. Ui::elide() fits a width in UI units instead.
std::string elide_middle(std::string_view s, size_t max_chars);

// Edge-triggered, auto-repeating menu input on top of the virtual pad.
class MenuInput {
public:
    void update(const PadState& pad, double dt);
    bool pressed(PadButton b) const { return pressed_ & (1u << static_cast<unsigned>(b)); }
    bool held(PadButton b) const { return held_ & (1u << static_cast<unsigned>(b)); }
    // Up/Down/Left/Right with key-repeat, plus left stick as a d-pad.
    int vertical() const;
    int horizontal() const;
    void suppress() { suppress_ = true; } // ignore current holds (after rebinding)

private:
    u16 held_ = 0, pressed_ = 0;
    double repeat_timer_[4] = {};
    bool suppress_ = false;
    u16 suppressed_ = 0;
};

} // namespace gt3r
