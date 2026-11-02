#include "net/voice_chat.h"

#include "base/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace gt3r::net {

// G.711 μ-law (standard bit-inverted form used by telephony).
u8 linear_to_mulaw(int16_t sample) {
    const int sign = (sample >> 8) & 0x80;
    if (sign) sample = static_cast<int16_t>(-sample);
    if (sample > 32635) sample = 32635;
    sample = static_cast<int16_t>(sample + 0x84);
    int exp = 7;
    for (int mask = 0x4000; (sample & mask) == 0 && exp > 0; mask >>= 1) --exp;
    const int mant = (sample >> (exp + 3)) & 0x0F;
    return static_cast<u8>(~(sign | (exp << 4) | mant));
}

int16_t mulaw_to_linear(u8 mulaw) {
    mulaw = static_cast<u8>(~mulaw);
    const int sign = mulaw & 0x80;
    const int exp = (mulaw >> 4) & 0x07;
    int quant = ((mulaw & 0x0F) << 3) + 0x84;
    quant <<= exp;
    quant -= 0x84;
    return static_cast<int16_t>(sign ? -quant : quant);
}

VoiceChat::VoiceChat() = default;

VoiceChat::~VoiceChat() { stop(); }

bool VoiceChat::start() {
    if (active_) return true;

    SDL_AudioSpec spec{};
    spec.freq = kVoiceSampleRate;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;

    capture_ = SDL_CreateAudioStream(&spec, &spec);
    playback_ = SDL_CreateAudioStream(&spec, &spec);
    if (!capture_ || !playback_) {
        LOG_ERROR("VoiceChat: CreateAudioStream failed: %s", SDL_GetError());
        stop();
        return false;
    }

    if (!SDL_SetAudioStreamGetCallback(playback_, &VoiceChat::playback_callback, this)) {
        LOG_ERROR("VoiceChat: playback callback failed: %s", SDL_GetError());
        stop();
        return false;
    }

    // Capture: open default recording device and bind the stream.
    capture_dev_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec);
    if (!capture_dev_) {
        LOG_ERROR("VoiceChat: no recording device: %s", SDL_GetError());
        // Still allow playback-only (hear others even if we have no mic).
    } else if (!SDL_BindAudioStream(capture_dev_, capture_)) {
        LOG_ERROR("VoiceChat: bind capture failed: %s", SDL_GetError());
        SDL_CloseAudioDevice(capture_dev_);
        capture_dev_ = 0;
    } else {
        SDL_ResumeAudioDevice(capture_dev_);
    }

    playback_dev_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!playback_dev_) {
        LOG_ERROR("VoiceChat: no playback device: %s", SDL_GetError());
        stop();
        return false;
    }
    if (!SDL_BindAudioStream(playback_dev_, playback_)) {
        LOG_ERROR("VoiceChat: bind playback failed: %s", SDL_GetError());
        stop();
        return false;
    }
    SDL_ResumeAudioDevice(playback_dev_);

    active_ = true;
    vad_hangover_frames_ = 0;
    LOG_INFO("VoiceChat: started (mic %s, open-mic %s, 8 kHz μ-law, proximity %.0f–%.0f m)",
             capture_dev_ ? "on" : "off", open_mic_ ? "on" : "off", double(kVoiceFullDistance),
             double(kVoiceMaxDistance));
    return true;
}

