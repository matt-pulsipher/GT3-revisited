// Texture pack commands (formats/texture_pack.h, docs/MODDING.md "Texture packs"):
//
//   tex-dump <outdir> [prefix]   every texture of the disc's files as PNGs named by key
//   tex-keys <vol-path>          the keys of a file's textures ("anim": its colours animate)
//   tex-pcsx2 <pack> <mod dir>   convert a PCSX2 texture pack (textures/<serial> folder) into a mod

#include "tool.h"

#include "base/file.h"
#include "formats/clut_animation.h"
#include "formats/menu/pmb.h"
#include "formats/pcsx2_textures.h"
#include "formats/texture_pack.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace gt3r::tool {

namespace {

int cmd_tex_dump(Disc& disc, const Args& a) {
    const std::string prefix = a.size() > 1 ? a[1] : "";
    texture_pack::Dump dump(a[0]);
    const auto start = std::chrono::steady_clock::now();
    size_t files = 0;
    bool ok = true;
    for (const Volume::Entry& e : disc.volume().entries()) {
        if (e.path.rfind(prefix, 0) != 0 || !texture_pack::dump_reads(e.path) || e.size == 0) continue;
        try {
            dump.add_file(e.path, disc.volume().read(e), &ok);
        } catch (const std::exception& ex) {
            std::printf("  %s: %s\n", e.path.c_str(), ex.what());
        }
        ++files;
    }
    ok = dump.finish() && ok;
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("%zu files, %zu textures saved to %s in %.1f s%s\n", files, dump.saved(), a[0].c_str(), s,
                ok ? "" : " (some could not be written)");
    return ok ? 0 : 1;
}

int cmd_tex_keys(Disc& disc, const Args& a) {
    Bytes data = disc.volume().read(need(disc.volume(), a[0]));
    auto show = [](const std::string& label, ByteSpan set) {
        const Tex1 t = Tex1::parse(set);
        const std::vector<u64> keys = texture_pack::keys_of_set(set);
        const std::vector<u32> animated = ClutTextures(set).records(); // colours animate: "anim"
        for (size_t i = 0; i < keys.size(); ++i)
            std::printf("  %s-%zu %ux%u %s%s\n", label.c_str(), i, t.textures[i].tex0.width(), t.textures[i].tex0.height(),
                        texture_pack::key_name(keys[i]).c_str(),
                        std::binary_search(animated.begin(), animated.end(), u32(i)) ? " anim" : "");
    };
    if (menu::PmbFile::is_pmb(data)) {
        menu::PmbFile pmb = menu::PmbFile::parse(data);
        for (size_t i = 0; i < pmb.images.size(); ++i) show("img" + std::to_string(i), pmb.image_data(data, i));
        return 0;
    }
    size_t n = 0;
    for (size_t off : find_tex1_sets(data)) show("set" + std::to_string(n++), ByteSpan(data).subspan(off));
    return 0;
}

int cmd_tex_pcsx2(Disc& disc, const Args& a) {
    const auto start = std::chrono::steady_clock::now();
    texture_pack::Pcsx2Index index;
    for (const Volume::Entry& e : disc.volume().entries()) {
        if (!texture_pack::dump_reads(e.path) || e.size == 0) continue;
        try {
            index.add_file(e.path, disc.volume().read(e));
        } catch (const std::exception&) {
        }
    }
    std::printf("indexed %zu textures in %.1f s\n", index.size(),
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    const std::string mod = a[1];
    texture_pack::Pcsx2Import r = texture_pack::import_pcsx2_pack(a[0], index, mod + "/textures", [&](const std::string& p) {
        const Volume::Entry* e = disc.volume().find(p);
        return e ? std::optional<Bytes>(disc.volume().read(*e)) : std::nullopt;
    });
    if (!host_path_exists(mod + "/mod.ini")) {
        const std::string ini = "[mod]\nname = PCSX2 texture pack\ndescription = Converted from a PCSX2 texture pack.\n";
        save(mod + "/mod.ini", ByteSpan(reinterpret_cast<const u8*>(ini.data()), ini.size()));
    }
    std::printf("%zu pictures: %zu converted, %zu without a dump, %zu not recognised, %zu failed\n", r.pictures,
                r.converted, r.no_dump.size(), r.unmatched.size(), r.failed.size());
    for (const std::string& n : r.unmatched) std::printf("  not recognised: %s\n", n.c_str());
    for (const std::string& n : r.no_dump) std::printf("  no dump: %s\n", n.c_str());
    for (const std::string& n : r.failed) std::printf("  failed: %s\n", n.c_str());
    return r.failed.empty() ? 0 : 1;
}

} // namespace

GT3TOOL_COMMAND(tex_dump, "tex-dump", "<outdir> [prefix]",
                "save every texture as a PNG named by its texture pack key (and textures.txt)", 1, cmd_tex_dump);
GT3TOOL_COMMAND(tex_keys, "tex-keys", "<vol-path>", "list the texture pack keys of a file's textures", 1, cmd_tex_keys);
GT3TOOL_COMMAND(tex_pcsx2, "tex-pcsx2", "<pcsx2 textures/<serial> folder> <mod dir>",
                "convert a PCSX2 texture pack (replacements/ named like dumps/) into a mod's textures/", 2, cmd_tex_pcsx2);

} // namespace gt3r::tool
