// Mods: the installed mods (on/off, details, the cars each adds and why one did not load),
// installing mods from a .zip (vfs/mod_archive.h), removing them, and the mods folder in the
// system's file manager (platform/user_files.h; on Android the Files app, since the folder is
// in private app storage there). docs/MODDING.md "Installing mods". Texture pack tools: saving
// every texture of the disc (formats/texture_pack.h) and converting a PCSX2 texture pack into
// a mod (formats/pcsx2_textures.h), docs/MODDING.md "Texture packs".

#include "app/app.h"
#include "app/screen_registry.h"
#include "base/config.h"
#include "base/file.h"
#include "base/log.h"
#include "formats/pcsx2_textures.h"
#include "formats/text/charset.h"
#include "formats/texture_pack.h"
#include "game/data/car_database.h"
#include "platform/ios_files.h"
#include "platform/music_folder.h"
#include "platform/user_files.h"
#include "vfs/mod_archive.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <thread>

namespace gt3r {

namespace {

const SDL_DialogFileFilter kZipFilters[] = {
    {"Mod archives (.zip)", "zip"},
    {"All files", "*"},
};

bool is_zip_name(std::string_view s) { return s.size() > 4 && to_lower(s.substr(s.size() - 4)) == ".zip"; }

// Every build has a mods folder to unpack into and to open.
constexpr bool can_install() { return true; }
constexpr bool can_open_folder() { return true; }
// PCSX2 packs live on computers: the folder dialog of a computer picks one.
bool can_convert_pcsx2() {
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    return false;
#else
    return can_install();
#endif
}

// A file picked in the system dialog. The dialog may answer from another thread, and after the
// screen is gone, so the callback holds its own reference.
struct PickSlot {
    std::mutex mutex;
    bool answered = false;
    std::string path;  // empty: cancelled
    std::string error; // the dialog could not open
};

void SDLCALL on_pick(void* user, const char* const* files, int) {
    auto* slot = static_cast<std::shared_ptr<PickSlot>*>(user);
    {
        std::lock_guard lock((*slot)->mutex);
        if (!files) (*slot)->error = SDL_GetError();
        else if (files[0]) (*slot)->path = files[0];
        (*slot)->answered = true;
    }
    delete slot;
}

// Reading an archive and unpacking it, off the main thread: a big archive, or one the system
// must fetch first, takes seconds.
class InstallJob {
public:
    enum class Stage { Reading, Planned, Installing, Done, Failed };

    ~InstallJob() {
        if (thread_.joinable()) thread_.join();
    }

    void read(const std::string& path, const std::string& name, const std::string& mods_dir) {
        thread_ = std::thread([this, path, name, mods_dir] {
            std::string err;
            zip_ = ZipArchive::open(path, &err);
            if (zip_) plan_ = plan_mod_install(*zip_, name, mods_dir, &err);
            error_ = err;
            stage_.store(plan_ ? Stage::Planned : Stage::Failed, std::memory_order_release);
        });
    }
    void install(const std::string& mods_dir, const std::string& staging_dir) {
        thread_.join();
        stage_ = Stage::Installing;
        thread_ = std::thread([this, mods_dir, staging_dir] {
            std::string err;
            const bool ok = install_mod_archive(*zip_, *plan_, mods_dir, staging_dir, &progress_, &err);
            error_ = err;
            stage_.store(ok ? Stage::Done : Stage::Failed, std::memory_order_release);
        });
    }

    Stage stage() const { return stage_.load(std::memory_order_acquire); }
    // From Planned on (the worker only reads it after that).
    const ModArchivePlan& plan() const { return *plan_; }
    // Once Failed.
    const std::string& error() const { return error_; }
    float progress() const {
        const u64 total = progress_.total;
        return total ? float(double(progress_.done) / double(total)) : 0.0f;
    }

private:
    std::atomic<Stage> stage_{Stage::Reading};
    std::thread thread_;
    std::unique_ptr<ZipArchive> zip_;
    std::optional<ModArchivePlan> plan_;
    ModInstallProgress progress_;
    std::string error_;
};

// Saving every texture of the disc, or converting a PCSX2 texture pack, off the main thread:
// both read every game file (a minute or so on a computer). The mods must not change meanwhile.
class TextureJob {
public:
    enum class Kind { Save, Convert };

    ~TextureJob() {
        cancel_ = true;
        if (thread_.joinable()) thread_.join();
    }

