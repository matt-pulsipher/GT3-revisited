// Tests for the course decoders: container, GTRW road, AI lines, course model.

#include "test_framework.h"

#include "formats/clut_animation.h"
#include "formats/course/course_model.h"
#include "formats/course/visibility.h"
#include "formats/gs_memory.h"
#include "formats/tex1.h"
#include "sim/exact/course_query.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <cmath>
#include <cstring>

using namespace gt3r;
using namespace gt3r::course;

namespace {

void put32(Bytes& b, size_t at, u32 v) {
    if (b.size() < at + 4) b.resize(at + 4);
    for (int i = 0; i < 4; ++i) b[at + i] = u8(v >> (8 * i));
}
void put16(Bytes& b, size_t at, u16 v) {
    if (b.size() < at + 2) b.resize(at + 2);
    b[at] = u8(v), b[at + 1] = u8(v >> 8);
}
void putf(Bytes& b, size_t at, float f) {
    u32 v;
    std::memcpy(&v, &f, 4);
    put32(b, at, v);
}

// A GTRW with one segment (a flat 10x10 quad at y = 2), one grid slot,
// one wall polyline and two checkpoints.
Bytes make_gtrw() {
    Bytes b(0x200, 0);
    std::memcpy(b.data(), "GTRW", 4);
    put32(b, 0x14, 0x1234);
    putf(b, 0x18, 100.f);
    put16(b, 0x22, 1), put32(b, 0x24, 0x60); // marker
    put32(b, 0x60, 2), putf(b, 0x64, 50.f);
    put32(b, 0x28, 1), put32(b, 0x2C, 0x70); // grid
    putf(b, 0x70, 5), putf(b, 0x74, 0), putf(b, 0x78, 5), putf(b, 0x7C, 1.5f);
    put32(b, 0x30, 1), put32(b, 0x34, 0x80); // segments
    put16(b, 0x80, 4), put16(b, 0x82, 2), put32(b, 0x84, 0x100), put32(b, 0x88, 0x140), put32(b, 0x90, 0x160);
    const float v[4][2] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    for (int i = 0; i < 4; ++i) {
        putf(b, 0x100 + 16 * i, v[i][0]), putf(b, 0x104 + 16 * i, 2.f), putf(b, 0x108 + 16 * i, v[i][1]);
        put32(b, 0x10C + 16 * i, 0xFFFFFFFF);
    }
    put16(b, 0x140, 0), put16(b, 0x142, 1), put16(b, 0x144, 2), put32(b, 0x14C, 8);
    put16(b, 0x150, 0), put16(b, 0x152, 2), put16(b, 0x154, 3), put32(b, 0x15C, 8);
    put32(b, 0x38, 3), put32(b, 0x3C, 0x170); // wall: 3 points, one polyline
    for (int i = 0; i < 3; ++i) putf(b, 0x170 + 16 * i, float(i));
    put32(b, 0x17C, 3), put32(b, 0x19C, u32(-3) & 0xFFFF);
    put32(b, 0x40, 2), put32(b, 0x44, 0x1A0); // checkpoints
    putf(b, 0x1A0, 0), putf(b, 0x1A4, 2), putf(b, 0x1A8, 10), putf(b, 0x1AC, 2), putf(b, 0x1B0, 0);
    putf(b, 0x1C0, 0), putf(b, 0x1C4, 8), putf(b, 0x1C8, 10), putf(b, 0x1CC, 8), putf(b, 0x1D0, 6);
    return b;
}

} // namespace

