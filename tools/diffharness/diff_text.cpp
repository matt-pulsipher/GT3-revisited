// Differential test for Latin text measurement / kerning: the program's
// per-character measure step (0x24D230, uses MMI saturating byte adds and
// halfword minimums for the kerning profiles) vs text::layout_text.

#include "harness.h"

#include "disc/disc.h"
#include "formats/text/fnt.h"
#include "formats/text/text_layout.h"

using namespace gt3r;
using namespace gt3r::diff;

namespace {
constexpr u32 kMeasureChar = 0x0024D230; // int (int code); updates the text state below
constexpr u32 kTextState = 0x002F72C0;   // global text renderer state
// Offsets in the text state used by the measure step.
constexpr u32 kFont = 0x78, kPrevCode = 0x80, kMaxWidth = 0x98, kSpacing = 0x9C, kLineSpacing = 0xA0,
              kScaleX = 0xA4, kScaleY = 0xA8, kStartX = 0xAC, kPenX = 0xBC, kLinesY = 0xC0;
constexpr u32 kHalfHeightFlag = 0x70003023; // scratchpad byte: interlaced half-height mode

// Copies an FT01 file into emulated memory and relocates font `index` the way
// the loader does (record name/glyph-table offsets and per-glyph profile
// offsets become absolute pointers). Returns the font record address.
u32 load_font(Ee& ee, ByteSpan file, u32 index) {
    u32 base = ee.alloc_bytes(file, 64);
    u32 rec = base + rd32(file, rd32(file, 0x14) + index * 4);
    ee.w32(rec + 0, ee.r32(rec + 0) + base);
    u32 table = ee.r32(rec + 4) + base;
    ee.w32(rec + 4, table);
    u32 first = ee.r16(rec + 8), last = ee.r16(rec + 10);
    for (u32 c = first; c <= last; ++c) {
        u32 g = table + (c - first) * 16;
        for (u32 o : {8u, 12u})
            if (u32 p = ee.r32(g + o)) ee.w32(g + o, p + base);
    }
    return rec;
}

void run_font(DiffContext& ctx, const char* path) {
    const Volume::Entry* e = ctx.disc->volume().find(path);
    if (!e) {
        ctx.note(std::string(path) + " not found");
        return;
    }
    Bytes file = ctx.disc->volume().read(*e);
    text::FontFile ff = text::FontFile::parse(file);
    for (u32 fi = 0; fi < ff.fonts.size(); ++fi) {
        const text::LatinFont& font = ff.fonts[fi];
        // Characters the original can measure without the null-profile read
        // (see docs/formats/text.md): defined glyphs with both profiles.
        std::vector<u8> chars;
        for (u32 c = std::max<u32>(font.first, 0x21); c <= std::min<u32>(font.last, 0x7E); ++c) {
            const text::FontGlyph* g = font.glyph(c);
            if (g && g->defined() && g->left_profile != text::FontGlyph::kNoProfile &&
                g->right_profile != text::FontGlyph::kNoProfile)
                chars.push_back(u8(c));
        }
        if (chars.empty()) continue;
        int kerned = 0;
        for (u8 a : chars)
            for (u8 b : chars) kerned += font.kerning(*font.glyph(a), *font.glyph(b)) != 0;
        ctx.note(std::string(path) + "#" + font.name + ": " + std::to_string(chars.size()) + " glyphs, " +
                 std::to_string(kerned) + " of " + std::to_string(chars.size() * chars.size()) + " pairs kerned");
        ctx.ee.reset();
        u32 rec = load_font(ctx.ee, file, fi);
        int n = std::max(1, ctx.iterations / int(ff.fonts.size()));
        for (int it = 0; it < n; ++it) {
            std::string s;
            int len = int(ctx.rng.range(1, 30));
            for (int i = 0; i < len; ++i) {
                if (ctx.rng.chance(0.05)) s.push_back(ctx.rng.chance(0.5) ? '\n' : '\r');
                else s.push_back(char(chars[ctx.rng.range(0, s64(chars.size()) - 1)]));
            }
            text::LayoutOptions opt;
            if (it % 4) {
                opt.scale_x = ctx.rng.uniform(0.5f, 2.0f);
                opt.scale_y = ctx.rng.uniform(0.5f, 2.0f);
                opt.spacing = float(ctx.rng.range(-2, 4));
                opt.line_spacing = float(ctx.rng.range(0, 4));
            }
            opt.valign = text::VAlign::Baseline;
            Ee& ee = ctx.ee;
            ee.w32(kTextState + kFont, rec);
            ee.w32(kTextState + kPrevCode, 0xFFFFFFFF);
            ee.wf(kTextState + kMaxWidth, 0);
            ee.wf(kTextState + kSpacing, opt.spacing);
            ee.wf(kTextState + kLineSpacing, opt.line_spacing);
            ee.wf(kTextState + kScaleX, opt.scale_x);
            ee.wf(kTextState + kScaleY, opt.scale_y);
            ee.wf(kTextState + kStartX, 0);
            ee.wf(kTextState + kPenX, 0);
            ee.wf(kTextState + kLinesY, 0);
            ee.w8(kHalfHeightFlag, 0);
            for (char c : s) ee.call(kMeasureChar, {Arg::integer(u8(c))});
            float orig = std::max(ee.rf(kTextState + kMaxWidth), ee.rf(kTextState + kPenX));

            text::TextLayout lay = text::layout_text(
                text::map_game_text(ByteSpan(reinterpret_cast<const u8*>(s.data()), s.size())), font, nullptr, 0, 0,
                opt);
            ctx.expect_near(orig, lay.width, 1e-4, 2e-6, [&] {
                std::string v;
                for (char c : s) v += c == '\n' ? "\\n" : c == '\r' ? "\\r" : std::string(1, c);
                char b[160];
                std::snprintf(b, sizeof b, " sx=%g sy=%g sp=%g", double(opt.scale_x), double(opt.scale_y),
                              double(opt.spacing));
                return std::string(path) + "#" + font.name + " \"" + v + "\"" + b;
            });
        }
    }
}

} // namespace

DIFF_TEST(text_measure_kerning, "text measure step 0x24D230 (MMI kerning) vs text::layout_text width") {
    run_font(ctx, "font/system.fnt");
    run_font(ctx, "font/menu.fnt");
}
