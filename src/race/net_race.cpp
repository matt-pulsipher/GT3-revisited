#include "race/net_race.h"

#include "base/log.h"
#include "race/exact_car.h"
#include "race/interp.h"
#include "sim/course_collision.h"
#include "sim/exact/race.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace gt3r::race {

namespace {

using ex::F;

// Rotation by the world-frame rotation vector w (axis * angle).
sim::Quat quat_from_rotvec(const sim::Vec3& w) {
    float a = w.length();
    if (a < 1e-9f) return {};
    float s = std::sin(a * 0.5f) / a;
    return {std::cos(a * 0.5f), w.x * s, w.y * s, w.z * s};
}

sim::Quat conj(const sim::Quat& q) { return {q.w, -q.x, -q.y, -q.z}; }

// A fixed-unit value of the snapshot: v x scale, rounded, within s16 (scale a power of two: exact both ways).
s16 fixed(F v, float scale) {
    const double x = double(v.host()) * double(scale);
    if (!(std::fabs(x) < 32767.0)) return x > 0 ? s16(32767) : s16(-32768); // (out of range, or not a number)
    return s16(std::lround(x));
}
F unfixed(s16 v, float scale) { return F(float(v) / scale); }

constexpr float kRate = 1024.0f, kRim = 128.0f, kSusp = 65536.0f;

// The road segment under a moved car (the collision step's and the race progress's hint): among the old one's neighbours,
// else anywhere; kept when the car is off the road.
void move_hint(ex::Car& c, const sim::CourseCollision& course) {
    int seg = course.road_segment(c.pos.x, -c.pos.y, c.road_seg);
    if (seg < 0) seg = course.road_segment(c.pos.x, -c.pos.y, -1);
    if (seg >= 0) c.road_seg = seg;
}

ex::PadFrame pad_frame(const net::PadInput& p) {
    ex::PadFrame f;
    f.flags = p.flags, f.kind = p.kind, f.shift = p.shift;
    f.steer = p.steer, f.throttle = p.throttle, f.brake = p.brake, f.back = p.back, f.third = p.third;
    return f;
}

} // namespace

net::CarMotion car_motion(const ex::Car& c) {
    net::CarMotion m;
    m.pos = {c.pos.x.bits, c.pos.y.bits, c.pos.z.bits};
    m.vel = {c.vel.x.bits, c.vel.y.bits, c.vel.z.bits};
    m.heading = c.heading.bits, m.pitch = c.pitch.bits, m.roll = c.roll.bits;
    m.yaw_rate = c.yaw_rate.bits;
    m.omega = c.omega.bits;
    m.steer_cmd = c.steer_cmd.bits;
    m.axle_speed = {c.axle_speed[0].bits, c.axle_speed[1].bits};
    m.pitch_rate = fixed(c.pitch_rate, kRate), m.roll_rate = fixed(c.roll_rate, kRate);
    for (size_t i = 0; i < 4; ++i) m.rim_speed[i] = fixed(c.w[i].rim_speed, kRim), m.susp[i] = fixed(c.w[i].susp, kSusp);
    m.gear = c.gear, m.clutch = c.clutch;
    return m;
}

