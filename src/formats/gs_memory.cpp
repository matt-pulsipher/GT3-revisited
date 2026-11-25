#include "formats/gs_memory.h"

#include <algorithm>
#include <cstring>

namespace gt3r {

namespace {

// Block arrangement within a page.
constexpr u8 kBlock32[4][8] = {
    {0, 1, 4, 5, 16, 17, 20, 21},
    {2, 3, 6, 7, 18, 19, 22, 23},
    {8, 9, 12, 13, 24, 25, 28, 29},
    {10, 11, 14, 15, 26, 27, 30, 31},
};
constexpr u8 kBlock16[8][4] = {
    {0, 2, 8, 10},   {1, 3, 9, 11},   {4, 6, 12, 14},  {5, 7, 13, 15},
    {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31},
};
// T8 pages use the 32-bit block arrangement, T4 pages the 16-bit one.

// Word order of one 8x2 column pair inside a 32-bit block.
constexpr u8 kColumnWords[2][8] = {
    {0, 1, 4, 5, 8, 9, 12, 13},
    {2, 3, 6, 7, 10, 11, 14, 15},
};

// Pixel offset tables within a block, generated from the column layout:
// a block is 4 columns; each column is 16 words arranged as kColumnWords.
// 8/4-bit formats interleave rows inside each column and swap the two
// 4-word halves on alternating row pairs (odd columns invert the swap).
// Built at compile time.
struct Tables {
    u16 col32[8][8] = {};  // word index within block
    u16 col16[8][16] = {}; // halfword index within block
    u16 col8[16][16] = {}; // byte index within block
    u16 col4[16][32] = {}; // nibble index within block

    constexpr Tables() {
        for (u32 y = 0; y < 8; ++y)
            for (u32 x = 0; x < 8; ++x) col32[y][x] = u16((y >> 1) * 16 + kColumnWords[y & 1][x]);
        for (u32 y = 0; y < 8; ++y)
            for (u32 x = 0; x < 16; ++x)
                col16[y][x] = u16(((y >> 1) * 16 + kColumnWords[y & 1][x & 7]) * 2 + ((x >> 3) & 1));
        for (u32 y = 0; y < 16; ++y) {
            u32 col = y >> 2, r = y & 3;
            u32 swap = ((r >> 1) ^ (col & 1)) ? 4 : 0;
            for (u32 x = 0; x < 16; ++x) {
                u32 word = col * 16 + kColumnWords[r & 1][(x & 7) ^ swap];
                col8[y][x] = u16(word * 4 + ((x >> 3) & 1) * 2 + (r >> 1));
            }
            for (u32 x = 0; x < 32; ++x) {
                u32 word = col * 16 + kColumnWords[r & 1][(x & 7) ^ swap];
                col4[y][x] = u16(word * 8 + ((x >> 3) & 3) * 2 + (r >> 1));
            }
        }
    }
};

constexpr Tables kTables{};

constexpr u32 kBlockMask = 0x3FFF; // 16384 blocks of 256 bytes

// Address functions return an offset in units of the format's pixel size; `>> 6` (32-bit), `>> 7`
// (16-bit), `>> 8` (8-bit) and `>> 9` (4-bit) of it is the block.
u32 addr32(u32 bp, u32 bw, u32 x, u32 y) {
    u32 block = (bp + (y & ~31u) * bw + ((x & ~63u) >> 1) + kBlock32[(y >> 3) & 3][(x >> 3) & 7]) & kBlockMask;
    return (block << 6) + kTables.col32[y & 7][x & 7];
}
u32 addr16(u32 bp, u32 bw, u32 x, u32 y) {
    u32 block = (bp + (((y & ~63u) * bw) >> 1) + ((x & ~63u) >> 1) + kBlock16[(y >> 3) & 7][(x >> 4) & 3]) & kBlockMask;
    return (block << 7) + kTables.col16[y & 7][x & 15];
}
u32 addr8(u32 bp, u32 bw, u32 x, u32 y) {
    u32 block = (bp + (((y & ~63u) * bw) >> 2) + ((x & ~127u) >> 2) + kBlock32[(y >> 4) & 3][(x >> 4) & 7]) & kBlockMask;
    return (block << 8) + kTables.col8[y & 15][x & 15];
}
u32 addr4(u32 bp, u32 bw, u32 x, u32 y) {
    u32 block = (bp + (((y & ~127u) * bw) >> 3) + ((x & ~127u) >> 2) + kBlock16[(y >> 4) & 7][(x >> 5) & 3]) & kBlockMask;
    return (block << 9) + kTables.col4[y & 15][x & 31];
}

u32 expand_alpha(u32 a) { return std::min<u32>(a * 2, 255); }

// GS 32-bit color (A8B8G8R8, alpha 0..128) -> RGBA8 in memory order.
u32 ct32_to_rgba(u32 c) { return (c & 0x00FFFFFF) | (expand_alpha(c >> 24) << 24); }
// The GS widens each 5-bit channel by a shift alone: the low 3 bits are 0 (31 -> 248).
u32 ct16_to_rgba(u32 c) {
    u32 r = (c & 0x1F) << 3, g = ((c >> 5) & 0x1F) << 3, b = ((c >> 10) & 0x1F) << 3;
    // Alpha really comes from TEXA.TA0/TA1; the game's TEXA setup is not yet
    // known, so treat the A bit as a straight on/off mask.
    u32 a = (c & 0x8000) ? 0xFF : 0x00;
    return r | (g << 8) | (b << 16) | (a << 24);
}

// Every texel of a w*h rect through `fn(x, y, i)`, i = the texel's index in row order. The callers pick
// the format once and pass a loop body per format.
template <class Fn> void each_texel(u32 w, u32 h, Fn fn) {
    size_t i = 0;
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x, ++i) fn(x, y, i);
}

} // namespace

