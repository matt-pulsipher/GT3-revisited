#include "disc/chd_image.h"

#include <libchdr/chd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace gt3r {

namespace {

constexpr u32 kCacheHunks = 32;
constexpr u32 kNoHunk = std::numeric_limits<u32>::max();
constexpr u32 kCdFrameBytes = 2448; // a CD image's unit: 2352 bytes of sector, 96 of subcode

// Where a CD track type keeps 2048 bytes of user data in each frame. Cooked tracks are stored from the frame's
// start (Mode 2 ones with their 8-byte subheader first), raw ones whole (sync and header; Mode 2: and the
// subheader). Audio and Form 2 tracks have none.
struct DataTrack {
    const char* type;
    u32 data_offset;
};
constexpr DataTrack kDataTracks[] = {
    {"MODE1", 0},      {"MODE1/2048", 0}, {"MODE2_FORM1", 0}, {"MODE2/2048", 0},     {"MODE2", 8},
    {"MODE2/2336", 8}, {"MODE2_FORM_MIX", 8}, {"MODE1_RAW", 16}, {"MODE1/2352", 16}, {"MODE2_RAW", 24},
    {"MODE2/2352", 24}, {"CDI/2352", 24},
};

} // namespace

ChdImage::ChdImage(std::unique_ptr<File> file) : file_(std::move(file)), source_{*file_} {}

ChdImage::~ChdImage() {
    if (chd_) chd_close(chd_);
}

bool ChdImage::is_chd(Reader& file) {
    u8 tag[8];
    if (file.size() < sizeof tag) return false;
    file.read_at(0, tag, sizeof tag);
    return std::memcmp(tag, "MComprHD", sizeof tag) == 0;
}

std::unique_ptr<ChdImage> ChdImage::open(std::unique_ptr<File> file) {
    if (!file) throw FormatError("Could not open CHD: no source file");
    std::unique_ptr<ChdImage> image(new ChdImage(std::move(file)));
    // No exception may escape a C callback.
    static const core_file_callbacks callbacks = {
        [](void* p) -> uint64_t { return static_cast<Source*>(p)->file.size(); },
        [](void* out, size_t size, size_t count, void* p) -> size_t {
            auto& s = *static_cast<Source*>(p);
            if (!size || !count || count > std::numeric_limits<size_t>::max() / size) return 0;
            if (s.position >= s.file.size()) return 0;
            const size_t bytes = size_t(std::min<u64>(size * count, s.file.size() - s.position));
            size_t got = 0;
            try {
                while (got < bytes) {
                    const size_t n = std::min<size_t>(64 * 1024, bytes - got);
                    s.file.read_at(s.position, static_cast<u8*>(out) + got, n);
                    s.position += n;
                    got += n;
                }
            } catch (...) {
                return got / size;
            }
            return got / size;
        },
        [](void*) -> int { return 0; },
        [](void* p, int64_t offset, int whence) -> int {
            auto& s = *static_cast<Source*>(p);
            u64 base;
            switch (whence) {
            case SEEK_SET: base = 0; break;
            case SEEK_CUR: base = s.position; break;
            case SEEK_END: base = s.file.size(); break;
            default: return -1;
            }
            if (offset < 0) {
                const u64 back = u64(-(offset + 1)) + 1;
                if (back > base) return -1;
                s.position = base - back;
            } else {
                if (u64(offset) > std::numeric_limits<u64>::max() - base) return -1;
                s.position = base + u64(offset);
            }
            return 0;
        },
    };
    chd_error err = chd_open_core_file_callbacks(&callbacks, &image->source_, CHD_OPEN_READ, nullptr, &image->chd_);
    if (err != CHDERR_NONE) throw FormatError("Could not open CHD " + image->path() + ": " + chd_error_string(err));
    const chd_header& header = *chd_get_header(image->chd_);
    image->hunkbytes_ = header.hunkbytes;
    image->unitbytes_ = header.unitbytes;
    image->logicalbytes_ = header.logicalbytes;
    if (header.unitbytes != 2048 && header.unitbytes != kCdFrameBytes)
        throw FormatError("This CHD is not a CD or DVD image (unit size " + std::to_string(header.unitbytes) +
                          "): " + image->path());
    if (!header.logicalbytes || !header.hunkbytes || header.hunkbytes % header.unitbytes != 0 ||
        header.logicalbytes / header.hunkbytes + (header.logicalbytes % header.hunkbytes != 0) > header.totalhunks)
        throw FormatError("Invalid CHD hunk layout: " + image->path());
    if (header.unitbytes == 2048) image->size_ = header.logicalbytes;
    else image->read_cd_layout();
    const u32 slots = std::min(kCacheHunks, header.totalhunks);
    const u64 cache_bytes = u64(slots) * header.hunkbytes;
    if (cache_bytes > std::numeric_limits<size_t>::max()) throw FormatError("CHD hunks are too large");
    image->slots_.assign(slots, Slot{kNoHunk, 0});
    image->cache_.resize(size_t(cache_bytes));
    return image;
}

