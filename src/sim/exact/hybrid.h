#pragma once

// The later builds' layout 7 system (AWD system 5, Gran Turismo Concept [physics §14.5]): an engine driving the rear axle with
// a motor of its own on that axle, a motor on each front wheel and a store the motors draw from and the brakes and the engine
// charge. The routines are Concept 2001 Tokyo's (the same code on every later build; 2002 Tokyo-Geneva's constants folded
// toward zero, build_math.h); the settings come from the program's Hybrid table (Hybrid, car_record.h). The store is the float
// Car::later.special[0..3] (their record's +0x149C), the flow display the signed byte special[5] (+0x14A1, -100 .. 100).

#include "sim/exact/car_record.h"
#include "sim/exact/program_data.h"

#include <span>

namespace gt3r::sim::exact {

// The settings as the set-up routine (Concept 2001 Tokyo) makes them from the program's Hybrid table; off where
// the program has none or the race follows GT3's rules.
Hybrid hybrid_settings(const ProgramData& d);
// The same from the table's 0x1B bytes under behaviour b (off under GT3's rules).
Hybrid hybrid_settings(std::span<const u8> table, const Behaviour& b);
// Whether car c runs the system: a layout 7 car under the later builds' rules (Rule::HybridStep's) with the settings on.
bool hybrid_car(const Car& c, const Hybrid& h);

// The store (the program sets it).
F hybrid_store(const Car& c);
void set_hybrid_store(Car& c, F v);
// The car's start: the store full, the bytes special[4] and special[5] cleared.
void hybrid_start(Car& c, const Hybrid& h);
// x into the store, at most up to its capacity; what went in. x out of the store, at most what it
// holds; what came out. Each moves the flow display by the amount (the byte plus the amount x the flow scale,
// truncated, held within -100 .. 100).
F hybrid_charge(Car& c, const Hybrid& h, F x);
F hybrid_draw(Car& c, const Hybrid& h, F x);
// The store's share between the low and the high mark (0 at or below the low, 1 at or above the high).
F hybrid_level(const Car& c, const Hybrid& h);
// The rear motor's demand, -1 .. 1: the throttle (the pedal while the clutch slips at a launch) over the engine's
// share of the power the engine and twice the rear motor give, by the store's level, 0 (-1) in the gears up to +0x13.
F hybrid_demand(const Car& c, const Hybrid& h);
// The lateral acceleration the steering asks for at the speed (v^2 tan(steer) / wheelbase), at most twice the
// outer front tyre's grip at 1.5 x the steering angle; signed by the steering.
F hybrid_wanted_lateral(const Car& c);

// the axle step: the engine's throttle when the car runs the system, the throttle over the engine's share.
F hybrid_engine_throttle(const Car& c, const Hybrid& h);
// the axle step, after the engine torque: the rear motor's torque. The rear wheels' braking charges the store;
// a positive demand draws the motor's power from it (the torque scaled by what came out), a negative one charges it.
F hybrid_rear_torque(Car& c, const Hybrid& h);
// the engine speed step with the clutch open: the engine's torque; below a full store the engine charges it, its
// throttle raised to what the charging torque (capped by the idle settings) needs and that torque taken off.
F hybrid_open_clutch_torque(Car& c, const Hybrid& h);
// the torque vectoring step's first call: the front motors. The front wheels' braking charges the store; the
// demand and the engine's power ask for a share of the motors' torque, split between the wheels by the yaw the steering
// wants against the car's lateral acceleration; added to the front wheels' net torques. Driving draws the power from the
// store (the clutch torque word: the power drawn over a front motor's rating).
void hybrid_front_motors(Car& c, const Hybrid& h);

} // namespace gt3r::sim::exact
