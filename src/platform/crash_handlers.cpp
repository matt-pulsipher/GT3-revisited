#include "platform/crash_handlers.h"

#include "base/log_file.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_platform.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>

#ifdef SDL_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#if defined(SDL_PLATFORM_APPLE) || (defined(SDL_PLATFORM_LINUX) && !defined(SDL_PLATFORM_ANDROID))
#include <execinfo.h>
#define GT3R_HAS_BACKTRACE 1
#endif
#endif

namespace gt3r::platform {

namespace {

// --- uncaught C++ errors -------------------------------------------------------

std::terminate_handler g_old_terminate = nullptr;

[[noreturn]] void on_terminate() {
    std::string what = "Closed by an uncaught error";
    if (std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            what += ": ";
            what += ex.what();
        } catch (...) {
            what += " (not a std::exception)";
        }
    } else {
        what += " (std::terminate with no error)";
    }
    log_file::crash_note(log_file::redact(what).c_str());
    if (g_old_terminate) g_old_terminate();
    std::abort();
}

#ifdef SDL_PLATFORM_WINDOWS

// --- Windows -----------------------------------------------------------------------

LPTOP_LEVEL_EXCEPTION_FILTER g_old_filter = nullptr;

const char* exception_name(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error (the file or drive went away)";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    default: return "exception";
    }
}

// "gt3r.exe+<offset>": the module's file name only (its folder could name the player).
void describe_address(const void* addr, char* out, size_t size) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(addr), &mod) ||
        !mod) {
        std::snprintf(out, size, "%p", addr);
        return;
    }
    wchar_t wpath[MAX_PATH] = {};
    GetModuleFileNameW(mod, wpath, MAX_PATH);
    const wchar_t* base = wpath;
    for (const wchar_t* p = wpath; *p; ++p)
        if (*p == L'\\' || *p == L'/') base = p + 1;
    char name[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, base, -1, name, int(sizeof(name)), nullptr, nullptr);
    std::snprintf(out, size, "%s+0x%llx", name,
                  (unsigned long long)(reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(mod)));
}

LONG WINAPI on_exception(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* r = info ? info->ExceptionRecord : nullptr;
    // An uncaught C++ error: the C runtime's filter (next) calls std::terminate, whose handler
    // notes it with its message.
    if (r && r->ExceptionCode != 0xE06D7363) {
        char where[MAX_PATH * 3 + 32];
        describe_address(r->ExceptionAddress, where, sizeof(where));
        char line[MAX_PATH * 3 + 160];
        if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
            std::snprintf(line, sizeof(line), "Crashed: 0x%08lX %s (%s 0x%llx) at %s", r->ExceptionCode,
                          exception_name(r->ExceptionCode), r->ExceptionInformation[0] == 1 ? "writing" : "reading",
                          (unsigned long long)r->ExceptionInformation[1], where);
        else
            std::snprintf(line, sizeof(line), "Crashed: 0x%08lX %s at %s", r->ExceptionCode,
                          exception_name(r->ExceptionCode), where);
        log_file::crash_note(line);
    }
    return g_old_filter ? g_old_filter(info) : EXCEPTION_CONTINUE_SEARCH; // Windows Error Reporting next
}

void on_abort(int) { log_file::crash_note("Closed by abort()"); }

#else

// --- POSIX signals -------------------------------------------------------------------
// Only async-signal-safe work: fixed buffers, no allocation, no stdio (dladdr and
// backtrace are not on the list but are what every crash reporter uses).

constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
struct sigaction g_old[sizeof(kSignals) / sizeof(kSignals[0])];
char g_alt_stack[64 * 1024]; // a stack overflow still has room to write the note

const char* signal_name(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGBUS: return "SIGBUS (bad memory access)";
    case SIGILL: return "SIGILL (illegal instruction)";
    case SIGFPE: return "SIGFPE (arithmetic error)";
    case SIGABRT: return "SIGABRT (abort)";
    default: return "signal";
    }
}

// Appends to a fixed buffer without stdio.
struct Text {
    char buf[512];
    size_t len = 0;
    Text& add(const char* s) {
        while (*s && len + 1 < sizeof(buf)) buf[len++] = *s++;
        buf[len] = 0;
        return *this;
    }
    Text& hex(uintptr_t v) {
        char tmp[2 + 2 * sizeof(v) + 1];
        int n = 0;
        do {
            tmp[n++] = "0123456789abcdef"[v & 15];
            v >>= 4;
        } while (v);
        add("0x");
        while (n) {
            const char c[2] = {tmp[--n], 0};
            add(c);
        }
        return *this;
    }
};

const char* base_name(const char* path) {
    const char* b = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/') b = p + 1;
    return b;
}

void on_signal(int sig, siginfo_t* info, void*) {
    Text t;
    t.add("Crashed: ").add(signal_name(sig));
    if (sig != SIGABRT && info) t.add(" at address ").hex(reinterpret_cast<uintptr_t>(info->si_addr));
    log_file::crash_note(sig == SIGABRT ? "Closed by abort()" : t.buf);
#ifdef GT3R_HAS_BACKTRACE
    if (sig != SIGABRT) {
        void* frames[48];
        const int n = backtrace(frames, 48);
        for (int i = 0; i < n; ++i) {
            Text f;
            f.add("  #");
            const char d[3] = {char('0' + i / 10), char('0' + i % 10), 0};
            f.add(d).add(" ");
            Dl_info dl{};
            if (dladdr(frames[i], &dl) && dl.dli_fname) {
                // The module's file name only: its folder could name the player.
                f.add(base_name(dl.dli_fname))
                    .add("+")
                    .hex(reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(dl.dli_fbase));
                if (dl.dli_sname) f.add("  ").add(dl.dli_sname);
            } else {
                f.hex(reinterpret_cast<uintptr_t>(frames[i]));
            }
            log_file::crash_append(f.buf);
        }
    }
#endif
    // The handler from before (Android's tombstones) or the default: the process ends as it
    // would have.
    for (size_t i = 0; i < sizeof(kSignals) / sizeof(kSignals[0]); ++i)
        if (kSignals[i] == sig) sigaction(sig, &g_old[i], nullptr);
    raise(sig);
}

#endif

} // namespace

void install_crash_handlers() {
    static bool done = false;
    if (done) return;
    done = true;
    g_old_terminate = std::set_terminate(on_terminate);
#ifdef SDL_PLATFORM_WINDOWS
    g_old_filter = SetUnhandledExceptionFilter(on_exception);
    std::signal(SIGABRT, on_abort);
#else
    stack_t ss{};
    ss.ss_sp = g_alt_stack;
    ss.ss_size = sizeof(g_alt_stack);
    sigaltstack(&ss, nullptr); // this thread's (the main one); other threads use their own stacks
#ifdef GT3R_HAS_BACKTRACE
    void* warm[1];
    backtrace(warm, 1); // its first call loads the unwinder, which a signal handler must not
#endif
    for (size_t i = 0; i < sizeof(kSignals) / sizeof(kSignals[0]); ++i) {
        struct sigaction sa{};
        sa.sa_sigaction = on_signal;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        sigaction(kSignals[i], &sa, &g_old[i]);
    }
#endif
}

void crash_test(const char* kind) {
    const std::string k = kind ? kind : "";
    if (k == "throw") {
        const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME);
        throw std::runtime_error(std::string("GT3R_CRASH_TEST: could not read ") + (home ? home : "") + "test.bin");
    }
    if (k == "segv") {
        volatile int* p = nullptr;
        *p = 1;
    }
    if (k == "abort") std::abort();
}

} // namespace gt3r::platform
