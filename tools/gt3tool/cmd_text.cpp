// gt3tool text commands: string tables, font atlases, text rendering.

#include "tool.h"

#include "formats/png_writer.h"
#include "formats/text/charset.h"
#include "formats/text/fnt.h"
#include "formats/text/mltx.h"
#include "formats/text/text_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace gt3r::tool {

namespace {

using namespace gt3r::text;

struct FontData {
    std::vector<std::pair<std::string, FontFile>> files;
    JisTable jis;
    std::unique_ptr<KanjiFont> kanji;

    explicit FontData(Volume& vol, Language lang = Language::JP) {
        for (const char* p : {"font/system.fnt", "font/menu.fnt"})
            files.emplace_back(p, FontFile::parse(vol.read(need(vol, p))));
        jis = JisTable::parse(vol.read(need(vol, "font/jis2uni.dat")));
        kanji = std::make_unique<KanjiFont>(KanjiFont::parse(vol.read(need(vol, kanji_font_path(lang)))));
    }
    const LatinFont* find(const std::string& name) const {
        for (auto& f : files)
            for (auto& lf : f.second.fonts)
                if (lf.name == name) return &lf;
        return nullptr;
    }
};

// Source-over blend of a straight-alpha RGBA pixel, tinted.
void blend(u32& dst, u32 src, u32 tint) {
    auto ch = [](u32 c, int s) { return (c >> s) & 0xFF; };
    u32 a = ch(src, 24) * ch(tint, 24) / 255;
    if (!a) return;
    u32 out = 0;
    for (int s = 0; s < 24; s += 8) {
        u32 sc = ch(src, s) * ch(tint, s) / 255, dc = ch(dst, s);
        out |= ((sc * a + dc * (255 - a)) / 255) << s;
    }
    u32 da = ch(dst, 24);
    out |= (a + da * (255 - a) / 255) << 24;
    dst = out;
}

// Nearest-neighbour CPU renderer for a layout (good enough for checking).
void draw_layout(std::vector<u32>& img, u32 w, u32 h, const TextLayout& lay, const LatinFont& font,
                 const KanjiFont* kanji, FontVariant v, u32 tint) {
    const Tex1::Image& atlas = font.atlas[size_t(v)];
    std::vector<u32> cell;
    for (const GlyphQuad& q : lay.quads) {
        const u32* src = nullptr;
        u32 src_w = 0;
        if (q.kanji) {
            cell.assign(size_t(q.sw) * q.sh, 0);
            if (!kanji || !kanji->render(q.code, v, cell.data())) continue;
            src = cell.data();
            src_w = q.sw;
        } else {
            src = atlas.rgba.data() + size_t(q.sy) * atlas.width + q.sx;
            src_w = atlas.width;
        }
        int x0 = int(std::lround(q.x)), y0 = int(std::lround(q.y));
        int dw = int(std::lround(q.w)), dh = int(std::lround(q.h));
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x) {
                int px = x0 + x, py = y0 + y;
                if (px < 0 || py < 0 || u32(px) >= w || u32(py) >= h) continue;
                u32 sx = u32(x * q.sw / std::max(dw, 1)), sy = u32(y * q.sh / std::max(dh, 1));
                blend(img[size_t(py) * w + px], src[size_t(sy) * src_w + sx], tint);
            }
    }
}

std::string sanitize(std::string s) {
    for (char& c : s)
        if (c == '\n') c = '|';
    return s;
}

// Game text with its double-byte pairs as "{XXXX}" (the cell font's code); the rest as UTF-8.
std::string cell_codes(const std::string& raw) {
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i) {
        u8 b = u8(raw[i]);
        if (b < 0x80 || i + 1 >= raw.size()) {
            out += raw[i];
        } else if (b >= 0xA1) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "{%02X%02X}", b & 0x7F, u8(raw[i + 1]) & 0x7F);
            out += buf, ++i;
        } else {
            out += game_text_to_utf8(ByteSpan(reinterpret_cast<const u8*>(raw.data()) + i, 2), nullptr), ++i;
        }
    }
    return out;
}

