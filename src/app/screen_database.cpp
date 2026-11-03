// Car database browser: GT3's car catalogue (the disc region's database/paramdb*.db), with
// specs, the engine torque curve, upgrade parts and the race events.

#include "app/app.h"
#include "app/screen_registry.h"
#include "formats/text/charset.h"
#include "game/data/car_database.h"
#include "game/gt_mode.h"
#include "menu/flow/menu_set.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <memory>

namespace gt3r {

namespace {

using data::CarDatabase;
using data::TableId;

std::string fmt(const char* f, ...)
#if defined(__MINGW32__)
    __attribute__((format(gnu_printf, 1, 2)))
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

std::string clip(std::string s, size_t n) {
    if (s.size() > n) s = s.substr(0, n - 2) + "..";
    return s;
}

class DatabaseScreen : public Screen {
public:
    void update(App& app, double) override {
        if (!loaded_) load(app);
        const MenuInput& m = app.menu();
        if (m.pressed(PadButton::Triangle)) {
            app.pop();
            return;
        }
        if (!gt_) return; // no data: only the error page
        if (m.pressed(PadButton::Square)) {
            mode_ = mode_ == Mode::Cars ? Mode::Events : Mode::Cars;
        }
        int n = mode_ == Mode::Cars ? int(order_.size()) : int(db().events().size());
        int& sel = mode_ == Mode::Cars ? car_sel_ : event_sel_;
        if (n) {
            int step = m.held(PadButton::R1) || m.held(PadButton::L1) ? 10 : 1;
            sel = std::clamp(sel + m.vertical() * step, 0, n - 1);
        }
        for (const XrList::Hit& h : {xr_list_.take_hover(), xr_list_.take_click()}) // the VR pointer
            if (h.row >= 0 && h.row < n) sel = h.row;
        if (mode_ == Mode::Cars && m.horizontal()) page_ = (page_ + 3 + m.horizontal()) % 3;
        if (mode_ == Mode::Cars && m.pressed(PadButton::Circle)) {
            sort_by_power_ = !sort_by_power_;
            sort();
        }
    }

    void on_event(App&, const SDL_Event& e) override { xr_list_.event(e); }

    void render(App&, Ui& ui) override {
        xr_list_.clear();
        if (!error_.empty() || !gt_) {
            ui.header("CAR DATABASE", "");
            ui.text_center(150, error_, colors::kBad);
            ui.footer("Triangle: back");
            return;
        }
        if (mode_ == Mode::Cars) render_cars(ui);
        else render_events(ui);
    }

private:
    enum class Mode { Cars, Events };
    static constexpr int kRows = 26;

    void load(App& app) {
        loaded_ = true;
        if (!app.vfs()) {
            error_ = "No disc loaded";
            return;
        }
        if (auto jis = app.vfs()->read("font/jis2uni.dat")) {
            try {
                jis_ = text::JisTable::parse(*jis);
            } catch (const std::exception&) {
            }
        }
        // The GT Mode data (the region's database and game config): prices and prizes as the
        // game shows them, through the same rules as the dealers and races (GtMode).
        auto data = std::make_shared<game::GtData>();
        if (!data->load(menu::flow::vfs_reader(*app.vfs()))) {
            error_ = data->error();
            return;
        }
        gt_ = std::make_unique<game::GtMode>(data);
        for (size_t i = 0; i < db().cars().size(); ++i) {
            const data::Car& c = db().cars()[i];
            names_.push_back(ascii(c.latin_name)); // this screen's font has no kana
            if (c.label.rfind("sample", 0) != 0) order_.push_back(i); // skip placeholder rows
        }
        sort();
        car_sel_ = 0;
    }

