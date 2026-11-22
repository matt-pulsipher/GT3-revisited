#pragma once

// The rendezvous / relay server's wire format (tools/gt3r-relay, docs/dev/ONLINE.md
// "Server"). Shared by the server (net::RelayServer) and the game (net::Session).
//
// Control datagrams: "GT3S", u16 kRendezvousVersion, u8 type, body.
// Requests (-> S) carry a u64 cookie and the game's version (u16 major, minor, patch) first
// and are zero-padded to kMinRequest bytes. The cookie proves the sender receives at its
// source address: a request with a missing / stale cookie only gets a Challenge (smaller
// than the request) back. Requests that create state (Register, Lookup, Bind of a new
// channel end, Reserve) from a game older than the server's min_online get
// Error(UpdateRequired) instead (after the cookie check).
//   Register   host -> S   u64 cookie, ver, u64 token, u8 n, {str endpoint} (LAN)       -> Registered / Error
//   Registered S -> host   str code, str public endpoint
//   Keepalive  host -> S   u64 cookie, ver, str code, u64 token (every few seconds; also keeps NAT mappings)
//   Lookup     client -> S u64 cookie, ver, str code, u16 game protocol, u8 n, {str endpoint} -> HostInfo / Error
//   HostInfo   S -> client u64 nonce, str public endpoint (the client's), u8 n, {str endpoint} (the host's)
//   Incoming   S -> host   u64 nonce, u8 n, {str endpoint} (the client's: public first)
//   Bind       * -> S      u64 cookie, ver, u64 key, str code, u64 token, u8 a, u8 b  -> Bound / Error
//              (a join pair: key = the Lookup nonce, token 0; two players: key =
//              pair_key(token, a, b) with the session token the host registered)
//   Bound      S -> *      u64 key
//   Unregister host -> S   u64 cookie (not checked: it creates nothing), ver, str code, u64 token
//   Error      S -> *      u8 code, str text (fixed server text; never echoes the request)
//              [UpdateRequired: then ver latest, str release page URL]
//   Challenge  S -> *      u8 request type, u64 cookie (resend that request with it)
//   Reserve    host -> S   u64 cookie, ver, str code, u64 token, u8 links -> Reservation / Error
//              (race start: link slots for the lobby's relayed pairs; granted or queued)
//   Reservation S -> host  u8 RvSlot state, u8 links, u16 position (queued: 1 = next), u16 wait s
//              (0xFFFF: unknown) [, str text: why, when Refused]. Also the reply to a Keepalive
//              while the session holds slots or waits for them, and sent unasked when queued
//              slots are granted.
//   Release    host -> S   u64 cookie (not checked: it frees, creates nothing), ver, str code, u64 token
//              (race over, or the host cancels its place in the queue)
//   VersionQuery game -> S u64 cookie (not checked: it creates nothing), ver, u8 RvPlatform;
//              zero-padded to kVersionQuerySize                                        -> VersionInfo
//   VersionInfo S -> game  ver latest, ver min_online (0.0.0: none), str release page URL (at
//              most kMaxReleaseUrl characters, so the reply is never larger than the query)
// A request with another protocol version gets Error(Version) in its own version's header
// (the Error layout is the same in every version), so an old game can show "update".
// Relayed data: "GT3R", u64 key, then one game datagram ("GT3N..."), forwarded unchanged
// between the two addresses bound to the key. Nothing else is relayed.

#include "base/bytes.h"
#include "net/protocol.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r::net {

// 2: cookies, padded requests, 8-character codes, link slots. 3: game versions in every request,
// VersionQuery / VersionInfo, Error(UpdateRequired).
constexpr u16 kRendezvousVersion = 3;
constexpr u16 kDefaultRelayPort = 47631;
constexpr size_t kMinRequest = 64;    // requests are padded to this (every reply to an unverified source is smaller)
constexpr size_t kVersionQuerySize = 160; // a VersionQuery is padded to this; its VersionInfo is smaller
constexpr size_t kMaxReleaseUrl = 128;    // the release page URL a server hands out (VersionInfo fits the query)
constexpr size_t kRvHeader = 7;       // magic + version + type

enum class Rv : u8 {
    Register = 1,
    Registered = 2,
    Keepalive = 3,
    Lookup = 4,
    HostInfo = 5,
    Incoming = 6,
    Bind = 7,
    Bound = 8,
    Unregister = 9,
    Error = 10,
    Challenge = 11,
    Reserve = 12,
    Reservation = 13,
    Release = 14,
    VersionQuery = 15,
    VersionInfo = 16,
};

