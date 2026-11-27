// PS2 memory-card containers (.psu, card images): synthetic data only.

#include "test_framework.h"

#include "formats/ps2/memcard.h"

#include <cstring>
#include <ctime>

using namespace gt3r;
using namespace gt3r::ps2;

namespace {

McDir sample_dir(const std::string& name, size_t big) {
    McDir d;
    d.name = name;
    d.time = 1792164309; // 2026-10-16
    Bytes a(big), b(96), c;
    for (size_t i = 0; i < a.size(); ++i) a[i] = u8(i * 7 + 3);
    for (size_t i = 0; i < b.size(); ++i) b[i] = u8(255 - i);
    d.files = {{"icon0.ico", b}, {name, a}, {"empty", c}};
    return d;
}

bool same(const McDir& a, const McDir& b) {
    if (a.name != b.name || a.files.size() != b.files.size()) return false;
    for (size_t i = 0; i < a.files.size(); ++i)
        if (a.files[i].name != b.files[i].name || a.files[i].data != b.files[i].data) return false;
    return true;
}

} // namespace

TEST(memcard_psu_roundtrip) {
    McDir d = sample_dir("BASCUS-97102GAMEDATA", 70464);
    Bytes psu = write_psu(d);
    // Directory entry, ".", "..", then each file entry + data padded to 1 KB.
    CHECK(psu.size() == 3 * 512 + (512 + 1024) + (512 + 69 * 1024) + 512);
    CHECK(rd16(psu, 0) == 0x8427 && rd32(psu, 4) == 5);
    CHECK(std::memcmp(&psu[0x40], "BASCUS-97102GAMEDATA", 20) == 0);
    CHECK(rd16(psu, 512) == 0x8427 && rd16(psu, 1024) == 0xA426 && rd16(psu, 1536) == 0x8497);
    McDir back = read_psu(psu);
    CHECK(same(d, back));
    CHECK(back.time == d.time); // card times are Japan time; the conversion round-trips
    bool threw = false;
    try {
        read_psu(ByteSpan(psu).first(700));
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(memcard_card_image_roundtrip) {
    std::vector<McDir> dirs = {sample_dir("BASCUS-97102GAMEDATA", 70464), sample_dir("BASCUS-97102S0001", 600)};
    Bytes card = make_card(dirs);
    CHECK(card.size() == kCardSizeEcc && is_card_image(card));
    // As the BIOS formats a card (compared with one formatted in PCSX2): type 2, flags
    // 0x2B, 41 = first allocatable cluster, 8135 allocatable; written erase blocks carry
    // the ECC in each page's spare area (a page of 0xFF: 77 7F 7F per 128 bytes), unused
    // blocks stay erased.
    CHECK(card[0x150] == 2 && card[0x151] == 0x2B && rd32(card, 0x34) == 41 && rd32(card, 0x38) == 8135);
    static const u8 kFfEcc[16] = {0x77, 0x7F, 0x7F, 0x77, 0x7F, 0x7F, 0x77, 0x7F, 0x7F, 0x77, 0x7F, 0x7F, 0, 0, 0, 0};
    CHECK(std::memcmp(&card[528 + 512], kFfEcc, 16) == 0);
    CHECK(card[(16383 * 528) + 512] == 0xFF);
    std::vector<McDir> back = read_card(card);
    CHECK(back.size() == 2 && same(back[0], dirs[0]) && same(back[1], dirs[1]));
    // The same card without the spare areas (a plain 8 MB dump) reads the same.
    Bytes plain;
    for (size_t p = 0; p < kCardSizeEcc / 528; ++p) plain.insert(plain.end(), card.begin() + p * 528, card.begin() + p * 528 + 512);
    CHECK(plain.size() == kCardSize && is_card_image(plain));
    back = read_card(plain);
    CHECK(back.size() == 2 && same(back[1], dirs[1]));
    // Unformatted cards and other files are refused.
    CHECK(!is_card_image(Bytes(kCardSizeEcc, 0xFF)));
    bool threw = false;
    try {
        read_card(Bytes(kCardSizeEcc, 0xFF));
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

// A card made at a time given: the root directory's entries and the directories without a
// time of their own take it, so the same directories make the same card.
TEST(memcard_card_image_time) {
    std::vector<McDir> dirs = {sample_dir("BASCUS-97102GAMEDATA", 3000), sample_dir("BASCUS-97102S0001", 600)};
    dirs[1].time = 0;
    const s64 t = 1792250709; // 2026-10-17 15:25:09 UTC
    const Bytes a = make_card(dirs, t), b = make_card(dirs, t);
    CHECK(a == b);
    const std::vector<McDir> back = read_card(a);
    CHECK(back.size() == 2 && back[0].time == dirs[0].time && back[1].time == t);
    // The root's "." entry and ".." (the root's first cluster: 41 + its cluster in the superblock; pages of 528 bytes): its
    // modification time {0, sec, min, hour, day, month, year} in Japan time, 2026-10-18 00:25:09.
    const size_t root = (41 + rd32(a, 0x3C)) * 2 * 528;
    static const u8 kRootTime[8] = {0, 9, 25, 0, 18, 10, 0xEA, 0x07};
    CHECK(std::memcmp(&a[root + 0x18], kRootTime, 8) == 0 && std::memcmp(&a[root + 528 + 0x18], kRootTime, 8) == 0);
    // Without a time: the clock's, as before.
    const s64 before = s64(std::time(nullptr));
    const std::vector<McDir> now = read_card(make_card(dirs));
    CHECK(now[1].time >= before && now[1].time <= s64(std::time(nullptr)));
}
