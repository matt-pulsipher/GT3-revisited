#pragma once

// SDL_GPU device shared by the 3D renderer and the 2D UI renderer.
//
// Frame structure: the window's swapchain belongs to the 2D SDL_Renderer
// (SDL's "gpu" render driver, created on our device with
// SDL_CreateGPURenderer). 3D views render into offscreen GPU textures in
// their own command buffers, which are submitted before the 2D renderer's;
// the 2D renderer then composites each view as an ordinary SDL_Texture that
// wraps the GPU texture, and draws the UI on top. Screenshots via
// SDL_RenderReadPixels therefore include the 3D layer.
//
// Requires SDL >= 3.4 (SDL_CreateGPURenderer, GPU texture wrapping). With
// older SDL, or when no backend supports our shaders, create() returns
// nullptr and the app runs 2D-only.

#include "base/bytes.h"
#include "xr/xr_session.h" // VR builds (GT3R_XR)

#include <SDL3/SDL.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gt3r::render {

enum class BlendMode : u8 {
    Opaque,
    Alpha,
    Additive,
    MultiplyDest, // dst * src alpha
    DestAlphaAdd, // GS ALPHA 0x58: Cs * Ad + Cd (frame-buffer alpha as the weight)
    DestAlphaLerp, // GS ALPHA 0x54: (Cs - Cd) * Ad + Cd
};
// What a draw does to the frame-buffer alpha (GS semantics: a draw writes its source alpha,
// unblended, unless FBMSK keeps it).
enum class AlphaWrite : u8 {
    Keep,  // never written (the default: the target stays opaque)
    Write, // the fragment's alpha replaces it, colour as the blend mode says
    Only,  // only the alpha is written (GS FBMSK 0x00FFFFFF)
};
enum class CullMode : u8 { None, Back, Front };

struct PipelineKey {
    BlendMode blend = BlendMode::Opaque;
    CullMode cull = CullMode::Back;
    bool depth_write = true;
    bool lines = false; // line list (wireframe overlay)
    // Write the alpha channel as coverage (pictures composited over the 2D frame with a
    // transparent clear); otherwise alpha is never written.
    bool coverage = false;
    AlphaWrite alpha = AlphaWrite::Keep;
    auto operator<=>(const PipelineKey&) const = default;
};

struct GpuOptions {
    int msaa = 4;       // 1, 2, 4 or 8; clamped to what the device supports
    bool vsync = true;
    bool debug = false; // backend validation layers
    float render_scale = 1.0f; // 3D resolution relative to the display (0.5..4); the UI stays native
    int anisotropy = 4;        // anisotropic texture filtering: 1 (off), 2, 4, 8 or 16
    // VR builds (GT3R_XR): create the device through the headset's OpenXR runtime when there is
    // one; the 2D renderer then draws offscreen (canvases of xr::Session) instead of the window.
    bool xr = false;
};

class Uploader;

class GpuContext {
public:
    // Creates the device, claims the window and creates the 2D renderer on it.
    static std::unique_ptr<GpuContext> create(SDL_Window* window, const GpuOptions& options);
    ~GpuContext();
    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;

    SDL_GPUDevice* device() const { return device_; }
    SDL_Renderer* renderer2d() const { return renderer_; }
#if GT3R_XR
    // Set when the device was created for a headset (see GpuOptions::xr).
    bool xr() const { return xr_; }
    const xr::Session::DeviceRequest& xr_request() const { return xr_request_; }
#endif
    const char* driver_name() const { return SDL_GetGPUDeviceDriver(device_); }

