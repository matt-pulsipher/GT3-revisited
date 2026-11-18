#pragma once

// Full-screen FMV playback for the game flow (boot opening, title loop,
// endings). Other screens use it like this:
//
//   #include "app/screen_movies.h"
//   play_movie(app, "stream/opening.pss", [](App& app) { app.push(make_title_screen()); });
//
// The movie screen is pushed on top of the current screen and pops itself
// when the movie ends (or the player skips it), then calls `on_done`. If the
// movie cannot be opened (missing, corrupt) it pops on its first update and
// still calls `on_done`, so callers never get stuck.
//
// For movies embedded in a menu page (menu_bg2.pss etc.), use
// video::MoviePlayer directly: start_loop(), update() each frame, draw().

#include <functional>
#include <memory>
#include <string>

namespace gt3r {

class App;
class Screen;

struct MovieScreenOptions {
    bool skippable = true;   // Cross / Start / Circle end the movie early
    bool loop = false;       // restart at the end (title/attract loops); ends only when skipped
    bool stop_music = true;  // stop the jukebox/BGM stream first
    float volume = -1.0f;    // < 0: use the music volume
    bool* skipped = nullptr; // set true when the player ended the movie early (the pointer must outlive the screen)
};

std::unique_ptr<Screen> make_movie_screen(const std::string& vfs_path, std::function<void(App&)> on_done = {},
                                          MovieScreenOptions options = {});

// app.push(make_movie_screen(...)).
void play_movie(App& app, const std::string& vfs_path, std::function<void(App&)> on_done = {},
                MovieScreenOptions options = {});

// Boot flow (screen id "boot"): opening.pss, skippable, then the "gt3menu" screen.
std::unique_ptr<Screen> make_boot_screen();

} // namespace gt3r
