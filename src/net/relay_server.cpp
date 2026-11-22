#include "net/relay_server.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <queue>
#include <random>

namespace gt3r::net {

RelayServer::RelayServer() : ip_slots_(kIpSlots) {
    race_ms_ = limits_.race_estimate_ms;
    std::random_device rd;
    for (u8* key : {cookie_key_, slot_key_, rng_key_})
        for (int i = 0; i < 16; i += 4) {
            u32 v = rd();
            std::memcpy(key + i, &v, 4);
        }
    // Should random_device be deterministic somewhere, the start time still differs per run.
    u64 t = u64(now_ms() * 1000);
    for (int i = 0; i < 8; ++i) rng_key_[i] ^= u8(t >> (8 * i));
}

bool RelayServer::open(u16 port, std::string* error) { return sock_.open(port, error); }

RelayServer::Stats RelayServer::stats() const {
    Stats s = stats_;
    s.sessions = sessions_.size();
    s.channels = channels_.size();
    s.pending = nonces_.size();
    s.links_used = links_used_;
    s.links_capacity = limits_.relay_links;
    s.reservations = reservations_.size();
    s.queued = queue_.size();
    s.race_estimate_ms = race_ms_;
    return s;
}

RelayServer::QueueInfo RelayServer::queue_info(const std::string& code, double now) const {
    QueueInfo q;
    std::vector<double> waits = queue_waits(now);
    for (size_t i = 0; i < queue_.size(); ++i)
        if (queue_[i].code == code) q.position = i + 1, q.wait_ms = waits[i];
    return q;
}

size_t RelayServer::links_of(const std::string& code) const {
    for (const auto& [serial, r] : reservations_)
        if (r.code == code) return r.links;
    return 0;
}

// --- helpers ---

bool RelayServer::Bucket::has(double rate, double burst, double now, double cost) {
    if (tokens < 0) tokens = burst, at = now;
    tokens = std::min(burst, tokens + std::max(0.0, now - at) / 1000.0 * rate);
    at = now;
    return tokens >= cost;
}

bool RelayServer::Bucket::take(double rate, double burst, double now, double cost) {
    if (!has(rate, burst, now, cost)) return false;
    tokens -= cost;
    return true;
}

u64 RelayServer::random() { // SipHash as a counter-mode PRF: codes and nonces cannot be predicted from earlier ones
    u8 c[8];
    for (int i = 0; i < 8; ++i) c[i] = u8(rng_counter_ >> (8 * i));
    ++rng_counter_;
    return siphash24(rng_key_, ByteSpan(c, 8));
}

u64 RelayServer::window(double now) const { return u64(std::max(0.0, std::floor(now / limits_.cookie_window_ms))); }

u64 RelayServer::cookie(const Address& a, u64 win) const {
    u8 m[26];
    a.ip16(m);
    u16 port = a.port();
    m[16] = u8(port), m[17] = u8(port >> 8);
    for (int i = 0; i < 8; ++i) m[18 + i] = u8(win >> (8 * i));
    return siphash24(cookie_key_, ByteSpan(m, sizeof m));
}

std::string RelayServer::ip_key(const Address& a) {
    u8 ip[16];
    a.ip16(ip);
    static const u8 kMapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
    if (std::memcmp(ip, kMapped, 12) != 0) std::memset(ip + 8, 0, 8); // IPv6: one /64 is one source
    return std::string(reinterpret_cast<const char*>(ip), 16);
}

bool RelayServer::trusted(const Address& a) const {
    if (!limits_.bench) return false;
    u8 ip[16];
    a.ip16(ip);
    static const u8 kMapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
    static const u8 kV6Loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    return (std::memcmp(ip, kMapped, 12) == 0 && ip[12] == 127) || std::memcmp(ip, kV6Loopback, 16) == 0;
}

// Unless --log-ips, the log never holds an IP: within a UTC day one source (IPv6: /64) always
// gets the same tag, so lobbies and joins can still be counted per player; a new day (or a
// restart) draws a new key, so tags cannot be matched across days or reversed by hashing every
// address.
std::string RelayServer::ip_tag(const Address& a) {
    long long day = (long long)(std::time(nullptr) / 86400);
    if (day != tag_day_) {
        for (int i = 0; i < 16; i += 8) {
            u64 v = random();
            std::memcpy(tag_key_ + i, &v, 8);
        }
        tag_day_ = day;
        log("ip tags: new key (tags do not match earlier ones)");
    }
    std::string k = ip_key(a);
    u64 h = siphash24(tag_key_, ByteSpan(reinterpret_cast<const u8*>(k.data()), k.size()));
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08x", unsigned(h >> 32));
    return buf;
}

std::string RelayServer::ip_of(const Address& a) {
    std::string s = a.to_string();
    size_t c = s.rfind(':');
    return c == std::string::npos ? s : s.substr(0, c);
}

std::string RelayServer::who(const Address& a) {
    std::string s = log_ips_ ? ip_of(a) : ip_tag(a);
    if (country_) {
        std::string c = country_(a);
        s += " (" + (c.empty() ? std::string("??") : c) + ")";
    }
    return s;
}

RelayServer::IpSlot& RelayServer::slot(const Address& a) {
    std::string k = ip_key(a);
    u64 h = siphash24(slot_key_, ByteSpan(reinterpret_cast<const u8*>(k.data()), k.size()));
    return ip_slots_[h % kIpSlots];
}

// Error texts are fixed and short: with the header they stay under kMinRequest, so even an
// error is no larger than the (cookie-checked) request that caused it.
void RelayServer::error(const Address& to, RvError code, const char* text) {
    Writer w = rv_begin(Rv::Error);
    w.u8_(u8(code));
    w.str(text);
    send(to, w);
}

void RelayServer::refused(const Address& to, const char* text) {
    Writer w = rv_begin(Rv::Reservation);
    w.u8_(u8(RvSlot::Refused));
    w.u8_(0);
    w.u16_(0), w.u16_(0);
    w.str(text);
    send(to, w);
}

void RelayServer::version_error(const Address& to, u16 version, size_t request_size) {
    // In the requester's own header when it is older (the Error layout never changed), so an
    // old game shows the text. The longest text that keeps the reply within the request's size.
    static const char* const kOld[] = {"please update the game to play online", "update the game", "update"};
    static const char* const kNew[] = {"the relay server is older than the game", "old relay server", "old relay"};
    bool older = version < kRendezvousVersion;
    for (const char* text : older ? kOld : kNew) {
        Writer w = rv_begin(Rv::Error, older ? version : kRendezvousVersion);
        w.u8_(u8(RvError::Version));
        w.str(text);
        if (w.data.size() <= request_size) return send(to, w);
    }
}

void RelayServer::registered(const Address& to, const std::string& code) {
    Writer w = rv_begin(Rv::Registered);
    w.str(code);
    w.str(to.to_string());
    send(to, w);
}

void RelayServer::move_host(Session& s, const Address& to) {
    if (s.host == to) return;
    if (auto c = sessions_per_ip_.find(ip_key(s.host)); c != sessions_per_ip_.end() && --c->second == 0)
        sessions_per_ip_.erase(c);
    ++sessions_per_ip_[ip_key(to)];
    s.host = to;
}

void RelayServer::erase_session(std::map<std::string, Session>::iterator it, double now) {
    if (auto c = sessions_per_ip_.find(ip_key(it->second.host)); c != sessions_per_ip_.end() && --c->second == 0)
        sessions_per_ip_.erase(c);
    by_token_.erase(it->second.token);
    u64 serial = it->second.serial;
    sessions_.erase(it); // its nonces expire on their own (forget_nonce checks the serial)
    // Its slots and its place in the queue go with it.
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [&](const Queued& q) { return q.serial == serial; }),
                 queue_.end());
    if (reservations_.count(serial)) release(serial, now, "session closed");
}