TEST(course_gtrw_synthetic) {
    Bytes b = make_gtrw();
    Road r = Road::parse(b);
    CHECK(r.course_id == 0x1234);
    CHECK(r.length == 100.f);
    CHECK(r.markers.size() == 1 && r.markers[0].distance == 50.f);
    CHECK(r.grid.size() == 1 && r.grid[0].yaw == 1.5f);
    CHECK(r.segments.size() == 1 && r.triangle_count() == 2);
    CHECK(r.segments[0].triangles[0].surface == 8);
    CHECK(r.walls.size() == 1 && r.walls[0].size() == 3);
    u32 surface = 0;
    auto h = r.height_at(3, 7, 1e30f, &surface);
    CHECK(h && std::fabs(*h - 2.f) < 1e-5f && surface == 8);
    CHECK(!r.height_at(11, 5));
    std::vector<Vec3> c = r.centre_line();
    CHECK(c.size() == 2 && std::fabs(c[1].z - 8.f) < 1e-5f && std::fabs(c[1].y - 2.f) < 1e-5f);

    Bytes bad = b;
    put16(bad, 0x144, 9); // triangle index out of range
    bool threw = false;
    try {
        Road::parse(bad);
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

// A Vls0 with a 2 x 1 grid over x 0..20, z 0..10: cell 0 holds view points A (2, 0, 5) and
// B (8, 0, 5), cell 1 holds C (15, 0, 5) and B again (points are shared between cells).
TEST(course_visibility_synthetic) {
    Bytes b(0xA8, 0);
    std::memcpy(b.data(), "Vls0", 4);
    put16(b, 0x10, 2), put16(b, 0x12, 1), put32(b, 0x14, 0x30);
    putf(b, 0x18, 0), putf(b, 0x1C, 0), putf(b, 0x20, 20), putf(b, 0x24, 10), putf(b, 0x28, 0.05f), putf(b, 0x2C, 0.1f);
    put32(b, 0x30, 0x40), put32(b, 0x34, 0x50);                     // cell offsets
    put32(b, 0x40, 0x60), put32(b, 0x44, 0x70), put32(b, 0x48, 0); // cell 0: A, B
    put32(b, 0x50, 0x80), put32(b, 0x54, 0x70), put32(b, 0x58, 0); // cell 1: C, B
    const float pts[3][3] = {{2, 0, 5}, {8, 0, 5}, {15, 0, 5}};
    const u32 lists[3] = {0x90, 0x98, 0xA0};
    for (int i = 0; i < 3; ++i) {
        put32(b, 0x60 + 16 * size_t(i), lists[i]);
        for (int k = 0; k < 3; ++k) putf(b, 0x64 + 16 * size_t(i) + 4 * size_t(k), pts[i][k]);
    }
    put16(b, 0x90, 2), put16(b, 0x92, 3), put16(b, 0x94, 1); // A: nodes 3, 1
    put16(b, 0x98, 1), put16(b, 0x9A, 7);                    // B: node 7
    put16(b, 0xA0, 2), put16(b, 0xA2, 9), put16(b, 0xA4, 4); // C: nodes 9, 4
    Visibility v = Visibility::parse(b);
    CHECK(!v.empty());
    auto nodes = [&](float x, float z) {
        const std::vector<u16>* n = v.visible(x, 0, z);
        return n ? *n : std::vector<u16>{};
    };
    CHECK((nodes(1, 5) == std::vector<u16>{3, 1}));  // cell 0, nearest A
    CHECK((nodes(7, 5) == std::vector<u16>{7}));     // cell 0, nearest B
    CHECK((nodes(5, 5) == std::vector<u16>{3, 1}));  // tie between A and B: the first point
    CHECK((nodes(12, 5) == std::vector<u16>{9, 4})); // cell 1, nearest C
    CHECK((nodes(10.5f, 5) == std::vector<u16>{7})); // cell 1, nearest B
    CHECK((nodes(-50, 5) == std::vector<u16>{3, 1})); // clamped to cell 0
    CHECK((nodes(500, 80) == std::vector<u16>{9, 4})); // clamped to cell 1
    Bytes bad = b;
    put32(bad, 0x34, 0x1000); // cell offset past the end
    CHECK(Visibility::parse(bad).empty());
    CHECK(!Visibility::parse(bad).visible(1, 0, 5));
}

TEST(course_trialmt) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    const Volume::Entry* e = disc->volume().find("courses/US/trialmt");
    CHECK(e != nullptr);
    if (!e) return;
    Bytes data = disc->volume().read(*e);
    CHECK(CourseFile::looks_like_course(data));
    Course c = load_course(data);
    CHECK(c.file.find(kSlotRoad) && c.file.find(kSlotRoad)->tag == "GTRW");
    CHECK(c.file.find(kSlotScenery) && c.file.find(kSlotScenery)->tag == "GTM1");
    CHECK(std::fabs(c.road.length - 3922.f) < 1.f);
    CHECK(c.road.segments.size() == 114);
    CHECK(c.road.checkpoints.size() == 254);
    CHECK(c.road.grid.size() == 8);
    CHECK(c.road.walls.size() == 3);
    // Every grid slot sits on the road.
    for (const GridSlot& g : c.road.grid) CHECK(c.road.height_at(g.pos.x, g.pos.z).has_value());
    // AI line 0: control points lie on the road (X mirrored from the sim frame).
    CHECK(c.ai.lines.size() == 7 && c.ai.lines[0].points.size() == 33);
    size_t on_road = 0;
    for (const AiPoint& p : c.ai.lines[0].points) on_road += c.road.height_at(p.pos.x, p.pos.z).has_value();
    CHECK(on_road * 10 >= c.ai.lines[0].points.size() * 9);
    std::vector<Vec3> line = c.ai.lines[0].sample(c.road, 2.f);
    CHECK(line.size() > 1500); // a full ~3.9 km lap
    size_t line_on_road = 0;
    for (const Vec3& p : line) line_on_road += c.road.height_at(p.x, p.z).has_value();
    CHECK(line_on_road * 100 >= line.size() * 98);

    CourseModelOptions opt;
    opt.textures = false;
    scene::Model m = build_course_model(data, opt, "trialmt");
    size_t tris = 0;
    for (const scene::Mesh& mesh : m.meshes) tris += mesh.indices.size() / 3;
    CHECK(m.meshes.size() > 100 && tris > 50000);
}

TEST(course_all_us) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    size_t files = 0, ok = 0, moving_files = 0, moving_nodes = 0;
    for (const auto& e : disc->volume().entries()) {
        if (e.path.rfind("courses/US/", 0) != 0) continue;
        ++files;
        Bytes data = disc->volume().read(e);
        try {
            Course c = load_course(data);
            bool good = !c.road.segments.empty() && c.road.length > 0 && c.ai.lines.size() == 7;
            CourseModelOptions opt;
            opt.textures = false;
            good &= !build_course_model(data, opt).meshes.empty();
            // The +0x3C animators' nodes: every one builds and is drawn by its parts.
            std::vector<gtm1::AnimatedNodes> moving;
            good &= !build_course_model(data, opt, {}, nullptr, &moving).meshes.empty();
            for (gtm1::AnimatedNodes& a : moving) {
                moving_nodes += a.nodes().size();
                a.update(0.5f);
                for (const scene::Mesh& m : a.meshes()) good &= !m.primitives.empty();
            }
            moving_files += !moving.empty();
            ok += good;
        } catch (const std::exception& ex) {
            std::printf("  %s: %s\n", e.path.c_str(), ex.what());
        }
    }
    CHECK(files == 80);
    CHECK(ok == files);
    std::printf("  moving nodes: %zu in %zu files\n", moving_nodes, moving_files);
    CHECK(moving_files == 69 && moving_nodes == 428);
}

