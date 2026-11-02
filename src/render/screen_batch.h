#pragma once

// Screen-space triangles drawn over a 3D view's picture: the sun glare and the lamp glows
// (inside View3D's picture, before its grade: DrawOptions::overlay), the heat haze lenses
// (DrawOptions::haze) and the replay vignette (DrawOptions::vignette); presentation §L6.
// The same batch also goes to the 2D renderer (submit_2d), where a picture has no grade (VR eyes).

#include "base/bytes.h"
#include "render/gpu_context.h"

#include <SDL3/SDL_render.h>

#include <vector>

namespace gt3r::render {

// A texture for both renderers: the 2D renderer's (submit_2d) and the GPU's (View3D).
class SpriteTexture {
public:
    SpriteTexture() = default;
    SpriteTexture(const SpriteTexture&) = delete;
    SpriteTexture& operator=(const SpriteTexture&) = delete;
    ~SpriteTexture() { release(); }
    // `rgba`: w x h texels, R G B A bytes in memory order. `gpu` may be null (2D only).
    bool create(SDL_Renderer* r, GpuContext* gpu, int w, int h, const u32* rgba);
    void release();
    SDL_Texture* sdl() const { return sdl_; }
    SDL_GPUTexture* gpu() const { return gpu_; }
    bool valid() const { return sdl_ || gpu_; }

private:
    SDL_Texture* sdl_ = nullptr;
    SDL_GPUTexture* gpu_ = nullptr;
    SDL_GPUDevice* device_ = nullptr;
};

struct ScreenBatch {
    struct Vertex {
        float x = 0, y = 0; // in `rect`'s units (top-left origin)
        float rgba[4] = {1, 1, 1, 1};
        float u = 0, v = 0; // texture coordinates 0..1 (the haze: in `rect`'s units, see View3D)
        float depth = 1;    // clip depth (reversed: 1 at the near plane, always in front)
    };
    struct Draw {
        const SpriteTexture* texture = nullptr; // null: untextured
        u32 first = 0, count = 0;               // indices
        int passes = 1;                         // drawn this many times (GS weights over 1)
        BlendMode blend = BlendMode::Additive;  // Additive (Cs As + Cd) or Alpha ((Cs - Cd) As + Cd)
    };
    SDL_FRect rect{0, 0, 1, 1};
    std::vector<Vertex> verts;
    std::vector<u32> idx;
    std::vector<Draw> draws;

    bool empty() const { return draws.empty(); }
    void clear() { verts.clear(), idx.clear(), draws.clear(); }
    // Triangles over `vertices` (three indices each, into `vertices`) with one texture.
    void add(const SpriteTexture* texture, const Vertex* vertices, size_t count, const u32* indices,
             size_t index_count, int passes = 1, BlendMode blend = BlendMode::Additive);
    // A textured rectangle (texture coordinates 0..1) in one colour.
    void add_rect(const SpriteTexture* texture, const SDL_FRect& dst, const float rgba[4]);
};

// Draws `b` with the 2D renderer into `b.rect` (each draw's blend; textures' SDL side).
void submit_2d(SDL_Renderer* r, const ScreenBatch& b);

} // namespace gt3r::render
