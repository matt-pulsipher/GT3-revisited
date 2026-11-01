#pragma once

// The race playlist (music §2.3): the Options music list kept as a playlist that
// carries over from race to race. It holds up to 64 race songs, each with the id
// code of its file name (db::id_hash("race_a01.ads")), an "in the list" mark, a
// "played" mark and its place in the current order, plus a cursor, a shuffle
// switch and a 16-bit seed.
//
//   - next() takes the song at the cursor's place in the order and moves the
//     cursor on; a song already played (or not in the list) is skipped. When the
//     cursor passes the end the order is rebuilt and the cursor starts again.
//   - rebuild(): every listed song becomes unplayed, the others played; then the
//     order is the list order, or (shuffle on) for i = 0 .. n-1 a draw
//     k = trunc(unit x n) on the game's generator (seeded with the stored seed)
//     whose first free place from k on (wrapping) gets order i; one more draw
//     stores the next seed, trunc(unit x 32768).
//   - with no song in the list, next() gives nothing and no race song plays.
// Ours: the player's own music keeps a list of the same kind (limit kMaxCustomSongs, ids
// custom_song_id), kept in step with their music folder by sync().
// SDL-free.

#include "base/bytes.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r::race {

class RacePlaylist {
public:
    static constexpr size_t kMaxSongs = 64;
    static constexpr size_t kMaxCustomSongs = 10000; // the player's own music (orders are s16)

    struct Song {
        u64 id = 0;          // db::id_hash of the song's file name
        bool listed = true;  // in the music list
        bool played = false; // taken in this pass (or not listed)
        s16 order = 0;       // its place in this pass's order
        bool operator==(const Song&) const = default;
    };

    // A new game's list: the race songs in group order (at most 64), all in the
    // list, shuffle on, the first order built from `seed`.
    // `limit`: the most songs the list holds (kMaxSongs, or kMaxCustomSongs for the player's own).
    static RacePlaylist with_songs(const std::vector<u64>& ids, u16 seed, size_t limit = kMaxSongs);
    // The stored list (its saved text, to_text()) when there is a valid one, else a new
    // list of `ids` whose seed is an integer draw on the shared generator.
    static RacePlaylist stored_or_new(const std::optional<std::string>& stored, const std::vector<u64>& ids,
                                      size_t limit = kMaxSongs);

    // The next song to play: an index into songs(), or nothing when no song is in
    // the list.
    std::optional<size_t> next();
    // Ours: the song next() would take now (a new pass's first when this one is over), without
    // taking it.
    std::optional<size_t> peek() const;
    // A new pass: played marks from the list marks, a new order, the cursor at 0.
    void rebuild();
    // The shuffle switch, then a rebuild.
    void set_shuffle(bool on);
    // The Options music list's commit (menus §10.1): the songs as the page lists them (its
    // order, its check marks; played / order as they were) replace the entries, then the
    // shuffle switch and a rebuild (a new pass: cursor 0, a new order).
    void commit(const std::vector<Song>& songs, bool shuffle);
    void set_listed(size_t i, bool on) {
        if (i < songs_.size()) songs_[i].listed = on;
    }
    // Ours (the player's own music): the songs become `ids`: songs no longer there leave, the
    // others keep their place, marks and order, new ones join at the end in the list. Any
    // change starts a new pass (rebuild). True when the songs changed.
    bool sync(const std::vector<u64>& ids);
    // Songs still to come in this pass: listed and not played.
    size_t remaining() const;

    bool has_songs() const { return !none_; }
    bool shuffle() const { return shuffle_; }
    u16 cursor() const { return cursor_; }
    u16 seed() const { return seed_; }
    const std::vector<Song>& songs() const { return songs_; }

    // The BGM track index of song `id` in the race-song group `group` (the ids of
    // its files in order); 0 when it is not there.
    static int track_index(u64 id, const std::vector<u64>& group);

    // Stored form ([options] music_playlist):
    //   "1 <cursor> <shuffle 0|1> <none 0|1> <seed> <id hex>:<listed>:<played>:<order> ..."
    std::string to_text() const;
    static std::optional<RacePlaylist> from_text(std::string_view text, size_t limit = kMaxSongs); // nothing if malformed

    bool operator==(const RacePlaylist&) const = default;

private:
    std::vector<Song> songs_;
    u16 cursor_ = 0;
    bool shuffle_ = true;
    bool none_ = true; // no song in the list (set by rebuild)
    u16 seed_ = 0;
    size_t limit_ = kMaxSongs;
};

} // namespace gt3r::race
