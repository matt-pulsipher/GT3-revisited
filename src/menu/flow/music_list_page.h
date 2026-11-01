#pragma once

// The Options "Favorite Music List" (menus §10.1): the race playlist (music §4.4,
// race::RacePlaylist) shown and edited on option.mbl page "music/music" (its widget of
// class "song_edit"); the top page's preview (class "song_list") shows the same list
// without a cursor.
//
// The list: one 24-unit row per song of the playlist, in its order (a check mark and
// "title / artist"), then the Order / Shuffle row. The cursor is on a row and, on a song
// row, on its check mark or its title:
//   - Up / Down move between the rows (no wrap; SE "cursor", "disable" at an end); a song
//     row takes the check / title column of the song row it came from. The list scrolls so
//     the cursor's row stays in the middle while it moves (clamped at the list's ends).
//   - Left / Right move between the check mark and the title, or choose Order / Shuffle
//     on the last row (SE "cursor", "disable" at an end).
//   - Confirm on the check mark: the song in or out of the list (SE "ok"). Confirm on the
//     title lifts the song (SE "ok"): Up / Down then carry it past its neighbours (not
//     below the last song), Left / Right do nothing, Confirm puts it down (SE "select").
//   - The focused song's intro plays (looping) while it is in the list; nothing while the
//     cursor is on an unlisted song or on the last row; a lifted song keeps playing.
//   - Cancel (the page's own back) keeps the edit: the songs in the page's order with
//     their marks and the Order / Shuffle choice go into the playlist, which starts a new
//     pass. With a song lifted the edit is dropped.
// Pointer (our addition): hovering a row moves the cursor there (the list holds still); a
// tap moves the cursor there, or acts like Confirm on the cursor's place; with a song
// lifted a tap carries it to the tapped row, a tap on it puts it down.
// Tabs (ours, with the player's own music: race/custom_music.h): a row above the list with the
// tabs (Original / Custom / Both) and, at its right, Order / Shuffle again (the same choice as
// the last row's, which a long list puts out of reach); the list's window is one mode row
// shorter under it. Up from the first song reaches the row; Left / Right move along it (SE
// "cursor", "disable" at an end) and Confirm takes what the cursor is on: a tab (SE "cursor";
// never with a song lifted) or Order / Shuffle (SE "ok"); a tap takes one at once; L1 / R1
// choose a tab from anywhere. The flow takes the tab (take_tab) and opens the page again on
// the other list. Both lists one set of songs after another (set_split): a lifted song moves
// only within its own set.
// Long lists (ours): L2 / R2 move the cursor (or carry a lifted song) a window of rows up or
// down (SE "cursor", "disable" at an end); a drag over the list or the mouse wheel scrolls it
// without moving the cursor.
//
// SDL-free and frame based like MenuController.

#include "menu/flow/menu_controller.h"
#include "menu/menu_renderer.h"
#include "race/race_playlist.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gt3r::menu::flow {

// A race song on the disc (bgm/ads.inf, the race-song group in track order).
struct MusicTrack {
    u64 id = 0; // db::id_hash of its file name
    std::string title, artist;
    std::string file = {}; // ours: one of the player's songs (a host path); empty for the disc's
};

// The rows' parts, from the page's hidden art (`label_check_box`, `check_box`, `label_box`,
// `mode_select_box`, `button_straight` / `button_shuffle`, `pop_*`); the defaults are the
// original's. x offsets from a row's left edge, y offsets down from its top.
struct MusicListLayout {
    float view_h = 308; // the widget's height (the list's window)
    float row_w = 580, row_h = 24, mode_h = 32;
    float check_x = 0, check_y = 0, check_w = 24, check_h = 24;
    float label_x = 28, label_y = 0, label_w = 552, label_h = 24;
    float button_x[2] = {169, 317}, button_y = 4, button_w = 102, button_h = 22; // Order, Shuffle
    float pop_x = -4, pop_w = 588, pop_h = 32; // the lifted song's bar, centred on its row
    float pop_check_x = -4, pop_check_w = 32, pop_check_h = 32;
    float pop_label_x = 32, pop_label_w = 552, pop_label_h = 32;
    static MusicListLayout from(const Layer* hidden, float view_h);
};

class MusicListPage {
public:
    using Song = race::RacePlaylist::Song;
    using Layout = MusicListLayout;

