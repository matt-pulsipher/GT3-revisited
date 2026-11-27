#include "formats/ps2/memcard.h"

#include <algorithm>
#include <cstring>
#include <ctime>

namespace gt3r::ps2 {

namespace {

constexpr size_t kPage = 512, kSpare = 16, kCluster = 1024, kEntry = 512;
constexpr u16 kModeDir = 0x8427, kModeFile = 0x8497, kModeParent = 0xA426;
constexpr u16 kExists = 0x8000, kIsDir = 0x0020, kIsFile = 0x0010;
const char kMagic[] = "Sony PS2 Memory Card Format ";

void wr16(Bytes& b, size_t o, u16 v) { std::memcpy(&b[o], &v, 2); }
void wr32(Bytes& b, size_t o, u32 v) { std::memcpy(&b[o], &v, 4); }

// Card times are Japan time (UTC+9).
void put_time(Bytes& b, size_t o, s64 t) {
    if (t <= 0) t = s64(std::time(nullptr));
    std::time_t j = std::time_t(t + 9 * 3600);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &j);
#else
    gmtime_r(&j, &tm);
#endif
    b[o + 1] = u8(tm.tm_sec);
    b[o + 2] = u8(tm.tm_min);
    b[o + 3] = u8(tm.tm_hour);
    b[o + 4] = u8(tm.tm_mday);
    b[o + 5] = u8(tm.tm_mon + 1);
    wr16(b, o + 6, u16(tm.tm_year + 1900));
}

s64 get_time(ByteSpan b, size_t o) {
    std::tm tm{};
    tm.tm_sec = b[o + 1];
    tm.tm_min = b[o + 2];
    tm.tm_hour = b[o + 3];
    tm.tm_mday = b[o + 4];
    tm.tm_mon = b[o + 5] - 1;
    tm.tm_year = rd16(b, o + 6) - 1900;
    if (tm.tm_year < 70 || tm.tm_mon < 0 || tm.tm_mday < 1) return 0;
#ifdef _WIN32
    s64 t = s64(_mkgmtime(&tm));
#else
    s64 t = s64(timegm(&tm));
#endif
    return t - 9 * 3600;
}

std::string entry_name(ByteSpan e) {
    std::string n;
    for (size_t i = 0x40; i < 0x60 && e[i]; ++i) n.push_back(char(e[i]));
    return n;
}

// A directory entry; `cluster` / `parent_entry` as the card stores them (0 in a .psu).
void put_entry(Bytes& b, size_t o, u16 mode, u32 length, s64 t, u32 cluster, u32 parent_entry,
               const std::string& name) {
    std::memset(&b[o], 0, kEntry);
    wr16(b, o, mode);
    wr32(b, o + 4, length);
    put_time(b, o + 8, t);
    wr32(b, o + 0x10, cluster);
    wr32(b, o + 0x14, parent_entry);
    put_time(b, o + 0x18, t);
    std::memcpy(&b[o + 0x40], name.data(), std::min<size_t>(name.size(), 31));
}

bool valid_name(const std::string& n) {
    return !n.empty() && n.size() < 32 && n != "." && n != ".." && n.find('/') == std::string::npos &&
           n.find('\\') == std::string::npos;
}

} // namespace

// --- .psu --------------------------------------------------------------------------

McDir read_psu(ByteSpan d) {
    if (d.size() < 3 * kEntry) throw FormatError("psu: too short");
    u16 mode = rd16(d, 0);
    if ((mode & (kExists | kIsDir)) != (kExists | kIsDir)) throw FormatError("psu: not a save directory");
    McDir dir;
    dir.name = entry_name(d.subspan(0, kEntry));
    dir.time = get_time(d, 0x18);
    if (!valid_name(dir.name)) throw FormatError("psu: bad directory name");
    u32 n = rd32(d, 4);
    size_t o = kEntry;
    for (u32 i = 0; i < n; ++i) {
        if (o + kEntry > d.size()) throw FormatError("psu: truncated");
        ByteSpan e = d.subspan(o, kEntry);
        o += kEntry;
        std::string name = entry_name(e);
        if (name == "." || name == "..") continue;
        if (!(rd16(e, 0) & kIsFile)) continue; // sub-directories do not occur in game saves
        u32 len = rd32(e, 4);
        if (o + len > d.size()) throw FormatError("psu: truncated file " + name);
        if (!valid_name(name)) throw FormatError("psu: bad file name");
        dir.files.push_back({name, Bytes(d.begin() + std::ptrdiff_t(o), d.begin() + std::ptrdiff_t(o + len))});
        o += (len + kCluster - 1) / kCluster * kCluster;
    }
    return dir;
}

