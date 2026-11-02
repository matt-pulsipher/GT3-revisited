#include "render/screen_batch.h"

#include "base/log.h"

#include <algorithm>

namespace gt3r::render {

bool SpriteTexture::create(SDL_Renderer* r, GpuContext* gpu, int w, int h, const u32* rgba) {
    release();
    if (w <= 0 || h <= 0 || !rgba) return false;
    if (r) {
        sdl_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (sdl_) {
            SDL_UpdateTexture(sdl_, nullptr, rgba, w * 4);
            SDL_SetTextureBlendMode(sdl_, SDL_BLENDMODE_ADD);
            SDL_SetTextureScaleMode(sdl_, SDL_SCALEMODE_LINEAR);
        }
    }
    if (gpu) {
        Uploader up(gpu->device());
        gpu_ = up.texture_rgba(u32(w), u32(h), rgba, false);
        if (!up.submit() && gpu_) {
            SDL_ReleaseGPUTexture(gpu->device(), gpu_);
            gpu_ = nullptr;
        }
        if (gpu_) device_ = gpu->device();
        else LOG_WARN("Sprite texture %dx%d: %s", w, h, SDL_GetError());
    }
    return valid();
}

void SpriteTexture::release() {
    if (sdl_) SDL_DestroyTexture(sdl_);
    if (gpu_ && device_) SDL_ReleaseGPUTexture(device_, gpu_);
    sdl_ = nullptr, gpu_ = nullptr, device_ = nullptr;
}

void ScreenBatch::add(const SpriteTexture* texture, const Vertex* vertices, size_t count, const u32* indices,
                      size_t index_count, int passes, BlendMode blend) {
    if (!count || index_count < 3 || passes <= 0) return;
    const u32 base = u32(verts.size()), first = u32(idx.size());
    verts.insert(verts.end(), vertices, vertices + count);
    for (size_t i = 0; i + 2 < index_count; i += 3)
        for (size_t k = 0; k < 3; ++k) idx.push_back(base + std::min(indices[i + k], u32(count - 1)));
    const u32 n = u32(idx.size()) - first;
    // Draws of the same texture, blend and weight in a row join.
    if (!draws.empty()) {
        Draw& d = draws.back();
        if (d.texture == texture && d.passes == passes && d.blend == blend && d.first + d.count == first) {
            d.count += n;
            return;
        }
    }
    draws.push_back({texture, first, n, passes, blend});
}

void ScreenBatch::add_rect(const SpriteTexture* texture, const SDL_FRect& dst, const float rgba[4]) {
    Vertex v[4];
    const float xs[4] = {dst.x, dst.x + dst.w, dst.x, dst.x + dst.w}, ys[4] = {dst.y, dst.y, dst.y + dst.h, dst.y + dst.h};
    for (int i = 0; i < 4; ++i) {
        v[i].x = xs[i], v[i].y = ys[i];
        v[i].u = (i & 1) ? 1.0f : 0.0f, v[i].v = (i & 2) ? 1.0f : 0.0f;
        std::copy(rgba, rgba + 4, v[i].rgba);
    }
    const u32 tris[6] = {0, 1, 2, 1, 3, 2};
    add(texture, v, 4, tris, 6);
}

void submit_2d(SDL_Renderer* r, const ScreenBatch& b) {
    if (b.empty()) return;
    SDL_BlendMode prev = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(r, &prev);
    std::vector<SDL_Vertex> sv;
    std::vector<int> si;
    for (const ScreenBatch::Draw& d : b.draws) {
        sv.clear(), si.clear();
        for (u32 i = d.first; i < d.first + d.count && i < b.idx.size(); ++i) {
            const ScreenBatch::Vertex& v = b.verts[b.idx[i]];
            SDL_Vertex o{};
            o.position = {v.x, v.y};
            o.color = {v.rgba[0], v.rgba[1], v.rgba[2], v.rgba[3]};
            o.tex_coord = {v.u, v.v};
            sv.push_back(o);
            si.push_back(int(si.size()));
        }
        SDL_Texture* t = d.texture ? d.texture->sdl() : nullptr;
        const SDL_BlendMode mode = d.blend == BlendMode::Alpha ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_ADD;
        if (t) SDL_SetTextureBlendMode(t, mode);
        SDL_SetRenderDrawBlendMode(r, mode);
        for (int p = 0; p < d.passes; ++p) SDL_RenderGeometry(r, t, sv.data(), int(sv.size()), si.data(), int(si.size()));
        if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
    }
    SDL_SetRenderDrawBlendMode(r, prev);
}

} // namespace gt3r::render