void sync_motion(ex::Car& c, const net::CarMotion& m) {
    // The wheels' ground points move with the car (until its next step queries the ground again).
    const ex::Vec3 old = c.pos;
    c.pos = {F::raw(m.pos[0]), F::raw(m.pos[1]), F::raw(m.pos[2])};
    const F dx = c.pos.x - old.x, dy = c.pos.y - old.y, dz = c.pos.z - old.z;
    for (size_t i = 0; i < 4; ++i) {
        ex::Ground& g = c.g[i];
        g.x = g.x + dx, g.z = g.z - dy, g.h = g.h + dz;
        c.w[i].ground = c.w[i].ground + dz, c.w[i].ground0 = c.w[i].ground0 + dz;
    }
    c.vel = {F::raw(m.vel[0]), F::raw(m.vel[1]), F::raw(m.vel[2])};
    c.heading = F::raw(m.heading), c.pitch = F::raw(m.pitch), c.roll = F::raw(m.roll);
    c.yaw_rate = F::raw(m.yaw_rate);
    c.omega = F::raw(m.omega);
    c.steer_cmd = F::raw(m.steer_cmd);
    c.axle_speed = {F::raw(m.axle_speed[0]), F::raw(m.axle_speed[1])};
    c.pitch_rate = unfixed(m.pitch_rate, kRate), c.roll_rate = unfixed(m.roll_rate, kRate);
    for (size_t i = 0; i < 4; ++i) c.w[i].rim_speed = unfixed(m.rim_speed[i], kRim), c.w[i].susp = unfixed(m.susp[i], kSusp);
    c.gear = m.gear, c.clutch = m.clutch;
    ex::body_axes(c);
    ex::hub_offsets(c);
}

net::CarEntry car_entry(const ex::Car& c, bool asm_on, bool tcs_on) {
    net::CarEntry e;
    e.pedal_style = u8(c.pedal_style & 7);
    e.wheel_type = u8(u8(c.wheel_type) & 7);
    e.asm_on = asm_on, e.tcs_on = tcs_on;
    e.control = u8(u8(c.control) & 3);
    e.driver_kind = u8(c.driver_kind & 15);
    return e;
}

net::CarSnapshot make_snapshot(const ex::Car& c, const net::CarEntry& entry, u32 tick, double race_time, int laps,
                               const net::PadInput* inputs, int n, bool respawned) {
    net::CarSnapshot s;
    s.tick = tick;
    s.clock = s32(std::lround(race_time * 3000.0));
    s.motion = car_motion(c);
    s.entry = entry;
    s.laps = u8(std::clamp(laps, 0, 255));
    s.rev_limiter = c.rev_limiter != 0;
    s.respawned = respawned;
    s.input_count = u8(std::clamp(n, 0, net::kInputHistory));
    for (int i = 0; i < s.input_count; ++i) s.inputs[i] = inputs[i];
    complete_snapshot(s);
    return s;
}

void complete_snapshot(net::CarSnapshot& s) {
    const net::CarMotion& m = s.motion;
    auto h = [](u32 bits) { return F::raw(bits).host(); };
    s.race_time = float(double(s.clock) / 3000.0);
    sim::VehicleState& st = s.state;
    st = {};
    // The physics frame (Z up) in the course frame: (x, z, -y).
    st.position = {h(m.pos[0]), h(m.pos[2]), -h(m.pos[1])};
    st.velocity = {h(m.vel[0]), h(m.vel[2]), -h(m.vel[1])};
    st.speed_kmh = st.velocity.length() * 3.6f;
    // The heading's rotation (about the course's up axis; the car's nose, -Z, along the forward axis (cos h, sin h) of the
    // physics frame).
    const float a = h(m.heading) - 1.5707963f;
    st.orientation = {std::cos(a * 0.5f), 0, std::sin(a * 0.5f), 0};
    st.angular_velocity = {0, h(m.yaw_rate), 0};
    st.gear = m.gear == 0 ? -1 : int(m.gear);
    st.rev_limiter = s.rev_limiter;
    s.body_x = st.position.x, s.body_z = st.position.z;
    s.heading = h(m.heading), s.yaw_rate = h(m.yaw_rate);
    s.brake = s.input_count ? std::min(1.0f, float(s.inputs[0].brake) / 4096.0f) : 0.0f;
}

// --- RemoteCar ---

RemoteCar::RemoteCar() = default;
RemoteCar::~RemoteCar() = default;
RemoteCar::RemoteCar(RemoteCar&&) noexcept = default;
RemoteCar& RemoteCar::operator=(RemoteCar&&) noexcept = default;