void VoiceChat::stop() {
    active_ = false;
    talking_ = false;
    if (capture_dev_) {
        SDL_CloseAudioDevice(capture_dev_);
        capture_dev_ = 0;
    }
    if (playback_dev_) {
        SDL_CloseAudioDevice(playback_dev_);
        playback_dev_ = 0;
    }
    if (capture_) {
        SDL_DestroyAudioStream(capture_);
        capture_ = nullptr;
    }
    if (playback_) {
        SDL_DestroyAudioStream(playback_);
        playback_ = nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    capture_pcm_.clear();
    outgoing_.clear();
    vad_hangover_frames_ = 0;
    for (Remote& r : remotes_) {
        r.pcm.clear();
        r.gain = 0;
        r.last_seq = 0xFF;
    }
}

std::vector<VoiceFrame> VoiceChat::take_outgoing() {
    if (active_ && transmitting() && capture_) {
        encode_pending();
    } else if (active_ && capture_ && !transmitting()) {
        // Drain mic so the stream does not backlog while muted / not talking.
        const int avail = SDL_GetAudioStreamAvailable(capture_);
        if (avail > 0) {
            std::vector<u8> dump(static_cast<size_t>(avail));
            (void)SDL_GetAudioStreamData(capture_, static_cast<void*>(dump.data()), avail);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        capture_pcm_.clear();
        vad_hangover_frames_ = 0;
    }

    std::vector<VoiceFrame> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        out.swap(outgoing_);
    }
    return out;
}

void VoiceChat::encode_pending() {
    if (!capture_) return;

    const int avail = SDL_GetAudioStreamAvailable(capture_);
    if (avail < static_cast<int>(sizeof(float))) return;

    const int samples = avail / static_cast<int>(sizeof(float));
    if (samples <= 0) return;

    std::vector<float> chunk(static_cast<size_t>(samples));
    const int nbytes = samples * static_cast<int>(sizeof(float));
    const int got = SDL_GetAudioStreamData(capture_, static_cast<void*>(chunk.data()), nbytes);
    if (got <= 0) return;

    const int n = got / static_cast<int>(sizeof(float));
    if (n <= 0) return;

    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < n; ++i) capture_pcm_.push_back(chunk[static_cast<size_t>(i)]);

    const bool force = talking_; // PTT forces send even if VAD would drop
    while (capture_pcm_.size() >= static_cast<size_t>(kVoiceFrameSamples)) {
        double acc = 0.0;
        for (int i = 0; i < kVoiceFrameSamples; ++i) {
            const float s = std::clamp(capture_pcm_[static_cast<size_t>(i)], -1.0f, 1.0f);
            acc += static_cast<double>(s) * static_cast<double>(s);
        }
        const float rms = static_cast<float>(std::sqrt(acc / static_cast<double>(kVoiceFrameSamples)));
        const bool loud = rms >= vad_threshold_;
        if (loud) vad_hangover_frames_ = kVadHangover;
        else if (vad_hangover_frames_ > 0) --vad_hangover_frames_;

        // Open-mic: send only when voice is active (or hangover). PTT: always send while held.
        const bool send = force || (open_mic_ && (loud || vad_hangover_frames_ > 0));
        if (send) {
            VoiceFrame f;
            f.seq = out_seq_++;
            for (int i = 0; i < kVoiceFrameSamples; ++i) {
                float s = capture_pcm_[static_cast<size_t>(i)] * kVoiceCaptureBoost;
                s = std::clamp(s, -1.0f, 1.0f);
                f.data[static_cast<size_t>(i)] = linear_to_mulaw(static_cast<int16_t>(s * 32767.0f));
            }
            outgoing_.push_back(f);
            if (outgoing_.size() > 8) {
                outgoing_.erase(outgoing_.begin(), outgoing_.begin() + static_cast<std::ptrdiff_t>(outgoing_.size() - 8));
            }
        }
        capture_pcm_.erase(capture_pcm_.begin(),
                           capture_pcm_.begin() + static_cast<std::ptrdiff_t>(kVoiceFrameSamples));
    }
}

void VoiceChat::push_remote(u8 player, u8 seq, ByteSpan mulaw) {
    if (player >= kVoiceMaxPlayers || mulaw.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    Remote& r = remotes_[player];
    r.last_seq = seq;
    for (size_t i = 0; i < mulaw.size(); ++i) {
        const float s = static_cast<float>(mulaw_to_linear(mulaw[i])) / 32768.0f;
        r.pcm.push_back(s);
    }
    // ~0.5 s backlog max per player.
    constexpr size_t kMax = static_cast<size_t>(kVoiceSampleRate / 2);
    if (r.pcm.size() > kMax) {
        r.pcm.erase(r.pcm.begin(), r.pcm.end() - static_cast<std::ptrdiff_t>(kMax));
    }
}

void VoiceChat::set_gain(u8 player, float gain) {
    if (player >= kVoiceMaxPlayers) return;
    std::lock_guard<std::mutex> lock(mutex_);
    remotes_[player].gain = std::clamp(gain, 0.0f, 1.0f);
}

void VoiceChat::mix_playback(float* out, int frames) {
    if (!out || frames <= 0) return;
    std::memset(out, 0, static_cast<size_t>(frames) * sizeof(float));
    std::lock_guard<std::mutex> lock(mutex_);
    const float master = volume_ * kVoicePlaybackBoost;
    for (Remote& r : remotes_) {
        if (r.gain <= 0.001f || r.pcm.empty()) continue;
        const float g = r.gain * master;
        const int n = std::min(frames, static_cast<int>(r.pcm.size()));
        for (int i = 0; i < n; ++i) out[i] += r.pcm[static_cast<size_t>(i)] * g;
        r.pcm.erase(r.pcm.begin(), r.pcm.begin() + static_cast<std::ptrdiff_t>(n));
    }
    for (int i = 0; i < frames; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

void SDLCALL VoiceChat::capture_callback(void* /*user*/, SDL_AudioStream* /*stream*/, int /*additional*/,
                                         int /*total*/) {
    // Capture is pulled from the game thread in encode_pending().
}

void SDLCALL VoiceChat::playback_callback(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto* self = static_cast<VoiceChat*>(user);
    if (!self || !stream || additional <= 0) return;
    const int frames = additional / static_cast<int>(sizeof(float));
    if (frames <= 0) return;
    std::vector<float> buf(static_cast<size_t>(frames));
    self->mix_playback(buf.data(), frames);
    (void)SDL_PutAudioStreamData(stream, static_cast<const void*>(buf.data()),
                                 frames * static_cast<int>(sizeof(float)));
}

} // namespace gt3r::net
