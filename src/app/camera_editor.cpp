#include "app/camera_editor.h"

#include "app/app.h"

#include "race/hood_camera.h"

#include <SDL3/SDL_gamepad.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace gt3r {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDeg = kPi / 180.0f;
constexpr float kOrbitPerUnit = 0.6f * kDeg; // a drag round the car: degrees per UI unit
constexpr float kStickTurn = 100 * kDeg;     // the right stick's turn rate (radians/s)
constexpr float kDeadZone = 0.15f;
constexpr float kScrollStart = 6;            // units a press on the panel moves before it scrolls

using Mount = race::CustomCamera::Mount;

float stick(const PadState& p, PadAxis a) {
    const float v = p.axis_f(a);
    return std::fabs(v) < kDeadZone ? 0.0f : (v - std::copysign(kDeadZone, v)) / (1 - kDeadZone);
}

std::string degrees(float v) { return std::to_string(int(std::lround(v))) + " deg"; }

std::string metres(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%+.2f m", v);
    return buf;
}

float snap(float v, float lo, float hi, float step) { return std::clamp(lo + std::round((v - lo) / step) * step, lo, hi); }

Color alpha(Color c, u8 a) { return {c.r, c.g, c.b, a}; }

// Reset's part of a default camera: where it sits and how it looks, not its name, switch, mirror,
// car or HUD choices.
void take_view(race::CustomCamera& to, const race::CustomCamera& from) {
    to.offset = from.offset;
    to.yaw = from.yaw, to.pitch = from.pitch, to.roll = from.roll;
    to.fov = from.fov;
    to.body_roll = from.body_roll;
}

} // namespace

CameraEditor::CameraEditor(race::CustomCamera cam, bool is_new, Defaults defaults)
    : cam_(std::move(cam)), start_(cam_), is_new_(is_new), defaults_(std::move(defaults)) {
    // A camera that keeps the car on screen rotates round it; one on the car turns in place.
    around_ = cam_.car != race::CustomCamera::OwnCar::Hidden;
    keep_mount_offset();
    build_rows();
}

void CameraEditor::begin() {
    if (!is_new_ || !defaults_) return;
    race::CustomCamera p = defaults_(Mount::Follow);
    p.id = cam_.id, p.name = cam_.name, p.enabled = cam_.enabled;
    cam_ = start_ = p;
    around_ = cam_.car != race::CustomCamera::OwnCar::Hidden;
    keep_mount_offset();
    build_rows();
}

bool CameraEditor::rocking() const {
    if (page_ != Page::Camera || hidden_ || confirm_ || bar_sel_ >= 0 || sel_ < 0 || sel_ >= int(rows_.size())) return false;
    return rows_[size_t(sel_)].rock && rows_[size_t(sel_)].enabled;
}

// --- the panel ------------------------------------------------------------------------------

