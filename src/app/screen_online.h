#pragma once

// Online battle (the arcade menu's i.LINK Battle over IP; docs/dev/ONLINE.md): the
// lobby screen and the race it starts. The session itself is net::online().

#include <memory>
#include <string>
#include <vector>

namespace gt3r {

class Screen;

// The relay server players use unless [online] server says otherwise (the project's public
// relay; docs/dev/ONLINE.md "Running your own relay"). The update check asks it too.
inline constexpr const char* kDefaultServer = "gt3r.gameversions.com";

struct OnlineLaunch {
    std::string course, car; // course label, database car label
    int paint = 0;
    int laps = 2;
    std::vector<unsigned char> car_info; // game::OnlineCar (garage cars: parts and settings)
    bool manual = false;
    bool asm_on = true, tcs_on = true; // the Options' ASM / TCS ([options] assist_asm / assist_tcs)
};

std::unique_ptr<Screen> make_online_lobby_screen();                    // screen_online.cpp
std::unique_ptr<Screen> make_online_race_screen(const OnlineLaunch& l); // screen_race.cpp

} // namespace gt3r