    // Save: the textures into `dir`. Convert: PCSX2's pack folder `pack` into the mod folder `dir`.
    void start(Kind kind, const Vfs& vfs, std::string dir, std::string pack, std::string mod_name) {
        kind_ = kind;
        dir_ = dir;
        thread_ = std::thread([this, &vfs, dir = std::move(dir), pack = std::move(pack), mod_name = std::move(mod_name)] {
            run(vfs, dir, pack, mod_name);
            done_.store(true, std::memory_order_release);
        });
    }
    Kind kind() const { return kind_; }
    const std::string& dir() const { return dir_; }
    bool done() const { return done_.load(std::memory_order_acquire); }
    float progress() const { return progress_.load(std::memory_order_relaxed); }
    void cancel() { cancel_ = true; }
    bool cancelled() const { return cancel_; }
    // Once done.
    const std::string& message() const { return message_; }
    bool ok() const { return ok_; }

private:
    void run(const Vfs& vfs, const std::string& dir, const std::string& pack, const std::string& mod_name) {
        std::vector<std::string> paths;
        if (const Volume* v = vfs.base())
            for (const Volume::Entry& e : v->entries())
                if (e.size && texture_pack::dump_reads(e.path)) paths.push_back(e.path);
        const bool save = kind_ == Kind::Save;
        texture_pack::Dump dump(dir);
        texture_pack::Pcsx2Index index;
        bool written = true;
        for (size_t i = 0; i < paths.size() && !cancel_; ++i) {
            progress_ = float(i) / float(paths.size()) * (save ? 1.0f : 0.8f);
            std::optional<Bytes> data = vfs.read(paths[i]);
            if (!data) continue;
            try {
                if (save) dump.add_file(paths[i], *data, &written);
                else index.add_file(paths[i], *data);
            } catch (const std::exception& e) {
                LOG_WARN("Textures: %s: %s", paths[i].c_str(), e.what());
            }
        }
        if (cancel_) {
            message_ = save ? "Stopped saving textures." : "Stopped converting the pack.";
            return;
        }
        if (save) {
            written = dump.finish() && written;
            ok_ = written;
            message_ = "Saved " + std::to_string(dump.saved()) + " textures to " + dir +
                       (written ? "." : " (some could not be written).");
            return;
        }
        texture_pack::Pcsx2Import r = texture_pack::import_pcsx2_pack(
            pack, index, dir + "/textures", [&](const std::string& p) { return vfs.read(p); },
            [&](size_t done, size_t total) { progress_ = 0.8f + 0.2f * float(done) / float(std::max<size_t>(total, 1)); });
        if (!host_path_exists(dir + "/mod.ini")) {
            const std::string ini = "[mod]\nname = " + mod_name + "\ndescription = Converted from a PCSX2 texture pack.\n";
            write_host_file(dir + "/mod.ini", ByteSpan(reinterpret_cast<const u8*>(ini.data()), ini.size()));
        }
        // What was left out and why, for the pack's maker.
        std::string report = "# PCSX2 texture pack conversion (docs/MODDING.md \"PCSX2 texture packs\")\n# from " + pack + "\n";
        auto list = [&](const char* title, const std::vector<std::string>& names) {
            if (names.empty()) return;
            report += std::string("\n") + title + "\n";
            for (const std::string& n : names) report += n + "\n";
        };
        list("# Not recognised (pictures the game makes while it runs, like text, or another release's):", r.unmatched);
        list("# No dump of the same name in dumps/:", r.no_dump);
        list("# Failed:", r.failed);
        write_host_file(dir + "/pcsx2-import.txt", ByteSpan(reinterpret_cast<const u8*>(report.data()), report.size()));
        ok_ = r.converted > 0;
        message_ = "Converted " + std::to_string(r.converted) + " of " + std::to_string(r.pictures) + " pictures into " +
                   mod_name + ".";
        if (!r.unmatched.empty())
            message_ += " " + std::to_string(r.unmatched.size()) +
                        " were not recognised (pictures the game makes while it runs, such as text).";
        if (!r.no_dump.empty())
            message_ += " " + std::to_string(r.no_dump.size()) + " have no dump of the same name in dumps/.";
        if (!r.failed.empty()) message_ += " " + std::to_string(r.failed.size()) + " failed (see pcsx2-import.txt).";
    }

    Kind kind_ = Kind::Save;
    std::string dir_;
    std::thread thread_;
    std::atomic<bool> done_{false}, cancel_{false};
    std::atomic<float> progress_{0};
    std::string message_;
    bool ok_ = false;
};

std::string with_version(const std::string& name, const std::string& version) {
    return version.empty() ? name : name + " v" + version;
}

class ModsScreen : public Screen {
public:
    bool clickable(float x, float y) const override { return target_at(x, y).kind != Target::None; }

    void on_event(App& app, const SDL_Event& e) override {
        if (e.type == SDL_EVENT_DROP_FILE && e.drop.data && is_zip_name(e.drop.data)) {
            if (!job_ && !confirm_) begin_install(app, e.drop.data);
            return;
        }
        // Back from the Files app or a file manager: show what was copied in or deleted.
        if (e.type == SDL_EVENT_DID_ENTER_FOREGROUND || e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) rescan_ = true;
        if (e.type == SDL_EVENT_MOUSE_WHEEL && !confirm_ && !job_) {
            scroll_ -= e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f) * lay_.row_h;
            follow_sel_ = false;
            return;
        }
        if (float x, y; xr_hover(e, x, y)) { // the VR pointer focuses what it is over (ui.h)
            const Target t = target_at(x, y);
            if (t.kind == Target::Row || t.kind == Target::Toggle) sel_ = t.index;
            if (t.kind == Target::Yes || t.kind == Target::No) confirm_.focus_yes = t.kind == Target::Yes;
        }
        switch (gesture_.feed(e)) {
        case Gesture::Result::Down:
            press_ = target_at(gesture_.x(), gesture_.y());
            press_scroll_ = scroll_;
            break;
        case Gesture::Result::DragStart:
        case Gesture::Result::Drag:
            if (!confirm_ && !job_ && lay_.list.contains(gesture_.start_x(), gesture_.start_y())) {
                scroll_ = press_scroll_ - (gesture_.y() - gesture_.start_y());
                follow_sel_ = false;
            }
            press_ = {};
            break;
        case Gesture::Result::Tap:
            if (Target t = target_at(gesture_.x(), gesture_.y()); t.kind != Target::None && t == press_) activate(app, t);
            press_ = {};
            break;
        case Gesture::Result::Up: press_ = {}; break;
        default: break;
        }
    }