void CameraEditor::build_rows() {
    rows_.clear();
    race::CustomCamera& c = cam_;
    auto choice = [&](std::string label, std::vector<std::string> names, int idx, std::function<void(int)> set) -> Row& {
        Row r;
        r.kind = Row::Kind::Choice;
        r.label = std::move(label);
        const int n = int(names.size());
        r.value = names[size_t(std::clamp(idx, 0, n - 1))];
        r.change = [set, idx, n](int d) { set(std::clamp(idx + d, 0, n - 1)); };
        r.activate = [set, idx, n] { set((idx + 1) % n); };
        rows_.push_back(std::move(r));
        return rows_.back();
    };
    auto slider = [&](std::string label, float v, float lo, float hi, float step, std::string text,
                      std::function<void(float)> set) -> Row& {
        Row r;
        r.kind = Row::Kind::Slider;
        r.label = std::move(label);
        r.value = std::move(text);
        r.frac = (v - lo) / (hi - lo);
        r.change = [=](int d) { set(snap(v + float(d) * step, lo, hi, step)); };
        r.set_frac = [=](float f) { set(snap(lo + std::clamp(f, 0.0f, 1.0f) * (hi - lo), lo, hi, step)); };
        rows_.push_back(std::move(r));
        return rows_.back();
    };
    auto action = [&](std::string label, std::function<void()> fn) -> Row& {
        Row r;
        r.kind = Row::Kind::Action;
        r.label = std::move(label);
        r.activate = std::move(fn);
        rows_.push_back(std::move(r));
        return rows_.back();
    };
    if (page_ == Page::Camera) {
        // The anchor stays the first row: the rows below it change with it.
        static const Mount kAnchors[] = {Mount::Follow, Mount::Body, Mount::Hood, Mount::Incar};
        const int at = int(std::find(std::begin(kAnchors), std::end(kAnchors), c.mount) - std::begin(kAnchors));
        choice("Anchor", {"Follows the car", "Fixed to the car", "Hood", "Driver's seat"}, at, [this, &c](int i) {
            const Mount m = kAnchors[std::clamp(i, 0, 3)];
            if (m == c.mount) return;
            // The position anchors keep the eye and aim they last had (switching between them
            // changes only the anchor); a seat anchor starts as Reset puts it (its centred seat,
            // aimed ahead, its own field of view and body roll) — a position anchor's aim is
            // usually turned back at the car, which in a seat is the inside of the cabin.
            keep_mount_offset();
            c.mount = m;
            if (seat_mount()) {
                c.offset = {};
                c.yaw = c.pitch = c.roll = 0;
                if (defaults_) take_view(c, defaults_(m));
            } else {
                c.offset = pos_offset_;
                c.yaw = pos_yaw_, c.pitch = pos_pitch_, c.roll = pos_roll_;
            }
        });
        if (!seat_mount()) {
            choice("Rotate", {"Around the car", "In place"}, around_ ? 0 : 1, [this](int i) { around_ = i == 0; });
        } else {
            // A seat's nudge (the hood's from the car's own shape, race/hood_camera.h; the driver's
            // seat's from the driver's eye): drags only aim, these are the moves.
            const bool hood = c.mount == Mount::Hood;
            const float fwd = hood ? race::kHoodForward : race::kSeatForward;
            const float lat = hood ? race::kHoodLateral : race::kSeatLateral;
            const float lo = hood ? race::kHoodHeightMin : race::kSeatHeightMin;
            const float hi = hood ? race::kHoodHeightMax : race::kSeatHeightMax;
            slider("Fore/aft", c.offset.z, -fwd, fwd, 0.05f, metres(c.offset.z), [&c](float v) { c.offset.z = v; });
            slider("Sideways", c.offset.x, -lat, lat, 0.05f, metres(c.offset.x), [&c](float v) { c.offset.x = v; });
            slider("Height", c.offset.y, lo, hi, 0.05f, metres(c.offset.y), [&c](float v) { c.offset.y = v; });
        }
        // Following the car is always level: its body roll row stays (the rows don't jump), greyed.
        const bool follow = c.mount == Mount::Follow;
        Row& roll = slider("Body roll", follow ? 0.0f : c.body_roll, 0, 1, 0.1f,
                           follow ? std::string("Level") : std::to_string(int(std::lround(c.body_roll * 100))) + "%",
                           [&c](float v) { c.body_roll = v; });
        roll.enabled = !follow, roll.rock = true;
        slider("Tilt", c.roll, -race::CustomCamera::kMaxRoll, race::CustomCamera::kMaxRoll, 1, degrees(c.roll),
               [&c](float v) { c.roll = v; });
        slider("Field of view", c.fov, race::CustomCamera::kMinFov, race::CustomCamera::kMaxFov, 1, degrees(c.fov),
               [&c](float v) { c.fov = v; });
        choice("Your car", {"Shown", "Hidden"}, c.car == race::CustomCamera::OwnCar::Hidden ? 1 : 0, [&c](int i) {
            c.car = i == 1 ? race::CustomCamera::OwnCar::Hidden : race::CustomCamera::OwnCar::Shown;
        });
        choice("HUD layout", {"Chase", "In-car"}, c.incar_hud ? 1 : 0, [&c](int i) { c.incar_hud = i == 1; });
        // The view turned straight along the car and its tilt taken out, the camera where it is and
        // still aimed as far up or down (a camera over the car keeps looking down at it).
        action("Reset rotation", [&c] { c.yaw = c.roll = 0; });
        // The anchor's own place and look (its default), the anchor and the choices kept.
        action("Reset", [this] {
            if (!defaults_) return;
            take_view(cam_, defaults_(cam_.mount));
            keep_mount_offset();
        });
        action("Undo changes", [this] {
            const race::CustomCamera keep = cam_;
            cam_ = start_;
            cam_.mirror = keep.mirror, cam_.mirror_x = keep.mirror_x, cam_.mirror_y = keep.mirror_y,
            cam_.mirror_size = keep.mirror_size; // (the Mirror page has its own)
            keep_mount_offset();
        });
    } else {
        choice("Mirror", {"Hidden", "Shown"}, c.mirror ? 1 : 0, [&c](int i) { c.mirror = i == 1; });
        slider("Size", c.mirror_size, race::CustomCamera::kMinMirror, race::CustomCamera::kMaxMirror, 0.05f,
               std::to_string(int(std::lround(c.mirror_size * 100))) + "%", [&c](float v) { c.mirror_size = v; });
        action("Usual place", [&c] { c.mirror_x = race::CustomCamera::kMirrorX, c.mirror_y = race::CustomCamera::kMirrorY; });
        action("Usual size", [&c] { c.mirror_size = 1; });
    }
    sel_ = std::clamp(sel_, 0, int(rows_.size()) - 1);
}

