#pragma once

// Custom paint colours (docs/dev/CUSTOM_PAINTS.md): colours the disc does not have, defined
// by a name, an RGB value and the finish of one of the car's own paints, and computed from
// the car's own paints when its model loads (gtm1::ComputedPaint). No pixels at all.
//
// A mod ships them as mods/<id>/liveries/<name>.ini (or liveries/<name>/livery.ini):
//
//   [livery]
//   name = Sunday Cup Blue
//   author = you
//   model = am0006      ; the body it is for: every car on it (carcolor.db's model), or "mod:<label>", a mod car
//   color = #1E3A8A
//   finish = 4          ; the car's paint (0 = its first) whose finish it takes; none = the nearest in colour
//
// A livery is a folder, mods/<id>/liveries/<name>/, with a livery.ini and pictures named as a
// texture pack's ("<anything>-<16 hex digit key>.png", any size): the keys of the car's textures
// in its paint (`paint = N`, a disc paint), as `gt3tool livery-template` writes them. The pictures
// go over the paint (their alpha is how much they cover; the car keeps its reflectivity and
// cut-outs), on every copy of the car with those textures (race, evening and night models, the
// showroom's). `color` with a livery paints a computed colour under the pictures.
//
// A mod car's paints are its own list ("mod:<label>", race::paint_key): its [paints], else its body's, then its
// custom colours and liveries. On a glTF body a livery's keys are the embedded images' (formats/model/gltf.h).
//
// Every list of a car's paints shows the model's custom colours after the disc's: paint index
// disc count + k is custom colour k of the model (in mod priority, then file order). Online a
// car in a custom colour sends its colour and finish (game/net_car.h), so other players need
// no file: their game adds it for the session (received_paint).

#include "base/bytes.h"
#include "formats/model/car_model.h"
#include "formats/model/gtm1.h"
#include "race/paint.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r {
class Vfs;
}

namespace gt3r::race {

struct CustomPaint {
    std::string name, author;
    std::string model; // the body it is for; a mod car's: its race::paint_key ("mod:<label>")
    u32 rgb = 0;       // bytes R, G, B (a livery without a colour: its paint's carcolor.db colour, for the chips)
    bool colour = true; // a computed colour (rgb); false: a livery on the disc paint `finish`
    int finish = -1;   // the car's paint whose finish it takes; -1 = the nearest in colour (resolved when added)
    // A livery's pictures (keyed by the textures in paint `finish`); null for a colour alone.
    std::shared_ptr<const texture_pack::Pack> pictures;
    std::string id; // a livery's "<mod id>/<folder name>", how replays find it again
    std::string mod, file; // the mod and host file it came from ("": received online)
    std::string from;      // received online: the player whose car wore it
    // Installed (from a file): shown in the paint lists. Colours added for the session (received online, or a replay's)
    // only draw the cars that wear them; they come after every installed one.
    bool listed() const { return !file.empty(); }
};

// One definition file. Refuses a file without a model, or a bad colour; `colour` false when it
// gives none (a livery on a disc paint: `paint = N`, which is also `finish`).
std::optional<CustomPaint> parse_custom_paint(std::string_view ini_text, std::string* error = nullptr);
// "#1E3A8A" / "1E3A8A" as bytes R, G, B.
std::optional<u32> parse_rgb(std::string_view text);
std::string rgb_text(u32 rgb); // "#1E3A8A"

// The paint of a car's paints (`paint_rgb`, carcolor.db's colours) nearest to `rgb` (0 when empty).
int nearest_paint(const std::vector<u32>& paint_rgb, u32 rgb);

class CustomPaints {
public:
    CustomPaints() = default;
    explicit CustomPaints(CarColors colors) : colors_(std::move(colors)) {}

    // Adds `p` to its model's list; false (and why) when the model has no paints on this disc or
    // the finish is not one of them. Returns its paint index through `index`.
    bool add(CustomPaint p, std::string* error = nullptr, int* index = nullptr);

    const CarColors& colors() const { return colors_; }
    // carcolor.db's colours of a model's paints, in paint order (empty: unknown model).
    std::vector<u32> paint_rgb(const std::string& model) const;
    int disc_count(const std::string& model) const;
    // The model's custom colours (paint index disc_count + k), empty when none.
    const std::vector<CustomPaint>& of(const std::string& model) const;
    // Paint index `paint` of a model, when it is a custom colour.
    const CustomPaint* at(const std::string& model, int paint) const;
    std::optional<gtm1::ComputedPaint> computed(const std::string& model, int paint) const;
    // What load_car_model draws for paint index `paint` of a model, when it is a custom one.
    std::optional<ResolvedPaint> resolved(const std::string& model, int paint) const;
    // The paint index of the model's custom colour (no livery) with this colour and finish, -1 when none.
    int index_of(const std::string& model, u32 rgb, int finish) const;
    // The paint index of the installed livery `id` of a model, -1 when none.
    int index_of_livery(const std::string& model, const std::string& id) const;
    size_t size() const;
    // Definitions refused while loading ("<file>: <reason>").
    const std::vector<std::string>& errors() const { return errors_; }
    void add_error(std::string e) { errors_.push_back(std::move(e)); }

private:
    CarColors colors_;
    std::map<std::string, std::vector<CustomPaint>> by_model_;
    std::vector<std::string> errors_;
};

// The enabled mods' colours on this disc (mods in ascending priority, files by name).
std::shared_ptr<CustomPaints> load_custom_paints(const Vfs& vfs);

// The app's current colours (an empty set before a disc opens), and setting them: that also
// makes load_car_model draw paint indices past a model's disc colours in them.
std::shared_ptr<const CustomPaints> custom_paints();
void set_custom_paints(std::shared_ptr<const CustomPaints> paints);
// A colour another player's car wears online: its paint index here, added for this session
// when no installed colour of the model has that colour and finish. -1 when the model has no
// paints here or `finish` is not one of them.
int received_paint(const std::string& model, u32 rgb, int finish, const std::string& from = {});
// The packages of our own liveries (game/livery_package.h) by hash, so our car's descriptor finds its livery again.
void note_livery_hash(const std::string& hash, const std::string& id);
std::string livery_id_for_hash(const std::string& hash); // "" when none

} // namespace gt3r::race