TEST(course_clut_animations_route11_disc) {
    // Route 11's grandstand camera flashes (formats/clut_animation.h): scenery texture set 13
    // animates the four palettes of its glow cards (records 12-15), fully transparent but for a
    // flash of a few hundredths of a second every 0.9-1.5 s.
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    const Volume::Entry* e = disc->volume().find("courses/US/route11");
    CHECK(e != nullptr);
    if (!e) return;
    Bytes data = disc->volume().read(*e);
    CourseModelOptions opt;
    opt.backdrop = false;
    std::vector<gtm1::ClutSet> sets;
    scene::Model m = build_course_model(data, opt, "route11", &sets);
    gtm1::ClutSet* glow = nullptr;
    for (gtm1::ClutSet& s : sets)
        if (s.textures.records() == std::vector<u32>{12, 13, 14, 15}) glow = &s;
    CHECK(sets.size() == 3 && glow != nullptr); // scenery sets 4, 13 and 14
    if (!glow) return;
    auto max_alpha = [](const std::vector<u32>& px) {
        u32 a = 0;
        for (u32 c : px) a = std::max(a, c >> 24);
        return a;
    };
    for (u32 r = 12; r < 16; ++r) {
        CHECK(max_alpha(m.textures[size_t(glow->first) + r].rgba) == 0); // the model starts dark
        CHECK(glow->textures.min_alpha(r) == 0);
    }
    ClutPlayer& p = glow->textures.player();
    CHECK(p.animations().size() == 4 && p.animations()[0].frames.size() == 12);
    p.update(0.012f); // inside the first flash: the glow palette
    CHECK(glow->textures.refresh().size() == 4);
    for (u32 r = 12; r < 16; ++r) CHECK(max_alpha(*glow->textures.rgba(r)) == 255);
    p.update(0.2f); // dark until the next flash
    glow->textures.refresh();
    for (u32 r = 12; r < 16; ++r) CHECK(max_alpha(*glow->textures.rgba(r)) == 0);
}

