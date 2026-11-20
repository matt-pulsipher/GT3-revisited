// Gran Turismo Concept's front end (the Concept spec): each release's languages, and what the menus read from the
// discs. The rules of the game state are in test_concept_state.cpp, the menu flow in test_concept_flow.cpp. The first
// tests need no disc; the concept_disc_* tests take GT3R_DISC_CONCEPT (disc images separated by ';').

#include "test_framework.h"

#include "concept_discs.h"
#include "disc/disc.h"
#include "disc/region.h"
#include "formats/menu/mbl.h"
#include "game/data/car_database.h"
#include "race/license.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <memory>

using namespace gt3r;

namespace {

RegionData concept_release(Region r, const char* gcf) {
    RegionData rd{r};
    rd.game = GameKind::Concept;
    rd.program_gcf = std::string("gameconf/") + gcf;
    return rd;
}

} // namespace

// A Concept release's own language and the ones its language page offers follow the game config its program names.
TEST(concept_release_languages) {
    using L = Language;
    const RegionData tokyo = concept_release(Region::Japan, "concept.gcf"), seoul = concept_release(Region::Japan, "concept_kr.gcf");
    const RegionData asia = concept_release(Region::Japan, "concept_tw.gcf"), geneva = concept_release(Region::Europe, "concept_eu.gcf");
    CHECK(tokyo.default_language() == L::JP && tokyo.languages() == std::vector<L>{L::JP} && tokyo.language_page() == "root");
    CHECK(seoul.default_language() == L::KR && seoul.languages() == std::vector<L>{L::KR});
    CHECK(asia.default_language() == L::TW && (asia.languages() == std::vector<L>{L::TW, L::US}) && asia.language_page() == "root_tw");
    CHECK(geneva.default_language() == L::BR && geneva.languages().size() == 6 && geneva.languages().back() == L::PT &&
          geneva.language_page() == "root");
    // The choice survives the region file, and GT3's releases are as before.
    const std::optional<RegionData> back = RegionData::from_text(asia.to_text());
    CHECK(back && back->default_language() == L::TW);
    CHECK(RegionData{Region::Japan}.default_language() == L::JP && RegionData{Region::Europe}.languages().size() == 5 &&
          RegionData{Region::America}.languages() == std::vector<L>{L::US} && RegionData{Region::Europe}.language_page() == "root");
    // Only the offered languages with menus on the disc are playable; the player's choice among them is kept.
    const auto has = [](L l) { return l != L::PT; };
    const std::vector<L> playable = playable_languages(geneva, has);
    CHECK(playable.size() == 5 && choose_language(geneva, playable, L::FR) == L::FR && choose_language(geneva, playable, L::PT) == L::BR);
    CHECK(choose_language(asia, playable_languages(asia, [](L) { return true; }), std::nullopt) == L::TW);
}

// The units of the languages Concept adds (the programs' language-to-units table).
TEST(concept_language_units) {
    const Units pt = language_units(Language::PT);
    CHECK(!pt.mph && std::string(pt.power) == "CV" && pt.power_hp && !pt.torque_ftlb);
    for (Language l : {Language::KR, Language::CN, Language::TW}) {
        const Units u = language_units(l);
        CHECK(!u.mph && std::string(u.power) == "PS" && !u.power_hp && !u.torque_ftlb);
    }
}

// Each disc: its release's languages have their Concept menus, its language page offers them, and the license tests'
// medal times are the disc's own (the 50 Hz release's rows hold the same times as the 60 Hz twin rows on the disc, so
// nothing is taken from another database as on GT3's PAL disc).
TEST(concept_disc_languages) {
    const std::vector<std::string> discs = test::concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> disc = Disc::open(path);
        const RegionData& rd = disc->info().data;
        Vfs vfs(&disc->volume());
        std::printf("  %s: starts in %s, %zu language(s)\n", disc->info().serial.c_str(), text::language_code(rd.default_language()),
                    rd.languages().size());
        const auto has = [&](Language l) { return vfs.exists(std::string("menu/") + text::language_code(l) + "/concept2.pmb"); };
        const std::vector<Language> playable = playable_languages(rd, has);
        CHECK(playable == rd.languages());
        CHECK(std::find(playable.begin(), playable.end(), rd.default_language()) != playable.end());
        if (rd.languages().size() > 1) {
            // The language page's items are the offered languages.
            const std::optional<Bytes> mbl = vfs.read("menu/language.mbl");
            CHECK(mbl.has_value());
            if (!mbl) continue;
            const menu::MblFile f = menu::MblFile::parse(*mbl);
            const menu::MblPage* page = nullptr;
            for (const menu::MblPage& p : f.pages)
                if (p.name == rd.language_page()) page = &p;
            CHECK(page != nullptr);
            if (!page) continue;
            std::vector<Language> offered;
            for (const menu::MblItem& it : page->items) {
                std::string code = it.name;
                for (char& c : code) c = char(std::toupper(static_cast<unsigned char>(c)));
                if (std::optional<Language> l = text::language_from_code(code)) offered.push_back(*l);
            }
            std::sort(offered.begin(), offered.end());
            std::vector<Language> want = rd.languages();
            std::sort(want.begin(), want.end());
            CHECK(offered == want);
        }
        data::CarDatabase db;
        CHECK(db.load(vfs));
        // LCeu000n (the 50 Hz release's tests) and LCtw000n (the 60 Hz Asian release's, same cars): the same times.
        int pairs = 0;
        for (int n = 0; n < 10; ++n) {
            const data::Event* eu = db.event("LCeu000" + std::to_string(n));
            const data::Event* tw = db.event("LCtw000" + std::to_string(n));
            if (!eu || !tw) continue;
            ++pairs;
            // Gold, silver, bronze: three words at +0x1C4.
            CHECK(eu->bytes.size() >= 0x1D0 && tw->bytes.size() >= 0x1D0 && rd32(eu->bytes, 0x1C4) > 0 &&
                  std::equal(eu->bytes.begin() + 0x1C4, eu->bytes.begin() + 0x1D0, tw->bytes.begin() + 0x1C4));
        }
        if (rd.default_language() == Language::BR || rd.default_language() == Language::TW) CHECK(pairs == 10);
        // Each test's medal times read as {minutes, seconds, milliseconds} (race/license.h): gold < silver < bronze, each a
        // lap of one to three minutes (Tokyo-Geneva's Midfield test: 1'27, 1'30, 1'37).
        for (const data::Event& ev : db.events()) {
            if (ev.label.rfind("LC", 0) != 0 || ev.label.size() != 8) continue;
            const race::LicenseTimes t = race::license_times(ev);
            CHECK(t.valid() && t.ms[0] < t.ms[1] && t.ms[1] < t.ms[2] && t.ms[0] > 60000 && t.ms[2] < 180000);
            if (ev.label == "LCeu0000") CHECK(t.ms[0] == 87000 && t.ms[1] == 90000 && t.ms[2] == 97000);
        }
    }
}
