#pragma once

#include "base/bytes.h"

#include <memory>
#include <vector>

namespace gt3r {

// PS2 Graphics Synthesizer pixel storage formats used by texture data.
enum class Psm : u8 {
    CT32 = 0x00,
    CT24 = 0x01,
    CT16 = 0x02,
    CT16S = 0x0A,
    T8 = 0x13,
    T4 = 0x14,
    T8H = 0x1B,
    T4HL = 0x24,
    T4HH = 0x2C,
};

const char* psm_name(u32 psm);
// Bits per pixel for formats with a direct transfer layout; 0 if unsupported.
u32 psm_bits(u32 psm);

// Decoded GS TEX0 register.
struct Tex0 {
    u32 tbp = 0, tbw = 0, psm = 0, tw = 0, th = 0; // tw/th are log2
    bool tcc = false;
    u32 tfx = 0, cbp = 0, cpsm = 0, csm = 0, csa = 0, cld = 0;
    static Tex0 decode(u64 v);
    u32 width() const { return 1u << tw; }
    u32 height() const { return 1u << th; }
};

// Decoded GS CLAMP register.
struct Clamp {
    u32 wms = 0, wmt = 0, minu = 0, maxu = 0, minv = 0, maxv = 0;
    static Clamp decode(u64 v);
};

// A 4 MiB model of GS local memory, sufficient to replay texture uploads and
// read textures back the way the hardware would. PS2 games routinely upload
// data in one format and sample it in another (e.g. upload as 32-bit, read
// as 8-bit indexed), so textures can only be decoded faithfully by going
// through the real swizzle layout. The address functions mirror the GS
// block/column arrangement documented in the GS User's Manual.
class GsMemory {
public:
    static constexpr u32 kBytes = 4 * 1024 * 1024;
    GsMemory();

    // Equivalent of a host->local GIF IMAGE transfer of a w*h rect at (0,0)
    // with BITBLTBUF(dbp, dbw, dpsm). `src` is packed at the format's bpp.
    void upload(u32 dbp, u32 dbw, u32 dpsm, u32 w, u32 h, ByteSpan src);

    // Reads a texel's raw value (index for T8/T4, color for CT formats).
    u32 read_texel(u32 bp, u32 bw, u32 psm, u32 x, u32 y) const;
    // The raw values of a w*h rect at (x0, y0), row by row, as read_texel gives them (the format is
    // picked once for the rect, not per texel).
    void read_texels(u32 bp, u32 bw, u32 psm, u32 x0, u32 y0, u32 w, u32 h, u32* out) const;

    // Decodes a texture to RGBA8 (alpha rescaled from GS 0..128 to 0..255).
    std::vector<u32> decode_rgba(const Tex0& tex0, u32 width, u32 height, u32 x0 = 0, u32 y0 = 0) const;

private:
    static constexpr u32 kBlocks = kBytes / 256;
    u32 clut_color(const Tex0& tex0, u32 index) const;
    std::unique_ptr<u8[]> mem_;     // not cleared up front: see written_
    std::unique_ptr<u8[]> written_; // per 256-byte block: 0 = never written, reads as zeros
};

} // namespace gt3r
