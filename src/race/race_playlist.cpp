#include "race/race_playlist.h"

#include "base/game_random.h"
#include "base/ps2_float.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <sstream>

namespace gt3r::race {

RacePlaylist RacePlaylist::with_songs(const std::vector<u64>& ids, u16 seed, size_t limit) {
    RacePlaylist p;
    p.limit_ = limit;
    for (size_t i = 0; i < ids.size() && i < limit; ++i) p.songs_.push_back(Song{ids[i]});
    p.seed_ = seed;
    p.set_shuffle(true);
    return p;
}

RacePlaylist RacePlaylist::stored_or_new(const std::optional<std::string>& stored, const std::vector<u64>& ids,
                                         size_t limit) {
    if (stored)
        if (std::optional<RacePlaylist> p = from_text(*stored, limit)) return std::move(*p);
    // A new list's seed is an integer draw on the shared generator (gt_mode §20.1).
    return with_songs(ids, u16(shared_random().int31()), limit);
}

void RacePlaylist::set_shuffle(bool on) {
    shuffle_ = on;
    rebuild();
}

void RacePlaylist::commit(const std::vector<Song>& songs, bool shuffle) {
    songs_.assign(songs.begin(), songs.begin() + std::ptrdiff_t(std::min(songs.size(), limit_)));
    set_shuffle(shuffle);
}

bool RacePlaylist::sync(const std::vector<u64>& ids) {
    std::vector<u64> wanted(ids.begin(), ids.begin() + std::ptrdiff_t(std::min(ids.size(), limit_)));
    std::vector<u64> sorted = wanted;
    std::sort(sorted.begin(), sorted.end());
    std::vector<Song> kept;
    std::vector<u64> have;
    for (const Song& s : songs_)
        if (std::binary_search(sorted.begin(), sorted.end(), s.id)) kept.push_back(s), have.push_back(s.id);
    std::sort(have.begin(), have.end());
    for (u64 id : wanted)
        if (!std::binary_search(have.begin(), have.end(), id)) kept.push_back(Song{id});
    if (kept.size() == songs_.size() && std::equal(kept.begin(), kept.end(), songs_.begin(),
                                                   [](const Song& a, const Song& b) { return a.id == b.id; }))
        return false;
    songs_ = std::move(kept);
    rebuild();
    return true;
}

size_t RacePlaylist::remaining() const {
    if (none_) return 0;
    return size_t(std::count_if(songs_.begin(), songs_.end(), [](const Song& s) { return s.listed && !s.played; }));
}

void RacePlaylist::rebuild() {
    none_ = true;
    for (Song& s : songs_) {
        s.played = !s.listed;
        if (s.listed) none_ = false;
    }
    cursor_ = 0;
    const u32 n = u32(songs_.size());
    if (!shuffle_) {
        for (u32 k = 0; k < n; ++k) songs_[k].order = s16(k);
        return;
    }
    for (Song& s : songs_) s.order = -1;
    u32 state = seed_;
    for (u32 i = 0; i < n; ++i) {
        // EE float arithmetic: the product rounds towards zero before the truncation.
        u32 k = u32(ps2::to_int(ps2::mul(GameRandom::rand01(state), float(n))));
        if (k >= n) k = n - 1; // (cannot happen: unit < 1)
        for (u32 probe = 0; probe < n && songs_[k].order >= 0; ++probe) k = k + 1 < n ? k + 1 : 0;
        songs_[k].order = s16(i);
    }
    seed_ = u16(ps2::to_int(GameRandom::rand01(state) * 32768.0f));
}

std::optional<size_t> RacePlaylist::next() {
    const size_t n = songs_.size();
    // Each pass looks at one place of the order; a full pass and a rebuild are enough to
    // find a song when one is listed.
    for (size_t guard = 0; guard <= 2 * n + 2; ++guard) {
        if (none_) return std::nullopt;
        size_t k = 0;
        while (k < n && songs_[k].order != s16(cursor_)) ++k;
        if (k == n) { // a damaged stored order (no song at the cursor's place): start a new pass
            rebuild();
            continue;
        }
        ++cursor_;
        std::optional<size_t> pick;
        if (!songs_[k].played) {
            songs_[k].played = true;
            pick = k;
        }
        if (cursor_ >= n) rebuild(); // the pass is over (also right after its last song)
        if (pick) return pick;
    }
    return std::nullopt;
}

std::optional<size_t> RacePlaylist::peek() const {
    RacePlaylist copy = *this; // next() is deterministic: the new pass's order comes from the seed
    return copy.next();
}

int RacePlaylist::track_index(u64 id, const std::vector<u64>& group) {
    auto it = std::find(group.begin(), group.end(), id);
    return it == group.end() ? 0 : int(it - group.begin());
}

std::string RacePlaylist::to_text() const {
    std::ostringstream o;
    o << "1 " << cursor_ << ' ' << int(shuffle_) << ' ' << int(none_) << ' ' << seed_;
    for (const Song& s : songs_) {
        char b[48];
        std::snprintf(b, sizeof b, " %016" PRIx64 ":%d:%d:%d", s.id, int(s.listed), int(s.played), int(s.order));
        o << b;
    }
    return o.str();
}

std::optional<RacePlaylist> RacePlaylist::from_text(std::string_view text, size_t limit) {
    std::istringstream in{std::string(text)};
    int version = 0;
    unsigned cursor = 0, shuffle = 0, none = 0, seed = 0;
    if (!(in >> version >> cursor >> shuffle >> none >> seed) || version != 1 || cursor > 0xFFFF || shuffle > 1 ||
        none > 1 || seed > 0xFFFF)
        return std::nullopt;
    RacePlaylist p;
    p.limit_ = limit;
    p.cursor_ = u16(cursor);
    p.shuffle_ = shuffle != 0;
    p.none_ = none != 0;
    p.seed_ = u16(seed);
    std::string tok;
    while (in >> tok) {
        unsigned long long id = 0;
        int listed = 0, played = 0, order = 0;
        if (p.songs_.size() >= limit ||
            std::sscanf(tok.c_str(), "%llx:%d:%d:%d", &id, &listed, &played, &order) != 4 || order < -32768 ||
            order > 32767)
            return std::nullopt;
        p.songs_.push_back(Song{u64(id), listed != 0, played != 0, s16(order)});
    }
    return p;
}

} // namespace gt3r::race
