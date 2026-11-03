#include "app/ui.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gt3r {

void Ui::clear() {
    SDL_SetRenderDrawColor(r_, colors::kBackground.r, colors::kBackground.g, colors::kBackground.b, 255);
    SDL_RenderClear(r_);
}

namespace {

SDL_FColor to_f(Color c) { return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f}; }

// Baseline of text whose capitals fill y .. y + band.
float baseline(FontFace f, float em, float band) { return (band + ui_font::cap_height(f, em)) / 2; }

float title_em(int scale) { return Ui::kTitleCap * float(scale) / std::max(ui_font::cap_height(FontFace::Title, 1), 0.1f); }

} // namespace

Ui::Ui(SDL_Renderer* r) : r_(r) {
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    if (ui_font::available()) font_ = std::make_unique<UiFontRenderer>(r_);
}

void Ui::text(float x, float y, std::string_view s, Color c) {
    if (font_ && font_->draw(FontFace::Body, kBodyEm, x, y + baseline(FontFace::Body, kBodyEm, kChar), s, to_f(c))) return;
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    std::string str(s);
    SDL_RenderDebugText(r_, x, y, str.c_str());
}

void Ui::text_mono(float x, float y, std::string_view s, Color c) {
    if (!font_) return text(x, y, s, c);
    for (size_t i = 0; i < s.size();) {
        size_t j = i + 1;
        while (j < s.size() && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) ++j;
        std::string_view ch = s.substr(i, j - i);
        if (ch != " ") text(x + std::round((kMonoCell - text_width(ch)) / 2), y, ch, c);
        x += kMonoCell, i = j;
    }
}

void Ui::text_right(float right, float y, std::string_view s, Color c) { text(right - text_width(s), y, s, c); }

void Ui::text_center(float y, std::string_view s, Color c) { text_center_in(0, w_, y, s, c); }

void Ui::text_center_in(float x, float w, float y, std::string_view s, Color c) {
    text(std::floor(x + (w - text_width(s)) / 2), y, s, c);
}

float Ui::text_width(std::string_view s, int scale) {
    scale = std::max(scale, 1);
    if (!ui_font::available()) return float(s.size() * kChar * size_t(scale));
    return scale == 1 ? ui_font::width(FontFace::Body, kBodyEm, s) : ui_font::width(FontFace::Title, title_em(scale), s);
}

void Ui::text_big(float x, float y, std::string_view s, int scale, Color c) {
    scale = std::max(scale, 1);
    if (scale == 1) return text(x, y, s, c);
    const float em = title_em(scale);
    if (font_ && font_->draw(FontFace::Title, em, x, y + baseline(FontFace::Title, em, float(kChar * scale)), s, to_f(c)))
        return;
    float sx = 1, sy = 1;
    SDL_GetRenderScale(r_, &sx, &sy);
    SDL_SetRenderScale(r_, sx * float(scale), sy * float(scale));
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    std::string str(s);
    SDL_RenderDebugText(r_, x / float(scale), y / float(scale), str.c_str());
    SDL_SetRenderScale(r_, sx, sy);
}

