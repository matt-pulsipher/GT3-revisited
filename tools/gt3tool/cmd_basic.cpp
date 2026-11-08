// Core gt3tool commands: disc info, listing, extraction, textures, audio.

#include "tool.h"

#include "disc/core_image.h"
#include "formats/ads.h"
#include "formats/bgm_index.h"
#include "formats/png_writer.h"
#include "formats/tex1.h"

#include <cstdio>

namespace gt3r::tool {

namespace {

int cmd_info(Disc& disc, const Args&) {
    const DiscInfo& i = disc.info();
    std::printf("Volume ID : %s\nSerial    : %s\nVersion   : %s\nVideo     : %s\nStatus    : %s (%s)\n",
                i.volume_id.c_str(), i.serial.c_str(), i.version.c_str(), i.video.c_str(),
                i.verified ? "verified" : i.reported ? "reported working" : "unverified", i.description.c_str());
    std::printf("Files     : %s%s\nDisc key  : %s\n", i.digest.c_str(), i.modified ? " (modified: not the release's)" : "",
                i.key.c_str());
    CoreImage core = CoreImage::decode(disc.read_core_gt3());
    std::printf("CORE.GT3  : %zu bytes at 0x%08X, entry 0x%08X\n", core.program.size(), core.load_address,
                core.entry_point);
    u64 total = 0, packed = 0;
    for (auto& e : disc.volume().entries()) {
        total += e.size;
        packed += e.compressed;
    }
    std::printf("GT3.VOL   : %zu files (%llu compressed), %.1f MiB\n", disc.volume().entries().size(),
                (unsigned long long)packed, double(total) / (1 << 20));
    return 0;
}

int cmd_ls(Disc& disc, const Args& a) {
    std::string dir = a.empty() ? "" : a[0];
    Volume& vol = disc.volume();
    for (const std::string& name : vol.list_dir(dir)) {
        std::string full = (dir.empty() || dir.back() == '/' ? dir : dir + "/") + name;
        if (const Volume::Entry* e = vol.find(full))
            std::printf("%10u %s %s\n", e->size, e->compressed ? "z" : " ", name.c_str());
        else
            std::printf("%10s   %s\n", "<dir>", name.c_str());
    }
    return 0;
}

int cmd_extract(Disc& disc, const Args& a) {
    const std::string& prefix = a[0];
    size_t n = 0;
    for (const auto& e : disc.volume().entries()) {
        if (e.path.compare(0, prefix.size(), prefix) != 0) continue;
        if (!save(a[1] + "/" + e.path, disc.volume().read(e))) return 1;
        ++n;
    }
    std::printf("extracted %zu files\n", n);
    return 0;
}

#ifdef GT3R_PROGRAM_TOOLS
int cmd_core(Disc& disc, const Args& a) {
    CoreImage core = CoreImage::decode(disc.read_core_gt3());
    return save(a[0], core.program) ? 0 : 1;
}
#endif

int cmd_tex(Disc& disc, const Args& a) {
    Bytes data = disc.volume().read(need(disc.volume(), a[0]));
    std::vector<size_t> sets = find_tex1_sets(data);
    std::printf("%zu texture set(s)\n", sets.size());
    for (size_t s = 0; s < sets.size(); ++s) {
        ByteSpan sub = ByteSpan(data).subspan(sets[s]);
        Tex1 tex = Tex1::parse(sub);
        GsMemory gs;
        tex.upload(sub, gs);
        for (size_t i = 0; i < tex.textures.size(); ++i) {
            const Tex0& t = tex.textures[i].tex0;
            Tex1::Image img = tex.decode(gs, i);
            char name[64];
            std::snprintf(name, sizeof(name), "/set%02zu_tex%02zu.png", s, i);
            std::printf("  set %zu @0x%zx tex %zu: %ux%u %s tbp=%u tbw=%u cbp=%u -> %ux%u\n", s, sets[s], i,
                        t.width(), t.height(), psm_name(t.psm), t.tbp, t.tbw, t.cbp, img.width, img.height);
            if (!save(a[1] + name, encode_png(img.width, img.height, img.rgba))) return 1;
        }
    }
    return 0;
}

int cmd_wav(Disc& disc, const Args& a) {
    Volume& vol = disc.volume();
    const Volume::Entry& e = need(vol, a[0]);
    Bytes head(AdsHeader::kHeaderBytes);
    vol.read_range(e, 0, head.data(), head.size());
    AdsHeader h = AdsHeader::parse(head);
    AdsStream stream(h, [&](u64 off, void* dst, size_t len) { vol.read_range(e, off, dst, len); }, false);
    std::vector<s16> pcm;
    s16 buf[4096 * 8];
    while (size_t n = stream.read(buf, 4096)) pcm.insert(pcm.end(), buf, buf + n * h.channels);
    std::printf("%u Hz, %u ch, %.1f s\n", h.sample_rate, h.channels, double(pcm.size()) / h.channels / h.sample_rate);
    return save(a[1], encode_wav(h.sample_rate, h.channels, pcm)) ? 0 : 1;
}

int cmd_bgm(Disc& disc, const Args&) {
    Bytes inf = disc.volume().read(need(disc.volume(), "bgm/ads.inf"));
    for (const BgmTrack& t : parse_bgm_index(inf))
        std::printf("%-14s %-48s %s\n", t.file.c_str(), t.title.c_str(), t.artist.c_str());
    return 0;
}

} // namespace

GT3TOOL_COMMAND(info, "info", "", "identify the disc", 0, cmd_info);
GT3TOOL_COMMAND(ls, "ls", "[dir]", "list GT3.VOL contents", 0, cmd_ls);
GT3TOOL_COMMAND(extract, "extract", "<prefix> <outdir>", "extract VOL files under prefix (decompressed)", 2,
                cmd_extract);
#ifdef GT3R_PROGRAM_TOOLS
GT3TOOL_COMMAND(core, "core", "<out.bin>", "write the unpacked game program", 1, cmd_core);
#endif
GT3TOOL_COMMAND(tex, "tex", "<vol-path> <outdir>", "decode every Tex1 set in a file to PNGs", 2, cmd_tex);
GT3TOOL_COMMAND(wav, "wav", "<vol-path> <out.wav>", "decode an .ads music stream", 2, cmd_wav);
GT3TOOL_COMMAND(bgm, "bgm", "", "list music tracks", 0, cmd_bgm);

} // namespace gt3r::tool
