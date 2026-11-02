#pragma once

#include "base/file.h"
#include "base/reader.h"

struct _chd_file;

namespace gt3r {

// CD frames contain 2048 user-data bytes in each 2448-byte unit.
class ChdImage : public Reader {
public:
    static std::unique_ptr<ChdImage> open(std::unique_ptr<File> file);
    ~ChdImage() override;

    u64 size() const override { return size_; }
    const std::string& path() const override { return file_->path(); }
    void read_at(u64 offset, void* out, size_t len) override;
    using Reader::read_at;

private:
    explicit ChdImage(std::unique_ptr<File> file);
    void validate_data_image();
    const u8* hunk(u32 index);
    struct Source {
        File& file;
        u64 position = 0;
    };

    std::unique_ptr<File> file_;
    Source source_;
    _chd_file* chd_ = nullptr;
    u64 size_ = 0;
    u32 hunkbytes_ = 0;
    u32 totalhunks_ = 0;
    u32 unitbytes_ = 0;
    Bytes cache_;
    u32 cache_first_ = 0;
    u32 cache_count_ = 0;
    std::mutex mutex_;
};

} // namespace gt3r