RemoteCar::Pose RemoteCar::raw(double t) const {
    Pose p;
    p.state = snap_.state;
    p.body_x = snap_.body_x, p.body_z = snap_.body_z;
    p.heading = snap_.heading, p.yaw_rate = snap_.yaw_rate;
    p.brake = snap_.brake;
    float age = float(std::clamp(t - double(snap_.race_time), 0.0, kMaxExtrapolation));
    if (age <= 0) return p;
    sim::VehicleState& s = p.state;
    sim::Vec3 d = s.velocity * age;
    s.position += d;
    s.orientation = normalize(quat_mul(quat_from_rotvec(s.angular_velocity * age), s.orientation));
    for (sim::WheelState& w : s.wheels) {
        w.contact += d;
        w.spin_angle += w.angular_velocity * age;
    }
    p.body_x += s.velocity.x * age;
    p.body_z += s.velocity.z * age;
    p.heading += p.yaw_rate * age;
    return p;
}

void RemoteCar::attach(std::unique_ptr<ex::ExactRace> shadow, const net::CarEntry& entry, bool physics) {
    shadow_ = std::move(shadow);
    physics_ = physics;
    moved_.reset();
    if (!shadow_ || shadow_->size() < 1) {
        shadow_.reset();
        return;
    }
    const ex::Car& c = shadow_->cars()[0];
    setup_ = std::make_unique<sim::VehicleSetup>(exact_setup(c));
    hz_ = double(ex::steps_per_second(c.behaviour));
    entry_ = entry;
    nhistory_ = 0;
    if (have_) sync(snap_);
}

void RemoteCar::reset() {
    std::unique_ptr<ex::ExactRace> sh = std::move(shadow_);
    const bool ph = physics_;
    const net::CarEntry e = entry_;
    *this = RemoteCar{};
    if (sh) attach(std::move(sh), e, ph);
}

// The shadow at the owner's state: its motion, the pedal style and driver kind, the start countdown as the owner's clock
// has it, the owner's newest frame.
void RemoteCar::sync(const net::CarSnapshot& s) {
    ex::Car& c = shadow_->cars()[0];
    sync_motion(c, s.motion);
    move_hint(c, *shadow_->course().collision);
    c.pedal_style = s.entry.pedal_style;
    if (s.entry.driver_kind != c.driver_kind) {
        if (s.entry.driver_kind == 1 && c.driver_kind == 0) shadow_->drive_on(0); // the AI drives it on (its line first)
        else c.driver_kind = s.entry.driver_kind;
    }
    shadow_->set_start_count(s32(std::max(0L, std::lround(-double(s.race_time) * hz_))));
    input_ = s.input_count ? s.inputs[0] : net::PadInput{};
    sim_time_ = s.race_time;
    history_[nhistory_++ % history_.size()] = {sim_time_, exact_to_world(c.pos)};
}

void RemoteCar::step_shadow() {
    ex::PadFrame f = pad_frame(input_);
    f.shift = 0; // edges belong to the owner's step; the gear comes with each state
    try {
        shadow_->step(std::span<const ex::PadFrame>(&f, 1));
    } catch (const std::exception& e) { // (the step itself never throws; out of memory: dead reckoning from here)
        LOG_WARN("Online: the prediction stopped (%s); dead reckoning", e.what());
        physics_ = false;
        return;
    }
    sim_time_ += 1.0 / hz_;
    history_[nhistory_++ % history_.size()] = {sim_time_, exact_to_world(shadow_->cars()[0].pos)};
}

