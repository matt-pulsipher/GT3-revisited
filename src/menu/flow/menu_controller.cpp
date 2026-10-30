#include "menu/flow/menu_controller.h"

#include "base/log.h"

#include <algorithm>
#include <cmath>

namespace gt3r::menu::flow {

namespace {

// Timings in 60 Hz frames.
constexpr int kRepeatDelay = 30;    // second event 31 frames after the press
constexpr int kRepeatInterval = 3;  // then every 4 frames
constexpr int kFadeInFrames = 24;   // element fade-in (alpha +2.5/s for 0.4 s)
constexpr int kFadeOutFrames = 9;   // element fade-out / container lifetime on dismiss (0.15 s)
constexpr int kBlackOutFrames = 16; // page flag 8: black 0 -> 1 in 15 frames, then the logic continues
constexpr int kTeardownGap = 2;     // frames with the old page gone before the new one is built
constexpr int kSettleFrames = 10;   // build waits for 10 vblanks (input blocked)
constexpr int kBlackInFrames = 30;  // page flag 4: black 1 -> 0 in 30 frames (input allowed)
constexpr float kScrollFactor = 0.3f;
constexpr float kSpringK = 600.0f, kSpringDamp = 30.0f;
constexpr int kSpringFrames = 31;   // 0.5 s script step (N + 1 frames)

int focus_mode(const Element& e) { return (e.type >> 12) & 0x3; } // 2: pulsing highlight, 1: static
bool fades_in(const Element& e) { return ((e.flags >> 4) & 0xF) != 0; }   // element flag bits 20-23
bool fades_out(const Element& e) { return ((e.flags >> 8) & 0xF) != 0; }  // element flag bits 24-27
bool has_parts(const Element& e) { return (e.type & 0x7) != 0; }

// Decide effect alphas (centre, edge) at frame f (1-based) of the effect.
void decide_alpha(int f, float& centre, float& edge) {
    auto ramp = [](float t) { return std::clamp(t, 0.0f, 1.0f) * 0.4f; };
    centre = f <= 11 ? ramp(f / 3.0f) : ramp(1.0f - (f - 11) / 6.0f);
    edge = f <= 18 ? ramp((f - 4) / 6.0f) : ramp(1.0f - (f - 18) / 6.0f);
}

// Free buttons (free button: the name-entry key rows and its OK row).
bool is_free_row(const MenuController::ItemState& s) { return s.flow && s.flow->name.rfind("free", 0) == 0; }

int feedback_frames(u16 fb) { return fb == 0 ? 0 : fb == 0x40 ? 26 : 30; }

// Blinking widgets (class "blink"): +10/s for 0.1 s, -2.5/s for 0.4 s.
float blink_alpha(u32 frame) {
    int f = int(frame % 32);
    if (f < 6) return (f + 1) / 6.0f;
    if (f == 6) return 1.0f;
    return std::max(0.0f, 1.0f - (f - 6) / 24.0f);
}

// "diagonal_in" / "diagonal_out" (the arcade pages' logo pair): a 10 s cycle in which
// the "in" image appears corner diagonal by corner diagonal (2 s hidden; top-left and
// bottom-right rise over 0.5 s, then top-right and bottom-left; 4 s shown; the same in
// reverse; 2 s hidden); "out" is its complement, corner by corner.
void diagonal_alpha(u32 frame, bool in, float out[4]) {
    float t = float(frame % 600) / 60.0f;
    auto ramp = [](float x) { return std::clamp(x, 0.0f, 1.0f); };
    // Pair A = corners 0, 2; pair B = corners 1, 3.
    float a = ramp((t - 2.0f) / 0.5f), b = ramp((t - 2.5f) / 0.5f);
    if (t >= 7.0f) {
        b = 1.0f - ramp((t - 7.0f) / 0.5f);
        a = 1.0f - ramp((t - 7.5f) / 0.5f);
    }
    out[0] = out[2] = in ? a : 1.0f - a;
    out[1] = out[3] = in ? b : 1.0f - b;
}

const Element* find_widget(const Page& p, std::string_view name) {
    for (const Layer& l : p.layers)
        if (l.name == "defaultLayer")
            for (const Element& e : l.widgets)
                if (e.name == name) return &e;
    return nullptr;
}

bool has_widget_class(const Page& p, std::string_view cls) {
    for (const Layer& l : p.layers)
        if (l.name == "defaultLayer")
            for (const Element& e : l.widgets)
                if (e.widget_class && *e.widget_class == cls) return true;
    return false;
}

// Pages whose items are parked off-screen (x >= 640) and that have a window
// widget ("menu_top"/"menu") are scrolling selectors: the list places the
// focused item in the window (e.g. the title menu "root"). A parked item may also
// sit below the screen (the Toyota dealer list keeps four of its cars there).
// The race quick menus (qm_*.pmb "root") park their icons above the screen instead
// (y >= 448) and place them as a horizontal row in their "menu" window.
const Element* scroller_window(const Page& p) {
    if (p.items.empty()) return nullptr;
    bool right = true, above = true;
    for (const Element& e : p.items) {
        right = right && (e.x >= 640 || e.y + e.h <= 0);
        above = above && (e.x >= 640 || e.y >= kMenuHeight);
    }
    if (!right) return above ? find_widget(p, "menu") : nullptr;
    if (const Element* w = find_widget(p, "menu_top")) return w;
    if (const Element* w = find_widget(p, "menu_car_goods")) return w; // GT mode dealer car lists
    if (const Element* w = find_widget(p, "menu_dealer")) return w;    // the England and Germany dealers
    return find_widget(p, "menu");
}

// Horizontal selectors (the GT-mode league and series pages: a row of event tiles in
// the "menu" window): items 4 px apart; the row is centred when it fits, else
// left / right aligned near the ends and the focused item centred in between (as the
// arcade pickers' icon strip). Returns the row's x offset from the window's left edge.
// `x` = each item's left edge in the row (cumulative widths + 4 px), `total` the row width.
template <class Items>
float row_layout(const Items& items, float scroll, float window_w, std::vector<float>& x, float& total) {
    std::vector<float> widths;
    for (const auto& it : items) widths.push_back(it.element ? it.element->w : 0.0f);
    return menu::flow::row_layout(widths, scroll, window_w, x, total);
}

// Text style flags (TextStyle::align bits 4-7): 0x10 marquee (enters from the
// right and scrolls left by one box width every 240 frames, restarting once the
// text has passed),
// 0x20 typewriter (one more character per frame).
void apply_text_effects(ElementDraw& d, const Element& e, u32 age) {
    const TextStyle& t = *e.text;
    if (t.align & 0x10) {
        FontSpec fs = FontSpec::parse(t.font);
        // Width estimate of the text (the game measures it with the font).
        float text_w = float(t.text.size()) * fs.size * fs.sx * 0.5f;
        float period = e.w + text_w;
        float off = period > 0 ? std::fmod(float(age) * e.w / 240.0f, period) : 0.0f;
        // The text enters from the box's right edge (the reference tickers).
        d.text_dx = e.w - off;
        d.text_clip = true;
    }
    if (t.align & 0x20) {
        // Count characters: game encoding uses two bytes for EUC-JP and
        // ISO-8859 escapes.
        size_t bytes = 0, chars = 0;
        while (bytes < t.text.size() && chars < age && chars < 255) {
            unsigned char c = u8(t.text[bytes]);
            bytes += (c >= 0x80 && bytes + 1 < t.text.size()) ? 2 : 1;
            ++chars;
        }
        d.text_bytes = int(bytes);
    }
}

} // namespace

void text_effects(ElementDraw& d, const Element& e, u32 age) { apply_text_effects(d, e, age); }

namespace {

// GT3's button list placement: along the list's axis, buttons of the given sizes kRowGap
// apart. A list that fits the window is centred (`centre_fit`, a horizontal row) or at its
// start (a vertical list); else at the window's start while the focused button begins less
// than half a window from the list's start, at its end while less than half a window follows
// the focused button, else the focused button centred. Returns the list's start from the
// window's start.
float list_layout(const std::vector<float>& sizes, float scroll, float window, std::vector<float>& pos, float& total,
                  bool centre_fit) {
    pos.clear();
    float at = 0;
    for (float w : sizes) {
        pos.push_back(at);
        at += w + kRowGap;
    }
    total = std::max(0.0f, at - kRowGap);
    if (pos.empty()) return 0;
    // The focused position, interpolated while the scroll eases.
    int i0 = std::clamp(int(std::floor(scroll)), 0, int(pos.size()) - 1), i1 = std::min(i0 + 1, int(pos.size()) - 1);
    float t = std::clamp(scroll - float(i0), 0.0f, 1.0f);
    float start = pos[size_t(i0)] + (pos[size_t(i1)] - pos[size_t(i0)]) * t;
    float fs = sizes[size_t(i0)] + (sizes[size_t(i1)] - sizes[size_t(i0)]) * t;
    float after = total - start - fs;
    if (total <= window) return centre_fit ? (window - total) * 0.5f : 0.0f;
    if (start < window * 0.5f) return 0;
    if (after < window * 0.5f) return window - total;
    return window * 0.5f - (start + fs * 0.5f);
}

} // namespace

float row_layout(const std::vector<float>& widths, float scroll, float window_w, std::vector<float>& x, float& total) {
    return list_layout(widths, scroll, window_w, x, total, true);
}

float column_layout(const std::vector<float>& heights, float focus, float window_h, std::vector<float>& y,
                    float& total) {
    return list_layout(heights, focus, window_h, y, total, false);
}

float ease_scroll(float value, float target) { return value + (target - value) * kScrollFactor; }

MenuController::MenuController(const MenuSet& set) : set_(set) {}

const std::string& MenuController::page_name() const {
    static const std::string empty;
    return levels_.empty() ? empty : top().name;
}
MenuSet::PageRef MenuController::page() const { return levels_.empty() ? MenuSet::PageRef{} : top().page; }
const std::vector<MenuController::ItemState>& MenuController::items() const {
    static const std::vector<ItemState> empty;
    return levels_.empty() ? empty : top().items;
}
int MenuController::focus() const { return levels_.empty() || top().items.empty() ? -1 : top().focus; }
const MenuController::ItemState* MenuController::focused() const {
    int f = focus();
    return f >= 0 ? &top().items[size_t(f)] : nullptr;
}

bool MenuController::popup(std::string_view page) {
    if (levels_.empty() || phase_ != Phase::Idle || !set_.page(page)) return false;
    sound("ok");
    build(std::string(page), {}, true);
    return true;
}

bool MenuController::open(std::string_view page, std::string_view from) {
    if (!set_.page(page)) {
        LOG_WARN("menu: no such page [%.*s]", int(page.size()), page.data());
        return false;
    }
    levels_.clear();
    finished_ = false;
    result_ = 0;
    phase_ = Phase::Idle;
    black_out_ = -1;
    return_page_ = std::string(from);
    build(std::string(page), std::string(from), false);
    return true;
}

void MenuController::build(const std::string& name, const std::string& from, bool popup) {
    Level lv;
    lv.name = name;
    lv.page = set_.page(name);
    if (!lv.page) LOG_WARN("menu: no such page [%s]", name.c_str());
    if (lv.page.flow) {
        lv.flags = lv.page.flow->flags42;
        if (!import_page(*lv.page.flow).empty()) lv.import = set_.page(import_page(*lv.page.flow));
    }
    lv.built = frame_;
    if (lv.page && lv.page.flow) {
        const Page& layout = lv.page.layout();
        auto find = [&](const std::string& n) -> const Element* {
            for (const Element& e : layout.items)
                if (e.name == n) return &e;
            return nullptr;
        };
        for (const MblItem& it : lv.page.flow->items) {
            ItemState s;
            s.flow = &it;
            s.element = find(it.name);
            // Multi/Free/Concurrent buttons: name prefix + kind 4 + children.
            bool prefix = it.name.rfind("multi", 0) == 0 || it.name.rfind("free", 0) == 0 ||
                          it.name.rfind("concurrent", 0) == 0;
            s.multi = prefix && item_is_multi(it);
            s.nested_horizontal = it.unk142 & 0x10;
            s.nested_wrap = it.unk142 & 0x20;
            for (const MblItem& c : it.children) s.children.push_back(find(c.name));
            // The button list only builds buttons for page items it finds, and not for the
            // items control code leaves out.
            if (!s.element || (listed && !listed(name, it))) continue;
            lv.items.push_back(s);
        }
        // Initial focus (menus §4.2): the control's hook, else the first item leading to the
        // page we came from, else 0. There is no remembered focus: a page opened again starts
        // by the same rule.
        int f = initial_focus ? initial_focus(name) : -1;
        if (f < 0 && initial_item)
            if (const std::string want = initial_item(name); !want.empty())
                for (int i = 0; i < int(lv.items.size()); ++i)
                    if (lv.items[size_t(i)].flow->name == want) {
                        f = i;
                        break;
                    }
        if (f < 0 && !from.empty())
            for (int i = 0; i < int(lv.items.size()); ++i)
                if (lv.items[size_t(i)].flow->link == from) {
                    f = i;
                    break;
                }
        lv.focus = std::clamp(f, 0, std::max(0, int(lv.items.size()) - 1));
        lv.scroll = float(lv.focus);
    }
    // One popup level: a popup opened from a popup replaces it (the tune shop's Purchase? ->
    // Install parts now? -> Installed), so closing it returns to the base page.
    if (!popup) levels_.clear();
    else if (levels_.size() > 1) levels_.pop_back();
    levels_.push_back(std::move(lv));
    Level& t = top();
    // An "ignore_control" widget takes the controls away until control code gives them back
    // (give_control); building another page does not.
    if (t.page && has_widget_class(t.page.layout(), "ignore_control")) ignore_control_ = true;
    if (t.flags & page_flag::kFadeIn) {
        black_in_ = true;
        black_in_since_ = frame_;
    }
    refresh_layers(t, 0);
    phase_ = Phase::Settle;
    phase_frames_ = kSettleFrames;
    emit(MenuEvent::Kind::PageChanged, {});
}

void MenuController::refresh_layers(Level& lv, int dir_code) {
    if (!lv.page) return;
    const Page& p = lv.page.layout();
    auto layer_index = [&](const std::string& n) {
        if (n.empty()) return -1;
        for (int i = 0; i < int(p.layers.size()); ++i)
            if (p.layers[size_t(i)].name == n) return i;
        return -1;
    };
    const ItemState* f = lv.items.empty() ? nullptr : &lv.items[size_t(lv.focus)];
    int want80 = f ? layer_index(f->flow->str80) : -1;
    int want40 = f ? layer_index(f->flow->str40) : -1;
    // Start positions of a sliding layer (menu units, y up) and where the old one goes.
    float sx = 0, sy = 0;
    switch (dir_code) {
    case 1: sy = 448; break;  // moved up: new comes from above
    case 2: sy = -448; break; // moved down: new comes from below
    case 3: sx = -640; break;
    case 4: sx = 640; break;
    default: break;
    }
    auto update = [&](bool slide, int want) {
        bool have = false;
        for (LayerInst& li : lv.layers) {
            if (li.dying || li.slide != slide) continue;
            if (li.layer == want) {
                have = true;
                continue;
            }
            li.dying = true;
            li.dying_since = frame_;
            if (slide && dir_code) {
                li.tx = -sx;
                li.ty = -sy;
                li.spring_frames = kSpringFrames;
            }
        }
        if (!have && want >= 0) {
            LayerInst li;
            li.layer = want;
            li.created = frame_;
            li.slide = slide;
            if (slide && dir_code) {
                li.x = sx;
                li.y = sy;
                li.spring_frames = kSpringFrames;
            }
            lv.layers.push_back(li);
        }
    };
    update(false, want80);
    update(true, want40);
    // Without a direction (page build) old instances vanish at once.
    if (!dir_code)
        lv.layers.erase(std::remove_if(lv.layers.begin(), lv.layers.end(), [](const LayerInst& l) { return l.dying; }),
                        lv.layers.end());
}

void MenuController::step_layers(Level& lv) {
    for (LayerInst& li : lv.layers) {
        if (li.spring_frames <= 0) continue;
        --li.spring_frames;
        float ax = (li.tx - li.x) * kSpringK, ay = (li.ty - li.y) * kSpringK;
        li.vx += ax / 60.0f;
        li.vy += ay / 60.0f;
        li.vx -= li.vx * kSpringDamp / 60.0f;
        li.vy -= li.vy * kSpringDamp / 60.0f;
        li.x += li.vx / 60.0f;
        li.y += li.vy / 60.0f;
    }
    lv.layers.erase(std::remove_if(lv.layers.begin(), lv.layers.end(),
                                   [&](const LayerInst& l) {
                                       return l.dying && frame_ - l.dying_since >= u32(kFadeOutFrames);
                                   }),
                    lv.layers.end());
}

void MenuController::tick(MenuKeys held) {
    ++frame_;
    MenuKeys pressed = held & ~held_;
    held_ = held;
    for (Level& lv : levels_) {
        step_layers(lv);
        if (!lv.pinned) lv.scroll += (float(lv.focus) - lv.scroll) * kScrollFactor;
        const Element* window = nullptr;
        std::vector<float> xs;
        float total = 0, target = 0;
        if (horizontal_row(lv, window, xs, total, target)) {
            if (!lv.row_off_set) lv.row_off = target, lv.row_off_set = true;
            else if (!lv.pinned) lv.row_off = ease_scroll(lv.row_off, target);
        }
    }
    if (!levels_.empty()) {
        Level& t = top();
        // Focus pulse of the active list: 0.5 -> 0 sawtooth, 30 frames.
        if (phase_ != Phase::Feedback) {
            t.pulse -= 1.0f / 60.0f;
            if (t.pulse < 0) t.pulse = 0.5f;
        }
    }
    if (black_in_ && frame_ - black_in_since_ >= u32(kBlackInFrames)) black_in_ = false;

    // Auto-repeat: one shared counter, reset by any new press.
    const MenuKeys repeatable = key_bit(MenuKey::Up) | key_bit(MenuKey::Down) | key_bit(MenuKey::Left) |
                                key_bit(MenuKey::Right) | key_bit(MenuKey::L1) | key_bit(MenuKey::R1);
    MenuKeys fired = pressed & repeatable;
    if (fired) {
        repeat_counter_ = 0;
        repeating_ = false;
    } else if (held & repeatable) {
        ++repeat_counter_;
        if (repeat_counter_ > (repeating_ ? kRepeatInterval : kRepeatDelay)) {
            fired = held & repeatable;
            repeat_counter_ = 0;
            repeating_ = true;
        }
    }

    const bool input = phase_ == Phase::Idle && !finished_ && !ignore_control_;
    fired_ = input ? fired : 0;
    pressed_ = input ? pressed : 0;
    switch (phase_) {
    case Phase::Idle: break;
    case Phase::Feedback:
        // The feedback starts once the list has stopped scrolling (< 1 px left). A row held
        // still for a hovering pointer (pinned) has stopped where it is.
        if (!levels_.empty() && !top().pinned && std::fabs(top().scroll - float(top().focus)) * 32.0f > 1.0f) return;
        if (--phase_frames_ <= 0) {
            phase_ = Phase::Idle;
            feedback_item_ = -1;
            perform(pending_op_, pending_target_);
        }
        return;
    case Phase::FadeOut:
        ++black_out_;
        if (--phase_frames_ <= 0) {
            // Tear down the old page(s); the screen stays black for a moment.
            levels_.clear();
            phase_ = Phase::Gap;
            phase_frames_ = kTeardownGap;
        }
        return;
    case Phase::Gap:
        if (--phase_frames_ <= 0) {
            black_out_ = -1;
            phase_ = Phase::Idle;
            build(pending_target_, pending_from_, false);
        }
        return;
    case Phase::Settle:
        if (--phase_frames_ <= 0) phase_ = Phase::Idle;
        return;
    }
    if (finished_ || levels_.empty() || ignore_control_) return;
    handle_input(pressed, fired);
}

void MenuController::handle_input(MenuKeys pressed, MenuKeys fired) {
    Level& lv = top();
    bool up = fired & key_bit(MenuKey::Up), down = !up && (fired & key_bit(MenuKey::Down));
    bool left = fired & key_bit(MenuKey::Left), right = !left && (fired & key_bit(MenuKey::Right));
    if (up || down) left = right = false;
    const MenuKeys dirs =
        key_bit(MenuKey::Up) | key_bit(MenuKey::Down) | key_bit(MenuKey::Left) | key_bit(MenuKey::Right);
    bool ok = !(held_ & dirs) && (pressed & key_bit(MenuKey::Confirm));
    bool cancel = !(held_ & dirs) && !ok && (pressed & key_bit(MenuKey::Cancel));

    if (lv.items.empty()) {
        // Code-driven page (no flow items): control code handles the keys.
        if (ok) emit(MenuEvent::Kind::Confirm, {});
        else if (cancel && (lv.flags & page_flag::kCancel)) this->cancel(false);
        else if (cancel) emit(MenuEvent::Kind::Cancel, {});
        return;
    }

    bool horizontal = lv.flags & page_flag::kHorizontal;
    ItemState& f = lv.items[size_t(lv.focus)];
    // A focused multi button handles its own axis first (Left/Right for the
    // option pages) and takes OK for its chosen child.
    if (f.multi && !f.flow->children.empty()) {
        bool prev = f.nested_horizontal ? left : up, next = f.nested_horizontal ? right : down;
        if (prev || next) {
            int n = int(f.flow->children.size());
            int c = choice(f.flow->name), nc = c + (next ? 1 : -1);
            nc = f.nested_wrap ? (nc + n) % n : std::clamp(nc, 0, n - 1);
            if (is_free_row(f) && f.nested_horizontal) free_moved_ = true;
            if (nc != c) {
                choices_[choice_key(lv.name, f.flow->name)] = nc;
                sound("select");
                emit(MenuEvent::Kind::Choice, f.flow->children[size_t(nc)].action, f.flow->name, nc);
            }
            if (f.nested_horizontal == horizontal) return; // same axis: the outer list does not move
        }
    }
    if (horizontal ? (left || right) : (up || down)) {
        bool prev = horizontal ? left : up;
        move_focus(prev ? -1 : 1, horizontal ? (prev ? 3 : 4) : (prev ? 1 : 2));
        return;
    }
    if (ok) confirm();
    else if (cancel && (lv.flags & page_flag::kCancel)) this->cancel(false);
}

bool MenuController::move_focus(int delta, int dir_code) {
    Level& lv = top();
    int n = int(lv.items.size());
    if (n == 0) return false;
    int f = lv.focus + delta;
    f = (lv.flags & page_flag::kWrap) ? (f + n) % n : std::clamp(f, 0, n - 1);
    if (f == lv.focus) return false;
    // Free buttons keep the column across rows (free button, name entry): after a Left /
    // Right, or coming from a row with at least as many keys, the new row's key is the one
    // under the previous key's centre (keys 24 wide, 28 apart; a one-key row's button
    // centre is 223 px in); else the row keeps the key it had.
    const ItemState& from = lv.items[size_t(lv.focus)];
    ItemState& to = lv.items[size_t(f)];
    if (from.multi && to.multi && is_free_row(from) && is_free_row(to) && !to.flow->children.empty()) {
        int k = int(from.flow->children.size()), m = int(to.flow->children.size());
        if (free_moved_ || k >= m) {
            float centre = k > 1 ? float(choice(from.flow->name)) * 28.0f + 12.0f : 223.0f;
            choices_[choice_key(lv.name, to.flow->name)] = std::clamp(int(centre / 28.0f), 0, m - 1);
        }
    }
    free_moved_ = false;
    lv.focus = f;
    lv.pinned = false;
    lv.pulse = 0.25f;
    sound("select");
    refresh_layers(lv, dir_code);
    return true;
}

bool MenuController::item_enabled(int index) const {
    if (levels_.empty() || index < 0 || index >= int(top().items.size())) return false;
    return !enabled || enabled(top().name, *top().items[size_t(index)].flow);
}

void MenuController::confirm() {
    Level& lv = top();
    int index = lv.focus;
    ItemState& s = lv.items[size_t(index)];
    if (s.multi) {
        // OK goes to the nested list: the chosen child is activated.
        int c = choice(s.flow->name);
        if (c >= 0 && c < int(s.flow->children.size())) run_item(s.flow->children[size_t(c)], index);
        return;
    }
    if (!item_enabled(index)) {
        sound("disable");
        return;
    }
    if (intercept) {
        std::string popup = intercept(lv.name, *s.flow);
        if (!popup.empty()) {
            feedback_item_ = index;
            if (popup == "00cmn/cd_ride_sure") {
                start_feedback(Op::Popup, std::move(popup)); // a question: normal confirm feedback
            } else {
                // A refusal ("no money", "not for sale", ...): SE "disable", the popup at once.
                sound("disable");
                build(popup, {}, true);
            }
            return;
        }
    }
    run_item(*s.flow, index);
}

void MenuController::run_item(const MblItem& it, int index) {
    if (!it.action.empty()) emit(MenuEvent::Kind::Action, it.action, it.name, index);
    feedback_item_ = index;
    switch (item_kind(it)) {
    case 0:
        if (!it.link.empty()) start_feedback(Op::Goto, it.link);
        break;
    case 1:
        if (!it.link.empty()) start_feedback(Op::Popup, it.link);
        break;
    case 2: start_feedback(Op::Finish, {}); break;
    case 3: start_feedback(Op::Back, {}); break;
    default: break; // 4: multi container
    }
}

void MenuController::start_feedback(Op op, std::string target) {
    feedback_ = top().flags & page_flag::kFeedback;
    sound(feedback_ ? "next" : "ok");
    pending_op_ = op;
    pending_target_ = std::move(target);
    phase_frames_ = feedback_frames(feedback_);
    if (phase_frames_ > 0) phase_ = Phase::Feedback;
    else perform(op, pending_target_);
}

void MenuController::perform(Op op, const std::string& target) {
    switch (op) {
    case Op::Goto: {
        // "return" now names the base page we leave.
        return_page_ = levels_.front().name;
        pending_from_ = return_page_;
        pending_target_ = target;
        pending_op_ = Op::Goto;
        if (top().flags & page_flag::kFadeOut) {
            phase_ = Phase::FadeOut;
            phase_frames_ = kBlackOutFrames;
            black_out_ = 0;
        } else {
            levels_.clear();
            phase_ = Phase::Gap;
            phase_frames_ = kTeardownGap;
        }
        break;
    }
    case Op::Popup: build(target, {}, true); break;
    case Op::Finish:
        finished_ = true;
        result_ = 1;
        emit(MenuEvent::Kind::Finish, top().items.empty() ? std::string() : top().items[size_t(top().focus)].flow->action,
             top().items.empty() ? std::string() : top().items[size_t(top().focus)].flow->name, top().focus);
        break;
    case Op::Back: cancel(true); break;
    default: break;
    }
}

void MenuController::cancel(bool via_ok) {
    if (levels_.size() > 1) {
        // Close the popup; the page below keeps its focus.
        if (!via_ok) sound("return");
        levels_.pop_back();
        return;
    }
    Level& lv = top();
    const MblPage* fp = lv.page.flow;
    std::string back = fp ? fp->back : std::string();
    std::string back_action = fp ? fp->back_action : std::string();
    if (back.empty()) {
        if (!via_ok) sound("return");
        if (!back_action.empty()) emit(MenuEvent::Kind::BackAction, back_action);
        finished_ = true;
        result_ = 0;
        emit(MenuEvent::Kind::Exit, back_action);
        return;
    }
    if (!via_ok) sound("cancel");
    std::string target = back == "return" ? return_page_ : back;
    if (target.empty()) target = back;
    pending_from_ = lv.name;
    if (!back_action.empty()) emit(MenuEvent::Kind::BackAction, back_action);
    pending_target_ = target;
    pending_op_ = Op::Back;
    if (lv.flags & page_flag::kFadeOut) {
        phase_ = Phase::FadeOut;
        phase_frames_ = kBlackOutFrames;
        black_out_ = 0;
    } else {
        levels_.clear();
        phase_ = Phase::Gap;
        phase_frames_ = kTeardownGap;
    }
}

void MenuController::emit(MenuEvent::Kind k, std::string action, std::string item, int child) {
    MenuEvent e;
    e.kind = k;
    e.action = std::move(action);
    e.page = page_name();
    e.item = std::move(item);
    e.child = child;
    events_.push_back(std::move(e));
}

bool MenuController::item_box(int index, SDL_FRect& box, SDL_FRect* clip) const {
    if (levels_.empty() || !top().page) return false;
    const Level& lv = top();
    if (index < 0 || index >= int(lv.items.size()) || !lv.items[size_t(index)].element) return false;
    const Element* e = lv.items[size_t(index)].element;
    const Element* window = scroller_window(lv.page.layout());
    box = {e->x, e->y, e->w, e->h};
    if (clip) *clip = window ? SDL_FRect{window->x, window->y, window->w, window->h} : SDL_FRect{0, 0, 640, 448};
    if (window) {
        // Same placement as draw_list(): the list scrolls through the window.
        float spacing = 32;
        if (lv.items.size() >= 2 && lv.items[0].element && lv.items[1].element)
            spacing = std::fabs(lv.items[0].element->y - lv.items[1].element->y);
        const Element* row_win = nullptr;
        std::vector<float> xs;
        float total = 0, target = 0;
        if (horizontal_row(lv, row_win, xs, total, target)) {
            const float off = row_offset(lv, target);
            box = {window->x + off + xs[size_t(index)], window->y + (window->h - e->h) * 0.5f, e->w, e->h};
            return true;
        }
        float cy = window->y + window->h * 0.5f + (float(index) - lv.scroll) * -spacing;
        box = {window->x + (window->w - e->w) * 0.5f, cy - e->h * 0.5f, e->w, e->h};
    }
    return true;
}

int MenuController::hit_test(float x, float y) const {
    if (levels_.empty() || !top().page) return -1;
    // Later items are drawn on top: test from the last one.
    for (int i = int(top().items.size()) - 1; i >= 0; --i) {
        SDL_FRect b, c;
        if (!item_box(i, b, &c)) continue;
        if (x < c.x || x > c.x + c.w || y < c.y || y > c.y + c.h) continue;
        if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) return i;
    }
    return -1;
}

