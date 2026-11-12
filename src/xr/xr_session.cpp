#if GT3R_XR

#include "xr/xr_internal.h"

#include "base/config.h"
#include "base/log.h"
#include "render/shaders/generated/present_frag.h"
#include "render/shaders/generated/present_vert.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gt3r::xr {

namespace {

constexpr XrViewConfigurationType kStereo = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

SDL_GPUTexture* gpu_texture(SDL_Texture* t) {
    return static_cast<SDL_GPUTexture*>(
        SDL_GetPointerProperty(SDL_GetTextureProperties(t), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
}

// Optional instance extensions, used when the runtime has them.
const char kFrameControllers[] = "XR_VALVE_frame_controller_interaction"; // (newer than our headers)
const char* const kOptionalExtensions[] = {
    XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
    XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME,
    kFrameControllers,
};

float clampf(float v, float lo, float hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : lo; }

std::optional<float> number(const IniDocument& cfg, const char* key) {
    auto v = cfg.get("vr", key);
    if (!v || v->empty()) return std::nullopt;
    char* end = nullptr;
    float f = std::strtof(v->c_str(), &end);
    if (end == v->c_str()) return std::nullopt;
    return f;
}

bool flag(const IniDocument& cfg, const char* key, bool fallback) {
    auto v = cfg.get("vr", key);
    if (!v) return fallback;
    std::string s = to_lower(*v);
    return s == "on" || s == "1" || s == "true" || s == "yes";
}

std::string fmt(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.2f", double(v));
    return b;
}

} // namespace

XrPosef to_xr(const Pose& p) {
    XrPosef r;
    r.orientation = {p.qx, p.qy, p.qz, p.qw};
    r.position = {p.pos.x, p.pos.y, p.pos.z};
    return r;
}

Pose from_xr(const XrPosef& p) {
    Pose r;
    r.pos = {p.position.x, p.position.y, p.position.z};
    r.qw = p.orientation.w, r.qx = p.orientation.x, r.qy = p.orientation.y, r.qz = p.orientation.z;
    return r;
}

// ---------------------------------------------------------------------------
// Settings

Settings Settings::load(const IniDocument& cfg) {
    Settings s;
    s.stereo = flag(cfg, "stereo", s.stereo);
    s.level_horizon = flag(cfg, "level_horizon", s.level_horizon);
    s.show_car = flag(cfg, "car_body", s.show_car);
    s.wheel = to_lower(cfg.get("vr", "steering").value_or("stick")) == "wheel";
    if (auto v = number(cfg, "wheel_degrees")) s.wheel_degrees = int(clampf(*v, 90, 900));
    if (auto v = number(cfg, "screen_distance")) s.screen_distance = clampf(*v, 1.0f, 10.0f);
    if (auto v = number(cfg, "screen_width")) s.screen_width = clampf(*v, 0.8f, 12.0f);
    if (auto v = number(cfg, "hud_distance")) s.hud_distance = clampf(*v, 0.5f, 5.0f);
    if (auto v = number(cfg, "hud_width")) s.hud_width = clampf(*v, 0.4f, 5.0f);
    if (auto v = number(cfg, "hud_drop")) s.hud_drop = clampf(*v, -1.0f, 1.0f);
    if (auto v = number(cfg, "seat_forward")) s.seat_forward = clampf(*v, -0.6f, 0.6f);
    if (auto v = number(cfg, "seat_height")) s.seat_height = clampf(*v, -0.6f, 0.6f);
    const std::string side = to_lower(cfg.get("vr", "seat_side").value_or("center"));
    s.seat_side = side == "left" ? SeatSide::Left : side == "right" ? SeatSide::Right : side == "car" ? SeatSide::Car : SeatSide::Center;
    if (auto v = number(cfg, "seat_offset")) s.seat_offset = clampf(*v, 0.1f, 0.6f);
    if (auto v = number(cfg, "refresh")) s.refresh_hz = int(clampf(*v, 60, 144));
    if (auto v = number(cfg, "resolution")) s.eye_scale = clampf(*v, 0.5f, 1.5f);
    return s;
}

void Settings::save(IniDocument& cfg) const {
    cfg.set("vr", "stereo", stereo ? "on" : "off");
    cfg.set("vr", "level_horizon", level_horizon ? "on" : "off");
    cfg.set("vr", "car_body", show_car ? "on" : "off");
    cfg.set("vr", "steering", wheel ? "wheel" : "stick");
    cfg.set("vr", "wheel_degrees", std::to_string(wheel_degrees));
    cfg.set("vr", "screen_distance", fmt(screen_distance));
    cfg.set("vr", "screen_width", fmt(screen_width));
    cfg.set("vr", "hud_distance", fmt(hud_distance));
    cfg.set("vr", "hud_width", fmt(hud_width));
    cfg.set("vr", "hud_drop", fmt(hud_drop));
    cfg.set("vr", "seat_forward", fmt(seat_forward));
    cfg.set("vr", "seat_height", fmt(seat_height));
    static const char* const kSides[] = {"center", "left", "right", "car"};
    cfg.set("vr", "seat_side", kSides[size_t(seat_side)]);
    cfg.set("vr", "seat_offset", fmt(seat_offset));
    cfg.set("vr", "refresh", std::to_string(refresh_hz));
    cfg.set("vr", "resolution", fmt(eye_scale));
}

// ---------------------------------------------------------------------------
// Device and session

namespace {

#if !defined(__ANDROID__)
enum class Probe { Headset, NoHeadset, NoRuntime };

// An instance of the runtime the loader picks now, and whether it has a headset. It stays while
// this lives: SteamVR started by it shuts down again (~20 s later) once it has no instance. One
// at a time (the loader allows one instance).
class RuntimeLook {
public:
    RuntimeLook() {
        if (!(loaded_ = SDL_OpenXR_LoadLibrary())) return;
        gipa_ = reinterpret_cast<PFN_xrGetInstanceProcAddr>(SDL_OpenXR_GetXrGetInstanceProcAddr());
        PFN_xrCreateInstance create_instance = nullptr;
        auto ci = xr_init<XrInstanceCreateInfo>(XR_TYPE_INSTANCE_CREATE_INFO);
        std::strcpy(ci.applicationInfo.applicationName, "GT3 Revisited");
        ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        const XrResult r = fn(XR_NULL_HANDLE, "xrCreateInstance", create_instance) ? create_instance(&ci, &inst_)
                                                                                   : XR_ERROR_RUNTIME_UNAVAILABLE;
        if (XR_FAILED(r)) {
            LOG_INFO("VR: no OpenXR runtime (%d)", int(r));
            inst_ = XR_NULL_HANDLE;
            return;
        }
        fn(inst_, "xrDestroyInstance", destroy_);
        PFN_xrGetInstanceProperties properties = nullptr;
        auto ip = xr_init<XrInstanceProperties>(XR_TYPE_INSTANCE_PROPERTIES);
        name_ = fn(inst_, "xrGetInstanceProperties", properties) && XR_SUCCEEDED(properties(inst_, &ip)) ? ip.runtimeName
                                                                                                         : "?";
        check();
    }
    ~RuntimeLook() {
        if (inst_ && destroy_) destroy_(inst_);
        if (loaded_) SDL_OpenXR_UnloadLibrary();
    }
    RuntimeLook(const RuntimeLook&) = delete;
    RuntimeLook& operator=(const RuntimeLook&) = delete;

    // Looks for the headset (again).
    Probe check() {
        PFN_xrGetSystem get_system = nullptr;
        if (!inst_ || !fn(inst_, "xrGetSystem", get_system)) return result_ = Probe::NoRuntime;
        auto gi = xr_init<XrSystemGetInfo>(XR_TYPE_SYSTEM_GET_INFO);
        gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrSystemId system = XR_NULL_SYSTEM_ID;
        const XrResult r = get_system(inst_, &gi, &system);
        if (XR_SUCCEEDED(r)) LOG_INFO("VR: %s has a headset", name_.c_str());
        else LOG_INFO("VR: %s has no headset (%d)", name_.c_str(), int(r));
        return result_ = XR_SUCCEEDED(r) ? Probe::Headset : Probe::NoHeadset;
    }
    Probe result() const { return result_; }

private:
    template <class F>
    bool fn(XrInstance inst, const char* name, F& f) {
        f = nullptr;
        if (gipa_) gipa_(inst, name, reinterpret_cast<PFN_xrVoidFunction*>(&f));
        return f != nullptr;
    }
    bool loaded_ = false;
    PFN_xrGetInstanceProcAddr gipa_ = nullptr;
    XrInstance inst_ = XR_NULL_HANDLE;
    PFN_xrDestroyInstance destroy_ = nullptr;
    std::string name_;
    Probe result_ = Probe::NoRuntime;
};
#endif

// Desktops: points the loader (XR_RUNTIME_JSON) at the first of the candidates with a headset:
// the [vr] runtime chosen, or (auto) the system's choice, then SteamVR while it runs. A PC with
// Meta Horizon Link has Meta's as the system's choice, while a Steam Frame or a headset streamed
// by Steam Link is in SteamVR (only the game added to Steam and started there got it). A
// player's own XR_RUNTIME_JSON: that one. False: no headset.
bool choose_runtime(const std::string& choice) {
#if defined(__ANDROID__)
    (void)choice;
    return true; // the Quest's (one attempt: its runtime cannot be set up twice in a process)
#else
    static const bool player_env = [] {
        const char* e = SDL_getenv("XR_RUNTIME_JSON");
        return e && *e;
    }();
    if (player_env) return RuntimeLook().result() == Probe::Headset;
    std::string steamvr;
    std::vector<std::string> candidates; // manifests; empty: the system's choice
    for (const RuntimeFile& f : installed_runtimes()) {
        if (f.key == "steamvr") steamvr = f.manifest;
        if (f.key == choice) candidates.push_back(f.manifest);
    }
    const std::string system = system_runtime();
    if (candidates.empty()) {
        candidates.push_back("");
        if (!steamvr.empty() && !same_file(steamvr, system) && steamvr_running()) candidates.push_back(steamvr);
    }
    for (const std::string& m : candidates) {
        if (m.empty()) SDL_unsetenv_unsafe("XR_RUNTIME_JSON");
        else SDL_setenv_unsafe("XR_RUNTIME_JSON", m.c_str(), 1);
        // SteamVR not running: started as its shortcut does. An instance starts its server alone,
        // which reports the headset but gives SDL no graphics device for it (no compositor).
        const bool is_steamvr = same_file(m.empty() ? system : m, steamvr);
        if (is_steamvr && !steamvr_running() && start_steamvr(steamvr)) LOG_INFO("VR: starting SteamVR");
        RuntimeLook look;
        // SteamVR starting: until its compositor runs, seconds later, SDL gets no graphics device
        // from it. The look's instance keeps it up meanwhile (it shuts down again ~20 s after its
        // last instance goes); SDL's own follows it at once.
        if (is_steamvr && !steamvr_compositor_running() && (steamvr_running() || look.result() == Probe::Headset)) {
            LOG_INFO("VR: waiting for SteamVR to start");
            int waited = 0;
            for (; waited < 45 && !steamvr_compositor_running() && (waited < 10 || steamvr_running()); ++waited)
                SDL_Delay(1000);
            LOG_INFO("VR: SteamVR's compositor %s after %d s", steamvr_compositor_running() ? "up" : "not up", waited);
            for (int i = 0; i < 5 && look.check() != Probe::Headset; ++i) SDL_Delay(1000);
        }
        if (look.result() == Probe::Headset) return true;
    }
    return false;
#endif
}

} // namespace

std::vector<std::pair<std::string, std::string>> Session::runtime_choices() {
    std::vector<std::pair<std::string, std::string>> r;
    for (const RuntimeFile& f : installed_runtimes()) r.emplace_back(f.key, f.name);
    return r;
}

bool Session::retry_device(int attempt) {
#if defined(__ANDROID__)
    (void)attempt;
    return false;
#else
    if (attempt >= 10) return false;
    LOG_INFO("VR: the headset's software is not ready; trying again");
    return true;
#endif
}

bool Session::prepare_device(SDL_PropertiesID props, DeviceRequest& req) {
    if (!choose_runtime(req.runtime)) return false;
    if (!SDL_OpenXR_LoadLibrary()) {
        LOG_INFO("VR: no OpenXR loader (%s)", SDL_GetError());
        return false;
    }
    // The runtime's instance extensions: SDL creates the instance with the ones we name, and
    // fails if one is missing.
    auto gipa = reinterpret_cast<PFN_xrGetInstanceProcAddr>(SDL_OpenXR_GetXrGetInstanceProcAddr());
    PFN_xrEnumerateInstanceExtensionProperties enumerate = nullptr;
    if (gipa)
        gipa(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties",
             reinterpret_cast<PFN_xrVoidFunction*>(&enumerate));
    req.extensions.clear();
    u32 count = 0;
    if (enumerate && XR_SUCCEEDED(enumerate(nullptr, 0, &count, nullptr)) && count > 0) {
        std::vector<XrExtensionProperties> ext(count, xr_init<XrExtensionProperties>(XR_TYPE_EXTENSION_PROPERTIES));
        if (XR_SUCCEEDED(enumerate(nullptr, count, &count, ext.data())))
            for (const char* want : kOptionalExtensions)
                for (u32 i = 0; i < count; ++i)
                    if (std::strcmp(ext[i].extensionName, want) == 0) req.extensions.push_back(want);
    }
    SDL_OpenXR_UnloadLibrary(); // SDL loads it again (reference counted) for the device
    req.names.clear();
    for (const std::string& e : req.extensions) req.names.push_back(e.c_str());
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_ENABLE_BOOLEAN, true);
    SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_INSTANCE_POINTER, &req.instance);
    SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_SYSTEM_ID_POINTER, &req.system);
    SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_APPLICATION_NAME_STRING, "GT3 Revisited");
    SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_ENGINE_NAME_STRING, "GT3 Revisited");
    if (!req.names.empty()) {
        SDL_SetNumberProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_EXTENSION_COUNT_NUMBER, Sint64(req.names.size()));
        SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_EXTENSION_NAMES_POINTER, req.names.data());
    }
    return true;
}

