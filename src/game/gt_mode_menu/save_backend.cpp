// GT-mode menu bindings: the save page's backend (menu/flow/save_page.h, SavePage::Backend): the
// save library's files (title Load / Save / File Operation, home Save, Trade, arcade Load
// Garage) and the current game's setting files (Load / Save Settings), and their operations.

#include "game/gt_mode_menu/internal.h"

#include "base/file.h"
#include "base/log.h"
#include "game/gt_save.h"
#include "game/gt_setting.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace gt3r::game::gt_menu {

using SavePage = menu::flow::SavePage;

namespace {

std::string date_text(s64 t) {
    if (t <= 0) return {};
    std::time_t tt = std::time_t(t);
    std::tm* tm = std::localtime(&tt);
    if (!tm) return {};
    char b[32];
    std::snprintf(b, sizeof b, "%02d/%02d/%04d", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900);
    return b;
}

using Texts = std::vector<std::pair<std::string, std::string>>;

Texts setting_file_texts(const GtMode& gt, const SettingFile& f) {
    Texts t;
    t.push_back({"s_pt_car", car_name(gt, f.car)});
    t.push_back({"s_pt_comment", f.comment});
    t.push_back({"s_pt_date", date_text(f.time)});
    if (f.power_ps > 0)
        t.push_back({"s_pt_power", gt.power_text(float(f.power_ps))});
    return t;
}

Texts game_file_texts(const GtMode& gt, const std::string& path) {
    const GameState& st = gt.state();
    Texts t;
    static const char* const kLic[] = {"B", "A", "IB", "IA", "S"};
    int lic = gt.highest_license();
    t.push_back({"g_pt_license", lic >= 0 && lic < 5 ? kLic[lic] : ""});
    t.push_back({"g_pt_cars", format_credits(s64(st.garage.size()))});
    t.push_back({"g_pt_g_achievement", GtMode::completion_file_text(gt.completion_percent())});
    // The native file's size in KB (rounded up; the original shows its card file's).
    if (std::optional<Bytes> b = read_host_file(path)) t.push_back({"g_pt_size", std::to_string((b->size() + 1023) / 1024) + "KB"});
    s64 mtime = 0;
    if (std::optional<s64> m = host_file_mtime(path)) mtime = *m;
    t.push_back({"g_pt_date", date_text(mtime)});
    t.push_back({"g_pt_day", std::to_string(st.day)});
    t.push_back({"g_pt_money", format_credits(st.credits)});
    return t;
}

int read_result(const std::string& err) {
    return err.find("version") != std::string::npos ? SavePage::BadData : SavePage::ReadError;
}

} // namespace

std::vector<SavePage::File> save_page_files(SavePage::Mode m) {
    const MenuState& ms = menu_state();
    std::vector<SavePage::File> out;
    GtModeSession* s = S();
    if (!s) return out;
    if (m == SavePage::Mode::LoadSettings || m == SavePage::Mode::SaveSettings) {
        // The current game's setting files, newest first (Load: only the ride car's).
        GarageCar* real = setting_real();
        if (m == SavePage::Mode::SaveSettings) {
            SavePage::File n;
            n.kind = SavePage::File::Kind::New;
            out.push_back(n);
        }
        const auto& files = s->gt().state().setting_files;
        for (int i = int(files.size()) - 1; i >= 0; --i) {
            const SettingFile& sf = files[size_t(i)];
            const bool own = real && sf.car == real->car;
            if (m == SavePage::Mode::LoadSettings && !own) continue;
            SavePage::File f;
            f.kind = SavePage::File::Kind::Setting;
            f.id = std::to_string(i);
            f.own = own;
            f.name = sf.comment;
            f.texts = setting_file_texts(s->gt(), sf);
            out.push_back(std::move(f));
        }
        return out;
    }
    SaveLibrary& lib = save_library();
    if (m == SavePage::Mode::LoadGame || m == SavePage::Mode::SaveGame) {
        SavePage::File n;
        n.kind = SavePage::File::Kind::New;
        n.name = lib.unique_name("GAME");
        out.push_back(n);
    }
    for (SaveInfo& info : lib.list()) {
        SavePage::File f;
        f.id = info.path;
        f.current = same_save_file(info.path, s->save_path());
        if (f.current && m == SavePage::Mode::LoadGuest) continue; // the home garage already
        f.name = info.name;
        f.broken = !info.state;
        if (info.state) {
            // The game being played as it is now (its file may be a race behind), or the
            // snapshot a save page was opened for (begin_snapshot_save).
            GtMode view(s->gt().data_ptr());
            view.state() = f.current ? (ms.snapshot ? *ms.snapshot : s->gt().state()) : std::move(*info.state);
            f.texts = game_file_texts(view, info.path);
        }
        out.push_back(std::move(f));
    }
    return out;
}

