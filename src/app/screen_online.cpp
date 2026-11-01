// Online battle lobby: the arcade menu's i.LINK Battle, over IP (docs/dev/ONLINE.md).
//
// Start page: host a battle (UDP port [online] port, default 47630) or join one by
// address; the player name. Lobby page: the players (name, car, ready, ping), the
// course and laps (the host chooses), the car rules (their own page), your car, colour
// and gearbox, Ready / Start. The host starts when everyone is ready; every player's race
// screen loads, and the countdown starts for all at once. After the race everyone comes
// back here.
//
// Every page takes the pad, the keyboard and pointers (mouse, touch, the VR controller's
// pointer): rows follow a hovering pointer and act on a click (a finger's first tap
// focuses), the "<" ">" around a value are buttons, the footer's actions are buttons, and
// a click on the session code copies it.
//
// Scripted runs (tests, two instances on one machine):
//   GT3R_ONLINE="host,players=2,course=trialmt,laps=1,car=r34_gtr_v_99"
//   GT3R_ONLINE="join=127.0.0.1,car=nsx_type_r_02,name=Two"
// readies at once; the host starts when `players` are in and ready. `page=cars`,
// `page=makers`, `page=classes`, `page=courses` or `page=rules` opens that page once in the
// lobby (screenshots; no auto start then). The host's rules: `class=B` (C, B, A, S, rally),
// `category=production|race`, `drive=FF|FR|4WD|MR|RR`, `aspiration=na|turbo`, `maxps=300`,
// `stock=1`, `mods=0`. `car=` also takes an arcade car label ("arm_tt_00").

#include "app/screen_online.h"

#include "app/app.h"
#include "app/online_pickers.h"
#include "app/screen_registry.h"
#include "app/updates.h"
#include "app/ui.h"
#include "base/log.h"
#include "game/data/car_database.h"
#include "game/gt_mode.h"
#include "game/gt_mode_menu.h"
#include "game/net_car.h"
#include "game/mod_package.h"
#include "base/sha256.h"
#include "menu/flow/menu_set.h"
#include "menu/flow/game_hooks.h"
#include "net/session.h"
#include "race/exact_setup.h"
#include "race/paint.h"
#include "vfs/vfs.h"

#include <SDL3/SDL_clipboard.h>

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace gt3r {

namespace {

using Entry = PickerCar;

struct Script {
    bool have = false, host = false;
    std::string join, name, car, course, server, code, codefile, page;
    int laps = 0, players = 2, paint = -1;
    bool accept = false;
    // The host's rules (-1 = as configured).
    int car_class = -1, category = -1, drive = -1, aspiration = -1, max_ps = -1, stock = -1, mods = -1;
};

int index_of(const std::string& v, std::initializer_list<const char*> names) {
    int i = 0;
    for (const char* n : names) {
        std::string a = v, b = n;
        for (char& c : a) c = char(std::tolower(u8(c)));
        for (char& c : b) c = char(std::tolower(u8(c)));
        if (a == b) return i;
        ++i;
    }
    return -1;
}

Script read_script() {
    Script s;
    const char* env = SDL_getenv("GT3R_ONLINE");
    if (!env) return s;
    s.have = true;
    std::string all = env;
    size_t pos = 0;
    while (pos <= all.size()) {
        size_t end = all.find(',', pos);
        if (end == std::string::npos) end = all.size();
        std::string kv = all.substr(pos, end - pos);
        pos = end + 1;
        size_t eq = kv.find('=');
        std::string k = kv.substr(0, eq), v = eq == std::string::npos ? "" : kv.substr(eq + 1);
        if (k == "host") s.host = true;
        else if (k == "join") s.join = v;
        else if (k == "name") s.name = v;
        else if (k == "car") s.car = v;
        else if (k == "course") s.course = v;
        else if (k == "laps") s.laps = std::atoi(v.c_str());
        else if (k == "players") s.players = std::max(2, std::atoi(v.c_str()));
        else if (k == "paint") s.paint = std::atoi(v.c_str());
        else if (k == "server") s.server = v;
        else if (k == "accept") s.accept = v != "0"; // answer download prompts with yes (tests)
        else if (k == "code") s.code = v;
        else if (k == "codefile") s.codefile = v; // host: write the code here; client: read it (tests)
        else if (k == "page") s.page = v;
        else if (k == "class") s.car_class = index_of(v, {"any", "c", "b", "a", "s", "rally"});
        else if (k == "category") s.category = index_of(v, {"any", "production", "race"});
        else if (k == "drive") s.drive = index_of(v, {"any", "ff", "fr", "4wd", "mr", "rr"});
        else if (k == "aspiration") s.aspiration = index_of(v, {"any", "na", "turbo"});
        else if (k == "maxps") s.max_ps = std::atoi(v.c_str());
        else if (k == "stock") s.stock = v != "0";
        else if (k == "mods") s.mods = v != "0";
    }
    return s;
}

// Database names may hold non-ASCII bytes (e.g. "V.spec"); the UI font is ASCII.
std::string ascii(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (c < 0x80) out += char(c);
        else if ((c & 0xC0) == 0xC0) out += '-';
    return out;
}

bool has_course(const Vfs& vfs, const std::string& label) { return !course_file_path(vfs, label).empty(); }

const game::GtData* gt_data() {
    game::GtModeSession* gs = game::gt_mode_session();
    return gs ? &gs->gt().data() : nullptr;
}

class OnlineScreen : public Screen {
public:
    OnlineScreen() : script_(read_script()) {}

    ~OnlineScreen() override {
        if (editing_ && window_) SDL_StopTextInput(window_);
        net::online().close(); // leaving the lobby ends the session
    }

    void update(App& app, double) override {
        if (!init_) init(app);
        net::Session& ns = net::online();
        if (in_race_) { // back from the race
            in_race_ = false;
            app.menu().suppress();
            pointer_.clear();
            if (ns.is_host() && !ns.in_lobby()) ns.lobby();
            if (ns.active()) ns.set_ready(false);
        }
        ns.poll();
        while (std::optional<net::Event> e = ns.next_event()) {
            using K = net::Event::Kind;
            switch (e->kind) {
            case K::Joined: status("Joined. Choose your car and press Ready.", false); break;
            case K::Rejected:
            case K::Disconnected:
                if (ns.update_required()) { // the relay server: this game is below its min_online
                    updates().note_required(app, *ns.update_required());
                    updates().open_dialog(net::UpdatePrompt::Required);
                    status(update_text(), true);
                } else {
                    status((e->kind == K::Rejected ? "The host refused: " : "Disconnected: ") + e->text, true);
                }
                to_start();
                break;
            case K::Retire:
                if (const net::Player* p = ns.player(e->player)) status(p->name + " left", false);
                else status("A player left", false);
                break;
            case K::Settings:
                course_sel_ = find_course(e->course, course_sel_);
                if (page_ == Page::Cars) { // the rules or the course changed
                    assign_classes();
                    car_picker_.refresh(app);
                }
                break;
            case K::Link:
                if (const net::Player* p = ns.player(e->player)) {
                    net::Link l = ns.link(e->player);
                    status(p->name + (l.direct ? ": direct connection" : l.relay ? ": through the relay server"
                                                                                 : ": relayed through the host"),
                           false);
                }
                break;
            case K::Load: start_race(app, *e); return;
            case K::Queue:
                if (ns.update_required()) {
                    updates().note_required(app, *ns.update_required());
                    updates().open_dialog(net::UpdatePrompt::Required);
                }
                status("Relay server: " + e->text, true);
                break;
            case K::PackageDone: install_download(app, e->text); break;
            case K::PackageFailed:
                if (auto it = downloading_.find(e->text); it != downloading_.end()) {
                    status("Download of " + it->second.name + " failed: " + e->course, true);
                    downloading_.erase(it);
                }
                break;
            default: break;
            }
        }
        if (page_ != Page::Start && page_ != Page::Cars && page_ != Page::Courses) offer_downloads(ns);
        // Another player's car that cannot race here (another region's disc): not ready while
        // it is in the lobby.
        if (!ns.is_host() && ns.in_lobby())
            if (const net::Player* me = ns.my_id() >= 0 ? ns.player(u8(ns.my_id())) : nullptr; me && me->ready)
                if (std::string why = remote_car_problem(ns); !why.empty()) {
                    ns.set_ready(false);
                    status(why, true);
                }
        // Waiting for the relay's link slots needs everyone still ready (the race starts by itself).
        if (ns.is_host() && ns.relay_wait().waiting && !ns.all_ready()) {
            ns.cancel_start();
            status("Start cancelled: not everyone is ready", true);
        }
        if (page_ != Page::Start && !ns.active()) to_start();
        if (script_.have) run_script(app);
        if (editing_) { // text entry has the keyboard (on_event); a click elsewhere finishes it
            edit_pointer(app);
            return;
        }
        if (page_ != Page::Start && page_ != Page::Cars && page_ != Page::Courses && !prompts_.empty()) {
            update_prompt(app);
            return;
        }
        if (page_ == Page::Start) update_start(app);
        else if (page_ == Page::Cars) update_car_picker(app);
        else if (page_ == Page::Courses) update_course_picker(app);
        else if (page_ == Page::Rules) update_rules(app);
        else update_lobby(app);
    }

    void on_event(App& app, const SDL_Event& e) override {
        // Pointers go to the page on screen (the pickers keep their own).
        if (page_ == Page::Cars) car_picker_.event(e);
        else if (page_ == Page::Courses) course_picker_.event(e);
        else pointer_.event(e);
        if (!editing_) return;
        std::string& s = edit_target();
        if (e.type == SDL_EVENT_TEXT_INPUT) {
            for (const char* c = e.text.text; *c; ++c)
                if (s.size() < 63 && u8(*c) >= 32 && u8(*c) < 127) s += *c;
        } else if (e.type == SDL_EVENT_KEY_DOWN) {
            if (e.key.key == SDLK_BACKSPACE && !s.empty()) s.pop_back();
            else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) end_edit(app, true);
            else if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK) end_edit(app, false);
        } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            // Controller typing: up/down change the last character, right adds one, left deletes.
            static const std::string kChars = "0123456789.:abcdefghijklmnopqrstuvwxyz-_[]";
            auto cycle = [&](int d) {
                if (s.empty()) s += '0';
                size_t i = kChars.find(char(std::tolower(u8(s.back()))));
                i = i == std::string::npos ? 0 : (i + kChars.size() + size_t(d)) % kChars.size();
                s.back() = kChars[i];
            };
            switch (e.gbutton.button) {
            case SDL_GAMEPAD_BUTTON_DPAD_UP: cycle(1); break;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: cycle(-1); break;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: if (s.size() < 63) s += s.empty() ? '0' : s.back(); break;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT: if (!s.empty()) s.pop_back(); break;
            case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: end_edit(app, true); break;
            case SDL_GAMEPAD_BUTTON_EAST: end_edit(app, false); break;
            default: break;
            }
        }
    }

    void render(App& app, Ui& ui) override {
        net::Session& ns = net::online();
        ui.clear();
        hits_.clear();
        if (page_ == Page::Start) render_start(ui);
        else if (page_ == Page::Cars) car_picker_.render(app, ui);
        else if (page_ == Page::Courses) course_picker_.render(app, ui, art_);
        else if (page_ == Page::Rules) render_rules(ui, ns);
        else render_lobby(app, ui, ns);
        if ((page_ == Page::Lobby || page_ == Page::Rules) && !prompts_.empty()) render_prompt(ui);
        for (const auto& [hash, d] : downloading_) // progress of the downloads
            if (const net::Session::Download* dl = ns.download(hash); dl && dl->total) {
                std::string t = "Downloading " + d.name + ": " + std::to_string(int(100.0 * dl->received / dl->total)) + " %";
                ui.text_center(Ui::kHeight - 48, t, colors::kAccent);
                break;
            }
        if (!status_.empty())
            ui.text_center(Ui::kHeight - 32, Ui::clip(status_, Ui::kWidth - 40), status_bad_ ? colors::kBad : colors::kGood);
    }

