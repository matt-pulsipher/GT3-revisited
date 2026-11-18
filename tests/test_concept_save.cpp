// Gran Turismo Concept's memory-card game save (concept §11; game/gt_save.h, game/concept_save.cpp): our
// progress written as the original's save and read back, on every disc of GT3R_DISC_CONCEPT (disc images separated by
// ';'; skipped without it). With GT3R_CONCEPT_PS2_REF naming a folder that holds saves the original games wrote (a
// PCSX2 folder memory card: <save name>/<save name>, icon.sys, icon0.ico, thumbnail), a new game's export is compared
// with the original's new game byte for byte.

#include "test_framework.h"

#include "base/file.h"
#include "concept_discs.h"
#include "disc/disc.h"
#include "formats/ps2/memcard.h"
#include "game/concept_game.h"
#include "game/gt_save.h"
#include "game/ps2_save_parts.h"
#include "vfs/vfs.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>

using namespace gt3r;
using namespace gt3r::game;

namespace {

Bytes payload_of(ByteSpan file) {
    Bytes p(file.begin() + rd32(file, 0), file.begin() + rd32(file, 0) + rd32(file, 4));
    for (u8& b : p) b = u8(~b);
    return p;
}

// The payload's blocks, for the messages of a comparison.
std::string where(size_t at, size_t cars) {
    struct Block {
        const char* name;
        size_t size, count;
    };
    const Block blocks[] = {{"scalar", 0x130, 1}, {"car", 0x204, cars}, {"license", 0x154, 10}, {"result", 0x14, 144},
                            {"ranking", 0x300, 44}, {"presents", 0x4C, 1}, {"options", 0x888, 1}};
    size_t o = 0;
    for (const Block& b : blocks) {
        if (at < o + b.size * b.count) {
            char t[64];
            std::snprintf(t, sizeof t, "%s %zu +0x%zX", b.name, (at - o) / b.size, (at - o) % b.size);
            return t;
        }
        o += b.size * b.count;
    }
    return "past the end";
}

} // namespace

// The releases' save names, versions and icons (the programs' constants).
TEST(concept_save_rules) {
    const ConceptSaveRule eu = concept_save_rule("gameconf/concept_eu.gcf"), tw = concept_save_rule("gameconf/concept_tw.gcf"),
                          kr = concept_save_rule("gameconf/concept_kr.gcf"), jp = concept_save_rule("gameconf/concept.gcf");
    CHECK(eu.dir == "BESCES-50858GAMEDATA" && eu.minor == 2 && eu.icon == "ICON_GTC_concept_GNV.ico");
    CHECK(tw.dir == "BISCPS-55903GAMEDATA" && tw.minor == 5 && tw.icon == eu.icon);
    CHECK(kr.dir == "BKSCPS-56005GAMEDATA" && kr.minor == 3 && kr.icon == "ICON_GTC_concept_KR.ico");
    CHECK(jp.dir == "BISCPS-15010GAMEDATA" && jp.minor == 0 && jp.icon == "ICON_GTC_GT_concept.ico");
}

