#include "audio/music_file.h"

#include "base/config.h"
#include "base/file.h"
#include "base/log.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <optional>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "../../third_party/dr_libs/dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
// libchdr embeds its own dr_flac; keep this decoder's symbols local.
#define DRFLAC_API static
#define DRFLAC_PRIVATE static
#include "../../third_party/dr_libs/dr_flac.h"
#undef DRFLAC_API
#undef DRFLAC_PRIVATE
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "../../third_party/dr_libs/dr_wav.h"
// stb_vorbis's declarations; its implementation is compiled in vorbis_decoder.cpp.
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "../../third_party/stb/stb_vorbis.c"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace gt3r {

namespace {

std::string extension(std::string_view name) {
    const size_t slash = name.find_last_of("/\\");
    const size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return {};
    return to_lower(name.substr(dot + 1));
}

// --- decoding ------------------------------------------------------------------------------------

// A host file, or bytes in memory, read through its own cursor (dr_libs' read / seek / tell
// callbacks).
struct Source {
    std::unique_ptr<File> file;
    std::shared_ptr<const Bytes> mem;
    MusicReadFn reader; // a mod's file through the VFS (Vfs::Stream)
    u64 reader_size = 0;
    u64 pos = 0;

    u64 size() const { return file ? file->size() : mem ? mem->size() : reader ? reader_size : 0; }
    size_t read(void* out, size_t len) {
        const u64 left = size() - std::min(pos, size());
        len = size_t(std::min<u64>(len, left));
        if (len == 0) return 0;
        if (mem) {
            std::memcpy(out, mem->data() + pos, len);
        } else if (reader) {
            try {
                reader(pos, out, len);
            } catch (const std::exception&) {
                return 0;
            }
        } else {
            try {
                file->read_at(pos, out, len);
            } catch (const std::exception&) {
                return 0;
            }
        }
        pos += len;
        return len;
    }
    // origin 0 set, 1 current, 2 end (every dr_lib's enum order).
    bool seek(s64 offset, int origin) {
        const s64 base = origin == 0 ? 0 : origin == 1 ? s64(pos) : s64(size());
        const s64 to = base + offset;
        if (to < 0 || u64(to) > size()) return false;
        pos = u64(to);
        return true;
    }
};

size_t on_read(void* user, void* out, size_t len) { return static_cast<Source*>(user)->read(out, len); }

// A Vorbis comment's value as a frame count ("LOOPSTART=44100"), if it is `key` (any case).
std::optional<u64> loop_tag(std::string_view comment, std::string_view key) {
    if (comment.size() <= key.size() || comment[key.size()] != '=' || to_lower(comment.substr(0, key.size())) != key)
        return std::nullopt;
    const std::string_view v = comment.substr(key.size() + 1);
    if (v.empty() || v.size() > 18) return std::nullopt;
    u64 n = 0;
    for (char c : v) {
        if (c < '0' || c > '9') return std::nullopt;
        n = n * 10 + u64(c - '0');
    }
    return n;
}

// The loop tags among a file's Vorbis comments (FLAC, Ogg Vorbis): LOOPSTART with LOOPLENGTH or
// LOOPEND (exclusive), in frames.
struct LoopTags {
    std::optional<u64> start, length, end;
    void add(std::string_view comment) {
        if (auto v = loop_tag(comment, "loopstart")) start = v;
        else if (auto v = loop_tag(comment, "looplength")) length = v;
        else if (auto v = loop_tag(comment, "loopend")) end = v;
    }
};

// The decoders' common part: the frame count and the loop. A loop region [loop_start,
// loop_end) from the file (a WAV's smpl chunk, a FLAC's LOOPSTART tags) plays the intro once,
// then the region over and over; without one, the whole file loops.
class DecodedStream : public MusicStream {
public:
    double position_seconds() const override { return sample_rate() ? double(frame_) / sample_rate() : 0.0; }
    size_t read(s16* out, size_t frames) override {
        if (loop_ && loop_end_ && frame_ >= loop_end_ && seek(loop_start_)) frame_ = loop_start_;
        size_t got = decode(out, want(frames));
        if (got == 0 && loop_ && frame_ > loop_start_ && seek(loop_start_)) {
            frame_ = loop_start_;
            got = decode(out, want(frames));
        }
        frame_ += got;
        return got;
    }
    u64 loop_start() const { return loop_start_; }
    u64 loop_end() const { return loop_end_; }

protected:
    virtual size_t decode(s16* out, size_t frames) = 0;
    virtual bool seek(u64 frame) = 0;
    void set_loop(const LoopTags& t) {
        if (t.start && (t.length || t.end)) set_loop(*t.start, t.length ? *t.start + *t.length : *t.end);
    }
    // A loop region in frames (`end` exclusive); one that is empty or runs past the end is ignored.
    void set_loop(u64 start, u64 end) {
        const u64 total = total_frames();
        if (end <= start || (total && end > total)) return;
        loop_start_ = start;
        loop_end_ = end;
    }
    bool loop_ = false;

private:
    size_t want(size_t frames) const {
        return loop_ && loop_end_ && frame_ < loop_end_ ? size_t(std::min<u64>(frames, loop_end_ - frame_)) : frames;
    }
    u64 frame_ = 0, loop_start_ = 0, loop_end_ = 0;
};

class Mp3Stream : public DecodedStream {
public:
    ~Mp3Stream() override {
        if (open_) drmp3_uninit(&mp3_);
    }
    bool open(Source src, bool loop) {
        src_ = std::move(src);
        loop_ = loop;
        open_ = drmp3_init(
            &mp3_, on_read,
            [](void* u, int off, drmp3_seek_origin o) -> drmp3_bool32 { return static_cast<Source*>(u)->seek(off, int(o)); },
            [](void* u, drmp3_int64* at) -> drmp3_bool32 { return *at = drmp3_int64(static_cast<Source*>(u)->pos), 1; },
            nullptr, &src_, nullptr);
        return open_ && mp3_.channels > 0 && mp3_.sampleRate > 0;
    }
    u32 sample_rate() const override { return mp3_.sampleRate; }
    u32 channels() const override { return mp3_.channels; }
    u64 total_frames() const override { return mp3_.totalPCMFrameCount == DRMP3_UINT64_MAX ? 0 : mp3_.totalPCMFrameCount; }

protected:
    size_t decode(s16* out, size_t frames) override { return size_t(drmp3_read_pcm_frames_s16(&mp3_, frames, out)); }
    bool seek(u64 frame) override { return drmp3_seek_to_pcm_frame(&mp3_, frame); }

private:
    Source src_;
    drmp3 mp3_{};
    bool open_ = false;
};

class FlacStream : public DecodedStream {
public:
    ~FlacStream() override {
        if (flac_) drflac_close(flac_);
    }
    bool open(Source src, bool loop) {
        src_ = std::move(src);
        loop_ = loop;
        flac_ = drflac_open_with_metadata(
            [](void* u, void* out, size_t len) { return static_cast<FlacStream*>(u)->src_.read(out, len); },
            [](void* u, int off, drflac_seek_origin o) -> drflac_bool32 {
                return static_cast<FlacStream*>(u)->src_.seek(off, int(o));
            },
            [](void* u, drflac_int64* at) -> drflac_bool32 { return *at = drflac_int64(static_cast<FlacStream*>(u)->src_.pos), 1; },
            [](void* u, drflac_metadata* m) { static_cast<FlacStream*>(u)->on_meta(*m); }, this, nullptr);
        if (!flac_ || flac_->channels == 0 || flac_->sampleRate == 0) return false;
        set_loop(tags_);
        return true;
    }
    u32 sample_rate() const override { return flac_->sampleRate; }
    u32 channels() const override { return flac_->channels; }
    u64 total_frames() const override { return flac_->totalPCMFrameCount; }

protected:
    size_t decode(s16* out, size_t frames) override { return size_t(drflac_read_pcm_frames_s16(flac_, frames, out)); }
    bool seek(u64 frame) override { return drflac_seek_to_pcm_frame(flac_, frame); }

private:
    // The callbacks get `this` (the metadata callback needs it); reads go to src_.
    void on_meta(const drflac_metadata& m) {
        if (m.type != DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) return;
        drflac_vorbis_comment_iterator it;
        drflac_init_vorbis_comment_iterator(&it, m.data.vorbis_comment.commentCount, m.data.vorbis_comment.pComments);
        drflac_uint32 len = 0;
        while (const char* c = drflac_next_vorbis_comment(&it, &len)) tags_.add(std::string_view(c, len));
    }
    Source src_;
    drflac* flac_ = nullptr;
    LoopTags tags_;
};

class WavStream : public DecodedStream {
public:
    ~WavStream() override {
        if (open_) drwav_uninit(&wav_);
    }
    bool open(Source src, bool loop) {
        src_ = std::move(src);
        loop_ = loop;
        open_ = drwav_init_with_metadata(
            &wav_, on_read,
            [](void* u, int off, drwav_seek_origin o) -> drwav_bool32 { return static_cast<Source*>(u)->seek(off, int(o)); },
            [](void* u, drwav_int64* at) -> drwav_bool32 { return *at = drwav_int64(static_cast<Source*>(u)->pos), 1; },
            &src_, 0, nullptr);
        if (!open_ || wav_.channels == 0 || wav_.sampleRate == 0) return false;
        // The smpl chunk's first loop: its sample offsets are frames, the last one inclusive.
        for (drwav_uint32 i = 0; i < wav_.metadataCount; ++i) {
            const drwav_metadata& m = wav_.pMetadata[i];
            if (m.type != drwav_metadata_type_smpl || m.data.smpl.sampleLoopCount == 0 || !m.data.smpl.pLoops) continue;
            const drwav_smpl_loop& l = m.data.smpl.pLoops[0];
            set_loop(l.firstSampleOffset, u64(l.lastSampleOffset) + 1);
            break;
        }
        return true;
    }
    u32 sample_rate() const override { return wav_.sampleRate; }
    u32 channels() const override { return wav_.channels; }
    u64 total_frames() const override { return wav_.totalPCMFrameCount; }

protected:
    size_t decode(s16* out, size_t frames) override { return size_t(drwav_read_pcm_frames_s16(&wav_, frames, out)); }
    bool seek(u64 frame) override { return drwav_seek_to_pcm_frame(&wav_, frame); }

private:
    Source src_;
    drwav wav_{};
    bool open_ = false;
};

// Ogg Vorbis through stb_vorbis, which decodes from memory: the file is read whole (a few MB
// for a song; at most kMaxBytes).
class OggStream : public DecodedStream {
public:
    static constexpr u64 kMaxBytes = 256ull << 20;
    ~OggStream() override {
        if (v_) stb_vorbis_close(v_);
    }
    bool open(Source src, bool loop) {
        loop_ = loop;
        const u64 size = src.size();
        if (size == 0 || size > kMaxBytes) return false;
        if (src.mem) {
            data_ = src.mem;
        } else {
            auto bytes = std::make_shared<Bytes>(size_t(size));
            src.pos = 0;
            if (src.read(bytes->data(), bytes->size()) != bytes->size()) return false;
            data_ = std::move(bytes);
        }
        int error = 0;
        v_ = stb_vorbis_open_memory(data_->data(), int(data_->size()), &error, nullptr);
        if (!v_) return false;
        info_ = stb_vorbis_get_info(v_);
        if (info_.channels <= 0 || info_.sample_rate == 0) return false;
        const stb_vorbis_int64 total = stb_vorbis_stream_length_in_samples64(v_);
        total_ = total > 0 ? u64(total) : 0;
        LoopTags tags;
        const stb_vorbis_comment c = stb_vorbis_get_comment(v_);
        for (int i = 0; i < c.comment_list_length; ++i)
            if (c.comment_list[i]) tags.add(c.comment_list[i]);
        set_loop(tags);
        return true;
    }
    u32 sample_rate() const override { return info_.sample_rate; }
    u32 channels() const override { return u32(info_.channels); }
    u64 total_frames() const override { return total_; }

protected:
    size_t decode(s16* out, size_t frames) override {
        const size_t ch = size_t(info_.channels);
        frames = std::min<size_t>(frames, size_t(INT32_MAX) / ch);
        const int got = stb_vorbis_get_samples_short_interleaved(v_, info_.channels, out, int(frames * ch));
        return got > 0 ? size_t(got) : 0;
    }
    bool seek(u64 frame) override { return stb_vorbis_seek64(v_, stb_vorbis_int64(frame)) != 0; }

private:
    std::shared_ptr<const Bytes> data_; // stb_vorbis reads it in place
    stb_vorbis* v_ = nullptr;
    stb_vorbis_info info_{};
    u64 total_ = 0;
};

// --- tags ------------------------------------------------------------------------------------------

u32 be32(ByteSpan d, size_t o) { return u32(d[o]) << 24 | u32(d[o + 1]) << 16 | u32(d[o + 2]) << 8 | d[o + 3]; }
u32 syncsafe(ByteSpan d, size_t o) {
    return u32(d[o] & 0x7F) << 21 | u32(d[o + 1] & 0x7F) << 14 | u32(d[o + 2] & 0x7F) << 7 | (d[o + 3] & 0x7F);
}

void put_utf8(std::string& out, u32 cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6)), out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12)), out += char(0x80 | ((cp >> 6) & 0x3F)), out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18)), out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F)), out += char(0x80 | (cp & 0x3F));
    }
}

