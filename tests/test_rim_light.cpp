// The menus' rims in races: the light painted into their images evened out round the axis (ours, issue 272;
// formats/model/rim_light.h).

#include "test_framework.h"

#include "formats/model/car_model.h"
#include "formats/model/gtm1.h"
#include "formats/model/rim_light.h"
#include "formats/model/wheel_model.h"
#include "formats/model/wheel_spec.h"
#include "game/data/car_database.h"

#include <cmath>
#include <functional>

using namespace gt3r;

namespace {

constexpr double kPi = 3.14159265358979323846;

// A unit rim face (x = 0, radius 1, the axis along x) as rings x segments, its image laid out as
// uv = (centre + m (y, z)) / size, and the image: `texel` gets the face point (y, z) a texel
// shows (by the inverse of the layout) and returns its RGBA.
scene::Model synthetic_rim(u32 w, u32 h, const double centre[2], const double m[2][2],
                           const std::function<u32(double y, double z)>& texel) {
    scene::Model model;
    scene::Mesh mesh;
    mesh.name = "rim";
    const int rings = 24, segments = 60;
    auto vertex = [&](double y, double z) {
        scene::Vertex v;
        v.pos[1] = float(y), v.pos[2] = float(z);
        v.normal[0] = -1;
        v.uv[0] = float((centre[0] + m[0][0] * y + m[0][1] * z) / w);
        v.uv[1] = float((centre[1] + m[1][0] * y + m[1][1] * z) / h);
        mesh.vertices.push_back(v);
    };
    vertex(0, 0);
    for (int r = 1; r <= rings; ++r)
        for (int s = 0; s < segments; ++s) {
            const double a = 2 * kPi * s / segments, rad = double(r) / rings;
            vertex(rad * std::cos(a), rad * std::sin(a));
        }
    auto at = [&](int r, int s) { return r == 0 ? 0u : u32(1 + (r - 1) * segments + (s % segments)); };
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < segments; ++s) {
            mesh.indices.insert(mesh.indices.end(), {at(r, s), at(r + 1, s), at(r + 1, s + 1)});
            if (r > 0) mesh.indices.insert(mesh.indices.end(), {at(r, s), at(r + 1, s + 1), at(r, s + 1)});
        }
    scene::Primitive p;
    p.index_count = u32(mesh.indices.size());
    p.texture = 0;
    mesh.primitives.push_back(p);
    model.meshes.push_back(mesh);
    scene::Texture t;
    t.width = w, t.height = h;
    const double det = m[0][0] * m[1][1] - m[0][1] * m[1][0];
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            const double dx = x + 0.5 - centre[0], dy = y + 0.5 - centre[1];
            t.rgba.push_back(texel((m[1][1] * dx - m[0][1] * dy) / det, (m[0][0] * dy - m[1][0] * dx) / det));
        }
    model.textures.push_back(t);
    return model;
}

u32 rgba(double v, double alpha) {
    auto c = [](double x) { return u32(std::lround(std::clamp(x, 0.0, 255.0))); };
    return c(v) | c(0.9 * v) << 8 | c(0.8 * v) << 16 | c(alpha) << 24;
}

} // namespace

