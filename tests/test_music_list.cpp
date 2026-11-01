// The Options "Favorite Music List" (menus §10.1): the list page on its own (keys, pointer,
// preview requests, scrolling, the commit into the race playlist, music §4.4) with invented
// songs, and on the disc every race song listed with its title.

#include "test_framework.h"

#include "formats/db/gt_db.h"
#include "formats/sound/mads.h"
#include "menu/flow/game_flow.h"
#include "menu/flow/music_list_page.h"
#include "race/race_playlist.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::menu::flow;
using race::RacePlaylist;

namespace {

// The flow's music list preview as a track index (-1 stop), or -99 when none was asked for.
int preview_track(GameFlow& f) {
    const std::optional<GameFlow::MusicPreview> p = f.take_preview();
    return p ? p->track : -99;
}

const std::vector<u64> kIds = {0x1111, 0x2222, 0x3333, 0x4444, 0x5555};

std::vector<MusicTrack> tracks() {
    return {{0x1111, "Alpha", "One"}, {0x2222, "Beta", "Two"}, {0x3333, "Gamma", "Three"},
            {0x4444, "Delta", "Four"}, {0x5555, "Epsilon", "Five"}};
}

// One frame with one key pressed (it fires too); the sounds it made.
std::vector<std::string> key(MusicListPage& m, MenuKey k) {
    std::vector<std::string> s;
    m.tick(key_bit(k), key_bit(k), s);
    return s;
}

std::vector<u64> order(const MusicListPage& m) {
    std::vector<u64> v;
    for (const RacePlaylist::Song& s : m.songs()) v.push_back(s.id);
    return v;
}

using Sounds = std::vector<std::string>;

} // namespace

// Keys: the cursor, its column, the check marks, lifting and carrying a song, Order /
// Shuffle, the preview requests and the commit.
TEST(music_list_page_keys) {
    RacePlaylist p = RacePlaylist::with_songs(kIds, 0x1234); // shuffle on, all listed
    MusicListPage m;
    m.open(p, tracks(), true);
    CHECK(m.rows() == 6 && m.focus() == 0 && m.column(0) == 0 && m.shuffle() && !m.lifted());
    CHECK(m.text(0) == "Alpha / One" && m.text(4) == "Epsilon / Five");
    CHECK(m.take_preview() == 0 && !m.take_preview()); // the first song's intro, once
    // Up at the top: blocked (no wrap).
    CHECK(key(m, MenuKey::Up) == Sounds{"disable"} && m.focus() == 0);
    CHECK(key(m, MenuKey::Down) == Sounds{"cursor"} && m.focus() == 1 && m.take_preview() == 1);
    // Left / Right: the check mark and the title.
    CHECK(key(m, MenuKey::Right) == Sounds{"cursor"} && m.column(1) == 1 && !m.take_preview());
    CHECK(key(m, MenuKey::Right) == Sounds{"disable"} && m.column(1) == 1);
    // Down: the next row takes the column.
    CHECK(key(m, MenuKey::Down) == Sounds{"cursor"} && m.focus() == 2 && m.column(2) == 1);
    CHECK(m.take_preview() == 2);
    // Confirm on the title lifts the song; Left / Right do nothing; the preview goes on.
    CHECK(key(m, MenuKey::Confirm) == Sounds{"ok"} && m.lifted() && !m.take_preview());
    CHECK(key(m, MenuKey::Left).empty() && m.column(2) == 1);
    // Up / Down carry it (the cursor goes along), never below the last song.
    CHECK(key(m, MenuKey::Up) == Sounds{"cursor"} && m.focus() == 1);
    CHECK((order(m) == std::vector<u64>{0x1111, 0x3333, 0x2222, 0x4444, 0x5555}));
    key(m, MenuKey::Down);
    key(m, MenuKey::Down);
    key(m, MenuKey::Down);
    CHECK(m.focus() == 4 && (order(m) == std::vector<u64>{0x1111, 0x2222, 0x4444, 0x5555, 0x3333}));
    CHECK(key(m, MenuKey::Down) == Sounds{"disable"} && m.focus() == 4);
    CHECK(!m.take_preview());
    // Confirm puts it down: its intro starts over.
    CHECK(key(m, MenuKey::Confirm) == Sounds{"select"} && !m.lifted() && m.take_preview() == 2);
    // The check mark: out of the list (the preview stops), back in, out again.
    CHECK(key(m, MenuKey::Left) == Sounds{"cursor"} && m.column(4) == 0);
    CHECK(key(m, MenuKey::Confirm) == Sounds{"ok"} && !m.songs()[4].listed && m.take_preview() == -1);
    CHECK(key(m, MenuKey::Confirm) == Sounds{"ok"} && m.songs()[4].listed && m.take_preview() == 2);
    key(m, MenuKey::Confirm);
    CHECK(m.take_preview() == -1);
    // An unlisted song plays nothing when the cursor comes to it.
    key(m, MenuKey::Up);
    CHECK(m.take_preview() == 4); // (row 3 holds the fifth song now)
    key(m, MenuKey::Down);
    CHECK(m.take_preview() == -1);
    // The last row: Order / Shuffle; the music stops there.
    CHECK(key(m, MenuKey::Down) == Sounds{"cursor"} && m.focus() == 5 && m.take_preview() == -1);
    CHECK(key(m, MenuKey::Right) == Sounds{"disable"} && m.shuffle());
    CHECK(key(m, MenuKey::Left) == Sounds{"cursor"} && !m.shuffle());
    CHECK(key(m, MenuKey::Left) == Sounds{"disable"});
    CHECK(key(m, MenuKey::Down) == Sounds{"disable"} && m.focus() == 5);
    CHECK(key(m, MenuKey::Confirm).empty()); // nothing to confirm there
    // Back up to a song row: it keeps its own column.
    key(m, MenuKey::Up);
    CHECK(m.focus() == 4 && m.column(4) == 0 && m.take_preview() == -1);
    // The commit: the page's order and marks, Order, a new pass.
    p.next();
    m.commit(p);
    CHECK(!p.shuffle() && p.cursor() == 0 && p.has_songs());
    CHECK(p.songs().size() == 5 && p.songs()[4].id == 0x3333 && !p.songs()[4].listed && p.songs()[4].played);
    CHECK(p.next() == 0u && p.next() == 1u && p.next() == 2u && p.next() == 3u && p.next() == 0u);
    // A song missing from the disc shows (and previews) the group's first song.
    RacePlaylist q = RacePlaylist::with_songs({0x9999}, 1);
    m.open(q, tracks(), true);
    CHECK(m.text(0) == "Alpha / One" && m.take_preview() == 0);
    // The top page's view: no cursor, no preview.
    MusicListPage v;
    v.open(p, tracks(), false);
    CHECK(!v.take_preview() && !v.shuffle());
    CHECK(key(v, MenuKey::Down).empty() && v.focus() == 0);
}

