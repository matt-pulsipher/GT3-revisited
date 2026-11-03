#pragma once

// The update check and the update dialog (docs/dev/ONLINE.md "Versions and updates"; README
// "Online").
//
// The check: at startup unless [online] update_check = off, and from Settings. One small message
// to the relay server ([online] server) with this game's version and platform; the answer is the
// latest release, the oldest version that may play online (min_online) and the release URL. The
// last answer is kept in config.ini ([online] update_latest, update_min_online, update_url,
// update_checked), so a start without a network still knows. --shot runs (screenshots, tests)
// only use that cache unless GT3R_UPDATE_CHECK=1: scripted runs never reach a live server.
//
// The dialog (drawn over any screen by App; pad, keyboard, mouse and touch):
// - "Update available" when a newer release exists, "Update required to play online" when this
//   game is below min_online (no relay sessions; offline and direct-IP play are never blocked).
// - Download update (the platform's package: net::update_links), Later, and Release notes.
// - At startup it shows once per release, over the main menu or the title menu: Later remembers
//   the release ([online] update_dismissed). The online lobby opens it whenever this game is
//   too old for the relay; the main menu's title bar badge and Settings -> Online open it too.

#include "app/ui.h"
#include "net/rendezvous.h"
#include "net/update_check.h"

#include <SDL3/SDL.h>

#include <string>

namespace gt3r {

class App;
namespace net {
struct UpdateRequired;
}

class Updates {
public:
    enum class State { Idle, Checking, Done, Failed };

    // Reads the last answer; checks when [online] update_check is on and `network` allows it.
    void startup(App& app, bool network = true);
    void check_now(App& app); // a manual check (even with the startup check off)
    void poll(App& app);      // takes a finished answer (every frame)

    bool enabled(App& app) const; // [online] update_check (default on)
    void set_enabled(App& app, bool on);

    bool known() const { return latest_.known() || min_online_.known(); } // an answer, now or cached
    bool available() const;
    bool required() const;
    net::UpdatePrompt prompt() const; // the dialog that applies now (None: up to date / unknown)
    const net::GameVersion& latest() const { return latest_; }
    const net::GameVersion& min_online() const { return min_online_; }
    const std::string& url() const { return url_; } // the server's URL (may hold {asset})
    net::UpdateLinks links() const;                  // this platform's download and the release page
    const std::string& checked() const { return checked_; } // "2026-10-18 14:05 UTC" ("" never)
    State state() const { return state_; }
    const std::string& error() const { return error_; } // State::Failed: why
    std::string status_text() const;                    // one line for Settings
    bool open_download() const;                         // the system browser (false: no URL known)
    bool open_notes() const;

    // A relay server refused a session (Error UpdateRequired): remembered like an answer.
    void note_required(App& app, const net::UpdateRequired& u);

    // --- the dialog ---
    void open_dialog();                       // the one that applies now (nothing when up to date)
    void open_dialog(net::UpdatePrompt kind); // Required or Available
    bool dialog_open() const { return dialog_.open; }
    // While true, App gives the dialog the menu input instead of the screen (also until the
    // button that closed it is released, so the screen below does not take that press).
    bool modal() const { return dialog_.open || dialog_.wait_release; }
    // Every frame before the screens update: shows the startup prompt when one is due and the
    // top screen allows it (Screen::update_prompts).
    void frame(App& app, bool screen_allows);
    void dialog_update(App& app);                       // menu input (when modal())
    void dialog_event(App& app, const SDL_Event& e);    // pointer input (render coordinates)
    bool dialog_clickable(float x, float y) const;      // Screen::clickable for the dialog
    void dialog_render(Ui& ui);
    std::string badge_text() const; // the main menu's title bar badge ("" none)

private:
    void start(App& app);
    void store(App& app);
    void update_due();
    void close_dialog(App& app, bool later);
    void activate(App& app, int item);

    net::VersionCheck check_;
    net::GameVersion latest_, min_online_, dismissed_, shown_;
    std::string url_, checked_, error_;
    State state_ = State::Idle;
    bool startup_due_ = false;
    struct Dialog {
        bool open = false, wait_release = false;
        net::UpdatePrompt kind = net::UpdatePrompt::None;
        int focus = 0;   // 0 Download update, 1 Later, 2 Release notes
        int pressed = -1;
        UiRect items[3]; // last frame's, for pointers
        Gesture gesture;
    } dialog_;
};

Updates& updates(); // the process-wide one

} // namespace gt3r
