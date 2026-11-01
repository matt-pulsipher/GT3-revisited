#pragma once

// Ours: the player's own race music. A music folder (Settings -> Audio -> Your music; [music]
// folder, by default the user data folder's music/) holds MP3, FLAC and WAV files at any depth;
// they play as race songs beside the disc's, chosen on the Options music list's tabs:
//   Original  the disc's race playlist only (GT3's rules, race_playlist.h)
//   Custom    the player's songs only, from a playlist of the same kind
//   Both      both playlists, mixed song by song: with Shuffle each song is drawn from the two
//             in proportion to the songs each still has in its pass (an even shuffle of all
//             of them); with Order they take turns, one of each, each in its own order
// Custom and Both need custom songs; without them the disc's playlist plays whatever was chosen.
// [options] music_source = original | custom | both, music_mix = order | shuffle (Both's).
// The custom playlist is kept in <user data>/music_playlist.txt.
//
// The songs as the last scan found them, and their loudness, are remembered in
// <user data>/music_songs.txt: the next start has them at once, before its own scan lands (a
// big folder on a phone takes seconds, and the first race may start before). A remembered song
// whose file has gone is passed over when it comes up, until the scan drops it.
//
// Loudness: the player's songs play as loud as the disc's race songs, on average. A thread of
// the library measures the disc's race songs once (remembered with their names and sizes, so a
// mod's songs are measured again), waiting while a race runs, and the player's songs one by
// one as they are asked for (audio/loudness.h): a song when it starts to play (RaceMusic eases
// it to its level when that lands, about a second in on a phone) and the next one of its list
// meanwhile, so it is ready. A whole library is never measured up front (a phone takes about
// a second a song). Each song plays at the disc's loudness over its own, from -20 to +12 dB, and
// never so far up that its loudest sample goes past a full-scale disc song's at the loudest
// level the game streams music at (a replay's). Songs not measured yet, and all of them until
// the disc's are, play as they are.
//
// The scan: every folder under the music folder, linked folders too (each folder once, however
// many links reach it, so a link back up can't loop), at most 32 deep; folders it may not read are
// passed over; names starting with '.' are skipped (hidden files, macOS's "._" copies). Folders
// are walked in name order, so a library over 10,000 songs keeps the same first 10,000. A file
// reached twice (through a link) counts once; files that don't open (damaged, or another format
// under the name) are counted and left out. A song is known by its folder's name, its file name
// (any case) and its size, so it keeps its place and check mark when its folder moves (an album
// into another folder); two songs that would share that, by their paths in the music folder.
// When the music folder itself can't be read (a drive unplugged, a share gone), there are no
// songs, but the custom playlist is left as it is until the folder is back.

#include "audio/loudness.h"
#include "race/race_playlist.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gt3r::race {

enum class MusicSource : u8 { Original, Custom, Both };
MusicSource parse_music_source(std::string_view s); // "custom", "both", anything else Original
std::string_view music_source_name(MusicSource s);  // "original", "custom", "both"
// The source that plays: the chosen one, or Original while there are no custom songs.
inline MusicSource effective_music_source(MusicSource chosen, bool has_custom) {
    return has_custom ? chosen : MusicSource::Original;
}

// A song of the music folder.
struct CustomSong {
    u64 id = 0;                // custom_song_id("folder/file", size)
    std::string path;          // host path
    std::string rel;           // its path in the folder, '/' between folders
    std::string title, artist; // from its tags; the title is the file's name without them
    bool measured = false;     // its loudness is known (or it has none: silence, unreadable)
    float lufs = 0, peak = 0;  // its loudness (audio/loudness.h); peak 0: none
};
// A song's id in the custom playlist: a 64-bit FNV-1a hash of `key` (its folder's name and file
// name, "folder/file"; any case) and its size.
u64 custom_song_id(std::string_view key, u64 size);
// "title / artist", or the title alone.
std::string song_label(const std::string& title, const std::string& artist);
// UTF-8 text in plain letters for the game's own fonts (the Options music list): accented Latin
// letters without their accents, curly quotes and dashes as ASCII ones, anything else '?'.
std::string menu_ascii(std::string_view utf8);

