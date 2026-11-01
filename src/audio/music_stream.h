#pragma once

#include "base/bytes.h"

namespace gt3r {

// A piece of music the audio engine streams: interleaved s16 frames at the piece's own rate
// and channel count (the output stream converts). The game's .ads streams and the player's own
// music files (music_file.h) both play through it. Read on the audio thread.
class MusicStream {
public:
    virtual ~MusicStream() = default;
    virtual u32 sample_rate() const = 0;
    virtual u32 channels() const = 0;
    virtual u64 total_frames() const = 0; // per channel; 0 when not known
    // Fills up to `frames` sample frames; returns the frames produced (0 = the end).
    virtual size_t read(s16* out, size_t frames) = 0;
    virtual double position_seconds() const = 0;
};

} // namespace gt3r