void CameraEditor::layout(float w, float h, bool touch) {
    touch_ = touch;
    const float bar_h = touch ? 38.0f : 22.0f, bh = bar_h - (touch ? 6 : 4); // (finger-sized on touch screens)
    bar_ = {0, 0, w, bar_h};
    buttons_.clear();
    float x = 6;
    for (Hit t : {kTabCamera, kTabMirror}) {
        const std::string label = t == kTabCamera ? "Camera" : "Mirror";
        const float tw = std::ceil(Ui::text_width(label)) + (touch ? 30 : 18);
        buttons_.push_back({{x, (bar_h - bh) / 2, tw, bh}, t, label});
        x += tw + 4;
    }
    float rx = w - 6;
    for (Hit b : {kSave, kCancel, kHide}) {
        const std::string label = b == kSave ? "Save" : b == kCancel ? "Cancel" : "Hide";
        const float bw = std::ceil(Ui::text_width(label)) + (touch ? 34 : 20);
        rx -= bw;
        buttons_.push_back({{rx, (bar_h - bh) / 2, bw, bh}, b, label});
        rx -= 4;
    }
    // Hidden: the one button that brings it back.
    if (hidden_) {
        buttons_.clear();
        const float bw = std::ceil(Ui::text_width("Show")) + (touch ? 34 : 20);
        buttons_.push_back({{w - 6 - bw, 4, bw, bh}, kShow, "Show"});
    }
    // Rows finger-sized on touch screens; the panel scrolls when they don't all fit above the legend.
    row_h_ = touch ? 32.0f : 20.0f;
    const float pw = std::min(touch ? 262.0f : 228.0f, w * 0.45f);
    const float room = std::max(row_h_, h - 18 - (bar_h + 6) - 8);
    const float content = float(rows_.size()) * row_h_;
    rows_h_ = std::min(content, room);
    panel_ = {w - pw - 6, bar_h + 6, pw, rows_h_ + 8};
    rows_y_ = panel_.y + 4;
    if (follow_sel_ && sel_ >= 0 && bar_sel_ < 0) {
        const float top = float(sel_) * row_h_;
        if (top < scroll_) scroll_ = top;
        if (top + row_h_ > scroll_ + rows_h_) scroll_ = top + row_h_ - rows_h_;
    }
    scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, content - rows_h_));
    label_x_ = panel_.x + 8;
    val_x_ = panel_.x + std::floor(pw * 0.42f);
    val_w_ = panel_.right() - 8 - val_x_ - (content > rows_h_ ? 4 : 0); // (room for the scroll bar)
    slider_w_ = std::max(40.0f, val_w_ - (touch ? 48 : 52));
    const float cw = 300, ch = touch ? 96.0f : 76.0f, cx = (w - cw) / 2, cy = (h - ch) / 2;
    const float bw = (cw - 30) / 2, by = cy + ch - bh - (touch ? 12 : 10);
    keep_ = {cx + 10, by, bw, bh};
    leave_ = {cx + 20 + bw, by, bw, bh};
}

CameraEditor::Hit CameraEditor::hit(float x, float y, int* row) const {
    if (confirm_) {
        if (keep_.contains(x, y)) return kKeep;
        if (leave_.contains(x, y)) return kLeave;
        return kNone;
    }
    for (const Button& b : buttons_)
        if (b.r.contains(x, y)) return b.id;
    if (hidden_) return kNone;
    if (panel_.contains(x, y)) {
        const int i = y >= rows_y_ && y < rows_y_ + rows_h_ ? int(std::floor((y - rows_y_ + scroll_) / row_h_)) : -1;
        if (row) *row = i >= 0 && i < int(rows_.size()) ? i : -1;
        return kRow;
    }
    if (bar_.contains(x, y)) return kRow; // (the bar's background: no scene drag)
    return kNone;
}

bool CameraEditor::clickable(float x, float y) const {
    int row = -1;
    const Hit h = hit(x, y, &row);
    return h != kNone && (h != kRow || row >= 0);
}

void CameraEditor::tap(Hit h, int row, float x) {
    switch (h) {
    case kTabCamera: page_ = Page::Camera, sel_ = 0, scroll_ = 0; break;
    case kTabMirror: page_ = Page::Mirror, sel_ = 0, scroll_ = 0; break;
    case kHide: hidden_ = true, bar_sel_ = -1; break;
    case kShow: hidden_ = false; break;
    case kCancel: leave_request(); break;
    case kSave: pending_ = Result::Save; break;
    case kKeep: confirm_ = false; break;
    case kLeave: pending_ = Result::Cancel; break;
    case kRow: {
        if (row < 0 || row >= int(rows_.size())) return;
        sel_ = row, bar_sel_ = -1;
        Row& r = rows_[size_t(row)];
        if (!r.enabled) return;
        const bool on_value = x >= val_x_ - 6;
        if (r.kind == Row::Kind::Slider) {
            if (on_value && r.set_frac) r.set_frac((x - val_x_) / slider_w_);
        } else if (r.kind == Row::Kind::Choice && on_value && r.change) {
            r.change(x < val_x_ + val_w_ / 2 ? -1 : 1);
        } else if (r.activate) {
            r.activate();
        }
        break;
    }
    default: break;
    }
    build_rows();
}

void CameraEditor::leave_request() {
    if (cam_ == start_ && !is_new_) {
        pending_ = Result::Cancel;
        return;
    }
    confirm_ = true, confirm_sel_ = 0;
}

std::vector<int> CameraEditor::bar_order() const {
    std::vector<int> order;
    for (int i = 0; i < int(buttons_.size()); ++i) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return buttons_[size_t(a)].r.x < buttons_[size_t(b)].r.x; });
    return order;
}

