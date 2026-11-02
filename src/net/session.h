#pragma once

// An online battle session (the original's i.LINK Battle over IP; docs/dev/ONLINE.md).
//
// One player hosts (player id 0, binds a UDP port); up to five join. The host runs the
// lobby (roster, settings, load, go) on a small reliable, ordered channel per
// connection. Every player simulates only its own car and sends its state every tick.
//
// Race traffic is a mesh: clients also connect to each other directly. The host hands
// out each player's endpoints (the address it sees them at, plus their LAN addresses)
// and both sides send "punch" datagrams to every candidate at once, which opens the
// NAT mappings on both ends (UDP hole punching). A pair that cannot connect within a
// few seconds stays relayed: the host forwards their states. Clocks are kept in step
// with the host by NTP-style ping exchanges (the minimum-round-trip sample wins), so
// the countdown starts at the same instant everywhere.
//
//   net::Session s;
//   s.host(net::kDefaultPort, "Matt", "r34_gtr_v_99", 0);  // or s.join("1.2.3.4", ...)
//   every frame: s.poll(); while (auto e = s.next_event()) ...;
//   host: s.set_settings({...}); when s.all_ready(): s.load(); when s.all_loaded(): s.go(1500);
//   race: s.send_state(snapshot) every tick, s.remote(id) for the others, s.send_finish(t, best).

#include "net/protocol.h"
#include "net/rendezvous.h"
#include "net/socket.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace gt3r::net {

struct Player {
    u8 id = 0;
    std::string name, car;
    u8 paint = 0;
    bool ready = false, loaded = false;
    u16 ping_ms = 0;                    // the host's round trip to this player
    std::vector<std::string> endpoints; // where peers can reach it: as the host sees it, then its LAN addresses
};

// Lobby settings (the host chooses).
struct Settings {
    std::string course;
    int laps = 2;
    bool allow_mods = true;  // mod cars may be raced (others are offered the download)
    u16 ghost_ping_ms = 150; // a pair whose round trip is above this does not collide (0 = always collide)
    bool stock_only = false; // no tuned GT Mode garage cars
    u16 max_ps = 0;          // power limit (PS at the crank, the car's own spec; 0 = none)
    // More car rules (game::CarRules; 0 = any): the arcade class (1 C .. 4 S, 5 rally), the
    // category (1 production, 2 race and rally), the drivetrain (1 FF, 2 FR, 3 4WD, 4 MR, 5 RR)
    // and the aspiration (1 NA, 2 turbo).
    u8 car_class = 0, category = 0, drive = 0, aspiration = 0;
};

// How this player reaches another one.
struct Link {
    bool known = false;  // a player we have a path to (direct, or through the host)
    bool direct = false; // our own connection, straight to them
    bool relay = false;  // our own connection, through the relay server
    double rtt_ms = 0;   // round trip (relayed: to the host and on from it)
};

// A race waiting for the relay server's link slots (docs/dev/ONLINE.md "Link slots and the
// queue"). Through a relay server, a race whose pairs go through the relay starts once the
// server granted its links; when the relay is full the lobby waits in line.
struct RelayWait {
    bool waiting = false; // the host asked for slots; the race starts when they are granted
    int position = 0;     // in the relay's queue (1 = next; 0 = not told yet)
    int wait_s = -1;      // the relay's estimate (< 0: unknown)
};
// "Relay busy: 2nd in line, about 3 min" (the lobby's text for it).
std::string relay_wait_text(const RelayWait& w);

// This build's version (gt3r_version.h: CMakeLists.txt's project VERSION) and platform.
const GameVersion& this_game_version();
RvPlatform this_platform();
// Why a direct-IP join was refused for its version (ours, the host's).
std::string version_mismatch(const GameVersion& ours, const GameVersion& host);
// Why a game speaking another online protocol (theirs) was refused by this host: who must update.
std::string protocol_mismatch(u16 theirs);

// The relay server refused us because this game is older than its min_online
// (docs/dev/ONLINE.md "Versions and updates").
struct UpdateRequired {
    GameVersion latest; // 0.0.0: not told
    std::string url;    // the release page ("" if the server has none)
};

