// A whole race in GT3's arithmetic; see race.h.

#include "sim/exact/race.h"

#include "formats/course/course_file.h"
#include "sim/course_collision.h"
#include "sim/exact/ai_setup.h"

#include <algorithm>
#include <stdexcept>

namespace gt3r::sim::exact {

namespace {

namespace cx = collide::exact;

const F kZero = F::raw(0), kHalf = F::raw(0x3F000000), kOne = F::raw(0x3F800000);

// A post to the race's event queue {type, word, priority}.
RaceEvent post(s32 type, s32 word, s32 prio) {
    RaceEvent e;
    e.type = type, e.car = word, e.arg = prio;
    return e;
}

// The conductors whose collision step is the all-bodies step (walls and car contact); the base conductor's
// steps each body against the walls only.
bool all_bodies(Conductor k) {
    return k == Conductor::SinglePlayer || k == Conductor::Battle2P || k == Conductor::BattleMP || k == Conductor::FreePractice;
}

// The car's strongest hit of the step (car +0x16C8 / +0x16CC / +0x16CD, the pad feedback): a wall's |j| / 50000, a
// car's twice the given strength, kind 2 |x| / 50, at most 1 and nothing below 0.2; kind -1 does nothing.
void hit_record(Car& c, s32 kind, s32 part, F x) {
    if (kind == -1) return;
    F v = kZero;
    if (kind == 1) v = x + x;
    else if (kind == 0) v = x / F::raw(0x47435000); // 50000
    else if (kind == 2) {
        v = x / F::raw(0x42480000);
        if (v < kZero) v = -v;
    }
    if (kOne < v) v = kOne;
    if (v < F::raw(0x3E4CCCCD)) v = kZero; // 0.2
    if (F::raw(c.feedback) < v) {
        c.hit_part = u8(part);
        c.feedback = v.bits;
        c.hit_kind = u8(kind);
    }
}

// A colour from four channels times k (alpha times 128), each rounded into 0..255.
u32 pack_colour(F r, F g, F b, F a, F k) {
    const F kMax = F::raw(0x437F0000);
    F ch[4] = {r * k, g * k, b * k, a * F::raw(0x43000000)};
    u32 out = 0;
    for (int i = 0; i < 4; ++i) {
        F v = ee::max(ch[i], kZero) + kHalf;
        v = ee::min(v, kMax);
        out |= (u32(ee::to_int(v)) & 0xFFu) << (8 * i);
    }
    return out;
}

F env_f(const std::vector<u8>& env, u32 o) { return F::raw(rd32(ByteSpan(env.data(), env.size()), o)); }

void env_w(std::vector<u8>& env, u32 o, u32 v) {
    for (u32 k = 0; k < 4; ++k) env[o + k] = u8(v >> (8 * k));
}

// the conductor's initialisation, every race's initialisation: the tyre effect kinds' parameters in the course's Env0
// block (+0x4D0 + 0x44 kind) from the program's table, 8 bytes per kind: an off kind (+0x4DC below 0.01) turned on (1), the
// size +0x4E0 (x 0.01), +0x4E4 (x 0.1), the growth +0x4E8, +0x4EC, +0x4F0, +0x4FC, +0x500 (x 0.01), the life count +0x510.
void effect_params(std::vector<u8>& env, const ProgramData& d) {
    const F k1 = F::raw(0x3C23D70A), k2 = F::raw(0x3DCCCCCD);
    if (env.size() < 0x4DC + 0x44 * 11) throw std::runtime_error("course: the Env0 block is too short for the tyre effects");
    for (u32 k = 0; k < 11; ++k) {
        const u32 p = 0x4DC + 0x44 * k, t = 8 * k;
        if (env_f(env, p) < k1) env_w(env, p, kOne.bits);
        auto b = [&](u32 i) { return ee::int_to_f(s32(d.b(ProgramData::Table::EffectParams, t + i))); };
        env_w(env, p + 0x04, (b(0) * k1).bits);
        env_w(env, p + 0x08, (b(1) * k2).bits);
        env_w(env, p + 0x0C, (b(2) * k1).bits);
        env_w(env, p + 0x10, b(3).bits);
        env_w(env, p + 0x14, (b(4) * k1).bits);
        env_w(env, p + 0x20, (b(5) * k1).bits);
        env_w(env, p + 0x24, (b(6) * k1).bits);
        env_w(env, p + 0x34, d.b(ProgramData::Table::EffectParams, t + 7));
    }
}

} // namespace

RaceCourse::~RaceCourse() = default;

std::shared_ptr<const RaceCourse> RaceCourse::load(ByteSpan file, const ProgramData& d) {
    auto c = std::make_shared<RaceCourse>();
    const course::CourseFile cf = course::CourseFile::parse(file);
    const ByteSpan road = cf.section(file, 28);
    if (road.empty()) throw std::runtime_error("course: no road block (slot 28)");
    c->road = course::Road::parse(road);
    // Every per-surface table the race reads has 16 entries (a wheel's surface index is its triangle's material byte, or 12 ..
    // 14 for the alternate materials): a course with a material past them would have the program read past its tables.
    for (const course::RoadSegment& seg : c->road.segments)
        for (const course::RoadTriangle& t : seg.triangles)
            if (u8(t.material) >= 16) throw std::runtime_error("course: a road material past the program's surface tables");
    const ByteSpan lines = cf.section(file, 31);
    c->has_lines = !lines.empty();
    if (c->has_lines) c->lines = course::AiLines::parse(lines);
    c->sets = course::DriverSets::parse(file, cf);
    const ByteSpan env = cf.section(file, 32);
    c->env.assign(env.begin(), env.end());
    if (!c->env.empty()) effect_params(c->env, d);
    c->ground = std::make_unique<CourseGround>(c->road);
    c->query = std::make_unique<CourseQuery>(c->road);
    c->collision = std::make_unique<sim::CourseCollision>(c->road);
    c->ai = ai_course_setup(*c->query, c->has_lines ? &c->lines : nullptr, d);
    return c;
}

ExactRace::ExactRace(const RaceSetup& s) {
    if (!s.data || !s.db || !s.course) throw std::runtime_error("ExactRace: the setup needs the program's tables, the cars and the course");
    const size_t n = s.entries.size();
    if (n == 0 || n > 8) throw std::runtime_error("ExactRace: a race has 1 to 8 cars");
    course_ = s.course;
    data_ = std::make_shared<const ProgramData>(*s.data);
    const ProgramData& d = *data_;
    kind_ = s.kind, category_ = s.course_category, effects_ = s.effects, flow_byte_ = s.config, ghost_switch_ = s.ghost_switch;
    // The race configuration (from the event record, the race's laps), the conductor's rubber-band switch and wear setting as
    // the initialisation asks them, then the tick's inputs from the program's tables.
    cfg_ = s.rules ? *s.rules : race_config(s.event);
    if (s.laps >= 0) cfg_[1] = u8(s.laps);
    // With records off or locked (race info + 0x6C / + 0x7C; the quick menus' races) the race has no laps and no
    // time limit (+0x38), so no lap or clock ends it.
    if (s.replay || s.locked) cfg_[1] = 0, cfg_[0x38] = cfg_[0x39] = 0;
    const Behaviour how = d.behaviour();
    const bool band_on = rubber_band_answer(kind_, cfg_, s.battle_catchup, how.rule(Rule::CatchupAnswer));
    const bool wear_on = wear_answer(kind_, cfg_, category_, s.battle_wear, how.rule(Rule::WearAnswer));
    race_ = race_inputs(d, kind_, cfg_, wear_on, category_);
    race_.course = course_->ground.get();
    race_.course_out = course_out_rule(kind_, category_, course_->ai);
    band_ = rubber_band_params(cfg_, band_on);
    band_mode_ = rubber_band_mode(kind_, cfg_);
    band_cap_ = rubber_band_cap(kind_);
    // The race flow's and the pit lane's inputs.
    info_.start_type = s.start_type, info_.seconds = s.countdown;
    RaceFlowInputs& f = flow_in_;
    f.behaviour = how;
    f.kind = kind_;
    f.course = course_->query.get();
    f.split = F(course_->road.start_distance);
    f.start_type = s.start_type;
    f.laps = race_laps(kind_, cfg_, s.battle_laps);
    for (size_t k = 0; k < 3; ++k) f.config[k] = cfg_[k];
    f.time_limit = u16(cfg_[0x38] | cfg_[0x39] << 8);
    f.flow = flow_byte_;
    f.messages = hud_messages(d);
    f.machine_sectors = machine_sectors(d);
    f.replay = s.replay, f.unjudged = s.unjudged, f.locked = s.locked;
    f.logger = s.logger != 0, f.logger_record = s.logger_record != 0;
    f.licence_times = s.licence_times;
    f.speed_unit = s.speed_unit;
    x70_ = s.logger;
    flow_.session = s.session;
    flow_.entries = s.records;
    flow_.entries.resize(n);
    pit_in_.lap = course_->query->lap_length();
    pit_in_.pit_lines = course_->ai.pit_lines;
    pit_in_.service_always = kind_ == Conductor::FreePractice; // the service rule
    pit_in_.wear = race_.prep.wear;
    pit_in_.config = flow_byte_;
    for (u32 k = 0; k < 16; ++k) effect_kinds_[k] = d.w(ProgramData::Table::EffectKinds, 4 * k);
    for (u32 k = 0; k < 8; ++k) effect_scales_[k] = F::raw(d.w(ProgramData::Table::EffectScales, 4 * k));
    // The emitter reads the course's Env0 at +0x910 and each surface's effect kind's parameters (+0x4D0 + 0x44 kind ..
    // + 0x514): a course whose block is shorter is refused here, so no step reads past it.
    if (effects_) {
        const size_t env = course_->env.size();
        bool ok = env >= 0x914;
        for (u32 kind : effect_kinds_) ok = ok && u64(0x44) * kind + 0x514 <= env;
        if (!ok) throw std::runtime_error("course: the Env0 block is too short for the tyre effects");
    }
    // Every car's record (the race start) and the ranking it ends with (the ranking set-up's rank reset and ranking).
    StartRace r;
    r.db = s.db, r.course = course_->query.get(), r.ai = &course_->ai, r.sets = &course_->sets, r.data = &d;
    r.rules = cfg_, r.wear = race_.prep.wear, r.category = category_, r.config = s.config;
    r.start_type = s.start_type, r.kind = kind_, r.handicap = s.handicap, r.mark_pit = has_pits(kind_);
    r.info = s.info, r.conductor = s.conductor, r.course_addr = s.course_addr, r.road = s.road_addr;
    const StartResult sr = start_cars(r, s.entries);
    cars_.resize(n);
    for (size_t i = 0; i < n; ++i) cars_[i].behaviour = how, load_image(cars_[i], sr.records[i].data());
    order_ = sr.order;
    // The rest of the ranking set-up: the proximity matrices cleared, the gap matrices,
    // the bodies built (per car, the pair tables) after the copy-in (the build leaves
    // the committed state alone; ours clears it, so the copy-in runs after it).
    prox_ = {};
    gaps_ = {};
    gap_matrix(cars_, pit_in_.lap, gaps_);
    bodies_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Params& p = cars_[i].p;
        bodies_[i].behaviour = how; // build() keeps it
        cx::build(bodies_[i], p.body_front, p.body_rear, p.body_width, p.mass, p.yaw_inertia, int(i));
        bodies_[i].course = course_->collision.get();
    }
    cx::pair_tables(bodies_);
    copy_in();
    // On race info + 0x40: no result yet (-2), no time.
    flow_.result = -2, flow_.result_time = 0x157529FF, flow_.record = 0;
    // The race's start-up: the particle system's reset seeds the VU0 random register with 0.1.
    rng_.init(Vu0Random::kRunSeed);
    hit_.assign(n, 0), hit_43c_.assign(n, 0);
    posed_.assign(n, 0);
    if (s.recorders) rec_ = *s.recorders, ghost_car_ = s.ghost_car, external_ghost_ = s.external_ghost;
    // The race's start-up: the sparks' reset.
    spark_points_ = s.sparks;
    spark_points_.resize(n);
    for (const std::vector<SparkPoint>& p : spark_points_) sparks_on_ = sparks_on_ || !p.empty();
    sparks_.reset();
}