std::string latin1(ByteSpan d) {
    std::string out;
    for (u8 c : d) {
        if (c == 0) break;
        put_utf8(out, c);
    }
    return out;
}

bool valid_utf8(ByteSpan d) {
    for (size_t i = 0; i < d.size(); ++i) {
        const u8 c = d[i];
        const size_t n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 4;
        if (n == 4 || i + n >= d.size() + (n == 0 ? 1 : 0)) return false;
        for (size_t k = 0; k < n; ++k)
            if ((d[++i] & 0xC0) != 0x80) return false;
    }
    return true;
}

// UTF-8 text up to its first NUL (or Latin-1 when it isn't valid UTF-8, as older taggers wrote).
std::string utf8_or_latin1(ByteSpan d) {
    size_t n = 0;
    while (n < d.size() && d[n]) ++n;
    d = d.first(n);
    return valid_utf8(d) ? std::string(reinterpret_cast<const char*>(d.data()), d.size()) : latin1(d);
}

std::string utf16(ByteSpan d, bool big_endian) {
    if (d.size() >= 2 && ((d[0] == 0xFF && d[1] == 0xFE) || (d[0] == 0xFE && d[1] == 0xFF))) {
        big_endian = d[0] == 0xFE;
        d = d.subspan(2);
    }
    std::string out;
    for (size_t i = 0; i + 1 < d.size(); i += 2) {
        u32 u = big_endian ? u32(d[i]) << 8 | d[i + 1] : u32(d[i + 1]) << 8 | d[i];
        if (u == 0) break;
        if (u >= 0xD800 && u < 0xDC00 && i + 3 < d.size()) { // a surrogate pair
            const u32 lo = big_endian ? u32(d[i + 2]) << 8 | d[i + 3] : u32(d[i + 3]) << 8 | d[i + 2];
            if (lo >= 0xDC00 && lo < 0xE000) u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00), i += 2;
        }
        put_utf8(out, u);
    }
    return out;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