struct Event {
    enum class Kind {
        Joined,       // client: the host accepted us (my_id() is valid)
        Rejected,     // client: text = reason; the session is closed
        Roster,       // players() changed
        Settings,     // settings() changed
        Link,         // player: our path to them changed (link(player))
        CarInfo,      // player: their car descriptor changed (car_info(player))
        PackageDone,  // text = package hash: downloaded and verified (take_package)
        PackageFailed,// text = package hash, course = reason
        Contact,      // player = who hit us: contact (point / impulse on our car)
        Flag,         // host: player = suspect, flag (kind, value, reporter)
        Load,         // load the race: course, laps, grid (player ids, pole first)
        Go,           // the countdown starts at start_at_ms (now_ms() clock; race_start_ms() refines it)
        Finish,       // player finished: time, best
        Retire,       // player left the race (or the session)
        Lobby,        // the race is over: back to the lobby
        Queue,        // host: the relay refused the race's link slots (text = why); still in the lobby
        Disconnected, // text = reason; the session is closed
    };
    Kind kind = Kind::Roster;
    std::string text;
    u8 player = 0;
    double time = 0, best = -1;
    std::string course;
    int laps = 0;
    std::vector<u8> grid;
    double start_at_ms = 0;
    struct Hit {
        u32 tick = 0;
        float point_x = 0, point_z = 0, jx = 0, jz = 0;
    } contact;
    struct FlagInfo {
        FlagKind kind = FlagKind::Speed;
        float value = 0;
        u8 reporter = 0;
    } flag;
};

class Session {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool host(u16 port, const std::string& name, const std::string& car, u8 paint, std::string* error = nullptr);
    bool join(const std::string& address, const std::string& name, const std::string& car, u8 paint,
              std::string* error = nullptr);
    // Through a rendezvous / relay server (docs/dev/ONLINE.md "Server"): the host gets a
    // session code (code(), once registered) and needs no port forward; players join with
    // the code. Every pair punches through directly and falls back to the server's relay.
    bool host_via(const std::string& server, const std::string& name, const std::string& car, u8 paint,
                  std::string* error = nullptr);
    bool join_code(const std::string& server, const std::string& code, const std::string& name, const std::string& car,
                   u8 paint, std::string* error = nullptr);
    const std::string& code() const { return code_; } // empty until the server registered the host
    // Set when the relay server answered UpdateRequired (the session then ends with Disconnected /
    // Rejected, or, at a race start, a Queue event); kept after it closed until the next start.
    const std::optional<UpdateRequired>& update_required() const { return update_required_; }
    // Tests: play as another game version (default: this build's).
    void set_game_version(const GameVersion& v) { version_ = v; }
    bool via_server() const { return server_.valid(); }

    void close(); // tells the others (best effort)

    // Receive, deliver, resend, punch, time out, flush. Call every frame (and after send_state).
    void poll();
    std::optional<Event> next_event();

    bool active() const { return sock_.is_open(); }
    bool is_host() const { return host_; }
    bool joined() const { return active() && (host_ || my_id_ >= 0); }
    int my_id() const { return my_id_; }
    u16 port() const { return sock_.local_port(); }
    const std::vector<Player>& players() const { return players_; }
    const Player* player(u8 id) const;
    const Settings& settings() const { return settings_; }
    const std::string& course() const { return settings_.course; }
    int laps() const { return settings_.laps; }
    const std::vector<u8>& grid() const { return grid_; } // the last Load's grid (pole first)
    bool in_lobby() const { return phase_ == Phase::Lobby; }
    double rtt_ms() const; // client: to the host; host: the largest to any client
    Link link(u8 id) const;
    // Host clock - our clock in ms (0 on the host), from the best ping exchange so far.
    double clock_offset_ms() const;
    // Our clock (now()) at the countdown start of the current race; 0 before Go.
    double race_start_ms() const;
    double now() const; // now_ms() (+ the test skew)

    // Lobby.
    // Our car descriptor (game::OnlineCar, opaque here; at most 1 KB), sent to everyone.
    void set_car_info(const Bytes& info);
    const Bytes* car_info(u8 id) const; // nullptr until that player sent one
    void set_car(const std::string& car, u8 paint);
    void set_ready(bool ready);
    void set_settings(const Settings& s); // host
    bool all_ready() const;               // host: everyone (two or more) ready
    // Host: Load with the roster as grid. Through a relay server, when some pairs go through
    // the relay, it first reserves their link slots; the Load goes out once they are granted
    // (relay_wait() meanwhile: the race may wait in the relay's queue).
    void load();
    void cancel_start();                   // host: stop waiting for the relay (leaves the queue)
    const RelayWait& relay_wait() const { return wait_; } // host: ours; clients: as the host tells
    int relay_links_needed() const;        // host: pairs of this lobby that go through the relay server
    void loaded();                        // the local race scene is ready
    bool all_loaded() const;              // host
    void go(u32 delay_ms);                // host: start the countdown
    void lobby();                         // host: back to the lobby