void RelayServer::forget_nonce(const Nonce& n) {
    if (auto s = sessions_.find(n.code); s != sessions_.end() && s->second.serial == n.serial && s->second.pending)
        --s->second.pending;
}

// --- datagrams ---

void RelayServer::step(int wait_ms) {
    if (!sock_.is_open()) return;
    sock_.wait_readable(wait_ms);
    u8 buf[2048];
    Address from;
    for (int i = 0; i < 4096; ++i) {
        size_t n = sock_.recv_from(from, buf, sizeof buf);
        if (n == 0) break;
        handle(from, ByteSpan(buf, n), now_ms());
    }
    double now = now_ms();
    if (now - last_expire_ > 1000) {
        expire(now);
        last_expire_ = now;
    }
}

void RelayServer::handle(const Address& from, ByteSpan data, double now) {
    if (data.size() >= kRelayHeader && std::memcmp(data.data(), kRelayMagic, 4) == 0) {
        relay(from, data, now);
        return;
    }
    u16 version = rv_version(data);
    if (version == 0) return; // anything else: silence (no reflection)
    IpSlot& ip = slot(from);
    if (!trusted(from) && (!ip.msgs.take(limits_.ip_msgs_per_s, limits_.ip_msgs_burst, now) ||
                           !all_msgs_.take(limits_.msgs_per_s, limits_.msgs_per_s, now))) {
        ++stats_.limited;
        return;
    }
    if (version != kRendezvousVersion) return version_error(from, version, data.size());
    Rv type;
    Reader r(data);
    if (data.size() < kMinRequest || !rv_parse(data, type, r)) return;
    // Creates nothing and answers with less than it was sent (no cookie needed).
    if (type == Rv::VersionQuery) return version_info(from, r, data.size());
    if (type != Rv::Register && type != Rv::Keepalive && type != Rv::Lookup && type != Rv::Bind &&
        type != Rv::Unregister && type != Rv::Reserve && type != Rv::Release)
        return; // replies (or unknown types) sent to us: silence
    u64 c = get_u64(r);
    GameVersion game = get_version(r);
    u64 win = window(now);
    // Unregister and Release create nothing and send nothing; the session token is their proof.
    bool checked = type != Rv::Unregister && type != Rv::Release;
    if (checked && c != cookie(from, win) && (win == 0 || c != cookie(from, win - 1))) {
        Writer w = rv_begin(Rv::Challenge); // 16 bytes, the request was >= kMinRequest
        w.u8_(u8(type));
        put_u64(w, cookie(from, win));
        ++stats_.challenges;
        return send(from, w);
    }
    handle_control(from, type, r, game, now, ip);
}