namespace {

bool is_cont(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// Byte length of the first `n` code points of `s`.
size_t prefix_bytes(std::string_view s, size_t n) {
    size_t i = 0;
    for (size_t k = 0; k < n && i < s.size(); ++k) {
        ++i;
        while (i < s.size() && is_cont(s[i])) ++i;
    }
    return i;
}

size_t count_cps(std::string_view s) {
    size_t n = 0;
    for (char c : s) n += !is_cont(c);
    return n;
}

// Largest n in [0, hi] with fits(n) (fits is monotone, fits(0) assumed).
template <class F> size_t largest_fit(size_t hi, F fits) {
    size_t lo = 0;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (fits(mid)) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

} // namespace

std::vector<std::string> Ui::wrap(std::string_view s, float width) {
    std::vector<std::string> out;
    std::string line;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') ++i;
        size_t j = s.find(' ', i);
        if (j == std::string_view::npos) j = s.size();
        std::string_view word = s.substr(i, j - i);
        i = j;
        if (word.empty()) break;
        if (!line.empty() && text_width(line + ' ' + std::string(word)) > width) {
            out.push_back(std::move(line));
            line.clear();
        }
        while (line.empty() && text_width(word) > width) { // a word longer than a line
            size_t n = std::max<size_t>(1, largest_fit(count_cps(word), [&](size_t k) {
                return text_width(word.substr(0, prefix_bytes(word, k))) <= width;
            }));
            size_t b = prefix_bytes(word, n);
            out.emplace_back(word.substr(0, b));
            word.remove_prefix(b);
            if (word.empty()) break;
        }
        if (word.empty()) continue;
        if (!line.empty()) line += ' ';
        line += word;
    }
    if (!line.empty()) out.push_back(std::move(line));
    return out;
}

std::string Ui::elide(std::string_view s, float width) {
    if (text_width(s) <= width) return std::string(s);
    size_t n = largest_fit(count_cps(s), [&](size_t k) { return text_width(elide_middle(s, k)) <= width; });
    return elide_middle(s, n);
}

std::string Ui::clip(std::string_view s, float width) {
    if (text_width(s) <= width) return std::string(s);
    size_t n = largest_fit(count_cps(s), [&](size_t k) {
        return text_width(std::string(s.substr(0, prefix_bytes(s, k))) + "...") <= width;
    });
    std::string_view head = s.substr(0, prefix_bytes(s, n));
    while (!head.empty() && head.back() == ' ') head.remove_suffix(1);
    return std::string(head) + "...";
}

namespace {

constexpr int kArcSegments = 6;

// Outline of a rounded rectangle, clockwise, 4 * (kArcSegments + 1) points.
void round_rect_points(float x, float y, float w, float h, float r, SDL_FPoint* out) {
    r = std::clamp(r, 0.0f, std::min(w, h) / 2);
    const float cx[4] = {x + w - r, x + w - r, x + r, x + r};
    const float cy[4] = {y + r, y + h - r, y + h - r, y + r};
    int n = 0;
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i <= kArcSegments; ++i) {
            float a = (float(c) - 1.0f + float(i) / kArcSegments) * std::numbers::pi_v<float> / 2;
            out[n++] = {cx[c] + r * std::cos(a), cy[c] + r * std::sin(a)};
        }
}

} // namespace

void Ui::fill_round(float x, float y, float w, float h, float r, Color c) {
    if (w <= 0 || h <= 0) return;
    if (r <= 0.01f) return fill(x, y, w, h, c);
    constexpr int kPts = 4 * (kArcSegments + 1);
    SDL_FPoint p[kPts];
    round_rect_points(x, y, w, h, r, p);
    SDL_Vertex v[kPts + 1];
    int idx[kPts * 3];
    const SDL_FColor fc = to_f(c);
    v[0] = {{x + w / 2, y + h / 2}, fc, {0, 0}};
    for (int i = 0; i < kPts; ++i) {
        v[i + 1] = {p[i], fc, {0, 0}};
        idx[i * 3] = 0, idx[i * 3 + 1] = i + 1, idx[i * 3 + 2] = (i + 1) % kPts + 1;
    }
    SDL_RenderGeometry(r_, nullptr, v, kPts + 1, idx, kPts * 3);
}

void Ui::frame_round(float x, float y, float w, float h, float r, float t, Color c) {
    if (w <= 2 * t || h <= 2 * t) return;
    constexpr int kPts = 4 * (kArcSegments + 1);
    SDL_FPoint outer[kPts], inner[kPts];
    round_rect_points(x, y, w, h, r, outer);
    round_rect_points(x + t, y + t, w - 2 * t, h - 2 * t, std::max(0.0f, r - t), inner);
    SDL_Vertex v[kPts * 2];
    int idx[kPts * 6];
    const SDL_FColor fc = to_f(c);
    for (int i = 0; i < kPts; ++i) {
        v[i * 2] = {outer[i], fc, {0, 0}};
        v[i * 2 + 1] = {inner[i], fc, {0, 0}};
        int a = i * 2, b = i * 2 + 1, c2 = (i + 1) % kPts * 2, d = c2 + 1;
        int* q = idx + i * 6;
        q[0] = a, q[1] = b, q[2] = c2, q[3] = b, q[4] = d, q[5] = c2;
    }
    SDL_RenderGeometry(r_, nullptr, v, kPts * 2, idx, kPts * 6);
}

