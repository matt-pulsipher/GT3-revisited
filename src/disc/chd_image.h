#pragma once

#include "base/file.h"
#include "base/reader.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

struct _chd_file;

namespace gt3r {

// A CHD image (chdman's compressed disc format) read as the 2048-byte sectors of the ISO it holds: a DVD image
// (createdvd) as it is, a CD image (createcd) as its first track's user data. Reads are serialized per hunk, so
// the main thread and the audio streaming thread can share one image without either waiting on a long read.
class ChdImage : public Reader {
public:
    // Whether `file` starts with a CHD header (the format is recognised by content, not by the file's name).
    static bool is_chd(Reader& file);
    static std::unique_ptr<ChdImage> open(std::unique_ptr<File> file);
    ~ChdImage() override;

    u64 size() const override { return size_; }
    const std::string& path() const override { return file_->path(); }
    void read_at(u64 offset, void* out, size_t len) override;
    using Reader::read_at;

private:
    explicit ChdImage(std::unique_ptr<File> file);
    void read_cd_layout();
    // Hunk `index`, decoded on a miss into the least recently used slot; call with lock_ held.
    const u8* hunk(u32 index);

    struct Source {
        File& file;
        u64 position = 0;
    };
    struct Slot {
        u32 hunk;
        u64 used;
    };
    // A first-come lock: a thread that asks for the image gets it right after the current hunk. (A mutex lets
    // the holder take it straight back, so a long load kept the music stream's reads waiting for hundreds of ms.)
    class TurnLock {
    public:
        void lock();
        void unlock();

    private:
        std::mutex mutex_;
        std::condition_variable turn_;
        u64 next_ = 0, serving_ = 0;
        u32 waiting_ = 0;
    };

    std::unique_ptr<File> file_;
    Source source_;
    _chd_file* chd_ = nullptr;
    u64 size_ = 0;
    u64 logicalbytes_ = 0;
    u32 hunkbytes_ = 0;
    u32 unitbytes_ = 0;
    // CD images: the frame holding sector 0, and the user data's offset in each frame.
    u32 first_frame_ = 0;
    u32 data_offset_ = 0;
    std::vector<Slot> slots_;
    Bytes cache_; // slots_.size() hunks
    u64 clock_ = 0;
    TurnLock lock_;
};

} // namespace gt3r
