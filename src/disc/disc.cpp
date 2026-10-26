#include "disc/disc.h"

#include "base/config.h"
#include "base/log.h"

namespace gt3r {

namespace {

struct KnownDisc {
    const char* serial;
    const char* version;
    const char* description;
};

// Discs this project has been tested against. Add entries as they are
// verified (see docs/COMPATIBILITY.md).
constexpr KnownDisc kKnownDiscs[] = {
    {"PBPX_955.03", "1.10", "Gran Turismo 3 A-Spec (NTSC-U/C)"},
    {"SCUS_971.02", "1.00", "Gran Turismo 3 A-Spec (NTSC-U/C)"}, // same CORE.GT3 and GT3.VOL as 1.10
    {"SCES_502.94", "2.00", "Gran Turismo 3 A-Spec (PAL)"},
};

std::string g_db_suffix = "_us";

} // namespace

const std::string& Disc::current_db_suffix() { return g_db_suffix; }

std::unique_ptr<Disc> Disc::open(const std::string& path) {
    std::unique_ptr<Disc> disc(new Disc());
    disc->file_ = File::open(path);
    if (!disc->file_) throw FormatError("Could not open file: " + path);
    disc->iso_ = std::make_unique<Iso9660>(*disc->file_);
    Iso9660& iso = *disc->iso_;

    DiscInfo& info = disc->info_;
    info.volume_id = iso.volume_id();
    if (auto cnf = iso.find("SYSTEM.CNF")) {
        Bytes b = iso.read(*cnf);
        IniDocument doc = IniDocument::parse(std::string_view(reinterpret_cast<char*>(b.data()), b.size()));
        std::string boot = doc.get("", "BOOT2").value_or("");
        // "cdrom0:\PBPX_955.03;1" -> "PBPX_955.03"
        if (auto bs = boot.find_last_of("\\/:"); bs != std::string::npos) boot = boot.substr(bs + 1);
        if (auto semi = boot.find(';'); semi != std::string::npos) boot.resize(semi);
        info.serial = boot;
        info.version = doc.get("", "VER").value_or("");
        info.video = doc.get("", "VMODE").value_or("");
    }

    auto core = iso.find("CORE.GT3");
    auto vol = iso.find("GT3.VOL");
    if (!core || !vol)
        throw FormatError("This does not look like a Gran Turismo 3 disc image (CORE.GT3 / GT3.VOL missing).");

    // PAL (SCES/SLES, VMODE PAL) loads the _eu set, NTSC-J the unsuffixed one.
    if (info.video == "PAL" || info.serial.rfind("SCES", 0) == 0 || info.serial.rfind("SLES", 0) == 0)
        info.db_suffix = "_eu";
    else if (info.serial.rfind("SCPS", 0) == 0 || info.serial.rfind("PAPX", 0) == 0 || info.serial.rfind("SLPS", 0) == 0)
        info.db_suffix = "";
    g_db_suffix = info.db_suffix;

    for (const KnownDisc& k : kKnownDiscs) {
        if (info.serial == k.serial && info.version == k.version) {
            info.verified = true;
            info.description = k.description;
        }
    }
    if (!info.verified) {
        info.description = "Unverified Gran Turismo 3 disc";
        LOG_WARN("Disc %s v%s has not been verified; continuing", info.serial.c_str(), info.version.c_str());
    }

    disc->volume_ = std::make_unique<Volume>(*disc->file_, u64(vol->lba) * kSectorSize, vol->size);
    Disc* d = disc.get();
    disc->volume_->set_program_reader([d] { return d->read_core_gt3(); });
    LOG_INFO("Opened %s (%s v%s, %s)", path.c_str(), info.serial.c_str(), info.version.c_str(), info.video.c_str());
    return disc;
}

Bytes Disc::read_core_gt3() {
    auto core = iso_->find("CORE.GT3");
    if (!core) throw FormatError("CORE.GT3 missing");
    return iso_->read(*core);
}

} // namespace gt3r
