#include "net/session.h"

#include "base/log.h"
#include "base/sha256.h"
#include "gt3r_version.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace gt3r::net {

const GameVersion& this_game_version() {
    static const GameVersion v = [] {
        if (std::optional<GameVersion> p = parse_game_version(GT3R_VERSION)) return *p;
        GameVersion g;
        g.major = GT3R_VERSION_MAJOR, g.minor = GT3R_VERSION_MINOR, g.patch = GT3R_VERSION_PATCH;
        return g;
    }();
    return v;
}

RvPlatform this_platform() {
#if defined(__ANDROID__)
    return RvPlatform::Android;
#elif defined(_WIN32)
    return RvPlatform::Windows;
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    return RvPlatform::Other; // iOS: no value of its own in the protocol (as update_check.cpp has it)
#elif defined(__APPLE__)
    return RvPlatform::MacOS;
#elif defined(__linux__)
    return RvPlatform::Linux;
#else
    return RvPlatform::Other;
#endif
}

std::string version_mismatch(const GameVersion& ours, const GameVersion& host) {
    if (!host.known()) return "the host has an older version of the game: the host needs to update";
    if (compare_core(ours, host) < 0)
        return "update required: the host has version " + host.number() + " (you have " + ours.number() + ")";
    return "the host has an older version (" + host.number() + ", you have " + ours.number() +
           "): the host needs to update";
}

// A game that speaks another online protocol (theirs) than this host: who must update. The text travels in Reject; a game
// of protocol 3 or older shows it as it is ("The host refused: ...").
std::string protocol_mismatch(u16 theirs) {
    const std::string t = std::to_string(theirs), h = std::to_string(kProtocolVersion);
    if (theirs < kProtocolVersion)
        return "update required: the host has a newer version of the game (online protocol " + h + ", yours " + t +
               "). Please update GT3 Revisited to play online";
    return "the host has an older version of the game (online protocol " + h + ", yours " + t + "): the host needs to update";
}

namespace {

constexpr double kTimeoutMs = 8000;     // no datagram for this long: the peer is gone
constexpr double kKeepaliveMs = 250;    // an empty datagram (ack) at least this often
constexpr double kPingMs = 250;         // also the clock-sync sample rate
constexpr double kRosterRefreshMs = 2000;
constexpr double kPunchEveryMs = 100;
constexpr double kPunchRetryMs = 30000; // a relayed pair tries again this much later
constexpr size_t kWindow = 32;          // reliable messages in flight per datagram train
constexpr u16 kMaxEarly = 512;          // out-of-order reliable messages kept
constexpr size_t kClockSamples = 16;

} // namespace

std::string relay_wait_text(const RelayWait& w) {
    if (!w.waiting) return "";
    if (w.position <= 0) return "Asking the relay server...";
    static const char* const kTh[] = {"th", "st", "nd", "rd"};
    int p = w.position, m = p % 100;
    std::string th = m >= 11 && m <= 13 ? "th" : p % 10 <= 3 ? kTh[p % 10] : "th";
    std::string t = "Relay busy: " + std::to_string(p) + th + " in line";
    if (w.wait_s >= 60) t += ", about " + std::to_string((w.wait_s + 30) / 60) + " min"; // else just the place
    return t;
}

namespace {

Event kind_event(Event::Kind k) {
    Event e;
    e.kind = k;
    return e;
}

bool seq_before(u16 a, u16 b) { return s16(u16(a - b)) < 0; }

const char kMagic[4] = {'G', 'T', '3', 'N'};
const char kPunchMagic[4] = {'G', 'T', '3', 'P'};

// Message types only used between sessions (not part of protocol.h's public list).
constexpr u8 kMsgLink = 40; // C->H u8 peer, u8 up: our direct link to that client

} // namespace

struct Session::Conn {
    Address addr;
    int player = -1;   // the player at the other end (client: 0 for the host)
    bool peer = false; // client <-> client (direct race link)
    std::vector<Address> aliases; // peer: other addresses its (token-checked) punches came from
    u64 relay_key = 0;  // through the relay server's channel (0: direct)
    struct Send {
        std::string hash;
        std::shared_ptr<const Bytes> blob;
        size_t offset = 0;
    };
    std::deque<Send> sends; // package transfers to this connection
    u64 join_nonce = 0; // host: the server-announced join it came from
    // Reliable channel.
    struct Out {
        u16 seq = 0;
        Bytes msg;
        double sent = -1;
    };
    u16 send_seq = 0;
    std::deque<Out> out;
    u16 recv_next = 0;
    std::map<u16, Bytes> early;
    bool ack_owed = false;
    std::vector<Bytes> unreliable;
    double last_recv = 0, last_send = 0, last_ping = 0;
    double rtt = 0;      // smoothed, ms
    double close_at = 0; // > 0: drop after this time (rejected peers)
    // Clock sync: (round trip, their clock - ours) samples; the smallest round trip is the
    // most symmetric one, so its offset is the estimate.
    std::deque<std::pair<double, double>> clock;
    double offset() const {
        double best = 1e300, off = 0;
        for (const auto& [r, o] : clock)
            if (r < best) best = r, off = o;
        return off;
    }
};

Session::Session() = default;
Session::~Session() { close(); }

void Session::start_rx() {
    stop_rx();
    // Testing: GT3R_NET_FORCE_RELAY=1 never punches (as behind a symmetric NAT).
    if (const char* f = std::getenv("GT3R_NET_FORCE_RELAY"); f && *f && *f != '0') block_direct_ = true;
    rx_run_ = true;
    rx_ = std::thread([this] {
        u8 buf[2048];
        while (rx_run_) {
            if (!sock_.wait_readable(5)) continue;
            Address from;
            while (size_t n = sock_.recv_from(from, buf, sizeof buf)) {
                Datagram d{from, Bytes(buf, buf + n), now()};
                std::lock_guard<std::mutex> lock(rx_mu_);
                if (rx_q_.size() < 4096) rx_q_.push_back(std::move(d));
            }
        }
    });
}

void Session::stop_rx() {
    rx_run_ = false;
    if (rx_.joinable()) rx_.join();
    std::lock_guard<std::mutex> lock(rx_mu_);
    rx_q_.clear();
}

double Session::now() const { return now_ms() + skew_; }

bool Session::host(u16 port, const std::string& name, const std::string& car, u8 paint, std::string* error) {
    close();
    update_required_.reset();
    if (!sock_.open(port, error)) return false;
    host_ = true;
    my_id_ = 0;
    std::random_device rd;
    token_ = (u64(rd()) << 32) ^ u64(rd()) ^ u64(now_ms());
    Player me;
    me.id = 0;
    me.name = name;
    me.car = car;
    me.paint = paint;
    players_ = {me};
    phase_ = Phase::Lobby;
    start_rx();
    LOG_INFO("Online: hosting on UDP port %u", unsigned(sock_.local_port()));
    return true;
}

bool Session::join(const std::string& address, const std::string& name, const std::string& car, u8 paint,
                   std::string* error) {
    close();
    update_required_.reset();
    Address a;
    if (!resolve(address, kDefaultPort, a, error)) return false;
    if (!sock_.open_for(a, error)) return false;
    host_ = false;
    my_id_ = -1;
    collect_local_endpoints();
    auto c = std::make_unique<Conn>();
    c->addr = a;
    c->player = 0;
    c->last_recv = now();
    Writer w;
    w.u8_(u8(Msg::Hello));
    w.u16_(kProtocolVersion);
    w.str(name);
    w.str(car);
    w.u8_(paint);
    w.str(version_.to_string());
    w.u8_(u8(std::min<size_t>(local_eps_.size(), 4)));
    for (size_t i = 0; i < local_eps_.size() && i < 4; ++i) w.str(local_eps_[i]);
    send_reliable(*c, w);
    conns_.push_back(std::move(c));
    phase_ = Phase::Lobby;
    start_rx();
    LOG_INFO("Online: joining %s", a.to_string().c_str());
    return true;
}

bool Session::host_via(const std::string& server, const std::string& name, const std::string& car, u8 paint,
                       std::string* error) {
    close();
    update_required_.reset();
    Address srv;
    if (!resolve(server, kDefaultRelayPort, srv, error)) return false;
    if (!sock_.open_for(srv, error)) return false;
    server_ = srv;
    host_ = true;
    my_id_ = 0;
    std::random_device rd;
    token_ = (u64(rd()) << 32) ^ u64(rd()) ^ u64(now_ms());
    if (token_ == 0) token_ = 1;
    Player me;
    me.id = 0;
    me.name = name;
    me.car = car;
    me.paint = paint;
    players_ = {me};
    collect_local_endpoints();
    phase_ = Phase::Lobby;
    server_started_ = now();
    server_sent_ = 0;
    start_rx();
    LOG_INFO("Online: hosting through %s", srv.to_string().c_str());
    return true;
}

