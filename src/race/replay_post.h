#pragma once

// The original's passes over the race picture that belong to its replays (presentation §L6): the
// heat haze lenses and the vignette. Both are built here in the 640 x 448 frame's
// pixels (View3D maps that frame onto its picture) and drawn by View3D (DrawOptions::haze,
// DrawOptions::vignette); RaceScreen decides when (docs/dev/RACE.md "Passes after the 3D picture").
//
// The haze: a field of lenses (400 on every US course) at points in a box that keeps a fixed
// distance ahead of the camera, drifting a little every frame and wrapped back into the box.
// Each lens is a flat hexagon of the frame itself, magnified 1.25x across and 1.11x down, mixed
// 50/50 over it: a ripple of small displacements, tens of pixels across in telephoto shots and
// invisible in wide ones.

#include "base/bytes.h"
#include "render/math3d.h"
#include "render/screen_batch.h"

#include <array>
#include <vector>

namespace gt3r::race {

struct SunParams;

// The wrap of one coordinate into a period L around 0 (fn_002465B8): r = d - trunc(d (1 / L)) L,
// + L when d < 0, then - L / 2. So d in [-L/2, L/2) for d >= 0 up to rounding, and d in
// (-L/2, L/2] for d < 0.
float haze_wrap(float d, float L);

// One lens on screen: its centre in 640 x 448 frame pixels (y down), its size S (frame pixels)
// and its clip depth (render::perspective's reversed depth).
struct HazeLens {
    float x = 0, y = 0, size = 0, depth = 1;
};

// A lens's six vertices (the strip's order): positions and texels (frame pixels) about its centre
// (fn_00245F30): positions (+-0.58 S, +-0.5 S), (+-1.16 S, 0), texels (+-0.58 S, +-0.45 S),
// (+-0.928 S, 0); the left-hand vertices' x and the top one's y kept at or right of / below the
// frame's left / top edge, their texels' at or above 0.
struct HazeVertex {
    float x = 0, y = 0, u = 0, v = 0;
};
std::array<HazeVertex, 6> haze_hexagon(float x, float y, float s);

class HeatHaze {
public:
    static constexpr u32 kSeed = 0x7F7F7F7Fu;       // fn_00245D88 at race set-up (0x17D66C)
    static constexpr u32 kMultiplier = 0x5D588B65u; // fn_00245D90: s = s * k + 1
    static constexpr u32 kMaxLenses = 800;          // the particle buffer (0x6400 bytes of 32)

    struct Particle {
        float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
    };

    // Race set-up (RacePS2Base::vf9): the generator seeded, every particle drawn with the defaults
    // (fn_002461D8: box 32, cap 8, drift 0.5 / 0.01 / 0.5), then again with the course's Env0
    // values (fn_00246290). Nothing when the course's Env0 flag 0x20 is clear.
    void setup(const SunParams& p);
    void clear() { particles_.clear(); }
    bool active() const { return !particles_.empty(); }
    // One 60 Hz frame (RacePS2Base::vf29 -> fn_00246320): every particle moves by its drift
    // (no time step; not reset at cuts).
    void step();
    // This frame's lenses (fn_00246380), back of the list first as drawn: the box centred
    // `ahead` metres along the view axis in camera-relative world axes, each particle wrapped
    // into it (x / z period B = 2 box / m, y period min(cap, B)), then projected with the view's
    // rotation and the projection (the draw composite the original rebuilds before the lenses),
    // kept when inside every clip plane; m = proj(0, 0) (read from the projection alone), the lens
    // size S = S0 sqrt(m). So the field is in front of the camera whatever its heading.
    // `view` the camera's view matrix (its rotation only is used), `proj` its projection.
    // `ticks` (ours, photo mode): the particles that many steps on from now (negative: back), each
    // by its drift times that, fractions too (its renders across the shutter).
    void lenses(const render::Mat4& view, const render::Mat4& proj, std::vector<HazeLens>& out, float ticks = 0) const;

    const std::vector<Particle>& particles() const { return particles_; }
    float size() const { return size_; }

private:
    std::vector<Particle> particles_;
    float size_ = 0, box_ = 0, cap_ = 0, ahead_ = 0;
};

// The lenses as View3D's haze batch (rect = the 640 x 448 frame; texture coordinates in frame
// pixels of the picture's copy; one alpha-blended draw at 50 %).
void add_haze_lenses(render::ScreenBatch& b, const std::vector<HazeLens>& lenses);

// The vignette ring (fn_001AD818): 32 steps (angle i 2 pi / 31, the last 0), a 64-vertex strip
// alternating the outer (radius Env0 +0x950, colour +0x958) and inner (+0x954, +0x968) circles,
// in frame pixels about the frame's centre (y up). Colours as the GS bytes: RGB trunc(255 c + 0.5),
// A trunc(128 a + 0.5), clamped to 255 (fn_0025A090; A 128 = 1.0).
struct VignetteVertex {
    float x = 0, y = 0;
    u8 rgba[4] = {};
};
std::array<VignetteVertex, 64> vignette_ring(const SunParams& p);
// The ring as View3D's vignette batch (rect = the 640 x 448 frame; (Cs - Cd) As + Cd).
void add_vignette(render::ScreenBatch& b, const SunParams& p);

} // namespace gt3r::race