int cmd_text(Disc& disc, const Args& a) {
    Volume& vol = disc.volume();
    const std::string& out = a[0];
    FontData fd(vol);

    // String table, one UTF-8 file per language.
    StringTable st = StringTable::parse(vol.read(need(vol, "text/realtime.dat")));
    std::printf("text/realtime.dat: %zu keys, %d languages\n", st.size(), st.language_count());
    for (Language lang : st.languages()) {
        std::string body;
        size_t bad = 0;
        // KR and TW text has no table to Unicode on the disc: its pairs are written as {cell font code}.
        const bool coded = lang == Language::KR || lang == Language::CN || lang == Language::TW;
        for (size_t i = 0; i < st.size(); ++i) {
            std::string v = coded ? cell_codes(st.raw(i, lang)) : st.utf8(i, lang, &fd.jis);
            if (v.find("\xEF\xBF\xBD") != std::string::npos) ++bad;
            body += st.key(i) + "\t" + sanitize(v) + "\n";
        }
        std::printf("  %s %-13s %3zu/%zu filled, %zu with unmapped characters\n", language_code(lang),
                    language_name(lang), st.filled(lang), st.size(), bad);
        if (!save(out + "/strings/realtime_" + language_code(lang) + ".txt",
                  ByteSpan(reinterpret_cast<const u8*>(body.data()), body.size())))
            return 1;
    }

    // Latin font atlases and metrics.
    std::string metrics;
    for (auto& [path, file] : fd.files) {
        std::string base = path.substr(path.find('/') + 1);
        base = base.substr(0, base.find('.'));
        for (const LatinFont& f : file.fonts) {
            size_t defined = std::count_if(f.glyphs.begin(), f.glyphs.end(), [](auto& g) { return g.defined(); });
            char line[256];
            std::snprintf(line, sizeof(line),
                          "%s/%s: codes 0x%X-0x%X (%zu defined), size %u, line %u, ascent %d, profile rows %u, "
                          "kanji %ux%u at (%d,%d), atlas %ux%u\n",
                          path.c_str(), f.name.c_str(), f.first, f.last, defined, f.size, f.line_height, f.ascent,
                          f.profile_rows, f.kanji_width, f.kanji_height, f.kanji_dx, f.kanji_dy, f.atlas[0].width,
                          f.atlas[0].height);
            std::fputs(line, stdout);
            metrics += line;
            for (int v = 0; v < kFontVariants; ++v) {
                const Tex1::Image& img = f.atlas[v];
                if (!save(out + "/fonts/" + base + "_" + f.name + "_v" + std::to_string(v) + ".png",
                          encode_png(img.width, img.height, img.rgba)))
                    return 1;
            }
        }
    }
    if (!save(out + "/fonts/metrics.txt", ByteSpan(reinterpret_cast<const u8*>(metrics.data()), metrics.size())))
        return 1;

    // Kanji fonts as grids of cells, 64 per row.
    // system.hf (Korean) and system.tf (Chinese) are on later discs of this engine only.
    for (const char* p : {"font/system.kf", "font/kanji22.kf", "font/system.hf", "font/system.tf"}) {
        if (!vol.find(p)) continue;
        KanjiFont k = KanjiFont::parse(vol.read(need(vol, p)));
        std::vector<u16> codes = k.codes();
        u32 cw = k.cell_width(), ch = k.cell_height(), cols = 64, rows = u32((codes.size() + cols - 1) / cols);
        std::vector<u32> img(size_t(cols * cw) * rows * ch, 0xFF402020u);
        std::vector<u32> cell(size_t(cw) * ch);
        for (size_t i = 0; i < codes.size(); ++i) {
            k.render(codes[i], FontVariant::Outlined, cell.data());
            u32 ox = u32(i % cols) * cw, oy = u32(i / cols) * ch;
            for (u32 y = 0; y < ch; ++y)
                for (u32 x = 0; x < cw; ++x) blend(img[size_t(oy + y) * cols * cw + ox + x], cell[y * cw + x], ~0u);
        }
        std::printf("%s: %s, %ux%u cells, %zu glyphs, %zu mapped codes\n", p, k.name().c_str(), cw, ch,
                    k.glyph_count(), codes.size());
        std::string name = std::string(p).substr(5);
        name[name.find('.')] = '_';
        if (!save(out + "/fonts/" + name + ".png", encode_png(cols * cw, rows * ch, img))) return 1;
        // The codes in atlas order, 64 per line (row major), to find a cell's code.
        std::string list;
        for (size_t i = 0; i < codes.size(); ++i) {
            char b[8];
            std::snprintf(b, sizeof b, "%04X%c", codes[i], i % cols == cols - 1 ? '\n' : ' ');
            list += b;
        }
        if (!save(out + "/fonts/" + name + "_codes.txt", ByteSpan(reinterpret_cast<const u8*>(list.data()), list.size())))
            return 1;
    }
    std::printf("jis2uni.dat: %zu JIS codes\n", fd.jis.size());
    return 0;
}