bool Session::join_code(const std::string& server, const std::string& code, const std::string& name,
                        const std::string& car, u8 paint, std::string* error) {
    close();
    update_required_.reset();
    std::string c = normalize_code(code);
    if (!valid_code(c)) {
        if (error) *error = "a session code is 8 letters and digits";
        return false;
    }
    Address srv;
    if (!resolve(server, kDefaultRelayPort, srv, error)) return false;
    if (!sock_.open_for(srv, error)) return false;
    server_ = srv;
    lookup_code_ = c;
    host_ = false;
    my_id_ = -1;
    collect_local_endpoints();
    Writer w;
    w.u8_(u8(Msg::Hello));
    w.u16_(kProtocolVersion);
    w.str(name);
    w.str(car);
    w.u8_(paint);
    w.str(version_.to_string());
    w.u8_(u8(std::min<size_t>(local_eps_.size(), 4)));
    for (size_t i = 0; i < local_eps_.size() && i < 4; ++i) w.str(local_eps_[i]);
    hello_ = w.data;
    phase_ = Phase::Lobby;
    server_started_ = now();
    server_sent_ = 0;
    start_rx();
    LOG_INFO("Online: joining %s through %s", c.c_str(), srv.to_string().c_str());
    return true;
}

void Session::close() {
    if (sock_.is_open() && host_ && registered_) {
        Writer w = rv_request(Rv::Unregister, cookie_, version_);
        w.str(code_);
        put_u64(w, token_);
        send_server(w);
    }
    if (sock_.is_open()) {
        Writer bye;
        bye.u8_(u8(Msg::Bye));
        for (auto& c : conns_)
            for (int i = 0; i < 3; ++i) { // unreliable, so a few copies
                c->unreliable.push_back(bye.data);
                flush(*c, now());
            }
    }
    stop_rx();
    sock_.close();
    conns_.clear();
    players_.clear();
    remotes_.clear();
    punches_.clear();
    links_.clear();
    car_info_.clear();
    flag_log_.clear();
    offers_.clear();
    downloads_.clear();
    host_ = false;
    my_id_ = -1;
    start_host_ms_ = 0;
    phase_ = Phase::Lobby;
    server_ = Address{};
    code_.clear();
    lookup_code_.clear();
    registered_ = false;
    cookie_ = 0;
    joins_.clear();
    join_nonce_ = 0;
    my_join_ = Join{};
    hello_.clear();
    binds_.clear();
    relayed_.clear();
    wait_ = RelayWait{};
    reserved_ = false;
    reserve_links_ = 0;
}

const Player* Session::player(u8 id) const {
    for (const Player& p : players_)
        if (p.id == id) return &p;
    return nullptr;
}

Player* Session::player_mut(u8 id) {
    for (Player& p : players_)
        if (p.id == id) return &p;
    return nullptr;
}

Session::Conn* Session::host_conn() const {
    if (host_) return nullptr;
    for (const auto& c : conns_)
        if (!c->peer) return c.get();
    return nullptr;
}

Session::Conn* Session::peer_conn(u8 id) const {
    for (const auto& c : conns_)
        if (c->peer && c->player == int(id)) return c.get();
    return nullptr;
}

double Session::rtt_ms() const {
    if (!host_) {
        const Conn* h = host_conn();
        return h ? h->rtt : 0;
    }
    double r = 0;
    for (const auto& c : conns_)
        if (c->player >= 0) r = std::max(r, c->rtt);
    return r;
}

Link Session::link(u8 id) const {
    Link l;
    if (!player(id) || int(id) == my_id_) return l;
    auto own = [&](const Conn& c) {
        l.known = true;
        l.direct = c.relay_key == 0;
        l.relay = c.relay_key != 0;
        l.rtt_ms = c.rtt;
    };
    if (host_) {
        for (const auto& c : conns_)
            if (c->player == int(id)) own(*c);
        return l;
    }
    const Conn* h = host_conn();
    if (!h) return l;
    l.known = true;
    if (id == 0) {
        own(*h);
    } else if (const Conn* p = peer_conn(id)) {
        own(*p);
    } else { // through the host: our round trip to it plus its round trip to them
        l.rtt_ms = h->rtt + double(player(id)->ping_ms);
    }
    return l;
}

double Session::clock_offset_ms() const {
    const Conn* h = host_conn();
    return h ? h->offset() : 0;
}

double Session::race_start_ms() const {
    if (start_host_ms_ <= 0) return 0;
    return start_host_ms_ - clock_offset_ms();
}

const Session::Remote* Session::remote(u8 id) const {
    auto it = remotes_.find(id);
    return it == remotes_.end() ? nullptr : &it->second;
}

Session::Conn* Session::conn_for(const Address& a) {
    for (auto& c : conns_) {
        if (c->addr == a) return c.get();
        for (const Address& x : c->aliases)
            if (x == a) return c.get();
    }
    return nullptr;
}

void Session::send_reliable(Conn& c, const Writer& w) {
    Conn::Out o;
    o.seq = c.send_seq++;
    o.msg = w.data;
    c.out.push_back(std::move(o));
}

void Session::send_unreliable(Conn& c, const Writer& w) { c.unreliable.push_back(w.data); }

void Session::broadcast_reliable(const Writer& w, const Conn* except) {
    // Lobby traffic goes host <-> clients only (never over the peer links).
    for (auto& c : conns_)
        if (c.get() != except && !c->peer && (c->player >= 0 || !host_) && c->close_at == 0) send_reliable(*c, w);
}

Writer Session::roster_msg() const {
    Writer w;
    w.u8_(u8(Msg::Roster));
    w.u8_(u8(players_.size()));
    for (const Player& p : players_) {
        w.u8_(p.id);
        w.str(p.name);
        w.str(p.car);
        w.u8_(p.paint);
        w.u8_(u8((p.ready ? 1 : 0) | (p.loaded ? 2 : 0)));
        w.u16_(p.ping_ms);
        w.u8_(u8(std::min<size_t>(p.endpoints.size(), 5)));
        for (size_t i = 0; i < p.endpoints.size() && i < 5; ++i) w.str(p.endpoints[i]);
    }
    // Waiting for the relay's link slots (older games ignore these trailing bytes).
    w.u8_(wait_.waiting ? 1 : 0);
    w.u16_(u16(std::clamp(wait_.position, 0, 0xFFFF)));
    w.u16_(wait_.wait_s < 0 ? kWaitUnknown : u16(std::min(wait_.wait_s, 65534)));
    return w;
}

Writer Session::settings_msg() const {
    Writer w;
    w.u8_(u8(Msg::Settings));
    w.str(settings_.course);
    w.u8_(u8(settings_.laps));
    w.u8_(settings_.allow_mods ? 1 : 0);
    w.u16_(settings_.ghost_ping_ms);
    w.u8_(settings_.stock_only ? 1 : 0);
    w.u16_(settings_.max_ps);
    w.u8_(settings_.car_class);
    w.u8_(settings_.category);
    w.u8_(settings_.drive);
    w.u8_(settings_.aspiration);
    return w;
}

// --- datagrams ---

void Session::flush(Conn& c, double now) {
    if (!sock_.is_open()) return;
    double resend = std::clamp(c.rtt * 1.5, 100.0, 1000.0);
    size_t ri = 0, ui = 0;
    size_t in_flight = std::min(c.out.size(), kWindow);
    bool sent_any = false;
    for (;;) {
        Writer w;
        w.bytes(ByteSpan(reinterpret_cast<const u8*>(kMagic), 4));
        w.u16_(kProtocolVersion);
        w.u16_(c.recv_next);
        size_t count_at = w.data.size();
        w.u8_(0);
        u8 rel = 0;
        for (; ri < in_flight; ++ri) {
            Conn::Out& o = c.out[ri];
            if (o.sent >= 0 && now - o.sent < resend) continue;
            if (w.data.size() + 4 + o.msg.size() > kMaxDatagram && rel > 0) break;
            w.u16_(o.seq);
            w.u16_(u16(o.msg.size()));
            w.bytes(o.msg);
            o.sent = now;
            if (++rel == 255) {
                ++ri;
                break;
            }
        }
        w.data[count_at] = rel;
        count_at = w.data.size();
        w.u8_(0);
        u8 unrel = 0;
        for (; ui < c.unreliable.size(); ++ui) {
            const Bytes& m = c.unreliable[ui];
            if (w.data.size() + 2 + m.size() > kMaxDatagram && (rel > 0 || unrel > 0)) break;
            w.u16_(u16(m.size()));
            w.bytes(m);
            if (++unrel == 255) {
                ++ui;
                break;
            }
        }
        w.data[count_at] = unrel;
        bool more = ri < in_flight || ui < c.unreliable.size();
        if (rel == 0 && unrel == 0 && (sent_any || (!c.ack_owed && now - c.last_send < kKeepaliveMs))) break;
        bool lose = false;
        if (loss_ > 0) {
            rng_ = rng_ * 1664525u + 1013904223u;
            lose = float(rng_ >> 8) / float(1 << 24) < loss_;
        }
        if (!lose) {
            if (c.relay_key) { // through the relay server's channel
                Writer rw;
                rw.bytes(ByteSpan(reinterpret_cast<const u8*>(kRelayMagic), 4));
                put_u64(rw, c.relay_key);
                rw.bytes(w.data);
                sock_.send_to(server_, rw.data);
            } else {
                sock_.send_to(c.addr, w.data);
            }
        }
        sent_any = true;
        c.last_send = now;
        c.ack_owed = false;
        if (!more) break;
    }
    c.unreliable.clear();
}

void Session::send_punch(const Address& to, u8 target, bool heard) {
    if (block_direct_ || my_id_ < 0) return;
    Writer w;
    w.bytes(ByteSpan(reinterpret_cast<const u8*>(kPunchMagic), 4));
    w.u16_(kProtocolVersion);
    w.u32_(u32(token_)), w.u32_(u32(token_ >> 32));
    w.u8_(u8(my_id_));
    w.u8_(target);
    w.u8_(heard ? 1 : 0);
    sock_.send_to(to, w.data);
}

