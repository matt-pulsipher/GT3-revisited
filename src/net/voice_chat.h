#pragma once

// Proximity voice chat for online races.
//
// Capture: mono 8 kHz → μ-law frames → Session::send_voice (unreliable, host-relayed).
// Modes: open-mic (always on, with voice-activity detection) or push-to-talk.
// Playback: decode remote frames, attenuate by distance to that player's car, mix to an SDL stream.
//
// No external codec dependency. Bandwidth is ~8 kB/s per talking player (one 160-byte frame / 20 ms).

#include "base/bytes.h"

#include <SDL3/SDL_audio.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

namespace gt3r::net {

constexpr int kVoiceSampleRate = 8000;
constexpr int kVoiceFrameSamples = 160; // 20 ms
constexpr int kVoiceMaxPlayers = 6;
constexpr float kVoiceFullDistance = 15.0f; // metres: full volume (race)
constexpr float kVoiceMaxDistance = 80.0f;  // metres: silence beyond this (race)
// Playback is soft at unity (μ-law + separate SDL device); boost so 100% is clearly audible.
constexpr float kVoicePlaybackBoost = 8.0f;
// Capture pre-gain before μ-law so quiet mics still reach full code range.
constexpr float kVoiceCaptureBoost = 2.5f;

// One encoded frame ready to send (μ-law, kVoiceFrameSamples bytes).
struct VoiceFrame {
    u8 seq = 0;
    std::array<u8, kVoiceFrameSamples> data{};
};

class VoiceChat {
public:
    VoiceChat();
    ~VoiceChat();

    VoiceChat(const VoiceChat&) = delete;
    VoiceChat& operator=(const VoiceChat&) = delete;

    // Opens capture (default mic) and a low-latency playback stream. Safe to call again.
    bool start();
    void stop();
    bool active() const { return active_; }

    // Open-mic: transmit continuously (quiet frames dropped by VAD). When off, use push-to-talk.
    void set_open_mic(bool on) { open_mic_ = on; }
    bool open_mic() const { return open_mic_; }

    // Mute blocks all outgoing audio (both open-mic and PTT).
    void set_muted(bool on) { muted_ = on; }
    bool muted() const { return muted_; }

    // Push-to-talk: while true (and not muted), mic frames are encoded. Ignored for gate when open-mic is on
    // except that holding PTT still forces transmit even if VAD would drop the frame.
    void set_talking(bool on) { talking_ = on; }
    bool talking() const { return talking_; }

    // True when we are currently allowed to send (not muted, and open-mic or PTT held).
    bool transmitting() const { return active_ && !muted_ && (open_mic_ || talking_); }

    // Drain encoded frames produced while transmitting (call after input / each frame).
    std::vector<VoiceFrame> take_outgoing();

    // Queue a received μ-law frame from `player` (0..kMaxPlayers-1).
    void push_remote(u8 player, u8 seq, ByteSpan mulaw);

    // Linear gain for that player this frame (0 = mute, 1 = full). Updated from race positions.
    void set_gain(u8 player, float gain);

    // Master voice volume (0..1), separate from game SFX.
    void set_volume(float v) { volume_ = v < 0 ? 0 : v > 1 ? 1 : v; }
    float volume() const { return volume_; }

private:
    static void SDLCALL capture_callback(void* user, SDL_AudioStream* stream, int additional, int total);
    static void SDLCALL playback_callback(void* user, SDL_AudioStream* stream, int additional, int total);

    void encode_pending();
    void mix_playback(float* out, int frames);

    bool active_ = false;
    bool open_mic_ = true;  // default: open-mic with VAD
    bool muted_ = false;
    bool talking_ = false;  // PTT held
    float volume_ = 1.0f;
    u8 out_seq_ = 0;
    // Open-mic VAD: RMS threshold and hangover so word tails are not clipped.
    float vad_threshold_ = 0.012f; // slightly sensitive so quieter speech still opens the mic
    int vad_hangover_frames_ = 0;
    static constexpr int kVadHangover = 20; // ~400 ms after last loud frame

    SDL_AudioStream* capture_ = nullptr;
    SDL_AudioStream* playback_ = nullptr;
    SDL_AudioDeviceID capture_dev_ = 0;
    SDL_AudioDeviceID playback_dev_ = 0;

    std::mutex mutex_;
    std::vector<float> capture_pcm_; // mono float pending encode
    std::vector<VoiceFrame> outgoing_;

    struct Remote {
        float gain = 0;
        u8 last_seq = 0xFF;
        std::vector<float> pcm; // decoded, awaiting mix
    };
    std::array<Remote, kVoiceMaxPlayers> remotes_{};
};

// ITU-T G.711 μ-law helpers (public for tests).
u8 linear_to_mulaw(int16_t sample);
int16_t mulaw_to_linear(u8 mulaw);

} // namespace gt3r::net
