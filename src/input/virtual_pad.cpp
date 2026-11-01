#include "input/virtual_pad.h"

#include "base/config.h"

#include <algorithm>
#include <cmath>

namespace gt3r {

namespace {
constexpr std::string_view kButtonNames[kPadButtonCount] = {
    "Select", "L3", "R3", "Start", "Up", "Right", "Down", "Left",
    "L2", "R2", "L1", "R1", "Triangle", "Circle", "Cross", "Square",
};
constexpr std::string_view kAxisNames[kPadAxisCount] = {"LX", "LY", "RX", "RY"};
} // namespace

u8 PadState::axis_byte(float v) { return u8(std::lround((std::clamp(v, -1.0f, 1.0f) + 1.0f) * 127.5f)); }

void PadState::set_axis(PadAxis a, float v) {
    const size_t i = static_cast<size_t>(a);
    fine[i] = std::clamp(v, -1.0f, 1.0f);
    axes[i] = axis_byte(v);
}

float PadState::axis_fine(PadAxis a) const {
    const size_t i = static_cast<size_t>(a);
    if (axis_byte(fine[i]) == axes[i]) return fine[i];
    const float b = float(axes[i]) - 128.0f;
    return b / (b < 0 ? 128.0f : 127.0f);
}

std::string_view pad_button_name(PadButton b) { return kButtonNames[static_cast<size_t>(b)]; }
std::string_view pad_axis_name(PadAxis a) { return kAxisNames[static_cast<size_t>(a)]; }

std::optional<PadButton> pad_button_from_name(std::string_view name) {
    std::string n = to_lower(name);
    for (size_t i = 0; i < kPadButtonCount; ++i)
        if (to_lower(kButtonNames[i]) == n) return static_cast<PadButton>(i);
    return std::nullopt;
}

std::optional<PadAxis> pad_axis_from_name(std::string_view name) {
    std::string n = to_lower(name);
    for (size_t i = 0; i < kPadAxisCount; ++i)
        if (to_lower(kAxisNames[i]) == n) return static_cast<PadAxis>(i);
    return std::nullopt;
}

} // namespace gt3r
