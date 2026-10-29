#include "render/gpu_context.h"

#include "base/log.h"
#include "platform/render_scale.h"
#include "render/shaders/generated/model_frag.h"
#include "render/shaders/generated/model_vert.h"
#include "scene/model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#ifdef __ANDROID__
#include <sys/prctl.h>
#endif

#define GT3R_HAVE_GPU_RENDERER SDL_VERSION_ATLEAST(3, 4, 0)

namespace gt3r::render {

bool gpu_renderer_supported() { return GT3R_HAVE_GPU_RENDERER; }

std::unique_ptr<GpuContext> GpuContext::create(SDL_Window* window, const GpuOptions& options) {
#if GT3R_HAVE_GPU_RENDERER
    std::unique_ptr<GpuContext> ctx(new GpuContext());
    if (!ctx->init(window, options)) return nullptr;
    return ctx;
#else
    (void)window;
    (void)options;
    LOG_WARN("3D renderer disabled: built against SDL %d.%d.%d, needs 3.4 or later", SDL_MAJOR_VERSION,
             SDL_MINOR_VERSION, SDL_MICRO_VERSION);
    return nullptr;
#endif
}

bool GpuContext::init(SDL_Window* window, const GpuOptions& options) {
#if GT3R_HAVE_GPU_RENDERER
    // Ask only for the optional Vulkan features the renderer uses. SDL requests clip distances,
    // depth clamping and first-instance indirect draws by default, and rejects every GPU without
    // them, e.g. Mali (Pixel, Galaxy, Tensor phones), which lacks shaderClipDistance. We clip
    // depth normally, draw directly and write no clip distances. Anisotropic filtering is the one
    // we use: a GPU without it gets a second try with it off (anisotropy_ = false).
#if GT3R_XR
    // A headset: one attempt through its OpenXR runtime (the Quest runtime cannot be set up a
    // second time in a process), with the same features as below.
    if (options.xr) {
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, options.debug);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, false);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, true);
        if (xr::Session::prepare_device(props, xr_request_)) {
            device_ = SDL_CreateGPUDeviceWithProperties(props);
            if (device_) xr_ = true;
            else LOG_INFO("VR: no headset (%s); using the window", SDL_GetError());
        }
        SDL_DestroyProperties(props);
    }
    for (bool aniso : {true, false}) {
        if (device_) break;
#else
    for (bool aniso : {true, false}) {
#endif
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, options.debug);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, false);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, aniso);
        device_ = SDL_CreateGPUDeviceWithProperties(props);
        SDL_DestroyProperties(props);
        if (device_) {
            anisotropy_ = aniso;
            if (!aniso) LOG_INFO("3D renderer: anisotropic filtering unavailable on this GPU");
            break;
        }
    }
    if (!device_) {
        LOG_WARN("No SDL_GPU device: %s", SDL_GetError());
        return false;
    }
    const SDL_PropertiesID device_props = SDL_GetGPUDeviceProperties(device_);
    const char* gpu_name = SDL_GetStringProperty(device_props, SDL_PROP_GPU_DEVICE_NAME_STRING, "");
    const char* driver = SDL_GetStringProperty(device_props, SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING, nullptr);
    if (!driver) driver = SDL_GetStringProperty(device_props, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "");
    LOG_INFO("3D renderer: GPU %s%s%s", gpu_name, *driver ? ", driver " : "", driver);
    // The 2D renderer claims the window and owns the swapchain (a headset: offscreen, it draws
    // into the session's canvases and the headset sets the pace).
    SDL_Window* target = window;
#if GT3R_XR
    if (xr_) target = nullptr;
