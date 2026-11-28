#pragma once

// The CLUT animations of a race's course models (formats/clut_animation.h), played as the game
// plays them: every animation back to its first frame when a run starts, then the start signal
// for the run's countdown; advanced by 1/60 s per race step (not while paused); the recoloured
// textures uploaded once per drawn frame. The game plays those of the scenery, the slot 5
// scenery, the wet-road reflection, the backdrop and the billboards.

#include "formats/model/gtm1.h"
#include "render/gpu_model.h"

#include <vector>

namespace gt3r::race {

class ClutAnimations {
public:
    // `sets`' `first` index `model`'s textures.
    void add(render::GpuModel* model, std::vector<gtm1::ClutSet>&& sets);
    bool empty() const { return models_.empty(); }

    // A run's start; `countdown` seconds of start hold (0 without one).
    void restart(float countdown);
    void animate(float seconds);
    // Uploads the textures recoloured since the last call.
    void upload();
    // Every animation's state (ours: photo mode puts it back after drawing other times; the next
    // upload() recolours what differs).
    std::vector<ClutPlayer> state() const;
    void set_state(const std::vector<ClutPlayer>& s);

private:
    struct Model {
        render::GpuModel* gpu = nullptr;
        std::vector<gtm1::ClutSet> sets;
    };
    std::vector<Model> models_;
};

} // namespace gt3r::race