// An ID3v2 text frame's body: an encoding byte, then the text (the first of several values).
std::string id3_text(ByteSpan d) {
    if (d.empty()) return {};
    const u8 enc = d[0];
    d = d.subspan(1);
    switch (enc) {
    case 0: return trim(latin1(d));
    case 1: return trim(utf16(d, false));
    case 2: return trim(utf16(d, true));
    default: return trim(utf8_or_latin1(d));
    }
}

// An ID3v2 tag at the start of `d`; its length (header included), or 0 when there is none.
size_t id3v2_size(ByteSpan d) {
    if (d.size() < 10 || std::memcmp(d.data(), "ID3", 3) != 0) return 0;
    return 10 + syncsafe(d, 6) + ((d[5] & 0x10) ? 10 : 0);
}

void parse_id3v2(ByteSpan d, MusicTags& t) {
    if (d.size() < 10 || std::memcmp(d.data(), "ID3", 3) != 0) return;
    const u8 ver = d[3], flags = d[5];
    if (ver < 2 || ver > 4) return;
    Bytes body(d.begin() + 10, d.begin() + std::ptrdiff_t(std::min<size_t>(d.size(), 10 + syncsafe(d, 6))));
    if (flags & 0x80) { // unsynchronised: every 0xFF 0x00 was 0xFF
        Bytes plain;
        for (size_t i = 0; i < body.size(); ++i) {
            plain.push_back(body[i]);
            if (body[i] == 0xFF && i + 1 < body.size() && body[i + 1] == 0) ++i;
        }
        body.swap(plain);
    }
    ByteSpan b(body);
    size_t at = 0;
    if ((flags & 0x40) && ver >= 3 && b.size() >= 4) // an extended header
        at = ver == 3 ? 4 + be32(b, 0) : syncsafe(b, 0);
    const size_t head = ver == 2 ? 6 : 10;
    while (at + head <= b.size() && b[at] != 0) {
        std::string id(reinterpret_cast<const char*>(b.data() + at), ver == 2 ? 3 : 4);
        const size_t size = ver == 2 ? (size_t(b[at + 3]) << 16 | size_t(b[at + 4]) << 8 | b[at + 5])
                            : ver == 4 ? syncsafe(b, at + 4)
                                       : be32(b, at + 4);
        at += head;
        if (size > b.size() - at) break;
        const ByteSpan f = b.subspan(at, size);
        if ((id == "TIT2" || id == "TT2") && t.title.empty()) t.title = id3_text(f);
        else if ((id == "TPE1" || id == "TP1") && t.artist.empty()) t.artist = id3_text(f);
        at += size;
    }
}

