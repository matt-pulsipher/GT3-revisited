// Text & fonts: encoding, MLTX string tables, font parsing and layout.

#include "test_framework.h"

#include "formats/text/charset.h"
#include "formats/text/fnt.h"
#include "formats/text/mltx.h"
#include "formats/text/text_layout.h"

#include <cstring>

using namespace gt3r;
using namespace gt3r::text;

namespace {

void put16(Bytes& b, size_t o, u16 v) {
    if (b.size() < o + 2) b.resize(o + 2);
    std::memcpy(&b[o], &v, 2);
}
void put32(Bytes& b, size_t o, u32 v) {
    if (b.size() < o + 4) b.resize(o + 4);
    std::memcpy(&b[o], &v, 4);
}

// Tiny UTBL: row 0x24 starting at col 0x22 -> U+3042 (あ), U+3043.
JisTable tiny_jis() {
    Bytes b(0x40, 0);
    std::memcpy(b.data(), "UTBL", 4);
    put16(b, 4, 1);      // rows
    put16(b, 6, 2);      // total entries
    put32(b, 8, 0x10);   // row table
    put32(b, 12, 0x20);  // data
    b[0x10] = 0x22;      // first low byte
    b[0x11] = 0x24;      // high byte
    put16(b, 0x12, 0);   // first index
    put16(b, 0x20, 0x3042);
    put16(b, 0x22, 0x3043);
    return JisTable::parse(b);
}

std::string str(std::initializer_list<int> bytes) {
    std::string s;
    for (int c : bytes) s.push_back(char(c));
    return s;
}

} // namespace

TEST(text_utf8_roundtrip) {
    std::u32string u = {U'A', U'é', U'€', U'あ', U'漢', 0x1F600};
    std::string s = utf8_encode(u);
    CHECK(utf8_decode(s) == u);
    CHECK(utf8_decode("\xC3") == std::u32string(1, kReplacementChar)); // truncated
    CHECK(utf8_decode("a\xFFz") == (std::u32string{U'a', kReplacementChar, U'z'}));
}

TEST(text_iso8859_pages) {
    CHECK(iso8859_to_font_code(1, 0xE9) == 0xE9);    // Latin-1 é
    CHECK(iso8859_to_font_code(2, 0xA1) == 0x104);   // Latin-2 Ą
    CHECK(iso8859_to_font_code(15, 0xA4) == 0x80);   // Latin-9 euro -> font code 0x80
    CHECK(iso8859_to_font_code(7, 0xA1) == 0x2BD);   // 1987 Greek
    CHECK(iso8859_to_font_code(6, '3') == 0x663);    // Arabic-Indic digit
    CHECK(iso8859_to_font_code(0, 0x80) == 0x80);    // raw font code
    CHECK(iso8859_to_font_code(0, 0x81) == kNoCode); // undefined in 1252
    CHECK(iso8859_to_font_code(1, 0x85) == kNoCode); // C1 range
    CHECK(font_code_to_unicode(0x80) == 0x20AC);
    CHECK(unicode_to_font_code(0x20AC) == 0x80);
    CHECK(unicode_to_font_code(0x141) == 0x141);
    CHECK(unicode_to_font_code(0x3042) == kNoCode);
}

TEST(text_game_encoding) {
    JisTable jis = tiny_jis();
    CHECK(jis.to_unicode(0x2422) == 0x3042);
    CHECK(jis.from_unicode(0x3043) == 0x2423);
    // "déjà" as stored in the French menus, then EUC-JP あい.
    std::string fr = str({'d', 0x81, 0xE9, 'j', 0x81, 0xE0});
    CHECK(game_text_to_utf8(fr, &jis) == "d\xC3\xA9j\xC3\xA0");
    std::string jp = str({0xA4, 0xA2, 0xA4, 0xA3});
    CHECK(game_text_to_utf8(jp, &jis) == "\xE3\x81\x82\xE3\x81\x83");
    CHECK(game_text_to_utf8(jp, nullptr) == "\xEF\xBF\xBD\xEF\xBF\xBD");
    // Encoding back.
    CHECK(encode_game_text(U"dé€あ", &jis) == str({'d', 0x81, 0xE9, 0x80, 0x80, 0xA4, 0xA2}));
}

