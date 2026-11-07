#include "net/update_check.h"

#include "net/socket.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace gt3r::net {

struct VersionCheck::State {
    std::mutex mu;
    bool done = false, taken = false;
    Result result;
    std::atomic<bool> cancel{false};
};

namespace {

VersionCheck::Result run(const std::string& server, const GameVersion& ours, RvPlatform platform, int timeout_ms,
                         const std::atomic<bool>& cancel) {
    using Clock = std::chrono::steady_clock; // real time (tests may stop now_ms())
    VersionCheck::Result res;
    Address to;
    std::string err;
    if (!resolve(server, kDefaultRelayPort, to, &err)) {
        res.error = err.empty() ? "cannot find " + server : err;
        return res;
    }
    UdpSocket sock;
    if (!sock.open_for(to, &err)) {
        res.error = err;
        return res;
    }
    Writer q = rv_request(Rv::VersionQuery, 0, ours);
    q.u8_(u8(platform));
    rv_pad(q, kVersionQuerySize);
    const auto start = Clock::now(), end = start + std::chrono::milliseconds(timeout_ms);
    const auto every = std::chrono::milliseconds(std::max(1, timeout_ms / 3));
    auto next = start;
    while (!cancel) {
        auto now = Clock::now();
        if (now >= end) break;
        if (now >= next) {
            sock.send_to(to, q.data);
            next = now + every;
        }
        auto until = std::min(next, end);
        int wait = int(std::chrono::duration_cast<std::chrono::milliseconds>(until - now).count());
        if (!sock.wait_readable(std::clamp(wait, 1, 100))) continue;
        u8 buf[512];
        Address from;
        while (size_t n = sock.recv_from(from, buf, sizeof buf)) {
            if (from != to) continue;
            ByteSpan d(buf, n);
            Rv type;
            Reader r(d);
            if (u16 v = rv_version(d); v && v != kRendezvousVersion) {
                res.error = v < kRendezvousVersion ? "the relay server is out of date" : "the relay server is newer than the game";
                return res;
            }
            if (!rv_parse(d, type, r) || type != Rv::VersionInfo) continue;
            res.latest = get_version(r);
            res.min_online = get_version(r);
            res.url = r.str();
            if (!r.ok()) continue;
            if (res.url.size() > kMaxReleaseUrl || (res.url.rfind("https://", 0) != 0 && res.url.rfind("http://", 0) != 0))
                res.url.clear(); // only web pages are opened
            res.answered = true;
            return res;
        }
    }
    res.error = "no answer from " + server;
    return res;
}

} // namespace

CpuArch this_arch() {
#if defined(__x86_64__) || defined(_M_X64)
    return CpuArch::X64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return CpuArch::Arm64;
#else
    return CpuArch::Other;
#endif
}

std::string release_asset(RvPlatform platform, CpuArch arch, bool vr) {
    switch (platform) {
    case RvPlatform::Windows:
        if (arch != CpuArch::X64) return "";
        return vr ? "gt3r-windows-x64-vr.zip" : "gt3r-windows-x64.zip";
    case RvPlatform::MacOS: return arch == CpuArch::Arm64 ? "gt3r-macos-arm64.zip" : "";
    case RvPlatform::Linux: return arch == CpuArch::X64 ? "gt3r-linux-x86_64.tar.gz" : "";
    case RvPlatform::Android: // one APK for arm64-v8a and x86_64; the Quest APK is arm64 only
        if (vr) return arch == CpuArch::Arm64 ? "gt3r-quest.apk" : "";
        return arch == CpuArch::Other ? "" : "gt3r-android.apk";
    default: return "";
    }
}

std::string this_release_asset() {
    RvPlatform p = RvPlatform::Other;
#if defined(__ANDROID__)
    p = RvPlatform::Android;
#elif defined(_WIN32)
    p = RvPlatform::Windows;
#elif defined(__APPLE__)
    p = RvPlatform::MacOS;
#elif defined(__linux__)
    p = RvPlatform::Linux;
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
    p = RvPlatform::Other; // no iOS package
#endif
#if GT3R_XR
    return release_asset(p, this_arch(), true);
#else
    return release_asset(p, this_arch());
#endif
}

UpdateLinks update_links(const std::string& url, const std::string& asset) {
    static const std::string kPlaceholder = "{asset}";
    UpdateLinks l;
    size_t at = url.find(kPlaceholder);
    if (at == std::string::npos) {
        l.download = l.notes = url;
        return l;
    }
    size_t cut = url.rfind("/download/", at);
    if (cut != std::string::npos) {
        l.notes = url.substr(0, cut);
    } else { // not GitHub's layout: the folder the file is in
        size_t slash = url.rfind('/', at);
        l.notes = url.substr(0, slash == std::string::npos ? at : slash);
    }
    std::string direct = url;
    direct.replace(at, kPlaceholder.size(), asset);
    l.download = asset.empty() ? l.notes : direct;
    return l;
}

UpdatePrompt update_prompt(const GameVersion& ours, const GameVersion& latest, const GameVersion& min_online) {
    if (!meets_minimum(ours, min_online)) return UpdatePrompt::Required;
    if (update_available(ours, latest)) return UpdatePrompt::Available;
    return UpdatePrompt::None;
}

GameVersion prompt_version(const GameVersion& latest, const GameVersion& min_online) {
    GameVersion v = latest.known() ? latest : min_online;
    v.suffix.clear();
    return v;
}

bool show_startup_prompt(const GameVersion& ours, const GameVersion& latest, const GameVersion& min_online,
                         const GameVersion& dismissed) {
    if (update_prompt(ours, latest, min_online) == UpdatePrompt::None) return false;
    return !dismissed.known() || compare_core(prompt_version(latest, min_online), dismissed) > 0;
}

VersionCheck::~VersionCheck() {
    if (state_) state_->cancel = true;
}

void VersionCheck::start(const std::string& server, const GameVersion& ours, RvPlatform platform, int timeout_ms) {
    if (state_) state_->cancel = true;
    auto st = std::make_shared<State>();
    state_ = st;
    // Detached: a slow name lookup never holds up the game (or its exit). The thread only
    // touches its own shared state.
    std::thread([st, server, ours, platform, timeout_ms] {
        Result r = run(server, ours, platform, timeout_ms, st->cancel);
        std::lock_guard<std::mutex> lock(st->mu);
        st->result = std::move(r);
        st->done = true;
    }).detach();
}

bool VersionCheck::running() const {
    if (!state_) return false;
    std::lock_guard<std::mutex> lock(state_->mu);
    return !state_->done;
}

std::optional<VersionCheck::Result> VersionCheck::take() {
    if (!state_) return std::nullopt;
    std::lock_guard<std::mutex> lock(state_->mu);
    if (!state_->done || state_->taken) return std::nullopt;
    state_->taken = true;
    return state_->result;
}

} // namespace gt3r::net