// A five-spoke image lit from one side: the light divided out leaves the spokes, each ring's mean and the alpha; the
// axis is found where the layout puts it (off the image's centre, the layout turned and stretched).
TEST(rim_light_evens_one_sided_light) {
    const double centre[2] = {30.0, 35.0}, m[2][2] = {{20.0, -12.0}, {11.0, 22.0}};
    auto spokes = [](double y, double z) {
        const double r = std::hypot(y, z), a = std::atan2(z, y);
        return (90 + 40 * r) * (1 + 0.5 * std::cos(5 * a));
    };
    auto alpha = [](double y, double z) { return std::cos(5 * std::atan2(z, y)) > -0.3 ? 255.0 : 128.0; };
    auto light = [](double y, double z) { return 1 + 0.3 * std::cos(std::atan2(z, y) - 0.7); };
    auto lit = [&](double y, double z) {
        if (std::hypot(y, z) > 1.0) return rgba(64, 0);
        return rgba(spokes(y, z) * light(y, z), alpha(y, z));
    };
    auto even = [&](double y, double z) {
        if (std::hypot(y, z) > 1.0) return rgba(64, 0);
        return rgba(spokes(y, z), alpha(y, z));
    };
    scene::Model rim = synthetic_rim(64, 64, centre, m, lit);
    const scene::Model reference = synthetic_rim(64, 64, centre, m, even), before = rim;

    // The texel holding the axis is placed at it.
    const RimTexelPlaces places = rim_texel_places(rim, 0);
    CHECK(places.width == 64 && places.texels.size() == 64 * 64);
    CHECK(places.texels[35 * 64 + 30].radius >= 0 && places.texels[35 * 64 + 30].radius < 1.0f);

    const double one_sided = *rim_light_one_sidedness(rim);
    CHECK(one_sided > 0.1);
    CHECK(even_rim_light(rim) == 1);
    CHECK(*rim_light_one_sidedness(rim) < 0.005);
    CHECK(*rim_light_one_sidedness(reference) < 0.005);

    // Per texel: alpha as it was; on the face (from 3 texels out, short of the edge) the unlit image; off the face
    // (two texels clear of it, past the neighbours) unchanged.
    const scene::Texture &out = rim.textures[0], &in = before.textures[0], &want = reference.textures[0];
    const double det = m[0][0] * m[1][1] - m[0][1] * m[1][0];
    double diff = 0, n = 0;
    bool alpha_kept = true, off_face_kept = true;
    std::vector<double> ring_in(64), ring_out(64);
    for (u32 y = 0; y < 64; ++y)
        for (u32 x = 0; x < 64; ++x) {
            const size_t i = y * 64 + x;
            alpha_kept &= (out.rgba[i] >> 24) == (in.rgba[i] >> 24);
            const double dx = x + 0.5 - centre[0], dy = y + 0.5 - centre[1];
            const double r = std::hypot((m[1][1] * dx - m[0][1] * dy) / det, (m[0][0] * dy - m[1][0] * dx) / det);
            const double r_texels = places.texels[i].radius;
            if (r > 1.0 + 2.0 / 20) off_face_kept &= out.rgba[i] == in.rgba[i];
            if (r_texels >= 0) { // each ring's mean as the correction weighs it, by alpha
                const double wt = (in.rgba[i] >> 24) / 255.0;
                ring_in[size_t(r_texels)] += wt * (in.rgba[i] & 0xFF), ring_out[size_t(r_texels)] += wt * (out.rgba[i] & 0xFF);
            }
            if (r_texels < 3 || r > 0.95) continue;
            for (int c = 0; c < 3; ++c) diff += std::abs(int((out.rgba[i] >> (8 * c)) & 0xFF) - int((want.rgba[i] >> (8 * c)) & 0xFF));
            n += 3;
        }
    CHECK(alpha_kept);
    CHECK(off_face_kept);
    CHECK(n > 1000 && diff / n < 2.0); // the spokes without the light (rounding, and rings of a few texels)
    bool means_kept = true;
    for (size_t k = 0; k < ring_in.size(); ++k)
        if (ring_in[k] > 0) means_kept &= std::fabs(ring_out[k] - ring_in[k]) <= 0.01 * ring_in[k] + 1;
    CHECK(means_kept);
}

// A rim image already even is left (nearly) as it is; an image area two halves of the face share (laid out
// mirrored) is left alone; a triangle sampling one spot places nothing.
TEST(rim_light_leaves_even_and_shared_images) {
    const double centre[2] = {32.0, 32.0}, m[2][2] = {{28.0, 0.0}, {0.0, 28.0}};
    auto spokes = [](double y, double z) {
        if (std::hypot(y, z) > 1.0) return rgba(64, 0);
        return rgba(100 * (1 + 0.5 * std::cos(5 * std::atan2(z, y))), 255);
    };
    scene::Model even = synthetic_rim(64, 64, centre, m, spokes);
    const scene::Model before = even;
    even_rim_light(even);
    int most = 0;
    for (size_t i = 0; i < even.textures[0].rgba.size(); ++i)
        for (int c = 0; c < 3; ++c)
            most = std::max(most, std::abs(int((even.textures[0].rgba[i] >> (8 * c)) & 0xFF) -
                                           int((before.textures[0].rgba[i] >> (8 * c)) & 0xFF)));
    CHECK(most <= 3);

    // Mirrored: the face's z < 0 half drawn with the z > 0 half's texels.
    scene::Model shared = synthetic_rim(64, 64, centre, m, [](double y, double z) {
        return rgba(100 * (1 + 0.3 * std::cos(std::atan2(z, y) - 0.7)), 255);
    });
    for (scene::Vertex& v : shared.meshes[0].vertices)
        if (v.pos[2] < 0) v.uv[1] = float((centre[1] - m[1][1] * v.pos[2]) / 64);
    const RimTexelPlaces places = rim_texel_places(shared, 0);
    bool shared_left_alone = true;
    for (u32 y = 0; y < 64; ++y)
        for (u32 x = 0; x < 64; ++x)
            if (y + 0.5 - centre[1] > 2.0 && std::hypot(x + 0.5 - centre[0], y + 0.5 - centre[1]) < 27)
                shared_left_alone &= places.texels[y * 64 + x].radius < 0;
    CHECK(shared_left_alone);

    // One spot: every corner on the same texel.
    for (scene::Vertex& v : shared.meshes[0].vertices) v.uv[0] = v.uv[1] = 0.25f;
    bool none = true;
    for (const RimTexelPlaces::Place& p : rim_texel_places(shared, 0).texels) none &= p.radius < 0;
    CHECK(none);
}