void ExactRace::start_countdown() { info_.start = 1; }

void ExactRace::set_start_count(s32 n) { info_.start_count = n; }

void ExactRace::set_start_type(s32 t) { info_.start_type = t, flow_in_.start_type = t; }

void ExactRace::set_flow_byte(u8 b) { flow_byte_ = b, flow_in_.flow = b, pit_in_.config = b; }

void ExactRace::set_logger_input(int i, u32 frame, bool relative) {
    if (u32(i) >= 8) return;
    flow_in_.logger_frame[size_t(i)] = frame, flow_in_.logger_relative[size_t(i)] = relative;
}

void race_info_frame(RaceInfoStart& info, std::vector<RaceEvent>& events, int steps) {
    if (info.start != 0) {
        info.start = 0;
        switch (info.start_type) {
        case -1:
            events.push_back(post(0x14, 0x14, 0));
            info.seconds = 0;
            break;
        case 0:
            if (info.seconds != 0) events.push_back(post(2, 0, 0));
            break;
        case 1:
            info.seconds = 4;
            events.push_back(post(2, 0, 0)), events.push_back(post(0x14, 0x1B, 0));
            break;
        case 2:
            info.seconds = 10;
            events.push_back(post(2, 0, 0)), events.push_back(post(0x14, 0x12, 0));
            break;
        case 3: info.seconds = 0; break;
        default: break;
        }
        info.start_count = steps * info.seconds;
    }
    if (info.start_count != 0) {
        if (--info.start_count == 0) events.push_back(post(4, 0, 0));
    } else if (info.timer_on != 0 && --info.timer == 0) {
        info.timer_on = 0;
        events.push_back(post(7, 0, 0));
    }
}