void RelayServer::version_info(const Address& from, Reader& r, size_t request_size) {
    if (request_size < kVersionQuerySize) return; // unpadded: the reply could be larger
    get_u64(r);    // cookie (not checked)
    get_version(r); // the game's (not kept)
    u8 platform = r.u8_();
    if (!r.ok()) return;
    ++stats_.version_queries;
    ++stats_.queries_by_platform[platform < kPlatformCount ? platform : 0];
    Writer w = rv_begin(Rv::VersionInfo);
    put_version(w, versions_.latest);
    put_version(w, versions_.min_online);
    w.str(versions_.release_url.size() <= kMaxReleaseUrl ? versions_.release_url : "");
    if (w.data.size() <= request_size) send(from, w);
}

void RelayServer::update_required(const Address& to) {
    ++stats_.outdated;
    Writer w = rv_begin(Rv::Error);
    w.u8_(u8(RvError::UpdateRequired));
    w.str("update required to play online");
    put_version(w, versions_.latest);
    w.str(versions_.release_url.size() <= kMaxReleaseUrl ? versions_.release_url : "");
    send(to, w);
}

void RelayServer::handle_control(const Address& from, Rv type, Reader& r, const GameVersion& game, double now,
                                 IpSlot& ip) {
    switch (type) {
    case Rv::Register: {
        if (outdated(game)) return update_required(from);
        u64 token = get_u64(r);
        std::vector<std::string> eps = get_endpoints(r, 4);
        if (!r.ok() || token == 0) return error(from, RvError::BadRequest, "malformed register");
        // A resent request (only the host knows its token): the session keeps its code.
        if (auto t = by_token_.find(token); t != by_token_.end()) {
            Session& s = sessions_.at(t->second);
            move_host(s, from); // its NAT mapping changed between tries
            s.last_seen = now;
            return registered(from, s.code);
        }
        if (sessions_.size() >= limits_.max_sessions) return error(from, RvError::Busy, "the relay is full");
        std::string key = ip_key(from);
        if (auto c = sessions_per_ip_.find(key);
            c != sessions_per_ip_.end() && c->second >= limits_.max_sessions_per_ip && !trusted(from))
            return error(from, RvError::Busy, "too many sessions from this address");
        std::string code;
        do code = make_code(random());
        while (sessions_.count(code));
        Session s;
        s.code = code;
        s.host = from;
        s.token = token;
        s.serial = ++serial_;
        s.host_eps = eps;
        s.last_seen = now;
        sessions_[code] = s;
        by_token_[token] = code;
        ++sessions_per_ip_[key];
        log("session " + code + " registered by " + who(from));
        return registered(from, code);
    }
    case Rv::Keepalive:
    case Rv::Unregister:
    case Rv::Release: {
        std::string code = r.str();
        u64 token = get_u64(r);
        auto it = sessions_.find(code);
        if (!r.ok() || it == sessions_.end() || it->second.token != token) return;
        Session& s = it->second;
        if (type == Rv::Unregister) {
            log("session " + code + " closed");
            erase_session(it, now);
            schedule(now);
            return;
        }
        if (type == Rv::Release) { // the race is over, or the host stopped waiting
            size_t queued = queue_.size();
            queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [&](const Queued& q) { return q.serial == s.serial; }),
                         queue_.end());
            if (queue_.size() != queued) log("session " + code + " left the queue");
            if (reservations_.count(s.serial)) release(s.serial, now, "released");
            schedule(now);
            return;
        }
        s.last_seen = now;
        move_host(s, from); // the host's NAT mapping changed (the cookie proves the new address)
        // A host holding or waiting for slots hears where it stands (a queued host keeps
        // its place with these keepalives).
        bool queued = false;
        for (const Queued& q : queue_) queued = queued || q.serial == s.serial;
        if (queued || reservations_.count(s.serial)) slot_status(from, s, now);
        return;
    }
    case Rv::Reserve: {
        if (outdated(game)) return update_required(from);
        std::string code = r.str();
        u64 token = get_u64(r);
        size_t links = r.u8_();
        if (!r.ok() || links == 0 || links > size_t(kMaxLinks)) return error(from, RvError::BadRequest, "malformed reserve");
        auto it = sessions_.find(code);
        if (it == sessions_.end() || it->second.token != token) return refused(from, "no such session");
        it->second.last_seen = now;
        move_host(it->second, from);
        return reserve(from, it->second, links, now);
    }
    case Rv::Lookup: {
        if (outdated(game)) return update_required(from);
        std::string code = normalize_code(r.str());
        r.u16_(); // the game's protocol (the host checks it in Hello)
        std::vector<std::string> eps = get_endpoints(r, 5);
        if (!r.ok()) return error(from, RvError::BadRequest, "malformed lookup");
        // Guessing codes: refused before the lookup, so a refusal tells nothing about the code.
        if (!trusted(from) && (!ip.misses.has(limits_.ip_misses_per_s, limits_.ip_misses_burst, now) ||
                               !all_misses_.has(limits_.misses_per_s, limits_.misses_per_s, now))) {
            ++stats_.limited;
            return error(from, RvError::Busy, "too many lookups: try again in a minute");
        }
        auto it = sessions_.find(code);
        if (it == sessions_.end()) {
            ip.misses.take(limits_.ip_misses_per_s, limits_.ip_misses_burst, now);
            all_misses_.take(limits_.misses_per_s, limits_.misses_per_s, now);
            return error(from, RvError::NotFound, "no session with that code");
        }
        Session& s = it->second;
        if (nonces_.size() >= limits_.max_pending || s.pending >= limits_.max_pending_per_session ||
            (!trusted(from) && !s.joins.take(limits_.joins_per_s, limits_.joins_burst, now))) {
            ++stats_.limited;
            return error(from, RvError::Busy, "the session is busy: try again shortly");
        }
        u64 nonce = 0;
        while (nonce == 0 || nonces_.count(nonce) || channels_.count(nonce)) nonce = random();
        nonces_[nonce] = {code, s.serial, now};
        ++s.pending;
        std::vector<std::string> host_eps{s.host.to_string()};
        for (const std::string& e : s.host_eps) host_eps.push_back(e);
        std::vector<std::string> client_eps{from.to_string()};
        for (const std::string& e : eps) client_eps.push_back(e);
        Writer w = rv_begin(Rv::HostInfo);
        put_u64(w, nonce);
        w.str(from.to_string());
        put_endpoints(w, host_eps);
        send(from, w);
        Writer in = rv_begin(Rv::Incoming);
        put_u64(in, nonce);
        put_endpoints(in, client_eps);
        send(s.host, in);
        log("join " + code + " from " + who(from));
        return;
    }
    case Rv::Bind: {
        u64 key = get_u64(r);
        std::string code = r.str();
        u64 token = get_u64(r);
        u8 a = r.u8_(), b = r.u8_();
        if (!r.ok() || key == 0) return error(from, RvError::BadRequest, "malformed bind");
        bool ok = false;
        u64 owner = 0; // the session a new channel belongs to (its link slots)
        // An end already bound may always refresh its binding (a join's nonce expires after a
        // couple of minutes, its channel lives on while used).
        if (auto ch = channels_.find(key); ch != channels_.end())
            for (const Address& e : ch->second.end) ok = ok || (e.valid() && e == from);
        if (!ok && outdated(game)) return update_required(from); // a new channel or end: creates state
        if (auto n = nonces_.find(key); n != nonces_.end() && n->second.code == code)
            ok = true, owner = n->second.serial; // a join pair
        if (auto s = sessions_.find(code); !ok && s != sessions_.end() && token == s->second.token && a != b &&
                                           a < kMaxPlayers && b < kMaxPlayers && key == pair_key(token, a, b))
            ok = true, owner = s->second.serial; // two players of the session (at most kMaxLinks channels per session)
        if (!ok) return error(from, RvError::Denied, "not allowed to bind this channel");
        auto it = channels_.find(key);
        if (it == channels_.end()) {
            if (channels_.size() >= limits_.max_channels) return error(from, RvError::Busy, "the relay is full");
            Channel c;
            c.budget[0] = c.budget[1] = limits_.channel_bytes_per_s;
            c.refill_at = now;
            c.serial = owner;
            it = channels_.emplace(key, c).first;
        }
        Channel& c = it->second;
        int slot = -1;
        for (int i = 0; i < 2; ++i)
            if (c.end[i].valid() && c.end[i] == from) slot = i;
        for (int i = 0; i < 2 && slot < 0; ++i)
            if (!c.end[i].valid()) slot = i;
        for (int i = 0; i < 2 && slot < 0; ++i) // a stale end (its NAT mapping moved)
            if (now - c.seen[i] > limits_.rebind_after_ms) slot = i;
        if (slot < 0) return error(from, RvError::Denied, "channel in use");
        c.end[slot] = from;
        c.seen[slot] = now;
        c.last_used = now;
        Writer w = rv_begin(Rv::Bound);
        put_u64(w, key);
        return send(from, w);
    }
    default: return;
    }
}

