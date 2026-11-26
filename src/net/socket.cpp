#include "net/socket.h"

#include <atomic>
#include <chrono>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace gt3r::net {

namespace {

#if defined(_WIN32)
struct WsaInit {
    WsaInit() {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    }
    ~WsaInit() { WSACleanup(); }
};
void ensure_init() { static WsaInit init; }
#else
void ensure_init() {}
#endif

const sockaddr* sa(const Address& a) { return reinterpret_cast<const sockaddr*>(a.storage); }

} // namespace

int Address::family() const { return length ? int(sa(*this)->sa_family) : 0; }

u16 Address::port() const {
    if (family() == AF_INET) return ntohs(reinterpret_cast<const sockaddr_in*>(storage)->sin_port);
    if (family() == AF_INET6) return ntohs(reinterpret_cast<const sockaddr_in6*>(storage)->sin6_port);
    return 0;
}

std::string Address::to_string() const {
    char buf[INET6_ADDRSTRLEN] = {};
    if (family() == AF_INET) {
        auto* s = reinterpret_cast<const sockaddr_in*>(storage);
        inet_ntop(AF_INET, &s->sin_addr, buf, sizeof buf);
        return std::string(buf) + ":" + std::to_string(port());
    }
    if (family() == AF_INET6) {
        auto* s = reinterpret_cast<const sockaddr_in6*>(storage);
        const u8* b = reinterpret_cast<const u8*>(&s->sin6_addr);
        static const u8 kMapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
        if (std::memcmp(b, kMapped, 12) == 0) { // IPv4 peer on a dual-stack socket
            inet_ntop(AF_INET, b + 12, buf, sizeof buf);
            return std::string(buf) + ":" + std::to_string(port());
        }
        inet_ntop(AF_INET6, &s->sin6_addr, buf, sizeof buf);
        return "[" + std::string(buf) + "]:" + std::to_string(port());
    }
    return "(none)";
}

void Address::ip16(u8 out[16]) const {
    std::memset(out, 0, 16);
    if (family() == AF_INET) {
        out[10] = out[11] = 0xFF;
        std::memcpy(out + 12, &reinterpret_cast<const sockaddr_in*>(storage)->sin_addr, 4);
    } else if (family() == AF_INET6) {
        std::memcpy(out, &reinterpret_cast<const sockaddr_in6*>(storage)->sin6_addr, 16);
    }
}

bool Address::operator==(const Address& o) const {
    if (family() != o.family() || port() != o.port()) return false;
    if (family() == AF_INET)
        return std::memcmp(&reinterpret_cast<const sockaddr_in*>(storage)->sin_addr,
                           &reinterpret_cast<const sockaddr_in*>(o.storage)->sin_addr, 4) == 0;
    if (family() == AF_INET6)
        return std::memcmp(&reinterpret_cast<const sockaddr_in6*>(storage)->sin6_addr,
                           &reinterpret_cast<const sockaddr_in6*>(o.storage)->sin6_addr, 16) == 0;
    return length == 0 && o.length == 0;
}