#endif
    renderer_ = SDL_CreateGPURenderer(device_, target);
    if (!renderer_) {
        LOG_WARN("SDL_CreateGPURenderer failed: %s", SDL_GetError());
        return false;
    }
    if (target) SDL_SetRenderVSync(renderer_, options.vsync ? 1 : 0);

    vs_ = make_shader(true);
    fs_ = make_shader(false);
    if (!vs_ || !fs_) return false;

    for (SDL_GPUTextureFormat f :
         {SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D24_UNORM, SDL_GPU_TEXTUREFORMAT_D16_UNORM})
        if (SDL_GPUTextureSupportsFormat(device_, f, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
            depth_format_ = f;
            break;
        }
    set_msaa(options.msaa);
    set_render_scale(options.render_scale);
    aniso_level_ = std::clamp(options.anisotropy, 1, 16);
    if (!create_samplers()) return false;

    Uploader up(device_);
    const u32 white = 0xFFFFFFFFu;
    white_ = up.texture_rgba(1, 1, &white, false);
    if (!white_ || !up.submit()) return false;

    LOG_INFO("3D renderer: SDL_GPU %s, MSAA %dx, anisotropy %dx, depth format %d, render scale %.2f", driver_name(),
             msaa(), anisotropy(), int(depth_format_), double(render_scale_));
    return true;
#else
    (void)window;
    (void)options;
    return false;
#endif
}

GpuContext::~GpuContext() {
    if (device_) {
        SDL_WaitForGPUIdle(device_);
        release_pipelines();
        release_samplers();
        if (white_) SDL_ReleaseGPUTexture(device_, white_);
        for (auto& [t, shared] : shared_textures_) SDL_ReleaseGPUTexture(device_, t);
        if (vs_) SDL_ReleaseGPUShader(device_, vs_);
        if (fs_) SDL_ReleaseGPUShader(device_, fs_);
    }
    if (renderer_) SDL_DestroyRenderer(renderer_); // releases the window claim
    if (device_) SDL_DestroyGPUDevice(device_);
}

SDL_GPUShader* GpuContext::make_shader(bool vertex) {
    SDL_GPUShaderCreateInfo info{};
    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device_);
    if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = vertex ? shaders::model_vert_spv : shaders::model_frag_spv;
        info.code_size = vertex ? sizeof(shaders::model_vert_spv) : sizeof(shaders::model_frag_spv);
        info.entrypoint = "main";
    } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        const char* src = vertex ? shaders::model_vert_msl : shaders::model_frag_msl;
        info.code = reinterpret_cast<const Uint8*>(src);
        info.code_size = std::strlen(src);
        info.entrypoint = "main0";
    } else {
        LOG_WARN("GPU driver %s accepts none of our shader formats", driver_name());
        return nullptr;
    }
    info.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = vertex ? 0 : 3; // fragment: the texture, the environment cube, the sphere map
    info.num_uniform_buffers = vertex ? 2 : 1; // vertex: transforms, GT3 per-vertex lighting
    SDL_GPUShader* s = SDL_CreateGPUShader(device_, &info);
    if (!s) LOG_WARN("Shader creation failed: %s", SDL_GetError());
    return s;
}

// Eight samplers: the clamp_u / clamp_v combinations, each with mipmaps (trilinear, anisotropic
// at aniso_level_) or without (the base level only, GS MXL 0).
bool GpuContext::create_samplers() {
    for (int i = 0; i < 8; ++i) {
        const bool no_mips = (i & 4) != 0;
        const bool aniso = anisotropy_ && !no_mips && aniso_level_ > 1;
        SDL_GPUSamplerCreateInfo si{};
        si.min_filter = SDL_GPU_FILTER_LINEAR;
        si.mag_filter = SDL_GPU_FILTER_LINEAR;
        si.mipmap_mode = no_mips ? SDL_GPU_SAMPLERMIPMAPMODE_NEAREST : SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        si.address_mode_u = (i & 1) ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        si.address_mode_v = (i & 2) ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        si.max_lod = no_mips ? 0.0f : 1000.0f;
        si.enable_anisotropy = aniso;
        si.max_anisotropy = aniso ? float(aniso_level_) : 1.0f;
        samplers_[i] = SDL_CreateGPUSampler(device_, &si);
        if (!samplers_[i]) {
            LOG_WARN("Sampler creation failed: %s", SDL_GetError());
            return false;
        }
    }
    return true;
}