void ChdImage::read_cd_layout() {
    // The first track's entry, "TRACK:1 TYPE:MODE2_RAW SUBTYPE:NONE FRAMES:1234 PREGAP:0 PGTYPE:MODE1 ..." (older
    // images' entries stop after FRAMES). Only that track is read: it holds the file system.
    char meta[256] = {};
    u32 len = 0;
    bool v2 = true;
    if (chd_get_metadata(chd_, CDROM_TRACK_METADATA2_TAG, 0, meta, sizeof meta - 1, &len, nullptr, nullptr) !=
        CHDERR_NONE) {
        v2 = false;
        if (chd_get_metadata(chd_, CDROM_TRACK_METADATA_TAG, 0, meta, sizeof meta - 1, &len, nullptr, nullptr) !=
            CHDERR_NONE)
            throw FormatError("This CHD has no CD track list: " + path());
    }
    meta[std::min<size_t>(len, sizeof meta - 1)] = '\0';
    int track = 0, frames = 0, pregap = 0;
    char type[32] = {}, subtype[32] = {}, pgtype[32] = {};
    const int fields = v2 ? std::sscanf(meta, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d PREGAP:%d PGTYPE:%31s", &track,
                                        type, subtype, &frames, &pregap, pgtype)
                          : std::sscanf(meta, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d", &track, type, subtype, &frames);
    if (fields < 4 || track != 1 || frames <= 0) throw FormatError("Invalid CD track list in CHD " + path());
    const DataTrack* data = nullptr;
    for (const DataTrack& t : kDataTracks)
        if (std::strcmp(type, t.type) == 0) data = &t;
    if (!data) throw FormatError(std::string("This CHD's first track is not a data track (") + type + "): " + path());
    // A pregap stored in the image ("V" types) comes before sector 0, inside the track's frames.
    u32 first = 0, count = u32(frames);
    if (fields >= 6 && pgtype[0] == 'V' && pregap > 0) {
        if (pregap >= frames) throw FormatError("Invalid CD track list in CHD " + path());
        first = u32(pregap);
        count -= first;
    }
    // chdman pads every track to a multiple of 4 frames; the padding and any later tracks are not sectors.
    if ((u64(first) + count) * kCdFrameBytes > logicalbytes_) throw FormatError("Invalid CD track list in CHD " + path());
    first_frame_ = first;
    data_offset_ = data->data_offset;
    size_ = u64(count) * 2048;
}

void ChdImage::TurnLock::lock() {
    std::unique_lock lock(mutex_);
    const u64 ticket = next_++;
    if (ticket == serving_) return;
    ++waiting_;
    turn_.wait(lock, [&] { return ticket == serving_; });
    --waiting_;
}

void ChdImage::TurnLock::unlock() {
    std::lock_guard lock(mutex_);
    ++serving_;
    if (waiting_) turn_.notify_all();
}

const u8* ChdImage::hunk(u32 index) {
    Slot* lru = &slots_[0];
    for (Slot& s : slots_) {
        if (s.hunk == index) {
            s.used = ++clock_;
            return cache_.data() + size_t(&s - slots_.data()) * hunkbytes_;
        }
        if (s.used < lru->used) lru = &s;
    }
    u8* data = cache_.data() + size_t(lru - slots_.data()) * hunkbytes_;
    lru->hunk = kNoHunk; // a failed decode leaves the slot empty
    const chd_error err = chd_read(chd_, index, data);
    if (err != CHDERR_NONE) throw FormatError("Could not read CHD " + path() + ": " + chd_error_string(err));
    lru->hunk = index;
    lru->used = ++clock_;
    return data;
}

void ChdImage::read_at(u64 offset, void* out, size_t len) {
    if (offset > size_ || size_ - offset < len) throw FormatError("read past end of CHD " + path());
    auto* dest = static_cast<u8*>(out);
    while (len) {
        // The part of the read in one hunk (a CD image: in one sector, as hunks hold whole frames).
        u64 at;
        size_t n;
        if (unitbytes_ == 2048) {
            at = offset;
            n = size_t(std::min<u64>(len, hunkbytes_ - offset % hunkbytes_));
        } else {
            at = (first_frame_ + offset / 2048) * u64(kCdFrameBytes) + data_offset_ + offset % 2048;
            n = size_t(std::min<u64>(len, 2048 - offset % 2048));
        }
        {
            // A lock per hunk: another thread's read (the music stream) gets in between a long read's hunks.
            std::lock_guard lock(lock_);
            std::memcpy(dest, hunk(u32(at / hunkbytes_)) + size_t(at % hunkbytes_), n);
        }
        dest += n;
        offset += n;
        len -= n;
    }
}

} // namespace gt3r
