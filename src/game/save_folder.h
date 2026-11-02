#pragma once

// The save folder: where the GT-mode saves (saves/, game/save_library.h) and the replays
// (replays/, race/replay_library.h) are kept, each with its region folders. The user data
// folder (user_data_dir) unless the player chooses another (Settings -> Disc & saves;
// [saves] folder in config.ini), such as a folder a sync app keeps the same on two devices. On
// Android that can be any folder the system's picker offers, used through its provider
// (platform/document_tree.h); elsewhere it is a path.

#include <string>

namespace gt3r::game {

// The save folder, with a trailing separator: the chosen one, else user_data_dir().
std::string save_folder();
// Whether another folder than the user data folder is chosen.
bool save_folder_chosen();
// The folder to use ("": the user data folder). Set at startup from the config and when the
// player chooses one; the caller saves the config.
void set_save_folder(std::string dir);

// What copy_save_folder copied: saves (*.sav) and replays (*.gt3replay) counted, the folders'
// other files (current.txt, saves set aside) along with them.
struct SaveFolderCopy {
    int saves = 0, replays = 0;
    bool ok = true; // false: a file could not be copied, or `to` could not be written
};
// When the player moves to another save folder `to` (from `from`): `from`'s saves/ (with its
// region folders) is copied in when `to` has no saves/ yet, and its replays/ when `to` has no
// replays/ yet. A folder that already has its own (another device's, through a sync app) is
// used as it is; nothing in `to` is written over, nothing in `from` changes. `to` ends with a
// saves/ folder (made when there was nothing to copy: a folder the game cannot write fails
// here). When something fails, what was copied is removed again.
SaveFolderCopy copy_save_folder(const std::string& from, const std::string& to);

// Whether two save folders are the same: the same place on disk, or (Android's picked folders)
// the same path.
bool same_save_folder(const std::string& a, const std::string& b);

} // namespace gt3r::game
