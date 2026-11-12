#pragma once

#include "formats/gs_memory.h"

#include <string>
#include <array>
#include <vector>

namespace gt3r {

// "Tex1" texture set (.img files, and embedded in other containers).
//
//   0x00 "Tex1"
//   0x0C u32 file size
//   0x14 u16 texture count, u16 buffer count
//   0x18 u32 texture table offset  -> 0x28-byte records of GS registers:
//          +0x00 TEX0, +0x08 TEX1, +0x10/+0x18 (mip registers?), +0x20 CLAMP
//   0x1C u32 buffer table offset   -> 12-byte upload records:
//          u32 data offset, u16 dbp, u8 dbw, u8 dpsm, u16 width, u16 height
//   0x20 u32 paint table offset (cars; 0 = none, see gtm1::paint_variants)
//   0x24 u32 CLUT animation offset (0 = none, see ClutAnimation)
//
// Buffers are uploaded into GS memory in their own format; textures are then
// sampled through TEX0, often in a different format (see GsMemory).
struct Tex1 {
    // CLUT animation (header +0x24 -> u32 count (low 16 bits), u32 entry table offset; offsets
    // from the Tex1 start). An entry (0x20 bytes) rewrites `words` CT32 colours of the set's
    // upload data at `target` (inside a CLUT buffer) from a list of timed palettes, cross-fading
    // from each frame to the next over the frame's time (clut_animation.h plays them):
    //   +0x00 u16 group, +0x02 u16 mode, +0x04 u16 words, +0x06 u16 frame count,
    //   +0x08 u32 target, +0x0C u32 frame table, +0x10..+0x1F run-time state (0 on disc).
    // Frame (8 bytes): f32 seconds (negative: the animation holds there until released),
    // u32 offset of its `words` colours.
    struct ClutAnimation {
        struct Frame {
            float seconds = 0;
            u32 source = 0;
        };
        u16 group = 0;          // 0: runs from the set's load; 1: the course start signal
        u16 mode = 0;           // 0 loop, 1 ping-pong, 2 once (stays on the last frame)
        u32 words = 0;          // colours per frame
        u32 target = 0;         // offset of the colours it rewrites
        std::vector<Frame> frames;
        std::vector<u32> colours; // frames x words, the frames' colours as stored
    };

    struct Texture {
        Tex0 tex0;
        Clamp clamp;
        u64 tex1 = 0;
    };
    struct Buffer {
        u32 offset = 0;
        u32 dbp = 0, dbw = 0, dpsm = 0, width = 0, height = 0;
    };
    struct Image {
        u32 width = 0, height = 0;
        std::vector<u32> rgba; // RGBA8, row-major
    };

    std::vector<Texture> textures;
    std::vector<Buffer> buffers;
    std::vector<ClutAnimation> clut_animations;

    // A part of a texture, in its texels.
    struct Region {
        u32 x = 0, y = 0, width = 0, height = 0;
    };

    static bool is_tex1(ByteSpan data);
    // `data` must begin with the "Tex1" header.
    static Tex1 parse(ByteSpan data);

    // Uploads all buffers into a fresh GS memory model.
    void upload(ByteSpan data, GsMemory& gs) const;
    // What decode() keeps of texture `i`: the CLAMP region on an axis that uses REGION_CLAMP,
    // otherwise the full power-of-two size.
    Region region(size_t i) const;
    // Decodes texture `i`, cropped to region(i).
    Image decode(const GsMemory& gs, size_t i) const;
    // A texture coordinate of texture `i` (s, t: fractions of its full power-of-two size, TEX0
    // TW / TH, as the GS samples it) as a fraction of the decoded picture, region(i).
    std::array<float, 2> region_uv(size_t i, float s, float t) const;
};

// Finds every embedded Tex1 set inside an arbitrary container (car models,
// course files and menus embed texture sets). Returns byte offsets.
std::vector<size_t> find_tex1_sets(ByteSpan data);

// True for an indexed texture (T4 / T8 and their H variants) with a 32-bit (CT32 / CT24) CSM1
// CLUT: the textures whose palette TEX0.CSA selects and CLUT animations recolour.
bool indexed_ct32(const Tex0& t);
// Position of CLUT entry `e` (0..255) in the 16 x 16 CT32 rectangle of a CSM1 CLUT (a T4
// texture's entry i is e = CSA x 16 + i).
void csm1_position(u32 e, u32& x, u32& y);
// A CT32 CLUT colour as RGBA8 (GS alpha 0..0x80 expanded to 0..255; CT24: opaque).
u32 clut_rgba(u32 c, u32 cpsm);

} // namespace gt3r
