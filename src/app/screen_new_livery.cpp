// New livery (docs/MODDING.md "Liveries"): the car picker (app/online_pickers.h) chooses a car and the
// paint to paint over; the livery template of that car (race/livery_template.h) is written from this
// disc (a mod car's from its own model) into the mod "my_liveries" as liveries/<car>/, so it is installed at once (an untouched
// template changes nothing on the car) and listed with the car's paints. Then the folder opens in
// the system's file manager for painting; the game picks the changes up the next time the car loads
// (after the Mods screen is opened again, or the game restarts).

#include "app/app.h"
#include "app/online_pickers.h"
#include "app/screen_registry.h"
#include "base/file.h"
#include "base/log.h"
#include "formats/png_writer.h"
#include "formats/text/charset.h"
#include "game/data/car_database.h"
#include "platform/user_files.h"
#include "race/custom_paints.h"
#include "race/livery_template.h"

#include <algorithm>
#include <cctype>

namespace gt3r {

namespace {

constexpr const char* kModId = "my_liveries";

std::string ascii(const std::string& s) { return text::game_text_to_utf8(s, nullptr); }

class NewLiveryScreen : public Screen {
public:
    TouchLayout touch_layout() const override { return done_ ? TouchLayout::None : TouchLayout::Menu; }
    bool adaptive_ui() const override { return done_; }
    bool clickable(float x, float y) const override { return done_ ? hits_.at(x, y) != nullptr : picker_.clickable(x, y); }
    void on_event(App&, const SDL_Event& e) override {
        if (done_) pointer_.event(e);
        else picker_.event(e);
    }

    void update(App& app, double) override {
        if (!init_) init(app);
        if (!error_.empty() && !done_) {
            if (app.menu().pressed(PadButton::Triangle) || app.menu().pressed(PadButton::Circle)) app.pop();
            return;
        }
        if (!done_) {
            const CarPicker::Result r = picker_.update(app);
            if (r == CarPicker::Result::Cancelled) app.pop();
            else if (r == CarPicker::Result::Chosen) write(app);
            return;
        }
        auto act = [&](int which) {
            if (which == kOpen) platform::open_user_folder(folder_, app.data_dir());
            else if (which == kAnother) done_ = false, picker_.show_groups(), message_.clear();
            else app.pop();
        };
        for (const PointerInput::Action& a : pointer_.take())
            if (a.kind == PointerInput::Action::Kind::Click)
                if (const HitArea* h = hits_.at(a.x, a.y)) act(h->arg);
        const MenuInput& m = app.menu();
        if (m.pressed(PadButton::Cross) && !failed_) act(kOpen);
        else if (m.pressed(PadButton::Square)) act(kAnother);
        else if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) act(kBack);
    }

    void render(App& app, Ui& ui) override {
        if (!init_) init(app);
        hits_.clear();
        if (!done_ && error_.empty()) {
            picker_.render(app, ui);
            return;
        }
        ui.clear();
        ui.header("NEW LIVERY", "");
        if (!error_.empty() && !done_) {
            ui.text_center(150, error_, colors::kBad);
            footer_actions(ui, "", {{"{Triangle}: back", kBack}}, hits_, 0, pointer_);
            return;
        }
        const UiRect& S = ui.safe();
        float y = S.y + 50;
        for (const std::string& line : message_) {
            ui.text(S.x + 20, y, Ui::clip(line, S.w - 40), failed_ ? colors::kBad : colors::kText);
            y += 14;
        }
        std::vector<FooterAction> acts;
        if (!failed_) acts.push_back({"{Cross}: open folder", kOpen});
        acts.push_back({"{Square}: another car", kAnother});
        acts.push_back({"{Triangle}: back", kBack});
        footer_actions(ui, "", acts, hits_, 0, pointer_);
    }

private:
    enum Act { kOpen, kAnother, kBack };

    void init(App& app) {
        init_ = true;
        if (!app.vfs()) {
            error_ = "No disc loaded";
            return;
        }
        if (!db_.load(*app.vfs())) {
            error_ = db_.player_error();
            return;
        }
        for (const data::Car& c : db_.cars()) {
            const std::string model = db_.model_of(c);
            if (model.empty() || !app.vfs()->exists("cars/day/" + model)) continue;
            cars_.push_back({c.label, ascii(c.name.empty() ? c.label : c.name), model, -1, db_.mod_car(c.label) != nullptr, c.maker, 0, {}, {}, 0, {}});
        }
        std::sort(cars_.begin(), cars_.end(), [](const PickerCar& a, const PickerCar& b) { return a.name < b.name; });
        if (cars_.empty()) {
            error_ = "No cars on this disc";
            return;
        }
        picker_.open(app, art_, db_, cars_, 0, 0, [](int, int) { return std::string(); });
        picker_.show_groups();
    }

