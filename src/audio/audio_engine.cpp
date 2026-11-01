#include "audio/audio_engine.h"

#include "audio/music_file.h"
#include "base/log.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace gt3r {

namespace {

// The music stream's input format is set per track; the voices are the mixer's own.
// Output adapts to the device.
constexpr SDL_AudioSpec kMusicSpec{SDL_AUDIO_S16, 2, 44100};
constexpr SDL_AudioSpec kVoiceSpec{SDL_AUDIO_S16, 2, int(VoiceMixer::kRate)};

// The connected playback device called `name`, or the default.
SDL_AudioDeviceID find_playback_device(const std::string& name) {
    SDL_AudioDeviceID found = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    if (name.empty()) return found;
    int n = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&n);
    for (int i = 0; i < n && found == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK; ++i)
        if (const char* d = SDL_GetAudioDeviceName(ids[i]); d && name == d) found = ids[i];
    SDL_free(ids);
    return found;
}

// The game's .ads streams as music.
class AdsMusic : public MusicStream {
public:
    explicit AdsMusic(std::unique_ptr<AdsStream> s) : s_(std::move(s)) {}
    u32 sample_rate() const override { return s_->header().sample_rate; }
    u32 channels() const override { return s_->header().channels; }
    u64 total_frames() const override { return s_->header().total_frames(); }
    size_t read(s16* out, size_t frames) override { return s_->read(out, frames); }
    double position_seconds() const override { return s_->position_seconds(); }

private:
    std::unique_ptr<AdsStream> s_;
};

// Opens a device for each stream on physical device `phys`; false (none left open) if either fails.
bool open_pair(SDL_AudioDeviceID phys, SDL_AudioDeviceID& music, SDL_AudioDeviceID& voices) {
    music = SDL_OpenAudioDevice(phys, &kMusicSpec);
    voices = music ? SDL_OpenAudioDevice(phys, &kVoiceSpec) : 0;
    if (voices) return true;
    if (music) SDL_CloseAudioDevice(music);
    return false;
}

// Moves `stream` onto the opened device `to`; returns the device it was on, still open. A paused
// device (the race pause) leaves the new one paused.
SDL_AudioDeviceID move_stream(SDL_AudioStream* stream, SDL_AudioDeviceID to) {
    const SDL_AudioDeviceID from = SDL_GetAudioStreamDevice(stream);
    if (from && SDL_AudioDevicePaused(from)) SDL_PauseAudioDevice(to);
    SDL_UnbindAudioStream(stream);
    if (!SDL_BindAudioStream(to, stream)) LOG_ERROR("Cannot move an audio stream: %s", SDL_GetError());
    return from;
}

} // namespace

AudioEngine::AudioEngine(const std::string& device) : device_name_(device) {
    // The game's sound init: effect mode 4 on both cores, EVOL 0x0FFF.
    voices_.set_reverb(SpuReverb::StudioC, 0x0FFF, 0x0FFF);
    voices_.set_bus_gain(VoiceBus::Music, music_volume_);
    voices_.set_bus_gain(VoiceBus::Effects, sfx_volume_);
    // The sequencer runs inside the mixer at the game's 60 Hz, sample-exact.
    voices_.set_tick_hook([this] { sequencer_.update(voices_); }, sequencer_.samples_per_update());

    // Each stream has a device of its own (opened on the same physical device), so the race
    // pause can pause the music alone.
    music_stream_ = SDL_CreateAudioStream(&kMusicSpec, &kMusicSpec);
    sfx_stream_ = SDL_CreateAudioStream(&kVoiceSpec, &kVoiceSpec);
    if (!music_stream_ || !sfx_stream_ || !SDL_SetAudioStreamGetCallback(music_stream_, &AudioEngine::feed, this) ||
        !SDL_SetAudioStreamGetCallback(sfx_stream_, &AudioEngine::feed_voices, this) ||
        !use_device(find_playback_device(device_name_))) {
        LOG_ERROR("Audio unavailable: %s", SDL_GetError());
        if (sfx_stream_) SDL_DestroyAudioStream(sfx_stream_);
        if (music_stream_) SDL_DestroyAudioStream(music_stream_);
        music_stream_ = sfx_stream_ = nullptr;
        return;
    }
    SDL_SetAudioStreamGain(music_stream_, stream_output_gain(stream_level_, music_volume_) * stream_trim_);
    // The voices play at unity gain: the volumes are the mixer's bus gains.
}

