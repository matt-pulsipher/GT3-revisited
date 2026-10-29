// Movie player: lists stream/*.pss and plays them (MPEG-2 video + PS-ADPCM
// audio) streamed from the disc through the VFS.
//
// GT3R_SKIP_MOVIES=1 makes game-flow movies (make_movie_screen) end at once.
// For non-interactive captures, GT3R_MOVIE=<vol path> starts that movie
// immediately and GT3R_MOVIE_START=<seconds> skips into it, e.g.
//   GT3R_MOVIE=stream/opening.pss GT3R_MOVIE_START=60 gt3r <iso> --screen movies --shot out.png

#include "app/app.h"
#include "app/screen_movies.h"
#include "app/screen_registry.h"
#include "base/log.h"
#include "video/ipu.h"
#include "video/movie_player.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace gt3r {

namespace {

// "r_deepforest_full.pss" -> "Deepforest (reverse)": a readable list entry.
std::string movie_title(std::string_view file) {
    std::string n(file.substr(0, file.rfind('.')));
    if (n.size() > 5 && n.compare(n.size() - 5, 5, "_full") == 0) n.resize(n.size() - 5);
    bool reverse = n.rfind("r_", 0) == 0;
    if (reverse) n.erase(0, 2);
    bool start = true;
    for (char& c : n) {
        if (c == '_') c = ' ';
        if (start && c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
        start = c == ' ';
    }
    return reverse ? n + " (reverse)" : n;
}

} // namespace

namespace {

class MoviesScreen : public Screen {
public:
    void update(App& app, double) override {
        if (!loaded_) load(app);
        const MenuInput& m = app.menu();
        if (playing_ >= 0) {
            player_.update(app.renderer());
            if (m.pressed(PadButton::Cross) || m.pressed(PadButton::Start)) player_.set_paused(!player_.paused());
            if (m.pressed(PadButton::Square)) overlay_ = !overlay_;
            if (m.horizontal()) {
                volume_ = std::clamp(volume_ + 0.05f * m.horizontal(), 0.0f, 1.0f);
                player_.set_volume(volume_);
            }
            int n = int(files_.size());
            if (m.pressed(PadButton::R1)) play(app, (playing_ + 1) % n);
            else if (m.pressed(PadButton::L1)) play(app, (playing_ + n - 1) % n);
            else if (m.pressed(PadButton::Circle) || m.pressed(PadButton::Triangle)) stop();
            else if (player_.finished()) stop();
            return;
        }
        if (m.pressed(PadButton::Square)) gallery_ = !gallery_;
        if (gallery_) {
            int n = int(previews_.entries().size());
            if (n) still_sel_ = std::clamp(still_sel_ + m.vertical(), 0, n - 1);
            if (m.pressed(PadButton::Triangle)) gallery_ = false;
            return;
        }
        int n = int(files_.size());
        if (n) sel_ = std::clamp(sel_ + m.vertical(), 0, n - 1);
        if (m.pressed(PadButton::Cross) && n) play(app, sel_);
        if (m.pressed(PadButton::Triangle)) app.pop();
    }

    ~MoviesScreen() override {
        stop();
        if (still_tex_) SDL_DestroyTexture(still_tex_);
    }

    void render(App&, Ui& ui) override {
        if (playing_ >= 0) return render_playback(ui);
        if (gallery_) return render_gallery(ui);
        ui.header("MOVIE THEATER", std::to_string(files_.size()) + " movies");
        if (files_.empty()) ui.text_center(150, error_.empty() ? "No movies found" : error_, colors::kBad);
        if (!error_.empty() && !files_.empty()) ui.text(10, Ui::kHeight - 44, error_, colors::kBad);
        int n = int(files_.size());
        int first = ui.list_window(n, sel_, kRows);
        for (int i = 0; i < kRows && first + i < n; ++i) {
            int idx = first + i;
            float y = 28 + i * 12.0f;
            if (idx == sel_) ui.fill(4, y - 2, Ui::kWidth - 8, 12, colors::kHighlight);
            ui.text(22, y, movie_title(files_[idx].name), colors::kText);
            ui.text_right(Ui::kWidth - 10, y, files_[idx].name, colors::kDim);
        }
        ui.footer("Cross: play   Square: course preview stills   Triangle: back");
    }

private:
    static constexpr int kRows = 24;

    struct MovieFile {
        std::string path, name;
        u64 size = 0;
    };

