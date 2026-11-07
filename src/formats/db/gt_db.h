#pragma once

// Container formats used by database/*.db (see docs/formats/database.md).
//
//   GTAR  archive of GTDT tables          (paramdb*.db)
//   GTDT  fixed-size record table         (inside GTAR; racemode.db, racedetail.db)
//   STDB  string table                    (paramstr*.db, paramunistr*.db, .id_db_str*.db, carcolor.sdb)
//   IDDB  64-bit id -> string index map   (.id_db_idx*.db)
//
// Records reference each other by 64-bit "id codes", a hash of the
// record's label string (`id_hash`). The IDDB/STDB pair maps codes back to
// labels; the game itself only ever compares codes.

#include "base/bytes.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gt3r::db {

// The id code of a label: v = sum(bytes) & 0xFFFF; for each byte b: v = rotl64(v, 7) + b.
// Verified against all 20,714 entries of .id_db_idx.db.
u64 id_hash(std::string_view label);

// STDB: u32 count, u16 flags, u16 pad, u32 total size, u32 offsets[count];
// each string is u16 length + bytes (+NUL, padded to 2).
class StringTable {
public:
    static StringTable parse(ByteSpan data);
    size_t size() const { return strings_.size(); }
    // Empty string for out-of-range indices. Trailing NUL padding is stripped.
    const std::string& at(size_t i) const;
    const std::vector<std::string>& strings() const { return strings_; }
    // Index of `s`, appending it when missing (mod cars add their own names).
    size_t add(std::string_view s);

private:
    std::vector<std::string> strings_;
};

// IDDB: "IDDB", u32 count, then count x { u64 code, u64 string_index } sorted by code.
class IdIndex {
public:
    static IdIndex parse(ByteSpan data);
    std::optional<u32> find(u64 code) const;
    size_t size() const { return map_.size(); }

private:
    std::unordered_map<u64, u32> map_;
};

// GTDT: "GTDT", u16 version, u16 table index (0xFFFF when standalone),
// u16 record count, u16 record size, u32 byte size, then records.
// Every record starts with its own u64 id code.
struct Table {
    u16 version = 0;
    u16 index = 0;
    u16 record_size = 0;
    std::vector<ByteSpan> records; // views into the source buffer

    static Table parse(ByteSpan data);
    size_t size() const { return records.size(); }
    u64 id_of(size_t i) const { return rd64(records[i], 0); }
};

// GTAR: "GTAR", u32 table count, u32 data offset, u32 7 (GT3 clears these low bits of an offset; the discs' offsets are
// 16-byte aligned), u32 offsets[count + 1] relative to the data offset. As GT3 reads it, the tag is not checked.
std::vector<ByteSpan> parse_archive(ByteSpan data);

// Resolves id codes to labels using an IDDB + STDB pair, falling back to a
// cache of labels registered by the caller (any label can be hashed).
class IdNames {
public:
    void load(ByteSpan iddb, ByteSpan stdb);
    void add(std::string_view label) { extra_.emplace(id_hash(label), std::string(label)); }
    // Empty when unknown.
    std::string name(u64 code) const;
    bool empty() const { return index_.size() == 0 && extra_.empty(); }

private:
    IdIndex index_;
    StringTable strings_;
    std::unordered_map<u64, std::string> extra_;
};

} // namespace gt3r::db