const char* psm_name(u32 psm) {
    switch (psm) {
    case 0x00: return "CT32";
    case 0x01: return "CT24";
    case 0x02: return "CT16";
    case 0x0A: return "CT16S";
    case 0x13: return "T8";
    case 0x14: return "T4";
    case 0x1B: return "T8H";
    case 0x24: return "T4HL";
    case 0x2C: return "T4HH";
    default: return "?";
    }
}

u32 psm_bits(u32 psm) {
    switch (psm) {
    case 0x00: return 32;
    case 0x01: return 24;
    case 0x02:
    case 0x0A: return 16;
    case 0x13: return 8;
    case 0x14: return 4;
    default: return 0;
    }
}

Tex0 Tex0::decode(u64 v) {
    Tex0 t;
    t.tbp = u32(v & 0x3FFF);
    t.tbw = u32((v >> 14) & 0x3F);
    t.psm = u32((v >> 20) & 0x3F);
    t.tw = u32((v >> 26) & 0xF);
    t.th = u32((v >> 30) & 0xF);
    t.tcc = (v >> 34) & 1;
    t.tfx = u32((v >> 35) & 3);
    t.cbp = u32((v >> 37) & 0x3FFF);
    t.cpsm = u32((v >> 51) & 0xF);
    t.csm = u32((v >> 55) & 1);
    t.csa = u32((v >> 56) & 0x1F);
    t.cld = u32((v >> 61) & 7);
    return t;
}

Clamp Clamp::decode(u64 v) {
    Clamp c;
    c.wms = u32(v & 3);
    c.wmt = u32((v >> 2) & 3);
    c.minu = u32((v >> 4) & 0x3FF);
    c.maxu = u32((v >> 14) & 0x3FF);
    c.minv = u32((v >> 24) & 0x3FF);
    c.maxv = u32((v >> 34) & 0x3FF);
    return c;
}

// The memory starts as zeros. Most texture sets write a few of its 16384 blocks, and a GsMemory is made
// for each one, so instead of clearing 4 MiB each time a block is
// cleared the first time a transfer writes to it, and a block never written reads as zeros.
GsMemory::GsMemory() : mem_(new u8[kBytes]), written_(new u8[kBlocks]()) {}

void GsMemory::upload(u32 dbp, u32 dbw, u32 dpsm, u32 w, u32 h, ByteSpan src) {
    u32 bits = psm_bits(dpsm);
    if (bits == 0) throw FormatError("unsupported upload PSM");
    if (u64(w) * h * bits / 8 > src.size()) throw FormatError("texture upload larger than source data");
    u8* m = mem_.get();
    u8* written = written_.get();
    const u8* s = src.data();
    auto touch = [m, written](u32 block) {
        if (!written[block]) {
            std::memset(m + size_t(block) * 256, 0, 256);
            written[block] = 1;
        }
    };
    switch (dpsm) {
    case 0x00:
        each_texel(w, h, [&](u32 x, u32 y, size_t i) {
            u32 a = addr32(dbp, dbw, x, y);
            touch(a >> 6);
            std::memcpy(m + size_t(a) * 4, s + i * 4, 4);
        });
        break;
    case 0x01:
        each_texel(w, h, [&](u32 x, u32 y, size_t i) {
            u32 a = addr32(dbp, dbw, x, y);
            touch(a >> 6);
            std::memcpy(m + size_t(a) * 4, s + i * 3, 3);
        });
        break;
    case 0x02:
    case 0x0A:
        each_texel(w, h, [&](u32 x, u32 y, size_t i) {
            u32 a = addr16(dbp, dbw, x, y);
            touch(a >> 7);
            std::memcpy(m + size_t(a) * 2, s + i * 2, 2);
        });
        break;
    case 0x13:
        each_texel(w, h, [&](u32 x, u32 y, size_t i) {
            u32 a = addr8(dbp, dbw, x, y);
            touch(a >> 8);
            m[a] = s[i];
        });
        break;
    case 0x14:
        each_texel(w, h, [&](u32 x, u32 y, size_t i) {
            u32 nib = (s[i >> 1] >> ((i & 1) * 4)) & 0xF;
            u32 a = addr4(dbp, dbw, x, y);
            touch(a >> 9);
            u8& b = m[a >> 1];
            b = (a & 1) ? u8((b & 0x0F) | (nib << 4)) : u8((b & 0xF0) | nib);
        });
        break;
    }
}