    // `editable`: the page's own list (cursor, preview); else the top page's view of it.
    void open(const race::RacePlaylist& list, std::vector<MusicTrack> tracks, bool editable,
              const Layout& layout = {});
    // Ours: the tabs (none with fewer than two names), the one chosen, the cursor on them.
    void set_tabs(std::vector<std::string> names, int chosen, bool focused);
    // Ours: rows from `split` on are the second set of songs (Both).
    void set_split(int split) { split_ = split; }
    // The tab chosen since the last call (to open its list).
    std::optional<int> take_tab() { return std::exchange(tab_request_, std::nullopt); }
    int tab() const { return tab_; }
    bool tabs_shown() const { return tabs_.size() > 1; }
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    // One frame: `fired` = the keys that fired (presses and direction repeats), `pressed` =
    // the presses (MenuController::fired / pressed). Cancel is the page's (see commit).
    void tick(MenuKeys fired, MenuKeys pressed, std::vector<std::string>& sounds);
    // The edit into the playlist (Cancel without a lifted song).
    void commit(race::RacePlaylist& list) const;
    bool lifted() const { return lifted_; }

    // The preview asked for since the last call: a track index of the race-song group (its
    // intro, looping), or -1 to stop.
    std::optional<int> take_preview() { return std::exchange(preview_, std::nullopt); }

    // Pointer in layout units (y up) over the list drawn in `area` (the widget's box).
    bool hover(const SDL_FRect& area, float x, float y, std::vector<std::string>& sounds);
    bool tap(const SDL_FRect& area, float x, float y, std::vector<std::string>& sounds);
    // Ours: the list scrolled by `dy` layout units (up: the rows move up) under a drag or the
    // wheel at (x, y); false when the point is off the list.
    bool scroll_by(const SDL_FRect& area, float x, float y, float dy);

    // The rows over the widget `w` (its draw), with the page's hidden art (`hidden`).
    void draw(const Layer* hidden, const ElementDraw& w, std::vector<ElementDraw>& out) const;

    // State.
    int rows() const { return int(songs_.size()) + 1; } // the songs and the Order / Shuffle row
    int focus() const { return focus_; }                // -1: the tabs
    int top_column() const { return top_col_; }         // on the tabs: a tab, then Order, Shuffle
    int column(int row) const { return row >= 0 && row < int(columns_.size()) ? columns_[size_t(row)] : 0; }
    bool shuffle() const { return mode_ == 1; }
    const std::vector<Song>& songs() const { return songs_; }
    std::string text(int row) const;
    int track(int row) const; // the song's track index in the group (0 when not on the disc)
    float scroll() const { return scroll_; }

    const Layout& layout() const { return layout_; }

private:
    float row_top(int row) const { return layout_.row_h * float(row); } // from the list's top, down
    float tabs_h() const { return tabs_shown() ? layout_.mode_h : 0; }
    float view_h() const { return layout_.view_h - tabs_h(); } // the list's window under the tabs
    void choose_tab(int tab, std::vector<std::string>& sounds);
    int top_items() const { return int(tabs_.size()) + 2; } // the tabs, Order, Shuffle
    int tab_at(const SDL_FRect& area, float x) const;      // the item under x on the tab row, or -1
    SDL_FRect tab_box(const SDL_FRect& area, int i) const; // item i: a tab, then Order, Shuffle
    void jump(int d, std::vector<std::string>& sounds);    // L2 / R2
    float row_height(int row) const { return row < int(songs_.size()) ? layout_.row_h : layout_.mode_h; }
    void move(int d, std::vector<std::string>& sounds);
    void side(int d, std::vector<std::string>& sounds);
    void confirm(std::vector<std::string>& sounds);
    void focus_row(int row, int dir); // cursor to `row` (column carried), preview, scroll by `dir`
    void scroll_to_focus(int dir);
    void preview_focus();
    int row_at(const SDL_FRect& area, float x, float y, int& col) const;

    bool open_ = false, editable_ = false;
    Layout layout_;
    std::vector<Song> songs_;
    std::vector<int> columns_; // per song row: 0 check mark, 1 title (the mode row's is mode_)
    std::vector<MusicTrack> tracks_;
    int focus_ = 0;
    int mode_ = 1; // 0 Order, 1 Shuffle
    bool lifted_ = false;
    std::optional<int> preview_;
    float scroll_ = 0, scroll_target_ = 0, scroll_step_ = 0;
    float pulse_ = 0, lift_pulse_ = 0; // the highlight's 30-frame pulse (0.5 -> 0)
    int pulse_row_ = -2;
    std::vector<std::string> tabs_;
    int tab_ = 0, split_ = -1;
    int top_col_ = 0; // the cursor's item on the tab row
    std::optional<int> tab_request_;
    mutable Element tab_part_[2]; // label_box / label_focus with centred text (draw)
};

} // namespace gt3r::menu::flow