void Session::handle_punch(const Address& from, ByteSpan data) {
    if (block_direct_) return;
    Reader r(data);
    r.bytes(4);
    u16 version = r.u16_();
    u64 token = r.u32_();
    token |= u64(r.u32_()) << 32;
    u8 sender = r.u8_(), target = r.u8_();
    bool heard = r.u8_() & 1;
    if (!r.ok() || version != kProtocolVersion) return;
    // Joins through the server: the client (0xFF) and the host (0) punch with the join's nonce.
    if (host_ && sender == 0xFF && target == 0) {
        auto it = joins_.find(token);
        if (it == joins_.end()) return;
        Join& j = it->second;
        if (!j.heard) j.from = from;
        j.heard = true;
        if (now() - j.last_reply > 50) send_join_punch(from, token, true, true), j.last_reply = now();
        return; // its Hello follows on this path
    }
    if (!host_ && sender == 0 && target == 0xFF) {
        if (!join_nonce_ || token != join_nonce_) return;
        if (!my_join_.heard) my_join_.from = from;
        my_join_.heard = true;
        if (now() - my_join_.last_reply > 50) send_join_punch(from, token, false, true), my_join_.last_reply = now();
        if (heard) connect_host(my_join_.from, 0); // both directions work
        return;
    }
    if (host_ || my_id_ < 0) return;
    if (!r.ok() || version != kProtocolVersion || token != token_ || int(target) != my_id_ || sender == 0 ||
        int(sender) == my_id_ || !player(sender))
        return;
    Punch& p = punches_[sender];
    if (!p.heard) p.from = from;
    if (Conn* pc = peer_conn(sender); pc && !(pc->addr == from) && // it reaches us on another path too
        std::find(pc->aliases.begin(), pc->aliases.end(), from) == pc->aliases.end() && pc->aliases.size() < 4)
        pc->aliases.push_back(from);
    p.heard = true;
    const double t = now();
    if (t - p.last_reply > 50) { // answer every punch (rate-limited): the other side may still need ours
        send_punch(from, sender, true);
        p.last_reply = t;
    }
    if (heard && !p.done) { // both directions work
        p.done = true;
        p.gave_up = false;
        p.confirmed_at = t;
        if (Conn* old = peer_conn(sender)) old->addr = p.from;
        else {
            auto c = std::make_unique<Conn>();
            c->addr = p.from;
            c->player = sender;
            c->peer = true;
            c->last_recv = t;
            conns_.push_back(std::move(c));
        }
        LOG_INFO("Online: direct link to player %u at %s", unsigned(sender), p.from.to_string().c_str());
        report_link(sender, true);
        Event e;
        e.kind = Event::Kind::Link;
        e.player = sender;
        push(std::move(e));
    }
}

void Session::report_link(u8 peer, bool up, bool relayed) {
    Conn* h = host_conn();
    if (!h) return;
    Writer w;
    w.u8_(kMsgLink);
    w.u8_(peer);
    w.u8_(up ? 1 : 0);
    w.u8_(relayed ? 1 : 0); // through the relay server: the host reserves a link slot for the pair
    send_reliable(*h, w);
}

bool Session::direct_pair(u8 a, u8 b) const { return links_.count({a, b}) && links_.count({b, a}); }

void Session::punch(double t) {
    if (host_ || my_id_ < 0) return;
    for (const Player& p : players_) {
        if (p.id == 0 || int(p.id) == my_id_ || p.endpoints.empty()) continue;
        Punch& s = punches_[p.id];
        // After the link is up, punch on for 2 s: every path the other side can use gets seen (aliases).
        if ((s.done && t - s.confirmed_at > 2000) || (!s.done && peer_conn(p.id))) continue;
        if (s.started == 0) s.started = t;
        if (s.gave_up) {
            if (t - s.started < kPunchRetryMs) continue;
            s = Punch{};
            s.started = t;
        }
        if (!s.done && t - s.started > punch_give_up_ms_) {
            s.gave_up = true;
            if (server_.valid() && !peer_conn(p.id)) { // through the relay server's channel for the pair
                u64 key = pair_key(token_, u8(my_id_), p.id);
                bind(key, token_, u8(my_id_), p.id);
                auto c = std::make_unique<Conn>();
                c->addr = server_;
                c->player = p.id;
                c->peer = true;
                c->relay_key = key;
                c->last_recv = t;
                conns_.push_back(std::move(c));
                s.done = true;
                s.confirmed_at = t - 10000; // no alias punching
                report_link(p.id, true, true);
                LOG_INFO("Online: no direct link to player %u; through the relay server", unsigned(p.id));
                Event e;
                e.kind = Event::Kind::Link;
                e.player = p.id;
                push(std::move(e));
                continue;
            }
            LOG_INFO("Online: no direct link to player %u; relayed through the host", unsigned(p.id));
            Event e;
            e.kind = Event::Kind::Link;
            e.player = p.id;
            push(std::move(e));
            continue;
        }
        if (t - s.last_sent < kPunchEveryMs) continue;
        s.last_sent = t;
        for (const std::string& ep : p.endpoints) {
            Address a;
            if (resolve(ep, kDefaultPort, a)) send_punch(a, p.id, s.heard);
        }
    }
}

Session::Conn* Session::conn_for_key(u64 key) const {
    for (const auto& c : conns_)
        if (c->relay_key == key) return c.get();
    return nullptr;
}

bool Session::alive(const Conn* c) const {
    if (!sock_.is_open()) return false;
    for (const auto& x : conns_)
        if (x.get() == c) return true;
    return false;
}

Session::Conn* Session::accept_client(const Address& addr, u64 relay_key, ByteSpan data) {
    Reader r(data);
    r.bytes(4);
    u16 version = r.u16_();
    r.u16_();
    if (!r.ok()) return nullptr;
    auto nc = std::make_unique<Conn>();
    nc->addr = addr;
    nc->relay_key = relay_key;
    nc->last_recv = now();
    if (version != kProtocolVersion) { // tell it why (it speaks another version)
        nc->close_at = now() + 3000;
        Writer w;
        w.u8_(u8(Msg::Reject));
        w.str(protocol_mismatch(version));
        send_reliable(*nc, w);
        conns_.push_back(std::move(nc));
        return nullptr;
    }
    // A new client: its first datagram carries Hello as reliable message 0.
    u8 n = r.u8_();
    u16 seq = r.u16_();
    r.u16_();
    u8 type = r.u8_();
    if (!r.ok() || n == 0 || seq != 0 || type != u8(Msg::Hello)) return nullptr;
    if (relay_key) nc->join_nonce = relay_key;
    else
        for (const auto& [nonce, j] : joins_)
            if (j.heard && j.from == addr) nc->join_nonce = nonce;
    conns_.push_back(std::move(nc));
    return conns_.back().get();
}

void Session::handle_datagram(const Address& from, ByteSpan data, double arrived) {
    if (data.size() >= 4 && std::memcmp(data.data(), kPunchMagic, 4) == 0) {
        handle_punch(from, data);
        return;
    }
    if (server_.valid() && from == server_) {
        if (data.size() >= kRelayHeader && std::memcmp(data.data(), kRelayMagic, 4) == 0) {
            Reader r(data);
            r.bytes(4);
            u64 key = get_u64(r);
            ByteSpan inner = data.subspan(kRelayHeader);
            Conn* c = conn_for_key(key);
            if (!c && host_ && joins_.count(key)) c = accept_client(server_, key, inner);
            if (c) process(*c, inner, arrived);
            return;
        }
        Rv type;
        Reader r(data);
        if (rv_parse(data, type, r)) handle_server(type, r);
        else if (u16 v = rv_version(data); v && ((host_ && !registered_) || (!host_ && !host_conn())))
            // Another protocol version (a server answers a request it cannot read with an Error in its own).
            server_failed(v > kRendezvousVersion ? "the relay server needs a newer version of the game: please update"
                                                 : "the relay server is out of date (it needs an update)");
        return;
    }
    if (data.size() < 4 || std::memcmp(data.data(), kMagic, 4) != 0) return;
    Conn* c = conn_for(from);
    if (!c) {
        if (!host_) return; // clients only talk to the host and to punched peers
        c = accept_client(from, 0, data);
        if (!c) return;
    }
    process(*c, data, arrived);
}

void Session::process(Conn& conn, ByteSpan data, double arrived) {
    Conn* c = &conn;
    Reader r(data);
    ByteSpan magic = r.bytes(4);
    if (!r.ok() || std::memcmp(magic.data(), kMagic, 4) != 0) return;
    r.u16_(); // version (checked when the connection was made)
    u16 ack = r.u16_();
    if (!r.ok()) return;
    c->last_recv = now();
    while (!c->out.empty() && seq_before(c->out.front().seq, ack)) c->out.pop_front();
    u8 nrel = r.u8_();
    for (u8 i = 0; i < nrel && r.ok(); ++i) {
        u16 seq = r.u16_();
        u16 len = r.u16_();
        ByteSpan m = r.bytes(len);
        if (!r.ok()) return;
        c->ack_owed = true;
        if (seq == c->recv_next) {
            ++c->recv_next;
            handle(*c, m, arrived);
            if (!alive(c)) return; // closed / dropped while handling
            for (auto it = c->early.find(c->recv_next); it != c->early.end(); it = c->early.find(c->recv_next)) {
                Bytes b = std::move(it->second);
                c->early.erase(it);
                ++c->recv_next;
                handle(*c, b, arrived);
                if (!alive(c)) return;
            }
        } else if (seq_before(c->recv_next, seq) && u16(seq - c->recv_next) < kMaxEarly) {
            c->early.emplace(seq, Bytes(m.begin(), m.end()));
        } // else: a duplicate
    }
    u8 nun = r.u8_();
    for (u8 i = 0; i < nun && r.ok(); ++i) {
        u16 len = r.u16_();
        ByteSpan m = r.bytes(len);
        if (!r.ok()) return;
        handle(*c, m, arrived);
        if (!alive(c)) return;
    }
}

