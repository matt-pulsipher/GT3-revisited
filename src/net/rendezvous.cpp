#include "net/rendezvous.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

namespace gt3r::net {

namespace {
constexpr char kCodeAlphabet[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ"; // 31 symbols
} // namespace

Writer rv_begin(Rv type, u16 version) {
    Writer w;
    w.bytes(ByteSpan(reinterpret_cast<const u8*>(kRvMagic), 4));
    w.u16_(version);
    w.u8_(u8(type));
    return w;
}

Writer rv_request(Rv type, u64 cookie, const GameVersion& game) {
    Writer w = rv_begin(type);
    put_u64(w, cookie);
    put_version(w, game);
    return w;
}

void rv_pad(Writer& w, size_t size) {
    if (w.data.size() < size) w.data.resize(size, 0);
}

bool valid_game_id(std::string_view id) {
    if (id.empty() || id.size() > kMaxGameId) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    return true;
}

void put_game_tag(Writer& w, const GameTag& tag) {
    w.str(tag.id);
    put_version(w, tag.version);
}

std::optional<GameTag> get_game_tag(Reader r) {
    if (r.remaining() == 0) return std::nullopt;
    GameTag t;
    t.id = r.str();
    t.version = get_version(r);
    if (!r.ok() || !valid_game_id(t.id)) return std::nullopt;
    return t;
}

const char* platform_name(RvPlatform p) {
    switch (p) {
    case RvPlatform::Windows: return "windows";
    case RvPlatform::MacOS: return "macos";
    case RvPlatform::Linux: return "linux";
    case RvPlatform::Android: return "android";
    case RvPlatform::Other: return "other";
    default: return "unknown";
    }
}

std::string GameVersion::number() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

std::string GameVersion::to_string() const { return number() + suffix; }

std::optional<GameVersion> parse_game_version(std::string_view text) {
    GameVersion v;
    u16* parts[3] = {&v.major, &v.minor, &v.patch};
    size_t i = 0;
    for (int k = 0; k < 3; ++k) {
        if (k > 0) {
            if (i >= text.size() || text[i] != '.') return std::nullopt;
            ++i;
        }
        size_t start = i;
        unsigned long n = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9' && i - start < 6) n = n * 10 + unsigned(text[i++] - '0');
        if (i == start || n > 0xFFFF) return std::nullopt;
        *parts[k] = u16(n);
    }
    if (i < text.size()) {
        if (text[i] != '-' || i + 1 == text.size() || text.size() - i > 32) return std::nullopt;
        for (size_t k = i + 1; k < text.size(); ++k)
            if (!std::isalnum(u8(text[k])) && text[k] != '.' && text[k] != '-') return std::nullopt;
        v.suffix = std::string(text.substr(i));
    }
    return v;
}

int compare_core(const GameVersion& a, const GameVersion& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

bool meets_minimum(const GameVersion& ours, const GameVersion& min_online) {
    return !min_online.known() || compare_core(ours, min_online) >= 0;
}

bool update_available(const GameVersion& ours, const GameVersion& latest) {
    if (!latest.known()) return false;
    int c = compare_core(latest, ours);
    return c > 0 || (c == 0 && !ours.suffix.empty());
}

void put_version(Writer& w, const GameVersion& v) { w.u16_(v.major), w.u16_(v.minor), w.u16_(v.patch); }

GameVersion get_version(Reader& r) {
    GameVersion v;
    v.major = r.u16_();
    v.minor = r.u16_();
    v.patch = r.u16_();
    return v;
}

u16 rv_version(ByteSpan data) {
    if (data.size() < kRvHeader || std::memcmp(data.data(), kRvMagic, 4) != 0) return 0;
    return u16(data[4] | data[5] << 8);
}

bool rv_parse(ByteSpan data, Rv& type, Reader& body) {
    if (data.size() < 7 || std::memcmp(data.data(), kRvMagic, 4) != 0) return false;
    Reader r(data);
    r.bytes(4);
    if (r.u16_() != kRendezvousVersion) return false;
    type = Rv(r.u8_());
    body = r;
    return r.ok();
}

void put_u64(Writer& w, u64 v) { w.u32_(u32(v)), w.u32_(u32(v >> 32)); }

u64 get_u64(Reader& r) {
    u64 lo = r.u32_();
    u64 hi = r.u32_();
    return lo | hi << 32;
}

void put_endpoints(Writer& w, const std::vector<std::string>& eps, size_t max) {
    size_t n = std::min(eps.size(), max);
    w.u8_(u8(n));
    for (size_t i = 0; i < n; ++i) w.str(eps[i]);
}

std::vector<std::string> get_endpoints(Reader& r, size_t max) {
    std::vector<std::string> out;
    size_t n = r.u8_();
    for (size_t i = 0; i < n && r.ok(); ++i) {
        std::string e = r.str();
        if (i < max && !e.empty() && e.size() < 64) out.push_back(e);
    }
    return out;
}

u64 pair_key(u64 token, u8 a, u8 b) {
    if (a > b) std::swap(a, b);
    u64 h = 1469598103934665603ull;
    auto mix = [&](u8 x) {
        h ^= x;
        h *= 1099511628211ull;
    };
    for (int i = 0; i < 8; ++i) mix(u8(token >> (8 * i)));
    mix(a), mix(b);
    return h ? h : 1;
}

namespace {
inline u64 rotl(u64 x, int b) { return (x << b) | (x >> (64 - b)); }
inline u64 load64(const u8* p) {
    u64 v = 0;
    for (int i = 7; i >= 0; --i) v = v << 8 | p[i];
    return v;
}
} // namespace

u64 siphash24(const u8 key[16], ByteSpan data) {
    u64 k0 = load64(key), k1 = load64(key + 8);
    u64 v0 = 0x736f6d6570736575ull ^ k0, v1 = 0x646f72616e646f6dull ^ k1;
    u64 v2 = 0x6c7967656e657261ull ^ k0, v3 = 0x7465646279746573ull ^ k1;
    auto round = [&] {
        v0 += v1, v1 = rotl(v1, 13), v1 ^= v0, v0 = rotl(v0, 32);
        v2 += v3, v3 = rotl(v3, 16), v3 ^= v2;
        v0 += v3, v3 = rotl(v3, 21), v3 ^= v0;
        v2 += v1, v1 = rotl(v1, 17), v1 ^= v2, v2 = rotl(v2, 32);
    };
    size_t n = data.size(), full = n & ~size_t(7);
    for (size_t i = 0; i < full; i += 8) {
        u64 m = load64(data.data() + i);
        v3 ^= m;
        round(), round();
        v0 ^= m;
    }
    u64 last = u64(n & 0xFF) << 56;
    for (size_t i = 0; i < (n & 7); ++i) last |= u64(data[full + i]) << (8 * i);
    v3 ^= last;
    round(), round();
    v0 ^= last;
    v2 ^= 0xFF;
    round(), round(), round(), round();
    return v0 ^ v1 ^ v2 ^ v3;
}

std::string make_code(u64 random) {
    std::string s;
    for (size_t i = 0; i < kCodeLength; ++i) {
        s += kCodeAlphabet[random % (sizeof(kCodeAlphabet) - 1)];
        random /= (sizeof(kCodeAlphabet) - 1);
    }
    return s;
}

bool valid_code(const std::string& code) {
    if (code.size() != kCodeLength) return false;
    for (char c : code)
        if (c == 0 || !std::strchr(kCodeAlphabet, c)) return false;
    return true;
}

std::string format_code(const std::string& code) {
    return valid_code(code) ? code.substr(0, 4) + "-" + code.substr(4) : code;
}

std::string normalize_code(const std::string& text) {
    std::string s;
    for (char c : text) {
        if (c == ' ' || c == '-') continue;
        s += char(std::toupper(u8(c)));
    }
    return s;
}

} // namespace gt3r::net
