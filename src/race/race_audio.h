#pragma once

// Race audio beyond the engine note: the reverb "ambience" model, race music
// and the other cars' engines.
//
// Ambience (from the game's race sound code):
// every frame the reverb depth (EVOL) moves towards a target by at most 1/36:
//   target 0.5   when the surface under the car has attribute bits
//                (flags & 0x18) == 0x08 (tunnels / covered sections),
//          0.25  when (flags & 0x18) is 0x10 or 0x18,
//          0.125 otherwise (= 0x0FFF, the boot setting);
// EVOL = depth * 32767. The flags are the GTRW triangle flags, i.e.
// SurfaceHit::surface >> 8 (see docs/dev/TRACK.md).

#include "audio/bgm.h"
#include "audio/engine_sound.h"
#include "base/bytes.h"
#include "race/custom_music.h"
#include "race/music_markers.h"
#include "race/race_playlist.h"
#include "sim/sim_types.h"

#include <memory>
#include <optional>
#include <random>
#include <utility>
#include <string>
#include <vector>

namespace gt3r {
class AudioEngine;
class Vfs;
class VoiceMixer;
} // namespace gt3r

namespace gt3r::race {

class ReverbAmbience {
public:
    static float target_for(u32 surface_flags);
    // One game frame (1/60 s): steps the depth and writes it to the mixer.
    void tick(VoiceMixer& mixer, u32 surface_flags);
    void reset(VoiceMixer& mixer); // back to the boot depth (0.125)
    float depth() const { return depth_; }

private:
    float depth_ = 0.125f;
};

// The race music's level (music §2.1): the level of its last request, kept until a stop,
// played only while the run's switch (race BGM live, replay BGM in replays and demos) is on.
// A request takes the factor of its kind (race song 0.5 / 0.7, jingle 1.0) when that base
// is not 0, else 0: a request made with the switch off stays silent, also after switching it
// on, until the next stop. Every frame the stream plays at the base.
struct RaceMusicLevel {
    float last = 1.0f; // the last request's level (1 after a stop)
    float base(bool on) const { return on ? last : 0.0f; }
    float request(bool on, float factor) { return last = base(on) != 0 ? factor : 0.0f; }
    void stop() { last = 1.0f; }
};

// The finish jingle's car (music §3.1, §2.5): the race display's car crossing the line
// after its laps, once per run or replay: live the player's car, in a replay the car the cameras
// follow at that moment (a car that finished while another one was followed does not count later).
class FinishCue {
public:
    void reset() { last_.clear(), heard_ = false; }
    // One frame: `finished[i]` whether car i has finished, `followed` the car followed now.
    // True from the frame the followed car finishes on.
    bool update(const std::vector<bool>& finished, int followed);

private:
    std::vector<bool> last_;
    bool heard_ = false;
};

// Race music (music §2-3): race songs from the race playlist (bgm id 0x14),
// played once each, the next one starting when a song ends; start / finish /
// medal / fail jingles; the quick menus' music. Each request carries its level
// (music §6): race songs 0.5 live and 0.7 in replays, jingles and the demo track
// 1.0, the quick menus the menu level 0.8. The race switches (race / replay BGM)
// do not stop the race's requests: with the switch off they play at level 0
// (RaceMusicLevel), so the playlist advances and the song's markers keep timing the
// replay cuts. The quick menus follow the Menu BGM switch.
// cues() drives the event-completion states every frame; step_markers() the markers.
// Ours: the race songs may also be the player's own (custom_music.h: the Options music list's
// Original / Custom / Both), and skip() moves to the next or previous song (the pause screen's
// L1 / R1, the Next / Previous song controls). The songs played are remembered in order, so
// Previous goes back through them and Next, and a song's end, then forward again before new
// songs are taken from the playlists.
class RaceMusic {
public:
    // Race: every one-player race and solo run; TwoPlayer: 2P battle (the
    // finish jingle only for the winner); License: no race tracks; Demo: attract.
    // LicenseReplay: a licence run replayed (or its disc demo): the finish posts
    // "jg12" as in races, no medal jingle.
    enum class Kind : u8 { Race, TwoPlayer, License, Demo, LicenseReplay };
    // The quick menu on screen: Result (race / 2P results: "result"), Short (pre-race,
    // time trial, licence, machine test: "shortmenu-2").
    enum class Menu : u8 { None, Short, Result };
    struct Cues {
        bool music_on = true;       // the run's BGM switch (race BGM live, replay BGM in replays and demos)
        bool menu_music_on = true;  // the Menu BGM switch: the quick menus' music
        Kind kind = Kind::Race;
        bool replay = false;        // a replay of the run: race songs at the replay level
        bool songs = true;          // the race playlist (has_songs()) has a song in the list
        bool paused = false;
        int start_type = 2;         // event start type (+0x1F3): 1 / 2 have a start jingle
        bool holding = false;       // the cars are held before the start signal
        bool finished = false;
        int position = 0;           // finishing place (1 = won; used by 2P)
        bool failed = false;        // licence test failed / time trial voided
        int medal = -1;             // licence medal 0 gold .. 2 bronze, -1 none
        double since_finish = 0;    // seconds since the finish
        Menu menu = Menu::None;
    };
    // One BGM request. Op Play restarts, PlayIfDifferent keeps the same piece. A Play
    // of bgm::kRace takes the next song of the race playlist.
    struct Action {
        enum class Op : u8 { Play, PlayIfDifferent, Stop, Pause, Resume } op;
        int id = -1;
        bool loop = false;
        float volume = bgm::kJingleVolume; // the request's level
        bool operator==(const Action&) const = default;
    };
    // The pure decision for one frame (also used by the tests).
    // `music_playing`: the current BGM is still sounding.
    std::vector<Action> decide(const Cues& c, bool music_playing);
    // decide() and apply the result through AudioEngine::bgm().
    void cues(AudioEngine& audio, Vfs& vfs, const Cues& c);

