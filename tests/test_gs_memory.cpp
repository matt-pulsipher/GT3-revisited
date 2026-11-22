// GsMemory read back against hashes: seeded random uploads in every upload format (base pointers
// wrapping round the 16384 blocks, widths 1..200, 4-bit uploads of odd counts), then every read
// format read texel by texel, by rect, and decoded through every CLUT format, each stream hashed.
// The hashes are those of the implementation that cleared all 4 MiB up front and read one texel per
// call (its rects read texel by texel), so the two read the same bit for bit.

#include "test_framework.h"

#include "formats/gs_memory.h"

#include <random>

using namespace gt3r;

namespace {

constexpr u32 kUploadPsms[] = {0x00, 0x01, 0x02, 0x0A, 0x13, 0x14};
constexpr u32 kReadPsms[] = {0x00, 0x01, 0x02, 0x0A, 0x13, 0x14, 0x1B, 0x24, 0x2C, 0x30};
constexpr u32 kClutPsms[] = {0x00, 0x01, 0x02, 0x0A};

// FNV-1a over the bytes of 32-bit values, low byte first.
struct Fnv {
    u64 h = 14695981039346656037ull;
    void add(u32 v) { for (int i = 0; i < 4; ++i) h = (h ^ ((v >> (i * 8)) & 0xFF)) * 1099511628211ull; }
};

} // namespace

TEST(gs_matches_reference) {
    std::mt19937 rng(20261115);
    // Not std::uniform_int_distribution: its numbers differ between standard libraries.
    auto pick = [&](u32 lo, u32 hi) { return lo + u32(rng() % (u64(hi) - lo + 1)); }; // [lo, hi]
    Fnv texels, rects, decoded;
    for (int round = 0; round < 24; ++round) {
        GsMemory gs;
        const int uploads = int(pick(0, 10));
        for (int k = 0; k < uploads; ++k) {
            const u32 psm = kUploadPsms[pick(0, 5)];
            // Base pointers near the top wrap round the 16384 blocks, as on the GS.
            const u32 dbp = pick(0, 3) == 0 ? pick(16000, 16383) : pick(0, 16383);
            const u32 dbw = pick(1, 16);
            const u32 w = pick(0, 4) == 0 ? pick(1, 9) : pick(1, 200), h = pick(1, 160);
            Bytes src((size_t(w) * h * psm_bits(psm) + 7) / 8 + 1); // + 1: a 4-bit upload of an odd count reads one past
            for (u8& b : src) b = u8(pick(0, 255));
            gs.upload(dbp, dbw, psm, w, h, src);
        }
        for (int k = 0; k < 4000; ++k) {
            const u32 psm = kReadPsms[pick(0, 9)], bp = pick(0, 16383), bw = pick(0, 16), x = pick(0, 2047), y = pick(0, 2047);
            texels.add(gs.read_texel(bp, bw, psm, x, y));
        }
        for (int k = 0; k < 40; ++k) { // the rect reader
            const u32 psm = kReadPsms[pick(0, 9)], bp = pick(0, 16383), bw = pick(0, 16);
            const u32 x0 = pick(0, 500), y0 = pick(0, 500), w = pick(1, 70), h = pick(1, 70);
            std::vector<u32> got(size_t(w) * h);
            gs.read_texels(bp, bw, psm, x0, y0, w, h, got.data());
            for (u32 v : got) rects.add(v);
        }
        for (int k = 0; k < 40; ++k) { // every CLUT format, both CSA halves, offsets into the texture
            Tex0 t;
            t.psm = kReadPsms[pick(0, 9)];
            t.tbp = pick(0, 16383), t.tbw = pick(0, 16), t.tw = pick(0, 8), t.th = pick(0, 8);
            t.tcc = pick(0, 1) == 1;
            t.cbp = pick(0, 16383), t.cpsm = kClutPsms[pick(0, 3)], t.csa = pick(0, 31);
            const u32 width = pick(1, 96), height = pick(1, 96), x0 = pick(0, 1) ? pick(0, 300) : 0, y0 = pick(0, 1) ? pick(0, 300) : 0;
            for (u32 c : gs.decode_rgba(t, width, height, x0, y0)) decoded.add(c);
        }
    }
    CHECK(texels.h == 0x7E4F9BFC86FFB458ull);
    CHECK(rects.h == 0x058927C1C77D54A6ull);
    CHECK(decoded.h == 0x00C188949AB62AFBull);
}

TEST(gs_starts_as_zeros) {
    // Nothing written: every format reads 0 everywhere (the memory is cleared lazily, per block). One
    // filled and freed first, so memory handed back to the next one would show.
    {
        GsMemory full;
        const Bytes ones(size_t(GsMemory::kBytes), 0xFF);
        full.upload(0, 16, 0x00, 1024, 1024, ones);
    }
    GsMemory gs;
    bool zero = true;
    for (u32 psm : kReadPsms)
        for (u32 i = 0; i < 2000 && zero; ++i) zero = gs.read_texel(i * 7, 4, psm, i * 13 % 1024, i * 29 % 1024) == 0;
    // A byte written, its block's other bytes still read 0 (the block is cleared before the write).
    const u8 one = 0xAB;
    gs.upload(100, 1, 0x13, 1, 1, ByteSpan(&one, 1));
    CHECK(gs.read_texel(100, 1, 0x13, 0, 0) == 0xAB);
    for (u32 x = 1; x < 16 && zero; ++x) zero = gs.read_texel(100, 1, 0x13, x, 0) == 0;
    CHECK(zero);
}
