#pragma once

struct SDL_Window;

namespace gt3r::platform {

// macOS: marks the window's Metal layers non-opaque, so the compositor always composites the
// window instead of switching fullscreen frames straight to the display and back. No-op elsewhere.
void keep_composited(SDL_Window* window);

// macOS, before SDL_Init: the app runs as an accessory, without a Dock icon or a menu bar, from the
// moment it finishes launching (a .app bundle starts as a regular app otherwise, whatever SDL's
// SDL_HINT_MAC_BACKGROUND_APP). Windowless runs (platform/windowless.h). No-op elsewhere.
void run_without_dock_icon();

} // namespace gt3r::platform
