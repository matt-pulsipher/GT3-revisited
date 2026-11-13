// Texture packs (formats/texture_pack.h) and binary glTF export (formats/model/gltf_export.h):
// keys and file names, pack folders and mod priority, replacements in full and cut, the dump,
// and export round trips through load_glb. Synthetic data, plus NTSC-U disc checks.

#include "test_framework.h"

#include "base/file.h"
#include "formats/clut_animation.h"
#include "formats/model/car_model.h"
#include "formats/model/gltf.h"
#include "formats/model/gltf_export.h"
#include "formats/model/gtm1.h"
#include "formats/pcsx2_textures.h"
#include "formats/png_reader.h"
#include "formats/png_writer.h"
#include "formats/texture_pack.h"
#include "vfs/vfs.h"

#include <cmath>
#include <cstring>

using namespace gt3r;

namespace {

void put32(Bytes& d, size_t o, u32 v) { std::memcpy(&d[o], &v, 4); }
void put16(Bytes& d, size_t o, u16 v) { std::memcpy(&d[o], &v, 2); }
void put64(Bytes& d, size_t o, u64 v) { std::memcpy(&d[o], &v, 8); }

// A Tex1 set: one 4x4 T4 texture (texel index (x + y) & 3), its 16-colour CT32 CLUT and, with
// `animated`, one CLUT animation of entries 1 and 2 (as test_clut_animation.cpp's). `clamp`: the
// texture's CLAMP register.
Bytes synthetic_set(bool animated, u64 clamp = 0) {
    Bytes d(0x200, 0);
    std::memcpy(&d[0], "Tex1", 4);
    put32(d, 0x0C, u32(d.size()));
    put16(d, 0x14, 1), put16(d, 0x16, 2);
    put32(d, 0x18, 0x30), put32(d, 0x1C, 0x60), put32(d, 0x24, animated ? 0x80 : 0);
    const u64 tex0 = 0 | (1ull << 14) | (0x14ull << 20) | (2ull << 26) | (2ull << 30) | (1ull << 34) | (1ull << 37) |
                     (1ull << 61);
    put64(d, 0x30, tex0);
    put64(d, 0x50, clamp);
    put32(d, 0x60, 0xC0), put16(d, 0x64, 0), d[0x66] = 1, d[0x67] = 0x14, put16(d, 0x68, 4), put16(d, 0x6A, 4);
    put32(d, 0x6C, 0x100), put16(d, 0x70, 1), d[0x72] = 1, d[0x73] = 0, put16(d, 0x74, 8), put16(d, 0x76, 2);
    for (u32 y = 0; y < 4; ++y)
        for (u32 x = 0; x < 4; x += 2) d[0xC0 + y * 2 + x / 2] = u8(((x + y) & 3) | (((x + 1 + y) & 3) << 4));
    for (u32 e = 0; e < 16; ++e) put32(d, 0x100 + 4 * e, 0x80000000u | (e * 0x10101u));
    if (animated) {
        put32(d, 0x80, 1), put32(d, 0x84, 0xA0);
        put16(d, 0xA0, 0), put16(d, 0xA2, 0), put16(d, 0xA4, 2), put16(d, 0xA6, 2);
        put32(d, 0xA8, 0x104), put32(d, 0xAC, 0x140);
        const float secs[2] = {0.5f, 1.0f};
        for (u32 f = 0; f < 2; ++f) {
            u32 bits;
            std::memcpy(&bits, &secs[f], 4);
            put32(d, 0x140 + 8 * f, bits), put32(d, 0x144 + 8 * f, 0x160 + 8 * f);
            put32(d, 0x160 + 8 * f, 0x80102030u + f), put32(d, 0x164 + 8 * f, 0x80405060u + f);
        }
    }
    return d;
}

// A picture with one colour per texel: texel (x, y) = x | y << 8 | 0xFF000000.
std::vector<u32> ramp(u32 w, u32 h) {
    std::vector<u32> px(size_t(w) * h);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) px[size_t(y) * w + x] = x | y << 8 | 0xFF000000u;
    return px;
}

std::string write_png(const std::filesystem::path& path, u32 w, u32 h, const std::vector<u32>& px) {
    std::filesystem::create_directories(path.parent_path());
    CHECK(write_host_file(path.string(), encode_png(w, h, px)));
    return path.string();
}

