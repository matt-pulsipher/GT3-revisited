#include "formats/db/gt_db.h"

namespace gt3r::db {

u64 id_hash(std::string_view label) {
    // The game seeds with the byte sum truncated to 16 bits
    // (docs/formats/database.md "Id codes"); this only matters for labels whose byte sum exceeds 0xFFFF.
    u64 v = 0;
    for (unsigned char c : label) v += c;
    v &= 0xFFFF;
    for (unsigned char c : label) v = ((v << 7) | (v >> 57)) + c;
    return v;
}

StringTable StringTable::parse(ByteSpan d) {
    if (!has_magic(d, "STDB")) throw FormatError("not an STDB string table");
    u32 count = rd32(d, 4);
    if (count > d.size() / 4) throw FormatError("STDB: bad count");
    StringTable t;
    t.strings_.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        u32 off = rd32(d, 16 + size_t(i) * 4);
        u16 len = rd16(d, off);
        if (size_t(off) + 2 + len > d.size()) throw FormatError("STDB: string past end");
        std::string s(reinterpret_cast<const char*>(d.data() + off + 2), len);
        while (!s.empty() && s.back() == '\0') s.pop_back();
        t.strings_.push_back(std::move(s));
    }
    return t;
}

const std::string& StringTable::at(size_t i) const {
    static const std::string empty;
    return i < strings_.size() ? strings_[i] : empty;
}

size_t StringTable::add(std::string_view s) {
    for (size_t i = 0; i < strings_.size(); ++i)
        if (strings_[i] == s) return i;
    strings_.emplace_back(s);
    return strings_.size() - 1;
}

IdIndex IdIndex::parse(ByteSpan d) {
    if (!has_magic(d, "IDDB")) throw FormatError("not an IDDB index");
    u32 count = rd32(d, 4);
    if (size_t(count) * 16 + 8 > d.size()) throw FormatError("IDDB: truncated");
    IdIndex x;
    x.map_.reserve(count);
    for (u32 i = 0; i < count; ++i) x.map_.emplace(rd64(d, 8 + size_t(i) * 16), u32(rd64(d, 16 + size_t(i) * 16)));
    return x;
}

std::optional<u32> IdIndex::find(u64 code) const {
    auto it = map_.find(code);
    if (it == map_.end()) return std::nullopt;
    return it->second;
}

Table Table::parse(ByteSpan d) {
    if (!has_magic(d, "GTDT")) throw FormatError("not a GTDT table");
    Table t;
    t.version = rd16(d, 4);
    t.index = rd16(d, 6);
    u16 count = rd16(d, 8);
    t.record_size = rd16(d, 10);
    if (t.record_size < 8) throw FormatError("GTDT: record size too small");
    if (16 + size_t(count) * t.record_size > d.size()) throw FormatError("GTDT: truncated");
    t.records.reserve(count);
    for (u16 i = 0; i < count; ++i) t.records.push_back(d.subspan(16 + size_t(i) * t.record_size, t.record_size));
    return t;
}

std::vector<ByteSpan> parse_archive(ByteSpan d) {
    // As GT3 reads it: by the offsets alone, the tag unread (a modified game's tools may write another than
    // "GTAR").
    if (d.size() < 16) throw FormatError("GTAR: truncated");
    u32 count = rd32(d, 4), base = rd32(d, 8);
    if (count > 4096 || 20 + size_t(count) * 4 > d.size()) throw FormatError("GTAR: bad count");
    std::vector<ByteSpan> out;
    for (u32 i = 0; i < count; ++i) {
        size_t a = size_t(base) + rd32(d, 16 + size_t(i) * 4), b = size_t(base) + rd32(d, 20 + size_t(i) * 4);
        if (a > b || b > d.size()) throw FormatError("GTAR: bad table bounds");
        out.push_back(d.subspan(a, b - a));
    }
    return out;
}

void IdNames::load(ByteSpan iddb, ByteSpan stdb) {
    index_ = IdIndex::parse(iddb);
    strings_ = StringTable::parse(stdb);
}

std::string IdNames::name(u64 code) const {
    if (auto i = index_.find(code)) return strings_.at(*i);
    auto it = extra_.find(code);
    return it == extra_.end() ? std::string() : it->second;
}

} // namespace gt3r::db
