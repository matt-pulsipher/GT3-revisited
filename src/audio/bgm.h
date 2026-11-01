#pragma once

// GT3's background-music layer. The game addresses
// all BGM with one id:
//   0x00..0x11  sequenced menu music: the music.inf entry with that name in
//               the game's table (alphabetical: 0 arcade01 ... 17 wheelshop01)
//   0x12..0x22  streamed music: bgm/ads.inf group id - 0x12; the track is
//               `index` within the group, or a random one for index -1;
//               index | 0x1000 plays the intro file ("bgm/i_<file>")
// Two entry points: play() always (re)starts; play_if_different() keeps the
// current music when it is the same piece (same sequence, or same stream
// file), as the menus do. Stopping is immediate (no fades).
// Every request carries a level (the k*Volume constants below, music §6): a
// stream plays at level x the Music volume setting; a sequence takes the level
// as the sequencer's music volume (sequence_volume) and the Music volume setting
// scales the mixer's music bus.

#include "formats/sound/mads.h"

#include <random>
#include <string>
#include <vector>

namespace gt3r {

class AudioEngine;
class Vfs;

namespace bgm {

// Sequenced ids (music.inf names).
constexpr int kArcade = 0x00, kBattle2P = 0x01, kDealer = 0x02, kFree = 0x03, kGorace = 0x04, kGtMenu = 0x05,
              kHome = 0x06, kLicense1 = 0x07, kLicense2 = 0x08, kMachTest = 0x09, kMain = 0x0A, kMini = 0x0B,
              kOption = 0x0C, kTheater = 0x0D, kTimeTrial = 0x0E, kTune = 0x0F, kWash = 0x10, kWheelShop = 0x11;
// Streamed ids (ads.inf groups; working titles).
constexpr int kStartLong = 0x12;     // str01a "jg11": standing start held 10 s (start type 2)
constexpr int kFinish = 0x13;        // str02a "jg12": the player finishes (2P: the winner)
constexpr int kRace = 0x14;          // race_a01..25: in-race music
constexpr int kQuickMenu = 0x15;     // str10a "shortmenu-2": pre-race / time trial / license quick menus
constexpr int kResult = 0x16;        // str05a "result": race result quick menus
constexpr int kMiniPrize = 0x17;     // str14a "jg05": arcade trophy, GT-mode trophy, consolation prize
constexpr int kTrophy = 0x18;        // str12a "champ-3": championship trophy presentation
constexpr int kCarGet = 0x19;        // str13a "getacar": prize car presentation (plays once)
constexpr int kReplayTheater = 0x1A; // str91a "Are You Gonna Go My Way" (GT remix): the Replay Theater page
constexpr int kStartShort = 0x1B;    // str03a "jg04e": standing start held 4 s (start type 1)
constexpr int kJg01 = 0x1C;          // str04a (no caller found)
constexpr int kLicenseGold = 0x1D;   // str06a "jg06"
constexpr int kLicenseSilver = 0x1E; // str07a "jg05"
constexpr int kLicenseBronze = 0x1F; // str08a "bronze"
constexpr int kFail = 0x20;          // str09a "fail": license failed / time trial failed
constexpr int kLicenseGet = 0x21;    // str11a "champ-2": license card presentation
constexpr int kDemo = 0x22;          // demo_a01: attract-mode race

constexpr int kIntro = 0x1000; // index flag: the intro variant of a stream

// Request levels (music §6).
constexpr float kMenuVolume = 0.8f;       // menu pages, presentations and quick menus (sequences and streams)
constexpr float kRaceSongVolume = 0.5f;   // race songs in a live run
constexpr float kReplaySongVolume = 0.7f; // race songs in a replay
constexpr float kJingleVolume = 1.0f;     // start / finish / medal / fail jingles and the demo track

const char* sequence_name(int id); // music.inf name for 0x00..0x11, else nullptr
bool is_sequence(int id);
bool is_stream(int id);
// The sequencer's music volume (Q14) for a request level: min(int(level x 16384), 0x3FFF).
int sequence_volume(float level);
// The output gain of a streamed request: level x the Music volume setting.
float stream_gain(float level, float music_volume);

class Player {
public:
    // Starts `id` (restarting it if it already plays) at request level `volume`.
    // loop: streams repeat (menus, presentations) or play once (race tracks,
    // jingles; the game's play flag). Sequences always loop. Returns false if
    // nothing could play. `resume` (AudioEngine::play_music): false keeps a paused race silent.
    bool play(AudioEngine& audio, Vfs& vfs, int id, int index = -1, bool loop = true, float volume = kJingleVolume,
              bool resume = true);
    // Ours: one of the player's own music files (a host path) as race song `id` (kRace): it
    // counts as that id, without a track record (no markers). `trim`: its loudness gain.
    bool play_file(AudioEngine& audio, const std::string& path, int id, bool loop, float volume, bool resume = true,
                   float trim = 1.0f);
    // Like play(), but keeps the current music (and its level) when it is the same piece.
    bool play_if_different(AudioEngine& audio, Vfs& vfs, int id, int index = -1, bool loop = true,
                           float volume = kJingleVolume);
    void stop(AudioEngine& audio);
    // The files of stream group `id` in index order (empty if unknown); kRace lists the race songs.
    std::vector<std::string> files(Vfs& vfs, int id);

    int current() const { return id_; } // -1 when stopped
    // True while the current piece plays (a non-looping stream has ended
    // when this turns false).
    bool playing(const AudioEngine& audio) const;
    // The streamed piece now playing (its ads.inf record, with the marker tracks), or
    // nullptr (a sequence, or nothing).
    const sound::MadsTrack* current_track() const { return track_; }
    // +1 at every play and stop: tells a new piece (or the same one restarted) apart.
    u32 generation() const { return generation_; }

private:
    bool load(Vfs& vfs);
    // Identity of what (id, index) would play: sequence index or stream file.
    std::string identity(AudioEngine& audio, Vfs& vfs, int id, int index);

    bool loaded_ = false;
    sound::MadsIndex mads_;
    int id_ = -1, index_ = -1;
    std::string what_;
    const sound::MadsTrack* track_ = nullptr;
    u32 generation_ = 0;
    std::mt19937 rng_{std::random_device{}()};
};

} // namespace bgm
} // namespace gt3r
