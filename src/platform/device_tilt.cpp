#include "platform/device_tilt.h"

#include <SDL3/SDL_video.h>

#include <cmath>
#include <numbers>

namespace gt3r::platform {

namespace {

// RollFilter's tuning: how hard a nearly still roll is smoothed (Hz), how much
// a moving one opens the filter per rad/s of speed, and the smoothing of that
// speed estimate itself.
constexpr float kMinCutoff = 1.0f;
constexpr float kBeta = 2.0f;
constexpr float kRateCutoff = 1.0f;

// Weight of one sample in an exponential low-pass at `cutoff` Hz, `dt` apart.
float euro_alpha(float cutoff, float dt) {
    const float tau = 1.0f / (2.0f * std::numbers::pi_v<float> * cutoff);
    return 1.0f / (1.0f + tau / dt);
}

int orientation_degrees(SDL_DisplayOrientation o) {
    switch (o) {
    case SDL_ORIENTATION_LANDSCAPE: return 90;
    case SDL_ORIENTATION_PORTRAIT_FLIPPED: return 180;
    case SDL_ORIENTATION_LANDSCAPE_FLIPPED: return 270;
    default: return 0; // portrait or unknown
    }
}

} // namespace

int display_rotation_degrees() {
    SDL_DisplayID d = SDL_GetPrimaryDisplay();
    if (!d) return 0;
    SDL_DisplayOrientation cur = SDL_GetCurrentDisplayOrientation(d);
    SDL_DisplayOrientation nat = SDL_GetNaturalDisplayOrientation(d);
    if (cur == SDL_ORIENTATION_UNKNOWN || nat == SDL_ORIENTATION_UNKNOWN) return 0;
    return (orientation_degrees(cur) - orientation_degrees(nat) + 360) % 360;
}

void accel_to_display(const float in[3], float out[3], int rotation_degrees) {
    // Same remapping as Android's Surface.ROTATION_* (SDL derives its
    // orientation values from it): the display frame is the device frame
    // turned by the display rotation.
    const float x = in[0], y = in[1];
    switch (((rotation_degrees % 360) + 360) % 360) {
    case 90: out[0] = -y, out[1] = x; break;
    case 180: out[0] = -x, out[1] = -y; break;
    case 270: out[0] = y, out[1] = -x; break;
    default: out[0] = x, out[1] = y; break;
    }
    out[2] = in[2];
}

float steering_roll(const float a[3]) {
    // Turning clockwise moves the "up" reading towards the display's -X.
    return std::atan2(-a[0], std::sqrt(a[1] * a[1] + a[2] * a[2]));
}

float RollFilter::filter(float roll, double dt) {
    if (dt <= 0.0) return roll;
    if (!primed_) {
        x_ = roll, dx_ = 0.0f, primed_ = true;
        return x_;
    }
    const float fdt = static_cast<float>(dt);
    const float speed = (roll - x_) / fdt;
    dx_ += euro_alpha(kRateCutoff, fdt) * (speed - dx_);
    x_ += euro_alpha(kMinCutoff + kBeta * std::fabs(dx_), fdt) * (roll - x_);
    return x_;
}

} // namespace gt3r::platform
