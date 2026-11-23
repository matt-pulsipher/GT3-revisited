#include "video/movie_player.h"

#include "base/log.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace gt3r::video {

namespace {
constexpr size_t kMaxQueuedFrames = 6;
constexpr double kAudioLead = 0.25; // seconds of audio kept queued in the SDL stream
constexpr size_t kAudioChunk = 2048;
constexpr double kAudioJump = 0.1; // seconds off the audio position: jump there (after a stall)
// A picture (a frame, or a field of a field-split movie) is shown when the clock is at most this
// much before its time, so a refresh that comes a little early does not leave it for the next.
constexpr double kEarly = 0.002;

u64 now_ns(const std::function<u64()>& now) { return now ? now() : SDL_GetTicksNS(); }
} // namespace

MoviePlayer::~MoviePlayer() {
    stop();
    if (texture_) SDL_DestroyTexture(texture_);
}

void MoviePlayer::start(std::unique_ptr<Movie> movie, float volume, double start_seconds) {
    loop_open_ = nullptr;
    begin(std::move(movie), volume, start_seconds);
}

void MoviePlayer::start_loop(std::function<std::unique_ptr<Movie>()> open, float volume) {
    loop_open_ = std::move(open);
    begin(loop_open_(), volume, 0.0);
}

void MoviePlayer::begin(std::unique_ptr<Movie> movie, float volume, double start_seconds) {
    stop();
    movie_ = std::move(movie);
    info_ = movie_->info();
    volume_ = volume;
    start_seconds_ = std::max(0.0, start_seconds);
    clock_ = start_seconds_;
    shown_time_ = start_seconds_;
    last_ticks_ = now_ns(now_);
    stats_ = {};
    quit_ = false;
    paused_ = false;
    video_done_ = false;
    audio_done_ = !info_.has_audio;
    audio_frames_put_ = 0;
    tex_left_split_ = texture_split(); // (still the earlier one's when the last movie put up no picture)
    field_split_ = false;
    layout_ = FieldLayoutDetector();
    ready_.clear();

    if (info_.has_audio) {
        SDL_AudioSpec spec{SDL_AUDIO_S16, int(info_.audio_channels), int(info_.audio_rate)};
        audio_ = SDL_OpenAudioDeviceStream(device_, &spec, nullptr, nullptr);
        if (!audio_ && device_ != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK) // unplugged since
            audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (audio_) {
            SDL_SetAudioStreamGain(audio_, volume_);
            SDL_ResumeAudioStreamDevice(audio_);
        } else {
            LOG_ERROR("Movie audio unavailable: %s", SDL_GetError());
            movie_->set_audio_enabled(false);
            audio_done_ = true;
        }
    } else {
        movie_->set_audio_enabled(false);
    }
    stats_.audio = audio_ != nullptr;
    worker_ = std::thread([this] { run(); });
}

void MoviePlayer::stop() {
    if (worker_.joinable()) {
        quit_ = true;
        worker_.join();
    }
    if (audio_) {
        SDL_DestroyAudioStream(audio_);
        audio_ = nullptr;
    }
    movie_.reset();
    std::lock_guard lock(mutex_);
    for (Frame& f : ready_) free_.push_back(std::move(f));
    ready_.clear();
}

void MoviePlayer::set_paused(bool p) {
    paused_ = p;
    if (audio_) {
        if (p) SDL_PauseAudioStreamDevice(audio_);
        else SDL_ResumeAudioStreamDevice(audio_);
    }
}

void MoviePlayer::set_volume(float v) {
    volume_ = std::clamp(v, 0.0f, 1.0f);
    if (audio_) SDL_SetAudioStreamGain(audio_, volume_);
}

double MoviePlayer::audio_clock() const {
    int queued = SDL_GetAudioStreamQueued(audio_);
    u64 played = audio_frames_put_ - std::min<u64>(audio_frames_put_, u64(std::max(queued, 0)) /
                                                                          (2 * info_.audio_channels));
    double offset = info_.audio_start_pts >= 0 && info_.start_pts >= 0
                        ? double(info_.audio_start_pts - info_.start_pts) / 90000.0
                        : 0.0;
    return offset + double(played) / info_.audio_rate;
}

