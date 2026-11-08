#pragma once

// Steering by tilting: turns raw accelerometer readings into a roll angle.
//
// SDL reports accelerometer data in the sensor's *natural* frame (for a
// phone: portrait; -X..+X = left..right, -Y..+Y = bottom..top, +Z towards
// the viewer; at rest the reading points away from the earth) and does not
// rotate it with the display. A phone held in landscape therefore has to be
// remapped to the display frame before "rolling like a steering wheel"
// means anything.

namespace gt3r::platform {

// Clockwise rotation of the current display orientation relative to the
// device's natural one, in degrees (0, 90, 180, 270). 0 when unknown.
int display_rotation_degrees();

// Rotates an accelerometer reading from natural device axes into display
// axes (x right, y up on the screen as currently shown).
void accel_to_display(const float in[3], float out[3], int rotation_degrees);

// Roll around the axis pointing at the player, in radians: positive when
// the device or pad is turned clockwise as seen by the player (steer right).
float steering_roll(const float accel[3]);

// A 1€ (one-euro) filter (Casiez, Roussel and Vogel, CHI 2012) for the roll:
// an exponential low-pass whose cutoff follows the signal's speed, so a
// nearly still reading is smoothed hard (sensor jitter stays out of the
// steering and the on-screen meter) while a fast turn is followed almost
// raw, without lag.
class RollFilter {
public:
    // The filtered roll. `dt` is the frame time in seconds; the first call
    // (or one after reset()) returns the input as it is.
    float filter(float roll, double dt);
    void reset() { primed_ = false; }

private:
    float x_ = 0.0f;  // filtered roll
    float dx_ = 0.0f; // filtered roll speed
    bool primed_ = false;
};

} // namespace gt3r::platform
