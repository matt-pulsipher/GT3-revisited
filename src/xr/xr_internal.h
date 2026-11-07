#pragma once

// Private to src/xr: the OpenXR state behind xr::Session (xr_session.cpp: instance, session,
// spaces, swapchains, frames; xr_input.cpp: controller actions and the virtual gamepad;
// xr_runtimes.cpp: the runtimes a desktop has).

#if GT3R_XR

#include "xr/xr_session.h"

// OpenXR's own headers first, so SDL_openxr.h uses their types.
#include <openxr/openxr.h>

#include <SDL3/SDL_openxr.h>

#include <array>
#include <string>
#include <vector>

namespace gt3r::xr {

// An OpenXR structure with its type set and everything else zero.
template <class T>
T xr_init(XrStructureType type) {
    T v{};
    v.type = type;
    return v;
}

// Core functions, resolved through xrGetInstanceProcAddr once the instance exists.
#define GT3R_XR_FUNCTIONS(X)                                                                                  \
    X(xrDestroyInstance) X(xrGetInstanceProperties) X(xrPollEvent) X(xrResultToString)                        \
    X(xrGetSystemProperties) X(xrBeginSession) X(xrEndSession) X(xrDestroySession) X(xrRequestExitSession)    \
    X(xrCreateReferenceSpace) X(xrDestroySpace)                                                               \
    X(xrLocateSpace) X(xrLocateViews) X(xrEnumerateViewConfigurationViews) X(xrEnumerateSwapchainImages)      \
    X(xrWaitFrame) X(xrBeginFrame) X(xrEndFrame) X(xrAcquireSwapchainImage) X(xrWaitSwapchainImage)           \
    X(xrReleaseSwapchainImage) X(xrStringToPath) X(xrCreateActionSet) X(xrDestroyActionSet) X(xrCreateAction) \
    X(xrSuggestInteractionProfileBindings) X(xrAttachSessionActionSets) X(xrSyncActions)                      \
    X(xrGetActionStateBoolean) X(xrGetActionStateFloat) X(xrGetActionStateVector2f) X(xrGetActionStatePose)   \
    X(xrCreateActionSpace) X(xrApplyHapticFeedback) X(xrStopHapticFeedback)

struct Chain {
    XrSwapchain handle = XR_NULL_HANDLE;
    SDL_GPUTexture** images = nullptr; // indexed by xrAcquireSwapchainImage
    int w = 0, h = 0;
    u32 acquired = 0;
    bool held = false;
};

// Controllers (xr_input.cpp).
struct Input {
    XrActionSet set = XR_NULL_HANDLE;
    XrPath hand[2] = {}; // /user/hand/left, /user/hand/right
    XrAction trigger = XR_NULL_HANDLE, squeeze = XR_NULL_HANDLE, stick = XR_NULL_HANDLE, stick_click = XR_NULL_HANDLE;
    XrAction a = XR_NULL_HANDLE, b = XR_NULL_HANDLE, x = XR_NULL_HANDLE, y = XR_NULL_HANDLE, menu = XR_NULL_HANDLE;
    XrAction grip_pose = XR_NULL_HANDLE, aim_pose = XR_NULL_HANDLE, haptic = XR_NULL_HANDLE;
    XrSpace grip_space[2] = {}, aim_space[2] = {};
    // Last frame's controller state.
    float trigger_v[2] = {}, squeeze_v[2] = {};
    float stick_v[2][2] = {};
    bool click[2] = {}, btn_a = false, btn_b = false, btn_x = false, btn_y = false, btn_menu = false;
    bool grip_valid[2] = {}, aim_valid[2] = {};
    Pose grip[2], aim[2];
    // Steering wheel (Settings::wheel): held while both grips are squeezed.
    bool wheel_held = false;
    float wheel_zero = 0;  // hands' angle when grabbed (radians)
    float wheel_angle = 0; // current, relative to wheel_zero
    // Rumble from the virtual gamepad (0..1 per side), re-sent every frame while on.
    float rumble[2] = {};
    Uint64 rumble_until = 0;
    SDL_JoystickID pad_id = 0;
    SDL_Joystick* pad = nullptr;
};

struct Session::Impl {
    SDL_GPUDevice* device = nullptr;
    SDL_Renderer* renderer = nullptr;
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    PFN_xrGetInstanceProcAddr gipa = nullptr;
#define GT3R_XR_DECLARE(f) PFN_##f f = nullptr;
    GT3R_XR_FUNCTIONS(GT3R_XR_DECLARE)
#undef GT3R_XR_DECLARE
    // XR_FB_display_refresh_rate (optional)
    PFN_xrEnumerateDisplayRefreshRatesFB xrEnumerateDisplayRefreshRatesFB = nullptr;
    PFN_xrRequestDisplayRefreshRateFB xrRequestDisplayRefreshRateFB = nullptr;
    PFN_xrGetDisplayRefreshRateFB xrGetDisplayRefreshRateFB = nullptr;
    // XR_EXT_performance_settings (optional)
    PFN_xrPerfSettingsSetPerformanceLevelEXT xrPerfSettingsSetPerformanceLevelEXT = nullptr;
    std::vector<float> rates;
    float rate = 72;
    // XR_VALVE_frame_controller_interaction (optional): Steam Frame controllers' own bindings
    bool frame_controllers = false;

    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool running = false, exit = false, focus_lost = false;
    XrSpace base_local = XR_NULL_HANDLE; // the runtime's LOCAL space
    XrSpace local = XR_NULL_HANDLE;      // our seated space: LOCAL re-centred on the head
    XrSpace view_space = XR_NULL_HANDLE;
    bool centred = false; // the seated space has been centred once on a tracked head

    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
    bool srgb = true; // the swapchains are sRGB (present.frag decodes into them)
    // The present pass: canvases drawn into the swapchain images (a copy into them stalls).
    SDL_GPUGraphicsPipeline* present = nullptr;
    SDL_GPUSampler* present_sampler = nullptr;
    // The desktop's window (Session::mirror_to).
    SDL_Window* mirror = nullptr;
    SDL_GPUGraphicsPipeline* mirror_opaque = nullptr;
    SDL_GPUGraphicsPipeline* mirror_panel = nullptr; // the HUD over the eye
    SDL_GPUSampler* mirror_sampler = nullptr;
    SDL_PixelFormat canvas_format = SDL_PIXELFORMAT_ABGR8888; // = GpuContext::kColorFormat
    Chain eyes[2], quad;
    XrFrameState frame = xr_init<XrFrameState>(XR_TYPE_FRAME_STATE);
    bool frame_open = false;
    XrView views[2] = {xr_init<XrView>(XR_TYPE_VIEW), xr_init<XrView>(XR_TYPE_VIEW)};
    Pose head;

