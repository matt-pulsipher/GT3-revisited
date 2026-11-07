#include "formats/text/fnt.h"

#include "formats/texture_pack.h"

#include <algorithm>
#include <map>

namespace gt3r::text {

namespace {

std::string cstring_at(ByteSpan d, size_t off) {
    if (off >= d.size()) throw FormatError("font name offset out of range");
    size_t e = off;
    while (e < d.size() && d[e]) ++e;
    return std::string(reinterpret_cast<const char*>(d.data() + off), e - off);
}

u32 gs_to_rgba(u32 c) { return (c & 0x00FFFFFFu) | (std::min<u32>((c >> 24) * 2, 255) << 24); }

} // namespace

int LatinFont::kerning(const FontGlyph& prev, const FontGlyph& cur) const {
    // Glyphs without a profile (the space) are never kerned. The original
    // reads through a null pointer there; treating it as "no kerning" is our
    // choice.
    if (prev.right_profile == FontGlyph::kNoProfile || cur.left_profile == FontGlyph::kNoProfile) return 0;
    int k = 0x80;
    for (u32 r = 0; r < profile_rows; ++r) {
        int sum = std::clamp(int(profiles[cur.left_profile + r]) + int(profiles[prev.right_profile + r]), -128, 127);
        k = std::min(k, sum & 0x7F);
    }
    return k & 0x7F;
}

FontFile FontFile::parse(ByteSpan d) {
    if (!is_ft01(d)) throw FormatError("not an FT01 font file");
    FontFile f;
    u32 count = rd16(d, 0x10);
    u32 font_table = rd32(d, 0x14), tex_table = rd32(d, 0x18);
    for (u32 i = 0; i < count; ++i) {
        u32 o = rd32(d, font_table + i * 4);
        LatinFont font;
        font.name = cstring_at(d, rd32(d, o));
        u32 glyph_table = rd32(d, o + 4);
        font.first = rd16(d, o + 8);
        font.last = rd16(d, o + 10);
        if (font.last < font.first) throw FormatError("font has an empty code range");
        font.size = d[o + 0x0C];
        font.line_height = d[o + 0x0D];
        font.ascent = i8(d[o + 0x0E]);
        font.profile_rows = d[o + 0x0F];
        font.kanji_width = d[o + 0x10];
        font.kanji_height = d[o + 0x11];
        font.kanji_dx = i8(d[o + 0x12]);
        font.kanji_dy = i8(d[o + 0x13]);

        std::map<u32, u32> profile_map; // file offset -> pool offset
        auto profile = [&](u32 off) -> u32 {
            if (off == 0) return FontGlyph::kNoProfile;
            auto it = profile_map.find(off);
            if (it != profile_map.end()) return it->second;
            if (off > d.size() || d.size() - off < font.profile_rows) throw FormatError("font profile out of range");
            u32 pos = u32(font.profiles.size());
            for (u32 r = 0; r < font.profile_rows; ++r) font.profiles.push_back(i8(d[off + r]));
            profile_map.emplace(off, pos);
            return pos;
        };
        u32 max_y = 0;
        for (u32 c = font.first; c <= font.last; ++c) {
            size_t g = glyph_table + size_t(c - font.first) * 16;
            if (g + 16 > d.size()) throw FormatError("glyph table out of range");
            FontGlyph glyph;
            glyph.left = i8(d[g]);
            glyph.top = i8(d[g + 1]);
            glyph.width = d[g + 2];
            glyph.height = d[g + 3];
            glyph.advance = d[g + 4];
            glyph.atlas_x = d[g + 5];
            glyph.atlas_y = rd16(d, g + 6);
            glyph.left_profile = profile(rd32(d, g + 8));
            glyph.right_profile = profile(rd32(d, g + 12));
            if (glyph.width) max_y = std::max<u32>(max_y, glyph.atlas_y + glyph.height);
            font.glyphs.push_back(glyph);
        }

        // Glyph atlas: an embedded Tex1 set with one texture per palette.
        u32 tex = rd32(d, tex_table + i * 4);
        ByteSpan sub = d.subspan(tex);
        if (!Tex1::is_tex1(sub)) throw FormatError("font texture is not a Tex1 set");
        Tex1 set = Tex1::parse(sub);
        GsMemory gs;
        set.upload(sub, gs);
        for (int v = 0; v < kFontVariants && v < int(set.textures.size()); ++v) {
            const Tex0& t0 = set.textures[v].tex0;
            u32 w = t0.width(), h = std::min(t0.height(), std::max<u32>(max_y, 1));
            Tex1::Image& img = font.atlas[v];
            img.width = w;
            img.height = h;
            img.rgba = gs.decode_rgba(t0, w, h);
            if (std::optional<texture_pack::Image> r = texture_pack::replacement(sub, size_t(v), {0, 0, w, h}))
                font.pack_atlas[v] = {r->width, r->height, std::move(r->rgba)};
        }
        if (set.textures.size() == 1) font.atlas[1] = font.atlas[0], font.pack_atlas[1] = font.pack_atlas[0];
        f.fonts.push_back(std::move(font));
    }
    return f;
}

KanjiFont KanjiFont::parse(ByteSpan d) {
    KanjiFont k;
    if (has_magic(d, "KNJI")) {
        u32 blocks = rd16(d, 0x10), palettes = rd16(d, 0x12);
        u32 block_table = rd32(d, 0x14), pal_off = rd32(d, 0x18);
        for (u32 p = 0; p < palettes; ++p) {
            std::array<u32, 16> pal{};
            for (u32 i = 0; i < 16; ++i) pal[i] = gs_to_rgba(rd32(d, pal_off + p * 64 + i * 4));
            k.palettes_.push_back(pal);
        }
        // All blocks of the retail font share one glyph pool; pixels are
        // addressed by the per-block data pointer, so copy each block's range.
        for (u32 b = 0; b < blocks; ++b) {
            u32 o = rd32(d, block_table + b * 4);
            Block blk;
            blk.first = rd16(d, o);
            blk.last = rd16(d, o + 2);
            u32 data = rd32(d, o + 4), stride = rd32(d, o + 8);
            u32 w = rd16(d, o + 12), h = rd16(d, o + 14);
            u32 index = rd32(d, o + 16), name = rd32(d, o + 20);
            if (blk.last < blk.first || !stride || !w || !h) throw FormatError("bad KNJI block");
            if (b == 0) {
                k.w_ = w;
                k.h_ = h;
                k.stride_ = stride;
                if (name) k.name_ = cstring_at(d, name);
            } else if (w != k.w_ || h != k.h_ || stride != k.stride_) {
                throw FormatError("KNJI blocks with differing cell sizes are not supported");
            }
            u32 base = u32(k.pixels_.size() / stride);
            u32 max_index = 0;
            for (u32 c = blk.first; c <= blk.last; ++c) {
                u16 idx = rd16(d, index + (c - blk.first) * 2);
                max_index = std::max<u32>(max_index, idx);
                blk.index.push_back(u16(base + idx));
            }
            u64 bytes = u64(max_index + 1) * stride;
            if (data > d.size() || d.size() - data < bytes) throw FormatError("KNJI glyph data out of range");
            k.pixels_.insert(k.pixels_.end(), d.begin() + data, d.begin() + data + bytes);
            k.blocks_.push_back(std::move(blk));
        }
    } else if (has_magic(d, "KFT0")) {
        k.name_ = "KFT0";
        k.w_ = k.h_ = 22;
        k.stride_ = rd16(d, 4);
        constexpr u32 kCells = 94 * 94, kIndex = 8, kData = kIndex + kCells * 2;
        if (k.stride_ < (k.w_ + 1) / 2 * k.h_ || d.size() < kData) throw FormatError("bad KFT0 header");
        size_t glyphs = (d.size() - kData) / k.stride_;
        Block blk;
        blk.first = 0x2121;
        blk.last = 0x7E7E;
        for (u32 c = blk.first; c <= blk.last; ++c) {
            u32 row = c >> 8, col = c & 0xFF;
            u16 idx = 0xFFFF;
            if (col >= 0x21 && col <= 0x7E) {
                u16 v = rd16(d, kIndex + ((row - 0x21) * 94 + (col - 0x21)) * 2);
                if (v < glyphs) idx = v;
            }
            blk.index.push_back(idx);
        }
        k.pixels_.assign(d.begin() + kData, d.begin() + kData + glyphs * k.stride_);
        k.blocks_.push_back(std::move(blk));
    } else {
        throw FormatError("not a kanji font");
    }
    k.glyph_count_ = k.pixels_.size() / k.stride_;
    // A glyph index shared by many valid JIS codes is the "missing" glyph.
    std::map<u16, int> uses;
    for (const Block& b : k.blocks_)
        for (u32 c = b.first; c <= b.last; ++c)
            if ((c >> 8) >= 0x21 && (c >> 8) <= 0x7E && (c & 0xFF) >= 0x21 && (c & 0xFF) <= 0x7E)
                ++uses[b.index[c - b.first]];
    for (auto [idx, n] : uses)
        if (n > 64 && idx < k.glyph_count_) k.missing_ = idx;
    if (k.palettes_.empty()) { // no palette in the file: white on an alpha ramp
        std::array<u32, 16> pal{};
        for (u32 i = 0; i < 16; ++i) pal[i] = 0x00FFFFFFu | ((i * 17) << 24);
        k.palettes_.push_back(pal);
    }
    return k;
}

int KanjiFont::index_of(u16 jis) const {
    for (const Block& b : blocks_)
        if (jis >= b.first && jis <= b.last) {
            u16 i = b.index[jis - b.first];
            return i < glyph_count_ ? int(i) : -1;
        }
    return -1;
}

bool KanjiFont::render(u16 jis, FontVariant v, u32* out) const {
    int idx = index_of(jis);
    if (idx < 0) return false;
    const auto& pal = palettes_[std::min<size_t>(size_t(v), palettes_.size() - 1)];
    const u8* src = pixels_.data() + size_t(idx) * stride_;
    u32 row_bytes = (w_ + 1) / 2;
    for (u32 y = 0; y < h_; ++y)
        for (u32 x = 0; x < w_; ++x) {
            u8 b = src[y * row_bytes + x / 2];
            out[y * w_ + x] = pal[(x & 1) ? b >> 4 : b & 15];
        }
    return true;
}

std::vector<u16> KanjiFont::codes() const {
    std::vector<u16> out;
    for (const Block& b : blocks_)
        for (u32 c = b.first; c <= b.last; ++c)
            if (has(u16(c)) && (c >> 8) >= 0x21 && (c >> 8) <= 0x7E && (c & 0xFF) >= 0x21 && (c & 0xFF) <= 0x7E)
                out.push_back(u16(c));
    return out;
}

} // namespace gt3r::text
