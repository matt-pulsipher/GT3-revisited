#pragma once

// The race screen (screen_race.cpp) started the way the interactive menus start it: a
// menu::flow::GameRequest (mode, event, car, paint, gearbox, ...). The screen pops itself
// when the race is left, after posting a menu::flow::GameResult.

#include "race/custom_cameras.h"

#include <memory>

namespace gt3r {

class App;
class Screen;
namespace menu::flow {
struct GameRequest;
}

std::unique_ptr<Screen> make_menu_race_screen(const menu::flow::GameRequest& request);

// Settings -> Cameras: the camera editor (app/camera_editor.h) over a frozen moment of a race on
// the course of the player's last race, with its car. Save writes `camera` to config.ini
// ([custom_camera<n>]: replaced by its id, or added last when `is_new`); either way the screen pops.
std::unique_ptr<Screen> make_camera_editor_screen(App& app, race::CustomCamera camera, bool is_new);

} // namespace gt3r
