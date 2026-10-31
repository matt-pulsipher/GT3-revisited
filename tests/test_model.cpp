// Tests for the PS2 VIF/GIF helpers and the GTM1 car/wheel model decoders.

#include "test_framework.h"

#include "formats/course/course_model.h"
#include "formats/model/car_model.h"
#include "formats/model/gtm1.h"
#include "formats/model/tyre_model.h"
#include "formats/model/wheel_model.h"
#include "formats/model/wheel_spec.h"
#include "formats/ps2/vif.h"
#include "game/data/car_database.h"

#include <array>
#include <cmath>
#include <cstring>

using namespace gt3r;

namespace {

void put32(Bytes& b, u32 v) {
    for (int i = 0; i < 4; ++i) b.push_back(u8(v >> (8 * i)));
}
void putf(Bytes& b, float f) {
    u32 v;
    std::memcpy(&v, &f, 4);
    put32(b, v);
}
void pad4(Bytes& b) {
    while (b.size() % 4) b.push_back(0);
}

// One VIF packet: GIF tag, 4 positions in one strip, normals, UVs.
Bytes make_packet() {
    Bytes b;
    put32(b, 0x6801C0C0);                          // UNPACK V3-32 num=1 @0xC0 (GIF tag)
    put32(b, 0x00008004), put32(b, 0x303E4000), put32(b, 0x00001412); // NLOOP 4, EOP, PRE, PRIM 0x7C, 3 regs
    put32(b, 0x6804C000);                          // positions
    const float pos[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    for (auto& p : pos) putf(b, p[0]), putf(b, p[1]), putf(b, p[2]);
    put32(b, 0x6202C040);                          // S-8 num=2 @0x40: 1 strip of (4-2)*3
    b.push_back(1), b.push_back(6);
    pad4(b);
    put32(b, 0x6804C080);                          // normals
    for (int i = 0; i < 4; ++i) putf(b, 0), putf(b, 0), putf(b, 1);
    put32(b, 0x7404C040);                          // masked V2-32 @0x40: UVs
    for (auto& p : pos) putf(b, p[0]), putf(b, p[1]);
    while (b.size() % 16) put32(b, 0);
    return b;
}

// Minimal GTM1: 1 node (draw chunk 0), 1 chunk with the packet above (or `pkt`), no textures.
Bytes make_gtm1(const Bytes& pkt = make_packet(), u8 chunk_flags = 1) {
    Bytes g(0x40, 0);
    std::memcpy(g.data(), "GTM1", 4);
    g[0x10] = 1; // nodes
    g[0x12] = 1; // chunks
    auto set32 = [&](size_t o, u32 v) { for (int i = 0; i < 4; ++i) g[o + i] = u8(v >> (8 * i)); };
    set32(0x20, 0x40);
    set32(0x24, 0x44);
    g.resize(0x48, 0);
    set32(0x40, 0x50); // node 0 program
    set32(0x44, 0x60); // chunk 0
    g.resize(0x50, 0);
    // program: 04 00 (draw chunk 0), 00 (end)
    g.insert(g.end(), {0x04, 0x00, 0x00});
    g.resize(0x60, 0);
    // chunk header + one entry (offset relative to chunk), then the packet
    Bytes c(0x20, 0);
    c[8] = chunk_flags, c[10] = 1, c[12] = 4, c[14] = 2;
    c[16] = 0x20, c[20] = u8(pkt.size() / 16), c[22] = 0x00;
    g.insert(g.end(), c.begin(), c.end());
    g.insert(g.end(), pkt.begin(), pkt.end());
    return g;
}

// The same chunk under two nodes: node 0 only changes render state (`state_ops`, then 00),
// node 1 draws chunk 0 (04 00 00). The layout of the licence course line (course.md "Course line").
Bytes make_gtm1_state_node(const Bytes& state_ops) {
    Bytes pkt = make_packet();
    Bytes g(0x40, 0);
    std::memcpy(g.data(), "GTM1", 4);
    g[0x10] = 2; // nodes
    g[0x12] = 1; // chunks
    auto set32 = [&](size_t o, u32 v) { for (int i = 0; i < 4; ++i) g[o + i] = u8(v >> (8 * i)); };
    set32(0x20, 0x40); // node table
    set32(0x24, 0x48); // chunk table
    g.resize(0x50, 0);
    set32(0x40, 0x50); // node 0 program
    set32(0x44, 0x70); // node 1 program
    set32(0x48, 0x80); // chunk 0
    g.insert(g.end(), state_ops.begin(), state_ops.end());
    g.push_back(0x00);
    g.resize(0x70, 0);
    g.insert(g.end(), {0x04, 0x00, 0x00});
    g.resize(0x80, 0);
    Bytes c(0x20, 0);
    c[8] = 1, c[10] = 1, c[12] = 4, c[14] = 2;
    c[16] = 0x20, c[20] = u8(pkt.size() / 16), c[22] = 0x00;
    g.insert(g.end(), c.begin(), c.end());
    g.insert(g.end(), pkt.begin(), pkt.end());
    return g;
}

// Culling off (2E), depth write off (25), FBMSK 0xFF000000 (24 + u32): colour only.
const Bytes kLineStateOps = {0x2E, 0x25, 0x24, 0x00, 0x00, 0x00, 0xFF};

// One VIF packet of `make_packet`'s quad moved `dx` along X.
Bytes make_packet_at(float dx) {
    Bytes b = make_packet();
    for (int i = 0; i < 4; ++i) {
        float x;
        std::memcpy(&x, &b[20 + 12 * size_t(i)], 4);
        x += dx;
        std::memcpy(&b[20 + 12 * size_t(i)], &x, 4);
    }
    return b;
}

// Two nodes moved by +0x3C animators, two chunks (the quad, and the quad at x + 10):
//   node 0: push, rotate about Y (type 3 animator, 90 deg/s), op 0x34 phase (type 7, 0.5 per s),
//           op 0x35 chunks 0..1, pop;
//   node 1: draw chunk (type 5 animator: chunks 0..1, 1 per s).
Bytes make_animated_gtm1() {
    Bytes g(0xC0, 0);
    std::memcpy(g.data(), "GTM1", 4);
    auto set16 = [&](size_t o, u16 v) { g[o] = u8(v), g[o + 1] = u8(v >> 8); };
    auto set32 = [&](size_t o, u32 v) { for (int i = 0; i < 4; ++i) g[o + i] = u8(v >> (8 * i)); };
    auto setf = [&](size_t o, float f) { u32 v; std::memcpy(&v, &f, 4); set32(o, v); };
    g[0x10] = 2, g[0x12] = 2;
    set32(0x20, 0x40), set32(0x24, 0x48), set32(0x3C, 0x50);
    set32(0x40, 0xA0), set32(0x44, 0xB4);
    // Animators: count 3 at 0x50, entries 0x60 / 0x74 / 0x88.
    set32(0x50, 3), set32(0x54, 0x60), set32(0x58, 0x74), set32(0x5C, 0x88);
    set16(0x60, 3), set16(0x62, 1), setf(0x64, 90), setf(0x68, 0), set32(0x70, 0xA2);
    set16(0x74, 7), set16(0x76, 1), setf(0x78, 0.5f), setf(0x7C, 0), set32(0x84, 0xA7);
    set16(0x88, 5), set16(0x8A, 1), setf(0x8C, 1), set16(0x90, 0), set16(0x92, 2), setf(0x94, 0), set32(0x9C, 0xB5);
    // node 0: 0C | 15 f32 | 34 f32 | 35 u16 0, u16 2 | 0D | 00
    const u8 p0[] = {0x0C, 0x15, 0, 0, 0, 0, 0x34, 0, 0, 0, 0, 0x35, 0, 0, 2, 0, 0x0D, 0x00};
    std::memcpy(&g[0xA0], p0, sizeof(p0));
    // node 1: 05 u16 0 | 00
    const u8 p1[] = {0x05, 0, 0, 0x00};
    std::memcpy(&g[0xB4], p1, sizeof(p1));
    for (int c = 0; c < 2; ++c) {
        set32(0x48 + 4 * size_t(c), u32(g.size()));
        Bytes pkt = make_packet_at(10.0f * float(c));
        Bytes h(0x20, 0);
        h[8] = 1, h[10] = 1, h[12] = 4, h[14] = 2;
        h[16] = 0x20, h[20] = u8(pkt.size() / 16);
        g.insert(g.end(), h.begin(), h.end());
        g.insert(g.end(), pkt.begin(), pkt.end());
    }
    return g;
}

// As the formula cars' steering: node 0 = op 0x06 callback id 5 (no branches), op 0x35 chunks 0..1;
// node 1 = a plain draw of chunk 0. No animators.
Bytes make_callback_gtm1() {
    Bytes g = make_animated_gtm1();
    std::fill(g.begin() + 0x3C, g.begin() + 0x40, u8(0));
    std::fill(g.begin() + 0xA0, g.begin() + 0xC0, u8(0));
    const u8 p0[] = {0x06, 5, 0, 0, 0x35, 0, 0, 2, 0, 0x00};
    std::memcpy(&g[0xA0], p0, sizeof(p0));
    const u8 p1[] = {0x05, 0, 0, 0x00};
    std::memcpy(&g[0xB4], p1, sizeof(p1));
    return g;
}

} // namespace

TEST(gtm1_car_morph_draws_synthetic) {
    Bytes data = make_callback_gtm1();
    gtm1::Gtm1 g = gtm1::Gtm1::parse(data);
    CHECK(g.animators.empty());
    auto x1 = [](const scene::Mesh& m) { return m.vertices.size() > 1 ? m.vertices[1].pos[0] : -1.0f; };
    // Car programs read the callback: the steering's default 0.5 x 2 chunks -> chunk 1 (x + 10).
    gtm1::Options o;
    o.run.callback = gtm1::kCarCallbackDefaults.data();
    scene::Model m = gtm1::build_model(g, o);
    CHECK(m.meshes.size() == 2 && x1(m.meshes[0]) == 11 && x1(m.meshes[1]) == 1);
    // With Options::morphs that draw is left out and kept with both chunks.
    gtm1::MorphDraws md;
    o.morphs = &md;
    m = gtm1::build_model(g, o);
    CHECK(m.meshes.size() == 1 && m.meshes[0].name == "node1");
    CHECK(md.size() == 1 && md.ids() == std::vector<int>{5});
    auto at = [&](float steer) {
        std::array<float, 5> v = gtm1::kCarCallbackDefaults;
        v[4] = steer;
        std::vector<scene::Mesh> ms = md.meshes(v);
        return ms.size() == 1 ? x1(ms[0]) : -1.0f;
    };
    CHECK(at(0) == 1 && at(0.49f) == 1 && at(0.5f) == 11 && at(1) == 11);
    CHECK(at(-3) == 1 && at(7) == 11); // clamped to 0..1
    // Without the callback (not a car) op 0x06 id 5 is a plain switch and the phase stays 0.
    gtm1::Options c;
    gtm1::MorphDraws none;
    c.morphs = &none;
    m = gtm1::build_model(g, c);
    CHECK(m.meshes.size() == 2 && x1(m.meshes[0]) == 1 && none.empty());
}

TEST(gtm1_animated_nodes_synthetic) {
    Bytes data = make_animated_gtm1();
    gtm1::Gtm1 g = gtm1::Gtm1::parse(data);
    CHECK(g.animators.size() == 3);
    if (g.animators.size() != 3) return;
    CHECK(g.animators[0].type == 3 && g.animators[0].speed == 90 && g.animators[0].targets == std::vector<u32>{0xA2});
    CHECK(g.animators[2].type == 5 && g.animators[2].first == 0 && g.animators[2].end == 2);
    CHECK(g.animated_nodes() == (std::vector<u32>{0, 1}));
    // Without Options::animated the nodes are built as stored (rotation 0, phase 0: chunk 0).
    CHECK(gtm1::build_model(g, {}).meshes.size() == 2);

    gtm1::AnimatedNodes an;
    gtm1::Options o;
    o.animated = &an;
    CHECK(gtm1::build_model(g, o).meshes.empty()); // both nodes move
    CHECK(an.nodes() == (std::vector<u32>{0, 1}) && an.part_count() == 2);
    auto vertex = [](const std::vector<scene::Mesh>& ms, size_t mesh, size_t v) {
        const float* p = ms[mesh].vertices[v].pos;
        return std::array<float, 3>{p[0], p[1], p[2]};
    };
    std::vector<scene::Mesh> a = an.meshes(); // reset: angle 0, phase 0, chunk 0
    CHECK(a.size() == 2 && a[0].vertices.size() == 4 && a[1].vertices.size() == 4);
    if (a.size() != 2) return;
    CHECK((vertex(a, 0, 1) == std::array<float, 3>{1, 0, 0}) && (vertex(a, 1, 1) == std::array<float, 3>{1, 0, 0}));

    an.update(1.0f); // 90 deg, phase 0.5 -> chunk 1; flip-book 1 -> chunk 1
    CHECK(an.operands().at(0xB5) == 1);
    std::vector<scene::Mesh> b = an.meshes();
    CHECK(b.size() == 2);
    if (b.size() != 2) return;
    std::array<float, 3> r = vertex(b, 0, 1); // (11, 0, 0) turned 90 degrees about Y
    CHECK(std::fabs(r[0]) < 1e-4f && std::fabs(r[1]) < 1e-6f && std::fabs(std::fabs(r[2]) - 11.0f) < 1e-4f);
    CHECK((vertex(b, 1, 1) == std::array<float, 3>{11, 0, 0}));

    an.update(0.6f); // flip-book phase 1.6 rounds to 2: wrapped back to chunk 0
    CHECK(an.operands().at(0xB5) == 0);
    an.reset();
    CHECK(an.operands().at(0xB5) == 0 && (vertex(an.meshes(), 0, 1) == std::array<float, 3>{1, 0, 0}));
}

TEST(vif_parse_and_unpack) {
    Bytes b = make_packet();
    std::vector<ps2::VifCommand> cmds = ps2::parse_vif(b);
    CHECK(cmds.size() >= 5);
    CHECK(cmds[0].is_unpack());
    ps2::VifUnpack u = cmds[1].unpack();
    CHECK(u.vn == 2 && u.vl == 0 && u.count == 4 && u.addr == 0 && u.usn && u.flg && !u.masked);
    auto f = ps2::unpack_floats(u);
    CHECK(f[3][0] == 1.0f && f[3][1] == 1.0f);
    CHECK(cmds[4].unpack().masked);
    ps2::GifTag t = ps2::GifTag::decode(cmds[0].payload);
    CHECK(t.nloop == 4 && t.eop && t.pre && t.nreg == 3);
    CHECK(t.reg(0) == ps2::kGifSt && t.reg(1) == ps2::kGifRgbaq && t.reg(2) == ps2::kGifXyzf2);
    ps2::Prim p = ps2::Prim::decode(t.prim);
    CHECK(p.type == 4 && p.iip && p.tme && p.fge && p.abe);
}

TEST(vif_signed_8bit) {
    Bytes b;
    put32(b, 0x6E02C000 & ~0x4000u); // V4-8 signed, num=2
    for (int v : {0x80, 0x7F, 0xFF, 0x00, 1, 2, 3, 4}) b.push_back(u8(v));
    auto cmds = ps2::parse_vif(b);
    auto w = ps2::unpack_words(cmds[0].unpack());
    CHECK(s32(w[0][0]) == -128 && w[0][1] == 127 && s32(w[0][2]) == -1 && w[1][3] == 4);
}

// The quad with vertex colours (V4-8 at 0x80: RGB 0x80, 0xFF, 0x40, alpha 0x80) and the GIF
// tag's PRIM word `prim` (0x7C textured, 0x2C untextured).
Bytes make_coloured_packet(u32 prim) {
    Bytes b = make_packet();
    const u32 tag1 = 0x30004000u | (prim << 15); // NREG 3, PRE, PRIM
    for (int i = 0; i < 4; ++i) b[8 + size_t(i)] = u8(tag1 >> (8 * i));
    put32(b, 0x6E04C080); // after the padding (VIF no-ops): V4-8 num=4 @0x80, colours
    for (int i = 0; i < 4; ++i) b.push_back(0x80), b.push_back(0xFF), b.push_back(0x40), b.push_back(0x80);
    while (b.size() % 16) put32(b, 0);
    return b;
}

// A GS vertex colour is 0x80 = full as a factor: a texture's MODULATE, or the VU's lighting and
// reflection maths. A pre-lit chunk (kind 0) passes it straight through, and an untextured
// primitive then shows it as it is (the licence course line's mid-grey dashes). Alpha, a blend
// weight, is 0x80 = full either way.
TEST(gtm1_untextured_colours_as_the_gs_shows_them) {
    auto colour = [](u32 prim, u8 kind) -> u32 {
        scene::Model m = gtm1::decode_gtm1(make_gtm1(make_coloured_packet(prim), kind));
        return m.meshes.empty() || m.meshes[0].vertices.empty() ? 0 : m.meshes[0].vertices[0].color;
    };
    auto rgb_is = [](u32 c, u32 r, u32 g, u32 b) { return (c & 0xFF) == r && ((c >> 8) & 0xFF) == g && ((c >> 16) & 0xFF) == b; };
    const u32 prelit_textured = colour(0x7C, 0), prelit_untextured = colour(0x2C, 0), lit_untextured = colour(0x2C, 1);
    CHECK(rgb_is(prelit_textured, 255, 255, 128));
    CHECK(rgb_is(prelit_untextured, 128, 255, 64));
    CHECK(rgb_is(lit_untextured, 255, 255, 128));
    CHECK((prelit_textured >> 24) == 255 && (prelit_untextured >> 24) == 255 && (lit_untextured >> 24) == 255);
}

TEST(gtm1_synthetic_model) {
    Bytes g = make_gtm1();
    scene::Model m = gtm1::decode_gtm1(g);
    CHECK(m.meshes.size() == 1);
    if (m.meshes.empty()) return;
    const scene::Mesh& mesh = m.meshes[0];
    CHECK(mesh.vertices.size() == 4);
    CHECK(mesh.indices.size() == 6);
    CHECK(mesh.primitives.size() == 1 && mesh.primitives[0].texture == -1);
    // Counter-clockwise when seen from +Z, matching the +Z normals.
    const float* a = mesh.vertices[mesh.indices[0]].pos;
    const float* b = mesh.vertices[mesh.indices[1]].pos;
    const float* c = mesh.vertices[mesh.indices[2]].pos;
    float cross_z = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    CHECK(cross_z > 0);
    CHECK(m.bounds_max[0] == 1.0f && m.bounds_max[1] == 1.0f);
}

// A texture set whose data is not a Tex1 (zeros: courses/EU/L_apricot1's slot 23) is a set without
// textures, as the game sets it up: the model loads and its textured packet is not drawn (the game
// draws it with whatever texture the GS still holds); an untextured packet still is. An offset past
// the model is still an error.
TEST(gtm1_texture_set_not_tex1) {
    auto with_set = [](u32 set_offset, u8 mat) {
        Bytes g = make_gtm1();
        auto set32 = [&](size_t o, u32 v) { for (int i = 0; i < 4; ++i) g[o + i] = u8(v >> (8 * i)); };
        g[0x60 + 0x16] = mat; // the packet's mat: 1 = the set's first texture, 0 = none
        u32 tab = u32(g.size());
        g.resize(tab + 0x10 + 0x80, 0); // the set table, then a zeroed 0x80-byte set
        g[0x16] = 1;
        set32(0x2C, tab);
        set32(tab, set_offset ? set_offset : tab + 0x10);
        return g;
    };
    auto primitives = [](const scene::Model& m) {
        size_t n = 0;
        for (const scene::Mesh& mesh : m.meshes) n += mesh.primitives.size();
        return n;
    };
    Bytes g = with_set(0, 1);
    gtm1::Gtm1 parsed = gtm1::Gtm1::parse(g);
    CHECK(parsed.texsets.size() == 1);
    scene::Model m = gtm1::decode_gtm1(g);
    CHECK(m.textures.empty() && primitives(m) == 0);
    CHECK(primitives(gtm1::decode_gtm1(with_set(0, 0))) == 1);
    bool threw = false;
    try {
        gtm1::Gtm1::parse(with_set(0x10000, 1));
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

// gtm1::Options::carry_state: the game's whole-model draw sets the default state once and runs
// the nodes in order, so a state-only node 0 applies to node 1's draw.
TEST(gtm1_state_carried_between_nodes) {
    Bytes g = make_gtm1_state_node(kLineStateOps);
    gtm1::Options o;
    o.gs_state = true;
    o.frame_alpha = true;
    auto flags_of = [](const scene::Model& m) {
        u32 f = 0;
        for (const scene::Mesh& mesh : m.meshes)
            for (const scene::Primitive& p : mesh.primitives) f |= p.flags;
        return f;
    };
    const u32 line_state = scene::kDoubleSided | scene::kNoDepthWrite | scene::kKeepAlpha;
    scene::Model fresh = gtm1::decode_gtm1(g, o);
    CHECK(fresh.meshes.size() == 1); // node 0 draws nothing
    CHECK((flags_of(fresh) & line_state) == 0); // every node from the default state
    o.carry_state = true;
    scene::Model carried = gtm1::decode_gtm1(g, o);
    CHECK(carried.meshes.size() == 1 && carried.meshes[0].name == "node1");
    CHECK((flags_of(carried) & line_state) == line_state);
    o.node = 1; // a node picked alone still starts from what the nodes before it left
    CHECK((flags_of(gtm1::decode_gtm1(g, o)) & line_state) == line_state);
    o.node = -1;
    // Ops that turn the state back on in node 0 leave the default.
    Bytes back = make_gtm1_state_node({0x2E, 0x25, 0x2D, 0x26});
    CHECK((flags_of(gtm1::decode_gtm1(back, o)) & line_state) == 0);
    // A called node (op 02) starts from the default state, as the game's call does.
    gtm1::Gtm1 parsed = gtm1::Gtm1::parse(g);
    gtm1::Draw state;
    CHECK(parsed.run(0, {}, &state).empty());
    CHECK(!state.cull && !state.depth_write && state.fb_mask == 0xFF000000u);
    std::vector<gtm1::Draw> draws = parsed.run(1, {}, &state);
    CHECK(draws.size() == 1 && !draws[0].cull && !draws[0].depth_write);
    CHECK(parsed.run(1).size() == 1 && parsed.run(1)[0].cull);
}

// The licence course line (course slot 27, course.md "Course line"): its own model with the state of
// its node 0; none on a course file without the slot.
TEST(course_line_synthetic) {
    Bytes line = make_gtm1_state_node(kLineStateOps);
    auto course_with = [&](bool slot27) {
        Bytes f(course::kSlotCount * 4, 0);
        if (slot27) {
            u32 off = u32(f.size());
            for (int i = 0; i < 4; ++i) f[course::kSlotCourseLine * 4 + i] = u8(off >> (8 * i));
            f.insert(f.end(), line.begin(), line.end());
        }
        return f;
    };
    scene::Model none = course::course_line_model(course_with(false));
    CHECK(none.meshes.empty());
    scene::Model m = course::course_line_model(course_with(true));
    CHECK(m.meshes.size() == 1);
    size_t prims = 0;
    for (const scene::Mesh& mesh : m.meshes)
        for (const scene::Primitive& p : mesh.primitives) {
            ++prims;
            CHECK((p.flags & (scene::kDoubleSided | scene::kNoDepthWrite | scene::kKeepAlpha)) ==
                  (scene::kDoubleSided | scene::kNoDepthWrite | scene::kKeepAlpha));
        }
    CHECK(prims == 1);
    // The merged course model keeps it unless asked not to.
    course::CourseModelOptions o;
    o.textures = false;
    CHECK(course::build_course_model(course_with(true), o).meshes.size() == 1);
    o.course_line = false;
    CHECK(course::build_course_model(course_with(true), o).meshes.empty());
}

TEST(car_model_disc) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Bytes data = disc->volume().read(*disc->volume().find("cars/day/am0006"));
    CHECK(CarFile::is_car(data));
    for (int lod = 0; lod < 3; ++lod) {
        CarModelOptions o;
        o.lod = lod;
        scene::Model m = load_car_model(data, o, "am0006");
        CHECK(!m.meshes.empty() && m.meshes[0].name == "body");
        CHECK(!m.textures.empty());
        // A car is roughly 4-5 m long, 1.2-1.4 m tall, 1.7-2.1 m wide.
        float len = m.bounds_max[2] - m.bounds_min[2];
        CHECK(len > 3.5f && len < 5.5f);
        CHECK(m.bounds_max[1] > 1.0f && m.bounds_max[1] < 1.6f);
        bool textured = false;
        for (const scene::Primitive& p : m.meshes[0].primitives) textured |= p.texture >= 0;
        CHECK(textured);
    }
    CarModelOptions o;
    o.wheels = false;
    scene::Model lod0 = load_car_model(data, o);
    size_t tris = lod0.meshes[0].indices.size() / 3;
    CHECK(tris > 2000 && tris < 6000);
}

// The cars whose node programs morph parts by the car's callback (op 0x06 ids 1-5, op 0x35): the rally
// cars' mud flaps and two race cars' aerials (ids 1-4, the wheels' pendulums), the formula cars'
// driver's arms and steering wheel (id 5).
TEST(car_morph_draws_disc) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    const Volume& vol = disc->volume();
    std::map<std::string, std::vector<int>> found; // model -> LOD 0's callback ids
    std::map<std::string, size_t> draws;
    size_t cars = 0;
    for (const Volume::Entry& e : vol.entries()) {
        if (e.path.rfind("cars/day/", 0) != 0 || e.size == 0) continue;
        std::optional<Bytes> data = vol.read(e);
        ++cars;
        std::vector<gtm1::MorphDraws> morphs;
        CarModelOptions o;
        o.lod = 0;
        o.wheels = false;
        o.morphs = &morphs;
        scene::Model m = load_car_model(*data, o);
        if (morphs.empty()) continue;
        std::string id = e.path.substr(e.path.rfind('/') + 1);
        found[id] = morphs[0].ids();
        draws[id] = morphs[0].size();
        // The parts move with their value, and the body leaves them out.
        std::array<float, 5> lo{}, hi{1, 1, 1, 1, 1};
        std::vector<scene::Mesh> a = morphs[0].meshes(lo), b = morphs[0].meshes(hi);
        bool moved = a.size() == 1 && b.size() == 1 && a[0].vertices.size() == b[0].vertices.size();
        float most = 0;
        for (size_t i = 0; moved && i < a[0].vertices.size(); ++i)
            for (int k = 0; k < 3; ++k) most = std::max(most, std::fabs(a[0].vertices[i].pos[k] - b[0].vertices[i].pos[k]));
        std::printf("  %s: %zu morph draws, ids", id.c_str(), draws[id]);
        for (int i : found[id]) std::printf(" %d", i);
        std::printf(", parts move %.3f m\n", most);
        CHECK(moved && most > 0.01f && most < 1.0f);
        CarModelOptions s;
        s.lod = 0;
        s.wheels = false;
        size_t with = load_car_model(*data, s).meshes[0].indices.size(), without = m.meshes[0].indices.size();
        CHECK(without < with);
    }
    CHECK(cars == 286);
    const std::map<std::string, std::vector<int>> expect = {
        {"la0008", {4}},          {"la0011", {3, 4}}, {"ma0095", {1, 2}}, {"ma0099", {3, 4}},
        {"ml0003", {5}},          {"pd0010", {5}},    {"ty0032", {1, 2, 3, 4}},
    };
    CHECK(found == expect);
    CHECK(draws["ty0032"] == 4 && draws["la0011"] == 2 && draws["ml0003"] == 1); // (LOD 1 has as many)
}

TEST(wheel_model_disc) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    size_t n = 0;
    for (const char* id : {"wheel/am0006", "wheel/enw001_s", "wheel/not_exist"}) {
        Bytes data = disc->volume().read(*disc->volume().find(id));
        CHECK(WheelFile::is_wheel(data));
        scene::Model m = load_wheel_model(data);
        CHECK(!m.meshes.empty());
        // Unit-scale rim: radius about 1.
        CHECK(m.bounds_max[1] > 0.8f && m.bounds_max[1] < 1.5f);
        ++n;
    }
    CHECK(n == 3);
}

