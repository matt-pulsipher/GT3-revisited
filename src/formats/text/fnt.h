#pragma once

// "FT01" bitmap fonts (font/system.fnt, font/menu.fnt) and "KNJI"/"KFT0"
// kanji fonts (font/system.kf, font/kanji22.kf). See docs/formats/text.md.
//
// FT01 container:
//   0x00 "FT01"
//   0x10 u16 font count
//   0x14 u32 offset of font offset table   (u32[count] -> font records)
//   0x18 u32 offset of texture offset table (u32[count] -> embedded Tex1 sets)
// Font record (0x20 bytes, +0x14.. is runtime state):
//   +0x00 u32 name offset, +0x04 u32 glyph table offset,
//   +0x08 u16 first code, +0x0A u16 last code,
//   +0x0C u8 size, u8 line height, i8 ascent, u8 kerning profile rows,
//   +0x10 u8 kanji width, u8 kanji height, i8 kanji x offset, i8 kanji y offset
// Glyph record (16 bytes, one per code first..last):
//   i8 left bearing, i8 top (relative to the baseline), u8 width, u8 height,
//   u8 advance, u8 atlas x, u16 atlas y, u32 left profile offset, u32 right
//   profile offset (0 = none). A profile holds one signed byte per row
//   ("profile rows" of them): the horizontal gap between the cell edge and
//   the ink in that row. The renderer pulls each glyph left by the smallest
//   (left gap of this glyph + right gap of the previous glyph) over all rows.

#include "base/bytes.h"
#include "formats/tex1.h"

#include <array>
#include <string>
#include <vector>

namespace gt3r::text {

using i8 = std::int8_t;

// Palette variants of a font texture. Every font set carries two textures
// sharing the 4-bit glyph bitmap: CLUT 0 renders white glyphs with a dark
// anti-aliased outline (the default), CLUT 1 white glyphs on an alpha ramp
// without outline (selected with a "#1" suffix in the game's font specs).
enum class FontVariant : u8 { Outlined = 0, Plain = 1 };
constexpr int kFontVariants = 2;

struct FontGlyph {
    static constexpr u32 kNoProfile = 0xFFFFFFFFu;
    i8 left = 0, top = 0;
    u8 width = 0, height = 0, advance = 0;
    u16 atlas_x = 0, atlas_y = 0;
    u32 left_profile = kNoProfile, right_profile = kNoProfile; // offsets into LatinFont::profiles
    bool defined() const { return width || advance || left; }
};

struct LatinFont {
    std::string name;
    u16 first = 0, last = 0;
    u8 size = 0, line_height = 0, profile_rows = 0;
    i8 ascent = 0;
    u8 kanji_width = 0, kanji_height = 0;
    i8 kanji_dx = 0, kanji_dy = 0;
    std::vector<FontGlyph> glyphs; // index = code - first
    std::vector<i8> profiles;      // profile bytes, profile_rows per profile
    // Glyph atlas per variant (RGBA8, GS alpha already expanded).
    std::array<Tex1::Image, kFontVariants> atlas;
    // A texture pack's picture of the atlas (formats/texture_pack.h), empty when none: drawn in
    // its place, the glyph rectangles scaled to its size.
    std::array<Tex1::Image, kFontVariants> pack_atlas;

    // nullptr when `code` is outside [first, last].
    const FontGlyph* glyph(u32 code) const {
        return code >= first && code <= last ? &glyphs[code - first] : nullptr;
    }
    bool has(u32 code) const {
        const FontGlyph* g = glyph(code);
        return g && g->defined();
    }
    // The amount the game pulls `cur` to the left when it follows `prev`.
    int kerning(const FontGlyph& prev, const FontGlyph& cur) const;
};

struct FontFile {
    std::vector<LatinFont> fonts;

    static bool is_ft01(ByteSpan d) { return has_magic(d, "FT01"); }
    static FontFile parse(ByteSpan d);
};

// Kanji fonts: fixed-size 4bpp cells addressed by JIS X 0208 code.
//
// KNJI (font/system.kf, used by the game):
//   0x10 u16 block count, u16 palette count
//   0x14 u32 offset of block offset table, 0x18 u32 offset of palettes
//        (16 RGBA entries each, GS alpha)
//   block: u16 first JIS, u16 last JIS, u32 glyph data offset, u32 glyph
//          stride, u16 cell width, u16 cell height, u32 index table offset
//          (u16 glyph index per code first..last), u32 name offset
// KFT0 (font/kanji22.kf, not referenced by the game program):
//   0x04 u16 glyph stride, 0x08 u16 index[94 * 94] by (row-0x21)*94 + (col-0x21)
//   (0xFFFF = none), glyph data follows the index; 22x22 cells.
// Cell pixels are row-major 4bpp, low nibble first, ceil(w/2) bytes per row.
class KanjiFont {
public:
    static bool is_kanji_font(ByteSpan d) { return has_magic(d, "KNJI") || has_magic(d, "KFT0"); }
    static KanjiFont parse(ByteSpan d);

    const std::string& name() const { return name_; }
    u32 cell_width() const { return w_; }
    u32 cell_height() const { return h_; }
    size_t glyph_count() const { return glyph_count_; }
    // True if `jis` has a real glyph. Undefined codes of the retail font share
    // one "missing" glyph (index 108); has() is false for them but
    // drawable() is true, so game strings still draw it like the game does.
    bool has(u16 jis) const {
        int i = index_of(jis);
        return i >= 0 && i != missing_;
    }
    bool drawable(u16 jis) const { return index_of(jis) >= 0; }
    int missing_glyph() const { return missing_; }
    // Draws a cell as RGBA8 (cell_width * cell_height). Returns false if missing.
    bool render(u16 jis, FontVariant v, u32* out) const;
    // Every JIS code that has a glyph, ascending.
    std::vector<u16> codes() const;

private:
    struct Block {
        u16 first = 0, last = 0;
        std::vector<u16> index;
    };
    int index_of(u16 jis) const;

    std::string name_;
    u32 w_ = 0, h_ = 0, stride_ = 0;
    size_t glyph_count_ = 0;
    int missing_ = -1;
    std::vector<Block> blocks_;
    Bytes pixels_; // glyph_count_ * stride_
    std::vector<std::array<u32, 16>> palettes_; // RGBA8
};

} // namespace gt3r::text
