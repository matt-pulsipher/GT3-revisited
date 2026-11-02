#include "race/procedural_cameras.h"

#include "formats/model/car_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt3r::race {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDeg = kPi / 180.0f;
constexpr float kToDeg = 57.29578f;

float rdf(ByteSpan d, size_t o) {
    u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

} // namespace

std::vector<CarMount> car_camera_mounts(ByteSpan car_file) {
    std::vector<CarMount> out;
    try {
        CarFile f = CarFile::parse(car_file);
        ByteSpan g = f.info;
        if (g.size() < 0x70) return out;
        const u32 n = rd32(g, 0x68), off = rd32(g, 0x6C);
        if (n > 64 || u64(off) + 0x40ull * n > g.size()) return out;
        for (u32 i = 0; i < n; ++i) {
            const size_t o = off + 0x40 * size_t(i);
            CarMount m;
            m.pos = {rdf(g, o), rdf(g, o + 4), rdf(g, o + 8)};
            m.pitch = rdf(g, o + 0x0C), m.yaw = rdf(g, o + 0x10), m.roll = rdf(g, o + 0x14);
            m.zoom = rdf(g, o + 0x18);
            m.flags = rd32(g, o + 0x1C);
            out.push_back(m);
        }
    } catch (const std::exception&) {
        out.clear();
    }
    return out;
}

float orbit_ease(float a) {
    auto g = [](float t) {
        t = std::clamp(t / 90.0f, 0.0f, 1.0f);
        return 90.0f * (((0.1285715f * t - 0.557143f) * t + 1.4285715f) * t);
    };
    switch (int(a / 90.0f)) {
    case 0: return g(a);
    case 1: return 180.0f - g(180.0f - a);
    case 2: return 180.0f + g(a - 180.0f);
    case 3: return 360.0f - g(360.0f - a);
    default: return 0.0f;
    }
}

float ProceduralCamera::yaw_of(const render::Vec3& v) { return (kPi - std::atan2(v.x, v.z)) * kToDeg; }

float ProceduralCamera::pitch_of(const render::Vec3& v) { return -std::atan2(v.y, std::hypot(v.x, v.z)) * kToDeg; }

render::Mat4 ProceduralCamera::rotation(float roll, float pitch, float yaw) {
    return render::rotate_z(roll * kDeg) * render::rotate_x(pitch * kDeg) * render::rotate_y(yaw * kDeg);
}

void ProceduralCamera::Spring::step(GameRandom& rng, float k, float p) {
    rng.next();
    if (rng.rand01() < p) {
        rng.next();
        target = 256.0f * rng.rand01() - 128.0f;
    }
    v += (k * (target - x) - 10.0f * v) / 60.0f;
    x += v / 60.0f;
}

void ProceduralCamera::Damper::step(float u) {
    v += ((u - u_last) - 12.0f * v) / 60.0f;
    x += v / 60.0f;
    u_last = u;
}

float ProceduralCamera::draw() {
    rng_.next();
    return rng_.rand01();
}

render::Mat4 ProceduralCamera::car_frame(const CameraCar& car) const {
    return render::rotate_y(-car.yaw) * render::translate(car.pos * -1.0f);
}

void ProceduralCamera::start(const ProceduralShot& shot, const CameraCar& car, const GroundHeight& ground,
                             const std::vector<CarMount>& mounts) {
    kind_ = std::clamp(shot.kind, 0, 4);
    rng_.x = shot.seed;
    roll_ = 0;
    focus_ = 100; // (roadside, mounts)
    switch (kind_) {
    case Roadside: start_roadside(car, ground, shot.x); break;
    case Hover: {
        const float d = 8.0f * draw() + 1.0f, th = 2.0f * kPi * draw();
        offset_ = {d * std::sin(th), 4.0f * draw() + 3.0f, d * std::cos(th)};
        fov_ = 30.0f * draw() + 55.0f;
        zoom_ = zoom_of(fov_);
        roll_ = 40.0f * draw() - 20.0f;
        for (Spring& s : wobble_) s = {};
        break;
    }
    case Orbit: {
        extra_ = 5.0f * draw();
        end_az_ = 60.0f * draw() - 30.0f;
        sweep_ = 100.0f + 220.0f * draw() * std::min(shot.x, 3.0f);
        dir_ = draw() < 0.5f ? -1.0f : 1.0f;
        x_end_ = draw() - 0.5f;
        x_start_ = 3.0f * draw() - 1.5f;
        z_end_ = draw() - 0.5f;
        z_start_ = 3.0f * draw() - 1.5f;
        h_end_ = draw() + 0.1f;
        h_rise_ = 2.0f * draw();
        fov_ = 30.0f * draw() + 40.0f;
        zoom_ = zoom_of(fov_);
        roll_ = 40.0f * draw() - 20.0f;
        total_ = left_ = std::max(shot.x, 0.01f);
        break;
    }
    case Telephoto: {
        roll_ = 40.0f * draw() - 20.0f;
        rear_ = draw() < 0.5f;
        dist_ = (rear_ ? car.rear : car.front) + 2.5f + 4.0f * draw();
        height_ = 1.2f * draw() + 0.1f;
        lateral_ = 3.0f * draw() - 1.5f;
        if (draw() < 0.3f) lateral_ = 0, roll_ = 0; // dead centre, level
        aim_height_ = 0.5f * draw() + 0.2f;
        // The depth of field's focus: past the bumper, or from the car's origin.
        focus_ = draw() < 0.5f ? dist_ - (rear_ ? car.rear : car.front) : dist_;
        fov_ = 15.0f * draw() + 25.0f;
        zoom_ = zoom_of(fov_);
        for (Spring& s : shake_) s = {};
        for (Damper& d : vibe_) d = {};
        speed_factor_ = 0;
        break;
    }
    default: { // a mount 3 .. n-1 (the cut drew it), 0 with three or fewer
        mount_ = CarMount{{0, -0.8f, 0}, 0, 0, 0, 1.435f, 0}; // (no table: the in-car point)
        if (shot.mount >= 0 && size_t(shot.mount) < mounts.size()) mount_ = mounts[size_t(shot.mount)];
        zoom_ = mount_.zoom > 0 ? mount_.zoom : 1.435f;
        break;
    }
    }
}

