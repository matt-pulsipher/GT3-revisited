#include "input/touch_controls.h"

#include "base/config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gt3r {

namespace {

// The 640x360 UI (Ui::kWidth/kHeight; src/app is not visible here): the steering range's and
// the steering zone's scale. The game area's own size is set_game_area's.
constexpr float kW = 640.0f;
constexpr float kH = 360.0f;
constexpr float kMargin = 14.0f;
constexpr float kGap = 10.0f;
constexpr float kHitScale = 1.2f; // circles take touches out to 1.2 x their radius: fingers are bigger

float parse_float(const std::optional<std::string>& s, float def, float lo, float hi) {
    if (!s) return def;
    char* end = nullptr;
    float v = std::strtof(s->c_str(), &end);
    if (end == s->c_str()) return def;
    return std::clamp(v, lo, hi);
}

std::string format_float(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

// Placement keys (TouchConfig::placements) of each layout's controls, in config.ini order.
constexpr const char* kDriveKeys[] = {"drive.gas",       "drive.brake", "drive.up",     "drive.down",
                                      "drive.handbrake", "drive.pause", "drive.camera"};
constexpr const char* kMenuKeys[] = {"menu.dpad", "menu.buttons", "menu.l1", "menu.r1", "menu.select", "menu.start"};

std::string_view layout_prefix(TouchLayout layout) {
    return layout == TouchLayout::Driving ? "drive." : layout == TouchLayout::Menu ? "menu." : "";
}

std::string format_units(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), v == std::round(v) ? "%.0f" : "%.1f", v);
    return buf;
}

// `c` moved so that [c - half, c + half] lies within [lo, hi]; centred when it can't fit.
float fit(float c, float half, float lo, float hi) {
    if (hi - lo < 2 * half) return (lo + hi) / 2;
    return std::clamp(c, lo + half, hi - half);
}

// A widget's drawn bounds (circles: centre and radius).
TouchRect drawn_bounds(const TouchControls::Widget& w) {
    if (w.shape == TouchControls::Widget::Shape::Circle) return {w.x - w.w, w.y - w.h, 2 * w.w, 2 * w.h};
    return {w.x, w.y, w.w, w.h};
}

TouchRect unite(const TouchRect& a, const TouchRect& b) {
    if (a.w <= 0 && a.h <= 0) return b;
    const float x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    const float x1 = std::max(a.x + a.w, b.x + b.w), y1 = std::max(a.y + a.h, b.y + b.h);
    return {x0, y0, x1 - x0, y1 - y0};
}

void press(PadState& pad, PadButton b, float v) {
    size_t i = static_cast<size_t>(b);
    pad.buttons = u16(pad.buttons | (1u << i));
    u8 p = is_pressure_sensitive(b) ? u8(std::clamp(std::lround(v * 255.0f), 1L, 255L)) : u8(255);
    pad.pressure[i] = std::max(pad.pressure[i], p);
}

} // namespace

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

TouchPlacement TouchPlacement::parse(std::string_view s) {
    std::vector<std::string> tokens;
    std::string cur;
    for (char ch : s) {
        if (ch == ' ' || ch == '\t' || ch == ',') {
            if (!cur.empty()) tokens.push_back(to_lower(cur)), cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) tokens.push_back(to_lower(cur));
    auto number = [&](size_t i, float& out) {
        if (i >= tokens.size()) return false;
        char* end = nullptr;
        float v = std::strtof(tokens[i].c_str(), &end);
        if (end == tokens[i].c_str() || !std::isfinite(v)) return false;
        out = std::clamp(v, -4000.0f, 4000.0f);
        return true;
    };
    TouchPlacement p;
    bool have_y = false;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        float v = 0;
        if ((t == "left" || t == "center" || t == "centre" || t == "right") && number(i + 1, v)) {
            p.ax = t == "left" ? AnchorX::Left : t == "right" ? AnchorX::Right : AnchorX::Centre;
            p.x = v, ++i;
        } else if ((t == "top" || t == "bottom") && number(i + 1, v)) {
            p.ay = t == "top" ? AnchorY::Top : AnchorY::Bottom;
            p.y = v, have_y = true, ++i;
        } else if (t == "size" && number(i + 1, v)) {
            p.scale = std::clamp(v, kMinScale, kMaxScale), ++i;
        } else if (t == "hidden") {
            p.hidden = true;
        }
    }
    if (!have_y) p.ax = AnchorX::Default; // half a position: the default one
    if (!p.moved()) p.ay = AnchorY::Bottom, p.x = p.y = 0;
    return p;
}

