#include "game/gt_save.h"

#include "base/file.h"
#include "base/game_random.h"
#include "formats/ps2/memcard.h"
#include "race/race_playlist.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

namespace gt3r::game {

namespace {

// --- Native text format ---------------------------------------------------------

const char* const kMagic = "GT3R-GTMODE";

std::string hex64(u64 v) {
    char b[24];
    std::snprintf(b, sizeof b, "%llx", (unsigned long long)v);
    return b;
}

// Strings are written as-is with spaces and '%' escaped, "-" for empty.
std::string esc(const std::string& s) {
    if (s.empty()) return "-";
    std::string o;
    for (char c : s) {
        // (Bytes >= 0x80 too: the seeded records' one-byte name marks.)
        if (c == ' ' || c == '%' || c == '\n' || c == '\r' || c == '\t' || u8(c) >= 0x80) {
            char b[4];
            std::snprintf(b, sizeof b, "%%%02X", unsigned(u8(c)));
            o += b;
        } else {
            o.push_back(c);
        }
    }
    return o == "-" ? "%2D" : o;
}

std::string unesc(const std::string& s) {
    if (s == "-") return {};
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            o.push_back(char(std::stoi(s.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            o.push_back(s[i]);
        }
    }
    return o;
}

// All CarSettings ints in a fixed order (the order of the struct): the native save's
// settings / ssettings values and the PS2 car setup record's words (kEqSettingOffsets).
// New fields must be appended at the end: read_settings() fills the fields from the front,
// so saves written before keep loading with the new fields at their defaults (and
// kEqSettingOffsets, indexed by this order, needs an entry or a bound for them).
std::vector<int*> settings_fields(sim::CarSettings& s) {
    std::vector<int*> f;
    for (int& v : s.gears) f.push_back(&v);
    f.push_back(&s.final_drive);
    f.push_back(&s.gear_slider);
    f.push_back(&s.front_torque);
    f.push_back(&s.brake_front);
    f.push_back(&s.brake_rear);
    f.push_back(&s.downforce_front);
    f.push_back(&s.downforce_rear);
    for (int& v : s.turbo) f.push_back(&v);
    for (auto* a : {&s.camber, &s.ride_height, &s.toe, &s.spring, &s.motion_ratio})
        for (int& v : *a) f.push_back(&v);
    for (int& v : s.damper) f.push_back(&v);
    for (int& v : s.arb) f.push_back(&v);
    for (int& v : s.lsd) f.push_back(&v);
    f.push_back(&s.tcs);
    for (int& v : s.ascc) f.push_back(&v);
    return f;
}

// A settings / ssettings line's values, settings_fields() order. A save from before a field
// was added has fewer (the rest keep their defaults), one from a newer build more (ignored).
// False on a value that is not a number.
bool read_settings(std::istream& in, sim::CarSettings& s) {
    for (int* v : settings_fields(s)) {
        if ((in >> std::ws).eof()) return true;
        if (!(in >> *v)) return false;
    }
    return true;
}

std::string owned_hex(const std::bitset<88>& b) {
    std::string s;
    for (int i = 0; i < 88; i += 4) {
        int v = 0;
        for (int k = 0; k < 4; ++k) v |= b.test(size_t(i + k)) ? 1 << k : 0;
        s.push_back("0123456789abcdef"[v]);
    }
    return s;
}

std::bitset<88> owned_from_hex(const std::string& s) {
    std::bitset<88> b;
    for (size_t i = 0; i < s.size() && i * 4 < 88; ++i) {
        int v = std::stoi(s.substr(i, 1), nullptr, 16);
        for (int k = 0; k < 4; ++k)
            if (v & (1 << k)) b.set(i * 4 + size_t(k));
    }
    return b;
}

// --- PS2 GAMEDATA -----------------------------------------------------------------

u32 crc32(ByteSpan d) {
    static u32 table[256];
    static bool init = false;
    if (!init) {
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    u32 c = 0xFFFFFFFFu;
    for (u8 b : d) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

constexpr size_t kHeader = 0x40, kBlock = 0x130, kCarRec = 0x204, kWheelRec = 0x44;
constexpr size_t kLicenses = 0x3FC0, kRaceBest = 0x5F8, kCourseBest = 0xB0, kArcade = 0xB40, kAt = 0x1A68,
                 kTimeTrial = 0x8400, kMachine = 0x900, kPrizeFlags = 0xC8, kChamp = 0xF2C, kOptions = 0x888;
constexpr u32 kEmptyTime = 0x157529FF;
// GAMEDATA version 7.x; the minor is the release's (7.0 NTSC-J, 7.1 NTSC-U, 7.2 PAL) and a load
// checks only the major.
constexpr u16 kMajor = 7;
u16 gamedata_minor(Region r) { return r == Region::Japan ? 0 : r == Region::Europe ? 2 : 1; }

// Car setup record part slots (+0x18 + 8 i) -> paramdb table.
constexpr int kEqSlotTables[26] = {0, 1, 3, 6, 13, 17, 18, 21, 28, 29, 2, 4, 5,
                                   7, 8, 9, 10, 11, 12, 14, 15, 16, 20, 19, 23, 22};
// Car setup record user settings words, record offsets in CarSettings field order (settings_fields()).
constexpr u32 kEqSettingOffsets[] = {
    0xF0,  0xF4,  0xF8,  0xFC,  0x100, 0x104, 0x108, 0x10C, // gears (reverse, 1..7)
    0x110, 0x114, 0x118, 0x11C, 0x120, 0x124, 0x128,        // final, slider, split, brakes, downforce
    0x12C, 0x130, 0x134, 0x138, 0x13C, 0x140,               // turbo
    0x144, 0x148, 0x14C, 0x150, 0x154, 0x158, 0x15C, 0x160, 0x164, 0x168, // camber..motion ratio
    0x16C, 0x170, 0x174, 0x178, 0x17C, 0x180, 0x184, 0x188, // dampers
    0x18C, 0x190,                                           // anti-roll bars
    0x194, 0x198, 0x19C, 0x1A0, 0x1A4, 0x1A8,               // LSD
    0x1B0,                                                  // TCS
    0x1BC, 0x1C0, 0x1C4, 0x1C8,                             // ASCC
};

void wr32(Bytes& b, size_t o, u32 v) { std::memcpy(&b[o], &v, 4); }
void wr16(Bytes& b, size_t o, u16 v) { std::memcpy(&b[o], &v, 2); }
void wr64(Bytes& b, size_t o, u64 v) { std::memcpy(&b[o], &v, 8); }
float rdf(ByteSpan d, size_t o) {
    float f;
    u32 v = rd32(d, o);
    std::memcpy(&f, &v, 4);
    return f;
}
void wrf(Bytes& b, size_t o, float f) {
    u32 v;
    std::memcpy(&v, &f, 4);
    wr32(b, o, v);
}

std::string label_of(const data::CarDatabase& db, data::TableId t, u64 code) {
    if (!code) return {};
    for (ByteSpan r : db.table(t).records)
        if (rd64(r, 0) == code) return db.label(code);
    return db.label(code);
}

std::string at_code(size_t i) {
    char b[16];
    std::snprintf(b, sizeof b, "AT%02u0001", unsigned(i + 1));
    return b;
}

// Ranking records (license 0x154, time trial 0x2A4, records 0x300) keep a 32-byte block per
// entry that the original encodes from the run after the name entry (30 characters, I: the
// records page's entry code); an unused or built-in entry has 30 '-'.
void put_dashes(Bytes& p, size_t o) { std::memset(&p[o], '-', 30); }

std::string fixed_str(ByteSpan d, size_t o, size_t n) {
    std::string s;
    for (size_t i = 0; i < n && d[o + i]; ++i) s.push_back(char(d[o + i]));
    return s;
}

std::string read_code(ByteSpan d, size_t o) {
    std::string s = fixed_str(d, o, 32);
    return s.find_first_not_of('-') == std::string::npos ? std::string() : s;
}

void put_code(Bytes& p, size_t o, const std::string& code) {
    if (code.empty()) return put_dashes(p, o);
    std::memset(&p[o], 0, 32);
    std::memcpy(&p[o], code.data(), std::min<size_t>(code.size(), 31));
}

// Course time-trial and machine-test rankings (0x300 each: u32 value[12], char name[12][16]
// at +0x30, u64 car code[12] at +0xF0, u32 stamp[12] at +0x150, the 32-byte blocks at
// +0x180). Times are ms ("no time" = empty); the top-speed test's speeds int(m/s x 1e5)
// (0 = empty).
constexpr size_t kRanking = 0x300;
static_assert(kTimeTrial == kRanking * size_t(kCourseTimeTrials) && kMachine == kRanking * 3);

std::vector<RankEntry> read_ranking(ByteSpan d, size_t r, bool speed, const data::CarDatabase& db) {
    std::vector<RankEntry> out;
    for (size_t k = 0; k < 12; ++k) {
        const u32 v = rd32(d, r + 4 * k);
        if (v == 0 || (!speed && v == kEmptyTime)) break;
        RankEntry e;
        e.value = speed ? double(v) * 1e-5 * 3.6 : v / 1000.0;
        e.name = fixed_str(d, r + 0x30 + 16 * k, 16);
        e.car = label_of(db, data::TableId::Car, rd64(d, r + 0xF0 + 8 * k));
        e.stamp = rd32(d, r + 0x150 + 4 * k);
        e.code = read_code(d, r + 0x180 + 32 * k);
        out.push_back(std::move(e));
    }
    return out;
}

// An empty time table is a new game's ("no time", 30 '-'); an empty speed table is all zero.
void write_ranking(Bytes& p, size_t r, const std::vector<RankEntry>* rank, bool speed) {
    for (size_t k = 0; k < 12; ++k) {
        if (!rank || k >= rank->size()) {
            if (!speed) {
                wr32(p, r + 4 * k, kEmptyTime);
                put_dashes(p, r + 0x180 + 32 * k);
            }
            continue;
        }
        const RankEntry& e = (*rank)[k];
        wr32(p, r + 4 * k, speed ? u32(std::floor(e.value / 3.6 * 1e5 + 1e-6)) : u32(std::llround(e.value * 1000)));
        std::memcpy(&p[r + 0x30 + 16 * k], e.name.data(), std::min<size_t>(e.name.size(), 15));
        if (!e.car.empty()) wr64(p, r + 0xF0 + 8 * k, db::id_hash(e.car));
        wr32(p, r + 0x150 + 4 * k, e.stamp);
        put_code(p, r + 0x180 + 32 * k, e.code);
    }
}

// The machine tests' tables follow the 44 course ones: 0-400 m, 0-1000 m, top speed.
const char* const kMachineTests[3] = {"M400", "M1000", "MMAX"};

// Arcade time trial i's car (CLASS_COMPULSORY entry i) as its base car (as GT mode enters it).
std::string challenge_car(const GtData& data, size_t i) {
    auto cls = data.arcade_classes.find("CLASS_COMPULSORY");
    if (cls == data.arcade_classes.end() || i >= cls->second.size()) return {};
    const std::string& label = cls->second[i].label;
    for (const data::CarVariant& v : data.db.arcade_cars())
        if (v.label == label) return v.car;
    return label;
}

// Arcade time-trial ranking (0x2A4: u32 ms[12], char name[12][16], u32 stamp[12],
// char car[12][32], s32 best rank). The new game's three built-in records per challenge
// have the one-byte name mark 0xFF - 3 i - k (k-th record of challenge i, see
// GtMode::time_trial_seed_ranking), the stamp 0x3264FD64 and no car (30 '-'). (That stamp is
// the clock of the PS2 the reference new game was made on, 2026-10-16: the original stamps
// the seeds with the new game's time; ours keep no such time.)
constexpr u32 kBuiltinStamp = 0x3264FD64;
// The last name entered (scalar block +0x70; gt_mode §12.2): up to 15 characters and a 0.
constexpr size_t kNameField = 16;

// The options block (0x888) of a new game, for saves that never came from the PS2 (a
// zeroed block would turn all sound off and empty the music list). Read from a new
// game saved by the original; decoded: +0x00 screen (0 = 4:3), the sound switches
// (1 = on) +0x04 menu BGM, +0x08 menu SE, +0x0C race BGM, +0x10 race SE, +0x54 replay
// BGM, +0x58 replay SE, and the music list at +0x228. The rest (vibration, assists, 2P
// battle, ghost, GT FORCE, the button tables at +0x63C) is written as the new game has
// it (M).
Bytes default_ps2_options(const GtData& data) {
    Bytes o(kOptions, 0);
    static const u32 kWords[25] = {0, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1};
    for (size_t i = 0; i < 25; ++i) wr32(o, 4 * i, kWords[i]);
    wr32(o, 0xD8, 2);
    wr32(o, 0xDC, 2);
    set_ps2_replay_pad(o, {}); // 0.1, off
    // The music list, kept as the race playlist (music §2.3, race::RacePlaylist): u32 song
    // count, u16 cursor, u8 shuffle, u8 "no song listed", u16 seed, then 64 entries of 16
    // bytes {u64 id_hash(file), u8 played, u8 in the list, s16 order}. A new game's list is
    // shuffled from the first integer draw of the shared generator (gt_mode §20.1) in its
    // start-up state; that leaves the seed 0x456, as in a new game saved by the original.
    std::vector<u64> ids;
    for (size_t k = 0; k < data.race_songs.size() && k < race::RacePlaylist::kMaxSongs; ++k)
        ids.push_back(db::id_hash(data.race_songs[k]));
    GameRandom boot;
    const race::RacePlaylist list = race::RacePlaylist::with_songs(ids, u16(boot.int31()));
    wr32(o, 0x228, u32(list.songs().size()));
    wr16(o, 0x22C, list.cursor());
    o[0x22E] = list.shuffle() ? 1 : 0;
    o[0x22F] = list.has_songs() ? 0 : 1;
    wr16(o, 0x230, list.seed());
    for (size_t k = 0; k < race::RacePlaylist::kMaxSongs; ++k) {
        const size_t e = 0x238 + 16 * k;
        o[e + 9] = 1; // every entry of a new list is marked "in the list", used or not
        if (k >= list.songs().size()) continue;
        const race::RacePlaylist::Song& song = list.songs()[k];
        wr64(o, e, song.id);
        o[e + 8] = song.played ? 1 : 0;
        wr16(o, e + 10, u16(song.order));
    }
    static const u8 kKeys[] = {12, 14, 13, 11, 16, 18, 17, 15, 19, 0,  12, 14, 13, 11, 16, 18, 17, 15, 19, 1,
                               12, 14, 13, 11, 16, 18, 17, 15, 19, 1,  12, 14, 13, 11, 16, 18, 17, 15, 19, 20,
                               27, 28, 26, 25, 24, 21, 22, 29, 23, 20, 27, 28, 26, 25, 24, 21, 22, 29, 23, 20,
                               27, 28, 30, 33, 26, 23, 21, 24, 19, 20, 27, 28, 30, 33, 26, 23, 21, 24, 19};
    for (size_t i = 0; i < sizeof kKeys; ++i) wr32(o, 0x63C + 4 * i, kKeys[i]);
    for (size_t k = 0; k < 4; ++k) { // four 0x20-byte records {0x400, 0x200, 0, 0x100, 0, 0x100, 0, 0}
        const size_t r = 0x7D0 + 0x20 * k;
        wr32(o, r, 0x400);
        wr32(o, r + 4, 0x200);
        wr32(o, r + 12, 0x100);
        wr32(o, r + 20, 0x100);
    }
    return o;
}

} // namespace

std::string save_to_text(const GameState& s) {
    std::ostringstream o;
    o.precision(12);
    o << kMagic << " " << kSaveVersion << "\n";
    o << "credits " << s.credits << "\n";
    o << "day " << s.day << "\n";
    o << "races " << s.races << "\n";
    o << "wins " << s.wins << "\n";
    o << "prize_money " << s.prize_money << "\n";
    o << "distance " << s.distance_m << "\n";
    o << "next_serial " << s.next_serial << "\n";
    o << "trophies " << s.trophies << "\n";
    o << "prize_cars " << s.prize_cars << "\n";
    o << "rng " << s.rng << "\n";
    o << "ride " << s.ride << "\n";
    for (const GarageCar& c : s.garage) {
        o << "car " << c.serial << " " << esc(c.car) << " " << c.color << " " << esc(c.wheel) << " " << esc(c.origin)
          << "\n";
        o << " parts";
        for (u64 p : c.parts) o << " " << hex64(p);
        o << "\n owned " << owned_hex(c.owned) << "\n settings";
        sim::CarSettings st = c.settings;
        for (int* v : settings_fields(st)) o << " " << *v;
        o << "\n ref_final " << c.ref_final;
        o << "\n cond " << c.cond.odometer_m << " " << c.cond.engine_wear_m << " " << c.cond.oil_m << " "
          << c.cond.dirt << "\n";
    }
    for (const std::string& w : s.wheels) o << "wheel " << esc(w) << "\n";
    for (const SettingFile& f : s.setting_files) {
        o << "setfile " << esc(f.car) << " " << f.day << " " << f.ref_final << "\n sparts";
        for (u64 p : f.parts) o << " " << hex64(p);
        o << "\n ssettings";
        sim::CarSettings st = f.settings;
        for (int* v : settings_fields(st)) o << " " << *v;
        o << "\n sinfo " << f.time << " " << f.power_ps << " " << esc(f.comment) << "\n";
    }
    if (s.machine.t400 > 0) o << "mtest 400 " << s.machine.t400 << " " << esc(s.machine.car400) << "\n";
    if (s.machine.t1000 > 0) o << "mtest 1000 " << s.machine.t1000 << " " << esc(s.machine.car1000) << "\n";
    if (s.machine.top_speed > 0) o << "mtest max " << s.machine.top_speed << " " << esc(s.machine.car_top) << "\n";
    for (const auto& [key, v] : s.rankings)
        for (const RankEntry& r : v)
            o << "rank " << esc(key) << " " << r.value << " " << esc(r.car) << " " << r.day << " " << esc(r.name) << " "
              << r.stamp << " " << esc(r.code) << "\n";
    if (s.trophy_flags) o << "trophyflags " << s.trophy_flags << "\n";
    o << "player " << esc(s.player_name) << "\n";
    if (!s.file_comment.empty()) o << "fcomment " << esc(s.file_comment) << "\n";
    if (!s.ps2_options.empty()) {
        o << "ps2opt ";
        for (u8 b : s.ps2_options) o << "0123456789abcdef"[b >> 4] << "0123456789abcdef"[b & 15];
        o << "\n";
    }
    {
        const ArcadeProgress& a = s.arcade;
        u32 flags = 0;
        for (int k = 0; k < kArcadeCarLevels; ++k) flags |= a.car_flags[size_t(k)] ? 1u << k : 0u;
        o << "arcade " << a.course_level << " " << flags << " " << a.awards << "\n";
        for (size_t i = 0; i < a.best.size(); ++i)
            if (a.best[i] != kArcadeNoPosition) o << "abest " << i << " " << a.best[i] << "\n";
        for (size_t i = 0; i < a.at_best_rank.size(); ++i)
            if (a.at_best_rank[i] != kArcadeNoPosition) o << "atrank " << i << " " << a.at_best_rank[i] << "\n";
    }
    if (s.ending_short_seen || s.ending_long_seen)
        o << "endings " << int(s.ending_short_seen) << " " << int(s.ending_long_seen) << "\n";
    for (int i = 0; i < kLicenseCount * kTestsPerLicense; ++i) {
        const LicenseTestRecord& t = s.license_tests[size_t(i)];
        if (t.medal == Medal::None && t.best_time < 0) continue;
        o << "license " << i / 8 << " " << i % 8 << " " << int(t.medal) << " " << t.best_time << "\n";
    }
    for (const auto& [code, pos] : s.race_best) o << "race " << esc(code) << " " << pos << "\n";
    for (const std::string& p : s.prizes_given) o << "prize " << esc(p) << "\n";
    if (s.championship.active()) {
        const Championship& c = s.championship;
        o << "champ " << esc(c.series) << " " << c.next << " " << c.races.size();
        for (const std::string& r : c.races) o << " " << esc(r);
        o << " " << c.entrants.size();
        for (const std::string& e : c.entrants) o << " " << esc(e);
        for (int p : c.points) o << " " << p;
        o << "\n";
    }
    o << "end\n";
    return o.str();
}

GameState load_from_text(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line)) throw FormatError("gtmode save: empty");
    {
        std::istringstream h(line);
        std::string magic;
        int version = 0;
        h >> magic >> version;
        if (magic != kMagic) throw FormatError("gtmode save: not a GT mode save");
        // A newer version is refused, not read as far as it goes: versions only change for
        // what older builds cannot read (new records are new keys, new car settings values
        // are appended), and a misread game would be autosaved over the save.
        if (version > kSaveVersion)
            throw FormatError("gtmode save: from a newer version (" + std::to_string(version) + ")");
        if (version < 1) throw FormatError("gtmode save: unsupported version " + std::to_string(version));
    }
    GameState s;
    GarageCar* car = nullptr;
    bool ended = false;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string key;
        if (!(l >> key)) continue;
        auto need = [&](bool ok) {
            if (!ok) throw FormatError("gtmode save: bad line: " + line);
        };
        if (key == "credits") need(bool(l >> s.credits));
        else if (key == "day") need(bool(l >> s.day));
        else if (key == "races") need(bool(l >> s.races));
        else if (key == "wins") need(bool(l >> s.wins));
        else if (key == "prize_money") need(bool(l >> s.prize_money));
        else if (key == "distance") need(bool(l >> s.distance_m));
        else if (key == "next_serial") need(bool(l >> s.next_serial));
        else if (key == "trophies") need(bool(l >> s.trophies));
        else if (key == "prize_cars") need(bool(l >> s.prize_cars));
        else if (key == "rng") need(bool(l >> s.rng));
        else if (key == "ride") need(bool(l >> s.ride));
        else if (key == "car") {
            GarageCar c;
            std::string label, wheel, origin;
            need(bool(l >> c.serial >> label >> c.color >> wheel >> origin));
            c.car = unesc(label), c.wheel = unesc(wheel), c.origin = unesc(origin);
            s.garage.push_back(std::move(c));
            car = &s.garage.back();
        } else if (key == "parts") {
            need(car != nullptr);
            for (u64& p : car->parts) {
                std::string h;
                need(bool(l >> h));
                p = std::stoull(h, nullptr, 16);
            }
        } else if (key == "owned") {
            std::string h;
            need(car && bool(l >> h));
            car->owned = owned_from_hex(h);
        } else if (key == "settings") {
            need(car && read_settings(l, car->settings));
        } else if (key == "ref_final") {
            need(car && bool(l >> car->ref_final));
        } else if (key == "cond") {
            need(car && bool(l >> car->cond.odometer_m >> car->cond.engine_wear_m >> car->cond.oil_m >> car->cond.dirt));
        } else if (key == "setfile") {
            SettingFile f;
            std::string label;
            need(bool(l >> label >> f.day >> f.ref_final));
            f.car = unesc(label);
            s.setting_files.push_back(std::move(f));
        } else if (key == "sparts") {
            need(!s.setting_files.empty());
            for (u64& p : s.setting_files.back().parts) {
                std::string h;
                need(bool(l >> h));
                p = std::stoull(h, nullptr, 16);
            }
        } else if (key == "sinfo") {
            need(!s.setting_files.empty());
            SettingFile& f = s.setting_files.back();
            std::string c;
            need(bool(l >> f.time >> f.power_ps >> c));
            f.comment = unesc(c);
        } else if (key == "ssettings") {
            need(!s.setting_files.empty() && read_settings(l, s.setting_files.back().settings));
        } else if (key == "mtest") {
            std::string which, label;
            double v;
            need(bool(l >> which >> v >> label));
            if (which == "400") s.machine.t400 = v, s.machine.car400 = unesc(label);
            else if (which == "1000") s.machine.t1000 = v, s.machine.car1000 = unesc(label);
            else if (which == "max") s.machine.top_speed = v, s.machine.car_top = unesc(label);
        } else if (key == "rank") {
            std::string k, label;
            RankEntry r;
            need(bool(l >> k >> r.value >> label >> r.day));
            r.car = unesc(label);
            if (std::string name; l >> name) r.name = unesc(name);
            if (u32 stamp = 0; l >> stamp) r.stamp = stamp; // (saves before the stamps: none)
            if (std::string code; l >> code) r.code = unesc(code);
            // A seeded time-trial record (no car) keeps the original's name mark. Older
            // saves named them "-" (or nothing), or 0xFF / 0xFE / 0xFD in every challenge:
            // those get the challenge's marks, by their order.
            const std::string board = unesc(k);
            std::vector<RankEntry>& rk = s.rankings[board];
            if (const int ch = board.rfind("at:", 0) == 0 ? GtMode::time_trial_index(board.substr(3)) : -1;
                ch >= 0 && r.car.empty() && (r.name.empty() || r.name == "-" || GtMode::is_record_mark(r.name))) {
                const int seeded = int(std::count_if(rk.begin(), rk.end(), [](const RankEntry& e) { return e.car.empty(); }));
                const int mark = GtMode::is_record_mark(r.name) ? 0xFF - u8(r.name[0]) - 3 * ch : -1;
                if (mark < 0 || mark > 2) r.name = GtMode::time_trial_seed_name(ch, std::min(seeded, 2));
            }
            rk.push_back(std::move(r));
        } else if (key == "ps2opt") {
            std::string h;
            need(bool(l >> h) && h.size() % 2 == 0);
            s.ps2_options.clear();
            for (size_t i = 0; i < h.size(); i += 2) s.ps2_options.push_back(u8(std::stoi(h.substr(i, 2), nullptr, 16)));
        } else if (key == "fcomment") {
            std::string n;
            need(bool(l >> n));
            s.file_comment = unesc(n);
        } else if (key == "player") {
            std::string n;
            need(bool(l >> n));
            s.player_name = unesc(n);
        } else if (key == "trophyflags") {
            need(bool(l >> s.trophy_flags));
        } else if (key == "arcade") {
            u32 flags = 0;
            need(bool(l >> s.arcade.course_level >> flags >> s.arcade.awards));
            for (int k = 0; k < kArcadeCarLevels; ++k) s.arcade.car_flags[size_t(k)] = (flags >> k) & 1;
        } else if (key == "abest" || key == "atrank") {
            size_t i = 0;
            int v = 0;
            need(bool(l >> i >> v));
            if (key == "abest" && i < s.arcade.best.size()) s.arcade.best[i] = v;
            if (key == "atrank" && i < s.arcade.at_best_rank.size()) s.arcade.at_best_rank[i] = v;
        } else if (key == "endings") {
            int a, b;
            need(bool(l >> a >> b));
            s.ending_short_seen = a != 0, s.ending_long_seen = b != 0;
        } else if (key == "wheel") {
            std::string w;
            need(bool(l >> w));
            s.wheels.push_back(unesc(w));
        } else if (key == "license") {
            int lic, test, medal;
            double t;
            need(bool(l >> lic >> test >> medal >> t) && lic >= 0 && lic < kLicenseCount && test >= 0 &&
                 test < kTestsPerLicense && medal >= -1 && medal <= 2);
            s.license_tests[size_t(lic * 8 + test)] = {Medal(medal), t};
        } else if (key == "race") {
            std::string c;
            int p;
            need(bool(l >> c >> p));
            s.race_best[unesc(c)] = p;
        } else if (key == "prize") {
            std::string p;
            need(bool(l >> p));
            s.prizes_given.insert(unesc(p));
        } else if (key == "champ") {
            Championship c;
            std::string series;
            size_t n = 0, m = 0;
            need(bool(l >> series >> c.next >> n) && n < 64);
            c.series = unesc(series);
            for (size_t i = 0; i < n; ++i) {
                std::string r;
                need(bool(l >> r));
                c.races.push_back(unesc(r));
            }
            need(bool(l >> m) && m < 64);
            for (size_t i = 0; i < m; ++i) {
                std::string e;
                need(bool(l >> e));
                c.entrants.push_back(unesc(e));
            }
            c.points.assign(m + 1, 0);
            for (int& p : c.points) need(bool(l >> p));
            s.championship = std::move(c);
        } else if (key == "end") {
            ended = true;
            break;
        }
        // Unknown keys: ignored (written by a newer version).
    }
    if (!ended) throw FormatError("gtmode save: truncated");
    if (s.ride >= int(s.garage.size())) s.ride = -1;
    return s;
}

std::string user_data_dir() {
    std::string dir;
    // GT3R_DATA_DIR=<dir> (tests, videos): a separate profile, the player's own left alone.
    if (const char* d = SDL_getenv("GT3R_DATA_DIR"); d && *d) {
        dir = d;
        if (dir.back() != '/' && dir.back() != '\\') dir += '/';
        return dir;
    }
#if defined(SDL_PLATFORM_IOS)
    // iOS: the app's Documents, which the Files app and Finder show as GT3 Revisited
    // (UIFileSharingEnabled): mods and the disc image go in, screenshots and saves come out, as
    // with Android's documents provider. SDL_GetPrefPath's Library folder is hidden from both.
    if (const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)) return docs;
#endif
    const char* base = SDL_GetBasePath();
    if (base && host_path_exists(std::string(base) + "portable.ini")) {
        dir = base;
    } else if (char* pref = SDL_GetPrefPath("GT3Revisited", "GT3Revisited")) {
        dir = pref;
        SDL_free(pref);
    }
    return dir;
}

