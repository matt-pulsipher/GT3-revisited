#include "base/log_file.h"

#include "base/log.h"
#include "gt3r_version.h"
#include "platform/crash_handlers.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_platform.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

#ifdef SDL_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace gt3r {

namespace {

bool is_sep(char c) { return c == '/' || c == '\\'; }
char fold(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }
// A character that can continue a file or folder name: past one, a match isn't a whole name.
bool name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
           c == '-' || (unsigned char)c >= 0x80;
}
bool same(char a, char b) { return (is_sep(a) && is_sep(b)) || fold(a) == fold(b); }
bool matches_at(std::string_view s, size_t i, std::string_view what) {
    if (what.empty() || s.size() - i < what.size()) return false;
    for (size_t k = 0; k < what.size(); ++k)
        if (!same(s[i + k], what[k])) return false;
    return true;
}
bool ends_name(std::string_view s, size_t end) { return end == s.size() || !name_char(s[end]); }

} // namespace

// --- LogRedactor -------------------------------------------------------------

LogRedactor::LogRedactor(std::string home, std::string user) : home_(std::move(home)), user_(std::move(user)) {
    while (!home_.empty() && is_sep(home_.back())) home_.pop_back();
    // "/" or "C:" alone would hide every path.
    if (home_.size() < 3) home_.clear();
    if (user_.size() < 3) user_.clear();
}

std::string LogRedactor::apply(std::string_view s) const {
    if (empty()) return std::string(s);
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (matches_at(s, i, home_) && ends_name(s, i + home_.size())) {
            out += '~';
            i += home_.size();
            continue;
        }
        if (i > 0 && is_sep(s[i - 1]) && matches_at(s, i, user_) && ends_name(s, i + user_.size())) {
            out += "<user>";
            i += user_.size();
            continue;
        }
        out += s[i++];
    }
    return out;
}

// --- LogLimiter --------------------------------------------------------------

std::string LogLimiter::take(std::string s) {
    written_ += s.size();
    return s;
}

std::string LogLimiter::feed(LogLevel level, std::string_view text, std::string_view line) {
    if (full_) return "";
    std::string out;
    if (text == last_ && !last_.empty()) {
        ++repeats_;
        return "";
    }
    if (repeats_) out = finish();
    const bool important = level >= LogLevel::Warn;
    if (written_ + line.size() > hard_) {
        full_ = true;
        return out + take("--- The log is full: nothing more is written this session ---\n");
    }
    if (!important && written_ + line.size() > soft_) {
        if (!trimmed_) {
            trimmed_ = true;
            out += take("--- The log is long: only warnings and errors from here ---\n");
        }
        return out;
    }
    last_ = std::string(text);
    return out + take(std::string(line));
}

std::string LogLimiter::finish() {
    if (!repeats_) return "";
    std::string s = "        (repeated " + std::to_string(repeats_) + (repeats_ == 1 ? " more time)\n" : " more times)\n");
    repeats_ = 0;
    return take(std::move(s));
}

// --- the file ----------------------------------------------------------------

