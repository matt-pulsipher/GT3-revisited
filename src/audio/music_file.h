#pragma once

// The player's own music (Settings -> Audio -> Your music): MP3, FLAC and WAV files, decoded
// with dr_mp3 / dr_flac / dr_wav (third_party/dr_libs), and their title / artist tags.

#include "audio/music_stream.h"
#include "base/bytes.h"

#include <memory>
#include <string>
#include <string_view>

namespace gt3r {

enum class MusicFileType : u8 { None, Mp3, Flac, Wav };

// By the file name's extension, any case: .mp3, .flac, .wav / .wave.
MusicFileType music_file_type(std::string_view name);
// Music formats that don't play (AAC .m4a / .aac, Ogg, Opus, WMA, AIFF...): a music folder's
// scan counts them, so Settings can say why they are missing.
bool unplayable_music_file(std::string_view name);

// Opens `path` (a host path) for streaming; `loop` starts it over at its end. nullptr when it
// can't be read or decoded.
std::unique_ptr<MusicStream> open_music_file(const std::string& path, bool loop);
// The same for a file's bytes in memory (the tests' and the fuzzer's).
std::unique_ptr<MusicStream> open_music_memory(std::shared_ptr<const Bytes> data, MusicFileType type, bool loop);
// Whether a file opens for playing (open_music_file without its log): the music folder's scan
// leaves damaged files out.
bool music_file_decodes(const std::string& path);

struct MusicTags {
    std::string title, artist; // UTF-8; empty when the file has none
};
// The tags in a file's first bytes `head` (and an MP3's last 128 bytes `tail`, its ID3v1 tag):
//   MP3   ID3v2.2 / 2.3 / 2.4 text frames TT2 / TIT2 (title), TP1 / TPE1 (artist), any of their
//         text encodings; else ID3v1 in `tail`
//   FLAC  the VORBIS_COMMENT block's TITLE / ARTIST (an ID3v2 tag before "fLaC" is skipped)
//   WAV   the LIST "INFO" chunk's INAM / IART, else an "id3 " chunk's ID3v2 tag
MusicTags parse_music_tags(MusicFileType type, ByteSpan head, ByteSpan tail = {});
// The same from the file (reads its start, and for WAV walks its chunks).
MusicTags read_music_tags(const std::string& path, MusicFileType type);

} // namespace gt3r