// A Reservation's state.
enum class RvSlot : u8 { Granted = 0, Queued = 1, Released = 2, Refused = 3 };
constexpr u16 kWaitUnknown = 0xFFFF;
constexpr int kMaxLinks = kMaxPlayers * (kMaxPlayers - 1) / 2; // relayed pairs in one race (120)

enum class RvError : u8 { BadRequest = 1, NotFound = 2, Version = 3, Busy = 4, Denied = 5, UpdateRequired = 6 };

// What a VersionQuery says it runs on (the server only counts them).
enum class RvPlatform : u8 { Unknown = 0, Windows = 1, MacOS = 2, Linux = 3, Android = 4, Other = 5 };
constexpr int kPlatformCount = 6;
const char* platform_name(RvPlatform p); // "windows", ... ("unknown" for anything else)

// A game version: x.y.z, plus an optional suffix for local builds ("-dev"; never sent to the
// server). Rules (docs/dev/ONLINE.md "Versions and updates"):
// - online play needs x.y.z >= the server's min_online; the suffix does not count, so a
//   0.2.0-dev build plays where 0.2.0 does (meets_minimum);
// - the update notice shows when the server's latest release is newer: latest > x.y.z for a
//   release, latest >= x.y.z for a suffixed build (a 0.2.0-dev build is older than the 0.2.0
//   release) (update_available);
// - 0.0.0 is "unknown": a min_online of 0.0.0 allows everything, a latest of 0.0.0 is no release.
struct GameVersion {
    u16 major = 0, minor = 0, patch = 0;
    std::string suffix;
    bool known() const { return major || minor || patch; }
    std::string number() const;    // "0.2.0"
    std::string to_string() const; // "0.2.0-dev"
};
// "x.y.z" or "x.y.z-suffix" (each part 0..65535; suffix: letters, digits, '.', '-'), else nullopt.
std::optional<GameVersion> parse_game_version(std::string_view text);
int compare_core(const GameVersion& a, const GameVersion& b); // x.y.z only: < 0, 0, > 0
bool meets_minimum(const GameVersion& ours, const GameVersion& min_online);
bool update_available(const GameVersion& ours, const GameVersion& latest);
void put_version(Writer& w, const GameVersion& v); // u16 major, minor, patch
GameVersion get_version(Reader& r);

inline constexpr char kRvMagic[4] = {'G', 'T', '3', 'S'};
inline constexpr char kRelayMagic[4] = {'G', 'T', '3', 'R'};
constexpr size_t kRelayHeader = 12; // magic + key

// Starts a control datagram (a reply, or with `version`, another version's header).
Writer rv_begin(Rv type, u16 version = kRendezvousVersion);
// Starts a request: the header, the cookie and the game's version. Finish it with rv_pad.
Writer rv_request(Rv type, u64 cookie, const GameVersion& game = {});
void rv_pad(Writer& w, size_t size = kMinRequest); // zero padding up to `size`
// Parses a control datagram's header; the reader is left at the body.
bool rv_parse(ByteSpan data, Rv& type, Reader& body);
// The version of a control datagram (0: not one).
u16 rv_version(ByteSpan data);

void put_u64(Writer& w, u64 v);
u64 get_u64(Reader& r);
void put_endpoints(Writer& w, const std::vector<std::string>& eps, size_t max = 6);
std::vector<std::string> get_endpoints(Reader& r, size_t max = 6);

// The relay channel of two players of a session (FNV-1a over token, lower id, higher id;
// never 0). Both players and the server compute it.
u64 pair_key(u64 token, u8 a, u8 b);

// SipHash-2-4 (Aumasson & Bernstein): a keyed 64-bit MAC / PRF. The server's cookies,
// bucket hashing and random codes / nonces use it.
u64 siphash24(const u8 key[16], ByteSpan data);

// Session codes: 8 characters from an alphabet without look-alikes (no 0/O, 1/I/L):
// 31^8 = 8.5e11 codes (~40 bits). Shown as "ABCD-EFGH"; typed with or without the dash.
constexpr size_t kCodeLength = 8;
std::string make_code(u64 random);
bool valid_code(const std::string& code);
std::string normalize_code(const std::string& text); // upper case, spaces and dashes removed
std::string format_code(const std::string& code);    // "ABCD-EFGH" (other text unchanged)

} // namespace gt3r::net
