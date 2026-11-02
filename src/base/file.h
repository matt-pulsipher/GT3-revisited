#pragma once

#include "base/reader.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>

struct SDL_IOStream;

namespace gt3r {

// Random-access read-only file. Backed by SDL_IOStream so that Android
// content:// URIs (from the system file picker) work the same as desktop
// paths. Reads are serialized so one handle can be shared between the main
// thread and the audio streaming thread.
class File : public Reader {
public:
    static std::unique_ptr<File> open(const std::string& path);
    ~File() override;

    u64 size() const override { return size_; }
    const std::string& path() const override { return path_; }

    // Reads exactly `len` bytes at `offset` into `out`; throws on short read.
    void read_at(u64 offset, void* out, size_t len) override;
    Bytes read_at(u64 offset, size_t len);

private:
    File(SDL_IOStream* io, std::string path, u64 size);
    SDL_IOStream* io_;
    std::string path_;
    u64 size_;
    std::mutex mutex_;
};

// Whole-file helpers for small host files (configs, mod files).
std::optional<Bytes> read_host_file(const std::string& path);
bool write_host_file(const std::string& path, ByteSpan data);

// Last modification time of a host file (seconds since 1970), nullopt if missing.
std::optional<s64> host_file_mtime(const std::string& path);
bool host_path_exists(const std::string& path);
bool host_is_directory(const std::string& path);

} // namespace gt3r