void RelayServer::relay(const Address& from, ByteSpan data, double now) {
    Reader r(data);
    r.bytes(4);
    u64 key = get_u64(r);
    auto it = channels_.find(key);
    // Only game datagrams, only between the two bound ends (both proved their address with a cookie).
    bool game = data.size() >= kRelayHeader + 4 && std::memcmp(data.data() + kRelayHeader, "GT3N", 4) == 0;
    if (it == channels_.end() || !game || data.size() > kMaxDatagram + kRelayHeader + 64) {
        ++stats_.dropped;
        return;
    }
    Channel& c = it->second;
    int me = c.end[0].valid() && c.end[0] == from ? 0 : c.end[1].valid() && c.end[1] == from ? 1 : -1;
    if (me < 0 || !c.end[1 - me].valid()) {
        ++stats_.dropped;
        return;
    }
    // A channel of a racing lobby takes one of its reservation's slots with its first datagram.
    if (!c.active && c.serial)
        if (auto rv = reservations_.find(c.serial); rv != reservations_.end() && rv->second.channels.size() < rv->second.links) {
            c.active = true;
            rv->second.channels.push_back(key);
        }
    // Flood guards: a racing channel's per direction; a lobby (idle) channel's, and all lobby
    // channels' together. Admission is the slots, not these.
    double rate = c.active ? limits_.channel_bytes_per_s : limits_.idle_channel_bytes_per_s;
    double dt = std::max(0.0, now - c.refill_at) / 1000.0;
    c.refill_at = now;
    for (double& b : c.budget) b = std::min(rate, b + dt * rate);
    double size = double(data.size());
    if (c.budget[me] < size || (!c.active && !all_idle_.take(limits_.idle_bytes_per_s, limits_.idle_bytes_per_s, now, size))) {
        ++stats_.dropped;
        return;
    }
    c.budget[me] -= size;
    c.seen[me] = now;
    c.last_used = c.last_data = now;
    if (now - c.win_at >= 1000) { // the peak per-channel rate (stats)
        if (c.win_at > 0)
            for (double b : c.win_bytes)
                stats_.peak_channel_bytes_per_s = std::max(stats_.peak_channel_bytes_per_s, b * 1000 / (now - c.win_at));
        c.win_at = now;
        c.win_bytes[0] = c.win_bytes[1] = 0;
    }
    c.win_bytes[me] += size;
    send_to(c.end[1 - me], data);
    ++stats_.relayed_datagrams;
    stats_.relayed_bytes += data.size();
}