void parse_id3v1(ByteSpan d, MusicTags& t) {
    if (d.size() < 128) return;
    d = d.last(128);
    if (std::memcmp(d.data(), "TAG", 3) != 0) return;
    if (t.title.empty()) t.title = trim(latin1(d.subspan(3, 30)));
    if (t.artist.empty()) t.artist = trim(latin1(d.subspan(33, 30)));
}

bool key_is(std::string_view comment, std::string_view key) {
    return comment.size() > key.size() && comment[key.size()] == '=' && to_lower(comment.substr(0, key.size())) == key;
}

// A Vorbis comment block (FLAC block type 4): vendor string, then "KEY=value" comments.
void parse_vorbis_comments(ByteSpan d, MusicTags& t) {
    if (d.size() < 8) return;
    size_t at = 4 + size_t(rd32(d, 0));
    if (at + 4 > d.size()) return;
    const u32 n = rd32(d, at);
    at += 4;
    for (u32 i = 0; i < n && at + 4 <= d.size(); ++i) {
        const size_t len = rd32(d, at);
        at += 4;
        if (len > d.size() - at) break;
        const std::string_view c(reinterpret_cast<const char*>(d.data() + at), len);
        if (key_is(c, "title") && t.title.empty()) t.title = trim(std::string(c.substr(6)));
        else if (key_is(c, "artist") && t.artist.empty()) t.artist = trim(std::string(c.substr(7)));
        at += len;
    }
}

