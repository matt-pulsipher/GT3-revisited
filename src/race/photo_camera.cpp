#include "race/photo_camera.h"

#include <algorithm>
#include <cmath>

namespace gt3r::race {

namespace {
constexpr float kTwoPi = 6.28318531f;

// The radical inverse of i in `base` (0..1): the Halton / van der Corput sequence.
float radical_inverse(u32 i, u32 base) {
    double f = 1, r = 0;
    while (i > 0) {
        f /= base;
        r += f * double(i % base);
        i /= base;
    }
    return float(r);
}

// A fixed hash of i to 0..1 (the place within a stratum).
float hash01(u32 i) {
    i ^= i >> 16, i *= 0x7FEB352Du, i ^= i >> 15, i *= 0x846CA68Bu, i ^= i >> 16;
    return float(i >> 8) * (1.0f / 16777216.0f);
}
} // namespace

const char* photo_follow_name(PhotoFollow f) { return f == PhotoFollow::FixedToCar ? "Fixed to car" : "Roadside"; }

PhotoMode photo_follow_mode(PhotoFollow f, bool car_hit) {
    if (f == PhotoFollow::FixedToCar) return PhotoMode::Fixed;
    return car_hit ? PhotoMode::Pan : PhotoMode::Free;
}

const char* photo_aspect_name(PhotoAspect a) { return a == PhotoAspect::Standard ? "4:3" : "16:9"; }

float photo_aspect_ratio(PhotoAspect a) { return a == PhotoAspect::Standard ? 4.0f / 3.0f : 16.0f / 9.0f; }

const char* photo_orientation_name(PhotoOrientation o) { return o == PhotoOrientation::Portrait ? "Portrait" : "Landscape"; }

float photo_frame_ratio(PhotoAspect a, PhotoOrientation o) {
    const float r = photo_aspect_ratio(a);
    return o == PhotoOrientation::Portrait ? 1.0f / r : r;
}

const char* photo_grid_name(PhotoGrid g) {
    switch (g) {
    case PhotoGrid::Thirds: return "Rule of thirds";
    case PhotoGrid::Diagonals: return "Square diagonals";
    case PhotoGrid::GoldenTriangle: return "Golden triangle";
    default: return "Off";
    }
}

std::vector<std::array<float, 4>> photo_grid_lines(PhotoGrid g, float aspect) {
    const float w = std::max(aspect, 1e-3f); // (the frame w x 1, then divided back to 0..1 across)
    switch (g) {
    case PhotoGrid::Thirds:
        return {{1 / 3.0f, 0, 1 / 3.0f, 1}, {2 / 3.0f, 0, 2 / 3.0f, 1}, {0, 1 / 3.0f, 1, 1 / 3.0f}, {0, 2 / 3.0f, 1, 2 / 3.0f}};
    case PhotoGrid::Diagonals: { // each corner's 45 degrees across the short side: the squares at the frame's ends
        if (w >= 1) { // landscape: one frame height across
            const float k = 1.0f / w;
            return {{0, 0, k, 1}, {0, 1, k, 0}, {1, 0, 1 - k, 1}, {1, 1, 1 - k, 0}};
        }
        // portrait: one frame width down
        return {{0, 0, 1, w}, {1, 0, 0, w}, {0, 1, 1, 1 - w}, {1, 1, 0, 1 - w}};
    }
    case PhotoGrid::GoldenTriangle: { // bottom left to top right, the feet from the other corners
        // The diagonal from a = (0, 1) to b = (w, 0); the foot of c on it: a + (c - a).u u.
        const float dx = w, dy = -1, len2 = dx * dx + dy * dy;
        auto foot = [&](float cx, float cy) {
            const float t = ((cx - 0) * dx + (cy - 1) * dy) / len2;
            return std::array<float, 2>{t * dx / w, 1 + t * dy};
        };
        const auto f1 = foot(0, 0), f2 = foot(w, 1);
        return {{0, 1, 1, 0}, {0, 0, f1[0], f1[1]}, {1, 1, f2[0], f2[1]}};
    }
    default: return {};
    }
}

PhotoShutter photo_shutter(float moment, float length, float earliest) {
    return {std::min(moment, std::max(moment - std::max(length, 0.0f), earliest)), moment};
}

PhotoView photo_view(PhotoMode m, const render::Vec3& eye, const render::Mat4& view, const render::Mat4& car_ref,
                     const render::Mat4& car_at) {
    switch (m) {
    case PhotoMode::Fixed: { // the camera in the car's frame at the moment, carried to the render's
        const render::Mat4 carry = car_at * render::inverse(car_ref);
        return {carry.transform_point(eye), view * car_ref * render::inverse(car_at)};
    }
    case PhotoMode::Pan: {
        const render::Vec3 a = render::normalize(car_ref.transform_point({}) - eye);
        const render::Vec3 b = render::normalize(car_at.transform_point({}) - eye);
        const render::Vec3 axis = render::cross(a, b);
        const float s = render::length(axis), c = render::dot(a, b);
        if (s < 1e-7f) return {eye, view}; // (not moving across the view)
        const render::Vec3 k = axis * (1.0f / s);
        // R takes a to b (Rodrigues); the view sees x where it saw R^-1 (x - eye): R^T = R^-1.
        render::Mat4 rt;
        const float t = 1 - c;
        const float r[3][3] = {{c + k.x * k.x * t, k.x * k.y * t - k.z * s, k.x * k.z * t + k.y * s},
                               {k.y * k.x * t + k.z * s, c + k.y * k.y * t, k.y * k.z * t - k.x * s},
                               {k.z * k.x * t - k.y * s, k.z * k.y * t + k.x * s, c + k.z * k.z * t}};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) rt.at(i, j) = r[j][i];
        return {eye, view * render::translate(eye) * rt * render::translate(-eye)};
    }
    default: return {eye, view};
    }
}

