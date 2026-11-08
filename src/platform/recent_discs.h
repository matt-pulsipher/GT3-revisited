#pragma once

// The disc library: remembered disc images, [game] in config.ini:
//
//   disc    = /path/to/current.iso   ; opened at startup
//   recent1 = /path/to/current.iso   ; most recently opened first
//   recent2 = D:\Games\GT3 (PAL).iso
//   ...                              ; up to kMaxLibraryDiscs
//
// One key per path, so paths may contain spaces, commas or '='. Paths are
// stored absolute (UTF-8); URIs such as Android's content:// are kept as
// given. What each image is (DiscRecord) is kept from when it was last opened,
// in [disc_library], so the list can say it without opening every image.
// Pure logic (no SDL, no file system access) so it can be unit-tested
// on every platform with Windows- and POSIX-style paths alike.

#include "base/config.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r::platform {

constexpr int kMaxLibraryDiscs = 32;

// True for "/x", "C:\x", "C:/x", "\\server\share" and "scheme://..." paths.
bool is_absolute_disc_path(std::string_view path);

// Same file as far as a path comparison can tell: separators are unified,
// and Windows drive / UNC paths compare case-insensitively.
bool same_disc_path(std::string_view a, std::string_view b);

// [game] recent1..recentN (empty values and duplicates dropped).
std::vector<std::string> load_recent_discs(const IniDocument& cfg);
// Rewrites [game] recent1..recentN; removes keys past the list's end, and the records of images
// no longer in it.
void save_recent_discs(IniDocument& cfg, const std::vector<std::string>& list);

// `path` moved (or added) to the front, without duplicates, capped at kMaxLibraryDiscs.
std::vector<std::string> push_recent_disc(std::vector<std::string> list, const std::string& path);
// `list` without `path`.
std::vector<std::string> remove_recent_disc(std::vector<std::string> list, std::string_view path);

// What a library image is (DiscInfo, disc/disc.h), as of when it was last opened.
struct DiscRecord {
    std::string serial, version; // "PBPX_955.03", "1.10"
    std::string region;          // region_key: "us", "eu", "jp"
    std::string digest;          // DiscInfo::digest
    std::string key;             // DiscInfo::key (disc_key)
    std::string description;
    bool known = false;    // a release in the known list (verified or reported working)
    bool modified = false; // DiscInfo::modified
};
// [disc_library] <digest of the path> = serial|version|region|digest|key|flags|description
std::optional<DiscRecord> load_disc_record(const IniDocument& cfg, std::string_view path);
void save_disc_record(IniDocument& cfg, std::string_view path, const DiscRecord& r);
// For the list: "NTSC-U v1.10", "PAL v1.00, modified", "SCEF-50294 v1.00".
std::string disc_record_label(const DiscRecord& r);

// Where to look for a saved disc path, in order. Absolute paths and URIs are
// returned as they are. A relative path (old configs) is tried as given
// (i.e. relative to the working directory, resolved by the caller) and then
// relative to `base_dir` (the executable's directory, SDL_GetBasePath()).
std::vector<std::string> disc_path_candidates(const std::string& saved, const std::string& base_dir);

// File name part of a path ("GT3.iso" for "C:\Games\GT3.iso").
std::string disc_file_name(std::string_view path);

// iOS: `path` moved into the app folder of `data_dir`. Each install of an iOS app gets its own
// folder (/var/mobile/Containers/Data/Application/<UUID>/), so a path saved by an earlier install
// names a folder that no longer exists; the rest of it (Documents/GT3.iso) is the same file now.
// `path` unchanged when either is not in such a folder, or both are in the same one.
// Only for files of the app's own folder: another app's folder (a browser's downloads) is
// found again through the file's bookmark, never by moving its path into this one.
std::string rebase_app_container_path(std::string_view path, std::string_view data_dir);

// iOS: `a` and `b` are in the same app's folder (/var/mobile/Containers/Data/Application/<UUID>/,
// with or without /private in front). False when either is in no app folder.
bool same_app_container(std::string_view a, std::string_view b);

} // namespace gt3r::platform
