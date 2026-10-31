#include "app/gles_probe.h"

#include "base/log.h"
#include "render/gpu_context.h"

#include <SDL3/SDL.h>

#include <cstring>
#include <string>

#if defined(SDL_PLATFORM_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(SDL_PLATFORM_LINUX)
#include <unistd.h>
#endif

namespace gt3r {

namespace {

#if GT3R_GLES
constexpr Uint64 kProbeTimeoutMs = 10000; // no answer by then (a driver that hangs): it does not open

struct Probe {
    bool started = false;
    SDL_Process* process = nullptr;
    Uint64 started_ms = 0;
    std::optional<bool> opens;
};

Probe g_probes[2]; // by GlesKind

const char* probe_arg(GlesKind kind) { return kind == GlesKind::Angle ? "angle" : "driver"; }

// This executable, to start the copy; empty where there is none to start.
std::string own_path() {
#if defined(SDL_PLATFORM_WINDOWS)
    std::wstring w(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, w.data(), DWORD(w.size()));
        if (n == 0) return {};
        if (n < w.size()) {
            w.resize(n);
            break;
        }
        w.resize(w.size() * 2);
    }
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(size_t(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), len, nullptr, nullptr);
    return s;
#elif defined(SDL_PLATFORM_LINUX)
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf);
    return n > 0 && size_t(n) < sizeof buf ? std::string(buf, size_t(n)) : std::string();
#else
    return {};
#endif
}

void start(GlesKind kind, Probe& p) {
    p.started = true;
    p.started_ms = SDL_GetTicks();
    const std::string exe = own_path();
    if (!exe.empty()) {
        const char* args[] = {exe.c_str(), "--probe-3d", probe_arg(kind), nullptr};
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args);
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
        p.process = SDL_CreateProcessWithProperties(props);
        SDL_DestroyProperties(props);
        if (!p.process) LOG_WARN("OpenGL ES (%s): no probe (%s)", probe_arg(kind), SDL_GetError());
    }
    if (!p.process) p.opens = true; // no way to ask: offer it (a start without it falls back)
}

void poll(GlesKind kind, Probe& p) {
    if (!p.process) return;
    int code = -1;
    const bool exited = SDL_WaitProcess(p.process, false, &code);
    if (!exited && SDL_GetTicks() - p.started_ms < kProbeTimeoutMs) return;
    if (!exited) {
        SDL_KillProcess(p.process, true);
        SDL_WaitProcess(p.process, true, nullptr);
    }
    p.opens = exited && code == 0;
    LOG_INFO("OpenGL ES (%s): %s", probe_arg(kind),
             *p.opens ? "opens here" : exited ? "does not open here" : "no answer in 10 s; not offered");
    SDL_DestroyProcess(p.process);
    p.process = nullptr;
}
#endif

} // namespace

std::optional<bool> run_gles_probe(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--probe-3d") == 0) {
#if GT3R_GLES
            return render::gles::probe(std::strcmp(argv[i + 1], "angle") == 0);
#else
            return false;
#endif
        }
    return std::nullopt;
}

std::optional<bool> gles_opens(GlesKind kind, const render::GpuContext* gpu) {
#if GT3R_GLES
    if (gpu && gpu->gles() && (std::strstr(render::gles::description(), "(ANGLE") != nullptr) == (kind == GlesKind::Angle))
        return true;
    Probe& p = g_probes[int(kind)];
    if (!p.started) start(kind, p);
    poll(kind, p);
    return p.opens;
#else
    (void)kind;
    (void)gpu;
    return false;
#endif
}

} // namespace gt3r
