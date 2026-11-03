#include "render/gamma_pass.h"

#include "base/log.h"
#include "render/gpu_context.h"
#include "render/shaders/generated/gamma_frag.h"
#include "render/shaders/generated/gamma_vert.h"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace gt3r::render {

float parse_gamma(std::string_view text) {
    const std::string s(text);
    char* end = nullptr;
    const float g = std::strtof(s.c_str(), &end);
    if (end == s.c_str() || !std::isfinite(g)) return 1.0f;
    return std::clamp(g, kGammaMin, kGammaMax);
}

bool drawing_to_window(SDL_Renderer* r) {
    SDL_Texture* t = SDL_GetRenderTarget(r);
    return !t || t == SDL_GetPointerProperty(SDL_GetRendererProperties(r), kWindowTargetProperty, nullptr);
}

GammaPass::GammaPass(GpuContext& ctx) : ctx_(ctx), renderer_(ctx.renderer2d()) {}

GammaPass::~GammaPass() {
    release_targets();
    SDL_GPUDevice* dev = ctx_.device();
#if SDL_VERSION_ATLEAST(3, 4, 0)
    if (state_) SDL_DestroyGPURenderState(state_);
#endif
    if (pipeline_) SDL_ReleaseGPUGraphicsPipeline(dev, pipeline_);
    if (indices_) SDL_ReleaseGPUBuffer(dev, indices_);
    if (vs_) SDL_ReleaseGPUShader(dev, vs_);
    if (fs_) SDL_ReleaseGPUShader(dev, fs_);
}

bool GammaPass::create_shaders() {
    if (fs_) return true;
    fs_ = ctx_.create_shader(GT3R_SHADER_CODE(gamma_frag), false, 1, 1);
    if (!fs_) return false;
    if (!ctx_.gles()) {
#if SDL_VERSION_ATLEAST(3, 4, 0)
        SDL_GPURenderStateCreateInfo si{};
        si.fragment_shader = fs_;
        state_ = SDL_CreateGPURenderState(renderer_, &si);
        if (!state_) LOG_WARN("Gamma: no render state for the 2D renderer: %s", SDL_GetError());
#endif
        return state_ != nullptr;
    }
    // OpenGL ES: a pipeline drawing gamma.vert's one triangle over the output.
    vs_ = ctx_.create_shader(GT3R_SHADER_CODE(gamma_vert), true, 0, 0);
    if (!vs_) return false;
    SDL_GPUColorTargetDescription target{};
    target.format = GpuContext::kColorFormat;
    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.vertex_shader = vs_;
    pi.fragment_shader = fs_;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.target_info.color_target_descriptions = &target;
    pi.target_info.num_color_targets = 1;
    pipeline_ = SDL_CreateGPUGraphicsPipeline(ctx_.device(), &pi);
    Uploader up(ctx_.device());
    static const Uint16 kTriangle[4] = {0, 1, 2, 0}; // (a multiple of 4 bytes)
    indices_ = up.buffer(SDL_GPU_BUFFERUSAGE_INDEX, kTriangle, sizeof(kTriangle));
    if (!up.submit() || !pipeline_ || !indices_) {
        LOG_WARN("Gamma: no OpenGL ES pass: %s", SDL_GetError());
        return false;
    }
    return true;
}

void GammaPass::release_targets() {
    SDL_SetPointerProperty(SDL_GetRendererProperties(renderer_), kWindowTargetProperty, nullptr);
    if (frame_tex_) SDL_DestroyTexture(frame_tex_);
    if (out_tex_) SDL_DestroyTexture(out_tex_);
    if (frame_) SDL_ReleaseGPUTexture(ctx_.device(), frame_);
    if (out_) SDL_ReleaseGPUTexture(ctx_.device(), out_);
    frame_tex_ = out_tex_ = nullptr;
    frame_ = out_ = nullptr;
    w_ = h_ = 0;
}

