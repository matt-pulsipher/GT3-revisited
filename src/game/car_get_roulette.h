#pragma once

// The prize car page (car_get, gt_mode §10.1): GT3's roulette over a prize set's candidates.
// The candidates stand on a turning carousel, each on its own turning spot; a floor light marks
// the car the roulette is on; it holds on the first candidate, spins at a constant speed, then
// slows down onto the car won and stops only with that car under the light. The camera then
// flies in on it while the others sink through the floor, and only after that does the page
// give the player the controls (its OK button), the "Bonus car acquired" message and the band
// behind it. One frame of the page per step(), in the program's order and EE single precision
// for everything that decides a frame; the program constants below with their addresses
// (NTSC-U 1.10; the NTSC-J program holds the same values, the PAL program its 50 Hz ones).

#include "base/ee_float.h"
#include "base/game_random.h"

#include <array>

namespace gt3r::game {

struct CarGetRoulette {
    static constexpr int kMaxCars = 4; // the page loads at most four candidates

    // --- program constants -------------------------------------------------------------
    // the cars item (state table):
    static constexpr int kHoldFrames = 120;    // state 0, several candidates (PAL 100)
    static constexpr int kHoldOneFrames = 210; // state 0, one candidate (PAL 175)
    static constexpr int kSpinFrames = 60;     // state 1 -> 2 (PAL 50)
    static constexpr float kGlowStep = 0.025f; // state 4, from 1 to below 0 (PAL 0.03)
    static constexpr float kSinkStep = 0.05f;  // state 5, the others' drop per frame (PAL 0.06)
    static constexpr float kAfterFadeStep = 0.033333335f; // the won car's light after the drop
    // The roulette's init, spin, stop and step:
    static constexpr float kSpinSpeed = 0.1f;          // candidates per frame (PAL 0.12)
    static constexpr float kMinSpeed = 0.016666668f;   // the slowdown's floor (PAL 0.02)
    static constexpr int kStopTurns = 12;              // whole candidates of travel: (12 div n) n
    // The floor light under each car (TheCar +0x360): 1 on the car the roulette is on,
    // else down by this per frame to 0 (PAL 0.06).
    static constexpr float kSpotStep = 0.05f;
    // The carousel (at page build, per frame):
    static constexpr float kUnit = 0.001f;              // mm -> m
    static constexpr float kSpacing = 1.1f;
    static constexpr double kRadius3 = 0.5773502691896257; // n = 3 (double)
    static constexpr float kRadius4 = 0.70710677f;      // n = 4; n = 2: 0.5, n = 1: 0
    static constexpr float kTurnSpeed = -0.20833333f;   // degrees per frame
    static constexpr float kTurnDecay = 0.0013888889f;  // per frame once slowing
    static constexpr float kTurnSign = -0.20833333f;    // the stop test's factor
    static constexpr float kCarSpin = 1.0f;             // each car's own turn, degrees per frame
    static constexpr float kCarSpinDecay = -0.0013888889f;
    static constexpr float kCarSpinFloor = 0.35f;       // (compared as |spin| > 0.35, double)
    static constexpr float kHeadingRange = 20.0f;       // shared unit x 20
    static constexpr float kHeadingStep = 80.0f;        // + 80 + the previous car's
    // The camera (CarGetMenuCamera; springs as menu car camera, dt):
    static constexpr float kDt = 0.016666668f;
    static constexpr double kEyeLift = 0.57735, kLookBack = 5.67128; // doubles
    static constexpr float kEyeBack = 2.4f, kEyeBackOne = 3.6f;
    static constexpr float kOpenK = 0.2f, kOpenDamp = 1.0f;
    static constexpr float kK = 1.9f, kDamp = 4.0f;
    static constexpr float kLookHeight = 0.3f, kEyeHeight = 1.8f, kEyeOut = 1.6f;
    static constexpr float kUpMin = 0.01f;
    static constexpr float kSpotDrop = -0.005f;                      // the light's height

    // The candidates' sizes as the page reads them from each car's built spec:
    // +0x1A (the chassis length, mm) and +0x22 (the body's height extent, mm), both s16.
    struct Size {
        int length_mm = 0;
        int height_mm = 0;
    };

    // A spring of menu car camera (12 bytes: force, velocity, position).
    struct Spring {
        ee::F force, v, x;
        void step(ee::F damp);
    };

    // --- state -----------------------------------------------------------------------
    int n = 0;
    int target = 0;   // the candidate won (the weighted pick, cars item +0x114)
    // Roulette (cars item +0x100): position in candidates, speed, travel left, deceleration.
    ee::F pos, speed, dist, decel;
    int roll = 0;     // 0 still, 1 spinning, 2 slowing (+0x118)
    // The page's own steps (cars item +0x1045, +0x1030).
    int state = 0;
    int timer = -1;
    ee::F glow = ee::F(1.0f); // +0x1034
    ee::F sink;               // +0x103C
    ee::F after_fade;         // +0x1038
    bool released = false;    // control +0xED5: the controls back, the message, the "after" band
    // Carousel (control +0xEA4): angle and its speed (degrees), each car's heading and spin,
    // the radius, the spacing, the tallest car (x 1.1), slowing (control +0xED0).
    ee::F angle, turn = ee::F(kTurnSpeed), spin = ee::F(kCarSpin), radius, side, tall;
    std::array<ee::F, kMaxCars> heading{};
    bool slowing = false;
    std::array<bool, kMaxCars> current{};
    std::array<ee::F, kMaxCars> spot{};
    // Camera (cars item +0xED4): state, the go flag (+0x150), eye / look springs and targets.
    int cam_state = 0;
    bool cam_go = false;
    std::array<Spring, 3> eye{}, look{};
    std::array<ee::F, 3> eye_to{}, look_to{};
    ee::F eye_k, eye_damp, look_k, look_damp;

    // The page is built: `count` candidates (1..4), the one won, their sizes; the cars'
    // headings are drawn from the shared state (count + 1 unit draws).
    void start(int count, int won, const std::array<Size, kMaxCars>& sizes, GameRandom& shared);
    // One frame of the page (the cars item).
    void step();

    // The candidate the roulette is on (the one lit; the car given when the page ends).
    int shown() const { return n > 0 ? ee::to_int(pos) : 0; }
    // The others have dropped below the floor by more than the tallest car: only the car won
    // is drawn from then on (the item's draw calls instead).
    bool alone() const { return sink < -tall; }
    // How much of the cars drawn before the lit one stays on screen: the
    // lit car's draw first puts the page's own background back over the frame, at the constant
    // alpha int(clamp((1 - glow) x 128, 0, 255)) / 128 (pgl_set_alpha 0x64), so the others and
    // their floor lights fade out while the glow runs down (they sink unseen) and the lit car,
    // its light and anything after are drawn over it.
    float others_opacity() const;
    // The cars' colour scale ((0, s) around each car's draw): 0 on the carousel (black
    // silhouettes), then the car won alone at +0x1038, which also scales the page light
    // it is lit by: it lights up over 30 frames.
    float shade() const { return alone() ? after_fade.host() : 0.0f; }
    // A candidate's place on the floor (m) and its heading (degrees).
    void place(int i, float& x, float& z) const;
};

} // namespace gt3r::game
