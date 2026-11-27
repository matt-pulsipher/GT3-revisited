// The car icon maker's camera (race/car_icon_camera.h, issue 520): the gaps' and results' car
// pictures seen as GT3 renders them. Disc-free.

#include "test_framework.h"

#include "race/car_icon_camera.h"

#include <cmath>

using namespace gt3r;

namespace {
constexpr float kPi = 3.14159265f;
bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
} // namespace

TEST(car_icon_camera_is_level_on_the_cars_middle) {
    // No pitch: eye and target both at half the chassis height.
    const race::CarIconCamera c = race::car_icon_camera(1300);
    CHECK(near(c.eye.y, 0.65f, 1e-5f));
    CHECK(near(c.target.y, 0.65f, 1e-5f));
    CHECK(near(c.target.x, 0, 1e-6f) && near(c.target.z, 0, 1e-6f));
    // A taller car: the axis moves up with it.
    CHECK(near(race::car_icon_camera(1800).eye.y, 0.9f, 1e-5f));
}

TEST(car_icon_camera_sees_the_nose_and_left_flank) {
    // The car turned 158 degrees: the eye 22 degrees round from straight ahead (nose -Z), on the
    // car's left (-X).
    const race::CarIconCamera c = race::car_icon_camera(1300);
    CHECK(c.eye.z < 0);
    CHECK(c.eye.x < 0);
    const float yaw = std::atan2(-c.eye.x, -c.eye.z) * 180.0f / kPi;
    CHECK(near(yaw, 22.0f, 1e-3f));
}

TEST(car_icon_camera_frames_the_original_picture) {
    // Distance = (64 / tan 0.157) / 35, about 11.55 m; the picture 128 x 64 px (2:1) shows
    // 128 / 35 m by 64 / 35 m at the car's middle.
    const race::CarIconCamera c = race::car_icon_camera(1300);
    const float dist = std::hypot(c.eye.x, c.eye.z);
    CHECK(near(dist, 64.0f / std::tan(0.157f) / 35.0f, 1e-4f));
    CHECK(near(dist, 11.55f, 0.01f));
    CHECK(near(c.aspect, 2.0f, 1e-6f));
    CHECK(near(2.0f * dist * std::tan(c.fovy * 0.5f), 64.0f / 35.0f, 1e-4f));
    CHECK(near(2.0f * dist * std::tan(c.fovy * 0.5f) * c.aspect, 128.0f / 35.0f, 1e-4f));
}
