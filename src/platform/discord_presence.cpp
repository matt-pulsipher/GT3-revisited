#include "platform/discord_presence.h"

#include "base/log.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(_WIN32)
#define GT3R_DISCORD 1
#elif defined(__APPLE__) && !TARGET_OS_IPHONE
#define GT3R_DISCORD 1
#elif defined(__linux__) && !defined(__ANDROID__)
#define GT3R_DISCORD 1
#else
#define GT3R_DISCORD 0
#endif

#if GT3R_DISCORD
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#endif

namespace gt3r::platform {

namespace discord {

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += ch;
            }
        }
    }
    return out;
}

std::string utf8_cut(std::string_view s, size_t max) {
    if (s.size() <= max) return std::string(s);
    size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n; // s[n] continues a character
    return std::string(s.substr(0, n));
}

std::string handshake_json(std::string_view app_id) {
    return "{\"v\":1,\"client_id\":\"" + json_escape(app_id) + "\"}";
}

namespace {

constexpr size_t kMinText = 2, kMaxText = 128;

// ,"key":"value" when the value is long enough for Discord.
void text_field(std::string& out, bool& first, const char* key, const std::string& value) {
    std::string v = utf8_cut(value, kMaxText);
    if (v.size() < kMinText) return;
    if (!first) out += ',';
    first = false;
    out += '"';
    out += key;
    out += "\":\"";
    out += json_escape(v);
    out += '"';
}

} // namespace

std::string activity_json(const std::optional<DiscordActivity>& a, long long pid, unsigned nonce) {
    std::string out = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) + ",\"activity\":";
    if (!a) {
        out += "null";
    } else {
        out += '{';
        bool first = true;
        text_field(out, first, "details", a->details);
        text_field(out, first, "state", a->state);
        if (a->start > 0) {
            if (!first) out += ',';
            first = false;
            out += "\"timestamps\":{\"start\":" + std::to_string(a->start) + "}";
        }
        if (!a->large_image.empty()) {
            if (!first) out += ',';
            first = false;
            out += "\"assets\":{";
            bool f2 = true;
            text_field(out, f2, "large_image", a->large_image);
            text_field(out, f2, "large_text", a->large_text);
            out += '}';
        }
        if (a->party_size > 0 && a->party_max >= a->party_size) {
            if (!first) out += ',';
            first = false;
            out += "\"party\":{\"id\":\"gt3r-" + std::to_string(pid) + "\",\"size\":[" + std::to_string(a->party_size) +
                   "," + std::to_string(a->party_max) + "]}";
        }
        if (!first) out += ',';
        out += "\"instance\":false}";
    }
    out += "},\"nonce\":\"" + std::to_string(nonce) + "\"}";
    return out;
}

std::vector<u8> frame(Op op, std::string_view json) {
    std::vector<u8> out(8 + json.size());
    u32 o = static_cast<u32>(op), n = static_cast<u32>(json.size());
    for (int i = 0; i < 4; ++i) {
        out[i] = static_cast<u8>(o >> (8 * i));
        out[4 + i] = static_cast<u8>(n >> (8 * i));
    }
    std::copy(json.begin(), json.end(), out.begin() + 8);
    return out;
}

long long frame_size(const u8* buf, size_t n) {
    if (n < 8) return 0;
    u32 len = u32(buf[4]) | u32(buf[5]) << 8 | u32(buf[6]) << 16 | u32(buf[7]) << 24;
    if (len > 64 * 1024) return -1;
    return n >= 8 + size_t(len) ? 8 + (long long)len : 0;
}

std::vector<std::string> socket_paths(const std::function<const char*(const char*)>& env) {
    std::vector<std::string> dirs;
    for (const char* var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        const char* v = env ? env(var) : nullptr;
        if (v && *v) dirs.emplace_back(v);
    }
    dirs.emplace_back("/tmp");
    std::vector<std::string> unique;
    for (std::string d : dirs) {
        while (d.size() > 1 && d.back() == '/') d.pop_back();
        if (std::find(unique.begin(), unique.end(), d) == unique.end()) unique.push_back(d);
    }
    // Discord's own folder first, then its Flatpak and Snap packages.
    static const char* const kSub[] = {"", "/app/com.discordapp.Discord", "/snap.discord"};
    std::vector<std::string> out;
    for (const char* sub : kSub)
        for (const std::string& d : unique)
            for (int i = 0; i < 10; ++i) out.push_back(d + sub + "/discord-ipc-" + std::to_string(i));
    return out;
}

namespace {

// The string value of "key" in a flat JSON object (no unescaping beyond \" and \\).
std::string string_value(std::string_view json, std::string_view key) {
    std::string pat = "\"" + std::string(key) + "\"";
    size_t at = json.find(pat);
    if (at == std::string_view::npos) return "";
    at = json.find(':', at + pat.size());
    if (at == std::string_view::npos) return "";
    at = json.find('"', at);
    if (at == std::string_view::npos) return "";
    std::string out;
    for (size_t i = at + 1; i < json.size(); ++i) {
        char c = json[i];
        if (c == '"') return out;
        if (c == '\\' && i + 1 < json.size()) c = json[++i];
        out += c;
    }
    return "";
}

} // namespace