TEST(text_game_text_prefix) {
    // ASCII, a Latin escape and an EUC-JP pair: one character each.
    const std::string s = str({'A', 0x81, 0xE9, 0xA1, 0xA6, 'B'});
    CHECK(game_text_length(s) == 4);
    CHECK(game_text_prefix(s, 0).empty());
    CHECK(game_text_prefix(s, 2) == str({'A', 0x81, 0xE9}));
    CHECK(game_text_prefix(s, 3) == str({'A', 0x81, 0xE9, 0xA1, 0xA6}));
    CHECK(game_text_prefix(s, 9) == s);
    // A truncated pair and anything after a NUL are not characters (the renderer stops there).
    CHECK(game_text_length(str({'A', 0xA1})) == 1);
    CHECK(game_text_prefix(str({'A', 0xA1}), 2) == "A");
    CHECK(game_text_length(std::string("AB\0C", 4)) == 2);
}

TEST(text_mltx_synthetic) {
    // 2 keys, 3 language pools of 8 bytes.
    Bytes b(0x40, 0);
    std::memcpy(b.data(), "MLTX", 4);
    put32(b, 4, 2);
    put32(b, 8, 0x28);  // pool
    put32(b, 12, 2);    // count
    put32(b, 16, 8);    // pool size
    put32(b, 0x14, 0x24); put32(b, 0x18, 0);
    put32(b, 0x1C, 0x26); put32(b, 0x20, 4);
    std::memcpy(&b[0x24], "A\0B\0", 4);
    std::memcpy(&b[0x28], "jp1\0jp2\0", 8);
    std::memcpy(&b[0x30], "us1\0\0\0\0\0", 8);
    std::memcpy(&b[0x38], "\0\0\0\0br2\0", 8);
    StringTable t = StringTable::parse(b);
    CHECK(t.size() == 2);
    CHECK(t.language_count() == 3);
    CHECK(t.key(1) == "B");
    CHECK(t.raw(1, Language::JP) == "jp2");
    CHECK(t.raw(1, Language::US).empty());
    CHECK(t.raw(1, Language::BR) == "br2");
    CHECK(t.filled(Language::US) == 1);
    CHECK(t.find("B") == 1);
    CHECK(std::string(language_code(Language::BR)) == "BR");
    CHECK(language_from_code("SP") == Language::SP);
}

TEST(text_layout_kerning) {
    LatinFont f;
    f.first = 0x20;
    f.last = 0x7F;
    f.line_height = 10;
    f.ascent = 8;
    f.profile_rows = 2;
    f.glyphs.resize(0x60);
    f.profiles = {1, 2, 3, 0}; // profile 0: {1,2}, profile 2: {3,0}
    FontGlyph& a = f.glyphs['A' - 0x20];
    a = {0, -8, 6, 8, 6, 10, 20, 0, 2};
    FontGlyph& v = f.glyphs['V' - 0x20];
    v = {1, -8, 6, 8, 6, 20, 20, 2, 0};
    f.glyphs[' ' - 0x20].advance = 4;
    // kerning(A, V) = min(V.left + A.right) = min(3+3, 0+0) = 0; (V, A) = min(1+1, 2+2) = 2
    CHECK(f.kerning(a, v) == 0);
    CHECK(f.kerning(v, a) == 2);
    LayoutOptions opt;
    opt.valign = VAlign::Baseline;
    TextLayout lay = layout_text(map_game_text(ByteSpan(reinterpret_cast<const u8*>("VA V"), 4)), f, nullptr, 0,
                                 0, opt);
    CHECK(lay.quads.size() == 3);
    CHECK(lay.quads[0].x == 1);          // V: left bearing 1
    CHECK(lay.quads[1].x == 7 + 0 - 2);  // A after V: pen 7, pulled left by 2
    CHECK(lay.quads[2].x == 15 + 1);     // space (no profile) then V
    CHECK(lay.quads[0].y == -8);
    CHECK(lay.width == 22);
    opt.align = HAlign::Right;
    opt.valign = VAlign::Top;
    opt.scale_x = opt.scale_y = 2;
    lay = layout_text(map_game_text(ByteSpan(reinterpret_cast<const u8*>("A\nA"), 3)), f, nullptr, 100, 0, opt);
    CHECK(lay.lines == 2);
    CHECK(lay.width == 12);
    CHECK(lay.height == 40);
    CHECK(lay.quads[0].x == 88);
    CHECK(lay.quads[0].y == 0);   // top-aligned: baseline at ascent*2, glyph top -8*2
    CHECK(lay.quads[1].y == 20);
}

