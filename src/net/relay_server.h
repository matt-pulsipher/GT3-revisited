#pragma once

// The rendezvous / relay server (tools/gt3r-relay; docs/dev/ONLINE.md "Server").
//
// - Session codes: a host registers and gets an 8-character code; players join with it.
// - Endpoint exchange: on a join, both sides learn each other's public address (as the
//   server sees it) and LAN addresses, then punch through their NATs directly.
// - Relay fallback (as TURN): a pair that cannot connect directly binds a channel and
//   the server forwards their game datagrams. Only datagrams that are game traffic
//   ("GT3N") are forwarded, between exactly the two bound addresses.
// - Link slots (admission control; ONLINE.md "Link slots and the queue"): the relay carries
//   at most `relay_links` racing links. A host reserves its lobby's relayed links when it
//   starts a race; what does not fit waits in a FIFO queue. Channels outside a reservation
//   (lobby traffic) are "idle": tightly rate limited, per channel and all together.
// - Abuse protection (ONLINE.md "Abuse protection"): every request that creates state or
//   makes the server send to someone else needs a return-routability cookie (a keyed MAC
//   of the source address and a time window), so spoofed sources get nothing but a
//   Challenge no larger than their request. Control datagrams are rate limited per
//   source IP (IPv6: per /64) and in total; every table is capped and expires.
// - Game versions (ONLINE.md "Versions and updates"): VersionQuery is answered with the
//   latest release, min_online and the release page; requests that create state from a
//   game below min_online are refused with Error(UpdateRequired). Unconfigured, every
//   version is allowed. Queries are only counted, never logged.
//
// The server never sees game state it needs to understand; it keeps no data on disk.
// Single threaded: call step() in a loop.

#include "net/rendezvous.h"
#include "net/socket.h"

#include <deque>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gt3r::net {

class RelayServer {
public:
    struct Limits {
        size_t max_sessions = 4000;
        size_t max_sessions_per_ip = 8;
        size_t max_channels = 20000;
        size_t max_pending = 8192;          // joins (Lookup nonces) not yet expired, all sessions
        size_t max_pending_per_session = 16;
        double session_timeout_ms = 60000; // no keepalive from the host
        double channel_timeout_ms = 60000; // no traffic
        double nonce_timeout_ms = 120000;  // a join that never bound its relay
        double rebind_after_ms = 10000;    // a bound address silent this long may be replaced (NAT rebinding)
        // Link slots and the queue.
        size_t relay_links = 1200;            // racing links the relay carries at once
        size_t max_queue = 100;               // lobbies waiting for slots
        double queue_timeout_ms = 600000;     // a queued lobby gives up after 10 min
        double queue_ping_timeout_ms = 15000; // a queued host must keep its keepalives coming
        double reservation_idle_ms = 10000;   // slots whose channels carried nothing this long are freed
        double skip_horizon_ms = 60000;       // a smaller lobby may go first if the head still starts within this
        double race_estimate_ms = 240000;     // typical race (slot hold) length until races measured it
        // Bandwidth guards (not admission): a racing link, a lobby link, all lobby links together.
        // (a car's states are ~0.07 Mbit/s: a host's channel to a player carries every other car's, up to 31 in a full field)
        double channel_bytes_per_s = 3e6 / 8;      // per racing channel and direction
        double idle_channel_bytes_per_s = 32e3;    // per lobby channel and direction (lobby messages, mod downloads)
        double idle_bytes_per_s = 20e6 / 8;        // all lobby channels together
        bool bench = false; // loopback sources skip the per-IP limits (gt3r-relay --bench; never on by default)
        double ip_msgs_per_s = 10, ip_msgs_burst = 40;      // control datagrams per source IP
        double ip_misses_per_s = 10 / 60.0, ip_misses_burst = 10; // lookups of codes that do not exist, per IP
        double msgs_per_s = 20000;         // control datagrams, all sources (a CPU guard)
        double misses_per_s = 200;         // failed lookups, all sources (beyond it every lookup waits)
        double joins_per_s = 0.5, joins_burst = 10; // Incoming notifications per session
        double cookie_window_ms = 30000;   // a cookie is good for 1 to 2 windows
    };
    // Game versions: what VersionQuery answers and the oldest game that may create or join
    // sessions (0.0.0: any). Release versions only (no suffix).
    struct Versions {
        GameVersion latest, min_online;
        std::string release_url; // at most kMaxReleaseUrl characters, http(s)://; may hold {asset} (net::update_links)
    };
    struct Stats {
        size_t sessions = 0, channels = 0, pending = 0;
        u64 version_queries = 0, queries_by_platform[kPlatformCount] = {}; // VersionQuery answered
        u64 outdated = 0; // requests refused with UpdateRequired
        size_t links_used = 0, links_capacity = 0, reservations = 0, queued = 0;
        u64 relayed_datagrams = 0, relayed_bytes = 0, dropped = 0;
        u64 challenges = 0, limited = 0; // cookie challenges sent; datagrams refused by a rate limit
        u64 granted = 0, queue_expired = 0; // reservations granted (at once or from the queue); queued lobbies dropped
        double peak_channel_bytes_per_s = 0; // the busiest channel direction in any whole second (since reset_peak)
        double race_estimate_ms = 0;         // the current typical slot hold time
    };
    // A queued lobby's place (tests): 1-based position (0: not queued) and estimated wait.
    struct QueueInfo {
        size_t position = 0;
        double wait_ms = -1; // < 0: unknown
    };

