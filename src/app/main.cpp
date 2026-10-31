// Entry point using SDL3's callback model, which maps onto every platform's
// native application lifecycle (including Android and iOS).

#define SDL_MAIN_USE_CALLBACKS 1
#include "app/app.h"
#include "app/gles_probe.h"
#include "app/launch_help.h"

#include <SDL3/SDL_main.h>

using gt3r::App;

#ifdef SDL_PLATFORM_WINDOWS
// Laptops with two GPUs (NVIDIA Optimus, AMD switchable graphics) run the game on the fast one.
// SDL_GPU picks it itself; OpenGL ES (ANGLE's Direct3D 11 or the driver's own) goes wherever the
// driver sends it, which is the integrated GPU unless the executable exports these.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

SDL_AppResult SDL_AppInit(void** state, int argc, char** argv) {
    if (gt3r::wants_launch_help(argc, argv)) { // before SDL starts: no window
        gt3r::print_launch_help();
        return SDL_APP_SUCCESS;
    }
    if (std::optional<bool> opens = gt3r::run_gles_probe(argc, argv)) // a copy trying OpenGL ES: no window
        return *opens ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    auto* app = new App();
    *state = app;
    return app->init(argc, argv) ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppIterate(void* state) { return static_cast<App*>(state)->iterate(); }

SDL_AppResult SDL_AppEvent(void* state, SDL_Event* event) { return static_cast<App*>(state)->event(*event); }

void SDL_AppQuit(void* state, SDL_AppResult) {
    if (auto* app = static_cast<App*>(state)) {
        app->shutdown();
        delete app;
    }
}