Bytes write_psu(const McDir& dir) {
    size_t size = 3 * kEntry;
    for (const McFile& f : dir.files) size += kEntry + (f.data.size() + kCluster - 1) / kCluster * kCluster;
    Bytes b(size, 0);
    put_entry(b, 0, kModeDir, u32(dir.files.size() + 2), dir.time, 0, 0, dir.name);
    put_entry(b, kEntry, kModeDir, 0, dir.time, 0, 0, ".");
    put_entry(b, 2 * kEntry, kModeParent, 0, dir.time, 0, 0, "..");
    size_t o = 3 * kEntry;
    for (const McFile& f : dir.files) {
        put_entry(b, o, kModeFile, u32(f.data.size()), dir.time, 0, 0, f.name);
        o += kEntry;
        if (!f.data.empty()) std::memcpy(&b[o], f.data.data(), f.data.size());
        o += (f.data.size() + kCluster - 1) / kCluster * kCluster;
    }
    return b;
}

// --- Card image ------------------------------------------------------------------------

bool is_card_image(ByteSpan d) {
    return (d.size() == kCardSize || d.size() == kCardSizeEcc) && std::memcmp(d.data(), kMagic, 28) == 0;
}

std::vector<McDir> read_card(ByteSpan d) {
    if (!is_card_image(d)) throw FormatError("memory card: not a formatted 8 MB card image");
    const size_t stride = d.size() == kCardSizeEcc ? kPage + kSpare : kPage;
    if (rd16(d, 0x28) != kPage || rd16(d, 0x2A) != 2) throw FormatError("memory card: unsupported page layout");
    const u32 clusters = rd32(d, 0x30), alloc_offset = rd32(d, 0x34), root = rd32(d, 0x3C);
    const size_t card_clusters = d.size() / stride / 2;
    if (clusters > card_clusters) throw FormatError("memory card: bad superblock");
    // One cluster's data (absolute cluster number).
    auto cluster = [&](u32 c) {
        if (c >= card_clusters) throw FormatError("memory card: cluster out of range");
        Bytes out(kCluster);
        for (size_t p = 0; p < 2; ++p)
            std::memcpy(&out[p * kPage], &d[(size_t(c) * 2 + p) * stride], kPage);
        return out;
    };
    auto fat = [&](u32 n) -> u32 { // FAT entry of allocatable cluster n
        u32 fc = n / 256, ifc_index = fc / 256;
        if (ifc_index >= 32) throw FormatError("memory card: FAT out of range");
        Bytes ind = cluster(rd32(d, 0x50 + 4 * size_t(ifc_index)));
        Bytes f = cluster(rd32(ind, 4 * size_t(fc % 256)));
        return rd32(f, 4 * size_t(n % 256));
    };
    // The data of a chain starting at allocatable cluster `first`, `bytes` long.
    auto chain = [&](u32 first, size_t bytes) {
        Bytes out;
        u32 c = first;
        for (size_t guard = 0; out.size() < bytes; ++guard) {
            if (guard > card_clusters) throw FormatError("memory card: FAT loop");
            Bytes b = cluster(c + alloc_offset);
            out.insert(out.end(), b.begin(), b.end());
            if (out.size() >= bytes) break;
            u32 next = fat(c);
            if (!(next & 0x80000000u) || next == 0xFFFFFFFFu) throw FormatError("memory card: chain ends early");
            c = next & 0x7FFFFFFFu;
        }
        out.resize(bytes);
        return out;
    };
    Bytes first = chain(root, kEntry);
    u32 root_len = rd32(first, 4);
    if (root_len < 2 || root_len > 4096) throw FormatError("memory card: bad root directory");
    Bytes rootdir = chain(root, size_t(root_len) * kEntry);
    std::vector<McDir> out;
    for (u32 i = 2; i < root_len; ++i) {
        ByteSpan e = ByteSpan(rootdir).subspan(size_t(i) * kEntry, kEntry);
        u16 mode = rd16(e, 0);
        if ((mode & (kExists | kIsDir)) != (kExists | kIsDir)) continue;
        McDir dir;
        dir.name = entry_name(e);
        dir.time = get_time(e, 0x18);
        u32 n = rd32(e, 4);
        if (n > 4096) throw FormatError("memory card: bad directory " + dir.name);
        Bytes sub = chain(rd32(e, 0x10), size_t(n) * kEntry);
        for (u32 k = 2; k < n; ++k) {
            ByteSpan f = ByteSpan(sub).subspan(size_t(k) * kEntry, kEntry);
            u16 fm = rd16(f, 0);
            if ((fm & (kExists | kIsFile)) != (kExists | kIsFile)) continue;
            u32 len = rd32(f, 4);
            if (len > d.size()) throw FormatError("memory card: bad file size");
            dir.files.push_back({entry_name(f), len ? chain(rd32(f, 0x10), len) : Bytes()});
        }
        out.push_back(std::move(dir));
    }
    return out;
}