    void update(App& app, double) override {
        // Testing and screenshots: GT3R_MOD_ZIP=<archive> installs it as if picked;
        // GT3R_PCSX2_PACK=<folder> converts that PCSX2 pack as if picked; GT3R_SAVE_TEXTURES=1 saves
        // every texture as if confirmed.
        if (!env_done_) {
            env_done_ = true;
            if (const char* zip = SDL_getenv("GT3R_MOD_ZIP"); zip && *zip) begin_install(app, zip);
            else if (const char* pack = SDL_getenv("GT3R_PCSX2_PACK"); pack && *pack && app.vfs()) convert_pack(app, pack);
            else if (const char* save = SDL_getenv("GT3R_SAVE_TEXTURES"); save && *save == '1' && app.vfs()) {
                start_texture_job(app, TextureJob::Kind::Save, dump_dir(app), "", "");
                scripted_ = true; // no file manager window at the end
            }
        }
        poll_pick(app);
        poll_job(app);
        poll_texture_job(app);
        poll_pack_pick(app);
        if (texture_job_) {
            if (app.menu().pressed(PadButton::Triangle)) texture_job_->cancel();
            return;
        }
        if (rescan_ && !job_) {
            rescan_ = false;
            rescan(app);
        }
        const MenuInput& m = app.menu();
        if (confirm_) {
            if (m.horizontal()) confirm_.focus_yes = !confirm_.focus_yes;
            if (m.pressed(PadButton::Cross)) answer(app, confirm_.focus_yes);
            else if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) answer(app, false);
            return;
        }
        if (job_) return; // reading or unpacking: wait (leaving included)
        const int n = int(app.vfs()->mods().size());
        if (int v = m.vertical(); v && n) {
            sel_ = std::clamp(sel_ + v, 0, n - 1);
            follow_sel_ = true;
        }
        if (m.pressed(PadButton::Cross) && n) toggle(app, sel_);
        if (m.pressed(PadButton::Square) && n) ask_remove(app, sel_);
        if (m.pressed(PadButton::Start)) pick(app);
        if (m.pressed(PadButton::Select) && can_open_folder()) open_folder(app);
        if (m.pressed(PadButton::Circle) && can_install()) ask_save_textures(app);
        if (m.pressed(PadButton::R1) && can_convert_pcsx2()) ask_convert(app);
        if (m.pressed(PadButton::Triangle)) app.pop();
    }

    void render(App& app, Ui& ui) override {
        layout(ui);
        const UiRect& S = ui.safe();
        const bool touch = ui.touch();
        const auto& mods = app.vfs()->mods();
        sel_ = std::clamp(sel_, 0, std::max(0, int(mods.size()) - 1));

        // Top bar: back, title, actions.
        ui.fill(0, 0, ui.width(), lay_.bar_bottom, colors::kPanel);
        ui.fill(0, lay_.bar_bottom, ui.width(), 1, colors::kOutline);
        ui.button(lay_.back, "< Back", false, pressed(Target::Back));
        ui.text(lay_.back.right() + 12, std::floor(lay_.back.y + (lay_.back.h - Ui::kChar) / 2), "MODS", colors::kAccent);
        if (lay_.install.w > 0) ui.button(lay_.install, kInstallLabel, false, pressed(Target::Install), true);
        if (lay_.open.w > 0) ui.button(lay_.open, kOpenLabel, false, pressed(Target::Open));
        if (lay_.textures.w > 0) ui.button(lay_.textures, kTexturesLabel, false, pressed(Target::Textures));
        if (lay_.convert.w > 0) ui.button(lay_.convert, kConvertLabel, false, pressed(Target::Convert));

        render_list(app, ui, mods);
        if (!mods.empty()) render_details(app, ui, mods[size_t(sel_)]);

        // Status: the last result, else where mods go.
        const float sy = lay_.strip_y;
        ui.fill(0, sy, ui.width(), ui.height() - sy, Color{22, 24, 31});
        ui.fill(0, sy, ui.width(), 1, colors::kOutline);
        std::string status = message_;
        if (status.empty()) {
#if defined(SDL_PLATFORM_ANDROID)
            status = "Mods folder: in the Files app, under GT3 Revisited > mods (Open folder takes you there).";
#elif defined(SDL_PLATFORM_IOS)
            status = "Mods folder: in the Files app, On My iPhone > GT3 Revisited > mods (Open folder takes you there).";
#else
            if (can_open_folder()) status = "Mods folder: " + Ui::elide(app.mods_dir(), S.w - 16 - Ui::text_width("Mods folder: "));
#endif
        }
        std::vector<std::string> lines = Ui::wrap(status, S.w - 16);
        if (lines.size() > 2) {
            for (size_t k = 2; k < lines.size(); ++k) lines[1] += " " + lines[k];
            lines.resize(2);
            lines[1] = Ui::clip(lines[1], S.w - 16);
        }
        const Color sc = message_.empty() ? colors::kDim : message_bad_ ? colors::kBad : colors::kGood;
        for (size_t i = 0; i < lines.size(); ++i) ui.text(S.x + 8, sy + 5 + float(i) * 11, lines[i], sc);
        if (!touch)
            ui.footer(can_convert_pcsx2() ? "Cross: on/off  Square: remove  Start: install .zip  Select: open folder  "
                                            "Circle: save textures  R1: PCSX2 pack  Triangle: back"
                      : can_install()     ? "Cross: on/off   Square: remove   Start: install .zip   Select: open folder   "
                                            "Circle: save textures   Triangle: back"
                                          : "Cross: on/off   Square: remove   Triangle: back");

        if (job_) render_progress(ui);
        if (texture_job_) render_texture_progress(ui);
        if (confirm_) render_confirm(ui);
    }

    bool takes_dropped_file(const std::string& path) const override { return can_install() && is_zip_name(path); }
    TouchLayout touch_layout() const override { return TouchLayout::None; } // buttons are tapped directly
    bool adaptive_ui() const override { return true; }

private:
    static constexpr const char* kInstallLabel = "Install from .zip";
    static constexpr const char* kOpenLabel = "Open folder";
    static constexpr const char* kTexturesLabel = "Save textures";
    static constexpr const char* kConvertLabel = "PCSX2 pack";

    struct Target {
        enum Kind { None, Back, Install, Open, Textures, Convert, Remove, Row, Toggle, Yes, No } kind = None;
        int index = 0;
        bool operator==(const Target& o) const { return kind == o.kind && index == o.index; }
    };
    struct Layout {
        UiRect back, install, open, textures, convert, list, details, remove, yes, no;
        float bar_bottom = 20, strip_y = 318, row_h = 30;
    };
    struct Confirm {
        enum Kind { None, Remove, Replace, SaveTextures, Convert } kind = None;
        std::string title, yes;
        std::vector<std::string> lines;
        std::string mod_id; // Remove
        bool focus_yes = false;
        explicit operator bool() const { return kind != None; }
    };

