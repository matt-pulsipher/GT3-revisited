#include "race/music_markers.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace gt3r::race {

sound::MadsMarkers fallback_markers(double length, u64 seed) {
    constexpr u32 kRate = 1000;
    constexpr double kBeat = 60.0 / 130.0;
    sound::MadsMarkers m;
    m.rate = kRate;
    if (!(length > 0)) length = 30 * 60;
    m.length = u32(std::min(length, 6.0 * 3600) * kRate);
    const int beats = int(length / kBeat);
    std::mt19937_64 rng(seed);
    auto spacing = [&rng] {
        const u64 roll = rng() % 100;
        return roll < 15 ? 1 : roll < 40 ? 2 : roll < 85 ? 4 : 8;
    };
    // From the first beat after the start: the song's first frame already fires every track.
    int next_cut = spacing();
    for (int b = 1; b < beats; ++b) {
        const u32 at = u32(std::lround(b * kBeat * kRate));
        m.tracks[1].push_back(at);
        if (b % 4 == 0) m.tracks[2].push_back(at);
        if (b == next_cut) {
            m.tracks[3].push_back(at);
            next_cut += spacing();
        }
    }
    m.tracks[4] = m.tracks[3];
    return m;
}

void MusicMarkers::start(const sound::MadsMarkers* song) {
    song_.reset();
    if (song && song->any() && song->rate > 0) song_ = *song;
    cursor_.fill(0);
    interval_.fill(kNone);
    event_.fill(false);
    restart_ = true;
}

void MusicMarkers::step(double position) {
    const float now = float(position) + kLead;
    const bool restart = std::exchange(restart_, false);
    for (int i = 0; i < kTracks; ++i) {
        interval_[size_t(i)] = kNone;
        event_[size_t(i)] = false;
        if (!song_) continue;
        const std::vector<u32>& m = song_->tracks[size_t(i)];
        if (m.empty()) continue;
        const float rate = float(song_->rate);
        size_t& c = cursor_[size_t(i)];
        float t = -FLT_MAX;
        if (c < m.size()) {
            t = float(m[c]) / rate;
            if (t < now) { // passed this frame
                event_[size_t(i)] = true;
                if (++c == m.size()) t = -FLT_MAX;
            }
        }
        if (t < 0) t = float(m[0]) / rate + float(song_->length) / rate; // after the last: as if looped
        if (restart) {
            c = 0;
            event_[size_t(i)] = true;
            t = float(m[0]) / rate;
        }
        interval_[size_t(i)] = t - now;
    }
}

} // namespace gt3r::race