// Each body's committed state from its car (position, velocity, heading, yaw rate) and its road segment hint.
void ExactRace::copy_in() {
    for (size_t i = 0; i < cars_.size(); ++i) {
        cx::Body& b = bodies_[i];
        const Car& c = cars_[i];
        b.cx = c.pos.x, b.cy = c.pos.y, b.cz = c.pos.z;
        b.cvx = c.vel.x, b.cvy = c.vel.y, b.cvz = c.vel.z;
        b.hint = c.road_seg;
        b.ch = c.heading, b.cw = c.yaw_rate;
    }
}

// Phase 10, the conductor's collision step: the copy-in, the step (all bodies, or each body against the
// walls), the copy-out; each response's report to the race (the conductor's hit rule, the car's hit
// record), in the order the step made them. The impact effects write the car's rumble strength. The step's second
// entry mode runs for the bodies from `first` (1).
void ExactRace::collide(size_t first) {
    const size_t n = cars_.size();
    copy_in();
    for (size_t i = 0; i < n; ++i) {
        cx::Body& b = bodies_[i];
        Car& c = cars_[i];
        b.dt_in = c.dt, b.steer = c.steer_pct, b.ghost = c.ghost != 0;
        b.rumble = &c.hit_rumble;
    }
    // Posed cars: the body starts back along the posed motion (the pose is the end of this step) from its own outline, with
    // none of last step's contact flags.
    for (size_t i = 0; i < n; ++i) {
        if (!posed_[i]) continue;
        cx::Body& b = bodies_[i];
        const Car& c = cars_[i];
        b.cx = c.pos.x - (c.vel.x * c.dt), b.cy = c.pos.y - (c.vel.y * c.dt), b.cz = c.pos.z - (c.vel.z * c.dt);
        b.ch = c.heading - (c.yaw_rate * c.dt);
        b.have_points = false;
        for (cx::Point& q : b.p) q.flags = 0;
    }
    static thread_local cx::Trace trace;
    trace.clear();
    if (first == 0 && all_bodies(kind_)) cx::step_all(bodies_, &trace);
    else
        for (size_t i = first; i < n; ++i) cx::step_walls(bodies_, int(i), &trace);
    for (size_t i = 0; i < n; ++i) {
        cx::Body& b = bodies_[i];
        Car& c = cars_[i];
        b.rumble = nullptr;
        hit_[i] = b.wall_hit || b.car_hit;
        hit_43c_[i] = b.wall_hit;
        if (posed_[i]) continue; // its record keeps the pose: its owner's machine answers its hits
        c.pos = {b.cx, b.cy, b.cz};
        c.vel = {b.cvx, b.cvy, b.cvz};
        c.heading = b.ch, c.yaw_rate = b.cw;
    }
    auto report = [&](int car, s32 kind, s32 part, F x) {
        hits_.push_back({car, kind, part, x});
        Car& c = cars_[size_t(car)];
        hit_course_out(c, kind_, kind, x);
        hit_record(c, kind, part, x);
    };
    for (const cx::Response& r : trace) {
        switch (r.kind) {
        case cx::Response::Point: report(r.body, 0, r.feature, r.mag); break;
        case cx::Response::Corner: report(r.body, 0, r.feature + 8, r.mag); break;
        case cx::Response::Car: {                                                 // |j| / 15000 to both
            const F s = r.mag / F::raw(0x466A6000);
            report(r.body, 1, r.feature + 8, s);
            report(r.other, 1, r.vertex, s);
            // The impulse on the struck body is -|j| along its edge's outward normal (pair_impulse: j n with j < 0 for a
            // vertex moving in), the striking body's the opposite.
            car_contacts_.push_back({r.body, r.other, float(r.x), float(r.y), float(-r.j * r.nx), float(-r.j * r.ny)});
            break;
        }
        }
    }
}

