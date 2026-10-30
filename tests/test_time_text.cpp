// Time, speed and date texts (race/time_text.h, Hud::format_time): the menus' and the race display's formats, and their
// "none" placeholders as GT3 prints them: compared with the game program on any region's disc (GT3R_DISC), and laid out
// with the game fonts, whose long dash (code 0x7F) they draw, through the game-text path and the UTF-8 path alike.

#include "test_framework.h"

#include "disc/core_image.h"
#include "disc/disc.h"
#include "formats/text/charset.h"
#include "formats/text/fnt.h"
#include "formats/text/text_layout.h"
#include "race/hud.h"
#include "race/machine_test.h"
#include "race/time_text.h"
#include "disc/volume.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::race;

namespace {

// The program's words and strings (offsets into the image, addresses as loaded).
struct Code {
    const CoreImage& c;
    u32 word(size_t o) const { return o + 4 <= c.program.size() ? rd32(ByteSpan(c.program.data(), c.program.size()), o) : 0; }
    // Offsets of the 4-aligned runs of `words` whose bits under `masks` match.
    std::vector<size_t> find(const std::vector<u32>& words, const std::vector<u32>& masks) const {
        std::vector<size_t> out;
        for (size_t o = 0; o + 4 * words.size() <= c.program.size(); o += 4) {
            bool ok = true;
            for (size_t k = 0; k < words.size() && ok; ++k) ok = (word(o + 4 * k) & masks[k]) == (words[k] & masks[k]);
            if (ok) out.push_back(o);
        }
        return out;
    }
    // The NUL-terminated string at a program address ("" outside the image).
    std::string str(u32 addr) const {
        std::string s;
        for (size_t o = addr - c.load_address; addr >= c.load_address && o < c.program.size() && c.program[o]; ++o)
            s.push_back(char(c.program[o]));
        return s;
    }
    // The address an upper-half load and an add to that register build within `n` words from `o` (0: none).
    u32 address_after(size_t o, int n) const {
        for (int k = 0; k < n; ++k) {
            const u32 w = word(o + 4 * size_t(k));
            if (w >> 26 != 0x0F) continue;
            const u32 rt = (w >> 16) & 31;
            for (int j = k + 1; j < k + 6; ++j) {
                const u32 w2 = word(o + 4 * size_t(j));
                if (w2 >> 26 == 0x09 && ((w2 >> 21) & 31) == rt) return ((w & 0xFFFF) << 16) + u32(s32(s16(w2 & 0xFFFF)));
            }
        }
        return 0;
    }
};

std::optional<CoreImage> disc_core() {
    Disc* disc = test::any_region_disc();
    if (!disc) return std::nullopt;
    return CoreImage::decode(disc->read_core_gt3());
}

bool program_has(const CoreImage& c, std::string_view s) {
    std::string z(s);
    z.push_back('\0');
    const auto it = std::search(c.program.begin(), c.program.end(), z.begin(), z.end());
    return it != c.program.end() && (it - c.program.begin()) % 4 == 0; // (the strings start on a word)
}

std::vector<u16> codes(const std::vector<text::GlyphRef>& g) {
    std::vector<u16> out;
    for (const text::GlyphRef& r : g) out.push_back(r.code);
    return out;
}

} // namespace

TEST(race_time_text_format) {
    // The menus' format: unpadded minutes, hours from 60 minutes on, the placeholder for exactly 359,999,999 ms.
    CHECK(menu_time_text(u32(0)) == "0'00.000" && menu_time_text(u32(62400)) == "1'02.400");
    CHECK(menu_time_text(u32(3599999)) == "59'59.999" && menu_time_text(u32(3600000)) == "1:00'00.000");
    CHECK(menu_time_text(u32(7384005)) == "2:03'04.005");
    CHECK(menu_time_text(u32(359999998)) == "99:59'59.998" && menu_time_text(u32(359999999)) == kNoTimeText);
    CHECK(menu_time_text(u32(360000000)) == "100:00'00.000");
    CHECK(menu_time_text(85.5534) == "1'25.553" && menu_time_text(-1.0) == kNoTimeText);
    CHECK(menu_time_text(359999.999) == kNoTimeText && menu_time_text(std::numeric_limits<double>::quiet_NaN()) == kNoTimeText);
    // The race display's: one dash fewer for the minutes, clamped instead of hours.
    CHECK(Hud::format_time(-1) == kHudNoTimeText && Hud::format_time(359999.999) == kHudNoTimeText);
    CHECK(Hud::format_time(7384.005) == "123'04.005");
    // The placeholders: the fonts' long dash, never the hyphen.
    for (std::string_view s : {kNoTimeText, kHudNoTimeText, kNoSpeedText, kNoDateText}) CHECK(s.find('-') == std::string_view::npos);
    CHECK(kNoTimeText == "\x7F\x7F'\x7F\x7F.\x7F\x7F\x7F" && kHudNoTimeText == "\x7F'\x7F\x7F.\x7F\x7F\x7F");
    CHECK(Hud::speed_text(0, false) == kNoSpeedText && record_speed_text(0, false) == std::string(kNoSpeedText) + "km/h");
}