namespace log_file {

namespace {

// The open file, written without stdio so a signal handler can use it.
#ifdef SDL_PLATFORM_WINDOWS
HANDLE g_file = INVALID_HANDLE_VALUE;
bool file_open() { return g_file != INVALID_HANDLE_VALUE; }
void raw_write(const char* p, size_t n) {
    DWORD done = 0;
    if (file_open() && n) WriteFile(g_file, p, DWORD(n), &done, nullptr);
}
bool raw_open(const std::string& path) {
    const int wn = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wn <= 0) return false;
    std::wstring w(size_t(wn), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), wn);
    // Others may read it while the game runs (a text editor, a second copy of the game).
    g_file = CreateFileW(w.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return file_open();
}
void raw_close() {
    if (file_open()) CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;
}
#else
int g_fd = -1;
bool file_open() { return g_fd >= 0; }
void raw_write(const char* p, size_t n) {
    while (file_open() && n) {
        const ssize_t w = ::write(g_fd, p, n);
        if (w <= 0) return;
        p += w;
        n -= size_t(w);
    }
}
bool raw_open(const std::string& path) {
    g_fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    return file_open();
}
void raw_close() {
    if (file_open()) ::close(g_fd);
    g_fd = -1;
}
#endif

// Lines wait here until a warning, an error, a second's time or a full buffer writes them.
char g_buf[16 * 1024];
std::atomic<size_t> g_len{0};
Uint64 g_last_flush = 0;

std::mutex g_mutex;
std::string g_path;
LogRedactor g_redactor;
LogLimiter g_limiter;
SDL_LogOutputFunction g_default = nullptr;
void* g_default_data = nullptr;
std::atomic<bool> g_crashed{false};

void flush_locked() {
    const size_t n = g_len.load();
    raw_write(g_buf, n);
    g_len = 0;
    g_last_flush = SDL_GetTicks();
}

void append_locked(std::string_view s, bool now) {
    if (s.size() > sizeof(g_buf)) { // longer than the buffer: straight through
        flush_locked();
        raw_write(s.data(), s.size());
        return;
    }
    if (g_len.load() + s.size() > sizeof(g_buf)) flush_locked();
    const size_t at = g_len.load();
    std::memcpy(g_buf + at, s.data(), s.size());
    g_len = at + s.size();
    if (now || SDL_GetTicks() - g_last_flush >= 1000) flush_locked();
}

LogLevel level_of(SDL_LogPriority p) {
    switch (p) {
    case SDL_LOG_PRIORITY_TRACE:
    case SDL_LOG_PRIORITY_VERBOSE:
    case SDL_LOG_PRIORITY_DEBUG: return LogLevel::Debug;
    case SDL_LOG_PRIORITY_WARN: return LogLevel::Warn;
    case SDL_LOG_PRIORITY_ERROR:
    case SDL_LOG_PRIORITY_CRITICAL: return LogLevel::Error;
    default: return LogLevel::Info;
    }
}

void SDLCALL output(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    if (g_default) g_default(g_default_data, category, priority, message);
    (void)userdata;
    if (!message || g_crashed) return;
    const LogLevel level = level_of(priority);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!file_open()) return;
    const std::string text = g_redactor.apply(message);
    const std::string line = format_line(double(SDL_GetTicksNS()) / 1e9, level, text);
    const std::string out = g_limiter.feed(level, text, line);
    if (!out.empty()) append_locked(out, level >= LogLevel::Warn);
}

// The home folder and, on computers, the account name (its last part). Phones, tablets and
// headsets keep apps in folders named after the app, not the player.
LogRedactor make_redactor() {
    const char* h = SDL_GetUserFolder(SDL_FOLDER_HOME);
    std::string home = h ? h : "";
    std::string user;
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    std::string trimmed = home;
    while (!trimmed.empty() && is_sep(trimmed.back())) trimmed.pop_back();
    const size_t slash = trimmed.find_last_of("/\\");
    user = slash == std::string::npos ? std::string() : trimmed.substr(slash + 1);
#endif
    return LogRedactor(std::move(home), std::move(user));
}

std::string timestamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef SDL_PLATFORM_WINDOWS
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

} // namespace

std::string format_line(double seconds, LogLevel level, std::string_view text) {
    static const char* const kNames[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
    char head[40];
    std::snprintf(head, sizeof(head), "%9.3f %s ", seconds, kNames[int(level)]);
    std::string line = head;
    line.append(text);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    line += '\n';
    return line;
}

bool open(const std::string& dir) {
    if (dir.empty()) return false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (file_open()) return true;
        const std::string path = dir + "gt3r.log";
        SDL_RenamePath(path.c_str(), (dir + "gt3r.prev.log").c_str()); // none the first time
        if (!raw_open(path)) return false;
        g_path = path;
        g_redactor = make_redactor();
        g_last_flush = SDL_GetTicks();
        SDL_GetLogOutputFunction(&g_default, &g_default_data);
        SDL_SetLogOutputFunction(output, nullptr);
        const std::string head = "GT3 Revisited " + std::string(GT3R_VERSION) + " on " + SDL_GetPlatform() +
                                 ", started " + timestamp() + "\n";
        append_locked(head, true);
    }
    platform::install_crash_handlers();
    return true;
}

void close() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!file_open()) return;
    append_locked(g_limiter.finish(), false);
    append_locked("Closed normally\n", true);
    SDL_SetLogOutputFunction(g_default, g_default_data);
    raw_close();
    g_path.clear();
}

std::string path() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_path;
}

std::string redact(std::string_view text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_redactor.apply(text);
}

void crash_note(const char* text) {
    // Another thread may hold the lock or be part way through a line: the buffer is written as
    // it stands. A second crash (in this handler, or another thread's) writes nothing more.
    if (g_crashed.exchange(true)) return;
    raw_write(g_buf, g_len.load());
    g_len = 0;
    raw_write(text, std::strlen(text));
    raw_write("\n", 1);
}

void crash_append(const char* text) {
    raw_write(text, std::strlen(text));
    raw_write("\n", 1);
}

} // namespace log_file

} // namespace gt3r