bool MenuController::tap(float x, float y) {
    if (busy() || ignore_control_ || levels_.empty()) return false;
    int i = hit_test(x, y);
    if (i < 0) return false;
    if (i == top().focus) {
        confirm();
    } else {
        set_focus(i);
        sound("select");
    }
    return true;
}

bool MenuController::hover(float x, float y) {
    if (busy() || ignore_control_ || levels_.empty() || !top().page) return false;
    Level& lv = top();
    const bool scrolls = scroller_window(lv.page.layout()) != nullptr;
    if (scrolls && !(lv.flags & page_flag::kHorizontal)) return false;
    int i = hit_test(x, y);
    if (i < 0) return false;
    if (i != lv.focus) {
        lv.focus = i;
        lv.pinned = scrolls; // (lv.scroll left where it is)
        lv.pulse = 0.25f;
        refresh_layers(lv, 0);
        sound("select");
    }
    return true;
}

void MenuController::set_focus(int index) {
    if (levels_.empty()) return;
    Level& lv = top();
    if (index >= 0 && index < int(lv.items.size()) && index != lv.focus) {
        lv.focus = index;
        lv.scroll = float(index);
        lv.row_off_set = false; // (placed at once, as the scroll)
        lv.pinned = false;
        lv.pulse = 0.25f;
        refresh_layers(lv, 0);
    }
}