    // Offscreen 3D formats.
    static constexpr SDL_GPUTextureFormat kColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUTextureFormat depth_format() const { return depth_format_; }
    SDL_GPUSampleCount sample_count() const { return samples_; }
    int msaa() const { return 1 << int(samples_); }
    // The MSAA levels the GPU draws the 3D pass with (1 = off), lowest first.
    std::vector<int> msaa_levels() const;
    // Changes the MSAA level (the nearest the GPU offers: a missing 2x is 4x, others round down);
    // drops cached pipelines.
    void set_msaa(int samples);
    // Anisotropic filtering of mipmapped textures: 1 (off), 2, 4, 8 or 16 (1 when the GPU has
    // none). set_anisotropy recreates the samplers.
    bool anisotropy_supported() const { return anisotropy_; }
    int anisotropy() const { return anisotropy_ ? aniso_level_ : 1; }
    void set_anisotropy(int level);
    // 3D views render at this fraction of their on-screen pixel size and are upscaled when
    // composited (cheaper on high-resolution phones/tablets); above 1 (supersampling) they
    // render larger and are scaled down (View3D::draw).
    float render_scale() const { return render_scale_; }
    void set_render_scale(float s);
    // The largest scale the views size their targets for (0: render_scale), so the dynamic scale
    // can move under it without new targets.
    float render_ceiling() const { return render_ceiling_; }
    void set_render_ceiling(float s);

    SDL_GPUGraphicsPipeline* pipeline(const PipelineKey& key);
    // Creates the pipelines races and menus draw with on a background thread, ahead of their
    // first use: one takes ~65 ms on a Galaxy A15 (Mali), a stutter the first time each blend /
    // cull / write combination appears in a session.
    void warm_pipelines();
    // no_mips: the full-size image only (GS MXL 0), linear, no anisotropy.
    SDL_GPUSampler* sampler(bool clamp_u, bool clamp_v, bool no_mips = false) const {
        return samplers_[(clamp_u ? 1 : 0) | (clamp_v ? 2 : 0) | (no_mips ? 4 : 0)];
    }
    SDL_GPUTexture* white_texture() const { return white_; }

    // Mipmapped RGBA8 textures shared by content: identical texels are one GPU texture,
    // counted per holder (GpuModel). GT3 repeats most textures across LOD sets and models (a
    // race: 3,674 uploads, 839 distinct), and the Galaxy A15's Mali driver spends ~185 KB of
    // memory on every texture object besides its texels. New ones upload with `up`.
    SDL_GPUTexture* acquire_texture(Uploader& up, u32 width, u32 height, const u32* rgba);
    // Drops a hold from acquire_texture; other textures are released outright.
    void release_texture(SDL_GPUTexture* texture);
    // Before rewriting a texture's texels: takes it out of the sharing when the caller is its
    // only holder (true; it is then the caller's own, released with release_texture as before).
    // False when others hold it too: the caller needs a texture of its own.
    bool take_texture(SDL_GPUTexture* texture);

    // After each presented frame: SDL has recycled the finished GPU work (see PendingGpuWork).
    void frame_presented();

private:
    GpuContext() = default;
    bool init(SDL_Window* window, const GpuOptions& options);
    SDL_GPUShader* make_shader(bool vertex);
    bool create_samplers();
    void release_samplers();
    SDL_GPUGraphicsPipeline* create_pipeline(const PipelineKey& key);
    bool build_pipeline(const PipelineKey& key); // into pipelines_ unless there; true when built
    void stop_warming();
    void release_pipelines();

    SDL_GPUDevice* device_ = nullptr;
    bool anisotropy_ = true; // the device was created with anisotropic filtering
    int aniso_level_ = 4;    // the samplers' anisotropy (when anisotropy_)
#if GT3R_XR
    bool xr_ = false;
    xr::Session::DeviceRequest xr_request_;
#endif
    float render_scale_ = 1.0f;
    float render_ceiling_ = 0.0f;
    SDL_Renderer* renderer_ = nullptr;
    SDL_GPUShader* vs_ = nullptr;
    SDL_GPUShader* fs_ = nullptr;
    SDL_GPUSampler* samplers_[8] = {};
    SDL_GPUTexture* white_ = nullptr;
    SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    SDL_GPUSampleCount samples_ = SDL_GPU_SAMPLECOUNT_1;
    bool msaa_supported(SDL_GPUSampleCount c) const; // both the color and the depth target
    std::map<PipelineKey, SDL_GPUGraphicsPipeline*> pipelines_;
    std::mutex pipelines_mutex_; // pipelines_ (warm_pipelines' thread adds to it)
    std::thread warm_thread_;
    std::atomic<bool> warm_stop_{false};
    bool warm_ = false; // warm_pipelines was asked for (again after an MSAA change)
    struct TextureKey {
        u32 width, height;
        u64 hash[2]; // two independent 64-bit hashes of the texels
        auto operator<=>(const TextureKey&) const = default;
    };
    struct SharedTexture {
        TextureKey key;
        u32 holders;
    };
    std::map<TextureKey, SDL_GPUTexture*> textures_by_key_;
    std::map<SDL_GPUTexture*, SharedTexture> shared_textures_;
};

