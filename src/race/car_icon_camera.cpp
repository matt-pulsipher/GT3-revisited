#include "race/car_icon_camera.h"

#include <cmath>

namespace gt3r::race {

CarIconCamera car_icon_camera(float chassis_height_mm) {
    using P = CarIconProgram;
    // The projection (0x1ADED0): the picture's own pixels, distance d = height / tan(0.157); the
    // car x35 at d in front of the eye, so it sits d / 35 m away and the picture is 128 / 35 m
    // wide at the car's middle.
    const float d = P::kHeight / std::tan(P::kViewAngle);
    const float distance = d / P::kScale;
    // The car's turn of 158 degrees, seen from the eye: the eye 180 - 158 = 22 degrees round from
    // straight ahead of the nose, towards the car's left (the original's pictures: the nose and
    // the left flank).
    const float a = (180.0f - P::kYawDeg) * 3.14159265f / 180.0f;
    // No pitch: the eye level with the car's middle, half its chassis height up (0x1ADF1C).
    const float mid = chassis_height_mm * P::kUnit * 0.5f;
    CarIconCamera c;
    c.target = {0, mid, 0};
    c.eye = {-distance * std::sin(a), mid, -distance * std::cos(a)};
    c.fovy = 2.0f * std::atan(P::kHeight * 0.5f / d);
    c.aspect = P::kWidth / P::kHeight;
    return c;
}

} // namespace gt3r::race
