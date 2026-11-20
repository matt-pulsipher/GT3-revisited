// Gran Turismo Concept's memory-card game save (concept §11): import into our game state and export back, the way
// gt_save.cpp does Gran Turismo 3's. The container is GT3's (ps2_save_parts.h) with major version 100; the payload is
// the scalar block, the garage cars, the ten course license records, the single-race results, the free-run rankings,
// the present flags and the options, the same layout in every release's program.

#include "base/game_random.h"
#include "game/concept_game.h"
#include "game/gt_save.h"
#include "game/ps2_save_parts.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt3r::game {

namespace {

using namespace ps2save;

constexpr u16 kConceptMajor = 100;
// The payload's blocks after the cars (the status object's writer, Tokyo-Geneva; the loops' counts and
// strides are the same in the five programs).
constexpr size_t kLicenseTests = 10;
constexpr size_t kResults = 144, kResultRec = 0x14;
constexpr size_t kRankings = 44;
constexpr size_t kPresents = 0x4C;
// The four presents that are not prize keys have fixed flags (0x216F40); a prize key's flag is its place in the game
// config's list.
constexpr size_t kMovie0Flag = 0x46, kMovie1Flag = 0x47, kPodFlag = 0x48, kTransferFlag = 0x49;
// A license record (GT3's, gt_mode §12): s32 medal, six entries' names (16 bytes each), times (ms), stamps and blocks.
constexpr size_t kLicNames = 0x04, kLicTimes = 0x64, kLicStamps = 0x7C, kLicCodes = 0x94, kLicEntries = 6;

size_t payload_size(size_t cars) {
    return kBlock + cars * kCarRec + kLicenseTests * kLicenseRec + kResults * kResultRec + kRankings * kRanking +
           kPresents + kOptions;
}

// A GtMode over `data` for the rules that want one (the garage cars' build): it does not own the data.
GtMode view_of(const GtData& data) { return GtMode(std::shared_ptr<const GtData>(&data, [](const GtData*) {})); }

const std::vector<GtData::ArcadeCourse>* course_list(const GtData& d, const char* name) {
    const auto it = d.arcade_courses.find(name);
    return it == d.arcade_courses.end() ? nullptr : &it->second;
}

std::string license_key(const GtData& d, size_t test) {
    return test < d.concept_lists.license_codes.size() ? "lic:" + d.concept_lists.license_codes[test] : std::string();
}

// A present's flag: nullopt for a name the disc does not know.
std::optional<size_t> present_flag(const GtData& d, const std::string& name) {
    if (name == kConceptMovie0) return kMovie0Flag;
    if (name == kConceptMovie1) return kMovie1Flag;
    if (name == kConceptPodRace) return kPodFlag;
    if (name == kConceptTransfer) return kTransferFlag;
    if (const Gcf::Section* sec = d.gcf.section(15))
        for (size_t i = 0; i < sec->entries.size() && i < kMovie0Flag; ++i)
            if (sec->entries[i].name == name) return i;
    return std::nullopt;
}

// What makes a garage car another one than the entry's new car (the serial is the garage's own count).
bool same_car(const GarageCar& a, const GarageCar& b) {
    const GarageCar &p = a, &q = b;
    return p.car == q.car && p.color == q.color && p.parts == q.parts && p.owned == q.owned && p.ref_final == q.ref_final &&
           p.wheel == q.wheel && std::memcmp(&p.settings, &q.settings, sizeof p.settings) == 0 &&
           p.cond.odometer_m == q.cond.odometer_m && p.cond.engine_wear_m == q.cond.engine_wear_m &&
           p.cond.oil_m == q.cond.oil_m && p.cond.dirt == q.cond.dirt;
}

// A new game's options. The 2001 Tokyo program's are GT3's (so are Tokyo-Seoul v1.02's, the 2001 generation's program with
// v2.00's game config: told apart by its config per language); the later programs start three switches the other way (+0x3C and +0x44
// on, +0x5C off) and fill the four wheel records after +0x7C8. The Tokyo-Geneva programs' compiler rounded its decimal
// constants toward zero: their 0.1 (+0x114) and their integer draw's scale (the music list's first seed) are one step
// lower. Each read from a new game the original saved.
Bytes default_concept_options(const GtData& data) {
    Bytes o = default_options(data);
    const std::string& gcf = data.game_config;
    if (gcf == "gameconf/concept.gcf" || data.game_config_per_language) return o;
    wr32(o, 0x3C, 1);
    wr32(o, 0x44, 1);
    wr32(o, 0x5C, 0);
    static const u32 kWheel[2][8] = {{1, 0, 0x400, 0x200, 0x29, 0xD5, 0x2B, 0xDA}, {1, 0, 0x400, 0x200, 0x25, 0xB4, 0x48, 0xDF}};
    for (size_t r = 0; r < 4; ++r)
        for (size_t k = 0; k < 8; ++k) wr32(o, 0x7C8 + 0x20 * r + 4 * k, kWheel[r / 2][k]);
    if (gcf == "gameconf/concept_eu.gcf" || gcf == "gameconf/concept_tw.gcf") {
        wr32(o, 0x114, 0x3DCCCCCC);
        GameRandom boot;
        put_new_music_list(o, data, u16(int(boot.rand01() * 2147483520.0f)));
    }
    return o;
}

// The language word (+0xC8): the language's number in the program's own order. Tokyo-Seoul's has Korean eighth (7),
// then Traditional Chinese; the Tokyo-Geneva programs have ours. 2001 Tokyo does not write it.
u32 language_word(const GtData& data) {
    if (data.game_config == "gameconf/concept.gcf") return 0;
    if (data.game_config == "gameconf/concept_kr.gcf") {
        if (data.language == Language::KR) return 7;
        if (data.language == Language::TW) return 8;
    }
    return u32(data.language);
}

} // namespace

