#pragma once

// Why the game closed, in the log file (base/log_file.h), when it didn't close normally:
// - an uncaught C++ error (std::terminate): its message. On Windows such an error closes the
//   game with 0xC0000409 and no other trace;
// - a crash: Windows' exception code and the module and offset it happened at; elsewhere the
//   signal and, on macOS and Linux, a backtrace of module + offset lines;
// - abort().
// The system's own handling carries on afterwards: Windows Error Reporting, Android's
// tombstones, macOS's crash reports.

namespace gt3r::platform {

// Once, from log_file::open.
void install_crash_handlers();

// GT3R_CRASH_TEST=throw|segv|abort: closes the game as a bug would (an uncaught error naming a
// path in the home folder, an invalid memory access, abort()), to check what the log says.
// Anything else does nothing.
void crash_test(const char* kind);

} // namespace gt3r::platform
