// The songs' loudness (audio/loudness.h): BS.1770's reference values, the gates, mono.

#include "test_framework.h"

#include "audio/loudness.h"

#include <cmath>
#include <numbers>

using namespace gt3r;

namespace {

// A sine (or silence) on every channel, then optionally silence after it.
class Tone : public MusicStream {
public:
    Tone(u32 rate, u32 channels, double hz, double amplitude, double seconds, double silent_seconds = 0)
        : rate_(rate), channels_(channels), hz_(hz), amp_(amplitude), loud_(u64(seconds * rate)),
          total_(loud_ + u64(silent_seconds * rate)) {}
    u32 sample_rate() const override { return rate_; }
    u32 channels() const override { return channels_; }
    u64 total_frames() const override { return total_; }
    double position_seconds() const override { return double(at_) / rate_; }
    size_t read(s16* out, size_t frames) override {
        size_t n = 0;
        for (; n < frames && at_ < total_; ++n, ++at_) {
            const double v = at_ < loud_ ? amp_ * std::sin(2 * std::numbers::pi * hz_ * double(at_) / rate_) : 0.0;
            for (u32 c = 0; c < channels_; ++c) out[n * channels_ + c] = s16(std::lround(v * 32767.0));
        }
        return n;
    }

private:
    u32 rate_, channels_;
    double hz_, amp_;
    u64 loud_, total_, at_ = 0;
};

} // namespace

// BS.1770's check: a 997 Hz sine at -20 dBFS on both channels reads -20 LUFS (the K-weighting
// is 0 dB there), at 44.1 and 48 kHz; mono reads the same (heard on both speakers).
TEST(loudness_reference_tone) {
    for (u32 rate : {44100u, 48000u}) {
        Tone t(rate, 2, 997.0, 0.1, 10.0);
        std::optional<Loudness> l = measure_loudness(t);
        CHECK(l && std::fabs(l->lufs - -20.0) < 0.1);
        CHECK(l && std::fabs(l->peak - 0.1f) < 0.001f);
    }
    Tone mono(44100, 1, 997.0, 0.1, 10.0);
    std::optional<Loudness> m = measure_loudness(mono);
    CHECK(m && std::fabs(m->lufs - -20.0) < 0.1);
    // 6 dB down reads 6 LU less.
    Tone quiet(48000, 2, 997.0, 0.05, 10.0);
    std::optional<Loudness> q = measure_loudness(quiet);
    CHECK(q && std::fabs(q->lufs - -26.02) < 0.1);
}

// Silence after the music doesn't make it quieter (the absolute gate); all silence, or under
// one block, has no loudness; low frequencies count less (K-weighting's high-pass).
TEST(loudness_gates_and_weighting) {
    Tone half(48000, 2, 997.0, 0.1, 10.0, 10.0);
    std::optional<Loudness> l = measure_loudness(half);
    CHECK(l && std::fabs(l->lufs - -20.0) < 0.1);
    Tone silent(48000, 2, 997.0, 0.0, 5.0);
    CHECK(!measure_loudness(silent));
    Tone short_one(48000, 2, 997.0, 0.1, 0.3);
    CHECK(!measure_loudness(short_one));
    Tone low(48000, 2, 25.0, 0.1, 10.0);
    std::optional<Loudness> bass = measure_loudness(low);
    CHECK(bass && bass->lufs < -25.0);
    std::atomic<bool> stop{true};
    Tone stopped(48000, 2, 997.0, 0.1, 10.0);
    CHECK(!measure_loudness(stopped, &stop));
}