// --- New card image ----------------------------------------------------------------------

namespace {

// The card's Hamming code: 3 bytes per 128-byte chunk (column parity, line parity of
// the odd-parity bytes' indices and of their complements).
void ecc128(const u8* s, u8* out) {
    static u8 parity[256], colmask[256];
    static bool init = false;
    if (!init) {
        for (int b = 0; b < 256; ++b) {
            int p = 0;
            for (int k = 0; k < 8; ++k) p ^= (b >> k) & 1;
            parity[b] = u8(p);
        }
        static const u8 kMasks[7] = {0x55, 0x33, 0x0F, 0x00, 0xAA, 0xCC, 0xF0};
        for (int b = 0; b < 256; ++b) {
            u8 m = 0;
            for (int k = 0; k < 7; ++k) m |= u8(parity[b & kMasks[k]] << k);
            colmask[b] = m;
        }
        init = true;
    }
    u8 cp = 0x77, lp0 = 0x7F, lp1 = 0x7F;
    for (int i = 0; i < 128; ++i) {
        cp ^= colmask[s[i]];
        if (parity[s[i]]) {
            lp0 ^= u8(~i);
            lp1 ^= u8(i);
        }
    }
    out[0] = cp;
    out[1] = lp0 & 0x7F;
    out[2] = lp1;
}

} // namespace