// --- rendezvous server ---

void Session::bind(u64 key, u64 token, u8 a, u8 b) {
    Bind& bd = binds_[key];
    bd.token = token, bd.a = a, bd.b = b;
    bd.last = 0; // send on the next tick
}

void Session::send_join_punch(const Address& to, u64 nonce, bool from_host, bool heard) {
    if (block_direct_) return;
    Writer w;
    w.bytes(ByteSpan(reinterpret_cast<const u8*>(kPunchMagic), 4));
    w.u16_(kProtocolVersion);
    w.u32_(u32(nonce)), w.u32_(u32(nonce >> 32));
    w.u8_(from_host ? 0 : 0xFF);
    w.u8_(from_host ? 0xFF : 0);
    w.u8_(heard ? 1 : 0);
    sock_.send_to(to, w.data);
}

void Session::connect_host(const Address& addr, u64 relay_key) {
    if (host_conn() || hello_.empty()) return;
    auto c = std::make_unique<Conn>();
    c->addr = addr;
    c->player = 0;
    c->relay_key = relay_key;
    c->last_recv = now();
    Writer w;
    w.data = hello_;
    send_reliable(*c, w);
    conns_.push_back(std::move(c));
    LOG_INFO("Online: connecting to the host %s", relay_key ? "through the relay" : ("at " + addr.to_string()).c_str());
}

void Session::handle_server(Rv type, Reader& r) {
    const double t = now();
    switch (type) {
    case Rv::Registered: {
        std::string code = r.str();
        std::string pub = r.str();
        if (!r.ok() || !host_) return;
        if (!registered_) LOG_INFO("Online: session code %s (public address %s)", code.c_str(), pub.c_str());
        code_ = code;
        registered_ = true;
        push(kind_event(Event::Kind::Roster)); // the lobby shows the code
        return;
    }
    case Rv::HostInfo: {
        u64 nonce = get_u64(r);
        r.str(); // our public address
        std::vector<std::string> eps = get_endpoints(r);
        if (!r.ok() || host_ || join_nonce_) return;
        join_nonce_ = nonce;
        my_join_ = Join{};
        my_join_.eps = eps;
        my_join_.started = t;
        return;
    }
    case Rv::Incoming: {
        u64 nonce = get_u64(r);
        std::vector<std::string> eps = get_endpoints(r);
        if (!r.ok() || !host_ || joins_.count(nonce)) return;
        Join j;
        j.eps = eps;
        j.started = t;
        joins_[nonce] = j;
        bind(nonce, 0, 0, 0); // the relay channel is ready if punching fails
        return;
    }
    case Rv::Reservation: { // our race's link slots: granted, or our place in the queue
        RvSlot state = RvSlot(r.u8_());
        r.u8_(); // links
        u16 position = r.u16_(), wait = r.u16_();
        if (!r.ok() || !host_) return;
        reserve_heard_ = t;
        if (state == RvSlot::Granted) {
            if (wait_.waiting) {
                LOG_INFO("Online: the relay server granted the race's link slots");
                wait_ = RelayWait{};
                reserved_ = true;
                roster_dirty_ = true;
                start_load();
            } else if (!reserved_) {
                release_slots(); // a grant we no longer want (we cancelled, and that Release was lost)
            }
        } else if (state == RvSlot::Queued && wait_.waiting) {
            int ws = wait == kWaitUnknown ? -1 : int(wait);
            if (wait_.position != position || wait_.wait_s != ws) roster_dirty_ = true; // the players see it too
            wait_.position = position;
            wait_.wait_s = ws;
        } else if (state == RvSlot::Released && wait_.waiting) {
            reserve_sent_ = 0; // the server forgot our place (it expired): ask again
        } else if (state == RvSlot::Refused && wait_.waiting) {
            std::string why = r.str();
            LOG_WARN("Online: relay server: %s", why.c_str());
            wait_ = RelayWait{};
            roster_dirty_ = true;
            Event e;
            e.kind = Event::Kind::Queue;
            e.text = why.empty() ? "the relay server refused the race" : why;
            push(std::move(e));
        }
        return;
    }
    case Rv::Bound: {
        u64 key = get_u64(r);
        if (auto it = binds_.find(key); r.ok() && it != binds_.end()) it->second.bound = true;
        return;
    }
    case Rv::Challenge: { // our cookie was missing or stale: resend that request with the new one
        Rv what = Rv(r.u8_());
        u64 cookie = get_u64(r);
        if (!r.ok()) return;
        cookie_ = cookie;
        if (what == Rv::Register || what == Rv::Keepalive || what == Rv::Lookup) server_sent_ = 0;
        if (what == Rv::Reserve) reserve_sent_ = 0;
        if (what == Rv::Bind)
            for (auto& [key, bd] : binds_)
                if (t - bd.last < 3000) bd.bound = false, bd.last = t - 1e6; // the ones just sent (re-bind on the next tick)
        return;
    }
    case Rv::Error: {
        u8 code = r.u8_();
        std::string text = r.str();
        if (!r.ok()) return;
        LOG_WARN("Online: relay server: %s", text.c_str());
        if (code == u8(RvError::UpdateRequired)) { // this game is below the server's min_online
            UpdateRequired u;
            u.latest = get_version(r);
            u.url = r.str();
            if (!r.ok()) u = UpdateRequired{};
            if (u.url.size() > kMaxReleaseUrl) u.url.clear();
            update_required_ = u;
            text = "update required to play online" + (u.latest.known() ? ": version " + u.latest.number() : std::string());
            if (wait_.waiting) { // a race start: stays in the lobby
                wait_ = RelayWait{};
                roster_dirty_ = true;
                Event e;
                e.kind = Event::Kind::Queue;
                e.text = text;
                push(std::move(e));
                return;
            }
        }
        bool fatal = (host_ && !registered_) || (!host_ && !host_conn());
        if (!fatal) return;
        Event e;
        e.kind = !host_ && code == u8(RvError::NotFound) ? Event::Kind::Rejected : Event::Kind::Disconnected;
        e.text = text;
        stop_rx();
        sock_.close();
        conns_.clear();
        players_.clear();
        push(std::move(e));
        return;
    }
    default: return;
    }
}

void Session::server_failed(const std::string& why) {
    LOG_WARN("Online: relay server: %s", why.c_str());
    Event e;
    e.kind = Event::Kind::Disconnected;
    e.text = why;
    stop_rx();
    sock_.close();
    conns_.clear();
    players_.clear();
    push(std::move(e));
}

void Session::server_tick(double t) {
    if (!server_.valid() || !sock_.is_open()) return;
    auto fail = [&](const std::string& why) { server_failed(why); };
    if (host_) {
        if (!registered_) {
            if (t - server_started_ > 10000) return fail("the relay server does not answer");
            if (t - server_sent_ > 500) {
                Writer w = rv_request(Rv::Register, cookie_, version_);
                put_u64(w, token_);
                put_endpoints(w, local_eps_, 4);
                send_server(w);
                server_sent_ = t;
            }
        } else if (t - server_sent_ > (wait_.waiting ? 2000 : 5000)) {
            // Keeps the session and our NAT mapping alive (and a queued race its place: the
            // server answers with where it stands).
            Writer w = rv_request(Rv::Keepalive, cookie_, version_);
            w.str(code_);
            put_u64(w, token_);
            send_server(w);
            server_sent_ = t;
        }
        if (registered_ && wait_.waiting) { // the race waits for its link slots
            int need = relay_links_needed();
            if (need == 0) { // the relayed players left: nothing to wait for
                wait_ = RelayWait{};
                roster_dirty_ = true;
                release_slots();
                start_load();
            } else if (reserve_heard_ == 0 && t - reserve_started_ > 10000) {
                LOG_WARN("Online: the relay server does not answer the reservation; starting anyway");
                wait_ = RelayWait{};
                roster_dirty_ = true;
                start_load();
            } else if (need != reserve_links_ || (t - reserve_sent_ > 1000 && t - reserve_heard_ > 3000)) {
                Writer w = rv_request(Rv::Reserve, cookie_, version_);
                w.str(code_);
                put_u64(w, token_);
                w.u8_(u8(need));
                send_server(w);
                reserve_links_ = need;
                reserve_sent_ = t;
            }
        }
        // Punch towards joining clients (they punch towards us at the same time).
        for (auto it = joins_.begin(); it != joins_.end();) {
            Join& j = it->second;
            if (t - j.started > 120000) {
                bool used = false;
                for (const auto& c : conns_) used = used || c->relay_key == it->first;
                if (!used) binds_.erase(it->first);
                it = joins_.erase(it);
                continue;
            }
            if (t - j.started < 8000 && t - j.last_sent > kPunchEveryMs) {
                j.last_sent = t;
                for (const std::string& ep : j.eps) {
                    Address a;
                    if (resolve(ep, kDefaultPort, a)) send_join_punch(a, it->first, true, j.heard);
                }
            }
            ++it;
        }
    } else if (!host_conn()) {
        if (!join_nonce_) {
            if (t - server_started_ > 10000) return fail("the relay server does not answer");
            if (t - server_sent_ > 500) {
                Writer w = rv_request(Rv::Lookup, cookie_, version_);
                w.str(lookup_code_);
                w.u16_(kProtocolVersion);
                put_endpoints(w, local_eps_, 4);
                send_server(w);
                server_sent_ = t;
            }
        } else if (t - my_join_.started > 2500 || block_direct_) {
            // No direct path to the host: through the relay.
            bind(join_nonce_, 0, 0, 0);
            connect_host(server_, join_nonce_);
        } else if (t - my_join_.last_sent > kPunchEveryMs) {
            my_join_.last_sent = t;
            for (const std::string& ep : my_join_.eps) {
                Address a;
                if (resolve(ep, kDefaultPort, a)) send_join_punch(a, join_nonce_, false, my_join_.heard);
            }
        }
    }
    for (auto& [key, bd] : binds_) {
        if (bd.bound && t - bd.last < 20000) continue; // re-bind now and then (keeps the channel)
        if (!bd.bound && t - bd.last < 1000) continue;
        Writer w = rv_request(Rv::Bind, cookie_, version_);
        put_u64(w, key);
        w.str(host_ ? code_ : lookup_code_);
        put_u64(w, bd.token);
        w.u8_(bd.a);
        w.u8_(bd.b);
        send_server(w);
        bd.last = t;
        bd.bound = false;
    }
}