void GpuContext::release_samplers() {
    for (SDL_GPUSampler*& s : samplers_) {
        if (s) SDL_ReleaseGPUSampler(device_, s);
        s = nullptr;
    }
}

void GpuContext::set_anisotropy(int level) {
    level = level >= 16 ? 16 : level >= 8 ? 8 : level >= 4 ? 4 : level >= 2 ? 2 : 1;
    if (level == aniso_level_) return;
    const int before = aniso_level_;
    aniso_level_ = level;
    if (!anisotropy_) return; // remembered; the samplers have none
    SDL_WaitForGPUIdle(device_);
    release_samplers();
    if (!create_samplers()) { // keep drawing with the previous level
        release_samplers();
        aniso_level_ = before;
        create_samplers();
    }
}

void GpuContext::set_render_scale(float s) {
    render_scale_ = std::isfinite(s) ? std::clamp(s, 0.5f, platform::kMaxRenderScale) : 1.0f;
}

void GpuContext::set_render_ceiling(float s) {
    render_ceiling_ = std::isfinite(s) ? std::clamp(s, 0.0f, platform::kMaxRenderScale) : 0.0f;
}

bool GpuContext::msaa_supported(SDL_GPUSampleCount c) const {
    return SDL_GPUTextureSupportsSampleCount(device_, kColorFormat, c) &&
           SDL_GPUTextureSupportsSampleCount(device_, depth_format_, c);
}

std::vector<int> GpuContext::msaa_levels() const {
    std::vector<int> levels{1};
    for (SDL_GPUSampleCount c : {SDL_GPU_SAMPLECOUNT_2, SDL_GPU_SAMPLECOUNT_4, SDL_GPU_SAMPLECOUNT_8})
        if (msaa_supported(c)) levels.push_back(1 << int(c));
    return levels;
}

void GpuContext::set_msaa(int samples) {
    SDL_GPUSampleCount want = samples >= 8   ? SDL_GPU_SAMPLECOUNT_8
                              : samples >= 4 ? SDL_GPU_SAMPLECOUNT_4
                              : samples >= 2 ? SDL_GPU_SAMPLECOUNT_2
                                             : SDL_GPU_SAMPLECOUNT_1;
    // The nearest count the GPU offers: a missing 2x becomes 4x (Mali GPUs offer 1x, 4x and 8x
    // only, and 4x cost a Galaxy A15 about 5 points of dynamic resolution), others the next lower.
    if (want == SDL_GPU_SAMPLECOUNT_2 && !msaa_supported(want) && msaa_supported(SDL_GPU_SAMPLECOUNT_4))
        want = SDL_GPU_SAMPLECOUNT_4;
    while (want != SDL_GPU_SAMPLECOUNT_1 && !msaa_supported(want)) want = SDL_GPUSampleCount(int(want) - 1);
    if (want != samples_) {
        SDL_WaitForGPUIdle(device_);
        release_pipelines();
        samples_ = want;
        if (warm_) warm_pipelines(); // the sample count is part of every pipeline
    }
}

void GpuContext::stop_warming() {
    warm_stop_ = true;
    if (warm_thread_.joinable()) warm_thread_.join();
}

void GpuContext::release_pipelines() {
    stop_warming();
    std::lock_guard lock(pipelines_mutex_);
    for (auto& [key, p] : pipelines_) SDL_ReleaseGPUGraphicsPipeline(device_, p);
    pipelines_.clear();
}

