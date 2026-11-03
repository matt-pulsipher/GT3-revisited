#include "race/pause_menu.h"

#include "base/log.h"
#include "menu/flow/menu_controller.h"
#include "menu/menu_text.h"
#include "text/game_font.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <utility>

namespace gt3r::race {

namespace {

using menu::flow::key_bit;
using menu::flow::MenuKey;

const menu::Element* find_element(const menu::MenuScene& s, const std::string& name) {
    if (s.file().pages.empty()) return nullptr;
    for (const menu::Layer& l : s.file().pages[0].layers) {
        for (const menu::Element& e : l.elements)
            if (e.name == name) return &e;
        for (const menu::Element& e : l.widgets)
            if (e.name == name) return &e;
    }
    return nullptr;
}

} // namespace

bool PauseMenu::load(const Vfs& vfs, SDL_Renderer* r, text::GameFonts& fonts, const std::string& lang) {
    static const char* const kFiles[3] = {"pause1", "pause3", "pause2"}; // Race, Retry, Replay
    bool any = false;
    for (int i = 0; i < 3; ++i) {
        try {
            scenes_[i] = menu::MenuScene::load(vfs, "menu/" + lang + "/" + kFiles[i] + ".pmb");
            any = true;
        } catch (const std::exception& e) {
            LOG_WARN("Pause menu %s: %s", kFiles[i], e.what());
            scenes_[i].reset();
        }
    }
    renderer_ = std::make_unique<menu::MenuRenderer>(r);
    if (fonts.loaded()) renderer_->set_text_drawer(menu::make_game_font_drawer(fonts));
    return any;
}

std::vector<PauseMenu::Button> PauseMenu::row() const {
    if (confirm_) return {{"ok", 4, true}, {"cancel", 1, true}};
    switch (kind_) {
    case Kind::Race: return {{"continue", 3, true}, {"exit", 1, true}};
    case Kind::Retry: return {{"continue", 3, true}, {"exit", 1, true}, {"retry", 5, retry_enabled_}};
    case Kind::Replay: return {{"continue", 3, true}, {"exit", 4, true}, {"replay", 1, true}};
    }
    return {};
}

void PauseMenu::open(Kind kind) {
    kind_ = kind;
    confirm_ = false;
    cursor_ = 0;
    blink_ = 1.0f;
    ignore_ = 0xFFFF;
    held_ = 0;
    repeat_ = 0;
    acc_ = 0;
    sounds_.clear();
}

void PauseMenu::set_cursor(int i) {
    cursor_ = std::clamp(i, 0, int(row().size()) - 1);
    blink_ = 1.0f;
}

PauseMenu::Result PauseMenu::tick(u16 held) {
    ignore_ &= held;
    held &= u16(~ignore_);
    const u16 pressed = held & u16(~held_);
    held_ = held;
    // Directions: an event on the press, then after 30 frames every 3 frames.
    const u16 prev_keys = key_bit(MenuKey::Left) | key_bit(MenuKey::Up);
    const u16 next_keys = key_bit(MenuKey::Right) | key_bit(MenuKey::Down);
    bool prev = false, next = false;
    if (pressed & (prev_keys | next_keys)) {
        repeat_ = 0;
        prev = pressed & prev_keys, next = !prev && (pressed & next_keys);
    } else if (held & (prev_keys | next_keys)) {
        ++repeat_;
        if (repeat_ >= 30 && (repeat_ - 30) % 3 == 0) prev = held & prev_keys, next = !prev && (held & next_keys);
    } else {
        repeat_ = 0;
    }
    const bool ok = pressed & key_bit(MenuKey::Confirm);
    const bool cancel = !ok && (pressed & key_bit(MenuKey::Cancel));
    if (ok) sounds_.push_back("ok");
    else if (cancel) sounds_.push_back("cancel");
    // The selected button's light copy: alpha 1 -> 0 by 1/30 per frame, then 1 again.
    blink_ -= 1.0f / 30.0f;
    if (blink_ < 0) blink_ = 1.0f;

    std::vector<Button> b = row();
    if (ok) return choose();
    if (cancel) {
        if (!confirm_) return Result::Continue;
        confirm_ = false;
        cursor_ = 0; // the row is rebuilt on CONTINUE
        blink_ = 1.0f;
        return Result::None;
    }
    int nc = cursor_;
    if (prev && cursor_ > 0) nc = cursor_ - 1;
    if (next && cursor_ + 1 < int(b.size()) && b[size_t(cursor_ + 1)].enabled) nc = cursor_ + 1;
    if (nc != cursor_) {
        cursor_ = nc;
        blink_ = 1.0f;
        sounds_.push_back("cursor");
    }
    return Result::None;
}

PauseMenu::Result PauseMenu::choose() {
    std::vector<Button> b = row();
    const Button& c = b[size_t(std::clamp(cursor_, 0, int(b.size()) - 1))];
    if (!c.enabled) return Result::None;
    switch (c.id) {
    case 3: return Result::Continue;
    case 5: return Result::Retry;
    case 4: return Result::Exit; // OK in the question, or the replay's EXIT
    case 1:
        if (kind_ == Kind::Replay && !confirm_) return Result::ReplayOptions;
        confirm_ = !confirm_; // EXIT asks; CANCEL goes back to the row
        cursor_ = confirm_ ? 1 : 0; // the question starts on CANCEL; the row on CONTINUE
        blink_ = 1.0f;
        return Result::None;
    default: return Result::None;
    }
}

int PauseMenu::button_at(float x, float y) const {
    const std::optional<menu::MenuScene>& sc = scenes_[int(kind_)];
    if (!sc) return -1;
    std::vector<Button> b = row();
    for (int i = 0; i < int(b.size()); ++i) {
        const menu::Element* e = find_element(*sc, "m_" + b[size_t(i)].name);
        if (e && b[size_t(i)].enabled && x >= e->x && x <= e->x + e->w && y >= e->y && y <= e->y + e->h) return i;
    }
    return -1;
}

bool PauseMenu::hover(float x, float y) {
    const int i = button_at(x, y);
    if (i < 0) return false;
    if (i != cursor_) {
        cursor_ = i, blink_ = 1.0f;
        sounds_.push_back("cursor");
    }
    return true;
}

PauseMenu::Result PauseMenu::tap(float x, float y) {
    const int i = button_at(x, y);
    if (i < 0) return Result::None;
    if (i != cursor_) {
        hover(x, y);
        return Result::None;
    }
    sounds_.push_back("ok");
    return choose();
}

PauseMenu::Result PauseMenu::update(u16 held, double dt) {
    acc_ += dt;
    Result out = Result::None;
    int n = 0;
    while (acc_ >= 1.0 / 60.0 && n < 8) {
        acc_ -= 1.0 / 60.0;
        ++n;
        out = tick(held);
        if (out != Result::None) break;
    }
    if (n >= 8) acc_ = 0;
    return out;
}

void PauseMenu::draw(SDL_Renderer*, const SDL_FRect& dst) {
    const std::optional<menu::MenuScene>& sc = scenes_[int(kind_)];
    if (!sc || !renderer_) return;
    renderer_->set_scene(&*sc);
    std::vector<menu::ElementDraw> list;
    auto add = [&](const std::string& name, float alpha) {
        if (const menu::Element* e = find_element(*sc, name)) {
            menu::ElementDraw d;
            d.element = e;
            d.alpha = alpha;
            list.push_back(d);
        }
    };
    // Every window dims the picture (bg1, full screen) and darkens its band (bg) (the
    // reference captures of the replay and licence pauses).
    add("bg1", 1.0f);
    add("bg", 1.0f);
    if (confirm_) add("exit_message", 1.0f);
    std::vector<Button> b = row();
    for (int i = 0; i < int(b.size()); ++i) {
        add("m_" + b[size_t(i)].name, b[size_t(i)].enabled ? 1.0f : 0.5f);
        if (i == cursor_) add("m_" + b[size_t(i)].name + "_sel", blink_);
    }
    menu::RenderOptions ro{false, false, true, true};
    renderer_->draw_elements(list, ro, dst);
}

} // namespace gt3r::race
