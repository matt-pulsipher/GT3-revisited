#pragma once

// GT3's spark draw (from the race's drawing: the one-player view only, not the rear-view mirror or the split-screen
// halves; presentation §L7). The sparks themselves are the race's (sim/exact/sparks.h). Every active particle is a streak
// from where it was in eye space one frame earlier to where it is now: a quad 2w wide (w its half-width), its ends pushed
// out by w along the streak, the race/particle.img glow (16x16, bilinear, clamped) times the particle's colour by its life
// remaining (four keys, orange to dark red), added to the frame (Cs + Cd: alpha ignored), depth-tested without depth
// writes, unfogged, both faces. A particle is first drawn once it has an earlier position, and a camera cut drops every
// earlier position (from the update), so no streak runs across a cut.
//
// Ours draws between the race's frames (the position blended like the cars'); the earlier position is the eye-space one at
// the same time one race frame (1/60 s) before, from each particle's last few drawn positions, so a streak spans one race
// frame at any display rate (GT3 draws once per frame: the last frame's position).

#include "render/view3d.h"
#include "sim/exact/sparks.h"

#include <array>
#include <memory>
#include <vector>

namespace gt3r {
class Vfs;
}

namespace gt3r::race {

// A particle's colour from its life remaining L (s): the first key whose time is below L (key 0 itself;
// later keys blended with the key before), each channel x 127.5 truncated (the GS's 0x80 = 1.0), RGBA in the low bytes;
// 0 (nothing added) when no key's time is below L.
u32 spark_colour(float life);

class SparkDraw {
public:
    SparkDraw();
    ~SparkDraw();
    // race/particle.img; false when the file is missing or unreadable (no sparks drawn).
    bool load(render::GpuContext& ctx, const Vfs& vfs);
    bool loaded() const { return gpu_ != nullptr; }
    // Every earlier position dropped (a new race, a camera cut).
    void clear();
    // The streaks of `s` seen with `view` (world to eye), the particles blended `alpha` of the way from their last positions
    // (the cars' blend), appended to `items` (none drawn: nothing appended). `cut`: the view switched camera since the last
    // call.
    void add_items(std::vector<render::DrawItem>& items, const sim::exact::Sparks& s, const render::Mat4& view, float alpha,
                   bool cut);

private:
    static constexpr size_t kHistory = 8; // drawn positions kept per particle (a race frame back at up to 420 Hz)
    struct Slot {
        u32 serial = 0;
        size_t n = 0; // samples kept, newest last
        std::array<double, kHistory> t{};
        std::array<render::Vec3, kHistory> eye{};
    };
    std::array<Slot, sim::exact::Sparks::kPool> slots_{};
    double last_t_ = -1;
    std::vector<scene::Vertex> verts_;
    std::unique_ptr<render::GpuModel> gpu_;
};

} // namespace gt3r::race