TEST(concept_disc_ps2_save) {
    const std::vector<std::string> discs = test::concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    const char* ref_dir = std::getenv("GT3R_CONCEPT_PS2_REF");
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> disc = Disc::open(path);
        Vfs vfs(&disc->volume());
        const GtData::Reader read = [&vfs](const std::string& p) { return vfs.read(p); };
        auto data = std::make_shared<GtData>();
        CHECK(data->load(read));
        if (data->game != GameKind::Concept) continue;
        const ConceptSaveRule rule = concept_save_rule(data->game_config);
        std::printf("  %s: %s, version 100.%d\n", disc->info().serial.c_str(), rule.dir.c_str(), int(rule.minor));
        CHECK(ps2_save_name(*data) == rule.dir);
        GtMode gt(data);
        const std::vector<std::string> entries = concept_garage_entries(gt);
        CHECK(!entries.empty() && entries.size() <= size_t(kMaxGarageCars));

        // A new game: the header, the size, every entry's car, no medal, no result, no present.
        const Bytes fresh = export_ps2_save(gt.state(), *data);
        CHECK(rd32(fresh, 0) == 0x40 && rd16(fresh, 8) == rule.minor && rd16(fresh, 0x0A) == 100);
        CHECK(rd32(fresh, 4) == 0xA68C + 0x204 * entries.size() && fresh.size() % 64 == 0);
        CHECK(ps2save::crc32(ByteSpan(fresh).subspan(0x40, rd32(fresh, 4))) == rd32(fresh, 0x0C));
        const Bytes fp = payload_of(fresh);
        CHECK(rd32(fp, 0x30) == entries.size() && s32(rd32(fp, 0x28)) == -1 && rd32(fp, 0x24) == entries.size());
        {
            const GameState back = import_ps2_save(fresh, *data);
            CHECK(back.garage.empty() && back.concept_game.empty() && back.rankings.empty());
            CHECK(back.credits == gt.state().credits && back.day == gt.state().day && back.rng == gt.state().rng);
            // (The options came from the file: the same file again.)
            CHECK(export_ps2_save(back, *data) == fresh);
        }
        try {
            import_ps2_save(Bytes(fresh.begin(), fresh.begin() + 0x100), *data);
            CHECK(false);
        } catch (const FormatError&) {
        }

        // Progress: medals and their runs, places, presents, a free-run record, a car on its Custom set.
        GameState s = gt.state();
        const ConceptRules& rules = data->concept_lists;
        CHECK(rules.license_codes.size() == 10 && rules.course_count() == 10);
        s.concept_game.record_license(0, 0, 87.123);
        s.concept_game.record_license(3, 2, 101.5);
        s.rankings["lic:" + rules.license_codes[0]] = {{87.123, "", 0, "ABC", 0x1234, ""}, {88.0, "", 0, "DEF GHI", 0x1235, "0123456789ABCDEFGHIJKLMNOPQRST"}};
        s.rankings["lic:" + rules.license_codes[3]] = {{101.5, "", 0, "", 7, ""}};
        s.concept_game.record_place(0, 0, 0);
        s.concept_game.record_place(1, 0, 2);
        s.concept_game.record_place(2, 9, 5);
        for (const ConceptRules::Key& k : rules.keys)
            if (k.course == 0) s.concept_game.presents.insert(k.name);
        for (const char* name : {kConceptMovie0, kConceptMovie1, kConceptPodRace, kConceptTransfer}) s.concept_game.presents.insert(name);
        const std::string ta = data->arcade_courses.at("TIME_ATTACK").at(1).course;
        s.rankings["ct:" + ta] = {{95.25, "", 0, "ME", 99, ""}};
        s.player_name = "PLAYER";
        std::string custom;
        for (const std::string& e : entries) {
            std::optional<GarageCar> c = concept_new_car(gt, e);
            if (!c) continue;
            concept_fit_custom(gt, *c);
            if (concept_car_normal(gt, *c)) continue;
            c->settings.final_drive += 100;
            c->color = 1;
            c->serial = s.next_serial++;
            s.garage.insert(s.garage.begin(), *c);
            custom = e;
            break;
        }
        CHECK(!custom.empty());
        const Bytes file = export_ps2_save(s, *data);
        CHECK(file.size() == fresh.size());
        const Bytes pp = payload_of(file);
        const size_t presents = 0x130 + 0x204 * entries.size() + 10 * 0x154 + 144 * 0x14 + 44 * 0x300;
        CHECK(pp[presents + 0x46] == 1 && pp[presents + 0x47] == 1 && pp[presents + 0x48] == 1 && pp[presents + 0x49] == 1);
        const GameState b = import_ps2_save(file, *data);
        CHECK(b.concept_game.licenses.size() == 2 && b.concept_game.medal(0) == 0 && b.concept_game.medal(3) == 2 &&
              b.concept_game.medal(1) == kConceptNoMedal);
        CHECK(std::abs(b.concept_game.licenses.at(0).best_time - 87.123) < 1e-6);
        CHECK(b.concept_game.places == s.concept_game.places && b.concept_game.presents == s.concept_game.presents);
        CHECK(b.rankings.size() == 3 && b.player_name == "PLAYER");
        if (b.rankings.count("lic:" + rules.license_codes[0])) {
            const std::vector<RankEntry>& r = b.rankings.at("lic:" + rules.license_codes[0]);
            CHECK(r.size() == 2 && r[0].name == "ABC" && r[0].stamp == 0x1234 && r[1].name == "DEF GHI" &&
                  r[1].code == "0123456789ABCDEFGHIJKLMNOPQRST" && std::abs(r[1].value - 88.0) < 1e-6);
        }
        if (b.rankings.count("ct:" + ta)) CHECK(b.rankings.at("ct:" + ta).size() == 1 && b.rankings.at("ct:" + ta)[0].name == "ME");
        CHECK(b.garage.size() == 1);
        if (b.garage.size() == 1) {
            const GarageCar& c = b.garage[0];
            CHECK(c.origin == custom && c.color == 1 && c.parts == s.garage[0].parts && c.owned == s.garage[0].owned &&
                  c.settings.final_drive == s.garage[0].settings.final_drive && !concept_car_normal(gt, c));
        }
        CHECK(export_ps2_save(b, *data) == file);
        // Through our own save too.
        CHECK(export_ps2_save(load_from_text(save_to_text(b)), *data) == file);

        // The save's files and the containers.
        const std::vector<Ps2SaveFile> files = export_ps2_save_files(s, data, read);
        CHECK(files.size() == 4 && files[0].name == "icon0.ico" && files[1].name == "icon.sys" && files[2].name == rule.dir &&
              files[3].name == "thumbnail" && files[2].data == file && files[3].data.size() == 0x60);
        if (files.size() == 4) {
            CHECK(files[0].data == *read("menu/icon/" + rule.icon));
            const Bytes& sys = files[1].data;
            CHECK(sys.size() == 964 && rd16(sys, 6) > 0 && rd16(sys, 6) % 2 == 0 && sys[0xC0] >= 0x81 && sys[0xC0 + rd16(sys, 6)] == 0);
            float done = 0;
            std::memcpy(&done, &files[3].data[0x1C], 4);
            CHECK(std::abs(done - concept_completion(rules, s.concept_game) * 100.0f) < 1e-4f && done > 0);
            CHECK(rd32(files[3].data, 0x10) == u32(concept_open_car_count(*data, s.concept_game)));
        }
        const std::filesystem::path dir = test::temp_dir("concept-ps2-save");
        for (const char* name : {"save.psu", "card.ps2", "data.raw", "folder"}) {
            const std::string out = (dir / name).string();
            export_ps2_save_path(s, data, read, out);
            const GameState r = import_ps2_save_path(out, *data);
            CHECK(r.concept_game.places == s.concept_game.places && r.concept_game.presents == s.concept_game.presents);
        }
        CHECK(std::filesystem::exists(dir / "folder" / rule.dir / rule.dir));

        // The original's own new game.
        if (!ref_dir) continue;
        const std::string ref_path = std::string(ref_dir) + "/" + rule.dir;
        const std::optional<Bytes> ref = read_host_file(ref_path + "/" + rule.dir);
        if (!ref) {
            std::printf("    no reference save in %s\n", ref_path.c_str());
            continue;
        }
        const Bytes rp = payload_of(*ref);
        GameState ours = import_ps2_save(*ref, *data);
        CHECK(ours.garage.empty() && ours.concept_game.empty() && ours.rankings.empty());
        // Ours from nothing: only the random state is the session's.
        GameState mine = GtMode(data).state();
        mine.rng = ours.rng;
        const Bytes made = export_ps2_save(mine, *data);
        const Bytes mp = payload_of(made);
        CHECK(mp.size() == rp.size());
        // Byte for byte, but for the gear words after a computed gearbox's own gears: the game leaves what its work area
        // held there (ours writes "no gear").
        auto spare_gear = [&](size_t at) {
            if (at < 0x130 || at >= 0x130 + 0x204 * entries.size()) return false;
            const size_t car = (at - 0x130) / 0x204, off = (at - 0x130) % 0x204;
            if (off < 0xF0 || off >= 0x110) return false;
            const std::optional<GarageCar> g = concept_new_car(gt, entries[car]);
            const std::optional<sim::GearGenerator> gg = g ? sim::gear_generator(data->db, g->parts) : std::nullopt;
            return gg && (off - 0xF0) / 4 > size_t(gg->n);
        };
        // The garage's serials count on through the session (a game that made its cars twice before the save starts
        // them at the list's size): compared as a count from the first car's.
        const u32 first_serial = rd32(rp, 0x130 + 0x200);
        CHECK(rd32(rp, 0x24) == first_serial + entries.size());
        auto serial_word = [&](size_t at) {
            if (at >= 0x24 && at < 0x28) return true;
            return at >= 0x130 && at < 0x130 + 0x204 * entries.size() && (at - 0x130) % 0x204 >= 0x200;
        };
        for (size_t i = 0; i < entries.size(); ++i) CHECK(rd32(rp, 0x130 + 0x204 * i + 0x200) == first_serial + i);
        int shown = 0, diffs = 0, spare = 0;
        for (size_t i = 0; i < std::min(mp.size(), rp.size()); ++i)
            if (mp[i] != rp[i] && !serial_word(i)) {
                if (spare_gear(i)) {
                    ++spare;
                    continue;
                }
                ++diffs;
                if (shown++ < 400) std::printf("    %s: ours %02X, the original's %02X\n", where(i, entries.size()).c_str(), mp[i], rp[i]);
            }
        std::printf("    new game against the original's: %d bytes differ (and %d of spare gear words)\n", diffs, spare);
        CHECK(diffs == 0);
        const std::vector<Ps2SaveFile> mf = export_ps2_save_files(mine, data, read);
        if (const std::optional<Bytes> sys = read_host_file(ref_path + "/icon.sys")) CHECK(mf[1].data == *sys);
        if (const std::optional<Bytes> ico = read_host_file(ref_path + "/icon0.ico")) CHECK(mf[0].data == *ico);
        if (const std::optional<Bytes> th = read_host_file(ref_path + "/thumbnail")) {
            // (The words the game sets: +0x00, +0x08, +0x10 .. +0x1F; the rest is whatever its stack held.)
            CHECK(th->size() == 0x60 && rd32(*th, 0) == rd32(mf[3].data, 0) && rd64(*th, 8) == rd64(mf[3].data, 8));
            CHECK(std::memcmp(th->data() + 0x10, mf[3].data.data() + 0x10, 0x10) == 0);
            std::printf("    thumbnail: %u cars to choose, ours %u\n", rd32(*th, 0x10), rd32(mf[3].data, 0x10));
        }
    }
}

