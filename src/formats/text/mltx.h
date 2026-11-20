#pragma once

// "MLTX" multi-language string tables (text/realtime.dat).
//
//   0x00 "MLTX"
//   0x04 u32 version (2)
//   0x08 u32 pool offset          -> language 0's string pool
//   0x0C u32 entry count
//   0x10 u32 pool size            -> language L's pool is at pool_offset + L * pool_size
//   0x14 entries[count]           {u32 key offset (absolute), u32 value offset (in pool)}
//   ...  keys (NUL-terminated ASCII, sorted)
//   ...  7 pools of identical layout (each value is padded so all languages
//        share the same offsets)
//
// Languages are indexed in the game's order: JP, US, BR (British English),
// FR, GR (German), IT, SP. An empty value means "keep the default text built
// into the program" for that key; on PBPX-955.03 only JP and US are complete,
// BR is partial and FR..SP are blank. Values use the game encoding
// (see charset.h).
//
// Later discs of this engine add languages, and their files hold more pools (the file holds
// as many whole pools as fit after the pool offset). A file's pool count tells its order:
//
//   pools   order after SP
//   7       -
//   8, 9    KR, TW         (the program of that time has KR as its eighth language)
//   11      PT, KR, CN, TW (the later programs' order, which Language follows)
//
// KR values are EUC-KR and TW values Big5 characters renumbered (charset.h "Other double-byte
// text"); both draw with their own cell font (fnt.h kanji_font_path). The CN pool is empty on
// the discs seen, PT is partial.

#include "base/bytes.h"
#include "formats/text/charset.h"

#include <optional>
#include <string>
#include <vector>

namespace gt3r::text {

// GT3's seven, then the ones later discs of this engine add, in their programs' order.
enum class Language : u8 { JP, US, BR, FR, GR, IT, SP, PT, KR, CN, TW };
// GT3's own languages (JP..SP).
constexpr int kLanguageCount = 7;
// Every language of the enum.
constexpr int kAllLanguageCount = 11;
// "JP", "US", "BR", ... (also the menu/<LANG>/ folder names).
const char* language_code(Language l);
// "Japanese", "English (US)", ...
const char* language_name(Language l);
std::optional<Language> language_from_code(std::string_view code);

class StringTable {
public:
    static bool is_mltx(ByteSpan data) { return has_magic(data, "MLTX"); }
    static StringTable parse(ByteSpan data);

    size_t size() const { return keys_.size(); }
    // The number of pools.
    int language_count() const { return int(values_.size()); }
    // The language of each pool, in file order (see the table above).
    const std::vector<Language>& languages() const { return languages_; }
    bool has(Language l) const { return pool_of(l) >= 0; }
    const std::string& key(size_t i) const { return keys_.at(i); }
    // Raw game-encoded value ("" = use the built-in default; also for a language the file
    // has no pool for).
    const std::string& raw(size_t i, Language l) const;
    std::string utf8(size_t i, Language l, const JisTable* jis) const { return game_text_to_utf8(raw(i, l), jis); }
    // Number of non-empty values for a language.
    size_t filled(Language l) const;
    // Entry index for a key, or -1.
    int find(std::string_view key) const;

private:
    int pool_of(Language l) const;

    std::vector<Language> languages_; // [pool]
    std::vector<std::string> keys_;
    std::vector<std::vector<std::string>> values_; // [pool][entry]
};

} // namespace gt3r::text
