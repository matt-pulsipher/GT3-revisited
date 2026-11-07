#if GT3R_XR

// The OpenXR runtimes a desktop has besides the system's choice, for [vr] runtime (docs/dev/VR.md
// "PC VR"). The loader runs the system's (Windows: the registry's ActiveRuntime) unless
// XR_RUNTIME_JSON names another's manifest. A PC often has two: Meta's (the active one once
// Meta Horizon Link is installed) and SteamVR's, which registers only while it is the active one,
// so its manifest is found through OpenVR's own paths file.

#include "xr/xr_internal.h"

#include "base/file.h"
#include "base/config.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace gt3r::xr {

#ifdef _WIN32

namespace {

const char kKhronosKey[] = "SOFTWARE\\Khronos\\OpenXR\\1";

std::string env(const char* name) {
    const char* v = SDL_getenv(name);
    return v ? v : "";
}

// %LOCALAPPDATA%\openvr\openvrpaths.vrpath: {"runtime": ["C:\\...\\SteamVR"], ...}; its manifest
// is steamxr_win64.json there.
std::string steamvr_manifest() {
    const std::optional<Bytes> file = read_host_file(env("LOCALAPPDATA") + "\\openvr\\openvrpaths.vrpath");
    if (!file) return {};
    const std::string text(file->begin(), file->end());
    size_t at = text.find("\"runtime\"");
    if (at == std::string::npos || (at = text.find('[', at)) == std::string::npos ||
        (at = text.find('"', at)) == std::string::npos)
        return {};
    std::string dir;
    for (size_t i = at + 1; i < text.size() && text[i] != '"'; ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) ++i; // JSON escapes: \\ in paths
        dir += text[i];
    }
    const std::string manifest = dir + "\\steamxr_win64.json";
    return !dir.empty() && host_path_exists(manifest) ? manifest : std::string{};
}

// Meta's: registered under AvailableRuntimes (the values' names are the manifests), else where
// Meta Horizon Link (formerly the Oculus app) installs it.
std::string meta_manifest() {
    auto is_meta = [](const std::string& m) { return to_lower(m).find("oculus_openxr") != std::string::npos; };
    HKEY key = nullptr;
    std::string found;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, (std::string(kKhronosKey) + "\\AvailableRuntimes").c_str(), 0,
                      KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        char name[1024];
        for (DWORD i = 0;; ++i) {
            DWORD len = sizeof(name);
            if (RegEnumValueA(key, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            if (is_meta(name) && host_path_exists(name)) {
                found = name;
                break;
            }
        }
        RegCloseKey(key);
    }
    if (found.empty() && is_meta(system_runtime())) found = system_runtime();
    for (const char* dir : {"\\Meta Horizon\\Support\\oculus-runtime\\", "\\Oculus\\Support\\oculus-runtime\\"})
        if (found.empty() && host_path_exists(env("ProgramFiles") + dir + "oculus_openxr_64.json"))
            found = env("ProgramFiles") + dir + "oculus_openxr_64.json";
    return found;
}

bool process_running(const wchar_t* exe) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &e); ok && !found; ok = Process32NextW(snap, &e))
        found = _wcsicmp(e.szExeFile, exe) == 0;
    CloseHandle(snap);
    return found;
}

} // namespace

std::vector<RuntimeFile> installed_runtimes() {
    std::vector<RuntimeFile> r;
    if (std::string m = steamvr_manifest(); !m.empty()) r.push_back({"steamvr", "SteamVR", m});
    if (std::string m = meta_manifest(); !m.empty()) r.push_back({"meta", "Meta Quest Link", m});
    return r;
}

std::string system_runtime() {
    char buf[2048];
    DWORD size = sizeof(buf);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, kKhronosKey, "ActiveRuntime", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr,
                     buf, &size) == ERROR_SUCCESS)
        return buf;
    return {};
}

bool steamvr_running() { return process_running(L"vrserver.exe"); }
bool steamvr_compositor_running() { return process_running(L"vrcompositor.exe"); }

bool start_steamvr(const std::string& manifest) {
    const size_t slash = manifest.find_last_of("\\/");
    if (slash == std::string::npos) return false;
    const std::string exe = manifest.substr(0, slash) + "\\bin\\win64\\vrstartup.exe";
    const char* args[] = {exe.c_str(), nullptr};
    SDL_Process* p = host_path_exists(exe) ? SDL_CreateProcess(args, false) : nullptr;
    if (!p) return false;
    SDL_DestroyProcess(p); // (it keeps running)
    return true;
}

bool same_file(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    auto norm = [](std::string s) {
        for (char& c : s)
            if (c == '/') c = '\\';
        return to_lower(s);
    };
    return norm(a) == norm(b);
}

#else

std::vector<RuntimeFile> installed_runtimes() { return {}; }
std::string system_runtime() { return {}; }
bool steamvr_running() { return false; }
bool steamvr_compositor_running() { return false; }
bool start_steamvr(const std::string&) { return false; }
bool same_file(const std::string& a, const std::string& b) { return !a.empty() && a == b; }

#endif

} // namespace gt3r::xr

#endif // GT3R_XR
