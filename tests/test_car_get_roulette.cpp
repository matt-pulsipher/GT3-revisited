// The prize car page's roulette (game/car_get_roulette.h, gt_mode §10.1): the timeline, the
// landing on the car won for every set size and candidate, the floor lights, the carousel and
// the camera. Disc-free.

#include "test_framework.h"

#include "base/game_random.h"
#include "game/car_get_roulette.h"

#include <cmath>

using namespace gt3r;
using namespace gt3r::game;

namespace {

struct Timeline {
    int spin = -1, slow = -1, land = -1, fly_in = -1, alone = -1, release = -1;
    bool landed_on_target = false, held_after_landing = true, released_before_landing = false;
};

Timeline run(int n, int target, const std::array<CarGetRoulette::Size, 4>& sizes, CarGetRoulette* out = nullptr) {
    CarGetRoulette r;
    GameRandom shared;
    r.start(n, target, sizes, shared);
    Timeline t;
    for (int f = 1; f <= 3000 && t.release < 0; ++f) {
        r.step();
        if (t.spin < 0 && r.roll == 1) t.spin = f;
        if (t.slow < 0 && r.roll == 2) t.slow = f;
        if (t.land < 0 && (t.slow > 0 || n == 1) && r.roll == 0 && r.state >= 4) {
            t.land = f;
            t.landed_on_target = r.shown() == target;
        }
        if (t.land > 0 && r.shown() != target) t.held_after_landing = false;
        if (t.fly_in < 0 && r.slowing) t.fly_in = f;
        if (t.alone < 0 && r.alone()) t.alone = f;
        if (r.released) {
            t.release = f;
            if (t.land < 0) t.released_before_landing = true;
        }
    }
    if (out) *out = r;
    return t;
}

std::array<CarGetRoulette::Size, 4> cars(int length_mm = 4500, int height_mm = 1300) {
    std::array<CarGetRoulette::Size, 4> s{};
    for (auto& c : s) c = {length_mm, height_mm};
    return s;
}

} // namespace

TEST(car_get_roulette_lands_on_the_car_won) {
    // Every set size 1..4 and every candidate: the roulette stops on the car won, stays on it,
    // and gives the controls back only after that.
    for (int n = 1; n <= 4; ++n)
        for (int t = 0; t < n; ++t) {
            Timeline tl = run(n, t, cars());
            CHECK(tl.release > 0 && !tl.released_before_landing);
            CHECK(tl.landed_on_target && tl.held_after_landing);
            CHECK(tl.fly_in > tl.land && tl.alone > tl.fly_in && tl.release > tl.alone);
        }
    // Sizes of every kind of car (spec +0x1A / +0x22, mm), ones with a zero height included.
    const int lengths[] = {3000, 3950, 4500, 5100};
    const int heights[] = {0, 1050, 1300, 1700};
    for (int n = 2; n <= 4; ++n)
        for (int t = 0; t < n; ++t)
            for (int l : lengths)
                for (int h : heights) {
                    std::array<CarGetRoulette::Size, 4> s{};
                    for (int i = 0; i < 4; ++i) s[size_t(i)] = {l + 170 * i, h ? h + 40 * i : 0};
                    Timeline tl = run(n, t, s);
                    CHECK(tl.release > 0 && tl.landed_on_target && tl.held_after_landing);
                }
}

TEST(car_get_roulette_timeline) {
    // Several candidates: candidate 0 held 120 frames, the spin at 0.1 a frame from frame 121 for
    // 60 frames, the slowdown from frame 181 over (12 div n) n whole candidates plus the way to
    // the middle of the car won; 41 frames after it stops the camera flies in and the others
    // drop 0.05 a frame to twice the tallest car (1.1 x 1300 mm), the controls back then.
    struct Case {
        int n, target, land, release;
    };
    const Case cases[] = {{2, 0, 379, 478}, {2, 1, 397, 496}, {3, 0, 379, 478}, {3, 1, 397, 496}, {3, 2, 414, 513},
                          {4, 0, 414, 513}, {4, 1, 432, 531}, {4, 2, 379, 478}, {4, 3, 397, 496}};
    for (const Case& c : cases) {
        Timeline tl = run(c.n, c.target, cars());
        CHECK(tl.spin == 121 && tl.slow == 181);
        CHECK(tl.land == c.land && tl.fly_in == c.land + 41 && tl.alone == tl.fly_in + 29);
        CHECK(tl.release == c.release);
    }
    // One candidate: no spin, held 210 frames.
    Timeline one = run(1, 0, cars());
    CHECK(one.spin < 0 && one.slow < 0);
    CHECK(one.land == 212 && one.fly_in == 253 && one.release == 311);
    // The spin's constant speed and the roulette's place each frame in 0..n.
    CarGetRoulette r;
    GameRandom shared;
    r.start(4, 1, cars(), shared);
    for (int f = 1; f <= 600; ++f) {
        r.step();
        if (f >= 121 && f < 181) CHECK(r.roll == 1 && r.speed.host() == 0.1f);
        if (f <= 120) CHECK(r.shown() == 0 && r.speed.host() == 0.0f);
        CHECK(r.pos.host() >= 0.0f && r.pos.host() < 4.0f);
        if (r.roll == 2) CHECK(r.speed.host() >= CarGetRoulette::kMinSpeed);
    }
}