bool PhotoCamera::aim(float yaw, float pitch, float zoom) {
    if (yaw == 0 && pitch == 0 && (zoom == 1 || !(zoom > 0))) return false;
    idle_ = 0;
    yaw_ = std::remainder(yaw_ + yaw, kTwoPi);
    pitch_ = std::clamp(pitch_ + pitch, -kMaxPitch, kMaxPitch);
    if (zoom > 0) fovy_ = std::clamp(fovy_ / zoom, kMinFovy, kMaxFovy);
    return true;
}

void PhotoCamera::step_follow(int dir) {
    const int n = int(PhotoFollow::Count);
    follow_ = PhotoFollow(((int(follow_) + dir) % n + n) % n);
    idle_ = 0;
}

void PhotoCamera::step_shutter(int dir) {
    shutter_ = std::clamp(shutter_ + dir, 0, kShutterCount - 1);
    idle_ = 0;
}

PhotoSample photo_sample(int i, int n, const PhotoShutter& s) {
    n = std::max(n, 1);
    i = std::clamp(i, 0, n - 1);
    PhotoSample p;
    // The stratum: i bit-reversed (radical inverse base 2) among n; a power of two takes each once.
    const float stratum = std::floor(radical_inverse(u32(i), 2) * float(n));
    p.t = s.open + (s.close - s.open) * (stratum + hash01(u32(i))) / float(n);
    p.dx = radical_inverse(u32(i) + 1, 3) - 0.5f;
    p.dy = radical_inverse(u32(i) + 1, 5) - 0.5f;
    // The lens: two more Halton bases (7 and 11) on the square, mapped evenly onto the disc
    // (Shirley and Chiu's concentric map: rings of the square to rings of the disc).
    const float a = 2 * radical_inverse(u32(i) + 1, 7) - 1, b = 2 * radical_inverse(u32(i) + 1, 11) - 1;
    if (a != 0 || b != 0) {
        constexpr float kQuarter = 0.78539816f;
        const bool wide = std::fabs(a) > std::fabs(b);
        const float r = wide ? a : b, phi = wide ? kQuarter * (b / a) : 2 * kQuarter - kQuarter * (a / b);
        p.lens_x = r * std::cos(phi), p.lens_y = r * std::sin(phi);
    }
    return p;
}

float photo_focal_mm(float fovy, float aspect) {
    constexpr float kHalfDiagonal = 21.633f; // mm: half of a 36 x 24 mm sensor's diagonal
    const float t = std::tan(std::clamp(fovy, 1e-3f, 3.0f) * 0.5f);
    return kHalfDiagonal / (t * std::sqrt(1 + aspect * aspect));
}

