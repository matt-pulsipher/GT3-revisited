// The race wheels' brake parts (race/brake.bin, the car files' GTCI records) and spin blurs.

#include "test_framework.h"

#include "formats/model/brake_model.h"
#include "formats/model/car_model.h"
#include "formats/model/gtm1.h"
#include "formats/model/wheel_model.h"
#include "race/wheel_fx.h"

#include <cmath>
#include <cstring>

using namespace gt3r;

namespace {

void put16(Bytes& d, size_t o, u16 v) { std::memcpy(&d[o], &v, 2); }
void put32(Bytes& d, size_t o, u32 v) { std::memcpy(&d[o], &v, 4); }
void put64(Bytes& d, size_t o, u64 v) { std::memcpy(&d[o], &v, 8); }
void putf(Bytes& d, size_t o, float f) { std::memcpy(&d[o], &f, 4); }

bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// A Tex1 set of `count` records sharing one 4x4 T4 image and its CT32 CLUT.
Bytes tiny_tex1(u16 count) {
    const size_t recs = 0x30, bufs = recs + 0x28 * size_t(count), texels = (bufs + 24 + 15) & ~size_t(15),
                 clut = texels + 0x10;
    Bytes d(clut + 0x40, 0);
    std::memcpy(&d[0], "Tex1", 4);
    put32(d, 0x0C, u32(d.size()));
    put16(d, 0x14, count), put16(d, 0x16, 2);
    put32(d, 0x18, u32(recs)), put32(d, 0x1C, u32(bufs));
    // TEX0: tbp 0, tbw 1, T4, 4x4, TCC, CBP 1, CT32, CLD 1.
    const u64 tex0 =
        (1ull << 14) | (0x14ull << 20) | (2ull << 26) | (2ull << 30) | (1ull << 34) | (1ull << 37) | (1ull << 61);
    for (u16 i = 0; i < count; ++i) put64(d, recs + 0x28 * i, tex0);
    put32(d, bufs, u32(texels)), put16(d, bufs + 4, 0), d[bufs + 6] = 1, d[bufs + 7] = 0x14, put16(d, bufs + 8, 4),
        put16(d, bufs + 10, 4);
    put32(d, bufs + 12, u32(clut)), put16(d, bufs + 16, 1), d[bufs + 18] = 1, d[bufs + 19] = 0, put16(d, bufs + 20, 8),
        put16(d, bufs + 22, 2);
    for (u32 e = 0; e < 16; ++e) put32(d, clut + 4 * e, 0x80000000u | (e * 0x10101u));
    return d;
}

// race/brake.bin's layout with 3 rotor and 2 caliper records and 1 + 3 + 2 textures.
Bytes synthetic_brake_bin() {
    Bytes tex = tiny_tex1(6);
    Bytes d(0x30 + 3 * 16 + 2 * 32, 0);
    std::memcpy(&d[0], "GTBR", 4);
    const u32 tex1 = u32(d.size());
    put32(d, 0x0C, tex1 + u32(tex.size()));
    put32(d, 0x10, tex1), put32(d, 0x14, 1), put32(d, 0x18, 4), put32(d, 0x1C, 0);
    put32(d, 0x20, 0x30), put32(d, 0x24, 0x30 + 3 * 16);
    put32(d, 0x30, 0), put32(d, 0x40, 1), put32(d, 0x50, 1); // rotor 0 does not glow
    const float place[2][4] = {{0.4f, 0.85f, 0.75f, 0.0f}, {0.6f, 1.0f, 0.65f, 0.1f}};
    for (int k = 0; k < 2; ++k)
        for (int j = 0; j < 4; ++j) putf(d, 0x60 + 32 * size_t(k) + 4 * size_t(j), place[k][j]);
    d.insert(d.end(), tex.begin(), tex.end());
    return d;
}

// A GTCI block with the two brake records.
Bytes gtci_with(u32 caliper_f, u32 rotor_f, float r_f, float off_f, float ang_f, u32 caliper_r, u32 rotor_r) {
    Bytes g(0x40, 0);
    std::memcpy(&g[0], "GTCI", 4);
    put32(g, 0x0C, 0x40), put32(g, 0x10, 1);
    put32(g, 0x14, caliper_f), put32(g, 0x18, rotor_f), putf(g, 0x1C, r_f), putf(g, 0x20, off_f), putf(g, 0x24, ang_f);
    put32(g, 0x28, caliper_r), put32(g, 0x2C, rotor_r), putf(g, 0x30, 0.2f), putf(g, 0x34, -0.05f), putf(g, 0x38, -40);
    return g;
}

} // namespace