void Session::drop(Conn& c, const std::string& reason) {
    int pid = c.player;
    bool peer = c.peer;
    std::string who = c.addr.to_string();
    for (auto it = conns_.begin(); it != conns_.end(); ++it)
        if (it->get() == &c) {
            conns_.erase(it);
            break;
        }
    if (!host_ && peer) { // a direct link went quiet: back to relaying (and punch again later)
        LOG_INFO("Online: direct link to player %d lost (%s)", pid, reason.c_str());
        Punch& p = punches_[u8(pid)];
        p = Punch{};
        p.gave_up = true;
        p.started = now() - kPunchRetryMs + 2000; // retry in 2 s
        report_link(u8(pid), false);
        Event e;
        e.kind = Event::Kind::Link;
        e.player = u8(pid);
        push(std::move(e));
        return;
    }
    if (!host_) {
        LOG_INFO("Online: disconnected (%s)", reason.c_str());
        Event e;
        e.kind = Event::Kind::Disconnected;
        e.text = reason;
        stop_rx();
        sock_.close();
        conns_.clear();
        players_.clear();
        remotes_.clear();
        punches_.clear();
        my_id_ = -1;
        push(std::move(e));
        return;
    }
    if (pid < 0) return;
    LOG_INFO("Online: player %d (%s) left: %s", pid, who.c_str(), reason.c_str());
    players_.erase(std::remove_if(players_.begin(), players_.end(), [&](const Player& p) { return p.id == pid; }),
                   players_.end());
    remotes_.erase(u8(pid));
    car_info_.erase(u8(pid));
    for (auto it = links_.begin(); it != links_.end();)
        it = it->first == pid || it->second == pid ? links_.erase(it) : std::next(it);
    for (auto it = relayed_.begin(); it != relayed_.end();)
        it = it->first == pid || it->second == pid ? relayed_.erase(it) : std::next(it);
    roster_dirty_ = true;
    Writer w;
    w.u8_(u8(Msg::Retire));
    w.u8_(u8(pid));
    broadcast_reliable(w);
    Event e;
    e.kind = Event::Kind::Retire;
    e.player = u8(pid);
    push(std::move(e));
}

