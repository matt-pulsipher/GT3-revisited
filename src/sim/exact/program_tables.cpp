// GT3's program tables as values; see program_tables.h. Each table is a struct with named fields (or a plain list where it
// is a lookup), packed back into the table's bytes in field order; the field comments give each one's offset and scale. A
// field named unread_* is one the exact core does not read.

#include "sim/exact/program_tables.h"

#include "base/ee_float.h"

#include <array>
#include <stdexcept>
#include <vector>

namespace gt3r::sim::exact {

namespace {

using ee::F;

// The tables' bytes in order (little-endian words, floats as their bits).
class Pack {
public:
    Pack& operator()(u8 v) {
        b.push_back(v);
        return *this;
    }
    Pack& operator()(s8 v) { return (*this)(u8(v)); }
    Pack& operator()(char v) { return (*this)(u8(v)); }
    Pack& operator()(u16 v) { return (*this)(u8(v))(u8(v >> 8)); }
    Pack& operator()(s16 v) { return (*this)(u16(v)); }
    Pack& operator()(u32 v) { return (*this)(u16(v))(u16(v >> 16)); }
    Pack& operator()(s32 v) { return (*this)(u32(v)); }
    Pack& operator()(F v) { return (*this)(u32(v.bits)); }
    template <class T, size_t N> Pack& operator()(const std::array<T, N>& a) {
        for (const T& x : a) (*this)(x);
        return *this;
    }
    template <class T> Pack& operator()(const T& s) // a table's struct: its pack() below
        requires std::is_class_v<T>
    {
        pack(*this, s);
        return *this;
    }
    std::vector<u8> b;
};

// The static physics block.
struct StaticBlock {
    u8 aero_divisor;                             // +0x00 the slipstream shares' divisor (0: no shares)
    u8 drag_share;                               // +0x01 the drag left in a slipstream
    u8 downforce_share;                          // +0x02 the downforce's share left in a slipstream
    std::array<u8, 2> added_mass;                // +0x03 kg added to the catalogue weight
    u8 slip_build;                               // +0x05 slipstream build-up: 10 / byte (0: 1)
    u8 slip_decay;                               // +0x06 its decay (0: 0.25)
    u8 pitch_tyre;                               // +0x07 the pitch-tyre term, x 0.01 x 2 pi
    u8 engine_soft;                              // +0x08 the engine block's +0xA0, x 0.01
    std::array<u8, 2> unread_09;                 // +0x09 (not read)
    u8 rolling_points;                           // +0x0B the rolling-resistance curve's points (1 .. 8)
    std::array<u8, 8> rolling_x;                 // +0x0C its speeds, x 2 km/h
    std::array<u8, 8> rolling_y;                 // +0x14 its values, x 0.001
    std::array<u8, 16> bump_amplitude;           // +0x1C a surface's road bump amplitude, mm
    std::array<s16, 16> rolling_mu;              // +0x2C a surface's rolling resistance, x 0.01
    std::array<u8, 16> bump_wavelength;          // +0x4C a surface's bump wavelength, dm
    std::array<s8, 16> bump_scaled;              // +0x5C a surface's bump scaling
    std::array<s8, 16> bump_mode;                // +0x6C a surface's bump mode
    u8 brake_heat;                               // +0x7C the brake heat constants: heating (x 9800; 0: 1)
    u8 brake_cool;                               // +0x7D cooling (x 0.1 x 100; 0: 1)
    u8 brake_fade_speed;                         // +0x7E x 1/3.6
    u8 brake_fade;                               // +0x7F x 0.01
    std::array<u8, 4> unread_80;                 // +0x80 (not read)
    std::array<u8, 2> slip_fade;                 // +0x84 the slip effects' fade, front / rear
    std::array<u8, 2> slip_gain_on;              // +0x86 their gain switches
    std::array<u8, 2> slip_gain_base;            // +0x88 their base gains
    std::array<u8, 2> slip_gain_knee;            // +0x8A their knees
    u8 load_knee_low;                            // +0x8C the tyre load knees, x 98 N: low
    u8 load_knee_high;                           // +0x8D high
    u8 unread_8e;                                // +0x8E (not read)
    std::array<u8, 3> tyre_grip;                 // +0x8F a tyre block's grip, x 0.01
    std::array<u8, 14> unread_92;                // +0x92 (not read)
    std::array<u8, 9> condition;                 // +0xA0 the engine condition curve (condition_params)
    std::array<u8, 4> unread_a9;                 // +0xA9 (not read)
    std::array<u8, 2> over_angle;                // +0xAD the oversteer indicator's angles
    std::array<u8, 2> over_gain;                 // +0xAF and gains
    std::array<u8, 4> unread_b1;                 // +0xB1 (not read)
    u8 dirt_steer;                               // +0xB5 the dirt steering aid
    std::array<u8, 2> dirt_cut;                  // +0xB6 the dirt throttle cut
    std::array<u8, 104> unread_b8;               // +0xB8 (not read)
};

constexpr StaticBlock kStaticBlock{
    .aero_divisor = 12,
    .drag_share = 6,
    .downforce_share = 12,
    .added_mass = {65, 55},
    .slip_build = 5,
    .slip_decay = 20,
    .pitch_tyre = 50,
    .engine_soft = 30,
    .unread_09 = {0, 0},
    .rolling_points = 8,
    .rolling_x = {0, 2, 10, 29, 58, 100, 154, 200},
    .rolling_y = {0, 0, 10, 15, 32, 72, 144, 255},
    .bump_amplitude = {0, 10, 10, 24, 20, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    .rolling_mu = {10, 10, 20, 2550, 30, 20, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10},
    .bump_wavelength = {5, 5, 25, 50, 50, 5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10},
    .bump_scaled = {0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    .bump_mode = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    .brake_heat = 7,
    .brake_cool = 20,
    .brake_fade_speed = 100,
    .brake_fade = 5,
    .unread_80 = {0, 0, 0, 0},
    .slip_fade = {20, 20},
    .slip_gain_on = {1, 1},
    .slip_gain_base = {50, 50},
    .slip_gain_knee = {100, 100},
    .load_knee_low = 20,
    .load_knee_high = 60,
    .unread_8e = 100,
    .tyre_grip = {125, 125, 125},
    .unread_92 = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    .condition = {30, 30, 50, 10, 20, 20, 50, 10, 30},
    .unread_a9 = {0, 0, 0, 0},
    .over_angle = {20, 5},
    .over_gain = {30, 140},
    .unread_b1 = {0, 0, 0, 0},
    .dirt_steer = 15,
    .dirt_cut = {100, 0},
    .unread_b8 = {0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 120, 64, 12, 29, 0, 9, 64, 31, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 80, 0, 0, 0, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            100, 100, 100, 100, 100, 100, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 2, 0, 10, 15, 3,
            0, 10, 20, 4, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0,
            0, 0, 0, 0, 0},
};

// A surface class's tyre effect curves on one channel.
struct EffectCurves {
    u8 by_lateral;                               // +0x00 curve a's x: lateral speed (else slip angle)
    u8 a_points;                                 // +0x01 curve a's points
    u8 b_points_neg;                             // +0x02 curve b's points below zero
    u8 b_points_pos;                             // +0x03 and above
    std::array<u8, 4> a_x;                       // +0x04 curve a
    std::array<u8, 4> a_y;                       // +0x08
    std::array<u8, 4> b_neg_x;                   // +0x0C curve b's first half (mirrored to negative x)
    std::array<u8, 4> b_neg_y;                   // +0x10
    std::array<u8, 4> b_pos_x;                   // +0x14 its second half
    std::array<u8, 4> b_pos_y;                   // +0x18
    u8 a_scale;                                  // +0x1C curve a's y scale (/ 2550)
    u8 b_neg_scale;                              // +0x1D curve b's halves' y scales
    u8 b_pos_scale;                              // +0x1E
    u8 by_slip_speed;                            // +0x1F curve b's x: slip speed (else slip ratio)
    u8 base;                                     // +0x20 the base, x 0.01
    u8 ramp_speed;                               // +0x21 the speed ramp's threshold, x 1/3.6
    u8 ramp_gain;                                // +0x22 its gain, x 0.01
    std::array<u8, 5> unread_23;                 // +0x23 (not read)
};

// The tyre effect and engine sound block.
struct EffectBlock {
    std::array<EffectCurves, 12> channel;        // +0x00 3 channels x 4 surface classes
    std::array<u8, 28> thresholds;               // +0x1E0 thresholds (+9 < +0xA, +0x10 < +0x11, +0x17 < +0x18; +2 >= 1)
    std::array<u8, 4> sound_scale;               // +0x1FC the engine sounds' scales (three / 100, one / 10)
    u8 kerb_scale;                               // +0x200 the kerb rumble: scale (x 0.1)
    u8 kerb_x0;                                  // +0x201 x 10 km/h
    u8 kerb_y0;                                  // +0x202 x 0.1
    u8 kerb_x1;                                  // +0x203
    u8 kerb_y1;                                  // +0x204
    std::array<u8, 11> unread_205;               // +0x205 (not read)
};

constexpr EffectBlock kEffectBlock{
    .channel = {{
        {.by_lateral = 1, .a_points = 4, .b_points_neg = 4, .b_points_pos = 4, .a_x = {10, 86, 110, 135},
            .a_y = {0, 152, 204, 255}, .b_neg_x = {9, 80, 148, 255}, .b_neg_y = {0, 100, 203, 203},
            .b_pos_x = {9, 80, 148, 255}, .b_pos_y = {0, 100, 203, 203}, .a_scale = 20, .b_neg_scale = 30,
            .b_pos_scale = 20, .by_slip_speed = 0, .base = 0, .ramp_speed = 3, .ramp_gain = 0,
            .unread_23 = {0, 0, 0, 0, 0}},
        {.by_lateral = 1, .a_points = 4, .b_points_neg = 4, .b_points_pos = 4, .a_x = {14, 30, 118, 127},
            .a_y = {0, 40, 232, 255}, .b_neg_x = {12, 32, 112, 128}, .b_neg_y = {0, 32, 224, 255},
            .b_pos_x = {12, 28, 60, 68}, .b_pos_y = {0, 52, 224, 255}, .a_scale = 4, .b_neg_scale = 2,
            .b_pos_scale = 2, .by_slip_speed = 0, .base = 7, .ramp_speed = 1, .ramp_gain = 30,
            .unread_23 = {0, 0, 0, 0, 0}},
        {.by_lateral = 1, .a_points = 4, .b_points_neg = 4, .b_points_pos = 4, .a_x = {14, 30, 118, 127},
            .a_y = {0, 40, 232, 255}, .b_neg_x = {12, 32, 112, 128}, .b_neg_y = {0, 32, 224, 255},
            .b_pos_x = {12, 28, 60, 68}, .b_pos_y = {0, 52, 224, 255}, .a_scale = 3, .b_neg_scale = 4,
            .b_pos_scale = 3, .by_slip_speed = 0, .base = 0, .ramp_speed = 10, .ramp_gain = 12,
            .unread_23 = {0, 0, 0, 0, 0}},
        {.by_lateral = 1, .a_points = 4, .b_points_neg = 4, .b_points_pos = 4, .a_x = {14, 30, 118, 127},
            .a_y = {0, 40, 232, 255}, .b_neg_x = {12, 32, 112, 128}, .b_neg_y = {0, 32, 224, 255},
            .b_pos_x = {12, 28, 60, 68}, .b_pos_y = {0, 52, 224, 255}, .a_scale = 15, .b_neg_scale = 15,
            .b_pos_scale = 15, .by_slip_speed = 0, .base = 0, .ramp_speed = 0, .ramp_gain = 0,
            .unread_23 = {0, 0, 0, 0, 0}},
        {.by_lateral = 1, .a_points = 4, .b_points_neg = 4, .b_points_pos = 4, .a_x = {4, 30, 54, 83},
            .a_y = {0, 56, 116, 179}, .b_neg_x = {4, 64, 168, 255}, .b_neg_y = {0, 36, 92, 147},
            .b_pos_x = {0, 52, 184, 255}, .b_pos_y = {0, 28, 104, 147}, .a_scale = 5, .b_neg_scale = 8,
            .b_pos_scale = 8, .by_slip_speed = 0, .base = 75, .ramp_speed = 3, .ramp_gain = 0,
            .unread_23 = {0, 0, 0, 0, 0}},
        {.by_lateral = 1, .a_points = 4, .b_points_neg = 4, .b_points_pos = 4, .a_x = {10, 62, 162, 255},
            .a_y = {0, 60, 196, 203}, .b_neg_x = {12, 28, 76, 88}, .b_neg_y = {0, 32, 224, 255},
            .b_pos_x = {12, 20, 48, 72}, .b_pos_y = {0, 52, 224, 255}, .a_scale = 20, .b_neg_scale = 8,
            .b_pos_scale = 8, .by_slip_speed = 0, .base = 50, .ramp_speed = 10, .ramp_gain = 10,
            .unread_23 = {0, 0, 0, 0, 0}},
        {.by_lateral = 1, .a_points = 4, .b_points_neg = 4, .b_points_pos = 4, .a_x = {14, 46, 106, 255},
            .a_y = {0, 96, 192, 187}, .b_neg_x = {12, 32, 112, 128}, .b_neg_y = {0, 32, 224, 255},
            .b_pos_x = {12, 28, 60, 68}, .b_pos_y = {0, 52, 224, 255}, .a_scale = 4, .b_neg_scale = 3,
            .b_pos_scale = 3, .by_slip_speed = 0, .base = 50, .ramp_speed = 30, .ramp_gain = 15,
            .unread_23 = {0, 0, 0, 0, 0}},
        {.by_lateral = 1, .a_points = 4, .b_points_neg = 4, .b_points_pos = 4, .a_x = {14, 30, 118, 127},
            .a_y = {0, 40, 232, 255}, .b_neg_x = {12, 32, 112, 128}, .b_neg_y = {0, 32, 224, 255},
            .b_pos_x = {12, 28, 60, 68}, .b_pos_y = {0, 52, 224, 255}, .a_scale = 15, .b_neg_scale = 15,
            .b_pos_scale = 15, .by_slip_speed = 0, .base = 50, .ramp_speed = 0, .ramp_gain = 0,
            .unread_23 = {0, 0, 0, 0, 0}},
        {.by_lateral = 1, .a_points = 4, .b_points_neg = 4, .b_points_pos = 4, .a_x = {104, 116, 124, 147},
            .a_y = {0, 40, 128, 151}, .b_neg_x = {136, 144, 160, 180}, .b_neg_y = {0, 32, 224, 255},
            .b_pos_x = {144, 152, 168, 188}, .b_pos_y = {0, 52, 224, 255}, .a_scale = 3, .b_neg_scale = 1,
            .b_pos_scale = 1, .by_slip_speed = 1, .base = 30, .ramp_speed = 0, .ramp_gain = 0,
            .unread_23 = {0, 0, 0, 0, 0}},
        {.by_lateral = 1, .a_points = 4, .b_points_neg = 4, .b_points_pos = 4, .a_x = {28, 60, 236, 255},
            .a_y = {0, 40, 232, 255}, .b_neg_x = {12, 32, 112, 128}, .b_neg_y = {0, 32, 224, 255},
            .b_pos_x = {12, 28, 60, 68}, .b_pos_y = {0, 52, 224, 255}, .a_scale = 5, .b_neg_scale = 5,
            .b_pos_scale = 2, .by_slip_speed = 1, .base = 0, .ramp_speed = 1, .ramp_gain = 40,
            .unread_23 = {0, 0, 0, 0, 0}},
        {.by_lateral = 1, .a_points = 4, .b_points_neg = 4, .b_points_pos = 4, .a_x = {48, 88, 236, 255},
            .a_y = {0, 40, 232, 255}, .b_neg_x = {84, 108, 136, 164}, .b_neg_y = {0, 32, 224, 255},
            .b_pos_x = {56, 76, 100, 140}, .b_pos_y = {0, 52, 232, 255}, .a_scale = 1, .b_neg_scale = 1,
            .b_pos_scale = 1, .by_slip_speed = 1, .base = 20, .ramp_speed = 10, .ramp_gain = 10,
            .unread_23 = {0, 0, 0, 0, 0}},
        {.by_lateral = 1, .a_points = 4, .b_points_neg = 4, .b_points_pos = 4, .a_x = {28, 60, 236, 255},
            .a_y = {0, 40, 232, 255}, .b_neg_x = {12, 32, 112, 128}, .b_neg_y = {0, 32, 224, 255},
            .b_pos_x = {12, 28, 60, 68}, .b_pos_y = {0, 52, 224, 255}, .a_scale = 15, .b_neg_scale = 15,
            .b_pos_scale = 15, .by_slip_speed = 0, .base = 0, .ramp_speed = 0, .ramp_gain = 0,
            .unread_23 = {0, 0, 0, 0, 0}},
    }},
    .thresholds = {30, 100, 50, 35, 3, 0, 50, 100, 255, 3, 15, 50, 100, 3, 100, 10, 1, 45, 25, 75, 5, 0, 20,
            1, 32, 90, 150, 20},
    .sound_scale = {13, 13, 20, 25},
    .kerb_scale = 5,
    .kerb_x0 = 1,
    .kerb_y0 = 5,
    .kerb_x1 = 20,
    .kerb_y1 = 43,
    .unread_205 = {10, 60, 0, 16, 0, 0, 0, 0, 0, 0, 0},
};

// The controller settings.
struct ControllerSettings {
    std::array<u8, 6> ramps;                     // +0x00 digital ramps: throttle, brake, third up / down
    u8 analog_jump;                              // +0x06 x 0.01
    std::array<u8, 5> unread_07;                 // +0x07 (not read)
    u8 k1;                                       // +0x0C
    u8 k2;                                       // +0x0D x 0.1
    u8 k3;                                       // +0x0E
    u8 rate_out;                                 // +0x0F the steering rates, x 10 degrees a second: out
    u8 rate_back;                                // +0x10 back
    u8 rate_both;                                // +0x11 both
    u8 rate;                                     // +0x12
    u8 unread_13;                                // +0x13 (not read)
    std::array<u8, 4> gear;                      // +0x14 the gear settings
    std::array<u8, 11> unread_18;                // +0x18 (not read)
    u8 wheel_slew;                               // +0x23 the steering wheel: slew, x 10 degrees
    std::array<u8, 2> wheel_lock;                // +0x24 its locks, degrees
    std::array<u8, 6> unread_26;                 // +0x26 (not read)
    std::array<u8, 2> wheel_throttle;            // +0x2C its throttle pedal curve (type, parameter)
    std::array<u8, 2> unread_2e;                 // +0x2E (not read)
    std::array<u8, 2> wheel_brake;               // +0x30 its brake pedal curve
    std::array<u8, 2> unread_32;                 // +0x32 (not read)
    u8 lat_gain;                                 // +0x34 the steering feel: lateral gain, x 0.01
    std::array<u8, 2> feel_curve;                // +0x35 its curve (type, parameter)
    u8 use_x420;                                 // +0x37
    u8 align_gain;                               // +0x38 the aligning gain, x 0.1
    u8 mode;                                     // +0x39
    u8 pad_gain;                                 // +0x3A x 0.01
    u8 steer_gain;                               // +0x3B x 0.01
    u8 steer_max;                                // +0x3C x 0.1 degree
    u8 slip_gain;                                // +0x3D x 0.01
    u8 slip_max;                                 // +0x3E x 0.1 degree
    std::array<u8, 9> unread_3f;                 // +0x3F (not read)
};

constexpr ControllerSettings kControllerSettings{
    .ramps = {10, 10, 10, 10, 1, 1},
    .analog_jump = 10,
    .unread_07 = {0, 1, 80, 13, 0},
    .k1 = 100,
    .k2 = 50,
    .k3 = 50,
    .rate_out = 3,
    .rate_back = 12,
    .rate_both = 12,
    .rate = 12,
    .unread_13 = 0,
    .gear = {1, 5, 0, 0},
    .unread_18 = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 50},
    .wheel_slew = 9,
    .wheel_lock = {8, 15},
    .unread_26 = {12, 20, 0, 0, 0, 0},
    .wheel_throttle = {1, 50},
    .unread_2e = {0, 0},
    .wheel_brake = {1, 50},
    .unread_32 = {0, 0},
    .lat_gain = 200,
    .feel_curve = {0, 100},
    .use_x420 = 1,
    .align_gain = 200,
    .mode = 1,
    .pad_gain = 10,
    .steer_gain = 0,
    .steer_max = 50,
    .slip_gain = 0,
    .slip_max = 40,
    .unread_3f = {0, 0, 0, 0, 0, 0, 0, 0, 0},
};

// A built-in AI driver parameter set [ai §9.1].
struct DriverSet {
    u8 brake;                                    // +0x00 the braking factor: 100 / byte
    u8 speed_scale;                              // +0x01 the corner speeds' scale, x 0.01
    u8 speed_offset;                             // +0x02 their offset, km/h + 100
    u8 yaw_gain;                                 // +0x03 x 5 x 0.01
    u8 sat_throttle;                             // +0x04 x 0.01
    u8 sat_brake;                                // +0x05 x 0.01
    u8 look_speed;                               // +0x06 x 2 km/h
    u8 look_min;                                 // +0x07
    u8 look_max;                                 // +0x08
    u8 aid_a_gain;                               // +0x09 the aids: a's gain, x 0.01
    u8 aid_a_threshold;                          // +0x0A x 0.01
    u8 tcs_lat_a;                                // +0x0B x 0.1
    u8 tcs_lat_b;                                // +0x0C x 0.01
    u8 tcs_gain;                                 // +0x0D x 0.1
    u8 front_weight;                             // +0x0E x 0.01
    u8 corner_bonus;                             // +0x0F km/h
    u8 steer_rate;                               // +0x10 x 10 degrees
    u8 bonus_ticks;                              // +0x11 x 10
    u8 pit_speed;                                // +0x12 km/h (0: 80)
    u8 aid_wheels;                               // +0x13 the aids' wheels, two bits each: a, d, c, e
    u8 aid_d_gain;                               // +0x14 x 0.01
    u8 aid_c_gain;                               // +0x15 x 0.01
    u8 aid_c_threshold;                          // +0x16 x 0.01
    u8 aid_e_gain;                               // +0x17 x 0.01
    u8 tcs_scale;                                // +0x18 x 0.01 (0, 100: 1)
    u8 start_back;                               // +0x19 a time trial's / free run's flying start: x 10 m before the line (0: none)
    u8 start_speed;                              // +0x1A its speed, x 5 km/h (0: none)
    std::array<u8, 5> unread_1b;                 // +0x1B (not read)
};

constexpr std::array<DriverSet, 3> kDriverSets{{
    {.brake = 100, .speed_scale = 100, .speed_offset = 100, .yaw_gain = 24, .sat_throttle = 100, .sat_brake = 0,
        .look_speed = 90, .look_min = 15, .look_max = 40, .aid_a_gain = 15, .aid_a_threshold = 5, .tcs_lat_a = 10,
        .tcs_lat_b = 0, .tcs_gain = 60, .front_weight = 100, .corner_bonus = 20, .steer_rate = 9, .bonus_ticks = 0,
        .pit_speed = 80, .aid_wheels = 0, .aid_d_gain = 0, .aid_c_gain = 0, .aid_c_threshold = 0, .aid_e_gain = 0,
        .tcs_scale = 0, .start_back = 0, .start_speed = 0, .unread_1b = {0, 0, 0, 0, 0}},
    {.brake = 100, .speed_scale = 100, .speed_offset = 100, .yaw_gain = 24, .sat_throttle = 100, .sat_brake = 0,
        .look_speed = 90, .look_min = 15, .look_max = 40, .aid_a_gain = 15, .aid_a_threshold = 5, .tcs_lat_a = 10,
        .tcs_lat_b = 0, .tcs_gain = 60, .front_weight = 100, .corner_bonus = 20, .steer_rate = 9, .bonus_ticks = 0,
        .pit_speed = 80, .aid_wheels = 0, .aid_d_gain = 0, .aid_c_gain = 0, .aid_c_threshold = 0, .aid_e_gain = 0,
        .tcs_scale = 0, .start_back = 0, .start_speed = 0, .unread_1b = {0, 0, 0, 0, 0}},
    {.brake = 100, .speed_scale = 100, .speed_offset = 100, .yaw_gain = 24, .sat_throttle = 100, .sat_brake = 0,
        .look_speed = 90, .look_min = 30, .look_max = 60, .aid_a_gain = 0, .aid_a_threshold = 0, .tcs_lat_a = 10,
        .tcs_lat_b = 0, .tcs_gain = 60, .front_weight = 100, .corner_bonus = 20, .steer_rate = 9, .bonus_ticks = 0,
        .pit_speed = 80, .aid_wheels = 0, .aid_d_gain = 0, .aid_c_gain = 0, .aid_c_threshold = 0, .aid_e_gain = 0,
        .tcs_scale = 0, .start_back = 0, .start_speed = 0, .unread_1b = {0, 0, 0, 0, 0}},
}};

// The car spec's default curves (6 points each, two bytes after each).
struct SpecCurves {
    std::array<u8, 6> lock_x;                    // +0x00 the steering-wheel lock curve's default: x
    std::array<u8, 2> pad_06;                    // +0x06
    std::array<u8, 6> lock_y;                    // +0x08 y
    std::array<u8, 2> pad_0e;                    // +0x0E
    std::array<u8, 6> curve_b_x;                 // +0x10 the tyre curve B's default (the curve B table): x
    std::array<u8, 2> pad_16;                    // +0x16
    std::array<u8, 6> curve_b_y;                 // +0x18 y
};

constexpr SpecCurves kSpecCurves{
    .lock_x = {0, 32, 76, 112, 166, 255},
    .pad_06 = {0, 0},
    .lock_y = {255, 202, 138, 94, 62, 47},
    .pad_0e = {0, 0},
    .curve_b_x = {0, 31, 62, 105, 165, 255},
    .pad_16 = {0, 0},
    .curve_b_y = {0, 28, 61, 114, 180, 196},
};

// A HUD message: its number and its display time in steps.
struct HudMessage {
    u32 id;                                      // +0x00
    u32 steps;                                   // +0x04
};

// The HUD messages, NTSC-U's and NTSC-J's (60 steps a second).
constexpr std::array<HudMessage, 16> kHudMessagesNtsc{{
    {.id = 0, .steps = 0},
    {.id = 1, .steps = 0},
    {.id = 2, .steps = 0},
    {.id = 3, .steps = 0},
    {.id = 4, .steps = 0},
    {.id = 5, .steps = 0},
    {.id = 6, .steps = 120},
    {.id = 7, .steps = 120},
    {.id = 8, .steps = 120},
    {.id = 9, .steps = 120},
    {.id = 10, .steps = 0},
    {.id = 11, .steps = 0},
    {.id = 12, .steps = 120},
    {.id = 13, .steps = 255},
    {.id = 14, .steps = 255},
    {.id = 15, .steps = 0},
}};

// The HUD messages, PAL's (50 steps a second).
constexpr std::array<HudMessage, 16> kHudMessagesPal{{
    {.id = 0, .steps = 0},
    {.id = 1, .steps = 0},
    {.id = 2, .steps = 0},
    {.id = 3, .steps = 0},
    {.id = 4, .steps = 0},
    {.id = 5, .steps = 0},
    {.id = 6, .steps = 100},
    {.id = 7, .steps = 100},
    {.id = 8, .steps = 100},
    {.id = 9, .steps = 100},
    {.id = 10, .steps = 0},
    {.id = 11, .steps = 0},
    {.id = 12, .steps = 100},
    {.id = 13, .steps = 255},
    {.id = 14, .steps = 255},
    {.id = 15, .steps = 0},
}};

// A tyre effect kind's particle parameters (written into the course's Env0 block).
struct EffectParam {
    u8 size;                                     // +0x00 x 0.01
    u8 size2;                                    // +0x01 x 0.1
    u8 growth;                                   // +0x02 x 0.01
    u8 growth2;                                  // +0x03
    u8 growth3;                                  // +0x04 x 0.01
    u8 growth4;                                  // +0x05 x 0.01
    u8 growth5;                                  // +0x06 x 0.01
    u8 life;                                     // +0x07 steps
};

constexpr std::array<EffectParam, 11> kEffectParams{{
    {.size = 30, .size2 = 24, .growth = 90, .growth2 = 114, .growth3 = 60, .growth4 = 0, .growth5 = 100, .life = 0},
    {.size = 50, .size2 = 20, .growth = 20, .growth2 = 30, .growth3 = 30, .growth4 = 0, .growth5 = 100, .life = 60},
    {.size = 60, .size2 = 26, .growth = 15, .growth2 = 30, .growth3 = 10, .growth4 = 0, .growth5 = 100, .life = 60},
    {.size = 60, .size2 = 27, .growth = 15, .growth2 = 30, .growth3 = 10, .growth4 = 0, .growth5 = 100, .life = 60},
    {.size = 70, .size2 = 30, .growth = 15, .growth2 = 30, .growth3 = 10, .growth4 = 0, .growth5 = 100, .life = 32},
    {.size = 100, .size2 = 32, .growth = 126, .growth2 = 246, .growth3 = 4, .growth4 = 51, .growth5 = 100, .life = 6},
    {.size = 0, .size2 = 0, .growth = 0, .growth2 = 0, .growth3 = 0, .growth4 = 0, .growth5 = 0, .life = 0},
    {.size = 0, .size2 = 0, .growth = 0, .growth2 = 0, .growth3 = 0, .growth4 = 0, .growth5 = 0, .life = 0},
    {.size = 0, .size2 = 0, .growth = 0, .growth2 = 0, .growth3 = 0, .growth4 = 0, .growth5 = 0, .life = 0},
    {.size = 0, .size2 = 0, .growth = 0, .growth2 = 0, .growth3 = 0, .growth4 = 0, .growth5 = 0, .life = 0},
    {.size = 0, .size2 = 0, .growth = 0, .growth2 = 0, .growth3 = 0, .growth4 = 0, .growth5 = 0, .life = 0},
}};

// A surface's class for the dirt lock curve (0 .. 2).
constexpr std::array<u8, 16> kSurfaceClasses = {
    0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 2, 2, 2, 2,
};

// A material's softness: 1 grass, gravel, sand.
constexpr std::array<u32, 16> kSoftMaterials = {
    0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// Per requested AI line 0 .. 7 the six lines tried, -1 ending the row.
constexpr std::array<s8, 48> kLineFallback = {
    -1, -1, -1, -1, 0, 0,
    1, 6, 2, -1, 0, 0,
    2, 6, 1, -1, 0, 0,
    3, -1, -1, -1, 0, 0,
    4, -1, -1, -1, 0, 0,
    -1, -1, -1, -1, 0, 0,
    6, 1, 2, -1, 0, 0,
    7, 6, -1, -1, 0, 0,
};

// The candidates for a car's initial line, in the order they are tried.
constexpr std::array<s32, 3> kInitialLines = {
    6, 2, 1,
};

// The order the line loader goes through the lines.
constexpr std::array<u8, 8> kLineOrder = {
    6, 0, 1, 2, 5, 3, 4, 7,
};

// A gear kind per gear record +0x10 byte.
constexpr std::array<u32, 88> kGearKinds = {
    19, 20, 21, 22, 23, 24, 25, 0, 26, 27, 28,
    29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 50, 51, 52, 53, 56, 57, 58,
    59, 60, 61, 62, 63, 64, 65, 88, 88, 66, 56,
    57, 58, 59, 60, 61, 62, 63, 64, 65, 88, 88,
    0, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76,
    88, 88, 77, 67, 68, 69, 70, 71, 72, 73, 74,
    75, 76, 88, 88, 0, 78, 79, 80, 81, 82, 24,
};

// The tyre blocks' surface classes, a byte per surface.
constexpr std::array<u8, 16> kSpecSurfaceClasses = {
    0, 0, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 2, 2, 2, 2,
};

// The differential type letters.
constexpr std::array<char, 8> kLsdTypes = {'f', 'r', 'n', 'm', 'h', 'v', 'y', 't'};

// The machine test's sector starts, metres (the last: none).
constexpr std::array<F, 4> kMachineSectors = {
    F::raw(0x43C80000), F::raw(0x447A0000), F::raw(0x45D5E800), F::raw(0xBF800000),
};

// The sticks' response curve: a stick position's value.
constexpr std::array<u8, 256> kStickCurve = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 3, 5, 6, 8, 10, 11, 13, 15, 16, 18, 20, 21, 23, 25, 26,
    28, 30, 32, 33, 35, 37, 38, 40, 42, 43, 45, 47, 48, 50, 52, 53,
    55, 57, 58, 60, 62, 64, 65, 67, 69, 70, 72, 74, 75, 77, 79, 80,
    82, 84, 85, 87, 89, 90, 92, 94, 96, 97, 99, 101, 102, 104, 106, 107,
    109, 111, 112, 114, 116, 117, 119, 121, 122, 124, 126, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 129, 131, 133, 134, 136, 138, 139, 141, 143, 144, 146,
    148, 149, 151, 153, 154, 156, 158, 160, 161, 163, 165, 166, 168, 170, 171, 173,
    175, 176, 178, 180, 181, 183, 185, 186, 188, 190, 192, 193, 195, 197, 198, 200,
    202, 203, 205, 207, 208, 210, 212, 213, 215, 217, 218, 220, 222, 224, 225, 227,
    229, 230, 232, 234, 235, 237, 239, 240, 242, 244, 245, 247, 249, 250, 252, 254,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
};

// A surface's tyre effect kind.
constexpr std::array<u32, 16> kEffectKinds = {
    0, 0, 1, 2, 3, 4, 5, 0, 0, 0, 0, 0, 5, 5, 5, 5,
};

// The tyre effect scales.
constexpr std::array<F, 8> kEffectScales = {
    F::raw(0x3F7ECD6C), F::raw(0x3B906E8D), F::raw(0x3DF2D44E), F::raw(0x3F0298AB),
    F::raw(0x3D1062B9), F::raw(0x3F4468EC), F::raw(0x3E8D7A9A), F::raw(0x3F711F4F),
};

// ---- packing (the tables' byte order) ----

void pack(Pack& p, const StaticBlock& s) {
    p(s.aero_divisor)(s.drag_share)(s.downforce_share)(s.added_mass)(s.slip_build)(s.slip_decay)(s.pitch_tyre)
        (s.engine_soft)(s.unread_09)(s.rolling_points)(s.rolling_x)(s.rolling_y)(s.bump_amplitude)(s.rolling_mu)
        (s.bump_wavelength)(s.bump_scaled)(s.bump_mode)(s.brake_heat)(s.brake_cool)(s.brake_fade_speed)(s.brake_fade)
        (s.unread_80)(s.slip_fade)(s.slip_gain_on)(s.slip_gain_base)(s.slip_gain_knee)(s.load_knee_low)
        (s.load_knee_high)(s.unread_8e)(s.tyre_grip)(s.unread_92)(s.condition)(s.unread_a9)(s.over_angle)(s.over_gain)
        (s.unread_b1)(s.dirt_steer)(s.dirt_cut)(s.unread_b8);
}

void pack(Pack& p, const EffectCurves& s) {
    p(s.by_lateral)(s.a_points)(s.b_points_neg)(s.b_points_pos)(s.a_x)(s.a_y)(s.b_neg_x)(s.b_neg_y)(s.b_pos_x)
        (s.b_pos_y)(s.a_scale)(s.b_neg_scale)(s.b_pos_scale)(s.by_slip_speed)(s.base)(s.ramp_speed)(s.ramp_gain)
        (s.unread_23);
}

void pack(Pack& p, const EffectBlock& s) {
    p(s.channel)(s.thresholds)(s.sound_scale)(s.kerb_scale)(s.kerb_x0)(s.kerb_y0)(s.kerb_x1)(s.kerb_y1)(s.unread_205);
}

void pack(Pack& p, const ControllerSettings& s) {
    p(s.ramps)(s.analog_jump)(s.unread_07)(s.k1)(s.k2)(s.k3)(s.rate_out)(s.rate_back)(s.rate_both)(s.rate)
        (s.unread_13)(s.gear)(s.unread_18)(s.wheel_slew)(s.wheel_lock)(s.unread_26)(s.wheel_throttle)(s.unread_2e)
        (s.wheel_brake)(s.unread_32)(s.lat_gain)(s.feel_curve)(s.use_x420)(s.align_gain)(s.mode)(s.pad_gain)
        (s.steer_gain)(s.steer_max)(s.slip_gain)(s.slip_max)(s.unread_3f);
}

void pack(Pack& p, const DriverSet& s) {
    p(s.brake)(s.speed_scale)(s.speed_offset)(s.yaw_gain)(s.sat_throttle)(s.sat_brake)(s.look_speed)(s.look_min)
        (s.look_max)(s.aid_a_gain)(s.aid_a_threshold)(s.tcs_lat_a)(s.tcs_lat_b)(s.tcs_gain)(s.front_weight)
        (s.corner_bonus)(s.steer_rate)(s.bonus_ticks)(s.pit_speed)(s.aid_wheels)(s.aid_d_gain)(s.aid_c_gain)
        (s.aid_c_threshold)(s.aid_e_gain)(s.tcs_scale)(s.start_back)(s.start_speed)(s.unread_1b);
}

void pack(Pack& p, const SpecCurves& s) {
    p(s.lock_x)(s.pad_06)(s.lock_y)(s.pad_0e)(s.curve_b_x)(s.pad_16)(s.curve_b_y);
}

void pack(Pack& p, const HudMessage& s) {
    p(s.id)(s.steps);
}

void pack(Pack& p, const EffectParam& s) {
    p(s.size)(s.size2)(s.growth)(s.growth2)(s.growth3)(s.growth4)(s.growth5)(s.life);
}


// ---- the tables' bytes -----------------------------------------------------------------------------------------------

using T = ProgramData::Table;

template <class V> std::vector<u8> packed(const V& v, u32 size) {
    Pack p;
    p(v);
    if (p.b.size() != size) throw std::logic_error("program_tables: a table packs to the wrong size");
    return std::move(p.b);
}

struct Store {
    std::array<std::vector<u8>, ProgramData::kTables> ntsc_u; // NTSC-U's (and NTSC-J's, and PAL's but for the messages)
    std::vector<u8> messages_pal;
};

Store build() {
    Store s;
    auto set = [&](T t, std::vector<u8> b) { s.ntsc_u[size_t(t)] = std::move(b); };
    set(T::Static, packed(kStaticBlock, ProgramData::size(T::Static)));
    set(T::Effects, packed(kEffectBlock, ProgramData::size(T::Effects)));
    set(T::Controller, packed(kControllerSettings, ProgramData::size(T::Controller)));
    set(T::SurfaceClass, packed(kSurfaceClasses, ProgramData::size(T::SurfaceClass)));
    set(T::Soft, packed(kSoftMaterials, ProgramData::size(T::Soft)));
    set(T::DriverSets, packed(kDriverSets, ProgramData::size(T::DriverSets)));
    set(T::LineFallback, packed(kLineFallback, ProgramData::size(T::LineFallback)));
    set(T::InitialLines, packed(kInitialLines, ProgramData::size(T::InitialLines)));
    set(T::LineOrder, packed(kLineOrder, ProgramData::size(T::LineOrder)));
    set(T::GearKinds, packed(kGearKinds, ProgramData::size(T::GearKinds)));
    std::vector<u8> curves = packed(kSpecCurves, ProgramData::size(T::SpecCurves));
    set(T::SpecCurveB, std::vector<u8>(curves.begin() + 0x10, curves.end())); // curve B: the spec curves' +0x10 ..
    set(T::SpecCurves, std::move(curves));
    set(T::SpecSurfaceClass, packed(kSpecSurfaceClasses, ProgramData::size(T::SpecSurfaceClass)));
    set(T::LsdTypes, packed(kLsdTypes, ProgramData::size(T::LsdTypes)));
    set(T::Messages, packed(kHudMessagesNtsc, ProgramData::size(T::Messages)));
    set(T::MachineSectors, packed(kMachineSectors, ProgramData::size(T::MachineSectors)));
    set(T::StickCurve, packed(kStickCurve, ProgramData::size(T::StickCurve)));
    set(T::EffectKinds, packed(kEffectKinds, ProgramData::size(T::EffectKinds)));
    set(T::EffectScales, packed(kEffectScales, ProgramData::size(T::EffectScales)));
    set(T::EffectParams, packed(kEffectParams, ProgramData::size(T::EffectParams)));
    s.messages_pal = packed(kHudMessagesPal, ProgramData::size(T::Messages));
    return s;
}

const Store& store() {
    static const Store s = build();
    return s;
}

} // namespace

bool gt3_table_differs(ProgramData::Table t, Program p) { return t == T::Messages && p == Program::Pal; }

ByteSpan gt3_table(ProgramData::Table t, Program p) {
    const Store& s = store();
    const std::vector<u8>& b = gt3_table_differs(t, gt3_release(p)) ? s.messages_pal : s.ntsc_u[size_t(t)];
    return ByteSpan(b.data(), b.size());
}

u64 gt3_tables_hash() {
    u64 h = 1469598103934665603ull; // FNV-1a
    auto mix = [&](ByteSpan b) {
        for (u8 c : b) h = (h ^ c) * 1099511628211ull;
    };
    for (int i = 0; i < ProgramData::kTables; ++i) mix(gt3_table(T(i)));
    mix(gt3_table(T::Messages, Program::Pal));
    return h;
}

} // namespace gt3r::sim::exact