void RelayServer::expire(double now) {
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (now - it->second.last_seen > limits_.session_timeout_ms) {
            log("session " + it->first + " expired");
            auto next = std::next(it);
            erase_session(it, now);
            it = next;
        } else {
            ++it;
        }
    }
    for (auto it = nonces_.begin(); it != nonces_.end();) {
        if (now - it->second.created > limits_.nonce_timeout_ms) {
            forget_nonce(it->second);
            it = nonces_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = channels_.begin(); it != channels_.end();)
        it = now - it->second.last_used > limits_.channel_timeout_ms ? channels_.erase(it) : std::next(it);
    // Slots whose session is gone or whose channels went quiet (the race ended without a
    // Release, or everyone left).
    std::vector<u64> idle;
    for (const auto& [serial, r] : reservations_) {
        auto s = sessions_.find(r.code);
        if (s == sessions_.end() || s->second.serial != serial || now - last_traffic(r) > limits_.reservation_idle_ms)
            idle.push_back(serial);
    }
    for (u64 serial : idle) release(serial, now, "idle");
    // Queued lobbies: their session must be alive and its host still keeping it (keepalives).
    for (auto it = queue_.begin(); it != queue_.end();) {
        auto s = sessions_.find(it->code);
        bool alive = s != sessions_.end() && s->second.serial == it->serial;
        if (alive && now - it->requested <= limits_.queue_timeout_ms &&
            now - s->second.last_seen <= limits_.queue_ping_timeout_ms) {
            ++it;
            continue;
        }
        ++stats_.queue_expired;
        log("session " + it->code + " dropped from the queue");
        it = queue_.erase(it);
    }
    schedule(now);
}

