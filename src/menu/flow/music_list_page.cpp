#include "menu/flow/music_list_page.h"

#include <algorithm>
#include <cmath>

namespace gt3r::menu::flow {

namespace {

const Element* find_in(const Layer* l, const char* name) {
    if (!l) return nullptr;
    for (const Element& e : l->elements)
        if (e.name == name) return &e;
    return nullptr;
}

// Left / right aligned text starts about 10 units in from its box (the title starts at x 66
// in the reference, its box at 56).
constexpr float kTextInset = 10;

} // namespace

MusicListLayout MusicListLayout::from(const Layer* hidden, float view_h) {
    MusicListLayout g;
    g.view_h = view_h;
    const Element* row = find_in(hidden, "label_check_box");
    if (!row) return g;
    // Layout units are y up: a part's offset down from the row's top is the difference of the tops.
    const float top = row->y + row->h;
    g.row_w = row->w, g.row_h = row->h;
    if (const Element* e = find_in(hidden, "check_box"))
        g.check_x = e->x - row->x, g.check_y = top - (e->y + e->h), g.check_w = e->w, g.check_h = e->h;
    if (const Element* e = find_in(hidden, "label_box"))
        g.label_x = e->x - row->x, g.label_y = top - (e->y + e->h), g.label_w = e->w, g.label_h = e->h;
    if (const Element* m = find_in(hidden, "mode_select_box")) {
        g.mode_h = m->h;
        const float mtop = m->y + m->h;
        const char* names[2] = {"button_straight", "button_shuffle"};
        for (int i = 0; i < 2; ++i)
            if (const Element* b = find_in(hidden, names[i])) {
                g.button_x[i] = b->x - m->x;
                g.button_y = mtop - (b->y + b->h);
                g.button_w = b->w, g.button_h = b->h;
            }
    }
    if (const Element* p = find_in(hidden, "pop_label_check_box")) {
        g.pop_x = p->x - row->x, g.pop_w = p->w, g.pop_h = p->h;
        if (const Element* e = find_in(hidden, "pop_check_box"))
            g.pop_check_x = e->x - row->x, g.pop_check_w = e->w, g.pop_check_h = e->h;
        if (const Element* e = find_in(hidden, "pop_label_box"))
            g.pop_label_x = e->x - row->x, g.pop_label_w = e->w, g.pop_label_h = e->h;
    }
    return g;
}

void MusicListPage::open(const race::RacePlaylist& list, std::vector<MusicTrack> tracks, bool editable,
                         const Layout& layout) {
    *this = MusicListPage{};
    open_ = true;
    editable_ = editable;
    layout_ = layout;
    songs_ = list.songs();
    columns_.assign(songs_.size(), 0); // each row starts on its check mark
    tracks_ = std::move(tracks);
    mode_ = list.shuffle() ? 1 : 0;
    // The cursor starts on the first song, whose preview starts at once.
    if (editable_) preview_focus();
}

void MusicListPage::set_tabs(std::vector<std::string> names, int chosen, bool focused) {
    tabs_ = std::move(names);
    tab_ = std::clamp(chosen, 0, std::max(0, int(tabs_.size()) - 1));
    top_col_ = tab_;
    if (tabs_shown() && focused && editable_) {
        focus_ = -1;
        preview_ = -1;
    }
}

void MusicListPage::choose_tab(int t, std::vector<std::string>& sounds) {
    if (t < 0 || t >= int(tabs_.size())) {
        sounds.push_back("disable");
        return;
    }
    if (t == tab_) return;
    tab_ = t;
    tab_request_ = t;
    sounds.push_back("cursor");
}

SDL_FRect MusicListPage::tab_box(const SDL_FRect& area, int i) const {
    // The tabs centred on the row's left part, Order and Shuffle (the last row's buttons) at its
    // right end.
    const Layout& g = layout_;
    const int n = int(tabs_.size());
    constexpr float kEdge = 4, kGap = 12, kButtonGap = 8, kGroupGap = 16;
    const float top = area.y + area.h, y = top - g.button_y - g.button_h;
    const float shuffle_x = g.row_w - kEdge - g.button_w, order_x = shuffle_x - kButtonGap - g.button_w;
    if (i >= n) return {area.x + (i == n ? order_x : shuffle_x), y, g.button_w, g.button_h};
    const float room = order_x - kGroupGap - kEdge;
    const float w = std::min(120.0f, (room - kGap * float(n - 1)) / float(std::max(n, 1)));
    const float x0 = kEdge + (room - (float(n) * w + float(n - 1) * kGap)) * 0.5f;
    return {area.x + x0 + float(i) * (w + kGap), y, w, g.button_h};
}

int MusicListPage::tab_at(const SDL_FRect& area, float x) const {
    for (int i = 0; i < top_items(); ++i) {
        const SDL_FRect b = tab_box(area, i);
        if (x >= b.x && x < b.x + b.w) return i;
    }
    return -1;
}

std::string MusicListPage::text(int row) const {
    if (row < 0 || row >= int(songs_.size()) || tracks_.empty()) return {};
    const MusicTrack& t = tracks_[size_t(track(row))];
    return t.artist.empty() ? t.title : t.title + " / " + t.artist; // (ours: a song of the player's without one)
}

int MusicListPage::track(int row) const {
    if (row < 0 || row >= int(songs_.size())) return 0;
    for (size_t i = 0; i < tracks_.size(); ++i)
        if (tracks_[i].id == songs_[size_t(row)].id) return int(i);
    return 0; // not on the disc: the group's first song stands in
}

void MusicListPage::preview_focus() {
    const bool song = focus_ >= 0 && focus_ < int(songs_.size());
    preview_ = song && songs_[size_t(focus_)].listed ? track(focus_) : -1;
}

void MusicListPage::tick(MenuKeys fired, MenuKeys pressed, std::vector<std::string>& sounds) {
    if (!open_ || !editable_) return;
    // The scroll and the highlight pulses step before this frame's input (it acts next frame).
    if (scroll_step_ != 0) {
        scroll_ += scroll_step_;
        if ((scroll_step_ > 0 && scroll_ >= scroll_target_) || (scroll_step_ < 0 && scroll_ <= scroll_target_)) {
            scroll_ = scroll_target_;
            scroll_step_ = 0;
        }
    }
    const int lit = lifted_ ? -2 : focus_;
    if (lit != pulse_row_) pulse_ = 0, pulse_row_ = lit;
    if ((pulse_ -= 1.0f / 60.0f) < 0) pulse_ = 0.5f;
    if (!lifted_) lift_pulse_ = 0;
    else if ((lift_pulse_ -= 1.0f / 60.0f) < 0) lift_pulse_ = 0.5f;
    // One key a frame, in this order (Cancel is the page's).
    if (pressed & key_bit(MenuKey::Confirm)) confirm(sounds);
    else if (pressed & key_bit(MenuKey::Cancel)) return;
    else if (tabs_shown() && !lifted_ && (pressed & key_bit(MenuKey::L1))) choose_tab(tab_ - 1, sounds);
    else if (tabs_shown() && !lifted_ && (pressed & key_bit(MenuKey::R1))) choose_tab(tab_ + 1, sounds);
    else if (fired & key_bit(MenuKey::L2)) jump(-1, sounds);
    else if (fired & key_bit(MenuKey::R2)) jump(1, sounds);
    else if (fired & key_bit(MenuKey::Up)) move(-1, sounds);
    else if (fired & key_bit(MenuKey::Down)) move(1, sounds);
    else if (fired & key_bit(MenuKey::Left)) side(-1, sounds);
    else if (fired & key_bit(MenuKey::Right)) side(1, sounds);
}

void MusicListPage::move(int d, std::vector<std::string>& sounds) {
    const int n = int(songs_.size());
    if (lifted_) {
        // The lifted song changes places with its neighbour (never with the last row); the
        // neighbour takes its column, the cursor goes along with the song.
        // Both: never past the end of its own set of songs.
        const int f = focus_, t = f + d;
        if (t < 0 || t >= n || (split_ >= 0 && (f < split_) != (t < split_))) {
            sounds.push_back("disable");
            return;
        }
        std::swap(songs_[size_t(f)], songs_[size_t(t)]);
        std::swap(columns_[size_t(f)], columns_[size_t(t)]);
        columns_[size_t(f)] = columns_[size_t(t)];
        focus_ = t;
        sounds.push_back("cursor");
        scroll_to_focus(d);
        return;
    }
    const int t = focus_ + d;
    if (t < (tabs_shown() ? -1 : 0) || t > n) {
        sounds.push_back("disable");
        return;
    }
    sounds.push_back("cursor");
    focus_row(t, d);
}

void MusicListPage::jump(int d, std::vector<std::string>& sounds) {
    // A window of rows less one (the row it came from stays in view).
    const int page = std::max(1, int(view_h() / layout_.row_h) - 1);
    if (lifted_) {
        std::vector<std::string> steps;
        for (int i = 0; i < page; ++i) {
            const int at = focus_;
            move(d, steps);
            if (focus_ == at) break; // its set's end
        }
        sounds.push_back(steps.empty() ? "disable" : steps.front());
        return;
    }
    const int n = int(songs_.size());
    const int to = std::clamp(focus_ + d * page, focus_ < 0 ? -1 : 0, n);
    if (to == focus_) {
        sounds.push_back("disable");
        return;
    }
    sounds.push_back("cursor");
    focus_row(to, d);
}

bool MusicListPage::scroll_by(const SDL_FRect& area, float x, float y, float dy) {
    if (!open_ || !editable_ || x < area.x || x >= area.x + area.w || y < area.y || y >= area.y + area.h - tabs_h())
        return false;
    const float list_h = layout_.row_h * float(songs_.size()) + layout_.mode_h;
    scroll_ = std::clamp(scroll_ + dy, 0.0f, std::max(0.0f, list_h - view_h()));
    scroll_target_ = scroll_, scroll_step_ = 0;
    return true;
}

void MusicListPage::focus_row(int row, int dir) {
    const int n = int(songs_.size());
    // Between two song rows the cursor keeps its column (check mark / title).
    if (focus_ >= 0 && focus_ < n && row >= 0 && row < n) columns_[size_t(row)] = columns_[size_t(focus_)];
    focus_ = row;
    preview_focus();
    if (dir && row >= 0) scroll_to_focus(dir);
}

void MusicListPage::side(int d, std::vector<std::string>& sounds) {
    if (lifted_) return; // a lifted song ignores Left / Right
    if (focus_ < 0) { // along the tabs and Order / Shuffle; Confirm takes one
        if (top_col_ + d < 0 || top_col_ + d >= top_items()) {
            sounds.push_back("disable");
            return;
        }
        top_col_ += d;
        sounds.push_back("cursor");
        return;
    }
    int& c = focus_ == int(songs_.size()) ? mode_ : columns_[size_t(focus_)];
    if (c + d < 0 || c + d > 1) {
        sounds.push_back("disable");
        return;
    }
    c += d;
    sounds.push_back("cursor");
}

void MusicListPage::confirm(std::vector<std::string>& sounds) {
    if (lifted_) {
        lifted_ = false;
        sounds.push_back("select");
        preview_focus(); // its row takes the cursor again: the intro starts over
        return;
    }
    if (focus_ < 0) { // the tab row: the tab or Order / Shuffle under the cursor
        const int n = int(tabs_.size());
        if (top_col_ < n) {
            choose_tab(top_col_, sounds);
        } else if (mode_ != top_col_ - n) {
            mode_ = top_col_ - n;
            sounds.push_back("ok");
        }
        return;
    }
    if (focus_ >= int(songs_.size())) return; // Order / Shuffle: Left / Right choose
    Song& s = songs_[size_t(focus_)];
    sounds.push_back("ok");
    if (columns_[size_t(focus_)] == 0) {
        s.listed = !s.listed;
        preview_ = s.listed ? track(focus_) : -1;
    } else {
        lifted_ = true;
    }
}

void MusicListPage::scroll_to_focus(int dir) {
    // From where the list is now: keep the cursor's row in view and, while moving, in the
    // middle (centred towards the move only), within the list's ends; 4 frames to get there.
    const float list_h = layout_.row_h * float(songs_.size()) + layout_.mode_h;
    const float s = scroll_, v = view_h();
    const float top = row_top(focus_), h = row_height(focus_);
    const float up_room = std::max(0.0f, s), down_room = std::max(0.0f, list_h - s - v);
    float above = top - s, below = v - (top - s + h);
    const float centre = (v - h) * 0.5f;
    if (dir < 0) above -= centre;
    else if (dir > 0) below -= centre;
    const float delta = std::max(std::min(std::min(0.0f, below) - std::min(0.0f, above), up_room), -down_room);
    scroll_target_ = s - delta;
    scroll_step_ = (scroll_target_ - s) * 0.25f;
}

void MusicListPage::commit(race::RacePlaylist& list) const { list.commit(songs_, mode_ == 1); }

int MusicListPage::row_at(const SDL_FRect& area, float x, float y, int& col) const {
    col = -1;
    if (x < area.x || x >= area.x + area.w || y < area.y || y >= area.y + area.h) return -2;
    if (y >= area.y + area.h - tabs_h()) { // the tabs
        col = tab_at(area, x);
        return -1;
    }
    const float down = area.y + area.h - tabs_h() - y + scroll_; // from the list's top
    const int n = int(songs_.size());
    const Layout& g = layout_;
    const float songs_h = g.row_h * float(n);
    int row = -1;
    if (down >= 0 && down < songs_h) row = std::min(n - 1, int(down / g.row_h));
    else if (down >= songs_h && down < songs_h + g.mode_h) row = n;
    if (row < 0) return -2;
    const float dx = x - area.x;
    if (row < n) col = dx < g.label_x ? 0 : 1;
    else
        for (int i = 0; i < 2; ++i)
            if (dx >= g.button_x[i] && dx < g.button_x[i] + g.button_w) col = i;
    return row;
}

bool MusicListPage::hover(const SDL_FRect& area, float x, float y, std::vector<std::string>& sounds) {
    if (!open_ || !editable_) return false;
    int col = -1;
    const int row = row_at(area, x, y, col);
    if (row < -1) return false;
    if (lifted_) return true; // a lifted song moves by key or tap only
    const bool song = row >= 0 && row < int(songs_.size());
    bool moved = false;
    if (row != focus_) {
        focus_row(row, 0); // the list holds still under a hovering pointer
        moved = true;
    }
    if (row < 0 && col >= 0 && col != top_col_) top_col_ = col, moved = true;
    if (song && col >= 0 && columns_[size_t(row)] != col) {
        columns_[size_t(row)] = col;
        moved = true;
    }
    if (moved) sounds.push_back("cursor");
    return true;
}

bool MusicListPage::tap(const SDL_FRect& area, float x, float y, std::vector<std::string>& sounds) {
    if (!open_ || !editable_) return false;
    int col = -1;
    const int row = row_at(area, x, y, col);
    if (row < -1) return false;
    const int n = int(songs_.size());
    if (row < 0) { // the tab row: a tab, Order or Shuffle
        if (lifted_) return true;
        if (focus_ != row) focus_row(row, 0), sounds.push_back("cursor");
        if (col < 0) return true;
        top_col_ = col;
        const int n_tabs = int(tabs_.size());
        if (col < n_tabs) choose_tab(col, sounds);
        else if (mode_ != col - n_tabs) mode_ = col - n_tabs, sounds.push_back("cursor");
        return true;
    }
    if (lifted_) {
        if (row == focus_) {
            confirm(sounds); // put it down
        } else {
            // Carried row by row to the tapped place (as far as the last song).
            const int to = std::min(row, n - 1), d = to > focus_ ? 1 : -1;
            std::vector<std::string> steps;
            while (focus_ != to) move(d, steps);
            if (!steps.empty()) sounds.push_back(steps.front());
        }
        return true;
    }
    if (row == n) {
        // Order / Shuffle: a tap on a button chooses it.
        bool moved = false;
        if (focus_ != n) focus_row(n, n > focus_ ? 1 : -1), moved = true;
        if (col >= 0 && col != mode_) mode_ = col, moved = true;
        if (moved) sounds.push_back("cursor");
        return true;
    }
    if (row == focus_ && col == columns_[size_t(row)]) {
        confirm(sounds);
        return true;
    }
    if (row != focus_) focus_row(row, row > focus_ ? 1 : -1);
    if (col >= 0) columns_[size_t(row)] = col;
    sounds.push_back("cursor");
    return true;
}

void MusicListPage::draw(const Layer* hidden, const ElementDraw& w, std::vector<ElementDraw>& out) const {
    if (!open_ || !w.element) return;
    const Element& box = *w.element;
    const Layout& g = layout_;
    const SDL_FRect whole{box.x, box.y, box.w, box.h};
    const SDL_FRect area{box.x, box.y, box.w, box.h - tabs_h()}; // the list's window, under the tabs
    const float list_top = area.y + area.h + scroll_; // layout y of the list's top edge (y up)
    std::vector<ElementDraw> add;
    auto put_part = [&](const Element* e, const SDL_FRect& r, float brightness, const std::string* text, float text_dx,
                        const SDL_FRect& clip) {
        if (!e) return;
        ElementDraw d;
        d.scene = w.scene;
        d.element = e;
        d.alpha = w.alpha;
        d.dx = w.dx, d.dy = w.dy;
        d.has_box = true;
        d.box = r;
        d.has_clip = true;
        d.clip = clip;
        d.brightness = brightness;
        d.text_dx = text_dx;
        d.z = e->z;
        if (text) d.text = std::make_shared<const std::string>(*text);
        add.push_back(std::move(d));
    };
    auto put = [&](const char* name, const SDL_FRect& r, float brightness = 1, const std::string* text = nullptr,
                   float text_dx = 0) { put_part(find_in(hidden, name), r, brightness, text, text_dx, area); };
    // A part's box (layout units) from its offsets in a row whose top edge is at `top`.
    auto part = [&](float top, float x, float y, float pw, float ph) {
        return SDL_FRect{area.x + x, top - y - ph, pw, ph};
    };
    const int n = int(songs_.size());
    const bool cursor = editable_ && !lifted_;
    const float lit = 1.0f + pulse_; // the highlighted row's brightness pulse (1.5 -> 1)
    for (int i = 0; i <= n; ++i) {
        const float top = list_top - row_top(i), h = i < n ? g.row_h : g.mode_h;
        if (top - h >= area.y + area.h || top <= area.y) continue; // outside the window
        const bool on = cursor && i == focus_;
        if (i < n) {
            const Song& s = songs_[size_t(i)];
            const std::string t = text(i);
            put(on ? "label_check_focus" : "label_check_box", part(top, 0, 0, g.row_w, g.row_h), on ? lit : 1);
            const bool check = on && columns_[size_t(i)] == 0, title = on && columns_[size_t(i)] == 1;
            put(s.listed ? (check ? "check_on_focus" : "check_on") : (check ? "check_off_focus" : "check_off"),
                part(top, g.check_x, g.check_y, g.check_w, g.check_h), check ? lit : 1);
            put(title ? "label_focus" : "label_box", part(top, g.label_x, g.label_y, g.label_w, g.label_h),
                title ? lit : 1, &t, kTextInset);
        } else {
            if (on) put("label_check_focus", part(top, 0, 0, g.row_w, g.mode_h), lit);
            put(mode_ == 0 ? "button_straight_focus" : "button_straight",
                part(top, g.button_x[0], g.button_y, g.button_w, g.button_h));
            put(mode_ == 1 ? "button_shuffle_focus" : "button_shuffle",
                part(top, g.button_x[1], g.button_y, g.button_w, g.button_h));
        }
    }
    if (tabs_shown()) {
        // Ours: the tabs, as unfocused / focused titles with their text centred.
        const char* names[2] = {"label_box", "label_focus"};
        for (int k = 0; k < 2; ++k)
            if (const Element* e = find_in(hidden, names[k])) {
                tab_part_[k] = *e;
                if (tab_part_[k].text) tab_part_[k].text->align = u16((tab_part_[k].text->align & ~0xF) | 4);
            }
        const bool on = editable_ && !lifted_ && focus_ < 0;
        // The cursor: the row's highlight behind the item it is on (the tab shown and the mode
        // chosen keep their focused look), pulsing.
        const int n_tabs = int(tabs_.size());
        if (on) {
            const SDL_FRect b = tab_box(whole, top_col_);
            constexpr float kPad = 4;
            put_part(find_in(hidden, "label_check_focus"), {b.x - kPad, b.y - kPad, b.w + 2 * kPad, b.h + 2 * kPad}, lit, nullptr,
                     0, whole);
        }
        for (int i = 0; i < n_tabs; ++i)
            put_part(&tab_part_[i == tab_ ? 1 : 0], tab_box(whole, i), on && top_col_ == i ? lit : 1, &tabs_[size_t(i)], 0,
                     whole);
        put_part(find_in(hidden, mode_ == 0 ? "button_straight_focus" : "button_straight"), tab_box(whole, n_tabs),
                 on && top_col_ == n_tabs ? lit : 1, nullptr, 0, whole);
        put_part(find_in(hidden, mode_ == 1 ? "button_shuffle_focus" : "button_shuffle"), tab_box(whole, n_tabs + 1),
                 on && top_col_ == n_tabs + 1 ? lit : 1, nullptr, 0, whole);
    }
    if (editable_ && lifted_ && focus_ >= 0 && focus_ < n) {
        // The lifted song's bar, centred on its row, pulsing (1.5 -> 0.5).
        const Song& s = songs_[size_t(focus_)];
        const std::string t = text(focus_);
        const float mid = list_top - row_top(focus_) - g.row_h * 0.5f;
        const float top = mid + g.pop_h * 0.5f;
        put("pop_label_check_box", part(top, g.pop_x, 0, g.pop_w, g.pop_h), 2.0f * lift_pulse_ + 0.5f);
        put(s.listed ? "check_on" : "check_off", part(top, g.pop_check_x, 0, g.pop_check_w, g.pop_check_h));
        put("pop_label_box", part(top, g.pop_label_x, 0, g.pop_label_w, g.pop_label_h), 1, &t, kTextInset);
    }
    std::stable_sort(add.begin(), add.end(), [](const ElementDraw& a, const ElementDraw& b) { return a.z < b.z; });
    for (ElementDraw& d : add) out.push_back(std::move(d));
}

} // namespace gt3r::menu::flow