    RelayServer();
    bool open(u16 port, std::string* error = nullptr);
    u16 port() const { return sock_.local_port(); }
    int set_buffers(int bytes) { return sock_.set_buffers(bytes); } // the kernel's receive buffer it got
    // Waits up to wait_ms for datagrams, handles everything that arrived, expires old state.
    void step(int wait_ms);
    // Handles one datagram as if it arrived on the socket (tests, fuzzing).
    void receive(const Address& from, ByteSpan data, double now) { handle(from, data, now); }
    void set_limits(const Limits& l) {
        limits_ = l;
        race_ms_ = l.race_estimate_ms;
    }
    const Limits& limits() const { return limits_; }
    // Takes effect at once (a reload keeps every session, channel and reservation).
    void set_versions(const Versions& v) { versions_ = v; }
    const Versions& versions() const { return versions_; }
    void set_log(std::function<void(const std::string&)> f) { log_ = std::move(f); }
    // Players' IPs in the log (gt3r-relay --log-ips) instead of daily tags (ip_tag; the default).
    void set_log_ips(bool on) { log_ips_ = on; }
    // Each player's country after the tag / IP ("a1b2c3d4 (BR)"; "??" when unknown; gt3r-relay --geoip).
    void set_log_country(std::function<std::string(const Address&)> f) { country_ = std::move(f); }
    Stats stats() const;
    void reset_peak() { stats_.peak_channel_bytes_per_s = 0; }
    QueueInfo queue_info(const std::string& code, double now) const;
    size_t links_of(const std::string& code) const; // granted slots (tests)
    std::string any_code() const { return sessions_.empty() ? "" : sessions_.begin()->first; } // fuzzing
    void tick(double now) { expire(now); } // expiry and the queue, as step() runs them (tests)
    // Tests: every datagram the server sends (replies, relayed data) goes to `hook` instead of
    // the socket, synchronously and in order.
    void set_send_hook(std::function<void(const Address& to, ByteSpan data)> hook) { send_hook_ = std::move(hook); }
    // The cookie `from` must send now (tests).
    u64 cookie_for(const Address& from, double now) const { return cookie(from, window(now)); }

private:
    struct Bucket { // token bucket
        double tokens = -1, at = 0; // -1: full (not used yet)
        bool has(double rate, double burst, double now, double cost = 1);
        bool take(double rate, double burst, double now, double cost = 1);
    };
    struct IpSlot {
        Bucket msgs, misses;
    };
    struct Session {
        std::string code;
        Address host;
        u64 token = 0;
        u64 serial = 0; // tells a reused code apart
        std::vector<std::string> host_eps; // LAN
        double last_seen = 0;
        size_t pending = 0; // its joins in nonces_
        Bucket joins;
    };
    struct Nonce {
        std::string code;
        u64 serial = 0;
        double created = 0;
    };
    struct Channel {
        Address end[2];
        double seen[2] = {0, 0};
        double last_used = 0;  // traffic or a bind (expiry)
        double last_data = 0;  // relayed traffic (a reservation's idle timeout)
        double budget[2] = {0, 0}; // token bucket per direction, bytes
        double refill_at = 0;
        u64 serial = 0;       // the session it belongs to
        bool active = false;  // carries a link slot of its session's reservation (else: idle, lobby rate)
        double win_at = 0, win_bytes[2] = {0, 0}; // this second's bytes per direction (peak stat)
    };
    // Link slots held by a racing lobby (by session serial).
    struct Reservation {
        std::string code;
        size_t links = 0;
        double granted = 0;
        std::vector<u64> channels; // its active channels (at most `links`)
    };
    struct Queued {
        std::string code;
        u64 serial = 0;
        size_t links = 0;
        double requested = 0;
    };

