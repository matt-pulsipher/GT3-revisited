#pragma once

// GT3's in-game text encoding and conversion to/from Unicode.
//
// Game strings (realtime.dat, menu .pmb files, ...) are byte strings that the
// text renderer walks as follows (see docs/formats/text.md):
//
//   b < 0x80             one byte, ASCII. '\n' starts a new line.
//   0x80 <= b <= 0x8F    two bytes {0x80+N, c}: character c of ISO-8859-N
//                        (N = 0 means "raw font code", i.e. Latin-1 plus the
//                        Windows-1252 symbols at 0x80..0x9F).
//   0xA1 <= b <= 0xFE    two bytes, EUC-JP: JIS X 0208 code (b0 & 0x7F, b1 & 0x7F),
//                        drawn with the kanji font.
//
// Latin fonts are indexed by "font code": Unicode for U+0020..U+017F except
// that 0x80..0x9F hold the Windows-1252 symbols (0x80 is the euro sign).
// JIS codes are converted to Unicode with the disc's own table,
// font/jis2uni.dat ("UTBL"), parsed by JisTable.

#include "base/bytes.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace gt3r::text {

constexpr char32_t kReplacementChar = 0xFFFD;
constexpr u16 kNoCode = 0xFFFF;

// font/jis2uni.dat: "UTBL" JIS X 0208 (plus Polyphony's Latin/Greek/Cyrillic
// extensions in rows 0x22..0x2B) and Shift-JIS lead rows -> Unicode.
//
//   0x00 "UTBL"
//   0x04 u16 row count, u16 total entries
//   0x08 u32 row table offset   -> {u8 first_low_byte, u8 high_byte, u16 first_index}
//   0x0C u32 data offset        -> u16 Unicode values; row i covers
//                                  [first_index(i), first_index(i+1)) and
//                                  code (high << 8 | first_low + k)
// Only rows with a high byte in 0x21..0x7E (JIS) are kept.
class JisTable {
public:
    static bool is_utbl(ByteSpan data) { return has_magic(data, "UTBL"); }
    static JisTable parse(ByteSpan data);

    // 0 if unmapped. `jis` is the 7-bit form (0x2121..0x7E7E).
    char32_t to_unicode(u16 jis) const;
    // 0 if Unicode `c` has no JIS code.
    u16 from_unicode(char32_t c) const;
    size_t size() const { return forward_.size(); }
    bool empty() const { return forward_.empty(); }

private:
    std::unordered_map<u16, char32_t> forward_;
    std::unordered_map<char32_t, u16> reverse_;
};

// Latin font code <-> Unicode.
char32_t font_code_to_unicode(u16 code);
// kNoCode if `c` is outside the Latin font code space (> U+017F and not a
// Windows-1252 symbol).
u16 unicode_to_font_code(char32_t c);

// The game's {0x80+page, c} escape: page 0..15, c 0x20..0xFF -> font code,
// kNoCode when unmapped. Pages 10..13 fall back to Latin-1 like the game.
u16 iso8859_to_font_code(unsigned page, u8 c);

// Decodes a game string (stops at the first NUL or the end of the span).
// Unmapped characters become U+FFFD. Without a JIS table, EUC-JP pairs also
// become U+FFFD.
std::u32string decode_game_text(ByteSpan s, const JisTable* jis);
std::string game_text_to_utf8(ByteSpan s, const JisTable* jis);
inline std::string game_text_to_utf8(std::string_view s, const JisTable* jis) {
    return game_text_to_utf8(ByteSpan(reinterpret_cast<const u8*>(s.data()), s.size()), jis);
}

// Game text's characters: a byte below 0x80 is one, every other byte starts a two-byte one
// (stops at the first NUL like the renderer). game_text_prefix keeps the first `chars`
// characters, never half of a pair.
size_t game_text_length(std::string_view s);
std::string game_text_prefix(std::string_view s, size_t chars);

// Encodes Unicode text back into the game's encoding: ASCII as-is, Latin as
// {0x81, c} (ISO-8859-1) or {0x80, code} (font code), anything with a JIS code
// as EUC-JP. Characters with no representation become '?'.
std::string encode_game_text(std::u32string_view s, const JisTable* jis);

// Minimal UTF-8 helpers. Invalid sequences decode to U+FFFD.
std::u32string utf8_decode(std::string_view s);
void utf8_append(std::string& out, char32_t c);
std::string utf8_encode(std::u32string_view s);

} // namespace gt3r::text