    // Game text (ASCII + EUC-JP) -> printable ASCII for the debug font.
    std::string ascii(const std::string& s) const {
        std::u32string u = text::decode_game_text(ByteSpan(reinterpret_cast<const u8*>(s.data()), s.size()),
                                                  jis_.empty() ? nullptr : &jis_);
        std::string out;
        for (char32_t c : u) {
            if (c >= 0x20 && c < 0x7F) out += char(c);
            else if (c >= 0xFF01 && c <= 0xFF5E) out += char(c - 0xFEE0);
            else if (c == 0x30FB || c == 0x2010 || c == 0x2212 || c == 0x2015) out += '-';
            else if (c == 0x3000) out += ' ';
            else out += '?';
        }
        return out;
    }

    void sort() {
        size_t keep = order_.empty() ? 0 : order_[std::min<size_t>(car_sel_, order_.size() - 1)];
        std::stable_sort(order_.begin(), order_.end(), [&](size_t a, size_t b) {
            const data::Car& x = db().cars()[a];
            const data::Car& y = db().cars()[b];
            if (sort_by_power_) {
                auto ex = db().engine(x.parts[6]), ey = db().engine(y.parts[6]);
                return (ex ? ex->power_ps : 0) > (ey ? ey->power_ps : 0);
            }
            if (x.maker != y.maker) return x.maker < y.maker;
            return names_[a] < names_[b];
        });
        auto it = std::find(order_.begin(), order_.end(), keep);
        car_sel_ = it == order_.end() ? 0 : int(it - order_.begin());
    }

    void render_cars(Ui& ui) {
        ui.header("CAR DATABASE", fmt("%zu cars  %s", order_.size(), sort_by_power_ ? "by power" : "by maker"));
        int n = int(order_.size());
        int first = xr_list_.first(0, car_sel_, kRows, ui.list_window(n, car_sel_, kRows));
        xr_list_.rows(0, first, std::min(kRows, n - first), 4, 240, 24, 12);
        for (int i = 0; i < kRows && first + i < n; ++i) {
            int idx = first + i;
            const data::Car& c = db().cars()[order_[idx]];
            float y = 26 + i * 12.0f;
            if (idx == car_sel_) ui.fill(4, y - 2, 236, 12, colors::kHighlight);
            ui.text(8, y, clip(ascii(c.maker), 8), colors::kDim);
            ui.text(80, y, clip(names_[order_[idx]], 20), idx == car_sel_ ? colors::kAccent : colors::kText);
        }
        if (n == 0) return;
        const data::Car& c = db().cars()[order_[car_sel_]];
        float x = 250;
        ui.fill(x - 6, 22, Ui::kWidth - x, Ui::kHeight - 46, colors::kPanel);
        ui.text(x, 26, clip(names_[order_[car_sel_]], 48), colors::kAccent);
        auto s = db().summarize(c);
        ui.text(x, 38, fmt("%s  %u  %s  model %s", clip(ascii(c.maker), 14).c_str(), s.year, c.label.c_str(),
                           s.model.c_str()),
                colors::kDim);
        const char* pages[] = {"SPECS", "TORQUE CURVE", "UPGRADES"};
        ui.text_right(Ui::kWidth - 8, 26, fmt("< %s >", pages[page_]), colors::kDim);
        if (page_ == 0) render_specs(ui, x, c, s);
        else if (page_ == 1) render_curve(ui, x, c);
        else render_parts(ui, x, c);
        ui.footer("Up/Down: car  Left/Right: page  Circle: sort  Square: events  Triangle: back");
    }