void Session::handle(Conn& c, ByteSpan msg, double arrived) {
    Reader r(msg);
    u8 raw = r.u8_();
    Msg type = Msg(raw);
    if (!r.ok()) return;
    const double t_now = now();
    switch (type) {
    case Msg::Ping: { // NTP-style: echo its send time, with when it arrived and when we answer
        double t0 = r.f64_();
        Writer w;
        w.u8_(u8(Msg::Pong));
        w.f64_(t0);
        w.f64_(arrived);
        w.f64_(t_now);
        send_unreliable(c, w);
        return;
    }
    case Msg::Pong: {
        double t0 = r.f64_(), t1 = r.f64_(), t1s = r.f64_(), t2 = arrived;
        double sample = (t2 - t0) - (t1s - t1); // time on the wire (the other side's hold removed)
        if (!r.ok() || sample < 0 || sample > 10000 || t1s < t1) return;
        c.rtt = c.rtt <= 0 ? sample : c.rtt * 0.8 + sample * 0.2;
        c.clock.emplace_back(sample, ((t1 - t0) + (t1s - t2)) * 0.5);
        if (c.clock.size() > kClockSamples) c.clock.pop_front();
        if (host_ && c.player >= 0)
            if (Player* p = player_mut(u8(c.player))) p->ping_ms = u16(std::min(c.rtt, 65535.0));
        return;
    }
    case Msg::Bye: drop(c, host_ || c.peer ? "left" : "the host closed the session"); return;
    case Msg::State: {
        CarSnapshot s;
        if (!read_snapshot(r, s)) return;
        if (host_) {
            if (c.player < 0) return;
            s.player = u8(c.player); // a client speaks only for its own car
            Writer w;
            w.u8_(u8(Msg::State));
            write_snapshot(w, s);
            for (auto& o : conns_) // relay to the clients without a direct link to it
                if (o.get() != &c && o->player >= 0 && !direct_pair(u8(c.player), u8(o->player))) send_unreliable(*o, w);
        } else if (c.peer) {
            s.player = u8(c.player);
        } else if (int(s.player) == my_id_) {
            return;
        }
        Remote& rm = remotes_[s.player];
        if (rm.count == 0 || s.tick > rm.snap.tick) { // newest only (ticks restart with each Load)
            rm.snap = s;
            rm.recv_ms = arrived;
            ++rm.count;
        }
        return;
    }
    case Msg::Contact: {
        u8 target = r.u8_(), from = r.u8_();
        Event e;
        e.kind = Event::Kind::Contact;
        e.contact.tick = r.u32_();
        e.contact.point_x = r.f32_(), e.contact.point_z = r.f32_();
        e.contact.jx = r.f32_(), e.contact.jz = r.f32_();
        if (!r.ok()) return;
        // A peer or a client speaks only for itself.
        if (c.peer || host_) from = u8(c.player);
        if (c.player < 0 && host_) return;
        if (int(target) == my_id_) {
            e.player = from;
            // Bound what another machine can do to our car (a hostile one could send anything).
            float j = std::hypot(e.contact.jx, e.contact.jz);
            const float kMaxImpulse = 60000; // N*s: ~a 2 t car changing speed by 30 m/s
            if (j > kMaxImpulse) e.contact.jx *= kMaxImpulse / j, e.contact.jz *= kMaxImpulse / j;
            push(std::move(e));
        } else if (host_) { // relay to its target
            Writer w;
            w.u8_(u8(Msg::Contact));
            w.u8_(target);
            w.u8_(from);
            w.u32_(e.contact.tick);
            w.f32_(e.contact.point_x), w.f32_(e.contact.point_z), w.f32_(e.contact.jx), w.f32_(e.contact.jz);
            for (auto& o : conns_)
                if (o->player == int(target) && !o->peer) send_reliable(*o, w);
        }
        return;
    }
    default: break;
    }
    if (c.peer) return; // peer links carry only race traffic

    if (host_) {
        if (type == Msg::Hello) {
            if (c.player >= 0) return;
            u16 version = r.u16_();
            std::string name = r.str(), car = r.str();
            u8 paint = r.u8_();
            std::string build = r.str();
            std::vector<std::string> eps{c.addr.to_string()};
            if (auto j = joins_.find(c.join_nonce); j != joins_.end() && !j->second.eps.empty())
                eps = j->second.eps; // as the relay server saw it (public first) and its LAN addresses
            if (c.join_nonce && !c.relay_key) binds_.erase(c.join_nonce); // came in directly: no relay needed
            u8 neps = r.u8_();
            for (u8 i = 0; i < neps && i < 4 && r.ok(); ++i) {
                std::string ep = r.str();
                if (std::find(eps.begin(), eps.end(), ep) == eps.end()) eps.push_back(ep);
            }
            // Direct-IP sessions (no relay server checks versions): the same release only. Through
            // a relay server, its min_online decides (docs/dev/ONLINE.md "Versions and updates").
            GameVersion theirs = parse_game_version(build).value_or(GameVersion{});
            std::string reject;
            if (!r.ok()) reject = "malformed hello";
            else if (version != kProtocolVersion) reject = protocol_mismatch(version);
            else if (!server_.valid() && compare_core(theirs, version_) != 0) reject = version_mismatch(theirs, version_);
            else if (phase_ != Phase::Lobby) reject = "a race is running";
            else if (int(players_.size()) >= kMaxPlayers) reject = "the session is full";
            if (!reject.empty()) {
                Writer w;
                w.u8_(u8(Msg::Reject));
                w.str(reject);
                send_reliable(c, w);
                c.close_at = t_now + 3000;
                return;
            }
            if (build != version_.to_string())
                LOG_WARN("Online: %s runs version %s (host %s)", name.c_str(), build.c_str(), version_.to_string().c_str());
            u8 id = 1;
            while (player(id)) ++id;
            c.player = id;
            Player p;
            p.id = id;
            p.name = name.empty() ? "Player " + std::to_string(id + 1) : name;
            p.car = car;
            p.paint = paint;
            p.endpoints = eps;
            players_.push_back(p);
            Writer w;
            w.u8_(u8(Msg::Welcome));
            w.u8_(id);
            w.u32_(u32(token_)), w.u32_(u32(token_ >> 32));
            w.str(version_.to_string()); // older games ignore it
            send_reliable(c, w);
            send_reliable(c, settings_msg());
            for (const auto& [pid, info] : car_info_) send_car_info(&c, pid, info);
            roster_dirty_ = true;
            LOG_INFO("Online: %s joined from %s as player %u", p.name.c_str(), c.addr.to_string().c_str(), unsigned(id));
            return;
        }
        if (c.player < 0) return;
        Player* p = player_mut(u8(c.player));
        if (!p) return;
        if (type == Msg::Flag) {
            u8 suspect = r.u8_(), kind = r.u8_();
            float value = r.f32_();
            if (!r.ok() || kind < 1 || kind > 5 || int(suspect) == c.player) return;
            Event e;
            e.kind = Event::Kind::Flag;
            e.player = suspect;
            e.flag = {FlagKind(kind), value, u8(c.player)};
            log_flag(e);
            push(std::move(e));
            return;
        }
        if (type == Msg::PkgRequest) {
            std::string hash = r.str();
            u8 owner = r.u8_();
            if (!r.ok()) return;
            if (offers_.count(hash)) return serve_package(c, hash);
            Incoming& in = downloads_[hash];
            if (std::find(in.waiting.begin(), in.waiting.end(), c.player) == in.waiting.end()) in.waiting.push_back(c.player);
            if (in.asked && !in.d.failed) return; // already fetching it from the owner
            Conn* oc = nullptr;
            for (auto& x : conns_)
                if (x->player == int(owner) && !x->peer && owner != 0) oc = x.get();
            if (!oc) {
                in.asked = true;
                return package_finished(hash, false, "not available");
            }
            in.asked = true;
            in.d = Download{};
            in.data.clear();
            Writer w;
            w.u8_(u8(Msg::PkgRequest));
            w.str(hash);
            w.u8_(owner);
            send_reliable(*oc, w);
            return;
        }
        if (type == Msg::PkgChunk) return package_chunk(c, r);
        if (type == Msg::PkgError) {
            std::string hash = r.str(), why = r.str();
            if (r.ok()) package_finished(hash, false, why);
            return;
        }
        if (raw == kMsgLink) {
            u8 peer = r.u8_();
            bool up = r.u8_() & 1;
            bool relayed = r.remaining() && (r.u8_() & 1);
            if (!r.ok()) return;
            std::pair<u8, u8> key{u8(c.player), peer};
            if (up) links_.insert(key);
            else links_.erase(key);
            if (up && relayed) relayed_.insert(key);
            else relayed_.erase(key);
            return;
        }
        switch (type) {
        case Msg::CarInfo: {
            ByteSpan info = r.bytes(r.remaining());
            if (info.size() > 1024) break;
            Bytes b(info.begin(), info.end());
            car_info_[u8(c.player)] = b;
            send_car_info(nullptr, u8(c.player), b);
            Event e;
            e.kind = Event::Kind::CarInfo;
            e.player = u8(c.player);
            push(std::move(e));
            break;
        }
        case Msg::Car:
            p->car = r.str();
            p->paint = r.u8_();
            p->ready = r.u8_() != 0;
            roster_dirty_ = true;
            break;
        case Msg::Loaded:
            p->loaded = true;
            roster_dirty_ = true;
            break;
        case Msg::Finish:
        case Msg::Retire: {
            Event e;
            e.kind = type == Msg::Finish ? Event::Kind::Finish : Event::Kind::Retire;
            r.u8_();
            e.player = u8(c.player);
            if (type == Msg::Finish) e.time = r.f64_(), e.best = r.f64_();
            Writer w;
            w.u8_(u8(type));
            w.u8_(e.player);
            if (type == Msg::Finish) w.f64_(e.time), w.f64_(e.best);
            broadcast_reliable(w, &c);
            push(std::move(e));
            break;
        }
        default: break;
        }
        return;
    }

    // Client, from the host.
    switch (type) {
    case Msg::PkgRequest: { // the host fetches our package for another player
        std::string hash = r.str();
        if (r.ok()) serve_package(c, hash);
        break;
    }
    case Msg::PkgChunk: package_chunk(c, r); break;
    case Msg::Kick: {
        std::string why = r.str();
        Event e;
        e.kind = Event::Kind::Rejected;
        e.text = why.empty() ? "removed by the host" : why;
        stop_rx();
        sock_.close();
        conns_.clear();
        players_.clear();
        push(std::move(e));
        break;
    }
    case Msg::PkgError: {
        std::string hash = r.str(), why = r.str();
        if (r.ok()) package_finished(hash, false, why);
        break;
    }
    case Msg::Welcome: {
        my_id_ = r.u8_();
        u64 tok = r.u32_();
        tok |= u64(r.u32_()) << 32;
        token_ = tok;
        // Direct-IP: the same release only (a host too old to check sends no version).
        GameVersion host_version = r.remaining() ? parse_game_version(r.str()).value_or(GameVersion{}) : GameVersion{};
        if (!server_.valid() && compare_core(host_version, version_) != 0) {
            std::string why = version_mismatch(version_, host_version);
            LOG_WARN("Online: %s", why.c_str());
            close(); // tells the host we left
            Event e;
            e.kind = Event::Kind::Rejected;
            e.text = why;
            push(std::move(e));
            return;
        }
        if (!my_car_info_.empty()) set_car_info(my_car_info_);
        push(kind_event(Event::Kind::Joined));
        LOG_INFO("Online: joined as player %d", my_id_);
        break;
    }
    case Msg::Reject: {
        std::string why = r.str();
        // An older host (protocol 3 and before) says only "different game version (protocol <ours>, host <its>)".
        if (unsigned theirs = 0, host = 0; std::sscanf(why.c_str(), "different game version (protocol %u, host %u)", &theirs, &host) == 2 &&
                                          host < kProtocolVersion)
            why = "the host has an older version of the game (online protocol " + std::to_string(host) + ", yours " +
                  std::to_string(kProtocolVersion) + "): the host needs to update";
        LOG_WARN("Online: the host refused: %s", why.c_str());
        Event e;
        e.kind = Event::Kind::Rejected;
        e.text = why;
        stop_rx();
        sock_.close();
        conns_.clear();
        players_.clear();
        push(std::move(e));
        break;
    }
    case Msg::Roster: {
        std::vector<Player> list;
        u8 n = r.u8_();
        for (u8 i = 0; i < n && r.ok(); ++i) {
            Player p;
            p.id = r.u8_();
            p.name = r.str();
            p.car = r.str();
            p.paint = r.u8_();
            u8 f = r.u8_();
            p.ready = f & 1;
            p.loaded = f & 2;
            p.ping_ms = r.u16_();
            u8 ne = r.u8_();
            for (u8 k = 0; k < ne && r.ok(); ++k) p.endpoints.push_back(r.str());
            list.push_back(p);
        }
        if (!r.ok()) break;
        RelayWait wait;
        if (r.remaining() >= 5) { // the host waits for the relay's link slots
            wait.waiting = r.u8_() & 1;
            wait.position = r.u16_();
            u16 ws = r.u16_();
            wait.wait_s = ws == kWaitUnknown ? -1 : int(ws);
        }
        wait_ = wait;
        players_ = std::move(list);
        // Peers no longer in the session: drop their links.
        for (size_t i = 0; i < conns_.size();) {
            if (conns_[i]->peer && !player(u8(conns_[i]->player))) conns_.erase(conns_.begin() + ptrdiff_t(i));
            else ++i;
        }
        for (auto it = punches_.begin(); it != punches_.end();) it = player(it->first) ? std::next(it) : punches_.erase(it);
        for (auto it = car_info_.begin(); it != car_info_.end();) it = player(it->first) ? std::next(it) : car_info_.erase(it);
        push(kind_event(Event::Kind::Roster));
        break;
    }
    case Msg::CarInfo: {
        u8 id = r.u8_();
        ByteSpan info = r.bytes(r.remaining());
        if (!r.ok() || info.size() > 1024) break;
        car_info_[id] = Bytes(info.begin(), info.end());
        Event e;
        e.kind = Event::Kind::CarInfo;
        e.player = id;
        push(std::move(e));
        break;
    }
    case Msg::Settings: {
        Settings s;
        s.course = r.str();
        s.laps = r.u8_();
        s.allow_mods = r.u8_() & 1;
        s.ghost_ping_ms = r.u16_();
        s.stock_only = r.u8_() & 1;
        s.max_ps = r.u16_();
        if (r.remaining() >= 4) { // the further rules (a host without them: any car)
            s.car_class = r.u8_();
            s.category = r.u8_();
            s.drive = r.u8_();
            s.aspiration = r.u8_();
        }
        if (!r.ok()) break;
        settings_ = s;
        Event e;
        e.kind = Event::Kind::Settings;
        e.course = s.course;
        e.laps = s.laps;
        push(std::move(e));
        break;
    }
    case Msg::Load: {
        Event e;
        e.kind = Event::Kind::Load;
        e.course = settings_.course = r.str();
        e.laps = settings_.laps = r.u8_();
        u8 n = r.u8_();
        for (u8 i = 0; i < n && r.ok(); ++i) e.grid.push_back(r.u8_());
        if (!r.ok()) break;
        grid_ = e.grid;
        remotes_.clear();
        start_host_ms_ = 0;
        phase_ = Phase::Loading;
        push(std::move(e));
        break;
    }
    case Msg::Go: {
        double start = r.f64_(); // host clock
        if (!r.ok()) break;
        start_host_ms_ = start;
        Event e;
        e.kind = Event::Kind::Go;
        e.start_at_ms = race_start_ms();
        phase_ = Phase::Racing;
        push(std::move(e));
        break;
    }
    case Msg::Finish:
    case Msg::Retire: {
        Event e;
        e.kind = type == Msg::Finish ? Event::Kind::Finish : Event::Kind::Retire;
        e.player = r.u8_();
        if (type == Msg::Finish) e.time = r.f64_(), e.best = r.f64_();
        if (r.ok()) push(std::move(e));
        break;
    }
    case Msg::Lobby:
        phase_ = Phase::Lobby;
        remotes_.clear();
        start_host_ms_ = 0;
        for (Player& p : players_) p.loaded = false;
        push(kind_event(Event::Kind::Lobby));
        break;
    default: break;
    }
}