// --- the scene's moves ----------------------------------------------------------------------

float CameraEditor::pivot_distance(const Scene& s) const { return render::length(cam_.offset - s.pivot); }

void CameraEditor::rotate(const Scene& s, float dx, float dy, bool around) {
    // On a seat anchor the seat nudges are panel rows only: drags aim the view in place.
    if (seat_mount()) around = false;
    if (around) {
        // Round the car, down to the road at most.
        float el = dy * kOrbitPerUnit;
        const render::Vec3 v = cam_.offset - s.pivot;
        if (const float d = render::length(v); d > 0.05f) {
            const float e0 = std::asin(std::clamp(v.y / d, -1.0f, 1.0f));
            const float lo = std::asin(std::clamp((s.min_y - s.pivot.y) / d, -1.0f, 1.0f));
            el = std::max(el, std::min(0.0f, lo - e0));
        }
        race::orbit_camera(cam_, s.pivot, -dx * kOrbitPerUnit, el);
        keep_above_road(s);
        return;
    }
    // In place: the picture follows the pointer (a unit of drag turns the view by a unit's angle).
    const float a = cam_.fovy() / std::max(s.frame.h, 1.0f);
    race::look_camera(cam_, -dx * a, dy * a);
}

void CameraEditor::pan(const Scene& s, float dx, float dy) {
    if (seat_mount()) return; // (the seat's sliders)
    // The picture at the car's distance follows the pointer.
    const float d = std::max(pivot_distance(s), 1.0f);
    const float k = 2 * d * std::tan(cam_.fovy() / 2) / std::max(s.frame.h, 1.0f);
    race::pan_camera(cam_, -dx * k, dy * k, 0);
    keep_above_road(s);
}

void CameraEditor::dolly(const Scene& s, float amount) {
    if (seat_mount()) return; // (the seat's sliders)
    race::pan_camera(cam_, 0, 0, amount * std::max(pivot_distance(s), 1.0f));
    keep_above_road(s);
}

void CameraEditor::move_mirror(const Scene& s, float dx, float dy) {
    cam_.mirror_x = std::clamp(cam_.mirror_x + dx / std::max(s.frame.w, 1.0f), 0.0f, 1.0f);
    cam_.mirror_y = std::clamp(cam_.mirror_y + dy / std::max(s.frame.h, 1.0f), 0.0f, 1.0f);
}

// --- input ------------------------------------------------------------------------------------

