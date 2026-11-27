#pragma once

// A 128-bit hash of texels (or any bytes) for telling contents apart in memory (never stored): 32 bytes a step in four
// independent lanes.

#include "base/bytes.h"

#include <array>
#include <cstring>

namespace gt3r {

inline std::array<u64, 2> content_hash(const void* data, size_t size) {
    auto rotl = [](u64 x, int r) { return (x << r) | (x >> (64 - r)); };
    auto mix = [&rotl](u64& lane, u64 word) { lane = rotl(lane + word * 0xC2B2AE3D27D4EB4Full, 31) * 0x9E3779B185EBCA87ull; };
    auto spread = [](u64 x) {
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    };
    u64 lane[4] = {0x60EA27EEADC0B5D6ull, 0xC2B2AE3D27D4EB4Full, 0x27D4EB2F165667C5ull, 0x61C8864E7A143579ull};
    const u8* p = static_cast<const u8*>(data);
    size_t i = 0;
    u64 word[4];
    for (; i + sizeof word <= size; i += sizeof word) {
        std::memcpy(word, p + i, sizeof word);
        for (int k = 0; k < 4; ++k) mix(lane[k], word[k]);
    }
    if (i < size) { // (the tail, padded with zeros; the size below keeps it apart from real zeros)
        std::memset(word, 0, sizeof word);
        std::memcpy(word, p + i, size - i);
        for (int k = 0; k < 4; ++k) mix(lane[k], word[k]);
    }
    return {spread(rotl(lane[0], 1) + rotl(lane[1], 7) + rotl(lane[2], 12) + rotl(lane[3], 18)),
            spread((lane[0] ^ rotl(lane[1], 23) ^ rotl(lane[2], 37) ^ rotl(lane[3], 51)) + size)};
}

inline std::array<u64, 2> texel_hash(const u32* texels, size_t n) { return content_hash(texels, n * sizeof(u32)); }
inline std::array<u64, 2> bytes_hash(ByteSpan b) { return content_hash(b.data(), b.size()); }

} // namespace gt3r
