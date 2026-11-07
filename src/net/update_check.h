#pragma once

// Asks a relay server which game versions it announces (VersionQuery / VersionInfo;
// docs/dev/ONLINE.md "Versions and updates"). One small exchange on a background thread
// (the name lookup can block): up to three queries a second apart, then it gives up (about
// 3 s). Never blocks the caller; nothing is kept or sent besides the query.
//
//   net::VersionCheck check;
//   check.start("gt3r.gameversions.com", net::this_game_version(), net::this_platform());
//   every frame: if (auto r = check.take()) ...;

#include "net/rendezvous.h"

#include <memory>
#include <optional>
#include <string>

namespace gt3r::net {

// Which update dialog applies (docs/dev/ONLINE.md "Versions and updates"): Required when this
// game is below min_online (no relay sessions), Available when a newer release exists.
enum class UpdatePrompt : u8 { None, Available, Required };
UpdatePrompt update_prompt(const GameVersion& ours, const GameVersion& latest, const GameVersion& min_online);
// The release a prompt is about: latest (min_online when the server names no latest).
GameVersion prompt_version(const GameVersion& latest, const GameVersion& min_online);
// The prompt at startup (or when a check answers), shown once per release: unless the player
// chose Later for this release or a newer one (`dismissed`, 0.0.0: never).
bool show_startup_prompt(const GameVersion& ours, const GameVersion& latest, const GameVersion& min_online,
                         const GameVersion& dismissed);

// Release downloads (docs/dev/ONLINE.md "Versions and updates"). Each GitHub Release carries
// copies of its packages under stable names, so ".../releases/latest/download/<name>" is always
// the newest. The relay's release_url may hold an {asset} placeholder for that name.
enum class CpuArch : u8 { X64, Arm64, Other };
CpuArch this_arch();
// The stable package name for a platform ("": no package for it, e.g. Linux arm64, Intel macOS).
// `vr`: the VR build (GT3R_XR), whose packages are the Meta Quest APK and a Windows zip of its own.
std::string release_asset(RvPlatform platform, CpuArch arch, bool vr = false);
std::string this_release_asset(); // this build's
struct UpdateLinks {
    std::string download; // the package file (or the release page when there is none)
    std::string notes;    // the release page
};
// From the relay's URL: {asset} replaced by `asset`; the release page is the URL cut at
// "/download/" (GitHub: .../releases/latest). Without a placeholder, or without an asset for this
// platform, both are the page. Empty URL: both empty.
UpdateLinks update_links(const std::string& url, const std::string& asset);

class VersionCheck {
public:
    struct Result {
        bool answered = false;
        GameVersion latest, min_online; // 0.0.0: the server has none
        std::string url;                // the release page ("" if none)
        std::string error;              // !answered: why
    };
    VersionCheck() = default;
    ~VersionCheck(); // does not wait: a check still running finishes on its own
    VersionCheck(const VersionCheck&) = delete;
    VersionCheck& operator=(const VersionCheck&) = delete;

    // Starts a check (an earlier one still running is abandoned).
    void start(const std::string& server, const GameVersion& ours, RvPlatform platform, int timeout_ms = 3000);
    bool running() const;
    std::optional<Result> take(); // the answer, once (nullopt while running or when none started)

private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace gt3r::net
