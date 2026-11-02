// The save folder (game/save_folder.h): moving to another one copies the saves and replays in
// where it has none, and the libraries follow it; the host file functions it uses
// (base/file.h); the paths of Android's picked folders (platform/document_tree.h), as strings.
// Works in the run's temporary folder only.

#include "test_framework.h"

#include "base/file.h"
#include "game/gt_save.h"
#include "game/save_folder.h"
#include "game/save_library.h"
#include "platform/document_tree.h"
#include "race/replay_library.h"

#include <filesystem>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace gt3r;
using namespace gt3r::game;
namespace fs = std::filesystem;

namespace {

bool write_text(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    return write_host_file(path.string(), ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()));
}

std::string read_text(const fs::path& path) {
    std::optional<Bytes> b = read_host_file(path.string());
    return b ? std::string(b->begin(), b->end()) : std::string("<missing>");
}

// Every file under `root`, relative, sorted.
std::vector<std::string> files_under(const fs::path& root) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(root, ec))
        if (e.is_regular_file()) out.push_back(fs::relative(e.path(), root).generic_string());
    std::sort(out.begin(), out.end());
    return out;
}

// A save folder as a player's: saves of two regions, the current one, replays of two regions.
void fill_folder(const fs::path& root) {
    write_text(root / "saves" / "Mine.sav", "GT3R-GTMODE mine");
    write_text(root / "saves" / "current.txt", "Mine.sav\n");
    write_text(root / "saves" / "eu" / "Europe.sav", "GT3R-GTMODE eu");
    write_text(root / "replays" / "a_b_20261101-120000.gt3replay", "replay us");
    write_text(root / "replays" / "jp" / "c_d_20261101-120001.gt3replay", "replay jp");
}

} // namespace

// The default is the user data folder; a chosen folder takes the saves' libraries and the
// replays with it (each region's in its folder) and back.
TEST(save_folder_libraries_follow) {
    CHECK(!save_folder_chosen() && save_folder() == user_data_dir());
    const fs::path dir = test::temp_dir("save_folder_follow");
    set_save_folder(dir.string()); // no trailing separator: one is added
    CHECK(save_folder_chosen());
    const std::string base = dir.string() + "/";
    CHECK(save_folder() == base);
    CHECK(SaveLibrary::user_dir(Region::America) == base + "saves/");
    CHECK(SaveLibrary::user_dir(Region::Europe) == base + "saves/" + region_key(Region::Europe) + "/");
    CHECK(SaveLibrary::legacy_path() == user_data_dir() + "gtmode.sav"); // earlier versions' place
    CHECK(race::replay_dir(save_folder(), Region::Japan) == base + "replays/" + region_key(Region::Japan));
    // A library there works as anywhere.
    SaveLibrary lib(SaveLibrary::user_dir());
    GameState s;
    s.credits = 5000;
    std::string path;
    CHECK(lib.create(s, "THERE", &path) && path.rfind(base, 0) == 0);
    CHECK(lib.list().size() == 1 && lib.list()[0].name == "THERE");
    set_save_folder("");
    CHECK(!save_folder_chosen() && save_folder() == user_data_dir());
    fs::remove_all(dir);
}

// A folder with no saves or replays gets a copy of the player's (region folders, the current
// save's note), counted; the originals stay. Once it has its own, nothing is copied again.
TEST(save_folder_copy_into_new_folder) {
    const fs::path from = test::temp_dir("save_folder_from"), to = test::temp_dir("save_folder_to");
    fill_folder(from);
    const std::vector<std::string> before = files_under(from);
    SaveFolderCopy c = copy_save_folder(from.string(), to.string());
    CHECK(c.ok && c.saves == 2 && c.replays == 2);
    CHECK(files_under(to) == before);
    CHECK(read_text(to / "saves" / "eu" / "Europe.sav") == "GT3R-GTMODE eu");
    CHECK(SaveLibrary((to / "saves").string()).current() == (to / "saves").string() + "/Mine.sav");
    CHECK(files_under(from) == before); // the originals stay
    // Again (the folder has its own now): nothing copied, nothing written over.
    write_text(to / "saves" / "Mine.sav", "GT3R-GTMODE changed there");
    c = copy_save_folder(from.string(), to.string());
    CHECK(c.ok && c.saves == 0 && c.replays == 0);
    CHECK(read_text(to / "saves" / "Mine.sav") == "GT3R-GTMODE changed there");
    fs::remove_all(from), fs::remove_all(to);
}

// A folder that has saves already (another device's, kept the same by a sync app) is used as
// it is; its missing replays/ still gets the player's.
TEST(save_folder_keeps_existing_saves) {
    const fs::path from = test::temp_dir("save_folder_mine"), to = test::temp_dir("save_folder_synced");
    fill_folder(from);
    write_text(to / "saves" / "Tablet.sav", "GT3R-GTMODE tablet");
    write_text(to / "saves" / "current.txt", "Tablet.sav\n");
    SaveFolderCopy c = copy_save_folder(from.string(), to.string());
    CHECK(c.ok && c.saves == 0 && c.replays == 2);
    CHECK(!fs::exists(to / "saves" / "Mine.sav") && !fs::exists(to / "saves" / "eu"));
    CHECK(read_text(to / "saves" / "current.txt") == "Tablet.sav\n");
    CHECK(fs::exists(to / "replays" / "jp" / "c_d_20261101-120001.gt3replay"));
    // An empty saves/ counts as the folder's own too.
    const fs::path empty = test::temp_dir("save_folder_empty_saves");
    fs::create_directories(empty / "saves");
    c = copy_save_folder(from.string(), empty.string());
    CHECK(c.ok && c.saves == 0 && files_under(empty / "saves").empty());
    fs::remove_all(from), fs::remove_all(to), fs::remove_all(empty);
}