// The program hands car i's four requests to the course's tyre effect emitter: a particle when the surface's
// effect kind is on (Env0 +0x4DC at least 0.01) and the wheel emits more than 0.001, with six draws of the VU0 random register.
void ExactRace::emit(int i, const ChassisOutputs& out) {
    if (!effects_) return;
    const std::vector<u8>& env = course_->env;
    for (const TyreEffect& q : out.effects) {
        const u32 kind = effect_kinds_[u32(q.surface) & 15]; // (below 16: RaceCourse::load)
        const u32 base = 0x44 * kind;                        // (in the course's Env0: the race's construction)
        if (env_f(env, base + 0x4DC) < F::raw(0x3C23D70A)) continue; // 0.01
        F e = q.smoke_emit;
        if (e <= F::raw(0x3A83126F)) continue; // 0.001
        SmokeParticle p;
        p.car = i, p.wheel = q.wheel;
        F r = vu0_random(rng_);
        s32 cls = ee::to_int(r * F::raw(0x41000000));
        if (!(cls < 8)) cls = 7;
        p.size_class = u8(cls);
        e = e * effect_scales_[size_t(cls)];
        if (q.wheel & 1) p.size_class = u8(cls | 0x20);
        p.kind = u8(kind);
        const F g = env_f(env, 0x910);
        F a = q.attr_a * (kOne - g);
        a = a + g;
        p.colour = pack_colour(env_f(env, base + 0x4D0) * a, env_f(env, base + 0x4D4) * a, env_f(env, base + 0x4D8) * a, kZero,
                               F::raw(0x43000000));
        p.emit = e;
        p.size = env_f(env, base + 0x4E0) * e;
        r = vu0_random(rng_);
        p.growth = env_f(env, base + 0x4E8) * (((r - F::raw(0x3E4CCCCD)) * F::raw(0x3E99999A)) + kOne);
        r = vu0_random(rng_);
        p.angle = r * F::raw(0x43B40000);
        p.x10 = e + ((kOne - e) * env_f(env, base + 0x500));
        r = vu0_random(rng_);
        const u32 count = rd32(ByteSpan(env.data(), env.size()), base + 0x510);
        F cf;
        if (s32(count) >= 0) cf = ee::int_to_f(s32(count));
        else {
            cf = ee::int_to_f(s32((count >> 1) | (count & 1)));
            cf = cf + cf;
        }
        F life = (r * (cf + F::raw(0x3E4CCCCD))) + kOne; // 0.2
        if (F::raw(0x437F0000) <= life) life = F::raw(0x437F0000);
        p.life = s16(ee::to_int(life * F::raw(0x43800000)));
        r = vu0_random(rng_);
        p.x1f = u8(ee::to_int(r * F::raw(0x42800000)));
        r = vu0_random(rng_);
        p.x1e = u8(ee::to_int((((r - kHalf) * F::raw(0x3E4CCCCD)) + kOne) * F::raw(0x43000000)));
        p.x = q.x, p.h = q.h, p.z = q.z;
        p.normal = q.normal, p.plane_d = q.plane_d;
        p.ground = (-(((q.normal.x * q.x) + (q.normal.z * q.z)) + q.plane_d)) / q.normal.y;
        particles_.push_back(p);
    }
}

