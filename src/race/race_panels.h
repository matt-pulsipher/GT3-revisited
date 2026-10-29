#pragma once

// Quick-menu panels the race screen opens from the icon menu (docs/dev/RACE.md
// "Quick menu"):
//
// - LapLogger ("data_logger"): speed, throttle ("accel"), brake and position against the
//   distance round the lap for the last lap (A) and the best lap (B); the Analyzer
//   (race/analyzer.h, logger1.pmb) shows them.
// - Records ("record"): best laps on the course from the saved replays, on the
//   records page of menu/<LANG>/records1.pmb (page "records_free_tt").
// - OptionList: a plain list panel (options / vehicle settings).
//
// Panels draw the 640x448 menu frame onto a 4:3 rectangle (`dst`).

#include "menu/menu_renderer.h"
#include "menu/menu_scene.h"
#include "text/game_font.h"

#include <SDL3/SDL_render.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gt3r {
class Vfs;
} // namespace gt3r

namespace gt3r::race {

// One lap's samples (one per simulation tick), distance in metres from the line.
struct LapTrace {
    std::vector<float> dist, speed_kmh, throttle, brake;
    std::vector<float> x, z; // world position (the Analyzer's map)
    double time = -1;
    bool empty() const { return dist.empty(); }
    size_t size() const { return dist.size(); }
};

class LapLogger {
public:
    void reset() { cur_ = last_ = best_ = {}; }
    void begin_lap() { cur_ = {}; }
    void sample(float dist_m, float speed_kmh, float throttle, float brake, float x = 0, float z = 0);
    // The lap being sampled is complete: it becomes the last lap (and the best one if faster).
    void finish_lap(double time);
    const LapTrace& last() const { return last_; }
    const LapTrace& best() const { return best_; }

private:
    LapTrace cur_, last_, best_;
};

struct RecordEntry {
    std::string car;  // database label
    double lap = -1;  // best lap, seconds
    std::string date; // "YYYY/MM/DD" from the replay's file name, may be empty
};

// "<course>_<car>_YYYYMMDD-HHMMSS.gt3replay" -> "YYYY/MM/DD" ("" if the name has no stamp).
std::string record_date(const std::string& file_name);
// Best laps on `course` from the saved replays in `dir` (at most `max_files` read, newest first),
// fastest first, at most `max_rows`.
std::vector<RecordEntry> course_records(const std::string& dir, const std::string& course, size_t max_rows = 12,
                                        size_t max_files = 40);

class RecordsPanel {
public:
    bool load(const Vfs& vfs, SDL_Renderer* r, text::GameFonts& fonts, const std::string& lang = "US");
    // `names` = display names for rows[i].car (game text); `course_name` UTF-8.
    void draw(SDL_Renderer* r, text::GameFonts& fonts, const std::vector<RecordEntry>& rows,
              const std::vector<std::string>& names, const std::string& course_name, const SDL_FRect& dst);

private:
    std::optional<menu::MenuScene> scene_;
    std::unique_ptr<menu::MenuRenderer> renderer_;
    int page_ = -1;
};

struct OptionRow {
    std::string label, value;
};

// A titled list with the selected row highlighted; `help` at the bottom.
void draw_option_list(SDL_Renderer* r, text::GameFonts& fonts, const std::string& title,
                      const std::vector<OptionRow>& rows, int selected, const std::string& help, const SDL_FRect& dst);

} // namespace gt3r::race
