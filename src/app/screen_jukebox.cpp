// Music jukebox: the sequenced menu music (music/music.seq, played through
// the SPU2-style sequencer) and the streamed soundtrack (bgm/*.ads), from
// the disc or a mod.

#include "app/app.h"
#include "app/screen_registry.h"
#include "formats/bgm_index.h"

#include <algorithm>
#include <map>

namespace gt3r {

namespace {

class JukeboxScreen : public Screen {
public:
    void update(App& app, double) override {
        if (!loaded_) load(app);
        const MenuInput& m = app.menu();
        int n = int(tracks_.size());
        if (n) sel_ = std::clamp(sel_ + m.vertical(), 0, n - 1);
        if (XrList::Hit h = xr_list_.take_hover(); h.row >= 0 && h.row < n) sel_ = h.row; // the VR pointer
        if (XrList::Hit h = xr_list_.take_click(); h.row >= 0 && h.row < n) play(app, sel_ = h.row);
        if (m.horizontal()) app.audio().set_music_volume(app.audio().music_volume() + 0.05f * m.horizontal());
        if (m.pressed(PadButton::Cross) && n) play(app, sel_);
        if (m.pressed(PadButton::Circle)) {
            app.audio().stop_music();
            playing_ = -1;
        }
        if (m.pressed(PadButton::Square)) {
            VoiceMixer& v = app.audio().voices();
            v.set_reverb(v.reverb_mode() == SpuReverb::Off ? SpuReverb::StudioC : SpuReverb::Off, 0x0FFF, 0x0FFF);
        }
        // Auto-advance when a streamed track ends (sequences loop forever).
        if (playing_ >= 0 && !app.audio().music_playing() && n) play(app, (playing_ + 1) % n);
        if (m.pressed(PadButton::Triangle)) {
            app.audio().stop_music();
            app.save_config();
            app.pop();
        }
    }

    void on_event(App&, const SDL_Event& e) override { xr_list_.event(e); }

    void render(App& app, Ui& ui) override {
        xr_list_.clear();
        char vol[64];
        int rev = app.audio().voices().reverb_mode();
        std::snprintf(vol, sizeof(vol), "Volume %d%%  reverb %s", int(app.audio().music_volume() * 100 + 0.5f),
                      SpuReverb::mode_name(rev));
        ui.header("MUSIC JUKEBOX", vol);
        if (tracks_.empty()) {
            ui.text_center(150, error_.empty() ? "No tracks found" : error_, colors::kBad);
        }
        int n = int(tracks_.size());
        int first = xr_list_.first(0, sel_, kRows, ui.list_window(n, sel_, kRows));
        xr_list_.rows(0, first, std::min(kRows, n - first), 4, Ui::kWidth - 4, 26, 12);
        for (int i = 0; i < kRows && first + i < n; ++i) {
            int idx = first + i;
            const Track& t = tracks_[idx];
            float y = 28 + i * 12.0f;
            if (idx == sel_) ui.fill(4, y - 2, Ui::kWidth - 8, 12, colors::kHighlight);
            Color c = idx == playing_ ? colors::kGood : colors::kText;
            std::string title = t.title;
            if (title.size() > 40) title = title.substr(0, 37) + "...";
            std::string artist = t.artist.size() > 24 ? t.artist.substr(0, 21) + "..." : t.artist;
            ui.text(10, y, idx == playing_ ? ">" : " ", colors::kGood);
            ui.text(22, y, title, c);
            ui.text(352, y, t.sequence >= 0 ? "SEQ" : "ADS", colors::kDim);
            ui.text(408, y, artist, colors::kDim);
        }
        if (playing_ >= 0) {
            double pos = app.audio().music_position(), len = app.audio().music_length();
            char t[64];
            if (tracks_[playing_].sequence >= 0) {
                // Sequences loop: show the time, the loop length and the passes.
                int loops = app.audio().sequencer().loops_completed();
                std::snprintf(t, sizeof(t), "%d:%02d  loop %d:%02d  x%d  %d voices", int(pos) / 60, int(pos) % 60,
                              int(len) / 60, int(len) % 60, loops, app.audio().voices().active_voices());
                ui.text_right(Ui::kWidth - 10, Ui::kHeight - 31, t, colors::kDim);
            } else {
                std::snprintf(t, sizeof(t), "%d:%02d / %d:%02d", int(pos) / 60, int(pos) % 60, int(len) / 60,
                              int(len) % 60);
                ui.bar(10, Ui::kHeight - 30, Ui::kWidth - 120, 6, len > 0 ? float(pos / len) : 0.0f, colors::kAccent);
                ui.text_right(Ui::kWidth - 10, Ui::kHeight - 31, t, colors::kDim);
            }
        }
        ui.footer("Cross: play  Circle: stop  Left/Right: volume  Square: reverb  Tri: back");
    }

private:
    static constexpr int kRows = 24;

    struct Track {
        std::string title, artist;
        std::string file; // streamed: path under bgm/
        int sequence = -1; // sequenced: index into music.seq
    };

    void load(App& app) {
        loaded_ = true;
        // Sequenced music: one entry per sequence, named by the game's ids
        // that use it.
        std::map<u32, Track> seqs;
        for (const sound::MusicEntry& e : app.audio().music_index(*app.vfs())) {
            Track& t = seqs[e.sequence];
            if (t.sequence < 0) {
                t.sequence = int(e.sequence);
                t.title = e.title + " -";
                t.artist = e.artist;
            }
            t.title += " " + e.name;
        }
        for (auto& [i, t] : seqs) tracks_.push_back(std::move(t));

        auto inf = app.vfs()->read("bgm/ads.inf");
        if (!inf) {
            if (tracks_.empty()) error_ = "bgm/ads.inf not found";
            return;
        }
        std::vector<BgmTrack> bgm;
        try {
            bgm = parse_bgm_index(*inf);
        } catch (const std::exception& e) {
            error_ = e.what();
        }
        // Keep only tracks whose stream exists (mods may add or remove).
        for (const BgmTrack& b : bgm) {
            if (!app.vfs()->exists("bgm/" + b.file)) continue;
            tracks_.push_back({b.title.empty() ? b.name : b.title, b.artist, b.file, -1});
        }
    }

    void play(App& app, int idx) {
        const Track& t = tracks_[idx];
        bool ok = t.sequence >= 0 ? app.audio().play_sequence(*app.vfs(), t.sequence)
                                  : app.audio().play_music(*app.vfs(), "bgm/" + t.file, false);
        playing_ = ok ? idx : -1;
    }

    std::vector<Track> tracks_;
    std::string error_;
    bool loaded_ = false;
    int sel_ = 0;
    int playing_ = -1;
    XrList xr_list_;
};

} // namespace

std::unique_ptr<Screen> make_jukebox_screen() { return std::make_unique<JukeboxScreen>(); }

GT3R_REGISTER_PLAYER_SCREEN(jukebox, "jukebox", "Music jukebox", "Play the soundtrack streamed from your disc.", 6,
                     [] { return make_jukebox_screen(); });

} // namespace gt3r