RemoteCar::Pose RemoteCar::shadow_pose(double t) {
    const ex::Car* c = &shadow_->cars()[0];
    if (!physics_) { // dead reckoning: the synced record carried on along its velocity
        if (!moved_) moved_ = std::make_unique<ex::Car>();
        *moved_ = *c;
        ex::Car& m = *moved_;
        const F age = F(float(std::clamp(t - double(snap_.race_time), 0.0, kMaxExtrapolation)));
        m.pos = {m.pos.x + (m.vel.x * age), m.pos.y + (m.vel.y * age), m.pos.z + (m.vel.z * age)};
        m.heading = m.heading + (m.yaw_rate * age);
        ex::body_axes(m);
        ex::hub_offsets(m);
        move_hint(m, *shadow_->course().collision);
        c = moved_.get();
    }
    Pose p;
    exact_state(*c, *setup_, p.state, &shadow_->inputs().later.moods);
    p.body_x = c->pos.x.host(), p.body_z = -c->pos.y.host();
    p.heading = c->heading.host(), p.yaw_rate = c->yaw_rate.host();
    p.brake = c->brake_pedal.host();
    p.car = c;
    return p;
}

void RemoteCar::receive(const net::CarSnapshot& s, double local_race_time) {
    if (have_ && s.tick <= newest_.tick) return; // late or duplicate
    const double t = local_race_time == kNoTime ? double(s.race_time) : local_race_time;
    newest_ = s;
    // A state from ahead of our race clock (our machine stalled and steps to catch up, or its clock runs a step early) waits
    // until our race reaches its time: the car is posed where it was at our race time, not where it is at the wall clock.
    if (have_ && shadow_ && double(s.race_time) > t + 0.5 / hz_) {
        pending_.push_back(s);
        if (pending_.size() > kPending) pending_.pop_front();
        return;
    }
    pending_.clear(); // (older than this one)
    apply(s, t);
}

void RemoteCar::apply(const net::CarSnapshot& s, double t) {
    if (!have_) {
        snap_ = s;
        have_ = true;
        off_pos_ = {};
        off_rot_ = {};
        corr_time_ = t;
        if (shadow_) sync(s);
        return;
    }
    Pose shown = predict_from_applied(t);
    // How good was the prediction: the shadow's position at this state's time against the state.
    if (physics()) {
        const double half = 0.5 / hz_;
        for (size_t k = 0; k < std::min(nhistory_, history_.size()); ++k)
            if (std::fabs(history_[k].first - double(s.race_time)) < half) {
                last_error_ = (history_[k].second - s.state.position).length();
                last_horizon_ = double(s.race_time) - double(snap_.race_time);
                break;
            }
    }
    snap_ = s;
    Pose fresh;
    if (shadow_) {
        sync(s);
        if (physics_) {
            const int catch_up = int(std::lround(std::clamp(t - double(s.race_time), 0.0, kMaxExtrapolation) * hz_));
            for (int i = 0; i < catch_up && physics_; ++i) step_shadow();
        }
        fresh = shadow_pose(t);
    } else {
        fresh = raw(t);
    }
    off_pos_ = shown.state.position - fresh.state.position;
    off_rot_ = normalize(quat_mul(shown.state.orientation, conj(fresh.state.orientation)));
    if (off_pos_.length() > kSnapDistance) off_pos_ = {}, off_rot_ = {};
    corr_time_ = t;
}

RemoteCar::Pose RemoteCar::predict(double t) {
    // The newest waiting state our race has reached (its time at most the coming step's start: the shadow then steps from
    // it, so the pose is always a step of its own, with its ground and road segment).
    const double reach = t - 0.5 / hz_;
    if (!pending_.empty() && double(pending_.front().race_time) <= reach) {
        while (pending_.size() > 1 && double(pending_[1].race_time) <= reach) pending_.pop_front();
        const net::CarSnapshot s = pending_.front();
        pending_.pop_front();
        apply(s, t - 1.0 / hz_);
    }
    return predict_from_applied(t);
}

