#pragma once

#include "base/reader.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

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
    using Reader::read_at;

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

// Folders and files of the save folder (game/save_folder.h). These and the functions above
// also take the paths of a folder picked on Android (platform/document_tree.h); failures set
// SDL_GetError().
struct HostDirEntry {
    std::string name;
    bool dir = false;
    s64 mtime = 0; // seconds since 1970
    u64 size = 0;
};
// A folder's files and folders (not what is inside those), sorted by name; empty when it
// cannot be read.
std::vector<HostDirEntry> host_dir_entries(const std::string& dir);
// Creates a folder and the missing ones above it; true when it is there.
bool host_make_dir(const std::string& dir);
// Removes a file or an empty folder.
bool host_remove(const std::string& path);
// Renames a file, replacing `to` (in the same folder).
bool host_rename(const std::string& from, const std::string& to);
bool host_copy_file(const std::string& from, const std::string& to);

} // namespace gt3r