    // Seconds after the result at which a licence medal's jingle starts: the end of the
    // presenter's 30-frame dim, 37 frames after the result (presentation.md A4).
    static constexpr double kMedalDelay = 37.0 / 60.0;

    // The race playlist: `stored` is its saved text ([options] music_playlist); without
    // a valid one, a new list of every race song on the disc (shuffle on).
    void load_playlist(AudioEngine& audio, Vfs& vfs, const std::optional<std::string>& stored);
    const RacePlaylist& playlist() const { return playlist_; }
    // The playlist's text after a race song was taken (to be saved), once; else nothing.
    std::optional<std::string> take_playlist_change();
    // Ours: the player's own songs (`library`, may be null), the source chosen on the Options
    // music list and Both's Order / Shuffle (custom_music.h).
    void set_custom(MusicLibrary* library, MusicSource source, bool mix_shuffle);
    // The source playing: the one chosen, Original without custom songs.
    MusicSource source() const;
    // A song in the list of the source playing (Cues::songs).
    bool has_songs() const;
    // The custom playlist changed (a song of it was taken): true once; the caller saves it.
    bool take_custom_change() { return std::exchange(custom_changed_, false); }
    // Ours ([music] next_race = same): every run starts with the race song the last one ended
    // on, from its start, instead of the next song of the list; `text` is the last run's before
    // this screen (take_song_change's), later runs here (a restart, the replay) take the one
    // stop() ended. A song that can't play now is passed over: a source that doesn't play it,
    // out of its list, its file gone. Not called: GT3's rule, a new song every run.
    void set_first_song(std::string_view text);
    // The race song that started, once, as text ("disc:<id>" / "yours:<id>", hex); the caller
    // saves it for the next run.
    std::optional<std::string> take_song_change();

    // A race song as the pause screen shows it.
    struct SongInfo {
        std::string title, artist;
        bool custom = false;
    };
    // Ours: the race song playing (or paused) now, when the race's music is heard: nothing for
    // jingles, the quick menus' music, the demo track, silence, or the run's BGM switch off.
    std::optional<SongInfo> now_playing(const AudioEngine& audio) const;
    // Ours: the next (+1) or previous (-1) race song, at once (Previous with none before it
    // starts the song over). While the race is paused the new song waits, silent, for the
    // resume. Only while now_playing() has a song; true when it did something.
    bool skip(AudioEngine& audio, Vfs& vfs, int dir);

    // A new run: the next cues() call starts the music afresh.
    void reset() { seen_ = false; }
    void stop(AudioEngine& audio);

    // The playing song's markers (music §8), one 60 Hz frame at its audible position. A race
    // song (and the attract demo's track) carries them; jingles, menu music and silence none.
    void step_markers(AudioEngine& audio);
    const MusicMarkers& markers() const { return markers_; }

private:
    // One song played: one of the disc's (its playlist id) or the player's (custom_song_id).
    struct Played {
        bool custom = false;
        u64 id = 0;
    };
    void play_race_song(AudioEngine& audio, Vfs& vfs, float volume, bool resume = true);
    std::optional<Played> take_next(); // the next song of the source playing
    bool can_play(const Played& song) const; // set_first_song's song, in the source playing now
    void ease_trim(AudioEngine& audio);      // the player's song toward its loudness gain
    bool play(AudioEngine& audio, Vfs& vfs, const Played& song, float volume, bool resume);
    void follow_piece(AudioEngine& audio); // a new piece: its markers (or none)