TEST(brake_records_from_gtci) {
    // am0006's front record: caliper 6, rotor 3 (the game keeps 6 - 2 and 3 - 1).
    std::array<BrakeRecord, 2> r = parse_brake_records(gtci_with(6, 3, 0.192f, -0.045f, 0, 1, 1));
    CHECK(r[0].caliper == 4 && r[0].rotor == 2);
    CHECK(near(r[0].radius, 0.192f) && near(r[0].offset, -0.045f) && r[0].angle == 0);
    // The rear: caliper 1 = none; the angle in radians.
    CHECK(r[1].caliper == -1 && r[1].rotor == 0 && near(r[1].angle, -40 * 0.017453292f));
    std::array<BrakeRecord, 2> none = parse_brake_records(Bytes(0x20, 0));
    CHECK(none[0].rotor == -1 && none[1].caliper == -1);
}

TEST(brake_bin_synthetic) {
    Bytes d = synthetic_brake_bin();
    CHECK(BrakeParts::is_brake_bin(d));
    BrakeParts b = BrakeParts::parse(d);
    CHECK(b.textures.size() == 6 && b.rotor_flags.size() == 3 && b.caliper_place.size() == 2);
    CHECK(b.first_rotor == 1 && b.first_caliper == 4 && b.glow == 0);
    CHECK(b.has_rotor(0) && b.has_rotor(2) && !b.has_rotor(3) && !b.has_rotor(-1));
    CHECK(b.has_caliper(1) && !b.has_caliper(2));
    CHECK(!b.rotor_glows(0) && b.rotor_glows(1));
    CHECK(b.textures[0].clamp_u && b.textures[0].clamp_v && b.textures[0].no_mips);
    CHECK(near(b.caliper_place[1][2], 0.65f));
}

TEST(brake_parts_geometry) {
    const BrakeParts b = BrakeParts::parse(synthetic_brake_bin());
    BrakeRecord rec;
    rec.caliper = 0, rec.rotor = 1, rec.radius = 0.2f, rec.offset = -0.05f, rec.angle = 0;
    for (int side = 0; side < 2; ++side) {
        const bool right = side == 1;
        const float out = right ? 1.0f : -1.0f; // the outer face's direction along X
        scene::Model m = build_brake_parts(b, rec, right, 8.0f);
        CHECK(m.meshes.size() == 1 && m.meshes[0].primitives.size() == 2 && m.textures.size() == 2);
        const scene::Mesh& mesh = m.meshes[0];
        const scene::Primitive& rotor = mesh.primitives[0];
        const scene::Primitive& caliper = mesh.primitives[1];
        CHECK(rotor.flags == (scene::kAlphaTest | scene::kDoubleSided) && near(rotor.alpha_ref, 64.5f / 128.0f));
        CHECK(caliper.flags == (scene::kAlphaBlend | scene::kAlphaTest | scene::kAlphaFailBlend | scene::kDoubleSided));
        CHECK(near(caliper.alpha_ref, 127.5f / 128.0f));
        CHECK(rotor.material.valid && near(rotor.material.diffuse[0], 0.15f) && near(rotor.material.shininess, 8));
        // The rotor: 2 r square on the hub, 5 cm outward, facing out, S along the car (both sides
        // alike in the car's axes), T down.
        for (u32 i = 0; i < 4; ++i) {
            const scene::Vertex& v = mesh.vertices[i];
            CHECK(near(v.pos[0], 0.05f * out) && near(std::fabs(v.pos[1]), 0.2f) && near(std::fabs(v.pos[2]), 0.2f));
            CHECK(near(v.normal[0], out));
            CHECK(near(v.uv[0], 0.5f + 0.5f * v.pos[2] / 0.2f) && near(v.uv[1], 0.5f - 0.5f * v.pos[1] / 0.2f));
        }
        // The caliper, 5 mm further out: radial 0.75 r +- 0.4 r towards the back at angle 0,
        // tangential +- 0.85 r; the image's top (T = 0) at its outer edge, its left (S = 0) at the top.
        for (u32 i = 4; i < 8; ++i) {
            const scene::Vertex& v = mesh.vertices[i];
            CHECK(near(v.pos[0], 0.055f * out));
            CHECK(near(std::fabs(v.pos[1]), 0.17f) && (near(v.pos[2], 0.07f) || near(v.pos[2], 0.23f)));
            CHECK(near(v.uv[1], v.pos[2] > 0.1f ? 0.0f : 1.0f) && near(v.uv[0], v.pos[1] > 0 ? 0.0f : 1.0f));
        }
    }
    // Turned half a turn (a front caliper at -180 degrees): in front of the hub.
    rec.angle = -3.14159265f;
    scene::Model m = build_brake_parts(b, rec, false, 8.0f);
    for (u32 i = 4; i < 8; ++i) CHECK(m.meshes[0].vertices[i].pos[2] < -0.06f);
    // No rotor: nothing; no caliper: the rotor alone.
    rec.rotor = 7;
    CHECK(build_brake_parts(b, rec, false, 8.0f).meshes.empty());
    rec.rotor = 0, rec.caliper = -1;
    CHECK(build_brake_parts(b, rec, false, 8.0f).meshes[0].primitives.size() == 1);
}