Bytes make_card(const std::vector<McDir>& dirs, s64 root_time) {
    constexpr u32 kClusters = 8192, kIfc = 8, kFatClusters = 32;
    constexpr u32 kAllocOffset = kIfc + 1 + kFatClusters;       // 41
    constexpr u32 kAllocEnd = (1024 - 2) * 8 - kAllocOffset;    // two erase blocks kept for backups
    Bytes data(size_t(kClusters) * kCluster, 0xFF);
    std::vector<u32> fat(kFatClusters * 256, 0x7FFFFFFFu);
    for (u32 i = kAllocEnd; i < fat.size(); ++i) fat[i] = 0xFFFFFFFFu; // M: past the allocatable area
    u32 next_free = 0;
    // Allocates a chain of n clusters (at least 1), returns its first allocatable cluster.
    auto alloc = [&](size_t bytes) -> u32 {
        u32 n = u32(std::max<size_t>(1, (bytes + kCluster - 1) / kCluster));
        if (next_free + n > kAllocEnd) throw FormatError("memory card: saves do not fit");
        u32 first = next_free;
        for (u32 k = 0; k < n; ++k) fat[first + k] = k + 1 < n ? 0x80000000u | (first + k + 1) : 0xFFFFFFFFu;
        next_free += n;
        return first;
    };
    auto at = [&](u32 c) { return size_t(c + kAllocOffset) * kCluster; };
    auto write_chain = [&](u32 first, ByteSpan bytes) {
        std::memcpy(&data[at(first)], bytes.data(), bytes.size()); // chains are contiguous here
    };
    const s64 when = root_time > 0 ? root_time : s64(std::time(nullptr));
    // Root directory: ".", "..", one entry per save directory.
    Bytes root((2 + dirs.size()) * kEntry, 0);
    u32 root_first = alloc(root.size());
    put_entry(root, 0, kModeDir, u32(2 + dirs.size()), when, 0, 0, ".");
    put_entry(root, kEntry, kModeParent, 0, when, 0, 0, "..");
    for (size_t i = 0; i < dirs.size(); ++i) {
        const McDir& d = dirs[i];
        if (!valid_name(d.name)) throw FormatError("memory card: bad directory name");
        Bytes sub((2 + d.files.size()) * kEntry, 0);
        u32 sub_first = alloc(sub.size());
        const s64 t = d.time > 0 ? d.time : when;
        put_entry(sub, 0, kModeDir, 0, t, root_first, u32(2 + i), ".");
        put_entry(sub, kEntry, kModeParent, 0, t, 0, 0, "..");
        for (size_t k = 0; k < d.files.size(); ++k) {
            const McFile& f = d.files[k];
            if (!valid_name(f.name)) throw FormatError("memory card: bad file name");
            u32 first = f.data.empty() ? 0 : alloc(f.data.size());
            if (!f.data.empty()) write_chain(first, f.data);
            put_entry(sub, (2 + k) * kEntry, kModeFile, u32(f.data.size()), t, first, 0, f.name);
        }
        write_chain(sub_first, sub);
        put_entry(root, (2 + i) * kEntry, kModeDir, u32(2 + d.files.size()), t, sub_first, 0, d.name);
    }
    write_chain(root_first, root);
    // Superblock (cluster 0) and the FAT.
    {
        Bytes sb(kCluster, 0xFF);
        std::memset(sb.data(), 0, 0x17C);
        std::memcpy(&sb[0], kMagic, 28);
        std::memcpy(&sb[0x1C], "1.2.0.0", 7);
        wr16(sb, 0x28, u16(kPage));
        wr16(sb, 0x2A, 2);
        wr16(sb, 0x2C, 16);
        wr16(sb, 0x2E, 0xFF00);
        wr32(sb, 0x30, kClusters);
        wr32(sb, 0x34, kAllocOffset);
        wr32(sb, 0x38, kAllocEnd);
        wr32(sb, 0x3C, root_first);
        wr32(sb, 0x40, 1023);
        wr32(sb, 0x44, 1022);
        wr32(sb, 0x50, kIfc);
        for (size_t i = 0; i < 32; ++i) wr32(sb, 0xD0 + 4 * i, 0xFFFFFFFFu);
        // Card type, flags and the trailing words as the PS2 BIOS formats an 8 MB card
        // (checked against a card the BIOS formatted in PCSX2).
        sb[0x150] = 2;
        sb[0x151] = 0x2B;
        wr32(sb, 0x154, u32(kCluster));
        wr32(sb, 0x158, 256);
        wr32(sb, 0x15C, 8);
        wr32(sb, 0x160, 0xFFFFFFFFu);
        wr32(sb, 0x170, 8001);
        std::memcpy(&data[0], sb.data(), kCluster);
        Bytes ifc(kCluster, 0xFF);
        for (u32 i = 0; i < kFatClusters; ++i) wr32(ifc, 4 * size_t(i), kIfc + 1 + i);
        std::memcpy(&data[size_t(kIfc) * kCluster], ifc.data(), kCluster);
        for (u32 i = 0; i < kFatClusters; ++i)
            std::memcpy(&data[size_t(kIfc + 1 + i) * kCluster], &fat[size_t(i) * 256], kCluster);
    }
    // Pages with their spare area: the ECC (12 bytes, then 4 zero bytes) for every page of
    // an erase block (8 clusters) that holds written clusters (superblock, FAT, allocated
    // clusters), erased (0xFF) otherwise, as the BIOS leaves a card it formatted and wrote.
    Bytes out(kCardSizeEcc, 0xFF);
    const size_t pages = data.size() / kPage;
    const u32 last_block = (kAllocOffset + next_free - 1) / 8;
    for (size_t p = 0; p < pages; ++p) {
        const bool written = p / 16 <= last_block;
        const u8* src = &data[p * kPage];
        u8* dst = &out[p * (kPage + kSpare)];
        std::memcpy(dst, src, kPage);
        if (!written) continue;
        std::memset(dst + kPage, 0, kSpare);
        for (int k = 0; k < 4; ++k) ecc128(src + 128 * k, dst + kPage + 3 * k);
    }
    return out;
}

} // namespace gt3r::ps2
