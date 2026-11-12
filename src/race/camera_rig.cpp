#include "race/camera_rig.h"

#include "race/interp.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace gt3r::race {

namespace {

constexpr float kPi = 3.14159265f;

render::Vec3 rv(const sim::Vec3& v) { return {v.x, v.y, v.z}; }

float heading_yaw(const sim::Quat& q) {
    sim::Vec3 f = rotate(q, {0, 0, -1});
    return std::atan2(-f.x, -f.z);
}

float wrap(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

float stick(const PadState& pad, PadAxis a) {
    float v = pad.axis_f(a);
    return std::fabs(v) < 0.15f ? 0.0f : v;
}

} // namespace

const char* camera_mode_name(CameraMode m) {
    switch (m) {
    case CameraMode::Chase: return "chase";
    case CameraMode::ChaseFar: return "chase (far)";
    case CameraMode::Bumper: return "bumper";
    case CameraMode::Roof: return "roof";
    case CameraMode::Orbit: return "free orbit";
    case CameraMode::Tv: return "replay (TV)";
    case CameraMode::Custom: return "custom";
    default: return "?";
    }
}

float CameraRig::shake_random() {
    // s <- 17 s + 17; out = s ^ rotl(s, 16); 23 mantissa bits -> [1, 2) - 1. Every draw first
    // advances once and discards the result.
    auto next = [this] {
        shake_rng_ = shake_rng_ * 17u + 17u;
        return shake_rng_ ^ ((shake_rng_ << 16) | (shake_rng_ >> 16));
    };
    next();
    u32 bits = (next() & 0x7FFFFFu) | 0x3F800000u;
    float f;
    std::memcpy(&f, &bits, 4);
    return f - 1.0f;
}

void CameraRig::next_mode() {
    if (mode_ == CameraMode::Bumper && mirror() && !gt3_views_) {
        mirror_ = false;
        return;
    }
    mirror_ = true;
    // The custom cameras after the last built-in view, then back to the bumper.
    auto custom_from = [this](size_t i) {
        if (i < customs_.size()) {
            custom_id_ = customs_[i].id;
            set_mode(CameraMode::Custom);
        } else {
            set_mode(CameraMode::Bumper);
        }
    };
    if (mode_ == CameraMode::Custom) {
        size_t i = 0;
        while (i < customs_.size() && customs_[i].id != custom_id_) ++i;
        custom_from(i < customs_.size() ? i + 1 : customs_.size());
    } else if (mode_ == CameraMode::Bumper) {
        set_mode(CameraMode::Chase);
    } else if (mode_ == CameraMode::Chase && !gt3_views_) {
        set_mode(CameraMode::ChaseFar);
    } else if (mode_ == CameraMode::Chase || mode_ == CameraMode::ChaseFar) {
        custom_from(0);
    } else {
        set_mode(CameraMode::Chase);
    }
}

void CameraRig::set_view(CameraMode m, bool mirror, int custom_id) {
    if (m == CameraMode::Custom) {
        if (!find_custom_camera(customs_, custom_id)) m = CameraMode::Bumper, mirror = true;
        else custom_id_ = custom_id;
    }
    set_mode(m);
    mirror_ = mirror;
}

std::string CameraRig::view_name() const {
    if (const CustomCamera* c = custom()) return c->name;
    return camera_mode_name(mode_);
}

void CameraRig::set_mode(CameraMode m) {
    // The custom view without a camera of its id: the first one.
    if (m == CameraMode::Custom && !find_custom_camera(customs_, custom_id_) && !customs_.empty())
        custom_id_ = customs_.front().id;
    mode_ = m;
    snapped_ = false;
}

namespace {
float pitch_of(const sim::Quat& q) {
    sim::Vec3 f = rotate(q, {0, 0, -1});
    return std::asin(std::clamp(float(f.y), -1.0f, 1.0f));
}
} // namespace

void CameraRig::snap(const sim::VehicleState& s) {
    chase_yaw_ = heading_yaw(s.orientation);
    lag_yaw_ = chase_yaw_, lag_yaw_w_ = lag_yaw_acc_ = 0;
    lag_pitch_ = pitch_of(s.orientation), lag_pitch_w_ = 0;
    lag_time_ = 0;
    chase_height_ = s.position.y;
    snapped_ = true;
    last_cut_.reset(); // (the eye jumps)
}

void CameraRig::update(const sim::VehicleState& s, const sim::VehicleSetup& setup, const PadState& pad, double dt) {
    place(s, setup, pad, dt);
    // The listener's motion for the sounds' Doppler: the eye's displacement, restarted at a cut.
    const std::pair<Shot, int> cut{shot(), director_ ? director_->cut_serial() : -1};
    if (dt > 0) eye_velocity_ = last_cut_ == cut ? (eye_ - last_eye_) * float(1.0 / dt) : render::Vec3{};
    last_eye_ = eye_, last_cut_ = cut;
}

CameraSound CameraRig::sound_kind() const {
    if (intro_) return CameraSound::Other; // (GT3's starting camera)
    switch (mode_) {
    case CameraMode::Bumper:
    case CameraMode::Roof: return CameraSound::Cockpit;
    case CameraMode::Chase:
    case CameraMode::ChaseFar: return CameraSound::Chase;
    case CameraMode::Custom: // fixed to the car (body, bonnet): a car mount; trailing it: a chase
        if (const CustomCamera* c = custom()) return c->mount == CustomCamera::Mount::Follow ? CameraSound::Chase : CameraSound::Cockpit;
        return CameraSound::Chase;
    case CameraMode::Tv: return replay_car_mount_ ? CameraSound::Cockpit : CameraSound::Other;
    default: return CameraSound::Other;
    }
}

void CameraRig::place(const sim::VehicleState& s, const sim::VehicleSetup& setup, const PadState& pad, double dt) {
    float fdt = float(std::min(dt, 0.1));
    if (!snapped_) snap(s);
    // The replay aim is for this update only.
    const std::optional<render::Vec3> replay_aim = std::exchange(replay_aim_, std::nullopt);
    const std::optional<ReplayDirector::Pair> replay_pair = std::exchange(replay_pair_, std::nullopt);
    const std::optional<sim::VehicleState> replay_partner = std::exchange(replay_partner_, std::nullopt);
    const std::vector<CarMount>* replay_mounts = std::exchange(replay_mounts_, nullptr);
    render::Vec3 pos = rv(s.position);
    sim::Vec3 fwd_s = rotate(s.orientation, {0, 0, -1}), up_s = rotate(s.orientation, {0, 1, 0});
    render::Vec3 fwd = rv(fwd_s), up = rv(up_s);

    // Chase smoothing runs in every mode so switching back is seamless.
    float yaw = heading_yaw(s.orientation);
    float speed = s.speed_kmh / 3.6f;
    // When reversing slowly, keep looking forwards along the car.
    float k = 3.5f + std::min(speed, 40.0f) * 0.05f;
    chase_yaw_ = wrap(chase_yaw_ + wrap(yaw - chase_yaw_) * (1 - std::exp(-k * fdt)));
    chase_height_ += (s.position.y - chase_height_) * (1 - std::exp(-6.0f * fdt));
    // GT3's chase lags, stepped at 60 Hz.
    lag_time_ += dt;
    for (int n = 0; lag_time_ >= 1.0 / 60.0 && n < 12; ++n) {
        lag_time_ -= 1.0 / 60.0;
        const float h = 1.0f / 60.0f;
        float acc = 30.0f * wrap(yaw - lag_yaw_) - 10.0f * lag_yaw_w_;
        lag_yaw_w_ += 0.5f * (acc + lag_yaw_acc_) * h;
        lag_yaw_acc_ = acc;
        lag_yaw_ = wrap(lag_yaw_ + lag_yaw_w_ * h);
        float e = wrap(pitch_of(s.orientation) - lag_pitch_);
        lag_pitch_w_ += (20.0f * e - 10.0f * lag_pitch_w_) * h;
        lag_pitch_ = wrap(lag_pitch_ + lag_pitch_w_ * h);
    }
    if (lag_time_ > 1.0 / 60.0) lag_time_ = 0;
    // Ours: the look around (set_look), eased to the stick's direction and back to the front.
    // The view always turns the short way to where the stick points: rolling the stick round
    // turns the view round with it, any number of times either way, and straight back with the
    // stick wobbling either side keeps to one side. Let go, it comes back to the front the short
    // way (look_ is kept within half a turn).
    {
        float aim = 0;
        const float m = std::min(std::hypot(look_x_, look_y_), 1.0f);
        if (m > kLookStart && !intro_ && mode_ != CameraMode::Orbit && mode_ != CameraMode::Tv) {
            const float t = std::clamp((m - kLookStart) / (kLookFull - kLookStart), 0.0f, 1.0f);
            aim = std::atan2(look_x_, -look_y_) * t * t * (3 - 2 * t);
        }
        const float target = look_ + wrap(aim - look_);
        look_ = wrap(look_ + (target - look_) * (1 - std::exp(-kLookRate * fdt)));
        if (aim == 0 && std::fabs(look_) < 1e-4f) look_ = 0; // back in front: the view exactly as without
    }
    // GT3's in-car vibration (on-board camera, view point 0 only), once per 60 Hz camera update.
    // Amplitude A = sum over the wheels of |slip ratio| 0.3 + |slip angle| / (pi/2), at most 3;
    // speed factor S = speed clamped to 10..120 km/h over 120 km/h. On about half the updates
    // each axis is kicked by (r - 0.5) A 250 through the shaker lag (v' = (du - 12 v)/60, x' = v/60);
    // the view turns by 8 S x degrees of pitch and 2 S x of yaw (x clamped to +-1), about the eye.
    // The original also draws for two replay onboard shakers first: their draws are kept so the
    // random sequence matches.
    bool shaking = shake_on_ && mode_ == CameraMode::Bumper && !intro_;
    if (shaking) {
        shake_time_ += dt;
        for (int n = 0; shake_time_ >= 1.0 / 60.0 && n < 12; ++n) {
            shake_time_ -= 1.0 / 60.0;
            float amp = 0;
            for (const sim::WheelState& w : s.wheels)
                amp += std::fabs(w.slip_ratio) * 0.3f + std::fabs(w.slip_angle) / (kPi * 0.5f);
            amp = std::clamp(amp, 0.0f, 3.0f);
            for (int i = 0; i < 2; ++i) // the replay onboard shakers' draws
                if (shake_random() < 0.4f) shake_random();
            shake_speed_ = std::clamp(s.speed_kmh / 3.6f, 2.7777778f, 33.333333f) / 33.333333f;
            float u_yaw = 0, u_pitch = 0;
            if (shake_random() < 0.5f) {
                u_yaw = (shake_random() - 0.5f) * amp * 250.0f;
                u_pitch = (shake_random() - 0.5f) * amp * 250.0f;
            }
            shake_yaw_.step(u_yaw);
            shake_pitch_.step(u_pitch);
        }
        if (shake_time_ > 1.0 / 60.0) shake_time_ = 0;
    } else {
        shake_yaw_ = shake_pitch_ = {};
        shake_time_ = 0;
    }
    // GT3 player views: eye at -p of the car's camera point in a frame at the body origin (ride
    // height above the sim's ground-level origin), looking along that frame's forward axis; zoom
    // sqrt(3): 60 degrees horizontal over the 640x448 frame, 48 degrees vertical.
    auto gt3_view = [&](int index) {
        float q = setup.wheels[0].ride_height > 0 ? setup.wheels[0].ride_height : 0.13f;
        render::Vec3 origin = rv(s.position + rotate(s.orientation, {0, q, 0}));
        render::Vec3 f, u;
        if (index == 1) { // chase: lagged yaw and pitch, no roll
            // Between 60 Hz steps (frame rates above 60) the lags carry on at their own rates:
            // held still, the view would turn on every other frame only (judder in fast turns).
            float yaw_now = lag_yaw_ + lag_yaw_w_ * float(lag_time_), pitch_now = lag_pitch_ + lag_pitch_w_ * float(lag_time_);
            // The look around turns the heading (round the car) and takes the pitch along with it.
            const float look = look_back_ ? 0.0f : look_;
            float th = yaw_now + (look_back_ ? kPi : 0.0f) - look, ph = (look_back_ ? -pitch_now : pitch_now) * std::cos(look);
            f = {-std::sin(th) * std::cos(ph), std::sin(ph), -std::cos(th) * std::cos(ph)};
            render::Vec3 r = render::normalize(render::cross(f, {0, 1, 0}));
            u = render::cross(r, f);
        } else { // in-car: the body's attitude
            f = fwd, u = up;
            if (look_back_) f = render::Vec3{0, 0, 0} - f;
        }
        render::Vec3 r = render::normalize(render::cross(f, u));
        render::Vec3 e = point_[index] * -1.0f; // model frame: x right, y up, z back
        if (look_back_ && index == 0) e.z = -e.z;
        eye_ = origin + r * e.x + u * e.y - f * e.z;
        // In-car, the look around turns the view about the car's up axis, the eye where it is.
        if (index == 0 && !look_back_ && look_ != 0) f = f * std::cos(look_) + r * std::sin(look_);
        view_ = render::look_at(eye_, eye_ + f, u);
        fovy_ = ReplayDirector::zoom_fovy(1.7320508f);
    };

    if (intro_ && mode_ != CameraMode::Orbit && mode_ != CameraMode::Tv) {
        // start.cam (GTRC record 0, kind 0x420, curves in the car's frame with z forward): three
        // cuts, t 0..0.8 from 30 m ahead to 6 m ahead (2.5 m up), 0.8..0.9 a sweep past the car's
        // side towards the rear, 0.9..1 round the front corner, aimed near the car's centre;
        // zoom 2.41 (45 degrees horizontal over 640x448).
        float t = std::clamp(intro_t_, 0.0f, 1.0f);
        sim::Vec3 e, tg;
        float zoom = 2.41f;
        if (start_shot_) {
            const GtrcRecord& r = *start_shot_;
            e = {r.pos[0].eval(t), r.pos[1].eval(t), -r.pos[2].eval(t)}; // our car frame: z back
            tg = {r.target[0].eval(t), r.target[1].eval(t), -r.target[2].eval(t)};
            if (!r.zoom.empty()) zoom = std::max(0.2f, r.zoom.eval(t));
        } else {
            sim::Vec3 a{3.04f, 2.04f, -30.44f}, b{-1.64f, 0.59f, -3.97f};
            e = a + (b - a) * t, tg = {0.65f, 0.4f, 0};
        }
        eye_ = rv(s.position + rotate(s.orientation, e));
        render::Vec3 target = rv(s.position + rotate(s.orientation, tg));
        view_ = render::look_at(eye_, target, {0, 1, 0});
        fovy_ = ReplayDirector::zoom_fovy(zoom);
        focus_ = render::length(target - eye_);
        return;
    }
    focus_ = 100.0f; // the player's views (GT3's on-board camera)
    switch (mode_) {
    case CameraMode::Chase: gt3_view(1); break;
    case CameraMode::Bumper: {
        gt3_view(0);
        if (shaking) {
            constexpr float kDeg = kPi / 180.0f;
            // Held between 60 Hz steps, the view would jump on every Nth frame only.
            float pitch = 8.0f * shake_speed_ * std::clamp(shake_pitch_.at(shake_time_), -1.0f, 1.0f) * kDeg;
            float dyaw = 2.0f * shake_speed_ * std::clamp(shake_yaw_.at(shake_time_), -1.0f, 1.0f) * kDeg;
            view_ = render::rotate_x(pitch) * render::rotate_y(dyaw) * view_; // in eye space
        }
        break;
    }
    case CameraMode::ChaseFar: {
        bool far = mode_ == CameraMode::ChaseFar;
        float dist = (far ? 7.5f : 4.6f) + setup.length * 0.5f;
        float height = far ? 2.4f : 1.55f;
        // Look back and the look around turn it round the car.
        const float turn = chase_yaw_ + (look_back_ ? kPi : -look_);
        render::Vec3 back{std::sin(turn), 0, std::cos(turn)};
        render::Vec3 base{pos.x, chase_height_, pos.z};
        eye_ = base + back * dist + render::Vec3{0, height, 0};
        render::Vec3 target = base + render::Vec3{0, far ? 1.0f : 0.85f, 0} - back * 2.5f;
        view_ = render::look_at(eye_, target, {0, 1, 0});
        fovy_ = far ? 0.72f : 0.78f;
        break;
    }
    case CameraMode::Roof: {
        bool roof = true;
        sim::Vec3 local = roof ? sim::Vec3{0, setup.height + 0.2f, 0.2f} : sim::Vec3{0, 0.62f, -setup.length * 0.5f + 0.15f};
        eye_ = rv(s.position + rotate(s.orientation, local));
        // Look back and the look around turn it about the car's up axis, as in-car.
        const float turn = look_back_ ? kPi : look_;
        render::Vec3 f = fwd;
        if (turn != 0) f = fwd * std::cos(turn) + render::normalize(render::cross(fwd, up)) * std::sin(turn);
        render::Vec3 target = eye_ + f * 10.0f - up * (roof ? 0.9f : 0.25f);
        view_ = render::look_at(eye_, target, up);
        fovy_ = roof ? 0.85f : 0.9f;
        break;
    }
    case CameraMode::Custom: {
        const CustomCamera* c = custom();
        if (!c) { // (none left: the chase)
            gt3_view(1);
            break;
        }
        // The mirror looks back from the in-car view, whatever the camera.
        gt3_view(0);
        mirror_eye_ = eye_, mirror_view_ = view_, mirror_fovy_ = fovy_;
        // The mount's frame at the body origin: the body's attitude, or the chase's lagged heading
        // and pitch with no roll (as gt3_view(1), between its 60 Hz steps too).
        const float q = setup.wheels[0].ride_height > 0 ? setup.wheels[0].ride_height : 0.13f;
        const render::Vec3 origin = rv(s.position + rotate(s.orientation, {0, q, 0}));
        render::Vec3 f = fwd, u = up;
        if (c->mount == CustomCamera::Mount::Follow) {
            const float th = lag_yaw_ + lag_yaw_w_ * float(lag_time_), ph = lag_pitch_ + lag_pitch_w_ * float(lag_time_);
            f = {-std::sin(th) * std::cos(ph), std::sin(ph), -std::cos(th) * std::cos(ph)};
            u = render::cross(render::normalize(render::cross(f, {0, 1, 0})), f);
        }
        const render::Vec3 r = render::normalize(render::cross(f, u));
        // Look back and the look around turn the camera round the car's up axis through the
        // driver's eye: a chase-like camera goes round the car, a hood camera looks behind (its
        // car is left out then: CameraRig::hides_car).
        const render::Mat4 turn = render::rotate_y(-(look_back_ ? kPi : look_));
        render::Vec3 d, du;
        c->axes(d, du);
        if (c->mount == CustomCamera::Mount::Hood) { // the bonnet's tilt onto its edge
            const float tilt = hood_camera_pitch(hood_profile_, *c);
            if (tilt != 0) {
                const render::Vec3 rd = render::normalize(render::cross(d, du));
                d = d * std::cos(tilt) - du * std::sin(tilt);
                du = render::cross(rd, d);
            }
        }
        // The eye in the mount's frame: the offset, or the bonnet mount the car's shape gives.
        const render::Vec3 e = driver_eye() + turn.transform_point(
            c->mount == CustomCamera::Mount::Hood ? hood_camera_eye(hood_profile_, setup, *c) - driver_eye()
                                                : c->offset);
        d = turn.transform_point(d), du = turn.transform_point(du);
        // Would the body block the view? The eye over the body's footprint and near it, body
        // behind it, looking back across it (the body's box from the hood profile, else the setup).
        {
            render::Vec3 lo, hi;
            if (hood_profile_.valid) {
                lo = hood_profile_.bounds_min, hi = hood_profile_.bounds_max;
            } else {
                const float hw = setup.width > 0 ? setup.width * 0.5f : 0.9f, hl = setup.length > 0 ? setup.length * 0.5f : 2.2f;
                lo = {-hw, -q, -hl}, hi = {hw, (setup.height > 0 ? setup.height : 1.3f) - q, hl};
            }
            const bool over = e.x > lo.x - 0.1f && e.x < hi.x + 0.1f && e.z > lo.z - 0.1f && e.z < hi.z + 0.1f &&
                              e.y > lo.y - 0.5f && e.y < hi.y + kNearBody;
            body_blocks_ = over && e.z < hi.z - 0.3f && d.z > kLookBack; // (local z is back)
        }
        auto world = [&](const render::Vec3& v) { return r * v.x + u * v.y - f * v.z; }; // (local z is back)
        eye_ = origin + world(e);
        // Body roll (the car mounts): the eye stays on the car while the view rolls with that
        // share of the body's roll and levels the rest out (1: the in-car look, 0: the horizon).
        const float lock = c->mount == CustomCamera::Mount::Follow ? 0.0f : 1.0f - c->body_roll;
        if (lock > 0 && std::fabs(f.y) < 0.999f) {
            const render::Vec3 w{0, 1, 0};
            const render::Vec3 u0 = render::normalize(w - f * render::dot(w, f));
            const render::Vec3 axis = render::cross(f, u0);
            const float roll = std::atan2(render::dot(render::cross(u0, u), f), render::dot(u0, u));
            u = render::normalize(u0 * std::cos(roll * (1.0f - lock)) + axis * std::sin(roll * (1.0f - lock)));
        }
        const render::Vec3 rv2 = render::normalize(render::cross(f, u));
        mount_frame_ = render::Mat4::from({rv2.x, rv2.y, rv2.z, 0, u.x, u.y, u.z, 0, -f.x, -f.y, -f.z, 0, 0, 0, 0, 1});
        auto vworld = [&](const render::Vec3& v) { return rv2 * v.x + u * v.y - f * v.z; };
        view_ = render::look_at(eye_, eye_ + vworld(d), vworld(du));
        fovy_ = c->fovy();
        break;
    }
    case CameraMode::Orbit: {
        float ox = stick(pad, PadAxis::RX), oy = stick(pad, PadAxis::RY);
        orbit_yaw_ += ox * 2.0f * fdt;
        orbit_pitch_ = std::clamp(orbit_pitch_ + oy * 1.5f * fdt, -0.2f, 1.45f);
        float z = (pad.down(PadButton::R2) ? 1.0f : 0.0f) - (pad.down(PadButton::L2) ? 1.0f : 0.0f);
        orbit_dist_ = std::clamp(orbit_dist_ * std::exp(z * 1.5f * fdt), 2.0f, 400.0f);
        render::Vec3 target = pos + render::Vec3{0, 0.8f, 0};
        float cp = std::cos(orbit_pitch_);
        eye_ = target + render::Vec3{std::sin(orbit_yaw_) * cp, std::sin(orbit_pitch_), std::cos(orbit_yaw_) * cp} * orbit_dist_;
        view_ = render::look_at(eye_, target, {0, 1, 0});
        fovy_ = 0.8f;
        break;
    }
    case CameraMode::Tv: {
        const render::Vec3 aim = replay_aim ? *replay_aim : pos;
        render::Vec3 target = aim + render::Vec3{0, 0.7f, 0};
        director_record_ = -1;
        director_haze_ = false;
        replay_on_board_ = replay_car_mount_ = false;
        if (director_) {
            ReplayDirector::Shot shot =
                director_->update(track_s_, lap_, aim, dt, 16.0f / 9.0f, replay_pair ? &*replay_pair : nullptr);
            director_record_ = shot.record;
            director_haze_ = shot.valid && shot.haze;
            if (shot.valid && shot.onboard) { // mode 2's on-board cut: a bumper view on the partner's car
                const bool other = shot.on_partner && replay_partner;
                const sim::VehicleState& o = other ? *replay_partner : s;
                const float length = other ? replay_partner_length_ : setup.length;
                const render::Vec3 of = rv(rotate(o.orientation, {0, 0, -1})), ou = rv(rotate(o.orientation, {0, 1, 0}));
                eye_ = rv(o.position + rotate(o.orientation, {0, 0.62f, -length * 0.5f + 0.15f}));
                view_ = render::look_at(eye_, eye_ + of * 10.0f - ou * 0.25f, ou);
                fovy_ = 0.9f;
                focus_ = 100.0f;
                replay_car_mount_ = true;
                break;
            }
            // View mode 1's procedural cameras (presentation §G15) on the followed car, planned
            // at the cut, moving at 60 Hz.
            if (shot.valid && shot.procedural >= 0) {
                const CameraCar cc = camera_car(s, setup);
                if (director_->cut_serial() != proc_serial_) {
                    proc_serial_ = director_->cut_serial();
                    static const std::vector<CarMount> kNoMounts;
                    proc_.start(director_->procedural_shot(), cc, ground_, replay_mounts ? *replay_mounts : kNoMounts);
                    proc_acc_ = 0;
                }
                proc_acc_ += dt;
                for (int n = 0; proc_acc_ >= 1.0 / 60.0 && n < 12; ++n) {
                    proc_acc_ -= 1.0 / 60.0;
                    proc_.step(cc);
                }
                if (proc_acc_ > 1.0 / 60.0) proc_acc_ = 0;
                view_ = proc_.view(cc, ground_, float(proc_acc_ * 60.0));
                eye_ = proc_.eye();
                focus_ = proc_.focus();
                replay_on_board_ = proc_.on_board();
                replay_car_mount_ = proc_.kind() == ProceduralCamera::Mount;
                fovy_ = std::clamp(ReplayDirector::zoom_fovy(proc_.zoom()), 0.02f, 2.4f);
                break;
            }
            if (shot.valid) { // a course shot; mode 1's roll about the view axis (Rz in eye space)
                eye_ = shot.eye;
                view_ = render::rotate_z(shot.roll) * render::look_at(eye_, shot.target, {0, 1, 0});
                focus_ = render::length(shot.target - eye_);
                fovy_ = std::clamp(shot.fovy, 0.02f, 2.4f);
                break;
            }
        }
        if (tv_.empty()) { // no trackside cameras: a high, wide chase
            eye_ = pos + render::Vec3{std::sin(chase_yaw_) * 14.0f, 6.0f, std::cos(chase_yaw_) * 14.0f};
            view_ = render::look_at(eye_, target, {0, 1, 0});
            fovy_ = 0.7f;
            focus_ = render::length(target - eye_);
            break;
        }
        // The camera whose point is closest to slightly ahead of the car (cuts as the car passes).
        int best = 0;
        float bd = 1e30f;
        for (size_t i = 0; i < tv_.size(); ++i) {
            float d = tv_[i].s - (track_s_ + 20.0f);
            if (tv_length_ > 0) d = std::remainder(d, tv_length_);
            if (std::fabs(d) < bd) bd = std::fabs(d), best = int(i);
        }
        tv_active_ = best;
        eye_ = tv_[size_t(best)].position;
        view_ = render::look_at(eye_, target, {0, 1, 0});
        focus_ = render::length(target - eye_);
        // Zoom so the car keeps a similar size on screen.
        float dist = render::length(target - eye_);
        fovy_ = std::clamp(2.0f * std::atan(6.0f / std::max(dist, 1.0f)) / tv_[size_t(best)].zoom, 0.08f, 0.9f);
        break;
    }
    default: break;
    }
}

bool CameraRig::handle_event(const SDL_Event& e) {
    if (mode_ != CameraMode::Orbit) return false;
    switch (e.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: dragging_ = true; return true;
    case SDL_EVENT_MOUSE_BUTTON_UP: dragging_ = false; return true;
    case SDL_EVENT_MOUSE_MOTION:
        if (!dragging_) return false;
        orbit_yaw_ -= e.motion.xrel * 0.01f;
        orbit_pitch_ = std::clamp(orbit_pitch_ + e.motion.yrel * 0.01f, -0.2f, 1.45f);
        return true;
    case SDL_EVENT_MOUSE_WHEEL: orbit_dist_ = std::clamp(orbit_dist_ * std::exp(-e.wheel.y * 0.1f), 2.0f, 400.0f); return true;
    default: return false;
    }
}

void CameraRig::set_replay_aim(const render::Vec3& aim, const ReplayDirector::Pair* pair, const sim::VehicleState* partner,
                               float partner_length) {
    replay_aim_ = aim;
    replay_pair_.reset();
    if (pair) replay_pair_ = *pair;
    replay_partner_.reset();
    if (partner) replay_partner_ = *partner;
    replay_partner_length_ = partner_length;
}

CustomCamera CameraRig::preset(CameraMode m, const sim::VehicleSetup& setup) const {
    constexpr float kDeg = 180.0f / kPi;
    CustomCamera c;
    const render::Vec3 eye0 = driver_eye();
    const float q = setup.wheels[0].ride_height > 0 ? setup.wheels[0].ride_height : 0.13f;
    switch (m) {
    case CameraMode::Bumper: // the driver's eye, the car's attitude, no car drawn, the in-car HUD and mirror
        c.mount = CustomCamera::Mount::Body;
        c.offset = {};
        c.car = CustomCamera::OwnCar::Hidden, c.incar_hud = true, c.mirror = true;
        c.fov = ReplayDirector::zoom_fovy(1.7320508f) * kDeg;
        break;
    case CameraMode::Roof: { // over the roof (from the sim's origin, below the body origin), 0.9 m down at 10 m
        c.mount = CustomCamera::Mount::Body;
        c.offset = render::Vec3{0, setup.height + 0.2f - q, 0.2f} - eye0;
        c.pitch = -std::atan(0.9f / 10.0f) * kDeg;
        c.fov = 0.85f * kDeg;
        break;
    }
    case CameraMode::ChaseFar: { // 7.5 m behind the car's rear, 2.4 m up, aimed 2.5 m ahead 1 m up
        c.mount = CustomCamera::Mount::Follow;
        const float dist = 7.5f + setup.length * 0.5f;
        c.offset = render::Vec3{0, 2.4f - q, dist} - eye0;
        c.pitch = -std::atan2(2.4f - 1.0f, dist + 2.5f) * kDeg;
        c.fov = 0.72f * kDeg;
        break;
    }
    default: // the chase: the car's camera point 1, level
        c.mount = CustomCamera::Mount::Follow;
        c.offset = point_[1] * -1.0f - eye0;
        c.fov = ReplayDirector::zoom_fovy(1.7320508f) * kDeg;
        break;
    }
    c.clamp();
    return c;
}

render::Mat4 CameraRig::custom_seat(bool level, const render::Vec3& nudge) const {
    const CustomCamera* c = custom();
    const float turn = (c ? c->yaw * kPi / 180.0f : 0.0f) + (look_back_ ? kPi : look_);
    render::Mat4 frame = mount_frame_;
    if (level || !car_mounted()) // its heading only (the frame's z axis is back)
        frame = render::rotate_y(std::atan2(frame.at(0, 2), frame.at(2, 2)));
    return render::translate(eye_) * frame * render::translate(nudge) * render::rotate_y(-turn);
}

CameraCar CameraRig::camera_car(const sim::VehicleState& s, const sim::VehicleSetup& setup) {
    CameraCar c;
    const float q = setup.wheels[0].ride_height > 0 ? setup.wheels[0].ride_height : 0.13f;
    c.pos = rv(s.position + rotate(s.orientation, {0, q, 0}));
    c.velocity = rv(s.velocity);
    c.yaw = heading_yaw(s.orientation);
    c.pitch = pitch_of(s.orientation);
    c.front = c.rear = setup.length * 0.5f;
    c.half_width = setup.width * 0.5f;
    const sim::Quat& o = s.orientation;
    c.body = render::translate(c.pos) * render::rotate_quat(o.w, o.x, o.y, o.z);
    return c;
}

render::Mat4 CameraRig::projection(float aspect) const {
    return render::perspective(fovy_, aspect * kPixelAspect, near_plane, far_plane);
}

} // namespace gt3r::race
