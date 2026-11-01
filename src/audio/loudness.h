#pragma once

// Ours: how loud a song is, so the player's own songs play as loud as the disc's (race/
// custom_music.h). Integrated loudness after ITU-R BS.1770-4, the measure ReplayGain 2 uses:
// the samples K-weighted (a high shelf and a high-pass, from the standard's formulas at any
// sample rate), mean squares of 400 ms blocks every 100 ms, blocks under -70 LUFS left out,
// then blocks more than 10 LU under the rest's loudness left out, and the loudness of what is
// left. A mono song counts as heard on both speakers (as it plays); channels past the second
// count as the front ones do.

#include "audio/music_stream.h"

#include <atomic>
#include <optional>

namespace gt3r {

struct Loudness {
    double lufs = 0;  // integrated loudness
    float peak = 0;   // the largest sample, 1 = full scale
};

// Reads `stream` from where it is to its end (at most kMaxLoudnessSeconds) and measures it;
// nullopt for silence, a stream with nothing in it, or when `stop` turns true.
inline constexpr double kMaxLoudnessSeconds = 20 * 60; // a long mix: its first 20 minutes
std::optional<Loudness> measure_loudness(MusicStream& stream, const std::atomic<bool>* stop = nullptr);

} // namespace gt3r