// Sets a pack for the test's duration.
struct PackScope {
    explicit PackScope(std::shared_ptr<const texture_pack::Pack> p) { texture_pack::set_pack(std::move(p)); }
    ~PackScope() { texture_pack::set_pack(nullptr); }
};

} // namespace

TEST(texture_pack_key_names) {
    using texture_pack::key_in_file_name;
    CHECK(key_in_file_name("0123456789abcdef.png") == 0x0123456789ABCDEFull);
    CHECK(key_in_file_name("tyre-0123456789ABCDEF.PNG") == 0x0123456789ABCDEFull);
    CHECK(key_in_file_name("set0-3-paint2-0123456789abcdef.png") == 0x0123456789ABCDEFull);
    CHECK(key_in_file_name("my tyre_0123456789abcdef.png") == 0x0123456789ABCDEFull);
    CHECK(!key_in_file_name("x0123456789abcdef.png"));    // no separator before the key
    CHECK(!key_in_file_name("0123456789abcdeg.png"));     // not hex
    CHECK(!key_in_file_name("0123456789abcdef.jpg"));
    CHECK(!key_in_file_name("123456789abcdef.png"));      // 15 digits
    CHECK(texture_pack::key_name(0x0123456789ABCDEFull) == "0123456789abcdef");
}

TEST(texture_pack_key_is_stable) {
    // Packs are named by these keys: the hash must never change.
    const std::vector<u32> px = ramp(2, 2);
    CHECK(texture_pack::key_name(texture_pack::key_of(2, 2, px.data())) == "eb379571a0ceb08e");
    CHECK(texture_pack::key_of(2, 2, px.data()) != texture_pack::key_of(1, 4, px.data()));
}

TEST(texture_pack_folders_and_mod_priority) {
    const auto mods = test::temp_dir("tp-mods");
    const std::vector<u32> px = ramp(2, 2);
    const std::string low = write_png(mods / "low" / "textures" / "a" / "x-00000000000000aa.png", 2, 2, px);
    const std::string high = write_png(mods / "high" / "textures" / "00000000000000aa.png", 2, 2, px);
    write_png(mods / "high" / "textures" / "00000000000000bb.png", 2, 2, px);
    write_png(mods / "high" / "textures" / "notes.png", 2, 2, px); // no key: not a replacement
    CHECK(write_host_file((mods / "high" / "mod.ini").string(), ByteSpan(reinterpret_cast<const u8*>("[mod]\npriority = 5\n"), 19)));
    Vfs vfs(nullptr);
    vfs.scan_mods(mods.string(), nullptr);
    std::shared_ptr<const texture_pack::Pack> pack = vfs.textures();
    CHECK(pack && pack->size() == 2);
    CHECK(pack && pack->source_of(0xAA) == "high");
    vfs.set_mod_enabled("high", false);
    pack = vfs.textures();
    CHECK(pack && pack->size() == 1 && pack->source_of(0xAA) == "low");
    vfs.set_mod_enabled("low", false);
    CHECK(!vfs.textures());
    texture_pack::Pack folder;
    CHECK(folder.add_folder((mods / "high" / "textures").string(), "f") == 2);
    std::shared_ptr<const texture_pack::Image> img = folder.find(0xBB);
    CHECK(img && img->width == 2 && img->rgba == px);
    CHECK(!folder.find(0xCC));
}

TEST(texture_pack_replaces_decoded_sets) {
    const Bytes set = synthetic_set(true);
    const std::vector<u64> keys = texture_pack::keys_of_set(set);
    CHECK(keys.size() == 1);
    std::vector<scene::Texture> disc;
    ClutTextures disc_clut;
    gtm1::decode_texture_set(set, disc, "t", 0, &disc_clut);
    CHECK(disc.size() == 1 && disc[0].width == 4 && !disc_clut.empty());
    CHECK(keys[0] == texture_pack::key_of(4, 4, disc[0].rgba.data())); // keys come from what is shown

    const auto dir = test::temp_dir("tp-replace");
    const std::vector<u32> px = ramp(8, 8);
    auto pack = std::make_shared<texture_pack::Pack>();
    pack->add_file(keys[0], write_png(dir / (texture_pack::key_name(keys[0]) + ".png"), 8, 8, px));
    PackScope scope(pack);
    std::vector<scene::Texture> out;
    ClutTextures clut;
    gtm1::decode_texture_set(set, out, "t", 0, &clut);
    CHECK(out.size() == 1 && out[0].width == 8 && out[0].height == 8 && out[0].rgba == px);
    CHECK(clut.empty()); // a replaced texture is not animated
    std::vector<scene::Texture> raw;
    gtm1::decode_texture_set(set, raw, "t", 0, nullptr, false);
    CHECK(raw.size() == 1 && raw[0].width == 4); // the disc's, for keys
}

