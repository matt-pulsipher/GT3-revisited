// The race start; see start.h. The per-car loop with the single-player conductor's entry set-up and start position,
// the builders of params_build, and what else it calls:

#include "sim/exact/start.h"

#include "base/ee_math.h"
#include "sim/exact/ai_setup.h"
#include "sim/exact/build_math.h"
#include "sim/exact/hybrid.h"
#include "sim/exact/pit_lane.h"
#include "sim/exact/race_flow.h"
#include "game/data/car_database.h"

#include <cmath>
#include <stdexcept>

namespace gt3r::sim::exact {

namespace {

using ee::int_to_f;

const F k0 = F::raw(0), kHalf = F::raw(0x3F000000), k1 = F::raw(0x3F800000), k100 = F::raw(0x42C80000);
const F k001 = F::raw(0x3C23D70A), k01 = F::raw(0x3DCCCCCD), kKmh = F::raw(0x3E8E38E4), kDeg = F::raw(0x3C8EFA35);
const F kPi = F::raw(0x40490FDB), kTwoPi = F::raw(0x40C90FDB), kRpm = F::raw(0x3DD67750), k60 = F::raw(0x42700000);

// The record image's words outside the mirror (the pointer words).
void put32(Record& r, u32 off, u32 v) {
    for (u32 k = 0; k < 4; ++k) r[off + k] = u8(v >> (8 * k));
}

// The driver parameter set of driveline class `cls` (>= 4 reads 0): the course file's, else the built-in set
// of the course category (1 -> set 1, 2 -> set 2, else set 0).
std::array<u8, 32> driver_set(const StartRace& race, int cls) {
    if (race.sets)
        if (const course::DriverSets::Set* s = race.sets->for_class(u32(cls) < 4 ? cls : 0)) return *s;
    const int cat = race.category == 0xFF ? 0 : race.category;
    std::array<u8, 32> set;
    const u32 at = 32 * u32(cat == 1 ? 1 : cat == 2 ? 2 : 0);
    for (u32 k = 0; k < 32; ++k) set[k] = race.data->b(ProgramData::Table::DriverSets, at + k);
    return set;
}

// The driver object's parameters and aid block from its set [ai §9.1].
void driver_params(Driver& d, const std::array<u8, 32>& b, bool z) {
    auto f = [](s32 v) { return int_to_f(v); };
    // (z: 2002 Tokyo-Geneva's 1 / 3.6, pi / 180 and 0.1, folded toward zero: Europe)
    const F kKmh = z ? F::raw(0x3E8E38E3) : sim::exact::kKmh, kDeg = z ? F::raw(0x3C8EFA34) : sim::exact::kDeg;
    const F k01 = z ? F::raw(0x3DCCCCCC) : sim::exact::k01;
    d.yaw_gain = f(s32(b[3]) * 5) * k001;
    d.look_min = f(b[7]);
    d.look_max = f(b[8]);
    d.throttle_scale = k1;
    d.look_speed = k1 / (f(s32(b[6]) * 2) * kKmh);
    d.brake_factor = k100 / f(b[0]);
    d.speed_scale = f(b[1]) * k001;
    d.speed_offset = f(s32(b[2]) - 100) * kKmh;
    d.sat_throttle = f(b[4]) * k001;
    d.sat_brake = f(b[5]) * k001;
    d.front_weight = f(b[14]) * k001;
    d.corner_bonus = f(b[15]) * kKmh;
    d.steer_rate = f(s32(b[16]) * 10) * kDeg;
    d.bonus_ticks = s32(b[17]) * 10;
    d.pit_speed = f(b[18] ? b[18] : 80) * kKmh;
    Aids& a = d.aids;
    const u8 flags = b[19];
    a.a_gain = f(b[9]) * k001;
    a.a_thr = f(b[10]) * k001;
    a.d_gain = f(b[20]) * k001;
    a.a_wheels = {u8(flags & 1), u8((flags >> 1) & 1)};
    a.d_wheels = {u8((flags >> 2) & 1), u8((flags >> 3) & 1)};
    a.on[0] = u8((a.a_wheels[0] | a.a_wheels[1]) && !(a.a_gain == k0));
    a.on[1] = u8((a.d_wheels[0] | a.d_wheels[1]) && !(a.d_gain == k0));
    a.c_gain = f(b[21]) * k001;
    a.c_thr = f(b[22]) * k001;
    a.c_wheels = {u8((flags >> 4) & 1), u8((flags >> 5) & 1)};
    a.e_wheels = {u8((flags >> 6) & 1), u8((flags >> 7) & 1)};
    a.e_gain = f(b[23]) * k001;
    a.on[2] = u8((a.c_wheels[0] | a.c_wheels[1]) && !(a.c_gain == k0));
    a.on[3] = u8((a.e_wheels[0] | a.e_wheels[1]) && !(a.e_gain == k0));
    a.tcs_lat_a = f(b[11]) * k01;
    a.tcs_lat_b = f(b[12]) * k001;
    a.tcs_gain = f(b[13]) * k01;
    a.tcs_scale = b[24] == 100 || b[24] == 0 ? k1 : f(b[24]) * k001;
}

} // namespace

void event_overrides(Car& c, const RaceConfig& rules, u8 config, int cls) {
    Driver& d = c.ai;
    const u8* r = rules.data() + cls;
    s32 throttle = r[0xC] ? r[0xC] : 100;
    s32 brake = r[0x10], corner = r[0x14];
    // (Rule::DriverSpread, 2002 Tokyo-Geneva: no speed offset of the event's, its rule block's +0x18 .. +0x1B being the
    // classes' grip percentages of phase 7 (ClassGrip); Europe)
    const bool geneva = c.behaviour.rule(Rule::DriverSpread) >= Program::ConceptGenevaEu;
    s32 offset = geneva ? 0 : s8(r[0x18]);
    if (!(throttle < 101)) throttle = 100;
    if (!corner) corner = 100;
    if (!brake) brake = 100;
    // (Rule::DriverSpread: the later builds' routine does not ask for configuration 2, Concept 2001 Tokyo: every
    // race's computer drivers differ by their car's index)
    if ((config == 2 || !gt3_program(c.behaviour.rule(Rule::DriverSpread))) && c.control == 2) {
        const s32 i = c.index;
        corner -= (i << 2) & 4;
        brake -= 7 * (i % 3);
        d.yaw_gain = d.yaw_gain * (int_to_f(3 * ((i + 1) % 3 - 1) + 100) * k001);
        offset -= 2 * ((7 * i) % 5);
    }
    if (throttle != 100) d.throttle_scale = int_to_f(throttle) * k001;
    if (brake != 100) d.brake_factor = d.brake_factor * (k100 / int_to_f(brake));
    if (corner != 100) d.speed_scale = d.speed_scale * (int_to_f(corner) * k001);
    if (offset != 0) d.speed_offset = d.speed_offset + int_to_f(offset) * folded(c.behaviour, kKmh.bits, 0x3E8E38E3);
}

namespace {

// The car on the road at (x, y, z) facing `heading`: the CoG's ground query from segment `hint`, the wheels' (from
// the CoG's segment), each wheel's static load, travel, steer, camber and half-track, the ground frame, the pitch from the
// ride heights, the CoG at its static height above the ground, the race progress.
void pose(Car& c, const StartRace& race, s32 hint, F x, F y, F z, F heading) {
    const CourseQuery& q = *race.course;
    Ground cog;
    cog.x = x, cog.h = z, cog.z = -y, cog.seg = hint;
    q.ground(cog);
    const F ground = cog.height;
    c.pos.x = x, c.pos.y = y;
    c.pitch = k0, c.roll = k0, c.heading = heading;
    c.road_seg = cog.seg;
    c.pos.z = ground + c.p.cog_height;
    body_axes(c);
    hub_offsets(c);
    int pit = 0;
    for (int i = 0; i < 4; ++i) {
        Wheel& w = c.w[size_t(i)];
        Ground& g = c.g[size_t(i)];
        const F hy = c.pos.y + w.hub.y, hx = c.pos.x + w.hub.x, hz = c.pos.z + w.hub.z;
        g.x = hx, g.z = -hy, g.h = hz, g.seg = c.road_seg;
        q.ground(g);
        if (!g.miss) {
            w.ground = w.ground0 = g.height;
            w.surface = u8(surface_index(g));
            w.x25 = g.x32;
            w.surface_flags = g.flags;
            pit += g.flags & 1;
        } else {
            w.surface = 0, w.x25 = 0, w.surface_flags = 0;
            w.ground = w.ground0 = ground + w.hub.z;
        }
    }
    c.x535 &= 0xEF;
    if (pit && race.mark_pit) c.x535 |= 0x10;
    for (int i = 0; i < 4; ++i) {
        Wheel& w = c.w[size_t(i)];
        const size_t a = size_t(i >> 1);
        w.smoke_count = 0;
        w.load = w.peak_force = (a ? c.p.xe58 : c.p.xe54) * kHalf + c.p.susp[a].preload;
        w.susp = c.p.susp[a].x_static;
        w.steer = wheel_steer_angle(c.p, i, k0);
        w.camber = wheel_camber(c.p, i, k0, k0);
        w.half_track = hub_half_track(c.behaviour, c.p, i, k0);
    }
    ground_frame(c);
    c.pitch = c.ground_pitch, c.roll = c.ground_roll;
    c.pitch = c.pitch + atan2(c.behaviour, c.p.ride_height[0] - c.p.ride_height[1], c.p.wheelbase);
    body_axes(c);
    c.pos.z = c.ground_pos.z + c.up.z * c.p.cog_height;
    hub_offsets(c);
    c.f3e4 = race_progress(c, race.kind, *race.course); // the conductor's race progress
}

// The lagging heading and pitch start at the attitude, at rest.
void view_reset(Car& c) {
    c.lag_heading = c.heading, c.lag_pitch = c.pitch;
    c.lag_pitch_diff = k0, c.lag_heading_acc = k0, c.lag_heading_rate = k0, c.lag_heading_diff = k0, c.lag_pitch_rate = k0;
}

// |speed| and the speedometer at v, the top speed raised to it.
void speeds(Car& c, F v) {
    c.speed_abs = v;
    c.speedo = v;
    if (c.max_speed < v) c.max_speed = v;
}

} // namespace

void tyres_start(Car& c, const WearParams& cp) {
    for (Wheel& w : c.w) {
        w.wear_level = 0, w.wear_byte = 0, w.wear = k0, w.wear_grip = k1, w.brake_heat = k0;
        if (cp.max == k0) continue;
        w.wear_level = cp.fresh < k0 ? s8(-0x3F) : 0;
        w.wear = cp.fresh;
        w.wear_grip = k1 - cp.fresh_loss;
    }
}

namespace {

// The initial line: of the candidates present as themselves, the one whose point at the car's race
// distance is nearest (within 100 m); default 6. A candidate the line evaluation fails for compares the last values again
// (the program's outputs are left as they were; 0 before the first).
int initial_line(const Car& c, const AiCourse& k, const ProgramData& data) {
    F best = k100, x = k0, y = k0, h = k0;
    int line = 6;
    for (u32 j = 0; j < 3; ++j) {
        const s32 cand = s32(data.w(ProgramData::Table::InitialLines, 4 * j));
        const int r = k.resolve(cand);
        if (r != cand) continue;
        k.evaluate(r, 0, c.f3e4, x, y, h);
        x = x - c.pos.x;
        const F x2 = x * x;
        y = y - c.pos.y;
        const F d = ee::sqrt(x2 + y * y);
        if (d < best) best = d, line = r;
    }
    return line;
}

// The driver's line (-1: the initial one) and plan, its recovery and traffic state cleared.
void driver_start(Car& c, const StartRace& race, int line) {
    Driver& d = c.ai;
    d.state = 0;
    d.v_next = d.v_cur = c.p.top_speed;
    const AiCourse& k = *race.ai;
    const int r = line == -1 ? k.resolve(initial_line(c, k, *race.data)) : k.resolve(k.resolve(line));
    ai_set_line(c, k, race.wear, r);
    d.pit_limit = 0, d.arc_ticks = 0, d.trim = 0, d.x99 = 0;
    d.zone = {};
    d.contact = k0;
    d.cooldown = 0;
    ai_gear_hint_clear(d);
}

// the race distance not clamped: the point of line `line` at race distance s (wrapped into the lap), the
// road's height under it and the heading towards the point 10 m on; false without the line (nothing written).
bool line_position(const StartRace& race, int line, F s, s32& seg, F& x, F& y, F& z, F& heading) {
    const AiCourse& k = *race.ai;
    const F lap = race.course->lap_length();
    const int l = k.resolve(line);
    if (l == -1) return false;
    const std::vector<AiPoint>& p = k.lines[size_t(l)];
    const int n = int(p.size());
    if (s < k0) {
        do s = s + lap;
        while (s < k0);
    } else
        while (lap <= s) s = s - lap;
    int hint = n - 1;
    if (n > 0) {
        if (s <= p[0].dist) hint = 0;
        else
            for (int a = 1; a < n; ++a)
                if (s <= p[size_t(a)].dist) {
                    hint = a;
                    break;
                }
    }
    F px = k0, py = k0, ph = k0, qx = k0, qy = k0, qh = k0; // the program's stack outputs, kept by a failed evaluation
    k.evaluate(l, hint, s, px, py, ph);
    k.evaluate(l, hint, s + F::raw(0x41200000), qx, qy, qh);
    x = px;
    s32 sg = p[size_t(hint)].seg;
    y = py;
    bool miss = false;
    z = road_height(*race.course, sg, miss, px, py, ph);
    if (miss) {
        s32 retry = -1;
        z = road_height(*race.course, retry, miss, px, py, ph + k1);
        if (!miss) sg = retry;
    }
    heading = atan2(k.behaviour, (-qx) + px, qy - py);
    seg = sg;
    return true;
}

// A car on the main line (6) at race distance `dist` (m, within the lap less 20 m either way): 1 at or past the
// line, -1 behind it, 0 without the line. (The later builds' routine, Concept 2001 Tokyo, takes the line: -1 the
// main line; only the flying start grid names another.)
int place_on_line(const StartRace& race, s32 dist, s32& seg, F& x, F& y, F& z, F& heading, int line = 6) {
    const F lim = race.course->lap_length() - F::raw(0x41A00000);
    F s = int_to_f(dist);
    if (s < -lim) s = -lim;
    if (lim < s) s = lim;
    if (!line_position(race, line, s, seg, x, y, z, heading)) return 0;
    return -1 < dist ? 1 : -1;
}

// The later builds' single race flying start (Rule::FlyingGrid; Concept 2001 Tokyo's single race conductor): an event with
// a start distance (not 0) and a start speed puts the cars on the line at the distance, each the grid table's step further
// on than the one before (+8: five entries, repeating) and the speed plus the table's speed steps (+0x10) faster.
bool flying_grid(const StartRace& race) {
    using T = ProgramData::Table;
    if (race.kind != Conductor::SinglePlayer || !race.data || gt3_program(race.data->behaviour().rule(Rule::FlyingGrid)) ||
        !race.data->has(T::FlyingStart))
        return false;
    const s16 dist = s16(u16(race.rules[0x3A] | race.rules[0x3B] << 8));
    return dist != 0 && race.rules[0] > 0;
}

// Car i's pit box: its point of the stop line, half a metre on along its heading. On a course whose stop
// line has no such point (fewer than i + 4 points) the program reads memory outside the line: before the line's points in the
// course file, or from address 4 on when the course has no stop line (the line loader nulls an empty line); refused (the race
// is not built). Only free practice's car 0 starts there, and only GT mode's race menus offer free practice: their courses
// without a stop line are all dirt courses, where the menu turns it off (QuickMenuItems::visit).
void pit_box(const StartRace& race, int i, s32& seg, F& x, F& y, F& z, F& heading) {
    const std::vector<AiPoint>& p = race.ai->lines[4];
    const s32 k = s32(p.size()) - (i + 4);
    if (k < 0 || size_t(k) >= p.size()) throw std::runtime_error("race start: the course's stop line has no pit box for the car");
    const AiPoint& b = p[size_t(k)];
    x = b.x, y = b.y, z = b.height, heading = b.heading;
    seg = b.seg;
    x = x - ee::sin(heading) * kHalf;
    y = y + ee::cos(heading) * kHalf;
}

// The race distance (less the lap, so negative) of the last start of a tight arc (|radius| <= r) on the main
// line at least -lo metres before the line's end; lo itself when there is none.
F last_tight_arc(const StartRace& race, F lo, F r) {
    const AiCourse& k = *race.ai;
    const int l = k.resolve(6);
    if (l == -1) return lo;
    const std::vector<AiPoint>& p = k.lines[size_t(l)];
    const int n = int(p.size());
    const F lap = race.course->lap_length(), lim = lap + lo;
    auto tight = [&](const AiPoint& a) {
        if (a.kind == 0) return false;
        return !(r < a.radius) && !(a.radius < -r);
    };
    for (int a = n - 1; a >= 0; --a) {
        const int prev = -1 < a - 1 ? a - 1 : a - 1 + n;
        if (tight(p[size_t(prev)]) || !tight(p[size_t(a)])) continue;
        if (!(lim < p[size_t(a)].dist)) return p[size_t(a)].dist - lap;
    }
    return lo;
}

// The car's set-up from its spec. mode, dist, speed: the conductor's start position (speed in km/h).
void setup(Car& c, Record& img, Spec& spec, const StartRace& race, const StartEntry& e, int i, s32 control, s32 pedal, u8 ghost,
           s32 mode, s32 dist, F x, F y, F z, F heading, s32 speed) {
    // The engine condition from the entry.
    c.odo_carry = k0;
    c.odometer = e.condition[0];
    c.engine_wear = F::raw(e.condition[1]), c.oil = F::raw(e.condition[2]), c.dirt = F::raw(e.condition[3]);
    for (size_t k = 0; k < 4; ++k) c.x408[k] = e.condition[4 + k];
    put32(img, 0x8, race.course_addr);
    put32(img, 0xC, e.human);
    c.wheel_type = s8(e.wheel_type < 5 ? e.wheel_type : 0);
    if (!e.asm_on) {
        // (the later builds with channel E's switch: an ASM switched off keeps channel E's wheel flags)
        using T = ProgramData::Table;
        const bool keep_e = race.data && !gt3_program(race.data->behaviour().rule(Rule::Ascc)) && race.data->has(T::RaceSwitches) &&
                            race.data->b(T::RaceSwitches, 5) != 0;
        spec[0x109] = keep_e ? u8(spec[0x109] & 0xC0) : 0;
    }
    if (!e.tcs_on) spec[0x110] = 0;
    c.ghost = ghost;
    c.index = u8(i);
    c.control = s8(control);
    c.driver_kind = 0;
    {
        ConvertInputs in;
        in.data = race.data;
        in.base = e.car + kParams;
        in.control = c.control;
        for (u32 k = 0; k < 12; ++k) in.rules[k] = race.rules[k];
        in.condition = e.condition;
        in.physics_globals();
        ParamImage p{};
        convert_params(spec, in, p);
        std::copy(p.begin(), p.end(), img.begin() + kParams);
        Car t;
        load_image(t, img.data());
        c.p = t.p;
    }
    const int cls = c.p.drive_class;
    driver_params(c.ai, driver_set(race, cls), toward_zero(c.behaviour));
    if (c.control != 0) {
        RaceConfig rules = race.rules;
        if (e.driver) std::copy(e.driver->begin(), e.driver->end(), rules.begin() + kDriverOverridesAt);
        event_overrides(c, rules, race.config, cls);
    }
    c.ai.downforce = (c.p.downforce_coef[0] + c.p.downforce_coef[1]) / c.p.weight;
    if (c.control == 0) c.pedal_style = u8(pedal);
    else if (c.control > 0 && c.control < 3) c.pedal_style = 0;
    c.x535 = 0;
    // The placement: on the main line (modes 1-5; the grid when the course has none: mode 0), the pit box (6, 7) or the grid.
    s32 hint = -1;
    bool behind = false; // placed on the line before the start line: lap and sector 0
    bool pit = false;
    if (u32(mode - 1) < 5) {
        if (const int r = place_on_line(race, dist, hint, x, y, z, heading)) behind = r < 0;
        else mode = 0;
    } else if (u32(mode - 6) < 2) {
        pit = true;
        pit_box(race, i, hint, x, y, z, heading);
    }
    if (mode == 0 || mode == 8) { // back along the heading by the body's front extent: the nose at the slot
        const F front = c.p.body_front;
        x = x + front * ee::sin(heading);
        y = y - front * ee::cos(heading);
    }
    pose(c, race, hint, x, y, z, heading);
    view_reset(c);
    standing_start(c);
    tyres_start(c, race.wear);
    step_length(c);
    c.lap_base = 0, c.x493 = 0;
    // Laps and the sector by the race progress: past every sector start (behind the line) lap 0, else lap 1; a pit box past
    // half the lap counts a lap less.
    if (behind) c.sector = 0, c.laps = 0;
    else {
        const CourseQuery& q = *race.course;
        const int last = sector_count(race.kind, race.rules[1], q) - 1; // the conductor's sector count / sector start
        const std::array<F, 4> machine = machine_sectors(*race.data);
        c.sector = u8(last);
        for (int s = 0; s < last; ++s)
            if (c.f3e4 < sector_line(race.kind, race.rules[1], machine, q, s)) {
                c.sector = u8(s);
                break;
            }
        if (mode == 6) {
            if (q.lap_length() * kHalf < c.f3e4) c.lap_base = 1;
            c.sector = 0, c.laps = 0;
        } else if (c.sector != last) c.laps = 1;
        else c.sector = 0, c.laps = 0;
    }
    c.x530 = 0;
    c.best_lap = 0x157529FF;
    c.race_hold = 0, c.hold = 0, c.finished = 0, c.x51c = 0, c.x51d = 0;
    { // the car's pit box, a point of the stop line (4) from its end
        const AiCourse& k = *race.ai;
        c.pit_box = s8(k.lines[4].empty() ? 0 : s32(k.lines[4].size()) - (i + 4));
    }
    c.rank = s8(i + 1);
    driver_start(c, race, u32(mode - 2) < 4 ? 6 : mode == 8 ? -1 : c.control == 0 ? 7 : -1);
    ai_gear_hint(c);
    CoursePlan plan(*race.ai, race.wear);
    if (pit) { // the pit box: on the stop line (pit_stop), held for 120 steps (PAL 100)
        pit_stop(c, plan);
        c.hold = pal_rate(c) ? 100 : 120;
    }
    c.ai_hold = 0;
    if (control == 0 && (u32(mode - 2) < 3 || mode == 8)) { // a player's flying or rolling start: the AI drives the run-up
        drive_on(c, plan);
        if (!ntsc_j_rules(c, Rule::RunUp)) c.shift_lockout = pal_rate(c) ? 10 : 12; // not in the NTSC-J program
        if (mode == 3) c.ai_hold = pal_rate(c) ? 150 : 180;
        else if (mode == 8) c.ai_hold = pal_rate(c) ? 250 : 300;
    }
    const F v = mode == 4 ? c.ai.v_next : int_to_f(speed) * folded(c.behaviour, kKmh.bits, 0x3E8E38E3); // (Europe's)
    if (!(v == k0)) moving_start(c, v);
    // The later builds' special cars (Rule::PodMood's routines): special car 1 starts with its stop count at 0 (Concept
    // 2001 Tokyo) and its mood bytes cleared but for the time, 60 steps (2002 Tokyo-Geneva Europe's
    // 50); a car with a wing starts with it in.
    if (const Program how = c.behaviour.rule(Rule::PodMood); !gt3_program(how) && (c.p.special == 1 || c.p.special == 2)) {
        c.later.special = {};
        if (c.p.special == 1) c.later.special[2] = how == Program::ConceptGenevaEu ? 0x32 : 0x3C;
    }
    // The later builds' layout 7 system (Concept 2001 Tokyo): its store starts full.
    if (race.data) {
        if (const Hybrid h = hybrid_settings(*race.data); hybrid_car(c, h)) hybrid_start(c, h);
    }
}

} // namespace

F nose_progress(const Car& c, const CourseQuery& q) {
    const F f = c.p.body_front;
    const F y = c.pos.y + c.fwd.y * f, x = c.pos.x + c.fwd.x * f, z = c.pos.z + c.fwd.z * f;
    return q.progress(x, z, -y, c.road_seg);
}

void clear_course_out(Car& c) { c.off_count = 0, c.course_out = 0; }

void standing_start(Car& c) {
    for (Wheel& w : c.w) {
        w.slip_dir = k0, w.slip_fade = k0, w.force_b = k0, w.force_a = k0, w.fx_drive = k0, w.fx_brake = k0;
        w.hub_vx = k0, w.hub_vy = k0, w.rim_speed = k0, w.flags = 0, w.slip_gain = k1;
    }
    for (size_t a = 0; a < 2; ++a) {
        c.diff_lock[a] = 0;
        if (u32(c.p.d.diff_type[a]) - 3 < 2) c.diff_lock[a] = 1;
        c.axle_speed[a] = k0;
    }
    c.centre_locked = 0;
    if (u32(c.p.d.layout) - 4 < 3) c.centre_locked = 1;
    c.clutch_torque = k0, c.x420 = k1, c.time_scale = k1, c.steer_cmd = k0;
    c.lock_lo = -c.p.max_lock, c.lock_hi = c.p.max_lock;
    c.yaw_rate = k0, c.steer_rate = k0, c.slip_target = k0, c.slip_filter = k0, c.vel = {};
    c.steer_pct = 0, c.throttle_pedal = k0, c.throttle = k0, c.brake_pedal = k0;
    for (Wheel& w : c.w) w.brake = k0;
    c.full_throttle = 0, c.blip = 0, c.intake = k0, c.turbo_load_prev = k0, c.turbo_load = k0, c.turbo_lag = {};
    c.turbo_pitch = 0, c.blowoff = k0, c.blowoff_state = 0, c.torque_wot = k0, c.torque_friction = k0, c.rev_limiter = 0;
    clear_course_out(c);
    c.max_speed = k0;
    const F top = c.max_speed;
    speeds(c, top);
    set_gear(c, 1);
    c.rpm = 0, c.ascc_status = 0, c.tcs_status = 0, c.hit_rumble = top;
    c.rumble = 0, c.x16d4 = 0, c.steer_force = 0x80, c.wheel_pos_prev = 0, c.wheel_pos = 0;
    c.ffb = top, c.ffb_count = 0, c.turbo_spool = 0, c.x4e5 = 0x7F, c.x4e4 = 0x7F;
    for (Wheel& w : c.w) {
        w.smoke_emit = k0, w.x29 = 0xFF, w.slip_intensity = k0, w.skid_pitch = k1, w.spin = k0, w.speed_ramp = k0;
        w.slip_ratio = k0, w.circle = k0, w.bump = k0, w.damper_v = k0, w.spring_f = k0, w.damper_f = k0, w.flap = k0, w.flap_v = k0;
    }
    c.roll_rate = top, c.speed = top, c.lat_speed = top, c.clutch = 0, c.clutch_req = top, c.omega = top, c.pitch_rate = top;
    c.bump_mask = 0, c.shift_lockout = 0;
    if (c.control == 1 || c.driver_kind != 0 || c.pedal_style == 0 || c.pedal_style == 2) c.shift_lockout = pal_rate(c) ? 10 : 12;
    c.drive_sum = k0, c.antihunt = 0, c.kickdown = 0, c.kickdown_timer = 0, c.kickdown_count = 0;
    c.downforce = {k0, k0}, c.a_x = k0, c.a_y = k0, c.heave_acc = k0, c.lat_g = k0, c.yaw_accel = k0, c.countersteer = k0;
    c.x4d0 = k0, c.x4d4 = k0;
}

bool stepless_ratio(const Driveline& d, F& omega, F& ratio, bool z) {
    if (d.kind != 1) return false;
    const F hold = int_to_f(d.down_rpm[0]) * (z ? F::raw(0x3DD6774E) : kRpm); // (z: 2002 Tokyo-Geneva's, Europe)
    if (omega < hold) {
        ratio = k1;
        return true;
    }
    ratio = hold / omega;
    if (const F least = d.ratio[d.gears & 7]; ratio < least) ratio = least;
    omega = ratio * omega;
    return true;
}

void moving_start(Car& c, F v) {
    c.yaw_rate = k0;
    c.vel.x = c.fwd.x * v, c.vel.y = c.fwd.y * v, c.vel.z = c.fwd.z * v;
    speeds(c, v);
    set_gear(c, u8(gear_for_speed(c, v)));
    // (2002 Tokyo-Geneva's 2 pi / 60 and 2 pi: Europe)
    const F kRpm = folded(c.behaviour, sim::exact::kRpm.bits, 0x3DD6774E), kTwoPi = folded(c.behaviour, sim::exact::kTwoPi.bits, 0x40C90FDA);
    const F idle = int_to_f(c.p.e.idle) * kRpm;
    F w = v * c.p.d.gear_speed[c.gear];
    // (Rule::MovingStart: the later builds set their record's ratio word to 1 and let a stepless gearbox take the speed)
    if (!gt3_program(c.behaviour.rule(Rule::MovingStart))) c.later.ratio = k1, stepless_ratio(c.p.d, w, c.later.ratio, toward_zero(c.behaviour));
    c.omega = w;
    if (w < idle) c.omega = idle;
    else if (const F limit = int_to_f(c.p.e.rev_limit) * kRpm; limit < w) c.omega = limit;
    c.rpm = s16(ee::to_int((c.omega * k60) / kTwoPi));
    if (!(c.p.e.boost_max == k0)) {
        if (k0 < c.p.e.turbo_peak[1]) c.turbo_lag[1] = c.rpm;
        c.turbo_lag[0] = c.rpm;
        c.intake = boost(c.p.e, c.rpm, c.rpm);
        c.turbo_load = k1;
        c.turbo_pitch = u16(c.rpm);
        c.blowoff = k0;
        c.turbo_load_prev = k1;
        c.blowoff_state = 0;
    }
    // (Rule::MovingStart: the later builds' routine, Concept 2001 Tokyo, opens the throttle of a car whose AI
    // segment mode D +0x83 is 0, a straight: pedal F +0x428 and throttle F +0x4A4 at 1.)
    if (!gt3_program(c.behaviour.rule(Rule::MovingStart)) && c.ai.mode == 0) c.throttle = k1, c.throttle_pedal = k1;
    c.clutch = 1;
    c.clutch_req = k1;
    c.speed = v;
    c.lat_speed = k0;
    for (Wheel& wh : c.w) wh.rim_speed = v, wh.hub_vx = v;
    // (the later builds: with unequal axles (D +0x01) both axle speeds by the driven axle's radius)
    const bool one_radius = c.p.d.unequal != 0 && !gt3_program(c.behaviour.rule(Rule::MovingStart));
    for (size_t a = 0; a < 2; ++a) c.axle_speed[a] = c.p.inv_tire_radius[one_radius ? c.p.d.driven_axle : a] * v;
}

namespace {

// Car a against car b: 1 a ahead, -1 b ahead, 0 level (laps less the pit-box flag).
int compare(const Car& a, const Car& b) {
    const s32 la = s32(a.laps) - a.lap_base, lb = s32(b.laps) - b.lap_base;
    if (lb < la) return 1;
    if (la < lb) return -1;
    if (!(a.finished | b.finished)) {
        if (b.f3e4 < a.f3e4) return 1;
        if (a.f3e4 < b.f3e4) return -1;
        return 0;
    }
    if (!b.finished) return -1;
    if (!a.finished) return 1;
    const s32 d = s32(b.x530 - a.x530);
    return d < 0 ? -1 : d > 0 ? 1 : 0;
}

} // namespace

void rank_reset(std::span<Car> cars, RaceOrder& order) {
    order.fill(-1);
    for (size_t i = 0; i < cars.size(); ++i) cars[i].rank = s8(i + 1), order[i] = s32(i);
}

void rank(std::span<Car> cars, RaceOrder& order) {
    const int n = int(cars.size());
    for (int k = 1; k < n; ++k) {
        const s32 car = order[size_t(k)];
        int j = k - 1;
        for (; j >= 0 && compare(cars[size_t(order[size_t(j)])], cars[size_t(car)]) < 0; --j) order[size_t(j + 1)] = order[size_t(j)];
        order[size_t(j + 1)] = car;
    }
    for (int k = 0; k < n; ++k) cars[size_t(order[size_t(k)])].rank = s8(k + 1);
}

StartResult start_cars(const StartRace& race_in, std::span<const StartEntry> entries) {
    StartRace race = race_in;
    race.cars = s32(entries.size());
    // Free practice's order is by grid slot (below): its slots must be the cars' places 0 .. n - 1, each once (another layout
    // leaves holes in the order that its ranking reads as cars).
    if (race.kind == Conductor::FreePractice) {
        u32 seen = 0;
        for (const StartEntry& e : entries) {
            if (u32(e.grid_slot) >= entries.size() || (seen >> e.grid_slot & 1)) throw std::runtime_error("race start: free practice's grid slots must be 0 .. cars - 1");
            seen |= 1u << e.grid_slot;
        }
    }
    StartResult r;
    r.later.resize(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) r.records.push_back(start_car(race, entries[i], int(i), &r.later[i]));
    std::vector<Car> cars(entries.size());
    for (size_t i = 0; i < cars.size(); ++i) {
        if (race.data) cars[i].behaviour = race.data->behaviour();
        cars[i].ev = electric_drive(race.db, entries[i].eq);
        load_image(cars[i], r.records[i].data());
    }
    // The rank reset: free practice's code puts the cars in grid-slot order, the others' rank_reset in index order; the ranking
    // sorts them (race_ranking; free practice's code by the best lap); then free practice's ranking set-up takes the
    // lower of each car's and its entry's best lap.
    if (race.kind == Conductor::FreePractice) {
        r.order.fill(-1);
        for (size_t i = 0; i < cars.size(); ++i) {
            const s32 slot = entries[i].grid_slot;
            cars[i].rank = s8(slot + 1);
            r.order[size_t(slot)] = s32(i);
        }
    } else {
        // The later builds' flying start (Rule::FlyingGrid; Concept 2001 Tokyo's ranking set-up) before the ranking:
        // car i again at its distance on the line the grid table names (+0: six entries, repeating), posed there (its speed
        // and the rest of its start kept), and asked to drive that line at once. A line the course lacks with every
        // fallback leaves the place the car before it had (the routine's outputs on its stack; refused for the first car).
        if (flying_grid(race)) {
            using T = ProgramData::Table;
            s32 dist = s32(s16(u16(race.rules[0x3A] | race.rules[0x3B] << 8)));
            s32 seg = 0;
            F x = k0, y = k0, z = k0, heading = k0;
            for (size_t i = 0; i < cars.size(); ++i) {
                const int line = race.data->b(T::FlyingStart, u32(i % 6));
                if (!place_on_line(race, dist, seg, x, y, z, heading, line) && i == 0)
                    throw std::runtime_error("race start: the flying start's line is not on the course");
                pose(cars[i], race, seg, x, y, z, heading);
                ai_request_line(cars[i], *race.ai, race.wear, line, true);
                dist += race.data->b(T::FlyingStart, 8 + u32(i % 5));
            }
        }
        rank_reset(cars, r.order);
    }
    race_ranking(cars, r.order, race.kind);
    if (race.kind == Conductor::FreePractice)
        for (size_t i = 0; i < cars.size(); ++i) keep_best_lap(cars[i], entries[i].best_lap);
    for (size_t i = 0; i < cars.size(); ++i) store_image(cars[i], r.records[i].data());
    return r;
}

ElectricDrive electric_drive(const data::CarDatabase* db, const Equipment& eq) {
    ElectricDrive ev;
    if (!db) return ev;
    const std::string label = db->label(eq.car);
    const data::ModCar* m = db->mod_car(label);
    const data::Car* car = db->car(label);
    // On its own motor only (part slot 6, the engine): an entry recorded with its donor's engine keeps GT3's drive.
    if (!m || !m->electric || !car || eq.part[size_t(data::TableId::Engine)] != car->parts[6]) return ev;
    ev.on = true;
    // Rounded (0.01 N m, 1 W): add_mod_car computes them in host floats, whose last bit can differ between compilers.
    // The rounding makes such a difference unlikely to survive; one that does is in the replay's data fingerprint, so
    // another build refuses the replay rather than playing it differently. (Online, each car is its owner's to drive.)
    ev.regen_torque = F(float(std::round(double(m->regen_torque_nm) * 100.0) / 100.0));
    ev.regen_power = F(float(std::round(double(m->regen_kw) * 1000.0)));
    return ev;
}

Record start_car(const StartRace& race, const StartEntry& e, int i, Car::Later* later) {
    Record img{};
    // Cleared; the race info and conductor; the driver object's conductor, car and AI course object.
    put32(img, 0x0, race.info);
    put32(img, 0x4, race.conductor);
    put32(img, 0xEC8, race.conductor);
    put32(img, 0xECC, e.car);
    put32(img, 0xED0, race.conductor + race.ai_course_at);
    Car c{};
    if (race.data) c.behaviour = race.data->behaviour();
    const Conductor kind = race.kind;
    // The entry set-up. Races, battles, free practice: a player's entry is driven by the pad (configuration 3: by the
    // AI with the pad decoded, 2: by the AI), with its spec flag and pedal style; other entries by the AI. Time
    // trials, free runs: the spec flag and pedal style from every entry, the player's on the pad (configurations 2 and 3:
    // by the AI with the pad decoded), the others' by the AI with the pad decoded, car 1 a ghost (free runs: the
    // ghost byte 2). The pace-car test (Concept's training conductor) takes the races' answer, the counterpart:
    // the pace car, an entry without a device, is a computer car.
    s32 control = 2, flag = 0, pedal = 0;
    u8 ghost = 0;
    if (kind == Conductor::TimeTrial || kind == Conductor::FreeRun || kind == Conductor::License || kind == Conductor::MachineTest) {
        flag = e.spec_flag, pedal = e.pedal_style;
        control = e.player && race.config != 3 && race.config != 2 ? 0 : 1;
        ghost = i == 1 ? kind == Conductor::FreeRun ? 2 : 1 : 0;
    } else {
        if (e.player) control = race.config == 3 ? 1 : race.config == 2 ? 2 : 0;
        if (control == 0) flag = e.spec_flag, pedal = e.pedal_style;
    }
    Spec spec{};
    Equipment eq = e.eq;
    build_spec(*race.db, *race.data, eq, flag, race.category == 0xFF ? 0 : race.category, false, spec);
    // The grid slot: its point, every car facing slot 0's yaw + pi. A slot past the course's grid table
    // reads what follows the table in the road block, as the program does (CourseQuery::slot).
    if (u32(e.grid_slot) >= 8) throw std::runtime_error("race start: a grid slot outside 0 .. 7");
    const CourseQuery::Slot s = race.course->slot(e.grid_slot, race.road);
    const F pi = race.data ? folded(race.data->behaviour(), kPi.bits, 0x40490FDA) : kPi; // (2002 Tokyo-Geneva's: Europe)
    const F x = s.x, y = -s.z, z = s.y, heading = race.course->slot(0, race.road).yaw + pi;
    // Virtual 25.
    s32 mode = 0, dist = 0, speed = 0;
    const u8 start_speed = race.rules[0];
    const s16 start_dist = s16(u16(race.rules[0x3A] | race.rules[0x3B] << 8));
    auto event_speed = [&] {
        if (start_speed > 0) speed = start_speed;
    };
    switch (kind) {
    case Conductor::SinglePlayer: // the rolling start puts car 0 in mode 8 and every car at 80 km/h
        if (flying_grid(race)) { // (the later builds' flying start, Concept 2001 Tokyo: car i past the first i steps)
            using T = ProgramData::Table;
            mode = 1, dist = start_dist, speed = start_speed;
            for (int k = 0; k < i; ++k)
                dist += race.data->b(T::FlyingStart, 8 + u32(k % 5)), speed += race.data->b(T::FlyingStart, 0x10 + u32(k % 5));
        } else if (race.start_type == 3) {
            if (i == 0) mode = 8;
            speed = 80;
        } else event_speed();
        break;
    case Conductor::TimeTrial:
    case Conductor::FreeRun: { // the flying start
        const std::array<u8, 32> set = driver_set(race, 0);
        const s32 back = s32(set[25]) * -10, fast = s32(set[26]) * 5;
        if (start_dist < 0 && start_speed > 0) mode = 2, dist = start_dist, speed = start_speed; // the event's
        else if (back < 0 && fast > 0) mode = 3, dist = back, speed = fast;                    // the driver set's
        else { // before the last tight arc: 400 m and radius 350 m (dirt 300 / 200)
            mode = 4;
            const int cat = race.category == 0xFF ? 0 : race.category;
            F lo = F::raw(0x43C80000), r = F::raw(0x43AF0000);
            if (cat == 2) lo = F::raw(0x43960000), r = F::raw(0x43480000);
            dist = ee::to_int(last_tight_arc(race, -lo, r)) - 5;
        }
        break;
    }
    case Conductor::FreePractice: // car 0 in its pit box, the others on the line behind at 120 km/h
        if (i == 0) mode = 6;
        else mode = 5, dist = -(200 * i + 300), speed = 120;
        break;
    case Conductor::Battle2P: // with a handicap, car 0 and the others apart on the line at the event's speed
        if (race.data && race.data->behaviour().rule(Rule::BattleStart) == Program::NtscJ && race.cars != 2) break; // the NTSC-J program: two cars only
        if (const s32 h = race.handicap) {
            s32 d0 = -10, d1 = -10;
            if (h > 0) d1 = -h < -19 ? -h : -20;
            else d0 = h < -19 ? h : -20;
            mode = 1, dist = i == 0 ? d0 : d1;
            event_speed();
        }
        break;
    case Conductor::License: // the event's flying start, else mode 1 at its distance
    case Conductor::Training:
        if (start_dist < 0 && start_speed > 0) {
            mode = 2, dist = start_dist, speed = start_speed;
            // The later builds' licence start: without a ghost car (the player-record test: the pace-car test), car i > 0 starts
            // trunc(i x speed / 3.6) m further on, one second ahead per car at the start speed (the divisor's bits are the
            // program's constant, 3.6 as a float).
            if (kind == Conductor::Training && i > 0) dist += ee::to_int(int_to_f(i) * int_to_f(speed) / F::raw(0x40666666));
        } else {
            if (start_dist != 0) mode = 1, dist = start_dist;
            event_speed();
        }
        break;
    default: event_speed();
    }
    setup(c, img, spec, race, e, i, control, pedal, ghost, mode, dist, x, y, z, heading, speed);
    // The gearbox part's kind (0x13 without a record).
    {
        u32 kind = 0x13;
        const u64 id = eq.part[size_t(data::TableId::Gear)];
        if (id != 0)
            if (const ByteSpan* r = race.db->record(data::TableId::Gear, id); r && r->size() > 0x10)
                kind = race.data->w(ProgramData::Table::GearKinds, 4 * u32((*r)[0x10]));
        c.x16d0 = kind != 0x13;
        // (Rule::Layout: the later builds' start, Concept 2001 Tokyo, sets it for a layout 7 car as well)
        if (!gt3_program(c.behaviour.rule(Rule::Layout)) && c.p.d.layout == 7) c.x16d0 = 1;
    }
    c.x16d1 = 1;
    c.dist_total = k0;
    save_previous(c);
    wheel_spin(c);
    store_image(c, img.data());
    if (later) *later = c.later;
    return img;
}

} // namespace gt3r::sim::exact
