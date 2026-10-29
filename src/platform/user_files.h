#pragma once

// The user data folder (config.ini, mods/, screenshots) as the player reaches it outside the
// game. On Android it is in the app's private storage, which file managers cannot open; the
// app's documents provider (android GT3DocumentsProvider) shows it in the system Files app and
// file pickers as "GT3 Revisited" instead.

#include <string>

namespace gt3r::platform {

// Shows `dir` (the user data folder or a folder in it; `data_dir` is the user data folder) in
// the system's file manager: Finder, Explorer or the desktop's own; on Android the Files app,
// opened at that folder when it can open folders directly, else at the GT3 Revisited entry; on
// iOS the Files app at that folder (On My iPhone > GT3 Revisited).
// False when nothing could open it.
bool open_user_folder(const std::string& dir, const std::string& data_dir);

// A picked file's name for messages and mod ids: an Android content:// URI's display name
// from its provider (the URI itself often holds only a document number), else the last part
// of the path.
std::string picked_file_name(const std::string& path);

} // namespace gt3r::platform
