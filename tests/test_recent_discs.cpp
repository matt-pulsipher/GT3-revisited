// The disc library ([game] recent1..recentN, [disc_library]) and saved-path resolution.

#include "test_framework.h"

#include "platform/recent_discs.h"

#include <algorithm>

using namespace gt3r;
using namespace gt3r::platform;

TEST(recent_discs_push_moves_to_front_and_caps) {
    std::vector<std::string> l;
    const int n = kMaxLibraryDiscs + 2;
    for (int i = 0; i < n; ++i) l = push_recent_disc(l, "/isos/disc" + std::to_string(i) + ".iso");
    CHECK(int(l.size()) == kMaxLibraryDiscs);
    CHECK(l.front() == "/isos/disc" + std::to_string(n - 1) + ".iso");
    CHECK(l.back() == "/isos/disc2.iso");
    l = push_recent_disc(l, "/isos/disc4.iso");
    CHECK(l.front() == "/isos/disc4.iso");
    CHECK(int(l.size()) == kMaxLibraryDiscs);
    CHECK(std::count(l.begin(), l.end(), std::string("/isos/disc4.iso")) == 1);
    l = remove_recent_disc(l, "/isos/disc5.iso");
    CHECK(int(l.size()) == kMaxLibraryDiscs - 1);
}

TEST(recent_discs_windows_paths_dedupe) {
    auto l = push_recent_disc({}, "C:\\Games\\GT3 (PAL), copy.iso");
    l = push_recent_disc(l, "c:/games/gt3 (pal), copy.iso");
    CHECK(l.size() == 1);
    CHECK(l.front() == "c:/games/gt3 (pal), copy.iso");
    // POSIX paths are case-sensitive.
    l = push_recent_disc(l, "/Isos/GT3.iso");
    l = push_recent_disc(l, "/isos/gt3.iso");
    CHECK(l.size() == 3);
}

TEST(recent_discs_ini_roundtrip) {
    IniDocument cfg;
    cfg.set("game", "disc", "/a.iso");
    cfg.set("game", "recent9", "/stale.iso");
    std::vector<std::string> l = {"/home/me/GT3 = A-Spec, v1.iso", "D:\\iso\\gt3.iso", "content://x/document/primary%3AGT3.iso"};
    save_recent_discs(cfg, l);
    IniDocument back = IniDocument::parse(cfg.serialize());
    CHECK(load_recent_discs(back) == l);
    CHECK(!back.get("game", "recent9"));
    CHECK(back.get("game", "disc") == std::optional<std::string>("/a.iso"));
    save_recent_discs(back, {"/only.iso"});
    CHECK(!back.get("game", "recent2"));
    CHECK(load_recent_discs(back).size() == 1);
}

TEST(recent_discs_records) {
    IniDocument cfg;
    DiscRecord pal{"SCES_502.94", "1.00", "eu", "075246ad42a4b418", "eu", "Gran Turismo 3 A-Spec (PAL)", true, true};
    DiscRecord mod{"SCEF_502.94", "1.00", "eu", "1e8d5712926622d2", "SCEF-50294", "Unverified | odd disc", false, false};
    save_recent_discs(cfg, {"D:\\ISO\\GT3 = PAL.iso", "/isos/gt3c.iso"});
    save_disc_record(cfg, "D:\\ISO\\GT3 = PAL.iso", pal);
    save_disc_record(cfg, "/isos/gt3c.iso", mod);
    IniDocument back = IniDocument::parse(cfg.serialize());
    // The same file however its path is written.
    std::optional<DiscRecord> a = load_disc_record(back, "d:/iso/gt3 = pal.iso"), b = load_disc_record(back, "/isos/gt3c.iso");
    CHECK(a && b && !load_disc_record(back, "/isos/GT3C.iso"));
    if (!a || !b) return;
    CHECK(a->serial == pal.serial && a->version == "1.00" && a->region == "eu" && a->digest == pal.digest && a->key == "eu");
    CHECK(a->known && a->modified && a->description == pal.description);
    CHECK(!b->known && !b->modified && b->description == "Unverified | odd disc" && b->key == "SCEF-50294");
    CHECK(disc_record_label(*a) == "PAL v1.00, modified");
    CHECK(disc_record_label(*b) == "SCEF-50294 v1.00");
    // Leaving the library drops the record.
    save_recent_discs(back, {"/isos/gt3c.iso"});
    CHECK(!load_disc_record(back, "D:\\ISO\\GT3 = PAL.iso") && load_disc_record(back, "/isos/gt3c.iso"));
}

