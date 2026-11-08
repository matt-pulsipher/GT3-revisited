#include "disc/disc.h"

#include "base/config.h"
#include "base/log.h"
#include "base/sha256.h"
#include "disc/core_image.h"
#include "disc/chd_image.h"

#include <algorithm>
#include <cstdio>

namespace gt3r {

namespace {

struct KnownDisc {
    const char* serial;
    const char* version;
    const char* description;
    const char* digest; // DiscInfo::digest of the release's own files; null when not known here
    bool tested = true; // false: a version players reported working
};

// Discs this project has been tested against, and ones players reported working. Add entries as
// they are verified (see docs/COMPATIBILITY.md).
constexpr KnownDisc kKnownDiscs[] = {
    {"PBPX_955.03", "1.10", "Gran Turismo 3 A-Spec (NTSC-U/C)", "164c486ca0a61886"},
    {"SCUS_971.02", "1.00", "Gran Turismo 3 A-Spec (NTSC-U/C)", "164c486ca0a61886"}, // same CORE.GT3 and GT3.VOL as 1.10
    {"SCES_502.94", "1.00", "Gran Turismo 3 A-Spec (PAL)", "075246ad42a4b418"},
    {"SCES_502.94", "2.00", "Gran Turismo 3 A-Spec (PAL)", nullptr, false},
    {"SCPS_150.09", "1.01", "Gran Turismo 3 A-Spec (NTSC-J)", "059939eff137da8f"},
};

bool known_serial(std::string_view serial) {
    for (const KnownDisc& k : kKnownDiscs)
        if (serial == k.serial) return true;
    return false;
}

} // namespace

std::string short_serial(std::string_view serial) {
    std::string out;
    for (char c : serial)
        if (c != '.') out += c == '_' ? '-' : c;
    return out;
}

std::string disc_key(const DiscInfo& info) {
    if (known_serial(info.serial)) return region_key(info.region);
    std::string key;
    for (char c : short_serial(info.serial)) // a folder name on every system
        key += (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ? c : '_';
    if (key.empty() || key.size() > 32) key = "disc-" + info.digest.substr(0, 8);
    return key;
}

std::string incomplete_image(u64 file_bytes, u64 volume_bytes) {
    if (volume_bytes == 0 || file_bytes >= volume_bytes) return {};
    char b[160];
    std::snprintf(b, sizeof b, "This disc image is incomplete (%.2f of %.2f GB). Make the image again from your disc.",
                  double(file_bytes) / 1e9, double(volume_bytes) / 1e9);
    return b;
}

std::unique_ptr<Disc> Disc::open(const std::string& path) {
    std::unique_ptr<Disc> disc(new Disc());
    auto file = File::open(path);
    if (!file) throw FormatError("Could not open file: " + path);
    // By content: a picked file's path (an Android content:// URI) often has no extension.
    if (ChdImage::is_chd(*file)) disc->file_ = ChdImage::open(std::move(file));
    else disc->file_ = std::move(file);
    // The file systems: one, or one per layer of a dual-layer image (the files may be in either).
    std::vector<std::unique_ptr<Iso9660>> layers = iso_layers(*disc->file_);
    disc->iso_ = std::move(layers[0]);
    for (size_t k = 1; k < layers.size(); ++k) disc->layers_.push_back(std::move(layers[k]));
    Iso9660& iso = *disc->iso_;

    DiscInfo& info = disc->info_;
    info.volume_id = iso.volume_id();
    // A dump that stopped early opens, but says so (its later files cannot be read).
    u64 volume_end = iso.volume_bytes();
    for (const auto& l : disc->layers_) volume_end = std::max(volume_end, l->base_sector() * kSectorSize + l->volume_bytes());
    info.problem = incomplete_image(disc->file_->size(), volume_end);
    if (!info.problem.empty()) LOG_WARN("%s (%s)", info.problem.c_str(), path.c_str());
    if (auto cnf = disc->find_file("SYSTEM.CNF")) {
        Bytes b = cnf->first->read(cnf->second);
        IniDocument doc = IniDocument::parse(std::string_view(reinterpret_cast<char*>(b.data()), b.size()));
        std::string boot = doc.get("", "BOOT2").value_or("");
        // "cdrom0:\PBPX_955.03;1" -> "PBPX_955.03"
        if (auto bs = boot.find_last_of("\\/:"); bs != std::string::npos) boot = boot.substr(bs + 1);
        if (auto semi = boot.find(';'); semi != std::string::npos) boot.resize(semi);
        info.serial = boot;
        info.version = doc.get("", "VER").value_or("");
        info.video = doc.get("", "VMODE").value_or("");
    }

    // The game volume: GT3.VOL, else the set of *.VOL files in the layers' roots (the one with the largest index first:
    // the main volume; the others' files join it).
    auto core = disc->find_file("CORE.GT3");
    struct Vol {
        u64 offset, size;
        u32 toc;
        bool gt3;
    };
    std::vector<Vol> vols;
    for (Iso9660* fs : disc->file_systems())
        for (const Iso9660::Entry& e : fs->list("")) {
            const std::string name = to_lower(e.name);
            if (e.is_dir || name.size() < 4 || name.compare(name.size() - 4, 4, ".vol") != 0 || e.size < 0x14) continue;
            const Bytes hdr = disc->file_->read_at(fs->offset(e), 0x14);
            if (rd32(hdr, 0) == Volume::kMagic) vols.push_back({fs->offset(e), e.size, rd32(hdr, 8), name == "gt3.vol"});
        }
    std::stable_sort(vols.begin(), vols.end(), [](const Vol& a, const Vol& b) { return a.gt3 != b.gt3 ? a.gt3 : a.toc > b.toc; });
    if (!core || vols.empty())
        throw FormatError("This does not look like a Gran Turismo 3 disc image (CORE.GT3 / GT3.VOL missing).");

    // The fingerprint: the program and the volumes' tables of contents (every file's name, place and size).
    const Bytes program = core->first->read(core->second);
    {
        Sha256 h;
        h.update(program);
        for (const Vol& v : vols) h.update(disc->file_->read_at(v.offset, std::min<u64>(v.toc, v.size)));
        const std::array<u8, 32> d = h.finish();
        static constexpr char kHex[] = "0123456789abcdef";
        for (size_t i = 0; i < 8; ++i) info.digest += kHex[d[i] >> 4], info.digest += kHex[d[i] & 15];
    }

    for (const KnownDisc& k : kKnownDiscs) {
        if (info.serial == k.serial && info.version == k.version) {
            info.verified = k.tested;
            info.reported = !k.tested;
            info.description = k.description;
            info.modified = k.digest && info.digest != k.digest;
        }
    }
    if (info.modified) {
        LOG_WARN("Disc %s v%s: files differ from the release's (a modified copy)", info.serial.c_str(), info.version.c_str());
    } else if (info.reported) {
        LOG_INFO("Disc %s v%s: reported working", info.serial.c_str(), info.version.c_str());
    } else if (!info.verified) {
        info.description = "Unverified Gran Turismo 3 disc";
        LOG_WARN("Disc %s v%s has not been verified; continuing", info.serial.c_str(), info.version.c_str());
    }

    // The region: the data set the disc's program loads, else the boot file's name, else the video mode.
    std::optional<Region> region;
    try {
        region = region_of_program(CoreImage::decode(program).program);
    } catch (const std::exception& e) {
        LOG_WARN("CORE.GT3: %s", e.what());
    }
    if (!region) region = region_of_serial(info.serial);
    if (!region && info.video == "PAL") region = Region::Europe; // a PAL video mode: a European release
    if (!region) LOG_WARN("Disc %s: region not recognised; using the NTSC-U data set", info.serial.c_str());
    info.region = region.value_or(Region::America);
    info.key = disc_key(info);

    disc->volume_ = std::make_unique<Volume>(*disc->file_, vols[0].offset, vols[0].size);
    for (size_t k = 1; k < vols.size(); ++k) disc->volume_->add_part(vols[k].offset, vols[k].size);
    Disc* d = disc.get();
    disc->volume_->set_program_reader([d] { return d->read_core_gt3(); });
    disc->volume_->set_region(info.region);
    disc->volume_->set_identity(info.key, info.serial);
    LOG_INFO("Opened %s (%s v%s, %s, %s data, files %s, key %s)", path.c_str(), info.serial.c_str(), info.version.c_str(),
             info.video.c_str(), region_name(info.region), info.digest.c_str(), info.key.c_str());
    return disc;
}

Bytes Disc::read_core_gt3() {
    auto core = find_file("CORE.GT3");
    if (!core) throw FormatError("CORE.GT3 missing");
    return core->first->read(core->second);
}

std::vector<Iso9660*> Disc::file_systems() const {
    std::vector<Iso9660*> v{iso_.get()};
    for (const auto& l : layers_) v.push_back(l.get());
    return v;
}

std::optional<std::pair<Iso9660*, Iso9660::Entry>> Disc::find_file(const std::string& path) const {
    for (Iso9660* fs : file_systems())
        if (auto e = fs->find(path)) return std::make_pair(fs, *e);
    return std::nullopt;
}

} // namespace gt3r
