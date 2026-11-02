#include "disc/iso9660.h"

#include "base/config.h"

#include <cstring>

namespace gt3r {

namespace {
bool has_pvd(Reader& file, u64 sector) {
    if (file.size() < (sector + 1) * u64(kSectorSize)) return false;
    const Bytes pvd = file.read_at(sector * kSectorSize, 6);
    return pvd[0] == 1 && std::memcmp(&pvd[1], "CD001", 5) == 0;
}
} // namespace

std::vector<std::unique_ptr<Iso9660>> iso_layers(Reader& file) {
    std::vector<std::unique_ptr<Iso9660>> layers;
    layers.push_back(std::make_unique<Iso9660>(file));
    const u64 end = layers[0]->volume_bytes() / kSectorSize; // the first layer's sectors
    for (u64 pvd : {end, end + 16, end - 16})
        if (end > 32 && has_pvd(file, pvd)) {
            layers.push_back(std::make_unique<Iso9660>(file, pvd - 16));
            break;
        }
    return layers;
}

Iso9660::Iso9660(Reader& file, u64 base_sector) : file_(file), base_(base_sector) {
    if (file.size() < (base_ + 17) * kSectorSize) throw FormatError("image too small to be ISO9660");
    Bytes pvd = file.read_at((base_ + 16) * kSectorSize, kSectorSize);
    if (pvd[0] != 1 || std::memcmp(&pvd[1], "CD001", 5) != 0)
        throw FormatError("no ISO9660 primary volume descriptor");
    volume_id_ = std::string(trim(std::string_view(reinterpret_cast<char*>(&pvd[40]), 32)));
    volume_bytes_ = u64(rd32(pvd, 80)) * rd16(pvd, 128);
    root_.lba = rd32(pvd, 156 + 2);
    root_.size = rd32(pvd, 156 + 10);
    root_.is_dir = true;
}

std::vector<Iso9660::Entry> Iso9660::read_dir(u32 lba, u32 size) const {
    Bytes d = file_.read_at((base_ + lba) * kSectorSize, size);
    std::vector<Entry> out;
    size_t i = 0;
    while (i < d.size()) {
        u8 len = d[i];
        if (len == 0) { // records never straddle sectors; skip padding
            i = (i / kSectorSize + 1) * kSectorSize;
            continue;
        }
        if (i + len > d.size() || len < 34) break;
        u8 name_len = d[i + 32];
        if (33 + name_len > len) break; // a name longer than its record
        std::string name(reinterpret_cast<char*>(&d[i + 33]), name_len);
        bool special = name_len == 1 && (name[0] == 0 || name[0] == 1);
        if (!special) {
            if (auto semi = name.find(';'); semi != std::string::npos) name.resize(semi);
            Entry e;
            e.name = name;
            e.lba = rd32(d, i + 2);
            e.size = rd32(d, i + 10);
            e.is_dir = (d[i + 25] & 2) != 0;
            out.push_back(std::move(e));
        }
        i += len;
    }
    return out;
}

std::vector<Iso9660::Entry> Iso9660::list(const std::string& path) const {
    Entry dir = root_;
    if (!path.empty()) {
        auto e = find(path);
        if (!e || !e->is_dir) return {};
        dir = *e;
    }
    return read_dir(dir.lba, static_cast<u32>(dir.size));
}

std::optional<Iso9660::Entry> Iso9660::find(const std::string& path) const {
    Entry cur = root_;
    for (std::string_view part : split(path, '/')) {
        if (part.empty()) continue;
        if (!cur.is_dir) return std::nullopt;
        bool found = false;
        for (const Entry& e : read_dir(cur.lba, static_cast<u32>(cur.size))) {
            if (to_lower(e.name) == to_lower(part)) {
                cur = e;
                found = true;
                break;
            }
        }
        if (!found) return std::nullopt;
    }
    return cur;
}

Bytes Iso9660::read(const Entry& e) const { return file_.read_at(offset(e), e.size); }

} // namespace gt3r