TEST(brake_glow) {
    const BrakeParts b = BrakeParts::parse(synthetic_brake_bin());
    BrakeRecord rec;
    rec.rotor = 0, rec.radius = 0.2f, rec.offset = -0.05f;
    CHECK(build_brake_glow(b, rec, false).meshes.empty()); // rotor 0 does not glow
    rec.rotor = 2;
    scene::Model g = build_brake_glow(b, rec, true);
    CHECK(g.meshes.size() == 1 && g.meshes[0].primitives.size() == 1);
    const scene::Primitive& p = g.meshes[0].primitives[0];
    CHECK(p.flags == (scene::kAdditive | scene::kUnlit | scene::kNoDepthWrite | scene::kDoubleSided) && p.texture == 0);
    for (const scene::Vertex& v : g.meshes[0].vertices) CHECK(near(v.pos[0], 0.051f) && v.color == 0xFFFFFFFFu);
    // FIX = trunc(clamp(128 heat^2 gain, 0, 255)) / 128.
    CHECK(brake_glow_fix(0, 2) == 0);
    CHECK(brake_glow_fix(1, 1) == 1.0f);
    CHECK(brake_glow_fix(1, 2) == 255.0f / 128.0f);
    CHECK(brake_glow_fix(0.5f, 1) == 0.25f);
    CHECK(brake_glow_fix(0.7f, 1.7f) == 106.0f / 128.0f);
    CHECK(brake_glow_fix(1, 0) == 0);
}

TEST(wheel_detail_and_blur_turn) {
    // s = proj_x x bound / depth: above 0.09 level 0, 0.03 level 1, 0.015 level 2, else 3.
    CHECK(race::wheel_detail(1.5f, 0.4f, 6.0f) == 0);   // 0.1
    CHECK(race::wheel_detail(1.5f, 0.4f, 6.67f) == 1);  // 0.0899
    CHECK(race::wheel_detail(1.5f, 0.4f, 19.9f) == 1);  // 0.0302
    CHECK(race::wheel_detail(1.5f, 0.4f, 20.1f) == 2);
    CHECK(race::wheel_detail(1.5f, 0.4f, 41.0f) == 3);
    CHECK(race::wheel_detail(1.5f, 0.4f, -1.0f) == 0); // (behind the eye: as near)
    race::WheelEnv env; // 0.8, -20
    CHECK(near(race::wheel_blur_turn(0.3f, 0, env), -0.24f));
    CHECK(near(race::wheel_blur_turn(0.3f, 0.05f, env), -0.24f * std::exp(-1.0f)));
    // The course's values from its Env0 block.
    Bytes e(0xF00, 0);
    std::memcpy(&e[0], "Env0", 4);
    putf(e, 0x4C, 1.7f), putf(e, 0xEA0, 0.5f), putf(e, 0xEA4, -10.0f);
    race::WheelEnv p = race::WheelEnv::parse(e);
    CHECK(p.glow_gain == 1.7f && p.blur_time == 0.5f && p.blur_falloff == -10.0f);
    CHECK(race::WheelEnv::parse(Bytes(16, 0)).blur_time == 0.8f);
}

