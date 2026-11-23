#pragma once

// Real-time movie playback: decodes a Movie on a worker thread, plays its
// audio on an SDL audio stream owned by the player, and presents the frame
// that is due on the audio clock (wall clock for silent movies) through an
// IYUV streaming texture.
//
//   MoviePlayer player;
//   player.start(open_movie(vfs, "stream/opening.pss"));
//   // every frame:  player.update(renderer);  draw player.texture()

#include "video/field_layout.h"
#include "video/movie.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_render.h>

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace gt3r::video {

// The shape a movie's picture is shown in (width / height). The PS2 shows a movie in its
// 640-pixel-wide display frame, which fills the 4:3 TV picture: 448 lines on NTSC and 512 on
// PAL, told apart by the frame rate (PAL movies run at 25 fps, 50 fields a second). So
// 640x448 at 29.97 / 30 fps and 640x512 at 25 fps both show at 4:3, and smaller pictures keep
// that pixel shape (menu_bg2's 320x112 strip).
double movie_display_aspect(const MovieInfo& info);

// How far a movie's clock moves toward the audio position in a step of `step` seconds when it
// is `err` seconds behind it (negative: ahead). The audio position only moves when the device
// pulls a buffer (10-20 ms steps, so `err` saws by about +-10 ms), so the clock runs on wall
// time and is steered gently: over about 2 s within 30 ms, over a quarter second further off,
// and it jumps there past 100 ms (after a stall). A stronger pull (5% a frame) made the clock's
// steps uneven by up to 1 ms, and where fields began near a refresh, as they keep doing with
// 29.97 fps at 60 Hz, they were shown twice and skipped.
double audio_clock_correction(double err, double step);

class MoviePlayer {
public:
    MoviePlayer() = default;
    ~MoviePlayer();
    MoviePlayer(const MoviePlayer&) = delete;
    MoviePlayer& operator=(const MoviePlayer&) = delete;

    // Starts playback (stopping any current movie). Frames and audio before
    // `start_seconds` are decoded and discarded.
    void start(std::unique_ptr<Movie> movie, float volume = 0.8f, double start_seconds = 0.0);
    // Plays a movie in a loop (menu backgrounds): `open` is called again each
    // time the movie ends, e.g. [&vfs] { return open_movie(vfs, "stream/menu_bg2.pss"); }.
    void start_loop(std::function<std::unique_ptr<Movie>()> open, float volume = 0.8f);
    void stop();
    bool active() const { return worker_.joinable(); }

    // Main thread, once per rendered frame: advances the clock and uploads
    // the frame that is due. Returns true when the texture changed.
    bool update(SDL_Renderer* renderer);
    SDL_Texture* texture() const { return texture_; }
    // Source and destination rectangles for drawing the current picture into
    // `display`: the whole frame, or for field-split movies (field_layout.h)
    // the full picture rebuilt for the field that is due (deinterlace_fields;
    // else the field line-doubled, at its half-line offset so it does not bob).
    void picture_rects(const SDL_FRect& display, SDL_FRect& src, SDL_FRect& dst) const;
    // Draws the current picture into `display` (no-op before the first frame).
    void draw(SDL_Renderer* renderer, const SDL_FRect& display) const;
    // Display rectangle for the movie's TV picture (movie_display_aspect: 4:3 for full-size
    // NTSC and PAL movies) fitted into a w x h area.
    SDL_FRect fit_4_3(float x, float y, float w, float h) const;
    bool field_split() const { return field_split_; }

    // Blocks until a decoded frame is queued (or decoding ended / timeout).
    // For non-interactive use such as screenshots.
    bool wait_for_frame(double timeout_seconds);

    void set_paused(bool p);
    bool paused() const { return paused_; }
    void set_volume(float v);
    // The playback device for the sound (AudioEngine::output_device_id()), from the next
    // start; the default when it can't be opened.
    void set_audio_device(SDL_AudioDeviceID device) { device_ = device; }
    // All frames presented and the audio drained.
    bool finished() const;

    double position() const { return clock_; } // seconds since the first frame
    // The presentation time of the picture in the texture (seconds since the first frame), and
    // for field-split movies whether its second field is the one drawn now.
    double picture_time() const { return shown_time_; }
    bool second_field() const;
    const MovieInfo& info() const { return info_; }
    // The time source for the clock (nanoseconds; SDL_GetTicksNS by default), set before start():
    // tests step it like a display's refreshes.
    void set_time_source(std::function<u64()> now) { now_ = std::move(now); }

    struct Stats {
        u64 decoded = 0, shown = 0, dropped = 0;
        double decode_ms_avg = 0, decode_ms_max = 0; // per frame, worker thread
        double av_drift_ms = 0;  // presented frame time minus clock at upload
        double av_error_ms_max = 0; // the most the clock was off the audio position while steered (A/V)
        u64 resyncs = 0;            // jumps to the audio position (more than 100 ms off: after a stall)
        bool audio = false;      // audio clock in use
    };
    Stats stats() const;
    // One line for the log: size, rate, position, frames shown and dropped, the clock and the A/V sync.
    std::string summary() const;

private:
    void run();
    double audio_clock() const;

    void begin(std::unique_ptr<Movie> movie, float volume, double start_seconds);
    // Worker thread: a frame from the free pool (or a new one), and queueing the picture made
    // from `cur` (deinterlaced with its neighbours when field-split); `t0` is when its decode began.
    Frame take_free();
    void emit(const Frame& prev, const Frame& cur, const Frame& next, u64 t0);

    std::unique_ptr<Movie> movie_;
    std::function<std::unique_ptr<Movie>()> loop_open_;
    MovieInfo info_;
    std::thread worker_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> video_done_{false};
    std::atomic<bool> audio_done_{false};
    std::atomic<bool> field_split_{false};
    FieldLayoutDetector layout_; // worker thread
    double start_seconds_ = 0;

    mutable std::mutex mutex_;
    std::deque<Frame> ready_; // decoded, in display order
    std::vector<Frame> free_;
    Stats stats_;

    SDL_AudioStream* audio_ = nullptr;
    SDL_AudioDeviceID device_ = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    std::atomic<u64> audio_frames_put_{0}; // including frames skipped before start_seconds
    float volume_ = 0.8f;

    SDL_Texture* texture_ = nullptr;
    int tex_w_ = 0, tex_h_ = 0;
    bool tex_stacked_ = false; // the texture holds deinterlace_fields() output
    // begin() starts the layout over, but the picture in the texture stays until the new movie's first one replaces
    // it: meanwhile it keeps the layout it was drawn with (none: the texture holds the playing movie's picture).
    std::optional<bool> tex_left_split_;
    bool texture_split() const { return tex_left_split_.value_or(field_split_.load()); }
    double clock_ = 0;
    double shown_time_ = 0; // presentation time of the frame in the texture
    u64 last_ticks_ = 0;
    std::function<u64()> now_;
};

} // namespace gt3r::video
