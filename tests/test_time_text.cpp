// Time, speed and date texts (race/time_text.h, Hud::format_time): the menus' and the race display's formats, and their
// "none" placeholders as GT3 prints them: compared with the game program on any region's disc (GT3R_DISC), and laid out
// with the game fonts, whose long dash (code 0x7F) they draw, through the game-text path and the UTF-8 path alike.

#include "test_framework.h"

#include "disc/core_image.h"
#include "disc/disc.h"
#include "disc/volume.h"
#include "formats/text/charset.h"
#include "formats/text/fnt.h"
#include "formats/text/text_layout.h"
#include "race/hud.h"
#include "race/machine_test.h"
#include "race/time_text.h"
#include "sim/exact/program_locate.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::race;

namespace {

// The time formatters' routines, known by the hash of their masked code (sim/exact/program_locate.h; the same in every
// region's program), and the positions in them of the instructions the test reads: the "no time" value's two halves, and
// the menus' placeholder address (its two halves) or the race display's three immediate halfwords.
struct Routine {
    u64 hash;
    u32 words;
};
constexpr Routine kMenuTime{0xCE487E5DFA4A90B0ull, 53}; // halves of "no time" at 1 and 4, of the placeholder at 7 and 8
constexpr Routine kHudTime{0x2754C6956C292648ull, 93};  // halves of "no time" at 2 and 3, the text's halfwords at 6, 8, 9

// The 16-bit immediate of the instruction at `at` + 4 `k`.
u32 imm(const sim::exact::CodeIndex& x, u32 at, u32 k) { return x.word(at + 4 * k) & 0xFFFF; }

// The NUL-terminated string at a program address ("" outside the image).
std::string cstring(const sim::exact::CodeIndex& x, u32 addr) {
    std::string s;
    for (; x.in_image(addr) && x.bytes(addr, 1)[0]; ++addr) s.push_back(char(x.bytes(addr, 1)[0]));
    return s;
}

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
    const sim::exact::CodeIndex x(ByteSpan(c->program.data(), c->program.size()), c->load_address, c->entry_point);
    const std::vector<u32> menu = x.find(kMenuTime.hash, kMenuTime.words);
    CHECK(menu.size() == 1);
    if (menu.size() == 1) {
        const u32 at = menu[0];
        CHECK((imm(x, at, 1) << 16 | imm(x, at, 4)) == 359999999);
        const u32 text = (imm(x, at, 7) << 16) + u32(s32(s16(imm(x, at, 8))));
        CHECK(cstring(x, text) == kNoTimeText);
    }
    const std::vector<u32> hud = x.find(kHudTime.hash, kHudTime.words);
    CHECK(hud.size() == 1);
    if (hud.size() == 1) {
        const u32 at = hud[0];
        CHECK((imm(x, at, 2) << 16 | imm(x, at, 3)) == 359999999);
        const u32 d = imm(x, at, 6), a = imm(x, at, 8), b = imm(x, at, 9);
        std::string s;
        for (u32 h : {b, d, a, d}) s += {char(h & 0xFF), char(h >> 8)}; // (little-endian, a NUL after them)
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