// The list scrolls to keep the cursor's row in the middle while it moves, within the list's
// ends, in 4 frames; a lifted song's moves scroll too.
TEST(music_list_page_scroll) {
    RacePlaylist p = RacePlaylist::with_songs(kIds, 7);
    MusicListLayout lay;
    lay.view_h = 72; // three rows
    MusicListPage m;
    m.open(p, tracks(), true, lay);
    auto settle = [&] {
        Sounds s;
        for (int i = 0; i < 4; ++i) m.tick(0, 0, s);
    };
    key(m, MenuKey::Down); // row 1 is above the middle: no scroll
    settle();
    CHECK(m.scroll() == 0);
    key(m, MenuKey::Down); // row 2: to the middle (top 24 down)
    Sounds s;
    m.tick(0, 0, s);
    CHECK(m.scroll() == 6); // a quarter a frame
    settle();
    CHECK(m.scroll() == 24);
    for (int i = 0; i < 3; ++i) key(m, MenuKey::Down), settle();
    // The last row (32 high) at the bottom of the window: the list's end (5 x 24 + 32 - 72).
    CHECK(m.focus() == 5 && m.scroll() == 80);
    key(m, MenuKey::Up), settle();
    CHECK(m.scroll() == 72); // row 4 to the middle
    key(m, MenuKey::Up), settle();
    CHECK(m.scroll() == 48); // row 3
    for (int i = 0; i < 3; ++i) key(m, MenuKey::Up), settle();
    CHECK(m.focus() == 0 && m.scroll() == 0);
    // Lifted: carrying the song down scrolls the same way.
    key(m, MenuKey::Right);
    key(m, MenuKey::Confirm);
    for (int i = 0; i < 3; ++i) key(m, MenuKey::Down), settle();
    CHECK(m.lifted() && m.focus() == 3 && m.scroll() == 48 && order(m).back() == 0x5555);
}