// The step after phase 13: the mode start and each car's race flow over the order (tick_flow; the free run
// ghost's extra step inside), the time-difference hook (the sector order's time differences), the race clock (race_mode_clock), the gap matrices,
// the ranking, the rubber band. The second entry mode: the race flow from the order's position 1, the rubber band.
void ExactRace::flow_after(AiPlan& plan, std::span<const PadFrame> pads, int mode) {
    const size_t n = cars_.size();
    // The free run's lap handlers read the recorders as phase 3 left them, the data logger the devices' frames (race input device +0xFC:
    // its recorder's count; its table counts from its first entry's in mode 2, +0xC8).
    if (rec_) {
        flow_in_.recorder = rec_->inputs(flow_in_.behaviour);
        flow_in_.logger_frame[0] = rec_->player.rec.count, flow_in_.logger_relative[0] = rec_->player.mode == 2;
        if (ghost_car_ > 0 && ghost_car_ < 8)
            flow_in_.logger_frame[size_t(ghost_car_)] = rec_->ghost.rec.count, flow_in_.logger_relative[size_t(ghost_car_)] = rec_->ghost.mode == 2;
    }
    FlowStep f;
    f.in = flow_in_;
    f.flow = &flow_;
    f.order = std::span<const s32>(order_.data(), n);
    f.first = mode != 0 ? 1 : 0;
    f.hit = hit_, f.hit_43c = hit_43c_;
    f.mode_start = mode == 0;
    f.pit = pit_in_;
    f.events = &events_, f.pit_events = &pit_events_;
    f.ghost_step = [this, pads] {
        std::array<PadFrame, 8> p{};
        for (size_t i = 0; i < pads.size() && i < 8; ++i) p[i] = pads[i];
        const std::span<PadFrame> frames(p.data(), cars_.size());
        if (nested_pads_) nested_pads_(frames);
        apply_recorder_events(); // (the ghost's restart before the step runs for it)
        run(frames, 1);
    };
    f.save_body = [this](int k, Snapshot& s) {
        if (size_t(k) >= bodies_.size()) return;
        s.body = bodies_[size_t(k)].p, s.has_body = true;
    };
    f.restore_body = [this](int k, const Snapshot& s) { restore_body(k, s); };
    tick_flow(cars_, f, plan);
    apply_recorder_events();
    if (mode == 0) {
        f.pit.flow = &f.in;
        f.pit.clock = flow_.clock;
        RaceFlowHooks hooks;
        hooks.rolling_handback = [&](Car& c) { pit_exit(c, plan); };
        hooks.finish_drive = [&](Car& c, int, int m) { end_of_run_drive(c, m, f.pit, plan); };
        hooks.ai_takeover = [&](Car& c, int) { plan.replan(c); };
        hooks.drive_on = [&](Car& c, int) { exact::drive_on(c, plan); };
        hooks.save_body = f.save_body;
        race_mode_display(cars_, f.in, flow_, events_);
        race_mode_clock(cars_, f.in, flow_, events_, hooks);
        gap_matrix(cars_, pit_in_.lap, gaps_);
        race_ranking(cars_, order_, kind_);
    }
    rubber_band(cars_, std::span<const s32>(order_.data(), n), band_mode_, band_, pit_in_.lap, band_cap_);
}

