#pragma once

// Little-endian helpers for parsing PS2 data. All on-disc data is little
// endian, matching the PS2.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace gt3r {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

using Bytes = std::vector<u8>;
using ByteSpan = std::span<const u8>;

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

template <typename T>
inline T read_le(ByteSpan data, size_t offset) {
    if (offset > data.size() || data.size() - offset < sizeof(T))
        throw FormatError("read past end of buffer");
    T v;
    std::memcpy(&v, data.data() + offset, sizeof(T));
    return v; // host is little endian on every supported platform
}

inline u16 rd16(ByteSpan d, size_t o) { return read_le<u16>(d, o); }
inline u32 rd32(ByteSpan d, size_t o) { return read_le<u32>(d, o); }
inline u64 rd64(ByteSpan d, size_t o) { return read_le<u64>(d, o); }

inline bool has_magic(ByteSpan d, const char* magic) {
    size_t n = std::strlen(magic);
    return d.size() >= n && std::memcmp(d.data(), magic, n) == 0;
}

// The race records' data hash (race/exact_setup.h's fingerprint, sim::exact::RaceCourse::file_hash): FNV-1a's 64-bit step
// from an offset basis of 1469598103934665603 (the standard basis with its last digit missing). Saved replays keep its
// values: never change it.
inline u64 record_hash(ByteSpan b) {
    u64 h = 1469598103934665603ull;
    for (u8 c : b) h = (h ^ c) * 1099511628211ull;
    return h;
}

} // namespace gt3r
