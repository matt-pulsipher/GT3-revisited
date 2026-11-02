#pragma once

// Android: a folder the player picked in the system's folder picker (the save folder,
// game/save_folder.h), read and written through its documents provider (the Storage Access
// Framework) wherever it is: the device's storage, a memory card, or another app's provider (a
// sync app's, a cloud drive's). The game keeps the access across runs (a persisted permission).
//
// Its files have paths like any other: the folder's tree URI, then "/" and the names inside it,
// "content://<authority>/tree/<folder id>/saves/eu/GAME.sav". The host file functions
// (base/file.h) take them on Android; GT3Activity's DocumentTree finds each name's document,
// child by child. Names match without case when no name matches exactly (as most of the folders
// players pick are on storage that ignores case).

#include "base/file.h"

#include <SDL3/SDL_platform_defines.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r::platform {

// Whether `path` is in a picked folder (the tree URI, or the tree URI, "/" and names). A
// document's own URI ("content://<authority>/document/<id>", ".../tree/<id>/document/<id>",
// what file pickers hand over) is not. Plain string rules: true on every platform for such a
// string, though only Android opens it.
bool is_document_tree_path(std::string_view path);
// The tree URI a path is in ("" when it is in none).
std::string document_tree_of(std::string_view path);

#if defined(SDL_PLATFORM_ANDROID)
struct DocumentInfo {
    bool dir = false;
    s64 mtime = 0; // seconds since 1970
    u64 size = 0;
};
// The document's own URI, for SDL's file functions (they open content:// URIs); "" when it is
// not there.
std::string document_uri(const std::string& path);
std::optional<DocumentInfo> document_info(const std::string& path);
std::vector<HostDirEntry> document_entries(const std::string& dir);
// Creates the file (empty) or folder, and the folders above it; the URI of what is there now
// ("" when it cannot be made). A file that is already there is kept as it is.
std::string document_create(const std::string& path, bool dir);
bool document_remove(const std::string& path);
// Renames a file within its folder, replacing `to`.
bool document_rename(const std::string& from, const std::string& to);

// The picked folder's name as its provider shows it ("" when it cannot be read).
std::string document_tree_name(const std::string& tree);
// Whether the game still has read and write access to the folder (the player can take it back
// in Android's settings, or its provider can go: the app uninstalled, the memory card out).
bool document_tree_access(const std::string& tree);
// Gives up the access kept for a folder the game no longer uses.
void forget_document_tree(const std::string& tree);
#endif

} // namespace gt3r::platform