TEST(course_clut_animations_billboards_disc) {
    // Billboard sets (slot 30) decode cropped to REGION_CLAMP (Tex1::decode); their CLUT
    // animations must recolour textures of the same size (race::Billboards swaps them in).
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    size_t animated = 0;
    for (const char* name : {"courses/US/seattle", "courses/US/route5", "courses/US/route11"}) {
        const Volume::Entry* e = disc->volume().find(name);
        CHECK(e != nullptr);
        if (!e) continue;
        Bytes data = disc->volume().read(*e);
        CourseFile f = CourseFile::parse(data);
        ByteSpan b = f.section(data, 30);
        CHECK(b.size() > 0x28 && std::memcmp(b.data(), "BLBd", 4) == 0);
        if (b.size() <= 0x28) continue;
        ByteSpan td = b.subspan(rd32(b, 0x1C));
        Tex1 t = Tex1::parse(td);
        if (t.clut_animations.empty()) continue;
        GsMemory gs;
        t.upload(td, gs);
        ClutTextures c(td, true);
        CHECK(!c.empty());
        for (u32 r : c.records()) {
            CHECK(c.rgba(r)->size() == t.decode(gs, r).rgba.size());
            ++animated;
        }
    }
    CHECK(animated > 0);
}

TEST(course_animated_nodes_disc) {
    // The +0x3C animators' nodes (gtm1::AnimatedNodes): Route 11's flags (node 366, morphing
    // chunk ranges) and its backdrop Ferris wheel (slot 22, turning 1 degree per second), Apricot
    // Hill's flip-book crowds (type 5 chunk numbers), left out of the static models.
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    auto load = [&](const char* name) {
        const Volume::Entry* e = disc->volume().find(name);
        return e ? disc->volume().read(*e) : Bytes{};
    };
    Bytes r11 = load("courses/US/route11");
    CHECK(!r11.empty());
    if (r11.empty()) return;
    CourseModelOptions opt;
    opt.textures = false;
    opt.backdrop = false;
    std::vector<gtm1::AnimatedNodes> nodes;
    scene::Model m = build_course_model(r11, opt, "route11", nullptr, &nodes);
    CHECK(nodes.size() == 1 && nodes[0].nodes() == std::vector<u32>{366});
    for (const scene::Mesh& mesh : m.meshes) CHECK(mesh.name != "scenery/node366");
    if (nodes.size() != 1) return;
    std::vector<scene::Mesh> flags0 = nodes[0].meshes();
    nodes[0].update(0.25f);
    std::vector<scene::Mesh> flags1 = nodes[0].meshes();
    CHECK(flags0.size() == 1 && flags1.size() == 1);
    if (flags0.size() != 1 || flags1.size() != 1) return;
    size_t moved = 0;
    for (size_t i = 0; i < std::min(flags0[0].vertices.size(), flags1[0].vertices.size()); ++i)
        moved += std::memcmp(flags0[0].vertices[i].pos, flags1[0].vertices[i].pos, 12) != 0;
    CHECK(moved > 100); // the six flags wave

    CourseModelOptions bo;
    bo.textures = false;
    bo.scenery = bo.course_line = false;
    bo.backdrop = true;
    std::vector<gtm1::AnimatedNodes> wheel;
    build_course_model(r11, bo, "route11/backdrop", nullptr, &wheel);
    CHECK(wheel.size() == 1);
    if (wheel.size() == 1) {
        std::vector<scene::Mesh> w0 = wheel[0].meshes();
        wheel[0].update(45.0f); // 45 degrees on
        std::vector<scene::Mesh> w1 = wheel[0].meshes();
        CHECK(w0.size() == 1 && w1.size() == 1 && w0[0].vertices.size() == w1[0].vertices.size());
        if (w0.size() == 1 && w1.size() == 1 && w0[0].vertices.size() == w1[0].vertices.size()) {
            // The turning part moves rigidly: distances to one of its vertices are kept.
            const std::vector<scene::Vertex>&a = w0[0].vertices, &b = w1[0].vertices;
            auto dist = [](const float* p, const float* q) { return std::hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]); };
            size_t ref = a.size(), moved = 0, rigid = 0;
            for (size_t i = 0; i < a.size(); ++i) {
                if (dist(a[i].pos, b[i].pos) < 0.5f) continue;
                if (ref == a.size()) ref = i;
                ++moved;
                rigid += std::fabs(dist(a[i].pos, a[ref].pos) - dist(b[i].pos, b[ref].pos)) < 0.01f;
            }
            CHECK(moved * 2 > a.size());
            CHECK(rigid == moved);
        }
    }

    Bytes ap = load("courses/US/apricot");
    CHECK(!ap.empty());
    if (ap.empty()) return;
    std::vector<gtm1::AnimatedNodes> crowd;
    build_course_model(ap, opt, "apricot", nullptr, &crowd);
    CHECK(crowd.size() == 1 && crowd[0].nodes().size() == 19);
    if (crowd.size() != 1) return;
    std::map<u32, u32> before = crowd[0].operands();
    auto t0 = std::chrono::steady_clock::now();
    size_t verts = 0;
    for (int f = 0; f < 60; ++f) { // one second of frames
        crowd[0].update(1.0f / 60);
        for (const scene::Mesh& mesh : crowd[0].meshes()) verts += mesh.vertices.size();
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 60;
    std::printf("  apricot: %zu parts, %zu vertices per frame, %.3f ms per frame\n", crowd[0].part_count(),
                verts / 60, ms);
    size_t changed = 0;
    for (auto& [o, v] : crowd[0].operands()) changed += before.at(o) != v;
    CHECK(changed == crowd[0].operands().size()); // every crowd moved on (15-30 frames a second)
}