bool MenuController::horizontal_row(const Level& lv, const Element*& window, std::vector<float>& x, float& total,
                                    float& target) const {
    if (!lv.page || !(lv.flags & page_flag::kHorizontal) || lv.items.empty()) return false;
    window = scroller_window(lv.page.layout());
    if (!window) return false;
    target = row_layout(lv.items, float(lv.focus), window->w, x, total);
    return true;
}

bool MenuController::set_focus(std::string_view item) {
    for (int i = 0; i < int(items().size()); ++i)
        if (items()[size_t(i)].flow->name == item) {
            set_focus(i);
            return true;
        }
    return false;
}

int MenuController::choice(std::string_view item) const {
    auto it = choices_.find(choice_key(page_name(), item));
    return it == choices_.end() ? 0 : it->second;
}

void MenuController::set_choice(std::string_view item, int child) { choices_[choice_key(page_name(), item)] = child; }

void MenuController::set_layer(std::string_view layer, int visible) {
    if (!levels_.empty()) top().layer_override[std::string(layer)] = visible;
}

bool MenuController::layer_visible(int layer) const {
    if (levels_.empty() || !top().page) return false;
    const Level& lv = top();
    const std::string& name = lv.page.layout().layers.at(size_t(layer)).name;
    auto ov = lv.layer_override.find(name);
    if (ov != lv.layer_override.end() && ov->second >= 0) return ov->second != 0;
    if (name == "defaultLayer") return true;
    for (const LayerInst& li : lv.layers)
        if (li.layer == layer && !li.dying) return true;
    return false;
}