std::string ps2_save_name(const GtData& data) {
    return data.game == GameKind::Concept ? concept_save_rule(data.game_config).dir : std::string(ps2_game_dir(data.region));
}

int concept_open_car_count(const GtData& d, const ConceptState& state) {
    // The availability table's count (the table is built over the five categories' own lists, whatever the
    // language): the slots that are not locked.
    int n = 0;
    for (const char* cls : {"CLASS_C", "CLASS_B", "CLASS_A", "CLASS_S", "CLASS_DIRT"}) {
        const auto it = d.arcade_classes.find(cls);
        if (it == d.arcade_classes.end()) continue;
        for (size_t i = 0; i < it->second.size(); ++i)
            if (!concept_slot_locked(d.concept_lists, state, cls, int(i))) ++n;
    }
    return n;
}

GameState import_concept_ps2_save(ByteSpan file, const GtData& data) {
    const Bytes p = open_payload(file, kConceptMajor, "GAMEDATA");
    ByteSpan d(p);
    if (d.size() < kBlock) throw FormatError("GAMEDATA: payload too small");
    const u32 ncars = rd32(d, 0x30);
    if (ncars > u32(kMaxGarageCars)) throw FormatError("GAMEDATA: bad counts");
    if (d.size() < payload_size(ncars)) throw FormatError("GAMEDATA: payload too small");
    const data::CarDatabase& db = data.db;
    const GtMode gt = view_of(data);

    GameState s;
    s.day = rd32(d, 0x00);
    s.races = rd32(d, 0x04);
    s.wins = rd32(d, 0x0C);
    s.credits = s64(rd64(d, 0x10));
    s.prize_money = s64(rd64(d, 0x18));
    s.distance_m = double(rd32(d, 0x20)) * 2;
    s.rng = rd32(d, 0xCC);
    s.player_name = fixed_str(d, 0x70, 16);

    // The cars: one per garage entry, in the entries' order. Ours keeps the ones that are not the entry's new car
    // (the others are made again when they are raced, the same car).
    const std::vector<std::string> entries = concept_garage_entries(gt);
    size_t o = kBlock;
    for (u32 i = 0; i < ncars; ++i, o += kCarRec) {
        GarageCar c = read_garage_car(d.subspan(o, kCarRec), db);
        // (This game's records hold the gear ratios as 16-bit words: "no gear" is 0xFC19, not GT3's -999.)
        for (int& g : c.settings.gears) g = s16(g);
        // (A gearbox whose ratios are computed fills its own gears; the game leaves whatever its work area held in
        // the words after them.)
        if (const std::optional<sim::GearGenerator> gg = sim::gear_generator(db, c.parts))
            for (size_t g = size_t(gg->n) + 1; g < c.settings.gears.size(); ++g) c.settings.gears[g] = -999;
        if (i >= entries.size()) continue;
        const std::optional<GarageCar> fresh = concept_new_car(gt, entries[i]);
        if (!fresh || fresh->car != c.car) continue; // (not this disc's car: a save of another release)
        c.origin = fresh->origin;
        if (same_car(c, *fresh)) continue;
        c.serial = s.next_serial++;
        s.garage.insert(s.garage.begin(), std::move(c));
    }
    // The course licenses: the medal, and the six best runs as a ranking.
    for (size_t t = 0; t < kLicenseTests; ++t, o += kLicenseRec) {
        const s32 medal = s32(rd32(d, o));
        std::vector<RankEntry> rank;
        for (size_t k = 0; k < kLicEntries; ++k) {
            const u32 ms = rd32(d, o + kLicTimes + 4 * k);
            if (ms == kEmptyTime || ms == 0) break;
            RankEntry e;
            e.value = ms / 1000.0;
            e.name = fixed_str(d, o + kLicNames + 16 * k, 16);
            e.stamp = rd32(d, o + kLicStamps + 4 * k);
            e.code = read_code(d, o + kLicCodes + 32 * k);
            rank.push_back(std::move(e));
        }
        if (medal >= 0 && medal <= 2) {
            ConceptState::License& l = s.concept_game.licenses[int(t)];
            l.medal = medal;
            l.best_time = rank.empty() ? -1 : rank.front().value;
        }
        if (const std::string key = license_key(data, t); !rank.empty() && !key.empty()) s.rankings[key] = std::move(rank);
    }
    // The single-race results: level x the SINGLE_RACE list's size + course, the best place at +0x10.
    const int courses = data.concept_lists.course_count();
    for (int level = 0; level < kConceptLevels; ++level)
        for (int course = 0; course < courses; ++course) {
            const size_t i = size_t(level * courses + course);
            if (i >= kResults) continue;
            const s32 place = s32(rd32(d, o + kResultRec * i + 0x10));
            if (place >= 0 && place < kConceptNoPlace) s.concept_game.places[{level, course}] = place;
        }
    o += kResults * kResultRec;
    // The free-run rankings, by the TIME_ATTACK list's order.
    if (const auto* ta = course_list(data, "TIME_ATTACK"))
        for (size_t i = 0; i < ta->size() && i < kRankings; ++i)
            if (std::vector<RankEntry> rank = read_ranking(d, o + kRanking * i, false, db); !rank.empty())
                s.rankings["ct:" + (*ta)[i].course] = std::move(rank);
    o += kRankings * kRanking;
    // The present flags.
    for (const char* name : {kConceptMovie0, kConceptMovie1, kConceptPodRace, kConceptTransfer})
        if (d[o + *present_flag(data, name)]) s.concept_game.presents.insert(name);
    if (const Gcf::Section* sec = data.gcf.section(15))
        for (size_t i = 0; i < sec->entries.size() && i < kMovie0Flag; ++i)
            if (d[o + i]) s.concept_game.presents.insert(sec->entries[i].name);
    o += kPresents;
    s.ps2_options.assign(d.begin() + std::ptrdiff_t(o), d.begin() + std::ptrdiff_t(o + kOptions));
    return s;
}