    // --- actions ---

    void activate(App& app, const Target& t) {
        switch (t.kind) {
        case Target::Back: app.pop(); break;
        case Target::Install: pick(app); break;
        case Target::Open: open_folder(app); break;
        case Target::Textures: ask_save_textures(app); break;
        case Target::Convert: ask_convert(app); break;
        case Target::Remove: ask_remove(app, sel_); break;
        case Target::Row: sel_ = t.index; break;
        case Target::Toggle:
            sel_ = t.index;
            toggle(app, t.index);
            break;
        case Target::Yes: answer(app, true); break;
        case Target::No: answer(app, false); break;
        case Target::None: break;
        }
    }

    void toggle(App& app, int i) {
        const ModInfo& mod = app.vfs()->mods()[size_t(i)];
        if (!mod.for_disc) {
            say(mod.name + " is made for other discs (" + discs_text(mod) + "): it stays off on this one.", true);
            return;
        }
        app.set_mod_enabled(mod.id, !mod.enabled);
        cars_loaded_ = false;
    }

    // mod.ini's discs, as written.
    static std::string discs_text(const ModInfo& mod) {
        std::string out;
        for (const std::string& d : mod.discs) out += (out.empty() ? "" : ", ") + d;
        return out;
    }

    void rescan(App& app) {
        const auto& mods = app.vfs()->mods();
        const std::string keep = sel_ < int(mods.size()) ? mods[size_t(sel_)].id : std::string();
        app.rescan_mods();
        cars_loaded_ = false;
        select(app, keep);
    }

    void select(App& app, const std::string& id) {
        const auto& mods = app.vfs()->mods();
        for (size_t i = 0; i < mods.size(); ++i)
            if (mods[i].id == id) sel_ = int(i), follow_sel_ = true;
        sel_ = std::clamp(sel_, 0, std::max(0, int(mods.size()) - 1)); // it was removed
    }

    void say(std::string text, bool bad) {
        message_ = std::move(text);
        message_bad_ = bad;
    }

    void open_folder(App& app) {
        SDL_CreateDirectory(app.mods_dir().c_str());
        if (platform::open_user_folder(app.mods_dir(), app.data_dir())) return;
#if defined(SDL_PLATFORM_ANDROID)
        say("No app could open the folder. Open the Files app: GT3 Revisited is listed with the other places there.", true);
#elif defined(SDL_PLATFORM_IOS)
        say("Could not open the Files app. The folder is there under On My iPhone > GT3 Revisited > mods.", true);
#else
        say("Could not open " + app.mods_dir(), true);
#endif
    }

    void pick(App& app) {
        if (!can_install() || pick_ || job_) return;
        pick_ = std::make_shared<PickSlot>();
        // iOS: the system's document picker (SDL has no file dialog there).
        if (platform::show_document_picker(app.window(), kZipFilters, SDL_arraysize(kZipFilters),
                                           [slot = pick_](std::string path) {
                                               std::lock_guard lock(slot->mutex);
                                               slot->path = std::move(path);
                                               slot->answered = true;
                                           }))
            return;
        SDL_ShowOpenFileDialog(on_pick, new std::shared_ptr<PickSlot>(pick_), app.window(), kZipFilters,
                               SDL_arraysize(kZipFilters), nullptr, false);
    }

    void poll_pick(App& app) {
        if (!pick_) return;
        std::string path, error;
        {
            std::lock_guard lock(pick_->mutex);
            if (!pick_->answered) return;
            path = pick_->path, error = pick_->error;
        }
        pick_.reset();
        if (!error.empty()) say("Could not show the file picker: " + error, true);
        else if (!path.empty()) begin_install(app, path);
    }

    void begin_install(App& app, const std::string& path) {
        job_name_ = platform::picked_file_name(path);
        say("", false);
        job_ = std::make_unique<InstallJob>();
        job_->read(path, job_name_, app.mods_dir());
    }

    // Unpacked in a hidden folder beside mods/ (same storage, so the move into place is a rename).
    static std::string staging_dir(App& app) { return app.data_dir() + ".mod-install"; }

    void poll_job(App& app) {
        if (!job_ || confirm_) return;
        switch (job_->stage()) {
        case InstallJob::Stage::Reading:
        case InstallJob::Stage::Installing: return;
        case InstallJob::Stage::Planned: {
            const ModArchivePlan& plan = job_->plan();
            std::vector<std::string> replaced;
            for (const ArchiveMod& m : plan.mods)
                if (m.replaces)
                    replaced.push_back(with_version(m.installed_name.empty() ? m.id : m.installed_name, m.installed_version) +
                                       " is installed; the archive has " +
                                       (m.version == m.installed_version ? std::string("the same version")
                                        : m.version.empty()              ? std::string("a copy without a version")
                                                                         : "v" + m.version) +
                                       ".");
            if (replaced.empty()) {
                job_->install(app.mods_dir(), staging_dir(app));
                return;
            }
            confirm_ = Confirm{};
            confirm_.kind = Confirm::Replace;
            confirm_.title = plan.mods.size() == 1 ? "Replace " + plan.mods[0].name + "?" : "Replace installed mods?";
            confirm_.lines = std::move(replaced);
            confirm_.lines.push_back("Replacing deletes the installed copy, with any changes made to it.");
            confirm_.yes = "Replace";
            confirm_.focus_yes = true;
            return;
        }
        case InstallJob::Stage::Done: {
            const ModArchivePlan& plan = job_->plan();
            for (const ArchiveMod& m : plan.mods) app.config().set("mods", m.id, "on"); // installing = wanting it
            app.save_config();
            app.rescan_mods();
            cars_loaded_ = false;
            select(app, plan.mods.front().id);
            if (plan.mods.size() == 1) {
                const ArchiveMod& m = plan.mods.front();
                say((m.replaces ? "Updated " : "Installed ") + with_version(m.name, m.version) + ".", false);
            } else {
                std::string names;
                for (const ArchiveMod& m : plan.mods) names += (names.empty() ? "" : ", ") + m.name;
                say("Installed " + std::to_string(plan.mods.size()) + " mods: " + names + ".", false);
            }
            break;
        }
        case InstallJob::Stage::Failed: say("Could not install " + job_name_ + ": " + job_->error(), true); break;
        }
        job_.reset();
    }