TEST(car_paint_variants_disc) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Bytes data = disc->volume().read(*disc->volume().find("cars/day/am0006"));
    CarFile f = CarFile::parse(data);
    gtm1::Gtm1 g = gtm1::Gtm1::parse(f.body);
    CHECK(g.texsets.size() == 4);
    ByteSpan set0 = f.body.subspan(g.texsets[0]);
    std::vector<gtm1::PaintVariant> v = gtm1::paint_variants(set0);
    CHECK(v.size() == 7); // seven colours in database/carcolor.db for am0006
    if (v.size() < 7) return;
    // Texture 19 (512x128 side panel) is painted: British Racing Green by default (TEX0.CSA honoured),
    // Braemar Blue for colour 3.
    auto mean = [](const scene::Texture& t) {
        double s[3] = {0, 0, 0};
        for (u32 p : t.rgba) for (int k = 0; k < 3; ++k) s[k] += (p >> (8 * k)) & 0xFF;
        for (double& x : s) x /= double(t.rgba.size());
        return std::array<double, 3>{s[0], s[1], s[2]};
    };
    std::vector<scene::Texture> t0, t3;
    gtm1::decode_texture_set(set0, t0, "", 0);
    gtm1::decode_texture_set(set0, t3, "", 3);
    CHECK(t0.size() == 43 && t3.size() == 43);
    auto g0 = mean(t0[19]), b3 = mean(t3[19]);
    CHECK(g0[1] > g0[0] + 5 && g0[1] > g0[2]); // green
    CHECK(b3[2] > b3[0] + 10);                   // blue
}

