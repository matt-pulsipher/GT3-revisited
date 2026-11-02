#include "disc/chd_image.h"

#include <libchdr/chd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace gt3r {

namespace {
constexpr u32 kCacheHunks = 32;
} // namespace

ChdImage::ChdImage(std::unique_ptr<File> file) : file_(std::move(file)), source_{*file_} {}

ChdImage::~ChdImage() {
    if (chd_) chd_close(chd_);
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
    image->totalhunks_ = header.totalhunks;
    image->unitbytes_ = header.unitbytes;
    if (header.unitbytes != 2048 && header.unitbytes != 2448)
        throw FormatError("unsupported CHD unit size: " + std::to_string(header.unitbytes));
    if (!header.logicalbytes || !header.hunkbytes || header.hunkbytes % header.unitbytes != 0 ||
        header.logicalbytes / header.hunkbytes + (header.logicalbytes % header.hunkbytes != 0) > header.totalhunks ||
        (header.unitbytes == 2448 && header.logicalbytes % 2448 != 0))
        throw FormatError("Invalid CHD hunk / frame layout: " + image->path());
    image->size_ = header.unitbytes == 2048 ? header.logicalbytes : header.logicalbytes / 2448 * 2048;
    const u64 cache_bytes = u64(std::min(kCacheHunks, header.totalhunks)) * header.hunkbytes;
    if (cache_bytes > std::numeric_limits<size_t>::max()) throw FormatError("CHD hunks are too large");
    image->cache_.resize(size_t(cache_bytes));
    image->validate_data_image();
    return image;
}

void ChdImage::validate_data_image() {
    if (unitbytes_ == 2448) {
        // Reject audio and non-data frames before ISO probing.
        u8 first;
        read_at(0, &first, 1);
    }
}

const u8* ChdImage::hunk(u32 index) {
    if (index < cache_first_ || index - cache_first_ >= cache_count_) {
        cache_count_ = 0; // A failed decode invalidates the window.
        cache_first_ = index / kCacheHunks * kCacheHunks;
        const u32 count = std::min(kCacheHunks, totalhunks_ - cache_first_);
        for (u32 i = 0; i < count; ++i) {
            chd_error err = chd_read(chd_, cache_first_ + i, cache_.data() + size_t(i) * hunkbytes_);
            if (err != CHDERR_NONE) throw FormatError("Could not read CHD " + path() + ": " + chd_error_string(err));
        }
        cache_count_ = count;
    }
    return cache_.data() + size_t(index - cache_first_) * hunkbytes_;
}

void ChdImage::read_at(u64 offset, void* out, size_t len) {
    if (offset > size_ || size_ - offset < len) throw FormatError("read past end of CHD " + path());
    std::lock_guard lock(mutex_);
    auto* dest = static_cast<u8*>(out);
    while (len) {
        const u8* data;
        size_t n;
        if (unitbytes_ == 2048) {
            const size_t within = size_t(offset % hunkbytes_);
            data = hunk(u32(offset / hunkbytes_)) + within;
            n = std::min(len, size_t(hunkbytes_) - within);
        } else {
            const u64 raw = offset / 2048 * 2448;
            const u8* frame = hunk(u32(raw / hunkbytes_)) + size_t(raw % hunkbytes_);
            static constexpr u8 sync[] = {0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0};
            if (std::memcmp(frame, sync, sizeof sync) != 0)
                throw FormatError("CHD non-data sector (audio / multi-track images are not supported)");
            size_t user;
            if (frame[15] == 1) user = 16;
            else if (frame[15] == 2 && !(frame[18] & 0x20) && !(frame[22] & 0x20)) user = 24;
            else throw FormatError("CHD Form 2 / non-data sector is not supported");
            const size_t within = size_t(offset % 2048);
            data = frame + user + within;
            n = std::min(len, size_t(2048) - within);
        }
        std::memcpy(dest, data, n);
        dest += n;
        offset += n;
        len -= n;
    }
}

} // namespace gt3r