// Every GTM1 model of every course file on the disc loads (every folder, every slot). The European
// L_apricot1's small sky dome (slot 23) has a zeroed texture set: it builds without textures and
// draws nothing, the UK copy draws (public issue 97: licence test IB-8 did not start in French,
// German, Italian or Spanish).
TEST(course_models_load_every_file_disc) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    size_t files = 0, models = 0, failed = 0;
    for (const auto& e : disc->volume().entries()) {
        if (e.path.rfind("courses/", 0) != 0 || e.path == "courses/.list") continue; // .list: the folder's names
        Bytes data = disc->volume().read(e);
        CourseFile f = CourseFile::parse(data);
        ++files;
        for (int s = 0; s < kSlotCount; ++s) {
            ByteSpan sec = f.section(data, s);
            if (!gtm1::Gtm1::is_gtm1(sec)) continue;
            ++models;
            try {
                gtm1::Gtm1::parse(sec);
            } catch (const std::exception& x) {
                ++failed;
                std::printf("  %s slot %d: %s\n", e.path.c_str(), s, x.what());
            }
        }
    }
    std::printf("  %zu course files, %zu models\n", files, models);
    CHECK(files >= 80 && models > files && failed == 0);
    auto dome = [&](const char* path) {
        const Volume::Entry* e = disc->volume().find(path);
        if (!e) return scene::Model{};
        Bytes data = disc->volume().read(*e);
        CourseModelOptions o;
        o.scenery = o.course_line = o.backdrop = false;
        o.sky_object = true;
        return build_course_model(data, o, path);
    };
    auto primitives = [](const scene::Model& m) {
        size_t n = 0;
        for (const scene::Mesh& mesh : m.meshes) n += mesh.primitives.size();
        return n;
    };
    if (disc->volume().find("courses/EU/L_apricot1")) {
        scene::Model eu = dome("courses/EU/L_apricot1"), uk = dome("courses/UK/L_apricot1");
        std::printf("  L_apricot1 slot 23: EU %zu primitives, UK %zu\n", primitives(eu), primitives(uk));
        CHECK(eu.textures.empty() && primitives(eu) == 0 && !uk.textures.empty() && primitives(uk) > 0);
    }
}