void CameraEditor::event(const SDL_Event& e, const Scene& s) {
    // The legend follows the last thing used.
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN: input_ = Input::Pad, input_known_ = true; break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (std::abs(int(e.gaxis.value)) > 16000) input_ = Input::Pad, input_known_ = true;
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_MOUSE_WHEEL: input_ = Input::Pointer, input_known_ = true; break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.which != SDL_TOUCH_MOUSEID) input_ = Input::Pointer, input_known_ = true;
        break;
    case SDL_EVENT_FINGER_DOWN:
        if (e.tfinger.touchID != SDL_MOUSE_TOUCHID) input_ = Input::Touch, input_known_ = true;
        break;
    default: break;
    }
    // Fingers lifted while the app is away never report "up".
    if (e.type == SDL_EVENT_WILL_ENTER_BACKGROUND || e.type == SDL_EVENT_DID_ENTER_BACKGROUND ||
        e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        ptrs_.clear();
        drag_ = Drag::None, mouse_button_ = 0, slider_ = false, scrolling_ = false;
        return;
    }
    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        const float steps = e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f);
        if (confirm_ || steps == 0) return;
        if (const Hit h = hit(e.wheel.mouse_x, e.wheel.mouse_y); h != kNone) {
            if (h == kRow && panel_.contains(e.wheel.mouse_x, e.wheel.mouse_y)) // the panel's own scroll
                scroll_ -= steps * row_h_, follow_sel_ = false;
            return;
        }
        if (page_ == Page::Mirror) {
            cam_.mirror_size = std::clamp(cam_.mirror_size * std::pow(1.08f, steps), race::CustomCamera::kMinMirror,
                                          race::CustomCamera::kMaxMirror);
        } else if (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) {
            cam_.fov = std::clamp(std::round(cam_.fov - steps * 2), race::CustomCamera::kMinFov, race::CustomCamera::kMaxFov);
        } else {
            dolly(s, steps * 0.1f);
        }
        build_rows();
        return;
    }
    // The right and middle buttons over the picture: the other rotation, and moving.
    if ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) && e.button.which != SDL_TOUCH_MOUSEID &&
        (e.button.button == SDL_BUTTON_RIGHT || e.button.button == SDL_BUTTON_MIDDLE)) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) mouse_button_ = 0;
        else if (!confirm_ && hit(e.button.x, e.button.y) == kNone)
            mouse_button_ = e.button.button, mouse_x_ = e.button.x, mouse_y_ = e.button.y;
        return;
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION && mouse_button_ && e.motion.which != SDL_TOUCH_MOUSEID) {
        const float dx = e.motion.x - mouse_x_, dy = e.motion.y - mouse_y_;
        mouse_x_ = e.motion.x, mouse_y_ = e.motion.y;
        if (page_ == Page::Mirror) move_mirror(s, dx, dy);
        else if (mouse_button_ == SDL_BUTTON_MIDDLE) pan(s, dx, dy);
        else rotate(s, dx, dy, !around_);
        build_rows();
        return;
    }
    const PointerEvent pe = pointer_event(e);
    switch (pe.kind) {
    case PointerEvent::Kind::Down: {
        std::erase_if(ptrs_, [&](const Pointer& p) { return p.id == pe.id; });
        ptrs_.push_back({pe.id, pe.x, pe.y});
        bar_sel_ = -1; // (the pointer takes over from the D-pad's place in the bar)
        if (ptrs_.size() == 1) {
            int row = -1;
            press_hit_ = hit(pe.x, pe.y, &row);
            press_row_ = row;
            press_x_ = pe.x, press_y_ = pe.y, press_scroll_ = scroll_;
            slider_ = scrolling_ = finger_on_slider_ = false;
            if (press_hit_ != kNone) {
                drag_ = Drag::Ui;
                // A slider follows the mouse from the press; a finger's drag on it is the slider's
                // when it starts sideways, the panel's scroll when it starts up or down.
                if (press_hit_ == kRow && row >= 0 && rows_[size_t(row)].kind == Row::Kind::Slider &&
                    rows_[size_t(row)].enabled && pe.x >= val_x_ - 6) {
                    sel_ = row;
                    if (pe.id == 0) {
                        slider_ = true;
                        rows_[size_t(row)].set_frac((pe.x - val_x_) / slider_w_);
                        build_rows();
                    } else {
                        finger_on_slider_ = true;
                    }
                }
            } else {
                drag_ = confirm_ ? Drag::Ignore : Drag::Scene;
                pan_drag_ = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 && pe.id == 0;
            }
        } else if (ptrs_.size() == 2 && (drag_ == Drag::Scene || drag_ == Drag::None)) {
            drag_ = Drag::Two;
            spread_ = std::max(8.0f, std::hypot(ptrs_[1].x - ptrs_[0].x, ptrs_[1].y - ptrs_[0].y));
            cx_ = (ptrs_[0].x + ptrs_[1].x) / 2, cy_ = (ptrs_[0].y + ptrs_[1].y) / 2;
        } else if (drag_ != Drag::Two) {
            drag_ = Drag::Ignore;
        }
        break;
    }
    case PointerEvent::Kind::Move: {
        Pointer* p = nullptr;
        for (Pointer& q : ptrs_)
            if (q.id == pe.id) p = &q;
        if (!p) break;
        const float dx = pe.x - p->x, dy = pe.y - p->y;
        p->x = pe.x, p->y = pe.y;
        if (drag_ == Drag::Ui && finger_on_slider_ && !slider_ && !scrolling_ &&
            std::hypot(pe.x - press_x_, pe.y - press_y_) > kScrollStart) { // which way it starts
            finger_on_slider_ = false;
            if (std::fabs(pe.y - press_y_) > std::fabs(pe.x - press_x_)) scrolling_ = true, follow_sel_ = false;
            else slider_ = true;
        }
        if (drag_ == Drag::Ui && slider_ && press_row_ >= 0 && press_row_ < int(rows_.size())) {
            rows_[size_t(press_row_)].set_frac((pe.x - val_x_) / slider_w_);
            build_rows();
        } else if (drag_ == Drag::Ui && press_hit_ == kRow && !finger_on_slider_ &&
                   (scrolling_ || std::fabs(pe.y - press_y_) > kScrollStart)) {
            scrolling_ = true, follow_sel_ = false; // the rows follow the finger
            scroll_ = press_scroll_ - (pe.y - press_y_);
        } else if (drag_ == Drag::Scene && ptrs_.size() == 1) {
            if (page_ == Page::Mirror) move_mirror(s, dx, dy);
            else if (pan_drag_) pan(s, dx, dy);
            else rotate(s, dx, dy, around_);
            build_rows();
        } else if (drag_ == Drag::Two && ptrs_.size() >= 2) {
            const float sp = std::max(8.0f, std::hypot(ptrs_[1].x - ptrs_[0].x, ptrs_[1].y - ptrs_[0].y));
            const float cx = (ptrs_[0].x + ptrs_[1].x) / 2, cy = (ptrs_[0].y + ptrs_[1].y) / 2;
            if (page_ == Page::Mirror) {
                cam_.mirror_size = std::clamp(cam_.mirror_size * sp / spread_, race::CustomCamera::kMinMirror,
                                              race::CustomCamera::kMaxMirror);
                move_mirror(s, cx - cx_, cy - cy_);
            } else {
                dolly(s, 1 - spread_ / sp);
                pan(s, cx - cx_, cy - cy_);
            }
            spread_ = sp, cx_ = cx, cy_ = cy;
            build_rows();
        }
        break;
    }
    case PointerEvent::Kind::Up: {
        const bool known = std::any_of(ptrs_.begin(), ptrs_.end(), [&](const Pointer& q) { return q.id == pe.id; });
        std::erase_if(ptrs_, [&](const Pointer& q) { return q.id == pe.id; });
        if (!known) break;
        if (drag_ == Drag::Ui && ptrs_.empty() && !slider_ && !scrolling_) {
            int row = -1;
            const Hit h = hit(pe.x, pe.y, &row);
            if (h == press_hit_ && (h != kRow || row == press_row_)) tap(h, row, pe.x);
        }
        if (ptrs_.empty()) drag_ = Drag::None, slider_ = scrolling_ = finger_on_slider_ = false;
        else if (drag_ == Drag::Two) drag_ = Drag::Ignore; // (until every finger is up)
        break;
    }
    case PointerEvent::Kind::None: break;
    }
}