bool resolve(const std::string& text, u16 default_port, Address& out, std::string* error) {
    ensure_init();
    std::string host = text, port = std::to_string(default_port);
    if (!host.empty() && host[0] == '[') { // [v6]:port
        size_t close = host.find(']');
        if (close == std::string::npos) {
            if (error) *error = "missing ']' in " + text;
            return false;
        }
        if (close + 1 < host.size() && host[close + 1] == ':') port = host.substr(close + 2);
        host = host.substr(1, close - 1);
    } else if (size_t c = host.find(':'); c != std::string::npos && host.find(':', c + 1) == std::string::npos) {
        port = host.substr(c + 1); // host:port (a bare v6 address has several ':')
        host = host.substr(0, c);
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (rc != 0 || !res) {
        if (error) *error = "cannot resolve " + text + ": " + gai_strerror(rc);
        return false;
    }
    // Prefer IPv4 (home routers forward it; v6 firewalls usually drop unsolicited packets).
    const addrinfo* pick = res;
    for (const addrinfo* r = res; r; r = r->ai_next)
        if (r->ai_family == AF_INET) {
            pick = r;
            break;
        }
    out = Address{};
    if (pick->ai_addrlen <= sizeof out.storage) {
        std::memcpy(out.storage, pick->ai_addr, pick->ai_addrlen);
        out.length = u32(pick->ai_addrlen);
    }
    freeaddrinfo(res);
    if (!out.valid() && error) *error = "unsupported address for " + text;
    return out.valid();
}

bool UdpSocket::setup(int family, u16 port, std::string* error) {
    close();
    ensure_init();
    Fd fd = Fd(::socket(family, SOCK_DGRAM, IPPROTO_UDP));
    if (fd == kInvalid) {
        if (error) *error = "socket() failed";
        return false;
    }
    if (family == AF_INET6) {
        int off = 0; // dual-stack
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&off), sizeof off);
    }
    sockaddr_storage local{};
    socklen_t len = 0;
    if (family == AF_INET6) {
        auto* s = reinterpret_cast<sockaddr_in6*>(&local);
        s->sin6_family = AF_INET6;
        s->sin6_addr = in6addr_any;
        s->sin6_port = htons(port);
        len = sizeof(sockaddr_in6);
    } else {
        auto* s = reinterpret_cast<sockaddr_in*>(&local);
        s->sin_family = AF_INET;
        s->sin_addr.s_addr = htonl(INADDR_ANY);
        s->sin_port = htons(port);
        len = sizeof(sockaddr_in);
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&local), len) != 0) {
        if (error) *error = "cannot use UDP port " + std::to_string(port) + " (in use?)";
#if defined(_WIN32)
        closesocket(fd);
#else
        ::close(fd);
#endif
        return false;
    }
#if defined(_WIN32)
    u_long nb = 1;
    ioctlsocket(fd, FIONBIO, &nb);
    // Don't report ICMP port-unreachable as WSAECONNRESET on later recvfrom calls.
    BOOL no = FALSE;
    DWORD ret = 0;
    WSAIoctl(fd, _WSAIOW(IOC_VENDOR, 12) /* SIO_UDP_CONNRESET */, &no, sizeof no, nullptr, 0, &ret, nullptr, nullptr);
#else
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
    fd_ = fd;
    return true;
}

bool UdpSocket::open(u16 port, std::string* error) {
#if defined(__APPLE__)
    // Here a dual-stack bind is not checked against IPv4 sockets: if another program already holds the port for IPv4,
    // that program gets every IPv4 datagram sent to it. So the IPv4 side is taken first (any free port for 0), and the
    // dual-stack socket is bound on that port while it is held; once it is bound, no IPv4 socket can take the port.
    close();
    for (int tries = 0;; ++tries) {
        UdpSocket v4;
        if (!v4.setup(AF_INET, port, error)) return false; // in use for IPv4
        const u16 got = v4.local_port();
        if (got && setup(AF_INET6, got, nullptr)) return true; // v4 closes here, the dual-stack socket keeps the port
        if (port != 0 || tries == 7) { // no dual-stack socket on it: IPv4 alone
            fd_ = v4.fd_;
            v4.fd_ = kInvalid;
            return true;
        }
    }
#else
    if (setup(AF_INET6, port, nullptr)) return true;
    return setup(AF_INET, port, error);
#endif
}

bool UdpSocket::open_for(const Address& peer, std::string* error) {
    return setup(peer.family() == AF_INET6 ? AF_INET6 : AF_INET, 0, error);
}

void UdpSocket::close() {
    if (fd_ == kInvalid) return;
#if defined(_WIN32)
    closesocket(fd_);
#else
    ::close(fd_);
#endif
    fd_ = kInvalid;
}

int UdpSocket::set_buffers(int bytes) {
    if (fd_ == kInvalid) return 0;
    for (int opt : {SO_RCVBUF, SO_SNDBUF})
        setsockopt(fd_, SOL_SOCKET, opt, reinterpret_cast<const char*>(&bytes), sizeof bytes);
    int got = 0;
    socklen_t len = sizeof got;
    getsockopt(fd_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&got), &len);
    return got;
}