// The pointer: hovering a row moves the cursor there (column under the pointer, the list
// holds still); a tap on the cursor's place acts like Confirm, elsewhere moves the cursor;
// with a song lifted a tap carries it to the tapped row and a tap on it puts it down.
TEST(music_list_page_pointer) {
    RacePlaylist p = RacePlaylist::with_songs(kIds, 7);
    MusicListPage m;
    m.open(p, tracks(), true);
    m.take_preview();
    const SDL_FRect area{28, 44, 580, 308}; // the widget's box (layout units, y up)
    auto row_y = [&](int r) { return area.y + area.h - 24.0f * float(r) - 12.0f; };
    Sounds s;
    CHECK(!m.hover(area, 10, row_y(1), s) && !m.hover(area, 300, 20, s)); // outside the window
    CHECK(m.hover(area, 300, row_y(2), s) && m.focus() == 2 && m.column(2) == 1 && m.scroll() == 0);
    CHECK(s == Sounds{"cursor"} && m.take_preview() == 2);
    s.clear();
    CHECK(m.hover(area, 300, row_y(2), s) && s.empty()); // no change
    CHECK(m.hover(area, 40, row_y(2), s) && m.column(2) == 0 && s == Sounds{"cursor"});
    // Tap on the cursor's check mark: out of the list.
    s.clear();
    CHECK(m.tap(area, 40, row_y(2), s) && !m.songs()[2].listed && s == Sounds{"ok"} && m.take_preview() == -1);
    // Tap on another row's title: the cursor goes there; a second tap lifts it.
    CHECK(m.tap(area, 300, row_y(4), s) && m.focus() == 4 && m.column(4) == 1 && !m.lifted());
    CHECK(m.tap(area, 300, row_y(4), s) && m.lifted());
    // Lifted: hovering does nothing, a tap on row 1 carries it there, a tap on it puts it down.
    CHECK(m.hover(area, 300, row_y(0), s) && m.focus() == 4);
    CHECK(m.tap(area, 300, row_y(1), s) && m.focus() == 1 && order(m)[1] == 0x5555 && m.lifted());
    CHECK(m.tap(area, 300, row_y(1), s) && !m.lifted() && m.take_preview() == 4);
    // Order / Shuffle: a tap on a button chooses it (the row after the five songs).
    const float mode_y = area.y + area.h - 24.0f * 5 - 16.0f;
    CHECK(m.tap(area, 28 + 200, mode_y, s) && m.focus() == 5 && !m.shuffle());
    CHECK(m.tap(area, 28 + 350, mode_y, s) && m.shuffle());
}

// The options' commit and the stored list: a valid stored list is kept, else a new one.
TEST(music_list_playlist_commit) {
    RacePlaylist p = RacePlaylist::with_songs(kIds, 0x1234);
    const RacePlaylist kept = RacePlaylist::stored_or_new(p.to_text(), kIds);
    CHECK(kept == p);
    const RacePlaylist fresh = RacePlaylist::stored_or_new(std::string("garbage"), kIds);
    CHECK(fresh.songs().size() == 5 && fresh.shuffle() && fresh.cursor() == 0);
    std::vector<RacePlaylist::Song> songs = p.songs();
    std::reverse(songs.begin(), songs.end());
    songs[0].listed = false;
    const u16 seed = p.seed();
    p.commit(songs, false);
    CHECK(!p.shuffle() && p.cursor() == 0 && p.seed() == seed && p.songs()[0].id == 0x5555 && p.songs()[0].played);
    CHECK(p.next() == 1u); // the first listed song in the page's order
}

// On the disc: the page lists every race song of bgm/ads.inf with its title / artist, in a new
// list's order (the group's), each drawn when the cursor comes to it; the first one's preview
// plays on entry.
TEST(music_list_disc_songs) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    std::vector<MusicTrack> group;
    {
        std::optional<Bytes> inf = vfs_reader(vfs)("bgm/ads.inf");
        CHECK(inf.has_value());
        if (!inf) return;
        const sound::MadsIndex mads = sound::MadsIndex::parse(*inf);
        for (const sound::MadsTrack& t : mads.groups[0x14 - 0x12])
            group.push_back({db::id_hash(t.file), t.title, t.artist});
    }
    CHECK(group.size() == 25);
    IniDocument cfg;
    GameFlow f(vfs_reader(vfs), "US");
    f.set_options(&cfg, [] {});
    f.debug_open("option", "music/music");
    for (int i = 0; i < 60; ++i) f.tick(0);
    const MusicListPage* m = f.music_list();
    CHECK(m != nullptr);
    if (!m) return;
    CHECK(preview_track(f) == 0);
    CHECK(m->rows() == int(group.size()) + 1);
    // The new list is kept in the options for the race.
    CHECK(cfg.get("options", "music_playlist").has_value());
    auto drawn = [&](const std::string& text) {
        for (const menu::ElementDraw& d : f.draw_list())
            if (d.text && *d.text == text) return true;
        return false;
    };
    int shown = 0;
    for (size_t i = 0; i < group.size(); ++i) {
        const std::string want = group[i].title + " / " + group[i].artist;
        CHECK(!group[i].title.empty() && m->text(int(i)) == want);
        if (m->focus() == int(i) && drawn(want)) ++shown;
        f.tick(key_bit(MenuKey::Down));
        for (int k = 0; k < 8; ++k) f.tick(0);
    }
    CHECK(shown == int(group.size()));
    CHECK(m->focus() == int(group.size())); // the Order / Shuffle row
}