u32 GsMemory::read_texel(u32 bp, u32 bw, u32 psm, u32 x, u32 y) const {
    u32 v;
    read_texels(bp, bw, psm, x, y, 1, 1, &v);
    return v;
}

void GsMemory::read_texels(u32 bp, u32 bw, u32 psm, u32 x0, u32 y0, u32 w, u32 h, u32* out) const {
    const u8* m = mem_.get();
    const u8* written = written_.get(); // a block never written reads as zeros
    auto w32 = [&](u32 a) { u32 v = 0; if (written[a >> 6]) std::memcpy(&v, m + size_t(a) * 4, 4); return v; };
    auto w16 = [&](u32 a) { u16 v = 0; if (written[a >> 7]) std::memcpy(&v, m + size_t(a) * 2, 2); return u32(v); };
    auto w8 = [&](u32 a) { return written[a >> 8] ? u32(m[a]) : 0u; };
    auto w4 = [&](u32 a) { return written[a >> 9] ? u32(m[a >> 1] >> ((a & 1) * 4)) & 0xF : 0u; };
    auto fill = [&](auto texel) { each_texel(w, h, [&](u32 x, u32 y, size_t i) { out[i] = texel(x0 + x, y0 + y); }); };
    switch (psm) {
    case 0x00:
    case 0x01: fill([&](u32 x, u32 y) { return w32(addr32(bp, bw, x, y)); }); break;
    case 0x02:
    case 0x0A: fill([&](u32 x, u32 y) { return w16(addr16(bp, bw, x, y)); }); break;
    case 0x13: fill([&](u32 x, u32 y) { return w8(addr8(bp, bw, x, y)); }); break;
    case 0x14: fill([&](u32 x, u32 y) { return w4(addr4(bp, bw, x, y)); }); break;
    case 0x1B: fill([&](u32 x, u32 y) { return w32(addr32(bp, bw, x, y)) >> 24; }); break;
    case 0x24: fill([&](u32 x, u32 y) { return (w32(addr32(bp, bw, x, y)) >> 24) & 0xF; }); break;
    case 0x2C: fill([&](u32 x, u32 y) { return w32(addr32(bp, bw, x, y)) >> 28; }); break;
    default: std::fill(out, out + size_t(w) * h, 0u); break;
    }
}

u32 GsMemory::clut_color(const Tex0& t, u32 index) const {
    // CSM1 layout: the palette is read from a 16x16 rect at CBP (buffer width
    // 1), with entries 8..15 / 16..23 of each 32-entry group swapped into the
    // second row. 4-bit textures select one 16-entry palette within it via
    // CSA (paint colours on car models rely on this); a plain 8x2 4-bit
    // palette is the CSA = 0 case of the same layout.
    bool is4 = t.psm == 0x14 || t.psm == 0x24 || t.psm == 0x2C;
    u32 e = is4 ? (t.csa & 0x0F) * 16 + (index & 0x0F) : index;
    u32 x = (e & 7) | ((e & 0x10) >> 1);
    u32 y = ((e >> 5) << 1) | ((e >> 3) & 1);
    if (t.cpsm == 0x00 || t.cpsm == 0x01) {
        u32 c = read_texel(t.cbp, 1, 0x00, x, y);
        return ct32_to_rgba(c);
    }
    return ct16_to_rgba(read_texel(t.cbp, 1, 0x02, x, y));
}

std::vector<u32> GsMemory::decode_rgba(const Tex0& t, u32 width, u32 height, u32 x0, u32 y0) const {
    std::vector<u32> out(size_t(width) * height);
    read_texels(t.tbp, t.tbw, t.psm, x0, y0, width, height, out.data()); // the raw values, then their colors in place
    const u32 opaque = t.tcc ? 0 : 0xFF000000u;
    bool indexed = t.psm == 0x13 || t.psm == 0x14 || t.psm == 0x1B || t.psm == 0x24 || t.psm == 0x2C;
    if (indexed) {
        u32 palette[256];
        u32 n = (t.psm == 0x13 || t.psm == 0x1B) ? 256 : 16;
        for (u32 i = 0; i < n; ++i) palette[i] = clut_color(t, i);
        for (u32& v : out) v = palette[v & 0xFF] | opaque;
    } else if (t.psm == 0x02 || t.psm == 0x0A) {
        for (u32& v : out) v = ct16_to_rgba(v) | opaque;
    } else { // CT32, CT24 (alpha 0x80), and formats with no layout here (read as zeros)
        const u32 a = t.psm == 0x01 ? 0x80000000u : 0;
        for (u32& v : out) v = ct32_to_rgba(v | a) | opaque;
    }
    return out;
}

} // namespace gt3r