// Mip chain of a w x h texture: levels down to 1 x 1, each side halving to no less than 1.
u32 mip_levels(u32 w, u32 h);
// The next mip level of RGBA8 texels: max(1, w / 2) x max(1, h / 2), each texel the rounded
// mean of its 2 x 2 block (what a linear blit of the level above gives).
std::vector<u32> half_size(const u32* rgba, u32 w, u32 h);

// GPU work submitted since the last presented frame. With a window, SDL's Vulkan backend
// recycles finished command buffers (their fences, transfer buffers and the resources released
// meanwhile) only when a frame is presented, and every texture it creates is a submit of its
// own. A load presents nothing: a 6-car race load kept ~1,800 command buffers and fences in
// flight at once (RTX 3080), and a Linux NVIDIA laptop failed race loads with vkCreateFence
// VK_ERROR_OUT_OF_HOST_MEMORY. Past either limit the uploads wait for the GPU, which recycles
// it all (SDL_WaitForGPUIdle).
class PendingGpuWork {
public:
    static constexpr u32 kMaxSubmits = 128;
    static constexpr u64 kMaxBytes = 64ull << 20; // staged upload data
    // True when the caller should wait for the GPU now (the count starts again).
    bool add(u32 submits, u64 bytes);
    void frame_presented() { submits_ = 0, bytes_ = 0; }

private:
    u32 submits_ = 0;
    u64 bytes_ = 0;
};

// After submits besides the frame's own (uploads, texture creation): waits for the GPU when
// the process's PendingGpuWork says so (one GPU device per process).
void note_gpu_submits(SDL_GPUDevice* device, u32 submits, u64 bytes = 0);

// Batches buffer/texture uploads into one command buffer and one transfer buffer:
//   Uploader up(device); auto* vb = up.buffer(VERTEX, data, size); ...; up.submit();
// Created resources are owned by the caller. Mipmaps are built on the CPU, never with
// SDL_GenerateMipmapsForGPUTexture: its Vulkan blits go zero-sized on non-square textures,
// a GPU fault that loses the device on Mali (e.g. Galaxy A15).
class Uploader {
public:
    explicit Uploader(SDL_GPUDevice* device);
    SDL_GPUBuffer* buffer(SDL_GPUBufferUsageFlags usage, const void* data, u32 size);
    SDL_GPUTexture* texture_rgba(u32 width, u32 height, const u32* rgba, bool mipmaps);
    // New texels for a texture made by texture_rgba (same size and mipmaps). cycle: frames
    // still drawing keep the old texels.
    void update_rgba(SDL_GPUTexture* texture, u32 width, u32 height, const u32* rgba, bool mipmaps, bool cycle);
    bool submit();

private:
    struct Pending {
        SDL_GPUBuffer* buffer;
        SDL_GPUTexture* texture;
        u32 offset, size; // in staging_
        u32 width, height, level;
        bool cycle;
    };
    u32 stage(const void* data, u32 size);
    SDL_GPUDevice* device_;
    std::vector<Pending> pending_;
    // Every upload's data, copied into a single transfer buffer at submit(): SDL's Vulkan
    // backend gives each transfer buffer its own device memory allocation.
    std::vector<u8> staging_;
};

// True when this build's SDL supports the shared GPU device path.
bool gpu_renderer_supported();

} // namespace gt3r::render
