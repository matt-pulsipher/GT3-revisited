#include "platform/windowless.h"

#include "base/log.h"

#include <SDL3/SDL_events.h>

#include <atomic>
#include <cstdlib>

namespace gt3r::platform {

const char* const kScriptVariables[] = {
    "GT3R_POINTER_SCRIPT",   // pointer events (App::pointer_script)
    "GT3R_GT3MENU_SCRIPT",   // the game's menus: keys
    "GT3R_GT3MENU_OPEN",     // the game's menus: a page opened directly
    "GT3R_GTSETTING_SCRIPT", // GT Mode car settings and presentation pages: keys
    "GT3R_GTMODE_FAKE_RACE", // GT Mode races won without driving
    "GT3R_RACE",             // races: course, car, auto-drive, warps, replays, quit
    "GT3R_QUICKRACE",        // Quick race: its steps
    "GT3R_ONLINE",           // the online lobby: host / join steps
    "GT3R_MENU",             // the menu viewer: a file opened
    "GT3R_VIEWER",           // the model viewer: item, angle, export
    "GT3R_MOD_ZIP",          // Mods: a package installed
    "GT3R_PCSX2_PACK",       // Mods: a texture pack converted
    "GT3R_SAVE_TEXTURES",    // Mods: the disc's textures saved
};
const size_t kScriptVariableCount = sizeof(kScriptVariables) / sizeof(kScriptVariables[0]);

namespace {

bool env_on(const Getenv& env, const char* name) {
    const char* v = env ? env(name) : nullptr;
    return v && v[0] == '1' && v[1] == 0;
}

std::atomic<bool> g_windowless{false};

} // namespace

bool scripted_run(const std::vector<std::string>& args, const Getenv& env) {
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == "--shot") return true;
    for (size_t i = 0; i < kScriptVariableCount && env; ++i)
        if (const char* v = env(kScriptVariables[i]); v && *v) return true;
    return false;
}

bool windowless_run(const std::vector<std::string>& args, const Getenv& env) {
    std::optional<bool> cmd; // the last of --visible / --headless
    for (const std::string& a : args) {
        if (a == "--visible") cmd = false;
        else if (a == "--headless") cmd = true;
    }
    if (cmd) return *cmd;
    if (env_on(env, "GT3R_VISIBLE")) return false;
    if (env_on(env, "GT3R_HEADLESS")) return true;
    return scripted_run(args, env);
}

std::optional<Pace> parse_pace(std::string_view value) {
    if (value == "display") return Pace{Pace::Kind::Display, 0};
    if (value == "off") return Pace{Pace::Kind::Off, 0};
    if (value.empty() || value.size() > 15) return std::nullopt;
    char buf[16] = {};
    value.copy(buf, value.size());
    char* end = nullptr;
    const double fps = std::strtod(buf, &end);
    if (end != buf + value.size() || !(fps >= 1 && fps <= 1000)) return std::nullopt;
    return Pace{Pace::Kind::Rate, fps};
}

Pace pace_from_args(const std::vector<std::string>& args) {
    Pace pace;
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] != "--pace") continue;
        if (std::optional<Pace> p = parse_pace(args[++i])) pace = *p;
        else LOG_WARN("--pace %s: expected frames a second (1 to 1000), display or off", args[i].c_str());
    }
    return pace;
}

bool is_real_input(Uint32 type) {
    // SDL groups its events in ranges of 0x100 (SDL_events.h).
    auto in = [type](Uint32 first) { return type >= first && type < first + 0x100; };
    return in(SDL_EVENT_KEY_DOWN) ||             // keys, text, keyboards, the screen keyboard
           in(SDL_EVENT_MOUSE_MOTION) ||         // the mouse
           in(SDL_EVENT_JOYSTICK_AXIS_MOTION) || // joysticks and gamepads
           in(SDL_EVENT_FINGER_DOWN) ||          // touch and pinches
           in(SDL_EVENT_CLIPBOARD_UPDATE) ||     // the clipboard
           in(SDL_EVENT_DROP_FILE) ||            // files and text dropped on the window
           in(SDL_EVENT_SENSOR_UPDATE) ||        // the device's motion sensors
           in(SDL_EVENT_PEN_PROXIMITY_IN) ||     // pens
           type == SDL_EVENT_WINDOW_FOCUS_GAINED || type == SDL_EVENT_WINDOW_FOCUS_LOST ||
           type == SDL_EVENT_WINDOW_MOUSE_ENTER || type == SDL_EVENT_WINDOW_MOUSE_LEAVE;
}

bool windowless() { return g_windowless.load(std::memory_order_relaxed); }

void set_windowless(bool on) { g_windowless.store(on, std::memory_order_relaxed); }

} // namespace gt3r::platform
