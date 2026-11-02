#include "game/save_folder.h"

#include "base/file.h"
#include "game/gt_save.h"
#include "platform/document_tree.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <vector>

namespace gt3r::game {

namespace {

std::mutex g_mutex;
std::string g_folder; // the chosen folder, with a trailing separator ("": none)

std::string with_sep(std::string d) {
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    return d;
}

bool ends_with(const std::string& name, const char* ext) {
    std::string e = ext;
    if (name.size() < e.size()) return false;
    return std::equal(e.rbegin(), e.rend(), name.rbegin(),
                      [](char a, char b) { return std::tolower(u8(a)) == std::tolower(u8(b)); });
}

// Copies the files of `from` into `to` (a new folder), and its folders' (region folders) one
// level down; `made` gets what was made, folders before their files.
void copy_files(const std::string& from, const std::string& to, const char* ext, int depth, int& count, bool& ok,
                std::vector<std::string>& made) {
    if (!ok) return;
    if (!host_make_dir(to)) {
        ok = false;
        return;
    }
    made.push_back(to);
    for (const HostDirEntry& e : host_dir_entries(from)) {
        if (!ok) return;
        if (e.dir) {
            if (depth > 0) copy_files(from + e.name + "/", to + e.name + "/", ext, depth - 1, count, ok, made);
            continue;
        }
        if (!host_copy_file(from + e.name, to + e.name)) {
            ok = false;
            host_remove(to + e.name); // a part written
            return;
        }
        made.push_back(to + e.name);
        if (ends_with(e.name, ext)) ++count;
    }
}

} // namespace

std::string save_folder() {
    {
        std::lock_guard lock(g_mutex);
        if (!g_folder.empty()) return g_folder;
    }
    return user_data_dir();
}

bool save_folder_chosen() {
    std::lock_guard lock(g_mutex);
    return !g_folder.empty();
}

void set_save_folder(std::string dir) {
    std::lock_guard lock(g_mutex);
    g_folder = with_sep(std::move(dir));
}

SaveFolderCopy copy_save_folder(const std::string& from, const std::string& to) {
    SaveFolderCopy c;
    const std::string src = with_sep(from), dst = with_sep(to);
    if (src.empty() || dst.empty()) return c;
    struct Kind {
        const char* folder;
        const char* ext;
        int SaveFolderCopy::*count;
    };
    std::vector<std::string> made;
    for (const Kind& k : {Kind{"saves", ".sav", &SaveFolderCopy::saves}, Kind{"replays", ".gt3replay", &SaveFolderCopy::replays}}) {
        const std::string s = src + k.folder + "/";
        if (!host_is_directory(s) || host_path_exists(dst + k.folder)) continue;
        copy_files(s, dst + k.folder + "/", k.ext, 1, c.*k.count, c.ok, made);
    }
    if (c.ok && !host_make_dir(dst + "saves/")) c.ok = false;
    if (!c.ok) {
        for (auto it = made.rbegin(); it != made.rend(); ++it) host_remove(*it);
        c.saves = c.replays = 0;
    }
    return c;
}

bool same_save_folder(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    if (platform::is_document_tree_path(a) || platform::is_document_tree_path(b)) return with_sep(a) == with_sep(b);
    namespace fs = std::filesystem;
    auto path = [](const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); };
    std::error_code ec;
    if (fs::equivalent(path(a), path(b), ec) && !ec) return true;
    std::string x = with_sep(a), y = with_sep(b);
    std::replace(x.begin(), x.end(), '\\', '/');
    std::replace(y.begin(), y.end(), '\\', '/');
    return x == y; // (neither there yet)
}

} // namespace gt3r::game