    bool active_ = false;
    RaceMusicLevel level_;
    bool race_piece_ = false;     // the BGM now playing was the race's request (not a quick menu's)
    bool marked_request_ = false; // ... a race song or the demo track (markers)
    u32 generation_ = 0;          // bgm::Player::generation() last seen
    MusicMarkers markers_;
    // cues() state
    bool seen_ = false, was_paused_ = false, was_finished_ = false, jingle_ = false;
    bool was_holding_ = false, start_jingle_ = false, demo_played_ = false;
    bool medal_played_ = false;
    Menu was_menu_ = Menu::None;
    RacePlaylist playlist_;
    std::vector<u64> song_ids_; // the race-song group's files (id codes) in track order
    bool playlist_changed_ = false;
    // Ours: the player's songs, the songs played (history_[at_] now), the last cues.
    MusicLibrary* library_ = nullptr;
    MusicSource chosen_ = MusicSource::Original;
    bool mix_shuffle_ = true, custom_changed_ = false;
    bool last_custom_ = true; // Both with Order: the disc's song first
    std::vector<Played> history_;
    size_t at_ = 0;
    // The player's song playing (0: none) and the loudness gain it plays at now, eased to its
    // level when that is measured (MusicLibrary::want_measured), on bgm generation trim_generation_.
    u64 trim_id_ = 0;
    u32 trim_generation_ = 0;
    float trim_ = 1.0f;
    u64 trim_ticks_ = 0;
    std::optional<Played> first_; // the run's first song (set_first_song)
    bool same_song_ = false;      // set_first_song was called
    bool run_song_ = false;       // a race song started in this run
    bool song_changed_ = false;   // a race song started (take_song_change)
    sound::MadsMarkers fallback_; // the markers of a race song without its own (follow_piece)
    Cues last_;
    std::mt19937 rng_{std::random_device{}()};
};

// Engine notes of the other cars, placed like GT3 does: volume
// min(1, 32 / d) (d = horizontal distance to the camera, >= 0.25 m), Doppler
// (c + v_listener) / (c + v_source) along the camera->car direction
// (c = 331.45 m/s), per-view channel gains ("other car" columns). GT3 sounds
// every car and lets the voice allocator drop quiet voices; we give the
// nearest `max_voiced` cars a note (4 voices each) to stay inside 46 voices.
class OpponentEngines {
public:
    struct Car {
        u16 sound_id = 0;
        u8 variant = 0;
        float cd = 0.35f; // drag coefficient for the wind unit
    };
    void load(Vfs& vfs, const std::vector<Car>& cars);
    // `listener`/`listener_velocity`: camera position and the followed car's velocity.
    // `right`/`forward`: the listener's (camera's) unit axes, for GT3's stereo pan.
    void update(VoiceMixer& mixer, const sim::Vec3& listener, const sim::Vec3& listener_velocity,
                const sim::Vec3& right, const sim::Vec3& forward, const std::vector<const sim::VehicleState*>& states,
                double dt, const std::vector<float>* slipstream = nullptr);
    void stop(VoiceMixer& mixer);

    int max_voiced = 5;    // cars with an engine note at once
    int max_wind = 2;      // of those, the nearest with their wind unit (voice budget)
    EngineSound::View view = EngineSound::View::ChaseOther;
    // GT3: full volume within 32 m, then 32 / d.
    static float attenuation(float horizontal_distance);
    // Doppler factor for a source moving with v_src and a listener with v_lis
    // along the unit direction listener -> source (speed of sound 331.45 m/s).
    static float doppler(const sim::Vec3& dir, const sim::Vec3& v_src, const sim::Vec3& v_lis);

private:
    std::vector<std::unique_ptr<EngineSound>> engines_;
    // The wind / road unit (rn_01.es) of each car: other cars are heard with the
    // "other car" wind gain (in-car / whine / turbo gains are 0 for other cars).
    std::vector<std::unique_ptr<EngineUnit>> winds_;
    std::vector<float> cd_;
    std::vector<bool> active_;
};

} // namespace gt3r::race
