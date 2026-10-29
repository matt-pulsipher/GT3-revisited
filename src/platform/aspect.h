#pragma once

#include <string_view>

namespace gt3r::platform {

// [video] aspect: the shape of the game area (the game's menus, races and movies; our own
// screens keep theirs). The game area is always kGameHeight UI units tall.
//  - Wide (16:9, default): 640 x 360, letterboxed to the window. The race uses GT3's
//    16:9 rules: the 3D view fills it, HUD elements flagged for 16:9 are pushed outward.
//  - Classic (4:3): 480 x 360, GT3's 4:3 picture: the 3D view at 4:3, the HUD
//    without the 16:9 push.
//  - Fill: the window's own shape (never narrower than 4:3): the 16:9 rules, the push
//    reaching the window's sides.
enum class Aspect { Wide, Classic, Fill };

constexpr int kGameHeight = 360;
constexpr int kWideWidth = 640;    // 16:9
constexpr int kClassicWidth = 480; // 4:3
constexpr int kMaxFillWidth = 1440; // 4:1, past 32:9

// "16:9" (or anything unknown), "4:3", "fill".
Aspect parse_aspect(std::string_view value);
const char* aspect_value(Aspect a);

// Width of the game area (UI units, kGameHeight tall) in a window of w x h pixels.
int game_width(Aspect a, int pixel_w, int pixel_h);

} // namespace gt3r::platform