// Nothing to copy (a first start): the new folder still gets its saves/ folder, which is how a
// folder the game cannot write is found out before it is used.
TEST(save_folder_copy_nothing_or_unwritable) {
    const fs::path from = test::temp_dir("save_folder_fresh"), to = test::temp_dir("save_folder_new");
    SaveFolderCopy c = copy_save_folder(from.string(), to.string());
    CHECK(c.ok && c.saves == 0 && c.replays == 0 && fs::is_directory(to / "saves"));
    // A "folder" that is a file: nothing can be made there.
    write_text(from / "saves" / "Mine.sav", "GT3R-GTMODE mine");
    const fs::path file = test::temp_path("save_folder_is_a_file");
    write_text(file, "x");
    c = copy_save_folder(from.string(), file.string());
    CHECK(!c.ok && c.saves == 0);
    CHECK(read_text(file) == "x");
    fs::remove_all(from), fs::remove_all(to), fs::remove(file);
}

#ifndef _WIN32
// A copy that fails part way (a file that cannot be read) leaves the new folder as it was.
TEST(save_folder_copy_failure_removes_copies) {
    if (geteuid() == 0) SKIP("root reads every file");
    const fs::path from = test::temp_dir("save_folder_locked"), to = test::temp_dir("save_folder_target");
    fill_folder(from);
    write_text(from / "replays" / "zz_locked.gt3replay", "locked");
    fs::permissions(from / "replays" / "zz_locked.gt3replay", fs::perms::none);
    write_text(to / "notes.txt", "mine");
    SaveFolderCopy c = copy_save_folder(from.string(), to.string());
    CHECK(!c.ok && c.saves == 0 && c.replays == 0);
    CHECK(files_under(to) == std::vector<std::string>{"notes.txt"});
    CHECK(!fs::exists(to / "saves") && !fs::exists(to / "replays"));
    fs::permissions(from / "replays" / "zz_locked.gt3replay", fs::perms::owner_all);
    fs::remove_all(from), fs::remove_all(to);
}
#endif

TEST(save_folder_same_folder) {
    const fs::path dir = test::temp_dir("save_folder_same");
    CHECK(same_save_folder(dir.string(), dir.string() + "/"));
    CHECK(same_save_folder(dir.string() + "/./", dir.string()));
    CHECK(!same_save_folder(dir.string(), (dir / "saves").string()));
    CHECK(!same_save_folder("", dir.string()));
    const std::string tree = "content://com.android.externalstorage.documents/tree/primary%3ASync%2FGT3";
    CHECK(same_save_folder(tree, tree + "/") && !same_save_folder(tree, tree + "/saves"));
    fs::remove_all(dir);
}

// The host functions the save folder uses, on a plain folder.
TEST(save_folder_host_files) {
    const fs::path dir = test::temp_dir("host_files");
    const std::string d = dir.string() + "/";
    CHECK(host_make_dir(d + "a/b/c") && fs::is_directory(dir / "a" / "b" / "c"));
    CHECK(host_make_dir(d + "a")); // there already
    write_text(dir / "z.sav", "zz");
    write_text(dir / "m.txt", "m");
    std::vector<HostDirEntry> e = host_dir_entries(d);
    CHECK(e.size() == 3 && e[0].name == "a" && e[0].dir && e[1].name == "m.txt" && e[2].name == "z.sav");
    CHECK(!e[2].dir && e[2].size == 2 && e[2].mtime > 0);
    CHECK(host_dir_entries(d + "missing").empty());
    // Rename replaces; copy writes a new file; remove takes files and empty folders.
    CHECK(host_rename(d + "m.txt", d + "z.sav") && read_text(dir / "z.sav") == "m" && !fs::exists(dir / "m.txt"));
    CHECK(host_copy_file(d + "z.sav", d + "a/y.sav") && read_text(dir / "a" / "y.sav") == "m");
    CHECK(host_remove(d + "a/y.sav") && !fs::exists(dir / "a" / "y.sav"));
    CHECK(host_remove(d + "a/b/c") && !fs::exists(dir / "a" / "b" / "c"));
    CHECK(!host_rename(d + "missing", d + "other"));
    fs::remove_all(dir);
}

// Android's picked folders: a tree URI and the names in it are a path; a document's own URI
// (what file pickers hand over) is not.
TEST(save_folder_document_tree_paths) {
    using platform::document_tree_of;
    using platform::is_document_tree_path;
    const std::string tree = "content://com.android.externalstorage.documents/tree/primary%3ASync%2FGT3";
    CHECK(is_document_tree_path(tree) && is_document_tree_path(tree + "/"));
    CHECK(is_document_tree_path(tree + "/saves/eu/GAME.sav"));
    CHECK(document_tree_of(tree + "/saves/eu/GAME.sav") == tree && document_tree_of(tree + "/") == tree);
    CHECK(!is_document_tree_path(tree + "/document/primary%3ASync%2FGT3%2Fsaves"));
    CHECK(!is_document_tree_path("content://com.android.providers.downloads.documents/document/42"));
    CHECK(!is_document_tree_path("content://com.android.externalstorage.documents/tree/"));
    CHECK(!is_document_tree_path("content://x/treehouse/a"));
    CHECK(!is_document_tree_path("/storage/emulated/0/GT3/saves"));
    CHECK(!is_document_tree_path("C:\\Users\\me\\tree\\saves"));
    CHECK(document_tree_of("/home/me/saves").empty());
}
