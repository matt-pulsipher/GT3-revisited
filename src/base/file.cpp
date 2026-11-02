#include "base/file.h"
#include "platform/document_tree.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

#include <algorithm>

namespace gt3r {

namespace {

// A path in a folder picked on Android: opened through its document (SDL opens the document's
// content:// URI, not the path).
#if defined(SDL_PLATFORM_ANDROID)
bool in_tree(const std::string& path) { return platform::is_document_tree_path(path); }
#else
bool in_tree(const std::string&) { return false; }
#endif

} // namespace

std::unique_ptr<File> File::open(const std::string& path) {
    SDL_IOStream* io = nullptr;
#if defined(SDL_PLATFORM_ANDROID)
    if (in_tree(path)) {
        const std::string uri = platform::document_uri(path);
        if (uri.empty()) return nullptr;
        io = SDL_IOFromFile(uri.c_str(), "rb");
    }
#endif
    if (!io && !in_tree(path)) io = SDL_IOFromFile(path.c_str(), "rb");
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
    std::string open = path;
#if defined(SDL_PLATFORM_ANDROID)
    if (in_tree(path) && (open = platform::document_uri(path)).empty()) return std::nullopt;
#endif
    size_t size = 0;
    void* data = SDL_LoadFile(open.c_str(), &size);
    if (!data) return std::nullopt;
    Bytes b(static_cast<u8*>(data), static_cast<u8*>(data) + size);
    SDL_free(data);
    return b;
}

bool write_host_file(const std::string& path, ByteSpan data) {
    std::string open = path;
#if defined(SDL_PLATFORM_ANDROID)
    // A new file is made first (its folder must be there); SDL then writes it from the start.
    if (in_tree(path) && (open = platform::document_create(path, false)).empty()) return false;
#endif
    return SDL_SaveFile(open.c_str(), data.data(), data.size());
}

bool host_path_exists(const std::string& path) {
#if defined(SDL_PLATFORM_ANDROID)
    if (in_tree(path)) return platform::document_info(path).has_value();
#endif
    return SDL_GetPathInfo(path.c_str(), nullptr);
}

bool host_is_directory(const std::string& path) {
#if defined(SDL_PLATFORM_ANDROID)
    if (in_tree(path)) {
        std::optional<platform::DocumentInfo> i = platform::document_info(path);
        return i && i->dir;
    }
#endif
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

std::optional<s64> host_file_mtime(const std::string& path) {
#if defined(SDL_PLATFORM_ANDROID)
    if (in_tree(path)) {
        std::optional<platform::DocumentInfo> i = platform::document_info(path);
        if (!i || i->dir) return std::nullopt;
        return i->mtime;
    }
#endif
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) return std::nullopt;
    return s64(info.modify_time / 1000000000); // SDL_Time: nanoseconds
}

std::vector<HostDirEntry> host_dir_entries(const std::string& dir) {
#if defined(SDL_PLATFORM_ANDROID)
    if (in_tree(dir)) return platform::document_entries(dir);
#endif
    std::vector<HostDirEntry> out;
    std::string base = dir;
    if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';
    std::vector<std::string> names;
    SDL_EnumerateDirectory(
        dir.c_str(),
        [](void* user, const char*, const char* name) {
            static_cast<std::vector<std::string>*>(user)->push_back(name);
            return SDL_ENUM_CONTINUE;
        },
        &names);
    for (std::string& n : names) {
        SDL_PathInfo info;
        if (!SDL_GetPathInfo((base + n).c_str(), &info)) continue; // gone since, or a dangling link
        HostDirEntry e;
        e.name = std::move(n);
        e.dir = info.type == SDL_PATHTYPE_DIRECTORY;
        e.mtime = s64(info.modify_time / 1000000000);
        e.size = info.size;
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const HostDirEntry& a, const HostDirEntry& b) { return a.name < b.name; });
    return out;
}

bool host_make_dir(const std::string& dir) {
#if defined(SDL_PLATFORM_ANDROID)
    if (in_tree(dir)) return !platform::document_create(dir, true).empty();
#endif
    return SDL_CreateDirectory(dir.c_str());
}

bool host_remove(const std::string& path) {
#if defined(SDL_PLATFORM_ANDROID)
    if (in_tree(path)) return platform::document_remove(path);
#endif
    return SDL_RemovePath(path.c_str());
}

bool host_rename(const std::string& from, const std::string& to) {
#if defined(SDL_PLATFORM_ANDROID)
    if (in_tree(from) || in_tree(to)) {
        if (platform::document_tree_of(from) == platform::document_tree_of(to)) return platform::document_rename(from, to);
        // Between a picked folder and anywhere else: a copy, then the original goes.
        return host_copy_file(from, to) && host_remove(from);
    }
#endif
    // SDL_RenamePath takes UTF-8 on every platform (std::filesystem would use the ANSI code
    // page on Windows) and replaces an existing file.
    return SDL_RenamePath(from.c_str(), to.c_str());
}

bool host_copy_file(const std::string& from, const std::string& to) {
    if (!in_tree(from) && !in_tree(to)) return SDL_CopyFile(from.c_str(), to.c_str());
    std::optional<Bytes> b = read_host_file(from);
    if (!b) return false;
    return write_host_file(to, *b);
}

} // namespace gt3r