// The licence course lines (slot 27, course.md "Course line") on every US course file: which files
// carry one, and that each decodes to the dashed 0.4 m ribbon on the road from the start
// line, coloured blue / white / red (and blends), with its node 0's state.
TEST(course_line_all_us) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    const std::vector<std::string> expected = {
        "L_deepforest1a", "L_deepforest1b", "L_deepforest2a", "L_grandvalley1", "L_grandvalley2",
        "L_grandvalley2a", "L_grandvalley2b", "L_laguna1", "L_midfield1", "L_midfield2",
        "L_midfield3", "L_seattle2", "L_trialmt1"};
    std::vector<std::string> found;
    for (const auto& e : disc->volume().entries()) {
        if (e.path.rfind("courses/US/", 0) != 0) continue;
        Bytes data = disc->volume().read(e);
        scene::Model m = course_line_model(data);
        CHECK(m.meshes.empty() == (CourseFile::parse(data).find(kSlotCourseLine) == nullptr));
        if (m.meshes.empty()) continue;
        std::string name = e.path.substr(11);
        found.push_back(name);
        Road road = Road::parse(CourseFile::parse(data).section(data, kSlotRoad));
        CHECK(m.meshes.size() == 1);
        size_t strips = 0, bad_colour = 0, bad_width = 0, on_road = 0, near_road = 0;
        for (const scene::Mesh& mesh : m.meshes) {
            for (const scene::Primitive& p : mesh.primitives) {
                CHECK(p.texture < 0);
                CHECK((p.flags & (scene::kUnlit | scene::kDoubleSided | scene::kNoDepthWrite | scene::kKeepAlpha)) ==
                      (scene::kUnlit | scene::kDoubleSided | scene::kNoDepthWrite | scene::kKeepAlpha));
                CHECK(!(p.flags & (scene::kAlphaBlend | scene::kAdditive)));
            }
            // Separate 4-vertex strips (a dash each): two vertex pairs 0.4 m apart.
            CHECK(mesh.vertices.size() % 4 == 0 && mesh.indices.size() * 2 == mesh.vertices.size() * 3);
            for (size_t i = 0; i + 1 < mesh.vertices.size(); i += 2) {
                const scene::Vertex& a = mesh.vertices[i];
                const scene::Vertex& b = mesh.vertices[i + 1];
                float w = std::hypot(a.pos[0] - b.pos[0], a.pos[1] - b.pos[1], a.pos[2] - b.pos[2]);
                bad_width += std::fabs(w - 0.4f) > 0.01f;
                float x = (a.pos[0] + b.pos[0]) / 2, y = (a.pos[1] + b.pos[1]) / 2, z = (a.pos[2] + b.pos[2]) / 2;
                if (auto h = road.height_at(x, z, y + 1.0f)) {
                    ++on_road;
                    near_road += y - *h > 0.0f && y - *h < 0.3f;
                }
            }
            strips += mesh.vertices.size() / 4;
            for (const scene::Vertex& v : mesh.vertices) {
                // Untextured pre-lit colours as the GS shows them (course.md "Course line"): one value
                // per dash, blue (c, c, 255 - c) through grey to red (255 - c, c, c); alpha 0x80 = 1.0.
                int r = int(v.color & 0xFF), g = int((v.color >> 8) & 0xFF), bl = int((v.color >> 16) & 0xFF);
                bool blue_side = r == g && std::abs(r + bl - 255) <= 1, red_side = g == bl && std::abs(r + g - 255) <= 1;
                bad_colour += !(blue_side || red_side) || (v.color >> 24) != 255;
            }
        }
        CHECK(bad_colour == 0 && bad_width == 0);
        size_t pairs = strips * 2;
        CHECK(on_road * 100 >= pairs * 98);
        CHECK(near_road * 100 >= on_road * 95);
        // From the start line (track distance 0, about 2 m before it) for 480-1450 m of the section.
        const scene::Vertex& first = m.meshes[0].vertices[0];
        float best = 1e30f, at = 0;
        for (const Checkpoint& k : road.checkpoints) {
            float d = std::hypot((k.x0 + k.x1) / 2 - first.pos[0], (k.z0 + k.z1) / 2 - first.pos[2]);
            if (d < best) best = d, at = k.distance;
        }
        CHECK(best < 20.0f && (at < 20.0f || at > road.length - 20.0f));
        CHECK(strips > 100 && strips < 400);
        std::printf("  %-16s %zu dashes\n", name.c_str(), strips);
    }
    std::sort(found.begin(), found.end());
    CHECK(found == expected);
}