void Session::poll() {
    if (!sock_.is_open()) return;
    const double t = now();
    std::deque<Datagram> in;
    {
        std::lock_guard<std::mutex> lock(rx_mu_);
        in.swap(rx_q_);
    }
    for (const Datagram& d : in) {
        if (!sock_.is_open()) break;
        handle_datagram(d.from, d.data, d.arrived);
    }
    if (!sock_.is_open()) return;
    // Time-outs and rejected peers.
    for (size_t i = 0; i < conns_.size();) {
        Conn& c = *conns_[i];
        if ((c.close_at > 0 && t > c.close_at) || t - c.last_recv > kTimeoutMs) {
            size_t before = conns_.size();
            drop(c, c.close_at > 0 ? "rejected" : "connection timed out");
            if (!sock_.is_open()) return;
            if (conns_.size() < before) continue;
        }
        ++i;
    }
    if (host_ && (roster_dirty_ || (players_.size() > 1 && t - last_roster_ > kRosterRefreshMs))) {
        broadcast_reliable(roster_msg());
        if (roster_dirty_) push(kind_event(Event::Kind::Roster));
        roster_dirty_ = false;
        last_roster_ = t;
    }
    server_tick(t);
    if (!sock_.is_open()) return;
    punch(t);
    for (auto& c : conns_) {
        if (t - c->last_ping > kPingMs) {
            Writer w;
            w.u8_(u8(Msg::Ping));
            w.f64_(t);
            send_unreliable(*c, w);
            c->last_ping = t;
        }
        pump_transfers(*c);
        flush(*c, t);
    }
}

std::optional<Event> Session::next_event() {
    if (events_.empty()) return std::nullopt;
    Event e = std::move(events_.front());
    events_.pop_front();
    return e;
}

// --- lobby ---

void Session::send_car_info(Conn* to, u8 id, const Bytes& info) {
    Writer w;
    w.u8_(u8(Msg::CarInfo));
    w.u8_(id);
    w.bytes(info);
    if (to) send_reliable(*to, w);
    else broadcast_reliable(w);
}

void Session::set_car_info(const Bytes& info) {
    if (info.size() > 1024) return;
    my_car_info_ = info;
    if (my_id_ >= 0) car_info_[u8(my_id_)] = info;
    if (host_) {
        send_car_info(nullptr, 0, info);
        push([] { Event e; e.kind = Event::Kind::CarInfo; e.player = 0; return e; }());
        return;
    }
    if (Conn* h = host_conn(); h && my_id_ >= 0) {
        Writer w;
        w.u8_(u8(Msg::CarInfo));
        w.bytes(info);
        send_reliable(*h, w);
    }
}

const Bytes* Session::car_info(u8 id) const {
    auto it = car_info_.find(id);
    return it == car_info_.end() ? nullptr : &it->second;
}

void Session::set_car(const std::string& car, u8 paint) {
    if (host_) {
        if (Player* p = player_mut(0)) p->car = car, p->paint = paint, roster_dirty_ = true;
        return;
    }
    Player* me = my_id_ >= 0 ? player_mut(u8(my_id_)) : nullptr;
    bool ready = me && me->ready;
    if (me) me->car = car, me->paint = paint;
    Conn* h = host_conn();
    if (!h) return;
    Writer w;
    w.u8_(u8(Msg::Car));
    w.str(car);
    w.u8_(paint);
    w.u8_(ready ? 1 : 0);
    send_reliable(*h, w);
}

void Session::set_ready(bool ready) {
    if (host_) {
        if (Player* p = player_mut(0)) p->ready = ready, roster_dirty_ = true;
        return;
    }
    Player* me = my_id_ >= 0 ? player_mut(u8(my_id_)) : nullptr;
    Conn* h = host_conn();
    if (!me || !h) return;
    me->ready = ready;
    Writer w;
    w.u8_(u8(Msg::Car));
    w.str(me->car);
    w.u8_(me->paint);
    w.u8_(ready ? 1 : 0);
    send_reliable(*h, w);
}

void Session::set_settings(const Settings& s) {
    if (!host_) return;
    settings_ = s;
    settings_.laps = std::clamp(s.laps, 1, 99);
    broadcast_reliable(settings_msg());
}

bool Session::all_ready() const {
    if (!host_ || players_.size() < 2) return false;
    for (const Player& p : players_)
        if (!p.ready && p.id != 0) return false;
    return true;
}

void Session::load() {
    if (!host_) return;
    if (server_.valid() && registered_ && relay_links_needed() > 0) {
        if (wait_.waiting) return;
        // Link slots first: server_tick asks, the Load goes out once they are granted.
        wait_ = RelayWait{};
        wait_.waiting = true;
        reserve_started_ = now();
        reserve_sent_ = reserve_heard_ = 0;
        reserve_links_ = 0;
        roster_dirty_ = true;
        LOG_INFO("Online: asking the relay server for %d link slots", relay_links_needed());
        return;
    }
    start_load();
}

void Session::cancel_start() {
    if (!host_ || !wait_.waiting) return;
    wait_ = RelayWait{};
    roster_dirty_ = true;
    release_slots();
}

void Session::release_slots() {
    if (!server_.valid() || !registered_) return;
    Writer w = rv_request(Rv::Release, cookie_, version_);
    w.str(code_);
    put_u64(w, token_);
    send_server(w);
}

int Session::relay_links_needed() const {
    if (!host_ || !server_.valid()) return 0;
    int n = 0;
    for (const auto& c : conns_) // players connected to us through the relay
        if (!c->peer && c->player >= 0 && c->relay_key && c->close_at == 0) ++n;
    // Pairs of players: only those both report as direct need no slot (one still punching counts).
    for (const Player& a : players_)
        for (const Player& b : players_)
            if (a.id && b.id && a.id < b.id &&
                (!direct_pair(a.id, b.id) || relayed_.count({a.id, b.id}) || relayed_.count({b.id, a.id})))
                ++n;
    return std::min(n, kMaxLinks);
}

void Session::start_load() {
    phase_ = Phase::Loading;
    remotes_.clear();
    start_host_ms_ = 0;
    for (Player& p : players_) p.loaded = false;
    Event e;
    e.kind = Event::Kind::Load;
    e.course = settings_.course;
    e.laps = settings_.laps;
    for (const Player& p : players_) e.grid.push_back(p.id);
    grid_ = e.grid;
    Writer w;
    w.u8_(u8(Msg::Load));
    w.str(settings_.course);
    w.u8_(u8(settings_.laps));
    w.u8_(u8(e.grid.size()));
    for (u8 id : e.grid) w.u8_(id);
    broadcast_reliable(w);
    roster_dirty_ = true;
    push(std::move(e));
}

void Session::loaded() {
    if (host_) {
        if (Player* p = player_mut(0)) p->loaded = true, roster_dirty_ = true;
        return;
    }
    Conn* h = host_conn();
    if (!h) return;
    Writer w;
    w.u8_(u8(Msg::Loaded));
    send_reliable(*h, w);
}

bool Session::all_loaded() const {
    if (!host_) return false;
    for (const Player& p : players_)
        if (!p.loaded) return false;
    return !players_.empty();
}

void Session::go(u32 delay_ms) {
    if (!host_) return;
    phase_ = Phase::Racing;
    start_host_ms_ = now() + double(delay_ms);
    Writer w;
    w.u8_(u8(Msg::Go));
    w.f64_(start_host_ms_);
    broadcast_reliable(w);
    Event e;
    e.kind = Event::Kind::Go;
    e.start_at_ms = start_host_ms_;
    push(std::move(e));
}