    // The chosen car's template, into mods/my_liveries/liveries/<car>[-n]/.
    void write(App& app) {
        done_ = true;
        failed_ = false;
        message_.clear();
        const PickerCar& c = cars_[size_t(picker_.car())];
        int paint = picker_.paint();
        std::string colour;
        // A custom colour or livery from the list: the template on the disc paint under it (a colour stays in the ini).
        const std::string key = race::paint_key(db_, c.label, c.model);
        const std::shared_ptr<const race::CustomPaints> custom = race::custom_paints();
        if (const race::CustomPaint* cp = custom->at(key, paint)) {
            paint = cp->finish;
            if (cp->colour) colour = race::rgb_text(cp->rgb);
        }
        std::string err;
        std::optional<race::LiveryTemplate> t = race::make_livery_template(*app.vfs(), db_, key, paint, 4, &err);
        if (!t) {
            fail(err);
            return;
        }
        const std::string mod = app.mods_dir() + "/" + kModId;
        if (!host_path_exists(mod + "/mod.ini")) {
            host_make_dir(mod);
            const std::string ini = "[mod]\nname = My liveries\ndescription = Liveries made with New livery\n";
            if (!write_host_file(mod + "/mod.ini", ByteSpan(reinterpret_cast<const u8*>(ini.data()), ini.size()))) {
                fail("Cannot write to " + mod);
                return;
            }
        }
        std::string base;
        for (char ch : c.label) base += std::isalnum(static_cast<unsigned char>(ch)) ? ch : '_';
        folder_ = mod + "/liveries/" + base;
        for (int n = 2; host_path_exists(folder_); ++n) folder_ = mod + "/liveries/" + base + "-" + std::to_string(n);
        if (!host_make_dir(folder_ + "/guides")) {
            fail("Cannot write to " + folder_);
            return;
        }
        std::string ini = t->ini;
        const std::string name = c.name + " livery";
        if (size_t p = ini.find("name = My livery"); p != std::string::npos) ini.replace(p, 16, "name = " + name);
        if (!colour.empty())
            if (size_t p = ini.find("; color = #1E3A8A"); p != std::string::npos) ini.replace(p, 17, "color = " + colour);
        bool ok = write_host_file(folder_ + "/livery.ini", ByteSpan(reinterpret_cast<const u8*>(ini.data()), ini.size()));
        for (const race::LiveryTemplate::Picture& p : t->canvases)
            ok = ok && write_host_file(folder_ + "/" + p.file, encode_png(p.width, p.height, p.rgba));
        for (const race::LiveryTemplate::Picture& p : t->guides)
            ok = ok && write_host_file(folder_ + "/guides/" + p.file, encode_png(p.width, p.height, p.rgba));
        if (!ok) {
            fail("Cannot write the template to " + folder_);
            return;
        }
        app.rescan_mods(); // (installed: an untouched template changes nothing)
        LOG_INFO("New livery: %s (%s, paint %d) in %s", c.label.c_str(), key.c_str(), paint, folder_.c_str());
        message_ = {"The template for the " + c.name + " is in", folder_, "",
                    "Paint over the body-... pictures (any editor, this size or larger), keeping their names.",
                    "guides/ shows which part of the car is where: blue left, orange right, green top,",
                    "red front, purple rear. Some textures serve both sides of the car, mirrored.",
                    "It is listed with the car's paints as \"" + name + "\"; open Mods again to see changes."};
    }

    void fail(const std::string& why) {
        failed_ = true;
        message_ = {"The template could not be made.", why};
        LOG_WARN("New livery: %s", why.c_str());
    }

    bool init_ = false, done_ = false, failed_ = false;
    std::string error_, folder_;
    std::vector<std::string> message_;
    data::CarDatabase db_;
    std::vector<PickerCar> cars_;
    PickerArt art_;
    CarPicker picker_;
    PointerInput pointer_;
    HitList hits_;
};

} // namespace

std::unique_ptr<Screen> make_new_livery_screen() { return std::make_unique<NewLiveryScreen>(); }

} // namespace gt3r