void ExactRace::step(std::span<const PadFrame> pads) {
    events_.clear(), pit_events_.clear(), hits_.clear(), particles_.clear(), car_contacts_.clear(), flow_.recorder.clear();
    rec_applied_ = 0;
    race_info_frame(info_, events_, steps_per_second(data_->behaviour()));
    run(pads, 0);
    rng_step_ = rng_;
    spark_frame();
}

// The race's frame after the race step: every car's bursts (each entry's model is loaded, its +0x18BC
// set), in race order; then the race's frame update's update of the sparks (dt 1/60), after the cars' animators,
// the flares, the tyre smoke's update and the course's rigid bodies, none of which draws the random register. A posed car
// (online) makes none: its record has no drawn pose of its own.
void ExactRace::spark_frame() {
    if (!sparks_on_) return;
    for (size_t i = 0; i < cars_.size(); ++i) {
        const Car& c = cars_[i];
        if (posed_[i]) continue;
        sparks_.emit(c, spark_points_[i], *course_->query, draw_mode(c, x70_, ghost_switch_) >= 2, rng_);
    }
    sparks_.update(F::raw(0x3C888889), rng_);
}

void ExactRace::race_step(std::span<const PadFrame> pads) {
    events_.clear(), pit_events_.clear(), hits_.clear(), particles_.clear(), car_contacts_.clear(), flow_.recorder.clear();
    rec_applied_ = 0;
    run(pads, 0);
}

void ExactRace::run(std::span<const PadFrame> pads, int mode) {
    pit_in_.flow = &flow_in_, pit_in_.rules = &cfg_;
    flow_in_.start_count = info_.start_count, flow_in_.flow = flow_byte_;
    race_.prep.start_count = info_.start_count;
    // Conductor, mode: the gate over every car, then the cars from `first` (1 in the second entry mode)
    const bool ran = race_flow_gate(cars_, flow_byte_, time_scale_);
    if (mode == 0) ran_ = ran;
    if (ran) {
        const size_t n = cars_.size(), first = mode != 0 ? 1 : 0;
        std::array<CarInputs, 8> in{};
        const bool pits = has_pits(kind_);
        const s32 pmode = pit_mode(kind_, category_);
        for (size_t i = 0; i < n; ++i) {
            CarInputs& k = in[i];
            const Car& c = cars_[i];
            if (i >= first) k.pad = device_frame(i, i < pads.size() ? pads[i] : PadFrame{});
            k.ai.start_count = info_.start_count;
            k.ai.clock = flow_.clock;
            const cx::Body& b = bodies_[std::min(size_t(c.index), n - 1)];
            k.ai.hit_x = 0, k.ai.hit_front = b.front, k.ai.hit_rear = b.rear;
            k.ai.course_category = category_;
            k.mark_flagged = pits;
            k.box_heading = pit_box_heading(course_->ai, c.pit_box);
            // The program reads the switch only with +0x70 clear; NTSC-J's reads only the switch
            if (ntsc_j_rules(c, Rule::DrawMode)) k.x70 = 0, k.ghost_switch = ghost_switch_;
            else k.x70 = x70_, k.ghost_switch = x70_ ? 0 : ghost_switch_;
            k.pit = {pits, pmode, flow_byte_};
            k.posed = posed_[i] != 0;
        }
        const std::span<const CarInputs> ins(in.data() + first, n - first);
        const std::span<Car> run_cars = std::span<Car>(cars_).subspan(first);
        CoursePlan plan(course_->ai, race_.prep.wear);
        tick_until_collision(run_cars, ins, race_, plan);
        collide(first);
        tick_after_collision(run_cars, ins, race_, mode == 0 ? &prox_ : nullptr, rng_,
                             [this, first](int i, const ChassisOutputs& o, Vu0Random&) { emit(i + int(first), o); });
        flow_after(plan, pads, mode);
    }
    // The race's end countdown (event 1 at its end).
    if (flow_.end_count != 0 && --flow_.end_count == 0) events_.push_back(post(1, 0, 0));
}