float MenuController::fade_alpha(const Element& e, u32 since) const {
    if (!fades_in(e)) return 1.0f;
    return std::min(1.0f, float(frame_ - since) / kFadeInFrames);
}

float MenuController::layer_fade_alpha(int layer, const Element& e) const {
    if (levels_.empty()) return 1.0f;
    const Level& lv = levels_.back();
    for (const LayerInst& li : lv.layers)
        if (li.layer == layer && !li.dying) return fade_alpha(e, li.created);
    return fade_alpha(e, lv.built);
}

float MenuController::dying_alpha(const Element& e, const LayerInst& li) const {
    if (!li.dying) return fade_alpha(e, li.created);
    if (!fades_out(e)) return 0.0f; // vanishes at once
    float a = fade_alpha(e, li.created);
    return a * std::max(0.0f, 1.0f - float(frame_ - li.dying_since) / kFadeOutFrames);
}

void MenuController::add_level_draws(std::vector<ElementDraw>& out, const Level& lv, bool is_top) const {
    if (!lv.page) return;
    const float zbase = float(&lv - &levels_.front()) * 1000.0f;
    auto push = [&](const MenuScene* scene, const Element* e, float alpha, bool widget = false,
                    u32 since = ~0u) -> ElementDraw& {
        ElementDraw d;
        d.scene = scene;
        d.element = e;
        d.alpha = alpha;
        d.widget = widget;
        d.z = zbase + e->z;
        if (e->text) apply_text_effects(d, *e, frame_ - (since == ~0u ? lv.built : since));
        out.push_back(d);
        return out.back();
    };
    const Page& p = lv.page.layout();
    const MenuScene* scene = lv.page.scene;
    // Import page (e.g. the GT-mode status bar), then the default layer.
    auto default_layer = [&](const MenuSet::PageRef& ref) {
        const Page& pg = ref.layout();
        for (const Layer& l : pg.layers) {
            if (l.name != "defaultLayer") continue;
            for (const Element& e : l.elements) push(ref.scene, &e, fade_alpha(e, lv.built));
            for (const Element& e : l.widgets) {
                // Drawn by game code (movie): "mpeg", and the arcade pickers' "mpeg_course" / "mpeg_class".
                bool code = e.widget_class && e.widget_class->rfind("mpeg", 0) == 0;
                if ((!has_parts(e) && !code) || e.name == "focus" || e.name == "disable") continue;
                float a = fade_alpha(e, lv.built);
                if (e.widget_class && *e.widget_class == "blink") a *= blink_alpha(frame_);
                const bool diag_in = e.widget_class && *e.widget_class == "diagonal_in";
                const bool diag_out = e.widget_class && *e.widget_class == "diagonal_out";
                // A horizontal selector's "scroll_bar" (menus §4.6; GT3's ScrollBar): nothing at
                // all, track included, while the row is no longer than the bar; else the track
                // and the hidden "pinch" over the visible share of the row at its scroll
                // position: x = bar x - offset x bar w / row w, width = window w x bar w / row w
                // (offset: the row's start from the window's left edge, as it eases).
                const Element* bar_win = nullptr;
                float row_off = 0, row_total = 0;
                if (e.name == "scroll_bar") {
                    std::vector<float> xs;
                    float target = 0;
                    if (horizontal_row(lv, bar_win, xs, row_total, target)) {
                        if (row_total <= e.w) continue;
                        row_off = row_offset(lv, target);
                    } else {
                        bar_win = nullptr;
                    }
                }
                // Movie widgets: their colours modulate the movie (drawn by game code), they
                // are not a quad of their own.
                ElementDraw& wd = push(ref.scene, &e, a, true);
                wd.hide_parts = code;
                if (diag_in || diag_out) diagonal_alpha(frame_, diag_in, wd.corner_alpha);
                if (bar_win) {
                    const Element* pinch = nullptr;
                    for (const Layer& hl : lv.page.layout().layers)
                        if (hl.name == "hidden")
                            for (const Element& he : hl.elements)
                                if (he.name == "pinch") pinch = &he;
                    if (pinch) {
                        const float z = wd.z; // (push may move `wd`)
                        ElementDraw& pd = push(ref.scene, pinch, a);
                        pd.has_box = true;
                        pd.box = {e.x - row_off * e.w / row_total, e.y, bar_win->w * e.w / row_total, e.h};
                        pd.z = z + 0.001f;
                    }
                }
            }
        }
    };
    if (lv.import) default_layer(lv.import);
    default_layer(lv.page);
    // Layers forced on by control code.
    for (const auto& [name, vis] : lv.layer_override) {
        if (vis != 1 || name == "defaultLayer") continue;
        for (const Layer& l : p.layers)
            if (l.name == name) {
                for (const Element& e : l.elements) push(scene, &e, fade_alpha(e, lv.built));
                // Code-filled text widgets too (the trophy's bonus box "get_money_disp").
                for (const Element& e : l.widgets)
                    if (e.text && e.widget_class) push(scene, &e, fade_alpha(e, lv.built), true);
            }
    }
    // Focused item layers (str80 plain, str40 sliding).
    for (const LayerInst& li : lv.layers) {
        const Layer& l = p.layers[size_t(li.layer)];
        auto add = [&](const Element& e, bool widget) {
            float a = dying_alpha(e, li);
            if (a <= 0) return;
            ElementDraw& d = push(scene, &e, a, widget, li.created);
            d.dx = li.x;
            d.dy = li.y;
        };
        for (const Element& e : l.elements) add(e, false);
        for (const Element& e : l.widgets)
            if (has_parts(e) && e.name != "focus" && e.name != "disable") add(e, true);
    }

    // Buttons.
    const Element* window = scroller_window(p);
    const Element* focus_overlay = find_widget(p, "focus");
    const Element* disable_overlay = find_widget(p, "disable");
    float spacing = 32;
    if (window && lv.items.size() >= 2)
        spacing = std::fabs(lv.items[0].element->y - lv.items[1].element->y);
    const int fb_frame = phase_ == Phase::Feedback && is_top ? feedback_frames(feedback_) - phase_frames_ + 1 : 0;
    // While the controls are taken away the top page's buttons are not drawn.
    const size_t buttons = is_top && ignore_control_ ? 0 : lv.items.size();
    for (int i = 0; i < int(buttons); ++i) {
        const ItemState& s = lv.items[size_t(i)];
        const Element& e = *s.element;
        const bool is_focus = i == lv.focus;
        const bool en = !enabled || enabled(lv.name, *s.flow);
        const float pulse = is_top ? lv.pulse : 0.25f;
        const int mode = focus_mode(e);
        const float p_val = mode == 2 ? pulse : mode == 1 ? 0.5f : 0.0f;
        float base_alpha = fade_alpha(e, lv.built);
        ElementDraw box_override;
        float distance_alpha = 1.0f;
        if (window) {
            // Scrolling selector: the focused item sits in the middle of the window.
            float cy = window->y + window->h * 0.5f + (float(i) - lv.scroll) * -spacing;
            box_override.has_box = true;
            box_override.box = {window->x + (window->w - e.w) * 0.5f, cy - e.h * 0.5f, e.w, e.h};
            const Element* row_win = nullptr;
            std::vector<float> xs;
            float total = 0, target = 0;
            if (horizontal_row(lv, row_win, xs, total, target)) {
                box_override.box = {window->x + row_offset(lv, target) + xs[size_t(i)], window->y + (window->h - e.h) * 0.5f,
                                    e.w, e.h};
                cy = window->y + window->h * 0.5f;
            }
            box_override.has_clip = true;
            box_override.clip = {window->x, window->y, window->w, window->h};
            // Title list (top button list): items fade with their distance d from the window
            // centre, alpha 1 - d / (1.8 x item height), gone beyond; the focused one stays.
            if (window->name == "menu_top" && !is_focus) {
                float d = std::fabs(cy - (window->y + window->h * 0.5f)), lim = 1.8f * e.h;
                distance_alpha = d < lim ? 1.0f - d / lim : 0.0f;
            }
        }
        auto place = [&](ElementDraw& d) {
            if (window) {
                d.has_box = true;
                d.box = box_override.box;
                d.has_clip = true;
                d.clip = box_override.clip;
            }
            if (!en && !disable_overlay) d.brightness = 0.5f;
            d.alpha *= distance_alpha;
        };
        // A button's highlight (its alt look) is drawn where the button is (in a scrolling list,
        // where the list puts it), in the button's place in the draw order: the alt's own x, y
        // and z in the layout are not used (menus §5.3). (The GT Auto page's Wheel Shop
        // highlight sits 10 units right of its button in the layout, the dealer car pages' Spec
        // highlight off-screen.)
        auto highlight = [&](const Element& button, const Element& alt, float alpha) -> ElementDraw& {
            ElementDraw& d = push(scene, &alt, alpha);
            d.z = zbase + button.z;
            d.has_box = true;
            d.box = {button.x, button.y, alt.w, alt.h};
            place(d);
            return d;
        };
        bool fb = fb_frame > 0 && i == feedback_item_;
        if (is_focus && !e.alt.empty()) {
            const Element& alt = e.alt[0];
            float alt_a = fade_alpha(alt, lv.built);
            float over = mode == 2 ? 2.0f * (0.5f - p_val) : 0.0f; // normal look drawn over the highlight
            if (fb && feedback_ == 0x80) {
                static const float q[4] = {0.4f, 0.6f, 0.8f, 1.0f};
                over = 1.0f - q[fb_frame % 4];
            } else if (fb && feedback_ == 0xC0) {
                static const float q[5] = {1.0f, 0.8f, 0.6f, 0.4f, 0.2f};
                alt_a *= q[fb_frame % 5];
                over = 0;
            } else if (fb) {
                over = 0;
            }
            // A disabled item's highlight is drawn at half brightness, under the page's
            // `disable` widget too (menus §5.3).
            ElementDraw& hd = highlight(e, alt, alt_a);
            if (!en) hd.brightness = 0.5f;
            if (over > 0) {
                ElementDraw& d = push(scene, &e, base_alpha * over);
                d.z = zbase + e.z + 0.0001f;
                place(d);
            }
        } else {
            float a = base_alpha;
            if (fb && feedback_ == 0x80) {
                static const float q[4] = {0.4f, 0.6f, 0.8f, 1.0f};
                a *= q[fb_frame % 4];
            } else if (fb && feedback_ == 0xC0) {
                static const float q[5] = {1.0f, 0.8f, 0.6f, 0.4f, 0.2f};
                a *= q[fb_frame % 5];
            }
            place(push(scene, &e, a));
            if (is_focus && p_val > 0 && !focus_overlay) {
                ElementDraw& d = push(scene, &e, base_alpha);
                d.kind = ElementDraw::Kind::AddQuad;
                d.level = p_val;
                d.z += 0.0001f;
                place(d);
            }
        }
        if (is_focus && focus_overlay && mode != 0) {
            ElementDraw& d = push(scene, focus_overlay, base_alpha * std::min(1.0f, p_val + 0.5f));
            d.has_box = true;
            const SDL_FRect& b = window ? box_override.box : SDL_FRect{e.x, e.y, e.w, e.h};
            d.box = {b.x + (b.w - focus_overlay->w) * 0.5f, b.y + (b.h - focus_overlay->h) * 0.5f, focus_overlay->w,
                     focus_overlay->h};
        }
        if (!en && disable_overlay) {
            ElementDraw& d = push(scene, disable_overlay, base_alpha);
            d.has_box = true;
            d.box = window ? box_override.box : SDL_FRect{e.x, e.y, e.w, e.h};
            d.z = zbase + e.z + 0.0002f;
        }
        if (fb && feedback_ == 0x40) {
            ElementDraw& d = push(scene, &e, 1.0f);
            d.kind = ElementDraw::Kind::Glow;
            decide_alpha(fb_frame, d.level, d.level2);
            d.z = 100000.0f;
            place(d);
        }
        // Multi items: every choice is drawn, the current one in its alt look. Free
        // buttons (the name-entry key rows) show their key cursor only while the row has
        // the focus: one cursor over the whole grid, pulsing like a focused button.
        const bool free_row = s.multi && is_free_row(s);
        int ch = s.multi && (!free_row || (is_focus && is_top)) ? choice(s.flow->name) : -1;
        for (int c = 0; c < int(s.children.size()); ++c) {
            const Element* ce = s.children[size_t(c)];
            if (!ce) continue;
            if (c == ch && !ce->alt.empty()) {
                const Element& alt = ce->alt[0];
                highlight(*ce, alt, fade_alpha(alt, lv.built));
                if (free_row && focus_mode(*ce) == 2) { // the normal look over it: the pulse
                    ElementDraw& d = push(scene, ce, fade_alpha(*ce, lv.built) * 2.0f * (0.5f - pulse));
                    d.z = zbase + ce->z + 0.0001f;
                    place(d);
                }
                continue;
            }
            place(push(scene, ce, fade_alpha(*ce, lv.built)));
        }
    }
    // Page items the flow does not list (and that are not multi children) are
    // drawn as static parts.
    for (const Element& e : p.items) {
        bool listed = std::any_of(lv.items.begin(), lv.items.end(), [&](const ItemState& s) {
            return s.element == &e || std::find(s.children.begin(), s.children.end(), &e) != s.children.end();
        });
        if (!listed && !window && e.x < 640) push(scene, &e, fade_alpha(e, lv.built));
    }
}

std::vector<ElementDraw> MenuController::draw_list() const {
    std::vector<ElementDraw> out;
    for (size_t i = 0; i < levels_.size(); ++i) add_level_draws(out, levels_[i], i + 1 == levels_.size());
    auto black = [&](float alpha) {
        ElementDraw d;
        d.kind = ElementDraw::Kind::Fill;
        d.has_box = true;
        d.box = {0, 0, kMenuWidth, kMenuHeight};
        d.alpha = std::clamp(alpha, 0.0f, 1.0f);
        d.z = 10000.0f;
        out.push_back(d);
    };
    if (black_out_ >= 0) black(std::min(1.0f, black_out_ / 15.0f));
    if (black_in_) black(1.0f - float(frame_ - black_in_since_) / kBlackInFrames);
    std::stable_sort(out.begin(), out.end(), [](const ElementDraw& a, const ElementDraw& b) { return a.z < b.z; });
    return out;
}

} // namespace gt3r::menu::flow
