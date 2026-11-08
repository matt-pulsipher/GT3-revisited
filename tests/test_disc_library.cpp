// The disc library's per-disc parts: a disc's key (disc/disc.h disc_key), where its saves and
// replays go and what it starts with, mods made for some discs (mod.ini discs) and the player's
// mod choices per disc ([mods.<disc key>]). The releases' own fingerprints need a disc
// (GT3R_DISC, any region).

#include "test_framework.h"

#include "base/config.h"
#include "base/file.h"
#include "disc/disc.h"
#include "game/save_folder.h"
#include "game/save_library.h"
#include "race/replay_library.h"
#include "vfs/vfs.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>

using namespace gt3r;
namespace fs = std::filesystem;

namespace {

bool write_text(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    return write_host_file(path.string(), ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()));
}

DiscInfo disc(const char* serial, Region region, const char* digest = "1e8d5712926622d2") {
    DiscInfo d;
    d.serial = serial;
    d.region = region;
    d.digest = digest;
    return d;
}

} // namespace

TEST(disc_library_keys) {
    CHECK(short_serial("PBPX_955.03") == "PBPX-95503");
    CHECK(short_serial("SCEF-50294") == "SCEF-50294");
    // The releases keep their region's folders (US 1.00 and 1.10 share theirs), patched copies too.
    CHECK(disc_key(disc("PBPX_955.03", Region::America)) == "us");
    CHECK(disc_key(disc("SCUS_971.02", Region::America)) == "us");
    CHECK(disc_key(disc("SCES_502.94", Region::Europe)) == "eu");
    CHECK(disc_key(disc("SCPS_150.09", Region::Japan)) == "jp");
    // Any other serial is a disc of its own, whatever its region.
    CHECK(disc_key(disc("SCEF_502.94", Region::Europe)) == "SCEF-50294");
    CHECK(disc_key(disc("SCPS_560.05", Region::Japan)) == "SCPS-56005");
    CHECK(disc_key(disc("", Region::Europe)) == "disc-1e8d5712");
    CHECK(disc_key(disc("A:B/C\\D", Region::America)) == "A_B_C_D"); // a folder name everywhere
}

TEST(disc_library_mod_discs) {
    CHECK(mod_for_disc({}, "us", "PBPX_955.03"));
    CHECK(mod_for_disc({"eu", "SCEF-50294"}, "SCEF-50294", "SCEF_502.94"));
    CHECK(mod_for_disc({" scef_502.94 "}, "SCEF-50294", "SCEF_502.94")); // the serial as SYSTEM.CNF writes it
    CHECK(mod_for_disc({"PBPX-95503"}, "us", "PBPX_955.03"));
    CHECK(!mod_for_disc({"PBPX-95503"}, "us", "SCUS_971.02")); // a serial names one release
    CHECK(!mod_for_disc({"eu"}, "SCEF-50294", "SCEF_502.94")); // "eu": the PAL releases, not discs of their own
    CHECK(!mod_for_disc({"us"}, "eu", ""));
}

// Without a disc the key is "us": [mods.us] holds the choices, [mods] (before discs had their
// own) is the starting point, and a mod for other discs stays out whatever the choice.
TEST(disc_library_mod_choices) {
    const fs::path mods = test::temp_dir("disc-library-mods");
    write_text(mods / "everywhere" / "files" / "a.txt", "a");
    write_text(mods / "old_off" / "files" / "b.txt", "b");
    write_text(mods / "pal" / "mod.ini", "[mod]\ndiscs = eu, SCEF-50294\n");
    write_text(mods / "pal" / "files" / "c.txt", "c");
    write_text(mods / "ours" / "mod.ini", "[mod]\ndiscs = SCES-50294, us\n");
    write_text(mods / "ours" / "files" / "d.txt", "d");
    IniDocument cfg;
    cfg.set("mods", "old_off", "off");
    cfg.set("mods", "everywhere", "off");
    cfg.set("mods.us", "everywhere", "on"); // this disc's choice wins
    cfg.set("mods.eu", "ours", "off");      // another disc's: not this one's
    cfg.set("mods.us", "pal", "on");
    Vfs vfs(nullptr);
    CHECK(vfs.disc_key() == "us" && vfs.mods_section() == "mods.us");
    vfs.scan_mods(mods.string(), &cfg);
    std::map<std::string, const ModInfo*> by_id;
    for (const ModInfo& m : vfs.mods()) by_id[m.id] = &m;
    CHECK(by_id.size() == 4);
    if (by_id.size() != 4) return;
    CHECK(by_id["everywhere"]->active() && vfs.exists("a.txt"));
    CHECK(!by_id["old_off"]->enabled && !vfs.exists("b.txt"));
    CHECK(by_id["pal"]->enabled && !by_id["pal"]->for_disc && !by_id["pal"]->active() && !vfs.exists("c.txt"));
    CHECK(by_id["pal"]->discs == (std::vector<std::string>{"eu", "SCEF-50294"}));
    CHECK(by_id["ours"]->active() && vfs.exists("d.txt"));
    CHECK(vfs.source_of("d.txt") == "ours");
    fs::remove_all(mods);
}