Bytes export_concept_ps2_save(const GameState& s, const GtData& data) {
    const data::CarDatabase& db = data.db;
    const GtMode gt = view_of(data);
    // Every garage entry's car, in the entries' order: ours when the game has one, else the entry's new car.
    std::vector<GarageCar> cars;
    for (const std::string& entry : concept_garage_entries(gt)) {
        const auto own = std::find_if(s.garage.begin(), s.garage.end(), [&](const GarageCar& c) { return c.origin == entry; });
        if (own != s.garage.end()) cars.push_back(*own);
        else if (std::optional<GarageCar> fresh = concept_new_car(gt, entry)) cars.push_back(std::move(*fresh));
    }
    Bytes p(payload_size(cars.size()), 0);
    wr32(p, 0x00, s.day);
    wr32(p, 0x04, s.races);
    wr32(p, 0x0C, s.wins);
    wr64(p, 0x10, u64(s.credits));
    wr64(p, 0x18, u64(s.prize_money));
    wr32(p, 0x20, u32(s.distance_m / 2));
    // The garage's serials count from a new game's first car.
    wr32(p, 0x24, u32(cars.size()));
    wr32(p, 0x28, u32(-1)); // no current car: the menus set one for a race
    wr32(p, 0x2C, u32(-1));
    wr32(p, 0x30, u32(cars.size()));
    wr32(p, 0x34, 1); // GT3's wheel count with its stock-wheel placeholder (the wheels are not saved)
    wr32(p, 0xC8, language_word(data));
    wr32(p, 0xCC, s.rng);
    std::memcpy(&p[0x70], s.player_name.data(), std::min<size_t>(s.player_name.size(), 15));
    size_t o = kBlock;
    for (size_t i = 0; i < cars.size(); ++i, o += kCarRec) {
        GarageCar c = cars[i];
        c.serial = u32(i);
        write_garage_car(p, o, c, db);
        // What this game's record holds besides (the car built from its arcade row, and
        // painted): the paint's color, the row's kind word 3, the gear ratios as 16-bit words and the row's engine
        // output factor (row +0x10A).
        wr32(p, o + 0x08, data.color_id(c.car, c.color));
        wr16(p, o + 0x10, 3);
        for (size_t g = 0; g < 8; ++g) wr32(p, o + 0xF0 + 4 * g, u32(u16(c.settings.gears[g])));
        for (const data::CarVariant& v : db.arcade_cars())
            if (v.label == c.origin && v.bytes.size() >= 0x10C) wr32(p, o + 0x1B8, rd16(v.bytes, 0x10A));
    }
    for (size_t t = 0; t < kLicenseTests; ++t, o += kLicenseRec) {
        wr32(p, o, u32(s.concept_game.medal(int(t))));
        std::vector<RankEntry> rank;
        if (auto it = s.rankings.find(license_key(data, t)); it != s.rankings.end()) rank = it->second;
        // (A record without its ranking, e.g. from a save of ours before the rankings: its best run alone.)
        if (auto it = s.concept_game.licenses.find(int(t)); rank.empty() && it != s.concept_game.licenses.end() && it->second.best_time > 0) {
            RankEntry e;
            e.value = it->second.best_time;
            rank.push_back(std::move(e));
        }
        for (size_t k = 0; k < kLicEntries; ++k) {
            if (k >= rank.size()) {
                wr32(p, o + kLicTimes + 4 * k, kEmptyTime);
                put_dashes(p, o + kLicCodes + 32 * k);
                continue;
            }
            const RankEntry& e = rank[k];
            std::memcpy(&p[o + kLicNames + 16 * k], e.name.data(), std::min<size_t>(e.name.size(), 15));
            wr32(p, o + kLicTimes + 4 * k, u32(std::llround(e.value * 1000)));
            wr32(p, o + kLicStamps + 4 * k, e.stamp);
            put_code(p, o + kLicCodes + 32 * k, e.code);
        }
    }
    const int courses = data.concept_lists.course_count();
    for (size_t i = 0; i < kResults; ++i) {
        for (int k = 0; k < 4; ++k) wr32(p, o + kResultRec * i + 4 * size_t(k), kEmptyTime);
        wr32(p, o + kResultRec * i + 0x10, u32(kConceptNoPlace));
    }
    for (const auto& [at, place] : s.concept_game.places) {
        const size_t i = size_t(at.first * courses + at.second);
        if (at.first >= 0 && at.first < kConceptLevels && at.second >= 0 && at.second < courses && i < kResults)
            wr32(p, o + kResultRec * i + 0x10, u32(place));
    }
    o += kResults * kResultRec;
    {
        const auto* ta = course_list(data, "TIME_ATTACK");
        for (size_t i = 0; i < kRankings; ++i) {
            const std::vector<RankEntry>* rank = nullptr;
            if (ta && i < ta->size())
                if (auto it = s.rankings.find("ct:" + (*ta)[i].course); it != s.rankings.end() && !it->second.empty()) rank = &it->second;
            write_ranking(p, o + kRanking * i, rank, false);
        }
    }
    o += kRankings * kRanking;
    for (const std::string& name : s.concept_game.presents)
        if (const std::optional<size_t> flag = present_flag(data, name)) p[o + *flag] = 1;
    o += kPresents;
    {
        const Bytes opt = s.ps2_options.size() == kOptions ? s.ps2_options : default_concept_options(data);
        std::memcpy(&p[o], opt.data(), kOptions);
    }
    return close_payload(std::move(p), kConceptMajor, concept_save_rule(data.game_config).minor);
}

