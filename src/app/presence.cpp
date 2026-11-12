#include "app/presence.h"

#include "app/app.h"
#include "base/config.h"
#include "net/session.h"

#include <SDL3/SDL.h>

#include <ctime>

namespace gt3r {

namespace {

// GT3 Revisited's Discord application (owned by the project's Discord team): its name is the
// "Playing ..." line, and its "logo" art asset the picture.
constexpr const char* kAppId = "1570301497529344040";
constexpr const char* kLogo = "logo";
constexpr double kAskEvery = 1.0; // seconds

std::string app_id(App& app) {
    std::string id = app.config().get("discord", "app_id").value_or("");
    return id.empty() ? kAppId : id;
}

} // namespace

Presence& discord_status() {
    static Presence p;
    return p;
}

bool Presence::enabled(App& app) const {
    return to_lower(app.config().get("discord", "status").value_or("off")) == "on";
}

void Presence::set_enabled(App& app, bool on) {
    app.config().set("discord", "status", on ? "on" : "off");
    app.save_config();
    apply(app);
}

void Presence::startup(App& app, bool allowed) {
    allowed_ = allowed && supported();
    apply(app);
}

void Presence::apply(App& app) {
    if (allowed_ && enabled(app)) {
        if (!client_.running()) { // the session's clock starts
            since_ = (long long)std::time(nullptr);
            disc_ = app.disc() ? app.disc()->info().key : "";
        }
        client_.start(app_id(app));
        next_ask_ = 0; // say what we are doing at once
    } else {
        client_.stop();
    }
}

void Presence::shutdown() { client_.stop(); }

void Presence::frame(App& app) {
    if (!client_.running()) return;
    const double now = double(SDL_GetTicks()) / 1000.0;
    if (now < next_ask_) return;
    next_ask_ = now + kAskEvery;

    PresenceText t;
    if (!app.screen_presence(t) || t.details.empty()) t = {"In the menus", "", 0, 0};
    // The clock is the session's, as for a game Discord finds itself: it starts again only with
    // another disc (from the library), the nearest thing to starting another game.
    const std::string disc = app.disc() ? app.disc()->info().key : "";
    if (disc != disc_) {
        disc_ = disc;
        since_ = (long long)std::time(nullptr);
    }
    platform::DiscordActivity a;
    a.details = t.details;
    a.state = t.state;
    a.start = since_;
    a.large_image = kLogo;
    a.large_text = "GT3 Revisited " + net::this_game_version().to_string();
    a.party_size = t.party_size;
    a.party_max = t.party_max;
    client_.set(a);
}

std::string Presence::status_text() const {
    if (!supported()) return "Not on this device";
    if (!client_.running()) return "Off";
    return client_.status();
}

} // namespace gt3r