TEST(text_disc_realtime) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Volume& vol = disc->volume();
    JisTable jis = JisTable::parse(vol.read(*vol.find("font/jis2uni.dat")));
    CHECK(jis.size() > 7000);
    CHECK(jis.to_unicode(0x2121) == 0x3000);
    StringTable t = StringTable::parse(vol.read(*vol.find("text/realtime.dat")));
    CHECK(t.size() == 193);
    CHECK(t.language_count() == 7);
    int brake = t.find("DATABASEPARTS_PTEXT_BRAKE");
    CHECK(brake >= 0);
    if (brake >= 0) {
        CHECK(t.utf8(brake, Language::JP, &jis) == "\xE3\x83\x96\xE3\x83\xAC\xE3\x83\xBC\xE3\x82\xAD"); // ブレーキ
        CHECK(t.utf8(brake, Language::US, &jis) == "Brakes");
    }
    CHECK(t.filled(Language::JP) == 193);
    CHECK(t.filled(Language::US) == 193);
    // Every value decodes without replacement characters.
    for (int l = 0; l < t.language_count(); ++l)
        for (size_t i = 0; i < t.size(); ++i)
            CHECK(t.utf8(i, Language(l), &jis).find("\xEF\xBF\xBD") == std::string::npos);
}

TEST(text_disc_fonts) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Volume& vol = disc->volume();
    FontFile sys = FontFile::parse(vol.read(*vol.find("font/system.fnt")));
    CHECK(sys.fonts.size() == 4);
    FontFile menu = FontFile::parse(vol.read(*vol.find("font/menu.fnt")));
    CHECK(menu.fonts.size() == 5);
    for (const LatinFont& f : sys.fonts) {
        CHECK(f.first == 0x20 && f.last == 0x17F);
        CHECK(f.has('A') && f.has(0xE9) && f.has(0x141) && f.has(0x80));
        for (int v = 0; v < kFontVariants; ++v) CHECK(f.atlas[v].width == 128 && f.atlas[v].height > 0);
        // Every glyph lies inside its atlas.
        for (const FontGlyph& g : f.glyphs)
            if (g.width) CHECK(u32(g.atlas_x + g.width) <= f.atlas[0].width && u32(g.atlas_y + g.height) <= f.atlas[0].height);
    }
    KanjiFont kf = KanjiFont::parse(vol.read(*vol.find("font/system.kf")));
    CHECK(kf.cell_width() == 22 && kf.cell_height() == 22);
    CHECK(kf.glyph_count() == 7318);
    CHECK(kf.has(0x2422) && kf.has(0x3441)); // あ, 漢
    CHECK(!kf.has(0x222F));                  // undefined -> "missing" glyph
    KanjiFont k22 = KanjiFont::parse(vol.read(*vol.find("font/kanji22.kf")));
    CHECK(k22.glyph_count() == 7482);
    CHECK(k22.has(0x2422));
    std::vector<u32> cell(22 * 22);
    CHECK(kf.render(0x2422, FontVariant::Outlined, cell.data()));
    size_t ink = 0;
    for (u32 p : cell) ink += (p >> 24) != 0;
    CHECK(ink > 50 && ink < 22 * 22);
}
