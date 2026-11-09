#pragma once

// A view's scenery nodes over time. GT3 draws a view's course scenery from the visibility list
// of the view point nearest its camera and switches lists at once (presentation §I7). At its
// resolution a node that joins the list while on screen is a few pixels; in our sharper and
// wider picture it pops in (Route 11's highway beyond the right-hand wall, a lit tower at the
// right edge). SceneryFade keeps the lists' choice, so what a view shows once it settles is the
// original's, but fades a node in over kSeconds when it joins the list and out when it leaves
// (drawn dithered, DrawItem::fade). A camera cut (the eye moves kCut metres or more in a frame)
// and the first frame switch at once.

#include "base/bytes.h"
#include "render/math3d.h"

#include <vector>

namespace gt3r::race {

class SceneryFade {
public:
    static constexpr float kSeconds = 0.6f;
    static constexpr float kCut = 20.0f;
    // Steps towards `list` (the view's visibility list for `eye`; null: no list, every node, no
    // fading) by `dt` seconds. `list` must stay valid until the next update.
    void update(const std::vector<u16>* list, const render::Vec3& eye, float dt);
    // The nodes drawn whole: the list's nodes at full weight, in the list's order; null = every
    // node.
    const std::vector<u16>* solid() const { return all_ ? nullptr : &solid_; }
    // The nodes being faded in or out in fifteen groups by weight (1/16 .. 15/16, some empty):
    // the list's ones in its order, then the leaving ones in the order they left.
    static constexpr int kLevels = 16;
    struct Group {
        float weight = 0;
        std::vector<u16> nodes;
    };
    const std::vector<Group>& faded() const { return faded_; }
    // Back to the first frame's state (a course or race change).
    void reset() { *this = SceneryFade{}; }

private:
    std::vector<float> weight_;     // per node
    std::vector<u8> listed_;        // scratch: per node, in the current list
    std::vector<u16> leaving_;      // nodes out of the list with a weight left
    const std::vector<u16>* list_ = nullptr;
    render::Vec3 eye_;
    bool started_ = false, all_ = true;
    std::vector<u16> solid_;
    std::vector<Group> faded_;
};

} // namespace gt3r::race