    // Mod car packages: blobs addressed by their SHA-256 (lower-case hex). Ours are offered;
    // others' are fetched through the host (which keeps a verified copy for the next player).
    // Transfers only run in the lobby, paced so lobby messages are not held up.
    void offer_package(const std::string& hash, Bytes blob);
    void request_package(const std::string& hash, u8 owner);
    struct Download {
        u32 total = 0, received = 0;
        bool done = false, failed = false;
    };
    const Download* download(const std::string& hash) const;
    std::optional<Bytes> take_package(const std::string& hash); // a finished download (once)

    // Race.
    void send_state(const CarSnapshot& s);
    struct Remote {
        CarSnapshot snap;
        double recv_ms = 0;
        u32 count = 0; // snapshots received this race
    };
    const Remote* remote(u8 id) const; // nullptr until a state arrived
    void send_finish(double race_time, double best_lap);
    void retire(); // leaving the race (the session stays)
    // A hit our machine saw between our car and player `target`'s: the impulse their car felt.
    void send_contact(u8 target, const Event::Hit& hit);
    // Proximity voice: one μ-law frame (unreliable; host relays like State). `seq` is a rolling byte.
    void send_voice(u8 seq, ByteSpan mulaw);
    // Voice frames received since the last call (player, seq, payload). Drained each poll/frame.
    struct VoiceRx {
        u8 player = 0;
        u8 seq = 0;
        Bytes data;
    };
    std::vector<VoiceRx> take_voice();
    // A plausibility check our machine flagged about another player's car (sent to the host).
    void report_flag(u8 suspect, FlagKind kind, float value);
    // Host: removes a player from the session (they are told why).
    void kick(u8 id, const std::string& reason = "removed by the host");
    // Host: every warning of this session (newest last, at most 50).
    struct FlagRecord {
        u8 suspect = 0, reporter = 0;
        FlagKind kind = FlagKind::Speed;
        float value = 0;
        std::string suspect_name, reporter_name;
    };
    const std::vector<FlagRecord>& flags() const { return flag_log_; }

    // Test hooks: drop this share of outgoing datagrams (0..1); shift this session's clock;
    // drop every punch datagram (forces relaying).
    void set_loss(float share) { loss_ = share; }
    void set_clock_skew(double ms) { skew_ = ms; }
    void set_block_direct(bool on) { block_direct_ = on; }
    void set_punch_give_up_ms(double ms) { punch_give_up_ms_ = ms; }
    // Tests on loopback: advertise no LAN addresses (on machines whose own LAN address drops
    // packets to itself, e.g. CI runners, a pair would otherwise link up over it and lose states).
    static void set_advertise_lan(bool on);

private:
    struct Conn;
    struct Punch {
        double started = 0, last_sent = 0, last_reply = 0, confirmed_at = 0;
        bool heard = false, done = false, gave_up = false;
        Address from; // where its punches came from
    };
    Conn* conn_for(const Address& a);
    Conn* host_conn() const;
    Conn* peer_conn(u8 id) const;
    void handle_datagram(const Address& from, ByteSpan data, double arrived);
    void process(Conn& c, ByteSpan data, double arrived); // one "GT3N" datagram of a connection
    Conn* accept_client(const Address& addr, u64 relay_key, ByteSpan data); // host: a new client's first datagram
    Conn* conn_for_key(u64 key) const;
    bool alive(const Conn* c) const;
    void handle_server(Rv type, Reader& r);
    void server_tick(double now);
    void send_server(Writer w) { // a request: padded (the server ignores short ones)
        rv_pad(w);
        sock_.send_to(server_, w.data);
    }
    void server_failed(const std::string& why);
    void bind(u64 key, u64 token, u8 a, u8 b);
    void send_join_punch(const Address& to, u64 nonce, bool from_host, bool heard);
    void connect_host(const Address& addr, u64 relay_key);
    void handle_punch(const Address& from, ByteSpan data);
    void handle(Conn& c, ByteSpan msg, double arrived);
    void start_rx();
    void stop_rx();
    void send_reliable(Conn& c, const Writer& w);
    void send_unreliable(Conn& c, const Writer& w);
    void broadcast_reliable(const Writer& w, const Conn* except = nullptr);
    void flush(Conn& c, double now);
    void drop(Conn& c, const std::string& reason);
    void push(Event e) { events_.push_back(std::move(e)); }
    void punch(double now);
    void send_punch(const Address& to, u8 target, bool heard);
    void report_link(u8 peer, bool up, bool relayed = false);
    void start_load();    // host: the Load itself
    void release_slots(); // host: tells the relay server the race's link slots are free
    bool direct_pair(u8 a, u8 b) const; // host: both report a direct link
    Writer roster_msg() const;
    Writer settings_msg() const;
    Player* player_mut(u8 id);