    Input input;

    // GT3R_VR_TIMING=1: where each frame's time goes, logged every few seconds (ms sums).
    struct Timing {
        bool on = false;
        Uint64 last_wait_end = 0, window_start = 0;
        double period = 0, wait = 0, app = 0, images = 0, copy = 0, end = 0;
        int frames = 0;
    } timing;

    bool check(XrResult r, const char* what) const;
    bool load_functions();
    bool make_chain(Chain& c, int w, int h);
    void release_chain(Chain& c);
    bool make_space(XrReferenceSpaceType type, const XrPosef& pose, XrSpace& out);
    SDL_GPUGraphicsPipeline* canvas_pipeline(SDL_GPUTextureFormat target, bool premultiplied);
    SDL_GPUSampler* canvas_sampler(SDL_GPUFilter filter);
    bool make_present_pipeline();
    void draw_mirror(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* canvas, int canvas_w, int canvas_h, SDL_GPUTexture* eye,
                     int eye_w, int eye_h);
    void release_mirror();
    void apply_refresh_rate(int hz);
    // xr_input.cpp
    bool create_input();
    void destroy_input();
};

XrPosef to_xr(const Pose& p);
Pose from_xr(const XrPosef& p);

// xr_runtimes.cpp: OpenXR runtimes on this computer besides the system's choice ([vr] runtime).
struct RuntimeFile {
    std::string key;      // the [vr] runtime setting: steamvr, meta
    std::string name;     // for Settings
    std::string manifest; // its JSON manifest (the loader's XR_RUNTIME_JSON)
};
std::vector<RuntimeFile> installed_runtimes(); // Windows; elsewhere none
std::string system_runtime();                  // the system's choice (Windows: the registry's), empty if unknown
bool steamvr_running();                        // its server
bool steamvr_compositor_running();
bool start_steamvr(const std::string& manifest); // as its own shortcut does (vrstartup)
bool same_file(const std::string& a, const std::string& b);

} // namespace gt3r::xr

#endif // GT3R_XR
