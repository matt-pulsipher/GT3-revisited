// The player's own race music (race/custom_music.h): music files and their tags
// (audio/music_file.h), the folder scan, the custom playlist, the Both mix, the race's song
// skips (RaceMusic::skip), the song chords (bindings.h Chord) and the touch song buttons.

#include "test_framework.h"

#include "audio/audio_engine.h"
#include "audio/bgm.h"
#include "audio/music_file.h"
#include "base/config.h"
#include "base/file.h"
#include "input/bindings.h"
#include "input/input_manager.h"
#include "input/touch_controls.h"
#include "menu/flow/music_list_page.h"
#include "race/custom_music.h"
#include "race/music_markers.h"
#include "race/race_audio.h"
#include "race/replay_cameras.h"
#include "vfs/vfs.h"

#include "music_fixtures.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace gt3r;
using race::MusicLibrary;
using race::RacePlaylist;

namespace {

void put32(Bytes& b, u32 v) {
    for (int i = 0; i < 4; ++i) b.push_back(u8(v >> (8 * i)));
}
void put16(Bytes& b, u16 v) { b.push_back(u8(v)), b.push_back(u8(v >> 8)); }
void put(Bytes& b, std::string_view s) { b.insert(b.end(), s.begin(), s.end()); }
void put_be32(Bytes& b, u32 v) {
    for (int i = 3; i >= 0; --i) b.push_back(u8(v >> (8 * i)));
}
void put_syncsafe(Bytes& b, u32 v) {
    for (int i = 3; i >= 0; --i) b.push_back(u8((v >> (7 * i)) & 0x7F));
}

// A 16-bit PCM WAV: frame f of channel c holds f * 2 + c; `info`: LIST INFO subchunks after the samples.
Bytes wav(u32 frames, u16 channels, u32 rate, const std::vector<std::pair<std::string, std::string>>& info = {}) {
    Bytes data;
    for (u32 f = 0; f < frames; ++f)
        for (u16 c = 0; c < channels; ++c) put16(data, u16(f * 2 + c));
    Bytes list;
    if (!info.empty()) {
        put(list, "INFO");
        for (const auto& [id, text] : info) {
            put(list, id);
            put32(list, u32(text.size() + 1));
            put(list, text);
            list.push_back(0);
            if ((text.size() + 1) & 1) list.push_back(0);
        }
    }
    Bytes b;
    put(b, "RIFF");
    put32(b, u32(4 + 8 + 16 + 8 + data.size() + (list.empty() ? 0 : 8 + list.size())));
    put(b, "WAVE");
    put(b, "fmt ");
    put32(b, 16);
    put16(b, 1);
    put16(b, channels);
    put32(b, rate);
    put32(b, rate * channels * 2);
    put16(b, u16(channels * 2));
    put16(b, 16);
    put(b, "data");
    put32(b, u32(data.size()));
    b.insert(b.end(), data.begin(), data.end());
    if (!list.empty()) {
        put(b, "LIST");
        put32(b, u32(list.size()));
        b.insert(b.end(), list.begin(), list.end());
    }
    return b;
}

// An ID3v2 tag of version `ver` (2, 3, 4) with text frames (id, encoding byte, raw text).
Bytes id3(u8 ver, const std::vector<std::tuple<std::string, u8, Bytes>>& frames) {
    Bytes body;
    for (const auto& [id, enc, text] : frames) {
        Bytes f{enc};
        f.insert(f.end(), text.begin(), text.end());
        put(body, id);
        if (ver == 2) {
            body.push_back(u8(f.size() >> 16)), body.push_back(u8(f.size() >> 8)), body.push_back(u8(f.size()));
        } else {
            if (ver == 4) put_syncsafe(body, u32(f.size()));
            else put_be32(body, u32(f.size()));
            body.push_back(0), body.push_back(0);
        }
        body.insert(body.end(), f.begin(), f.end());
    }
    Bytes b;
    put(b, "ID3");
    b.push_back(ver), b.push_back(0), b.push_back(0);
    put_syncsafe(b, u32(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

Bytes bytes_of(std::string_view s) { return Bytes(s.begin(), s.end()); }

void write(const std::filesystem::path& p, const Bytes& b) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

std::string utf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

} // namespace

TEST(music_file_types) {
    CHECK(music_file_type("a/Song.MP3") == MusicFileType::Mp3);
    CHECK(music_file_type("b.flac") == MusicFileType::Flac);
    CHECK(music_file_type("c.wav") == MusicFileType::Wav && music_file_type("c.WAVE") == MusicFileType::Wav);
    CHECK(music_file_type("d.m4a") == MusicFileType::None && music_file_type("mp3") == MusicFileType::None);
    CHECK(music_file_type("dir.mp3/readme") == MusicFileType::None);
    CHECK(unplayable_music_file("e.m4a") && unplayable_music_file("f.OGG") && !unplayable_music_file("g.txt"));
    CHECK(!unplayable_music_file("h.mp3"));
}

// ID3v2.2 / 2.3 / 2.4 text frames in each encoding, ID3v1 as the fallback, unsynchronisation.
TEST(music_tags_id3) {
    MusicTags t = parse_music_tags(MusicFileType::Mp3, id3(3, {{"TIT2", 0, bytes_of("Caf\xe9")}, {"TPE1", 0, bytes_of("Band")}}));
    CHECK(t.title == "Caf\xc3\xa9" && t.artist == "Band"); // Latin-1 to UTF-8
    // UTF-16 with a BOM (little endian), a second value after a NUL ignored.
    Bytes u16le = {0xFF, 0xFE, 'H', 0, 'i', 0, 0x3A, 0x26, 0, 0, 'X', 0};
    t = parse_music_tags(MusicFileType::Mp3, id3(3, {{"TPE1", 1, u16le}}));
    CHECK(t.artist == "Hi\xe2\x98\xba" && t.title.empty());
    // UTF-16BE without a BOM (encoding 2), UTF-8 (3), v2.4's syncsafe frame sizes.
    t = parse_music_tags(MusicFileType::Mp3, id3(4, {{"TIT2", 2, {0, 'O', 0, 'k'}}, {"TPE1", 3, bytes_of("\xc3\xbc")}}));
    CHECK(t.title == "Ok" && t.artist == "\xc3\xbc");
    t = parse_music_tags(MusicFileType::Mp3, id3(2, {{"TT2", 0, bytes_of("Two")}, {"TP1", 0, bytes_of("Point")}}));
    CHECK(t.title == "Two" && t.artist == "Point");
    // ID3v1 at the end fills what v2 lacks.
    Bytes v1(128, 0);
    std::memcpy(v1.data(), "TAG", 3);
    std::memcpy(v1.data() + 3, "Old title   ", 12);
    std::memcpy(v1.data() + 33, "Old artist", 10);
    t = parse_music_tags(MusicFileType::Mp3, id3(3, {{"TIT2", 0, bytes_of("New")}}), v1);
    CHECK(t.title == "New" && t.artist == "Old artist");
    t = parse_music_tags(MusicFileType::Mp3, Bytes(16, 0), v1);
    CHECK(t.title == "Old title" && t.artist == "Old artist");
    // Unsynchronised (flag 0x80): the writer put 0x00 after each 0xFF; the frame sizes are without them.
    Bytes tag = id3(3, {{"TIT2", 0, {'A', 0xFF, 'B'}}});
    tag.insert(std::find(tag.begin() + 10, tag.end(), u8(0xFF)) + 1, u8(0x00));
    tag[5] = 0x80;
    tag[9] = u8(tag[9] + 1);
    t = parse_music_tags(MusicFileType::Mp3, tag);
    CHECK(t.title == "A\xc3\xbf" "B");
    // A truncated tag: nothing past its end, no crash.
    Bytes cut = id3(3, {{"TIT2", 0, bytes_of("Long title")}});
    cut.resize(cut.size() - 4);
    t = parse_music_tags(MusicFileType::Mp3, cut);
    CHECK(t.title.empty());
}

TEST(music_tags_flac_and_wav) {
    Bytes f;
    put(f, "fLaC");
    f.push_back(0x00), f.push_back(0), f.push_back(0), f.push_back(34); // STREAMINFO
    f.insert(f.end(), 34, 0);
    Bytes vc;
    put32(vc, 6);
    put(vc, "vendor");
    put32(vc, 3);
    for (std::string_view c : {"ALBUM=X", "title=Flac Song", "Artist=Flac Band"}) {
        put32(vc, u32(c.size()));
        put(vc, c);
    }
    f.push_back(0x84), f.push_back(0), f.push_back(u8(vc.size() >> 8)), f.push_back(u8(vc.size()));
    f.insert(f.end(), vc.begin(), vc.end());
    MusicTags t = parse_music_tags(MusicFileType::Flac, f);
    CHECK(t.title == "Flac Song" && t.artist == "Flac Band");

    t = parse_music_tags(MusicFileType::Wav, wav(4, 2, 44100, {{"IART", "Wav Band"}, {"INAM", "Wav Song"}}));
    CHECK(t.title == "Wav Song" && t.artist == "Wav Band");
    // An "id3 " chunk.
    Bytes w = wav(2, 1, 8000);
    const Bytes tag = id3(3, {{"TIT2", 0, bytes_of("Tagged")}});
    put(w, "id3 ");
    put32(w, u32(tag.size()));
    w.insert(w.end(), tag.begin(), tag.end());
    t = parse_music_tags(MusicFileType::Wav, w);
    CHECK(t.title == "Tagged");
}

// A WAV file decodes frame for frame, loops when asked, and its tags come from after the samples.
TEST(music_file_wav_stream) {
    const std::filesystem::path p = test::temp_path("song.wav");
    write(p, wav(1000, 2, 22050, {{"INAM", "Looped"}}));
    std::unique_ptr<MusicStream> s = open_music_file(utf8(p), false);
    CHECK(s != nullptr);
    if (!s) return;
    CHECK(s->sample_rate() == 22050 && s->channels() == 2 && s->total_frames() == 1000);
    std::vector<s16> buf(2 * 600);
    CHECK(s->read(buf.data(), 600) == 600 && buf[0] == 0 && buf[1] == 1 && buf[2 * 599] == s16(1198));
    CHECK(s->read(buf.data(), 600) == 400 && buf[0] == s16(1200));
    CHECK(s->read(buf.data(), 600) == 0);
    std::unique_ptr<MusicStream> l = open_music_file(utf8(p), true);
    CHECK(l && l->read(buf.data(), 600) == 600 && l->read(buf.data(), 600) == 400);
    CHECK(l && l->read(buf.data(), 600) == 600 && buf[0] == 0 && l->position_seconds() < 0.03);
    CHECK(read_music_tags(utf8(p), MusicFileType::Wav).title == "Looped");
    CHECK(!open_music_file(utf8(test::temp_path("missing.wav")), false));
    write(test::temp_path("bad.mp3"), bytes_of("not an mp3 at all"));
    CHECK(!open_music_file(utf8(test::temp_path("bad.mp3")), false));
}

// Waits for a library's scan (true when one landed).
bool settle(MusicLibrary& lib) {
    for (int i = 0; i < 1000; ++i) {
        if (lib.poll() || !lib.scanning()) return true;
        SDL_Delay(5);
    }
    return false;
}

// The scan: songs at any depth, sorted by their path in the folder, hidden files skipped,
// other music formats and damaged files counted, titles from the tags or the file name.
TEST(music_library_scan) {
    const std::filesystem::path d = test::temp_dir("music_scan");
    write(d / "b.wav", wav(10, 1, 8000, {{"INAM", "Bee"}, {"IART", "Band"}}));
    write(d / "Album" / "a.wav", wav(10, 1, 8000));
    write(d / ".hidden" / "c.wav", wav(10, 1, 8000));
    write(d / "._b.wav", bytes_of("AppleDouble"));
    write(d / "d.m4a", bytes_of("aac"));
    write(d / "e.txt", bytes_of("notes"));
    write(d / "broken.mp3", bytes_of("not an mp3 at all"));
    write(d / "empty.flac", {});
    MusicLibrary::ScanInfo info;
    std::vector<race::CustomSong> songs = MusicLibrary::scan(utf8(d), info);
    CHECK(songs.size() == 2 && info.unplayable == 1 && info.broken == 2 && info.found && !info.capped);
    if (songs.size() != 2) return;
    CHECK(songs[0].rel == "Album/a.wav" && songs[0].title == "a" && songs[0].artist.empty());
    CHECK(songs[1].rel == "b.wav" && songs[1].title == "Bee" && songs[1].artist == "Band");
    const u64 size_a = std::filesystem::file_size(d / "Album" / "a.wav");
    CHECK(songs[0].id == race::custom_song_id("album/A.WAV", size_a) && songs[0].id != songs[1].id);
    CHECK(race::song_label("Bee", "Band") == "Bee / Band" && race::song_label("a", "") == "a");
    CHECK(MusicLibrary::scan(utf8(d / "none"), info).empty() && !info.found);

    // The library: a scan on its own thread, the playlist made, kept in step and saved.
    MusicLibrary lib;
    const std::string list_file = utf8(test::temp_path("music_playlist.txt"));
    lib.set_playlist_file(list_file);
    lib.set_folder(utf8(d));
    CHECK(settle(lib));
    CHECK(!lib.scanning() && lib.songs().size() == 2 && lib.playlist().songs().size() == 2 && lib.unplayable() == 1);
    CHECK(lib.broken() == 2 && lib.folder_found() && !lib.capped());
    CHECK(lib.find(songs[1].id) && lib.find(songs[1].id)->title == "Bee" && lib.generation() == 1);
    CHECK(host_path_exists(list_file));
    write(d / "c.wav", wav(10, 1, 8000));
    lib.rescan();
    lib.rescan(); // asked for again while scanning: the first stops, the second lands
    CHECK(settle(lib));
    CHECK(lib.songs().size() == 3 && lib.playlist().songs().size() == 3 && lib.generation() == 2);
    // Another folder while a scan runs: its songs, not the old folder's.
    const std::filesystem::path other = test::temp_dir("music_scan_other");
    write(other / "only.wav", wav(10, 1, 8000));
    lib.rescan();
    lib.set_folder(utf8(other));
    CHECK(settle(lib) && lib.songs().size() == 1 && lib.songs()[0].rel == "only.wav");
    lib.set_folder(utf8(d));
    CHECK(settle(lib) && lib.songs().size() == 3);
    // Another library reads the saved list.
    lib.playlist().set_listed(0, false);
    lib.save_playlist();
    MusicLibrary again;
    again.set_playlist_file(list_file);
    again.set_folder(utf8(d));
    CHECK(settle(again));
    CHECK(again.playlist().songs().size() == 3 && !again.playlist().songs()[0].listed);
}

// Folders deep down, any case and non-ASCII names, in the same order every time; upper-case
// extensions; the limit keeps the first songs in that order.
TEST(music_scan_depth_names_and_limit) {
    const std::filesystem::path d = test::temp_dir("music_scan_names");
    write(d / "z" / "y" / "x" / "w" / "v" / "deep.WAV", wav(4, 1, 8000));
    write(d / std::filesystem::path(u8"Caf\u00e9") / std::filesystem::path(u8"\u6771\u4eac.wav"), wav(5, 1, 8000)); // (UTF-8 on Windows too)
    write(d / "B side" / "Two Words.Wav", wav(6, 1, 8000));
    write(d / "a side" / "one.wav", wav(7, 1, 8000));
    MusicLibrary::ScanInfo info;
    std::vector<race::CustomSong> songs = MusicLibrary::scan(utf8(d), info);
    CHECK(songs.size() == 4 && !info.capped);
    if (songs.size() != 4) return;
    CHECK(songs[0].rel == "a side/one.wav" && songs[1].rel == "B side/Two Words.Wav");
    CHECK(songs[2].rel == "Caf\xc3\xa9/\xe6\x9d\xb1\xe4\xba\xac.wav" && songs[2].title == "\xe6\x9d\xb1\xe4\xba\xac");
    CHECK(songs[3].rel == "z/y/x/w/v/deep.WAV" && songs[3].title == "deep");
    // At the limit: not capped; one more: capped, the first ones in name order kept.
    CHECK(MusicLibrary::scan(utf8(d), info, nullptr, 4).size() == 4 && !info.capped);
    songs = MusicLibrary::scan(utf8(d), info, nullptr, 2);
    CHECK(songs.size() == 2 && info.capped && songs[0].rel == "a side/one.wav" && songs[1].rel == "B side/Two Words.Wav");
    // Deeper than the scan goes: left out.
    std::filesystem::path deep = d / "deep";
    for (int i = 0; i <= MusicLibrary::kMaxDepth; ++i) deep /= "d";
    write(deep / "too_deep.wav", wav(4, 1, 8000));
    songs = MusicLibrary::scan(utf8(d), info);
    CHECK(songs.size() == 4);
}

// A song keeps its place and marks when its folder moves; a folder that can't be read leaves the
// playlist (and its file) alone until it is back.
TEST(music_scan_moves_and_missing_folder) {
    const std::filesystem::path d = test::temp_dir("music_scan_moves");
    write(d / "Album" / "one.wav", wav(10, 1, 8000));
    write(d / "Album" / "two.wav", wav(11, 1, 8000));
    write(d / "three.wav", wav(12, 1, 8000));
    MusicLibrary lib;
    const std::string list_file = utf8(test::temp_path("moves_playlist.txt"));
    lib.set_playlist_file(list_file);
    lib.set_folder(utf8(d));
    CHECK(settle(lib) && lib.songs().size() == 3);
    lib.playlist().set_shuffle(false);
    lib.playlist().set_listed(1, false); // Album/two.wav
    lib.save_playlist();
    const u64 two = lib.playlist().songs()[1].id;
    // The album goes into another folder: same songs, same marks.
    std::filesystem::create_directories(d / "Rock");
    std::filesystem::rename(d / "Album", d / "Rock" / "Album");
    lib.rescan();
    CHECK(settle(lib) && lib.songs().size() == 3);
    CHECK(lib.playlist().songs().size() == 3 && lib.playlist().songs()[1].id == two && !lib.playlist().songs()[1].listed);
    CHECK(lib.find(two) && lib.find(two)->rel == "Rock/Album/two.wav");
    // The folder goes (a drive unplugged): no songs, the playlist and its file as they were.
    const std::optional<Bytes> saved = read_host_file(list_file);
    std::filesystem::rename(d, d.parent_path() / "music_scan_moves_away");
    lib.rescan();
    CHECK(settle(lib) && lib.songs().empty() && !lib.folder_found());
    CHECK(lib.playlist().songs().size() == 3 && read_host_file(list_file) == saved);
    std::filesystem::rename(d.parent_path() / "music_scan_moves_away", d);
    lib.rescan();
    CHECK(settle(lib) && lib.songs().size() == 3 && lib.folder_found() && !lib.playlist().songs()[1].listed);
    // A file of the same name and size in another folder of the same name: both, kept apart.
    write(d / "Pop" / "Album" / "two.wav", wav(11, 1, 8000));
    lib.rescan();
    CHECK(settle(lib) && lib.songs().size() == 4 && lib.playlist().songs().size() == 4);
    // A folder that is there from the start but missing: a playlist file is read all the same.
    MusicLibrary later;
    later.set_playlist_file(list_file);
    later.set_folder(utf8(d / "nowhere"));
    CHECK(settle(later) && later.songs().empty() && !later.folder_found());
}

// A two-channel 997 Hz sine at `amp` of full scale, `seconds` long, at 16 kHz.
Bytes sine_wav(double seconds, double amp) {
    const u32 rate = 16000, frames = u32(seconds * rate);
    Bytes data;
    for (u32 f = 0; f < frames; ++f) {
        const s16 v = s16(std::lround(amp * 32767.0 * std::sin(2 * 3.141592653589793 * 997.0 * f / rate)));
        put16(data, u16(v)), put16(data, u16(v));
    }
    Bytes b;
    put(b, "RIFF");
    put32(b, u32(36 + data.size()));
    put(b, "WAVE");
    put(b, "fmt ");
    put32(b, 16);
    put16(b, 1), put16(b, 2);
    put32(b, rate), put32(b, rate * 4);
    put16(b, 4), put16(b, 16);
    put(b, "data");
    put32(b, u32(data.size()));
    b.insert(b.end(), data.begin(), data.end());
    return b;
}

// Polls until the scan and the measuring are done (and their results taken in).
bool wait_measured(MusicLibrary& lib) {
    for (int i = 0; i < 4000; ++i) {
        lib.poll();
        if (!lib.scanning() && !lib.measuring()) {
            lib.poll();
            return true;
        }
        SDL_Delay(5);
    }
    return false;
}

const race::CustomSong* song_at(const MusicLibrary& lib, std::string_view rel) {
    for (const race::CustomSong& s : lib.songs())
        if (s.rel == rel) return &s;
    return nullptr;
}

// The remembered song list: written after a scan, read at the next start, whose songs are there
// at once (before its own scan lands); a list of another folder isn't used; the text keeps tabs
// and line breaks, and anything that isn't a list is no list.
TEST(music_song_list_remembered) {
    const std::filesystem::path d = test::temp_dir("music_song_list");
    write(d / "Album" / "one.wav", wav(4000, 2, 8000, {{"INAM", "Tab\there"}}));
    write(d / "two.wav", sine_wav(1.0, 0.1));
    const std::string list_file = utf8(test::temp_path("song_list.txt"));
    {
        MusicLibrary lib;
        lib.set_playlist_file(utf8(test::temp_path("song_list_playlist.txt")));
        lib.set_song_list_file(list_file);
        lib.set_folder(utf8(d));
        CHECK(!lib.has_songs()); // nothing remembered yet
        CHECK(settle(lib) && lib.songs().size() == 2);
        CHECK(wait_measured(lib) && lib.measured() == 0); // only songs asked for are measured
        for (const race::CustomSong& song : lib.songs()) lib.want_measured(song.id);
        CHECK(wait_measured(lib) && lib.measured() == 2);
    }
    CHECK(read_host_file(list_file).has_value());
    MusicLibrary again;
    again.set_playlist_file(utf8(test::temp_path("song_list_playlist.txt")));
    again.set_song_list_file(list_file);
    again.set_folder(utf8(d));
    CHECK(again.scanning() && again.songs().size() == 2 && again.measured() == 2); // before the scan
    if (const race::CustomSong* one = song_at(again, "Album/one.wav")) CHECK(one->title == "Tab\there");
    else CHECK(false);
    const u32 gen = again.generation();
    CHECK(again.poll()); // the remembered songs, reported once
    CHECK(settle(again) && again.songs().size() == 2 && again.generation() == gen); // the same songs
    MusicLibrary other;
    other.set_song_list_file(list_file);
    other.set_folder(utf8(d / "Album"));
    CHECK(!other.has_songs());
    CHECK(settle(other) && other.songs().size() == 1);

    race::CustomSong s;
    s.id = 0x0123456789ABCDEFull, s.path = "/a\\b\nc", s.rel = "b\tc", s.title = "T\r", s.artist = "";
    race::CustomSong measured = s;
    measured.id = 7, measured.measured = true, measured.lufs = -11.5f, measured.peak = 0.75f;
    race::CustomSong none = s;
    none.id = 8, none.measured = true;
    const std::string text = MusicLibrary::song_list_text("/music/", {s, measured, none}, "k\t1", -14.25f);
    std::optional<MusicLibrary::SongList> back = MusicLibrary::parse_song_list(text);
    CHECK(back && back->folder == "/music/" && back->disc_key == "k\t1" && back->disc_lufs == -14.25f);
    CHECK(back && back->songs.size() == 3);
    if (back && back->songs.size() == 3) {
        CHECK(back->songs[0].id == s.id && back->songs[0].path == s.path && back->songs[0].rel == s.rel &&
              back->songs[0].title == s.title && !back->songs[0].measured);
        CHECK(back->songs[1].measured && back->songs[1].lufs == -11.5f && back->songs[1].peak == 0.75f);
        CHECK(back->songs[2].measured && back->songs[2].peak == 0.0f);
    }
    CHECK(!MusicLibrary::parse_song_list("not a song list\n"));
    CHECK(!MusicLibrary::parse_song_list(""));
    // Damaged lines are passed over.
    std::optional<MusicLibrary::SongList> rough =
        MusicLibrary::parse_song_list("GT3R music songs 1\nfolder\t/m/\nzz\t-\t-\tp\tr\tt\ta\n0000000000000001\t-\t-\t/m/x.wav\tx.wav\tx\t\n");
    CHECK(rough && rough->songs.size() == 1 && rough->songs[0].path == "/m/x.wav");
}

// Loudness: the disc's race songs measured (their mean), then the folder's; a song plays at the
// disc's loudness over its own, from -20 to +12 dB, capped by its peak; songs not measured play
// at 1; both are remembered, the disc's with its key only.
TEST(music_loudness_gain) {
    const std::filesystem::path d = test::temp_dir("music_loudness");
    write(d / "loud.wav", sine_wav(3.0, 0.5));   // -6.0 LUFS
    write(d / "quiet.wav", sine_wav(3.0, 0.02)); // -34.0 LUFS
    const std::string list_file = utf8(test::temp_path("loudness_songs.txt"));
    auto disc_song = [](double amp) -> MusicLibrary::StreamOpener {
        auto bytes = std::make_shared<const Bytes>(sine_wav(3.0, amp));
        return [bytes] { return open_music_memory(bytes, MusicFileType::Wav, false); };
    };
    {
        MusicLibrary lib;
        lib.set_song_list_file(list_file);
        lib.set_folder(utf8(d));
        CHECK(settle(lib) && lib.songs().size() == 2);
        const race::CustomSong* loud = song_at(lib, "loud.wav");
        CHECK(loud && lib.gain(*loud) == 1.0f); // the disc's level not known yet
        lib.set_disc_songs("test disc", {disc_song(0.1), disc_song(0.05)}); // -20, -26.02
        for (const race::CustomSong& song : lib.songs()) lib.want_measured(song.id);
        CHECK(wait_measured(lib));
        CHECK(lib.disc_loudness() && std::fabs(*lib.disc_loudness() - -23.01f) < 0.2f);
        loud = song_at(lib, "loud.wav");
        const race::CustomSong* quiet = song_at(lib, "quiet.wav");
        CHECK(loud && loud->measured && std::fabs(loud->lufs - -6.02f) < 0.2f);
        CHECK(quiet && quiet->measured && std::fabs(quiet->lufs - -33.98f) < 0.2f);
        if (loud && quiet) {
            CHECK(std::fabs(lib.gain(*loud) - 0.1413f) < 0.01f);  // -17 dB
            CHECK(std::fabs(lib.gain(*quiet) - 3.535f) < 0.15f); // +11 dB
        }
        race::CustomSong peaky;
        peaky.measured = true, peaky.lufs = -35.0f, peaky.peak = 0.9f; // +12 dB wanted, its peak allows less
        CHECK(std::fabs(lib.gain(peaky) - 1.0f / (0.9f * bgm::kReplaySongVolume)) < 0.001f);
        race::CustomSong blaring = peaky;
        blaring.lufs = 5.0f; // -28 dB wanted: -20 at most
        CHECK(std::fabs(lib.gain(blaring) - 0.1f) < 0.001f);
        race::CustomSong unmeasured;
        CHECK(lib.gain(unmeasured) == 1.0f);
    }
    MusicLibrary again;
    again.set_song_list_file(list_file);
    again.set_folder(utf8(d));
    again.set_disc_songs("test disc", {}); // the same key: its level remembered
    CHECK(again.disc_loudness() && std::fabs(*again.disc_loudness() - -23.01f) < 0.2f);
    const race::CustomSong* loud = song_at(again, "loud.wav");
    CHECK(loud && loud->measured && std::fabs(again.gain(*loud) - 0.1413f) < 0.01f);
    again.set_disc_songs("another disc", {});
    CHECK(!again.disc_loudness());
    CHECK(settle(again));
}

// Long lists: L2 / R2 move the cursor a window of rows (and carry a lifted song so far); a drag
// or the wheel scrolls the list without moving the cursor, within its ends.
TEST(music_list_long_lists) {
    using namespace gt3r::menu::flow;
    using Sounds = std::vector<std::string>;
    std::vector<u64> ids;
    std::vector<MusicTrack> tracks;
    for (u64 i = 1; i <= 40; ++i) ids.push_back(i), tracks.push_back({i, "S" + std::to_string(i), ""});
    MusicListPage m;
    m.open(RacePlaylist::with_songs(ids, 3), tracks, true);
    auto key = [&](MenuKey k) {
        Sounds s;
        m.tick(key_bit(k), key_bit(k), s);
        return s;
    };
    // 308 units of window, 24 a row: 12 rows in view, 11 a jump.
    CHECK(key(MenuKey::L2) == Sounds{"disable"} && m.focus() == 0);
    CHECK(key(MenuKey::R2) == Sounds{"cursor"} && m.focus() == 11);
    CHECK(key(MenuKey::R2) == Sounds{"cursor"} && key(MenuKey::R2) == Sounds{"cursor"} && m.focus() == 33);
    CHECK(key(MenuKey::R2) == Sounds{"cursor"} && m.focus() == 40); // the Order / Shuffle row
    CHECK(key(MenuKey::R2) == Sounds{"disable"});
    CHECK(key(MenuKey::L2) == Sounds{"cursor"} && m.focus() == 29);
    // A lifted song carried a jump, then to the last song.
    key(MenuKey::Right);
    CHECK(key(MenuKey::Confirm) == Sounds{"ok"} && m.lifted());
    const u64 lifted = m.songs()[29].id;
    CHECK(key(MenuKey::R2) == Sounds{"cursor"} && m.focus() == 39 && m.songs()[39].id == lifted); // (as far as the last song)
    CHECK(key(MenuKey::R2) == Sounds{"disable"} && m.focus() == 39);
    key(MenuKey::Confirm);
    // Scrolling: the rows move, the cursor stays.
    Sounds none;
    for (int i = 0; i < 30; ++i) m.tick(0, 0, none); // (the jumps' scroll settles)
    const SDL_FRect area{28, 44, 580, 308};
    const float at = m.scroll();
    CHECK(m.scroll_by(area, 300, 200, -100) && std::fabs(m.scroll() - (at - 100)) < 0.01f && m.focus() == 39);
    CHECK(m.scroll_by(area, 300, 200, -100000) && m.scroll() == 0);
    CHECK(m.scroll_by(area, 300, 200, 100000) && m.scroll() == 40 * 24 + 32 - 308);
    CHECK(!m.scroll_by(area, 10, 200, 50)); // off the list
}

// Songs asked for: the latest first, a few kept, one measured or unknown not asked again; the
// playlist's peek is the song next() takes, also across a new pass.
TEST(music_want_measured) {
    const std::filesystem::path d = test::temp_dir("music_want");
    for (int i = 0; i < 6; ++i) write(d / ("s" + std::to_string(i) + ".wav"), sine_wav(0.6, 0.1));
    MusicLibrary lib;
    lib.set_folder(utf8(d));
    CHECK(settle(lib) && lib.songs().size() == 6);
    lib.racing();          // (only the disc's songs wait for races)
    lib.want_measured(1);  // no such song
    for (const race::CustomSong& s : lib.songs()) lib.want_measured(s.id);
    CHECK(wait_measured(lib));
    const size_t n = lib.measured();
    CHECK(n >= 4 && n <= 5); // 4 kept, and maybe one taken before the last asks came
    CHECK(lib.songs().back().measured && lib.songs()[size_t(2)].measured); // the latest ones
    const u32 gen = lib.generation();
    lib.want_measured(lib.songs().back().id); // measured: nothing to do
    CHECK(!lib.measuring() && lib.generation() == gen);

    RacePlaylist list = RacePlaylist::with_songs({1, 2, 3, 4}, 77);
    for (int i = 0; i < 9; ++i) {
        const std::optional<size_t> p = list.peek();
        CHECK(p && p == list.next());
    }
}

// No access to the folder (Android's permission taken back): the library reads none, which keeps
// the playlist and its file. same_folder sees through "..", and needs both folders there.
TEST(music_no_folder_and_same_folder) {
    const std::filesystem::path d = test::temp_dir("music_no_folder");
    write(d / "one.wav", wav(10, 1, 8000));
    MusicLibrary lib;
    const std::string list_file = utf8(test::temp_path("no_folder_playlist.txt"));
    lib.set_playlist_file(list_file);
    CHECK(lib.set_folder(utf8(d)) && settle(lib) && lib.songs().size() == 1);
    CHECK(!lib.set_folder(utf8(d))); // the same folder: no new scan
    lib.save_playlist();
    const std::optional<Bytes> saved = read_host_file(list_file);
    CHECK(saved.has_value());
    CHECK(lib.set_folder("") && settle(lib) && lib.songs().empty() && !lib.folder_found());
    CHECK(lib.playlist().songs().size() == 1 && read_host_file(list_file) == saved);
    CHECK(lib.set_folder(utf8(d)) && settle(lib) && lib.songs().size() == 1);

    std::filesystem::create_directories(d / "sub");
    CHECK(race::same_folder(utf8(d), utf8(d / "sub" / "..")));
    CHECK(race::same_folder(utf8(d) + "/", utf8(d)));
    CHECK(!race::same_folder(utf8(d), utf8(d / "sub")));
    CHECK(!race::same_folder(utf8(d / "nowhere"), utf8(d / "nowhere")));
    CHECK(!race::same_folder("", utf8(d)));
}

#ifndef _WIN32
// Linked folders are followed, each folder once (a link back up doesn't loop); a linked file is
// one song; a folder the scan may not read is passed over.
TEST(music_scan_links_and_permissions) {
    namespace fs = std::filesystem;
    const fs::path d = test::temp_dir("music_scan_links"), outside = test::temp_dir("music_scan_links_outside");
    write(outside / "linked.wav", wav(10, 1, 8000));
    write(d / "own.wav", wav(11, 1, 8000));
    std::error_code ec;
    fs::create_directory_symlink(outside, d / "Linked folder", ec);
    if (ec) SKIP("no symlinks here");
    fs::create_directory_symlink(d, d / "loop", ec);
    fs::create_symlink(d / "own.wav", d / "own again.wav", ec);
    MusicLibrary::ScanInfo info;
    std::vector<race::CustomSong> songs = MusicLibrary::scan(utf8(d), info);
    CHECK(songs.size() == 2);
    if (songs.size() == 2) CHECK(songs[0].rel == "Linked folder/linked.wav" && songs[1].rel == "own.wav");
    write(d / "Locked" / "secret.wav", wav(12, 1, 8000));
    fs::permissions(d / "Locked", fs::perms::none, ec);
    std::error_code probe;
    fs::directory_iterator(d / "Locked", probe);
    const bool locked = !ec && probe; // (not when running as root)
    songs = MusicLibrary::scan(utf8(d), info);
    fs::permissions(d / "Locked", fs::perms::owner_all, ec);
    CHECK(songs.size() == (locked ? 2u : 3u));
}
#endif

// Odd files: every WAV sample format dr_wav reads, mono to 8 channels, 8 to 384 kHz; what the
// output can't take (9 channels, 4 kHz) refused; truncated and empty files; MP3 and FLAC.
TEST(music_file_odd_files) {
    auto opens = [](const Bytes& b, MusicFileType t) { return open_music_memory(std::make_shared<const Bytes>(b), t, false); };
    auto frames = [](MusicStream& s) {
        std::vector<s16> buf(1024 * s.channels());
        size_t n = 0;
        for (size_t got; (got = s.read(buf.data(), 1024)) > 0 && n < 10'000'000;) n += got;
        return n;
    };
    // A WAV header with `format`, `bits` and `channels` around `data`.
    auto wav_of = [](u16 format, u16 channels, u32 rate, u16 bits, const Bytes& data) {
        Bytes b;
        put(b, "RIFF");
        put32(b, u32(36 + data.size()));
        put(b, "WAVEfmt ");
        put32(b, 16);
        put16(b, format);
        put16(b, channels);
        put32(b, rate);
        put32(b, rate * channels * bits / 8);
        put16(b, u16(channels * bits / 8));
        put16(b, bits);
        put(b, "data");
        put32(b, u32(data.size()));
        b.insert(b.end(), data.begin(), data.end());
        return b;
    };
    struct Case {
        u16 format, channels;
        u32 rate;
        u16 bits;
        bool plays;
    };
    for (const Case& c : {Case{1, 1, 8000, 8, true}, Case{1, 2, 44100, 24, true}, Case{1, 2, 48000, 32, true},
                          Case{3, 2, 44100, 32, true}, Case{3, 1, 96000, 64, true}, Case{1, 8, 384000, 16, true},
                          Case{1, 9, 44100, 16, false}, Case{1, 2, 4000, 16, false}}) {
        const Bytes data(size_t(100) * c.channels * c.bits / 8, 0x10);
        std::unique_ptr<MusicStream> s = opens(wav_of(c.format, c.channels, c.rate, c.bits, data), MusicFileType::Wav);
        CHECK((s != nullptr) == c.plays);
        if (s) CHECK(s->channels() == c.channels && s->sample_rate() == c.rate && frames(*s) == 100);
    }
    // Float samples past full scale: clipped, not wrapped.
    {
        Bytes data;
        for (float f : {2.0f, -2.0f}) {
            u32 bits;
            std::memcpy(&bits, &f, 4);
            put32(data, bits);
        }
        std::unique_ptr<MusicStream> s = opens(wav_of(3, 1, 44100, 32, data), MusicFileType::Wav);
        std::vector<s16> buf(2);
        CHECK(s && s->read(buf.data(), 2) == 2 && buf[0] == 32767 && buf[1] <= -32767);
    }
    // A data chunk that claims more than the file has: what is there plays.
    {
        Bytes b = wav(100, 2, 44100);
        b.resize(b.size() - 200);
        std::unique_ptr<MusicStream> s = opens(b, MusicFileType::Wav);
        CHECK(s && frames(*s) == 50);
    }
    CHECK(!opens({}, MusicFileType::Wav) && !opens({}, MusicFileType::Mp3) && !opens({}, MusicFileType::Flac));
    CHECK(!opens(bytes_of("RIFF"), MusicFileType::Wav) && !opens(Bytes(4096, 0), MusicFileType::Mp3));
    // The tiny MP3 (Xing header: its length known) and FLAC; a WAV named .mp3 doesn't open as one.
    const Bytes mp3(test::kTinyMp3, test::kTinyMp3 + sizeof test::kTinyMp3), flac(test::kTinyFlac, test::kTinyFlac + sizeof test::kTinyFlac);
    std::unique_ptr<MusicStream> m = opens(mp3, MusicFileType::Mp3);
    CHECK(m && m->sample_rate() == 22050 && m->channels() == 1);
    if (m) {
        const size_t n = frames(*m);
        CHECK(n > 3000 && n < 5000 && m->total_frames() > 3000);
    }
    std::unique_ptr<MusicStream> f = opens(flac, MusicFileType::Flac);
    CHECK(f && f->sample_rate() == 8000 && f->channels() == 1 && f->total_frames() == 800 && frames(*f) == 800);
    CHECK(!opens(wav(10, 1, 8000), MusicFileType::Mp3) || true); // (dr_mp3 may find a frame in anything: no crash)
    CHECK(!opens(mp3, MusicFileType::Flac) && !opens(flac, MusicFileType::Wav));
    // Truncated in the middle: fewer frames, no crash; a big ID3 tag (cover art) first: skipped.
    {
        Bytes cut(mp3.begin(), mp3.begin() + std::ptrdiff_t(mp3.size() / 2));
        std::unique_ptr<MusicStream> s = opens(cut, MusicFileType::Mp3);
        if (s) CHECK(frames(*s) < 4000);
        Bytes half_flac(flac.begin(), flac.begin() + std::ptrdiff_t(flac.size() / 2));
        std::unique_ptr<MusicStream> h = opens(half_flac, MusicFileType::Flac);
        if (h) CHECK(frames(*h) < 800);
        Bytes art = id3(3, {{"TIT2", 0, bytes_of("Cover")}, {"APIC", 0, Bytes(300000, 0xAB)}});
        Bytes tagged = art;
        tagged.insert(tagged.end(), mp3.begin(), mp3.end());
        std::unique_ptr<MusicStream> t = opens(tagged, MusicFileType::Mp3);
        CHECK(t && frames(*t) > 3000);
        const std::filesystem::path p = test::temp_path("art.mp3");
        write(p, tagged);
        CHECK(read_music_tags(utf8(p), MusicFileType::Mp3).title == "Cover" && music_file_decodes(utf8(p)));
    }
}

TEST(custom_playlist_sync) {
    RacePlaylist p = RacePlaylist::with_songs({1, 2, 3}, 7, RacePlaylist::kMaxCustomSongs);
    p.set_listed(1, false);
    p.rebuild();
    CHECK(p.remaining() == 2);
    CHECK(!p.sync({1, 2, 3}));
    // 2 goes, 4 comes: the others keep their marks and place, the new one at the end, listed.
    CHECK(p.sync({4, 3, 1}));
    CHECK(p.songs().size() == 3 && p.songs()[0].id == 1 && p.songs()[1].id == 3 && p.songs()[2].id == 4);
    CHECK(p.songs()[2].listed && p.remaining() == 3 && p.cursor() == 0);
    // Stored with the custom limit (more than the disc's 64).
    std::vector<u64> many;
    for (u64 i = 1; i <= 100; ++i) many.push_back(i);
    RacePlaylist big = RacePlaylist::with_songs(many, 1, RacePlaylist::kMaxCustomSongs);
    CHECK(big.songs().size() == 100);
    CHECK(!RacePlaylist::from_text(big.to_text()));
    std::optional<RacePlaylist> back = RacePlaylist::from_text(big.to_text(), RacePlaylist::kMaxCustomSongs);
    CHECK(back && back->songs().size() == 100 && *back == big);
    CHECK(RacePlaylist::with_songs(many, 1).songs().size() == RacePlaylist::kMaxSongs);
    // Every song once per pass, however many.
    std::vector<int> seen(101, 0);
    for (int i = 0; i < 100; ++i)
        if (std::optional<size_t> k = big.next()) ++seen[size_t(big.songs()[*k].id)];
    CHECK(std::count(seen.begin() + 1, seen.end(), 1) == 100);
}

TEST(custom_music_mix) {
    using race::MusicSource;
    CHECK(race::parse_music_source("custom") == MusicSource::Custom && race::parse_music_source("both") == MusicSource::Both);
    CHECK(race::parse_music_source("x") == MusicSource::Original && race::music_source_name(MusicSource::Both) == "both");
    CHECK(race::effective_music_source(MusicSource::Both, false) == MusicSource::Original);
    CHECK(race::effective_music_source(MusicSource::Custom, true) == MusicSource::Custom);
    // Order takes turns; one list empty: the other one.
    CHECK(race::pick_custom(false, 5, 5, true, true, false, 0.9) && !race::pick_custom(false, 5, 5, true, true, true, 0.0));
    CHECK(!race::pick_custom(true, 5, 5, true, false, false, 0.0) && race::pick_custom(true, 5, 5, false, true, true, 0.9));
    // Shuffle: in proportion to the songs left in each pass.
    CHECK(race::pick_custom(true, 1, 3, true, true, false, 0.7) && !race::pick_custom(true, 1, 3, true, true, false, 0.8));
    int custom = 0;
    for (int i = 0; i < 100; ++i) custom += race::pick_custom(true, 20, 80, true, true, false, (i + 0.5) / 100.0);
    CHECK(custom == 80);
}

TEST(custom_music_menu_ascii) {
    CHECK(race::menu_ascii("Caf\xc3\xa9 Stra\xc3\x9f" "e") == "Cafe Strasse");
    CHECK(race::menu_ascii("\xc5\x81\xc3\xb3" "d\xc5\xba") == "Lodz");
    CHECK(race::menu_ascii("Don\xe2\x80\x99t \xe2\x80\x94 Stop\xe2\x80\xa6") == "Don't - Stop...");
    CHECK(race::menu_ascii("\xe6\x9d\xb1\xe4\xba\xac") == "??" && race::menu_ascii("bad \xc3") == "bad ?");
}

// Race songs from the player's music: Custom plays them, Next / Previous move through them (back
// through the ones played, then forward again), a skip while paused waits for the resume.
TEST(race_music_custom_skip) {
    const std::filesystem::path d = test::temp_dir("music_skip");
    for (const char* n : {"one", "two", "three"}) write(d / (std::string(n) + ".wav"), wav(44100, 2, 44100, {{"INAM", n}}));
    MusicLibrary lib;
    lib.set_playlist_file(utf8(test::temp_path("skip_playlist.txt")));
    lib.set_folder(utf8(d));
    for (int i = 0; i < 500 && !lib.poll(); ++i) SDL_Delay(5);
    CHECK(lib.songs().size() == 3);
    lib.playlist().set_shuffle(false); // in order: one, three, two (sorted by file name)

    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) SKIP("no audio");
    {
        AudioEngine audio;
        if (!audio.ok()) SKIP("no audio device");
        Vfs vfs(nullptr);
        race::RaceMusic music;
        music.load_playlist(audio, vfs, std::nullopt); // no disc: no disc songs
        music.set_custom(&lib, race::MusicSource::Custom, true);
        CHECK(music.source() == race::MusicSource::Custom && music.has_songs());
        race::RaceMusic::Cues c;
        c.songs = music.has_songs();
        c.start_type = 0;
        music.cues(audio, vfs, c);
        auto title = [&] {
            std::optional<race::RaceMusic::SongInfo> s = music.now_playing(audio);
            return s ? s->title : std::string("-");
        };
        CHECK(title() == "one" && music.now_playing(audio)->custom);
        // The player's song has no markers of its own: the fallback ones time the replay cuts.
        music.step_markers(audio);
        CHECK(music.markers().marked() && music.markers().event(1) && music.markers().interval(1) < 80.0f); // (1 s: beats only)
        CHECK(music.take_custom_change() && !music.take_custom_change());
        CHECK(music.skip(audio, vfs, 1) && title() == "three");
        CHECK(music.skip(audio, vfs, -1) && title() == "one");
        CHECK(music.skip(audio, vfs, -1) && title() == "one"); // none before it: from its start
        CHECK(music.skip(audio, vfs, 1) && title() == "three"); // forward through the songs played
        CHECK(music.skip(audio, vfs, 1) && title() == "two");   // then new ones
        // Paused: the skip is taken (shown) and the race's resume plays it.
        c.paused = true;
        music.cues(audio, vfs, c);
        CHECK(music.skip(audio, vfs, 1) && title() == "one");
        c.paused = false;
        music.cues(audio, vfs, c);
        CHECK(title() == "one");
        // The race BGM switch off: nothing shown, nothing to skip.
        c.music_on = false;
        music.cues(audio, vfs, c);
        CHECK(!music.now_playing(audio) && !music.skip(audio, vfs, 1));
        // Original without disc songs: no race music at all.
        music.set_custom(nullptr, race::MusicSource::Custom, true);
        CHECK(music.source() == race::MusicSource::Original && !music.has_songs());
        music.stop(audio);
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// A race song of the player's not measured yet starts as it is and is eased to its level once
// measured (6 dB a second); the next song of its list is measured meanwhile.
TEST(race_music_eases_to_level) {
    const std::filesystem::path d = test::temp_dir("music_ease");
    write(d / "a.wav", sine_wav(20.0, 0.5)); // -6 LUFS: 17 dB over the disc's -23
    write(d / "b.wav", sine_wav(0.6, 0.5));
    MusicLibrary lib;
    lib.set_folder(utf8(d));
    CHECK(settle(lib) && lib.songs().size() == 2);
    lib.playlist().set_shuffle(false);
    auto disc_song = [](double amp) -> MusicLibrary::StreamOpener {
        auto bytes = std::make_shared<const Bytes>(sine_wav(1.0, amp));
        return [bytes] { return open_music_memory(bytes, MusicFileType::Wav, false); };
    };
    lib.set_disc_songs("ease disc", {disc_song(0.1), disc_song(0.05)});
    CHECK(wait_measured(lib) && lib.disc_loudness() && lib.measured() == 0);

    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) SKIP("no audio");
    {
        AudioEngine audio;
        if (!audio.ok()) SKIP("no audio device");
        Vfs vfs(nullptr);
        race::RaceMusic music;
        music.load_playlist(audio, vfs, std::nullopt);
        music.set_custom(&lib, race::MusicSource::Custom, false);
        race::RaceMusic::Cues c;
        c.songs = music.has_songs();
        c.start_type = 0;
        music.cues(audio, vfs, c);
        CHECK(music.now_playing(audio) && music.now_playing(audio)->title == "a");
        CHECK(audio.stream_trim() == 1.0f); // not measured: as it is
        const race::CustomSong* a = song_at(lib, "a.wav");
        for (int i = 0; i < 400 && !(a && a->measured); ++i) {
            lib.poll();
            music.cues(audio, vfs, c);
            SDL_Delay(5);
            a = song_at(lib, "a.wav");
        }
        CHECK(a && a->measured && lib.gain(*a) < 0.15f);
        float before = audio.stream_trim();
        SDL_Delay(50);
        music.cues(audio, vfs, c);
        CHECK(audio.stream_trim() < before && audio.stream_trim() > lib.gain(*a)); // easing down, not a jump
        for (int i = 0; i < 400 && audio.stream_trim() != lib.gain(*a); ++i) {
            SDL_Delay(10);
            music.cues(audio, vfs, c);
        }
        CHECK(a && audio.stream_trim() == lib.gain(*a));
        CHECK(wait_measured(lib));
        const race::CustomSong* b = song_at(lib, "b.wav");
        CHECK(b && b->measured); // the next one, asked for ahead of time
        music.stop(audio);
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// [music] next_race: with the same song, a run starts with the one the last run ended on (from
// another screen through its saved text, or the last run here), passed over when it can't play;
// without, a new song every run, as GT3 does.
TEST(race_music_next_race_song) {
    const std::filesystem::path d = test::temp_dir("music_next_race");
    for (const char* n : {"one", "two", "three"}) write(d / (std::string(n) + ".wav"), wav(44100, 2, 44100, {{"INAM", n}}));
    MusicLibrary lib;
    lib.set_playlist_file(utf8(test::temp_path("next_race_playlist.txt")));
    lib.set_folder(utf8(d));
    for (int i = 0; i < 500 && !lib.poll(); ++i) SDL_Delay(5);
    CHECK(lib.songs().size() == 3);
    lib.playlist().set_shuffle(false); // one, three, two

    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) SKIP("no audio");
    {
        AudioEngine audio;
        if (!audio.ok()) SKIP("no audio device");
        Vfs vfs(nullptr);
        race::RaceMusic::Cues c;
        c.start_type = 0;
        auto run = [&](race::RaceMusic& music) { // a run: its first song's title
            music.reset();
            c.songs = music.has_songs();
            music.cues(audio, vfs, c);
            std::optional<race::RaceMusic::SongInfo> s = music.now_playing(audio);
            return s ? s->title : std::string("-");
        };
        auto make = [&](race::RaceMusic& music, race::MusicSource src) {
            music.load_playlist(audio, vfs, std::nullopt);
            music.set_custom(&lib, src, false);
        };
        std::string saved;
        {
            race::RaceMusic music; // the same song
            make(music, race::MusicSource::Custom);
            music.set_first_song("");
            CHECK(run(music) == "one");
            std::optional<std::string> text = music.take_song_change();
            CHECK(text && text->rfind("yours:", 0) == 0 && !music.take_song_change());
            CHECK(music.skip(audio, vfs, 1)); // three, cut off by the finish
            saved = music.take_song_change().value_or("");
            music.stop(audio);
            CHECK(run(music) == "three"); // the restart: from its start
            music.stop(audio);
        }
        {
            race::RaceMusic music; // the next screen: from the saved text
            make(music, race::MusicSource::Custom);
            music.set_first_song(saved);
            CHECK(run(music) == "three");
            music.stop(audio);
        }
        {
            race::RaceMusic music; // GT3's rule: the next song of the list
            make(music, race::MusicSource::Custom);
            const std::string first = run(music);
            music.stop(audio);
            CHECK(first != "-" && run(music) != first);
            music.stop(audio);
        }
        {
            race::RaceMusic music; // one of yours with Original playing: passed over
            make(music, race::MusicSource::Original);
            music.set_first_song(saved);
            CHECK(run(music) == "-"); // (no disc songs here: no race music)
            music.stop(audio);
        }
        {
            race::RaceMusic music; // out of the list now: passed over; not a song's text: none
            make(music, race::MusicSource::Custom);
            std::vector<RacePlaylist::Song> songs = lib.playlist().songs(); // unchecked on the Options page
            for (RacePlaylist::Song& song : songs)
                if (lib.find(song.id)->title == "three") song.listed = false;
            lib.playlist().commit(songs, false);
            music.set_first_song(saved);
            CHECK(run(music) == "one");
            music.stop(audio);
            race::RaceMusic other;
            make(other, race::MusicSource::Custom);
            other.set_first_song("yours:zz");
            CHECK(run(other) != "-");
            other.stop(audio);
        }
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// The fallback markers: the disc songs' pace, the same for the same song, cut points on beats.
TEST(fallback_song_markers) {
    const sound::MadsMarkers m = race::fallback_markers(240, 42);
    CHECK(m.rate == 1000 && m.length == 240000 && m.any());
    const size_t beats = m.tracks[1].size(), bars = m.tracks[2].size(), cuts = m.tracks[3].size();
    CHECK(beats == 519 && bars == 129 && m.tracks[4] == m.tracks[3]);
    CHECK(m.tracks[1][0] == 462 && m.tracks[2][0] == 1846 && m.tracks[3][0] >= 462); // from the first beat
    const double mean = 240.0 / double(cuts);
    CHECK(mean > 1.3 && mean < 2.1);
    double longest = 0;
    for (size_t i = 0; i < cuts; ++i) {
        CHECK(std::binary_search(m.tracks[1].begin(), m.tracks[1].end(), m.tracks[3][i]));
        if (i) longest = std::max(longest, m.seconds(3, i) - m.seconds(3, i - 1));
    }
    CHECK(longest < 3.8);
    CHECK(race::fallback_markers(240, 42).tracks[3] == m.tracks[3] && race::fallback_markers(240, 7).tracks[3] != m.tracks[3]);
    CHECK(race::fallback_markers(0, 1).length == 30 * 60 * 1000); // length not known: 30 minutes
    // The replay cameras get shot lengths from them, not the 2 s countdown (over 80 s: none).
    race::MusicMarkers mk;
    mk.start(&m);
    mk.step(10.0);
    race::CutClock clock;
    clock.setup(0.1f, 0);
    CHECK(clock.update_shot_length(&mk, 1) < 4.0f && clock.update_shot_length(&mk, 0) < 2.0f);
}

TEST(chord_parse_and_save) {
    Chord c;
    CHECK(Chord::parse("pad.back+pad.rightshoulder", c) && c.inputs.size() == 2 && c.set() == BindingSet::Controller);
    CHECK(c.to_string() == "pad.back+pad.rightshoulder");
    CHECK(Chord::parse("key.left_ctrl + key.n", c) && c.set() == BindingSet::Keyboard && c.inputs.size() == 2);
    CHECK(!Chord::parse("key.n+pad.south", c));      // mixed sets
    CHECK(!Chord::parse("", c) && !Chord::parse("pad.nothing", c));
    CHECK(!Chord::parse("key.a+key.b+key.c+key.d+key.e", c)); // more than four
    CHECK(Chord::parse("pad.lefttrigger+pad.leftx-", c) && c.inputs[1].half == InputSource::Half::Negative);
    CHECK(chord_input_held(c.inputs[0], 0.6f) && !chord_input_held(c.inputs[0], 0.4f));
    CHECK(chord_input_held(c.inputs[1], -0.7f) && !chord_input_held(c.inputs[1], 0.7f));

    ActionBindings a;
    Chord pad, key, other;
    CHECK(Chord::parse("pad.back+pad.rightshoulder", pad) && Chord::parse("key.j", key));
    CHECK(Chord::parse("joy.button3", other));
    other.inputs[0].device = "Wheel+Pedals 2@p:usb 1";
    a.bind(PadAction::NextSong, pad, true);
    a.bind(PadAction::NextSong, key, true);
    a.bind(PadAction::PreviousSong, other, false);
    CHECK(a.of(PadAction::NextSong).size() == 2);
    IniDocument cfg;
    a.save(cfg, "player1");
    CHECK(cfg.get("player1.actions", "next_song") == "pad.back+pad.rightshoulder | key.J");
    ActionBindings b;
    b.load(cfg, "player1");
    CHECK(b.chords == a.chords && b.of(PadAction::PreviousSong)[0].inputs[0].device == "Wheel+Pedals 2@p:usb 1");
    // Replace takes only its own set's chords; clear one set.
    Chord pad2;
    CHECK(Chord::parse("pad.start+pad.leftshoulder", pad2));
    b.bind(PadAction::NextSong, pad2, true);
    CHECK(b.of(PadAction::NextSong).size() == 2 && b.of(PadAction::NextSong)[1] == pad2);
    b.clear(PadAction::NextSong, BindingSet::Keyboard);
    CHECK(b.of(PadAction::NextSong).size() == 1);
    b.clear(PadAction::NextSong, BindingSet::Controller);
    b.clear(PadAction::PreviousSong, BindingSet::Controller);
    b.save(cfg, "player1");
    CHECK(!cfg.find("player1.actions"));
    CHECK(pad_action_name(PadAction::PreviousSong) == "Previous song" && pad_action_key(PadAction::NextSong) == "next_song");
}

// A chord through the input manager: captured from the keys pressed until all are let go (Esc
// first cancels), saved per player, and firing once when all of its keys are down.
TEST(input_chord_capture_and_fire) {
    auto key = [](SDL_Scancode sc) {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.scancode = sc;
        return e;
    };
    InputManager in;
    in.begin_chord_capture(0);
    in.handle_event(key(SDL_SCANCODE_ESCAPE));
    CHECK(!in.capturing() && !in.poll_chord());
    in.begin_chord_capture(0);
    in.handle_event(key(SDL_SCANCODE_LCTRL));
    in.handle_event(key(SDL_SCANCODE_N));
    in.handle_event(key(SDL_SCANCODE_ESCAPE)); // after other keys: not a cancel, not an input
    CHECK(in.chord_so_far().size() == 2);
    in.update(1.0 / 60); // still held (this frame's presses)
    CHECK(in.capturing() && !in.poll_chord());
    in.update(1.0 / 60); // let go
    std::optional<Chord> c = in.poll_chord();
    CHECK(c && c->inputs.size() == 2 && c->set() == BindingSet::Keyboard && !in.capturing());
    if (!c) return;
    in.bind_action(0, PadAction::NextSong, *c, true);
    in.handle_event(key(SDL_SCANCODE_N));
    in.update(1.0 / 60);
    CHECK(!in.action_pressed(0, PadAction::NextSong)); // one key of two
    in.handle_event(key(SDL_SCANCODE_LCTRL));
    in.handle_event(key(SDL_SCANCODE_N));
    in.update(1.0 / 60);
    CHECK(in.action_pressed(0, PadAction::NextSong) && in.action_held(0, PadAction::NextSong));
    CHECK(!in.action_pressed(0, PadAction::PreviousSong) && !in.action_pressed(1, PadAction::NextSong));
    in.update(1.0 / 60);
    CHECK(!in.action_pressed(0, PadAction::NextSong) && !in.action_held(0, PadAction::NextSong));
    IniDocument cfg;
    in.save(cfg);
    CHECK(cfg.get("player1.actions", "next_song") == c->to_string());
    InputManager again;
    again.load(cfg);
    CHECK(again.actions(0).of(PadAction::NextSong).size() == 1 && again.actions(1).of(PadAction::NextSong).empty());
}

// Chords on a controller (SDL's virtual gamepad): buttons together, a trigger, a stick direction;
// captured from the presses until all are let go, firing once when all are down, named as the
// controller names them.
TEST(input_chord_gamepad) {
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) SKIP("no SDL gamepad subsystem");
    int connected = 0;
    SDL_free(SDL_GetGamepads(&connected));
    if (connected > 0) { // a real controller would be player 1, not the test's
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        SKIP("a controller is connected");
    }
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.name = "Test Pad";
    desc.nbuttons = 3; // in mask order: south, back, right shoulder
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_SOUTH) | (1u << SDL_GAMEPAD_BUTTON_BACK) | (1u << SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    SDL_Joystick* pad = id ? SDL_OpenJoystick(id) : nullptr;
    CHECK(pad != nullptr);
    if (pad) {
        InputManager in;
        in.set_background(true); // no window has the focus here
        auto frame = [&] {
            SDL_UpdateJoysticks();
            for (SDL_Event e; SDL_PollEvent(&e);) in.handle_event(e);
            in.update(1.0 / 60);
        };
        frame();
        CHECK(in.device_name(0) == "Test Pad");
        auto button = [&](int b, bool down) { SDL_SetJoystickVirtualButton(pad, b, down); };
        auto axis = [&](SDL_GamepadAxis a, Sint16 v) { SDL_SetJoystickVirtualAxis(pad, int(a), v); };
        // Back, then RB, held together, let go: one chord of both.
        in.begin_chord_capture(0);
        button(1, true);
        frame();
        button(2, true);
        frame();
        CHECK(in.chord_so_far().size() == 2 && !in.poll_chord());
        button(1, false);
        frame(); // RB still held
        CHECK(in.capturing());
        button(2, false);
        frame();
        std::optional<Chord> c = in.poll_chord();
        CHECK(c && c->to_string() == "pad.back+pad.rightshoulder" && c->set() == BindingSet::Controller);
        // A trigger and a stick pushed left, past half way.
        in.begin_chord_capture(0);
        axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 30000);
        axis(SDL_GAMEPAD_AXIS_LEFTX, -30000);
        frame();
        axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0);
        axis(SDL_GAMEPAD_AXIS_LEFTX, 0);
        frame();
        std::optional<Chord> t = in.poll_chord();
        CHECK(t && t->inputs.size() == 2);
        if (t) CHECK(t->to_string() == "pad.leftx-+pad.lefttrigger" || t->to_string() == "pad.lefttrigger+pad.leftx-");
        // Firing: once when both are down, not on either alone, again after a release.
        if (c) in.bind_action(0, PadAction::NextSong, *c, true);
        if (t) in.bind_action(0, PadAction::PreviousSong, *t, true);
        button(2, true);
        frame();
        CHECK(!in.action_pressed(0, PadAction::NextSong));
        button(1, true);
        frame();
        CHECK(in.action_pressed(0, PadAction::NextSong) && !in.action_pressed(0, PadAction::PreviousSong));
        frame();
        CHECK(!in.action_pressed(0, PadAction::NextSong) && in.action_held(0, PadAction::NextSong));
        button(1, false), button(2, false);
        frame();
        axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767);
        axis(SDL_GAMEPAD_AXIS_LEFTX, -12000); // under half way
        frame();
        CHECK(!in.action_pressed(0, PadAction::PreviousSong));
        axis(SDL_GAMEPAD_AXIS_LEFTX, -32768);
        frame();
        CHECK(in.action_pressed(0, PadAction::PreviousSong));
        axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0), axis(SDL_GAMEPAD_AXIS_LEFTX, 0);
        frame();
        // Keyboard only: the controller's chords read nothing.
        in.set_device_filter(0, InputManager::kKeyboardOnly);
        button(1, true), button(2, true);
        frame();
        CHECK(!in.action_pressed(0, PadAction::NextSong));
        button(1, false), button(2, false);
        in.set_background(false);
        SDL_CloseJoystick(pad);
    }
    if (id) SDL_DetachVirtualJoystick(id);
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

// The driving layout's song buttons: hidden without custom music unless shown in the editor,
// shown with it unless hidden; a tap is one press, a finger sliding onto one doesn't skip.
TEST(touch_song_buttons) {
    TouchPlacement p = TouchPlacement::parse("shown");
    CHECK(p.shown && !p.hidden && !p.is_default() && p.to_string() == "shown");
    TouchConfig cfg;
    CHECK(cfg.hidden("drive.music") && cfg.default_hidden("drive.music") && !cfg.hidden("drive.pause"));
    cfg.set_placement("drive.music", p);
    CHECK(!cfg.hidden("drive.music"));
    TouchPlacement h;
    h.hidden = true;
    cfg.set_placement("drive.music", h);
    CHECK(cfg.hidden("drive.music"));
    IniDocument ini;
    cfg.set_placement("drive.music", p);
    cfg.save(ini);
    CHECK(ini.get("touch", "drive.music") == "shown" && TouchConfig::load(ini).placement("drive.music").shown);

    TouchControls t;
    TouchConfig on;
    on.mode = TouchConfig::Mode::On;
    t.configure(on);
    t.set_layout(TouchLayout::Driving);
    auto song_button = [&](TouchAction a) {
        for (const TouchControls::Widget& w : t.widgets())
            if (w.kind == TouchControls::Widget::Kind::Action && w.action == a) return w;
        return TouchControls::Widget{};
    };
    CHECK(song_button(TouchAction::NextSong).hidden); // hidden by default, songs or not
    on.set_placement("drive.music", p);               // shown in the layout editor
    t.configure(on);
    const TouchControls::Widget next = song_button(TouchAction::NextSong), prev = song_button(TouchAction::PreviousSong);
    CHECK(!next.hidden && prev.x < next.x && next.w > 0);
    CHECK(t.finger_down(1, next.x, next.y, 1));
    t.update(1.0 / 60, 0);
    CHECK(t.action_pressed(TouchAction::NextSong) && !t.action_pressed(TouchAction::PreviousSong));
    t.update(1.0 / 60, 0);
    CHECK(!t.action_pressed(TouchAction::NextSong)); // held: once
    t.finger_up(1);
    t.update(1.0 / 60, 0);
    // A finger from the pause button sliding onto it.
    const TouchControls::Widget* pause = nullptr;
    for (const TouchControls::Widget& w : t.widgets())
        if (w.kind == TouchControls::Widget::Kind::Button && w.button == PadButton::Start) pause = &w;
    CHECK(pause != nullptr);
    if (!pause) return;
    CHECK(t.finger_down(2, pause->x, pause->y, 1));
    t.update(1.0 / 60, 0);
    t.finger_move(2, prev.x, prev.y, 1);
    t.update(1.0 / 60, 0);
    CHECK(!t.action_pressed(TouchAction::PreviousSong));
    // configure() keeps the default from the music folder.
    t.configure(on);
    CHECK(!song_button(TouchAction::NextSong).hidden);
}

// The music list's tabs: Up from the first song, Left / Right there, L1 / R1 anywhere, a tap;
// none with a song lifted. Both's two sets: a lifted song stays in its own.
TEST(music_list_tabs) {
    using namespace gt3r::menu::flow;
    using Sounds = std::vector<std::string>;
    RacePlaylist p = RacePlaylist::with_songs({1, 2, 3, 4}, 9);
    MusicListPage m;
    m.open(p, {{1, "A", "a"}, {2, "B", "b"}, {3, "C", "c"}, {4, "D", "d"}}, true);
    auto key = [&](MenuKey k) {
        Sounds s;
        m.tick(key_bit(k), key_bit(k), s);
        return s;
    };
    CHECK(key(MenuKey::Up) == Sounds{"disable"}); // no tabs: the top
    m.set_tabs({"Original", "Custom", "Both"}, 2, false);
    m.set_split(2);
    m.take_preview();
    CHECK(m.tabs_shown() && m.tab() == 2 && m.focus() == 0);
    CHECK(key(MenuKey::Up) == Sounds{"cursor"} && m.focus() == -1 && m.take_preview() == -1);
    // Along the row: the tab shown (Both), then Order and Shuffle; Confirm takes one.
    CHECK(m.top_column() == 2);
    CHECK(key(MenuKey::Right) == Sounds{"cursor"} && m.top_column() == 3 && !m.take_tab());
    CHECK(key(MenuKey::Confirm) == Sounds{"ok"} && !m.shuffle()); // Order
    CHECK(key(MenuKey::Confirm).empty() && !m.shuffle());         // (already)
    CHECK(key(MenuKey::Right) == Sounds{"cursor"} && key(MenuKey::Right) == Sounds{"disable"} && m.top_column() == 4);
    CHECK(key(MenuKey::Confirm) == Sounds{"ok"} && m.shuffle());
    for (int i = 0; i < 3; ++i) key(MenuKey::Left); // to Custom: moving along takes nothing
    CHECK(m.top_column() == 1 && m.tab() == 2 && !m.take_tab());
    CHECK(key(MenuKey::Confirm) == Sounds{"cursor"} && m.tab() == 1 && m.take_tab() == 1 && m.focus() == -1);
    CHECK(key(MenuKey::Down) == Sounds{"cursor"} && m.focus() == 0 && m.take_preview() == 0);
    CHECK(key(MenuKey::R1) == Sounds{"cursor"} && m.take_tab() == 2);
    CHECK(key(MenuKey::L1) == Sounds{"cursor"} && key(MenuKey::L1) == Sounds{"cursor"} && m.tab() == 0);
    CHECK(key(MenuKey::L1) == Sounds{"disable"} && m.take_tab() == 0);
    // Lift the second song: it can't go into the second set, and the tabs don't change.
    key(MenuKey::Down);
    key(MenuKey::Right);
    CHECK(key(MenuKey::Confirm) == Sounds{"ok"} && m.lifted() && m.focus() == 1);
    CHECK(key(MenuKey::Down) == Sounds{"disable"} && m.focus() == 1);
    CHECK(key(MenuKey::R1).empty() && !m.take_tab());
    CHECK(key(MenuKey::Up) == Sounds{"cursor"} && m.focus() == 0 && m.songs()[0].id == 2);
    // A tap on a tab (the row above the list: the widget's top 32 units), on Order / Shuffle.
    key(MenuKey::Confirm);
    const SDL_FRect area{28, 44, 580, 308};
    Sounds s;
    CHECK(m.tap(area, 28 + 175, 44 + 308 - 10, s) && m.focus() == -1 && m.take_tab() == 1);
    CHECK(m.tap(area, 28 + 400, 44 + 308 - 10, s) && !m.shuffle() && m.top_column() == 3 && !m.take_tab());
    CHECK(m.tap(area, 28 + 500, 44 + 308 - 10, s) && m.shuffle() && m.top_column() == 4);
    // The first song's row is under the tabs now.
    s.clear();
    CHECK(m.tap(area, 28 + 300, 44 + 308 - 32 - 12, s) && m.focus() == 0);
}