    void render_specs(Ui& ui, float x, const data::Car& c, const CarDatabase::Summary& s) {
        float y = 56;
        auto row = [&](const char* k, const std::string& v) {
            ui.text(x, y, k, colors::kDim);
            ui.text(x + 104, y, v);
            y += 12;
        };
        const std::string price = game::format_credits(gt_->car_price(c.label)) + " Cr";
        row("Price", gt_->car_flags(c.label) & 1 ? price + " (not for sale)" : price);
        row("Engine", fmt("%s %s %s", s.displacement.c_str(), s.layout.c_str(), s.aspiration.c_str()));
        row("Max power", fmt("%u PS @ %s", s.power_ps, s.power_rpm.c_str()));
        row("Max torque", fmt("%.1f kgf-m @ %s", s.torque_kgfm, s.torque_rpm.c_str()));
        row("Drivetrain", s.drive);
        row("Weight", fmt("%u kg  (front %u%%)", s.weight_kg, s.front_weight_pct));
        if (s.weight_kg) row("Power/weight", fmt("%.2f kg/PS", s.power_ps ? double(s.weight_kg) / s.power_ps : 0.0));
        row("Dimensions", fmt("%u x %u x %u mm", s.length_mm, s.width_mm, s.height_mm));
        row("Wheelbase", fmt("%u mm", s.wheelbase_mm));
        if (auto b = db().body(c.parts[5])) row("Tread F/R", fmt("%u / %u mm", b->front_tread_mm, b->rear_tread_mm));
        row("Gearbox", fmt("%u-speed, final %.3f", s.gears, s.final_drive));
        std::string ratios;
        for (float r : s.ratios) ratios += fmt("%.3f ", r);
        row("Ratios", ratios);
        row("Tires F", s.front_tire);
        row("Tires R", s.rear_tire);
        y += 6;
        ui.text(x, y, "Default parts", colors::kDim);
        y += 12;
        std::string line;
        for (u64 id : c.parts) {
            if (!id) continue;
            std::string l = db().label(id);
            if (l.size() < 3 || l[2] != '_') continue;
            std::string p = l.substr(0, 2);
            if (line.size() + p.size() + 1 > 46) {
                ui.text(x, y, line);
                y += 12;
                line.clear();
            }
            line += p + " ";
        }
        if (!line.empty()) ui.text(x, y, line);
    }

    void render_curve(Ui& ui, float x, const data::Car& c) {
        auto e = db().engine(c.parts[6]);
        if (!e || e->rpm.empty()) {
            ui.text(x, 60, "No engine data", colors::kBad);
            return;
        }
        const float gx = x + 30, gy = 60, gw = Ui::kWidth - gx - 20, gh = 200;
        float tmax = 1, pmax = 1;
        u16 rmax = e->rpm.back();
        for (size_t i = 0; i < e->rpm.size(); ++i) {
            tmax = std::max(tmax, e->torque[i]);
            pmax = std::max(pmax, e->torque[i] * e->rpm[i] / 716.2f);
        }
        ui.frame(gx, gy, gw, gh, colors::kDim);
        auto px = [&](u16 rpm) { return gx + gw * rpm / float(rmax + 500); };
        float bw = std::max(2.0f, gw / float(e->rpm.size() + 2) / 2.5f);
        for (size_t i = 0; i < e->rpm.size(); ++i) {
            float tx = px(e->rpm[i]);
            float th = gh * e->torque[i] / tmax * 0.95f;
            float ph = gh * (e->torque[i] * e->rpm[i] / 716.2f) / pmax * 0.95f;
            ui.fill(tx - bw, gy + gh - th, bw, th, Color{90, 160, 240});
            ui.fill(tx, gy + gh - ph, bw, ph, colors::kAccent);
            if (i % 2 == 0) ui.text(tx - 12, gy + gh + 4, fmt("%u", e->rpm[i] / 100), colors::kDim);
        }
        ui.text(gx, gy + gh + 16, "rpm x100", colors::kDim);
        ui.text(x, gy + gh + 30, fmt("Torque (blue) peak %.1f kgf-m   Power (amber) peak %.0f PS", tmax, pmax),
                colors::kText);
        ui.text(x, gy + gh + 42,
                fmt("Catalogue %u PS; table scale byte %u%%", e->power_ps, unsigned(e->torque_scale_pct)),
                colors::kDim);
    }

