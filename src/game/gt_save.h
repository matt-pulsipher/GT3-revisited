#pragma once

// GT Mode saves (docs/dev/GT_MODE.md "Save format").
//
// Native format: a versioned UTF-8 text file, one record per line
// ("key value..." tokens separated by spaces; strings never contain
// spaces). Written atomically (temporary file + rename); the saves live in
// <user data dir>/saves/ (save_library.h). Unknown keys are ignored when
// loading, so newer versions can add records; a car's settings line may hold
// fewer values (an older save: the rest keep their defaults) or more (ignored).
// A save of a newer version is refused (FormatError).
//
// Import: the original PS2 memory-card file
// BASCUS-97102GAMEDATA/BASCUS-97102GAMEDATA (save version 7.1: 0x40-byte
// header, payload bytes inverted, zlib CRC-32), see docs/FORMATS.md "PS2 memory-card saves".

#include "game/gt_mode.h"

#include <optional>
#include <string>

namespace gt3r::game {

constexpr int kSaveVersion = 1;

std::string save_to_text(const GameState& s);
// Throws FormatError on malformed input or an unsupported version.
GameState load_from_text(const std::string& text);

// The user data folder (with a trailing separator): SDL_GetPrefPath, or the executable's
// folder in portable mode (portable.ini beside it; on macOS the folder holding gt3r.app first),
// or GT3R_DATA_DIR when set; the app keeps mods, config, saves and replays there.
std::string user_data_dir();
// The folder holding a macOS app bundle whose base path (SDL_GetBasePath) is `base`
// (".../gt3r.app/Contents/Resources/"), with a trailing '/'; "" when `base` is not a bundle's.
std::string app_bundle_folder(const std::string& base);
bool write_save(const std::string& path, const GameState& s, std::string* error = nullptr);
// nullopt when there is no save at `path` or it cannot be read (damaged, or from a newer
// version); `error` says which, host_path_exists() tells them apart.
std::optional<GameState> read_save(const std::string& path, std::string* error = nullptr);
// Moves the file at `path` aside to "<path>.unreadable-<yyyymmdd>-<hhmmss>" ("-2", "-3"...
// when taken; copied when it cannot be moved), keeping its bytes: a save that cannot be read
// goes there before a new game is written to its path (GtModeSession::load). The save
// library lists *.sav files only. Returns the new path, "" on failure (`error`).
std::string set_aside_save(const std::string& path, std::string* error = nullptr);

// Decodes an original GT3 (NTSC-U) GAMEDATA file. Needs the disc data for
// the race-code / prize-key orders and the part tables. Throws FormatError.
GameState import_ps2_save(ByteSpan file, const GtData& data);
// The inverse, for tests and for moving progress back to the PS2: builds a
// GAMEDATA file from a state (fields this reimplementation does not track
// are written as a new game has them).
Bytes export_ps2_save(const GameState& s, const GtData& data);

// A Gran Turismo Concept disc's game save (concept §11: version 100.x, its own blocks) goes through the same
// calls: import_ps2_save, export_ps2_save and the file-level ones below read and write the save of the disc's game.
// The save directory (and data file) of the disc's release: ps2_game_dir(region) for Gran Turismo 3, a Concept
// release's own (BESCES-50858GAMEDATA for the European Tokyo-Geneva).
std::string ps2_save_name(const GtData& data);

// The original's memory-card save directory for the game file (gt_mode §12.2):
// BASCUS-97102GAMEDATA/ {BASCUS-97102GAMEDATA, icon.sys, icon0.ico, thumbnail}. Each release
// has its own and reads only that one: BESCES-50294GAMEDATA (PAL), BISCPS-15009GAMEDATA (NTSC-J).
constexpr const char* kPs2GameDir = "BASCUS-97102GAMEDATA";
const char* ps2_game_dir(Region r);
struct Ps2SaveFile {
    std::string name;
    Bytes data;
};
// Every file of the directory, in the order the original writes them (icon0.ico,
// icon.sys, the data file, thumbnail). The icon files come from the disc
// (menu/icon/icon.sys is the template the game fills in, menu/icon/ICON_a_spec.ico
// the icon); `read` reads GT3.VOL paths. Throws FormatError when they are missing.
std::vector<Ps2SaveFile> export_ps2_save_files(const GameState& s, std::shared_ptr<const GtData> data,
                                               const GtData::Reader& read);
// File-level import / export for players (gt3tool save-import / save-export; the
// game's file picker). Supported containers: the data file itself
// (BASCUS-97102GAMEDATA, under any name, e.g. a .raw from a save editor), its save
// directory, a PCSX2 folder memory card (the folder holding BASCUS-97102GAMEDATA/), an
// EMS / uLaunchELF .psu and a raw 8 MB card image (PCSX2 .ps2 / .mcd, with or without
// ECC). Throws FormatError.
GameState import_ps2_save_path(const std::string& path, const GtData& data);

// The single-file exports: a .psu, a new 8 MB card image with ECC holding only this save,
// or the data file alone (the "raw" save that PS2 save editors such as GT3SaveEditor
// open: export_ps2_save, without the icons and thumbnail).
enum class Ps2Export { Psu, Card, Raw };
// The format a file name asks for: ".ps2" / ".mcd" a card image, ".raw" the data file,
// anything else a .psu.
Ps2Export ps2_export_format(const std::string& name);
// `out` ending in .psu, .ps2 / .mcd or .raw: that file (ps2_export_format); otherwise a
// folder: <out>/BASCUS-97102GAMEDATA/... (e.g. a PCSX2 folder memory card). Throws
// FormatError.
void export_ps2_save_path(const GameState& s, std::shared_ptr<const GtData> data, const GtData::Reader& read,
                          const std::string& out);

// The same as one file in `format`, e.g. for a path from a save dialog that has no
// extension. Throws FormatError.
Bytes export_ps2_save_container(const GameState& s, std::shared_ptr<const GtData> data, const GtData::Reader& read,
                                Ps2Export format);

// The game save's options block (0x888 bytes, gt_mode §12.2) fields of view mode 1's replay pad
// (presentation §G3): +0x114 the race-sound level of the music's marked sections (Up / Down,
// a new game 0.1), +0x118 the music-timed cuts (Right / Left, 0 off, a new game). Ours keeps both
// in its config (race::ReplaySettings::level / music_cuts), as the other option fields; the
// block itself travels with an imported save (GameState::ps2_options) unchanged.
struct Ps2ReplayPad {
    float level = 0.1f, music_cuts = 0.0f;
};
Ps2ReplayPad ps2_replay_pad(ByteSpan options);              // (a short block: the new-game values)
void set_ps2_replay_pad(Bytes& options, const Ps2ReplayPad& p); // (grown to 0x888 if short)

// The 0x60-byte thumbnail the file list shows without loading the data file.
Bytes ps2_thumbnail(const GameState& s, std::shared_ptr<const GtData> data);

// The original's car setup file (memory card BASCUS-97102S…: version
// 1.0x827, payload = one 0x1D0-byte car setup record, same header,
// inversion and CRC as the game data). Throws FormatError.
SettingFile import_ps2_setting(ByteSpan file, const GtData& data);
// One car setup record (0x1D0 bytes; also the head of the car records in the game's replay
// saves, e.g. the disc's demo runs): the car label, its fitted parts and user settings.
void decode_car_equipments(ByteSpan r, const data::CarDatabase& db, std::string& car, std::array<u64, 27>& parts,
                           sim::CarSettings& settings, int& ref_final);
Bytes export_ps2_setting(const SettingFile& f, const GtData& data);

} // namespace gt3r::game