// A disc of its own keeps saves/<key>/ and replays/<key>/; the first time, they start with copies
// of its region's (earlier versions kept them there), once; the region's stay as they are.
TEST(disc_library_own_folders) {
    const fs::path dir = test::temp_dir("disc-library-folders");
    game::set_save_folder(dir.string());
    const std::string base = dir.string() + "/";
    CHECK(game::SaveLibrary::user_dir("us") == base + "saves/");
    CHECK(game::SaveLibrary::user_dir("eu") == game::SaveLibrary::user_dir(Region::Europe));
    CHECK(game::SaveLibrary::user_dir("SCEF-50294") == base + "saves/SCEF-50294/");
    CHECK(race::replay_dir(base, "us") == base + "replays");
    CHECK(race::replay_dir(base, "SCEF-50294") == base + "replays/SCEF-50294");
    CHECK(race::replay_dir(base, "jp") == race::replay_dir(base, Region::Japan));

    write_text(dir / "replays" / "eu" / "tsukuba_nsx_20261101-120000.gt3replay", "replay eu");
    write_text(dir / "replays" / "eu" / "notes.txt", "not a replay");
    write_text(dir / "replays" / "eu" / "old" / "x_y_20261101-120001.gt3replay", "below: stays");
    CHECK(race::seed_replay_dir(base, "eu", Region::Europe) == 0); // a release: its own folder already
    CHECK(race::seed_replay_dir(base, "SCEF-50294", Region::Europe) == 1);
    const fs::path own = dir / "replays" / "SCEF-50294";
    CHECK(fs::exists(own / "tsukuba_nsx_20261101-120000.gt3replay") && !fs::exists(own / "notes.txt") &&
          !fs::exists(own / "old"));
    CHECK(fs::exists(dir / "replays" / "eu" / "tsukuba_nsx_20261101-120000.gt3replay")); // copies
    write_text(dir / "replays" / "eu" / "later_20261102-120000.gt3replay", "after");
    CHECK(race::seed_replay_dir(base, "SCEF-50294", Region::Europe) == 0); // once
    CHECK(!fs::exists(own / "later_20261102-120000.gt3replay"));

    // Saves: the same rule, through SaveLibrary::copy_library (GT Mode's first use).
    write_text(dir / "saves" / "eu" / "Europe.sav", "GT3R-GTMODE eu");
    game::SaveLibrary lib(game::SaveLibrary::user_dir("SCEF-50294"));
    CHECK(lib.copy_library(game::SaveLibrary::user_dir(Region::Europe)) == 1);
    CHECK(fs::exists(dir / "saves" / "SCEF-50294" / "Europe.sav") && fs::exists(dir / "saves" / "eu" / "Europe.sav"));
    CHECK(lib.copy_library(game::SaveLibrary::user_dir(Region::Europe)) == 0);
    game::set_save_folder("");
    fs::remove_all(dir);
}

// The key through a reader (GT Mode's save library has only one).
TEST(disc_library_key_through_reader) {
    auto reader = [](const char* key, const char* region) {
        return [key, region](const std::string& p) -> std::optional<Bytes> {
            const char* v = p == Vfs::kDiscKeyFile ? key : p == Vfs::kRegionFile ? region : nullptr;
            if (!v) return std::nullopt;
            return Bytes(v, v + std::strlen(v));
        };
    };
    CHECK(disc_key_of(reader("SCEF-50294", "eu")) == "SCEF-50294");
    CHECK(disc_key_of(reader(nullptr, "eu")) == "eu"); // a reader without one: its region's
    CHECK(disc_key_of(nullptr) == "us");
}

// The releases open as themselves: verified, their own files, their region's folders.
TEST(disc_library_release_fingerprint) {
    Disc* d = test::any_region_disc();
    if (!d) SKIP("needs GT3R_DISC");
    const DiscInfo& i = d->info();
    std::printf("  %s v%s: files %s, key %s\n", i.serial.c_str(), i.version.c_str(), i.digest.c_str(), i.key.c_str());
    CHECK(i.verified && !i.modified);
    CHECK(i.digest.size() == 16);
    CHECK(i.key == region_key(i.region) && d->volume().disc_key() == i.key && d->volume().serial() == i.serial);
}