TEST(texture_pack_cuts_region_clamped_textures) {
    // REGION_CLAMP u 1..2, v 0..3: Tex1::decode keeps a 2x4 part of the 4x4 texture.
    const u64 clamp = 2 | (0ull << 2) | (1ull << 4) | (2ull << 14);
    const Bytes set = synthetic_set(false, clamp);
    Tex1 t = Tex1::parse(set);
    GsMemory gs;
    t.upload(set, gs);
    Tex1::Image img = t.decode(gs, 0);
    CHECK(img.width == 2 && img.height == 4);
    const std::vector<u64> keys = texture_pack::keys_of_set(set); // the whole texture's key
    const auto dir = test::temp_dir("tp-cut");
    auto pack = std::make_shared<texture_pack::Pack>();
    pack->add_file(keys[0], write_png(dir / "cut-x.png", 8, 8, ramp(8, 8))); // 2x the texture
    PackScope scope(pack);
    CHECK(texture_pack::replace_decoded(set, 0, img));
    CHECK(img.width == 4 && img.height == 8);
    CHECK(img.rgba.size() == 32 && img.rgba[0] == (2u | 0xFF000000u) && img.rgba[31] == (5u | 7u << 8 | 0xFF000000u));
}

TEST(texture_pack_dump_names_by_key) {
    // A container with a still set 16 bytes in, then a set whose CLUT animation recolours its texture.
    Bytes file(16, 0);
    const Bytes still = synthetic_set(false), animated = synthetic_set(true);
    file.insert(file.end(), still.begin(), still.end());
    file.insert(file.end(), animated.begin(), animated.end());
    const auto root = test::temp_dir("tp-dump");
    texture_pack::Dump dump(root.string());
    bool ok = true;
    CHECK(dump.add_file("courses/US/x", file, &ok) == 2 && ok);
    CHECK(dump.add_file("courses/US/y", file, &ok) == 0); // the same textures again: not saved twice
    CHECK(dump.finish());
    const u64 key = texture_pack::keys_of_set(still)[0], anim_key = texture_pack::keys_of_set(animated)[0];
    CHECK(key != anim_key);
    for (const auto& [name, k] : {std::pair{std::string("set0-0-"), key}, std::pair{std::string("set1-0-anim-"), anim_key}}) {
        std::optional<Bytes> data = read_host_file((root / "courses/US/x" / (name + texture_pack::key_name(k) + ".png")).string());
        CHECK(data.has_value());
        if (data) {
            PngImage img = decode_png(*data);
            CHECK(texture_pack::key_of(img.width, img.height, img.rgba.data()) == k);
        }
    }
    std::optional<Bytes> index = read_host_file((root / "textures.txt").string());
    CHECK(index.has_value());
    if (index) {
        const std::string text(index->begin(), index->end());
        CHECK(text.find(texture_pack::key_name(key) + "\tcourses/US/x\tset0-0\n") != std::string::npos);
        CHECK(text.find(texture_pack::key_name(key) + "\tcourses/US/y\tset0-0\n") != std::string::npos);
        CHECK(text.find(texture_pack::key_name(anim_key) + "\tcourses/US/x\tset1-0-anim\n") != std::string::npos);
    }
    // for_each_texture says which textures animate; the names it gives (PCSX2 pack imports) stay plain.
    std::vector<std::pair<std::string, bool>> seen;
    texture_pack::for_each_texture("courses/US/x", file, [&](const texture_pack::FileTexture& ft) { seen.emplace_back(ft.name, ft.animated); });
    CHECK(seen.size() == 2);
    if (seen.size() == 2) {
        CHECK(seen[0].first == "set0-0" && !seen[0].second);
        CHECK(seen[1].first == "set1-0" && seen[1].second);
    }
    // The same pixels still in one file and animated in a later one: the saved file is marked.
    Bytes look_alike = synthetic_set(false);
    put32(look_alike, 0x104, 0x80102030u), put32(look_alike, 0x108, 0x80405060u); // frame 0's colours
    CHECK(texture_pack::keys_of_set(look_alike)[0] == anim_key);
    const auto root2 = test::temp_dir("tp-dump-mixed");
    texture_pack::Dump mixed(root2.string());
    CHECK(mixed.add_file("courses/US/still", look_alike, &ok) == 1 && ok);
    CHECK(mixed.add_file("courses/US/lit", animated, &ok) == 0 && ok);
    const std::string base = "set0-0", tail = "-" + texture_pack::key_name(anim_key) + ".png";
    CHECK(!read_host_file((root2 / "courses/US/still" / (base + tail)).string()).has_value());
    CHECK(read_host_file((root2 / "courses/US/still" / (base + "-anim" + tail)).string()).has_value());
    CHECK(texture_pack::dump_reads("cars/day/am0006"));
    CHECK(!texture_pack::dump_reads("stream/akasaka_full.pss"));
    CHECK(!texture_pack::dump_reads("bgm/demo_a01.ads"));
}