std::string TouchPlacement::to_string() const {
    std::string out;
    auto part = [&](const std::string& s) { out += (out.empty() ? "" : " ") + s; };
    if (moved()) {
        part(ax == AnchorX::Left ? "left" : ax == AnchorX::Right ? "right" : "center");
        part(format_units(x));
        part(ay == AnchorY::Top ? "top" : "bottom");
        part(format_units(y));
    }
    if (scale != 1.0f) part("size " + format_float(scale));
    if (hidden) part("hidden");
    return out;
}

TouchPlacement TouchConfig::placement(std::string_view key) const {
    auto it = placements.find(key);
    return it == placements.end() ? TouchPlacement{} : it->second;
}

void TouchConfig::set_placement(std::string_view key, const TouchPlacement& p) {
    if (p.is_default()) {
        if (auto it = placements.find(key); it != placements.end()) placements.erase(it);
    } else {
        placements.insert_or_assign(std::string(key), p);
    }
}

void TouchConfig::reset_placements(TouchLayout layout) {
    const std::string_view prefix = layout_prefix(layout);
    if (!prefix.empty()) std::erase_if(placements, [&](const auto& e) { return e.first.starts_with(prefix); });
}

bool TouchConfig::customized(TouchLayout layout) const {
    const std::string_view prefix = layout_prefix(layout);
    if (prefix.empty()) return false;
    for (const auto& e : placements)
        if (e.first.starts_with(prefix)) return true;
    return false;
}

TouchConfig TouchConfig::load(const IniDocument& ini) {
    TouchConfig c;
    auto get = [&](const char* key) { return ini.get("touch", key); };
    if (auto v = get("mode")) {
        std::string m = to_lower(*v);
        c.mode = m == "on" || m == "1" ? Mode::On : m == "off" || m == "0" ? Mode::Off : Mode::Auto;
    }
    if (auto v = get("steering")) c.steering = to_lower(*v) == "tilt" ? Steering::Tilt : Steering::Drag;
    if (auto v = get("pedals")) {
        std::string m = to_lower(*v);
        c.pedals = m == "area" ? Pedals::Area : m == "full" ? Pedals::Full : Pedals::Position;
    }
    c.pedal_min = parse_float(get("pedal_min"), c.pedal_min, 0.0f, 1.0f);
    c.steer_range = parse_float(get("steer_range"), c.steer_range, 0.03f, 0.5f);
    c.tilt_deadzone = parse_float(get("tilt_deadzone"), c.tilt_deadzone, 0.0f, 0.5f);
    c.opacity = parse_float(get("opacity"), c.opacity, 0.05f, 1.0f);
    c.size = parse_float(get("size"), c.size, 0.6f, 1.6f);
    if (auto v = get("left_handed")) c.left_handed = to_lower(*v) == "on" || *v == "1";
    auto load_keys = [&](const auto& keys) {
        for (const char* k : keys)
            if (auto v = get(k)) c.set_placement(k, TouchPlacement::parse(*v));
    };
    load_keys(kDriveKeys);
    load_keys(kMenuKeys);
    return c;
}