namespace {

// The combinations five courses and the menus drew with: opaque and alpha blending with every
// cull, depth write and frame-alpha mode; the other blends without depth or alpha writes. Then
// the same for transparent pictures (coverage, no frame alpha): car models on the race's gap
// pop-ups and results bar, the online pickers' cars. A pop-up's first car built three of these
// mid-race, a 0.5 s stall on a Moto G Power 2024. Others are still created on first use.
std::vector<PipelineKey> warm_keys() {
    std::vector<PipelineKey> keys;
    for (bool coverage : {false, true}) {
        const std::vector<AlphaWrite> alphas = coverage ? std::vector<AlphaWrite>{AlphaWrite::Keep}
                                                        : std::vector<AlphaWrite>{AlphaWrite::Keep, AlphaWrite::Write,
                                                                                  AlphaWrite::Only};
        for (BlendMode b : {BlendMode::Opaque, BlendMode::Alpha})
            for (bool depth_write : {true, false})
                for (CullMode c : {CullMode::Back, CullMode::None, CullMode::Front})
                    for (AlphaWrite a : alphas) {
                        PipelineKey k;
                        k.blend = b, k.depth_write = depth_write, k.cull = c, k.alpha = a, k.coverage = coverage;
                        keys.push_back(k);
                    }
        for (BlendMode b :
             {BlendMode::Additive, BlendMode::DestAlphaAdd, BlendMode::MultiplyDest, BlendMode::DestAlphaLerp})
            for (CullMode c : {CullMode::Back, CullMode::None, CullMode::Front}) {
                PipelineKey k;
                k.blend = b, k.depth_write = false, k.cull = c, k.coverage = coverage;
                keys.push_back(k);
            }
    }
    return keys;
}

} // namespace

void GpuContext::warm_pipelines() {
    stop_warming();
    warm_ = true;
    warm_stop_ = false;
    warm_thread_ = std::thread([this, keys = warm_keys()] {
        const u64 t0 = SDL_GetTicksNS();
        int made = 0;
        for (const PipelineKey& k : keys) {
            if (warm_stop_) return;
            made += build_pipeline(k);
        }
        LOG_INFO("3D pipelines warmed: %d in %.0f ms", made, double(SDL_GetTicksNS() - t0) / 1e6);
    });
}

bool GpuContext::build_pipeline(const PipelineKey& key) {
    {
        std::lock_guard lock(pipelines_mutex_);
        if (pipelines_.count(key)) return false;
    }
    SDL_GPUGraphicsPipeline* p = create_pipeline(key);
    std::lock_guard lock(pipelines_mutex_);
    if (!pipelines_.emplace(key, p).second && p) SDL_ReleaseGPUGraphicsPipeline(device_, p); // built meanwhile
    return true;
}

SDL_GPUGraphicsPipeline* GpuContext::pipeline(const PipelineKey& key) {
    {
        std::lock_guard lock(pipelines_mutex_);
        auto it = pipelines_.find(key);
        if (it != pipelines_.end()) return it->second;
    }
    // Not warmed: built here, on the render thread, which stalls the frame (~200 ms per
    // pipeline on some phone GPUs).
    const u64 t0 = SDL_GetTicksNS();
    SDL_GPUGraphicsPipeline* p = create_pipeline(key);
    LOG_INFO("3D pipeline built on first use in %.0f ms: blend %d, cull %d, depth write %d, lines %d, coverage %d, "
             "alpha %d",
             double(SDL_GetTicksNS() - t0) / 1e6, int(key.blend), int(key.cull), int(key.depth_write), int(key.lines),
             int(key.coverage), int(key.alpha));
    std::lock_guard lock(pipelines_mutex_);
    auto [it, added] = pipelines_.emplace(key, p); // failures too, so we don't retry every frame
    if (!added && p) SDL_ReleaseGPUGraphicsPipeline(device_, p); // the warm-up made it meanwhile
    return it->second;
}