void Ui::fill_circle(float cx, float cy, float r, Color c) { fill_round(cx - r, cy - r, 2 * r, 2 * r, r, c); }

void Ui::button(const UiRect& b, std::string_view label, bool focused, bool pressed, bool primary) {
    const float rad = touch_ ? 6.0f : 4.0f;
    focused = focused || (pointer_on_ && b.contains(pointer_x_, pointer_y_));
    Color bg = primary ? (pressed ? Color{200, 150, 20} : colors::kAccent)
                       : pressed ? Color{70, 78, 108} : focused ? colors::kHighlight : colors::kPanel;
    fill_round(b.x, b.y, b.w, b.h, rad, bg);
    if (!primary) frame_round(b.x, b.y, b.w, b.h, rad, 1, focused ? colors::kAccent : colors::kOutline);
    else if (focused) frame_round(b.x - 2, b.y - 2, b.w + 4, b.h + 4, rad + 2, 1, colors::kText);
    Color fg = primary ? Color{24, 20, 10} : focused ? colors::kAccent : colors::kText;
    text_center_in(b.x, b.w, std::floor(b.y + (b.h - kChar) / 2), label, fg);
}

void Ui::scrollbar(const UiRect& view, float content, float offset) {
    if (content <= view.h + 0.5f) return;
    float x = view.right() - 3, h = std::max(12.0f, view.h * view.h / content);
    float y = view.y + (view.h - h) * std::clamp(offset / (content - view.h), 0.0f, 1.0f);
    fill_round(x, view.y, 3, view.h, 1.5f, colors::kPanel);
    fill_round(x, y, 3, h, 1.5f, colors::kOutline);
}

void Ui::fill(float x, float y, float w, float h, Color c) {
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_FRect rc{x, y, w, h};
    SDL_RenderFillRect(r_, &rc);
}

void Ui::frame(float x, float y, float w, float h, Color c) {
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_FRect rc{x, y, w, h};
    SDL_RenderRect(r_, &rc);
}

void Ui::bar(float x, float y, float w, float h, float v, Color c) {
    fill(x, y, w, h, colors::kPanel);
    fill(x, y, w * std::clamp(v, 0.0f, 1.0f), h, c);
    frame(x, y, w, h, colors::kHighlight);
}

void Ui::header(std::string_view title, std::string_view hint) {
    fill(0, 0, w_, 20, colors::kPanel);
    text(safe_.x + 8, 6, title, colors::kAccent);
    const float room = safe_.right() - 8 - (safe_.x + 8 + text_width(title) + 16);
    if (room > 16) text_right(safe_.right() - 8, 6, elide(hint, room), colors::kDim);
}

void Ui::footer(std::string_view hint) {
    fill(0, h_ - 16, w_, 16, colors::kPanel);
    text(safe_.x + 8, h_ - 12, hint, colors::kDim);
}

float Ui::tab(float x, float y, float h, std::string_view label, bool active, bool focused) {
    const float w = tab_width(label), rad = touch_ ? 6.0f : 4.0f;
    focused = focused || (pointer_on_ && UiRect{x, y, w, h}.contains(pointer_x_, pointer_y_));
    if (active) {
        fill_round(x, y, w, h, rad, colors::kHighlight);
        fill_round(x + 6, y + h - 3, w - 12, 2, 1, colors::kAccent);
    }
    if (focused) frame_round(x, y, w, h, rad, 1, colors::kAccent);
    text_center_in(x, w, std::floor(y + (h - kChar) / 2), label, active ? colors::kAccent : colors::kDim);
    return w;
}