bool write_save(const std::string& path, const GameState& s, std::string* error) {
    // No path (a session whose library went away): nothing written, not even ".tmp" in the
    // working directory.
    if (path.empty()) {
        if (error) *error = "no save file";
        return false;
    }
    std::string text = save_to_text(s);
    std::string tmp = path + ".tmp";
    if (!write_host_file(tmp, ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()))) {
        if (error) *error = "cannot write " + tmp;
        return false;
    }
    // SDL_RenamePath takes UTF-8 on every platform (std::filesystem would use
    // the ANSI code page on Windows) and replaces an existing file.
    if (!SDL_RenamePath(tmp.c_str(), path.c_str())) {
        if (error) *error = "cannot rename to " + path + ": " + SDL_GetError();
        return false;
    }
    return true;
}

std::optional<GameState> read_save(const std::string& path, std::string* error) {
    std::optional<Bytes> b = read_host_file(path);
    if (!b) {
        if (error) *error = "no save at " + path;
        return std::nullopt;
    }
    try {
        return load_from_text(std::string(b->begin(), b->end()));
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

std::string set_aside_save(const std::string& path, std::string* error) {
    // When it happened (local time) in the name; a counter for the same second.
    const std::time_t now = std::time(nullptr);
    char stamp[32] = "";
    if (const std::tm* tm = std::localtime(&now)) std::strftime(stamp, sizeof stamp, "-%Y%m%d-%H%M%S", tm);
    const std::string base = path + ".unreadable" + stamp;
    for (int i = 1; i < 1000; ++i) {
        const std::string to = i == 1 ? base : base + "-" + std::to_string(i);
        if (host_path_exists(to)) continue;
        // A file that cannot be renamed (locked, immutable) is copied: its bytes are kept
        // either way.
        if (SDL_RenamePath(path.c_str(), to.c_str()) || SDL_CopyFile(path.c_str(), to.c_str())) return to;
        if (error) *error = "cannot move " + path + " to " + to + ": " + SDL_GetError();
        return {};
    }
    if (error) *error = "no free name for " + base;
    return {};
}

// --- PS2 import / export ------------------------------------------------------------

// Car setup record (0x1D0) <-> car label, parts, settings.
void decode_car_equipments(ByteSpan r, const data::CarDatabase& db, std::string& car, std::array<u64, 27>& parts,
               sim::CarSettings& st, int& ref_final) {
    car = label_of(db, data::TableId::Car, rd64(r, 0));
    for (int k = 0; k < 26; ++k) {
        int slot = part_slot(kEqSlotTables[k]);
        if (slot >= 0) parts[size_t(slot)] = rd64(r, 0x18 + 8 * size_t(k));
    }
    std::vector<int*> f = settings_fields(st);
    for (size_t k = 0; k < f.size(); ++k) *f[k] = s32(rd32(r, kEqSettingOffsets[k]));
    ref_final = s32(rd32(r, 0x1AC));
}

void encode_eq(Bytes& p, size_t o, const std::string& car, const std::array<u64, 27>& parts,
               const sim::CarSettings& settings, int ref_final, const data::CarDatabase& db) {
    wr64(p, o, db::id_hash(car));
    if (const data::Car* c = db.car(car); c && c->bytes.size() > 0xF1) wr16(p, o + 0x10, c->bytes[0xF1]);
    for (int k = 0; k < 26; ++k) {
        int slot = part_slot(kEqSlotTables[k]);
        if (slot >= 0) wr64(p, o + 0x18 + 8 * size_t(k), parts[size_t(slot)]);
    }
    sim::CarSettings st = settings;
    std::vector<int*> f = settings_fields(st);
    for (size_t k = 0; k < f.size(); ++k) wr32(p, o + kEqSettingOffsets[k], u32(*f[k]));
    wr32(p, o + 0x1AC, u32(ref_final));
}

// Serialize container (header 0x40, inverted payload, CRC-32).
ByteSpan open_serialized(ByteSpan file, u16 major, Bytes& payload) {
    if (file.size() < kHeader) throw FormatError("save file: too short");
    u32 hsize = rd32(file, 0), psize = rd32(file, 4);
    if (rd16(file, 0x0A) != major) throw FormatError("save file: unsupported version " + std::to_string(rd16(file, 0x0A)));
    if (size_t(hsize) + psize > file.size()) throw FormatError("save file: truncated");
    ByteSpan stored = file.subspan(hsize, psize);
    if (crc32(stored) != rd32(file, 0x0C)) throw FormatError("save file: CRC mismatch");
    payload.assign(stored.begin(), stored.end());
    for (u8& b : payload) b = u8(~b);
    return ByteSpan(payload);
}

Bytes close_serialized(Bytes p, u16 major, u16 minor) {
    for (u8& b : p) b = u8(~b);
    Bytes f((kHeader + p.size() + 0x3F) & ~size_t(0x3F), 0);
    wr32(f, 0, u32(kHeader));
    wr32(f, 4, u32(p.size()));
    wr16(f, 8, minor);
    wr16(f, 0x0A, major);
    wr32(f, 0x0C, crc32(p));
    std::memcpy(&f[0x10], "Base", 4);
    std::memcpy(&f[kHeader], p.data(), p.size());
    return f;
}

SettingFile import_ps2_setting(ByteSpan file, const GtData& data) {
    Bytes buf;
    ByteSpan d = open_serialized(file, 1, buf);
    if (d.size() < 0x1D0) throw FormatError("setting file: payload too small");
    SettingFile f;
    decode_car_equipments(d, data.db, f.car, f.parts, f.settings, f.ref_final);
    return f;
}

Bytes export_ps2_setting(const SettingFile& f, const GtData& data) {
    Bytes p(0x1D0, 0);
    encode_eq(p, 0, f.car, f.parts, f.settings, f.ref_final, data.db);
    return close_serialized(std::move(p), 1, data.region == Region::Japan ? 0x826 : 0x827); // NTSC-J: 1.0x826
}

GameState import_ps2_save(ByteSpan file, const GtData& data) {
    if (file.size() < kHeader) throw FormatError("GAMEDATA: too short");
    u32 hsize = rd32(file, 0), psize = rd32(file, 4);
    u16 major = rd16(file, 0x0A);
    if (major != kMajor) throw FormatError("GAMEDATA: unsupported version " + std::to_string(major));
    if (size_t(hsize) + psize > file.size()) throw FormatError("GAMEDATA: truncated");
    ByteSpan stored = file.subspan(hsize, psize);
    if (crc32(stored) != rd32(file, 0x0C)) throw FormatError("GAMEDATA: CRC mismatch");
    Bytes p(stored.begin(), stored.end());
    for (u8& b : p) b = u8(~b);
    ByteSpan d(p);
    const data::CarDatabase& db = data.db;

    GameState s;
    s.day = rd32(d, 0x00);
    s.races = rd32(d, 0x04);
    s.wins = rd32(d, 0x0C);
    s.credits = s64(rd64(d, 0x10));
    s.prize_money = s64(rd64(d, 0x18));
    s.distance_m = double(rd32(d, 0x20)) * 2;
    s.next_serial = rd32(d, 0x24);
    s.ride = s32(rd32(d, 0x28));
    u32 ncars = rd32(d, 0x30), nwheels = rd32(d, 0x34);
    s.trophies = rd32(d, 0xB0);
    s.ending_short_seen = rd32(d, 0xB4) != 0; // the endings already played (gt_mode §10)
    s.ending_long_seen = rd32(d, 0xB8) != 0;
    s.prize_cars = rd32(d, 0xBC);
    s.rng = rd32(d, 0xCC);
    s.player_name = fixed_str(d, 0x70, kNameField); // the last name entered (the name entry's start)
    // Arcade progression in the scalar block (block +0x3C..).
    s.arcade.course_level = std::clamp(int(rd32(d, 0x3C)), 0, 6);
    for (int k = 0; k < kArcadeCarLevels; ++k) s.arcade.car_flags[size_t(k)] = rd32(d, 0x40 + 4 * size_t(k)) != 0;
    s.arcade.awards = rd32(d, 0xC4);
    s.trophy_flags = rd32(d, 0xC0) & 0xFFFFu; // 1 << license card, 0x40 << trophy index
    if (ncars > u32(kMaxGarageCars) || nwheels > u32(kMaxWheels)) throw FormatError("GAMEDATA: bad counts");
    size_t need = kBlock + ncars * kCarRec + nwheels * kWheelRec + kLicenses + kRaceBest + kCourseBest + kArcade +
                  kAt + kTimeTrial + kMachine + kPrizeFlags + kChamp + kOptions;
    if (d.size() < need) throw FormatError("GAMEDATA: payload too small");

    size_t o = kBlock;
    for (u32 i = 0; i < ncars; ++i, o += kCarRec) {
        ByteSpan r = d.subspan(o, kCarRec);
        GarageCar c;
        c.car = label_of(db, data::TableId::Car, rd64(r, 0));
        c.color = s32(rd32(r, 0x0C));
        for (int k = 0; k < 26; ++k) {
            int slot = part_slot(kEqSlotTables[k]);
            if (slot >= 0) c.parts[size_t(slot)] = rd64(r, 0x18 + 8 * size_t(k));
        }
        c.wheel = rd64(r, 0xE8) ? label_of(db, data::TableId::Wheel, rd64(r, 0xE8)) : std::string();
        std::vector<int*> f = settings_fields(c.settings);
        for (size_t k = 0; k < f.size(); ++k) *f[k] = s32(rd32(r, kEqSettingOffsets[k]));
        c.ref_final = s32(rd32(r, 0x1AC));
        for (int b = 0; b < 87; ++b)
            if (r[0x1D0 + size_t(b >> 3)] & (1 << (b & 7))) c.owned.set(size_t(b));
        c.cond.odometer_m = double(rd32(r, 0x1D0 + 0x0C)) * 2;
        c.cond.engine_wear_m = rdf(r, 0x1D0 + 0x10);
        c.cond.oil_m = rdf(r, 0x1D0 + 0x14);
        c.cond.dirt = rdf(r, 0x1D0 + 0x18);
        c.serial = rd32(r, 0x200);
        s.garage.push_back(std::move(c));
    }
    for (u32 i = 0; i < nwheels; ++i, o += kWheelRec) {
        ByteSpan r = d.subspan(o, kWheelRec);
        std::string name;
        for (size_t k = 4; k < kWheelRec && r[k]; ++k) name.push_back(char(r[k]));
        if (!name.empty()) s.wheels.push_back(name); // entry 0: the stock-wheel placeholder
    }
    for (int i = 0; i < kLicenseCount * kTestsPerLicense; ++i) {
        ByteSpan r = d.subspan(o + size_t(i) * 0x154, 0x154);
        s32 m = s32(rd32(r, 0));
        LicenseTestRecord& t = s.license_tests[size_t(i)];
        t.medal = m >= 0 && m <= 2 ? Medal(m) : Medal::None;
        u32 best = rd32(r, 0x64);
        t.best_time = best != kEmptyTime && best != 0 ? best / 1000.0 : -1;
    }
    o += kLicenses;
    for (size_t i = 0; i < data.race_codes.size() && i < kRaceBest / 4; ++i) {
        s32 v = s32(rd32(d, o + 4 * i));
        if (v >= 0 && v < kNoPosition) s.race_best[data.race_codes[i]] = v;
    }
    o += kRaceBest + kCourseBest;
    // Arcade Single Race results (difficulty x the SINGLE_RACE list's size + course, best place
    // at +0x10)
    // and the time-trial best ranks (+0x2A0 of each 0x2A4 record).
    for (size_t i = 0; i < s.arcade.best.size(); ++i) {
        s32 v = s32(rd32(d, o + 0x14 * i + 0x10));
        s.arcade.best[i] = v >= 0 && v < kArcadeNoPosition ? v : kArcadeNoPosition;
    }
    o += kArcade;
    for (size_t i = 0; i < size_t(kTimeTrials); ++i) {
        const size_t r = o + 0x2A4 * i;
        s32 v = s32(rd32(d, r + 0x2A0));
        s.arcade.at_best_rank[i] = v >= 0 && v < kArcadeNoPosition ? v : kArcadeNoPosition;
        std::vector<RankEntry> rank;
        for (size_t k = 0; k < 12; ++k) {
            u32 ms = rd32(d, r + 4 * k);
            if (ms == kEmptyTime || ms == 0) break;
            RankEntry e;
            e.value = ms / 1000.0;
            e.stamp = rd32(d, r + 0xF0 + 4 * k);
            std::string name = fixed_str(d, r + 0x30 + 16 * k, 16);
            e.code = read_code(d, r + 0x120 + 32 * k);
            if (u8(d[r + 0x30 + 16 * k]) >= 0x80) {
                e.name = name.substr(0, 1); // a built-in record: its name mark, no car
            } else {
                // (The record keeps no car: the challenge's, as GtMode::arcade_time_trial enters it.)
                e.name = name;
                e.car = challenge_car(data, i);
            }
            rank.push_back(std::move(e));
        }
        if (!rank.empty()) s.rankings["at:" + at_code(i)] = std::move(rank);
    }
    o += kAt;
    // Course time trials (the TIME_ATTACK courses' slots) and machine tests.
    if (auto ta = data.arcade_courses.find("TIME_ATTACK"); ta != data.arcade_courses.end())
        for (size_t i = 0; i < ta->second.size() && i < size_t(kCourseTimeTrials); ++i)
            if (std::vector<RankEntry> rank = read_ranking(d, o + kRanking * i, false, db); !rank.empty())
                s.rankings["ct:" + ta->second[i].course] = std::move(rank);
    for (size_t t = 0; t < 3; ++t) {
        const bool speed = t == 2;
        std::vector<RankEntry> rank = read_ranking(d, o + kRanking * (size_t(kCourseTimeTrials) + t), speed, db);
        if (rank.empty()) continue;
        // The best of each test (GameState::machine) is the ranking's first entry.
        MachineRecords& m = s.machine;
        if (t == 0) m.t400 = rank[0].value, m.car400 = rank[0].car;
        if (t == 1) m.t1000 = rank[0].value, m.car1000 = rank[0].car;
        if (t == 2) m.top_speed = rank[0].value, m.car_top = rank[0].car;
        s.rankings[std::string("mt:") + kMachineTests[t]] = std::move(rank);
    }
    o += kTimeTrial + kMachine;
    if (const Gcf::Section* sec = data.gcf.section(15))
        for (size_t i = 0; i < sec->entries.size() && i < kPrizeFlags; ++i)
            if (d[o + i]) s.prizes_given.insert(sec->entries[i].name);
    // The suspended championship (o + kPrizeFlags) is not imported: it restarts.
    o += kPrizeFlags + kChamp;
    s.ps2_options.assign(d.begin() + std::ptrdiff_t(o), d.begin() + std::ptrdiff_t(o + kOptions));
    if (s.ride >= int(s.garage.size())) s.ride = -1;
    return s;
}

Bytes export_ps2_save(const GameState& s, const GtData& data) {
    const data::CarDatabase& db = data.db;
    size_t nwheels = s.wheels.size() + 1; // + the stock-wheel placeholder
    size_t psize = kBlock + s.garage.size() * kCarRec + nwheels * kWheelRec + kLicenses + kRaceBest + kCourseBest +
                   kArcade + kAt + kTimeTrial + kMachine + kPrizeFlags + kChamp + kOptions;
    Bytes p(psize, 0);
    wr32(p, 0x00, s.day);
    wr32(p, 0x04, s.races);
    wr32(p, 0x0C, s.wins);
    wr64(p, 0x10, u64(s.credits));
    wr64(p, 0x18, u64(s.prize_money));
    wr32(p, 0x20, u32(s.distance_m / 2));
    wr32(p, 0x24, s.next_serial);
    wr32(p, 0x28, u32(s.ride));
    wr32(p, 0x2C, u32(s.ride)); // M: the original writes the ride index here too (-1 in a new game)
    wr32(p, 0x30, u32(s.garage.size()));
    wr32(p, 0x34, u32(nwheels));
    wr32(p, 0xB0, s.trophies);
    wr32(p, 0xB4, s.ending_short_seen ? 1 : 0);
    wr32(p, 0xB8, s.ending_long_seen ? 1 : 0);
    wr32(p, 0xBC, s.prize_cars);
    // The language the game was saved in (NTSC-J never writes it: 0).
    wr32(p, 0xC8, data.region == Region::Japan ? 0 : u32(data.language));
    wr32(p, 0xCC, s.rng);
    std::memcpy(&p[0x70], s.player_name.data(), std::min(s.player_name.size(), kNameField - 1));
    wr32(p, 0x3C, u32(s.arcade.course_level));
    for (int k = 0; k < kArcadeCarLevels; ++k) wr32(p, 0x40 + 4 * size_t(k), s.arcade.car_flags[size_t(k)] ? 1 : 0);
    wr32(p, 0xC4, s.arcade.awards);
    wr32(p, 0xC0, s.trophy_flags);
    size_t o = kBlock;
    for (const GarageCar& c : s.garage) {
        wr64(p, o, db::id_hash(c.car));
        wr32(p, o + 0x0C, u32(c.color));
        if (const data::Car* car = db.car(c.car); car && car->bytes.size() > 0xF1) wr16(p, o + 0x10, car->bytes[0xF1]);
        for (int k = 0; k < 26; ++k) {
            int slot = part_slot(kEqSlotTables[k]);
            if (slot >= 0) wr64(p, o + 0x18 + 8 * size_t(k), c.parts[size_t(slot)]);
        }
        wr64(p, o + 0xE8, c.wheel.empty() ? 0 : db::id_hash(c.wheel));
        sim::CarSettings st = c.settings;
        std::vector<int*> f = settings_fields(st);
        for (size_t k = 0; k < f.size(); ++k) wr32(p, o + kEqSettingOffsets[k], u32(*f[k]));
        wr32(p, o + 0x1AC, u32(c.ref_final));
        for (int b = 0; b < 87; ++b)
            if (c.owned.test(size_t(b))) p[o + 0x1D0 + size_t(b >> 3)] |= u8(1 << (b & 7));
        wr32(p, o + 0x1D0 + 0x0C, u32(c.cond.odometer_m / 2));
        wrf(p, o + 0x1D0 + 0x10, c.cond.engine_wear_m);
        wrf(p, o + 0x1D0 + 0x14, c.cond.oil_m);
        wrf(p, o + 0x1D0 + 0x18, c.cond.dirt);
        wr32(p, o + 0x1FC, 1);
        wr32(p, o + 0x200, c.serial);
        o += kCarRec;
    }
    for (size_t i = 0; i < nwheels; ++i, o += kWheelRec) {
        wr32(p, o, 1);
        if (i > 0) {
            const std::string& w = s.wheels[i - 1];
            std::memcpy(&p[o + 4], w.data(), std::min<size_t>(w.size(), 0x3F));
        }
    }
    for (int i = 0; i < kLicenseCount * kTestsPerLicense; ++i) {
        size_t r = o + size_t(i) * 0x154;
        const LicenseTestRecord& t = s.license_tests[size_t(i)];
        wr32(p, r, u32(int(t.medal)));
        for (int k = 0; k < 6; ++k) wr32(p, r + 0x64 + 4 * size_t(k), kEmptyTime);
        if (t.best_time > 0) wr32(p, r + 0x64, u32(std::llround(t.best_time * 1000)));
        for (int k = 0; k < 6; ++k) std::memset(&p[r + 0x94 + 32 * size_t(k)], '-', 30);
    }
    o += kLicenses;
    for (size_t i = 0; i < kRaceBest / 4; ++i) {
        int v = i < data.race_codes.size() ? s.best_position(data.race_codes[i]) : kNoPosition;
        wr32(p, o + 4 * i, u32(v));
    }
    o += kRaceBest;
    for (size_t i = 0; i < kCourseBest / 4; ++i) wr32(p, o + 4 * i, kEmptyTime);
    o += kCourseBest;
    for (size_t i = 0; i < kArcade / 0x14; ++i) {
        for (int k = 0; k < 4; ++k) wr32(p, o + 0x14 * i + 4 * size_t(k), kEmptyTime);
        wr32(p, o + 0x14 * i + 0x10, i < s.arcade.best.size() ? u32(s.arcade.best[i]) : 8u);
    }
    o += kArcade;
    for (size_t i = 0; i < size_t(kTimeTrials); ++i) {
        const size_t r = o + 0x2A4 * i;
        std::vector<RankEntry> rank;
        if (auto it = s.rankings.find("at:" + at_code(i)); it != s.rankings.end()) rank = it->second;
        else rank = GtMode::time_trial_seed_ranking(db, at_code(i));
        for (size_t k = 0; k < 12; ++k) {
            if (k >= rank.size()) {
                wr32(p, r + 4 * k, kEmptyTime);
                put_dashes(p, r + 0x120 + 32 * k);
                continue;
            }
            const RankEntry& e = rank[k];
            wr32(p, r + 4 * k, u32(std::llround(e.value * 1000)));
            wr32(p, r + 0xF0 + 4 * k, e.stamp);
            put_code(p, r + 0x120 + 32 * k, e.code);
            if (e.car.empty() && GtMode::is_record_mark(e.name)) {
                p[r + 0x30 + 16 * k] = u8(e.name[0]); // the stored mark, as the original wrote it
                if (!e.stamp) wr32(p, r + 0xF0 + 4 * k, kBuiltinStamp);
            } else {
                std::memcpy(&p[r + 0x30 + 16 * k], e.name.data(), std::min<size_t>(e.name.size(), 15));
            }
        }
        wr32(p, r + 0x2A0, u32(s.arcade.at_best_rank[i]));
    }
    o += kAt;
    // Course time-trial rankings (slot = the course's place in the TIME_ATTACK list; the slots
    // past it stay empty), then the machine tests'.
    {
        auto ranking = [&](const std::string& key) -> const std::vector<RankEntry>* {
            auto it = s.rankings.find(key);
            return it != s.rankings.end() && !it->second.empty() ? &it->second : nullptr;
        };
        const auto ta = data.arcade_courses.find("TIME_ATTACK");
        for (size_t i = 0; i < size_t(kCourseTimeTrials); ++i) {
            const bool listed = ta != data.arcade_courses.end() && i < ta->second.size();
            write_ranking(p, o + kRanking * i, listed ? ranking("ct:" + ta->second[i].course) : nullptr, false);
        }
        for (size_t t = 0; t < 3; ++t) {
            const size_t r = o + kRanking * (size_t(kCourseTimeTrials) + t);
            write_ranking(p, r, ranking(std::string("mt:") + kMachineTests[t]), t == 2);
        }
    }
    o += kTimeTrial + kMachine;
    if (const Gcf::Section* sec = data.gcf.section(15))
        for (size_t i = 0; i < sec->entries.size() && i < kPrizeFlags; ++i)
            p[o + i] = s.prizes_given.count(sec->entries[i].name) ? 1 : 0;
    o += kPrizeFlags;
    // Championship (inactive): a new game has 0xFF in 8 bytes of each of the 8 entrants
    // (0x1D4 apart, M: an empty part slot).
    for (size_t k = 0; k < 8; ++k) std::memset(&p[o + 0x94 + 0x1D4 * k], 0xFF, 8);
    o += kChamp;
    // Options: those of the save this one was imported from, else a new game's.
    {
        Bytes opt = s.ps2_options.size() == kOptions ? s.ps2_options : default_ps2_options(data);
        std::memcpy(&p[o], opt.data(), kOptions);
    }
    for (u8& b : p) b = u8(~b);
    size_t total = (kHeader + psize + 0x3F) & ~size_t(0x3F);
    Bytes f(total, 0);
    wr32(f, 0, u32(kHeader));
    wr32(f, 4, u32(psize));
    wr16(f, 8, gamedata_minor(data.region));
    wr16(f, 0x0A, kMajor);
    wr32(f, 0x0C, crc32(p));
    std::memcpy(&f[0x10], "Base", 4);
    std::memcpy(&f[kHeader], p.data(), psize);
    return f;
}

Ps2ReplayPad ps2_replay_pad(ByteSpan o) {
    Ps2ReplayPad p;
    if (o.size() < kOptions) return p;
    p.level = rdf(o, 0x114);
    p.music_cuts = rdf(o, 0x118);
    return p;
}

void set_ps2_replay_pad(Bytes& o, const Ps2ReplayPad& p) {
    if (o.size() < kOptions) o.resize(kOptions, 0);
    wrf(o, 0x114, p.level);
    wrf(o, 0x118, p.music_cuts);
}

Bytes ps2_thumbnail(const GameState& s, std::shared_ptr<const GtData> data) {
    GtMode view(std::move(data));
    view.state() = s;
    Bytes t(0x60, 0);
    wr64(t, 0x08, u64(s.credits));
    wr32(t, 0x10, u32(s.garage.size()));
    wr32(t, 0x14, u32(view.highest_license())); // -1 = none
    wr32(t, 0x18, s.day);
    wrf(t, 0x1C, view.completion_percent()); // per cent (2.0992 shown as "2.0%")
    std::memcpy(&t[0x28], s.file_comment.data(), std::min<size_t>(s.file_comment.size(), 15));
    return t;
}

std::vector<Ps2SaveFile> export_ps2_save_files(const GameState& s, std::shared_ptr<const GtData> data,
                                               const GtData::Reader& read) {
    std::optional<Bytes> sys = read("menu/icon/icon.sys"), icon = read("menu/icon/ICON_a_spec.ico");
    if (!sys || sys->size() != 964 || std::memcmp(sys->data(), "PS2D", 4) != 0 || !icon)
        throw FormatError("disc: menu/icon/icon.sys or ICON_a_spec.ico missing");
    // icon.sys as the game fills it: the title "GT3 GAME DATA" (full-width Shift-JIS, one
    // line: the line break offset at +6 is its length) and the icon file names.
    static const u8 kTitle[] = {0x82, 0x66, 0x82, 0x73, 0x82, 0x52, 0x81, 0x40, 0x82, 0x66, 0x82, 0x60, 0x82,
                                0x6C, 0x82, 0x64, 0x81, 0x40, 0x82, 0x63, 0x82, 0x60, 0x82, 0x73, 0x82, 0x60};
    Bytes& y = *sys;
    wr16(y, 6, u16(sizeof kTitle));
    std::memset(&y[0xC0], 0, 0x44);
    std::memcpy(&y[0xC0], kTitle, sizeof kTitle);
    for (size_t o : {0x104, 0x144, 0x184}) {
        std::memset(&y[o], 0, 0x40);
        std::memcpy(&y[o], "icon0.ico", 9);
    }
    std::vector<Ps2SaveFile> files;
    files.push_back({"icon0.ico", std::move(*icon)});
    files.push_back({"icon.sys", std::move(y)});
    files.push_back({ps2_game_dir(data->region), export_ps2_save(s, *data)});
    files.push_back({"thumbnail", ps2_thumbnail(s, data)});
    return files;
}

namespace {

bool ends_with_ci(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(u8(s[s.size() - n + i])) != suffix[i]) return false;
    return true;
}

const ps2::McFile* find_file(const ps2::McDir& d, const std::string& name) {
    for (const ps2::McFile& f : d.files)
        if (f.name == name) return &f;
    return nullptr;
}

ByteSpan game_file_of(const std::vector<ps2::McDir>& dirs, const char* name) {
    for (const ps2::McDir& d : dirs)
        if (d.name == name)
            if (const ps2::McFile* f = find_file(d, name)) return f->data;
    throw FormatError("no GT3 game save (" + std::string(name) + ") found");
}

} // namespace

const char* ps2_game_dir(Region r) {
    switch (r) {
    case Region::Japan: return "BISCPS-15009GAMEDATA";
    case Region::Europe: return "BESCES-50294GAMEDATA";
    case Region::America: break;
    }
    return kPs2GameDir;
}

GameState import_ps2_save_path(const std::string& path, const GtData& data) {
    // The disc's release reads its own save only (another release's names other cars and,
    // in NTSC-J, sums x100).
    const std::string dir = ps2_game_dir(data.region);
    if (host_is_directory(path)) {
        // The save directory itself, or a folder memory card holding it.
        for (const std::string& p : {path + "/" + dir, path + "/" + dir + "/" + dir})
            if (!host_is_directory(p))
                if (std::optional<Bytes> b = read_host_file(p)) return import_ps2_save(*b, data);
        throw FormatError("no GT3 game save (" + dir + ") in " + path);
    }
    std::optional<Bytes> b = read_host_file(path);
    if (!b) throw FormatError("cannot read " + path);
    if (ps2::is_card_image(*b)) return import_ps2_save(game_file_of(ps2::read_card(*b), dir.c_str()), data);
    // The data file itself, by its header (0x40 bytes, version 7), whatever it is called.
    if (b->size() >= kHeader && rd32(*b, 0) == kHeader && rd16(*b, 0x0A) == kMajor) return import_ps2_save(*b, data);
    // A .psu: by its extension, or by its first entry (the save directory), for paths
    // without one (Android content:// URIs).
    const bool psu_entry = b->size() >= 0x400 && std::memcmp(b->data() + 0x40, dir.c_str(), dir.size() + 1) == 0;
    if (b->size() >= 0x200 && (ends_with_ci(path, ".psu") || psu_entry))
        return import_ps2_save(game_file_of({ps2::read_psu(*b)}, dir.c_str()), data);
    return import_ps2_save(*b, data);
}

namespace {

ps2::McDir ps2_save_dir(const GameState& s, std::shared_ptr<const GtData> data, const GtData::Reader& read) {
    ps2::McDir dir;
    dir.name = ps2_game_dir(data->region);
    for (Ps2SaveFile& f : export_ps2_save_files(s, std::move(data), read))
        dir.files.push_back({f.name, std::move(f.data)});
    return dir;
}

} // namespace

Ps2Export ps2_export_format(const std::string& name) {
    if (ends_with_ci(name, ".ps2") || ends_with_ci(name, ".mcd")) return Ps2Export::Card;
    if (ends_with_ci(name, ".raw")) return Ps2Export::Raw;
    return Ps2Export::Psu;
}

Bytes export_ps2_save_container(const GameState& s, std::shared_ptr<const GtData> data, const GtData::Reader& read,
                                Ps2Export format) {
    if (format == Ps2Export::Raw) return export_ps2_save(s, *data);
    ps2::McDir dir = ps2_save_dir(s, std::move(data), read);
    return format == Ps2Export::Card ? ps2::make_card({dir}) : ps2::write_psu(dir);
}

void export_ps2_save_path(const GameState& s, std::shared_ptr<const GtData> data, const GtData::Reader& read,
                          const std::string& out) {
    auto write = [](const std::string& p, ByteSpan b) {
        if (!write_host_file(p, b)) throw FormatError("cannot write " + p);
    };
    for (const char* ext : {".psu", ".ps2", ".mcd", ".raw"})
        if (ends_with_ci(out, ext))
            return write(out, export_ps2_save_container(s, std::move(data), read, ps2_export_format(out)));
    ps2::McDir dir = ps2_save_dir(s, std::move(data), read);
    std::string folder = out + "/" + dir.name;
    SDL_CreateDirectory(folder.c_str());
    for (const ps2::McFile& f : dir.files) write(folder + "/" + f.name, f.data);
}

} // namespace gt3r::game
