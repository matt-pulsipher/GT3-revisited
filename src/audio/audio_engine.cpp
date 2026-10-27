#include "audio/audio_engine.h"

#include "base/log.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <vector>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

namespace gt3r {

AudioEngine::AudioEngine() {
    // The game's sound init: effect mode 4 on both cores, EVOL 0x0FFF.
    voices_.set_reverb(SpuReverb::StudioC, 0x0FFF, 0x0FFF);
    voices_.set_bus_gain(VoiceBus::Music, music_volume_);
    voices_.set_bus_gain(VoiceBus::Effects, sfx_volume_);
    // The sequencer runs inside the mixer at the game's 60 Hz, sample-exact.
    voices_.set_tick_hook([this] { sequencer_.update(voices_); }, sequencer_.samples_per_update());

    external_decode_thread_ = std::thread(&AudioEngine::decode_external_music, this);

    // The stream's input format is set per track; output adapts to the device.
    SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 44100};
    music_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioEngine::feed, this);
    if (!music_stream_) {
        LOG_ERROR("Audio unavailable: %s", SDL_GetError());
        return;
    }
    SDL_SetAudioStreamGain(music_stream_, stream_output_gain(stream_level_, music_volume_));
    SDL_ResumeAudioStreamDevice(music_stream_);

    SDL_AudioSpec vspec{SDL_AUDIO_S16, 2, int(VoiceMixer::kRate)};
    sfx_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &vspec, &AudioEngine::feed_voices, this);
    if (!sfx_stream_) {
        LOG_ERROR("Voice mixer unavailable: %s", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(sfx_stream_); // unity gain: the volumes are the mixer's bus gains
}

AudioEngine::~AudioEngine() {
    {
        std::lock_guard lock(external_decode_mutex_);
        external_decode_stopping_ = true;
        external_decode_request_.reset();
    }
    external_decode_cv_.notify_one();
    if (external_decode_thread_.joinable()) external_decode_thread_.join();
    if (sfx_stream_) SDL_DestroyAudioStream(sfx_stream_);
    if (music_stream_) SDL_DestroyAudioStream(music_stream_);
    voices_.set_tick_hook({}, 0);
}

bool AudioEngine::load_sequences(Vfs& vfs) {
    if (seq_load_tried_) return seqs_ != nullptr;
    seq_load_tried_ = true;
    try {
        auto inf = vfs.read("music/music.inf");
        auto seq = vfs.read("music/music.seq");
        auto ins = vfs.read("music/music.ins");
        if (!inf || !seq || !ins) {
            LOG_ERROR("Sequenced music not found (music/music.inf, .seq, .ins)");
            return false;
        }
        music_index_ = sound::parse_music_index(*inf);
        seqs_ = std::make_shared<const sound::SeqBank>(sound::SeqBank::parse(*seq));
        sequencer_.set_sequences(seqs_);
        for (size_t i = 0; i < seqs_->sequences.size(); ++i) {
            sound::SeqSummary sum = sound::summarize_sequence(*seqs_, i);
            u64 loop = sum.loop_start_tick >= 0 ? sum.length_ticks - u64(sum.loop_start_tick) : sum.length_ticks;
            seq_loop_seconds_.push_back(sound::seq_ticks_to_seconds(loop, seqs_->sequences[i].tempo));
        }
        sequencer_.set_instruments(std::make_shared<const sound::InstBank>(sound::InstBank::parse(*ins)));
    } catch (const std::exception& ex) {
        LOG_ERROR("Cannot load sequenced music: %s", ex.what());
        seqs_.reset();
        music_index_.clear();
        seq_loop_seconds_.clear();
        return false;
    }
    return true;
}

const std::vector<sound::MusicEntry>& AudioEngine::music_index(Vfs& vfs) {
    load_sequences(vfs);
    return music_index_;
}

bool AudioEngine::play_sequence(Vfs& vfs, int index, float volume) {
    invalidate_external_decode();
    if (!load_sequences(vfs)) return false;
    stop_stream();
    // The request's level is the sequencer's music volume (music §6).
    sequencer_.set_master_volume(bgm::sequence_volume(volume));
    if (!sequencer_.play(voices_, index)) return false;
    LOG_INFO("Playing sequence %d", index);
    return true;
}

bool AudioEngine::play_sequence(Vfs& vfs, const std::string& name, float volume) {
    // "bgm:<id>[:once]": a BGM-layer cue (streamed presentation music), played
    // "if different".
    if (name.rfind("bgm:", 0) == 0) {
        int id = std::atoi(name.c_str() + 4);
        bool loop = name.find(":once") == std::string::npos;
        return bgm_.play_if_different(*this, vfs, id, 0, loop, volume);
    }
    if (!load_sequences(vfs)) return false;
    for (const sound::MusicEntry& e : music_index_)
        if (e.name == name) return play_sequence(vfs, int(e.sequence), volume);
    LOG_ERROR("No sequenced music named %s", name.c_str());
    return false;
}

void AudioEngine::set_sfx_volume(float v) {
    sfx_volume_ = std::clamp(v, 0.0f, 1.0f);
    voices_.set_bus_gain(VoiceBus::Effects, sfx_volume_);
}

void SDLCALL AudioEngine::feed_voices(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* self = static_cast<AudioEngine*>(user);
    if (additional <= 0) return;
    // Render in small blocks so parameter changes land with low latency.
    size_t frames = size_t(additional) / 4;
    if (frames == 0) frames = 1;
    std::vector<s16> buf(frames * 2);
    self->voices_.mix(buf.data(), frames);
    SDL_PutAudioStreamData(stream, buf.data(), int(buf.size() * sizeof(s16)));
}

bool AudioEngine::play_music(Vfs& vfs, const std::string& path, bool loop, float volume) {
    invalidate_external_decode();
    if (!music_stream_) return false;
    sequencer_.stop(voices_);
    std::unique_ptr<AdsStream> stream;
    std::shared_ptr<Bytes> data;
    try {
        std::string src = vfs.source_of(path);
        const Volume::Entry* e = vfs.base() ? vfs.base()->find(path) : nullptr;
        if (src == "disc" && e && !e->compressed) {
            // Stream directly from the disc image; no need to load ~10 MB.
            Volume* vol = vfs.base();
            Bytes head(AdsHeader::kHeaderBytes);
            vol->read_range(*e, 0, head.data(), head.size());
            AdsHeader h = AdsHeader::parse(head);
            Volume::Entry entry = *e;
            stream = std::make_unique<AdsStream>(
                h, [vol, entry](u64 off, void* out, size_t len) { vol->read_range(entry, off, out, len); }, loop);
        } else {
            auto bytes = vfs.read(path);
            if (!bytes) return false;
            data = std::make_shared<Bytes>(std::move(*bytes));
            AdsHeader h = AdsHeader::parse(*data);
            if (u64(h.body_offset) + h.body_size > data->size()) throw FormatError("ADS truncated");
            stream = std::make_unique<AdsStream>(
                h, [data](u64 off, void* out, size_t len) { std::memcpy(out, data->data() + off, len); }, loop);
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("Cannot play %s: %s", path.c_str(), ex.what());
        return false;
    }

    const AdsHeader& h = stream->header();
    SDL_AudioSpec in{SDL_AUDIO_S16, int(h.channels), int(h.sample_rate)};
    // Lock order matches the audio thread's: SDL device -> SDL stream -> mutex_ (feed() runs
    // with the stream lock held). So the stream is locked before mutex_, and the device call
    // below is made with neither held; otherwise the two threads can deadlock.
    SDL_LockAudioStream(music_stream_);
    {
        std::lock_guard lock(mutex_);
        SDL_ClearAudioStream(music_stream_);
        SDL_SetAudioStreamFormat(music_stream_, &in, nullptr);
        music_length_ = double(h.total_frames()) / h.sample_rate;
        music_ = std::move(stream);
        music_data_ = std::move(data);
        external_music_.reset();
        external_music_pos_ = 0;
        music_finished_ = false;
        stream_level_ = volume;
    }
    SDL_UnlockAudioStream(music_stream_);
    SDL_SetAudioStreamGain(music_stream_, stream_output_gain(volume, music_volume_));
    // A new piece always plays: a pause (the race pause) ends with the piece it paused.
    // Without this, leaving a race from its pause menu left the device paused and the
    // next race's music silent until the next pause / resume.
    SDL_ResumeAudioStreamDevice(music_stream_);
    LOG_INFO("Playing %s (%u Hz, %u ch, %.0f s)", path.c_str(), h.sample_rate, h.channels, music_length_);
    return true;
}

bool AudioEngine::play_external_music(const std::string& path, float volume) {
    if (!music_stream_) return false;
    {
        std::lock_guard lock(external_decode_mutex_);
        ++external_decode_generation_;
        external_decode_request_ = ExternalMusicRequest{path, volume, external_decode_generation_};
        external_decode_result_.reset();
    }
    external_decode_cv_.notify_one();
    return true;
}

void AudioEngine::invalidate_external_decode() {
    std::lock_guard lock(external_decode_mutex_);
    ++external_decode_generation_;
    external_decode_request_.reset();
    external_decode_result_.reset();
}

void AudioEngine::decode_external_music() {
    for (;;) {
        ExternalMusicRequest request;
        {
            std::unique_lock lock(external_decode_mutex_);
            external_decode_cv_.wait(lock, [this] {
                return external_decode_stopping_ || external_decode_request_.has_value();
            });
            if (external_decode_stopping_) return;
            request = std::move(*external_decode_request_);
            external_decode_request_.reset();
        }

        ExternalMusicResult result;
        result.path = request.path;
        result.volume = request.volume;
        result.generation = request.generation;
        std::ifstream file(request.path, std::ios::binary | std::ios::ate);
        if (!file) {
            LOG_ERROR("Cannot open user music %s", request.path.c_str());
            continue;
        }
        const auto file_size = file.tellg();
        if (file_size <= 0 || file_size > std::numeric_limits<int>::max()) {
            LOG_ERROR("Unsupported MP3 size in %s", request.path.c_str());
            continue;
        }
        std::vector<uint8_t> bytes(static_cast<size_t>(file_size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
            LOG_ERROR("Cannot read user music %s", request.path.c_str());
            continue;
        }

        auto decoded = std::make_shared<std::vector<s16>>();
        decoded->reserve(bytes.size() * 4);
        mp3dec_t decoder{};
        mp3dec_init(&decoder);
        mp3dec_frame_info_t info{};
        std::vector<mp3d_sample_t> frame(MINIMP3_MAX_SAMPLES_PER_FRAME);
        size_t offset = 0;
        bool cancelled = false;
        while (offset < bytes.size()) {
            {
                std::lock_guard lock(external_decode_mutex_);
                if (request.generation != external_decode_generation_) {
                    cancelled = true;
                    break;
                }
            }
            const int remaining = static_cast<int>(bytes.size() - offset);
            const int samples = mp3dec_decode_frame(&decoder, bytes.data() + offset, remaining, frame.data(), &info);
            if (info.frame_bytes <= 0) break;
            offset += static_cast<size_t>(info.frame_bytes);
            if (samples <= 0 || info.channels < 1 || info.channels > 2 || info.hz <= 0) continue;
            if (result.sample_rate == 0) result.sample_rate = info.hz;
            if (info.hz != result.sample_rate) {
                LOG_ERROR("MP3 changes sample rate in %s", request.path.c_str());
                decoded->clear();
                break;
            }
            for (int i = 0; i < samples; ++i) {
                const s16 left = static_cast<s16>(frame[static_cast<size_t>(i) * info.channels]);
                const s16 right = info.channels == 2 ? static_cast<s16>(frame[static_cast<size_t>(i) * 2 + 1]) : left;
                decoded->push_back(left);
                decoded->push_back(right);
            }
        }
        if (cancelled) continue;
        if (decoded->empty() || result.sample_rate == 0) {
            LOG_ERROR("No decodable MP3 audio in %s", request.path.c_str());
            continue;
        }
        result.samples = std::move(decoded);
        {
            std::lock_guard lock(external_decode_mutex_);
            if (external_decode_stopping_) return;
            if (request.generation == external_decode_generation_) external_decode_result_ = std::move(result);
        }
    }
}

void AudioEngine::update() {
    std::optional<ExternalMusicResult> result;
    {
        std::lock_guard lock(external_decode_mutex_);
        if (external_decode_result_ && external_decode_result_->generation == external_decode_generation_) {
            result = std::move(external_decode_result_);
            external_decode_result_.reset();
        }
    }
    if (!result || !music_stream_) return;

    // Only the main game thread changes sequencer state; decoding never blocks it.
    sequencer_.stop(voices_);
    SDL_AudioSpec in{SDL_AUDIO_S16, 2, result->sample_rate};
    const double decoded_length = double(result->samples->size() / 2) / result->sample_rate;
    SDL_LockAudioStream(music_stream_);
    {
        std::lock_guard lock(mutex_);
        SDL_ClearAudioStream(music_stream_);
        SDL_SetAudioStreamFormat(music_stream_, &in, nullptr);
        music_.reset();
        music_data_.reset();
        external_music_ = std::move(result->samples);
        external_music_pos_ = 0;
        external_music_rate_ = result->sample_rate;
        music_length_ = decoded_length;
        music_finished_ = false;
        stream_level_ = result->volume;
    }
    SDL_UnlockAudioStream(music_stream_);
    SDL_SetAudioStreamGain(music_stream_, stream_output_gain(result->volume, music_volume_));
    SDL_ResumeAudioStreamDevice(music_stream_);
    LOG_INFO("Playing user music %s", result->path.c_str());
}

void AudioEngine::stop_stream() {
    invalidate_external_decode();
    // Same lock order as play_stream(): stream, then mutex_, and the device call unlocked.
    if (music_stream_) SDL_LockAudioStream(music_stream_);
    {
        std::lock_guard lock(mutex_);
        music_.reset();
        music_data_.reset();
        external_music_.reset();
        external_music_pos_ = 0;
        if (music_stream_) SDL_ClearAudioStream(music_stream_);
    }
    if (music_stream_) {
        SDL_UnlockAudioStream(music_stream_);
        SDL_ResumeAudioStreamDevice(music_stream_); // a stop also ends a pause
    }
}

void AudioEngine::stop_music() {
    stop_stream();
    sequencer_.stop(voices_);
}

void AudioEngine::pause_music() {
    if (music_stream_) SDL_PauseAudioStreamDevice(music_stream_);
}

void AudioEngine::resume_music() {
    if (music_stream_) SDL_ResumeAudioStreamDevice(music_stream_);
}

bool AudioEngine::music_playing() const {
    if (sequencer_.active()) return true;
    std::lock_guard lock(mutex_);
    return (music_ || external_music_) && !music_finished_;
}

double AudioEngine::music_position() const {
    if (sequencer_.active()) return double(sequencer_.updates()) / sequencer_.update_rate();
    std::lock_guard lock(mutex_);
    if (external_music_) return double(external_music_pos_) / std::max(external_music_rate_, 1);
    return music_ ? music_->position_seconds() : 0.0;
}

double AudioEngine::music_audible_position() const {
    if (sequencer_.active() || !music_stream_) return music_position();
    double pos = 0, rate = 0;
    u32 ch = 0;
    {
        std::lock_guard lock(mutex_);
        if (!music_) return 0.0;
        pos = music_->position_seconds();
        rate = music_->header().sample_rate;
        ch = music_->header().channels;
    }
    if (rate <= 0 || ch == 0) return pos;
    // Queued input (S16 at the stream's rate), then the device's own buffer.
    const int queued = SDL_GetAudioStreamQueued(music_stream_);
    if (queued > 0) pos -= double(queued) / (2.0 * ch * rate);
    SDL_AudioSpec spec{};
    int frames = 0;
    if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(music_stream_), &spec, &frames) && frames > 0 && spec.freq > 0)
        pos -= double(frames) / spec.freq;
    return std::max(pos, 0.0);
}

void AudioEngine::set_stream_level(float level) {
    {
        std::lock_guard lock(mutex_);
        if (stream_level_ == level) return;
        stream_level_ = level;
    }
    if (music_stream_) SDL_SetAudioStreamGain(music_stream_, stream_output_gain(level, music_volume_));
}

double AudioEngine::music_length() const {
    if (int cur = sequencer_.current(); cur >= 0 && size_t(cur) < seq_loop_seconds_.size())
        return seq_loop_seconds_[size_t(cur)];
    std::lock_guard lock(mutex_);
    if (external_music_) return double(external_music_->size() / 2) / std::max(external_music_rate_, 1);
    return music_ ? music_length_ : 0.0;
}

void AudioEngine::set_music_volume(float v) {
    music_volume_ = std::clamp(v, 0.0f, 1.0f);
    float level;
    {
        std::lock_guard lock(mutex_);
        level = stream_level_;
    }
    if (music_stream_) SDL_SetAudioStreamGain(music_stream_, stream_output_gain(level, music_volume_));
    voices_.set_bus_gain(VoiceBus::Music, music_volume_);
}

void SDLCALL AudioEngine::feed(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* self = static_cast<AudioEngine*>(user);
    std::lock_guard lock(self->mutex_);
    if (self->external_music_) {
        if (self->music_finished_ || additional <= 0) return;
        size_t frames = size_t(additional) / 4 + 1;
        size_t left = self->external_music_->size() / 2 - self->external_music_pos_;
        size_t take = std::min(frames, left);
        if (take == 0) { self->music_finished_ = true; return; }
        const s16* data = self->external_music_->data() + self->external_music_pos_ * 2;
        SDL_PutAudioStreamData(stream, data, int(take * 2 * sizeof(s16)));
        self->external_music_pos_ += take;
        return;
    }
    if (!self->music_ || self->music_finished_ || additional <= 0) return;
    const u32 ch = self->music_->header().channels;
    size_t frames = size_t(additional) / (2 * ch) + 1;
    std::vector<s16> buf(frames * ch);
    try {
        size_t got = self->music_->read(buf.data(), frames);
        if (got == 0) self->music_finished_ = true;
        else SDL_PutAudioStreamData(stream, buf.data(), int(got * ch * sizeof(s16)));
    } catch (const std::exception& ex) {
        LOG_ERROR("Music stream error: %s", ex.what());
        self->music_finished_ = true;
    }
}

} // namespace gt3r