// The next race song of a Both mix (see above): true for one of the player's songs.
// `stock_left` / `custom_left`: songs each playlist still has in its pass (remaining());
// `stock_listed` / `custom_listed`: whether each has a song in its list; `last_custom`: what
// the previous pick was (Order takes turns); `unit`: a draw in [0, 1).
bool pick_custom(bool shuffle, size_t stock_left, size_t custom_left, bool stock_listed, bool custom_listed,
                 bool last_custom, double unit);

// Whether host paths `a` and `b` name the same folder (through links, "..", a letter case the
// file system ignores); false when either can't be read.
bool same_folder(const std::string& a, const std::string& b);

// The music folder, scanned on a thread of its own, and the custom playlist.
class MusicLibrary {
public:
    MusicLibrary() = default;
    ~MusicLibrary();
    MusicLibrary(const MusicLibrary&) = delete;
    MusicLibrary& operator=(const MusicLibrary&) = delete;

    // Where the custom playlist is kept (read when the first scan lands).
    void set_playlist_file(std::string path) { playlist_file_ = std::move(path); }
    // Where the songs and their loudness are remembered (see the top); read by the first
    // set_folder, so set before it.
    void set_song_list_file(std::string path) { song_list_file_ = std::move(path); }
    // The folder to play from (a host path; "" for none); a new one is scanned at once (true).
    bool set_folder(std::string folder);
    const std::string& folder() const { return folder_; }
    // Scans the folder again (files added or removed while the game ran). A scan under way stops
    // and the new one starts after it, without waiting on the game's thread.
    void rescan();
    // Every frame on the game's thread: takes a finished scan in, keeps the playlist in step
    // and saves it when it changed. True when the songs changed.
    bool poll();
    bool scanning() const { return scanning_; }
    // Ends a scan and a measuring under way (the game quitting); the songs stay as they were,
    // and what was measured is remembered.
    void cancel();

    // The disc's race songs, measured for the level the player's songs play at: `key` names the
    // set (their names and sizes); each opener gives one song's stream, called on the measuring
    // thread (so it must read through a thread-safe reader, Vfs::Stream). Measured at once
    // unless remembered with that key.
    using StreamOpener = std::function<std::unique_ptr<MusicStream>()>;
    void set_disc_songs(std::string key, std::vector<StreamOpener> songs);
    // The disc's race songs' loudness (their mean, LUFS), once measured.
    std::optional<float> disc_loudness() const { return disc_lufs_; }
    // The gain `s` plays at (see the top); 1 until it and the disc's songs are measured.
    float gain(const CustomSong& s) const;
    // Measures `id`'s loudness soon, before songs asked for earlier (a few are kept); nothing
    // when it is measured or no song of the folder.
    void want_measured(u64 id);
    // Called every frame a race runs: the disc's songs are measured between races (phones:
    // decoding beside a race costs frames); a song asked for is measured at once.
    void racing();
    // The game is away (a phone's app in the background, its screen off): the measuring waits.
    void set_away(bool away) { away_ = away; }
    // Songs measured so far.
    size_t measured() const;
    // Something is being measured or waits to be.
    bool measuring() const;

    const std::vector<CustomSong>& songs() const { return songs_; }
    bool has_songs() const { return !songs_.empty(); }
    // Files in music formats that don't play (AAC, Ogg...), from the last scan.
    size_t unplayable() const { return info_.unplayable; }
    size_t broken() const { return info_.broken; } // files that don't open, from the last scan
    // The last scan found more songs than RacePlaylist::kMaxCustomSongs (the first ones kept).
    bool capped() const { return info_.capped; }
    // The last scan could read the folder.
    bool folder_found() const { return info_.found; }
    const CustomSong* find(u64 id) const;
    const CustomSong* find_path(const std::string& path) const;
    // +1 whenever the songs change.
    u32 generation() const { return generation_; }