TEST(glb_export_round_trip) {
    scene::Model m;
    m.name = "test";
    scene::Texture tex;
    tex.width = tex.height = 2;
    tex.rgba = ramp(2, 2);
    m.textures.push_back(tex);
    scene::Mesh mesh;
    mesh.name = "body";
    mesh.transform[12] = 0.5f; // x + 0.5
    const float pos[4][3] = {{0, 0, -1}, {1, 0, -1}, {0, 1, -1}, {1, 1, -1}};
    for (const auto& p : pos) {
        scene::Vertex v;
        std::memcpy(v.pos, p, sizeof v.pos);
        v.normal[2] = -1;
        v.uv[0] = p[0], v.uv[1] = p[1];
        mesh.vertices.push_back(v);
    }
    mesh.indices = {0, 1, 2, 2, 1, 3};
    scene::Primitive prim;
    prim.index_count = 6;
    prim.texture = 0;
    mesh.primitives.push_back(prim);
    scene::Primitive env = prim; // a reflection pass: left out
    env.flags = scene::kEnvironmentMap;
    mesh.primitives.push_back(env);
    m.meshes.push_back(mesh);
    scene::Mesh lod1 = mesh;
    lod1.lod = 1;
    m.meshes.push_back(lod1);

    const Bytes glb = export_glb(m);
    CHECK(glb.size() > 20 && std::memcmp(glb.data(), "glTF", 4) == 0);
    const std::string json(glb.begin() + 20, glb.begin() + 20 + rd32(glb, 12));
    CHECK(json.find("\"LOD") == std::string::npos); // one LOD: no LOD nodes
    scene::Model back = load_glb(glb, {}, "back");
    size_t tris = 0;
    for (const scene::Mesh& bm : back.meshes) tris += bm.indices.size() / 3;
    CHECK(tris == 2);
    CHECK(back.textures.size() == 2 && back.textures[1].rgba == tex.rgba); // after load_glb's white
    // load_glb turns the file back into GT3's frame, the node transform applied.
    bool found = false;
    for (const scene::Mesh& bm : back.meshes)
        for (const scene::Vertex& v : bm.vertices) {
            float x = v.pos[0], y = v.pos[1], z = v.pos[2];
            const float* t = bm.transform.data();
            const float wx = t[0] * x + t[4] * y + t[8] * z + t[12], wz = t[2] * x + t[6] * y + t[10] * z + t[14];
            if (std::fabs(wx - 1.5f) < 1e-5f && std::fabs(y - 1) < 1e-5f && std::fabs(wz + 1) < 1e-5f) found = true;
        }
    CHECK(found); // the vertex (1, 1, -1) moved 0.5 along x
    GlbExportOptions all;
    all.lod = -1;
    const Bytes both = export_glb(m, all);
    const std::string json2(both.begin() + 20, both.begin() + 20 + rd32(both, 12));
    CHECK(json2.find("\"LOD 0\"") != std::string::npos && json2.find("\"LOD 1\"") != std::string::npos);
}

