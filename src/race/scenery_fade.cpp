#include "race/scenery_fade.h"

#include <algorithm>
#include <cmath>

namespace gt3r::race {

void SceneryFade::update(const std::vector<u16>* list, const render::Vec3& eye, float dt) {
    const render::Vec3 moved = eye - eye_;
    const bool cut = !started_ || all_ || std::sqrt(dot(moved, moved)) >= kCut;
    started_ = true;
    eye_ = eye;
    solid_.clear();
    faded_.resize(kLevels - 1);
    for (size_t k = 0; k < faded_.size(); ++k) faded_[k].weight = float(k + 1) / kLevels, faded_[k].nodes.clear();
    if (!list) {
        all_ = true;
        list_ = nullptr;
        weight_.clear(), listed_.clear(), leaving_.clear();
        return;
    }
    all_ = false;
    size_t top = 0;
    for (u16 n : *list) top = std::max(top, size_t(n) + 1);
    if (list_)
        for (u16 n : *list_) top = std::max(top, size_t(n) + 1);
    if (weight_.size() < top) weight_.resize(top, 0.0f), listed_.resize(top, 0);
    for (u16 n : *list) listed_[n] = 1;
    // The nodes that left: fading out, unless they are back in the list.
    leaving_.erase(std::remove_if(leaving_.begin(), leaving_.end(), [&](u16 n) { return listed_[n] != 0; }),
                   leaving_.end());
    if (list_ && list_ != list)
        for (u16 n : *list_)
            if (!listed_[n] && weight_[n] > 0 && std::find(leaving_.begin(), leaving_.end(), n) == leaving_.end())
                leaving_.push_back(n);
    list_ = list;
    const float step = cut ? 1.0f : std::max(dt, 0.0f) / kSeconds;
    auto group = [&](u16 n) {
        const int level = std::clamp(int(weight_[n] * kLevels + 0.5f), 0, kLevels);
        if (level == kLevels) solid_.push_back(n);
        else if (level > 0) faded_[size_t(level - 1)].nodes.push_back(n);
    };
    for (u16 n : *list) {
        float& w = weight_[n];
        w = std::min(1.0f, w + step);
        if (w >= 1.0f) solid_.push_back(n);
        else group(n);
        listed_[n] = 0;
    }
    for (u16 n : leaving_) {
        float& w = weight_[n];
        w = std::max(0.0f, w - step);
        group(n);
    }
    leaving_.erase(std::remove_if(leaving_.begin(), leaving_.end(), [&](u16 n) { return weight_[n] <= 0; }),
                   leaving_.end());
}

} // namespace gt3r::race