void parse_flac(ByteSpan d, MusicTags& t) {
    d = d.subspan(std::min(id3v2_size(d), d.size()));
    if (!has_magic(d, "fLaC")) return;
    for (size_t at = 4; at + 4 <= d.size();) {
        const u8 type = d[at] & 0x7F;
        const bool last = d[at] & 0x80;
        const size_t len = size_t(d[at + 1]) << 16 | size_t(d[at + 2]) << 8 | d[at + 3];
        at += 4;
        if (type == 4) {
            parse_vorbis_comments(d.subspan(at, std::min(len, d.size() - at)), t);
            return;
        }
        if (last) return;
        at += len;
    }
}

// An Ogg file's second packet (its first logical stream's), as far as `d` holds it: Vorbis puts
// its comment header there. Packets are cut into segments across pages; a page starts "OggS".
Bytes ogg_second_packet(ByteSpan d) {
    Bytes packet;
    int index = 0; // the packet being read
    u32 serial = 0;
    for (size_t at = 0; at + 27 <= d.size() && has_magic(d.subspan(at), "OggS");) {
        const u32 page_serial = rd32(d, at + 14);
        const size_t segments = d[at + 26];
        if (at + 27 + segments > d.size()) break;
        if (at == 0) serial = page_serial;
        size_t body = at + 27 + segments;
        for (size_t i = 0; i < segments; ++i) {
            const size_t len = d[at + 27 + i];
            const size_t have = body < d.size() ? std::min(len, d.size() - body) : 0;
            if (page_serial == serial) {
                if (index == 1) packet.insert(packet.end(), d.begin() + ptrdiff_t(body), d.begin() + ptrdiff_t(body + have));
                if (len < 255 && ++index > 1) return packet; // the packet ends with a short segment
            }
            body += len;
        }
        at = body;
    }
    return packet;
}