// Our placeholders are the game's (any region): the menus' "no time" where its time formatter takes it from after the test
// for 359,999,999 ms, the race display's as its formatter puts it together from three halfwords, and the no-speed and
// no-date placeholders among the program's strings (the NTSC-J program also has its speed placeholder with "km/h").
TEST(race_time_text_disc_program) {
    const std::optional<CoreImage> c = disc_core();
    if (!c) SKIP("needs GT3R_DISC");
    const Code code{*c};
    // The menus' formatter: the test for 359,999,999 (0x157529FF, its lower half here, the upper half two words before),
    // then the placeholder's address.
    const std::vector<size_t> menu = code.find({0x00A0182D, 0x344229FF, 0x14620000, 0x0080502D},
                                               {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF0000, 0xFFFFFFFF});
    CHECK(menu.size() == 1);
    if (!menu.empty()) {
        CHECK((code.word(menu[0] - 8) & 0xFFFF0000) == 0x3C020000 && (code.word(menu[0] - 8) & 0xFFFF) == 0x1575);
        const u32 at = code.address_after(menu[0] + 16, 4);
        std::printf("  menus' no time at %08X\n", at);
        CHECK(at && code.str(at) == kNoTimeText);
    }
    // The race display's: the same test, then three halfwords D, A and B put together as B, D, A, D (little-endian),
    // a NUL after them.
    const std::vector<size_t> hud = code.find({0x3C021575, 0x344229FF, 0x14A20000, 0x3C020000, 0x24030000, 0x70031EE9,
                                               0x3C020000, 0x34420000, 0x70621D88},
                                              {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF0000, 0xFFFF0000, 0xFFFF0000, 0xFFFFFFFF,
                                               0xFFFF0000, 0xFFFF0000, 0xFFFFFFFF});
    CHECK(hud.size() == 1);
    if (!hud.empty()) {
        const u16 d = u16(code.word(hud[0] + 16)), a = u16(code.word(hud[0] + 24)), b = u16(code.word(hud[0] + 28));
        std::string s;
        for (u16 h : {b, d, a, d}) s += {char(h & 0xFF), char(h >> 8)};
        CHECK(s == kHudNoTimeText);
    }
    CHECK(program_has(*c, kNoSpeedText) || program_has(*c, std::string(kNoSpeedText) + "km/h"));
    CHECK(program_has(*c, kNoDateText));
}

// The game's text fonts (system.fnt: sw_12, sw_16, sw_20, sq_9; menu.fnt holds the meters' digit fonts) draw the
// placeholders with their long dash: code 0x7F, wider than '-'. The game-text path and the UTF-8 path lay them out alike,
// each dash cut from the 0x7F glyph; the same texts with hyphens lay out otherwise.
TEST(race_time_text_disc_fonts) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Volume& vol = disc->volume();
    const text::FontFile file = text::FontFile::parse(vol.read(*vol.find("font/system.fnt")));
    CHECK(file.fonts.size() == 4);
    for (const text::LatinFont& f : file.fonts) {
        CHECK(f.has(0x7F) && f.has('-'));
        if (!f.has(0x7F) || !f.has('-')) continue;
        const text::FontGlyph& dash = *f.glyph(0x7F);
        const text::FontGlyph& hyphen = *f.glyph('-');
        std::printf("  %s: dash %u px wide, hyphen %u\n", f.name.c_str(), dash.width, hyphen.width);
        CHECK(dash.width > hyphen.width);
        for (std::string_view s : {kNoTimeText, kHudNoTimeText, kNoSpeedText, kNoDateText}) {
            const ByteSpan bytes(reinterpret_cast<const u8*>(s.data()), s.size());
            const std::vector<text::GlyphRef> game = text::map_game_text(bytes);
            const std::vector<text::GlyphRef> uni = text::map_unicode(text::utf8_decode(s), f, nullptr, nullptr);
            CHECK(codes(game) == codes(uni));
            const text::TextLayout lg = text::layout_text(game, f, nullptr, 0, 0, {});
            const text::TextLayout lu = text::layout_text(uni, f, nullptr, 0, 0, {});
            CHECK(lg.quads.size() == s.size() && lg.width == lu.width);
            size_t dashes = 0;
            for (const text::GlyphQuad& q : lg.quads)
                if (q.code == 0x7F) dashes += q.sw == dash.width && q.sx == dash.atlas_x && q.sy == dash.atlas_y;
            CHECK(dashes == size_t(std::count(s.begin(), s.end(), kDash)));
            std::string hyphens(s);
            std::replace(hyphens.begin(), hyphens.end(), kDash, '-');
            const text::TextLayout lh = text::layout_text(text::map_unicode(text::utf8_decode(hyphens), f, nullptr, nullptr), f,
                                                          nullptr, 0, 0, {});
            CHECK(lh.quads.size() == s.size() && lh.width != lg.width);
        }
    }
}