void Ui::toggle_pill(float x, float cy, bool on, bool enabled) {
    const float w = toggle_width(), h = touch_ ? 22.0f : 14.0f, y = std::floor(cy - h / 2), kr = h / 2 - 2;
    Color track = on ? (enabled ? Color{46, 140, 78} : Color{50, 80, 60}) : Color{40, 43, 54};
    fill_round(x, y, w, h, h / 2, track);
    frame_round(x, y, w, h, h / 2, 1, on ? (enabled ? colors::kGood : colors::kDim) : colors::kOutline);
    // Knob on the active side, label on the other.
    fill_circle(on ? x + w - h / 2 : x + h / 2, y + h / 2, kr, enabled ? colors::kText : colors::kDim);
    const char* lbl = on ? "ON" : "OFF";
    float lx = on ? x + (w - h - text_width(lbl)) / 2 + 1 : x + h + (w - h - text_width(lbl)) / 2 - 1;
    text(std::floor(lx), std::floor(y + (h - kChar) / 2), lbl, enabled ? (on ? colors::kText : colors::kDim) : colors::kDim);
}

void Ui::slider(float x, float cy, float w, float v, bool focused, bool enabled) {
    v = std::clamp(v, 0.0f, 1.0f);
    const float th = touch_ ? 6.0f : 4.0f, kr = touch_ ? 9.0f : 5.0f;
    fill_round(x, cy - th / 2, w, th, th / 2, Color{40, 43, 54});
    Color c = !enabled ? colors::kDim : focused ? colors::kAccent : Color{170, 150, 90};
    fill_round(x, cy - th / 2, std::max(th, w * v), th, th / 2, c);
    float kx = x + w * v;
    if (focused && enabled) fill_circle(kx, cy, kr + 3, Color{255, 196, 40, 60});
    fill_circle(kx, cy, kr, enabled ? colors::kText : colors::kDim);
}

float Ui::tag(float x, float y, std::string_view label, Color c) {
    float w = std::ceil(text_width(label)) + 6;
    frame(x, y - 2, w, 12, c);
    text(x + 3, y, label, c);
    return w;
}

int Ui::list_window(int count, int selected, int rows) {
    if (count <= rows) return 0;
    int first = std::clamp(selected - rows / 2, 0, count - rows);
    return first;
}

std::vector<std::string> wrap_text(std::string_view s, size_t max_chars) {
    std::vector<std::string> out;
    max_chars = std::max<size_t>(max_chars, 1);
    std::string line;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') ++i;
        size_t j = s.find(' ', i);
        if (j == std::string_view::npos) j = s.size();
        std::string_view word = s.substr(i, j - i);
        i = j;
        if (word.empty()) break;
        if (!line.empty() && line.size() + 1 + word.size() > max_chars) {
            out.push_back(std::move(line));
            line.clear();
        }
        while (word.size() > max_chars) { // a word longer than a line
            if (!line.empty()) out.push_back(std::move(line)), line.clear();
            out.emplace_back(word.substr(0, max_chars));
            word.remove_prefix(max_chars);
        }
        if (!line.empty()) line += ' ';
        line += word;
    }
    if (!line.empty()) out.push_back(std::move(line));
    return out;
}

PointerEvent pointer_event(const SDL_Event& e) {
    PointerEvent p;
    switch (e.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button != SDL_BUTTON_LEFT || e.button.which == SDL_TOUCH_MOUSEID) break;
        p.kind = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? PointerEvent::Kind::Down : PointerEvent::Kind::Up;
        p.x = e.button.x, p.y = e.button.y;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (e.motion.which == SDL_TOUCH_MOUSEID) break;
        p.kind = PointerEvent::Kind::Move;
        p.x = e.motion.x, p.y = e.motion.y;
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        if (e.tfinger.touchID == SDL_MOUSE_TOUCHID) break;
        p.kind = e.type == SDL_EVENT_FINGER_DOWN     ? PointerEvent::Kind::Down
               : e.type == SDL_EVENT_FINGER_MOTION ? PointerEvent::Kind::Move
                                                   : PointerEvent::Kind::Up;
        p.x = e.tfinger.x, p.y = e.tfinger.y;
        p.id = u64(e.tfinger.fingerID) + 1;
        break;
    default: break;
    }
    return p;
}

