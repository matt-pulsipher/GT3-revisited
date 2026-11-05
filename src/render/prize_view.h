#pragma once

// Prize presentations (.prm: race medals and trophies, licence cards; docs/formats/prm.md)
// drawn with the file's own animation and camera: fly-in, then the loop. The model is
// uploaded once; its textured environment-map primitives use View3D's sphere map
// (DrawItem::sphere_map, from the view-space normals). Drawn through View3D in
// camera space with the game's projection (60 degrees horizontal, near 0.1, far 300).

#include "base/bytes.h"
#include "formats/model/prm_model.h"
#include "render/gpu_model.h"
#include "render/view3d.h"

#include <SDL3/SDL_render.h>

#include <memory>
#include <optional>
#include <string>

namespace gt3r::render {

class PrizeView {
public:
    explicit PrizeView(GpuContext& ctx) : ctx_(ctx) {}
    // False if the data is not a decodable .prm.
    bool load(ByteSpan prm_bytes, const std::string& name = {});
    bool loaded() const { return file_.has_value(); }
    void restart();
    // Opacity of the drawn picture (the licence medal's fade-in).
    void set_alpha(float a) { alpha_ = a; }
    // Advances the animation (fps / 60 frames per 60 Hz tick).
    void tick(double seconds);
    float frame() const { return t_; }
    // One pass of the game's multi-pass draw (accumulation anti-aliasing / depth of
    // field): a sub-pixel shift of the picture (pixels) and an eye offset in view space
    // (metres) with the plane at `focus` kept fixed (0 = no depth of field).
    // `aspect`: of the game's frame, when `rect` has another shape (0 = the rectangle's);
    // `repeated`: DrawOptions::repeated.
    struct Pass {
        float px = 0, py = 0;
        float ex = 0, ey = 0, focus = 0;
        float aspect = 0;
        bool repeated = false;
    };
    // Draws into `rect` (cleared to `clear`; alpha 0 keeps what is behind when supported).
    void draw(SDL_Renderer* r, const SDL_FRect& rect, const float clear[4] = nullptr, const Pass* pass = nullptr);
    // Header display parameters (+0x38 focus distance, +0x3C aperture radius).
    float focus() const { return file_ ? file_->display[0] : 0; }
    float aperture() const { return file_ ? file_->display[1] : 0; }

private:
    GpuContext& ctx_;
    float alpha_ = 1.0f;
    std::optional<prm::PrmFile> file_;
    Bytes bytes_;
    scene::Model model_;
    std::unique_ptr<GpuModel> gpu_;
    std::unique_ptr<GpuModel> shadow_; // GTM1 node 1 (license cards), drawn under light 0
    std::unique_ptr<View3D> view_;
    float t_ = 0;
    double acc_ = 0;
};

} // namespace gt3r::render
