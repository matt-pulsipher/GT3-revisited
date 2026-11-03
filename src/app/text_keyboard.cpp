#include "app/text_keyboard.h"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

namespace gt3r {

namespace {

constexpr int kRows = 6;
// The character rows (letters lower case; Shift shows them in upper case).
const char* const kCharRows[] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm.:_"};

// As the page's own typing: printable ASCII, up to kMaxLength characters.
void add(std::string& text, std::string_view s) {
    for (char c : s)
        if (text.size() < TextKeyboard::kMaxLength && u8(c) >= 32 && u8(c) < 127) text += c;
}

} // namespace

void TextKeyboard::open(std::string title, std::string done, bool upper) {
    open_ = true, upper_ = upper;
    title_ = std::move(title);
    done_ = std::move(done);
    if (caps_.empty()) {
        auto cap = [&](Key key, char c, int row, float x, float w) {
            Cap k;
            k.key = key, k.c = c, k.row = row, k.x = x, k.w = w;
            caps_.push_back(k);
        };
        for (int row = 0; row < 4; ++row)
            for (int i = 0; kCharRows[row][i]; ++i) cap(kChar, kCharRows[row][i], row, float(i), 1);
        // Ten columns wide, as the character rows.
        cap(kShift, 0, 4, 0, 2);
        cap(kChar, '[', 4, 2, 1);
        cap(kChar, ']', 4, 3, 1);
        cap(kSpace, 0, 4, 4, 3);
        cap(kDelete, 0, 4, 7, 3);
        cap(kPaste, 0, 5, 0, 3);
        cap(kCancel, 0, 5, 3, 3);
        cap(kDone, 0, 5, 6, 4);
    }
    focus_ = int(caps_.size()) - 1; // the finishing key: a remembered text is one press away
}

TextKeyboard::Result TextKeyboard::press(int i, std::string& text) {
    const Cap& k = caps_[size_t(i)];
    switch (k.key) {
    case kChar: {
        const char c = upper_ ? char(std::toupper(u8(k.c))) : k.c;
        add(text, std::string_view(&c, 1));
        break;
    }
    case kShift: upper_ = !upper_; break;
    case kSpace: add(text, " "); break;
    case kDelete:
        if (!text.empty()) text.pop_back();
        break;
    case kPaste:
        if (char* clip = SDL_GetClipboardText()) {
            add(text, clip);
            SDL_free(clip);
        }
        break;
    case kCancel: return finish(Result::Cancel);
    case kDone: return finish(Result::Done);
    }
    return Result::None;
}

bool TextKeyboard::clickable(float x, float y) const {
    if (!open_) return false;
    for (const Cap& k : caps_)
        if (k.r.contains(x, y)) return true;
    return false;
}

int TextKeyboard::nearest(int row, float x) const {
    int best = 0;
    float best_d = 1e9f;
    for (size_t i = 0; i < caps_.size(); ++i) {
        if (caps_[i].row != row) continue;
        const float d = std::fabs(caps_[i].x + caps_[i].w / 2 - x);
        if (d < best_d) best = int(i), best_d = d;
    }
    return best;
}

TextKeyboard::Result TextKeyboard::update(const MenuInput& m, const std::vector<PointerInput::Action>& pointer,
                                          std::string& text) {
    if (!open_) return Result::None;
    using K = PointerInput::Action::Kind;
    for (const PointerInput::Action& a : pointer) {
        if (a.kind == K::Back) return finish(Result::Cancel);
        if (a.kind != K::Hover && a.kind != K::Click) continue;
        for (size_t i = 0; i < caps_.size(); ++i) {
            if (!caps_[i].r.contains(a.x, a.y)) continue;
            focus_ = int(i);
            if (a.kind == K::Click)
                if (Result r = press(int(i), text); r != Result::None) return r;
            break;
        }
    }
    // Up/Down to the nearest key of the next row, Left/Right along the row; both wrap around.
    const Cap& f = caps_[size_t(focus_)];
    if (int v = m.vertical()) {
        focus_ = nearest((f.row + v + kRows) % kRows, f.x + f.w / 2);
    } else if (int h = m.horizontal()) {
        std::vector<int> row;
        for (size_t i = 0; i < caps_.size(); ++i)
            if (caps_[i].row == f.row) row.push_back(int(i));
        const int at = int(std::find(row.begin(), row.end(), focus_) - row.begin());
        focus_ = row[size_t((at + h + int(row.size())) % int(row.size()))];
    }
    if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) return finish(Result::Cancel);
    if (m.pressed(PadButton::Start)) return finish(Result::Done);
    if (m.pressed(PadButton::Square) && !text.empty()) text.pop_back();
    if (m.pressed(PadButton::Cross)) return press(focus_, text);
    return Result::None;
}

void TextKeyboard::draw(Ui& ui, const std::string& text) {
    if (!open_) return;
    const bool touch = ui.touch();
    const float gap = 4, kh = touch ? 30 : 22;
    const float kw = std::min(touch ? 54.0f : 40.0f, std::floor((ui.width() - 40 - 9 * gap) / 10));
    const float gw = 10 * kw + 9 * gap, w = gw + 24, h = 52 + kRows * kh + (kRows - 1) * gap + 12;
    const float x = std::floor((ui.width() - w) / 2), y = std::floor((ui.height() - 16 - h) / 2); // above the footer
    ui.fill(0, 0, ui.width(), ui.height(), Color{0, 0, 0, 120});
    ui.fill_round(x, y, w, h, 6, colors::kPanel);
    ui.frame_round(x, y, w, h, 6, 1, colors::kAccent);
    ui.text(x + 12, y + 10, title_, colors::kAccent);
    // The text, its end in view.
    const UiRect box{x + 12, y + 24, gw, 22};
    ui.fill_round(box.x, box.y, box.w, box.h, 4, colors::kBackground);
    ui.frame_round(box.x, box.y, box.w, box.h, 4, 1, colors::kAccent);
    std::string shown = text + "_";
    for (size_t from = 0; Ui::text_width(shown) > box.w - 16 && from < text.size();)
        shown = "..." + text.substr(++from) + "_";
    ui.text(box.x + 8, std::floor(box.y + (box.h - Ui::kChar) / 2), shown, colors::kText);
    const float gx = box.x, gy = box.bottom() + 8;
    for (size_t i = 0; i < caps_.size(); ++i) {
        Cap& k = caps_[i];
        k.r = {gx + k.x * (kw + gap), gy + float(k.row) * (kh + gap), k.w * kw + (k.w - 1) * gap, kh};
        std::string label;
        switch (k.key) {
        case kChar: label = std::string(1, upper_ ? char(std::toupper(u8(k.c))) : k.c); break;
        case kShift: label = "Shift"; break;
        case kSpace: label = "Space"; break;
        case kDelete: label = "Delete"; break;
        case kPaste: label = "Paste"; break;
        case kCancel: label = "Cancel"; break;
        case kDone: label = done_; break;
        }
        ui.button(k.r, label, int(i) == focus_, k.key == kShift && upper_, k.key == kDone);
    }
    ui.footer("Cross: type   Square: delete   Start: " + done_ + "   Triangle: cancel");
}

} // namespace gt3r