AudioEngine::~AudioEngine() {
    {
        std::lock_guard lock(switch_mutex_);
        switch_quit_ = true;
    }
    switch_cv_.notify_one();
    if (switcher_.joinable()) switcher_.join();
    for (SDL_AudioStream* s : {sfx_stream_, music_stream_}) {
        if (!s) continue;
        const SDL_AudioDeviceID dev = SDL_GetAudioStreamDevice(s);
        SDL_DestroyAudioStream(s);
        if (dev) SDL_CloseAudioDevice(dev);
    }
    voices_.set_tick_hook({}, 0);
}

bool AudioEngine::use_device(SDL_AudioDeviceID phys) {
    // Both devices open before either stream moves, so a failure leaves the sound where it was.
    SDL_AudioDeviceID music = 0, voices = 0;
    if (!open_pair(phys, music, voices)) {
        LOG_WARN("Cannot open audio device: %s", SDL_GetError());
        if (phys == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK) return false;
        phys = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
        if (phys == device_) return true; // already there
        if (!open_pair(phys, music, voices)) return false;
    }
    SDL_AudioDeviceID old_music = 0, old_voices = 0;
    {
        std::lock_guard lock(device_mutex_);
        old_music = move_stream(music_stream_, music);
        old_voices = move_stream(sfx_stream_, voices);
        device_ = phys;
    }
    if (old_music) SDL_CloseAudioDevice(old_music);
    if (old_voices) SDL_CloseAudioDevice(old_voices);
    const char* name = SDL_GetAudioDeviceName(phys);
    LOG_INFO("Audio output: %s%s", phys == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK ? "system default, " : "",
             name ? name : "?");
    return true;
}

void AudioEngine::set_output_device(const std::string& name) {
    device_name_ = name;
    request_switch();
}

void AudioEngine::device_event(const SDL_Event& e) {
    if (device_name_.empty() || e.adevice.recording) return; // the default follows the system by itself
    request_switch();
}

void AudioEngine::request_switch() {
    if (!music_stream_) return;
    std::lock_guard lock(switch_mutex_);
    switch_name_ = device_name_;
    switch_requested_ = true;
    if (!switcher_.joinable()) switcher_ = std::thread([this] { run_switcher(); });
    switch_cv_.notify_one();
}

void AudioEngine::run_switcher() {
    std::unique_lock lock(switch_mutex_);
    for (;;) {
        switch_cv_.wait(lock, [this] { return switch_requested_ || switch_quit_; });
        if (switch_quit_) return;
        switch_requested_ = false; // requests made meanwhile collapse into the latest
        const std::string name = switch_name_;
        lock.unlock();
        // A reconnected device has a new id; a lost one is no longer listed.
        const SDL_AudioDeviceID want = find_playback_device(name);
        if (want != device_) use_device(want);
        lock.lock();
    }
}

std::vector<std::string> AudioEngine::playback_devices() {
    std::vector<std::string> names;
    int n = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&n);
    for (int i = 0; i < n; ++i)
        if (const char* d = SDL_GetAudioDeviceName(ids[i]); d && *d && std::find(names.begin(), names.end(), d) == names.end())
            names.emplace_back(d);
    SDL_free(ids);
    return names;
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

bool AudioEngine::play_music(Vfs& vfs, const std::string& path, bool loop, float volume, bool resume) {
    if (!music_stream_) return false;
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
    start_stream(std::make_unique<AdsMusic>(std::move(stream)), std::move(data), volume, resume, path);
    return true;
}

bool AudioEngine::play_music_file(const std::string& path, bool loop, float volume, bool resume, float trim) {
    if (!music_stream_) return false;
    std::unique_ptr<MusicStream> stream = open_music_file(path, loop);
    if (!stream) return false;
    start_stream(std::move(stream), nullptr, volume, resume, path, trim);
    return true;
}

std::unique_ptr<MusicStream> open_ads_music(AdsStream::ReadFn read) {
    Bytes head(AdsHeader::kHeaderBytes);
    read(0, head.data(), head.size());
    return std::make_unique<AdsMusic>(std::make_unique<AdsStream>(AdsHeader::parse(head), std::move(read), false));
}