    void handle(const Address& from, ByteSpan data, double now);
    void handle_control(const Address& from, Rv type, Reader& r, const GameVersion& game, double now, IpSlot& ip);
    void version_info(const Address& from, Reader& r, size_t request_size);
    bool outdated(const GameVersion& game) const { return !meets_minimum(game, versions_.min_online); }
    void update_required(const Address& to);
    void relay(const Address& from, ByteSpan data, double now);
    void send(const Address& to, const Writer& w) { send_to(to, w.data); }
    void send_to(const Address& to, ByteSpan data) {
        if (send_hook_) send_hook_(to, data);
        else sock_.send_to(to, data);
    }
    void error(const Address& to, RvError code, const char* text);
    void refused(const Address& to, const char* text); // a Reserve that cannot be granted or queued
    void version_error(const Address& to, u16 version, size_t request_size);
    void registered(const Address& to, const std::string& code);
    void erase_session(std::map<std::string, Session>::iterator it, double now);
    void forget_nonce(const Nonce& n);
    void move_host(Session& s, const Address& to);
    void expire(double now);
    // Slots.
    void reserve(const Address& from, Session& s, size_t links, double now);
    void release(u64 serial, double now, const char* why);
    void schedule(double now);
    void grant(const Queued& q, double now);
    void slot_status(const Address& to, const Session& s, double now);
    size_t free_links() const { return limits_.relay_links > links_used_ ? limits_.relay_links - links_used_ : 0; }
    double last_traffic(const Reservation& r) const;
    // When `need` slots are free (ms from now; < 0: unknown) if `extra` more start now.
    double start_estimate(size_t need, size_t extra, double now) const;
    std::vector<double> queue_waits(double now) const; // per queue entry (ms; < 0: unknown)
    bool trusted(const Address& a) const; // --bench: loopback
    void log(const std::string& s) const {
        if (log_) log_(s);
    }
    u64 random();
    u64 window(double now) const;
    u64 cookie(const Address& a, u64 window) const;
    static std::string ip_key(const Address& a); // 16 bytes: the IP (IPv6: its /64)
    std::string ip_tag(const Address& a);        // a keyed hash of ip_key, new key each UTC day
    static std::string ip_of(const Address& a);  // the IP, no port
    std::string who(const Address& a); // for the log: the tag (or IP) and the country
    IpSlot& slot(const Address& a);

    UdpSocket sock_;
    Limits limits_;
    Versions versions_;
    std::function<void(const std::string&)> log_;
    std::function<std::string(const Address&)> country_;
    std::function<void(const Address&, ByteSpan)> send_hook_;
    std::map<std::string, Session> sessions_; // by code
    std::unordered_map<u64, std::string> by_token_;          // session token -> code
    std::unordered_map<std::string, size_t> sessions_per_ip_; // ip_key -> sessions
    std::map<u64, Nonce> nonces_;
    std::map<u64, Channel> channels_;
    std::unordered_map<u64, Reservation> reservations_; // by session serial
    std::deque<Queued> queue_;
    size_t links_used_ = 0;
    double race_ms_ = 0; // typical slot hold time (measured; starts at Limits::race_estimate_ms)
    static constexpr size_t kIpSlots = 65536; // hashed (keyed): fixed memory however many sources
    std::vector<IpSlot> ip_slots_;
    Bucket all_msgs_, all_misses_, all_idle_;
    u8 cookie_key_[16] = {}, slot_key_[16] = {}, rng_key_[16] = {}; // random per process
    u8 tag_key_[16] = {}; // ip_tag's, for UTC day tag_day_ (never written anywhere)
    long long tag_day_ = -1;
    bool log_ips_ = false;
    u64 rng_counter_ = 0, serial_ = 0;
    double last_expire_ = 0;
    Stats stats_;
};

// The relay's versions file (gt3r-relay --versions-file; tools/gt3r-relay/set-versions.sh writes
// it): "key = value" lines, '#' comments. Keys: latest, min_online, release_url. A key left out
// keeps the value `out` has (the flags, else 0.0.0 / none: every version allowed). Fails on
// unknown keys, bad versions, a bad URL, or min_online above latest; `out` is then unchanged.
bool parse_relay_versions(std::string_view text, RelayServer::Versions& out, std::string* error = nullptr);
// Checks values from flags the same way.
bool check_relay_versions(const RelayServer::Versions& v, std::string* error = nullptr);

} // namespace gt3r::net