TEST(car_alpha_test_and_reflection_disc) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Bytes data = disc->volume().read(*disc->volume().find("cars/day/ni0137")); // R34 GT-R V-spec
    CarModelOptions o;
    o.wheels = false;
    scene::Model m = load_car_model(data, o, "ni0137");
    CHECK(!m.meshes.empty());
    if (m.meshes.empty()) return;
    // The body's side panels are alpha-tested (wheel-arch cut-outs) and the reflection passes
    // become additive environment layers weighted by vertex alpha.
    size_t cut = 0, refl = 0, weighted = 0, per_pixel = 0;
    const scene::Mesh& body = m.meshes[0];
    for (const scene::Primitive& p : body.primitives) {
        if (p.flags & scene::kAlphaTest) {
            ++cut;
            CHECK(p.texture >= 0);
            if (p.texture >= 0)
                for (u32 c : m.textures[size_t(p.texture)].rgba) CHECK((c >> 24) == 0 || (c >> 24) == 255);
        }
        u32 want = scene::kAdditive | scene::kEnvironmentMap | scene::kUnlit | scene::kNoDepthWrite;
        // Reflection triangles over one textured pass carry that texture: its alpha is the reflectivity.
        if ((p.flags & want) == want && p.texture >= 0) ++per_pixel;
        if ((p.flags & want) == want && p.texture < 0) {
            ++refl;
            for (u32 i = 0; i < p.index_count; ++i)
                if ((body.vertices[body.indices[p.first_index + i]].color >> 24) > 0) { ++weighted; break; }
        }
    }
    CHECK(cut > 0);
    CHECK(refl > 0);
    CHECK(weighted > 0);
    CHECK(per_pixel > 0);
}

