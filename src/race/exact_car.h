#pragma once

// A car of the exact core (sim::exact::Car, GT3's own record) as the race screen reads a car: its drawn state
// (sim::VehicleState, in the course / GTRW frame of sim_types.h), its geometry (sim::VehicleSetup, the car frame), the last
// step's telemetry, its make-up (CarFacts) and its engine condition. The record's physics frame is Z up (x, y horizontal,
// z height); GTRW = (x, z, -y) [physics §1.3].

#include "race/backend.h"
#include "sim/exact/car_record.h"
#include "sim/exact/special.h"
#include "sim/exact/tick.h"

namespace gt3r::race {

namespace ex = sim::exact;

// The physics-frame vector v in the course frame.
inline sim::Vec3 exact_to_world(const ex::Vec3& v) { return {v.x.host(), v.z.host(), -v.y.host()}; }

// The car frame's geometry from the parameter block: the wheels at their static positions (the origin at the wheelbase
// midpoint on the ground, the CoG cog_height above it and (a - b) / 2 along it), the body extents, the rpm range.
sim::VehicleSetup exact_setup(const ex::Car& c);
// The drawn state after a step: the car-frame origin and orientation from the CoG and the body axes, the velocities, each
// wheel's contact point (the ground under its hub), steer, spin, suspension (compression positive, as VehicleSetup's
// static_suspension), slip, load and surface (the raw GTRW value: the triangle's material | flags << 8); the speedometer;
// with `moods` (the race's special car 1 tables, RaceInputs::later) what a special car's model shows (ex::special_look).
void exact_state(const ex::Car& c, const sim::VehicleSetup& setup, sim::VehicleState& out, const ex::MoodTables* moods = nullptr);
// The last step's telemetry as the game's sounds, smoke, dirt, HUD lamps and force feedback read it. wear_on: the race has
// tyre wear; wall: the collision step's wall impulse on the car this step.
void exact_telemetry(const ex::Car& c, bool wear_on, float wall, sim::VehicleTelemetry& out);
CarFacts exact_facts(const ex::Car& c);
// The engine condition words F +0x3F8 .. +0x400 (physics §10.3).
sim::EngineCondition exact_condition(const ex::Car& c);
// The entry's condition words (StartEntry::condition) for a run that starts with `c` and the car's dirt.
std::array<u32, 8> exact_condition_words(const sim::EngineCondition& c, float dirt = 0);

} // namespace gt3r::race