// text-render <font> <utf8 text> <out.png> [scale]
int cmd_text_render(Disc& disc, const Args& a) {
    // --lang=KR | TW: the text is drawn with that language's cell font. It is then given as
    // "key:<realtime.dat key>" (that language's value) or as hex bytes of game text ("hex:B0A1...").
    Language lang = Language::JP;
    for (size_t i = 3; i < a.size(); ++i)
        if (a[i].rfind("--lang=", 0) == 0) lang = language_from_code(a[i].substr(7)).value_or(Language::JP);
    FontData fd(disc.volume(), lang);
    const LatinFont* font = fd.find(a[0]);
    if (!font) {
        std::fprintf(stderr, "unknown font %s\n", a[0].c_str());
        return 1;
    }
    std::string text;
    for (size_t i = 0; i < a[1].size(); ++i) // allow "\n" escapes on the command line
        if (a[1][i] == '\\' && i + 1 < a[1].size() && a[1][i + 1] == 'n') {
            text += '\n';
            ++i;
        } else {
            text += a[1][i];
        }
    LayoutOptions opt;
    opt.scale_x = opt.scale_y = a.size() > 3 && a[3].rfind("--", 0) != 0 ? std::stof(a[3]) : 1.0f;
    std::vector<GlyphRef> refs;
    if (text.rfind("key:", 0) == 0) {
        StringTable st = StringTable::parse(disc.volume().read(need(disc.volume(), "text/realtime.dat")));
        int k = st.find(text.substr(4));
        if (k < 0 || st.raw(size_t(k), lang).empty()) {
            std::fprintf(stderr, "no %s text for %s\n", language_code(lang), text.c_str() + 4);
            return 1;
        }
        const std::string& raw = st.raw(size_t(k), lang);
        refs = map_game_text(ByteSpan(reinterpret_cast<const u8*>(raw.data()), raw.size()));
    } else if (text.rfind("hex:", 0) == 0) {
        Bytes raw;
        for (size_t i = 4; i + 1 < text.size(); i += 2) raw.push_back(u8(std::stoul(text.substr(i, 2), nullptr, 16)));
        refs = map_game_text(raw);
    } else {
        refs = map_unicode(utf8_decode(text), *font, fd.kanji.get(), &fd.jis);
    }
    TextLayout lay = layout_text(refs, *font, fd.kanji.get(), 8, 8, opt);
    u32 w = u32(std::ceil(lay.width)) + 16, h = u32(std::ceil(lay.height)) + 16;
    std::vector<u32> img(size_t(w) * 2 * h, 0xFF603020u);
    draw_layout(img, w, 2 * h, lay, *font, fd.kanji.get(), FontVariant::Outlined, ~0u);
    TextLayout lay2 = layout_text(refs, *font, fd.kanji.get(), 8, 8 + float(h), opt);
    draw_layout(img, w, 2 * h, lay2, *font, fd.kanji.get(), FontVariant::Plain, 0xFF40C0FFu);
    std::printf("%zu glyphs, %.1f x %.1f px, %d line(s)\n", lay.quads.size(), lay.width, lay.height, lay.lines);
    return save(a[2], encode_png(w, 2 * h, img)) ? 0 : 1;
}

} // namespace

GT3TOOL_COMMAND(text, "text", "<outdir>", "dump string tables (UTF-8) and font atlases (PNG)", 1, cmd_text);
GT3TOOL_COMMAND(text_render, "text-render", "<font> <utf8 | key:KEY | hex:BYTES> <out.png> [scale] [--lang=XX]",
                "render a string with a game font (outlined and plain variants)", 3, cmd_text_render);

} // namespace gt3r::tool