    void ask_remove(App& app, int i) {
        const ModInfo& mod = app.vfs()->mods()[size_t(i)];
        confirm_ = Confirm{};
        confirm_.kind = Confirm::Remove;
        confirm_.title = "Remove " + with_version(mod.name, mod.version) + "?";
        confirm_.lines = {"Its folder mods/" + mod.id + " is deleted, with everything in it."};
        confirm_.yes = "Remove";
        confirm_.mod_id = mod.id;
        confirm_.focus_yes = false; // Cross on its own does not delete
    }

    void answer(App& app, bool yes) {
        Confirm c = std::move(confirm_);
        confirm_ = Confirm{};
        if (c.kind == Confirm::SaveTextures) {
            if (yes) start_texture_job(app, TextureJob::Kind::Save, dump_dir(app), "", "");
            return;
        }
        if (c.kind == Confirm::Convert) {
            if (yes && !platform::start_folder_pick(app.window(), "")) say("Could not show the folder picker.", true);
            pack_pick_ = yes;
            return;
        }
        if (c.kind == Confirm::Replace) {
            if (yes) job_->install(app.mods_dir(), staging_dir(app));
            else job_.reset();
            return;
        }
        if (c.kind != Confirm::Remove || !yes) return;
        std::string name = c.mod_id, err;
        for (const ModInfo& m : app.vfs()->mods())
            if (m.id == c.mod_id) name = m.name;
        if (!remove_mod_folder(app.mods_dir(), c.mod_id, &err)) {
            say("Could not remove " + name + ": " + err, true);
            return;
        }
        for (const IniDocument::Section& sec : app.config().sections()) // [mods] and every disc's [mods.<disc key>]
            if (sec.name == "mods" || sec.name.rfind("mods.", 0) == 0) app.config().remove(sec.name, c.mod_id);
        app.save_config();
        rescan(app);
        say("Removed " + name + ".", false);
    }

    // --- texture packs ---

    static std::string dump_dir(App& app) {
        return app.data_dir() + texture_pack::dump_folder(app.disc() ? app.disc()->info().serial : std::string());
    }

    void ask_save_textures(App& app) {
        if (!can_install() || !app.vfs() || texture_job_ || job_) return;
        confirm_ = Confirm{};
        confirm_.kind = Confirm::SaveTextures;
        confirm_.title = "Save every texture?";
        confirm_.lines = {"The disc's textures are saved as PNG pictures, each once, named so that a texture pack "
                          "can replace them: about 400 MB, a minute or two.",
                          "Folder: " + dump_dir(app)};
        confirm_.yes = "Save";
        confirm_.focus_yes = true;
    }

    void ask_convert(App& app) {
        if (!can_convert_pcsx2() || !app.vfs() || texture_job_ || job_ || pack_pick_) return;
        confirm_ = Confirm{};
        confirm_.kind = Confirm::Convert;
        confirm_.title = "Convert a PCSX2 texture pack?";
        confirm_.lines = {"Pick the pack's folder in PCSX2's textures folder (" +
                              (app.disc() ? texture_pack::pcsx2_serial(app.disc()->info().serial) : std::string("the game's serial")) +
                              "), with its replacements and PCSX2's dumps of the same pictures.",
                          "The pictures become a mod. Reading the disc takes a minute or so."};
        confirm_.yes = "Pick folder";
        confirm_.focus_yes = true;
    }

    // The PCSX2 pack folder that `picked` names: itself, or the disc's folder in it.
    static std::string pack_folder(App& app, const std::string& picked) {
        if (host_is_directory(picked + "/replacements")) return picked;
        const std::string serial = app.disc() ? texture_pack::pcsx2_serial(app.disc()->info().serial) : std::string();
        if (!serial.empty() && host_is_directory(picked + "/" + serial + "/replacements")) return picked + "/" + serial;
        if (host_is_directory(picked + "/textures/" + serial + "/replacements")) return picked + "/textures/" + serial;
        return {};
    }

    void poll_pack_pick(App& app) {
        if (!pack_pick_) return;
        std::optional<platform::FolderAnswer> a = platform::take_folder_pick();
        if (!a) return;
        pack_pick_ = false;
        if (a->kind == platform::FolderAnswer::Failed) say("Could not show the folder picker.", true);
        if (a->kind == platform::FolderAnswer::Picked && app.vfs()) convert_pack(app, a->path);
    }

    void convert_pack(App& app, const std::string& picked) {
        const std::string pack = pack_folder(app, picked);
        if (pack.empty()) {
            say("That folder has no replacements folder. Pick the game's folder in PCSX2's textures folder.", true);
            return;
        }
        std::string base = pack.substr(pack.find_last_of('/') + 1), id = "pcsx2";
        for (char c : base) id += (std::isalnum(u8(c)) ? char(std::tolower(u8(c))) : '_');
        if (id == "pcsx2") id = "pcsx2_pack";
        else id.insert(5, "_");
        start_texture_job(app, TextureJob::Kind::Convert, app.mods_dir() + "/" + id, pack, "PCSX2 texture pack (" + base + ")");
    }

    void start_texture_job(App& app, TextureJob::Kind kind, const std::string& dir, const std::string& pack,
                           const std::string& mod_name) {
        say("", false);
        texture_job_ = std::make_unique<TextureJob>();
        texture_job_->start(kind, *app.vfs(), dir, pack, mod_name);
    }

