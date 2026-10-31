// gt3tool GT-mode save commands: move progress between a GT3 Revisited native
// save (<user data>/saves/*.sav) and the original game's PS2 memory-card save
// (docs/dev/GT_MODE.md "Saves").

#include "tool.h"

#include "game/gt_save.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace gt3r::tool {

namespace {

// The disc as the game reads it, without mods: the Vfs answers the region, language and
// program files (Vfs::kRegionFile, ...), so the data set and the save's card folder,
// GAMEDATA version and language word are the disc release's own.
game::GtData::Reader disc_reader(const Vfs& vfs) {
    return [&vfs](const std::string& p) { return vfs.read(p); };
}

std::shared_ptr<game::GtData> load_gt_data(const Vfs& vfs) {
    auto d = std::make_shared<game::GtData>();
    if (!d->load(disc_reader(vfs))) throw FormatError("GT mode data: " + d->error());
    return d;
}

// Takes "--lang=XX" out of `a` and sets it as the game's language: one of the disc
// release's (PAL: BR, FR, GR, IT, SP); the release's own without it.
Args take_language(Vfs& vfs, const Args& a) {
    Args rest;
    for (const std::string& s : a) {
        if (s.rfind("--lang=", 0) != 0) {
            rest.push_back(s);
            continue;
        }
        std::string code = s.substr(7);
        for (char& c : code) c = char(std::toupper(u8(c)));
        const std::vector<Language> langs = vfs.region().languages();
        std::optional<Language> l = text::language_from_code(code);
        if (!l || std::find(langs.begin(), langs.end(), *l) == langs.end()) {
            std::string have;
            for (Language x : langs) have += std::string(have.empty() ? "" : ", ") + text::language_code(x);
            throw FormatError("the " + std::string(region_name(vfs.region().region)) + " release has no language " +
                              code + " (" + have + ")");
        }
        vfs.set_language(*l);
    }
    return rest;
}

void summary(const char* what, const game::GameState& s, const game::GtData& data) {
    std::printf("%s (%s): day %u, %lld Cr, %zu cars, %zu race results\n", what, game::ps2_game_dir(data.region), s.day,
                (long long)s.credits, s.garage.size(), s.race_best.size());
}

} // namespace

GT3TOOL_COMMAND(save_import, "save-import", "<ps2 save> <out.sav>",
                "convert an original GT3 save (data file / .raw, save folder, PCSX2 folder card, .psu, .ps2 card) to a GT3 Revisited save",
                2, [](Disc& disc, const Args& a) {
                    Vfs vfs(&disc.volume());
                    auto data = load_gt_data(vfs);
                    game::GameState s = game::import_ps2_save_path(a[0], *data);
                    std::string err;
                    if (!game::write_save(a[1], s, &err)) throw FormatError(err);
                    summary(a[1].c_str(), s, *data);
                    return 0;
                });

GT3TOOL_COMMAND(save_export, "save-export", "<save.sav> <out.psu | out.ps2 | out.raw | out folder> [--lang=XX]",
                "write a GT3 Revisited save as an original GT3 memory-card save (.raw: the data file alone, "
                "for save editors; --lang: the language a PAL save starts the game in, BR (the default), FR, GR, IT or SP)",
                2, [](Disc& disc, const Args& args) {
                    Vfs vfs(&disc.volume());
                    const Args a = take_language(vfs, args);
                    if (a.size() < 2) throw FormatError("save-export needs <save.sav> and the output");
                    auto data = load_gt_data(vfs);
                    std::string err;
                    std::optional<game::GameState> s = game::read_save(a[0], &err);
                    if (!s) throw FormatError(err);
                    game::export_ps2_save_path(*s, data, disc_reader(vfs), a[1]);
                    summary(a[1].c_str(), *s, *data);
                    return 0;
                });

} // namespace gt3r::tool