// A game with progress as a memory-card image for each disc, to load in the original: GT3R_CONCEPT_PS2_CARD names the
// folder to write "<save name>.ps2" into (skipped without it). Every course license has a medal (gold on the even
// ones), every course is won at Normal, the first five at Professional, with the presents the prize pass gives for
// that; the first car that has a Custom set wears it, with its second paint and a longer final drive.
TEST(concept_disc_ps2_demo_card) {
    const std::vector<std::string> discs = test::concept_discs();
    const char* out = std::getenv("GT3R_CONCEPT_PS2_CARD");
    if (discs.empty() || !out) SKIP("needs GT3R_DISC_CONCEPT and GT3R_CONCEPT_PS2_CARD");
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> disc = Disc::open(path);
        Vfs vfs(&disc->volume());
        const GtData::Reader read = [&vfs](const std::string& p) { return vfs.read(p); };
        auto data = std::make_shared<GtData>();
        CHECK(data->load(read));
        if (data->game != GameKind::Concept) continue;
        GtMode gt(data);
        GameState& s = gt.state();
        const ConceptRules& rules = data->concept_lists;
        for (int course = 0; course < rules.course_count(); ++course) {
            const int medal = course % 2 ? 2 : 0;
            const double seconds = 80.0 + course + 0.123;
            s.concept_game.record_license(course, medal, seconds);
            s.rankings["lic:" + rules.license_codes[size_t(course)]] = {{seconds, "", 0, "GT3R", 0, ""}};
            concept_set_presents(s.concept_game, concept_prize_pass(rules, s.concept_game, ConceptRun{0, course}));
        }
        for (int level = 0; level < 2; ++level)
            for (int course = 0; course < (level ? 5 : rules.course_count()); ++course) {
                s.concept_game.record_place(level, course, 0);
                concept_set_presents(s.concept_game, concept_prize_pass(rules, s.concept_game, ConceptRun{level + 1, course}));
            }
        for (const std::string& e : concept_garage_entries(gt)) {
            std::optional<GarageCar> c = concept_new_car(gt, e);
            if (!c) continue;
            concept_fit_custom(gt, *c);
            if (concept_car_normal(gt, *c)) continue;
            c->settings.final_drive += 100;
            c->color = data->color_count(c->car) > 1 ? 1 : 0;
            s.garage.insert(s.garage.begin(), *c);
            break;
        }
        const std::string file = std::string(out) + "/" + ps2_save_name(*data) + ".ps2";
        export_ps2_save_path(s, data, read, file);
        std::printf("  %s: %zu presents, %.0f%% -> %s\n", disc->info().serial.c_str(), s.concept_game.presents.size(),
                    concept_completion(rules, s.concept_game) * 100.0f, file.c_str());
        CHECK(import_ps2_save_path(file, *data).concept_game.presents == s.concept_game.presents);
    }
}