SDL_GPUGraphicsPipeline* GpuContext::create_pipeline(const PipelineKey& key) {
    SDL_GPUVertexBufferDescription vb{};
    vb.slot = 0;
    vb.pitch = sizeof(scene::Vertex);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attrs[4] = {
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, u32(offsetof(scene::Vertex, pos))},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, u32(offsetof(scene::Vertex, normal))},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, u32(offsetof(scene::Vertex, uv))},
        {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, u32(offsetof(scene::Vertex, color))},
    };

    SDL_GPUColorTargetDescription color{};
    color.format = kColorFormat;
    SDL_GPUColorTargetBlendState& b = color.blend_state;
    // Never write alpha: the target is cleared to opaque and must stay opaque, otherwise
    // translucent texels (e.g. car paint layers, alpha-tested foliage) leak into the
    // composited view and into screenshots.
    b.enable_color_write_mask = true;
    b.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
    if (key.coverage) b.color_write_mask |= SDL_GPU_COLORCOMPONENT_A;
    switch (key.blend) {
    case BlendMode::Opaque:
        break;
    case BlendMode::Alpha:
        b.enable_blend = true;
        b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        b.color_blend_op = SDL_GPU_BLENDOP_ADD;
        b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        break;
    case BlendMode::Additive:
        b.enable_blend = true;
        b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        b.color_blend_op = SDL_GPU_BLENDOP_ADD;
        b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        break;
    case BlendMode::MultiplyDest:
        b.enable_blend = true;
        b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        b.color_blend_op = SDL_GPU_BLENDOP_ADD;
        b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        break;
    case BlendMode::DestAlphaAdd:
        b.enable_blend = true;
        b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_ALPHA;
        b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        b.color_blend_op = SDL_GPU_BLENDOP_ADD;
        b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        break;
    case BlendMode::DestAlphaLerp:
        b.enable_blend = true;
        b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_ALPHA;
        b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
        b.color_blend_op = SDL_GPU_BLENDOP_ADD;
        b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        break;
    }
    // GS-style frame alpha: the source alpha replaces the destination's (the GS never blends
    // the alpha channel), with the colour channels written or masked off.
    if (key.alpha != AlphaWrite::Keep) {
        if (!b.enable_blend) { // opaque: the colour as is
            b.enable_blend = true;
            b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            b.color_blend_op = SDL_GPU_BLENDOP_ADD;
        }
        b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        b.color_write_mask = key.alpha == AlphaWrite::Only
                                 ? SDL_GPU_COLORCOMPONENT_A
                                 : SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B |
                                       SDL_GPU_COLORCOMPONENT_A;
    }

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vs_;
    info.fragment_shader = fs_;
    info.vertex_input_state.vertex_buffer_descriptions = &vb;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attrs;
    info.vertex_input_state.num_vertex_attributes = 4;
    info.primitive_type = key.lines ? SDL_GPU_PRIMITIVETYPE_LINELIST : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = key.lines || key.cull == CullMode::None ? SDL_GPU_CULLMODE_NONE
                                      : key.cull == CullMode::Back          ? SDL_GPU_CULLMODE_BACK
                                                                            : SDL_GPU_CULLMODE_FRONT;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = samples_;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = key.depth_write;
    // GT3's depth test, for every pass with or without depth writes: GEQUAL on a reversed depth
    // (render::perspective: 1 near, 0 far), so second passes over the same vertices (decals,
    // reflections) pass on equal depth.
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
    info.target_info.color_target_descriptions = &color;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = depth_format_;
    info.target_info.has_depth_stencil_target = true;

#ifdef __ANDROID__
    // Android's crash reports name the crashing thread: while the driver compiles, the thread is
    // named after the key, in the order of the first-use log (blend, cull, depth write, lines,
    // coverage, alpha), then the MSAA level, e.g. "pipe100000x4".
    char thread_name[16] = {}, key_name[16];
    prctl(PR_GET_NAME, thread_name);
    std::snprintf(key_name, sizeof key_name, "pipe%d%d%d%d%d%dx%d", int(key.blend), int(key.cull),
                  int(key.depth_write), int(key.lines), int(key.coverage), int(key.alpha), msaa());
    prctl(PR_SET_NAME, key_name);
