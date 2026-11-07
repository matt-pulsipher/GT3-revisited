#include "audio/car_sound_model.h"

#include <algorithm>
#include <cmath>

namespace gt3r::car_sound {

namespace {

// Linear map of |x| from [x0, x1] to [y0, y1], held at the ends.
float ramp(float x, float x0, float x1, float y0, float y1) {
    x = std::fabs(x);
    if (x < x0) return y0;
    if (x > x1) return y1;
    return y0 + (x - x0) * (y1 - y0) / (x1 - x0);
}

} // namespace

Wind wind(float u, float y, float cd) {
    using namespace tuning;
    Wind w;
    w.gain = std::min(kWindCoef * cd * u * u * (1.0f - y), kWindGainMax);
    w.rpm = int(ramp(u, kWindSpeedLo, kWindSpeedHi, kWindRpmLo, kWindRpmHi));
    return w;
}

Loop in_car(float u, float y, float cd) {
    using namespace tuning;
    Loop l;
    l.gain = std::min(kInCarCoef * cd * u * u * (1.0f - y), kInCarGainMax);
    l.pitch = ramp(u, kInCarSpeedLo, kInCarSpeedHi, kInCarPitchLo, kInCarPitchHi);
    return l;
}

Loop whine(bool enabled, int clutch_state, float torque, float ratio, float first_ratio, float shaft) {
    using namespace tuning;
    if (!enabled || clutch_state == 0) return {0.0f, 1.0f};
    float t = std::min(std::fabs(torque), kWhineTorqueMax);
    float denom = kWhineTorqueMax * first_ratio;
    float x = denom != 0 ? t * ratio / denom : 0.0f;
    Loop l;
    l.gain = kWhineGainLo + (kWhineGainHi - kWhineGainLo) * x;
    l.pitch = kWhinePitchLo + std::fabs(shaft) * (kWhinePitchHi - kWhinePitchLo) / kWhineShaftMax;
    return l;
}

void Turbo::step(bool turbo, float throttle, float prev_load, float load, float torque, int lag0, int lag1) {
    (void)load;
    if (!turbo) {
        level_ = 0;
        return;
    }
    // Blow-off: fires once when the throttle drops under 75 %, with the boost
    // load above 0.8 setting the strength; re-armed above 90 % throttle. The
    // trigger waits for tick(), the one-shot for the next displayed frame (one
    // sound per firing step, whatever the display rate).
    if (!fired_) {
        if (throttle < 0.75f) {
            trigger_ = std::max(trigger_, std::max(prev_load - 0.8f, 0.0f) / 256.0f);
            fired_ = true;
        }
    } else if (throttle > 0.9f) {
        fired_ = false;
    }
    // Spool pitch follows the larger of the two stages' lag states.
    spool_ = u16(std::max(lag0 & 0xFFFF, lag1));
    // Spool level while the load is above 0.75: (load - 0.75), scaled down
    // below 588 N m of torque, squared; 1 otherwise (silent after the frame's scale).
    float over = prev_load - 0.75f;
    if (over <= 0 || torque <= 0) {
        level_ = 1;
        return;
    }
    float v = torque < 588.0f ? over * torque * (1.0f / 588.0f) : over;
    level_ = u8(std::clamp(int(v * v * 128.0f), 1, 127));
}

void Turbo::tick() {
    level_ = u8(std::min(127, int(level_ * tuning::kTurboLevelScale)));
    blow_off_ = std::max(blow_off_, trigger_ * tuning::kBlowOffScale);
    trigger_ = 0;
}

} // namespace gt3r::car_sound