void Session::lobby() {
    if (!host_) return;
    if (reserved_) release_slots(); // the race's link slots are free for the next lobby
    reserved_ = false;
    phase_ = Phase::Lobby;
    remotes_.clear();
    start_host_ms_ = 0;
    for (Player& p : players_) p.loaded = false, p.ready = false;
    Writer w;
    w.u8_(u8(Msg::Lobby));
    broadcast_reliable(w);
    roster_dirty_ = true;
    push(kind_event(Event::Kind::Lobby));
}

// --- race ---

void Session::send_state(const CarSnapshot& s) {
    if (!joined()) return;
    CarSnapshot me = s;
    me.player = u8(my_id_);
    Writer w;
    w.u8_(u8(Msg::State));
    write_snapshot(w, me);
    // Host: to every client. Client: to the host (it plays too, and relays for pairs
    // without a direct link) and to every directly linked peer.
    for (auto& c : conns_)
        if (c->close_at == 0 && (c->player >= 0 || !host_)) send_unreliable(*c, w);
}

void Session::send_finish(double race_time, double best_lap) {
    if (!joined()) return;
    Writer w;
    w.u8_(u8(Msg::Finish));
    w.u8_(u8(my_id_));
    w.f64_(race_time);
    w.f64_(best_lap);
    broadcast_reliable(w);
}

void Session::send_contact(u8 target, const Event::Hit& h) {
    if (!joined() || int(target) == my_id_) return;
    Writer w;
    w.u8_(u8(Msg::Contact));
    w.u8_(target);
    w.u8_(u8(my_id_));
    w.u32_(h.tick);
    w.f32_(h.point_x), w.f32_(h.point_z), w.f32_(h.jx), w.f32_(h.jz);
    // Straight to them when we have a link (host: every client), else through the host.
    if (Conn* p = host_ ? nullptr : peer_conn(target)) return send_reliable(*p, w);
    if (host_) {
        for (auto& c : conns_)
            if (c->player == int(target) && !c->peer) return send_reliable(*c, w);
        return;
    }
    if (Conn* hc = host_conn()) send_reliable(*hc, w);
}

void Session::log_flag(const Event& e) {
    FlagRecord r;
    r.suspect = e.player, r.reporter = e.flag.reporter, r.kind = e.flag.kind, r.value = e.flag.value;
    if (const Player* p = player(r.suspect)) r.suspect_name = p->name;
    if (const Player* p = player(r.reporter)) r.reporter_name = p->name;
    for (const FlagRecord& f : flag_log_) // one line per suspect, kind and reporter
        if (f.suspect == r.suspect && f.kind == r.kind && f.reporter == r.reporter) return;
    LOG_WARN("Online: %s flagged by %s: %s (%.1f)", r.suspect_name.c_str(), r.reporter_name.c_str(), flag_text(r.kind), r.value);
    flag_log_.push_back(r);
    if (flag_log_.size() > 50) flag_log_.erase(flag_log_.begin());
}

void Session::report_flag(u8 suspect, FlagKind kind, float value) {
    if (!joined() || int(suspect) == my_id_) return;
    if (host_) {
        Event e;
        e.kind = Event::Kind::Flag;
        e.player = suspect;
        e.flag = {kind, value, 0};
        log_flag(e);
        push(std::move(e));
        return;
    }
    Conn* h = host_conn();
    if (!h) return;
    Writer w;
    w.u8_(u8(Msg::Flag));
    w.u8_(suspect);
    w.u8_(u8(kind));
    w.f32_(value);
    send_reliable(*h, w);
}

void Session::kick(u8 id, const std::string& reason) {
    if (!host_ || id == 0) return;
    for (auto& c : conns_)
        if (c->player == int(id) && !c->peer) {
            Writer w;
            w.u8_(u8(Msg::Kick));
            w.str(reason);
            send_reliable(*c, w);
            c->close_at = now() + 1500; // dropped once it heard (or after 1.5 s)
            LOG_INFO("Online: removed player %u (%s)", unsigned(id), reason.c_str());
        }
    // Gone from the roster at once (no more races with them).
    players_.erase(std::remove_if(players_.begin(), players_.end(), [&](const Player& p) { return p.id == id; }), players_.end());
    roster_dirty_ = true;
}

void Session::retire() {
    if (!joined()) return;
    Writer w;
    w.u8_(u8(Msg::Retire));
    w.u8_(u8(my_id_));
    broadcast_reliable(w);
}

// --- mod car packages ---

namespace {
constexpr size_t kChunk = 1000;
constexpr size_t kChunksInFlight = 24;
constexpr u32 kMaxPackage = 32u << 20;
} // namespace

void Session::offer_package(const std::string& hash, Bytes blob) {
    offers_[hash] = std::make_shared<const Bytes>(std::move(blob));
}

const Session::Download* Session::download(const std::string& hash) const {
    auto it = downloads_.find(hash);
    return it == downloads_.end() ? nullptr : &it->second.d;
}

std::optional<Bytes> Session::take_package(const std::string& hash) {
    auto it = downloads_.find(hash);
    if (it == downloads_.end() || !it->second.d.done) return std::nullopt;
    Bytes b = std::move(it->second.data);
    downloads_.erase(it);
    return b;
}

void Session::serve_package(Conn& to, const std::string& hash) {
    auto it = offers_.find(hash);
    if (it == offers_.end()) return package_error(to, hash, "not available");
    for (const Conn::Send& s : to.sends)
        if (s.hash == hash) return; // already on its way
    to.sends.push_back({hash, it->second, 0});
}

void Session::package_error(Conn& to, const std::string& hash, const std::string& why) {
    Writer w;
    w.u8_(u8(Msg::PkgError));
    w.str(hash);
    w.str(why);
    send_reliable(to, w);
}

void Session::pump_transfers(Conn& c) {
    if (phase_ != Phase::Lobby) return; // never during a race
    while (!c.sends.empty() && c.out.size() < kChunksInFlight) {
        Conn::Send& s = c.sends.front();
        size_t n = std::min(kChunk, s.blob->size() - s.offset);
        Writer w;
        w.u8_(u8(Msg::PkgChunk));
        w.str(s.hash);
        w.u32_(u32(s.blob->size()));
        w.u32_(u32(s.offset));
        w.bytes(ByteSpan(s.blob->data() + s.offset, n));
        send_reliable(c, w);
        s.offset += n;
        if (s.offset >= s.blob->size()) c.sends.pop_front();
    }
}

void Session::request_package(const std::string& hash, u8 owner) {
    if (offers_.count(hash)) return; // we have it
    Incoming& in = downloads_[hash];
    if (in.d.done || (in.asked && !in.d.failed)) return;
    in = Incoming{};
    in.asked = true;
    Writer w;
    w.u8_(u8(Msg::PkgRequest));
    w.str(hash);
    w.u8_(owner);
    if (host_) {
        in.waiting.push_back(0);
        for (auto& c : conns_)
            if (c->player == int(owner) && !c->peer) {
                send_reliable(*c, w);
                return;
            }
        package_finished(hash, false, "that player is gone");
    } else if (Conn* h = host_conn()) {
        send_reliable(*h, w);
    }
}

void Session::package_finished(const std::string& hash, bool ok, const std::string& why) {
    auto it = downloads_.find(hash);
    if (it == downloads_.end()) return;
    Incoming& in = it->second;
    in.d.done = ok;
    in.d.failed = !ok;
    bool self = !host_;
    if (host_) {
        if (ok) offers_[hash] = std::make_shared<const Bytes>(in.data); // a verified copy for later players
        for (int pid : in.waiting) {
            if (pid == 0) {
                self = true;
                continue;
            }
            for (auto& c : conns_)
                if (c->player == pid && !c->peer) {
                    if (ok) serve_package(*c, hash);
                    else package_error(*c, hash, why);
                }
        }
        in.waiting.clear();
    }
    if (self) {
        Event e;
        e.kind = ok ? Event::Kind::PackageDone : Event::Kind::PackageFailed;
        e.text = hash;
        e.course = why;
        push(std::move(e));
        if (!ok) in.data.clear();
    } else if (host_) {
        downloads_.erase(it); // only relayed for others: the copy is in offers_
    }
}

void Session::package_chunk(Conn& from, Reader& r) {
    std::string hash = r.str();
    u32 total = r.u32_(), offset = r.u32_();
    ByteSpan data = r.bytes(r.remaining());
    if (!r.ok()) return;
    auto it = downloads_.find(hash);
    if (it == downloads_.end() || it->second.d.done || it->second.d.failed) return; // not asked for
    Incoming& in = it->second;
    if (total == 0 || total > kMaxPackage || (in.d.total && total != in.d.total) || offset != in.data.size() ||
        data.size() > total - offset)
        return package_finished(hash, false, "bad transfer");
    in.d.total = total;
    in.data.insert(in.data.end(), data.begin(), data.end());
    in.d.received = u32(in.data.size());
    if (in.data.size() < total) return;
    if (sha256_hex(in.data) != hash) return package_finished(hash, false, "the package does not match its hash");
    (void)from;
    package_finished(hash, true, "");
}

Session& online() {
    static Session s;
    return s;
}

namespace {
bool g_advertise_lan = true;
} // namespace

void Session::set_advertise_lan(bool on) { g_advertise_lan = on; }

void Session::collect_local_endpoints() {
    local_eps_.clear();
    if (!g_advertise_lan) return;
    for (const std::string& ip : local_addresses()) local_eps_.push_back(ip + ":" + std::to_string(sock_.local_port()));
}

} // namespace gt3r::net
