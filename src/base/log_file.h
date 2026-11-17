#pragma once

// The game's log as a file a player can attach to a bug report: gt3r.log in the user data
// folder, this session's, and gt3r.prev.log, the one before (the session that crashed, when
// the player relaunches). Every SDL_Log line goes there as well as to the console or logcat:
// ours (base/log.h) and SDL's own.
//
// What reaches the file:
// - Paths with the player's home folder start as "~", and the account name as a whole folder
//   name anywhere else as "<user>" (LogRedactor), so a log can be posted publicly. The
//   console keeps the real paths.
// - At most kSoftLimit bytes of every level, then warnings and errors only up to kHardLimit,
//   then nothing (LogLimiter); a line repeated straight after itself is counted, not written.
// - Lines are buffered and written out on a warning or an error, once a second, and when the
//   buffer fills; a crash writes what is buffered and why the game closed (crash_note).

#include "base/log.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace gt3r {

// Hides the player's name in paths. `home` is the home folder ("C:\Users\Ana",
// "/home/ana"); `user` the account name (its last part on computers, none on phones). Matching
// ignores ASCII case and takes '/' and '\\' as the same character; the name only counts as
// a whole folder name (a separator before it, a separator or the end of the path after it)
// and only when it has at least 3 characters.
class LogRedactor {
public:
    LogRedactor() = default;
    LogRedactor(std::string home, std::string user = {});
    std::string apply(std::string_view line) const;
    bool empty() const { return home_.empty() && user_.empty(); }

private:
    std::string home_; // without a trailing separator
    std::string user_;
};

// What to do with each line, by level and the bytes written so far, and runs of the same
// line. Pure: the file sink feeds it and writes what it returns.
class LogLimiter {
public:
    static constexpr size_t kSoftLimit = 2u << 20; // all levels until here
    static constexpr size_t kHardLimit = 4u << 20; // then warnings and errors until here

    LogLimiter(size_t soft = kSoftLimit, size_t hard = kHardLimit) : soft_(soft), hard_(hard) {}
    // The text to write for `line` (already formatted, with its newline): "" when it is
    // dropped or repeats the last one; a "(repeated N times)" line before it when a run of
    // repeats ended; a note when a limit is reached.
    std::string feed(LogLevel level, std::string_view text, std::string_view line);
    // A pending repeat count as a line, at the end of the session ("" when none).
    std::string finish();
    size_t written() const { return written_; }

private:
    std::string take(std::string s); // counts s against the limits
    size_t soft_, hard_;
    size_t written_ = 0;
    bool trimmed_ = false, full_ = false;
    std::string last_; // the last line's text (without its prefix)
    size_t repeats_ = 0;
};

namespace log_file {

// Starts the file in `dir` (the user data folder, with a trailing separator): moves the last
// session's gt3r.log to gt3r.prev.log, opens a new one, sends SDL's log output there too and
// installs the crash handlers (platform/crash_handlers.h). Once per process; false (and the
// console only) when the file can't be opened.
bool open(const std::string& dir);
// The session ended normally: writes the pending repeats and "Closed normally", and closes.
void close();
std::string path(); // "" when no file is open

// The player's name hidden in `text` as in the file (for a crash handler's message, before it
// is written).
std::string redact(std::string_view text);

// From a crash handler: writes the buffered lines and `text` (with a newline) straight to the
// file, without locks or allocation. Safe in a signal handler. Only the first call writes: a
// second crash, or abort() after an uncaught error was noted, adds nothing.
void crash_note(const char* text);
// More lines after crash_note (a backtrace), the same way.
void crash_append(const char* text);

// For tests: the sink's formatting of one line ("12.345 WARN  text\n").
std::string format_line(double seconds, LogLevel level, std::string_view text);

} // namespace log_file

} // namespace gt3r