bool is_ready(std::string_view json) { return string_value(json, "evt") == "READY"; }

std::string error_message(std::string_view json) { return string_value(json, "message"); }

} // namespace discord

// --- the client ----------------------------------------------------------------------------

struct DiscordPresence::Shared {
    std::mutex mu;
    std::condition_variable cv;
    bool quit = false;
    bool dirty = false; // `want` not sent yet
    std::optional<DiscordActivity> want;
    bool connected = false;
    std::string status = "Starting";
    std::thread thread;
};

#if GT3R_DISCORD

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kRetry = std::chrono::seconds(15);
constexpr auto kMinInterval = std::chrono::seconds(5);

long long this_pid() {
#if defined(_WIN32)
    return (long long)GetCurrentProcessId();
#else
    return (long long)getpid();
#endif
}

// One connection to the Discord app.
class Pipe {
public:
    ~Pipe() { close(); }

    bool open() {
#if defined(_WIN32)
        for (int i = 0; i < 10; ++i) {
            std::string name = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
            HANDLE h = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                h_ = h;
                return true;
            }
        }
        return false;
#else
        for (const std::string& path : discord::socket_paths([](const char* v) { return std::getenv(v); })) {
            if (path.size() >= sizeof(sockaddr_un{}.sun_path)) continue;
            int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd < 0) return false;
#if defined(__APPLE__)
            int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
            fcntl(fd, F_SETFD, FD_CLOEXEC);
            sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            std::copy(path.begin(), path.end(), addr.sun_path);
            if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0) {
                fd_ = fd;
                return true;
            }
            ::close(fd);
        }
        return false;
#endif
    }

    void close() {
#if defined(_WIN32)
        if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
        h_ = INVALID_HANDLE_VALUE;
#else
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
#endif
        buf_.clear();
    }

    bool write(discord::Op op, std::string_view json) {
        std::vector<u8> f = discord::frame(op, json);
#if defined(_WIN32)
        DWORD done = 0;
        return WriteFile(h_, f.data(), DWORD(f.size()), &done, nullptr) && done == f.size();
#else
        size_t off = 0;
        while (off < f.size()) {
#if defined(MSG_NOSIGNAL)
            ssize_t n = ::send(fd_, f.data() + off, f.size() - off, MSG_NOSIGNAL);
#else
            ssize_t n = ::send(fd_, f.data() + off, f.size() - off, 0);
#endif
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return false;
            off += size_t(n);
        }
        return true;
#endif
    }

    // Waits up to `ms` for a whole frame. 1: a frame in op/json; 0: none yet; -1: closed or broken.
    int read(int ms, discord::Op& op, std::string& json) {
        auto until = Clock::now() + std::chrono::milliseconds(ms);
        for (;;) {
            long long size = discord::frame_size(buf_.data(), buf_.size());
            if (size < 0) return -1;
            if (size > 0) {
                op = discord::Op(u32(buf_[0]) | u32(buf_[1]) << 8 | u32(buf_[2]) << 16 | u32(buf_[3]) << 24);
                json.assign(reinterpret_cast<const char*>(buf_.data()) + 8, size_t(size - 8));
                buf_.erase(buf_.begin(), buf_.begin() + size);
                return 1;
            }
            int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count());
            if (left <= 0) return 0;
            u8 tmp[4096];
#if defined(_WIN32)
            DWORD avail = 0;
            if (!PeekNamedPipe(h_, nullptr, 0, nullptr, &avail, nullptr)) return -1;
            if (avail == 0) {
                Sleep(DWORD(std::min(left, 20)));
                continue;
            }
            DWORD got = 0;
            if (!ReadFile(h_, tmp, std::min<DWORD>(avail, sizeof tmp), &got, nullptr) || got == 0) return -1;
            buf_.insert(buf_.end(), tmp, tmp + got);
#else
            pollfd p{fd_, POLLIN, 0};
            int r = ::poll(&p, 1, left);
            if (r < 0 && errno == EINTR) continue;
            if (r < 0) return -1;
            if (r == 0) return 0;
            ssize_t got = ::recv(fd_, tmp, sizeof tmp, 0);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) return -1;
            buf_.insert(buf_.end(), tmp, tmp + got);
#endif
        }
    }

private:
#if defined(_WIN32)
    HANDLE h_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
    std::vector<u8> buf_;
};

void set_status(DiscordPresence::Shared& s, bool connected, const char* text) {
    std::lock_guard<std::mutex> lock(s.mu);
    s.connected = connected;
    s.status = text;
}