    void load(App& app) {
        loaded_ = true;
        volume_ = app.audio().music_volume();
        Vfs& vfs = *app.vfs();
        for (const std::string& name : vfs.list_dir("stream")) {
            if (name.size() < 4 || name.substr(name.size() - 4) != ".pss") continue;
            MovieFile f{"stream/" + name, name, 0};
            if (const Volume::Entry* e = vfs.base() ? vfs.base()->find(f.path) : nullptr) f.size = e->size;
            files_.push_back(f);
        }
        previews_.load(vfs);
        if (std::getenv("GT3R_MOVIE_STILLS")) gallery_ = true;
        if (const char* env = std::getenv("GT3R_MOVIE")) {
            std::string want = normalize_vfs_path(env);
            for (size_t i = 0; i < files_.size(); ++i)
                if (files_[i].path == want) {
                    sel_ = int(i);
                    const char* start = std::getenv("GT3R_MOVIE_START");
                    play(app, int(i), start ? std::atof(start) : 0.0);
                    player_.wait_for_frame(30.0); // deterministic --shot captures
                    overlay_ = true;
                }
        }
    }

    void play(App& app, int idx, double start = 0.0) {
        stop();
        try {
            app.audio().stop_music();
            player_.set_audio_device(app.audio().output_device_id());
            player_.start(video::open_movie(*app.vfs(), files_[idx].path), volume_, start);
            playing_ = idx;
            sel_ = idx;
            error_.clear();
        } catch (const std::exception& e) {
            error_ = files_[idx].name + ": " + e.what();
            LOG_ERROR("Cannot play %s: %s", files_[idx].path.c_str(), e.what());
        }
    }

    void stop() {
        if (playing_ >= 0 && player_.active())
            LOG_INFO("Movie %s: %s", files_[size_t(playing_)].path.c_str(), player_.summary().c_str());
        player_.stop();
        playing_ = -1;
    }

    // Course preview stills from stream/course.ipic (IPU pictures).
    void render_gallery(Ui& ui) {
        const auto& entries = previews_.entries();
        ui.header("COURSE PREVIEWS", std::string(video::CoursePreviews::kPath) + "  " +
                                         std::to_string(entries.size()) + " stills");
        if (entries.empty()) {
            ui.text_center(150, "stream/course.ipic not found", colors::kBad);
        } else {
            int n = int(entries.size());
            int first = ui.list_window(n, still_sel_, kRows);
            for (int i = 0; i < kRows && first + i < n; ++i) {
                int idx = first + i;
                float y = 28 + i * 12.0f;
                if (idx == still_sel_) ui.fill(4, y - 2, 150, 12, colors::kHighlight);
                ui.text(10, y, entries[idx].name, colors::kText);
            }
            const std::string& name = entries[still_sel_].name;
            if (name != still_name_) {
                still_name_ = name;
                if (still_tex_) SDL_DestroyTexture(still_tex_);
                still_tex_ = nullptr;
                if (const video::RgbaImage* img = previews_.image(name)) {
                    still_tex_ = SDL_CreateTexture(ui.renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                                   img->width, img->height);
                    if (still_tex_) SDL_UpdateTexture(still_tex_, nullptr, img->rgba.data(), img->width * 4);
                }
            }
            if (still_tex_) {
                // 4:3 picture in the area right of the list.
                float h = Ui::kHeight - 60.0f, w = h * 4.0f / 3.0f;
                SDL_FRect dst{Ui::kWidth - w - 10, 28, w, h};
                SDL_RenderTexture(ui.renderer(), still_tex_, nullptr, &dst);
            } else {
                ui.text(200, 150, "cannot decode " + name, colors::kBad);
            }
        }
        ui.footer("Up/Down: course   Square/Triangle: back to movies");
    }

    void render_playback(Ui& ui) {
        ui.fill(0, 0, Ui::kWidth, Ui::kHeight, Color{0, 0, 0});
        const video::MovieInfo& info = player_.info();
        // 4:3 picture; field-split movies show one field at a time (see MoviePlayer::draw).
        player_.draw(ui.renderer(), player_.fit_4_3(0, 0, float(Ui::kWidth), float(Ui::kHeight)));
        if (player_.paused()) ui.text_center(Ui::kHeight / 2 - 4, "PAUSED", colors::kAccent);
        if (!overlay_) return;

        const video::MoviePlayer::Stats st = player_.stats();
        double pos = player_.position(), len = info.estimated_seconds();
        char line[160];
        ui.fill(0, 0, Ui::kWidth, 24, Color{0, 0, 0, 170});
        std::snprintf(line, sizeof(line), "%s  %dx%d %.2f fps %s  %s", files_[playing_].name.c_str(), info.width,
                      info.height, info.frame_rate, player_.field_split() ? "(2 fields/frame)" : "progressive",
                      info.has_audio ? (std::to_string(info.audio_rate) + " Hz ADPCM").c_str() : "no audio");
        ui.text(6, 4, line, colors::kText);
        std::snprintf(line, sizeof(line), "decode %.1f ms (max %.1f)  shown %llu  dropped %llu  A/V %+.0f ms  clock: %s",
                      st.decode_ms_avg, st.decode_ms_max, (unsigned long long)st.shown,
                      (unsigned long long)st.dropped, st.av_drift_ms, st.audio ? "audio" : "wall");
        ui.text(6, 14, line, colors::kDim);

        ui.fill(0, Ui::kHeight - 34, Ui::kWidth, 34, Color{0, 0, 0, 170});
        char t[48];
        std::snprintf(t, sizeof(t), "%d:%02d / %d:%02d   vol %d%%", int(pos) / 60, int(pos) % 60, int(len) / 60,
                      int(len) % 60, int(volume_ * 100 + 0.5f));
        ui.bar(10, Ui::kHeight - 30, Ui::kWidth - 190, 6, len > 0 ? float(std::min(1.0, pos / len)) : 0.0f,
               colors::kAccent);
        ui.text_right(Ui::kWidth - 10, Ui::kHeight - 31, t, colors::kDim);
        ui.footer("Cross: pause  L1/R1: prev/next  Left/Right: volume  Square: info  Circle: stop");
    }