namespace {

int save_page_run(SavePage::Mode m, SavePage::Op op, const SavePage::File& f, const std::string& arg) {
    MenuState& ms = menu_state();
    GtModeSession* s = S();
    if (!s) return SavePage::ReadError;
    GameState& live = s->gt().state();
    const bool write = saving_enabled();
    // --- Setting files of the current game.
    if (f.kind == SavePage::File::Kind::Setting || m == SavePage::Mode::SaveSettings ||
        m == SavePage::Mode::LoadSettings) {
        GarageCar* real = setting_real();
        const int idx = f.kind == SavePage::File::Kind::Setting ? std::atoi(f.id.c_str()) : -1;
        auto& files = live.setting_files;
        const bool valid = idx >= 0 && idx < int(files.size());
        switch (op) {
        case SavePage::OpLoad: {
            if (!real || !valid) return SavePage::ReadError;
            int miss = apply_setting_file(files[size_t(idx)], *real);
            if (miss < 0) return SavePage::BadData;
            s->setting_work = *real;
            if (miss > 0) ms.popup = "load_warning";
            break;
        }
        case SavePage::OpSave:
        case SavePage::OpOverwrite:
            if (!real) return SavePage::WriteError;
            write_setting_file(live, *real, op == SavePage::OpSave ? -1 : idx, s64(std::time(nullptr)),
                               int(s->gt().power_ps(*real))); // the computed PS, truncated
            break;
        case SavePage::OpDelete:
            if (!delete_setting_file(live, idx)) return SavePage::WriteError;
            break;
        case SavePage::OpRename:
            if (!valid) return SavePage::WriteError;
            files[size_t(idx)].comment = arg;
            break;
        case SavePage::OpCopy:
            if (!valid) return SavePage::WriteError;
            files.push_back(files[size_t(idx)]);
            break;
        default: return SavePage::WriteError;
        }
        std::string err;
        if (write && !s->save(&err)) {
            LOG_WARN("gt mode: save failed: %s", err.c_str());
            return SavePage::WriteError;
        }
        return SavePage::Ok;
    }
    // --- Saves.
    SaveLibrary& lib = save_library();
    std::string err;
    auto save_live = [&]() {
        if (write && !s->save(&err)) LOG_WARN("gt mode: save failed: %s", err.c_str());
    };
    auto play = [&](const std::string& path) {
        s->set_save_path(path);
        lib.set_current(path);
    };
    switch (op) {
    case SavePage::OpLoad: {
        if (m == SavePage::Mode::LoadGuest) {
            // A friend's game: any save file, native or PS2 (the guest garage only).
            const std::string path = f.kind == SavePage::File::Kind::Open ? arg : f.id;
            try {
                GameState st = read_any_save(path, s->gt().data());
                s->guest = std::make_unique<GtMode>(s->gt().data_ptr());
                s->guest->state() = std::move(st);
            } catch (const std::exception& e) {
                LOG_WARN("gt mode: guest load failed: %s", e.what());
                return read_result(e.what());
            }
            return SavePage::Ok;
        }
        std::optional<GameState> st = read_save(f.id, &err);
        if (!st) {
            LOG_WARN("gt mode: load failed: %s", err.c_str());
            return read_result(err);
        }
        save_live(); // the game being left keeps its progress
        live = std::move(*st);
        s->setting_work.reset();
        play(f.id);
        LOG_INFO("gt mode: loaded %s", f.id.c_str());
        return SavePage::Ok;
    }
    case SavePage::OpNewGame: {
        save_live();
        const std::string name = lib.unique_name(arg);
        s->gt().new_game();
        live.file_comment = name;
        s->setting_work.reset();
        play(lib.path_for(name));
        save_live();
        return SavePage::Ok;
    }
    case SavePage::OpSave:
    case SavePage::OpOverwrite: {
        // The running game, or the snapshot offered by begin_snapshot_save (which leaves the
        // running game and its save alone).
        const bool snap = ms.snapshot.has_value();
        GameState st = snap ? *ms.snapshot : live;
        std::string path;
        if (op == SavePage::OpSave) {
            const std::string name = lib.unique_name(arg);
            if (write && !lib.create(st, name, &path, &err)) {
                LOG_WARN("gt mode: save failed: %s", err.c_str());
                return SavePage::WriteError;
            }
            if (!write) path = lib.path_for(name);
            st.file_comment = name;
        } else {
            path = f.id;
            st.file_comment = f.name;
            if (write && !write_save(path, st, &err)) {
                LOG_WARN("gt mode: save failed: %s", err.c_str());
                return SavePage::WriteError;
            }
        }
        if (!snap) {
            live.file_comment = st.file_comment;
            play(path);
        }
        LOG_INFO("gt mode: saved %s", path.c_str());
        return SavePage::Ok;
    }
    case SavePage::OpRename: {
        if (!write) return SavePage::Ok;
        const std::string name = lib.unique_name(arg, SavePage::kNameMax, f.id);
        std::string np;
        if (f.current) {
            live.file_comment = name;
            if (!lib.rename(f.id, name, &np, &live, &err)) return SavePage::WriteError;
            play(np);
        } else if (!lib.rename(f.id, name, &np, nullptr, &err)) {
            return SavePage::WriteError;
        }
        return SavePage::Ok;
    }
    case SavePage::OpCopy: {
        if (!write) return SavePage::Ok;
        if (f.current) save_live();
        std::string np;
        return lib.copy(f.id, &np, &err) ? SavePage::Ok : SavePage::WriteError;
    }
    case SavePage::OpDelete:
        if (f.current) return SavePage::WriteError; // the page never offers it
        if (!write) return SavePage::Ok;
        return lib.remove(f.id) ? SavePage::Ok : SavePage::WriteError;
    case SavePage::OpExport: return SavePage::WriteError; // (the page runs the format chosen)
    case SavePage::OpExportPsu:
    case SavePage::OpExportCard:
    case SavePage::OpExportRaw: {
        // To the PS2: a .psu or a memory-card image (PCSX2 .ps2) holding this save, or the
        // raw data file for save editors; the format as chosen, whatever the file is called.
        std::optional<GameState> st;
        if (f.current) st = live;
        else st = read_save(f.id, &err);
        if (!st) return read_result(err);
        const Ps2Export format = op == SavePage::OpExportCard  ? Ps2Export::Card
                                 : op == SavePage::OpExportRaw ? Ps2Export::Raw
                                                               : Ps2Export::Psu;
        const std::string out = SavePage::export_path(arg, op); // (the page names it the same way)
        try {
            Bytes b = export_ps2_save_container(*st, s->gt().data_ptr(), ms.reader, format);
            if (!write_host_file(out, b)) throw FormatError("cannot write " + out);
        } catch (const std::exception& e) {
            LOG_WARN("gt mode: export failed: %s", e.what());
            return SavePage::WriteError;
        }
        LOG_INFO("gt mode: exported %s", out.c_str());
        return SavePage::Ok;
    }
    case SavePage::OpImport: {
        // Another save into the library: a native save from elsewhere or a PS2 save.
        GameState st;
        try {
            st = read_any_save(arg, s->gt().data());
        } catch (const std::exception& e) {
            LOG_WARN("gt mode: import failed: %s", e.what());
            return read_result(e.what());
        }
        std::string base = st.file_comment;
        if (base.empty()) {
            const menu::flow::FileDialogs& d = menu::flow::file_dialogs();
            size_t sl = arg.find_last_of("/\\");
            if (arg.rfind("content://", 0) != 0) base = arg.substr(sl == std::string::npos ? 0 : sl + 1);
            else if (d.name) base = d.name(arg); // (its display name)
            if (size_t dot = base.rfind('.'); dot != std::string::npos && dot > 0) base.resize(dot);
            if (base.empty() || base == kPs2GameDir) base = "PS2 GAME";
        }
        if (!write) return SavePage::Ok;
        std::string np;
        return lib.create(std::move(st), lib.unique_name(base), &np, &err) ? SavePage::Ok : SavePage::WriteError;
    }
    }
    return SavePage::WriteError;
}

} // namespace

int save_run(SavePage::Mode m, SavePage::Op op, const SavePage::File& f, const std::string& arg) {
    const int r = save_page_run(m, op, f, arg);
    // A snapshot (begin_snapshot_save) is good for one save.
    if (r == 0 && (op == SavePage::OpSave || op == SavePage::OpOverwrite)) menu_state().snapshot.reset();
    return r;
}

void save_closed() { menu_state().snapshot.reset(); }

} // namespace gt3r::game::gt_menu