// Every menu rim of the disc (menu/cars section 4; menu/wheel): the stock rims' painted light (about 0.04 on NTSC-U
// 1.10) comes down to the wheel shop's level or below, the shop's (about 0.015) further.
TEST(rim_light_menu_rims_disc) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    auto& vol = disc->volume();
    struct Totals {
        int rims = 0, even = 0, worse = 0;
        double before = 0, after = 0;
    };
    auto survey = [&](const std::string& prefix, bool cars) {
        Totals t;
        for (const auto& e : vol.entries()) {
            if (e.path.rfind(prefix, 0) != 0 || e.size == 0) continue;
            const std::string name = e.path.substr(prefix.size());
            if (cars ? name.find('.') != std::string::npos : name.find('_') == std::string::npos) continue;
            const Bytes data = vol.read(e);
            gtm1::Options o;
            o.run.lod = 0;
            o.runtime_set = 1; // the rim image, as load_car_model takes it
            scene::Model rim;
            try {
                rim = load_wheel_model(CarFile::is_car(data) ? CarFile::parse(data).wheel : ByteSpan(data), o, name);
            } catch (const std::exception&) {
                continue;
            }
            const std::optional<double> before = rim_light_one_sidedness(rim);
            if (!before) continue;
            even_rim_light(rim);
            const double after = *rim_light_one_sidedness(rim);
            ++t.rims, t.before += *before, t.after += after;
            t.even += after <= 0.015;
            t.worse += after > *before + 0.006;
        }
        return t;
    };
    const Totals stock = survey("menu/cars/", true), shop = survey("menu/wheel/", false);
    std::printf("  stock rims %d: one-sidedness %.4f -> %.4f, %d at or under 0.015; shop rims %d: %.4f -> %.4f\n",
                stock.rims, stock.before / stock.rims, stock.after / stock.rims, stock.even, shop.rims,
                shop.before / shop.rims, shop.after / shop.rims);
    CHECK(stock.rims >= 200 && shop.rims >= 100);
    CHECK(stock.before / stock.rims > 0.03);
    CHECK(stock.after / stock.rims < 0.006);
    CHECK(stock.even >= 0.95 * stock.rims);
    CHECK(stock.worse == 0);
    CHECK(shop.after / shop.rims < shop.before / shop.rims && shop.after / shop.rims < 0.005);
    CHECK(shop.worse == 0);
}

// As the race loads a car: with showroom wheels, LOD 0's menus' rim has its image evened; the geometry, the other
// levels and a fitted wheel's race rim (none in the menus) are as without it.
TEST(rim_light_race_car_disc) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    data::CarDatabase db;
    CHECK(db.load(disc->volume()));
    std::optional<WheelSpec> ws = wheel_spec_for_car(db, "slk230_komp_00");
    CHECK(ws.has_value());
    if (!ws) return;
    auto& vol = disc->volume();
    const Bytes race_file = vol.read(*vol.find("cars/day/mb0007")), menu_file = vol.read(*vol.find("menu/cars/mb0007"));
    CarModelOptions plain;
    plain.lod = -1;
    plain.effects = true;
    plain.wheel_spec = ws;
    plain.showroom_car = menu_file;
    CarModelOptions evened = plain;
    evened.even_menu_rim_light = true;
    // The images the meshes named `name` at `lod` draw, and their vertices.
    auto images = [](const scene::Model& m, const std::string& name, int lod) {
        std::vector<std::vector<u32>> out;
        for (const scene::Mesh& mesh : m.meshes)
            if (mesh.name == name && mesh.lod == lod)
                for (const scene::Primitive& p : mesh.primitives)
                    if (p.texture >= 0) out.push_back(m.textures[size_t(p.texture)].rgba);
        return out;
    };
    auto verts = [](const scene::Model& m) {
        std::vector<float> out;
        for (const scene::Mesh& mesh : m.meshes)
            for (const scene::Vertex& v : mesh.vertices) out.insert(out.end(), {v.pos[0], v.pos[1], v.pos[2], v.uv[0], v.uv[1]});
        return out;
    };
    const scene::Model a = load_car_model(race_file, plain, "mb0007"), b = load_car_model(race_file, evened, "mb0007");
    CHECK(verts(a) == verts(b));
    CHECK(!images(a, "wheel_fl", 0).empty() && images(a, "wheel_fl", 0) != images(b, "wheel_fl", 0));
    CHECK(images(a, "wheel_fl", 1) == images(b, "wheel_fl", 1));
    CHECK(images(a, "body", 0) == images(b, "body", 0));
    CHECK(images(a, "wheel_fl_tyre", 0) == images(b, "wheel_fl_tyre", 0));
    // A fitted wheel without a menus' model: its race rim at LOD 0, as it is.
    std::string label;
    for (const auto& e : vol.entries())
        if (e.path.rfind("wheel/", 0) == 0 && e.path.find('_') != std::string::npos) {
            label = e.path.substr(6);
            break;
        }
    CHECK(!label.empty());
    if (label.empty()) return;
    const Bytes race_wheel = vol.read(*vol.find("wheel/" + label));
    plain.wheel_override = evened.wheel_override = race_wheel;
    CHECK(images(load_car_model(race_file, plain, "mb0007"), "wheel_fl", 0) ==
          images(load_car_model(race_file, evened, "mb0007"), "wheel_fl", 0));
}