void ProceduralCamera::start_roadside(const CameraCar& car, const GroundHeight& ground, float x) {
    zoom_ = zoom_of(70.0f);
    const render::Vec3 P = car.pos, V = car.velocity;
    auto height = [&](float px, float pz, float from) { return ground ? ground(px, pz, from) : std::nullopt; };
    render::Vec3 eye;
    float pitch = 0, yaw = 0, roll = 0;
    bool ok = false;
    if (draw() > 0.5f) { // A: on the car's straight-line path
        const bool ahead = draw() > 0.5f; // looking on along the car's travel
        fov_ = 30.0f * draw() + 55.0f;
        zoom_ = zoom_of(fov_);
        const float r3 = draw();
        const float t = x * (ahead ? 0.15f * r3 + 0.05f : 0.35f * r3 + 0.5f);
        eye.x = P.x + V.x * t + (2.0f * draw() - 1.0f);
        eye.z = P.z + V.z * t + (2.0f * draw() - 1.0f);
        if (std::optional<float> g = height(eye.x, eye.z, P.y + V.y * t + 100.0f)) {
            eye.y = *g + 0.1f + draw();
            pitch = 20.0f * draw() - 15.0f;
            yaw = ahead ? yaw_of(eye - P) : yaw_of(P - eye);
            yaw += 20.0f * draw() - 10.0f;
            roll = 30.0f * draw() - 15.0f;
            ok = true;
        }
    } else { // B: beside the car
        float lx = car.half_width * (0.7f + 1.5f * draw());
        float lz = (car.front + car.rear) * draw();
        if (draw() < 0.5f) lx = -lx;
        if (draw() < 0.5f) lz = -lz;
        const render::Vec3 B = P + (render::rotate_y(car.yaw) * render::rotate_x(car.pitch)).transform_point({lx, 0, lz});
        if (std::optional<float> g = height(B.x, B.z, B.y + 100.0f)) {
            eye = {B.x, *g + 0.1f + 0.6f * draw(), B.z};
            fov_ = 30.0f * draw() + 55.0f;
            zoom_ = zoom_of(fov_);
            const float lead = 1.0f + 2.0f * draw();
            const render::Vec3 T = P + V * lead; // where the car will be
            const render::Vec3 a = P - eye, b = T - eye;
            const float side = a.z * b.x - a.x * b.z < 0 ? -1.0f : 1.0f;
            yaw = yaw_of(b) + side * fov_ * 0.5f;
            pitch = 20.0f * draw() - 15.0f;
            roll = 30.0f * draw() - 15.0f;
            ok = true;
        }
    }
    if (!ok) { // no road there: above the car, along its motion
        eye = P + render::Vec3{0, 1, 0};
        yaw = yaw_of(V);
        pitch = 20.0f * draw() - 10.0f;
        roll = 0;
    }
    fixed_ = rotation(roll, pitch, yaw) * render::translate(eye * -1.0f);
    eye_ = eye;
}

void ProceduralCamera::step(const CameraCar& car) {
    switch (kind_) {
    case Hover:
        wobble_[0].step(rng_, 18.0f, 0.1f);
        wobble_[1].step(rng_, 18.0f, 0.1f);
        wobble_[2].step(rng_, 5.0f, 0.1f);
        wobble_[3].step(rng_, 5.0f, 0.1f);
        break;
    case Orbit: left_ -= 1.0f / 60.0f; break;
    case Telephoto: {
        // Speed 5 .. 40 km/h -> 0 .. 1 (squared speeds).
        const float v2 = render::dot(car.velocity, car.velocity);
        const float q = std::clamp(v2 - 1.929f, 0.0f, 123.457f);
        speed_factor_ = q / 123.457f;
        const float amp = 57.5f * speed_factor_;
        shake_[0].step(rng_, amp, 0.4f);
        shake_[1].step(rng_, amp, 0.4f);
        float u0 = 0, u1 = 0;
        if (draw() < 0.5f) u0 = (draw() - 0.5f) * 750.0f, u1 = (draw() - 0.5f) * 750.0f;
        vibe_[0].step(u0);
        vibe_[1].step(u1);
        break;
    }
    default: break;
    }
}