void run(std::shared_ptr<DiscordPresence::Shared> s, std::string app_id) {
    Pipe pipe;
    bool connected = false;
    Clock::time_point next_try{}, last_send{};
    bool sent_any = false;
    unsigned nonce = 0;
    const long long pid = this_pid();
    bool logged_missing = false;

    auto hang_up = [&](const char* why) {
        pipe.close();
        connected = false;
        next_try = Clock::now() + kRetry;
        set_status(*s, false, why);
        std::lock_guard<std::mutex> lock(s->mu);
        s->dirty = true; // the next connection sends it again
    };

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(s->mu);
            if (s->quit) break;
            if (!connected) s->cv.wait_until(lock, std::max(next_try, Clock::now()), [&] { return s->quit; });
            if (s->quit) break;
        }
        if (!connected) {
            if (!pipe.open()) {
                if (!logged_missing) LOG_INFO("Discord: no Discord app found; trying again every 15 s");
                logged_missing = true;
                next_try = Clock::now() + kRetry;
                set_status(*s, false, "Discord is not running");
                continue;
            }
            discord::Op op{};
            std::string json;
            int r = -1;
            if (pipe.write(discord::Op::Handshake, discord::handshake_json(app_id)))
                while ((r = pipe.read(3000, op, json)) == 1 && op == discord::Op::Ping) pipe.write(discord::Op::Pong, json);
            if (r != 1 || op != discord::Op::Frame || !discord::is_ready(json)) {
                std::string msg = r == 1 ? discord::error_message(json) : "";
                LOG_WARN("Discord: handshake refused%s%s", msg.empty() ? "" : ": ", msg.c_str());
                hang_up(msg.empty() ? "Discord did not answer" : "Discord refused the connection");
                continue;
            }
            LOG_INFO("Discord: connected");
            logged_missing = false;
            connected = true;
            sent_any = false;
            set_status(*s, true, "Connected");
        }

        // Send the latest activity when it changed and the rate allows.
        std::optional<std::optional<DiscordActivity>> send;
        {
            std::lock_guard<std::mutex> lock(s->mu);
            if (s->dirty && (!sent_any || Clock::now() - last_send >= kMinInterval)) {
                send = s->want;
                s->dirty = false;
            }
        }
        if (send) {
            if (!pipe.write(discord::Op::Frame, discord::activity_json(*send, pid, ++nonce))) {
                hang_up("Disconnected");
                continue;
            }
            last_send = Clock::now();
            sent_any = true;
        }

        discord::Op op{};
        std::string json;
        int r = pipe.read(250, op, json);
        if (r < 0) {
            LOG_INFO("Discord: disconnected");
            hang_up("Disconnected");
            continue;
        }
        if (r == 1) {
            if (op == discord::Op::Ping) {
                pipe.write(discord::Op::Pong, json);
            } else if (op == discord::Op::Close) {
                LOG_INFO("Discord: closed (%s)", discord::error_message(json).c_str());
                hang_up("Disconnected");
            } else if (op == discord::Op::Frame && json.find("\"evt\":\"ERROR\"") != std::string::npos) {
                LOG_WARN("Discord: %s", discord::error_message(json).c_str());
            }
        }
    }

    if (connected) { // leave the profile as it was
        pipe.write(discord::Op::Frame, discord::activity_json(std::nullopt, pid, ++nonce));
        discord::Op op{};
        std::string json;
        pipe.read(200, op, json);
    }
}

} // namespace

bool DiscordPresence::supported() { return true; }

#else

bool DiscordPresence::supported() { return false; }

#endif

DiscordPresence::DiscordPresence() = default;

DiscordPresence::~DiscordPresence() { stop(); }

void DiscordPresence::start(const std::string& app_id) {
    if (s_ && app_id == app_id_) return;
    stop();
#if GT3R_DISCORD
    if (app_id.empty()) return;
    app_id_ = app_id;
    s_ = std::make_shared<Shared>();
    s_->thread = std::thread(run, s_, app_id);
#endif
}

void DiscordPresence::stop() {
    if (!s_) return;
    {
        std::lock_guard<std::mutex> lock(s_->mu);
        s_->quit = true;
    }
    s_->cv.notify_all();
    if (s_->thread.joinable()) s_->thread.join();
    s_.reset();
    app_id_.clear();
}

bool DiscordPresence::running() const { return s_ != nullptr; }

void DiscordPresence::set(const std::optional<DiscordActivity>& a) {
    if (!s_) return;
    std::lock_guard<std::mutex> lock(s_->mu);
    if (s_->want == a) return;
    s_->want = a;
    s_->dirty = true;
}

bool DiscordPresence::connected() const {
    if (!s_) return false;
    std::lock_guard<std::mutex> lock(s_->mu);
    return s_->connected;
}

std::string DiscordPresence::status() const {
    if (!s_) return "Off";
    std::lock_guard<std::mutex> lock(s_->mu);
    return s_->status;
}

} // namespace gt3r::platform
