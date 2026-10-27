#pragma once

#include "audio/bgm.h"
#include "audio/sequencer.h"
#include "audio/voice_mixer.h"
#include "formats/ads.h"
#include "formats/sound/seq.h"

#include <SDL3/SDL_audio.h>

#include <memory>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gt3r {

class Vfs;

// Audio output: streamed music (.ads) plus the SPU2-style voice mixer used
// for engine notes, sound effects and the sequenced menu music. The stream
// and the mixer each run on their own SDL audio stream. The mixer starts in
// the game's boot state: reverb "studio C" with return volume 0x0FFF.
// Background music is either a stream or a sequence; starting one stops the
// other, and the music_* queries cover both.
// Volumes (music §6, sound §1.1): each music request has a level (bgm.h). A
// stream plays at level x the Music volume (x the mixer's output trim, like
// every voice: the game plays its streams on SPU2 voices); a sequence sets the sequencer's
// music volume from its level, and its voices play on the mixer's Music bus,
// whose gain is the Music volume. The Sound effects volume is the gain of the
// mixer's Effects bus only.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool ok() const { return music_stream_ != nullptr; }

    // Streams an .ads file through the VFS (mods may replace it) at request level `volume`.
    bool play_music(Vfs& vfs, const std::string& path, bool loop, float volume = 1.0f);
    // Plays a user-provided MP3 file.
    bool play_external_music(const std::string& path, float volume = 1.0f);
    // Apply any MP3 that finished decoding on the background worker. Call once per frame.
    void update();
    // Plays sequence `index` of music/music.seq with music/music.ins (loaded
    // on first use through the VFS) at request level `volume`. Sequences loop forever.
    bool play_sequence(Vfs& vfs, int index, float volume = 1.0f);
    // Same, by music.inf name (e.g. "gtmenu01", the ids the game uses).
    // A music.inf name, or "bgm:<id>[:once]" for a BGM-layer cue (bgm::Player).
    bool play_sequence(Vfs& vfs, const std::string& name, float volume = 1.0f);
    // music.inf entries (loads the sequenced music if needed; empty if absent).
    const std::vector<sound::MusicEntry>& music_index(Vfs& vfs);
    bool sequence_playing() const { return sequencer_.active(); }
    Sequencer& sequencer() { return sequencer_; }
    // The game's BGM layer (ids, restart rules). Shared
    // by menus, race and presentations so "play if different" sees one state.
    bgm::Player& bgm() { return bgm_; }

    void stop_music(); // stream and sequence
    // Pauses / resumes the streamed music where it is (the race pause).
    void pause_music();
    void resume_music();
    bool music_playing() const;
    double music_position() const;
    // The streamed piece's position as heard (seconds): the decoded position less what still
    // waits in the output stream and one device buffer; 0 without a stream. The replay
    // cameras time their cuts on it (music §8).
    double music_audible_position() const;
    // The current stream's request level, changed without restarting it (the race music's
    // per-frame level, music §2.1).
    void set_stream_level(float level);
    double music_length() const; // stream length; for a sequence, one pass of its loop
    // The Music volume setting (0..1): streams and the mixer's music bus.
    void set_music_volume(float v);
    // A music stream's output gain: its request level x the Music volume x the voice mix's
    // output trim. The game streams music through SPU2 voices, so music and effects meet at
    // equal weight; the shared trim keeps that balance (sound §2.4).
    static float stream_output_gain(float level, float music_volume) {
        return bgm::stream_gain(level, music_volume) * VoiceMixer::kOutputTrim;
    }
    float music_volume() const { return music_volume_; }

    // Voice mixer for sound effects and engine notes (thread-safe).
    VoiceMixer& voices() { return voices_; }
    // The Sound effects volume setting (0..1): the mixer's effects bus, never the music.
    void set_sfx_volume(float v);
    float sfx_volume() const { return sfx_volume_; }

private:
    bool load_sequences(Vfs& vfs);
    void stop_stream();
    void invalidate_external_decode();
    void decode_external_music();

    struct ExternalMusicRequest {
        std::string path;
        float volume = 1.0f;
        u64 generation = 0;
    };
    struct ExternalMusicResult {
        std::string path;
        float volume = 1.0f;
        u64 generation = 0;
        std::shared_ptr<std::vector<s16>> samples;
        int sample_rate = 0;
    };

    static void SDLCALL feed(void* user, SDL_AudioStream* stream, int additional, int total);
    static void SDLCALL feed_voices(void* user, SDL_AudioStream* stream, int additional, int total);

    SDL_AudioStream* music_stream_ = nullptr;
    mutable std::mutex mutex_;
    std::unique_ptr<AdsStream> music_;
    std::shared_ptr<Bytes> music_data_; // backing store when not streamed from disc
    std::shared_ptr<std::vector<s16>> external_music_; // decoded user track, stereo 44.1 kHz
    size_t external_music_pos_ = 0;
    int external_music_rate_ = 44100;
    double music_length_ = 0.0;
    float music_volume_ = 0.8f;
    float stream_level_ = 1.0f; // the current stream request's level
    bool music_finished_ = false;

    std::mutex external_decode_mutex_;
    std::condition_variable external_decode_cv_;
    std::optional<ExternalMusicRequest> external_decode_request_;
    std::optional<ExternalMusicResult> external_decode_result_;
    u64 external_decode_generation_ = 0;
    bool external_decode_stopping_ = false;
    std::thread external_decode_thread_;

    VoiceMixer voices_;
    Sequencer sequencer_;
    bgm::Player bgm_;
    std::vector<sound::MusicEntry> music_index_;
    std::shared_ptr<const sound::SeqBank> seqs_;
    std::vector<double> seq_loop_seconds_;
    bool seq_load_tried_ = false;
    SDL_AudioStream* sfx_stream_ = nullptr;
    float sfx_volume_ = 0.8f;
};

} // namespace gt3r