u16 UdpSocket::local_port() const {
    if (fd_ == kInvalid) return 0;
    Address a;
    socklen_t len = sizeof a.storage;
    if (getsockname(fd_, reinterpret_cast<sockaddr*>(a.storage), &len) != 0) return 0;
    a.length = u32(len);
    return a.port();
}

bool UdpSocket::send_to(const Address& to, ByteSpan data) {
    if (fd_ == kInvalid || !to.valid()) return false;
    Address dst = to;
    // An IPv4 destination on a dual-stack socket: send to the v4-mapped address.
    sockaddr_in6 mapped{};
    if (to.family() == AF_INET) {
        sockaddr_storage me{};
        socklen_t ml = sizeof me;
        if (getsockname(fd_, reinterpret_cast<sockaddr*>(&me), &ml) == 0 && me.ss_family == AF_INET6) {
            auto* v4 = reinterpret_cast<const sockaddr_in*>(to.storage);
            mapped.sin6_family = AF_INET6;
            mapped.sin6_port = v4->sin_port;
            u8* b = reinterpret_cast<u8*>(&mapped.sin6_addr);
            b[10] = b[11] = 0xFF;
            std::memcpy(b + 12, &v4->sin_addr, 4);
            std::memcpy(dst.storage, &mapped, sizeof mapped);
            dst.length = sizeof mapped;
        }
    }
    auto n = ::sendto(fd_, reinterpret_cast<const char*>(data.data()), int(data.size()), 0, sa(dst), socklen_t(dst.length));
    return n == decltype(n)(data.size());
}

size_t UdpSocket::recv_from(Address& from, u8* buf, size_t cap) {
    if (fd_ == kInvalid) return 0;
    for (int tries = 0; tries < 64; ++tries) {
        Address a;
        socklen_t len = sizeof a.storage;
        auto n = ::recvfrom(fd_, reinterpret_cast<char*>(buf), int(cap), 0, reinterpret_cast<sockaddr*>(a.storage), &len);
        if (n > 0) {
            a.length = u32(len);
            from = a;
            return size_t(n);
        }
        if (n == 0) continue;
#if defined(_WIN32)
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) return 0;
        if (e == WSAECONNRESET || e == WSAEMSGSIZE) continue;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        if (errno == ECONNREFUSED || errno == EINTR) continue;
#endif
        return 0;
    }
    return 0;
}

bool UdpSocket::wait_readable(int timeout_ms) const {
    if (fd_ == kInvalid) return false;
    fd_set set;
    FD_ZERO(&set);
#if defined(_WIN32)
    FD_SET(SOCKET(fd_), &set);
#else
    FD_SET(fd_, &set);
#endif
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return ::select(int(fd_) + 1, &set, nullptr, nullptr, &tv) > 0;
}

std::vector<std::string> local_addresses() {
    std::vector<std::string> out;
#if !defined(_WIN32)
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return out;
    for (ifaddrs* i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        char buf[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr, buf, sizeof buf);
        std::string s = buf;
        if (s.rfind("127.", 0) != 0 && s.rfind("169.254.", 0) != 0) out.push_back(s);
    }
    freeifaddrs(list);
#else
    ensure_init();
    char name[256] = {};
    if (gethostname(name, sizeof name) != 0) return out;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (getaddrinfo(name, nullptr, &hints, &res) != 0) return out;
    for (addrinfo* r = res; r; r = r->ai_next) {
        char buf[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(r->ai_addr)->sin_addr, buf, sizeof buf);
        std::string s = buf;
        if (s.rfind("127.", 0) != 0 && s.rfind("169.254.", 0) != 0) out.push_back(s);
    }
    freeaddrinfo(res);
#endif
    return out;
}

namespace {
std::atomic<double (*)()> g_clock{nullptr}; // set_clock (tests)
}

void set_clock(double (*clock)()) { g_clock.store(clock); }

double now_ms() {
    if (double (*clock)() = g_clock.load(std::memory_order_relaxed)) return clock();
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

} // namespace gt3r::net
