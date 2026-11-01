#pragma once

#include "base/bytes.h"

#include <array>
#include <optional>
#include <string_view>

namespace gt3r {

// The game was written against a DualShock 2: 16 buttons (12 of which
// report 8-bit pressure), two 8-bit analog sticks, and two rumble motors.
// The engine always talks to this virtual pad; physical devices are mapped
// onto it by the binding layer, so every original control behavior
// (including pressure-sensitive throttle/brake) is preserved.
enum class PadButton : u8 {
    // libpad bit order
    Select, L3, R3, Start, Up, Right, Down, Left,
    L2, R2, L1, R1, Triangle, Circle, Cross, Square,
    Count
};
constexpr size_t kPadButtonCount = static_cast<size_t>(PadButton::Count);

enum class PadAxis : u8 { LX, LY, RX, RY, Count };
constexpr size_t kPadAxisCount = static_cast<size_t>(PadAxis::Count);

constexpr bool is_pressure_sensitive(PadButton b) {
    return b >= PadButton::Up && b <= PadButton::Square;
}

struct PadState {
    u16 buttons = 0;                            // bit set = pressed
    std::array<u8, kPadButtonCount> pressure{}; // 0..255 (digital buttons: 0 or 255)
    std::array<u8, kPadAxisCount> axes{128, 128, 128, 128}; // 0..255, 128 = center
    // The axes before their 8-bit read (-1..1), for inputs finer than a DualShock 2's stick (a
    // wheel's 256 steps would be ~3.5 degrees each on a 900-degree wheel). It counts only where it
    // reads as the byte (set_axis); whatever sets the byte alone leaves it stale.
    std::array<float, kPadAxisCount> fine{};
    bool connected = false;

    bool down(PadButton b) const { return buttons & (1u << static_cast<unsigned>(b)); }
    u8 press(PadButton b) const { return pressure[static_cast<size_t>(b)]; }
    u8 axis(PadAxis a) const { return axes[static_cast<size_t>(a)]; }
    // -1..1 convenience for engine code.
    float axis_f(PadAxis a) const { return (static_cast<float>(axis(a)) - 128.0f) / 127.0f; }
    // An axis at its full resolution, -1..1: its fine value, or the byte's (-1 at 0, 1 at 255).
    float axis_fine(PadAxis a) const;
    // Both: v (-1..1) and its byte.
    void set_axis(PadAxis a, float v);
    static u8 axis_byte(float v);
};

// Rumble request from the game: DS2 small motor is on/off, large is 0..255.
struct PadRumble {
    bool small = false;
    u8 large = 0;
};

std::string_view pad_button_name(PadButton b);
std::string_view pad_axis_name(PadAxis a);
std::optional<PadButton> pad_button_from_name(std::string_view name);
std::optional<PadAxis> pad_axis_from_name(std::string_view name);

} // namespace gt3r
