#pragma once

// Discord status (issue 233; README "Discord"): "Playing GT3 Revisited" on the player's Discord
// profile with what they are doing, e.g. "Arcade · Tsukuba Circuit" / "Mazda RX-7 Type RS · Lap 2/5".
//
// Off unless the player turns it on (Settings -> Online, [discord] status = on). The text comes
// from the top screen (Screen::presence; the screen below answers when the top one does not),
// asked about once a second. The elapsed time is the session's: from when the status starts
// (launch, or turning it on) until another disc is opened from the library. The client
// (platform/discord_presence.h) talks only to the Discord app on the same computer. Desktop
// builds only; --shot runs leave Discord alone unless GT3R_DISCORD=1.
//
// [discord] app_id: another Discord application to show as (the default is GT3 Revisited's).

#include "platform/discord_presence.h"

#include <string>

namespace gt3r {

class App;

// What a screen says it is: two short lines (UTF-8) and, online, the session's players.
struct PresenceText {
    std::string details;               // first line
    std::string state;                 // second line ("" none)
    int party_size = 0, party_max = 0; // "(2 of 6)" after the second line (0: none)
};

class Presence {
public:
    void startup(App& app, bool allowed); // `allowed` false: never connects (--shot runs)
    void frame(App& app);                 // every frame: asks the screens about once a second
    void shutdown();                      // clears the status and disconnects

    static bool supported() { return platform::DiscordPresence::supported(); }
    bool enabled(App& app) const; // [discord] status (default off)
    void set_enabled(App& app, bool on);
    std::string status_text() const; // one line for Settings

private:
    void apply(App& app);

    platform::DiscordPresence client_;
    bool allowed_ = false;
    double next_ask_ = 0;     // seconds (SDL ticks)
    std::string disc_;        // the disc the clock started for (DiscInfo::key, "" none)
    long long since_ = 0;     // Unix seconds: the session's start
};

Presence& discord_status(); // the process-wide one

} // namespace gt3r
