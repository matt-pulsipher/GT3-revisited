#pragma once

// Car model files: cars/day/<id>, cars/eve/<id>, cars/night/<id> (gzip in the
// VOL; ids listed in cars/.list). Layout (all little endian):
//
//   0x00 u32 section offsets[5] (0x00 is the header itself), u32 end
//        [1] "GTCI" car info: lamp, camera and spark point placements
//        [2] "GTM1" body model (node 0 = body with 3 LODs, others = effects/shadows)
//        [3] "GTTR" tyre: header + Tex1 tread/sidewall texture
//        [4] "GTTW" wheel model (byte-identical to wheel/<id>)
//   end: 64 bytes "dummy" padding
//
// Units are metres, +Y up, nose towards -Z, +X to the car's right; the origin
// is the wheelbase midpoint on the ground (body is symmetric in X). See docs/formats/car_model.md.

#include "formats/model/gtm1.h"
#include "scene/model.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gt3r {

struct CarFile {
    ByteSpan info;  // GTCI
    ByteSpan body;  // GTM1
    ByteSpan tire;  // GTTR
    ByteSpan wheel; // GTTW

    static bool is_car(ByteSpan data);
    static CarFile parse(ByteSpan data);
};

// The car's underbody spark points from its GTCI block (+0x58 count, +0x5C offset of 16-byte records: x, y, z floats in the
// model frame, a fourth word unused), as the file's words; where the race's sparks start (docs/formats/car_model.md "GTCI").
// Empty when the block has none (or the list runs past the block).
std::vector<std::array<u32, 3>> car_spark_points(ByteSpan gtci);

// Wheel and tyre dimensions (metres). Not stored in the car file: they come
// from the car parameter database (see wheel_spec.h). Axle 0 = front, 1 = rear.
struct WheelSpec {
    float wheelbase = 2.5f;
    float track[2] = {1.5f, 1.5f};    // hub to hub: the tread less twice its per-side offset
    float width[2] = {0.2f, 0.2f};    // tyre section width
    float radius[2] = {0.3f, 0.3f};   // tyre outer radius
    float sidewall[2] = {0.1f, 0.1f}; // sidewall height
};

// Stretches a borrowed body shell to another car's dimensions (mod cars,
// game/data/mod_cars.h), in mm. Between the axles z scales by the wheelbase
// ratio, so the wheel arches land on the new wheel positions; the overhangs
// scale so the total length matches; x and y scale by width and height.
// Wheels are not touched (they are placed from the WheelSpec).
struct BodyFit {
    float from_length = 0, from_wheelbase = 0, from_width = 0, from_height = 0;
    float to_length = 0, to_wheelbase = 0, to_width = 0, to_height = 0;
};
void fit_body(scene::Model& m, const BodyFit& f);

// How a mod car's body differs from the car file it borrows (game/data/mod_cars.h).
struct CarBodyMod {
    std::optional<BodyFit> fit;                // stretch the borrowed body (and its shadows)
    std::shared_ptr<const Bytes> glb;          // a body of its own (binary glTF); shadows, lamps,
                                               // wheels and camera points stay the car file's
    // glTF bodies have their origin on the ground; GT3's body frame sits at the
    // stock ride height (the race lifts the body by the current ride height),
    // so the glTF body is moved down by this much (metres).
    float glb_ground = 0;
    std::optional<std::array<float, 3>> paint; // colour of the glTF paint materials (0..1)
    std::string wheel;                         // a rim to fit by default (wheel/<label>), empty = the car file's
    bool active() const { return fit || glb; }
};

struct CarModelOptions {
    int lod = 0;               // 0..2 body detail; -1 = every LOD (Mesh::lod set, textures per LOD)
    bool effects = false;      // include the extra nodes (ground shadow, glow shells)
    bool reflection = true;    // include the environment-map (reflection) passes
    int paint = 0;             // colour index into the car's list in database/carcolor.db
    bool wheels = true;        // add wheels + generated tyres
    bool menu_tyres = false;   // the menus' tyres (30 segments) and brake parts; else the race's tyres for each LOD (tyre_model.h)
    // Another wheel to fit instead of the car's own: the contents of a wheel/<label>
    // file (the same GTTW format as the car file's wheel section). Empty = stock.
    ByteSpan wheel_override;
    // Showroom wheels in races ([video] showroom_wheels): LOD 0's rims and tyres as the menus
    // draw them, from the car's menu/cars file (its GTTW and GTTR) with the menus' tyres. A
    // fitted wheel's rim is `showroom_wheel` (its menu/wheel/<label>), else `wheel_override`.
    // Empty = the car file's own at every LOD.
    ByteSpan showroom_car;
    ByteSpan showroom_wheel;
    // A rim (a GTTW file) drawn at its full detail at every LOD when the model's own draws nothing
    // at its full detail (a modified game's wheel that only its other kind of model has), read
    // only then: in races the menus' (menu_rim), in the menus the race's (race_rim).
    std::function<std::optional<Bytes>()> stand_in_rim;
    std::optional<WheelSpec> wheel_spec; // placement; estimated from the body when absent
    bool brake = false;        // the brake-lamp branch of the node programs (lit lenses)
    CarBodyMod body_mod;       // mod cars: stretched or replaced body
    // When set, the draws the car's callback morphs (mud flaps, aerials, steering) are left
    // out of the body and built into these, one per LOD that has any (texture indices: the
    // returned model's). Without it they are drawn with the callback's defaults
    // (gtm1::kCarCallbackDefaults). Not for mod bodies.
    std::vector<gtm1::MorphDraws>* morphs = nullptr;
};

// Number of distinct LOD branches node `node` takes (1 when it has no LOD switch).
int gtm1_lod_count(const gtm1::Gtm1& g, u32 node);

// Decodes a whole car file into a Model: mesh "body" (+ "node<N>" effects,
// + "wheel_fl/fr/rl/rr"). Textures of every set used are included.
scene::Model load_car_model(ByteSpan data, const CarModelOptions& opt = {}, const std::string& name = {});
// A car's rims as CarModelOptions::stand_in_rim takes them: the race's (a fitted wheel's
// wheel/<wheel>, else the GTTW section of cars/day/<model>) and the menus' (menu/wheel/<wheel>,
// else menu/cars/<model>'s); none when `read` has neither.
using FileReader = std::function<std::optional<Bytes>(const std::string&)>;
std::optional<Bytes> race_rim(const FileReader& read, const std::string& model, const std::string& wheel = {});
std::optional<Bytes> menu_rim(const FileReader& read, const std::string& model, const std::string& wheel = {});

} // namespace gt3r
