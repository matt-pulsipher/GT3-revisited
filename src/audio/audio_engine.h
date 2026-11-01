#pragma once

#include "audio/bgm.h"
#include "audio/music_stream.h"
#include "audio/sequencer.h"
#include "audio/voice_mixer.h"
#include "formats/ads.h"
#include "formats/sound/seq.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_events.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
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
// Output device (sound §1.1): both streams play on the chosen playback device, or the system
// default when none is chosen or the chosen one is not connected.
// An .ads stream read through `read` (offsets from the file's start; a Vfs::Stream's reader),
// once through, as a MusicStream: the race songs' loudness is measured through it
// (race/custom_music.h). Throws FormatError when it is no .ads.
std::unique_ptr<MusicStream> open_ads_music(AdsStream::ReadFn read);

class AudioEngine {
public:
    // `device`: the output device's name (config [audio] device); empty for the system default.
    explicit AudioEngine(const std::string& device = {});
    ~AudioEngine();

    bool ok() const { return music_stream_ != nullptr; }

    // The output device by name, empty for the system default (which follows the system's own
    // choice). Moves both streams at once; the music keeps playing (or stays paused).
    void set_output_device(const std::string& name);
    const std::string& output_device() const { return device_name_; }
    // The physical device in use: the chosen one if connected, else SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK.
    // A change lands a moment later: devices open and close on a thread of their own (a
    // CoreAudio device takes up to ~300 ms), so the game never waits for one.
    SDL_AudioDeviceID output_device_id() const {
        const SDL_AudioDeviceID d = device_;
        return d ? d : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    }
    // The connected playback devices' names, in SDL's order, without repeats.
    static std::vector<std::string> playback_devices();
    // SDL_EVENT_AUDIO_DEVICE_ADDED / _REMOVED: the sound moves to the chosen device when it
    // is plugged in and back to the default when it goes.
    void device_event(const SDL_Event& e);

    // Streams an .ads file through the VFS (mods may replace it) at request level `volume`.
    // `resume` false: a race paused on its pause screen stays silent (the new piece waits for
    // resume_music()); otherwise a new piece always plays.
    bool play_music(Vfs& vfs, const std::string& path, bool loop, float volume = 1.0f, bool resume = true);
    // The same for one of the player's own music files (a host path; music_file.h), its samples
    // scaled by `trim` (its loudness gain, race/custom_music.h) on top of the level.
    bool play_music_file(const std::string& path, bool loop, float volume = 1.0f, bool resume = true, float trim = 1.0f);
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
    const bgm::Player& bgm() const { return bgm_; }

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
    // The current stream's own gain (play_music_file's trim), changed while it plays.
    void set_stream_trim(float trim);
    float stream_trim() const {
        std::lock_guard lock(mutex_);
        return stream_trim_;
    }
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
    void start_stream(std::unique_ptr<MusicStream> stream, std::shared_ptr<Bytes> data, float volume, bool resume,
                      const std::string& what, float trim = 1.0f);
    void stop_stream();
    // Puts both streams on physical device `phys`, else on the default; false if neither opens.
    bool use_device(SDL_AudioDeviceID phys);
    // Wakes the switcher thread (started on first use) to put the sound on device_name_.
    void request_switch();
    void run_switcher();

    static void SDLCALL feed(void* user, SDL_AudioStream* stream, int additional, int total);
    static void SDLCALL feed_voices(void* user, SDL_AudioStream* stream, int additional, int total);

    SDL_AudioStream* music_stream_ = nullptr;
    mutable std::mutex mutex_;
    std::unique_ptr<MusicStream> music_;
    std::shared_ptr<Bytes> music_data_; // backing store when not streamed from disc
    double music_length_ = 0.0;
    float music_volume_ = 0.8f;
    float stream_level_ = 1.0f; // the current stream request's level
    float stream_trim_ = 1.0f;  // the current stream's own gain (a music file's loudness)
    bool music_finished_ = false;

    VoiceMixer voices_;
    Sequencer sequencer_;
    bgm::Player bgm_;
    std::vector<sound::MusicEntry> music_index_;
    std::shared_ptr<const sound::SeqBank> seqs_;
    std::vector<double> seq_loop_seconds_;
    bool seq_load_tried_ = false;
    SDL_AudioStream* sfx_stream_ = nullptr;
    float sfx_volume_ = 0.8f;
    std::string device_name_;
    std::atomic<SDL_AudioDeviceID> device_{0}; // physical device in use, 0 before the first open
    // Held while a switch moves the streams, and by the game thread's pause / resume, so a pause
    // is never made on a device that is about to close.
    std::mutex device_mutex_;
    std::thread switcher_;
    std::mutex switch_mutex_;
    std::condition_variable switch_cv_;
    std::string switch_name_;
    bool switch_requested_ = false, switch_quit_ = false;
};

} // namespace gt3r
