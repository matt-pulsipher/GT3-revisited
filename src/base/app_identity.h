#pragma once

// The name the engine gives the app where a player or a log reader sees it: the log file's first
// line and file names (base/log_file.h), the text a host sends a player whose game is too old to
// join (net/session.h), the VR runtime's list of apps, the hidden window of the OpenGL ES probe
// and the "generator" of exported models. GT3 Revisited's own by default; other apps built on the
// engine set theirs once, first thing at start (before log_file::open).

#include <string>

namespace gt3r {

struct AppIdentity {
    std::string name;      // "GT3 Revisited"
    std::string version;   // "0.9.0"
    std::string file_stem; // "gt3r": the log is gt3r.log, the session before gt3r.prev.log
};

const AppIdentity& app_identity();
// Empty fields keep the default's.
void set_app_identity(AppIdentity identity);

} // namespace gt3r