#endif
    SDL_GPUGraphicsPipeline* p = SDL_CreateGPUGraphicsPipeline(device_, &info);
#ifdef __ANDROID__
    prctl(PR_SET_NAME, thread_name);
#endif
    if (!p) LOG_WARN("Pipeline creation failed: %s", SDL_GetError());
    return p;
}

SDL_GPUTexture* GpuContext::acquire_texture(Uploader& up, u32 w, u32 h, const u32* rgba) {
    if (w == 0 || h == 0) return nullptr;
    // FNV-1a and a multiply-rotate mix over the texels: a 128-bit content key.
    TextureKey key{w, h, {14695981039346656037ull, 0x9E3779B97F4A7C15ull}};
    for (size_t i = 0, n = size_t(w) * h; i < n; ++i) {
        key.hash[0] = (key.hash[0] ^ rgba[i]) * 1099511628211ull;
        u64 x = key.hash[1] + rgba[i] * 0xBF58476D1CE4E5B9ull;
        key.hash[1] = ((x << 31) | (x >> 33)) * 0x94D049BB133111EBull;
    }
    if (auto it = textures_by_key_.find(key); it != textures_by_key_.end()) {
        ++shared_textures_[it->second].holders;
        return it->second;
    }
    SDL_GPUTexture* t = up.texture_rgba(w, h, rgba, true);
    if (t) {
        textures_by_key_[key] = t;
        shared_textures_[t] = {key, 1};
    }
    return t;
}

void GpuContext::release_texture(SDL_GPUTexture* t) {
    if (!t) return;
    auto it = shared_textures_.find(t);
    if (it != shared_textures_.end()) {
        if (--it->second.holders > 0) return;
        textures_by_key_.erase(it->second.key);
        shared_textures_.erase(it);
    }
    SDL_ReleaseGPUTexture(device_, t);
}

bool GpuContext::take_texture(SDL_GPUTexture* t) {
    auto it = shared_textures_.find(t);
    if (it == shared_textures_.end()) return true;
    if (it->second.holders > 1) return false;
    textures_by_key_.erase(it->second.key);
    shared_textures_.erase(it);
    return true;
}

// ---------------------------------------------------------------------------

bool PendingGpuWork::add(u32 submits, u64 bytes) {
    submits_ += submits;
    bytes_ += bytes;
    if (submits_ < kMaxSubmits && bytes_ < kMaxBytes) return false;
    frame_presented();
    return true;
}

namespace {
PendingGpuWork& pending_gpu_work() {
    static PendingGpuWork work;
    return work;
}
} // namespace

void note_gpu_submits(SDL_GPUDevice* device, u32 submits, u64 bytes) {
    if (pending_gpu_work().add(submits, bytes)) SDL_WaitForGPUIdle(device);
}

void GpuContext::frame_presented() { pending_gpu_work().frame_presented(); }

Uploader::Uploader(SDL_GPUDevice* device) : device_(device) {}

u32 Uploader::stage(const void* data, u32 size) {
    // Each upload at a 512-byte boundary: D3D12 places texture data that way (SDL copies
    // anything else through a temporary buffer).
    size_t at = (staging_.size() + 511) & ~size_t(511);
    staging_.resize(at + size);
    std::memcpy(staging_.data() + at, data, size);
    return u32(at);
}

SDL_GPUBuffer* Uploader::buffer(SDL_GPUBufferUsageFlags usage, const void* data, u32 size) {
    if (size == 0) return nullptr;
    SDL_GPUBufferCreateInfo bi{};
    bi.usage = usage;
    bi.size = size;
    SDL_GPUBuffer* b = SDL_CreateGPUBuffer(device_, &bi);
    if (!b) return nullptr;
    pending_.push_back({b, nullptr, stage(data, size), size, 0, 0, 0, false});
    return b;
}

u32 mip_levels(u32 w, u32 h) {
    u32 levels = 1;
    while ((std::max(w, h) >> levels) > 0) ++levels;
    return levels;
}