namespace {

float radial(const scene::Vertex& v) { return std::sqrt(v.pos[1] * v.pos[1] + v.pos[2] * v.pos[2]); }

// Vertices a primitive's triangles use.
std::vector<const scene::Vertex*> prim_vertices(const scene::Mesh& m, const scene::Primitive& p) {
    std::vector<const scene::Vertex*> out;
    for (u32 i = p.first_index; i < p.first_index + p.index_count; ++i) out.push_back(&m.vertices[m.indices[i]]);
    return out;
}

} // namespace

// The tyre ring as the game builds it (the SLK 230's front tyre, 205/55R16): the menus' 30
// segments with the shoulder, the outer sidewall from the rim out, the tyre material, the black
// inner faces and the arch shade over the top.
TEST(tyre_ring_as_the_game_builds_it) {
    TyreShape s;
    s.radius = 0.31595f, s.width = 0.205f, s.sidewall = 0.205f * 0.55f;
    const float b = s.radius - 0.75f * s.sidewall, h = 0.25f * (s.radius - b);
    std::vector<scene::Mesh> t = build_tyre(s, menu_tyre_detail(), 3, false, 0);
    CHECK(t.size() == 2 && t[0].primitives.size() == 2);
    if (t.size() != 2 || t[0].primitives.size() != 2) return;
    const scene::Mesh& ring = t[0];
    const scene::Primitive &lit = ring.primitives[0], &black = ring.primitives[1];
    CHECK(lit.texture == 3 && lit.flags == 0 && lit.material.valid);
    CHECK(lit.material.ambient[0] == 0.3f && lit.material.diffuse[0] == 0.15f && lit.material.specular[0] == 0.9f);
    CHECK(lit.material.shininess == 8.0f);
    CHECK(black.texture == -1 && (black.flags & scene::kUnlit));
    // Tread, shoulder and sidewall: 3 strips of 31 vertex pairs.
    std::vector<const scene::Vertex*> lv = prim_vertices(ring, lit);
    float xmin = 1, xmax = -1, rmin = 1;
    bool shoulder_out = false, shoulder_in = false;
    for (const scene::Vertex* v : lv) {
        xmin = std::min(xmin, v->pos[0]), xmax = std::max(xmax, v->pos[0]), rmin = std::min(rmin, radial(*v));
        const float xo = -0.5f * s.width + 0.001f;
        shoulder_out |= std::fabs(v->pos[0] - xo) < 1e-5f && std::fabs(radial(*v) - (s.radius - h)) < 1e-4f;
        shoulder_in |= std::fabs(v->pos[0] - (xo + h)) < 1e-5f && std::fabs(radial(*v) - s.radius) < 1e-4f;
    }
    CHECK(ring.vertices.size() == 3 * 2 * 31 + 2 * 30 + 2 * 31);
    CHECK(std::fabs(xmin - (-0.1025f + 0.001f)) < 1e-5f && std::fabs(xmax - 0.1025f) < 1e-5f);
    CHECK(std::fabs(rmin - b) < 1e-4f);
    CHECK(shoulder_out && shoulder_in);
    // The textured faces look away from the tyre: the tread outwards, the sidewall towards -X.
    size_t tread = 0, side = 0, wrong = 0;
    for (u32 i = lit.first_index; i < lit.first_index + lit.index_count; i += 3) {
        const scene::Vertex &a = ring.vertices[ring.indices[i]], &c = ring.vertices[ring.indices[i + 1]],
                            &d = ring.vertices[ring.indices[i + 2]];
        float e1[3], e2[3], g[3], mid[3];
        for (int k = 0; k < 3; ++k) e1[k] = c.pos[k] - a.pos[k], e2[k] = d.pos[k] - a.pos[k], mid[k] = (a.pos[k] + c.pos[k] + d.pos[k]) / 3;
        g[0] = e1[1] * e2[2] - e1[2] * e2[1], g[1] = e1[2] * e2[0] - e1[0] * e2[2], g[2] = e1[0] * e2[1] - e1[1] * e2[0];
        const bool on_tread = radial(a) > s.radius - 1e-4f && radial(c) > s.radius - 1e-4f && radial(d) > s.radius - 1e-4f;
        if (on_tread) ++tread, wrong += g[1] * mid[1] + g[2] * mid[2] <= 0;
        else if (a.pos[0] == c.pos[0] && c.pos[0] == d.pos[0]) ++side, wrong += g[0] >= 0;
    }
    CHECK(tread == 60 && side == 60 && wrong == 0);
    // The tread texture: S runs down round a left wheel, up round a right one (2 repeats).
    const float s0 = ring.vertices[0].uv[0], s1 = ring.vertices[2].uv[0];
    CHECK(std::fabs(s0 - 2.0f) < 1e-5f && s1 < s0);
    std::vector<scene::Mesh> r = build_tyre(s, menu_tyre_detail(), 3, true, 0);
    CHECK(r[0].vertices[0].uv[0] == 0.0f && r[0].vertices[2].uv[0] > 0.0f);
    // The arch shade: 16 columns of 3, from 2 cm inboard of each face, alpha 256 - (i - 8)^2 4 at
    // the inner edge (0 at both ends), fully black where it passes 128.
    const scene::Mesh& band = t[1];
    CHECK(band.vertices.size() == 48 && band.primitives.size() == 1);
    CHECK(band.primitives[0].flags == (scene::kAlphaBlend | scene::kUnlit | scene::kNoDepthWrite | scene::kDoubleSided));
    auto alpha = [&](int col, int k) { return band.vertices[size_t(3 * col + k)].color >> 24; };
    CHECK(alpha(0, 2) == 0 && alpha(15, 2) == 0 && alpha(8, 0) == 0);
    CHECK(alpha(1, 2) == 120 && alpha(8, 1) == 255 && alpha(8, 2) == 255);
    const scene::Vertex& top = band.vertices[3 * 8 + 2];
    const float th = 2 * 3.14159265f * 8 / 30;
    CHECK(std::fabs(top.pos[0] - (0.1025f + 0.02f)) < 1e-5f);
    CHECK(std::fabs(top.pos[1] - (s.radius * std::sin(th) + 0.003f)) < 1e-5f);
    CHECK(std::fabs(top.pos[2] - std::sqrt(s.radius * s.radius + 0.25f * 0.205f * 0.205f) * std::cos(th)) < 1e-5f);
    CHECK(std::fabs(band.vertices[3 * 8 + 1].pos[0] - (-0.1025f + 0.02f + 128.0f / 255.0f * 0.205f)) < 1e-5f);

    // The race's far level: 8 segments, no shoulder, no inner faces, no shade, the outer face
    // 4 mm out from the rim's.
    std::vector<scene::Mesh> far = build_tyre(s, race_tyre_detail(2), 0, false, 0);
    CHECK(far.size() == 1 && far[0].vertices.size() == 2 * 2 * 9 + 8);
    float fmin = 1;
    for (const scene::Vertex& v : far[0].vertices) fmin = std::min(fmin, v.pos[0]);
    CHECK(std::fabs(fmin - (-0.1025f - 0.004f)) < 1e-5f);
    CHECK(race_tyre_detail(0).segments == 18 && race_tyre_detail(1).segments == 12 && !race_tyre_detail(1).band);

    // A symmetric tyre (GTTR flag 2): both sides textured, the disc only to the rim, no shade.
    s.flags = 2;
    std::vector<scene::Mesh> sym = build_tyre(s, menu_tyre_detail(), 0, false, 0);
    CHECK(sym.size() == 1);
    float rdisc = 0, inner_side = 0;
    for (const scene::Vertex* v : prim_vertices(sym[0], sym[0].primitives[1]))
        if (std::fabs(v->pos[0] - 0.1025f) < 1e-6f && v->normal[1] == 0 && v->normal[2] == 0) rdisc = std::max(rdisc, radial(*v));
    for (const scene::Vertex* v : prim_vertices(sym[0], sym[0].primitives[0]))
        if (std::fabs(v->pos[0] - 0.1025f) < 1e-6f) inner_side = std::max(inner_side, v->normal[0]);
    CHECK(std::fabs(rdisc - b) < 1e-4f && inner_side > 0.7f);
}