void CameraEditor::pad_input(App& app, const Scene& s, double dt) {
    const PadState& pad = app.input().menu_pad();
    const float t = float(std::min(dt, 0.1));
    const float lx = stick(pad, PadAxis::LX), ly = stick(pad, PadAxis::LY);
    const float rx = stick(pad, PadAxis::RX), ry = stick(pad, PadAxis::RY);
    const float ud = (pad.down(PadButton::R2) ? 1.0f : 0.0f) - (pad.down(PadButton::L2) ? 1.0f : 0.0f);
    if (lx == 0 && ly == 0 && rx == 0 && ry == 0 && ud == 0) return;
    if (page_ == Page::Mirror) {
        move_mirror(s, lx * 0.5f * t * s.frame.w, ly * 0.5f * t * s.frame.h);
        const float grow = ud - ry;
        cam_.mirror_size = std::clamp(cam_.mirror_size * std::exp(grow * 0.8f * t), race::CustomCamera::kMinMirror,
                                      race::CustomCamera::kMaxMirror);
    } else {
        // On a seat anchor the seat is panel rows only: the left stick and the triggers can't move it.
        if (!seat_mount()) {
            // Faster the farther from the car: 0.8 of the distance a second (0.6 .. 12 m/s).
            const float v = std::clamp(pivot_distance(s) * 0.8f, 0.6f, 12.0f) * t;
            race::pan_camera(cam_, lx * v, ud * v, -ly * v);
            keep_above_road(s);
        }
        if (rx != 0 || ry != 0) {
            if (around_) rotate(s, rx * kStickTurn * t / kOrbitPerUnit, -ry * kStickTurn * t / kOrbitPerUnit, true);
            else race::look_camera(cam_, rx * kStickTurn * t, -ry * kStickTurn * t);
        }
    }
    build_rows();
}

CameraEditor::Result CameraEditor::update(App& app, const Scene& s, double dt) {
    const MenuInput& m = app.menu();
    if (!input_known_) { // (opened from a controller or a keyboard: nothing used here yet)
        input_known_ = true;
        input_ = touch_ ? Input::Touch : SDL_HasGamepad() ? Input::Pad : Input::Pointer;
    }
    if (pending_ != Result::None) return std::exchange(pending_, Result::None);
    // The D-pad alone (MenuInput takes the left stick as one too): edges and repeats.
    const u16 dpad_bits[4] = {u16(1u << unsigned(PadButton::Up)), u16(1u << unsigned(PadButton::Down)),
                              u16(1u << unsigned(PadButton::Left)), u16(1u << unsigned(PadButton::Right))};
    const u16 now = app.input().menu_pad().buttons;
    bool fire[4] = {};
    for (int i = 0; i < 4; ++i) {
        const bool held = now & dpad_bits[i], was = dpad_prev_ & dpad_bits[i];
        if (!held) repeat_[i] = 0;
        else if (!was) fire[i] = true, repeat_[i] = -0.35;
        else if ((repeat_[i] += dt) >= 0.07) fire[i] = true, repeat_[i] = 0;
    }
    dpad_prev_ = now;
    if (confirm_) {
        if (fire[2] || fire[3]) confirm_sel_ ^= 1;
        if (m.pressed(PadButton::Cross)) {
            if (confirm_sel_ == 1) return Result::Cancel;
            confirm_ = false;
        }
        if (m.pressed(PadButton::Triangle)) confirm_ = false;
        return Result::None;
    }
    if (m.pressed(PadButton::Start)) return Result::Save;
    if (m.pressed(PadButton::Triangle)) {
        if (hidden_) hidden_ = false;
        else leave_request();
        return std::exchange(pending_, Result::None);
    }
    if (m.pressed(PadButton::Select)) hidden_ = !hidden_, bar_sel_ = -1;
    if (m.pressed(PadButton::L1) || m.pressed(PadButton::R1)) {
        page_ = page_ == Page::Camera ? Page::Mirror : Page::Camera;
        sel_ = 0, scroll_ = 0;
        build_rows();
    }
    if (!hidden_ && !rows_.empty()) {
        const int n = int(rows_.size());
        if (bar_sel_ >= 0) {
            // In the bar: Left / Right along it, Down back to the panel, Cross presses.
            const std::vector<int> order = bar_order();
            const int k = int(order.size());
            if (k > 0) {
                bar_sel_ = std::clamp(bar_sel_, 0, k - 1);
                if (fire[2]) bar_sel_ = std::max(0, bar_sel_ - 1);
                if (fire[3]) bar_sel_ = std::min(k - 1, bar_sel_ + 1);
                if (fire[1]) bar_sel_ = -1, sel_ = 0, follow_sel_ = true;
                else if (m.pressed(PadButton::Cross)) {
                    const Hit id = buttons_[size_t(order[size_t(bar_sel_)])].id;
                    tap(id, -1, 0);
                    if (id == kTabCamera || id == kTabMirror) bar_sel_ = std::clamp(bar_sel_, 0, k - 1);
                }
            } else {
                bar_sel_ = -1;
            }
        } else {
            // Up from the first row (down from the last) into the bar, at Save.
            if (fire[0]) {
                if (sel_ == 0) {
                    const std::vector<int> order = bar_order();
                    for (int i = 0; i < int(order.size()); ++i)
                        if (buttons_[size_t(order[size_t(i)])].id == kSave) bar_sel_ = i;
                } else {
                    sel_ -= 1;
                }
                follow_sel_ = true;
            }
            if (fire[1]) {
                if (sel_ == n - 1) {
                    const std::vector<int> order = bar_order();
                    for (int i = 0; i < int(order.size()); ++i)
                        if (buttons_[size_t(order[size_t(i)])].id == kSave) bar_sel_ = i;
                } else {
                    sel_ += 1;
                }
                follow_sel_ = true;
            }
            if (bar_sel_ < 0) {
                Row& r = rows_[size_t(sel_)];
                if (r.enabled) {
                    if ((fire[2] || fire[3]) && r.change) r.change(fire[3] ? 1 : -1);
                    if (m.pressed(PadButton::Cross) && r.activate) r.activate();
                }
            }
        }
        build_rows();
    }
    pad_input(app, s, dt);
    cam_.clamp();
    return Result::None;
}