    std::vector<MovieFile> files_;
    std::string error_;
    bool loaded_ = false;
    int sel_ = 0;
    int playing_ = -1;
    bool overlay_ = true;
    float volume_ = 0.8f;
    video::MoviePlayer player_;
    video::CoursePreviews previews_;
    bool gallery_ = false;
    int still_sel_ = 0;
    std::string still_name_;
    SDL_Texture* still_tex_ = nullptr;
};

// Full-screen playback used by the game flow (see screen_movies.h).
class MovieScreen : public Screen {
public:
    MovieScreen(std::string path, std::function<void(App&)> on_done, MovieScreenOptions opt)
        : path_(std::move(path)), on_done_(std::move(on_done)), opt_(opt) {}
    ~MovieScreen() override {
        if (player_.active()) LOG_INFO("Movie %s: %s", path_.c_str(), player_.summary().c_str()); // quit while playing
    }

    void update(App& app, double) override {
        if (!started_) {
            started_ = true;
            if (std::getenv("GT3R_SKIP_MOVIES")) return finish(app); // developer shortcut
            float volume = opt_.volume >= 0 ? opt_.volume : app.audio().music_volume();
            try {
                if (opt_.stop_music) app.audio().stop_music();
                Vfs* vfs = app.vfs();
                if (!vfs) throw FormatError("no disc");
                player_.set_audio_device(app.audio().output_device_id());
                if (opt_.loop) {
                    std::string path = path_;
                    player_.start_loop([vfs, path] { return video::open_movie(*vfs, path); }, volume);
                } else {
                    player_.start(video::open_movie(*vfs, path_), volume);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Cannot play %s: %s", path_.c_str(), e.what());
                return finish(app);
            }
        }
        player_.update(app.renderer());
        const MenuInput& m = app.menu();
        bool skip = opt_.skippable &&
                    (m.pressed(PadButton::Cross) || m.pressed(PadButton::Start) || m.pressed(PadButton::Circle));
        if (skip || (!opt_.loop && player_.finished())) finish(app);
    }

    bool follows_aspect() const override { return true; } // the game's own movies

    void render(App&, Ui& ui) override {
        ui.fill(0, 0, ui.width(), ui.height(), Color{0, 0, 0});
        player_.draw(ui.renderer(), player_.fit_4_3(0, 0, ui.width(), ui.height()));
    }

private:
    void finish(App& app) {
        if (done_) return;
        done_ = true;
        if (player_.active()) LOG_INFO("Movie %s: %s", path_.c_str(), player_.summary().c_str());
        player_.stop();
        app.pop();
        if (on_done_) on_done_(app);
    }

    std::string path_;
    std::function<void(App&)> on_done_;
    MovieScreenOptions opt_;
    video::MoviePlayer player_;
    bool started_ = false, done_ = false;
};

} // namespace

std::unique_ptr<Screen> make_movie_screen(const std::string& vfs_path, std::function<void(App&)> on_done,
                                          MovieScreenOptions options) {
    return std::make_unique<MovieScreen>(vfs_path, std::move(on_done), options);
}

void play_movie(App& app, const std::string& vfs_path, std::function<void(App&)> on_done, MovieScreenOptions options) {
    app.push(make_movie_screen(vfs_path, std::move(on_done), options));
}

// Boot flow: the opening movie (skippable), then the game menus.
std::unique_ptr<Screen> make_boot_screen() {
    return make_movie_screen("stream/opening.pss", [](App& app) {
        for (const ScreenEntry& e : screen_registry())
            if (std::string_view(e.id) == "gt3menu") return app.push(e.factory());
        LOG_ERROR("boot: no gt3menu screen registered");
    });
}

GT3R_REGISTER_PLAYER_SCREEN(boot, "boot", "Play", "Opening movie, then the game's title menu.", 1,
                     [] { return make_boot_screen(); });
GT3R_REGISTER_PLAYER_SCREEN(movies, "movies", "Movie theater", "Watch the game's movies: opening, endings and more.", 9,
                     [] { return std::make_unique<MoviesScreen>(); });

} // namespace gt3r
