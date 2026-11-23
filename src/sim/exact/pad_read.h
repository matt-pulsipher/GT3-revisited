#pragma once

// The pad read 0x210740 [physics §4.4]: the race's input device (RaceInput) turns the controller's state into the 16-byte
// frame phase 3 reads (PadFrame, tick.h). Live devices (device mode 0 or 3) go through the pads' read (0x211430: the
// PlayStation pads by their type) or the steering wheel's (0x2115F8); the replay stream and the recorder (0x1F4660 /
// 0x1F4998) are not part of it.
//
// The device's own read (libpad's buttons, sticks and pressures, the GT Force's axes) and the controller configuration (which
// button or stick a driving function is on: RaceInput +0x144 and its options table, mapped through 0x2E2CF8) are the
// caller's: PadReading holds what the device object has after its read, with the functions already on their channels.

#include "sim/exact/program_data.h"
#include "sim/exact/tick.h"

#include <array>

namespace gt3r::sim::exact {

// The device object's state as 0x211430 / 0x2115F8 read it.
struct PadReading {
    // RaceInput +0x14 bits 0-4: the pad type (libpad's mode id): 4 digital, 5 / 7 analog sticks (the buttons digital), 8 the
    // analog pad with pressure-sensitive buttons; any other: no pad. Bits 5-7: a steering wheel 1..3 (0x2115F8), 0 none.
    u8 type = 8;
    u8 wheel = 0;
    // The driving functions' buttons (vtable +0x28): bit 8 steer left, 9 right, 10 throttle, 11 brake, 12 the third axis
    // (handbrake), 13 back (reverse), 14 shift up, 15 shift down.
    u16 buttons = 0;
    // Per function the analog channel it is on, as the controller configuration's entry gives it (the table at 0x2E2CF8, 6
    // bytes an entry): the low
    // byte the channel, 0x100 / 0x200 the lower / upper half of a stick; -1 none. Functions 0 steer, 1 throttle, 2 brake, 3
    // back, 4 third, 5 shift up, 6 shift down. `channel_a` is the entry's +2 (0x2178C8: the stick, the shifts and the analog
    // pedals of pad types 5 / 7), `channel_b` its +4 (0x217938: the pedals of pad type 8).
    std::array<s16, 7> channel_a{-1, -1, -1, -1, -1, -1, -1};
    std::array<s16, 7> channel_b{-1, -1, -1, -1, -1, -1, -1};
    // The channels (RaceInput +0x38 + 4 c, a byte each): 0..3 the sticks (right x, right y, left x, left y; 0x80 centred), 4..15
    // the pressures (right, left, up, down, triangle, circle, cross, square, L1, R1, L2, R2; 0 released .. 255).
    std::array<u8, 16> channel{0x80, 0x80, 0x80, 0x80};
    // A steering wheel's axes (vtable +0x40 with 16 / 17 / 18): the wheel 0..1023 (0x200 centred), the throttle and brake
    // pedals 0xFF released .. 0.
    u16 wheel_pos = 0x200;
    u8 wheel_throttle = 0xFF, wheel_brake = 0xFF;
};

// The type A configuration's entries (steering on the left stick, throttle Cross, brake Square, back Triangle, the third
// axis Circle, the shifts R2 / L2) into r's channel_a / channel_b.
void type_a_channels(PadReading& r);

// 0x210740's live path for one device: the frame cleared (0x1F4330), then the pads' read (0x211430) and the steering
// wheel's (0x2115F8). `d` holds the stick response curve 0x3416F8.
PadFrame pad_read(const PadReading& r, const ProgramData& d);

// Their parts, by the program's function.
// 0x210EF8: the digital buttons: throttle, brake, third and back at 1 (their analog flags cleared), the shift request.
void pad_buttons(PadFrame& f, u16 buttons);
// 0x210D68: the d-pad steering: kind 0, steer +2 left / -2 right (digital).
void pad_dpad(PadFrame& f, u16 buttons);
// 0x210DA8 (pad type 8): the d-pad's pressures as the steering, kind 3.
void pad_dpad_pressure(PadFrame& f, const PadReading& r);
// 0x210E68: with no steering yet, the configured stick through the response curve, kind 2.
void pad_stick(PadFrame& f, const PadReading& r, const ProgramData& d);
// 0x210F90: a channel's value 0..256: a stick through the response curve (a half: twice its distance from the centre), a
// pressure doubled times 100 / `scale`; both at most 256.
s32 pad_channel(s32 code, u8 value, s32 scale, const ProgramData& d);
// The analog throttle, brake, third and back (0x211050 for pad types 5 / 7, 0x2111C0 for type 8, with their own scales).
void pad_pedals(PadFrame& f, const PadReading& r, const ProgramData& d, bool pressure);
// 0x211398: the analog shift functions (the channel at least half way: +1 up, -1 down).
void pad_shift(PadFrame& f, const PadReading& r, const ProgramData& d);
// 0x2115F8: a steering wheel: buttons, then steer (0x200 - pos) x 9 within +-4096 (kind 4), the pedals beyond their dead travel.
void pad_wheel(PadFrame& f, const PadReading& r);

} // namespace gt3r::sim::exact
