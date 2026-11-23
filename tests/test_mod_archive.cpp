// Installing mods from .zip archives (vfs/mod_archive.h): archives built here in memory.

#include "test_framework.h"

#include "base/config.h"
#include "base/file.h"
#include "vfs/mod_archive.h"

#include <zlib.h>

#include <filesystem>

using namespace gt3r;
namespace fs = std::filesystem;

namespace {

struct ZipItem {
    std::string name;
    std::string data;
    u16 method = 8;    // 0 stored, 8 Deflate (others are written stored, as a label only)
    bool symlink = false;
    bool bad_crc = false;
};

void put16(Bytes& b, u32 v) { b.push_back(u8(v)), b.push_back(u8(v >> 8)); }
void put32(Bytes& b, u32 v) { put16(b, v & 0xFFFF), put16(b, v >> 16); }
void put64(Bytes& b, u64 v) { put32(b, u32(v)), put32(b, u32(v >> 32)); }

Bytes raw_deflate(const std::string& s) {
    z_stream zs{};
    deflateInit2(&zs, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    Bytes out(deflateBound(&zs, uLong(s.size())));
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(s.data()));
    zs.avail_in = uInt(s.size());
    zs.next_out = out.data();
    zs.avail_out = uInt(out.size());
    deflate(&zs, Z_FINISH);
    out.resize(zs.total_out);
    deflateEnd(&zs);
    return out;
}

// A .zip of `items`; zip64 writes every size and offset in ZIP64 form.
Bytes make_zip(const std::vector<ZipItem>& items, bool zip64 = false) {
    Bytes out, cd;
    for (const ZipItem& it : items) {
        const Bytes body = it.method == 8 ? raw_deflate(it.data) : Bytes(it.data.begin(), it.data.end());
        u32 crc = u32(crc32(0, reinterpret_cast<const Bytef*>(it.data.data()), uInt(it.data.size())));
        if (it.bad_crc) crc ^= 1;
        const u64 offset = out.size();
        put32(out, 0x04034b50), put16(out, 20), put16(out, 0), put16(out, it.method), put32(out, 0);
        put32(out, crc), put32(out, u32(body.size())), put32(out, u32(it.data.size()));
        put16(out, u16(it.name.size())), put16(out, 0);
        out.insert(out.end(), it.name.begin(), it.name.end());
        out.insert(out.end(), body.begin(), body.end());

        Bytes extra;
        if (zip64) put16(extra, 1), put16(extra, 24), put64(extra, it.data.size()), put64(extra, body.size()), put64(extra, offset);
        put32(cd, 0x02014b50), put16(cd, 3 << 8 | 20), put16(cd, 20), put16(cd, 0), put16(cd, it.method), put32(cd, 0);
        put32(cd, crc);
        put32(cd, zip64 ? 0xFFFFFFFF : u32(body.size())), put32(cd, zip64 ? 0xFFFFFFFF : u32(it.data.size()));
        put16(cd, u16(it.name.size())), put16(cd, u16(extra.size())), put16(cd, 0), put16(cd, 0), put16(cd, 0);
        put32(cd, u32(it.symlink ? 0120777 : 0100644) << 16);
        put32(cd, zip64 ? 0xFFFFFFFF : u32(offset));
        cd.insert(cd.end(), it.name.begin(), it.name.end());
        cd.insert(cd.end(), extra.begin(), extra.end());
    }
    const u64 cd_offset = out.size();
    out.insert(out.end(), cd.begin(), cd.end());
    if (zip64) {
        const u64 at = out.size();
        put32(out, 0x06064b50), put64(out, 44), put16(out, 45), put16(out, 45), put32(out, 0), put32(out, 0);
        put64(out, items.size()), put64(out, items.size()), put64(out, cd.size()), put64(out, cd_offset);
        put32(out, 0x07064b50), put32(out, 0), put64(out, at), put32(out, 1);
    }
    put32(out, 0x06054b50), put16(out, 0), put16(out, 0);
    put16(out, zip64 ? 0xFFFF : u16(items.size())), put16(out, zip64 ? 0xFFFF : u16(items.size()));
    put32(out, zip64 ? 0xFFFFFFFF : u32(cd.size())), put32(out, zip64 ? 0xFFFFFFFF : u32(cd_offset));
    put16(out, 0);
    return out;
}

std::string str(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// A fresh folder for one test: <test temp root>/mod-archive-<n>/ with mods/ and the archive.
struct Scratch {
    fs::path root;
    Scratch() {
        static int made = 0;
        root = test::temp_dir("mod-archive-" + std::to_string(++made));
        fs::create_directories(root / "mods");
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    std::string mods() const { return str(root / "mods"); }
    std::string staging() const { return str(root / ".mod-install"); }
    std::unique_ptr<ZipArchive> zip(const Bytes& data, const std::string& name = "test.zip") const {
        const std::string path = str(root / name);
        write_host_file(path, data);
        return ZipArchive::open(path);
    }
    std::string read(const std::string& rel) const {
        auto b = read_host_file(str(root / "mods" / rel));
        return b ? std::string(b->begin(), b->end()) : std::string("<missing>");
    }
    bool exists(const std::string& rel) const { return fs::exists(root / "mods" / rel); }
};

// Plans and installs `zip` into s.mods(); the plan, or nullopt with *error.
std::optional<ModArchivePlan> install(const Scratch& s, const ZipArchive& zip, std::string_view name = "test.zip",
                                      std::string* error = nullptr) {
    auto plan = plan_mod_install(zip, name, s.mods(), error);
    if (!plan || !install_mod_archive(zip, *plan, s.mods(), s.staging(), nullptr, error)) return std::nullopt;
    return plan;
}

const std::string kBig(300000, 'x'); // more than one 256 KB piece

} // namespace

TEST(mod_archive_ids_and_paths) {
    CHECK(mod_id_from("Example Coupe") == "Example_Coupe");
    CHECK(mod_id_from("../evil") == "evil");
    CHECK(mod_id_from("a  /  b") == "a_b");
    CHECK(mod_id_from(".hidden") == "hidden");
    CHECK(mod_id_from("\xE6\x97\xA5\xE6\x9C\xAC").empty());
    CHECK(mod_id_from("con") == "con_mod");
    CHECK(mod_id_from(std::string(80, 'a')).size() == 48);
    CHECK(safe_archive_path("mod/files/menu/a.img"));
    CHECK(safe_archive_path("mod/cars/"));
    CHECK(!safe_archive_path("../a"));
    CHECK(!safe_archive_path("a/../../b"));
    CHECK(!safe_archive_path("/etc/passwd"));
    CHECK(!safe_archive_path("C:/Windows/a"));
    CHECK(!safe_archive_path("a\nb"));
    CHECK(!safe_archive_path(""));
}

TEST(mod_archive_installs_a_folder_mod) {
    Scratch s;
    auto zip = s.zip(make_zip({
        {"example_coupe/mod.ini", "[mod]\nname = Example Coupe\nversion = 1.0\n"},
        {"example_coupe/cars/coupe.ini", "label = coupe\n", 0},
        {"example_coupe/files/bgm/big.ads", kBig},
        {"example_coupe/files/empty.bin", ""},
        {"example_coupe/files/", "", 0},
        {"__MACOSX/example_coupe/._mod.ini", "junk"},
        {"example_coupe/.DS_Store", "junk"},
        {"README.txt", "read me"},
    }));
    CHECK(zip != nullptr);
    if (!zip) return;
    std::string err;
    auto plan = install(s, *zip, "test.zip", &err);
    CHECK(plan.has_value());
    if (!plan) return std::printf("    %s\n", err.c_str()), void();
    CHECK(plan->mods.size() == 1);
    CHECK(plan->mods[0].id == "example_coupe");
    CHECK(plan->mods[0].name == "Example Coupe");
    CHECK(plan->mods[0].version == "1.0");
    CHECK(!plan->mods[0].replaces);
    CHECK(s.read("example_coupe/mod.ini").find("Example Coupe") != std::string::npos);
    CHECK(s.read("example_coupe/cars/coupe.ini") == "label = coupe\n");
    CHECK(s.read("example_coupe/files/bgm/big.ads") == kBig);
    CHECK(s.read("example_coupe/files/empty.bin").empty());
    CHECK(!s.exists("example_coupe/.DS_Store"));
    CHECK(!s.exists("README.txt"));
    CHECK(!s.exists("__MACOSX"));
    CHECK(!fs::exists(s.root / ".mod-install")); // staging cleaned up
}

TEST(mod_archive_top_level_mod_ids) {
    Scratch s;
    const Bytes data = make_zip({{"mod.ini", "[mod]\nname = Shiny Wheels\n"}, {"files/a.bin", "a"}});
    auto zip = s.zip(data);
    auto plan = install(s, *zip, "Cool Mod v2.zip");
    CHECK(plan && plan->mods.size() == 1 && plan->mods[0].id == "Cool_Mod_v2");
    CHECK(s.read("Cool_Mod_v2/files/a.bin") == "a");
    // A picked file without a usable name (an Android document id): the mod's own name.
    auto plan2 = plan_mod_install(*zip, "msf:1000012345", s.mods());
    CHECK(plan2 && plan2->mods[0].id == "Shiny_Wheels");
    // No mod.ini: a top folder with files/ is still a mod.
    auto loose = s.zip(make_zip({{"Readme.md", "hi"}, {"pack/files/a.bin", "a"}}), "loose.zip");
    auto plan3 = plan_mod_install(*loose, "loose.zip", s.mods());
    CHECK(plan3 && plan3->mods.size() == 1 && plan3->mods[0].id == "pack" && plan3->mods[0].files.size() == 1);
    // Paint colours alone (liveries/*.ini, race/custom_paints.h): a mod too.
    auto paints = s.zip(make_zip({{"liveries/orange.ini", "[livery]\nmodel = ni0137\ncolor = #FF7A00\n"}}), "orange.zip");
    auto plan4 = install(s, *paints, "R34 orange.zip");
    CHECK(plan4 && plan4->mods.size() == 1 && plan4->mods[0].id == "R34_orange");
    CHECK(s.read("R34_orange/liveries/orange.ini").rfind("[livery]", 0) == 0);
}

TEST(mod_archive_several_mods) {
    Scratch s;
    auto zip = s.zip(make_zip({
        {"pack/a/mod.ini", "[mod]\nname = A\n"},
        {"pack/a/files/x.bin", "ax"},
        {"pack/a/extras/mod.ini", "[mod]\nname = nested, part of A\n"},
        {"pack/b/MOD.INI", "[mod]\nname = B\n"},
        {"pack/b/cars/b.ini", "b"},
        {"pack/README.txt", "outside every mod"},
    }));
    auto plan = install(s, *zip);
    CHECK(plan && plan->mods.size() == 2);
    if (!plan || plan->mods.size() != 2) return;
    CHECK(plan->mods[0].id == "a" && plan->mods[1].id == "b");
    CHECK(plan->mods[0].files.size() == 3);
    CHECK(s.read("a/extras/mod.ini").find("nested") != std::string::npos);
    CHECK(s.read("b/mod.ini").find("name = B") != std::string::npos); // written as mod.ini
    CHECK(!s.exists("README.txt") && !s.exists("pack"));
}

TEST(mod_archive_replaces_an_installed_mod) {
    Scratch s;
    auto v1 = s.zip(make_zip({{"m/mod.ini", "[mod]\nname = M\nversion = 1\n"}, {"m/files/old.bin", "old"}}), "v1.zip");
    CHECK(install(s, *v1).has_value());
    auto v2 = s.zip(make_zip({{"m/mod.ini", "[mod]\nname = M\nversion = 2\n"}, {"m/files/new.bin", "new"}}), "v2.zip");
    auto plan = plan_mod_install(*v2, "v2.zip", s.mods());
    CHECK(plan && plan->mods[0].replaces && plan->mods[0].installed_version == "1" && plan->mods[0].version == "2");
    if (!plan) return;
    ModInstallProgress progress;
    CHECK(install_mod_archive(*v2, *plan, s.mods(), s.staging(), &progress));
    CHECK(progress.total == plan->bytes && progress.done == plan->bytes);
    CHECK(s.read("m/files/new.bin") == "new");
    CHECK(!s.exists("m/files/old.bin")); // replaced, not merged
    CHECK(!fs::exists(s.root / ".mod-install"));
}

TEST(mod_archive_damage_leaves_installed_mods_alone) {
    Scratch s;
    auto v1 = s.zip(make_zip({{"m/mod.ini", "[mod]\nversion = 1\n"}}), "v1.zip");
    CHECK(install(s, *v1).has_value());
    auto bad = s.zip(make_zip({{"m/mod.ini", "[mod]\nversion = 2\n"}, {"m/files/a.bin", kBig, 8, false, true}}), "bad.zip");
    std::string err;
    CHECK(!install(s, *bad, "bad.zip", &err).has_value());
    CHECK(err.find("damaged") != std::string::npos);
    CHECK(s.read("m/mod.ini").find("version = 1") != std::string::npos);
    CHECK(!fs::exists(s.root / ".mod-install"));
}

TEST(mod_archive_refuses_unsafe_and_unsupported) {
    Scratch s;
    std::string err;
    auto evil = s.zip(make_zip({{"m/mod.ini", "[mod]\n"}, {"m/../../escape.txt", "x"}}), "evil.zip");
    CHECK(evil && !plan_mod_install(*evil, "evil.zip", s.mods(), &err));
    CHECK(err.find("unsafe") != std::string::npos);
    CHECK(!fs::exists(s.root / "escape.txt"));
    auto abs = s.zip(make_zip({{"/m/mod.ini", "[mod]\n"}}), "abs.zip");
    CHECK(abs && !plan_mod_install(*abs, "abs.zip", s.mods()));
    auto none = s.zip(make_zip({{"photo.png", "x"}, {"notes/readme.txt", "y"}}), "none.zip");
    CHECK(none && !plan_mod_install(*none, "none.zip", s.mods(), &err));
    CHECK(err.find("no mod") != std::string::npos);
    auto bzip = s.zip(make_zip({{"m/mod.ini", "[mod]\n", 12}}), "bzip.zip");
    CHECK(bzip && !install(s, *bzip, "bzip.zip", &err));
    CHECK(err.find("Deflate") != std::string::npos);
    CHECK(!s.exists("m"));
    CHECK(!s.zip(Bytes{'n', 'o', 't', ' ', 'a', ' ', 'z', 'i', 'p'}, "text.zip"));
}

TEST(mod_archive_zip64_and_symlinks) {
    Scratch s;
    auto zip = s.zip(make_zip({{"m/mod.ini", "[mod]\nname = Z\n"}, {"m/files/big.bin", kBig}, {"m/files/link", "/etc", 0, true}}, true));
    CHECK(zip != nullptr);
    if (!zip) return;
    CHECK(zip->entries().size() == 3);
    auto plan = install(s, *zip);
    CHECK(plan && plan->mods[0].name == "Z");
    CHECK(s.read("m/files/big.bin") == kBig);
    CHECK(!s.exists("m/files/link"));
}

TEST(mod_archive_remove) {
    Scratch s;
    fs::create_directories(s.root / "mods" / "gone" / "files");
    write_host_file(str(s.root / "mods" / "gone" / "files" / "a.bin"), Bytes{1});
    CHECK(remove_mod_folder(s.mods(), "gone"));
    CHECK(!s.exists("gone"));
    CHECK(!remove_mod_folder(s.mods(), ".."));
    CHECK(!remove_mod_folder(s.mods(), "a/b"));
    CHECK(fs::exists(s.root / "mods"));
}