    RacePlaylist& playlist() { return playlist_; }
    const RacePlaylist& playlist() const { return playlist_; }
    void save_playlist() const;

    // What a scan found besides the songs.
    struct ScanInfo {
        size_t unplayable = 0; // files in formats that don't play
        size_t broken = 0;     // MP3 / FLAC / WAV files that don't open (damaged, or not what they're named)
        bool found = false;    // the folder could be read
        bool capped = false;   // more songs than the limit
    };
    static constexpr int kMaxDepth = 32;
    // The scan itself (also the tests'; see the top): the songs of `folder` that open, sorted by
    // their path in it (any case), at most `limit`, a file reached twice once. `stop` ends it
    // early (no songs).
    static std::vector<CustomSong> scan(const std::string& folder, ScanInfo& info, const std::atomic<bool>* stop = nullptr,
                                        size_t limit = RacePlaylist::kMaxCustomSongs);

    // The song list file's text (see the top), and the songs read back from it: those of
    // `folder` (empty for another), the disc's level when remembered with `disc_key`.
    static std::string song_list_text(const std::string& folder, const std::vector<CustomSong>& songs,
                                      const std::string& disc_key, std::optional<float> disc_lufs);
    struct SongList {
        std::string folder;
        std::vector<CustomSong> songs;
        std::string disc_key;
        std::optional<float> disc_lufs;
    };
    static std::optional<SongList> parse_song_list(std::string_view text);

private:
    void start_scan();
    void join();
    // Takes songs in (a scan's, or the remembered ones): keeps the playlist in step and saves
    // it. True when the songs changed.
    bool take(std::vector<CustomSong> songs, const ScanInfo& info, bool remembered);
    void read_song_list(); // once, into remembered_
    void save_song_list();
    void ensure_measuring(); // the thread running while there is work
    void stop_measuring();
    void measure_loop();
    bool take_measured(); // the measuring thread's results; true when any landed

    std::string folder_, playlist_file_, song_list_file_;
    std::vector<CustomSong> songs_;
    ScanInfo info_;
    u32 generation_ = 0;
    RacePlaylist playlist_;
    bool playlist_loaded_ = false;

    std::thread thread_;
    std::atomic<bool> stop_{false};
    bool scanning_ = false;
    bool again_ = false; // a scan asked for while one ran: started when that one ends
    std::mutex mutex_;
    std::optional<std::vector<CustomSong>> result_; // a finished scan, for poll()
    ScanInfo result_info_;
    bool list_read_ = false;            // the song list file was read (into remembered_)
    std::optional<SongList> remembered_; // what it held
    bool list_tried_ = false;           // its songs were offered (the first set_folder)
    bool from_list_ = false;            // songs taken from it, for poll() to report
    bool list_dirty_ = false;    // measured since the list was saved

    // Loudness: the disc's songs, then the folder's songs not measured yet, on a thread.
    std::string disc_key_;
    std::vector<StreamOpener> disc_songs_;
    std::vector<std::optional<float>> disc_measured_; // per disc song, as they land
    std::optional<float> disc_lufs_;
    struct Measured {
        bool disc = false;
        size_t index = 0; // a disc song's
        u64 id = 0;       // a folder song's, and its path
        std::string path;
        std::optional<Loudness> loudness;
    };
    std::thread measure_thread_;
    std::atomic<bool> measure_stop_{false};
    std::atomic<u64> racing_until_{0}; // SDL ticks (ms)
    std::atomic<bool> away_{false};
    mutable std::mutex measured_mutex_;   // the work below and the results
    bool worker_running_ = false;         // measure_thread_ is taking work
    std::vector<size_t> disc_work_;       // disc songs to measure (indices of disc_songs_)
    std::deque<std::pair<u64, std::string>> wanted_; // songs asked for, the latest first
    u64 measuring_id_ = 0;                // the song being measured now
    std::vector<Measured> measured_;      // landed, for poll()
};

} // namespace gt3r::race
