// Windowless scripted runs (platform/windowless.h): which runs hide the window, --pace, the
// filter of the player's own input, and the frame clock (base/frame_clock.h).

#include "test_framework.h"

#include "base/frame_clock.h"
#include "platform/windowless.h"

#include <SDL3/SDL_events.h>

#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::platform;

namespace {

using Args = std::vector<std::string>;

Getenv env_of(std::map<std::string, std::string> vars) {
    return [vars = std::move(vars)](const char* name) -> const char* {
        auto it = vars.find(name);
        return it == vars.end() ? nullptr : it->second.c_str();
    };
}

} // namespace

TEST(windowless_scripted_runs) {
    const Getenv none = env_of({});
    CHECK(!scripted_run({}, none));
    CHECK(!scripted_run({"game.iso"}, none));
    CHECK(!scripted_run({"game.iso", "--screen", "race"}, none));
    CHECK(scripted_run({"game.iso", "--shot", "out.png"}, none));
    CHECK(!scripted_run({"game.iso", "--shot"}, none)); // (no path: the app ignores it too)
    for (size_t i = 0; i < kScriptVariableCount; ++i) {
        CHECK(scripted_run({"game.iso"}, env_of({{kScriptVariables[i], "x"}})));
        CHECK(!scripted_run({"game.iso"}, env_of({{kScriptVariables[i], ""}}))); // set but empty
    }
    CHECK(scripted_run({}, env_of({{"GT3R_RACE", "course=trialmt,auto=1"}})));
    CHECK(scripted_run({}, env_of({{"GT3R_POINTER_SCRIPT", "wait:10;click:100 100"}})));
    CHECK(scripted_run({}, env_of({{"GT3R_GT3MENU_SCRIPT", "cross"}})));
    CHECK(scripted_run({}, env_of({{"GT3R_QUICKRACE", "go"}})));
    // Screenshot helpers alone (they need --shot to be a scripted run).
    CHECK(!scripted_run({}, env_of({{"GT3R_SHOT_FRAME", "120"}, {"GT3R_LANG", "fr"}, {"GT3R_DATA_DIR", "/tmp/x"}})));
}

TEST(windowless_command_line_before_environment) {
    const Args shot = {"game.iso", "--shot", "out.png"};
    const Args plain = {"game.iso"};
    CHECK(windowless_run(shot, env_of({})));
    CHECK(!windowless_run(plain, env_of({})));
    // --visible / --headless, the last one given
    CHECK(!windowless_run({"game.iso", "--shot", "out.png", "--visible"}, env_of({})));
    CHECK(windowless_run({"game.iso", "--headless"}, env_of({})));
    CHECK(windowless_run({"--visible", "--headless"}, env_of({})));
    CHECK(!windowless_run({"--headless", "--visible"}, env_of({})));
    // the environment
    CHECK(!windowless_run(shot, env_of({{"GT3R_VISIBLE", "1"}})));
    CHECK(windowless_run(plain, env_of({{"GT3R_HEADLESS", "1"}})));
    CHECK(windowless_run(shot, env_of({{"GT3R_VISIBLE", "0"}})));
    CHECK(!windowless_run(plain, env_of({{"GT3R_HEADLESS", "0"}})));
    CHECK(!windowless_run(shot, env_of({{"GT3R_VISIBLE", "1"}, {"GT3R_HEADLESS", "1"}}))); // visible first
    // the command line wins over the environment
    CHECK(windowless_run({"game.iso", "--headless"}, env_of({{"GT3R_VISIBLE", "1"}})));
    CHECK(!windowless_run({"game.iso", "--visible"}, env_of({{"GT3R_HEADLESS", "1"}})));
    CHECK(!windowless_run({"--visible"}, env_of({{"GT3R_RACE", "auto=1"}})));
    // no environment at all
    CHECK(windowless_run(shot, Getenv{}));
    CHECK(!windowless_run(plain, Getenv{}));
}

TEST(windowless_pace) {
    using K = Pace::Kind;
    CHECK(parse_pace("60") && parse_pace("60")->kind == K::Rate && parse_pace("60")->fps == 60);
    CHECK(parse_pace("144")->fps == 144);
    CHECK(parse_pace("29.97") && std::abs(parse_pace("29.97")->fps - 29.97) < 1e-9);
    CHECK(parse_pace("display") && parse_pace("display")->kind == K::Display);
    CHECK(parse_pace("off") && parse_pace("off")->kind == K::Off);
    for (const char* bad : {"", "0", "-5", "abc", "60fps", "1001", "nan", "inf", "Display", "1e9"})
        CHECK(!parse_pace(bad));
    CHECK(pace_from_args({}).kind == K::Rate && pace_from_args({}).fps == 60); // the default
    CHECK(pace_from_args({"--pace", "off"}).kind == K::Off);
    CHECK(pace_from_args({"--pace", "30", "--pace", "display"}).kind == K::Display); // the last
    CHECK(pace_from_args({"--pace", "bogus"}).fps == 60);                              // a bad value: left out
    CHECK(pace_from_args({"--pace"}).fps == 60);                                       // no value
}

