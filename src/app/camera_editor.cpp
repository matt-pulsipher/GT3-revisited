#include "app/camera_editor.h"

#include "app/app.h"

#include "race/hood_camera.h"

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

} // namespace

CameraEditor::CameraEditor(race::CustomCamera cam, bool is_new, std::function<race::CustomCamera(int)> preset)
    : cam_(std::move(cam)), start_(cam_), is_new_(is_new), preset_(std::move(preset)) {
    // A camera that keeps the car on screen rotates round it; one on the car turns in place.
    around_ = cam_.car != race::CustomCamera::OwnCar::Hidden;
    keep_mount_offset();
    build_rows();
}

void CameraEditor::begin() {
    if (!is_new_ || !preset_) return;
    race::CustomCamera p = preset_(0);
    p.id = cam_.id, p.name = cam_.name, p.enabled = cam_.enabled;
    cam_ = start_ = p;
    around_ = cam_.car != race::CustomCamera::OwnCar::Hidden;
    keep_mount_offset();
    build_rows();
}

// --- the panel ------------------------------------------------------------------------------

void CameraEditor::build_rows() {
    rows_.clear();
    race::CustomCamera& c = cam_;
    auto choice = [&](std::string label, std::vector<std::string> names, int idx, std::function<void(int)> set) {
        Row r;
        r.kind = Row::Kind::Choice;
        r.label = std::move(label);
        const int n = int(names.size());
        r.value = names[size_t(std::clamp(idx, 0, n - 1))];
        r.change = [set, idx, n](int d) { set(std::clamp(idx + d, 0, n - 1)); };
        r.activate = [set, idx, n] { set((idx + 1) % n); };
        rows_.push_back(std::move(r));
    };
    auto slider = [&](std::string label, float v, float lo, float hi, float step, std::string text, std::function<void(float)> set) {
        Row r;
        r.kind = Row::Kind::Slider;
        r.label = std::move(label);
        r.value = std::move(text);
        r.frac = (v - lo) / (hi - lo);
        r.change = [=](int d) { set(snap(v + float(d) * step, lo, hi, step)); };
        r.set_frac = [=](float f) { set(snap(lo + std::clamp(f, 0.0f, 1.0f) * (hi - lo), lo, hi, step)); };
        rows_.push_back(std::move(r));
    };
    auto action = [&](std::string label, std::string value, std::function<void()> fn) {
        Row r;
        r.kind = Row::Kind::Action;
        r.label = std::move(label);
        r.value = std::move(value);
        r.activate = std::move(fn);
        rows_.push_back(std::move(r));
    };
    if (page_ == Page::Camera) {
        // Movement stays the first row on every mount: the rows below it change with the mount.
        choice("Movement", {"Smooth follow", "On the bonnet", "Fixed to the car"},
               c.mount == race::CustomCamera::Mount::Body ? 2 : c.mount == race::CustomCamera::Mount::Hood ? 1 : 0,
               [this, &c](int i) {
                   const race::CustomCamera::Mount m = i == 2 ? race::CustomCamera::Mount::Body
                                                            : i == 1 ? race::CustomCamera::Mount::Hood
                                                                     : race::CustomCamera::Mount::Follow;
                   if (m == c.mount) return;
                   // Position mounts keep the eye and aim they last had (switching between them
                   // changes only the mount); the bonnet starts at its default centred seat,
                   // aimed ahead — a position mount's aim is usually turned back at the car,
                   // which on the bonnet is the inside of the cabin.
                   keep_mount_offset();
                   c.mount = m;
                   if (m == race::CustomCamera::Mount::Hood) {
                       c.offset = {};
                       c.yaw = c.pitch = c.roll = 0;
                   } else {
                       c.offset = pos_offset_;
                       c.yaw = pos_yaw_, c.pitch = pos_pitch_, c.roll = pos_roll_;
                   }
               });
        // On the bonnet drags only aim the view (rotate in place): no orbit to hide behind.
        if (c.mount != race::CustomCamera::Mount::Hood)
            choice("Rotate", {"Around the car", "In place"}, around_ ? 0 : 1, [this](int i) { around_ = i == 0; });
        // On the bonnet the offset nudges the seat the car's shape gives (race/hood_camera.h);
        // drags can't move it — these rows are the precise moves.
        if (c.mount == race::CustomCamera::Mount::Hood) {
            slider("Fore/aft", c.offset.z, -race::kHoodForward, race::kHoodForward, 0.05f, metres(c.offset.z),
                   [&c](float v) { c.offset.z = v; });
            slider("Sideways", c.offset.x, -race::kHoodLateral, race::kHoodLateral, 0.05f, metres(c.offset.x),
                   [&c](float v) { c.offset.x = v; });
            slider("Height", c.offset.y, race::kHoodHeightMin, race::kHoodHeightMax, 0.05f, metres(c.offset.y),
                   [&c](float v) { c.offset.y = v; });
            action("Reset seat", "", [&c] { c.offset = {}; });
        }
        slider("Field of view", c.fov, race::CustomCamera::kMinFov, race::CustomCamera::kMaxFov, 1, degrees(c.fov),
               [&c](float v) { c.fov = v; });
        if (c.mount != race::CustomCamera::Mount::Follow)
            slider("Horizon lock", c.roll_lock, 0, 1, 0.1f,
                   std::to_string(int(std::lround(c.roll_lock * 100))) + "%", [&c](float v) { c.roll_lock = v; });
        slider("Roll", c.roll, -race::CustomCamera::kMaxRoll, race::CustomCamera::kMaxRoll, 1, degrees(c.roll),
               [&c](float v) { c.roll = v; });
        // The view straight along the car (no turn, tilt or roll), the camera where it is.
        action("Reset rotation", "", [&c] { c.yaw = c.pitch = c.roll = 0; });
        choice("Your car", {"Shown", "Hidden", "Facing forward"},
               c.car == race::CustomCamera::OwnCar::Hidden ? 1 : c.car == race::CustomCamera::OwnCar::Ahead ? 2 : 0,
               [&c](int i) {
                   c.car = i == 1 ? race::CustomCamera::OwnCar::Hidden
                                : i == 2 ? race::CustomCamera::OwnCar::Ahead
                                         : race::CustomCamera::OwnCar::Shown;
               });
        choice("HUD layout", {"Chase", "In-car"}, c.incar_hud ? 1 : 0, [&c](int i) { c.incar_hud = i == 1; });
        {
            Row r;
            r.kind = Row::Kind::Choice;
            r.label = "Start from";
            r.value = kPresetNames[preset_sel_];
            // Each view as it is picked (the arrows go round); Cross / a tap on the label takes it again.
            // The view only: the name, the switch and the mirror's place stay.
            auto take = [this](int i) {
                preset_sel_ = (i + kPresets) % kPresets;
                if (!preset_) return;
                race::CustomCamera p = preset_(preset_sel_);
                p.id = cam_.id, p.name = cam_.name, p.enabled = cam_.enabled;
                p.mirror_x = cam_.mirror_x, p.mirror_y = cam_.mirror_y, p.mirror_size = cam_.mirror_size;
                cam_ = p;
                around_ = cam_.car != race::CustomCamera::OwnCar::Hidden;
                keep_mount_offset();
            };
            r.change = [this, take](int d) { take(preset_sel_ + d); };
            r.activate = [this, take] { take(preset_sel_); };
            rows_.push_back(std::move(r));
        }
        action("Undo changes", "", [this] {
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
        action("Usual place", "", [&c] { c.mirror_x = race::CustomCamera::kMirrorX, c.mirror_y = race::CustomCamera::kMirrorY; });
        action("Usual size", "", [&c] { c.mirror_size = 1; });
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
    // Rows as tall as fit above the hint line (a touch screen's are finger-sized, 30 at least).
    const float room = h - 18 - (bar_h + 6) - 8;
    row_h_ = std::max(touch ? 30.0f : 16.0f, std::min(touch ? 34.0f : 20.0f, std::floor(room / float(rows_.size()))));
    const float pw = std::min(touch ? 262.0f : 228.0f, w * 0.45f);
    panel_ = {w - pw - 6, bar_h + 6, pw, float(rows_.size()) * row_h_ + 8};
    if (panel_.bottom() > h - 18) panel_.h = h - 18 - panel_.y;
    rows_y_ = panel_.y + 4;
    label_x_ = panel_.x + 8;
    val_x_ = panel_.x + std::floor(pw * 0.42f);
    val_w_ = panel_.right() - 8 - val_x_;
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
        const int i = int(std::floor((y - rows_y_) / row_h_));
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
    case kTabCamera: page_ = Page::Camera, sel_ = 0; break;
    case kTabMirror: page_ = Page::Mirror, sel_ = 0; break;
    case kHide: hidden_ = true; break;
    case kShow: hidden_ = false; break;
    case kCancel: leave_request(); break;
    case kSave: pending_ = Result::Save; break;
    case kKeep: confirm_ = false; break;
    case kLeave: pending_ = Result::Cancel; break;
    case kRow: {
        if (row < 0 || row >= int(rows_.size())) return;
        sel_ = row;
        Row& r = rows_[size_t(row)];
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

// --- the scene's moves ----------------------------------------------------------------------

float CameraEditor::pivot_distance(const Scene& s) const { return render::length(cam_.offset - s.pivot); }

void CameraEditor::rotate(const Scene& s, float dx, float dy, bool around) {
    // On the bonnet the seat nudges are panel rows only: drags aim the view in place.
    if (cam_.mount == race::CustomCamera::Mount::Hood) around = false;
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
    if (cam_.mount == race::CustomCamera::Mount::Hood) return; // (the seat's sliders)
    // The picture at the car's distance follows the pointer.
    const float d = std::max(pivot_distance(s), 1.0f);
    const float k = 2 * d * std::tan(cam_.fovy() / 2) / std::max(s.frame.h, 1.0f);
    race::pan_camera(cam_, -dx * k, dy * k, 0);
    keep_above_road(s);
}

void CameraEditor::dolly(const Scene& s, float amount) {
    if (cam_.mount == race::CustomCamera::Mount::Hood) return; // (the seat's sliders)
    race::pan_camera(cam_, 0, 0, amount * std::max(pivot_distance(s), 1.0f));
    keep_above_road(s);
}

void CameraEditor::move_mirror(const Scene& s, float dx, float dy) {
    cam_.mirror_x = std::clamp(cam_.mirror_x + dx / std::max(s.frame.w, 1.0f), 0.0f, 1.0f);
    cam_.mirror_y = std::clamp(cam_.mirror_y + dy / std::max(s.frame.h, 1.0f), 0.0f, 1.0f);
}

// --- input ------------------------------------------------------------------------------------

void CameraEditor::event(const SDL_Event& e, const Scene& s) {
    // Fingers lifted while the app is away never report "up".
    if (e.type == SDL_EVENT_WILL_ENTER_BACKGROUND || e.type == SDL_EVENT_DID_ENTER_BACKGROUND ||
        e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        ptrs_.clear();
        drag_ = Drag::None, mouse_button_ = 0, slider_ = false;
        return;
    }
    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        const float steps = e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f);
        if (confirm_ || steps == 0 || hit(e.wheel.mouse_x, e.wheel.mouse_y) != kNone) return;
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
        if (ptrs_.size() == 1) {
            int row = -1;
            press_hit_ = hit(pe.x, pe.y, &row);
            press_row_ = row;
            slider_ = false;
            if (press_hit_ != kNone) {
                drag_ = Drag::Ui;
                // A slider follows the pointer from the press.
                if (press_hit_ == kRow && row >= 0 && rows_[size_t(row)].kind == Row::Kind::Slider && pe.x >= val_x_ - 6) {
                    sel_ = row, slider_ = true;
                    rows_[size_t(row)].set_frac((pe.x - val_x_) / slider_w_);
                    build_rows();
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
        if (drag_ == Drag::Ui && slider_ && press_row_ >= 0 && press_row_ < int(rows_.size())) {
            rows_[size_t(press_row_)].set_frac((pe.x - val_x_) / slider_w_);
            build_rows();
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
        if (drag_ == Drag::Ui && ptrs_.empty() && !slider_) {
            int row = -1;
            const Hit h = hit(pe.x, pe.y, &row);
            if (h == press_hit_ && (h != kRow || row == press_row_)) tap(h, row, pe.x);
        }
        if (ptrs_.empty()) drag_ = Drag::None, slider_ = false;
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
        // On the bonnet the seat is panel rows only: the left stick and the triggers can't move it.
        if (cam_.mount != race::CustomCamera::Mount::Hood) {
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
    if (m.pressed(PadButton::Select)) hidden_ = !hidden_;
    if (m.pressed(PadButton::L1) || m.pressed(PadButton::R1)) {
        page_ = page_ == Page::Camera ? Page::Mirror : Page::Camera;
        sel_ = 0;
        build_rows();
    }
    if (!hidden_ && !rows_.empty()) {
        if (fire[0]) sel_ = (sel_ + int(rows_.size()) - 1) % int(rows_.size());
        if (fire[1]) sel_ = (sel_ + 1) % int(rows_.size());
        Row& r = rows_[size_t(sel_)];
        if ((fire[2] || fire[3]) && r.change) r.change(fire[3] ? 1 : -1);
        if (m.pressed(PadButton::Cross) && r.activate) r.activate();
        build_rows();
    }
    pad_input(app, s, dt);
    cam_.clamp();
    return Result::None;
}

// --- drawing ------------------------------------------------------------------------------------

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
    // The buttons (and the bar behind them).
    if (!hidden_) ui.fill(bar_.x, bar_.y, bar_.w, bar_.h, panel_bg);
    const bool pressing = drag_ == Drag::Ui && !ptrs_.empty();
    for (const Button& b : buttons_) {
        const bool tab = b.id == kTabCamera || b.id == kTabMirror;
        const bool active = (b.id == kTabCamera && page_ == Page::Camera) || (b.id == kTabMirror && page_ == Page::Mirror);
        const bool down = pressing && press_hit_ == b.id && b.r.contains(ptrs_[0].x, ptrs_[0].y);
        if (tab) ui.tab(b.r.x, b.r.y, b.r.h, b.label, active, false);
        else ui.button(b.r, b.label, false, down, b.id == kSave);
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
    // The panel.
    ui.fill_round(panel_.x, panel_.y, panel_.w, panel_.h, 6, panel_bg);
    ui.frame_round(panel_.x, panel_.y, panel_.w, panel_.h, 6, 1, colors::kOutline);
    for (int i = 0; i < int(rows_.size()); ++i) {
        const Row& r = rows_[size_t(i)];
        const float y = rows_y_ + float(i) * row_h_;
        if (y + row_h_ > panel_.bottom()) break;
        const bool focus = i == sel_;
        if (focus) ui.fill_round(panel_.x + 3, y, panel_.w - 6, row_h_, 4, colors::kHighlight);
        const float ty = std::floor(y + (row_h_ - Ui::kChar) / 2);
        ui.text(label_x_, ty, Ui::clip(r.label, val_x_ - label_x_ - 6), focus ? colors::kAccent : colors::kText);
        switch (r.kind) {
        case Row::Kind::Choice: {
            const Color arrow = focus ? colors::kAccent : colors::kDim;
            float aw = 10;
            if (touch_) { // tap targets for the two halves, as Settings draws them
                aw = 24;
                const float bh = row_h_ - 8, ax1 = panel_.right() - 8 - aw;
                ui.fill_round(val_x_, y + 4, aw, bh, 5, Color{40, 43, 54});
                ui.fill_round(ax1, y + 4, aw, bh, 5, Color{40, 43, 54});
                ui.text_center_in(val_x_, aw, ty, "<", arrow);
                ui.text_center_in(ax1, aw, ty, ">", arrow);
            } else {
                ui.text(val_x_, ty, "<", arrow);
                ui.text_right(panel_.right() - 8, ty, ">", arrow);
            }
            ui.text_center_in(val_x_ + aw, val_w_ - 2 * aw, ty, Ui::clip(r.value, val_w_ - 2 * aw - 4), colors::kText);
            break;
        }
        case Row::Kind::Slider:
            ui.slider(val_x_, y + row_h_ / 2, slider_w_, r.frac, focus);
            ui.text_right(panel_.right() - 8, ty, r.value, colors::kText);
            break;
        case Row::Kind::Action:
            ui.text_right(panel_.right() - 8, ty, r.value.empty() ? ">" : r.value, colors::kDim);
            break;
        }
    }
    // What the pointer and the pad do, at the bottom.
    {
        const bool mirror = page_ == Page::Mirror;
        std::string hint;
        if (ui.touch())
            hint = mirror ? "Drag: move the mirror    Pinch: size" : "Drag: rotate    Two fingers: move    Pinch: nearer / farther";
        else
            hint = mirror ? "Drag: move the mirror    Wheel: size    Pad: sticks move and size it"
                          : "Drag: rotate    Right drag: other way    Middle / Shift drag: move    Wheel: nearer    Ctrl+wheel: zoom";
        hint = Ui::clip(hint, ui.width() - 16);
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