Gesture::Result Gesture::feed(const SDL_Event& e) {
    const PointerEvent p = pointer_event(e);
    switch (p.kind) {
    case PointerEvent::Kind::Down:
        if (active_) return Result::None; // a second finger
        active_ = true, dragging_ = false, id_ = p.id;
        x_ = sx_ = p.x, y_ = sy_ = p.y;
        return Result::Down;
    case PointerEvent::Kind::Move:
        if (!active_ || p.id != id_) return Result::None;
        x_ = p.x, y_ = p.y;
        if (dragging_) return Result::Drag;
        if (std::hypot(x_ - sx_, y_ - sy_) > 6.0f) {
            dragging_ = true;
            return Result::DragStart;
        }
        return Result::None;
    case PointerEvent::Kind::Up: {
        if (!active_ || p.id != id_) return Result::None;
        x_ = p.x, y_ = p.y;
        bool tap = !dragging_;
        active_ = dragging_ = false;
        return tap ? Result::Tap : Result::Up;
    }
    case PointerEvent::Kind::None: break;
    }
    return Result::None;
}

const HitArea* HitList::at(float x, float y) const {
    for (auto it = areas_.rbegin(); it != areas_.rend(); ++it)
        if (it->r.contains(x, y)) return &*it;
    return nullptr;
}

void PointerInput::event(const SDL_Event& e) {
    using K = Action::Kind;
    auto push = [&](Action a) {
        if (a.kind == K::Hover && !actions_.empty() && actions_.back().kind == K::Hover) actions_.back() = a;
        else if (a.kind == K::Drag && !actions_.empty() && actions_.back().kind == K::Drag) actions_.back() = a;
        else actions_.push_back(a);
    };
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION: // the mouse, or the VR pointer (not SDL's mouse made from a touch)
        if (e.motion.which == SDL_TOUCH_MOUSEID) break;
        hover_known_ = true, hx_ = e.motion.x, hy_ = e.motion.y;
        if (!e.motion.state && !gesture_.active()) push({K::Hover, e.motion.x, e.motion.y});
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: hover_known_ = false; break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (e.wheel.y != 0) {
            Action a{K::Wheel, e.wheel.mouse_x, e.wheel.mouse_y};
            a.wheel = e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f);
            push(a);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_RIGHT && e.button.which != SDL_TOUCH_MOUSEID) push({K::Back, e.button.x, e.button.y});
        break;
    case SDL_EVENT_FINGER_DOWN:
        if (e.tfinger.touchID != SDL_MOUSE_TOUCHID) hover_known_ = false;
        break;
    default: break;
    }
    const PointerEvent p = pointer_event(e);
    if (p.kind == PointerEvent::Kind::Down && !gesture_.active()) finger_ = p.id != 0;
    switch (gesture_.feed(e)) {
    case Gesture::Result::Tap: {
        Action a{K::Click, gesture_.x(), gesture_.y()};
        a.finger = finger_;
        push(a);
        break;
    }
    case Gesture::Result::DragStart:
    case Gesture::Result::Drag: {
        Action a{K::Drag, gesture_.x(), gesture_.y(), gesture_.start_x(), gesture_.start_y()};
        a.finger = finger_;
        push(a);
        break;
    }
    case Gesture::Result::Up: {
        Action a{K::DragEnd, gesture_.x(), gesture_.y(), gesture_.start_x(), gesture_.start_y()};
        a.finger = finger_;
        push(a);
        break;
    }
    default: break;
    }
}

bool pointer_down(const SDL_Event& e, float& x, float& y) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID) {
        x = e.button.x;
        y = e.button.y;
        return true;
    }
    if (e.type == SDL_EVENT_FINGER_DOWN) {
        x = e.tfinger.x;
        y = e.tfinger.y;
        return true;
    }
    return false;
}

bool ui_to_menu(const Ui& ui, SDL_FPoint p, float& x, float& y) {
    const UiRect pic = ui.picture43();
    if (p.x < pic.x || p.x >= pic.right() || p.y < 0 || p.y >= pic.h) return false;
    x = (p.x - pic.x) * 640.0f / pic.w;
    y = 448.0f - p.y * 448.0f / pic.h;
    return true;
}

XrList::Hit XrList::at(float x, float y) const {
    for (const Rows& l : lists_) {
        if (x < l.x0 || x >= l.x1 || y < l.y0) continue;
        const int i = int((y - l.y0) / l.h);
        if (i < l.count) return {l.list, l.first + i};
    }
    return {};
}