// --- link slots and the queue ---

double RelayServer::last_traffic(const Reservation& r) const {
    double t = r.granted;
    for (u64 key : r.channels)
        if (auto c = channels_.find(key); c != channels_.end()) t = std::max(t, c->second.last_data);
    return t;
}

void RelayServer::reserve(const Address& from, Session& s, size_t links, double now) {
    if (auto rv = reservations_.find(s.serial); rv != reservations_.end()) {
        // Already racing: fewer links are given back; more are added only if they fit now
        // (never ahead of a queue). The reply says what it holds.
        Reservation& r = rv->second;
        if (links < r.links) {
            links_used_ -= r.links - links;
            r.links = links;
            while (r.channels.size() > links) {
                if (auto c = channels_.find(r.channels.back()); c != channels_.end()) c->second.active = false;
                r.channels.pop_back();
            }
            schedule(now);
        } else if (links > r.links && queue_.empty() && free_links() >= links - r.links) {
            links_used_ += links - r.links;
            r.links = links;
        }
        return slot_status(from, s, now);
    }
    if (links > limits_.relay_links) return refused(from, "the relay is too small for this race");
    auto q = std::find_if(queue_.begin(), queue_.end(), [&](const Queued& e) { return e.serial == s.serial; });
    if (q != queue_.end()) {
        q->links = links; // a player joined or left while waiting: keeps its place
    } else {
        if (queue_.size() >= limits_.max_queue) {
            ++stats_.limited;
            return refused(from, "the relay is busy: try again in a few minutes");
        }
        queue_.push_back({s.code, s.serial, links, now});
    }
    schedule(now); // a grant tells the host itself
    if (!reservations_.count(s.serial)) {
        if (q == queue_.end()) log("session " + s.code + " queued for " + std::to_string(links) + " links");
        slot_status(from, s, now);
    }
}

