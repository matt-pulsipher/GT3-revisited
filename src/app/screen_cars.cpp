// Car viewer: every car listed in cars/.list, decoded from cars/day/<id>
// (body LOD 0 + wheels) and shown in the shared 3D model viewer.

#include "sim/car_spec.h"
#include "app/app.h"
#include "app/screen_model_viewer.h"
#include "app/screen_registry.h"
#include "formats/model/car_model.h"
#include "formats/model/wheel_spec.h"
#include "formats/text/charset.h"
#include "game/data/car_database.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gt3r {

namespace {

std::vector<std::string> read_car_list(Vfs& vfs) {
    std::vector<std::string> ids;
    std::optional<Bytes> list = vfs.read("cars/.list");
    if (!list) return ids;
    std::string cur;
    for (u8 c : *list) {
        if (c == '\n' || c == '\r' || c == 0) {
            if (!cur.empty()) ids.push_back(cur);
            cur.clear();
        } else {
            cur += char(c);
        }
    }
    if (!cur.empty()) ids.push_back(cur);
    return ids;
}

// The viewer needs its item list up front, but the registry factory has no
// App; this placeholder reads the list on its first frame and swaps itself
// for the real viewer.
class CarViewerLauncher : public Screen {
public:
    void update(App& app, double) override {
        if (done_) return;
        done_ = true;
        std::vector<std::string> ids, labels;
        if (app.vfs()) ids = read_car_list(*app.vfs());
        // Label models with the name of a car that uses them (when the database loads; game text,
        // UTF-8 for this font).
        auto db = std::make_shared<data::CarDatabase>();
        bool have_db = app.vfs() && db->load(*app.vfs());
        auto name = [](const data::Car* c) {
            return c ? text::game_text_to_utf8(c->latin_name, nullptr) : std::string();
        };
        for (const std::string& id : ids) {
            const data::Car* car = have_db ? db->car_by_model(id) : nullptr;
            labels.push_back(car && !car->latin_name.empty() ? id + "  " + name(car) : id);
        }
        // Mod cars: "mod:<label>", their own body, rim and paint (GT3R_VIEWER_PAINT=<n>).
        if (have_db)
            for (const data::ModCar& m : db->mod_cars()) {
                ids.push_back("mod:" + m.label);
                const data::Car* c = db->car(m.label);
                labels.push_back("mod:" + m.label + "  " + name(c));
            }
        app.pop();
        app.push(make_model_viewer("Car viewer", labels, [ids, db, have_db](App& a, size_t i) -> scene::Model {
            if (!a.vfs()) throw FormatError("no disc loaded");
            if (ids.at(i).rfind("mod:", 0) == 0) {
                std::string label = ids[i].substr(4);
                const data::Car* car = db->car(label);
                if (!car) throw FormatError("no car " + label);
                std::string model = db->model_of(*car);
                std::optional<Bytes> data = a.vfs()->read("cars/day/" + model);
                if (!data || data->empty()) throw FormatError("missing cars/day/" + model);
                const char* pe = SDL_getenv("GT3R_VIEWER_PAINT");
                CarModelOptions opt;
                opt.lod = 0;
                opt.wheel_spec = wheel_spec_for_car(*db, label);
                opt.body_mod = body_mod_for_car(*db, label, pe ? std::atoi(pe) : 0);
                static std::optional<Bytes> rim;
                rim = opt.body_mod.wheel.empty() ? std::nullopt : a.vfs()->read("wheel/" + opt.body_mod.wheel);
                if (rim) opt.wheel_override = *rim;
                scene::Model m = load_car_model(*data, opt, label);
                if (auto spec = sim::build_car_spec(*db, label); spec && spec->axle[0].ride_height > 0) {
                    for (scene::Mesh& mesh : m.meshes)
                        if (mesh.name.rfind("wheel_", 0) != 0) mesh.transform[13] += spec->axle[0].ride_height;
                    m.bounds_max[1] += spec->axle[0].ride_height;
                }
                return m;
            }
            std::string path = "cars/day/" + ids.at(i);
            std::optional<Bytes> data = a.vfs()->read(path);
            if (!data) throw FormatError("missing " + path);
            if (data->empty()) throw FormatError(path + " is an empty placeholder");
            CarModelOptions opt;
            opt.lod = -1; // all LODs: the viewer switches between them
            if (have_db) opt.wheel_spec = wheel_spec_from_database(*db, ids[i]);
            scene::Model m = load_car_model(*data, opt, ids[i]);
            // Showroom pose (as the game's menus): the body rides q (ride height) above the
            // floor, the wheels stay on it.
            if (const data::Car* car = have_db ? db->car_by_model(ids[i]) : nullptr)
                if (auto spec = sim::build_car_spec(*db, car->label); spec && spec->axle[0].ride_height > 0) {
                    for (scene::Mesh& mesh : m.meshes)
                        if (mesh.name.rfind("wheel_", 0) != 0) mesh.transform[13] += spec->axle[0].ride_height;
                    m.bounds_max[1] += spec->axle[0].ride_height;
                }
            return m;
        }));
    }
    void render(App&, Ui&) override {}

private:
    bool done_ = false;
};

} // namespace

GT3R_REGISTER_SCREEN(cars, "cars", "Car viewer", "Inspect the car models (body, wheels, textures).", 50,
                     [] { return std::make_unique<CarViewerLauncher>(); });

} // namespace gt3r
