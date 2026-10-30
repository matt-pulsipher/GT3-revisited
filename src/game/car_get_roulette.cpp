#include "game/car_get_roulette.h"

#include <algorithm>
#include <cmath>

namespace gt3r::game {

namespace {

using ee::F;

F f(float v) { return F(v); }
// A double result rounded to single (__truncdfsf2: to nearest).
F from_double(double v) { return F(float(v)); }

} // namespace

void CarGetRoulette::Spring::step(F damp) {
    const F dt = f(kDt);
    F vel = v + force * dt;
    force = F();
    vel = vel - (vel * damp) * dt;
    v = vel;
    x = x + vel * dt;
}

void CarGetRoulette::start(int count, int won, const std::array<Size, kMaxCars>& sizes, GameRandom& shared) {
    *this = CarGetRoulette{};
    n = count < 1 ? 1 : count > kMaxCars ? kMaxCars : count;
    target = won >= 0 && won < n ? won : 0;
    // The carousel: the spacing from the longest pair of neighbours
    // (half lengths summed), the tallest car, the radius of the polygon of that side.
    std::array<F, kMaxCars> half{}, high{};
    for (int i = 0; i < n; ++i) {
        half[size_t(i)] = (ee::int_to_f(s16(sizes[size_t(i)].length_mm)) * f(kUnit)) * f(0.5f);
        high[size_t(i)] = ee::int_to_f(s16(sizes[size_t(i)].height_mm)) * f(kUnit);
    }
    for (int i = 0; i < n; ++i) {
        const F s = (half[size_t(i)] + half[size_t((i + 1) % n)]) * f(kSpacing);
        const F t = high[size_t(i)] * f(kSpacing);
        side = i == 0 ? s : ee::max(side, s);
        tall = i == 0 ? t : ee::max(tall, t);
    }
    switch (n) {
    case 1: radius = F(); break;
    case 2: radius = side * f(0.5f); break;
    case 3: radius = from_double(double(side.host()) * kRadius3); break;
    default: radius = side * f(kRadius4); break;
    }
    // Each car's heading: the previous car's + 80 + 20 x a unit draw (the first from one more
    // draw), within 0..360.
    F prev = f(shared.rand01()) * f(kHeadingRange);
    for (int i = 0; i < n; ++i) {
        F h = (f(shared.rand01()) * f(kHeadingRange) + f(kHeadingStep)) + prev;
        while (f(360.0f) <= h) h = h - f(360.0f);
        heading[size_t(i)] = h;
        prev = h;
    }
    turn = f(kTurnSpeed);
    spin = f(kCarSpin);
}

float CarGetRoulette::others_opacity() const {
    // pgl_set_alpha: FIX = int(clamp(a x 128, 0, 255)), 0x80 = 1.
    const F a = ee::min(ee::max((f(1.0f) - glow) * f(128.0f), F()), f(255.0f));
    return std::max(0.0f, 1.0f - float(ee::to_int(a)) / 128.0f);
}

void CarGetRoulette::place(int i, float& x, float& z) const {
    // The i-th corner of the polygon (i 360 / n degrees, radius) turned by the
    // carousel's angle (the program's sincos, right-handed about y).
    const double d2r = 3.14159265358979323846 / 180.0;
    const double a = double(float(i) * 360.0f / float(n > 0 ? n : 1)) * d2r;
    const double r = radius.host();
    const double x0 = r * std::cos(a), z0 = r * std::sin(a);
    const double k = double(angle.host()) * d2r, s = std::sin(k), c = std::cos(k);
    x = float(x0 * c + z0 * s);
    z = float(-x0 * s + z0 * c);
}

void CarGetRoulette::step() {
    if (n <= 0) return;
    const F nf = ee::int_to_f(n);
    // --- the roulette ---
    if (roll == 2) {
        F v = speed + decel;
        dist = dist - v;
        speed = v <= f(kMinSpeed) ? f(kMinSpeed) : v;
        if (dist <= F() && ee::to_int(pos) == target) {
            speed = F();
            roll = 0;
        }
    }
    pos = pos + speed;
    while (nf <= pos) pos = pos - ee::int_to_f(ee::to_int(pos / nf)) * nf;
    // --- the carousel ---
    if (slowing) {
        if (std::fabs(double(spin.host())) > double(kCarSpinFloor)) spin = spin + f(kCarSpinDecay);
        else spin = f(kCarSpinFloor);
        const F t = turn + f(kTurnDecay);
        turn = t * f(kTurnSign) < F() ? F() : t;
    }
    for (int i = 0; i < n; ++i) {
        F h = heading[size_t(i)] + spin;
        if (f(360.0f) <= h) h = h - f(360.0f);
        else if (h < F()) h = h + f(360.0f);
        heading[size_t(i)] = h;
    }
    F a = angle + turn;
    if (f(360.0f) <= a) a = a - f(360.0f);
    else if (a < F()) a = a + f(360.0f);
    angle = a;
    // --- the cars item ---
    --timer;
    int lit = -1;
    for (int i = 0; i < n; ++i) {
        current[size_t(i)] = i == shown();
        if (current[size_t(i)]) lit = i;
        // The floor light.
        F& s = spot[size_t(i)];
        if (current[size_t(i)]) s = f(1.0f);
        else s = s - f(kSpotStep) < F() ? F() : s - f(kSpotStep);
    }
    // --- the camera (state table) ---
    switch (cam_state) {
    case 0: {
        const F far = side * f(0.5f) + radius;
        const F lift = from_double(double(tall.host()) + double(far.host()) * kEyeLift);
        const F back = from_double(double(lift.host()) * kLookBack);
        eye_to = {F(), tall + tall, far * f(n < 2 ? kEyeBackOne : kEyeBack)};
        eye_k = f(kOpenK), eye_damp = f(kOpenDamp);
        eye = {Spring{F(), F(), F()}, Spring{F(), F(), lift}, Spring{F(), F(), F()}};
        look = {Spring{F(), F(), F()}, Spring{F(), F(), F()}, Spring{F(), F(), -back}};
        look_to = {F(), F(), F()};
        look_k = f(kK), look_damp = f(kDamp);
        cam_state = 1;
        if (cam_go) cam_go = false, cam_state = 2;
        break;
    }
    case 1:
        if (cam_go) cam_go = false, cam_state = 2;
        break;
    case 2:
        cam_state = 3;
        if (cam_go) cam_go = false, cam_state = 4;
        break;
    case 3:
        if (cam_go) cam_go = false, cam_state = 4;
        break;
    case 4:
    case 5: {
        cam_state = 5;
        float x = 0, z = 0;
        place(lit < 0 ? 0 : lit, x, z);
        look_to = {F(x), tall * f(kLookHeight), F(z)};
        look_k = f(kK), look_damp = f(kDamp);
        eye_to = {F(x), tall * f(kEyeHeight), F(z) + side * f(kEyeOut)};
        eye_k = f(kK), eye_damp = f(kDamp);
        break;
    }
    default: break;
    }
    for (int k = 0; k < 3; ++k) { // The menu car camera: the look springs
        look[size_t(k)].force = look[size_t(k)].force + (look_to[size_t(k)] - look[size_t(k)].x) * look_k;
        look[size_t(k)].step(look_damp);
    }
    for (int k = 0; k < 3; ++k) {
        eye[size_t(k)].force = eye[size_t(k)].force + (eye_to[size_t(k)] - eye[size_t(k)].x) * eye_k;
        eye[size_t(k)].step(eye_damp);
    }
    // --- the page's steps ---
    switch (state) {
    case 0:
        timer = n == 1 ? kHoldOneFrames : kHoldFrames;
        state = 1;
        [[fallthrough]];
    case 1:
        if (timer > 0) break;
        cam_go = true;
        if (n == 1) {
            state = 3;
            break;
        }
        roll = 1, speed = f(kSpinSpeed);
        timer = kSpinFrames;
        state = 2;
        break;
    case 2: {
        if (timer > 0) break;
        // The travel to the middle of the car won plus (12 div n) n whole candidates,
        // the deceleration that ends it at speed 0 one candidate later.
        F to = (ee::int_to_f(target) + f(0.5f)) - pos;
        while (to < F()) to = to + nf;
        roll = 2;
        dist = ee::int_to_f(n * (kStopTurns / n)) + to;
        decel = -(speed * speed) / ((dist + f(1.0f)) + (dist + f(1.0f)));
        state = 3;
        break;
    }
    case 3:
        if (roll == 0) state = 4;
        break;
    case 4:
        glow = glow - f(kGlowStep);
        if (glow < F()) {
            glow = F();
            slowing = true; // the carousel and the cars slow down
            cam_go = true;  // the camera flies in on the car won
            state = 5;
        }
        break;
    case 5: {
        sink = sink - f(kSinkStep);
        const F floor = -tall + -tall;
        if (sink < floor) {
            released = true; // the controls back ((pm, 0)), the message, the band
            sink = floor;
            state = 6;
        }
        break;
    }
    default: break;
    }
    if (!alone()) after_fade = F();
    else after_fade = ee::min(after_fade + f(kAfterFadeStep), f(1.0f));
}

} // namespace gt3r::game
