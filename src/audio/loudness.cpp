#include "audio/loudness.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <vector>

namespace gt3r {

namespace {

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

// BS.1770's K-weighting at `rate` Hz: its two stages from the analog prototypes the standard's
// 48 kHz coefficients come from (a high shelf around 1.7 kHz, +4 dB; a high-pass at 38 Hz).
void k_weighting(double rate, Biquad& shelf, Biquad& high_pass) {
    {
        const double f0 = 1681.974450955533, gain_db = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan(std::numbers::pi * f0 / rate);
        const double vh = std::pow(10.0, gain_db / 20.0), vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        shelf = {(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
    }
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan(std::numbers::pi * f0 / rate);
        const double a0 = 1.0 + k / q + k * k;
        high_pass = {1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
    }
}

// One channel's two stages (direct form II, transposed).
struct Channel {
    double s1[2] = {}, s2[2] = {};
    double run(const Biquad& f, int stage, double x) {
        const double y = f.b0 * x + s1[stage];
        s1[stage] = f.b1 * x - f.a1 * y + s2[stage];
        s2[stage] = f.b2 * x - f.a2 * y;
        return y;
    }
};

double lufs_of(double energy) { return -0.691 + 10.0 * std::log10(energy); }

} // namespace

std::optional<Loudness> measure_loudness(MusicStream& stream, const std::atomic<bool>* stop) {
    const u32 rate = stream.sample_rate(), channels = stream.channels();
    if (rate == 0 || channels == 0) return std::nullopt;
    Biquad shelf, high_pass;
    k_weighting(double(rate), shelf, high_pass);
    std::vector<Channel> state(channels);
    // Mono plays on both speakers: its energy counts twice.
    const double weight = channels == 1 ? 2.0 : 1.0;

    // Weighted energy per 100 ms quarter-block; a 400 ms block is four in a row.
    const size_t quarter = std::max<size_t>(1, rate / 10);
    std::vector<double> quarters;
    double sum = 0;
    size_t in_quarter = 0;
    int peak = 0;
    const u64 max_frames = u64(kMaxLoudnessSeconds * rate);
    u64 frames_read = 0;
    std::vector<s16> buf(size_t(4096) * channels);
    while (frames_read < max_frames) {
        if (stop && stop->load(std::memory_order_relaxed)) return std::nullopt;
        const size_t want = size_t(std::min<u64>(4096, max_frames - frames_read));
        const size_t got = stream.read(buf.data(), want);
        if (got == 0) break;
        frames_read += got;
        for (size_t f = 0; f < got; ++f) {
            for (u32 c = 0; c < channels; ++c) {
                const int v = buf[f * channels + c];
                peak = std::max(peak, std::abs(v));
                double y = state[c].run(shelf, 0, v / 32768.0);
                y = state[c].run(high_pass, 1, y);
                sum += weight * y * y;
            }
            if (++in_quarter == quarter) {
                quarters.push_back(sum / double(quarter));
                sum = 0;
                in_quarter = 0;
            }
        }
    }
    if (quarters.size() < 4) return std::nullopt; // under 400 ms

    // The blocks (400 ms, 75 % overlap) over the absolute gate, then over the relative one.
    std::vector<double> blocks;
    blocks.reserve(quarters.size() - 3);
    for (size_t i = 0; i + 4 <= quarters.size(); ++i) {
        const double e = (quarters[i] + quarters[i + 1] + quarters[i + 2] + quarters[i + 3]) / 4.0;
        if (e > 0 && lufs_of(e) > -70.0) blocks.push_back(e);
    }
    if (blocks.empty()) return std::nullopt; // silence
    double total = 0;
    for (double e : blocks) total += e;
    const double gate = lufs_of(total / double(blocks.size())) - 10.0;
    double kept = 0;
    size_t n = 0;
    for (double e : blocks)
        if (lufs_of(e) > gate) kept += e, ++n;
    if (n == 0) return std::nullopt;
    return Loudness{lufs_of(kept / double(n)), float(peak) / 32768.0f};
}

} // namespace gt3r