    void poll_texture_job(App& app) {
        if (!texture_job_ || !texture_job_->done()) return;
        std::unique_ptr<TextureJob> job = std::move(texture_job_);
        say(job->message(), !job->ok() && !job->cancelled());
        if (job->kind() == TextureJob::Kind::Convert && job->ok()) {
            const std::string id = job->dir().substr(job->dir().find_last_of('/') + 1);
            app.config().set("mods", id, "on"); // converting = wanting it
            app.save_config();
            rescan(app); // the new mod, and its pictures in the texture pack
            select(app, id);
        } else if (job->kind() == TextureJob::Kind::Save && job->ok() && can_open_folder() && !scripted_) {
            platform::open_user_folder(dump_dir(app), app.data_dir());
        }
    }

    void render_texture_progress(Ui& ui) {
        ui.fill(0, 0, ui.width(), ui.height(), Color{0, 0, 0, 150});
        const UiRect& S = ui.safe();
        const float w = std::min(380.0f, S.w - 40), h = 62;
        const UiRect p{std::floor(S.x + (S.w - w) / 2), std::floor(S.y + (S.h - h) / 2), w, h};
        ui.fill_round(p.x, p.y, p.w, p.h, 6, colors::kPanel);
        ui.frame_round(p.x, p.y, p.w, p.h, 6, 1, colors::kOutline);
        const bool save = texture_job_->kind() == TextureJob::Kind::Save;
        ui.text(p.x + 14, p.y + 12, save ? "Saving the disc's textures..." : "Converting the PCSX2 texture pack...",
                colors::kText);
        const float v = texture_job_->progress();
        ui.bar(p.x + 14, p.y + 32, w - 70, 8, v, colors::kAccent);
        ui.text_right(p.right() - 14, p.y + 31, std::to_string(int(v * 100)) + "%", colors::kDim);
        ui.text(p.x + 14, p.y + 45, texture_job_->cancelled() ? "Stopping..." : ui.touch() ? "" : "Triangle: stop", colors::kDim);
    }

    // --- drawing ---

    bool pressed(Target::Kind k, int index = 0) const {
        return gesture_.active() && press_.kind == k && press_.index == index;
    }

    void layout(const Ui& ui) {
        const UiRect& S = ui.safe();
        const bool touch = ui.touch();
        Layout& l = lay_;
        const float bar_h = touch ? 30.0f : 20.0f, pad = touch ? 20.0f : 16.0f;
        l.bar_bottom = S.y + bar_h;
        l.back = {S.x + 6, S.y + (touch ? 3 : 2), touch ? 72.0f : 64.0f, bar_h - (touch ? 6 : 4)};
        const float ow = std::ceil(Ui::text_width(kOpenLabel)) + pad, iw = std::ceil(Ui::text_width(kInstallLabel)) + pad;
        l.open = can_open_folder() ? UiRect{S.right() - 6 - ow, l.back.y, ow, l.back.h} : UiRect{S.right(), l.back.y, 0, 0};
        l.install = can_install() ? UiRect{l.open.x - 6 - iw, l.back.y, iw, l.back.h} : UiRect{};
        const float tw = std::ceil(Ui::text_width(kTexturesLabel)) + pad, cw = std::ceil(Ui::text_width(kConvertLabel)) + pad;
        const float left = can_install() ? l.install.x : l.open.x;
        l.convert = can_convert_pcsx2() ? UiRect{left - 6 - cw, l.back.y, cw, l.back.h} : UiRect{left, l.back.y, 0, 0};
        l.textures = can_install() ? UiRect{l.convert.x - (l.convert.w > 0 ? 6 : 0) - tw, l.back.y, tw, l.back.h} : UiRect{};
        const float strip_h = 26, footer_h = touch ? 0.0f : 16.0f;
        l.strip_y = ui.height() - footer_h - strip_h - (touch ? ui.height() - S.bottom() : 0);
        const float top = l.bar_bottom + 5, split = std::floor(S.x + S.w * 0.5f);
        l.list = {S.x + 6, top, split - S.x - 9, l.strip_y - top - 5};
        l.details = {split + 3, top, S.right() - 6 - split - 3, l.list.h};
        l.row_h = touch ? 42.0f : 30.0f;
    }