// The SLK 230's wheels where the game draws them (the tread less its per-side offset: 1488 - 70
// and 1485 - 40 mm hub to hub), its menu model's tyres and their texture.
TEST(tyre_placement_and_menu_tyres_disc) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    data::CarDatabase db;
    CHECK(db.load(disc->volume()));
    std::optional<WheelSpec> ws = wheel_spec_for_car(db, "slk230_komp_00");
    CHECK(ws.has_value());
    if (!ws) return;
    CHECK(std::fabs(ws->track[0] - 1.418f) < 1e-5f && std::fabs(ws->track[1] - 1.445f) < 1e-5f);
    Bytes data = disc->volume().read(*disc->volume().find("menu/cars/mb0007"));
    CarModelOptions o;
    o.lod = 0;
    o.menu_tyres = true;
    o.wheel_spec = ws;
    scene::Model m = load_car_model(data, o, "mb0007");
    const scene::Mesh *tyre = nullptr, *band = nullptr;
    for (const scene::Mesh& mesh : m.meshes) {
        if (mesh.name == "wheel_fl_tyre") tyre = &mesh;
        if (mesh.name == "wheel_fl_band") band = &mesh;
    }
    CHECK(tyre && band);
    if (!tyre || tyre->primitives.empty()) return;
    float xmin = 0;
    for (const scene::Vertex& v : tyre->vertices) xmin = std::min(xmin, v.pos[0]);
    CHECK(std::fabs(xmin - (-0.709f - 0.1025f + 0.001f)) < 1e-4f); // the outer sidewall, 46 mm inside the body
    const scene::Primitive& lit = tyre->primitives[0];
    CHECK(lit.material.shininess == 8.0f && lit.texture >= 0);
    if (lit.texture < 0) return;
    const scene::Texture& tex = m.textures[size_t(lit.texture)];
    CHECK(tex.no_mips && tex.clamp_v && !tex.clamp_u && tex.width == 256 && tex.height == 128);
    // The race car: 18, 12 and 8 segments for levels 0-2.
    CarModelOptions ro;
    ro.lod = -1;
    ro.wheel_spec = ws;
    scene::Model race = load_car_model(disc->volume().read(*disc->volume().find("cars/day/mb0007")), ro, "mb0007");
    std::array<size_t, 3> verts{};
    for (const scene::Mesh& mesh : race.meshes)
        if (mesh.name == "wheel_fl_tyre" && mesh.lod < 3) verts[size_t(mesh.lod)] = mesh.vertices.size();
    CHECK(verts[0] == 3 * 2 * 19 + 2 * 18 + 2 * 19 && verts[1] == 3 * 2 * 13 + 2 * 12 + 2 * 13 && verts[2] == 2 * 2 * 9 + 8);
}