void RelayServer::release(u64 serial, double now, const char* why) {
    auto it = reservations_.find(serial);
    if (it == reservations_.end()) return;
    Reservation& r = it->second;
    for (u64 key : r.channels)
        if (auto c = channels_.find(key); c != channels_.end()) c->second.active = false;
    links_used_ -= std::min(links_used_, r.links);
    // The typical hold time (queue estimates) follows the races that ran.
    double held = std::min(now, last_traffic(r)) - r.granted;
    if (held > 20000) race_ms_ = std::clamp(race_ms_ * 0.7 + held * 0.3, 60000.0, 1800000.0);
    log("session " + r.code + ": " + std::to_string(r.links) + " links freed (" + why + ", held " +
        std::to_string(int(held / 1000)) + " s)");
    reservations_.erase(it);
}

void RelayServer::grant(const Queued& q, double now) {
    Reservation r;
    r.code = q.code;
    r.links = q.links;
    r.granted = now;
    reservations_[q.serial] = r;
    links_used_ += q.links;
    ++stats_.granted;
    log("session " + q.code + ": " + std::to_string(q.links) + " links granted" +
        (now - q.requested > 1000 ? " after " + std::to_string(int((now - q.requested) / 1000)) + " s in the queue" : "") +
        " (" + std::to_string(links_used_) + "/" + std::to_string(limits_.relay_links) + " in use)");
    if (auto s = sessions_.find(q.code); s != sessions_.end() && s->second.serial == q.serial)
        slot_status(s->second.host, s->second, now);
}

// FIFO, except that a smaller request may start ahead of the head when the head would still
// start within skip_horizon_ms (from the running reservations' expected ends); when that is
// unknown, only while the head has waited less than skip_horizon_ms.
void RelayServer::schedule(double now) {
    while (!queue_.empty()) {
        if (queue_.front().links <= free_links()) {
            Queued q = queue_.front();
            queue_.pop_front();
            grant(q, now);
            continue;
        }
        bool started = false;
        for (size_t i = 1; i < queue_.size() && !started; ++i) {
            if (queue_[i].links > free_links()) continue;
            double est = start_estimate(queue_.front().links, queue_[i].links, now);
            bool ok = est >= 0 ? est <= limits_.skip_horizon_ms : now - queue_.front().requested < limits_.skip_horizon_ms;
            if (!ok) continue;
            Queued q = queue_[i];
            queue_.erase(queue_.begin() + ptrdiff_t(i));
            grant(q, now);
            started = true;
        }
        if (!started) return;
    }
}

double RelayServer::start_estimate(size_t need, size_t extra, double now) const {
    double free = double(free_links()) - double(extra);
    if (free >= double(need)) return 0;
    std::vector<std::pair<double, size_t>> ends; // expected end, links
    for (const auto& [serial, r] : reservations_)
        if (r.granted + race_ms_ > now) ends.push_back({r.granted + race_ms_, r.links}); // overdue ones: unknown
    if (extra) ends.push_back({now + race_ms_, extra});
    std::sort(ends.begin(), ends.end());
    for (const auto& [end, links] : ends) {
        free += double(links);
        if (free >= double(need)) return end - now;
    }
    return -1;
}

