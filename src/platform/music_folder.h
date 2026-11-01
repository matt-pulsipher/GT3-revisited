#pragma once

// Choosing the player's music folder (Settings -> Audio -> Your music), read where it is on
// every platform: the scan (race/custom_music.h) walks it by its path.
//   desktop  SDL's folder dialog
//   iOS      the system's document picker for a folder; a bookmark of it gives the access back
//            in later runs (as for disc images, ios_files.h)
//   Android  the system's folder picker, once the game may read audio files (READ_MEDIA_AUDIO,
//            READ_EXTERNAL_STORAGE before Android 13: Android 11 and later then let apps read
//            audio files by their paths). The folder must be on the device's storage or a
//            memory card, not in another app's provider (a cloud drive).

#include <SDL3/SDL_video.h>

#include <optional>
#include <string>

namespace gt3r::platform {

struct FolderAnswer {
    enum Kind {
        Picked,    // `path`, readable
        Cancelled,
        NoAccess,  // Android: the player didn't allow reading audio files
        Elsewhere, // Android: a folder of another app's provider, which has no path
        Failed,    // no picker could be shown
    } kind = Cancelled;
    std::string path;
};

// Shows the picker, starting at `start` where the platform can. One at a time: false when one
// is already open.
bool start_folder_pick(SDL_Window* window, const std::string& start);
// Each frame while one is open (on the game's thread): the answer once there is one.
std::optional<FolderAnswer> take_folder_pick();

// Whether the game may read `folder` now. Android: true inside the game's own storage
// (`data_dir`), else only with the permission to read audio files (the player can take it back
// in Android's settings). True elsewhere.
bool music_access(const std::string& folder, const std::string& data_dir);
// Android: opens the game's page in Android's settings (to allow music access after "Don't
// allow", which the system doesn't ask again). False elsewhere.
bool open_app_settings();

} // namespace gt3r::platform
