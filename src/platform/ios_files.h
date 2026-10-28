#pragma once

// iOS: files the player keeps outside the app (iCloud Drive, On My iPhone, other file providers)
// are read where they are, never copied (a disc image is 3.9 GB). The system's document picker
// hands the app a security-scoped URL: readable only after startAccessingSecurityScopedResource,
// and only in this run. A bookmark of it (kept in the app's user defaults, keyed by the path)
// gives the access back in later runs. Files in the app's own folder (the user data folder,
// Documents) need neither.

#include "base/config.h"

#include <SDL3/SDL_dialog.h>

#include <functional>
#include <string>

struct SDL_Window;

namespace gt3r::platform {

// iOS: the system's document picker over `window`, opening one file in place. `filters` as for
// SDL_ShowOpenFileDialog ("*" = any file). `done` gets the file's path, already readable, or ""
// when cancelled, on the main thread. False (and `done` never called) without a UIKit window,
// and elsewhere: SDL has no file dialog on iOS.
bool show_document_picker(SDL_Window* window, const SDL_DialogFileFilter* filters, int count,
                          std::function<void(std::string path)> done);

// iOS: makes the remembered disc images ([game] disc and recent1..5) readable before they are
// opened or listed: paths in an earlier install's app folder are moved to this one's
// (rebase_app_container_path), picked files get their access back from their bookmarks, and
// files that moved since are followed. True when a path in `cfg` changed (for the caller to
// save). No-op elsewhere.
bool restore_disc_access(IniDocument& cfg, const std::string& data_dir);

} // namespace gt3r::platform