void parse_ogg(ByteSpan d, MusicTags& t) {
    const Bytes packet = ogg_second_packet(d);
    if (packet.size() > 7 && packet[0] == 3 && std::memcmp(packet.data() + 1, "vorbis", 6) == 0)
        parse_vorbis_comments(ByteSpan(packet).subspan(7), t);
}

// A WAV LIST chunk's body ("INFO" and its subchunks).
void parse_info_list(ByteSpan d, MusicTags& t) {
    if (!has_magic(d, "INFO")) return;
    for (size_t at = 4; at + 8 <= d.size();) {
        const std::string_view id(reinterpret_cast<const char*>(d.data() + at), 4);
        const size_t len = rd32(d, at + 4);
        at += 8;
        if (len > d.size() - at) break;
        if (id == "INAM" && t.title.empty()) t.title = trim(utf8_or_latin1(d.subspan(at, len)));
        else if (id == "IART" && t.artist.empty()) t.artist = trim(utf8_or_latin1(d.subspan(at, len)));
        at += len + (len & 1);
    }
}

// Walks a RIFF WAVE file's chunks: `chunk(id, offset, length)` for each; `size` the file's length.
template <typename Fn>
void walk_wav(ByteSpan head, u64 size, Fn&& chunk, const std::function<bool(u64, u8*)>& header_at) {
    if (head.size() < 12 || !has_magic(head, "RIFF") || std::memcmp(head.data() + 8, "WAVE", 4) != 0) return;
    u64 at = 12;
    for (int guard = 0; guard < 256 && at + 8 <= size; ++guard) {
        u8 h[8];
        if (!header_at(at, h)) return;
        const u64 len = rd32(ByteSpan(h, 8), 4);
        chunk(std::string_view(reinterpret_cast<const char*>(h), 4), at + 8, len);
        at += 8 + len + (len & 1);
    }
}

} // namespace

MusicFileType music_file_type(std::string_view name) {
    const std::string e = extension(name);
    if (e == "mp3") return MusicFileType::Mp3;
    if (e == "flac") return MusicFileType::Flac;
    if (e == "wav" || e == "wave") return MusicFileType::Wav;
    if (e == "ogg" || e == "oga") return MusicFileType::Ogg;
    return MusicFileType::None;
}

bool unplayable_music_file(std::string_view name) {
    static const char* const kOther[] = {"m4a", "aac", "mp4", "m4b", "opus", "wma", "aif", "aiff", "alac", "ape", "wv"};
    const std::string e = extension(name);
    return std::find(std::begin(kOther), std::end(kOther), e) != std::end(kOther);
}

namespace {

// What the output stream can take: 1 to 8 channels (SDL's layouts), 8 to 384 kHz.
bool playable(const MusicStream& s) {
    return s.channels() >= 1 && s.channels() <= 8 && s.sample_rate() >= 8000 && s.sample_rate() <= 384000;
}

std::unique_ptr<MusicStream> open_source(Source src, MusicFileType type, bool loop) {
    std::unique_ptr<MusicStream> s = [&]() -> std::unique_ptr<MusicStream> {
        switch (type) {
        case MusicFileType::Mp3: {
            auto m = std::make_unique<Mp3Stream>();
            if (m->open(std::move(src), loop)) return m;
            break;
        }
        case MusicFileType::Flac: {
            auto f = std::make_unique<FlacStream>();
            if (f->open(std::move(src), loop)) return f;
            break;
        }
        case MusicFileType::Wav: {
            auto w = std::make_unique<WavStream>();
            if (w->open(std::move(src), loop)) return w;
            break;
        }
        case MusicFileType::Ogg: {
            auto o = std::make_unique<OggStream>();
            if (o->open(std::move(src), loop)) return o;
            break;
        }
        case MusicFileType::None: break;
        }
        return nullptr;
    }();
    return s && playable(*s) ? std::move(s) : nullptr;
}

} // namespace

std::unique_ptr<MusicStream> open_music_file(const std::string& path, bool loop) {
    Source src;
    try {
        src.file = File::open(path);
    } catch (const std::exception& e) {
        LOG_ERROR("music: %s", e.what());
    }
    if (!src.file) return nullptr;
    std::unique_ptr<MusicStream> s = open_source(std::move(src), music_file_type(path), loop);
    if (!s) LOG_WARN("music: cannot decode %s", path.c_str());
    return s;
}