XrList::Hit XrList::held(Hit h) {
    for (const Rows& l : lists_)
        if (h.row >= 0 && l.list == h.list) held_list_ = l.list, held_first_ = l.first;
    return h;
}

int XrList::first(int list, int sel, int rows, int centred) {
    if (list != held_list_) return centred;
    if (sel >= held_first_ && sel < held_first_ + rows) return held_first_;
    held_list_ = -1; // the pad moved it out of view: centred again
    return centred;
}

void XrList::event(const SDL_Event& e) {
    if (float x, y; xr_hover(e, x, y)) {
        if (Hit h = at(x, y); h.row >= 0) hover_ = h;
        return;
    }
    if ((e.type != SDL_EVENT_MOUSE_BUTTON_DOWN && e.type != SDL_EVENT_MOUSE_BUTTON_UP) ||
        e.button.which != kXrPointerMouse || e.button.button != SDL_BUTTON_LEFT)
        return;
    const Hit h = at(e.button.x, e.button.y);
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) press_ = h;
    else if (std::exchange(press_, Hit{}) == h && h.row >= 0) click_ = h;
}

bool xr_hover(const SDL_Event& e, float& x, float& y) {
    if (e.type != SDL_EVENT_MOUSE_MOTION || e.motion.which != kXrPointerMouse || e.motion.state) return false;
    x = e.motion.x, y = e.motion.y;
    return true;
}

std::string elide_middle(std::string_view s, size_t max_chars) {
    // Count characters, not bytes (the debug font draws one glyph per code point).
    auto is_cont = [](char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; };
    size_t chars = 0;
    for (char c : s) chars += !is_cont(c);
    if (chars <= max_chars) return std::string(s);
    if (max_chars <= 3) return std::string("...").substr(0, max_chars);
    size_t keep = max_chars - 3, head = keep / 3, tail = keep - head; // file names live at the end
    // Byte offsets of the head end and the tail start.
    size_t i = 0, n = 0;
    while (i < s.size() && n < head) {
        ++i;
        while (i < s.size() && is_cont(s[i])) ++i;
        ++n;
    }
    size_t j = s.size(), m = 0;
    while (j > 0 && m < tail) {
        --j;
        while (j > 0 && is_cont(s[j])) --j;
        ++m;
    }
    return std::string(s.substr(0, i)) + "..." + std::string(s.substr(j));
}

void MenuInput::update(const PadState& pad, double dt) {
    u16 now = pad.buttons;
    // Treat the left stick as a d-pad for menus.
    if (pad.axis(PadAxis::LY) < 50) now |= 1u << unsigned(PadButton::Up);
    if (pad.axis(PadAxis::LY) > 205) now |= 1u << unsigned(PadButton::Down);
    if (pad.axis(PadAxis::LX) < 50) now |= 1u << unsigned(PadButton::Left);
    if (pad.axis(PadAxis::LX) > 205) now |= 1u << unsigned(PadButton::Right);

    if (suppress_) {
        suppressed_ = now;
        suppress_ = false;
    }
    suppressed_ &= now; // released buttons stop being suppressed
    now &= ~suppressed_;

    pressed_ = now & ~held_;
    // Auto-repeat for directions.
    const PadButton dirs[4] = {PadButton::Up, PadButton::Down, PadButton::Left, PadButton::Right};
    for (int i = 0; i < 4; ++i) {
        u16 bit = u16(1u << unsigned(dirs[i]));
        if (!(now & bit)) {
            repeat_timer_[i] = 0;
            continue;
        }
        if (pressed_ & bit) {
            repeat_timer_[i] = -0.35; // initial delay
            continue;
        }
        repeat_timer_[i] += dt;
        if (repeat_timer_[i] >= 0.07) {
            repeat_timer_[i] = 0;
            pressed_ |= bit;
        }
    }
    held_ = now;
}

int MenuInput::vertical() const { return pressed(PadButton::Down) - pressed(PadButton::Up); }
int MenuInput::horizontal() const { return pressed(PadButton::Right) - pressed(PadButton::Left); }

} // namespace gt3r