    UdpSocket sock_;
    GameVersion version_ = this_game_version();
    std::optional<UpdateRequired> update_required_;
    // Receive thread: datagrams are read and timestamped the moment they arrive (not when the
    // game loop next polls), so round trips and clock offsets measure the network only.
    struct Datagram {
        Address from;
        Bytes data;
        double arrived = 0;
    };
    std::thread rx_;
    std::atomic<bool> rx_run_{false};
    std::mutex rx_mu_;
    std::deque<Datagram> rx_q_;
    bool host_ = false;
    int my_id_ = -1;
    // Rendezvous server.
    Address server_;
    std::string code_, lookup_code_;
    bool registered_ = false;
    double server_started_ = 0, server_sent_ = 0;
    u64 cookie_ = 0; // the server's return-routability cookie for our address (from its last Challenge)
    struct Join { // host: a join the server announced (client: our own join to the host)
        std::vector<std::string> eps;
        double started = 0, last_sent = 0, last_reply = 0;
        bool heard = false;
        Address from;
    };
    std::map<u64, Join> joins_;
    u64 join_nonce_ = 0;  // client
    Join my_join_;        // client: the host's endpoints and our punching
    Bytes hello_;         // client: the Hello to send once a path to the host exists
    struct Bind {
        u64 token = 0;
        u8 a = 0, b = 0;
        bool bound = false;
        double last = 0;
    };
    std::map<u64, Bind> binds_;
    u64 token_ = 0; // session secret: punches without it are ignored
    std::vector<std::unique_ptr<Conn>> conns_; // host: one per client; client: the host first, then peers
    std::vector<Player> players_;
    std::map<u8, Remote> remotes_;
    std::map<u8, Bytes> car_info_; // per player (survives roster updates)
    std::vector<VoiceRx> voice_rx_; // proximity voice frames awaiting the race
    std::vector<FlagRecord> flag_log_;
    void log_flag(const Event& e);
    struct Incoming {
        Download d;
        Bytes data;
        std::vector<int> waiting; // host: players who asked (0 = the host itself)
        bool asked = false;       // the request went out
    };
    std::map<std::string, std::shared_ptr<const Bytes>> offers_; // packages we can send (ours; host: also cached)
    std::map<std::string, Incoming> downloads_;
    void serve_package(Conn& to, const std::string& hash);
    void package_error(Conn& to, const std::string& hash, const std::string& why);
    void package_chunk(Conn& from, Reader& r);
    void package_finished(const std::string& hash, bool ok, const std::string& why);
    void pump_transfers(Conn& c);
    Bytes my_car_info_;
    void send_car_info(Conn* to, u8 id, const Bytes& info); // host: to one client, or everyone (nullptr)
    std::map<u8, Punch> punches_;         // client: per other client
    std::set<std::pair<u8, u8>> links_;   // host: (reporter, peer) own links (direct or a relay server channel)
    std::set<std::pair<u8, u8>> relayed_; // host: (reporter, peer) links through the relay server
    RelayWait wait_;
    bool reserved_ = false;               // host: the relay granted this race's links
    int reserve_links_ = 0;               // host: the links last asked for
    double reserve_started_ = 0, reserve_sent_ = 0, reserve_heard_ = 0;
    std::vector<std::string> local_eps_;  // client: our LAN endpoints (sent in Hello)
    void collect_local_endpoints();
    std::deque<Event> events_;
    Settings settings_;
    std::vector<u8> grid_;
    double start_host_ms_ = 0;            // host clock at the countdown start
    enum class Phase { Lobby, Loading, Racing } phase_ = Phase::Lobby;
    bool roster_dirty_ = false;
    double last_roster_ = 0;
    float loss_ = 0;
    double skew_ = 0;
    bool block_direct_ = false;
    double punch_give_up_ms_ = 6000;
    u32 rng_ = 0x2545F491;
};

// The process-wide session the online lobby and the race screen share.
Session& online();

} // namespace gt3r::net