std::vector<double> RelayServer::queue_waits(double now) const {
    using End = std::pair<double, size_t>;
    std::priority_queue<End, std::vector<End>, std::greater<End>> ends;
    for (const auto& [serial, r] : reservations_)
        if (r.granted + race_ms_ > now) ends.push({r.granted + race_ms_, r.links});
    size_t free = free_links();
    bool unknown = false;
    std::vector<double> out;
    double t = now; // in order: nobody starts before the one ahead of it
    for (const Queued& q : queue_) {
        while (!unknown && free < q.links) {
            if (ends.empty()) unknown = true;
            else {
                t = std::max(t, ends.top().first);
                free += ends.top().second;
                ends.pop();
            }
        }
        if (unknown) {
            out.push_back(-1);
            continue;
        }
        out.push_back(t - now);
        free -= q.links;
        ends.push({t + race_ms_, q.links});
    }
    return out;
}

void RelayServer::slot_status(const Address& to, const Session& s, double now) {
    Writer w = rv_begin(Rv::Reservation);
    if (auto rv = reservations_.find(s.serial); rv != reservations_.end()) {
        w.u8_(u8(RvSlot::Granted));
        w.u8_(u8(rv->second.links));
        w.u16_(0), w.u16_(0);
        return send(to, w);
    }
    for (size_t i = 0; i < queue_.size(); ++i)
        if (queue_[i].serial == s.serial) {
            double wait = queue_waits(now)[i];
            w.u8_(u8(RvSlot::Queued));
            w.u8_(u8(queue_[i].links));
            w.u16_(u16(std::min<size_t>(i + 1, 0xFFFF)));
            w.u16_(wait < 0 ? kWaitUnknown : u16(std::min(65534.0, std::ceil(wait / 1000))));
            return send(to, w);
        }
    w.u8_(u8(RvSlot::Released));
    w.u8_(0);
    w.u16_(0), w.u16_(0);
    send(to, w);
}

// --- versions file ---

bool check_relay_versions(const RelayServer::Versions& v, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    if (!v.latest.suffix.empty() || !v.min_online.suffix.empty()) return fail("versions are x.y.z (no suffix)");
    if (v.latest.known() && v.min_online.known() && compare_core(v.min_online, v.latest) > 0)
        return fail("min_online " + v.min_online.number() + " is above latest " + v.latest.number());
    const std::string& u = v.release_url;
    if (u.size() > kMaxReleaseUrl) return fail("release_url is longer than " + std::to_string(kMaxReleaseUrl) + " characters");
    if (!u.empty() && u.rfind("https://", 0) != 0 && u.rfind("http://", 0) != 0)
        return fail("release_url must start with https:// or http://");
    for (char ch : u)
        if (u8(ch) <= 32 || u8(ch) >= 127) return fail("release_url has a space or a non-ASCII character");
    return true;
}

bool parse_relay_versions(std::string_view text, RelayServer::Versions& out, std::string* error) {
    RelayServer::Versions v = out;
    auto trim = [](std::string_view t) {
        while (!t.empty() && (t.front() == ' ' || t.front() == '\t' || t.front() == '\r')) t.remove_prefix(1);
        while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r')) t.remove_suffix(1);
        return t;
    };
    int line_no = 0;
    while (!text.empty()) {
        size_t nl = text.find('\n');
        std::string_view line = trim(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        ++line_no;
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        std::string where = "line " + std::to_string(line_no) + ": ";
        if (eq == std::string_view::npos) {
            if (error) *error = where + "expected key = value";
            return false;
        }
        std::string_view key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key == "latest" || key == "min_online") {
            std::optional<GameVersion> g = parse_game_version(value);
            if (!g) {
                if (error) *error = where + "not a version (x.y.z): " + std::string(value.substr(0, 40));
                return false;
            }
            (key == "latest" ? v.latest : v.min_online) = *g;
        } else if (key == "release_url") {
            v.release_url = std::string(value);
        } else {
            if (error) *error = where + "unknown key " + std::string(key.substr(0, 40));
            return false;
        }
    }
    if (!check_relay_versions(v, error)) return false;
    out = v;
    return true;
}

} // namespace gt3r::net
