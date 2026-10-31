#pragma once

// The GT-mode save library (docs/dev/GT_MODE.md "Saves"): any number of native saves
// (gt_save.h text files, "<name>.sav") in one folder, <user data>/saves/, and which of
// them is the game being played (current.txt in that folder holds its file name). The
// menus' save page (menu/flow/save_page.h) lists them; the game autosaves into the
// current one.
//
// A save's name is its GameState::file_comment (the original's file name, shown and
// edited in File Operation) or, when that is empty, its file name without ".sav". File
// names follow the names (sanitised for every file system, made unique with " 2", " 3").
// No operation writes over another file: new files take free names, and an unreadable save
// is listed (state nullopt) but never replaced; when it is the current one, the session
// moves it aside before playing a new game in its place (GtModeSession::load).
//
// Earlier versions kept one save, <user data>/gtmode.sav, and a second "memory card
// slot" next to it, gtmode_2.sav: migrate() copies them into the library the first time
// it is created (the originals stay where they are). Versions up to 0.3 kept every disc's
// games in saves/: the PAL library copies them in when it is first used (copy_library).

#include "disc/region.h"
#include "game/gt_mode.h"

#include <optional>
#include <string>
#include <vector>

namespace gt3r::game {

struct SaveInfo {
    std::string path;               // the file
    std::string name;               // display name
    s64 mtime = 0;                  // last written (seconds since 1970)
    std::optional<GameState> state; // nullopt: unreadable (`error`)
    std::string error;
};

class SaveLibrary {
public:
    // `dir`: the library folder (created on the first write). `tracked`: whether the
    // current save is remembered in current.txt (off for GT3R_GTMODE_SAVE).
    explicit SaveLibrary(std::string dir, bool tracked = true);
    // The library of a region's games (each release keeps its own, as on the PS2's memory
    // cards): <user data>/saves/ for NTSC-U, <user data>/saves/<region key>/ for the
    // others (the user data folder: SDL_GetPrefPath, or the executable's folder in
    // portable mode).
    static std::string user_dir(Region region = Region::America);
    // The single save of earlier versions: <user data>/gtmode.sav.
    static std::string legacy_path();

    const std::string& dir() const { return dir_; }
    // First run: when the library folder does not exist yet it is created and `legacy`
    // and its second slot (<legacy stem>_2.sav) are copied in (the first one becomes the
    // current save). Returns the number of saves copied.
    int migrate(const std::string& legacy);
    // First run of a region's library: when the folder does not exist yet it is created and
    // every *.sav of the library `from` (its subfolders not included) is copied in, and its
    // current save stays the current one; the originals stay where they are. Versions up to
    // 0.3 kept every disc's games in saves/ (all on the NTSC-U data set, also from a PAL disc),
    // so a PAL library starts with them. Returns the number of saves copied.
    int copy_library(const std::string& from);

    // Every *.sav in the folder, newest first.
    std::vector<SaveInfo> list() const;
    // The current save's path ("" when none is recorded or its file is gone).
    std::string current() const;
    bool set_current(const std::string& path);
    bool tracked() const { return tracked_; }

    // A name no save in the library has: `base` (cut to `max` characters) or
    // "<base> 2", "<base> 3"... `except` (a path) does not count.
    std::string unique_name(const std::string& base, size_t max = 16, const std::string& except = {}) const;
    // A free file path for a save called `name` (`except`: a path that may be reused).
    std::string path_for(const std::string& name, const std::string& except = {}) const;

    // Writes a new save called `name` (file_comment = name) holding `s`; its path in `path`.
    bool create(GameState s, const std::string& name, std::string* path, std::string* error = nullptr);
    // Renames a save: its name is set to `name` and its file follows. `live`: the state to
    // write instead of the file's own (the game being played). The current save stays
    // current. New path in `new_path`.
    bool rename(const std::string& path, const std::string& name, std::string* new_path,
                const GameState* live = nullptr, std::string* error = nullptr);
    // A copy named "<name> 2" (...); its path in `new_path`.
    bool copy(const std::string& path, std::string* new_path, std::string* error = nullptr);
    bool remove(const std::string& path);

    static std::string display_name(const GameState& s, const std::string& path);
    // A name usable as a file name on every system ("" -> "save").
    static std::string file_stem(const std::string& name);

private:
    std::string dir_;
    bool tracked_ = true;
};

// Two names of one save file, with either path separator ("" names none).
bool same_save_file(const std::string& a, const std::string& b);

// A save the player picked anywhere: a native save, or a PS2 save in any of the
// containers import_ps2_save_path reads (a .psu or card image is also recognised by its
// contents, for paths without an extension such as Android's content:// URIs). Throws
// FormatError.
GameState read_any_save(const std::string& path, const GtData& data);

} // namespace gt3r::game