TEST(car_get_roulette_lights_and_carousel) {
    CarGetRoulette r;
    GameRandom shared;
    GameRandom before = shared;
    std::array<CarGetRoulette::Size, 4> s = {{{4000, 1200}, {4600, 1350}, {4200, 1250}, {3900, 1150}}};
    r.start(4, 2, s, shared);
    // The headings: n + 1 unit draws on the shared state, each 80..100 degrees after the last.
    before.skip(5);
    CHECK(shared.x == before.x);
    for (int i = 1; i < 4; ++i) {
        float d = std::fmod(r.heading[size_t(i)].host() - r.heading[size_t(i - 1)].host() + 360.0f, 360.0f);
        CHECK(d >= 80.0f && d <= 100.0f);
    }
    // The spacing: the longest pair of neighbours' half lengths x 1.1; the tallest car x 1.1;
    // the square's radius side / sqrt 2.
    CHECK(std::abs(r.side.host() - (2.3f + 2.1f) * 1.1f) < 1e-4f);
    CHECK(std::abs(r.tall.host() - 1.35f * 1.1f) < 1e-4f);
    CHECK(std::abs(r.radius.host() - r.side.host() * 0.70710677f) < 1e-5f);
    int spin_frames = 0;
    for (int f = 1; f <= 2000 && !r.released; ++f) {
        r.step();
        // The light: 1 under the car the roulette is on, the others fading 0.05 a frame.
        for (int i = 0; i < 4; ++i) {
            if (r.current[size_t(i)]) CHECK(r.spot[size_t(i)].host() == 1.0f && i == r.shown());
            else CHECK(r.spot[size_t(i)].host() < 1.0f);
        }
        if (!r.slowing) {
            CHECK(r.turn.host() == CarGetRoulette::kTurnSpeed && r.spin.host() == 1.0f);
            ++spin_frames;
        }
    }
    CHECK(r.released && r.shown() == 2 && r.alone());
    // After the release: the carousel stops (150 frames), the cars' own turn slows to 0.35.
    for (int f = 0; f < 1200; ++f) r.step();
    CHECK(r.turn.host() == 0.0f && r.spin.host() == CarGetRoulette::kCarSpinFloor);
    // The camera has flown in on the car won: looking at 0.3 x its height, from 1.8 x it.
    float x = 0, z = 0;
    r.place(2, x, z);
    CHECK(std::abs(r.look[0].x.host() - x) < 0.01f && std::abs(r.look[2].x.host() - z) < 0.01f);
    CHECK(std::abs(r.look[1].x.host() - r.tall.host() * 0.3f) < 0.01f);
    CHECK(std::abs(r.eye[1].x.host() - r.tall.host() * 1.8f) < 0.01f);
    CHECK(std::abs(r.eye[2].x.host() - (z + r.side.host() * 1.6f)) < 0.01f);
    (void)spin_frames;
}

TEST(car_get_roulette_silhouettes_and_fades) {
    // Every car a black silhouette until the car won is alone; the others fade out while the
    // camera waits to fly in (the page's background put back over them at 1 - glow); the car
    // won then lights up over 30 frames; the controls come back at twice the tallest car's
    // drop, just after it is fully lit (1.548 m x 1.1 here: 35 frames to alone, 69 to the
    // release; the Japan Championship's four cars).
    std::array<CarGetRoulette::Size, 4> s = {{{4350, 1548}, {4330, 1267}, {4365, 1131}, {4285, 1103}}};
    CarGetRoulette r;
    GameRandom shared;
    r.start(4, 2, s, shared);
    int glow_start = -1, gone = -1, alone = -1, lit = -1, release = -1;
    for (int f = 1; f <= 2000 && release < 0; ++f) {
        r.step();
        if (!r.alone()) CHECK(r.shade() == 0.0f);
        if (r.state < 4 || (r.state == 4 && glow_start < 0)) {
            if (r.state < 4) CHECK(r.others_opacity() == 1.0f);
        }
        if (r.state >= 4 && glow_start < 0) glow_start = f;
        if (glow_start > 0 && !r.slowing) {
            // The constant alpha int((1 - glow) x 128) / 128 put back over the others.
            const float fix = float(int((1.0f - r.glow.host()) * 128.0f));
            CHECK(std::abs(r.others_opacity() - (1.0f - fix / 128.0f)) < 1e-6f);
        }
        if (gone < 0 && r.others_opacity() == 0.0f) gone = f;
        if (alone < 0 && r.alone()) {
            alone = f;
            CHECK(r.shade() > 0.0f && r.shade() < 0.04f); // 1/30 on its first frame
        }
        if (lit < 0 && r.shade() == 1.0f) lit = f;
        if (r.released) release = f;
    }
    CHECK(gone > glow_start && gone <= glow_start + 41);
    // (1/30 a frame in EE singles reaches 1 on the 31st frame, the clamp.)
    CHECK(alone > gone && lit == alone + 30 && release == lit + 4);
    CHECK(alone - gone == 35 && release - alone == 34);
    CHECK(r.shade() == 1.0f && r.others_opacity() == 0.0f);
}
