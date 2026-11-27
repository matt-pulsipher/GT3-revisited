#pragma once

// The camera of GT3's car icon maker (CarIconMaker, presentation §J): the car pictures of the
// event display's gaps, the free practice's P.P. line and the results panel (issue 520).

#include "render/math3d.h"

namespace gt3r::race {

// The icon maker's program constants (NTSC-U 1.10, CarIconMaker's draw 0x1ADCB8).
struct CarIconProgram {
    static constexpr float kWidth = 128, kHeight = 64; // the picture it renders (0x1ADDF0), shown at 64 x 32
    static constexpr float kViewAngle = 0.157f;        // 0x34FFAC: projection distance = height / tan(this) px
    static constexpr float kScale = 35;                // 0x1ADEF8: the car scaled x35 into those pixels
    static constexpr float kYawDeg = 158;              // 0x1ADF20: the car turned about y (degrees)
    static constexpr float kUnit = 0.001f;             // 0x34FFA8: chassis height mm -> m
};

// The picture's camera in the car frame (+X right, +Y up, nose -Z, origin at the wheelbase middle on the ground).
struct CarIconCamera {
    render::Vec3 eye, target;
    float fovy = 0;   // vertical field of view (radians)
    float aspect = 2; // the picture's width / height: the icon is stretched to its rect like the original's
};

// The icon maker's view: the car turned 158 degrees, level, half its chassis height (table 3 +0x16)
// on the axis, at the projection distance / 35: an eye 22 degrees round from the nose on the car's
// left, about 11.5 m out, with a field 9 degrees tall.
CarIconCamera car_icon_camera(float chassis_height_mm);

} // namespace gt3r::race