// --- drawing ------------------------------------------------------------------------------------

std::string CameraEditor::legend() const {
    const bool mirror = page_ == Page::Mirror, seat = seat_mount();
    switch (input_) {
    case Input::Touch:
        return mirror ? "Drag: move the mirror    Pinch: size"
               : seat ? "Drag: aim    The seat moves with its sliders"
                      : "Drag: rotate    Two fingers: move    Pinch: nearer / farther";
    case Input::Pad:
        return mirror ? "Left stick: move    L2 / R2: size    D-pad: menu, up to Save    L1 / R1: page    Start: save"
               : seat ? "Right stick: aim    D-pad: menu and seat, up to Save    L1 / R1: page    Start: save    Select: hide"
                      : "Left stick: move    Right stick: rotate    L2 / R2: down / up    D-pad: menu, up to Save    Start: save";
    case Input::Pointer:
    default:
        return mirror ? "Drag: move the mirror    Wheel: size"
               : seat ? "Drag: aim    Ctrl+wheel: zoom    The seat moves with its sliders"
                      : "Drag: rotate    Right drag: other way    Middle / Shift drag: move    Wheel: nearer    Ctrl+wheel: zoom";
    }
}

void CameraEditor::draw(Ui& ui, const Scene& s) {
    build_rows();
    layout(ui.width(), ui.height(), ui.touch());
    const Color panel_bg{18, 20, 27, 220};
    // The mirror page: the mirror's frame (shown or not) with its corner handles.
    if (page_ == Page::Mirror && !hidden_ && s.mirror.w > 0) {
        const UiRect& r = s.mirror;
        const Color c = cam_.mirror ? colors::kAccent : alpha(colors::kDim, 200);
        if (!cam_.mirror) {
            ui.fill(r.x, r.y, r.w, r.h, Color{0, 0, 0, 90});
            ui.text_center_in(r.x, r.w, std::floor(r.y + r.h / 2 - Ui::kChar / 2.0f), "Mirror hidden", colors::kDim);
        }
        ui.frame_round(r.x - 3, r.y - 3, r.w + 6, r.h + 6, 4, 1.5f, c);
        for (float hx : {r.x - 3, r.right() + 3})
            for (float hy : {r.y - 3, r.bottom() + 3}) ui.fill_round(hx - 3, hy - 3, 6, 6, 1.5f, c);
    }
    // The buttons (and the bar behind them); the D-pad's place in the bar shows focused.
    if (!hidden_) ui.fill(bar_.x, bar_.y, bar_.w, bar_.h, panel_bg);
    const bool pressing = drag_ == Drag::Ui && !ptrs_.empty();
    const std::vector<int> order = bar_order();
    const int focused = bar_sel_ >= 0 && bar_sel_ < int(order.size()) ? order[size_t(bar_sel_)] : -1;
    for (int i = 0; i < int(buttons_.size()); ++i) {
        const Button& b = buttons_[size_t(i)];
        const bool tab = b.id == kTabCamera || b.id == kTabMirror;
        const bool active = (b.id == kTabCamera && page_ == Page::Camera) || (b.id == kTabMirror && page_ == Page::Mirror);
        const bool down = pressing && press_hit_ == b.id && b.r.contains(ptrs_[0].x, ptrs_[0].y);
        if (tab) ui.tab(b.r.x, b.r.y, b.r.h, b.label, active, i == focused);
        else ui.button(b.r, b.label, i == focused, down, b.id == kSave);
    }
    if (hidden_) return;
    // The camera's name between the pages and the buttons.
    {
        float x0 = 0, x1 = bar_.w;
        for (const Button& b : buttons_) {
            if (b.id == kTabMirror) x0 = b.r.right() + 10;
            if (b.id == kHide) x1 = b.r.x - 10;
        }
        if (x1 > x0) ui.text_center_in(x0, x1 - x0, std::floor((bar_.h - Ui::kChar) / 2.0f), Ui::clip(cam_.name, x1 - x0),
                                       colors::kAccent);
    }
    // The panel: its rows clipped to it, scrolled when they don't all fit.
    ui.fill_round(panel_.x, panel_.y, panel_.w, panel_.h, 6, panel_bg);
    ui.frame_round(panel_.x, panel_.y, panel_.w, panel_.h, 6, 1, colors::kOutline);
    const SDL_Rect clip{int(panel_.x), int(rows_y_), int(std::ceil(panel_.w)), int(std::ceil(rows_h_))};
    SDL_SetRenderClipRect(ui.renderer(), &clip);
    for (int i = 0; i < int(rows_.size()); ++i) {
        const Row& r = rows_[size_t(i)];
        const float y = rows_y_ + float(i) * row_h_ - scroll_;
        if (y + row_h_ <= rows_y_ || y >= rows_y_ + rows_h_) continue;
        const bool focus = i == sel_ && bar_sel_ < 0;
        if (focus) ui.fill_round(panel_.x + 3, y, panel_.w - 6, row_h_, 4, colors::kHighlight);
        const float ty = std::floor(y + (row_h_ - Ui::kChar) / 2);
        const Color label = !r.enabled ? colors::kDim : focus ? colors::kAccent : colors::kText;
        ui.text(label_x_, ty, Ui::clip(r.label, val_x_ - label_x_ - 6), label);
        const float vr = val_x_ + val_w_; // the value column's right edge
        switch (r.kind) {
        case Row::Kind::Choice: {
            const Color arrow = !r.enabled ? colors::kOutline : focus ? colors::kAccent : colors::kDim;
            float aw = 10;
            if (touch_) { // tap targets for the two halves, as Settings draws them
                aw = 24;
                const float bh = row_h_ - 8, ax1 = vr - aw;
                ui.fill_round(val_x_, y + 4, aw, bh, 5, Color{40, 43, 54});
                ui.fill_round(ax1, y + 4, aw, bh, 5, Color{40, 43, 54});
                ui.text_center_in(val_x_, aw, ty, "<", arrow);
                ui.text_center_in(ax1, aw, ty, ">", arrow);
            } else {
                ui.text(val_x_, ty, "<", arrow);
                ui.text_right(vr, ty, ">", arrow);
            }
            ui.text_center_in(val_x_ + aw, val_w_ - 2 * aw, ty, Ui::clip(r.value, val_w_ - 2 * aw - 4),
                              r.enabled ? colors::kText : colors::kDim);
            break;
        }
        case Row::Kind::Slider:
            ui.slider(val_x_, y + row_h_ / 2, slider_w_, r.frac, focus && r.enabled, r.enabled);
            ui.text_right(vr, ty, r.value, r.enabled ? colors::kText : colors::kDim);
            break;
        case Row::Kind::Action: ui.text_right(vr, ty, r.value.empty() ? ">" : r.value, colors::kDim); break;
        }
    }
    SDL_SetRenderClipRect(ui.renderer(), nullptr);
    ui.scrollbar({panel_.x, rows_y_, panel_.w - 3, rows_h_}, float(rows_.size()) * row_h_, scroll_);
    // What the controls do, at the bottom: for the controller, the keyboard and mouse, or touch,
    // whichever was used last.
    {
        const std::string hint = Ui::clip(legend(), ui.width() - 16);
        const float hw = std::ceil(Ui::text_width(hint)) + 16;
        ui.fill_round((ui.width() - hw) / 2, ui.height() - 18, hw, 15, 4, panel_bg);
        ui.text_center_in(0, ui.width(), ui.height() - 14, hint, colors::kDim);
    }
    if (confirm_) {
        const float cw = 300, ch = touch_ ? 96.0f : 76.0f, cx = (ui.width() - cw) / 2, cy = (ui.height() - ch) / 2;
        ui.fill(0, 0, ui.width(), ui.height(), Color{0, 0, 0, 120});
        ui.fill_round(cx, cy, cw, ch, 8, colors::kPanel);
        ui.frame_round(cx, cy, cw, ch, 8, 1, colors::kOutline);
        ui.text_center_in(cx, cw, cy + 14, is_new_ ? "Leave without saving this camera?" : "Leave without saving your changes?",
                          colors::kText);
        const bool down = pressing && !ptrs_.empty();
        ui.button(keep_, "Keep editing", confirm_sel_ == 0, down && press_hit_ == kKeep);
        ui.button(leave_, "Leave", confirm_sel_ == 1, down && press_hit_ == kLeave);
    }
}

} // namespace gt3r