    void render_list(App&, Ui& ui, const std::vector<ModInfo>& mods) {
        rows_.assign(mods.size(), UiRect{});
        toggles_.assign(mods.size(), UiRect{});
        if (mods.empty()) { // no details beside it: the whole width
            const UiRect L{lay_.list.x, lay_.list.y, lay_.details.right() - lay_.list.x, lay_.list.h};
            ui.fill_round(L.x, L.y, L.w, L.h, 5, Color{20, 22, 29});
            float y = std::floor(L.y + L.h / 2 - 30);
            ui.text_center_in(L.x, L.w, y, "No mods installed yet.", colors::kText);
            y += 18;
            const std::string how = can_install() ? "Install one from a .zip file, or copy mod folders into the mods folder."
                                                  : "The browser version cannot install mods yet.";
            for (const std::string& line : Ui::wrap(how, L.w - 24)) {
                ui.text_center_in(L.x, L.w, y, line, colors::kDim);
                y += 11;
            }
            ui.text_center_in(L.x, L.w, y + 6, "A mod is a folder with a mod.ini; see the modding guide.", colors::kDim);
            return;
        }
        const UiRect& L = lay_.list;
        const float rh = lay_.row_h, content = rh * float(mods.size());
        if (follow_sel_) {
            const float top = rh * float(sel_);
            if (top < scroll_) scroll_ = top;
            if (top + rh > scroll_ + L.h) scroll_ = top + rh - L.h;
        }
        scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, content - L.h));
        SDL_Rect clip{int(L.x), int(L.y), int(std::ceil(L.w)), int(L.h)};
        SDL_SetRenderClipRect(ui.renderer(), &clip);
        const bool touch = ui.touch();
        for (size_t i = 0; i < mods.size(); ++i) {
            const ModInfo& mod = mods[i];
            const UiRect r{L.x, L.y + rh * float(i) - scroll_, L.w - 6, rh - 4};
            if (r.bottom() < L.y || r.y > L.bottom()) continue;
            rows_[i] = r;
            const bool s = int(i) == sel_;
            ui.fill_round(r.x, r.y, r.w, r.h, touch ? 5.0f : 3.0f,
                          pressed(Target::Row, int(i)) ? Color{62, 70, 100} : s ? colors::kHighlight : Color{24, 26, 34});
            if (s) ui.fill_round(r.x, r.y + 3, 3, r.h - 6, 1.5f, colors::kAccent);
            const float tx = r.x + 10, cy = r.y + r.h / 2;
            toggles_[i] = {tx - 4, r.y, ui.toggle_width() + 8, r.h};
            ui.toggle_pill(tx, cy, mod.active());
            const float x = tx + ui.toggle_width() + 10;
            std::string right;
            if (!mod.cars.empty()) right = std::to_string(mod.cars.size()) + (mod.cars.size() == 1 ? " car" : " cars");
            else if (!mod.textures.empty())
                right = std::to_string(mod.textures.size()) + (mod.textures.size() == 1 ? " texture" : " textures");
            const float rw = right.empty() ? 0 : Ui::text_width(right) + 10;
            ui.text(x, std::floor(cy - 10), Ui::clip(with_version(mod.name, mod.version), r.right() - x - 8 - rw),
                    s ? colors::kAccent : mod.active() ? colors::kText : colors::kDim);
            if (!right.empty()) ui.text_right(r.right() - 8, std::floor(cy - 10), right, colors::kDim);
            const std::string sub = !mod.for_disc              ? "Made for other discs: " + discs_text(mod)
                                    : !mod.description.empty() ? mod.description
                                    : !mod.author.empty()      ? "by " + mod.author
                                                               : mod.id;
            ui.text(x, std::floor(cy + 2), Ui::clip(sub, r.right() - x - 8), colors::kDim);
        }
        SDL_SetRenderClipRect(ui.renderer(), nullptr);
        ui.scrollbar({L.x, L.y, L.w, L.h}, content, scroll_);
    }

    void render_details(App& app, Ui& ui, const ModInfo& mod) {
        const UiRect& D = lay_.details;
        const bool touch = ui.touch();
        ui.fill_round(D.x, D.y, D.w, D.h, 5, Color{20, 22, 29});
        const float x = D.x + 10, w = D.w - 20;
        const float bh = touch ? 32.0f : 20.0f;
        lay_.remove = {x, D.bottom() - bh - 8, std::ceil(Ui::text_width("Remove")) + (touch ? 36.0f : 28.0f), bh};
        const float bottom = lay_.remove.y - 6;
        float y = D.y + 8;
        auto line = [&](const std::string& s, Color c) {
            if (y + Ui::kChar > bottom) return;
            ui.text(x, y, Ui::clip(s, w), c);
            y += 11;
        };
        line(with_version(mod.name, mod.version), colors::kAccent);
        if (!mod.author.empty()) line("by " + mod.author, colors::kDim);
        y += 3;
        std::vector<std::string> desc = Ui::wrap(mod.description, w);
        if (desc.size() > 4) desc.resize(4), desc[3] = Ui::clip(desc[3] + "...", w);
        for (const std::string& d : desc) line(d, colors::kText);
        y += 3;
        std::string what;
        auto count = [&](size_t n, const char* one, const char* many) {
            if (n) what += (what.empty() ? "" : ", ") + std::to_string(n) + (n == 1 ? one : many);
        };
        count(mod.files.size(), " game file", " game files");
        count(mod.cars.size(), " car", " cars");
        count(mod.makers.size(), " maker", " makers");
        count(mod.textures.size(), " texture", " textures");
        line((what.empty() ? std::string("Nothing to load") : what) + "   priority " + std::to_string(mod.priority) +
                 (!mod.for_disc ? "" : mod.enabled ? "" : "   (off on this disc)"),
             colors::kDim);
        line(mod.discs.empty() ? std::string("For every disc")
                               : "For " + discs_text(mod) + (mod.for_disc ? "" : " (not this disc)"),
             mod.for_disc ? colors::kDim : colors::kBad);
        line("Folder: mods/" + mod.id, colors::kDim);
        if (!mod.cars.empty()) {
            y += 5;
            render_cars(app, ui, mod, x, w, y, bottom);
        }
        ui.button(lay_.remove, "Remove", false, pressed(Target::Remove));
    }

    // The mod's cars (mods/<id>/cars/*.ini): what loaded, and why the others did not. The
    // database is reloaded whenever the mod set changes.
    void render_cars(App& app, Ui& ui, const ModInfo& mod, float x, float w, float& y, float bottom) {
        if (!cars_loaded_) {
            cars_loaded_ = true;
            db_ok_ = db_.load(*app.vfs());
        }
        if (y + Ui::kChar > bottom) return;
        ui.text(x, y, mod.active() ? "Cars" : "Cars (the mod is off)", colors::kText);
        y += 12;
        for (const std::string& file : mod.cars) {
            if (y + Ui::kChar > bottom) break;
            const std::string base = file.substr(file.find_last_of('/') + 1);
            std::string err;
            for (const std::string& e : db_.mod_car_errors())
                if (e.rfind(file + ": ", 0) == 0) err = e.substr(file.size() + 2);
            const data::ModCar* car = nullptr;
            for (const data::ModCar& c : db_.mod_cars())
                if (c.file == file) car = &c;
            std::string text = base;
            Color c = colors::kDim;
            if (db_ok_ && car) {
                const data::Car* dc = db_.car(car->label);
                auto sm = dc ? db_.summarize(*dc) : data::CarDatabase::Summary{};
                char buf[200];
                // (An electric car races with its donor's engine until GT3's physics has the EV drive. The
                // names are game text: UTF-8 for this font.)
                const std::string maker = text::game_text_to_utf8(sm.maker, nullptr);
                const std::string name = text::game_text_to_utf8(sm.name, nullptr);
                std::snprintf(buf, sizeof(buf), "%s %s %u, %s%s %.0f kW %u kg, shell %s%s", maker.c_str(), name.c_str(),
                              sm.year, sm.drive.c_str(), car->electric ? " EV" : "", sm.power_ps / 1.35962, sm.weight_kg,
                              car->donor.c_str(), car->electric ? "; races with its engine for now" : "");
                text = buf, c = colors::kGood;
            } else if (db_ok_ && !err.empty()) {
                text = base + ": " + err, c = colors::kBad;
            } else if (db_ok_ && mod.active()) {
                text = base + ": replaced by another mod";
            }
            ui.text(x + 8, y, Ui::clip(text, w - 8), c);
            y += 11;
        }
    }

    void render_progress(Ui& ui) {
        ui.fill(0, 0, ui.width(), ui.height(), Color{0, 0, 0, 150});
        const UiRect& S = ui.safe();
        const float w = std::min(380.0f, S.w - 40), h = 62;
        const UiRect p{std::floor(S.x + (S.w - w) / 2), std::floor(S.y + (S.h - h) / 2), w, h};
        ui.fill_round(p.x, p.y, p.w, p.h, 6, colors::kPanel);
        ui.frame_round(p.x, p.y, p.w, p.h, 6, 1, colors::kOutline);
        const bool reading = job_->stage() == InstallJob::Stage::Reading || job_->stage() == InstallJob::Stage::Planned;
        ui.text(p.x + 14, p.y + 12, Ui::clip((reading ? "Reading " : "Installing ") + job_name_ + "...", w - 28), colors::kText);
        if (!reading) {
            const float v = job_->progress();
            ui.bar(p.x + 14, p.y + 32, w - 70, 8, v, colors::kAccent);
            ui.text_right(p.right() - 14, p.y + 31, std::to_string(int(v * 100)) + "%", colors::kDim);
        } else {
            ui.text(p.x + 14, p.y + 32, "Looking for mods in the archive", colors::kDim);
        }
    }

    void render_confirm(Ui& ui) {
        ui.fill(0, 0, ui.width(), ui.height(), Color{0, 0, 0, 150});
        const UiRect& S = ui.safe();
        const bool touch = ui.touch();
        const float w = std::min(420.0f, S.w - 40), tw = w - 28;
        std::vector<std::string> lines;
        for (const std::string& l : confirm_.lines)
            for (std::string& wl : Ui::wrap(l, tw)) lines.push_back(std::move(wl));
        const float bh = touch ? 32.0f : 22.0f;
        const float h = 14 + 16 + 11 * float(lines.size()) + 12 + bh + 14;
        const UiRect p{std::floor(S.x + (S.w - w) / 2), std::floor(S.y + (S.h - h) / 2), w, h};
        ui.fill_round(p.x, p.y, p.w, p.h, 6, colors::kPanel);
        ui.frame_round(p.x, p.y, p.w, p.h, 6, 1, colors::kOutline);
        float y = p.y + 14;
        ui.text(p.x + 14, y, Ui::clip(confirm_.title, tw), colors::kAccent);
        y += 16;
        for (const std::string& l : lines) {
            ui.text(p.x + 14, y, l, colors::kText);
            y += 11;
        }
        const float bw = std::min(120.0f, (tw - 10) / 2);
        lay_.no = {p.right() - 14 - bw, p.bottom() - 14 - bh, bw, bh};
        lay_.yes = {lay_.no.x - 10 - bw, lay_.no.y, bw, bh};
        const bool danger = confirm_.kind == Confirm::Remove;
        ui.button(lay_.yes, confirm_.yes, confirm_.focus_yes, pressed(Target::Yes), !danger);
        ui.button(lay_.no, "Cancel", !confirm_.focus_yes, pressed(Target::No));
    }

    Target target_at(float x, float y) const {
        if (confirm_) {
            if (lay_.yes.contains(x, y)) return {Target::Yes};
            if (lay_.no.contains(x, y)) return {Target::No};
            return {};
        }
        if (job_ || texture_job_) return {};
        if (lay_.back.contains(x, y)) return {Target::Back};
        if (lay_.install.w > 0 && lay_.install.contains(x, y)) return {Target::Install};
        if (lay_.open.w > 0 && lay_.open.contains(x, y)) return {Target::Open};
        if (lay_.textures.w > 0 && lay_.textures.contains(x, y)) return {Target::Textures};
        if (lay_.convert.w > 0 && lay_.convert.contains(x, y)) return {Target::Convert};
        if (!rows_.empty() && lay_.remove.contains(x, y)) return {Target::Remove};
        if (lay_.list.contains(x, y))
            for (size_t i = 0; i < rows_.size(); ++i) {
                if (rows_[i].w <= 0 || !rows_[i].contains(x, y)) continue;
                return {toggles_[i].contains(x, y) ? Target::Toggle : Target::Row, int(i)};
            }
        return {};
    }

    Layout lay_;
    std::vector<UiRect> rows_, toggles_; // last frame's, for taps
    Gesture gesture_;
    Target press_;
    float scroll_ = 0, press_scroll_ = 0;
    bool follow_sel_ = true;
    int sel_ = 0;
    bool rescan_ = false;
    bool env_done_ = false;

    std::shared_ptr<PickSlot> pick_;
    std::unique_ptr<InstallJob> job_;
    std::unique_ptr<TextureJob> texture_job_;
    bool pack_pick_ = false; // the PCSX2 pack's folder picker is open
    bool scripted_ = false;  // GT3R_SAVE_TEXTURES started the save
    std::string job_name_;
    Confirm confirm_;
    std::string message_;
    bool message_bad_ = false;

    data::CarDatabase db_;
    bool cars_loaded_ = false, db_ok_ = false;
};

} // namespace

std::unique_ptr<Screen> make_mods_screen() { return std::make_unique<ModsScreen>(); }

GT3R_REGISTER_PLAYER_SCREEN(mods, "mods", "Mods", "Install, turn on and off, or remove mods.", 5,
                            [] { return make_mods_screen(); });

} // namespace gt3r