bool Session::Impl::check(XrResult r, const char* what) const {
    if (XR_SUCCEEDED(r)) return true;
    char name[XR_MAX_RESULT_STRING_SIZE] = {};
    if (xrResultToString && instance) xrResultToString(instance, r, name);
    LOG_WARN("VR: %s failed: %s (%d)", what, name[0] ? name : "?", int(r));
    return false;
}

bool Session::Impl::load_functions() {
    gipa = reinterpret_cast<PFN_xrGetInstanceProcAddr>(SDL_OpenXR_GetXrGetInstanceProcAddr());
    if (!gipa) return false;
    bool ok = true;
#define GT3R_XR_LOAD(f)                                                                                \
    if (XR_FAILED(gipa(instance, #f, reinterpret_cast<PFN_xrVoidFunction*>(&f))) || !f) {             \
        LOG_WARN("VR: OpenXR function " #f " missing");                                               \
        ok = false;                                                                                    \
    }
    GT3R_XR_FUNCTIONS(GT3R_XR_LOAD)
#undef GT3R_XR_LOAD
    auto opt = [&](const char* name, auto& fn) {
        if (XR_FAILED(gipa(instance, name, reinterpret_cast<PFN_xrVoidFunction*>(&fn)))) fn = nullptr;
    };
    opt("xrEnumerateDisplayRefreshRatesFB", xrEnumerateDisplayRefreshRatesFB);
    opt("xrRequestDisplayRefreshRateFB", xrRequestDisplayRefreshRateFB);
    opt("xrGetDisplayRefreshRateFB", xrGetDisplayRefreshRateFB);
    opt("xrPerfSettingsSetPerformanceLevelEXT", xrPerfSettingsSetPerformanceLevelEXT);
    return ok;
}

bool Session::Impl::make_space(XrReferenceSpaceType type, const XrPosef& pose, XrSpace& out) {
    auto ci = xr_init<XrReferenceSpaceCreateInfo>(XR_TYPE_REFERENCE_SPACE_CREATE_INFO);
    ci.referenceSpaceType = type;
    ci.poseInReferenceSpace = pose;
    return check(xrCreateReferenceSpace(session, &ci, &out), "xrCreateReferenceSpace");
}

bool Session::Impl::make_chain(Chain& c, int w, int h) {
    auto ci = xr_init<XrSwapchainCreateInfo>(XR_TYPE_SWAPCHAIN_CREATE_INFO);
    // The canvases are drawn in by the present pass (present.frag: GT3's colours are
    // display-referred, their bytes go into the sRGB image unchanged).
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.sampleCount = 1;
    ci.width = u32(w);
    ci.height = u32(h);
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    if (!check(SDL_CreateGPUXRSwapchain(device, session, &ci, format, &c.handle, &c.images), "SDL_CreateGPUXRSwapchain"))
        return false;
    c.w = w, c.h = h;
    return true;
}

// A pipeline that draws a canvas over the whole viewport (present.vert, present.frag).
SDL_GPUGraphicsPipeline* Session::Impl::canvas_pipeline(SDL_GPUTextureFormat target, bool premultiplied) {
    SDL_GPUShaderCreateInfo si{};
    si.format = SDL_GPU_SHADERFORMAT_SPIRV;
    si.entrypoint = "main";
    si.code = render::shaders::present_vert_spv;
    si.code_size = sizeof(render::shaders::present_vert_spv);
    si.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    SDL_GPUShader* vs = SDL_CreateGPUShader(device, &si);
    si.code = render::shaders::present_frag_spv;
    si.code_size = sizeof(render::shaders::present_frag_spv);
    si.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    si.num_samplers = 1;
    si.num_uniform_buffers = 1;
    SDL_GPUShader* fs = SDL_CreateGPUShader(device, &si);
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    if (vs && fs) {
        SDL_GPUColorTargetDescription color{};
        color.format = target;
        if (premultiplied) {
            SDL_GPUColorTargetBlendState& b = color.blend_state;
            b.enable_blend = true;
            b.src_color_blendfactor = b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            b.dst_color_blendfactor = b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            b.color_blend_op = b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        }
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = vs;
        pi.fragment_shader = fs;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pi.target_info.color_target_descriptions = &color;
        pi.target_info.num_color_targets = 1;
        pipeline = SDL_CreateGPUGraphicsPipeline(device, &pi);
    }
    if (vs) SDL_ReleaseGPUShader(device, vs);
    if (fs) SDL_ReleaseGPUShader(device, fs);
    return pipeline;
}

SDL_GPUSampler* Session::Impl::canvas_sampler(SDL_GPUFilter filter) {
    SDL_GPUSamplerCreateInfo smp{};
    smp.min_filter = smp.mag_filter = filter;
    smp.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    smp.address_mode_u = smp.address_mode_v = smp.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    return SDL_CreateGPUSampler(device, &smp);
}

bool Session::Impl::make_present_pipeline() {
    present = canvas_pipeline(format, false);
    present_sampler = canvas_sampler(SDL_GPU_FILTER_NEAREST); // same size as the image: texel for pixel
    if (!present || !present_sampler) LOG_WARN("VR: present pass: %s", SDL_GetError());
    return present && present_sampler;
}

bool Session::mirror_to(SDL_Window* window) {
    Impl& x = *impl_;
    if (!SDL_ClaimWindowForGPUDevice(x.device, window)) {
        LOG_WARN("VR: no picture in the window: %s", SDL_GetError());
        return false;
    }
    // Not held to the desktop's refresh when the driver allows it.
    if (SDL_WindowSupportsGPUPresentMode(x.device, window, SDL_GPU_PRESENTMODE_MAILBOX))
        SDL_SetGPUSwapchainParameters(x.device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_MAILBOX);
    const SDL_GPUTextureFormat f = SDL_GetGPUSwapchainTextureFormat(x.device, window);
    x.mirror_opaque = x.canvas_pipeline(f, false);
    x.mirror_panel = x.canvas_pipeline(f, true);
    x.mirror_sampler = x.canvas_sampler(SDL_GPU_FILTER_LINEAR);
    x.mirror = window;
    if (x.mirror_opaque && x.mirror_panel && x.mirror_sampler) return true;
    LOG_WARN("VR: no picture in the window: %s", SDL_GetError());
    x.release_mirror();
    return false;
}

void Session::Impl::release_mirror() {
    if (mirror_opaque) SDL_ReleaseGPUGraphicsPipeline(device, mirror_opaque);
    if (mirror_panel) SDL_ReleaseGPUGraphicsPipeline(device, mirror_panel);
    if (mirror_sampler) SDL_ReleaseGPUSampler(device, mirror_sampler);
    if (mirror) SDL_ReleaseWindowFromGPUDevice(device, mirror);
    mirror_opaque = mirror_panel = nullptr;
    mirror_sampler = nullptr;
    mirror = nullptr;
}

void Session::Impl::draw_mirror(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* canvas, int canvas_w, int canvas_h,
                                SDL_GPUTexture* eye, int eye_w, int eye_h) {
    SDL_GPUTexture* target = nullptr;
    Uint32 w = 0, h = 0;
    // Every frame, whatever it shows: with a window claimed, SDL recycles finished GPU work only
    // in a submit that asked for its swapchain (without that, frames filled the GPU's memory
    // within minutes). No texture while the window is busy or minimized: it misses this frame,
    // the headset waits for nothing.
    if (!SDL_AcquireGPUSwapchainTexture(cmd, mirror, &target, &w, &h) || !target) return;
    SDL_GPUColorTargetInfo ct{};
    ct.texture = target;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.clear_color = {0, 0, 0, 1};
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    auto draw = [&](SDL_GPUGraphicsPipeline* pipeline, SDL_GPUTexture* tex, const SDL_FRect& r, bool premultiplied) {
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        const SDL_GPUViewport vp{r.x, r.y, r.w, r.h, 0, 1};
        SDL_SetGPUViewport(pass, &vp);
        SDL_GPUTextureSamplerBinding b{tex, mirror_sampler};
        SDL_BindGPUFragmentSamplers(pass, 0, &b, 1);
        const float mode[4] = {premultiplied ? 1.0f : 0.0f, 1.0f, 0, 0}; // the canvas's bytes as they are
        SDL_PushGPUFragmentUniformData(cmd, 0, mode, sizeof(mode));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    };
    const SDL_FRect shown = fit_picture(canvas_w, canvas_h, float(w), float(h), false);
    if (eye) {
        draw(mirror_opaque, eye, fit_picture(eye_w, eye_h, float(w), float(h), true), false);
        draw(mirror_panel, canvas, shown, true);
    } else {
        draw(mirror_opaque, canvas, shown, false);
    }
    SDL_EndGPURenderPass(pass);
}

void Session::Impl::release_chain(Chain& c) {
    if (c.handle) SDL_DestroyGPUXRSwapchain(device, c.handle, c.images);
    c = Chain{};
}

void Session::Impl::apply_refresh_rate(int hz) {
    if (!xrRequestDisplayRefreshRateFB || rates.empty()) return;
    // The offered rate nearest the wish.
    float best = rates.front();
    for (float r : rates)
        if (std::fabs(r - float(hz)) < std::fabs(best - float(hz))) best = r;
    if (check(xrRequestDisplayRefreshRateFB(session, best), "xrRequestDisplayRefreshRateFB")) {
        rate = best;
        LOG_INFO("VR: display refresh %.0f Hz", double(best));
    }
}

std::unique_ptr<Session> Session::create(SDL_GPUDevice* device, SDL_Renderer* renderer, const DeviceRequest& req,
                                         const Settings& settings) {
    std::unique_ptr<Session> s(new Session());
    s->impl_ = std::make_unique<Impl>();
    Impl& x = *s->impl_;
    x.device = device;
    x.renderer = renderer;
    x.instance = XrInstance(req.instance);
    x.system = XrSystemId(req.system);
    s->settings_ = settings;
    if (!x.instance || !x.load_functions()) return nullptr;

    auto ip = xr_init<XrInstanceProperties>(XR_TYPE_INSTANCE_PROPERTIES);
    if (XR_SUCCEEDED(x.xrGetInstanceProperties(x.instance, &ip))) {
        char v[32];
        std::snprintf(v, sizeof(v), " %u.%u.%u", unsigned(XR_VERSION_MAJOR(ip.runtimeVersion)),
                      unsigned(XR_VERSION_MINOR(ip.runtimeVersion)), unsigned(XR_VERSION_PATCH(ip.runtimeVersion)));
        s->runtime_name_ = std::string(ip.runtimeName) + v;
        LOG_INFO("VR: runtime %s", s->runtime_name_.c_str());
    }
    for (const std::string& e : req.extensions) x.frame_controllers = x.frame_controllers || e == kFrameControllers;
    auto sp = xr_init<XrSystemProperties>(XR_TYPE_SYSTEM_PROPERTIES);
    if (XR_SUCCEEDED(x.xrGetSystemProperties(x.instance, x.system, &sp)))
        LOG_INFO("VR: %s (vendor %u), %ux%u max swapchain", sp.systemName, sp.vendorId,
                 sp.graphicsProperties.maxSwapchainImageWidth, sp.graphicsProperties.maxSwapchainImageHeight);

    auto sci = xr_init<XrSessionCreateInfo>(XR_TYPE_SESSION_CREATE_INFO);
    if (!x.check(SDL_CreateGPUXRSession(device, &sci, &x.session), "SDL_CreateGPUXRSession")) return nullptr;

    const XrPosef identity{{0, 0, 0, 1}, {0, 0, 0}};
    if (!x.make_space(XR_REFERENCE_SPACE_TYPE_LOCAL, identity, x.base_local) ||
        !x.make_space(XR_REFERENCE_SPACE_TYPE_LOCAL, identity, x.local) ||
        !x.make_space(XR_REFERENCE_SPACE_TYPE_VIEW, identity, x.view_space))
        return nullptr;

    // Swapchain format: 8-bit sRGB in the runtime's order of preference (see make_chain). The
    // canvases are RGBA8 whatever it is: the 3D pass resolves into the eyes' (View3D::draw_to),
    // the present pass converts.
    int nf = 0;
    SDL_GPUTextureFormat* formats = SDL_GetGPUXRSwapchainFormats(device, x.session, &nf);
    const SDL_GPUTextureFormat kPreferred[] = {SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
                                               SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB,
                                               SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM};
    for (SDL_GPUTextureFormat want : kPreferred) {
        for (int i = 0; formats && i < nf; ++i)
            if (formats[i] == want) x.format = want;
        if (x.format != SDL_GPU_TEXTUREFORMAT_INVALID) break;
    }
    SDL_free(formats);
    if (x.format == SDL_GPU_TEXTUREFORMAT_INVALID) {
        LOG_WARN("VR: the runtime offers no 8-bit RGBA swapchain format");
        return nullptr;
    }
    x.srgb = x.format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB || x.format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
    if (!x.make_present_pipeline()) return nullptr;

    // Eye size: the recommended one scaled by [vr] resolution, within the maximum.
    u32 nv = 0;
    XrViewConfigurationView vcv[2] = {xr_init<XrViewConfigurationView>(XR_TYPE_VIEW_CONFIGURATION_VIEW),
                                      xr_init<XrViewConfigurationView>(XR_TYPE_VIEW_CONFIGURATION_VIEW)};
    if (!x.check(x.xrEnumerateViewConfigurationViews(x.instance, x.system, kStereo, 2, &nv, vcv),
                 "xrEnumerateViewConfigurationViews") ||
        nv != 2)
        return nullptr;
    for (int i = 0; i < 2; ++i) {
        int w = int(std::lround(float(vcv[i].recommendedImageRectWidth) * settings.eye_scale));
        int h = int(std::lround(float(vcv[i].recommendedImageRectHeight) * settings.eye_scale));
        if (const char* f = SDL_getenv("GT3R_VR_EYE_SIZE"); f && *f) w = h = std::atoi(f); // testing (simulated headsets)
        w = std::clamp(w & ~1, 256, int(vcv[i].maxImageRectWidth));
        h = std::clamp(h & ~1, 256, int(vcv[i].maxImageRectHeight));
        if (!x.make_chain(x.eyes[i], w, h)) return nullptr;
        Eye& e = s->eyes_[i];
        e.width = w, e.height = h;
        e.canvas = SDL_CreateTexture(renderer, x.canvas_format, SDL_TEXTUREACCESS_TARGET, w, h);
        if (!e.canvas) {
            LOG_WARN("VR: eye canvas %dx%d: %s", w, h, SDL_GetError());
            return nullptr;
        }
        SDL_SetTextureBlendMode(e.canvas, SDL_BLENDMODE_NONE);
    }
    LOG_INFO("VR: eyes %dx%d (recommended %ux%u x %.2f), swapchain format %d", s->eyes_[0].width,
             s->eyes_[0].height, vcv[0].recommendedImageRectWidth, vcv[0].recommendedImageRectHeight,
             double(settings.eye_scale), int(x.format));

    if (!x.make_chain(x.quad, s->canvas_w_, s->canvas_h_)) return nullptr;
    s->canvas_ = SDL_CreateTexture(renderer, x.canvas_format, SDL_TEXTUREACCESS_TARGET, s->canvas_w_, s->canvas_h_);
    if (!s->canvas_) {
        LOG_WARN("VR: canvas: %s", SDL_GetError());
        return nullptr;
    }
    SDL_SetTextureBlendMode(s->canvas_, SDL_BLENDMODE_NONE);

    if (x.xrEnumerateDisplayRefreshRatesFB) {
        u32 n = 0;
        if (XR_SUCCEEDED(x.xrEnumerateDisplayRefreshRatesFB(x.session, 0, &n, nullptr)) && n > 0) {
            x.rates.resize(n);
            if (XR_FAILED(x.xrEnumerateDisplayRefreshRatesFB(x.session, n, &n, x.rates.data()))) x.rates.clear();
        }
        if (x.xrGetDisplayRefreshRateFB) x.xrGetDisplayRefreshRateFB(x.session, &x.rate);
    }
    if (!x.create_input()) LOG_WARN("VR: controller input unavailable");
    LOG_INFO("VR: session ready");
    return s;
}

Session::~Session() {
    if (!impl_) return;
    Impl& x = *impl_;
    if (x.device) SDL_WaitForGPUIdle(x.device);
    x.release_mirror();
    x.destroy_input();
    if (x.present) SDL_ReleaseGPUGraphicsPipeline(x.device, x.present);
    if (x.present_sampler) SDL_ReleaseGPUSampler(x.device, x.present_sampler);
    for (Eye& e : eyes_)
        if (e.canvas) SDL_DestroyTexture(e.canvas);
    if (canvas_) SDL_DestroyTexture(canvas_);
    for (Chain& c : x.eyes) x.release_chain(c);
    x.release_chain(x.quad);
    for (XrSpace* sp : {&x.local, &x.base_local, &x.view_space})
        if (*sp) x.xrDestroySpace(*sp), *sp = XR_NULL_HANDLE;
    if (x.session) x.xrDestroySession(x.session); // (allowed in any state)
    // The instance belongs to the GPU device (SDL destroys it with the device).
}

void Session::set_settings(const Settings& s) {
    const bool rate_changed = s.refresh_hz != settings_.refresh_hz;
    settings_ = s;
    if (rate_changed && impl_->running) impl_->apply_refresh_rate(s.refresh_hz);
}

const std::vector<float>& Session::refresh_rates() const { return impl_->rates; }

bool Session::running() const { return impl_->running; }
bool Session::focused() const { return impl_->state == XR_SESSION_STATE_FOCUSED; }
bool Session::exit_requested() const { return impl_->exit; }

bool Session::take_focus_lost() {
    bool v = impl_->focus_lost;
    impl_->focus_lost = false;
    return v;
}

double Session::frame_interval() const { return impl_->rate > 1 ? 1.0 / double(impl_->rate) : 1.0 / 72.0; }
s64 Session::display_time() const { return s64(impl_->frame.predictedDisplayTime); }
Pose Session::head() const { return impl_->head; }

void Session::poll() {
    Impl& x = *impl_;
    auto ev = xr_init<XrEventDataBuffer>(XR_TYPE_EVENT_DATA_BUFFER);
    while (x.xrPollEvent(x.instance, &ev) == XR_SUCCESS) {
        switch (ev.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            const auto& e = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&ev);
            const XrSessionState before = x.state;
            x.state = e.state;
            LOG_INFO("VR: session state %d -> %d", int(before), int(e.state));
            if (before == XR_SESSION_STATE_FOCUSED && e.state != XR_SESSION_STATE_FOCUSED) x.focus_lost = true;
            if (e.state == XR_SESSION_STATE_READY) {
                auto bi = xr_init<XrSessionBeginInfo>(XR_TYPE_SESSION_BEGIN_INFO);
                bi.primaryViewConfigurationType = kStereo;
                if (x.check(x.xrBeginSession(x.session, &bi), "xrBeginSession")) {
                    x.running = true;
                    x.apply_refresh_rate(settings_.refresh_hz);
                    // Two views of a race at 90 Hz: the clocks the headset can hold for a long
                    // session (it starts low and only rises on its own after missed frames).
                    if (x.xrPerfSettingsSetPerformanceLevelEXT)
                        for (XrPerfSettingsDomainEXT d : {XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_DOMAIN_GPU_EXT})
                            x.check(x.xrPerfSettingsSetPerformanceLevelEXT(x.session, d, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT),
                                    "xrPerfSettingsSetPerformanceLevelEXT");
                }
            } else if (e.state == XR_SESSION_STATE_STOPPING) {
                x.check(x.xrEndSession(x.session), "xrEndSession");
                x.running = false;
            } else if (e.state == XR_SESSION_STATE_EXITING || e.state == XR_SESSION_STATE_LOSS_PENDING) {
                x.running = false;
                x.exit = true;
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            x.running = false;
            x.exit = true;
            break;
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
            // The system re-centred its LOCAL space (long press on the Meta button): ours, offset
            // from it, follows by starting again from its new origin.
            const auto& e = *reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&ev);
            if (e.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL) {
                XrSpace fresh = XR_NULL_HANDLE;
                if (x.make_space(XR_REFERENCE_SPACE_TYPE_LOCAL, {{0, 0, 0, 1}, {0, 0, 0}}, fresh)) {
                    x.xrDestroySpace(x.local);
                    x.local = fresh;
                }
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB: {
            const auto& e = *reinterpret_cast<const XrEventDataDisplayRefreshRateChangedFB*>(&ev);
            x.rate = e.toDisplayRefreshRate;
            break;
        }
        default:
            break;
        }
        ev = xr_init<XrEventDataBuffer>(XR_TYPE_EVENT_DATA_BUFFER);
    }
}

bool Session::begin_frame() {
    Impl& x = *impl_;
    stereo_ = false;
    if (!x.running) return false;
    static const bool timing = [] { // (read late: Android's args.txt --env sets it after startup)
        const char* t = SDL_getenv("GT3R_VR_TIMING");
        return t && *t == '1';
    }();
    x.timing.on = timing;
    x.frame = xr_init<XrFrameState>(XR_TYPE_FRAME_STATE);
    auto wi = xr_init<XrFrameWaitInfo>(XR_TYPE_FRAME_WAIT_INFO);
    const Uint64 t_wait = SDL_GetTicksNS();
    if (!x.check(x.xrWaitFrame(x.session, &wi, &x.frame), "xrWaitFrame")) return false;
    if (x.timing.on) {
        const Uint64 now = SDL_GetTicksNS();
        if (x.timing.last_wait_end) x.timing.period += double(now - x.timing.last_wait_end) / 1e6;
        x.timing.wait += double(now - t_wait) / 1e6;
        x.timing.last_wait_end = now;
    }
    auto bi = xr_init<XrFrameBeginInfo>(XR_TYPE_FRAME_BEGIN_INFO);
    if (!x.check(x.xrBeginFrame(x.session, &bi), "xrBeginFrame")) return false;
    x.frame_open = true;

    const XrTime t = x.frame.predictedDisplayTime;
    // Head in the runtime's LOCAL space: centres the seated space on the first tracked frame
    // and on request (yaw only, so the floor stays level).
    auto loc = xr_init<XrSpaceLocation>(XR_TYPE_SPACE_LOCATION);
    const XrSpaceLocationFlags tracked = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if ((recenter_pending_ || !x.centred) && XR_SUCCEEDED(x.xrLocateSpace(x.view_space, x.base_local, t, &loc)) &&
        (loc.locationFlags & tracked) == tracked) {
        const XrQuaternionf& q = loc.pose.orientation;
        // Heading of the head's forward axis (-z) about +y.
        float fx = -2 * (q.x * q.z + q.w * q.y), fz = -(1 - 2 * (q.x * q.x + q.y * q.y));
        float yaw = std::atan2(-fx, -fz);
        XrPosef p{{0, std::sin(yaw / 2), 0, std::cos(yaw / 2)}, loc.pose.position};
        XrSpace fresh = XR_NULL_HANDLE;
        if (x.make_space(XR_REFERENCE_SPACE_TYPE_LOCAL, p, fresh)) {
            x.xrDestroySpace(x.local);
            x.local = fresh;
            x.centred = true;
            recenter_pending_ = false;
            LOG_INFO("VR: view centred");
        }
    }

    eyes_valid_ = false;
    if (x.frame.shouldRender) {
        auto li = xr_init<XrViewLocateInfo>(XR_TYPE_VIEW_LOCATE_INFO);
        li.viewConfigurationType = kStereo;
        li.displayTime = t;
        li.space = x.local;
        auto vs = xr_init<XrViewState>(XR_TYPE_VIEW_STATE);
        u32 n = 0;
        if (XR_SUCCEEDED(x.xrLocateViews(x.session, &li, &vs, 2, &n, x.views)) && n == 2 &&
            (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
            eyes_valid_ = true;
            for (int i = 0; i < 2; ++i) {
                Eye& e = eyes_[i];
                e.pose = from_xr(x.views[i].pose);
                e.tan_left = std::tan(x.views[i].fov.angleLeft);
                e.tan_right = std::tan(x.views[i].fov.angleRight);
                e.tan_down = std::tan(x.views[i].fov.angleDown);
                e.tan_up = std::tan(x.views[i].fov.angleUp);
            }
        }
        auto hl = xr_init<XrSpaceLocation>(XR_TYPE_SPACE_LOCATION);
        if (XR_SUCCEEDED(x.xrLocateSpace(x.view_space, x.local, t, &hl)) &&
            (hl.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
            x.head = from_xr(hl.pose);
    }
    return true;
}

void Session::end_frame() {
    Impl& x = *impl_;
    if (!x.frame_open) return;
    x.frame_open = false;
    Uint64 t0 = SDL_GetTicksNS(), t1 = t0, t2 = t0;
    if (x.timing.on && x.timing.last_wait_end) x.timing.app += double(t0 - x.timing.last_wait_end) / 1e6;
    const bool stereo = stereo_ && eyes_valid_;
    hud_panel_ = stereo; // where the canvas shows (the pointer aims there next frame)

    auto mirror = [&](SDL_GPUCommandBuffer* cmd) {
        if (x.mirror)
            x.draw_mirror(cmd, gpu_texture(canvas_), canvas_w_, canvas_h_, stereo ? gpu_texture(eyes_[0].canvas) : nullptr,
                          eyes_[0].width, eyes_[0].height);
    };

    XrCompositionLayerProjectionView pv[2] = {
        xr_init<XrCompositionLayerProjectionView>(XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW),
        xr_init<XrCompositionLayerProjectionView>(XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW)};
    auto proj = xr_init<XrCompositionLayerProjection>(XR_TYPE_COMPOSITION_LAYER_PROJECTION);
    auto quad = xr_init<XrCompositionLayerQuad>(XR_TYPE_COMPOSITION_LAYER_QUAD);
    const XrCompositionLayerBaseHeader* layers[2] = {};
    u32 layer_count = 0;

    if (x.frame.shouldRender) {
        // Acquire the images, copy the canvases in, release them once the copy is submitted.
        std::vector<std::pair<Chain*, SDL_Texture*>> jobs;
        jobs.push_back({&x.quad, canvas_});
        if (stereo)
            for (int i = 0; i < 2; ++i) jobs.push_back({&x.eyes[i], eyes_[i].canvas});
        for (auto& [c, tex] : jobs) {
            auto ai = xr_init<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);
            if (!x.check(x.xrAcquireSwapchainImage(c->handle, &ai, &c->acquired), "xrAcquireSwapchainImage")) continue;
            auto wi = xr_init<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);
            wi.timeout = XR_INFINITE_DURATION;
            c->held = true;
            if (!x.check(x.xrWaitSwapchainImage(c->handle, &wi), "xrWaitSwapchainImage")) {
                auto ri = xr_init<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
                x.xrReleaseSwapchainImage(c->handle, &ri);
                c->held = false;
            }
        }
        t1 = SDL_GetTicksNS();
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(x.device);
        if (cmd) {
            for (auto& [c, tex] : jobs) {
                if (!c->held || !tex) continue;
                SDL_GPUTexture* src = gpu_texture(tex);
                if (!src) continue;
                SDL_GPUColorTargetInfo ct{};
                ct.texture = c->images[c->acquired];
                ct.load_op = SDL_GPU_LOADOP_DONT_CARE;
                ct.store_op = SDL_GPU_STOREOP_STORE;
                SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
                SDL_BindGPUGraphicsPipeline(pass, x.present);
                SDL_GPUTextureSamplerBinding b{src, x.present_sampler};
                SDL_BindGPUFragmentSamplers(pass, 0, &b, 1);
                // The HUD panel (stereo frames) is premultiplied over the eyes; the screen is opaque.
                const float mode[4] = {c == &x.quad && stereo ? 1.0f : 0.0f, x.srgb ? 0.0f : 1.0f, 0, 0};
                SDL_PushGPUFragmentUniformData(cmd, 0, mode, sizeof(mode));
                SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
                SDL_EndGPURenderPass(pass);
            }
            mirror(cmd);
            SDL_SubmitGPUCommandBuffer(cmd);
        }
        t2 = SDL_GetTicksNS();
        bool ok = true;
        for (auto& [c, tex] : jobs) {
            if (!c->held) {
                ok = false;
                continue;
            }
            auto ri = xr_init<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
            x.xrReleaseSwapchainImage(c->handle, &ri);
            c->held = false;
        }

        if (ok && cmd) {
            const float aspect = float(canvas_h_) / float(canvas_w_);
            quad.space = x.local;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = x.quad.handle;
            quad.subImage.imageRect = {{0, 0}, {x.quad.w, x.quad.h}};
            if (stereo) {
                for (int i = 0; i < 2; ++i) {
                    pv[i].pose = x.views[i].pose;
                    pv[i].fov = x.views[i].fov;
                    pv[i].subImage.swapchain = x.eyes[i].handle;
                    pv[i].subImage.imageRect = {{0, 0}, {x.eyes[i].w, x.eyes[i].h}};
                }
                proj.space = x.local;
                proj.viewCount = 2;
                proj.views = pv;
                layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&proj);
                // The HUD: a transparent panel ahead of the seat, tilted to face the eyes.
                const Settings& st = settings_;
                float tilt = -std::atan2(st.hud_drop, st.hud_distance);
                quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                quad.pose = {{std::sin(tilt / 2), 0, 0, std::cos(tilt / 2)}, {0, -st.hud_drop, -st.hud_distance}};
                quad.size = {st.hud_width, st.hud_width * aspect};
            } else {
                quad.layerFlags = 0;
                quad.pose = {{0, 0, 0, 1}, {0, 0, -settings_.screen_distance}};
                quad.size = {settings_.screen_width, settings_.screen_width * aspect};
            }
            layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
        }
    } else if (x.mirror) { // the headset isn't showing the game: the window still does
        if (SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(x.device)) {
            mirror(cmd);
            SDL_SubmitGPUCommandBuffer(cmd);
        }
    }

    auto ei = xr_init<XrFrameEndInfo>(XR_TYPE_FRAME_END_INFO);
    ei.displayTime = x.frame.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = layer_count;
    ei.layers = layers;
    x.check(x.xrEndFrame(x.session, &ei), "xrEndFrame");
    if (x.timing.on) {
        Impl::Timing& tm = x.timing;
        const Uint64 t3 = SDL_GetTicksNS();
        tm.images += double(t1 - t0) / 1e6, tm.copy += double(t2 - t1) / 1e6, tm.end += double(t3 - t2) / 1e6;
        ++tm.frames;
        if (!tm.window_start) tm.window_start = t3;
        if (t3 - tm.window_start > 3'000'000'000ull && tm.frames > 0) {
            const double n = tm.frames;
            LOG_INFO("VR timing (ms/frame, %d frames): period %.2f = wait %.2f + app %.2f + images %.2f + copy %.2f + xrEndFrame %.2f",
                     tm.frames, tm.period / n, tm.wait / n, tm.app / n, tm.images / n, tm.copy / n, tm.end / n);
            tm = Impl::Timing{true, tm.last_wait_end, t3};
        }
    }
}

} // namespace gt3r::xr

#endif // GT3R_XR
