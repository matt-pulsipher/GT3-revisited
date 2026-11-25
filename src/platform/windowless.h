#pragma once

// Windowless runs: scripted runs (--shot, the GT3R_* scripts below) show no window, no Dock
// icon, take no focus, play no sound and read no real keyboard, mouse, touch, pen or
// controller, while drawing the same pictures as with a window (docs/dev/TESTING.md
// "Scripted runs"). --visible / GT3R_VISIBLE=1 shows the window anyway; --headless /
// GT3R_HEADLESS=1 hides any run. Desktop only: phones, tablets and VR builds always show.

#include <SDL3/SDL_stdinc.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r::platform {

// The environment as getenv returns it (nullptr: not set).
using Getenv = std::function<const char*(const char*)>;

// A run something else drives: --shot, or one of the testing variables that script the
// menus, races, pointer or online lobby (kScriptVariables, set and not empty).
bool scripted_run(const std::vector<std::string>& args, const Getenv& env);

// Whether the run shows no window. The command line first (the last of --visible /
// --headless), then the environment (GT3R_VISIBLE=1, GT3R_HEADLESS=1), then scripted_run.
bool windowless_run(const std::vector<std::string>& args, const Getenv& env);

// The testing variables that make a run scripted.
extern const char* const kScriptVariables[];
extern const size_t kScriptVariableCount;

// --pace <n> | display | off: the frames a second of a windowless run. Default 60 (a
// --shot frame is 1/60 s of game time: real time again); display, the display's refresh
// rate; off, as fast as the machine draws.
struct Pace {
    enum class Kind { Rate, Display, Off } kind = Kind::Rate;
    double fps = 60;
};
// The value after --pace; nullopt when it is none of those.
std::optional<Pace> parse_pace(std::string_view value);
// The last --pace on the command line (a bad value: warned and left out); 60 without one.
Pace pace_from_args(const std::vector<std::string>& args);

// An event from the player's own devices or the desktop: keyboard and text, mouse, touch, pen,
// gamepads and joysticks, the clipboard, files dropped and the window's focus. A windowless run
// drops them all (scripted events are pushed past the filter).
bool is_real_input(Uint32 event_type);

// The process's choice (set once at start): code that would open a window of the system's
// (file dialogs, message boxes, the browser, a folder) does nothing in a windowless run.
bool windowless();
void set_windowless(bool on);

} // namespace gt3r::platform
