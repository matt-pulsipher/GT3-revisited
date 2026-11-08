#pragma once

// The saved replays as the Replay Theater lists them: native files in one folder per disc key
// (DiscInfo::key), <save folder>/replays/ (NTSC-U; replays/jp/, replays/eu/ for the other
// releases, whose cars and AI lines differ; replays/<serial>/ for a disc of its own, such as a
// modified copy under another serial; the save folder: game/save_folder.h), where every save from
// a race lands (time trial, arcade, licence, GT mode, quick race, 2P). No slots and no capacity:
// as many as the disk holds.

#include "disc/region.h"
#include "race/replay.h"

#include <string>
#include <string_view>
#include <vector>

namespace gt3r::race {

// <save_dir>/replays for the disc key "us", else <save_dir>/replays/<disc key>.
std::string replay_dir(const std::string& save_dir, std::string_view disc_key);
// The folder of a region's releases (disc key = region key).
std::string replay_dir(const std::string& save_dir, Region region = Region::America);
// First use of a disc of its own (a disc key that is not its region's): when its folder does not
// exist yet it is created and the region's replays (its subfolders not included) are copied in.
// Earlier versions saved every disc's replays in its region's folder. Returns the number copied.
int seed_replay_dir(const std::string& save_dir, std::string_view disc_key, Region region);

struct ReplayEntry {
    std::string path, name; // full path / file name
    u64 size = 0;           // bytes
    u32 ticks = 0;
    Replay header;          // everything but the inputs (header.inputs / inputs2 are empty)
    bool too_old = false;   // a file of a version before 12: listed (it can be deleted), not played (header empty)
};

// The folder's replays, newest first (unreadable files are skipped; those of older versions are listed as too_old).
std::vector<ReplayEntry> list_replay_entries(const std::string& dir);
// Reads a whole replay; nullopt (and *error) when it cannot be read.
std::optional<Replay> read_replay_file(const std::string& path, std::string* error = nullptr);

bool delete_replay_file(const std::string& path);
// Rewrites the file with a new comment (the file name the theater shows).
bool rename_replay_file(const std::string& path, const std::string& comment);

// Info panel texts: "YYYY/MM/DD" (saved time, else the file name's stamp), "123KB", m:ss.mmm.
std::string replay_date(const ReplayEntry& e);
std::string replay_size_text(u64 bytes);
std::string replay_time_text(double seconds); // "-:--.---" when < 0
// The file name shown: the comment, else the file's name without its extension.
std::string replay_display_name(const ReplayEntry& e);

} // namespace gt3r::race
