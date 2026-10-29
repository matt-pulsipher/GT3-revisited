// Platform helpers (src/platform/): accelerometer remapping for tilt steering.

#include "test_framework.h"

#include "platform/aspect.h"
#include "platform/device_tilt.h"
#include "platform/render_scale.h"

#include <cmath>
#include <numbers>

using namespace gt3r;

namespace {

constexpr float kG = 9.80665f;
constexpr float kDeg = std::numbers::pi_v<float> / 180.0f;

// Reading of a device held upright with the display rotated by `display_deg`
// (clockwise) from natural, then turned clockwise by `turn_deg` as seen by
// the player: "up" in display coordinates is (-sin t, cos t); map it back to
// device axes.
void reading(int display_deg, float turn_deg, float out[3]) {
    float t = turn_deg * kDeg;
    float dx = -std::sin(t) * kG, dy = std::cos(t) * kG;
    switch (display_deg) {
    case 90: out[0] = dy, out[1] = -dx; break; // inverse of (x, y) -> (-y, x)
    case 180: out[0] = -dx, out[1] = -dy; break;
    case 270: out[0] = -dy, out[1] = dx; break;
    default: out[0] = dx, out[1] = dy; break;
    }
    out[2] = 0;
}

} // namespace

TEST(platform_tilt_upright_is_centered) {
    for (int rot : {0, 90, 180, 270}) {
        float raw[3], disp[3];
        reading(rot, 0, raw);
        platform::accel_to_display(raw, disp, rot);
        CHECK(std::fabs(disp[0]) < 1e-4f);
        CHECK(std::fabs(disp[1] - kG) < 1e-4f);
        CHECK(std::fabs(platform::steering_roll(disp)) < 1e-4f);
    }
}

TEST(platform_tilt_clockwise_steers_right) {
    for (int rot : {0, 90, 180, 270})
        for (float turn : {-40.0f, -10.0f, 15.0f, 30.0f}) {
            float raw[3], disp[3];
            reading(rot, turn, raw);
            platform::accel_to_display(raw, disp, rot);
            float roll = platform::steering_roll(disp);
            CHECK(std::fabs(roll - turn * kDeg) < 1e-3f);
        }
}

TEST(platform_tilt_landscape_phone_reading) {
    // SDL_ORIENTATION_LANDSCAPE on a portrait phone: the phone's top points
    // left, so gravity's reaction is along device +X.
    float raw[3] = {kG, 0, 0}, disp[3];
    platform::accel_to_display(raw, disp, 90);
    CHECK(std::fabs(platform::steering_roll(disp)) < 1e-4f);
    // Without the remap the same pose would read as a full right/left lock.
    CHECK(std::fabs(platform::steering_roll(raw)) > 1.5f);
}

TEST(platform_render_scale) {
    CHECK(!platform::parse_render_scale("auto"));
    CHECK(!platform::parse_render_scale(""));
    CHECK(!platform::parse_render_scale("fast"));
    CHECK(*platform::parse_render_scale(" 0.75 ") == 0.75f);
    CHECK(*platform::parse_render_scale("0.2") == 0.5f);
    CHECK(*platform::parse_render_scale("3") == 3.0f); // supersampling
    CHECK(*platform::parse_render_scale("9") == platform::kMaxRenderScale);
    // Supersampling's limit: 16384 pixels a side, never below native.
    CHECK(platform::supersample_limit(3840, 2160) == 4.0f);
    CHECK(platform::supersample_limit(2340, 1080) == 4.0f); // phone
    CHECK(std::fabs(platform::supersample_limit(5120, 2880) - 3.2f) < 1e-4f);
    CHECK(std::fabs(platform::supersample_limit(7680, 4320) - 16384.0f / 7680.0f) < 1e-4f);
    CHECK(platform::supersample_limit(20000, 9000) == 1.0f);
    CHECK(platform::supersample_limit(0, 0) == 1.0f);
    // Desktop: always native.
    CHECK(platform::auto_render_scale(3840, 2160, false) == 1.0f);
    // Mobile: about 900 lines of 16:9 game area.
    CHECK(std::fabs(platform::auto_render_scale(2560, 1600, true) - 900.0f / 1440.0f) < 1e-4f); // 16:10 tablet
    CHECK(std::fabs(platform::auto_render_scale(2856, 1280, true) - 900.0f / 1280.0f) < 1e-4f); // 20:9 phone
    CHECK(platform::auto_render_scale(1280, 720, true) == 1.0f);
    CHECK(platform::auto_render_scale(7680, 4320, true) == 0.5f);
    // Dynamic resolution's ceiling with Auto: full resolution up to 1440 lines of 16:9 game area.
    CHECK(platform::dynamic_ceiling(2400, 1080) == 1.0f); // 20:9 phone
    CHECK(platform::dynamic_ceiling(2560, 1600) == 1.0f); // 16:10 tablet: 1440 lines
    CHECK(std::fabs(platform::dynamic_ceiling(3840, 2160) - 1440.0f / 2160.0f) < 1e-4f);
    // MSAA when the config has none: 4x on desktop, 2x on phones and tablets.
    CHECK(platform::default_msaa(false) == 4);
    CHECK(platform::default_msaa(true) == 2);
}

TEST(platform_aspect) {
    using platform::Aspect;
    CHECK(platform::parse_aspect("16:9") == Aspect::Wide);
    CHECK(platform::parse_aspect(" 4:3 ") == Aspect::Classic);
    CHECK(platform::parse_aspect("fill") == Aspect::Fill);
    CHECK(platform::parse_aspect("") == Aspect::Wide);
    CHECK(platform::parse_aspect("21:9") == Aspect::Wide);
    for (Aspect a : {Aspect::Wide, Aspect::Classic, Aspect::Fill})
        CHECK(platform::parse_aspect(platform::aspect_value(a)) == a);
    // 16:9 and 4:3 keep their width in any window.
    CHECK(platform::game_width(Aspect::Wide, 2400, 1080) == 640);
    CHECK(platform::game_width(Aspect::Classic, 2400, 1080) == 480);
    CHECK(platform::game_width(Aspect::Classic, 1024, 768) == 480);
    // Fill: the window's shape, 360 tall; never narrower than 4:3.
    CHECK(platform::game_width(Aspect::Fill, 3840, 2160) == 640);
    CHECK(platform::game_width(Aspect::Fill, 2400, 1080) == 800); // 20:9 phone
    CHECK(platform::game_width(Aspect::Fill, 3440, 1440) == 860); // 21:9 monitor
    CHECK(platform::game_width(Aspect::Fill, 2560, 1600) == 576); // 16:10
    CHECK(platform::game_width(Aspect::Fill, 2048, 1536) == 480); // 4:3 tablet
    CHECK(platform::game_width(Aspect::Fill, 1080, 2400) == 480); // portrait: letterboxed 4:3
    CHECK(platform::game_width(Aspect::Fill, 0, 0) == 640);
}
