#include "race/clut_animations.h"

namespace gt3r::race {

void ClutAnimations::add(render::GpuModel* model, std::vector<gtm1::ClutSet>&& sets) {
    if (model && !sets.empty()) models_.push_back({model, std::move(sets)});
}

void ClutAnimations::restart(float countdown) {
    for (Model& m : models_)
        for (gtm1::ClutSet& s : m.sets) {
            s.textures.player().reset();
            s.textures.player().start_signal(countdown);
        }
}

void ClutAnimations::animate(float seconds) {
    for (Model& m : models_)
        for (gtm1::ClutSet& s : m.sets) s.textures.player().update(seconds);
}

std::vector<ClutPlayer> ClutAnimations::state() const {
    std::vector<ClutPlayer> s;
    for (const Model& m : models_)
        for (const gtm1::ClutSet& c : m.sets) s.push_back(c.textures.player());
    return s;
}

void ClutAnimations::set_state(const std::vector<ClutPlayer>& s) {
    size_t i = 0;
    for (Model& m : models_)
        for (gtm1::ClutSet& c : m.sets)
            if (i < s.size()) c.textures.player() = s[i++];
}

void ClutAnimations::upload() {
    for (Model& m : models_) {
        std::vector<render::GpuModel::TextureUpdate> updates;
        for (gtm1::ClutSet& s : m.sets)
            for (u32 r : s.textures.refresh()) updates.push_back({size_t(s.first) + r, s.textures.rgba(r)});
        if (!updates.empty()) m.gpu->update_textures(updates);
    }
}

} // namespace gt3r::race