float photo_aperture_radius(float focal_mm, float f_number) { return focal_mm / std::max(f_number, 0.5f) * 0.5f / 1000.0f; }

PhotoLens photo_lens(const render::Vec3& eye, const render::Mat4& view, float dx, float dy, float focus) {
    // The view's first two rows are its right and up axes in the world.
    const render::Vec3 right{view.at(0, 0), view.at(0, 1), view.at(0, 2)}, up{view.at(1, 0), view.at(1, 1), view.at(1, 2)};
    PhotoLens l;
    l.eye = eye + right * dx + up * dy;
    l.view = render::translate({-dx, -dy, 0}) * view;
    // In eye space a point (x, y, z) is now (x - dx, y - dy, z); the shear adds back dx and dy
    // times its depth over the focus (-z / focus), all of them on the plane z = -focus.
    const float f = std::max(focus, 0.05f);
    l.shear.at(0, 2) = -dx / f, l.shear.at(1, 2) = -dy / f;
    return l;
}

void PhotoCamera::begin(const render::Vec3& eye, const render::Mat4& view, float fovy, const render::Vec3& anchor) {
    eye_ = eye, anchor_ = anchor;
    // The view's third row is the eye's back axis in world space (look_at): forward is its negation.
    const render::Vec3 f = render::normalize({-view.at(2, 0), -view.at(2, 1), -view.at(2, 2)});
    yaw_ = std::atan2(-f.x, -f.z);
    pitch_ = std::clamp(std::asin(std::clamp(f.y, -1.0f, 1.0f)), -kMaxPitch, kMaxPitch);
    fovy_ = std::clamp(fovy, kMinFovy, kMaxFovy);
    roll_ = 0;
    idle_ = 0;
}

render::Vec3 PhotoCamera::forward() const {
    const float c = std::cos(pitch_);
    return {-std::sin(yaw_) * c, std::sin(pitch_), -std::cos(yaw_) * c};
}

// (The roll turns the picture about the view's axis: eye space rotated the other way.)
render::Mat4 PhotoCamera::view() const {
    const render::Mat4 level = render::look_at(eye_, eye_ + forward(), {0, 1, 0});
    return roll_ == 0 ? level : render::rotate_z(roll_) * level;
}

bool PhotoCamera::update(const PhotoInput& in, double dt) {
    if (!in.any()) {
        idle_ += dt;
        return false;
    }
    idle_ = 0;
    const float t = float(std::min(dt, 0.1));
    // Turning slows with the zoom: the same stick moves the picture about as far at any lens.
    const float turn = kTurnRate * std::clamp(fovy_ / kBaseFovy, 0.1f, 1.5f) * t;
    yaw_ = std::remainder(yaw_ - in.turn_x * turn, kTwoPi);
    pitch_ = std::clamp(pitch_ + in.turn_y * turn, -kMaxPitch, kMaxPitch);
    fovy_ = std::clamp(fovy_ * std::exp(-in.zoom * kZoomRate * t), kMinFovy, kMaxFovy);
    roll_ = std::clamp(roll_ + in.roll * kRollRate * t, -kMaxRoll, kMaxRoll);

    const float fast = in.fast ? kFastScale : 1.0f;
    const render::Vec3 level{-std::sin(yaw_), 0, -std::cos(yaw_)}; // forward on the ground plane
    const render::Vec3 right{std::cos(yaw_), 0, -std::sin(yaw_)};  // cross(level, up)
    render::Vec3 e = eye_ + level * (in.move_y * kSpeed * fast * t) + right * (in.move_x * kSpeed * fast * t) +
                     render::Vec3{0, in.rise * kRiseSpeed * fast * t, 0};
    const render::Vec3 d = e - anchor_;
    if (const float l = render::length(d); l > kRange) e = anchor_ + d * (kRange / l);
    // Over the road: the surface found looking down from the higher of the last eye and the new
    // one (the last was above it), so a bridge overhead is not taken for the ground.
    if (ground_)
        if (std::optional<float> g = ground_(e.x, e.z, std::max(eye_.y, e.y) + kClearance)) e.y = std::max(e.y, *g + kClearance);
    eye_ = e;
    return true;
}

} // namespace gt3r::race
