#include "formats/text/mltx.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace gt3r::text {

namespace {

constexpr const char* kCodes[kAllLanguageCount] = {"JP", "US", "BR", "FR", "GR", "IT", "SP", "PT", "KR", "CN", "TW"};
constexpr const char* kNames[kAllLanguageCount] = {
    "Japanese", "English (US)", "English (UK)", "French",               "German",               "Italian",
    "Spanish",  "Portuguese",   "Korean",       "Chinese (Simplified)", "Chinese (Traditional)"};
// The pools after SP of a file with fewer pools than the enum has languages.
constexpr Language kEarlyOrder[] = {Language::KR, Language::TW};

std::string cstring_at(ByteSpan d, size_t off, size_t end) {
    if (off >= end) throw FormatError("MLTX string offset out of range");
    size_t e = off;
    while (e < end && d[e]) ++e;
    return std::string(reinterpret_cast<const char*>(d.data() + off), e - off);
}

} // namespace

const char* language_code(Language l) { return kCodes[size_t(l) % kAllLanguageCount]; }
const char* language_name(Language l) { return kNames[size_t(l) % kAllLanguageCount]; }

std::optional<Language> language_from_code(std::string_view code) {
    for (int i = 0; i < kAllLanguageCount; ++i)
        if (code == kCodes[i]) return Language(i);
    return std::nullopt;
}

StringTable StringTable::parse(ByteSpan d) {
    if (!is_mltx(d)) throw FormatError("not an MLTX string table");
    if (rd32(d, 4) != 2) throw FormatError("unsupported MLTX version");
    u32 pool = rd32(d, 8), count = rd32(d, 12), pool_size = rd32(d, 16);
    if (pool > d.size() || 0x14 + u64(count) * 8 > pool) throw FormatError("MLTX header out of range");
    StringTable t;
    // The file holds as many complete pools as fit (7 on GT3's discs).
    int langs = pool_size ? int(std::min<u64>(kAllLanguageCount, (d.size() - pool) / pool_size)) : 0;
    if (langs == 0) throw FormatError("MLTX has no language pools");
    // Fewer pools than the enum has languages: the earlier order (a pool past it is not read).
    const bool early = langs > kLanguageCount && langs < kAllLanguageCount;
    if (early) langs = std::min(langs, kLanguageCount + int(std::size(kEarlyOrder)));
    for (int l = 0; l < langs; ++l)
        t.languages_.push_back(early && l >= kLanguageCount ? kEarlyOrder[l - kLanguageCount] : Language(l));
    t.values_.resize(size_t(langs));
    for (u32 i = 0; i < count; ++i) {
        u32 key = rd32(d, 0x14 + i * 8), value = rd32(d, 0x18 + i * 8);
        t.keys_.push_back(cstring_at(d, key, pool));
        for (int l = 0; l < langs; ++l) {
            size_t base = pool + size_t(l) * pool_size;
            t.values_[l].push_back(cstring_at(d, base + value, base + pool_size));
        }
    }
    return t;
}

int StringTable::pool_of(Language l) const {
    for (size_t i = 0; i < languages_.size(); ++i)
        if (languages_[i] == l) return int(i);
    return -1;
}

const std::string& StringTable::raw(size_t i, Language l) const {
    static const std::string none;
    int p = pool_of(l);
    if (p < 0) {
        if (i >= keys_.size()) throw std::out_of_range("MLTX entry");
        return none;
    }
    return values_[size_t(p)].at(i);
}

size_t StringTable::filled(Language l) const {
    int p = pool_of(l);
    if (p < 0) return 0;
    const auto& v = values_[size_t(p)];
    return size_t(std::count_if(v.begin(), v.end(), [](const std::string& s) { return !s.empty(); }));
}

int StringTable::find(std::string_view key) const {
    for (size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i] == key) return int(i);
    return -1;
}

} // namespace gt3r::text