TEST(recent_discs_path_candidates) {
    CHECK(is_absolute_disc_path("/x.iso"));
    CHECK(is_absolute_disc_path("C:\\x.iso"));
    CHECK(is_absolute_disc_path("C:/x.iso"));
    CHECK(is_absolute_disc_path("\\\\server\\share\\x.iso"));
    CHECK(is_absolute_disc_path("content://a/b"));
    CHECK(!is_absolute_disc_path("C:x.iso"));
    CHECK(!is_absolute_disc_path("isos/x.iso"));
    CHECK(disc_path_candidates("/x.iso", "/app/").size() == 1);
    auto c = disc_path_candidates("./isos/x.iso", "C:\\gt3r\\");
    CHECK(c.size() == 2);
    CHECK(c[0] == "./isos/x.iso");
    CHECK(c[1] == "C:\\gt3r\\isos/x.iso");
    CHECK(disc_path_candidates("x.iso", "/opt/gt3r")[1] == "/opt/gt3r/x.iso");
    CHECK(disc_file_name("C:\\Games\\GT3.iso") == "GT3.iso");
    CHECK(disc_file_name("/a/b/GT3 (US).iso") == "GT3 (US).iso");
    CHECK(disc_file_name("content://p/document/primary%3AGames%2FGT3.iso") == "GT3.iso");
}

TEST(recent_discs_rebase_app_container) {
    const std::string now = "/var/mobile/Containers/Data/Application/BBBB-2222/Documents/";
    // An earlier install's folder (as the system reports it, with /private): this install's.
    CHECK(rebase_app_container_path("/private/var/mobile/Containers/Data/Application/AAAA-1111/Documents/GT3.iso", now) ==
          "/var/mobile/Containers/Data/Application/BBBB-2222/Documents/GT3.iso");
    CHECK(rebase_app_container_path("/var/mobile/Containers/Data/Application/BBBB-2222/Documents/a/GT3.iso", now) ==
          "/var/mobile/Containers/Data/Application/BBBB-2222/Documents/a/GT3.iso");
    // Outside any app folder (iCloud Drive, another provider), or not on iOS: unchanged.
    const std::string icloud = "/private/var/mobile/Library/Mobile Documents/com~apple~CloudDocs/GT3.iso";
    CHECK(rebase_app_container_path(icloud, now) == icloud);
    CHECK(rebase_app_container_path("/home/me/GT3.iso", "/home/me/.local/share/GT3Revisited/") == "/home/me/GT3.iso");
    CHECK(rebase_app_container_path("/x/Containers/Data/Application/", now) == "/x/Containers/Data/Application/");
}

TEST(recent_discs_same_app_container) {
    const std::string docs = "/var/mobile/Containers/Data/Application/BBBB-2222/Documents/";
    // This app's folder, as the system reports it (with /private) or as data_dir has it.
    CHECK(same_app_container("/private/var/mobile/Containers/Data/Application/BBBB-2222/Documents/GT3.iso", docs));
    CHECK(same_app_container("/var/mobile/Containers/Data/Application/BBBB-2222/Documents/a/GT3.iso", docs));
    // Another app's folder (a browser's downloads), a longer UUID, or no app folder at all.
    CHECK(!same_app_container("/private/var/mobile/Containers/Data/Application/CCCC-3333/Documents/Downloads/GT3.iso",
                              docs));
    CHECK(!same_app_container("/var/mobile/Containers/Data/Application/BBBB-22223/Documents/GT3.iso", docs));
    CHECK(!same_app_container("/private/var/mobile/Library/Mobile Documents/com~apple~CloudDocs/GT3.iso", docs));
    CHECK(!same_app_container("/var/mobile/Containers/Data/Application/BBBB-2222/Documents/GT3.iso", ""));
    CHECK(!same_app_container("/x/Containers/Data/Application/", "/x/Containers/Data/Application/"));
}