std::vector<u32> half_size(const u32* rgba, u32 w, u32 h) {
    const u32 hw = std::max(1u, w / 2), hh = std::max(1u, h / 2);
    std::vector<u32> out(size_t(hw) * hh);
    for (u32 y = 0; y < hh; ++y)
        for (u32 x = 0; x < hw; ++x) {
            const u32 x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1);
            const u32 y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
            const u32 p[4] = {rgba[size_t(y0) * w + x0], rgba[size_t(y0) * w + x1], rgba[size_t(y1) * w + x0],
                              rgba[size_t(y1) * w + x1]};
            u32 c = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                u32 sum = 2;
                for (u32 q : p) sum += (q >> shift) & 0xFF;
                c |= (sum / 4) << shift;
            }
            out[size_t(y) * hw + x] = c;
        }
    return out;
}

SDL_GPUTexture* Uploader::texture_rgba(u32 w, u32 h, const u32* rgba, bool mipmaps) {
    if (w == 0 || h == 0) return nullptr;
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = GpuContext::kColorFormat;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = w;
    ti.height = h;
    ti.layer_count_or_depth = 1;
    ti.num_levels = mipmaps ? mip_levels(w, h) : 1;
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(device_, &ti);
    if (!tex) return nullptr;
    note_gpu_submits(device_, 1); // Vulkan: a submit that sets the texture's layout
    update_rgba(tex, w, h, rgba, mipmaps, false);
    return tex;
}

void Uploader::update_rgba(SDL_GPUTexture* tex, u32 w, u32 h, const u32* rgba, bool mipmaps, bool cycle) {
    pending_.push_back({nullptr, tex, stage(rgba, w * h * 4), w * h * 4, w, h, 0, cycle});
    if (!mipmaps) return;
    std::vector<u32> level;
    const u32* above = rgba;
    for (u32 i = 1, n = mip_levels(w, h); i < n; ++i) {
        level = half_size(above, w, h);
        w = std::max(1u, w / 2), h = std::max(1u, h / 2);
        pending_.push_back({nullptr, tex, stage(level.data(), w * h * 4), w * h * 4, w, h, i, false});
        above = level.data();
    }
}

bool Uploader::submit() {
    if (pending_.empty()) return true;
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = u32(staging_.size());
    SDL_GPUTransferBuffer* t = SDL_CreateGPUTransferBuffer(device_, &ti);
    void* mapped = t ? SDL_MapGPUTransferBuffer(device_, t, false) : nullptr;
    bool ok = false;
    if (mapped) {
        std::memcpy(mapped, staging_.data(), staging_.size());
        SDL_UnmapGPUTransferBuffer(device_, t);
        if (SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_)) {
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
            for (const Pending& p : pending_) {
                if (p.buffer) {
                    SDL_GPUTransferBufferLocation src{t, p.offset};
                    SDL_GPUBufferRegion dst{p.buffer, 0, p.size};
                    SDL_UploadToGPUBuffer(copy, &src, &dst, false);
                } else {
                    SDL_GPUTextureTransferInfo src{};
                    src.transfer_buffer = t;
                    src.offset = p.offset;
                    src.pixels_per_row = p.width;
                    src.rows_per_layer = p.height;
                    SDL_GPUTextureRegion dst{};
                    dst.texture = p.texture;
                    dst.mip_level = p.level;
                    dst.w = p.width;
                    dst.h = p.height;
                    dst.d = 1;
                    SDL_UploadToGPUTexture(copy, &src, &dst, p.cycle);
                }
            }
            SDL_EndGPUCopyPass(copy);
            ok = SDL_SubmitGPUCommandBuffer(cmd);
        }
    }
    if (t) SDL_ReleaseGPUTransferBuffer(device_, t); // deferred until the GPU is done
    if (ok) note_gpu_submits(device_, 1, staging_.size());
    pending_.clear();
    staging_ = {};
    return ok;
}

} // namespace gt3r::render