RemoteCar::Pose RemoteCar::predict_from_applied(double t) {
    Pose p;
    if (shadow_ && have_) {
        // One step per call in play (t = the end of the coming step); held beyond the horizon.
        while (physics_ && sim_time_ + 0.5 / hz_ < t && sim_time_ - double(snap_.race_time) < kMaxExtrapolation) step_shadow();
        p = shadow_pose(t);
    } else {
        p = raw(t);
    }
    if (!have_) return p;
    float k = float(std::exp(-std::max(0.0, t - corr_time_) / kBlend));
    if (k < 1e-3f) return p;
    sim::Vec3 o = off_pos_ * k;
    p.state.position += o;
    for (sim::WheelState& w : p.state.wheels) w.contact += o;
    p.state.orientation = normalize(quat_mul(slerp(sim::Quat{}, off_rot_, k), p.state.orientation));
    return p;
}

// --- Plausibility ---

std::optional<Plausibility::Flag> Plausibility::fire(net::FlagKind k, float v) {
    unsigned bit = 1u << unsigned(k);
    if (fired_ & bit) return std::nullopt;
    fired_ |= bit;
    return Flag{k, v};
}

std::optional<Plausibility::Flag> Plausibility::on_state(const net::CarSnapshot& s, bool contact) {
    std::optional<Flag> out;
    auto horiz = [](const sim::Vec3& v) { return std::hypot(v.x, v.z); };
    float v = horiz(s.state.velocity);
    if (have_ && s.tick > prev_.tick) {
        double dt = double(s.tick - prev_.tick) / sim::kDefaultTickHz;
        // Faster than its gearing allows, for half a second (a shove can spike it briefly).
        if (v > top_ * kSpeedMargin + kSpeedSlack) over_ += dt;
        else over_ = 0;
        if (over_ > kSpeedTime) out = fire(net::FlagKind::Speed, v * 3.6f);
        // A jump its own motion does not explain (a respawn says so).
        if (!out && !s.respawned && !prev_.respawned && dt <= 0.5) {
            sim::Vec3 expect = (prev_.state.velocity + s.state.velocity) * float(0.5 * dt);
            sim::Vec3 moved = s.state.position - prev_.state.position;
            float jump = horiz(moved - expect);
            if (jump > kTeleport) out = fire(net::FlagKind::Teleport, jump);
        }
        // Speed gained over the last half second, in both its quarters: engines ramp, while a shove
        // from another car is one step (walls and brakes only take speed away).
        speeds_[nspeeds_++ % speeds_.size()] = {s.race_time, v};
        if (!out && !contact && !s.respawned) {
            auto speed_at = [&](double ago, float& vout) {
                for (size_t k = 0; k < std::min(nspeeds_, speeds_.size()); ++k) {
                    double span = s.race_time - speeds_[k].first;
                    if (std::fabs(span - ago) <= 0.03) {
                        vout = speeds_[k].second;
                        return true;
                    }
                }
                return false;
            };
            float v25 = 0, v50 = 0;
            if (speed_at(0.25, v25) && speed_at(0.5, v50)) {
                float g1 = float((v - v25) / 0.25 / 9.81), g2 = float((v25 - v50) / 0.25 / 9.81);
                if (std::min(g1, g2) > kAccelG) out = fire(net::FlagKind::Accel, 0.5f * (g1 + g2));
            }
        }
    } else if (!have_) {
        speeds_[nspeeds_++ % speeds_.size()] = {s.race_time, v};
    }
    prev_ = s;
    have_ = true;
    return out;
}

std::optional<Plausibility::Flag> Plausibility::on_prediction_error(float m, double horizon, bool contact) {
    if (contact || !(horizon > 0)) {
        motion_ = 0;
        return std::nullopt;
    }
    if (m > kMotionError) motion_ += horizon;
    else motion_ = std::max(0.0, motion_ - horizon);
    if (motion_ > kMotionTime) return fire(net::FlagKind::Motion, m);
    return std::nullopt;
}

std::optional<Plausibility::Flag> Plausibility::on_finish(double claimed, double observed) {
    double off = std::fabs(claimed - observed);
    if (off > kFinishSlack) return fire(net::FlagKind::FinishTime, float(off));
    return std::nullopt;
}

} // namespace gt3r::race