TEST(texture_pack_disc_car) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    const Volume::Entry* e = disc->volume().find("cars/day/am0006");
    CHECK(e);
    if (!e) return;
    const Bytes data = disc->volume().read(*e);
    std::vector<size_t> sets = find_tex1_sets(data);
    CHECK(!sets.empty());
    const std::vector<u64> keys = texture_pack::keys_of_set(ByteSpan(data).subspan(sets[0]));
    CHECK(!keys.empty());
    CarModelOptions opt;
    const scene::Model before = load_car_model(data, opt, "am0006");
    const auto dir = test::temp_dir("tp-car");
    std::vector<u32> px(64 * 64, 0xFF00FF00u);
    auto pack = std::make_shared<texture_pack::Pack>();
    pack->add_file(keys[0], write_png(dir / "green.png", 64, 64, px));
    PackScope scope(pack);
    const scene::Model after = load_car_model(data, opt, "am0006");
    CHECK(after.textures.size() == before.textures.size());
    size_t replaced = 0;
    for (const scene::Texture& t : after.textures) replaced += t.width == 64 && t.rgba == px ? 1 : 0;
    CHECK(replaced >= 1);
    // The car exports and loads back with every exported triangle.
    texture_pack::set_pack(nullptr);
    const Bytes glb = export_glb(before);
    size_t exported = 0;
    for (const scene::Mesh& m : before.meshes)
        if (m.lod == 0)
            for (const scene::Primitive& p : m.primitives)
                if (!(p.flags & (scene::kEnvironmentMap | scene::kAlphaOnly | scene::kDepthOnly | scene::kMultiplyDest)) &&
                    p.env_kind == 0)
                    exported += p.index_count / 3;
    GlbOptions go;
    go.reflections = false;
    const scene::Model back = load_glb(glb, go, "back");
    size_t tris = 0;
    for (const scene::Mesh& m : back.meshes) tris += m.indices.size() / 3;
    CHECK(exported > 0 && tris == exported);
}

TEST(texture_pack_pcsx2_pack_import) {
    // The game file: a container with a REGION_CLAMP set (u 1..2): PCSX2 dumps a 2x4 part.
    const u64 clamp = 2 | (1ull << 4) | (2ull << 14);
    const Bytes set = synthetic_set(false, clamp);
    Bytes file(16, 0);
    file.insert(file.end(), set.begin(), set.end());
    texture_pack::Pcsx2Index index;
    index.add_file("menu/x", file);
    std::vector<scene::Texture> full;
    gtm1::decode_texture_set(set, full, "", 0, nullptr, false);
    const u64 key = texture_pack::key_of(4, 4, full[0].rgba.data());
    // PCSX2's dump: the part, its alpha as the console keeps it (0x80 = opaque).
    std::vector<u32> part;
    for (u32 y = 0; y < 4; ++y)
        for (u32 x = 1; x < 3; ++x) part.push_back((full[0].rgba[y * 4 + x] & 0x00FFFFFFu) | 0x80000000u);
    std::optional<texture_pack::Pcsx2Index::Match> m = index.find(2, 4, part);
    CHECK(m && m->key == key && m->part.x == 1 && m->part.width == 2);
    CHECK(!index.find(2, 4, ramp(2, 4)));

    const auto pack = test::temp_dir("tp-pcsx2");
    const std::string name = "0123456789abcdef-fedcba9876543210-r2x4-00001234.png";
    write_png(pack / "dumps" / name, 2, 4, part);
    std::vector<u32> hd(4 * 8, 0x80112233u); // the artist's picture: 2x, half alpha as PCSX2 reads it
    write_png(pack / "replacements" / "cars" / name, 4, 8, hd);
    write_png(pack / "replacements" / "other-0000000000000001.png", 2, 2, ramp(2, 2)); // no dump
    const auto out = pack / "out";
    auto read = [&](const std::string& p) { return p == "menu/x" ? std::optional<Bytes>(file) : std::nullopt; };
    texture_pack::Pcsx2Import r = texture_pack::import_pcsx2_pack(pack.string(), index, out.string(), read);
    CHECK(r.pictures == 2 && r.converted == 1 && r.no_dump.size() == 1 && r.unmatched.empty() && r.failed.empty());
    const std::string stem = name.substr(0, name.size() - 4);
    const std::string written = (out / (stem + "-" + texture_pack::key_name(key) + ".png")).string();
    std::optional<Bytes> data = read_host_file(written);
    CHECK(data.has_value());
    if (data) {
        // The whole texture at the picture's scale (8x8), the picture at u 2..5, the rest the game's own.
        PngImage img = decode_png(*data);
        CHECK(img.width == 8 && img.height == 8);
        CHECK(img.rgba.size() == 64 && img.rgba[2] == 0xFF112233u && img.rgba[5] == 0xFF112233u);
        CHECK(img.rgba.size() == 64 && img.rgba[0] == full[0].rgba[0] && img.rgba[7] == full[0].rgba[3]);
        CHECK(texture_pack::key_in_file_name(written.substr(written.find_last_of('/') + 1)) == key);
    }
}
