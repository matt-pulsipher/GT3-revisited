#pragma once

// Fixed-tick simulation helpers: an accumulator that turns variable frame
// times into whole simulation ticks, and interpolation of vehicle states
// between the last two ticks for rendering.

#include "sim/sim_types.h"

namespace gt3r::race {

class FixedStep {
public:
    explicit FixedStep(double hz = sim::kDefaultTickHz) : step_(1.0 / hz) {}

    // Adds frame time; returns the number of ticks to run now (at most
    // `max_ticks`: after a long stall the excess time is dropped instead of
    // making the game spiral).
    int advance(double frame_dt, int max_ticks = 8);
    // Ticks paced by a shared clock instead (online races): `clock_ticks` ticks are due by now
    // and `done` have run. Returns the number to run now (at most `max_ticks`) and counts them
    // in `done`; alpha() is then the clock's way into the tick after them (0..1). Frame times
    // and the clock's ticks fall in different places: the blend has to come from the clock too.
    int follow(double clock_ticks, long long& done, int max_ticks);
    // Fraction (0..1) of a tick elapsed since the last tick: render the
    // state interpolated between previous and current by this amount.
    float alpha() const { return float(accum_ / step_); }
    double step() const { return step_; }
    void reset() { accum_ = 0; }

private:
    double step_;
    double accum_ = 0;
};

// Frame times for a display that shows frames at a steady rate. The clock read at the start of
// a frame wanders by a millisecond or more against the refresh the frame is shown on (the
// driver queues frames; a compositor paces them), and stepping by it moves the world unevenly
// from one shown frame to the next: a vibration while the view turns (cornering), however the
// ticks are blended. Steps by the frame period instead (a slow average of the frames that took
// about one), a whole number of periods a frame (a missed refresh steps two), and pays the
// clock's lead or lag back a little each frame, so the total follows the clock. Where frames
// keep to no period (variable refresh with the frame rate changing, a new refresh rate for its
// first frames, readings that wander too far to tell) it steps by the clock.
class FramePacer {
public:
    double step(double frame_dt);
    double period() const { return period_; }
    bool locked() const { return locked_; } // stepping by the period

private:
    double period_ = 0;   // the frame period in use (s), 0 until the first frame
    double owed_ = 0;     // the clock's time not yet in the period's steps (s; negative: ahead)
    double behind_ = 0;   // the clock's time not yet stepped (s; negative: stepped ahead)
    double spread_ = 0;   // how often the clock is far from whole periods (an average of 0 / 1)
    int off_ = 0;         // frames in a row far from the period (the rate changed)
    double off_time_ = 0; // and their time (s)
    int frames_ = 0;      // frames in the period's mean (up to 64)
    int short_ = 0;       // frames in a row well under a period
    bool locked_ = false;
};

sim::Quat normalize(const sim::Quat& q);
// Shortest-arc spherical interpolation.
sim::Quat slerp(const sim::Quat& a, const sim::Quat& b, float t);
// Rotates a car-frame vector into the world frame.
sim::Vec3 rotate(const sim::Quat& q, const sim::Vec3& v);
sim::Quat quat_from_yaw(float yaw); // rotation about +Y
sim::Quat quat_mul(const sim::Quat& a, const sim::Quat& b);

// The wheel's turn from a to b: b's spin_step when the angles are that far apart (by whole turns; b a tick after a),
// else the shorter way round.
float spin_change(const sim::WheelState& a, const sim::WheelState& b);
// Interpolates everything rendering, camera, HUD and audio read. The wheels turn by
// spin_change (their spin angles wrap); gear and flags come from `b`.
sim::VehicleState interpolate(const sim::VehicleState& a, const sim::VehicleState& b, float t);

} // namespace gt3r::race
