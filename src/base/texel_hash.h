#pragma once

// A 128-bit hash of texels (or any bytes) for telling contents apart in memory (never stored): FNV-1a and a
// multiply-rotate mix, a 32-bit word at a time.

#include "base/bytes.h"

#include <array>
#include <cstring>

namespace gt3r {

struct ContentHash {
    std::array<u64, 2> lanes{14695981039346656037ull, 0x9E3779B97F4A7C15ull};
    void add(u32 word) {
        lanes[0] = (lanes[0] ^ word) * 1099511628211ull;
        u64 x = lanes[1] + word * 0xBF58476D1CE4E5B9ull;
        lanes[1] = ((x << 31) | (x >> 33)) * 0x94D049BB133111EBull;
    }
};

inline std::array<u64, 2> texel_hash(const u32* texels, size_t n) {
    ContentHash h;
    for (size_t i = 0; i < n; ++i) h.add(texels[i]);
    return h.lanes;
}

inline std::array<u64, 2> bytes_hash(ByteSpan b) {
    ContentHash h;
    size_t i = 0;
    for (u32 word; i + sizeof word <= b.size(); i += sizeof word) {
        std::memcpy(&word, b.data() + i, sizeof word);
        h.add(word);
    }
    if (i < b.size()) { // (the tail, padded with zeros; the size below keeps it apart from real zeros)
        u32 word = 0;
        std::memcpy(&word, b.data() + i, b.size() - i);
        h.add(word);
    }
    h.add(u32(b.size()));
    return h.lanes;
}

} // namespace gt3r