void TouchConfig::save(IniDocument& ini) const {
    ini.set("touch", "mode", mode == Mode::On ? "on" : mode == Mode::Off ? "off" : "auto");
    ini.set("touch", "steering", steering == Steering::Tilt ? "tilt" : "drag");
    ini.set("touch", "pedals", pedals == Pedals::Area ? "area" : pedals == Pedals::Full ? "full" : "position");
    ini.set("touch", "pedal_min", format_float(pedal_min));
    ini.set("touch", "steer_range", format_float(steer_range));
    ini.set("touch", "tilt_deadzone", format_float(tilt_deadzone));
    ini.set("touch", "opacity", format_float(opacity));
    ini.set("touch", "size", format_float(size));
    ini.set("touch", "left_handed", left_handed ? "on" : "off");
    auto save_keys = [&](const auto& keys) {
        for (const char* k : keys) {
            if (auto it = placements.find(k); it != placements.end()) ini.set("touch", k, it->second.to_string());
            else ini.remove("touch", k);
        }
    };
    save_keys(kDriveKeys);
    save_keys(kMenuKeys);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void TouchControls::configure(const TouchConfig& cfg) {
    cfg_ = cfg;
    release_all();
    build_layout();
}

void TouchControls::set_layout(TouchLayout layout) {
    if (layout == layout_) return;
    layout_ = layout;
    release_all(); // never carry a held control into another context
    build_layout();
}

TouchControls::Area TouchControls::area() const {
    // Anchor to the whole screen, not just the 16:9 game area, so wide phones
    // get the controls in the side bars and tablets in the top/bottom bars.
    Area a;
    a.L = screen_x0_, a.R = screen_x1_, a.T = screen_y0_, a.B = screen_y1_, a.mid = (a.L + a.R) / 2;
    // Edges of the area the controls sit in: kMargin from the screen edge, or
    // clear of a cutout / rounded corner inset.
    // The layout is built right-handed and mirrored afterwards, so mirrored
    // layouts swap the left and right insets.
    const bool mirror = cfg_.left_handed;
    const float il = mirror ? inset_r_ : inset_l_, ir = mirror ? inset_l_ : inset_r_;
    a.mL = a.L + std::max(kMargin, il + 4), a.mR = a.R - std::max(kMargin, ir + 4);
    a.mT = a.T + std::max(kMargin, inset_t_ + 4), a.mB = a.B - std::max(kMargin, inset_b_ + 4);
    return a;
}

void TouchControls::build_layout() {
    widgets_.clear();
    const float s = cfg_.size;
    const Area A = area();
    const float L = A.L, B = A.B, cx_mid = A.mid;
    const float mL = A.mL, mR = A.mR, mT = A.mT, mB = A.mB;
    auto add = [&](Widget w, const char* key) {
        w.key = key;
        widgets_.push_back(std::move(w));
    };
    auto button = [&](float cx, float cy, float r, PadButton b, std::string label) {
        Widget w;
        w.kind = Widget::Kind::Button;
        w.shape = Widget::Shape::Circle;
        w.x = cx, w.y = cy, w.w = r, w.h = r;
        w.button = b;
        w.label = std::move(label);
        return w;
    };
    auto rect_button = [&](float x, float y, float w_, float h, PadButton b, std::string label) {
        Widget w = button(0, 0, 0, b, std::move(label));
        w.shape = Widget::Shape::Rect;
        w.x = x, w.y = y, w.w = w_, w.h = h;
        return w;
    };

    if (layout_ == TouchLayout::Driving) {
        // Steering zone first: everything after it takes priority in widget_at.
        Widget steer;
        steer.kind = Widget::Kind::Steering;
        steer.shape = Widget::Shape::Rect;
        steer.x = L, steer.y = kH * 0.22f, steer.w = cx_mid - L, steer.h = B - steer.y;
        add(steer, "");

        const float pw = 62 * s, ph = 132 * s;
        Widget gas;
        gas.kind = Widget::Kind::Pedal;
        gas.shape = Widget::Shape::Rect;
        gas.x = mR - pw, gas.y = mB - ph, gas.w = pw, gas.h = ph;
        gas.button = PadButton::Cross;
        gas.label = "GAS";
        Widget brake = gas;
        brake.x = gas.x - kGap - pw;
        brake.button = PadButton::Square;
        brake.label = "BRAKE";
        add(gas, "drive.gas");
        add(brake, "drive.brake");

        const float r = 22 * s;
        // The race pad's buttons in GT3's meaning (race/controls.h), whatever the race layout.
        add(button(gas.x + pw / 2, gas.y - kGap - r, r, PadButton::R2, "UP"), "drive.up");
        add(button(brake.x + pw / 2, brake.y - kGap - r, r, PadButton::L2, "DOWN"), "drive.down");
        const float hb = 18 * s;
        add(button(brake.x - kGap - hb, mB - hb, hb, PadButton::Circle, "HB"), "drive.handbrake");

        const float sr = 15 * s;
        add(button(cx_mid, mT + sr, sr, PadButton::Start, "||"), "drive.pause");
        add(button(cx_mid + 2.6f * sr, mT + sr, sr, PadButton::R1, "CAM"), "drive.camera");
    } else if (layout_ == TouchLayout::Menu) {
        const float half = 54 * s;
        Widget pad;
        pad.kind = Widget::Kind::DPad;
        pad.shape = Widget::Shape::Rect;
        pad.x = mL, pad.y = mB - 2 * half, pad.w = 2 * half, pad.h = 2 * half;
        add(pad, "menu.dpad");

        const float r = 19 * s, off = 34 * s;
        const float cx = mR - half, cy = mB - half;
        add(button(cx, cy + off, r, PadButton::Cross, ""), "menu.buttons");
        add(button(cx + off, cy, r, PadButton::Circle, ""), "menu.buttons");
        add(button(cx - off, cy, r, PadButton::Square, ""), "menu.buttons");
        add(button(cx, cy - off, r, PadButton::Triangle, ""), "menu.buttons");

        const float bw = 40 * s, bh = 20 * s;
        add(rect_button(mL, mT, bw, bh, PadButton::L1, "L1"), "menu.l1");
        add(rect_button(mR - bw, mT, bw, bh, PadButton::R1, "R1"), "menu.r1");
        add(rect_button(cx_mid - bw - 4, mT, bw, bh, PadButton::Select, "SEL"), "menu.select");
        add(rect_button(cx_mid + 4, mT, bw, bh, PadButton::Start, "START"), "menu.start");
    }
    apply_placements(A);
    if (cfg_.left_handed)
        for (Widget& w : widgets_) w.x = w.shape == Widget::Shape::Circle ? A.L + A.R - w.x : A.L + A.R - w.x - w.w;
}

void TouchControls::apply_placements(const Area& a) {
    for (size_t i = 0; i < widgets_.size(); ++i) {
        const std::string_view key = widgets_[i].key;
        if (key.empty()) continue;
        bool first = true; // each group once, at its first widget
        for (size_t j = 0; j < i && first; ++j) first = key != widgets_[j].key;
        auto it = cfg_.placements.find(key);
        if (!first || it == cfg_.placements.end()) continue;
        const TouchPlacement& p = it->second;
        TouchRect b;
        for (size_t j = i; j < widgets_.size(); ++j)
            if (key == widgets_[j].key) b = unite(b, drawn_bounds(widgets_[j]));
        const float gx = b.x + b.w / 2, gy = b.y + b.h / 2, k = p.scale;
        float nx = gx, ny = gy;
        if (p.moved()) {
            using AX = TouchPlacement::AnchorX;
            nx = p.ax == AX::Left ? a.mL + p.x : p.ax == AX::Right ? a.mR - p.x : a.mid + p.x;
            ny = p.ay == TouchPlacement::AnchorY::Top ? a.mT + p.y : a.mB - p.y;
        }
        // Never off the screen or under a cutout, wherever the screen's shape puts it.
        nx = fit(nx, b.w * k / 2, a.mL, a.mR), ny = fit(ny, b.h * k / 2, a.mT, a.mB);
        for (size_t j = i; j < widgets_.size(); ++j) {
            Widget& w = widgets_[j];
            if (key != w.key) continue;
            const bool round = w.shape == Widget::Shape::Circle;
            const float cx = round ? w.x : w.x + w.w / 2, cy = round ? w.y : w.y + w.h / 2;
            const float ncx = nx + (cx - gx) * k, ncy = ny + (cy - gy) * k;
            w.w *= k, w.h *= k;
            w.x = round ? ncx : ncx - w.w / 2, w.y = round ? ncy : ncy - w.h / 2;
            w.hidden = p.hidden;
        }
    }
}

std::string_view TouchControls::key_at(float x, float y) const {
    const int i = widget_at(x, y, true);
    return i < 0 ? std::string_view{} : std::string_view(widgets_[size_t(i)].key);
}

TouchRect TouchControls::group_bounds(std::string_view key) const {
    TouchRect b;
    if (key.empty()) return b;
    for (const Widget& w : widgets_)
        if (key == w.key) b = unite(b, drawn_bounds(w));
    return b;
}

TouchPlacement TouchControls::placement_at(std::string_view key, float x, float y) const {
    TouchPlacement p = cfg_.placement(key);
    const TouchRect b = group_bounds(key);
    if (b.w <= 0 || b.h <= 0) return p;
    const Area a = area();
    if (cfg_.left_handed) x = a.L + a.R - x; // placements are right-handed
    x = fit(x, b.w / 2, a.mL, a.mR), y = fit(y, b.h / 2, a.mT, a.mB);
    // Measured from the nearest edge (or the centre line), so the control keeps its
    // distance from the thumbs on screens of other shapes.
    using AX = TouchPlacement::AnchorX;
    const float third = (a.mR - a.mL) / 3;
    if (x < a.mL + third) p.ax = AX::Left, p.x = x - a.mL;
    else if (x > a.mR - third) p.ax = AX::Right, p.x = a.mR - x;
    else p.ax = AX::Centre, p.x = x - a.mid;
    if (y < (a.mT + a.mB) / 2) p.ay = TouchPlacement::AnchorY::Top, p.y = y - a.mT;
    else p.ay = TouchPlacement::AnchorY::Bottom, p.y = a.mB - y;
    p.x = std::round(p.x), p.y = std::round(p.y);
    return p;
}

void TouchControls::set_game_area(float w, float h) {
    if (!(w > 0) || !(h > 0) || (w == game_w_ && h == game_h_)) return;
    game_w_ = w, game_h_ = h;
    set_screen(screen_x0_, screen_y0_, screen_x1_, screen_y1_);
}

void TouchControls::set_screen(float x0, float y0, float x1, float y1) {
    // Never smaller than the game area; ignore sub-unit jitter.
    x0 = std::min(x0, 0.0f), y0 = std::min(y0, 0.0f), x1 = std::max(x1, game_w_), y1 = std::max(y1, game_h_);
    if (std::fabs(x0 - screen_x0_) < 0.5f && std::fabs(y0 - screen_y0_) < 0.5f && std::fabs(x1 - screen_x1_) < 0.5f &&
        std::fabs(y1 - screen_y1_) < 0.5f)
        return;
    screen_x0_ = x0, screen_y0_ = y0, screen_x1_ = x1, screen_y1_ = y1;
    // Same widgets in the same order, so held fingers stay valid.
    build_layout();
}

void TouchControls::set_insets(float left, float top, float right, float bottom) {
    left = std::max(left, 0.0f), top = std::max(top, 0.0f), right = std::max(right, 0.0f), bottom = std::max(bottom, 0.0f);
    if (std::fabs(left - inset_l_) < 0.5f && std::fabs(top - inset_t_) < 0.5f && std::fabs(right - inset_r_) < 0.5f &&
        std::fabs(bottom - inset_b_) < 0.5f)
        return;
    inset_l_ = left, inset_t_ = top, inset_r_ = right, inset_b_ = bottom;
    build_layout(); // same widgets in the same order: held fingers stay valid
}

std::vector<TouchRect> TouchControls::control_bounds() const {
    std::vector<TouchRect> out;
    if (!visible()) return out;
    for (const Widget& w : widgets_)
        if (w.kind != Widget::Kind::Steering && !w.hidden) out.push_back(drawn_bounds(w)); // the zone is only a hint
    return out;
}

TouchRect TouchControls::safe_area() const {
    const float W = game_w_, H = game_h_;
    TouchRect full{0, 0, W, H};
    if (!visible()) return full;
    float left = 0, right = W, top = 0;
    for (const TouchRect& b : control_bounds()) {
        float x0 = b.x, x1 = b.x + b.w, y0 = b.y, y1 = b.y + b.h;
        float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
        if (cy < H * 0.25f) top = std::max(top, y1);
        else if (cx < W / 2) left = std::max(left, x1);
        else right = std::min(right, x0);
    }
    left = std::clamp(left, 0.0f, W / 2), right = std::clamp(right, W / 2, W), top = std::clamp(top, 0.0f, H / 2);
    return {left, top, right - left, H - top};
}

TouchRect TouchControls::clear_above(TouchRect r) const {
    if (!visible()) return r;
    // Each move goes up, so once above a control `r` stays above it.
    for (bool moved = true; moved;) {
        moved = false;
        for (const Widget& w : widgets_) {
            if (w.kind == Widget::Kind::Steering || w.hidden) continue;
            const bool round = w.shape == Widget::Shape::Circle;
            const float hr = w.w * kHitScale;
            const float x0 = round ? w.x - hr : w.x, x1 = round ? w.x + hr : w.x + w.w;
            const float y0 = round ? w.y - hr : w.y, y1 = round ? w.y + hr : w.y + w.h;
            if (r.x < x1 && x0 < r.x + r.w && r.y < y1 && y0 < r.y + r.h) {
                r.y = y0 - kGap - r.h;
                moved = true;
            }
        }
    }
    return r;
}

int TouchControls::widget_at(float x, float y, bool hidden) const {
    for (int i = int(widgets_.size()) - 1; i >= 0; --i) {
        const Widget& w = widgets_[size_t(i)];
        if (w.hidden && !hidden) continue;
        if (w.shape == Widget::Shape::Circle) {
            float r = w.w * kHitScale;
            float dx = x - w.x, dy = y - w.y;
            if (dx * dx + dy * dy <= r * r) return i;
        } else if (x >= w.x && x < w.x + w.w && y >= w.y && y < w.y + w.h) {
            return i;
        }
    }
    return -1;
}

bool TouchControls::hit(float x, float y) const { return visible() && widget_at(x, y) >= 0; }

int TouchControls::dpad_direction(const Widget& w, float x, float y) const {
    float dx = x - (w.x + w.w / 2), dy = y - (w.y + w.h / 2);
    float dead = w.w * 0.12f;
    if (std::fabs(dx) < dead && std::fabs(dy) < dead) return -1;
    if (std::fabs(dx) > std::fabs(dy)) return int(dx > 0 ? PadButton::Right : PadButton::Left);
    return int(dy > 0 ? PadButton::Down : PadButton::Up);
}

float TouchControls::pedal_value(const Widget& w, const Finger& f) const {
    switch (cfg_.pedals) {
    case TouchConfig::Pedals::Full: return 1.0f;
    case TouchConfig::Pedals::Area:
        // Touchscreens without pressure report a constant 1 (or 0): full.
        if (f.pressure <= 0.0f || f.pressure >= 1.0f) return 1.0f;
        return std::max(cfg_.pedal_min, f.pressure);
    case TouchConfig::Pedals::Position: break;
    }
    // Bottom of the pedal = pedal_min, top 20 % = full.
    float t = std::clamp((w.y + w.h - f.y) / (w.h * 0.8f), 0.0f, 1.0f);
    return cfg_.pedal_min + (1.0f - cfg_.pedal_min) * t;
}

// ---------------------------------------------------------------------------
// Fingers
// ---------------------------------------------------------------------------

TouchControls::Finger* TouchControls::find(u64 id) {
    for (Finger& f : fingers_)
        if (f.id == id) return &f;
    return nullptr;
}

bool TouchControls::finger_down(u64 id, float x, float y, float pressure) {
    note_touch();
    if (!visible()) return false;
    std::erase_if(fingers_, [&](const Finger& f) { return f.id == id; });
    int w = widget_at(x, y);
    if (w < 0) return false;
    Finger f;
    f.id = id;
    f.widget = w;
    f.x = f.origin_x = x;
    f.y = f.origin_y = y;
    f.pressure = pressure;
    fingers_.push_back(f);
    return true;
}

void TouchControls::finger_move(u64 id, float x, float y, float pressure) {
    Finger* f = find(id);
    if (!f) return;
    f->x = x, f->y = y, f->pressure = pressure;
    // Buttons and pedals follow a sliding finger (brake -> gas, D-pad -> face
    // buttons); the steering zone keeps its finger.
    if (widgets_[size_t(f->widget)].kind == Widget::Kind::Steering) return;
    int w = widget_at(x, y);
    if (w >= 0 && widgets_[size_t(w)].kind != Widget::Kind::Steering) f->widget = w;
}

void TouchControls::finger_up(u64 id) {
    // A tap shorter than a frame must still register as one press.
    if (Finger* f = find(id); f && !f->seen) {
        f->released = true;
        return;
    }
    std::erase_if(fingers_, [&](const Finger& f) { return f.id == id; });
}

void TouchControls::release_all() {
    fingers_.clear();
    pad_ = PadState{};
}

void TouchControls::note_other_input() {
    if (!touch_active_) return;
    touch_active_ = false;
    release_all();
}

bool TouchControls::visible() const {
    if (layout_ == TouchLayout::None || cfg_.mode == TouchConfig::Mode::Off) return false;
    return cfg_.mode == TouchConfig::Mode::On || touch_active_;
}

// ---------------------------------------------------------------------------
// Pad output
// ---------------------------------------------------------------------------

void TouchControls::update(double, float tilt) {
    pad_ = PadState{};
    for (Widget& w : widgets_) {
        w.active = false;
        w.value = 0.0f;
    }
    if (!visible()) {
        fingers_.clear();
        return;
    }
    pad_.connected = true;
    float lx = 0.0f;
    for (const Finger& f : fingers_) {
        Widget& w = widgets_[size_t(f.widget)];
        switch (w.kind) {
        case Widget::Kind::Button:
            press(pad_, w.button, 1.0f);
            w.active = true;
            break;
        case Widget::Kind::Pedal: {
            float v = pedal_value(w, f);
            press(pad_, w.button, v);
            w.active = true;
            w.value = std::max(w.value, v);
            break;
        }
        case Widget::Kind::DPad:
            if (int d = dpad_direction(w, f.x, f.y); d >= 0) {
                press(pad_, PadButton(d), 1.0f);
                w.active = true;
                w.button = PadButton(d);
            }
            break;
        case Widget::Kind::Steering:
            if (cfg_.steering != TouchConfig::Steering::Drag) break;
            {
                float range = cfg_.steer_range * kW;
                lx = std::clamp((f.x - f.origin_x) / range, -1.0f, 1.0f);
                w.active = true;
                w.value = lx;
                w.origin_x = f.origin_x;
                w.origin_y = f.origin_y;
                w.knob_x = f.origin_x + lx * range;
            }
            break;
        }
    }
    if (layout_ == TouchLayout::Driving && cfg_.steering == TouchConfig::Steering::Tilt) {
        float m = std::fabs(tilt);
        float dz = cfg_.tilt_deadzone;
        m = m <= dz ? 0.0f : std::min((m - dz) / (1.0f - dz), 1.0f);
        lx = std::copysign(m, tilt);
        for (Widget& w : widgets_)
            if (w.kind == Widget::Kind::Steering) w.value = lx;
    }
    pad_.axes[size_t(PadAxis::LX)] = u8(std::lround((std::clamp(lx, -1.0f, 1.0f) + 1.0f) * 127.5f));
    std::erase_if(fingers_, [](const Finger& f) { return f.released; });
    for (Finger& f : fingers_) f.seen = true;
}

} // namespace gt3r