// Phase 3's pad read for car i: with the solo recorders the ghost car's frame is its device's next
// one and the player's car's frame is recorded on its device; then, a car that is no ghost in the free run, the frame into
// the lap recording (the conductor's pad hook records it for a car with an input device: the
// player's).
PadFrame ExactRace::device_frame(size_t i, const PadFrame& given) {
    if (!rec_) return given;
    PadFrame f = given;
    if (int(i) == ghost_car_) {
        if (!external_ghost_) f = rec_->ghost_frame();
    } else if (i == 0) {
        f = rec_->player_frame(given);
    }
    if (kind_ == Conductor::FreeRun && i == 0 && cars_[i].ghost == 0) rec_->lap_frame(f);
    return f;
}

bool ExactRace::logger_jump(int k) {
    if (flow_.logger.empty() || !flow_.logger[0].count(k)) return false;
    info_.start = 0;
    auto jump = [&](int i) {
        if (size_t(i) >= flow_.logger.size() || size_t(i) >= cars_.size()) return;
        const auto it = flow_.logger[size_t(i)].find(k);
        if (it == flow_.logger[size_t(i)].end()) return; // (no such entry: the program would read the table's stale entry)
        const Snapshot& s = it->second;
        Car& c = cars_[size_t(i)];
        restore_car(c, s, flow_.clock);
        restore_body(c.index, s);
        if (!rec_) return;
        ControlManager& m = i == 0 ? rec_->player : rec_->ghost;
        // its frame (+4), but the first entry's of a table counting from it (+0xC8 2): 0
        const u32 frame = k == 0 && m.mode == 2 ? 0 : s.frame;
        m.rec.seek(i == 0 ? rec_->pool0 : rec_->pool1, frame);
    };
    jump(0);
    if (rec_ && !rec_->ghost_empty && ghost_car_ == 1) jump(1);
    return true;
}

void ExactRace::apply_recorder_events() {
    if (!rec_) return;
    for (; rec_applied_ < flow_.recorder.size(); ++rec_applied_) {
        const RecorderEvent& e = flow_.recorder[rec_applied_];
        rec_->apply(e);
        // 1, 1 taking the lap at once (the Ghost option on): race info + 0x48 cleared (nothing left to ask), the
        // player's entry copied into the ghost car's (its lap list among the rest) and that list then counted only
        // (1: no laps yet).
        if (e.kind == RecorderEvent::ghost && rec_->ghost_option) {
            flow_.ghost = 0;
            if (ghost_car_ > 0 && size_t(ghost_car_) < flow_.entries.size()) {
                LapList& l = flow_.entries[size_t(ghost_car_)].laps;
                l = flow_.entries[0].laps;
                l.count = 0, l.counted_only = 1;
            }
        }
    }
}

namespace {
// The snapshot's body points but their local geometry (+0x00 .. +0x0F of each).
void restore_points(cx::Body& b, const Snapshot& s) {
    for (size_t j = 0; j < 8; ++j) {
        const cx::Point keep = b.p[j];
        b.p[j] = s.body[j];
        b.p[j].lx = keep.lx, b.p[j].ly = keep.ly, b.p[j].ux = keep.ux, b.p[j].uy = keep.uy;
    }
}
} // namespace