TEST(windowless_drops_real_input) {
    // The player's devices and the desktop.
    for (Uint32 t : {SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP, SDL_EVENT_TEXT_INPUT, SDL_EVENT_TEXT_EDITING,
                     SDL_EVENT_KEYMAP_CHANGED, SDL_EVENT_KEYBOARD_ADDED, SDL_EVENT_KEYBOARD_REMOVED,
                     SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP,
                     SDL_EVENT_MOUSE_WHEEL, SDL_EVENT_MOUSE_ADDED, SDL_EVENT_MOUSE_REMOVED,
                     SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_JOYSTICK_BUTTON_DOWN, SDL_EVENT_JOYSTICK_ADDED,
                     SDL_EVENT_JOYSTICK_REMOVED, SDL_EVENT_GAMEPAD_AXIS_MOTION, SDL_EVENT_GAMEPAD_BUTTON_DOWN,
                     SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_EVENT_GAMEPAD_ADDED, SDL_EVENT_GAMEPAD_SENSOR_UPDATE,
                     SDL_EVENT_FINGER_DOWN, SDL_EVENT_FINGER_UP, SDL_EVENT_FINGER_MOTION, SDL_EVENT_FINGER_CANCELED,
                     SDL_EVENT_CLIPBOARD_UPDATE, SDL_EVENT_DROP_FILE, SDL_EVENT_DROP_TEXT, SDL_EVENT_DROP_BEGIN,
                     SDL_EVENT_DROP_COMPLETE, SDL_EVENT_DROP_POSITION, SDL_EVENT_SENSOR_UPDATE,
                     SDL_EVENT_PEN_PROXIMITY_IN, SDL_EVENT_PEN_DOWN, SDL_EVENT_PEN_MOTION, SDL_EVENT_PEN_AXIS,
                     SDL_EVENT_WINDOW_FOCUS_GAINED, SDL_EVENT_WINDOW_FOCUS_LOST, SDL_EVENT_WINDOW_MOUSE_ENTER,
                     SDL_EVENT_WINDOW_MOUSE_LEAVE})
        CHECK(is_real_input(t));
    CHECK(is_real_input(0x710)); // pinch gestures (SDL 3.4's SDL_EVENT_PINCH_BEGIN)
    // What the run itself needs: quitting (SIGTERM too), the window, displays, audio devices,
    // the app's life cycle, rendering, user events.
    for (Uint32 t : {SDL_EVENT_QUIT, SDL_EVENT_TERMINATING, SDL_EVENT_LOW_MEMORY, SDL_EVENT_WILL_ENTER_BACKGROUND,
                     SDL_EVENT_DID_ENTER_FOREGROUND, SDL_EVENT_DISPLAY_ADDED, SDL_EVENT_WINDOW_SHOWN,
                     SDL_EVENT_WINDOW_RESIZED, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, SDL_EVENT_WINDOW_CLOSE_REQUESTED,
                     SDL_EVENT_WINDOW_DISPLAY_CHANGED, SDL_EVENT_AUDIO_DEVICE_ADDED, SDL_EVENT_AUDIO_DEVICE_REMOVED,
                     SDL_EVENT_RENDER_TARGETS_RESET, SDL_EVENT_RENDER_DEVICE_RESET, SDL_EVENT_USER})
        CHECK(!is_real_input(t));
}

TEST(windowless_flag) {
    const bool was = windowless();
    set_windowless(true);
    CHECK(windowless());
    set_windowless(false);
    CHECK(!windowless());
    set_windowless(was);
}

TEST(frame_clock_wall_until_fixed) {
    // (gt3r_tests never sets fixed steps: --shot runs do, at start)
    if (frame_clock::fixed()) SKIP("fixed steps already on");
    const u64 a = frame_clock::now_ns();
    frame_clock::step(1'000'000'000); // only counts with fixed steps
    const u64 b = frame_clock::now_ns();
    CHECK(b >= a && b - a < 500'000'000ull);
    CHECK(frame_clock::counter() != 0);
}
