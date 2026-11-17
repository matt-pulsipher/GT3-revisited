// A car body's windows (formats/model/car_model.h mark_car_glass), which VR's in-car view can
// leave out (issue 320): on a made-up body, and over every car of a real disc (GT3R_DISC).

#include "test_framework.h"

#include "disc/disc.h"
#include "formats/model/car_model.h"
#include "formats/model/wheel_spec.h"
#include "game/data/car_database.h"
#include "vfs/vfs.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace gt3r;

namespace {

using P = std::array<float, 3>;

// A one-colour 2x2 texture; returns its index.
int add_texture(scene::Model& model, u8 r, u8 g, u8 b) {
    scene::Texture t;
    t.width = t.height = 2;
    t.rgba.assign(4, u32(r) | u32(g) << 8 | u32(b) << 16 | 0x73u << 24);
    model.textures.push_back(t);
    return int(model.textures.size()) - 1;
}

// A quad (two triangles) as its own primitive of the body mesh; returns the primitive's index.
size_t add_quad(scene::Mesh& mesh, P a, P b, P c, P d, int texture, u32 flags) {
    const u32 base = u32(mesh.vertices.size());
    for (const P& p : {a, b, c, d}) {
        scene::Vertex v;
        v.pos[0] = p[0], v.pos[1] = p[1], v.pos[2] = p[2];
        mesh.vertices.push_back(v);
    }
    scene::Primitive prim;
    prim.first_index = u32(mesh.indices.size());
    for (u32 k : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(base + k);
    prim.index_count = 6;
    prim.texture = texture;
    prim.flags = flags;
    mesh.primitives.push_back(prim);
    return mesh.primitives.size() - 1;
}

bool glass(const scene::Primitive& p) {
    return (p.flags & scene::kGlass) != 0;
}

} // namespace

TEST(car_glass_made_up_body) {
    scene::Model model;
    scene::Mesh body;
    body.name = "body";
    const int paint = add_texture(model, 20, 30, 120), dark = add_texture(model, 8, 16, 16),
              black = add_texture(model, 0, 0, 0), red = add_texture(model, 90, 20, 10);
    // The body shell, 0 to 1.25 m high (opaque, painted).
    add_quad(body, {-0.9f, 0, -2.2f}, {0.9f, 0, -2.2f}, {0.9f, 0.7f, -2.2f}, {-0.9f, 0.7f, -2.2f}, paint, scene::kAlphaTest);
    const size_t roof = add_quad(body, {-0.7f, 1.25f, -0.5f}, {0.7f, 1.25f, -0.5f}, {0.7f, 1.25f, 1.0f},
                                 {-0.7f, 1.25f, 1.0f}, black, scene::kAlphaTest);
    // The windscreen: dark grey, blended, high up; its reflection over the same corners.
    const P w0{-0.7f, 0.75f, -1.0f}, w1{0.7f, 0.75f, -1.0f}, w2{0.7f, 1.2f, -0.5f}, w3{-0.7f, 1.2f, -0.5f};
    const size_t screen = add_quad(body, w0, w1, w2, w3, dark, scene::kAlphaBlend | scene::kNoDepthWrite);
    const size_t shine = add_quad(body, w0, w1, w2, w3, dark,
                                  scene::kAdditive | scene::kEnvironmentMap | scene::kUnlit | scene::kNoDepthWrite);
    // A black panel 5 cm behind it (GT3's blacked-out windows), and a small mirror on it.
    const size_t backing = add_quad(body, {-0.7f, 0.75f, -0.95f}, {0.7f, 0.75f, -0.95f}, {0.7f, 1.15f, -0.5f},
                                    {-0.7f, 1.15f, -0.5f}, black, scene::kAlphaTest);
    const size_t mirror = add_quad(body, {-0.08f, 1.05f, -0.65f}, {0.08f, 1.05f, -0.65f}, {0.08f, 1.12f, -0.6f},
                                   {-0.08f, 1.12f, -0.6f}, black, scene::kAlphaTest);
    // A simple interior painted on a panel just behind the side glass (dark grey shades): kept.
    const int card = add_texture(model, 40, 40, 44);
    const size_t interior = add_quad(body, {-0.6f, 0.78f, -0.9f}, {0.6f, 0.78f, -0.9f}, {0.6f, 1.1f, -0.55f},
                                     {-0.6f, 1.1f, -0.55f}, card, scene::kAlphaTest);
    // A red lamp lens and a dark lens low on the nose: not glass.
    const size_t lamp = add_quad(body, {-0.8f, 0.4f, -2.21f}, {-0.5f, 0.4f, -2.21f}, {-0.5f, 0.55f, -2.21f},
                                 {-0.8f, 0.55f, -2.21f}, red, scene::kAlphaBlend);
    const size_t low = add_quad(body, {0.5f, 0.4f, -2.21f}, {0.8f, 0.4f, -2.21f}, {0.8f, 0.55f, -2.21f},
                                {0.5f, 0.55f, -2.21f}, dark, scene::kAlphaBlend);
    model.meshes.push_back(body);
    // A part spanning the windscreen's backing and the bonnet: split, the bonnet half kept.
    scene::Mesh& m = model.meshes[0];
    const u32 base = u32(m.vertices.size());
    for (const P& p : {P{-0.6f, 0.8f, -0.92f}, P{0.6f, 0.8f, -0.92f}, P{0.0f, 1.1f, -0.6f}, P{-0.6f, 0.6f, -2.0f},
                       P{0.6f, 0.6f, -2.0f}, P{0.0f, 0.65f, -1.6f}}) {
        scene::Vertex v;
        v.pos[0] = p[0], v.pos[1] = p[1], v.pos[2] = p[2];
        m.vertices.push_back(v);
    }
    scene::Primitive mixed;
    mixed.first_index = u32(m.indices.size());
    for (u32 k = 0; k < 6; ++k) m.indices.push_back(base + k);
    mixed.index_count = 6, mixed.texture = black, mixed.flags = scene::kAlphaTest;
    m.primitives.push_back(mixed);
    const size_t before = m.primitives.size();

    mark_car_glass(model);
    const std::vector<scene::Primitive>& out = model.meshes[0].primitives;
    CHECK(out.size() == before + 1); // (the mixed part split in two)
    CHECK(glass(out[screen]) && glass(out[shine]) && glass(out[backing]));
    CHECK(!glass(out[roof]) && !glass(out[mirror]) && !glass(out[interior]) && !glass(out[lamp]) && !glass(out[low]) && !glass(out[0]));
    CHECK(out.size() == before + 1 && !glass(out[before - 1]) && glass(out[before]));
    CHECK(out[before - 1].index_count == 3 && out[before].index_count == 3);
    // The kept half is the bonnet's triangle.
    if (out.size() == before + 1) {
        const scene::Vertex& v = m.vertices[m.indices[out[before - 1].first_index]];
        CHECK(v.pos[2] < -1.5f);
    }

    // No glass: nothing changes.
    scene::Model open;
    open.meshes.push_back(scene::Mesh{});
    open.meshes[0].name = "body";
    const int t = add_texture(open, 0, 0, 0);
    add_quad(open.meshes[0], {-0.7f, 0.75f, -0.95f}, {0.7f, 0.75f, -0.95f}, {0.7f, 1.15f, -0.5f}, {-0.7f, 1.15f, -0.5f}, t,
             scene::kAlphaTest);
    mark_car_glass(open);
    CHECK(!glass(open.meshes[0].primitives[0]));
}

TEST(car_glass_all_models_disc) {
    Disc* disc = test::any_region_disc();
    if (!disc) {
        if (const char* configured = std::getenv("GT3R_DISC"); configured && *configured) {
            CHECK(false);
            return;
        }
        SKIP("needs GT3R_DISC");
    }
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    bool loaded = db.load(vfs);
    CHECK(loaded);
    if (!loaded) return;
    size_t cars = 0, with_glass = 0;
    for (const data::Car& car : db.cars()) {
        const std::string model = db.model_of(car);
        std::optional<Bytes> bytes = vfs.read("cars/day/" + model);
        if (!bytes || bytes->empty() || !CarFile::is_car(*bytes)) continue;
        CarModelOptions options;
        options.wheels = false, options.effects = false, options.lod = 0;
        options.wheel_spec = wheel_spec_from_database(db, model);
        const scene::Model m = load_car_model(*bytes, options, model);
        ++cars;
        bool found = false;
        for (const scene::Mesh& mesh : m.meshes) {
            float lo = 1e9f, hi = -1e9f;
            for (const scene::Vertex& v : mesh.vertices) lo = std::min(lo, v.pos[1]), hi = std::max(hi, v.pos[1]);
            for (const scene::Primitive& p : mesh.primitives) {
                if (!glass(p)) continue;
                CHECK(mesh.name == "body");
                found = found || (p.flags & scene::kAlphaBlend);
                // Everything taken lies above the body's lower third (no lamps, sills or wheels).
                for (u32 i = p.first_index; i < p.first_index + p.index_count; ++i)
                    CHECK(mesh.vertices[mesh.indices[i]].pos[1] > lo + 0.3f * (hi - lo));
            }
        }
        with_glass += found;
    }
    // Every closed car has its glass found; single-seaters, a few rally and race cars and the
    // sample bodies have none (US 1.10: 239 of 244).
    std::printf("car glass: %zu of %zu bodies\n", with_glass, cars);
    CHECK(cars > 200 && with_glass * 100 >= cars * 95);
}
