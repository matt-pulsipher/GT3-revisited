#include "base/file.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

namespace gt3r {

std::unique_ptr<File> File::open(const std::string& path) {
    SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io) return nullptr;
    Sint64 sz = SDL_GetIOSize(io);
    if (sz < 0) {
        SDL_CloseIO(io);
        return nullptr;
    }
    return std::unique_ptr<File>(new File(io, path, static_cast<u64>(sz)));
}

File::File(SDL_IOStream* io, std::string path, u64 size)
    : io_(io), path_(std::move(path)), size_(size) {}

File::~File() {
    if (io_) SDL_CloseIO(io_);
}

void File::read_at(u64 offset, void* out, size_t len) {
    if (offset > size_ || size_ - offset < len) throw FormatError("read past end of " + path_);
    std::lock_guard lock(mutex_);
    if (SDL_SeekIO(io_, static_cast<Sint64>(offset), SDL_IO_SEEK_SET) < 0)
        throw FormatError("seek failed in " + path_);
    size_t got = 0;
    auto* p = static_cast<u8*>(out);
    while (got < len) {
        size_t n = SDL_ReadIO(io_, p + got, len - got);
        if (n == 0) throw FormatError("short read in " + path_);
        got += n;
    }
}

std::optional<Bytes> read_host_file(const std::string& path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return std::nullopt;
    Bytes b(static_cast<u8*>(data), static_cast<u8*>(data) + size);
    SDL_free(data);
    return b;
}

bool write_host_file(const std::string& path, ByteSpan data) {
    return SDL_SaveFile(path.c_str(), data.data(), data.size());
}

bool host_path_exists(const std::string& path) {
    return SDL_GetPathInfo(path.c_str(), nullptr);
}

bool host_is_directory(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

std::optional<s64> host_file_mtime(const std::string& path) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) return std::nullopt;
    return s64(info.modify_time / 1000000000); // SDL_Time: nanoseconds
}

} // namespace gt3r