void AudioEngine::start_stream(std::unique_ptr<MusicStream> stream, std::shared_ptr<Bytes> data, float volume,
                               bool resume, const std::string& what, float trim) {
    sequencer_.stop(voices_);
    const u32 rate = stream->sample_rate(), channels = stream->channels();
    SDL_AudioSpec in{SDL_AUDIO_S16, int(channels), int(rate)};
    const double length = rate ? double(stream->total_frames()) / rate : 0.0;
    // Lock order matches the audio thread's: SDL device -> SDL stream -> mutex_ (feed() runs
    // with the stream lock held). So the stream is locked before mutex_, and the device call
    // below is made with neither held; otherwise the two threads can deadlock.
    SDL_LockAudioStream(music_stream_);
    {
        std::lock_guard lock(mutex_);
        SDL_ClearAudioStream(music_stream_);
        SDL_SetAudioStreamFormat(music_stream_, &in, nullptr);
        music_length_ = length;
        music_ = std::move(stream);
        music_data_ = std::move(data);
        music_finished_ = false;
        stream_level_ = volume;
        stream_trim_ = trim;
    }
    SDL_UnlockAudioStream(music_stream_);
    SDL_SetAudioStreamGain(music_stream_, stream_output_gain(volume, music_volume_) * trim);
    // A new piece plays at once: a pause (the race pause) ends with the piece it paused.
    // Without this, leaving a race from its pause menu left the device paused and the
    // next race's music silent until the next pause / resume. Only a piece chosen while the
    // race is paused (a skip on the pause screen) waits for the race's resume.
    if (resume) {
        std::lock_guard dl(device_mutex_);
        SDL_ResumeAudioStreamDevice(music_stream_);
    }
    if (trim != 1.0f)
        LOG_INFO("Playing %s (%u Hz, %u ch, %.0f s, %+.1f dB)", what.c_str(), rate, channels, length,
                 20.0 * std::log10(double(trim)));
    else
        LOG_INFO("Playing %s (%u Hz, %u ch, %.0f s)", what.c_str(), rate, channels, length);
}

void AudioEngine::stop_stream() {
    // Same lock order as play_stream(): stream, then mutex_, and the device call unlocked.
    if (music_stream_) SDL_LockAudioStream(music_stream_);
    {
        std::lock_guard lock(mutex_);
        music_.reset();
        music_data_.reset();
        if (music_stream_) SDL_ClearAudioStream(music_stream_);
    }
    if (music_stream_) {
        SDL_UnlockAudioStream(music_stream_);
        std::lock_guard dl(device_mutex_);
        SDL_ResumeAudioStreamDevice(music_stream_); // a stop also ends a pause
    }
}

void AudioEngine::stop_music() {
    stop_stream();
    sequencer_.stop(voices_);
}

void AudioEngine::pause_music() {
    std::lock_guard dl(device_mutex_);
    if (music_stream_) SDL_PauseAudioStreamDevice(music_stream_);
}

void AudioEngine::resume_music() {
    std::lock_guard dl(device_mutex_);
    if (music_stream_) SDL_ResumeAudioStreamDevice(music_stream_);
}

bool AudioEngine::music_playing() const {
    if (sequencer_.active()) return true;
    std::lock_guard lock(mutex_);
    return music_ && !music_finished_;
}

double AudioEngine::music_position() const {
    if (sequencer_.active()) return double(sequencer_.updates()) / sequencer_.update_rate();
    std::lock_guard lock(mutex_);
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
        rate = music_->sample_rate();
        ch = music_->channels();
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
    float trim;
    {
        std::lock_guard lock(mutex_);
        if (stream_level_ == level) return;
        stream_level_ = level;
        trim = stream_trim_;
    }
    if (music_stream_) SDL_SetAudioStreamGain(music_stream_, stream_output_gain(level, music_volume_) * trim);
}

void AudioEngine::set_stream_trim(float trim) {
    float level;
    {
        std::lock_guard lock(mutex_);
        if (stream_trim_ == trim) return;
        stream_trim_ = trim;
        level = stream_level_;
    }
    if (music_stream_) SDL_SetAudioStreamGain(music_stream_, stream_output_gain(level, music_volume_) * trim);
}

double AudioEngine::music_length() const {
    if (int cur = sequencer_.current(); cur >= 0 && size_t(cur) < seq_loop_seconds_.size())
        return seq_loop_seconds_[size_t(cur)];
    std::lock_guard lock(mutex_);
    return music_ ? music_length_ : 0.0;
}

void AudioEngine::set_music_volume(float v) {
    music_volume_ = std::clamp(v, 0.0f, 1.0f);
    float level, trim;
    {
        std::lock_guard lock(mutex_);
        level = stream_level_;
        trim = stream_trim_;
    }
    if (music_stream_) SDL_SetAudioStreamGain(music_stream_, stream_output_gain(level, music_volume_) * trim);
    voices_.set_bus_gain(VoiceBus::Music, music_volume_);
}

void SDLCALL AudioEngine::feed(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* self = static_cast<AudioEngine*>(user);
    std::lock_guard lock(self->mutex_);
    if (!self->music_ || self->music_finished_ || additional <= 0) return;
    const u32 ch = self->music_->channels();
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