private:
    enum class Page { Start, Lobby, Cars, Courses, Rules };
    // Pointer areas of the start, lobby and rules pages (arg: the row; footer: the action).
    enum Hit { kHitRow, kHitLeft, kHitRight, kHitFooter, kHitCode, kHitPromptYes, kHitPromptNo };

    void init(App& app) {
        init_ = true;
        // Online races run on the exact core (GT3's own race step, with this disc's program tables): a machine without it can
        // be in a lobby but cannot race (docs/dev/ONLINE.md "Model").
        core_problem_.clear();
        std::string err;
        program_ = app.vfs() ? race::exact_program_data(*app.vfs(), &err) : nullptr;
        if (!program_)
            core_problem_ = "This disc cannot race online yet (GT3's own physics: " + (err.empty() ? std::string("no program") : err) + ")";
        // Too old for the relay: say so at once (every time Online battle opens).
        if (updates().required() && !script_.have) updates().open_dialog(net::UpdatePrompt::Required);
        app_ = &app;
        window_ = app.window();
        if (app.vfs()) {
            db_.load(*app.vfs());
            colors_.load(*app.vfs());
            build_car_list(app);
            for (const data::Course& c : db_.courses())
                if (c.label.rfind("L_", 0) != 0 && has_course(*app.vfs(), c.label))
                    courses_.push_back({c.label, ascii(c.name.empty() ? c.label : c.name)});
        }
        IniDocument& cfg = app.config();
        auto num = [&](const char* key, const char* def, int lo, int hi) {
            return std::clamp(std::atoi(cfg.get("online", key).value_or(def).c_str()), lo, hi);
        };
        name_ = cfg.get("online", "name").value_or("Player");
        address_ = cfg.get("online", "address").value_or("");
        server_ = cfg.get("online", "server").value_or(kDefaultServer);
        code_ = cfg.get("online", "code").value_or("");
        port_ = u16(num("port", "47630", 1, 65535));
        car_sel_ = find(cars_, cfg.get("online", "car").value_or("r34_gtr_v_99"), 0);
        course_sel_ = find_course(cfg.get("online", "course").value_or("trialmt"), 0);
        laps_ = num("laps", "2", 1, 99);
        paint_ = std::max(0, std::atoi(cfg.get("online", "paint").value_or("0").c_str()));
        manual_ = cfg.get("online", "gearbox").value_or("at") == "mt";
        rules_.allow_mods = cfg.get("online", "mods").value_or("on") != "off";
        ghost_ms_ = u16(num("ghost_ms", "150", 0, 1000));
        rules_.stock_only = cfg.get("online", "stock_only").value_or("off") == "on";
        rules_.max_ps = num("max_ps", "0", 0, 2000);
        rules_.car_class = num("car_class", "0", 0, game::kArcadeClasses);
        rules_.category = num("category", "0", 0, game::kCategoryRules);
        rules_.drive = num("drive", "0", 0, game::kDriveRules);
        rules_.aspiration = num("aspiration", "0", 0, game::kAspirationRules);
        car_picker_.set_by_class(cfg.get("online", "car_page").value_or("makers") == "classes");
        car_picker_.set_allowed_only(cfg.get("online", "car_page_filter").value_or("allowed") != "all");
        if (script_.have) {
            if (!script_.name.empty()) name_ = script_.name;
            if (!script_.car.empty()) {
                car_sel_ = find(cars_, script_.car, -1);
                if (car_sel_ < 0) LOG_WARN("Online: no car '%s' on this disc", script_.car.c_str()), car_sel_ = 0;
            }
            if (!script_.course.empty()) course_sel_ = find_course(script_.course, course_sel_);
            if (script_.laps > 0) laps_ = script_.laps;
            if (script_.paint >= 0) paint_ = script_.paint;
            if (script_.car_class >= 0) rules_.car_class = script_.car_class;
            if (script_.category >= 0) rules_.category = script_.category;
            if (script_.drive >= 0) rules_.drive = script_.drive;
            if (script_.aspiration >= 0) rules_.aspiration = script_.aspiration;
            if (script_.max_ps >= 0) rules_.max_ps = script_.max_ps;
            if (script_.stock >= 0) rules_.stock_only = script_.stock != 0;
            if (script_.mods >= 0) rules_.allow_mods = script_.mods != 0;
        }
        if (cars_.empty()) status("No cars found on the disc", true);
    }

    // GT Mode garage cars first (with their parts and settings), then every disc and mod car,
    // then the arcade's class lists (the car page's classes: the cars as the arcade races them).
    // Nothing is locked: every car of the disc and its class lists is offered.
    void build_car_list(App& app) {
        cars_.clear();
        game::GtModeSession* gs = nullptr;
        if (game::ensure_gt_mode_session(menu::flow::vfs_reader(*app.vfs()))) gs = game::gt_mode_session();
        if (gs) {
            const std::vector<game::GarageCar>& g = gs->gt().state().garage;
            for (size_t i = 0; i < g.size(); ++i) {
                const data::Car* c = db_.car(g[i].car);
                if (!c) continue;
                Entry e{g[i].car, "Garage: " + ascii(c->latin_name.empty() ? c->label : c->latin_name), gs->gt().model_of(g[i]),
                        int(i), db_.mod_car(g[i].car) != nullptr, c->maker, g[i].color, g[i].wheel, {}, 0, {}};
                cars_.push_back(e);
            }
        }
        size_t garage = cars_.size();
        for (const data::Car& c : db_.cars()) {
            std::string model = db_.model_of(c);
            if (model.empty() || !app.vfs()->exists("cars/day/" + model)) continue;
            Entry e{c.label, ascii(c.name.empty() ? c.label : c.name), model, -1, db_.mod_car(c.label) != nullptr, c.maker, 0, {}, {}, 0, {}};
            if (e.mod) e.name += " (mod)";
            cars_.push_back(e);
        }
        std::sort(cars_.begin() + ptrdiff_t(garage), cars_.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
        // The class lists, every car of them: online play does not wait for arcade mode's
        // unlocks. Labels this disc's region lacks cannot be built and are left out.
        if (gs)
            for (int k = 1; k <= game::kArcadeClasses; ++k) {
                auto list = gs->gt().data().arcade_classes.find(game::arcade_class_tag(k));
                if (list == gs->gt().data().arcade_classes.end()) continue;
                for (const game::GtData::ArcadeCar& a : list->second) {
                    game::OnlineCar oc = game::online_car_from_arcade(db_, a.label, 0);
                    const data::Car* c = oc.car.empty() ? nullptr : db_.car(oc.car);
                    if (!c) continue; // mod cars (marked above) and labels this region lacks
                    std::string model = db_.model_of(*c);
                    if (model.empty() || !app.vfs()->exists("cars/day/" + model)) continue;
                    cars_.push_back(Entry{c->label, ascii(c->latin_name.empty() ? c->label : c->latin_name), model, -1, false, c->maker, 0,
                                          {}, a.label, k, {}});
                }
            }
        facts_.assign(cars_.size(), std::nullopt);
        here_.clear(); // the database changed
    }

    // The car page's classes for this course: every car on the class page of its class
    // (game::car_class: its class list's, else by weight per power, a rally car on dirt), each
    // car once: the stock car of an arcade car is there as that arcade car (and under its maker).
    void assign_classes() {
        const bool dirt = rules_of(net::online()).dirt_course;
        std::set<std::string> arcade_bases;
        for (const Entry& e : cars_)
            if (!e.arcade.empty()) arcade_bases.insert(e.label);
        for (size_t i = 0; i < cars_.size(); ++i) {
            Entry& e = cars_[i];
            const game::CarFacts& f = facts_at(int(i));
            const bool as_arcade = e.arcade.empty() && e.garage < 0 && !f.arcade_class && arcade_bases.count(e.label);
            e.arcade_class = as_arcade ? 0 : game::car_class(f, dirt);
            e.class_text = game::car_class_text(f, dirt);
        }
    }

    // "Class A: Skyline GT-R V-spec II (R34)" for an arcade car.
    static std::string entry_name(const Entry& e) {
        return e.arcade.empty() ? e.name : std::string(game::arcade_class_name(e.arcade_class)) + ": " + e.name;
    }

    // The chosen car as the lobby sends it, with the digest of its parameter block as this machine builds it.
    game::OnlineCar current_car() const {
        game::OnlineCar c = car_at(car_sel_, paint_);
        if (program_) c.digest = game::online_car_digest(db_, *program_, c);
        return c;
    }
    game::OnlineCar car_at(int index, int paint) const {
        if (cars_.empty() || index < 0 || size_t(index) >= cars_.size()) return {};
        const Entry& e = cars_[size_t(index)];
        if (e.garage >= 0)
            if (game::GtModeSession* gs = game::gt_mode_session())
                if (size_t(e.garage) < gs->gt().state().garage.size())
                    return game::online_car_from_garage(db_, gs->gt().state().garage[size_t(e.garage)]);
        if (!e.arcade.empty()) return game::online_car_from_arcade(db_, e.arcade, u8(paint));
        return game::online_car_from_db(db_, e.label, u8(paint));
    }
    bool garage_selected() const { return !cars_.empty() && cars_[size_t(car_sel_)].garage >= 0; }

    // Tells the session our car (label for the roster, the full descriptor for everyone).
    void announce_car() {
        net::Session& ns = net::online();
        game::OnlineCar oc = current_car();
        if (oc.kind == game::OnlineCar::Kind::Mod && app_ && app_->vfs()) offer_own_package(oc);
        ns.set_car(oc.car, oc.paint);
        ns.set_car_info(game::encode_online_car(oc));
    }

    // --- mod car downloads (docs/dev/ONLINE.md "Mod cars") ---

    // Our mod car's package: built once per car, offered to the session, its hash in our descriptor.
    void offer_own_package(game::OnlineCar& oc) {
        auto it = own_packages_.find(oc.car);
        if (it == own_packages_.end()) {
            std::string err;
            std::optional<Bytes> blob = game::build_mod_package(db_, *app_->vfs(), oc.car, &err);
            std::optional<game::ModPackage> pkg = blob ? game::parse_mod_package(*blob, &err) : std::nullopt;
            if (!pkg || !game::validate_mod_package(*pkg, db_, oc.car, &err)) { // others would refuse it too
                LOG_WARN("Online: %s cannot be shared: %s", oc.car.c_str(), err.c_str());
                status(oc.name + " cannot be shared (" + err + "); others see a stand-in", true);
                own_packages_[oc.car] = {};
                return;
            }
            OwnPackage op{sha256_hex(*blob), u32(blob->size())};
            net::online().offer_package(op.hash, std::move(*blob));
            it = own_packages_.emplace(oc.car, op).first;
        }
        oc.package = it->second.hash;
        oc.package_size = it->second.size;
        if (oc.package.empty()) oc.kind = game::OnlineCar::Kind::Disc; // not shareable: others use a stand-in
    }

    // Other players' mod cars we lack: ask once per package (when the lobby allows mods).
    void offer_downloads(const net::Session& ns) {
        if (!ns.settings().allow_mods) return;
        for (const net::Player& p : ns.players()) {
            if (int(p.id) == ns.my_id()) continue;
            const Bytes* info = ns.car_info(p.id);
            std::optional<game::OnlineCar> oc = info ? game::decode_online_car(*info) : std::nullopt;
            if (!oc || oc->kind != game::OnlineCar::Kind::Mod || oc->package.empty() || db_.car(oc->car)) continue;
            if (!asked_.insert(oc->package).second) continue;
            prompts_.push_back({p.id, p.name, *oc});
        }
    }

    void update_prompt(App& app) {
        const MenuInput& m = app.menu();
        bool yes = m.pressed(PadButton::Cross) || (script_.have && script_.accept);
        bool no = m.pressed(PadButton::Circle) || m.pressed(PadButton::Triangle);
        using K = PointerInput::Action::Kind;
        for (const PointerInput::Action& a : pointer_.take())
            if (a.kind == K::Click)
                if (const HitArea* h = hits_.at(a.x, a.y)) {
                    yes = yes || h->id == kHitPromptYes;
                    no = no || h->id == kHitPromptNo;
                }
        Prompt p = prompts_.front();
        if (yes) {
            prompts_.pop_front();
            downloading_[p.car.package] = {p.car.name.empty() ? p.car.car : p.car.name, p.car.car, p.name};
            net::online().request_package(p.car.package, p.player);
            status("Downloading " + downloading_[p.car.package].name + " from " + p.name + "...", false);
        } else if (no) {
            prompts_.pop_front();
            status(p.name + "'s car will show as a stand-in", false);
        }
    }

    void render_prompt(Ui& ui) {
        const Prompt& p = prompts_.front();
        float w = 440, h = 112, x = (Ui::kWidth - w) / 2, y = 116;
        ui.fill(0, 20, Ui::kWidth, Ui::kHeight - 36, Color{0, 0, 0, 120}); // modal
        ui.fill_round(x, y, w, h, 8, colors::kPanel);
        ui.frame_round(x, y, w, h, 8, 1, colors::kOutline);
        std::string car = p.car.name.empty() ? p.car.car : p.car.name;
        char size[32];
        std::snprintf(size, sizeof size, "%.1f MB", double(p.car.package_size) / (1024.0 * 1024.0));
        ui.text_center_in(x, w, y + 14, p.name + " is racing a mod car you don't have:", colors::kText);
        ui.text_center_in(x, w, y + 32, Ui::clip(car, w - 40) + "  (" + size + ")", colors::kAccent);
        ui.text_center_in(x, w, y + 50, "Download it? It is checked, then added to your mods.", colors::kDim);
        const UiRect yes{x + w / 2 - 170, y + 74, 160, 24}, no{x + w / 2 + 10, y + 74, 160, 24};
        ui.button(yes, "Cross: download", hovering(pointer_, yes), false, true);
        ui.button(no, "Circle: no (a stand-in)", hovering(pointer_, no), false);
        hits_.add({0, 0, Ui::kWidth, Ui::kHeight}, -1); // nothing behind the prompt
        hits_.add(yes, kHitPromptYes);
        hits_.add(no, kHitPromptNo);
    }

    void install_download(App& app, const std::string& hash) {
        auto it = downloading_.find(hash);
        std::optional<Bytes> blob = net::online().take_package(hash);
        if (it == downloading_.end() || !blob) return;
        Download d = it->second;
        downloading_.erase(it);
        std::string err;
        std::optional<game::ModPackage> pkg = game::parse_mod_package(*blob, &err);
        if (!pkg || !game::validate_mod_package(*pkg, db_, d.label, &err)) {
            LOG_WARN("Online: refused %s's package for %s: %s", d.from.c_str(), d.label.c_str(), err.c_str());
            status("Refused " + d.name + ": " + err, true);
            return;
        }
        std::optional<std::string> dir = game::install_mod_package(*pkg, hash, app.mods_dir(), d.name, d.from, &err);
        if (!dir) {
            status("Could not install " + d.name + ": " + err, true);
            return;
        }
        LOG_INFO("Online: installed %s from %s in %s", d.label.c_str(), d.from.c_str(), dir->c_str());
        // Mount it and reload the cars (the race screen loads its own database from the VFS).
        std::string keep = cars_.empty() ? "" : car_key(cars_[size_t(car_sel_)]);
        int keep_garage = cars_.empty() ? -1 : cars_[size_t(car_sel_)].garage;
        app.rescan_mods();
        db_ = data::CarDatabase{};
        db_.load(*app.vfs());
        build_car_list(app);
        for (size_t i = 0; i < cars_.size(); ++i)
            if (car_key(cars_[i]) == keep && cars_[i].garage == keep_garage) car_sel_ = int(i);
        status("Installed " + d.name + " (from " + d.from + ")", false);
    }

    // The label an entry is remembered by: an arcade car's own, else its car's.
    static const std::string& car_key(const Entry& e) { return e.arcade.empty() ? e.label : e.arcade; }

    static int find(const std::vector<Entry>& v, const std::string& label, int fallback) {
        for (size_t i = 0; i < v.size(); ++i)
            if (!v[i].arcade.empty() && (v[i].arcade == label || v[i].arcade == "_" + label)) return int(i);
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i].label == label || v[i].label == "_" + label || v[i].model == label) return int(i);
        return fallback;
    }
    int find_course(const std::string& label, int fallback) const {
        for (size_t i = 0; i < courses_.size(); ++i)
            if (courses_[i].label == label || courses_[i].label == "_" + label) return int(i);
        return fallback;
    }
    const std::string& car_label() const { static const std::string none; return cars_.empty() ? none : cars_[size_t(car_sel_)].label; }
    const std::string& course_label() const { static const std::string none; return courses_.empty() ? none : courses_[size_t(course_sel_)].label; }
    int paint_count() const {
        if (cars_.empty()) return 1;
        return app_ ? int(car_picker_.paints(*app_, cars_[size_t(car_sel_)]).size()) : 1;
    }
    void status(std::string s, bool bad) { status_ = std::move(s), status_bad_ = bad; }

    void remember(App& app) {
        IniDocument& cfg = app.config();
        cfg.set("online", "name", name_);
        cfg.set("online", "address", address_);
        cfg.set("online", "server", server_);
        cfg.set("online", "code", code_);
        cfg.set("online", "car", cars_.empty() ? std::string() : car_key(cars_[size_t(car_sel_)]));
        cfg.set("online", "course", course_label());
        cfg.set("online", "laps", std::to_string(laps_));
        cfg.set("online", "paint", std::to_string(paint_));
        cfg.set("online", "gearbox", manual_ ? "mt" : "at");
        cfg.set("online", "mods", rules_.allow_mods ? "on" : "off");
        cfg.set("online", "ghost_ms", std::to_string(ghost_ms_));
        cfg.set("online", "stock_only", rules_.stock_only ? "on" : "off");
        cfg.set("online", "max_ps", std::to_string(rules_.max_ps));
        cfg.set("online", "car_class", std::to_string(rules_.car_class));
        cfg.set("online", "category", std::to_string(rules_.category));
        cfg.set("online", "drive", std::to_string(rules_.drive));
        cfg.set("online", "aspiration", std::to_string(rules_.aspiration));
        cfg.set("online", "car_page", car_picker_.by_class() ? "classes" : "makers");
        cfg.set("online", "car_page_filter", car_picker_.allowed_only() ? "allowed" : "all");
        app.save_config();
    }

    // --- pointers on the start, lobby and rules pages ---

    // A row: `label`, then `value` (with "<" ">" buttons around it when `arrows`) and a dim
    // `hint` after it. The row and its arrows go to hits_. `indent`: room after "<" the
    // caller fills (an icon, a paint chip).
    void draw_row(Ui& ui, int r, float y, bool focused, const std::string& label, const std::string& value, bool enabled,
                  bool arrows, float vx, float right, float indent = 0, const std::string& hint = {},
                  Color hint_color = colors::kDim) {
        if (focused) ui.fill_round(20, y - 3, right - 20, 14, 4, colors::kHighlight);
        hits_.add({20, y - 3, right - 20, 14}, kHitRow, r);
        ui.text(36, y, label, !enabled ? colors::kDim : focused ? colors::kAccent : colors::kText);
        const Color vc = enabled ? colors::kText : colors::kDim;
        float x = vx;
        auto arrow = [&](float ax, const char* glyph, int id) {
            const UiRect a{ax - 4, y - 3, 14, 14};
            const bool hot = hovering(pointer_, a);
            if (hot) ui.fill_round(a.x, a.y, a.w, a.h, 3, colors::kOutline);
            ui.text(ax, y, glyph, hot ? colors::kAccent : vc);
            hits_.add(a, id, r);
        };
        if (arrows) arrow(x, "<", kHitLeft), x += 12;
        x += indent;
        const float room = right - x - (arrows ? 20 : 6);
        const std::string v = Ui::clip(value, room);
        ui.text(x, y, v, vc);
        x += Ui::text_width(v) + 6;
        if (arrows) arrow(x, ">", kHitRight), x += 16;
        if (!hint.empty() && x < right - 20) ui.text(x, y, Ui::clip(hint, right - x - 6), hint_color);
    }

    // Hover focuses a row; a click on an arrow steps its value, on the row confirms it (a
    // finger's first tap only focuses). `act(row, h, cross)` does it; false = the page changed
    // (the remaining actions are for a page no longer on screen).
    template <class Act>
    void row_pointer(int& sel, Act&& act, int rows, bool* back = nullptr) {
        using K = PointerInput::Action::Kind;
        for (const PointerInput::Action& a : pointer_.take()) {
            if (a.kind == K::Back && back) *back = true;
            if (a.kind != K::Hover && a.kind != K::Click) continue;
            const HitArea* h = hits_.at(a.x, a.y);
            if (!h || h->id < 0) continue;
            if (h->id == kHitFooter) {
                if (a.kind == K::Click && !act(-1 - h->arg, 0, true)) return;
                continue;
            }
            if (h->id == kHitCode) {
                if (a.kind == K::Click) copy_code();
                continue;
            }
            if (h->id != kHitRow && h->id != kHitLeft && h->id != kHitRight) continue;
            if (h->arg < 0 || h->arg >= rows) continue;
            if (a.kind == K::Hover) {
                if (sel != h->arg) sel = h->arg, kick_armed_ = false;
                continue;
            }
            const bool focused = sel == h->arg;
            if (!focused) kick_armed_ = false;
            sel = h->arg;
            if (h->id != kHitRow) {
                if (!act(h->arg, h->id == kHitLeft ? -1 : 1, false)) return;
            } else if (focused || !a.finger) {
                if (!act(h->arg, 0, true)) return;
            }
        }
    }

    void copy_code() {
        const net::Session& ns = net::online();
        std::string code = net::format_code(ns.is_host() ? ns.code() : net::normalize_code(code_));
        if (code.empty()) return;
        SDL_SetClipboardText(code.c_str());
        status("Session code " + code + " copied", false);
    }

    // While typing: a click on another row finishes the text (as Enter).
    void edit_pointer(App& app) {
        for (const PointerInput::Action& a : pointer_.take()) {
            if (a.kind != PointerInput::Action::Kind::Click) continue;
            const HitArea* h = hits_.at(a.x, a.y);
            if (h && h->id == kHitRow && h->arg == edit_row()) continue;
            end_edit(app, true);
            return;
        }
    }

    // --- start page ---

    enum StartRow { kCreate, kJoinCode, kHostIp, kJoinIp, kName, kServer, kBack, kStartRows };
    int start_row_ = kCreate; // the row the session was started from

    // Back to the start page (left, refused or disconnected) on the row the session came from,
    // so a failed join is one Cross from editing its code.
    void to_start() {
        page_ = Page::Start, sel_ = start_row_;
        pointer_.clear();
    }

    // "Update required to play online: version 0.2.0" (the update check, or a relay's refusal).
    static std::string update_text() {
        const net::GameVersion& v = updates().latest();
        return "Update required to play online" + (v.known() ? ": version " + v.number() : std::string());
    }

    void update_start(App& app) {
        const MenuInput& m = app.menu();
        bool changed = false;
        row_pointer(sel_, [&](int row, int, bool) {
            changed = true;
            if (row == -1 - kFootBack) app.pop();
            else start_action(app, row < 0 ? sel_ : row, false);
            return false;
        }, kStartRows);
        if (changed) return;
        sel_ = (sel_ + m.vertical() + kStartRows) % kStartRows;
        if (m.pressed(PadButton::Triangle)) {
            app.pop();
            return;
        }
        bool circle = m.pressed(PadButton::Circle);
        if (m.pressed(PadButton::Cross) || circle) start_action(app, sel_, circle);
    }

    // Cross (or a click) on a start row; Circle only edits a join row's text.
    void start_action(App& app, int row, bool circle) {
        sel_ = row;
        // Below the relay's min_online: no sessions through it; the update dialog again (direct-IP
        // and offline play stay open).
        const bool blocked = updates().required() && (sel_ == kCreate || (sel_ == kJoinCode && !circle));
        if (blocked && !server_.empty()) {
            updates().open_dialog(net::UpdatePrompt::Required);
            return;
        }
        switch (sel_) {
        case kCreate:
            if (server_.empty()) begin_edit(Edit::Server);
            else create(app);
            break;
        // Cross opens the last code / address for editing, Enter there joins; Circle only edits.
        case kJoinCode: begin_edit(Edit::Code, circle ? After::Nothing : After::JoinCode); break;
        case kHostIp: host(app); break;
        case kJoinIp: begin_edit(Edit::Address, circle ? After::Nothing : After::JoinAddress); break;
        case kName: begin_edit(Edit::Name); break;
        case kServer: begin_edit(Edit::Server); break;
        default: app.pop(); break;
        }
    }

    // Through the relay server: a session code, no port forward.
    void create(App& app) {
        std::string err;
        net::Session& ns = net::online();
        if (!ns.host_via(server_, name_, car_label(), u8(paint_), &err)) {
            status("Cannot create a session: " + err, true);
            return;
        }
        push_settings();
        announce_car();
        to_lobby(kCreate, 0);
        status("Creating the session...", false);
        remember(app);
    }

    void join_code(App& app) {
        std::string err;
        net::Session& ns = net::online();
        if (!ns.join_code(server_, code_, name_, car_label(), u8(paint_), &err)) {
            status("Cannot join: " + err, true);
            return;
        }
        announce_car();
        to_lobby(kJoinCode, kCar);
        status("Looking for session " + net::format_code(net::normalize_code(code_)) + "...", false);
        remember(app);
    }

    void host(App& app) {
        std::string err;
        net::Session& ns = net::online();
        if (!ns.host(port_, name_, car_label(), u8(paint_), &err)) {
            status("Cannot host: " + err, true);
            return;
        }
        push_settings();
        announce_car();
        to_lobby(kHostIp, 0);
        std::string where;
        for (const std::string& a : net::local_addresses()) where += (where.empty() ? "" : ", ") + a;
        status("Hosting on port " + std::to_string(ns.port()) + (where.empty() ? "" : " - LAN address " + where), false);
        remember(app);
    }

    void join(App& app) {
        std::string err;
        net::Session& ns = net::online();
        if (!ns.join(address_, name_, car_label(), u8(paint_), &err)) {
            status("Cannot join: " + err, true);
            return;
        }
        announce_car();
        to_lobby(kJoinIp, kCar);
        status("Connecting to " + address_ + "...", false);
        remember(app);
    }

    void to_lobby(int from, int row) {
        page_ = Page::Lobby, start_row_ = from;
        sel_ = row;
        pointer_.clear();
    }

    enum StartFooter { kFootChoose, kFootBack };

    void render_start(Ui& ui) {
        ui.header("ONLINE BATTLE", "i.LINK Battle over the internet or a LAN");
        const char* labels[kStartRows] = {"Create a session", "Join with a code", "Host by address (LAN / port forward)",
                                          "Join by address",  "Name",             "Relay server",
                                          "Back"};
        for (int i = 0; i < kStartRows; ++i) {
            float y = 46 + i * 24.0f + (i >= kName ? 10 : 0);
            bool f = i == sel_;
            if (f) ui.fill_round(40, y - 6, 560, 21, 4, colors::kHighlight);
            else if (hovering(pointer_, {40, y - 6, 560, 21})) ui.frame_round(40, y - 6, 560, 21, 4, 1, colors::kOutline);
            hits_.add({40, y - 6, 560, 21}, kHitRow, i);
            ui.text(56, y, labels[i], f ? colors::kAccent : colors::kText);
            std::string v;
            if (i == kCreate) v = server_.empty() ? "(set the relay server first)" : "you get a code to share";
            if (i == kJoinCode) v = code_.empty() ? "(enter the code)" : net::format_code(net::normalize_code(code_));
            if (i == kHostIp) v = "UDP port " + std::to_string(port_);
            if (i == kJoinIp) v = address_.empty() ? "(enter address)" : address_;
            if (i == kName) v = name_;
            if (i == kServer) v = server_.empty() ? "(none)" : server_;
            bool edit = editing_ && i == edit_row();
            if (edit) v = edit_target() + "_";
            if (!v.empty()) ui.text(330, y, Ui::clip(v, 260), edit ? colors::kAccent : colors::kDim);
        }
        const bool joins = after_edit_ != After::Nothing;
        if (editing_)
            ui.footer(joins ? "Type, Enter to join, Esc to cancel (pad: up/down letter, right add, left delete, A join)"
                            : "Type, Enter to finish, Esc to cancel (pad: up/down letter, right add, left delete, A done)");
        else
            footer_actions(ui, "Up/Down: choose", {{"Cross: choose", kFootChoose}, {"Triangle: back", kFootBack}}, hits_,
                           kHitFooter, pointer_);
        ui.text_center(Ui::kHeight - 62, "With a code nobody forwards ports: players connect directly when they can,",
                       colors::kDim);
        ui.text_center(Ui::kHeight - 50, "otherwise through the relay server.", colors::kDim);
    }

    // --- text entry ---

    enum class Edit { Address, Name, Code, Server } edit_ = Edit::Address;
    // What confirming the text does next (a join row's Cross: edit, then join).
    enum class After { Nothing, JoinCode, JoinAddress } after_edit_ = After::Nothing;
    bool editing_ = false;
    std::string edit_backup_;
    std::string& edit_target() {
        switch (edit_) {
        case Edit::Address: return address_;
        case Edit::Name: return name_;
        case Edit::Code: return code_;
        default: return server_;
        }
    }
    int edit_row() const {
        switch (edit_) {
        case Edit::Address: return kJoinIp;
        case Edit::Name: return kName;
        case Edit::Code: return kJoinCode;
        default: return kServer;
        }
    }

    void begin_edit(Edit which, After then = After::Nothing) {
        edit_ = which;
        after_edit_ = then;
        editing_ = true;
        edit_backup_ = edit_target();
        if (window_) SDL_StartTextInput(window_);
    }

    void end_edit(App& app, bool keep) {
        if (!keep) edit_target() = edit_backup_;
        if (edit_ == Edit::Name && name_.empty()) name_ = "Player";
        editing_ = false;
        if (window_) SDL_StopTextInput(window_);
        app.menu().suppress(); // the keys typed are no menu input
        remember(app);
        const After then = std::exchange(after_edit_, After::Nothing);
        if (!keep) return;
        if (then == After::JoinCode && !code_.empty()) {
            if (!server_.empty()) join_code(app);
            else if (edit_ != Edit::Server) begin_edit(Edit::Server, After::JoinCode); // then the relay server
        } else if (then == After::JoinAddress && !address_.empty()) {
            join(app);
        }
    }

    // --- lobby page ---

    enum Row { kCourse, kLaps, kGhost, kRules, kCar, kColour, kGearbox, kGo, kKick, kLeave, kRows };
    static constexpr float kRowY = 130, kRowH = 15;
    enum LobbyFooter { kFootLeave };

    // Host: the players that can be removed (everyone but us), and their warnings.
    std::vector<const net::Player*> kickable(const net::Session& ns) const {
        std::vector<const net::Player*> out;
        for (const net::Player& p : ns.players())
            if (p.id != 0) out.push_back(&p);
        return out;
    }
    static std::string warnings_of(const net::Session& ns, u8 id) {
        std::string t;
        int n = 0;
        for (const net::Session::FlagRecord& f : ns.flags())
            if (f.suspect == id) {
                ++n;
                if (t.size() < 80) t += (t.empty() ? "" : "; ") + std::string(net::flag_text(f.kind)) + " (seen by " +
                                        (f.reporter_name.empty() ? "?" : f.reporter_name) + ")";
            }
        return n ? std::to_string(n) + (n == 1 ? " warning: " : " warnings: ") + t : "";
    }
    static constexpr int kPowerSteps[] = {0, 200, 250, 300, 350, 400, 450, 500, 600, 700};
    static constexpr int kPowerCount = int(sizeof(kPowerSteps) / sizeof(kPowerSteps[0]));

    // The lobby's rules: the host's own, everyone else's from the host's settings; the course's
    // surface for the arcade classes.
    game::CarRules rules_of(const net::Session& ns) const {
        game::CarRules r = rules_;
        if (!ns.is_host()) {
            const net::Settings& st = ns.settings();
            r.allow_mods = st.allow_mods, r.stock_only = st.stock_only, r.max_ps = st.max_ps;
            r.car_class = st.car_class, r.category = st.category, r.drive = st.drive, r.aspiration = st.aspiration;
        }
        const std::string course = ns.is_host() ? course_label() : ns.course();
        for (const data::Course& c : db_.courses())
            if (c.label == course) r.dirt_course = c.surface == 2;
        return r;
    }

    // What the rules look at for car list entry `index`, built once per car list.
    const game::CarFacts& facts_at(int index) const {
        std::optional<game::CarFacts>& f = facts_[size_t(index)];
        if (!f) f = game::online_car_facts(db_, gt_data(), car_at(index, 0));
        return *f;
    }
    // Entry `index` against the lobby's rules ("" = allowed).
    std::string car_problem(const net::Session& ns, int index) const {
        if (index < 0 || size_t(index) >= cars_.size()) return "";
        std::string why;
        return game::car_meets_rules(car_at(index, 0), facts_at(index), rules_of(ns), &why) ? std::string() : why;
    }
    std::string rule_problem(const net::Session& ns) const { return car_problem(ns, car_sel_); }

    // Another player's car on this machine (game::online_car_here), by its descriptor, and why it is not a legal setup.
    struct Here {
        game::CarHere h = game::CarHere::Ok;
        std::string why;
    };
    const Here& car_here(const Bytes& info) const {
        auto it = here_.find(info);
        if (it != here_.end()) return it->second;
        Here h; // a descriptor this game cannot read: nothing to check
        if (std::optional<game::OnlineCar> oc = game::decode_online_car(info))
            h.h = game::online_car_here(
                db_, program_.get(), *oc,
                [this](const std::string& model) { return app_ && app_->vfs() && app_->vfs()->exists("cars/day/" + model); },
                &h.why);
        return here_[info] = std::move(h);
    }
    bool car_blocked(const net::Session& ns, const net::Player& p) const {
        const Bytes* info = int(p.id) == ns.my_id() ? nullptr : ns.car_info(p.id);
        return info && game::car_here_blocks(car_here(*info).h);
    }
    // Why this machine cannot race the lobby: no exact core here, or the first other player's car that cannot race here
    // ("Bob's car has no model on this disc", "Bob's car is not a legal setup: front spring rate out of range").
    std::string remote_car_problem(const net::Session& ns) const {
        if (!core_problem_.empty()) return core_problem_;
        for (const net::Player& p : ns.players())
            if (car_blocked(ns, p)) {
                const Here& h = car_here(*ns.car_info(p.id));
                return p.name + "'s car " + game::car_here_text(h.h) + (h.why.empty() ? "" : ": " + h.why);
            }
        return {};
    }

    // Ghost above this round trip (0 = always collide).
    static constexpr u16 kGhostMs[] = {0, 80, 100, 150, 200, 300};
    static constexpr int kGhostSteps = int(sizeof(kGhostMs) / sizeof(kGhostMs[0]));
    static std::string ghost_text(u16 ms) { return ms ? "above " + std::to_string(ms) + " ms round trip" : "never (always contact)"; }

    void push_settings() {
        net::Settings st;
        st.course = course_label();
        st.laps = laps_;
        st.allow_mods = rules_.allow_mods;
        st.ghost_ping_ms = ghost_ms_;
        st.stock_only = rules_.stock_only;
        st.max_ps = u16(rules_.max_ps);
        st.car_class = u8(rules_.car_class), st.category = u8(rules_.category);
        st.drive = u8(rules_.drive), st.aspiration = u8(rules_.aspiration);
        net::online().set_settings(st);
        if (page_ == Page::Cars && app_) {
            assign_classes();
            car_picker_.refresh(*app_);
        }
    }

    void update_lobby(App& app) {
        const MenuInput& m = app.menu();
        bool left = false;
        row_pointer(sel_, [&](int row, int h, bool cross) {
            if (row < 0) { // the footer: leave
                leave();
                left = true;
                return false;
            }
            Page before = page_;
            lobby_action(app, row, h, cross);
            return page_ == before;
        }, kRows);
        if (left || page_ != Page::Lobby) return;
        int before = sel_;
        sel_ = (sel_ + m.vertical() + kRows) % kRows;
        if (sel_ != before) kick_armed_ = false;
        if (m.pressed(PadButton::Triangle)) {
            leave();
            return;
        }
        int h = m.horizontal();
        if (m.pressed(PadButton::L1)) h = -10;
        if (m.pressed(PadButton::R1)) h = 10;
        bool cross = m.pressed(PadButton::Cross);
        if (h || cross) lobby_action(app, sel_, h, cross);
    }

    void leave() {
        net::online().close();
        to_start();
        status("Left the session", false);
    }

    // Left / Right (`h`, or an arrow) and Cross (or a click) on a lobby row.
    void lobby_action(App& app, int row, int h, bool cross) {
        net::Session& ns = net::online();
        const net::Player* me = ns.my_id() >= 0 ? ns.player(u8(ns.my_id())) : nullptr;
        bool ready = me && me->ready && !ns.is_host();
        switch (row) {
        case kCourse:
            if (!ns.is_host() || courses_.empty()) break;
            if (cross) { // the course page
                course_picker_.open(courses_, course_sel_, [this](const std::string& c) { return course_length(c); });
                page_ = Page::Courses;
                picker_sound(app, "ok");
                break;
            }
            if (!h) break;
            course_sel_ = (course_sel_ + h % int(courses_.size()) + int(courses_.size())) % int(courses_.size());
            push_settings();
            break;
        case kLaps:
            if (!ns.is_host() || !h) break;
            laps_ = std::clamp(laps_ + (h > 0 ? 1 : -1), 1, 99);
            push_settings();
            break;
        case kGhost: {
            if (!ns.is_host() || !h) break;
            int k = 0;
            for (int j = 0; j < kGhostSteps; ++j)
                if (kGhostMs[j] == ghost_ms_) k = j;
            ghost_ms_ = kGhostMs[std::clamp(k + (h > 0 ? 1 : -1), 0, kGhostSteps - 1)];
            push_settings();
            break;
        }
        case kRules: // the rules page (everyone sees it; the host changes it)
            if (!cross) break;
            page_ = Page::Rules;
            rules_sel_ = 0;
            pointer_.clear();
            picker_sound(app, "ok");
            break;
        case kCar:
            if (cars_.empty() || ready) break;
            if (cross) { // the car page
                open_car_picker(app);
                picker_sound(app, "ok");
                break;
            }
            if (!h) break;
            car_sel_ = (car_sel_ + h % int(cars_.size()) + int(cars_.size())) % int(cars_.size());
            paint_ = 0;
            announce_car();
            break;
        case kColour:
            if (!h || ready || garage_selected()) break; // a garage car keeps its own paint
            paint_ = (paint_ + (h > 0 ? 1 : -1) + paint_count()) % paint_count();
            announce_car();
            break;
        case kGearbox:
            if (h || cross) manual_ = !manual_;
            break;
        case kGo:
            if (!cross) break;
            if (ns.is_host() && ns.relay_wait().waiting) { // in the relay's queue: give up waiting
                ns.cancel_start();
                status("Stopped waiting for the relay server", false);
            } else if (ns.is_host()) {
                // Every car the host can rebuild against the rules (a mod car it lacks: its owner's check).
                std::string rule_breaker;
                for (const net::Player& p : ns.players())
                    if (const Bytes* info = ns.car_info(p.id))
                        if (auto oc = game::decode_online_car(*info)) {
                            std::string why;
                            if (!game::car_meets_rules(*oc, game::online_car_facts(db_, gt_data(), *oc), rules_of(ns), &why))
                                rule_breaker = (int(p.id) == ns.my_id() ? std::string("Your car") : p.name + "'s car") + ": " + why;
                        }
                // And every other player's car must race here as its owner races it.
                std::string not_here = remote_car_problem(ns);
                if (!rule_breaker.empty()) status(rule_breaker, true);
                else if (!not_here.empty()) status(not_here, true);
                else if (ns.all_ready()) ns.load();
                else status(ns.players().size() < 2 ? "Waiting for players to join" : "Not everyone is ready", true);
            } else if (ns.joined()) {
                std::string why = rule_problem(ns), not_here = remote_car_problem(ns);
                if (!ready && !why.empty()) status("Your car: " + why, true);
                else if (!ready && !not_here.empty()) status(not_here, true);
                else ns.set_ready(!ready);
            }
            break;
        case kKick: {
            if (!ns.is_host()) break;
            std::vector<const net::Player*> k = kickable(ns);
            if (k.empty()) break;
            if (h) {
                kick_sel_ = (kick_sel_ + (h > 0 ? 1 : -1) + int(k.size())) % int(k.size());
                kick_armed_ = false;
            }
            kick_sel_ = std::clamp(kick_sel_, 0, int(k.size()) - 1);
            if (cross) {
                const net::Player* p = k[size_t(kick_sel_)];
                if (!kick_armed_) {
                    kick_armed_ = true;
                    status("Press Cross again to remove " + p->name, true);
                } else {
                    std::string name = p->name;
                    ns.kick(p->id);
                    kick_armed_ = false;
                    status("Removed " + name, false);
                }
            }
            break;
        }
        case kLeave:
            if (cross) leave();
            return;
        default: break;
        }
        if (row != kGo) remember(app);
    }

    // A player's paint: the colour of paint index `paint` of database car `car` (nullopt = unknown).
    std::optional<u32> paint_rgb(App& app, const std::string& car, int paint) {
        const data::Car* c = db_.car(car);
        if (!c) return std::nullopt;
        Entry e{car, "", db_.model_of(*c), -1, false, {}, 0, {}, {}, 0, {}};
        std::vector<PaintInfo> p = car_picker_.paints(app, e);
        if (paint < 0 || size_t(paint) >= p.size()) return std::nullopt;
        return p[size_t(paint)].rgb;
    }

    void render_lobby(App& app, Ui& ui, const net::Session& ns) {
        std::string head = ns.via_server() ? (ns.is_host() ? "Hosting" : "Joined")
                           : ns.is_host() ? "Hosting - port " + std::to_string(ns.port())
                                          : "Joined";
        if (!ns.is_host() && ns.rtt_ms() > 0) head += " - ping " + std::to_string(int(ns.rtt_ms())) + " ms";
        ui.header("ONLINE BATTLE", head);
        // The session code, big, for the host to read out (a click copies it).
        if (ns.via_server()) {
            std::string code = net::format_code(ns.is_host() ? ns.code() : net::normalize_code(code_));
            if (code.empty()) ui.text_right(Ui::kWidth - 24, 30, "getting a code...", colors::kDim);
            else {
                int scale = Ui::text_width(code, 3) > 170 ? 2 : 3; // clear of the players' link column
                float w = Ui::text_width(code, scale);
                const UiRect r{Ui::kWidth - 28 - w, 26, w + 8, 18 + 8.0f * float(scale)};
                const bool hot = hovering(pointer_, r);
                if (hot) ui.fill_round(r.x, r.y, r.w, r.h, 4, colors::kHighlight);
                ui.text_right(Ui::kWidth - 24, 30, hot ? "click to copy" : "SESSION CODE", colors::kDim);
                ui.text_big(Ui::kWidth - 24 - w, 44, code, scale, colors::kAccent);
                hits_.add(r, kHitCode);
            }
        }
        // Players.
        ui.text(24, 30, "PLAYERS", colors::kDim);
        float y = 44;
        const game::GtData* gd = gt_data();
        for (const net::Player& p : ns.players()) {
            bool me = int(p.id) == ns.my_id();
            ui.text(24, y, Ui::clip(p.name, 110), me ? colors::kAccent : colors::kText);
            std::string car = p.car;
            for (const Entry& e : cars_)
                if (e.label == p.car && e.garage < 0 && e.arcade.empty()) car = e.name;
            if (const Bytes* info = ns.car_info(p.id))
                if (std::optional<game::OnlineCar> oc = game::decode_online_car(*info)) {
                    car = oc->name.empty() ? oc->car : oc->name;
                    if (oc->kind == game::OnlineCar::Kind::Garage) car = "Garage: " + car;
                    if (oc->kind == game::OnlineCar::Kind::Mod) car += db_.car(oc->car) ? " (mod)" : " (mod, not installed)";
                    if (oc->kind == game::OnlineCar::Kind::Arcade)
                        if (int k = gd ? game::arcade_class_of(*gd, oc->arcade) : 0)
                            car = std::string(game::arcade_class_name(k)) + ": " + car;
                }
            if (std::optional<u32> rgb = paint_rgb(app, p.car, p.paint)) draw_paint_chip(ui, 140, y, 8, 8, *rgb, false);
            ui.text(152, y, Ui::clip(car, 130), car_blocked(ns, p) ? colors::kBad : colors::kDim); // cannot race here
            if (ns.is_host())
                for (const net::Session::FlagRecord& f : ns.flags())
                    if (f.suspect == p.id) {
                        ui.text(118, y, "!", colors::kBad); // has warnings (details on "Remove a player")
                        break;
                    }
            const char* st = p.id == 0 ? "HOST" : p.ready ? "READY" : "...";
            ui.text(290, y, st, p.id == 0 || p.ready ? colors::kGood : colors::kDim);
            if (!me) {
                net::Link l = ns.link(p.id);
                if (l.known) {
                    std::string t = std::string(l.direct ? "direct " : l.relay ? "relay " : "via host ") +
                                    std::to_string(int(l.rtt_ms + 0.5)) + " ms";
                    u16 gms = ns.settings().ghost_ping_ms;
                    bool ghost = gms && l.rtt_ms > gms;
                    ui.text(350, y, t, ghost ? colors::kBad : l.direct ? colors::kGood : colors::kDim);
                }
            }
            y += 14;
        }
        if (!ns.joined()) ui.text(24, y, "Connecting...", colors::kDim);
        // Settings.
        const net::Player* meP = ns.my_id() >= 0 ? ns.player(u8(ns.my_id())) : nullptr;
        bool ready = meP && meP->ready && !ns.is_host();
        const bool host = ns.is_host();
        std::string course = host ? course_label() : ns.course();
        for (const PickerCourse& e : courses_)
            if (e.label == course) course = e.name;
        // The course's still at the right of the settings (the rows beside it are clipped).
        const SDL_FRect thumb{472, 124, 148, 111};
        auto row_right = [&](int r) { return kRowY + float(r) * kRowH - 3 < thumb.y + thumb.h ? thumb.x - 6 : 620.0f; };
        auto row = [&](int r, const std::string& label, const std::string& value, bool enabled, bool arrows,
                       float indent = 0, const std::string& hint = {}, Color hint_color = colors::kDim) {
            draw_row(ui, r, kRowY + float(r) * kRowH, r == sel_, label, value, enabled, arrows, 200, row_right(r), indent, hint,
                     hint_color);
        };
        if (!course.empty()) {
            std::string label = host ? course_label() : ns.course();
            ui.fill_round(thumb.x - 2, thumb.y - 2, thumb.w + 4, thumb.h + 4, 4,
                          host && hovering(pointer_, {thumb.x, thumb.y, thumb.w, thumb.h}) ? colors::kAccent : colors::kOutline);
            if (!art_.course_still(app, label, thumb, true)) {
                ui.fill(thumb.x, thumb.y, thumb.w, thumb.h, colors::kPanel);
                if (!art_.course_icon(app, label, SDL_FRect{thumb.x + 20, thumb.y + 20, thumb.w - 40, thumb.h - 40}))
                    ui.text_center_in(thumb.x, thumb.w, thumb.y + thumb.h / 2 - 4, Ui::clip(course, thumb.w - 8), colors::kDim);
            } else {
                art_.course_logo(app, label, SDL_FRect{thumb.x + 4, thumb.y + thumb.h - 34, 90, 30});
            }
            if (host) hits_.add({thumb.x, thumb.y, thumb.w, thumb.h}, kHitRow, kCourse); // the course page
        }
        row(kCourse, "Course", course, host, host, 0, host ? "(Cross: course page)" : "");
        row(kLaps, "Laps", std::to_string(host ? laps_ : ns.laps()), host, host);
        u16 gms = host ? ghost_ms_ : ns.settings().ghost_ping_ms;
        row(kGhost, "Ghost cars", ghost_text(gms), host, host);
        row(kRules, "Car rules", game::car_rules_summary(rules_of(ns)), true, false, 0,
            host ? "(Cross: set the rules)" : "(Cross: see the rules)");
        std::string problem = ns.joined() ? rule_problem(ns) : "";
        if (cars_.empty()) row(kCar, "Car", "-", !ready, false);
        else {
            // The car's strip icon (the arcade car page's) after "<".
            const Entry& e = cars_[size_t(car_sel_)];
            float ry = kRowY + float(kCar) * kRowH;
            const std::string icon = art_.has_car_icon(app, car_icon_name(e)) ? car_icon_name(e)
                                     : art_.has_car_icon(app, e.label)       ? e.label
                                                                              : std::string();
            std::string hint = !problem.empty() ? problem : ready ? "" : "(Cross: car page)";
            row(kCar, "Car", entry_name(e), !ready, !ready, icon.empty() ? 0.0f : 30.0f, hint,
                problem.empty() ? colors::kDim : colors::kBad);
            if (!icon.empty()) art_.car_icon(app, icon, SDL_FRect{ready ? 200.0f : 212.0f, ry - 3, 26, 14});
        }
        {
            std::vector<PaintInfo> pal = cars_.empty() ? std::vector<PaintInfo>{} : car_picker_.paints(app, cars_[size_t(car_sel_)]);
            int cur = garage_selected() ? 0 : std::clamp(paint_, 0, std::max(0, int(pal.size()) - 1));
            float ry = kRowY + float(kColour) * kRowH;
            bool arrows = !ready && !garage_selected();
            std::string pname = pal.empty() ? "" : pal[size_t(cur)].name;
            if (garage_selected()) row(kColour, "Colour", (pname.empty() ? "" : pname + "  ") + "(the garage car's own)", false, false, 26);
            else
                row(kColour, "Colour",
                    (pname.empty() ? "" : pname + "  ") + std::to_string(paint_ + 1) + " of " + std::to_string(paint_count()),
                    !ready, arrows, 26);
            if (!pal.empty()) draw_paint_chip(ui, arrows ? 212 : 200, ry - 1, 18, 9, pal[size_t(cur)].rgb, false);
        }
        row(kGearbox, "Transmission", manual_ ? "Manual" : "Automatic", true, true);
        // "Relay busy: 2nd in line, about 3 min" while the race waits for the relay's link slots.
        std::string wait = net::relay_wait_text(ns.relay_wait());
        const std::string not_here = ns.joined() ? remote_car_problem(ns) : "";
        if (!wait.empty())
            row(kGo, host ? "Cancel start" : ready ? "Ready - waiting for the host" : "Ready",
                wait + " - the race starts by itself", true, false);
        else if (!not_here.empty()) // another player's car cannot race here
            row(kGo, host ? "Start race" : "Ready", "", true, false, 0, not_here, colors::kBad);
        else
            row(kGo, host ? "Start race" : ready ? "Ready - waiting for the host" : "Ready",
                host ? (ns.all_ready() ? "everyone is ready" : "waiting for everyone to be ready") : "", true, false);
        if (host) {
            std::vector<const net::Player*> k = kickable(ns);
            if (k.empty()) row(kKick, "Remove a player", "(nobody else here)", false, false);
            else {
                const net::Player* p = k[size_t(std::clamp(kick_sel_, 0, int(k.size()) - 1))];
                row(kKick, kick_armed_ ? "Remove? Cross again" : "Remove a player", p->name, true, true, 0, warnings_of(ns, p->id));
            }
        } else {
            row(kKick, "Remove a player", "(the host's choice)", false, false);
        }
        row(kLeave, "Leave", "", true, false);
        footer_actions(ui, "Up/Down: choose   Left/Right: change   Cross: confirm", {{"Triangle: leave", kFootLeave}}, hits_,
                       kHitFooter, pointer_);
    }

    // --- rules page ---

    enum RulesRow { kRMods, kRTuning, kRClass, kRCategory, kRDrive, kRAspiration, kRPower, kRReset, kRBack, kRRows };
    static constexpr float kRulesY = 52, kRulesH = 18;
    enum RulesFooter { kFootRulesBack };
    int rules_sel_ = 0;

    void update_rules(App& app) {
        const MenuInput& m = app.menu();
        bool back = false; // the pointer's right button too (it only leaves this page)
        row_pointer(rules_sel_, [&](int row, int h, bool cross) {
            if (row < 0) row = kRBack, cross = true; // the footer: back
            rules_action(app, row, h, cross);
            return page_ == Page::Rules;
        }, kRRows, &back);
        if (page_ != Page::Rules) return;
        rules_sel_ = (rules_sel_ + m.vertical() + kRRows) % kRRows;
        if (back || m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) return rules_action(app, kRBack, 0, true);
        int h = m.horizontal();
        bool cross = m.pressed(PadButton::Cross);
        if (h || cross) rules_action(app, rules_sel_, h, cross);
    }

    // Left / Right (or an arrow) and Cross (or a click) on a rules row; only the host changes them.
    void rules_action(App& app, int row, int h, bool cross) {
        net::Session& ns = net::online();
        if (row == kRBack) {
            if (!cross) return;
            page_ = Page::Lobby;
            sel_ = kRules;
            pointer_.clear();
            picker_sound(app, "cancel");
            return;
        }
        if (!ns.is_host() || (!h && !cross)) return;
        const int step = h ? (h > 0 ? 1 : -1) : 1; // Cross steps forward
        auto cycle = [&](int& v, int n) { v = (v + step + n + 1) % (n + 1); };
        game::CarRules& r = rules_;
        switch (row) {
        case kRMods: r.allow_mods = !r.allow_mods; break;
        case kRTuning: r.stock_only = !r.stock_only; break;
        case kRClass: cycle(r.car_class, game::kArcadeClasses); break;
        case kRCategory: cycle(r.category, game::kCategoryRules); break;
        case kRDrive: cycle(r.drive, game::kDriveRules); break;
        case kRAspiration: cycle(r.aspiration, game::kAspirationRules); break;
        case kRPower: {
            int k = 0;
            for (int j = 0; j < kPowerCount; ++j)
                if (kPowerSteps[j] == r.max_ps) k = j;
            r.max_ps = kPowerSteps[std::clamp(k + step, 0, kPowerCount - 1)];
            break;
        }
        case kRReset:
            if (!cross) return;
            r = game::CarRules{};
            break;
        default: return;
        }
        picker_sound(app, "cursor");
        push_settings();
        remember(app);
    }

    void render_rules(Ui& ui, const net::Session& ns) {
        const bool host = ns.is_host();
        ui.fill(0, 20, Ui::kWidth, Ui::kHeight - 36, colors::kBackground);
        ui.header("CAR RULES", host ? "Which cars may race in your lobby" : "The host sets them");
        const game::CarRules r = rules_of(ns);
        ui.text(24, 30, host ? "Every player's car must meet these rules before Ready and the start."
                             : "Your car must meet these rules before you can be ready.",
                colors::kDim);
        const char* labels[kRRows] = {"Mod cars", "Tuned cars", "Class", "Type", "Drivetrain", "Aspiration", "Power limit",
                                      "Any car", "Back to the lobby"};
        auto value = [&](int row) -> std::string {
            switch (row) {
            case kRMods: return r.allow_mods ? "allowed (others are offered the download)" : "not allowed";
            case kRTuning: return r.stock_only ? "stock cars only (no tuned garage cars)" : "allowed";
            case kRClass: return r.car_class ? game::arcade_class_name(r.car_class) + std::string(" only") : "any";
            case kRCategory: return game::category_rule_name(r.category) + std::string(r.category ? " only" : "");
            case kRDrive: return game::drive_rule_name(r.drive) + std::string(r.drive ? " only" : "");
            case kRAspiration: return game::aspiration_rule_name(r.aspiration) + std::string(r.aspiration ? " only" : "");
            case kRPower: return r.max_ps ? "at most " + std::to_string(r.max_ps) + " PS" : "no limit";
            case kRReset: return host ? "(Cross: clear every rule)" : "";
            default: return "";
            }
        };
        static const char* const kHelp[kRRows] = {
            "Cars added by mods. Players who lack one are offered it.",
            "GT Mode garage cars with parts or settings off the stock car's.",
            "",
            "Production cars, or the race and rally cars (as GT3's own events tell them apart).",
            "The car's drivetrain.",
            "NA engines, or turbocharged ones.",
            "Peak power at the crank, from the car as it is built (garage parts and engine wear count).",
            "",
            ""};
        for (int i = 0; i < kRRows; ++i) {
            const bool setting = i < kRReset;
            const float y = kRulesY + float(i) * kRulesH + (i >= kRReset ? 6 : 0);
            draw_row(ui, i, y, i == rules_sel_, labels[i], value(i), host || i == kRBack, host && setting, 160, 620);
        }
        const float y = kRulesY + float(kRRows) * kRulesH + 14;
        if (kHelp[rules_sel_][0]) ui.text(36, y, Ui::clip(kHelp[rules_sel_], 580), colors::kDim);
        if (r.dirt_course)
            ui.text(36, y + 14, "A dirt course: of the classes, only the rally cars (as the arcade's class page).", colors::kDim);
        // Your cars under these rules.
        if (!cars_.empty() && ns.joined()) {
            int allowed = 0;
            for (size_t i = 0; i < cars_.size(); ++i) allowed += car_problem(ns, int(i)).empty();
            std::string why = rule_problem(ns);
            ui.text(36, y + 32, std::to_string(allowed) + " of your " + std::to_string(cars_.size()) + " cars are allowed.",
                    colors::kText);
            ui.text(36, y + 46, "Your car: " + Ui::clip(entry_name(cars_[size_t(car_sel_)]), 300) +
                                    (why.empty() ? std::string(" - allowed") : " - " + why),
                    why.empty() ? colors::kGood : colors::kBad);
        }
        footer_actions(ui, host ? "Up/Down: choose   Left/Right: change" : "Up/Down: choose",
                       {{"Triangle: back to the lobby", kFootRulesBack}}, hits_, kHitFooter, pointer_);
    }

    // --- pickers ---

    void open_car_picker(App& app) {
        assign_classes();
        car_picker_.open(app, art_, db_, cars_, car_sel_, paint_, [this](int i, int paint) { return car_note(i, paint); },
                         [this](int i) { return car_problem(net::online(), i).empty(); });
        page_ = Page::Cars;
    }

    void update_car_picker(App& app) {
        CarPicker::Result r = car_picker_.update(app);
        if (r == CarPicker::Result::None) return;
        page_ = Page::Lobby;
        pointer_.clear();
        remember(app); // the page's view and filter
        const net::Player* me = net::online().my_id() >= 0 ? net::online().player(u8(net::online().my_id())) : nullptr;
        if (r != CarPicker::Result::Chosen || (me && me->ready && !net::online().is_host())) return;
        car_sel_ = car_picker_.car();
        paint_ = cars_[size_t(car_sel_)].garage >= 0 ? 0 : car_picker_.paint();
        announce_car();
        remember(app);
    }

    void update_course_picker(App& app) {
        CoursePicker::Result r = course_picker_.update(app);
        if (r == CoursePicker::Result::None) return;
        page_ = Page::Lobby;
        pointer_.clear();
        if (r != CoursePicker::Result::Chosen || !net::online().is_host()) return;
        course_sel_ = course_picker_.course();
        push_settings();
        remember(app);
    }

    // A car on the car page against the lobby's rules ("" = fine).
    std::string car_note(int index, int) const {
        const net::Session& ns = net::online();
        if (!ns.joined()) return "";
        std::string why = car_problem(ns, index);
        return why.empty() ? "" : "Not allowed here: " + why;
    }

    // A course's length in metres from the arcade course lists (0 = unknown).
    u32 course_length(const std::string& label) const {
        if (game::GtModeSession* gs = game::gt_mode_session())
            for (const auto& [game_name, list] : gs->gt().data().arcade_courses)
                for (const game::GtData::ArcadeCourse& c : list)
                    if (c.course == label && c.length_m) return c.length_m;
        return 0;
    }

    // --- race ---

    void start_race(App& app, const net::Event& e) {
        if (!app.vfs()) return;
        OnlineLaunch l;
        l.course = e.course;
        l.laps = e.laps;
        game::OnlineCar oc = current_car();
        l.car = oc.car;
        l.paint = oc.paint;
        l.car_info = game::encode_online_car(oc);
        l.manual = manual_;
        LOG_INFO("Online: race on %s, %d laps, %zu cars", l.course.c_str(), l.laps, e.grid.size());
        in_race_ = true;
        page_ = Page::Lobby; // a picker or the rules page left open closes for the race
        app.push(make_online_race_screen(l));
    }

    // --- scripted runs ---

    void run_script(App& app) {
        net::Session& ns = net::online();
        if (!ns.active() && !script_connected_) {
            if (!script_.server.empty()) server_ = script_.server;
            if (script_.host && !script_.server.empty()) {
                script_connected_ = true;
                create(app);
            } else if (script_.host) {
                script_connected_ = true;
                host(app);
            } else if (!script_.code.empty() || !script_.codefile.empty()) {
                std::string code = script_.code;
                if (code.empty()) { // wait for the host's code file
                    if (FILE* f = std::fopen(script_.codefile.c_str(), "r")) {
                        char buf[16] = {};
                        if (std::fgets(buf, sizeof buf, f)) code = buf;
                        std::fclose(f);
                    }
                    while (!code.empty() && (code.back() == '\n' || code.back() == '\r')) code.pop_back();
                    if (code.empty()) return;
                }
                script_connected_ = true;
                code_ = code;
                join_code(app);
            } else if (!script_.join.empty()) {
                script_connected_ = true;
                address_ = script_.join;
                join(app);
            }
            return;
        }
        if (ns.is_host() && !ns.code().empty() && !script_.codefile.empty() && !script_code_written_) {
            script_code_written_ = true;
            if (FILE* f = std::fopen(script_.codefile.c_str(), "w")) std::fprintf(f, "%s\n", ns.code().c_str()), std::fclose(f);
        }
        if (!ns.joined()) return;
        if (!script_.page.empty()) { // a page for screenshots
            if (!script_page_opened_ && page_ == Page::Lobby) {
                script_page_opened_ = true;
                if (script_.page == "cars" || script_.page == "makers" || script_.page == "classes") {
                    open_car_picker(app);
                    if (script_.page != "cars") {
                        car_picker_.set_by_class(script_.page == "classes");
                        car_picker_.refresh(app);
                        car_picker_.show_groups();
                    }
                } else if (script_.page == "courses") {
                    course_picker_.open(courses_, course_sel_, [this](const std::string& c) { return course_length(c); });
                    page_ = Page::Courses;
                } else if (script_.page == "rules") {
                    page_ = Page::Rules;
                    rules_sel_ = kRClass;
                }
            }
            return;
        }
        const net::Player* me = ns.player(u8(ns.my_id()));
        if (!ns.is_host() && me && !me->ready && ns.in_lobby() && prompts_.empty() && downloading_.empty() &&
            remote_car_problem(ns).empty())
            ns.set_ready(true);
        if (ns.is_host() && !script_started_ && int(ns.players().size()) >= script_.players && ns.all_ready()) {
            script_started_ = true;
            ns.load();
        }
    }

    App* app_ = nullptr;
    SDL_Window* window_ = nullptr;
    bool init_ = false, in_race_ = false;
    std::string core_problem_; // this machine cannot run online races (init)
    std::shared_ptr<const sim::exact::ProgramData> program_; // this disc's program tables (the cars' digests)
    Script script_;
    bool script_connected_ = false, script_started_ = false, script_code_written_ = false, script_page_opened_ = false;
    data::CarDatabase db_;
    race::CarColors colors_;
    std::vector<Entry> cars_;
    mutable std::vector<std::optional<game::CarFacts>> facts_; // per cars_ entry, built on use
    mutable std::map<Bytes, Here> here_;                       // other players' cars, by descriptor
    std::vector<PickerCourse> courses_;
    PickerArt art_;
    CarPicker car_picker_;
    CoursePicker course_picker_;
    PointerInput pointer_;
    HitList hits_;
    Page page_ = Page::Start;
    int sel_ = 0;
    std::string name_, address_, server_, code_;
    u16 port_ = net::kDefaultPort;
    int car_sel_ = 0, course_sel_ = 0, laps_ = 2, paint_ = 0;
    bool manual_ = false;
    struct OwnPackage {
        std::string hash; // empty: the car cannot be shared
        u32 size = 0;
    };
    std::map<std::string, OwnPackage> own_packages_; // by car label
    struct Prompt {
        u8 player = 0;
        std::string name;
        game::OnlineCar car;
    };
    std::deque<Prompt> prompts_;
    std::set<std::string> asked_; // package hashes already offered to the user
    struct Download {
        std::string name, label, from;
    };
    std::map<std::string, Download> downloading_; // by package hash
    game::CarRules rules_; // the host's rules (as set here; without dirt_course)
    u16 ghost_ms_ = 150;
    int kick_sel_ = 0;
    bool kick_armed_ = false;
    std::string status_;
    bool status_bad_ = false;
};

} // namespace

std::unique_ptr<Screen> make_online_lobby_screen() { return std::make_unique<OnlineScreen>(); }

GT3R_REGISTER_PLAYER_SCREEN(online, "online", "Online battle", "Race other players over a LAN or the internet (i.LINK Battle).", 2,
                     [] { return make_online_lobby_screen(); });

} // namespace gt3r
