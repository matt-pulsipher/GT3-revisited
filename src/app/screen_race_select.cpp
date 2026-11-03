// The --screen race selection page (Stage::Select): the time-trial course / car picker
// and the saved-replay list.
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"

namespace gt3r::race_screen {

void RaceScreen::update_select(App& app) {
    const MenuInput& m = app.menu();
    // The VR pointer: a hover selects the row under it; a click on a replay watches it.
    const XrList::Hit hover = xr_list_.take_hover(), click = xr_list_.take_click();
    if (show_replays_) {
        int n = int(replay_files_.size());
        if (n) replay_sel_ = std::clamp(replay_sel_ + m.vertical(), 0, n - 1);
        for (const XrList::Hit& h : {hover, click})
            if (h.row >= 0 && h.row < n) replay_sel_ = h.row;
        if (m.pressed(PadButton::Circle) || m.pressed(PadButton::Triangle)) show_replays_ = false;
        else if (n && (m.pressed(PadButton::Cross) || (click.row >= 0 && click.row < n)))
            load_replay_file(app, replay_files_[size_t(replay_sel_)]);
        return;
    }
    if (m.pressed(PadButton::Triangle)) {
        app.pop();
        return;
    }
    if (courses_.empty() || cars_.empty()) return;
    for (const XrList::Hit& p : {hover, click}) {
        if (p.list == 0 && p.row >= 0 && p.row < int(courses_.size())) column_ = 0, course_sel_ = p.row;
        if (p.list == 1 && p.row >= 0 && p.row < int(cars_.size())) column_ = 1, car_sel_ = p.row;
    }
    int h = m.horizontal();
    if (h) column_ = std::clamp(column_ + h, 0, 1);
    int& sel = column_ == 0 ? course_sel_ : car_sel_;
    int n = column_ == 0 ? int(courses_.size()) : int(cars_.size());
    sel = std::clamp(sel + m.vertical(), 0, n - 1);
    if (m.pressed(PadButton::Square)) controls_.config().automatic = !controls_.config().automatic;
    if (m.pressed(PadButton::Circle)) {
        replay_files_ = list_replays(replay_dir(app));
        replay_sel_ = 0;
        show_replays_ = true;
    }
    if (m.pressed(PadButton::Cross) || m.pressed(PadButton::Start)) start(app);
}

bool RaceScreen::select_entries(const std::string& course, const std::string& car) {
    int ci = -1, ki = -1;
    for (size_t i = 0; i < courses_.size(); ++i)
        if (courses_[i].label == course) ci = int(i);
    for (size_t i = 0; i < cars_.size(); ++i)
        if (cars_[i].label == car) ki = int(i);
    if (ci < 0 || ki < 0) return false;
    course_sel_ = ci, car_sel_ = ki;
    return true;
}

void RaceScreen::load_replay_file(App& app, const ReplayFile& f) {
    try {
        std::optional<Bytes> b = read_host_file(f.path);
        if (!b) throw FormatError("cannot read " + f.path);
        Replay r = Replay::parse(*b);
        if (!select_entries(r.course, r.car)) throw FormatError("unknown course or car: " + r.course + " / " + r.car);
        preset_.paint = r.paint;
        show_replays_ = false;
        start(app, &r);
    } catch (const std::exception& e) {
        error_ = e.what();
        show_replays_ = false;
    }
}

void RaceScreen::render_select(App&, Ui& ui) {
    ui.header("TIME TRIAL", show_replays_ ? "saved replays" : "choose a course and a car");
    constexpr float kTop = 34, kRow = 12;
    int rows = int((Ui::kHeight - 40 - kTop) / kRow);
    auto list = [&](int id, float x, float w, const char* title, int count, int sel, bool active, auto name) {
        ui.text(x, kTop - 12, title, active ? colors::kAccent : colors::kDim);
        int first = xr_list_.first(id, sel, rows, ui.list_window(count, sel, rows));
        xr_list_.rows(id, first, std::min(rows, count - first), x - 4, x - 4 + w, kTop, kRow);
        for (int i = 0; i < rows && first + i < count; ++i) {
            int idx = first + i;
            float y = kTop + 2 + i * kRow;
            if (idx == sel) ui.fill(x - 4, y - 2, w, kRow, active ? colors::kHighlight : Color{40, 44, 54, 255});
            std::string s = name(idx);
            ui.text(x, y, Ui::clip(s, w - 8), idx == sel ? colors::kText : colors::kDim);
        }
    };
    if (show_replays_) {
        list(0, 12, 616, "REPLAY", int(replay_files_.size()), replay_sel_, true,
             [&](int i) { return replay_files_[size_t(i)].name; });
        if (replay_files_.empty()) ui.text(12, kTop + 2, "No saved replays (Pause > Save replay while driving).", colors::kDim);
        ui.footer("Up/Down: select  Cross: watch  Circle: back");
        return;
    }
    list(0, 12, 236, "COURSE", int(courses_.size()), course_sel_, column_ == 0,
         [&](int i) { return courses_[size_t(i)].name; });
    list(1, 262, 370, "CAR", int(cars_.size()), car_sel_, column_ == 1, [&](int i) { return cars_[size_t(i)].name; });
    if (!error_.empty()) ui.text(12, Ui::kHeight - 30, error_.substr(0, 76), colors::kBad);
    std::string gb = std::string("Gearbox: ") + (controls_.config().automatic ? "automatic" : "manual");
    ui.text_right(Ui::kWidth - 12, Ui::kHeight - 30, gb, colors::kDim);
    ui.footer("Up/Down/Left/Right: select  []: AT/MT  O: replays  X: drive  Tri: back");
}

} // namespace gt3r::race_screen
