// The whole phases whose calls are ported one by one (phases 7 and 12), and the race step's vehicle phases; see tick.h.

#include "sim/exact/tick.h"

#include "base/ee_math.h"
#include "sim/exact/build_math.h"

#include "sim/exact/pit_lane.h"

namespace gt3r::sim::exact {

void class_grip_scale(Car& c, const ClassGrip& grip) {
    if (!toward_zero(c.behaviour) || !(c.control == 1 || c.control == 2 || c.driver_kind != 0)) return;
    const s8 pct = grip[size_t(c.p.drive_class) & 3];
    if (pct == 0) return;
    const F k = ee::int_to_f(pct) * F::raw(0x3C23D70A) + F::raw(0x3F800000); // (Europe: 0.01)
    for (Wheel& w : c.w) {
        w.slip_fade = w.slip_fade * k;
        w.circle = w.circle * k;
    }
}

void dynamics(Car& c, const Statics& s, const GearSettings& g, const ClassGrip& grip) {
    brakes_and_clutch(c);
    rolling_resistance(c, s);
    tyre_curves(c);
    class_grip_scale(c, grip);
    tyre_long_force(c, s);
    tyre_slip_force(c);
    forward_step(c);
    wheel_net_torque(c);
    torque_vectoring(c, s.hybrid);
    axle_step(c, s.awd3, s.stepless, s.hybrid);
    wheel_speeds(c);
    engine_speed(c, s.hybrid);
    gear_step(c, g);
    const bool air = airborne(c);
    lateral_yaw(c, air);
    stop_hold(c, air);
}

ChassisOutputs chassis(Car& c, const ChassisInputs& in) {
    ChassisOutputs out;
    hub_offsets_xy(c);
    speed_ramp(c);
    out.counts = ground_contact(c, *in.course, *in.bumps, in.mark_flagged);
    if (in.course_out) course_out(c, *in.course_out, out.counts);
    terrain_factor(c);
    suspension(c, in.ride);
    attitude(c, in.box_heading);
    bump_nudge(c);
    pad_rumble(c);
    steer_force(c, *in.feel);
    tyre_effects(c, *in.effects, in.course_kind);
    phase12_stub(c);
    engine_sound(c, in.sound_scales);
    effects_reset(c, in.x70, in.ghost_switch);
    view_follow(c);
    hub_offsets(c);
    body_axes(c);
    ground_frame(c);
    speed_bookkeeping(c, in.condition);
    wheel_spin(c);
    out.effects = tyre_effect_requests(c);
    return out;
}

void tick_until_collision(std::span<Car> cars, std::span<const CarInputs> in, const RaceInputs& race, AiPlan& plan) {
    const size_t n = cars.size();
    auto run = [&](size_t i) { return !in[i].posed; };
    for (size_t i = 0; i < n; ++i)
        if (run(i)) save_previous(cars[i]);
    // The later builds' special cars (special.h): GT3's step has no such calls.
    const bool later = n != 0 && later_step(cars[0]);
    if (later) {
        for (size_t i = 0; i < n; ++i)
            if (run(i)) hybrid_step(cars[i]);
        for (size_t i = 0; i < n; ++i) {
            Car& c = cars[i];
            if (!run(i) || c.p.special != 1) continue;
            bool soft = true;
            for (const Ground& g : c.g) soft = soft && soft_material(g, race.bumps);
            mood_step(c, race.later.moods, rival_level(int(c.index), int(n), race.later), soft);
        }
    }
    for (size_t i = 0; i < n; ++i)
        if (run(i)) prep(cars[i], race.prep);
    const u8 course_kind = course_category(race.aids.course_category);
    for (size_t i = 0; i < n; ++i)
        if (run(i)) driver_input(cars[i], race.pad, in[i].pad, course_kind, in[i].ai, plan);
    if (later)
        for (size_t i = 0; i < n; ++i)
            if (run(i)) stop_count_step(cars[i]);
    for (size_t i = 0; i < n; ++i)
        if (run(i)) wheel_steer(cars[i]);
    for (size_t i = 0; i < n; ++i)
        if (run(i)) slip_ratios(cars[i], race.statics);
    for (size_t i = 0; i < n; ++i)
        if (run(i)) aids(cars[i], race.aids);
    for (size_t i = 0; i < n; ++i)
        if (run(i)) dynamics(cars[i], race.statics, race.gears, race.class_grip);
    for (size_t i = 0; i < n; ++i)
        if (run(i)) brake_heat(cars[i], race.brake_heat);
    for (size_t i = 0; i < n; ++i)
        if (run(i)) clear_feedback(cars[i]);
}

void tick_after_collision(std::span<Car> cars, std::span<const CarInputs> in, const RaceInputs& race, Proximity* k,
                          Vu0Random& rng, const EmitHook& emit) {
    const size_t n = cars.size();
    auto run = [&](size_t i) { return !in[i].posed; };
    if (k) proximity(cars, *k);
    if (n != 0 && later_step(cars[0]))
        for (size_t i = 0; i < n; ++i)
            if (run(i)) wing_step(cars[i]);
    ChassisInputs ch;
    ch.course = race.course, ch.bumps = &race.bumps, ch.feel = &race.feel, ch.effects = &race.effects;
    ch.course_kind = race.aids.course_category, ch.sound_scales = race.sound_scales, ch.condition = race.prep.condition;
    ch.course_out = race.course_out.on ? &race.course_out : nullptr;
    ch.ride = race.ride.on ? &race.ride : nullptr;
    for (size_t i = 0; i < n; ++i) {
        if (!run(i)) continue;
        ch.mark_flagged = in[i].mark_flagged, ch.box_heading = in[i].box_heading;
        ch.x70 = in[i].x70, ch.ghost_switch = in[i].ghost_switch;
        emit(int(i), chassis(cars[i], ch), rng);
    }
    for (size_t i = 0; i < n; ++i)
        if (run(i)) tire_wear(cars[i], race.prep.wear);
    for (size_t i = 0; i < n; ++i)
        if (run(i)) wear_flags(cars[i], race.prep.wear, in[i].pit);
    for (size_t i = 0; i < n; ++i)
        if (run(i)) flaps(cars[i], rng);
}

void tick(std::span<Car> cars, std::span<const CarInputs> in, const RaceInputs& race, AiPlan& plan,
          const std::function<void(std::span<Car>)>& collide, Proximity* k, Vu0Random& rng, const EmitHook& emit, FlowStep* flow) {
    tick_until_collision(cars, in, race, plan);
    collide(cars);
    tick_after_collision(cars, in, race, k, rng, emit);
    if (flow) tick_flow(cars, *flow, plan);
}

} // namespace gt3r::sim::exact
