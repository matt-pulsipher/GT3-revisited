#include "app/launch_help.h"

#include "app/screen_registry.h"
#include "gt3r_version.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gt3r {

namespace {

const char kOptions[] = R"(Usage: gt3r [disc.iso] [options]

  disc.iso               a GT3 disc image (default: the one opened last)
  --screen <id>          open a screen instead of the game (ids below)
  --hub                  start in Developer options ([app] start = hub)
  --browse <vol path>    open the asset browser on a file in GT3.VOL
  --shot <out.png>       save frame 30 (GT3R_SHOT_FRAME) as a PNG and quit
  --msaa <n>             MSAA sample count ([video] msaa)
  --render-scale <s>     auto or 0.5..4.0 ([video] render_scale)
  --no-gpu               the 2D renderer only, no 3D ([video] gpu = 0)
  --gles                 OpenGL ES before SDL_GPU for 3D, Windows and Linux ([video] gpu = gles)
  --gles-driver          Windows: --gles with the driver's, not ANGLE ([video] gpu = gles_driver)
  --no-vr                VR builds: the window, not the headset ([vr] enabled = off)
  --env NAME=value       set one of the variables below
  -h, --help             this text

Android and iOS: one argument per line in args.txt in the user data folder.
)";

const char kVariables[] = R"(
Testing variables (in the environment, or --env NAME=value):

 Screenshots and scripted input (docs/dev/PLATFORMS.md)
  GT3R_SHOT_FRAME=<n>          --shot saves frame n (default 30)
  GT3R_SHOT_COUNT=<n>          n frames from there, as <out>-<k>.png
  GT3R_CAPTURE3D=<first>,<count>,<path>  the 3D view's frames, without waiting for the display
  GT3R_POINTER_SCRIPT=<steps>  mouse and touch in 640x360 UI units, steps split by ';':
                               wait:<frames> move:<x> <y> click:<x> <y> rclick:<x> <y>
                               drag:<x0> <y0> <x1> <y1> wheel:<n> <x> <y> tap:<x> <y>
                               pinch:<x> <y> <from> <to>
  GT3R_FILE_DIALOG=<path>      answer file dialogs with this path ("-": cancel)
  GT3R_UPDATE_CHECK=1          --shot runs check for updates too (otherwise the cached answer)

 General
  GT3R_DATA_DIR=<dir>          the user data folder (config.ini, saves, replays, mods)
  GT3R_LANG=<code>             the game's language, any the disc has menus for: US JP BR FR GR IT SP
  GT3R_SKIP_MOVIES=1           game-flow movies end at once
  GT3R_PHONE=1                 desktop: a 20:9 phone-shaped window with the touch UI
  GT3R_TOUCH_SHOW=1            show the touch controls
  GT3R_SETTINGS_TAB=<n>        Settings opens on page n
  GT3R_MOD_ZIP=<archive>       the Mods screen installs this archive

 Races (docs/dev/RACE.md)
  GT3R_RACE=<key=value,...>    --screen race, e.g. course=trialmt,car=r34_gtr_v_99,auto=1,cam=2
                               course car paint cam auto mt event tt_event laps p2 car2 split
                               warp quit leave replay watch rwarp save hud perf ghost music
                               pause script ... (all keys: read_preset, screen_race_launch.cpp)
  GT3R_RACE_DEBUG=1            log vehicle telemetry twice a second
  GT3R_RACE_TRACE=<file>       one line per race step with the player's car (live vs replay)
  GT3R_EXACT_DEBUG=1           log every car of the exact race core every 2 s
  GT3R_ORIGINAL_RES=1          the 3D scene at 640x448 in a 4:3 frame
  GT3R_NO_ENV=1                no car reflections
  GT3R_NO_COURSE_FOG=1         the old placeholder fog
  GT3R_DEMO_SIGN=<s>           steering sign of the disc's demo inputs (default -1)

 Menus and GT Mode (docs/dev/GT_MODE.md, docs/behaviour/menus.md)
  GT3R_GT3MENU_SCRIPT=<keys>   --screen gt3menu keys, split by ',': up down left right cross
                               circle square triangle start select wait:<frames>
  GT3R_GT3MENU_OPEN=<set>:<page>  open a page of the game's menus directly
  GT3R_GT3MENU_FREEZE=1        stop the menu clock after the script
  GT3R_GT3MENU_DEBUG=1         the menu debug overlay
  GT3R_GTMODE_SAVE=<path>      play this save file; nothing else is remembered
  GT3R_GTMODE_NOSAVE=1         write no saves
  GT3R_GTMODE_FAKE_RACE=1      win every race at once (license tests with gold)
  GT3R_GTMODE_FAKE_TROPHY=<NN> show trophy NN after any race win
  GT3R_GTMODE_PRIZE_FRAME=<n>  start presentations n frames in
  GT3R_GTSETTING_EVENT=<event> --screen gtsetting for this event
  GT3R_GTSETTING_SCRIPT=<keys> its keys: up down left right cross triangle l1 r1 wait:<frames>
  GT3R_PRIZE=<event>:<place>   --screen gtprize
  GT3R_RECORDS_KEY=<key>       --screen gtrecords (default mt:M400)
  GT3R_QUICKRACE=<key=value,...>  --screen quickrace: event car paint mt level course page start
  GT3R_MENU=<file>[:<page>[:<focus>]]  --screen menu opens this .pmb or .imgs file
  GT3R_MOVIE=<vol path>        --screen movies plays it (from GT3R_MOVIE_START=<s> seconds)
  GT3R_MOVIE_STILLS=1          --screen movies opens the stills gallery
  GT3R_TEXT_MODE=<n>           --screen text opens on page n (0..2)
  GT3R_VIEWER=<key=value,...>  --screen cars state: find clean key yaw pitch zoom (docs/MODDING.md)
  GT3R_VIEWER_PAINT=<n>        --screen cars: a mod car's paint

 Online (docs/dev/ONLINE.md)
  GT3R_ONLINE=<key=value,...>  --screen online: host, join=<address> or code=<code>; name car
                               paint course laps players server codefile accept page class
                               category drive aspiration maxps stock mods
  GT3R_NET_FORCE_RELAY=1       everything through the relay server (no hole punching)
  GT3R_NET_PREDICT=dead        other cars by dead reckoning instead of stepping their inputs
  GT3R_NET_DEBUG=1             log our car, the posed car and the newest state

 Rendering and timing (docs/dev/RENDERING.md, docs/dev/VR.md)
  GT3R_HITCH=<ms>              log every frame longer than this, with its parts
  GT3R_DYN_CYCLE=<s>           sweep the dynamic resolution every s seconds
  GT3R_DIRECT=1                macOS: let fullscreen go direct-to-display
  GT3R_VR_EYE_SIZE=<px>        VR: the eye image size
  GT3R_VR_TIMING=1             VR: log the frame loop's parts
)";

void print_screens(bool player) {
    std::vector<const ScreenEntry*> list;
    for (const ScreenEntry& e : screen_registry())
        if (e.player == player) list.push_back(&e);
    std::stable_sort(list.begin(), list.end(), [](const ScreenEntry* a, const ScreenEntry* b) { return a->order < b->order; });
    for (const ScreenEntry* e : list) std::printf("  %-12s %s%s\n", e->id, e->help, e->needs_disc ? "" : " *");
}

} // namespace

bool wants_launch_help(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) return true;
    return false;
}

void print_launch_help() {
#ifdef _WIN32
    // A GUI program has no console of its own: write to the one it was started from, if any.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
    }
#endif
    std::printf("GT3 Revisited %s\n\n", GT3R_VERSION);
    std::fputs(kOptions, stdout);
    std::printf("\nScreens (--screen <id>; * needs no disc):\n On the home menu\n");
    print_screens(true);
    std::printf(" Developer options\n");
    print_screens(false);
    std::fputs(kVariables, stdout);
    std::fflush(stdout);
}

} // namespace gt3r
