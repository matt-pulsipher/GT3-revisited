// The Discord status during a race (app/presence.h): the kind of run and the course, then the
// player's car, lap and place.

#include "app/presence.h"
#include "app/screen_race_impl.h"
#include "formats/text/charset.h"
#include "net/session.h"

namespace gt3r::race_screen {

namespace {

std::string ordinal(int n) {
    const int t = n % 100;
    const char* suffix = (t >= 11 && t <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    return std::to_string(n) + suffix;
}

} // namespace

bool RaceScreen::presence(App&, PresenceText& t) {
    if (backdrop_) return false; // a menu's background: the menu says
    const text::JisTable* jis = hud_.fonts().jis();
    const std::string course = course_name_.empty() ? "" : text::game_text_for_ui(course_name_, jis);
    const std::string car = car_.name.empty() ? "" : text::game_text_for_ui(car_.name, jis);
    const std::string& mode = launch_.request.mode;
    const bool gt = from_menu_ && mode.rfind("go_", 0) == 0;

    std::string kind;
    bool watching = false;
    if (online_) kind = "Online battle";
    else if (attract_ && !race_preview_) return false; // the title demo: the title menu says
    else if (race_preview_) kind = "GT Mode preview", watching = true;
    else if (logger_mode_) kind = "Analyzer", watching = true;
    else if (theater_ || replaying_) kind = "Watching a replay", watching = true;
    else if (demo_) kind = "Watching a demonstration", watching = true;
    else if (!license_event_.empty()) kind = "License test " + license_title(license_event_);
    else if (two_player_) kind = "2P battle";
    else if (mode == "go_machine_test") kind = "Machine test";
    else if (championship())
        kind = "GT Mode championship, round " + std::to_string(launch_.request.champ_round) + " of " +
               std::to_string(launch_.request.champ_rounds);
    else if (launch_.request.practice) kind = "Quick race";
    else if (solo_free_run()) kind = gt ? "GT Mode free run" : "Free run";
    else if (gt) kind = "GT Mode race";
    else if (mode == "time_trial") kind = "Time trial";
    else if (mode == "single_race") kind = "Arcade race";
    else kind = from_menu_ ? "Arcade" : "Time trial";
    t.details = course.empty() ? kind : kind + " · " + course;

    std::string state = car;
    auto add = [&](const std::string& s) { state += (state.empty() ? "" : " · ") + s; };
    if (!watching && track_) {
        if (finished_) {
            if (field_ && finish_position_ > 0) add("Finished " + ordinal(finish_position_));
            else add("Finished");
        } else if (int lap = lap_count(); lap > 0) {
            add(race_laps_ > 0 ? "Lap " + std::to_string(std::min(lap, race_laps_)) + "/" + std::to_string(race_laps_)
                               : "Lap " + std::to_string(lap));
            if (field_ && field_->opponents() > 0)
                add(ordinal(field_->player_position()) + " of " + std::to_string(field_->opponents() + 1));
        }
    }
    t.state = state;
    if (online_) {
        t.party_size = int(net::online().players().size());
        t.party_max = net::kMaxPlayers;
    }
    return true;
}

} // namespace gt3r::race_screen