bool GammaPass::ensure_targets(int w, int h) {
    if (frame_tex_ && w == w_ && h == h_) return true;
    if (w == failed_w_ && h == failed_h_) return false;
    release_targets();
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = GpuContext::kColorFormat;
    ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = Uint32(w), ti.height = Uint32(h);
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    frame_ = SDL_CreateGPUTexture(ctx_.device(), &ti);
    if (frame_) frame_tex_ = wrap_texture(renderer_, frame_, w, h, SDL_TEXTUREACCESS_TARGET);
    if (ctx_.gles()) {
        out_ = SDL_CreateGPUTexture(ctx_.device(), &ti);
        if (out_) out_tex_ = wrap_texture(renderer_, out_, w, h);
    }
    if (!frame_tex_ || (ctx_.gles() && !out_tex_)) {
        LOG_WARN("Gamma: no %dx%d frame target: %s", w, h, SDL_GetError());
        release_targets();
        failed_w_ = w, failed_h_ = h; // tried again at another size
        return false;
    }
    w_ = w, h_ = h;
    SDL_SetPointerProperty(SDL_GetRendererProperties(renderer_), kWindowTargetProperty, frame_tex_);
    return true;
}

bool GammaPass::begin(int w, int h) {
    if (failed_ || w <= 0 || h <= 0) return false;
    if (!create_shaders()) {
        failed_ = true;
        return false;
    }
    if (!ensure_targets(w, h)) return false;
    int lw = 0, lh = 0, tw = 0, th = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED, tmode = mode;
    SDL_GetRenderLogicalPresentation(renderer_, &lw, &lh, &mode); // the window's
    SDL_SetRenderTarget(renderer_, frame_tex_);
    SDL_GetRenderLogicalPresentation(renderer_, &tw, &th, &tmode); // (each target keeps its own)
    if (tw != lw || th != lh || tmode != mode) SDL_SetRenderLogicalPresentation(renderer_, lw, lh, mode);
    return true;
}

void GammaPass::end(float gamma) {
    SDL_SetRenderTarget(renderer_, nullptr);
    // Over the whole window in pixels, its letterbox included; an explicit clip, since SDL's GPU
    // renderer can keep a stale scissor from a clip turned off earlier.
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_GetRenderLogicalPresentation(renderer_, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    const SDL_Rect whole{0, 0, w_, h_};
    SDL_SetRenderClipRect(renderer_, &whole);
    const float uniforms[4] = {1.0f / gamma, 0, 0, 0};
    if (ctx_.gles()) {
        // The 2D renderer's frame (flushed when the pass runs) into out_, then out_ to the window.
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(ctx_.device());
        SDL_GPUColorTargetInfo ct{};
        ct.texture = out_;
        ct.load_op = SDL_GPU_LOADOP_DONT_CARE;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = cmd ? SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr) : nullptr;
        if (pass) {
            SDL_BindGPUGraphicsPipeline(pass, pipeline_);
            const SDL_GPUBufferBinding ib{indices_, 0};
            SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
            const SDL_GPUTextureSamplerBinding sb{frame_, ctx_.sampler(true, true, true)};
            SDL_BindGPUFragmentSamplers(pass, 0, &sb, 1);
            SDL_PushGPUFragmentUniformData(cmd, 0, uniforms, sizeof(uniforms));
            SDL_DrawGPUIndexedPrimitives(pass, 3, 1, 0, 0, 0);
            SDL_EndGPURenderPass(pass);
        }
        if (cmd) SDL_SubmitGPUCommandBuffer(cmd);
        SDL_RenderTexture(renderer_, out_tex_, nullptr, nullptr);
    } else {
#if SDL_VERSION_ATLEAST(3, 4, 0)
        SDL_SetGPURenderStateFragmentUniforms(state_, 0, uniforms, sizeof(uniforms));
        SDL_SetGPURenderState(renderer_, state_);
        SDL_RenderTexture(renderer_, frame_tex_, nullptr, nullptr);
        SDL_SetGPURenderState(renderer_, nullptr);
#endif
    }
    SDL_SetRenderClipRect(renderer_, nullptr);
    SDL_SetRenderLogicalPresentation(renderer_, lw, lh, mode);
}

} // namespace gt3r::render