Bytes concept_ps2_thumbnail(const GameState& s, const GtData& data) {
    // The status object's thumbnail call writes the first word, the credits (zero), the cars that can be
    // chosen, two more zero words and the completion x 100; the rest is not set.
    Bytes t(0x60, 0);
    wr32(t, 0x10, u32(concept_open_car_count(data, s.concept_game)));
    wrf(t, 0x1C, concept_completion(data.concept_lists, s.concept_game) * 100.0f);
    std::memcpy(&t[0x28], s.file_comment.data(), std::min<size_t>(s.file_comment.size(), 15));
    return t;
}

Bytes concept_save_title_sjis(const GtData& data) {
    // The title as icon.sys holds it: Shift-JIS. The game's text is EUC-JP for its two-byte characters (charset.h).
    Bytes out;
    const std::string& t = data.save_title;
    for (size_t i = 0; i < t.size(); ++i) {
        const u8 a = u8(t[i]);
        if (a >= 0xA1 && a <= 0xFE && i + 1 < t.size()) {
            const int row = (a & 0x7F) - 0x21, cell = (u8(t[++i]) & 0x7F) - 0x21;
            const int lead = row / 2 + (row < 62 ? 0x81 : 0xC1);
            const int trail = row % 2 ? cell + 0x9F : cell + 0x40 + (cell >= 63 ? 1 : 0);
            out.push_back(u8(lead));
            out.push_back(u8(trail));
        } else if (a < 0x80) {
            out.push_back(a);
        }
    }
    return out;
}

} // namespace gt3r::game