TEST(wheel_tread_blur) {
    CHECK(!race::tread_blur_wanted(0, true));
    CHECK(race::tread_blur_wanted(1, true) && !race::tread_blur_wanted(1, false));
    CHECK(race::tread_blur_wanted(3, false) && !race::tread_blur_wanted(2, true)); // symmetric: either side
    // turn 0.1, 1 repeat: 0.1 / 2 x 325.94931 = 16.30 sixteenths, halved per pass: 8, 4, 2 (truncated).
    std::array<float, 3> s = race::tread_blur_shifts(0.1f, 1, false);
    CHECK(s[0] == 8.0f / 16 / 128 && s[1] == 4.0f / 16 / 128 && s[2] == 2.0f / 16 / 128);
    // A right wheel the other way, wrapped at the texture's 128 texels (2048 sixteenths).
    std::array<float, 3> r = race::tread_blur_shifts(0.1f, 1, true);
    CHECK(r[0] == 2040.0f / 16 / 128 && r[2] == 2046.0f / 16 / 128);
    // Two repeats: twice the travel.
    CHECK(race::tread_blur_shifts(0.1f, 2, false)[0] == 16.0f / 16 / 128);
    CHECK(race::tread_blur_shifts(0, 2, false)[0] == 0);
}

TEST(wheel_rim_blur) {
    std::array<float, 4> a = race::rim_blur_angles(0.4f, false);
    CHECK(near(a[0], 0.2f) && near(a[1], 0.1f) && near(a[2], 0.05f) && near(a[3], 0.025f));
    CHECK(near(race::rim_blur_angles(0.4f, true)[0], -0.2f));
    CHECK(race::rim_blur_lod(128) == 1.0f && race::rim_blur_lod(64) == 0 && race::rim_blur_lod(32) == 0);
    CHECK(race::rim_blur_lod(256) == 2.0f);
    // The other cars' spinning image: past 3 degrees of turn either way.
    CHECK(!race::rim_spinning_image(0.052f) && race::rim_spinning_image(0.0524f) && race::rim_spinning_image(-0.06f));
}

// race/brake.bin on the disc, and every car's brake records within its tables.
TEST(brake_parts_disc) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    const Volume& vol = disc->volume();
    const Volume::Entry* be = vol.find("race/brake.bin");
    CHECK(be != nullptr);
    const Bytes bb = vol.read(*be);
    CHECK(BrakeParts::is_brake_bin(bb));
    const BrakeParts b = BrakeParts::parse(bb);
    CHECK(b.textures.size() == 38 && b.rotor_flags.size() == 7 && b.caliper_place.size() == 30);
    CHECK(b.first_rotor == 1 && b.first_caliper == 8 && b.glow == 0);
    CHECK(b.textures[0].width == 64 && b.textures[8].width == 32);
    CHECK(!b.rotor_glows(0) && b.rotor_glows(1) && b.rotor_glows(6));
    size_t cars = 0, with_caliper = 0, spinning = 0;
    for (const Volume::Entry& e : vol.entries()) {
        const bool day = e.path.rfind("cars/day/", 0) == 0;
        if ((!day && e.path.rfind("cars/night/", 0) != 0 && e.path.rfind("cars/eve/", 0) != 0) || e.size == 0) continue;
        const Bytes data = vol.read(e);
        CarFile f = CarFile::parse(data);
        std::array<BrakeRecord, 2> r = parse_brake_records(f.info);
        const std::string id = e.path.substr(e.path.rfind('/') + 1);
        for (const BrakeRecord& x : r) {
            CHECK(b.has_rotor(x.rotor));
            CHECK(x.caliper == -1 || b.has_caliper(x.caliper));
            // (1.0 m with no caliper: placeholders on a few race and concept cars)
            CHECK(x.radius >= 0.1f && x.radius <= 1.0f && std::fabs(x.offset) < 0.13f);
        }
        if (!day) continue; // (the times of day mostly share the records; cv0022's calipers differ)
        ++cars;
        with_caliper += r[0].caliper >= 0;
        // The wheel's texture table: the rim (entry 1) and the spinning rim (entry 2); a few wheels
        // have one set only.
        gtm1::Gtm1 g = gtm1::Gtm1::parse(WheelFile::parse(f.wheel).gtm1);
        const std::vector<u32>* tab = g.texset_tables.empty() ? nullptr : &g.texset_tables[0];
        spinning += tab && tab->size() > 2 && (*tab)[2] < g.texsets.size();
        if (id == "am0006") {
            CHECK(r[0].caliper == 4 && r[0].rotor == 2 && near(r[0].radius, 0.192f));
            CHECK(near(r[1].angle, -40 * 0.017453292f));
        }
        if (id == "fi0008") CHECK(near(r[0].angle, -3.14159265f, 1e-4f) && r[1].angle == 0);
    }
    std::printf("  %zu cars, %zu with a front caliper, %zu with a spinning rim image\n", cars, with_caliper, spinning);
    CHECK(cars == 286 && with_caliper > 200 && spinning > 200);
}