    void render_parts(Ui& ui, float x, const data::Car& c) {
        float y = 56;
        static const TableId kTables[] = {
            TableId::Muffler,  TableId::Computer,    TableId::PortPolish, TableId::EngineBalance,
            TableId::Displacement, TableId::NaTune,  TableId::TurbineKit, TableId::Intercooler,
            TableId::Flywheel, TableId::Clutch,      TableId::PropellerShaft, TableId::Gear,
            TableId::Lsd,      TableId::Suspension,  TableId::Brake,      TableId::BrakeController,
            TableId::Lightweight,
        };
        for (TableId t : kTables) {
            auto parts = db().parts_for(t, c.label);
            std::string line;
            for (const auto& p : parts)
                if (p.stage) line += fmt("%u:%s ", p.stage, game::format_credits(gt_->part_price(p.price)).c_str());
            if (line.empty()) continue;
            ui.text(x, y, data::table_name(t), colors::kDim);
            ui.text(x + 136, y, clip(line, 30));
            y += 12;
        }
        ui.text(x, y + 6, "stage:price (credits)", colors::kDim);
    }

    void render_events(Ui& ui) {
        const auto& ev = db().events();
        ui.header("RACE EVENTS", fmt("%zu events", ev.size()));
        int n = int(ev.size());
        int first = xr_list_.first(0, event_sel_, kRows, ui.list_window(n, event_sel_, kRows));
        xr_list_.rows(0, first, std::min(kRows, n - first), 4, 240, 24, 12);
        for (int i = 0; i < kRows && first + i < n; ++i) {
            int idx = first + i;
            float y = 26 + i * 12.0f;
            if (idx == event_sel_) ui.fill(4, y - 2, 236, 12, colors::kHighlight);
            ui.text(8, y, clip(ev[idx].label, 28), idx == event_sel_ ? colors::kAccent : colors::kText);
        }
        if (n == 0) return;
        const data::Event& e = ev[event_sel_];
        float x = 250, y = 26;
        ui.fill(x - 6, 22, Ui::kWidth - x, Ui::kHeight - 46, colors::kPanel);
        ui.text(x, y, e.label, colors::kAccent);
        y += 14;
        std::string course = e.course;
        for (const auto& c : db().courses())
            if (c.label == e.course && !c.name.empty()) course = ascii(c.name) + " (" + e.course + ")";
        auto row = [&](const char* k, const std::string& v) {
            ui.text(x, y, k, colors::kDim);
            ui.text(x + 96, y, clip(v, 36));
            y += 12;
        };
        row("Course", course);
        row("Mode", e.race_mode);
        row("Laps", fmt("%u", e.laps));
        row("Regulation", e.regulation.empty() ? "-" : e.regulation);
        std::string prize;
        if (std::optional<game::EventRules> r = gt_->rules(e.label))
            for (size_t place = 0; place < r->prize.size(); ++place)
                if (r->prize[place]) prize += fmt("%zu: %s  ", place + 1, game::format_credits(r->prize[place]).c_str());
        row("Prize (Cr)", prize.empty() ? "-" : prize);
        ui.text(x, y + 4, fmt("Entries (%zu)", e.entries.size()), colors::kDim);
        y += 16;
        for (size_t i = 0; i < e.entries.size() && y < Ui::kHeight - 40; ++i) {
            const data::CarVariant* v = db().variant(e.entries[i]);
            std::string name = e.entries[i];
            if (v) {
                if (const data::Car* c = db().car(v->car)) name = ascii(c->latin_name) + "  [" + e.entries[i] + "]";
            }
            ui.text(x, y, clip(name, 46));
            y += 11;
        }
        ui.footer("Up/Down: event  Square: cars  Triangle: back   (prize/laps fields partly inferred)");
    }

    const CarDatabase& db() const { return gt_->db(); }

    std::unique_ptr<game::GtMode> gt_;
    text::JisTable jis_;
    std::vector<size_t> order_;
    std::vector<std::string> names_;
    std::string error_;
    bool loaded_ = false;
    bool sort_by_power_ = false;
    Mode mode_ = Mode::Cars;
    int car_sel_ = 0, event_sel_ = 0, page_ = 0;
    XrList xr_list_;
};

} // namespace

GT3R_REGISTER_PLAYER_SCREEN(database, "database", "Car database",
                     "Browse the car catalogue: specs, torque curves, upgrades and race events.", 10,
                     [] { return std::make_unique<DatabaseScreen>(); });

} // namespace gt3r
