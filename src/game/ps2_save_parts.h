#pragma once

// The pieces Gran Turismo 3's and Gran Turismo Concept's memory-card saves share (gt_save.cpp, concept_save.cpp):
// the container (0x40-byte header, inverted payload, CRC-32), the garage car record and the 0x300-byte ranking
// record (gt_mode §12). Internal to the save code.

#include "game/gt_mode.h"

namespace gt3r::game::ps2save {

constexpr size_t kHeader = 0x40, kBlock = 0x130, kCarRec = 0x204, kOptions = 0x888, kRanking = 0x300,
                 kLicenseRec = 0x154;
// "No time" in a ranking.
constexpr u32 kEmptyTime = 0x157529FF;

u32 crc32(ByteSpan d);
void wr16(Bytes& b, size_t o, u16 v);
void wr32(Bytes& b, size_t o, u32 v);
void wr64(Bytes& b, size_t o, u64 v);
float rdf(ByteSpan d, size_t o);
void wrf(Bytes& b, size_t o, float f);
// A fixed field's text up to its first 0.
std::string fixed_str(ByteSpan d, size_t o, size_t n);
// A ranking entry's 32-byte block: 30 '-' when unused ("" here).
std::string read_code(ByteSpan d, size_t o);
void put_code(Bytes& p, size_t o, const std::string& code);
void put_dashes(Bytes& p, size_t o);

// The payload of a save file of version `major`.x, as the game holds it (the stored bytes inverted). Throws
// FormatError (`what` names the file in the message) on a short file, another major version or a CRC mismatch.
Bytes open_payload(ByteSpan file, u16 major, const char* what);
// The file of a payload: header, the bytes inverted, padded to 64 bytes.
Bytes close_payload(Bytes payload, u16 major, u16 minor);

// A garage car record (0x204 bytes: the car setup record, the owned goods, the car's condition and serial).
GarageCar read_garage_car(ByteSpan r, const data::CarDatabase& db);
void write_garage_car(Bytes& p, size_t o, const GarageCar& c, const data::CarDatabase& db);

// A course time-trial or machine-test ranking (0x300 bytes, 12 entries).
std::vector<RankEntry> read_ranking(ByteSpan d, size_t r, bool speed, const data::CarDatabase& db);
void write_ranking(Bytes& p, size_t r, const std::vector<RankEntry>* rank, bool speed);

// The options block (0x888 bytes) of a new game, for a state that never came from a memory card.
Bytes default_options(const GtData& data);
// A new game's music list in an options block (+0x228): the disc's race songs shuffled from `seed`.
void put_new_music_list(Bytes& options, const GtData& data, u16 seed);

} // namespace gt3r::game::ps2save

namespace gt3r::game {

// Gran Turismo Concept's game save (concept_save.cpp; gt_save.h's import_ps2_save / export_ps2_save hand over to
// these on a Concept disc).
GameState import_concept_ps2_save(ByteSpan file, const GtData& data);
Bytes export_concept_ps2_save(const GameState& s, const GtData& data);
Bytes concept_ps2_thumbnail(const GameState& s, const GtData& data);
// The save's title for icon.sys (Shift-JIS), from GtData::save_title.
Bytes concept_save_title_sjis(const GtData& data);
// The cars that can be chosen (the save thumbnail's count).
int concept_open_car_count(const GtData& data, const ConceptState& state);

} // namespace gt3r::game