// Another game's course read as GT3's: numbers that point past the course's own tables (an AI line's segment used as the
// ground query's hint, a segment's checkpoint id) are no segment / no checkpoint, not a read past the tables. A course of one
// triangle in one 8 m grid cell (invented values).
TEST(course_query_foreign_indices) {
    using gt3r::ee::F;
    Road r;
    r.length = 100;
    RoadSegment s;
    s.vertices = {{0, 0, 0}, {8, 0, 0}, {0, 0, 8}};
    s.vertex_attrs = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    RoadTriangle t;
    t.a = 0, t.b = 1, t.c = 2;
    s.triangles = {t};
    s.checkpoint_ids = {7}; // the course has one checkpoint
    r.segments = {s};
    r.checkpoints = {Checkpoint{0, 0, 8, 0, 0, 0, 1, 0}};
    r.grid_index.xs = {0, 8}, r.grid_index.zs = {0, 8};
    r.grid_index.cell_begin = {0, 1}, r.grid_index.refs = {{0, 0}};
    r.grid_index.cell_group_begin = {0, 1}, r.grid_index.groups = {{0, 0, 1}};
    const sim::exact::CourseQuery q(r);
    sim::exact::Ground g;
    g.x = F(2.0f), g.z = F(2.0f), g.h = F(0.0f);
    for (s32 hint : {-1, 0, 5}) {
        g.seg = hint;
        q.ground(g);
        if (hint < 1) CHECK(g.miss == 0 && g.seg == 0);
        else CHECK(g.miss == 1 && g.seg == -1); // a hint past the segments has no neighbours
    }
    CHECK(q.progress(F(2.0f), F(0.0f), F(2.0f), 0).bits == 0 && q.progress(F(2.0f), F(0.0f), F(2.0f), -1).bits == 0);
}