// The body: the body rebuilt from its car; every pair table, then the
// snapshot's points but their local geometry. The NTSC-J program does not rebuild the body: only the snapshot's points.
void ExactRace::restore_body(int k, const Snapshot& s) {
    if (size_t(k) >= bodies_.size()) return;
    cx::Body& b = bodies_[size_t(k)];
    if (ntsc_j_rules(cars_[size_t(k)], Rule::LapStartRestore)) {
        if (s.has_body) restore_points(b, s);
        return;
    }
    const Params& p = cars_[size_t(k)].p;
    // The program writes the body's geometry, mass, radius and its flags and effect counters (a point's held state and wall,
    // the start points' carry-over); the sweep, the hits and the points' and edges' crossing state stay.
    cx::Body built;
    cx::build(built, p.body_front, p.body_rear, p.body_width, p.mass, p.yaw_inertia, k);
    b.im = built.im, b.ii = built.ii, b.car = built.car, b.mass = built.mass, b.radius = built.radius, b.enabled = built.enabled;
    b.have_points = false, b.rumble_steps = 0, b.rumble_level = built.rumble_level, b.sound_steps = 0, b.sound_level = built.sound_level;
    b.course = course_->collision.get();
    for (size_t j = 0; j < 8; ++j) {
        cx::Point& q = b.p[j];
        q.lx = built.p[j].lx, q.ly = built.p[j].ly, q.ux = built.p[j].ux, q.uy = built.p[j].uy;
        q.flags = 0, q.wall = -1;
        cx::Edge& e = b.e[j];
        const cx::Edge& n = built.e[j];
        e.sx = n.sx, e.sy = n.sy, e.ex = n.ex, e.ey = n.ey, e.nx = n.nx, e.ny = n.ny, e.d = n.d;
    }
    cx::pair_tables(bodies_);
    if (s.has_body) restore_points(b, s);
}

void ExactRace::set_posed(int i, bool on) { posed_.at(size_t(i)) = on ? 1 : 0; }

bool ExactRace::posed(int i) const { return posed_.at(size_t(i)) != 0; }

void pose_motion(Car& d, const Car& s) {
    d.pos = s.pos, d.vel = s.vel;
    d.fwd = s.fwd, d.lat = s.lat, d.up = s.up;
    d.pitch = s.pitch, d.roll = s.roll, d.heading = s.heading;
    d.yaw_rate = s.yaw_rate, d.pitch_rate = s.pitch_rate, d.roll_rate = s.roll_rate;
    d.speed = s.speed, d.lat_speed = s.lat_speed;
    d.road_seg = s.road_seg;
    d.dt = s.dt, d.time_scale = s.time_scale;
    d.steer_pct = s.steer_pct, d.steer_cmd = s.steer_cmd;
    d.throttle_pedal = s.throttle_pedal, d.brake_pedal = s.brake_pedal, d.handbrake = s.handbrake;
    d.gear = s.gear, d.rpm = s.rpm, d.omega = s.omega, d.rev_limiter = s.rev_limiter;
    d.x535 = u8((d.x535 & ~0x10u) | (s.x535 & 0x10u));
}

void ExactRace::pose(int i, const Car& src) { pose_motion(cars_.at(size_t(i)), src); }

void ExactRace::apply_impulse(int i, F px, F py, F jx, F jy) {
    Car& c = cars_.at(size_t(i));
    const cx::Body& b = bodies_.at(size_t(i));
    const F rx = px - c.pos.x, ry = py - c.pos.y;
    c.vel.x = c.vel.x + (jx * b.im);
    c.vel.y = c.vel.y + (jy * b.im);
    c.yaw_rate = c.yaw_rate + (((rx * jy) - (ry * jx)) * b.ii);
}

bool ExactRace::drive_on(int i) {
    CoursePlan plan(course_->ai, race_.prep.wear);
    return exact::drive_on(cars_.at(size_t(i)), plan);
}

bool ExactRace::hand_back(int i) {
    CoursePlan plan(course_->ai, race_.prep.wear);
    return pit_exit(cars_.at(size_t(i)), plan);
}

void ExactRace::end_run(int i, int mode) {
    CoursePlan plan(course_->ai, race_.prep.wear);
    PitInputs in = pit_in_;
    in.flow = &flow_in_, in.rules = &cfg_, in.clock = flow_.clock;
    end_of_run(cars_.at(size_t(i)), mode, in, plan);
}

void ExactRace::close_recordings(bool lap) {
    if (!rec_) return;
    rec_->player.rec.close(rec_->pool0);
    if (lap) rec_->lap.rec.close(rec_->pool1);
}

} // namespace gt3r::sim::exact