render::Mat4 ProceduralCamera::view(const CameraCar& car, const GroundHeight& ground, float frac) {
    render::Mat4 m;
    auto to_world = [&](const render::Vec3& local) { return car.pos + render::rotate_y(car.yaw).transform_point(local); };
    auto below = [&](const render::Vec3& local) { // metres under the road + 0.2 (0: clear)
        const render::Vec3 w = to_world(local);
        if (!ground) return 0.0f;
        std::optional<float> g = ground(w.x, w.z, std::max(w.y, car.pos.y) + 5.0f);
        return g && w.y < *g + 0.2f ? *g + 0.2f - w.y : 0.0f;
    };
    switch (kind_) {
    case Roadside: m = fixed_; break;
    case Hover: {
        const render::Vec3 e{offset_.x + 2.0f * wobble_[2].unit() - 1.0f, offset_.y, offset_.z + 2.0f * wobble_[3].unit() - 1.0f};
        const float a = fov_ / 7.0f;
        const float jp = a * wobble_[0].unit() - a * 0.5f, jy = a * wobble_[1].unit() - a * 0.5f;
        const render::Vec3 back = offset_ * -1.0f; // aimed at the car's origin, without the wobble
        m = render::rotate_y(jy * kDeg) * render::rotate_x(jp * kDeg) *
            rotation(roll_, pitch_of(back), yaw_of(back)) * render::translate(e * -1.0f) * car_frame(car);
        focus_ = render::length(e);
        break;
    }
    case Orbit: {
        const float left = left_ - std::clamp(frac, 0.0f, 1.0f) / 60.0f;
        const float f = std::clamp(left, 0.0f, total_) / total_;
        const float rc = std::max({car.front, car.rear, car.half_width});
        const float radius = rc + 0.2f + extra_;
        if (radius + z_end_ - 0.2f < rc) z_end_ = 1.0f;
        float raw = std::fmod(end_az_ + sweep_ * f * dir_, 360.0f);
        if (raw < 0) raw += 360.0f;
        const float theta = orbit_ease(raw);
        float h = h_end_ + h_rise_ * f;
        const float p0 = std::atan2(h, radius) * kToDeg;
        const float pitch = p0 - fov_ / 3.0f * 0.4f * std::sin(p0 * kDeg); // the car a little low
        const float ox = x_end_ + (x_start_ - x_end_) * f, oz = z_end_ + (z_start_ - z_end_) * f;
        const float st = std::sin(theta * kDeg), ct = std::cos(theta * kDeg);
        h += below({ox - radius * st, h, oz + radius * ct}); // kept above the road (the pitch as it was)
        focus_ = std::sqrt(radius * radius + h * h);
        m = render::rotate_z(roll_ * kDeg) * render::rotate_x(pitch * kDeg) * render::translate({0, -h, -radius}) *
            render::rotate_y(theta * kDeg) * render::translate({-ox, 0, -oz}) * car_frame(car);
        break;
    }
    case Telephoto: {
        auto local_eye = [&] { return render::Vec3{-lateral_, height_, rear_ ? dist_ : -dist_}; };
        height_ += below(local_eye()); // raised above the road, aim recomputed
        const render::Vec3 e = local_eye(), to = render::Vec3{0, aim_height_, 0} - e;
        const float n0 = std::clamp(shake_[0].unit(), -1.0f, 1.0f), n1 = std::clamp(shake_[1].unit(), -1.0f, 1.0f);
        const float d0 = std::clamp(vibe_[0].x, -1.0f, 1.0f), d1 = std::clamp(vibe_[1].x, -1.0f, 1.0f);
        m = render::rotate_x(n0 * kDeg) * render::rotate_x(d1 * speed_factor_ * kDeg) * render::rotate_y(0.8f * n1 * kDeg) *
            render::rotate_y(0.25f * d0 * speed_factor_ * kDeg) * rotation(roll_, pitch_of(to), yaw_of(to)) *
            render::translate(e * -1.0f) * car_frame(car);
        break;
    }
    default: // the mount: its angles, then the eye at -pos in the model frame
        m = rotation(mount_.roll, mount_.pitch, mount_.yaw) * render::translate(mount_.pos) * render::inverse(car.body);
        break;
    }
    eye_ = render::inverse(m).transform_point({0, 0, 0});
    return m;
}

} // namespace gt3r::race