bool music_file_decodes(const std::string& path) {
    Source src;
    try {
        src.file = File::open(path);
    } catch (const std::exception&) {
    }
    return src.file && open_source(std::move(src), music_file_type(path), false) != nullptr;
}

std::unique_ptr<MusicStream> open_music_memory(std::shared_ptr<const Bytes> data, MusicFileType type, bool loop) {
    if (!data) return nullptr;
    Source src;
    src.mem = std::move(data);
    return open_source(std::move(src), type, loop);
}

std::unique_ptr<MusicStream> open_music_reader(u64 size, MusicReadFn read, MusicFileType type, bool loop) {
    if (!read) return nullptr;
    Source src;
    src.reader = std::move(read);
    src.reader_size = size;
    return open_source(std::move(src), type, loop);
}

MusicTags parse_music_tags(MusicFileType type, ByteSpan head, ByteSpan tail) {
    MusicTags t;
    try {
        switch (type) {
        case MusicFileType::Mp3:
            parse_id3v2(head, t);
            parse_id3v1(tail, t);
            break;
        case MusicFileType::Flac: parse_flac(head, t); break;
        case MusicFileType::Ogg: parse_ogg(head, t); break;
        case MusicFileType::Wav: {
            ByteSpan id3;
            walk_wav(
                head, head.size(),
                [&](std::string_view id, u64 at, u64 len) {
                    if (at + len > head.size()) return;
                    if (id == "LIST") parse_info_list(head.subspan(size_t(at), size_t(len)), t);
                    else if (id == "id3 " || id == "ID3 ") id3 = head.subspan(size_t(at), size_t(len));
                },
                [&](u64 at, u8* h) { return at + 8 <= head.size() && (std::memcpy(h, head.data() + at, 8), true); });
            if (!id3.empty()) parse_id3v2(id3, t);
            break;
        }
        case MusicFileType::None: break;
        }
    } catch (const std::exception&) {
        // A damaged tag: whatever was read before it.
    }
    return t;
}

MusicTags read_music_tags(const std::string& path, MusicFileType type) {
    MusicTags t;
    std::unique_ptr<File> f;
    try {
        f = File::open(path);
    } catch (const std::exception&) {
    }
    if (!f) return t;
    constexpr size_t kHead = 64 * 1024, kMaxTag = 1024 * 1024;
    try {
        const u64 size = f->size();
        Bytes head = f->read_at(0, size_t(std::min<u64>(size, kHead)));
        if (type == MusicFileType::Mp3 || type == MusicFileType::Flac) {
            // A tag longer than the first read (cover art first): read it whole, up to 1 MB.
            const size_t want = id3v2_size(head);
            if (want > head.size() && size > head.size())
                head = f->read_at(0, size_t(std::min<u64>({size, want + (type == MusicFileType::Flac ? kHead : 0), kMaxTag})));
            Bytes tail;
            if (type == MusicFileType::Mp3 && size >= 128) tail = f->read_at(size - 128, 128);
            return parse_music_tags(type, head, tail);
        }
        if (type == MusicFileType::Ogg) {
            // A comment header longer than the first read (cover art in it): up to 1 MB.
            MusicTags got = parse_music_tags(type, head);
            if (got.title.empty() && got.artist.empty() && size > head.size())
                got = parse_music_tags(type, f->read_at(0, size_t(std::min<u64>(size, kMaxTag))));
            return got;
        }
        if (type == MusicFileType::Wav) {
            // The LIST and id3 chunks are often after the samples, at the end.
            Bytes id3;
            walk_wav(
                head, size,
                [&](std::string_view id, u64 at, u64 len) {
                    if (len > kMaxTag || at + len > size) return;
                    if (id == "LIST") parse_info_list(f->read_at(at, size_t(len)), t);
                    else if ((id == "id3 " || id == "ID3 ") && id3.empty()) id3 = f->read_at(at, size_t(len));
                },
                [&](u64 at, u8* h) {
                    if (at + 8 > size) return false;
                    f->read_at(at, h, 8);
                    return true;
                });
            if (!id3.empty()) parse_id3v2(id3, t);
        }
    } catch (const std::exception&) {
    }
    return t;
}

} // namespace gt3r
