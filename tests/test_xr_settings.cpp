// The headset's settings ([vr], xr::Settings). Only VR builds have them (GT3R_XR): elsewhere
// this file compiles to nothing.

#include "test_framework.h"

#if GT3R_XR

#include "base/config.h"
#include "xr/xr_session.h"

#include <cmath>

using namespace gt3r;

// The seat side: center by default; left and right move the in-car seat seat_offset across the
// car (x right), and `car` to the side the driven car has its wheel on (the middle when unknown).
TEST(xr_settings_seat_side) {
    const xr::Settings d = xr::Settings::load(IniDocument::parse(""));
    CHECK(d.seat_side == xr::SeatSide::Center && std::fabs(d.seat_offset - 0.36f) < 1e-6f);
    CHECK(d.seat(-1).x == 0 && d.seat(1).x == 0 && d.seat(0).x == 0);

    struct Want {
        const char* text;
        xr::SeatSide side;
        float x_left_car, x_right_car; // across the car, in a left- and a right-hand-drive car
    };
    const Want wants[] = {
        {"center", xr::SeatSide::Center, 0, 0},      {"left", xr::SeatSide::Left, -0.4f, -0.4f},
        {"Right", xr::SeatSide::Right, 0.4f, 0.4f},  {"car", xr::SeatSide::Car, -0.4f, 0.4f},
        {"middle", xr::SeatSide::Center, 0, 0},
    };
    for (const Want& w : wants) {
        const std::string ini = std::string("[vr]\nseat_side = ") + w.text +
                                "\nseat_offset = 0.4\nseat_height = 0.1\nseat_forward = 0.2\n";
        const xr::Settings s = xr::Settings::load(IniDocument::parse(ini));
        CHECK(s.seat_side == w.side);
        CHECK(std::fabs(s.seat(-1).x - w.x_left_car) < 1e-6f && std::fabs(s.seat(1).x - w.x_right_car) < 1e-6f);
        CHECK(std::fabs(s.seat(0).x - (w.side == xr::SeatSide::Car ? 0.0f : w.x_left_car)) < 1e-6f); // (a car the disc has no word on)
        // Height up, reach forward (z is back), whatever the side.
        CHECK(std::fabs(s.seat(1).y - 0.1f) < 1e-6f && std::fabs(s.seat(1).z + 0.2f) < 1e-6f);
        // Saved and read back the same.
        IniDocument out;
        s.save(out);
        const xr::Settings back = xr::Settings::load(out);
        CHECK(back.seat_side == s.seat_side && std::fabs(back.seat_offset - s.seat_offset) < 1e-6f);
    }
    // The distance stays inside the car.
    CHECK(std::fabs(xr::Settings::load(IniDocument::parse("[vr]\nseat_offset = 3\n")).seat_offset - 0.6f) < 1e-6f);
    CHECK(std::fabs(xr::Settings::load(IniDocument::parse("[vr]\nseat_offset = 0\n")).seat_offset - 0.1f) < 1e-6f);
}

#endif // GT3R_XR
