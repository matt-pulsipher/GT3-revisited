#pragma once

// Discord Rich Presence (issue 233; README "Discord"): what the player is doing, on their Discord
// profile ("Playing GT3 Revisited", "Arcade · Tsukuba Circuit", "Mazda RX-7 Type RS · Lap 2/5").
//
// No Discord library: the Discord app on the same computer listens on a local channel (a named
// pipe on Windows, a Unix socket elsewhere), and two kinds of message are all this needs. Each
// message is a frame: u32 opcode, u32 length (little-endian), then that many bytes of JSON.
//   Handshake (op 0)  {"v":1,"client_id":"<application id>"}   -> Frame {"evt":"READY",...}
//   Frame (op 1)      {"cmd":"SET_ACTIVITY","args":{"pid":..,"activity":{...}},"nonce":".."}
//   Ping (op 3)       answered with Pong (op 4), the same payload; Close (op 2): Discord hangs up.
// Programs that stand in for Discord's app (arRPC and the clients built on it) take the same
// frames on the same channel.
//
// Nothing goes over the network from the game: the Discord app shows the activity to the
// player's friends. Desktop only (Windows, macOS, Linux): phones and standalone headsets have
// no Discord app on the same device to talk to (supported() is false there).
//
//   DiscordPresence p;
//   p.start("1234567890");             // a background thread connects, and reconnects
//   p.set({"Arcade · Tsukuba", "Mazda RX-7 · Lap 2/5", start_unix});  // latest wins
//   p.stop();                           // clears the profile's activity and disconnects

#include "base/bytes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r::platform {

struct DiscordActivity {
    std::string details;     // the first line ("" none)
    std::string state;       // the second line ("" none)
    std::int64_t start = 0;  // Unix seconds the elapsed timer counts from (0: no timer)
    std::string large_image; // an art asset of the Discord application ("" none)
    std::string large_text;  // the image's tooltip
    int party_size = 0, party_max = 0; // "(2 of 8)" after the state (0: none)

    bool operator==(const DiscordActivity&) const = default;
};

class DiscordPresence {
public:
    // Whether this build can talk to a Discord app (desktop builds).
    static bool supported();

    DiscordPresence();
    ~DiscordPresence(); // stop()
    DiscordPresence(const DiscordPresence&) = delete;
    DiscordPresence& operator=(const DiscordPresence&) = delete;

    // Starts the background thread for this Discord application: it finds the Discord app
    // (again every 15 s while there is none) and sends the activity. A running thread for
    // another id is stopped first. Nothing happens when unsupported or `app_id` is empty.
    void start(const std::string& app_id);
    // Clears the activity from the profile and disconnects (waits for the thread, at most a
    // moment: it never blocks on the Discord app for long).
    void stop();
    bool running() const;

    // The activity to show (std::nullopt: none). Repeats of the same activity cost nothing; a
    // new one goes out at once when the last went 5 s ago or more (Discord takes about 5 every
    // 20 s), else when that time is up. The latest one wins.
    void set(const std::optional<DiscordActivity>& a);

    bool connected() const; // a Discord app answered the handshake
    std::string status() const; // one line for Settings ("Connected", "Discord is not running", ...)

    struct Shared; // with the background thread

private:
    std::shared_ptr<Shared> s_;
    std::string app_id_;
};

// The protocol, for the client above and tests.
namespace discord {

enum class Op : u32 { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };

// A JSON string's contents (no quotes): " \ and control characters escaped; UTF-8 kept.
std::string json_escape(std::string_view s);
// `s` cut to at most `max` bytes without splitting a UTF-8 character.
std::string utf8_cut(std::string_view s, size_t max);

std::string handshake_json(std::string_view app_id);
// SET_ACTIVITY for process `pid`; nullopt clears the activity. Discord refuses text fields
// under 2 or over 128 bytes: shorter ones are left out, longer ones cut.
std::string activity_json(const std::optional<DiscordActivity>& a, long long pid, unsigned nonce);

std::vector<u8> frame(Op op, std::string_view json);
// A frame at the front of `buf`: its size in bytes (header + payload), or 0 when `buf` does not
// hold a whole one yet. Payloads over 64 KiB are refused (-1).
long long frame_size(const u8* buf, size_t n);

// The Unix socket paths to try, in order: discord-ipc-0..9 under $XDG_RUNTIME_DIR, $TMPDIR,
// $TMP, $TEMP or /tmp, each also in the Flatpak and Snap Discord folders. `env` reads a variable
// (nullptr: unset).
std::vector<std::string> socket_paths(const std::function<const char*(const char*)>& env);

// A READY event in a Frame payload; an ERROR event's or a Close payload's message ("" none).
bool is_ready(std::string_view json);
std::string error_message(std::string_view json);

} // namespace discord

} // namespace gt3r::platform
