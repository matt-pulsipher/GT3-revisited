#pragma once

// The player's gamma over the whole picture (Settings → Display → Gamma, [video] gamma): menus,
// movies, races and our own screens alike. At 1, the default, nothing changes and no pass runs.
// Otherwise the 2D renderer draws each frame into a target the window's size (begin), and end()
// draws it onto the window through shaders/gamma.frag: out = in ^ (1 / gamma) per channel, so
// black and white stay and a higher gamma brightens. Screenshots, read before end(), keep GT3's
// colours.
//
// SDL_GPU: SDL's GPU renderer draws the target with gamma.frag as its fragment shader
// (SDL_GPURenderState), in the frame's own command buffer. OpenGL ES (gpu_gles.h), whose 2D
// renderer takes no shaders: our own pass into a second target, which the 2D renderer then
// copies to the window. Without the 3D renderer, and in VR, there is no gamma.

#include <SDL3/SDL.h>

#include <cmath>
#include <string_view>

struct SDL_GPURenderState; // SDL >= 3.4

namespace gt3r::render {

class GpuContext;

inline constexpr float kGammaMin = 0.5f, kGammaMax = 2.0f, kGammaStep = 0.05f;

// A [video] gamma value, clamped to kGammaMin..kGammaMax (Settings moves it in kGammaStep);
// 1 when empty or not a number.
float parse_gamma(std::string_view text);
// True when `gamma` changes the picture (anything but 1).
inline bool gamma_changes(float gamma) { return std::fabs(gamma - 1.0f) > 0.001f; }

// A renderer property: the texture target standing in for the window this frame (the gamma
// pass's), null otherwise.
inline constexpr const char* kWindowTargetProperty = "gt3r.window_target";
// True while the 2D renderer draws to the window or to the target standing in for it: what is
// sized to the window's pixels (UI text) treats both alike.
bool drawing_to_window(SDL_Renderer* r);

class GammaPass {
public:
    explicit GammaPass(GpuContext& ctx);
    ~GammaPass();
    GammaPass(const GammaPass&) = delete;
    GammaPass& operator=(const GammaPass&) = delete;

    // From the window (the 2D renderer's current target): points the renderer at the frame
    // target, w x h pixels (the window's), with the window's logical presentation. False when
    // the pass cannot run: the frame then goes to the window as usual.
    bool begin(int w, int h);
    // Back to the window: the frame target over all of it, through `gamma`.
    void end(float gamma);

private:
    bool ensure_targets(int w, int h);
    void release_targets();
    bool create_shaders();

    GpuContext& ctx_;
    SDL_Renderer* renderer_;
    int w_ = 0, h_ = 0;
    int failed_w_ = 0, failed_h_ = 0; // the size whose targets could not be made
    SDL_GPUTexture* frame_ = nullptr; // what the 2D renderer draws the frame into
    SDL_Texture* frame_tex_ = nullptr;
    bool failed_ = false; // the shaders could not be made: no gamma this session
    // SDL_GPU: gamma.frag in SDL's GPU renderer.
    SDL_GPUShader* fs_ = nullptr;
    SDL_GPURenderState* state_ = nullptr;
    // OpenGL ES: the pass's own pipeline (gamma.vert's triangle) and its output.
    SDL_GPUShader* vs_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    SDL_GPUBuffer* indices_ = nullptr;
    SDL_GPUTexture* out_ = nullptr;
    SDL_Texture* out_tex_ = nullptr;
};

} // namespace gt3r::render
