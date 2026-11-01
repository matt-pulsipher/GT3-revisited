#pragma once

// The race music's markers (music §8): the race music object follows the playing song's
// ads.inf marker tracks (sound::MadsMarkers) every 60 Hz frame, before the cameras. For
// each track it keeps the event "a marker passed this frame" and the interval "seconds to
// the next marker", which the replay cameras turn into cut requests and shot lengths
// (presentation §G14, race::CutClock).
//
// Per frame, with now = the song's audible position + 1/30 s (two frames ahead), for every
// track with markers: the next marker t = marker[cursor] / rate; t < now: event, cursor + 1
// (one marker per track per frame at most); past the last marker the next one is the first
// plus the song's length (as if it looped); interval = t - now (slightly negative on the
// frame a marker fires). A new piece (or the same one restarted) fires every marked track
// once on its first frame, cursors back to 0 and t = the first marker. Without a marked
// song (jingles, licence runs, an empty playlist, a stop) every interval is "none" (FLT_MAX)
// and no event fires.
//
// Ours: a race song without markers of its own (the player's music, race/custom_music.h) gets
// fallback_markers(), so the replay cameras keep cutting at the pace of the disc's songs.

#include "formats/sound/mads.h"

#include <array>
#include <cfloat>
#include <optional>
#include <utility>

namespace gt3r::race {

// Markers for a race song that has none, at the disc's race songs' pace (their medians: a beat
// 0.46 s, a phrase of four beats 1.82 s, a cut point 1.46 s apart, a mean of 1.56 s): from the
// song's first beat after its start (its first frame fires every track already) a steady 130
// beats a minute, track 1 every beat, track 2 every four beats,
// tracks 3 and 4 the same cut points 1, 2, 4 or 8 beats apart (drawn 15 / 25 / 45 / 15 % on
// `seed`: a mean of 1.69 s). Up to `length` seconds (30 minutes when it is not known), sample
// positions at 1 kHz. Not timed to the music itself: the cuts keep a musical rhythm only.
sound::MadsMarkers fallback_markers(double length, u64 seed);

class MusicMarkers {
public:
    static constexpr int kTracks = sound::MadsMarkers::kTracks;
    static constexpr float kNone = FLT_MAX; // an interval without a next marker
    static constexpr float kLead = 1.0f / 30.0f;

    // A new piece starts (or the music stops): `song` = its markers, nullptr for a piece
    // without them. The next step() fires every marked track once.
    void start(const sound::MadsMarkers* song);
    // One 60 Hz frame at the song's audible position (seconds).
    void step(double position);

    float interval(int track) const { return track >= 0 && track < kTracks ? interval_[size_t(track)] : kNone; }
    bool event(int track) const { return track >= 0 && track < kTracks && event_[size_t(track)]; }
    bool marked() const { return song_.has_value(); }

private:
    static std::array<float, kTracks> filled(float v) {
        std::array<float, kTracks> a;
        a.fill(v);
        return a;
    }
    std::optional<sound::MadsMarkers> song_;
    std::array<size_t, kTracks> cursor_{};
    std::array<float, kTracks> interval_ = filled(kNone);
    std::array<bool, kTracks> event_{};
    bool restart_ = false;
};

} // namespace gt3r::race