void MoviePlayer::run() {
    const u32 ch = std::max<u32>(1, info_.audio_channels);
    std::vector<s16> pcm(kAudioChunk * ch);
    u64 audio_skip = info_.has_audio ? u64(start_seconds_ * info_.audio_rate) : 0;
    Frame prev, pending; // decoded frames: the one before the next picture out, and that picture
    while (!quit_) {
        bool worked = false;
        try {
            // Audio first: it is cheap and must never underrun.
            if (audio_ && !audio_done_) {
                int queued = SDL_GetAudioStreamQueued(audio_);
                double queued_s = double(std::max(queued, 0)) / (2.0 * ch * info_.audio_rate);
                if (queued_s < kAudioLead || audio_frames_put_ < audio_skip) {
                    size_t n = movie_->read_audio(pcm.data(), kAudioChunk);
                    if (n == 0) {
                        audio_done_ = true;
                    } else {
                        u64 put = audio_frames_put_;
                        size_t skip = put < audio_skip ? size_t(std::min<u64>(audio_skip - put, n)) : 0;
                        if (n > skip)
                            SDL_PutAudioStreamData(audio_, pcm.data() + skip * ch, int((n - skip) * ch * sizeof(s16)));
                        audio_frames_put_ += n;
                    }
                    worked = true;
                }
            }
            bool want_video = false;
            {
                std::lock_guard lock(mutex_);
                want_video = ready_.size() < kMaxQueuedFrames;
            }
            if (!worked && !video_done_ && want_video) {
                Frame f = take_free();
                u64 t0 = SDL_GetTicksNS();
                bool got = movie_->next_frame(f);
                // Pictures go out one frame behind the decoder: deinterlacing a frame needs the
                // next one's fields. The last frame goes out once the stream has ended.
                if (got) {
                    layout_.add(f);
                    field_split_ = layout_.field_split();
                }
                if (pending.width > 0) emit(prev, pending, got ? f : pending, t0);
                if (got) {
                    std::lock_guard lock(mutex_);
                    free_.push_back(std::move(prev));
                    prev = std::move(pending);
                    pending = std::move(f);
                } else {
                    video_done_ = true;
                }
                worked = true;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Movie decode failed: %s", e.what());
            video_done_ = true;
            audio_done_ = true;
        }
        if (video_done_ && audio_done_) break;
        if (!worked) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

Frame MoviePlayer::take_free() {
    std::lock_guard lock(mutex_);
    if (free_.empty()) return {};
    Frame f = std::move(free_.back());
    free_.pop_back();
    return f;
}

void MoviePlayer::emit(const Frame& prev, const Frame& cur, const Frame& next, u64 t0) {
    Frame out = take_free();
    if (field_split_ && can_deinterlace(cur)) deinterlace_fields(prev.width > 0 ? prev : cur, cur, next, out);
    else out = cur;
    double ms = double(SDL_GetTicksNS() - t0) / 1e6;
    double t = info_.start_pts >= 0 ? double(out.pts - info_.start_pts) / 90000.0 : 0.0;
    std::lock_guard lock(mutex_);
    ++stats_.decoded;
    stats_.decode_ms_avg += (ms - stats_.decode_ms_avg) / double(std::min<u64>(stats_.decoded, 120));
    stats_.decode_ms_max = std::max(stats_.decode_ms_max, ms);
    if (t + 0.5 / std::max(1.0, info_.frame_rate) < start_seconds_) free_.push_back(std::move(out));
    else ready_.push_back(std::move(out));
}

bool MoviePlayer::wait_for_frame(double timeout_seconds) {
    u64 deadline = SDL_GetTicksNS() + u64(timeout_seconds * 1e9);
    while (SDL_GetTicksNS() < deadline) {
        {
            std::lock_guard lock(mutex_);
            if (!ready_.empty()) return true;
        }
        if (video_done_) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

bool MoviePlayer::finished() const {
    if (!movie_) return true;
    std::lock_guard lock(mutex_);
    bool audio_drained = audio_done_ && (!audio_ || SDL_GetAudioStreamQueued(audio_) == 0);
    return video_done_ && ready_.empty() && audio_drained;
}

MoviePlayer::Stats MoviePlayer::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}

std::string MoviePlayer::summary() const {
    const Stats st = stats();
    char b[256];
    std::string av;
    if (st.audio) {
        av = ", A/V within " + std::to_string(int(std::lround(st.av_error_ms_max))) + " ms";
        if (st.resyncs)
            av += " (" + std::to_string(st.resyncs) + (st.resyncs == 1 ? " jump" : " jumps") +
                  " back in sync after a stall)";
    }
    std::snprintf(b, sizeof b, "%dx%d %.2f fps%s, %.2f s: %llu frames shown, %llu dropped, %s clock%s", info_.width,
                  info_.height, info_.frame_rate, field_split_ ? " (2 fields/frame)" : "", clock_,
                  (unsigned long long)st.shown, (unsigned long long)st.dropped, st.audio ? "audio" : "wall",
                  av.c_str());
    return b;
}

void MoviePlayer::picture_rects(const SDL_FRect& display, SDL_FRect& src, SDL_FRect& dst) const {
    float w = float(tex_w_), h = float(tex_h_);
    src = {0, 0, w, h};
    dst = display;
    if (!texture_split() || tex_h_ < 4) return;
    if (tex_stacked_) { // deinterlaced: a full picture per field, stacked
        src = {0, second_field() ? h / 2 : 0, w, h / 2};
        return;
    }
    // Not deinterlaced (field_layout.h's can_deinterlace): the field itself, line-doubled. First field (top half, even frame lines) for the first half of the frame
    // period, then the second field (bottom half, odd lines). Frame line Y of
    // field line k is 2k (+1 for the second field), so mapping frame row
    // centre y to field coordinates gives y/2 + 1/4 (first) or y/2 - 1/4
    // (second). Drawing frame rows [0, H-1) resp. [1, H) keeps both inside
    // their own half: half-height minus half a line of source, and one frame
    // row less of destination.
    const bool second = second_field();
    float half = h / 2, row = display.h / h;
    src = {0, (second ? half : 0) + 0.25f, w, half - 0.5f};
    dst = {display.x, display.y + (second ? row : 0), display.w, display.h - row};
}

bool MoviePlayer::second_field() const {
    if (!texture_split()) return false;
    // The second field's time, by the same early rule as the frames': with 29.97 fps at 60 Hz a
    // refresh that took a frame up to 2 ms early came 16.67 ms later just before its second
    // field, so for seconds at a time only first fields were shown.
    const double period = info_.frame_rate > 0 ? 1.0 / info_.frame_rate : 1.0 / 30.0;
    return clock_ + kEarly - shown_time_ >= period * 0.5;
}

void MoviePlayer::draw(SDL_Renderer* renderer, const SDL_FRect& display) const {
    if (!texture_) return;
    SDL_FRect src, dst;
    picture_rects(display, src, dst);
    SDL_RenderTexture(renderer, texture_, &src, &dst);
}

double audio_clock_correction(double err, double step) {
    if (std::abs(err) > kAudioJump) return err;
    return err * std::min(1.0, step / (std::abs(err) > 0.03 ? 0.25 : 2.0));
}

double movie_display_aspect(const MovieInfo& info) {
    if (info.width <= 0 || info.height <= 0) return 4.0 / 3.0;
    const bool pal = std::abs(info.frame_rate - 25.0) < 0.5;
    const double frame_lines = pal ? 512.0 : 448.0; // the display frame that fills the 4:3 picture
    return double(info.width) / double(info.height) * (frame_lines / 480.0);
}

SDL_FRect MoviePlayer::fit_4_3(float x, float y, float w, float h) const {
    const float aspect = float(movie_display_aspect(info_));
    float dw = w, dh = w / aspect;
    if (dh > h) {
        dh = h;
        dw = h * aspect;
    }
    return {x + (w - dw) / 2, y + (h - dh) / 2, dw, dh};
}

bool MoviePlayer::update(SDL_Renderer* renderer) {
    u64 now = now_ns(now_);
    double dt = double(now - last_ticks_) / 1e9;
    last_ticks_ = now;
    if (!movie_) return false;
    if (loop_open_ && finished()) {
        try {
            begin(loop_open_(), volume_, 0.0);
        } catch (const std::exception& e) {
            LOG_ERROR("Cannot restart looping movie: %s", e.what());
            loop_open_ = nullptr;
            return false;
        }
    }

    // Clock: audio position while audio is playing, wall clock otherwise.
    bool audio_live = audio_ && !(audio_done_ && SDL_GetAudioStreamQueued(audio_) == 0);
    if (!paused_) {
        // Wall time, steered toward the audio position (audio_clock_correction).
        const double step = std::min(dt, 0.1);
        clock_ += step;
        if (audio_live) {
            double err = audio_clock() - clock_;
            const bool jump = std::abs(err) > kAudioJump;
            clock_ += audio_clock_correction(err, step);
            if (stats_.shown > 0) {
                std::lock_guard lock(mutex_);
                if (jump) ++stats_.resyncs;
                else stats_.av_error_ms_max = std::max(stats_.av_error_ms_max, std::abs(err) * 1000.0);
            }
        }
    }

    Frame frame;
    bool have = false;
    {
        std::lock_guard lock(mutex_);
        // Present the newest frame that is due; older due frames are dropped.
        while (!ready_.empty()) {
            const Frame& f = ready_.front();
            double t = info_.start_pts >= 0 ? double(f.pts - info_.start_pts) / 90000.0 : 0.0;
            bool due = t <= clock_ + kEarly || (stats_.shown == 0 && !have);
            if (!due) break;
            if (have) {
                ++stats_.dropped;
                free_.push_back(std::move(frame));
            }
            frame = std::move(ready_.front());
            ready_.pop_front();
            have = true;
            stats_.av_drift_ms = (t - clock_) * 1000.0;
            shown_time_ = t;
            if (stats_.shown == 0) break; // first frame: show immediately
        }
        if (have) ++stats_.shown;
    }
    if (!have) return false;

    if (!texture_ || tex_w_ != frame.width || tex_h_ != frame.height) {
        if (texture_) SDL_DestroyTexture(texture_);
        texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, frame.width,
                                     frame.height);
        if (!texture_) LOG_ERROR("Cannot create movie texture: %s", SDL_GetError());
        tex_w_ = frame.width;
        tex_h_ = frame.height;
    }
    tex_stacked_ = frame.fields_stacked;
    tex_left_split_.reset();
    if (texture_)
        SDL_UpdateYUVTexture(texture_, nullptr, frame.y.data(), frame.width, frame.u.data(), frame.chroma_width(),
                             frame.v.data(), frame.chroma_width());
    std::lock_guard lock(mutex_);
    free_.push_back(std::move(frame));
    return true;
}

} // namespace gt3r::video
