// Mod wheels (vfs/mod_wheels.h): the definitions, the Vfs serving their rims as wheel/<label> and
// menu/wheel/<label>, glTF rims (formats/model/wheel_model.h) and the wheel shop's pages
// (game/gt_mode_wheels_menu.cpp). Synthetic tests run everywhere; the disc tests need $GT3R_DISC
// (any region).

#include "test_framework.h"

#include "base/file.h"
#include "disc/disc.h"
#include "formats/menu/mbl.h"
#include "formats/model/car_model.h"
#include "formats/model/gltf_export.h"
#include "formats/model/wheel_model.h"
#include "formats/png_writer.h"
#include "game/gt_mode_mods.h"
#include "menu/flow/menu_set.h"
#include "formats/text/mltx.h"
#include "vfs/mod_wheels.h"
#include "vfs/vfs.h"

#include <cmath>
#include <filesystem>

using namespace gt3r;
using namespace gt3r::test;

namespace {

bool write_text(const std::filesystem::path& p, std::string_view text) {
    std::filesystem::create_directories(p.parent_path());
    return write_host_file(p.generic_string(), ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()));
}

bool write_bytes(const std::filesystem::path& p, const Bytes& b) {
    std::filesystem::create_directories(p.parent_path());
    return write_host_file(p.generic_string(), b);
}

// A rim as the game's are made: a disc of radius 1 on x = 0 (GT3 axes), outer face towards -X, a
// cut-out (MASK) spoke face and a metal hub.
scene::Model synthetic_rim() {
    scene::Model m;
    scene::Mesh mesh;
    mesh.name = "rim";
    const int n = 16;
    for (int k = 0; k <= n; ++k) {
        scene::Vertex v;
        if (k < n) {
            const float a = 6.2831853f * float(k) / float(n);
            v.pos[1] = std::cos(a), v.pos[2] = std::sin(a);
        }
        v.pos[0] = k < n ? 0.0f : 0.1f; // (the centre, the last one, set back)
        v.normal[0] = -1;
        mesh.vertices.push_back(v);
    }
    for (int k = 0; k < n; ++k) mesh.indices.insert(mesh.indices.end(), {u32(n), u32(k), u32((k + 1) % n)});
    scene::Primitive cut;
    cut.index_count = u32(mesh.indices.size()) / 2;
    cut.flags = scene::kAlphaTest;
    cut.alpha_ref = 0.25f;
    scene::Primitive rest;
    rest.first_index = cut.index_count;
    rest.index_count = u32(mesh.indices.size()) - cut.index_count;
    mesh.primitives = {cut, rest};
    m.meshes.push_back(std::move(mesh));
    m.compute_bounds();
    return m;
}

} // namespace

TEST(mod_wheel_parse) {
    const std::filesystem::path root = temp_dir("mod_wheel_parse") / "my_wheels";
    std::filesystem::create_directories(root / "models");
    write_bytes(root / "models" / "rim.glb", export_glb(synthetic_rim()));
    write_text(root / "wheels" / "Mesh_Racer.ini", "[wheel]\nbrand = OZ Racing   ; the maker\nmodel = models/rim.glb ; a rim\n"
                                                   "picture = pictures/rim.png\n[brand]\nlogo = pictures/logo.png\n");
    write_text(root / "wheels" / "own.ini", "[wheel]\nlabel = car_only_01\nrace = models/rim.glb\nfor_sale = no\n");
    write_text(root / "wheels" / "bad.ini", "[wheel]\nlabel = Bad Label!\nbrand = BBS\nmodel = models/rim.glb\n");
    write_text(root / "wheels" / "nomodel.ini", "[wheel]\nbrand = BBS\n");
    write_text(root / "wheels" / "missing.ini", "[wheel]\nbrand = BBS\nmenu = models/nope.glb\n");
    write_text(root / "wheels" / "nobrand.ini", "[wheel]\nmodel = models/rim.glb\n");
    auto load = [&](const char* f) { return load_mod_wheel((root / "wheels" / f).generic_string(), "my_wheels", root.generic_string()); };

    const ModWheelDef a = load("Mesh_Racer.ini");
    CHECK(a.error.empty());
    CHECK(a.label == "mesh_racer"); // (the file name, lower case)
    CHECK(a.brand == "OZ Racing" && a.for_sale && a.mod_id == "my_wheels");
    CHECK(a.model == root.generic_string() + "/models/rim.glb");
    CHECK(a.race_file() == a.model && a.menu_file() == a.model);
    CHECK(a.picture == root.generic_string() + "/pictures/rim.png");
    CHECK(a.brand_logo == root.generic_string() + "/pictures/logo.png");

    const ModWheelDef b = load("own.ini");
    CHECK(b.error.empty() && b.label == "car_only_01" && !b.for_sale); // (no brand needed)
    CHECK(b.menu_file() == b.race && b.race_file() == b.race);

    CHECK(!load("bad.ini").error.empty());
    CHECK(load("nomodel.ini").error.find("no model") != std::string::npos);
    CHECK(load("missing.ini").error == "missing models/nope.glb");
    CHECK(load("nobrand.ini").error.find("no brand") != std::string::npos);
    CHECK(!load("not_there.ini").error.empty());

    CHECK(valid_wheel_label("a_1") && !valid_wheel_label("") && !valid_wheel_label("A") && !valid_wheel_label("a-b"));
    CHECK(!valid_wheel_label(std::string(33, 'a')) && valid_wheel_label(std::string(32, 'a')));
    CHECK(wheel_brand_key("OZ Racing") == "ozracing" && wheel_brand_key("OZ Racing", true) == "oz");
    CHECK(wheel_brand_key(" R.A.Y.S ") == "rays");
}

// The Vfs serves each wheel's rims where the disc keeps its own; a label two mods give is the higher
// priority one's; a mod that is off serves nothing.
TEST(mod_wheel_vfs_serves_rims) {
    const std::filesystem::path mods = temp_dir("mod_wheel_vfs") / "mods";
    const Bytes race = export_glb(synthetic_rim());
    scene::Model bigger = synthetic_rim();
    for (scene::Vertex& v : bigger.meshes[0].vertices) v.pos[1] *= 1.01f;
    const Bytes menu = export_glb(bigger);
    for (const char* id : {"a_low", "b_high"}) {
        write_bytes(mods / id / "models" / "race.glb", race);
        write_bytes(mods / id / "models" / "menu.glb", menu);
        write_text(mods / id / "mod.ini", std::string("[mod]\nname = ") + id + "\npriority = " + (id[0] == 'a' ? "0" : "5") + "\n");
        write_text(mods / id / "wheels" / "rim.ini", "[wheel]\nbrand = BBS\nrace = models/race.glb\nmenu = models/menu.glb\n");
    }
    write_text(mods / "a_low" / "wheels" / "solo.ini", "[wheel]\nbrand = Claude Forge\nmodel = models/race.glb\n");

    Vfs vfs(nullptr);
    vfs.scan_mods(mods.generic_string(), nullptr);
    CHECK(vfs.read("wheel/rim") == std::optional<Bytes>(race));
    CHECK(vfs.read("menu/wheel/rim") == std::optional<Bytes>(menu));
    CHECK(vfs.source_of("wheel/rim") == "b_high");
    CHECK(vfs.source_of("menu/wheel/solo") == "a_low");
    CHECK(vfs.read("menu/wheel/solo") == std::optional<Bytes>(race)); // (model: everywhere)

    const std::vector<ModWheelDef> list = mod_wheels([&](const std::string& p) { return vfs.read(p); });
    CHECK(list.size() == 3);
    int ok = 0;
    for (const ModWheelDef& d : list) {
        if (d.error.empty()) ++ok;
        if (d.mod_id == "a_low" && d.label == "rim") CHECK(d.error.find("b_high") != std::string::npos);
    }
    CHECK(ok == 2);
    CHECK(vfs.mod_wheels().size() == 3);

    vfs.set_mod_enabled("a_low", false);
    CHECK(!vfs.read("wheel/solo") && vfs.read("wheel/rim"));
    vfs.set_mod_enabled("b_high", false);
    CHECK(!vfs.read("wheel/rim") && vfs.mod_wheels().empty());
}

// A glTF rim loads as a GTTW one does, in the same frame (the export's half turn undone), with its
// cut-outs kept.
TEST(mod_wheel_glb_rim_loads) {
    const scene::Model src = synthetic_rim();
    const Bytes glb = export_glb(src);
    CHECK(is_glb_rim(glb) && is_rim(glb) && !WheelFile::is_wheel(glb));
    const scene::Model m = load_wheel_model(glb, {}, "wheel");
    CHECK(!m.meshes.empty());
    for (int i = 0; i < 3; ++i) {
        CHECK(std::fabs(m.bounds_min[i] - src.bounds_min[i]) < 1e-4f);
        CHECK(std::fabs(m.bounds_max[i] - src.bounds_max[i]) < 1e-4f);
    }
    bool cut = false;
    size_t tris = 0;
    for (const scene::Mesh& mesh : m.meshes) {
        CHECK(mesh.name == "wheel");
        for (const scene::Primitive& p : mesh.primitives) {
            if (p.flags & scene::kAlphaTest) {
                cut = true;
                CHECK(std::fabs(p.alpha_ref - 0.25f) < 1e-6f);
            }
            tris += p.index_count / 3;
        }
    }
    CHECK(cut && tris == 16);
}

namespace {

struct WheelWorld {
    std::filesystem::path mod;
    std::unique_ptr<Vfs> vfs;
    menu::flow::FileReader read;
    menu::MblFile flow;
    Bytes race_glb;
};

WheelWorld wheel_world(Disc& disc) {
    WheelWorld w;
    Volume& vol = disc.volume();
    const scene::Model rim = load_wheel_model(vol.read(*vol.find("wheel/enw001_s")), {}, "rim");
    w.race_glb = export_glb(rim);
    const std::filesystem::path mods = temp_dir("mod_wheels_disc") / "mods";
    w.mod = mods / "my_wheels";
    write_bytes(w.mod / "models" / "rim.glb", w.race_glb);
    write_bytes(w.mod / "pictures" / "tile.png", encode_png(52, 52, std::vector<u32>(52 * 52, 0xFF0000FFu)));
    write_bytes(w.mod / "pictures" / "logo.png", encode_png(480, 144, std::vector<u32>(480 * 144, 0xFF00FFFFu)));
    write_text(w.mod / "wheels" / "mesh_racer.ini", "[wheel]\nbrand = BBS\nmodel = models/rim.glb\npicture = pictures/tile.png\n");
    write_text(w.mod / "wheels" / "star.ini",
               "[wheel]\nbrand = Claude Forge\nmodel = models/rim.glb\npicture = pictures/tile.png\n[brand]\nlogo = pictures/logo.png\n");
    write_text(w.mod / "wheels" / "car_only.ini", "[wheel]\nmodel = models/rim.glb\nfor_sale = no\npicture = pictures/tile.png\n");
    write_text(w.mod / "wheels" / "taken.ini", "[wheel]\nlabel = enw001_s\nbrand = Enkei\nmodel = models/rim.glb\n");
    w.vfs = std::make_unique<Vfs>(&vol);
    w.vfs->scan_mods(mods.generic_string(), nullptr);
    Vfs* v = w.vfs.get();
    w.read = [v](const std::string& p) { return v->read(p); };
    game::install_mod_menu_patches();
    game::forget_mod_menu_plan();
    if (std::optional<Bytes> mb = w.read("menu/gt_mode.mbl")) {
        w.flow = menu::MblFile::parse(*mb);
        menu::flow::menu_patches().flow("gt_mode", w.read, w.flow);
    }
    return w;
}

const menu::MblPage* page(const menu::MblFile& f, const std::string& name) {
    const int i = f.find_page(name);
    return i < 0 ? nullptr : &f.pages[size_t(i)];
}

bool has_item(const menu::MblPage* p, const std::string& name) {
    if (p)
        for (const menu::MblItem& it : p->items)
            if (it.name == name) return true;
    return false;
}

const menu::Element* hidden(const menu::Page& p, const std::string& name) {
    for (const menu::Layer& l : p.layers)
        if (l.name == "hidden")
            for (const menu::Element& e : l.elements)
                if (e.name == name) return &e;
    return nullptr;
}

} // namespace

TEST(mod_wheel_disc_shop) {
    Disc* disc = any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    WheelWorld w = wheel_world(*disc);
    // The disc's own wheel stays the disc's; the mod's one by that label does not load.
    CHECK(w.vfs->source_of("wheel/enw001_s") == "disc");
    CHECK(w.vfs->source_of("wheel/mesh_racer") == "my_wheels" && w.vfs->read("wheel/mesh_racer") == std::optional<Bytes>(w.race_glb));
    for (const ModWheelDef& d : w.vfs->mod_wheels())
        if (d.label == "enw001_s") CHECK(d.error.find("disc") != std::string::npos);

    // The flow: on BBS's page as its wheels are; a new maker with its page and button; none for the car-only one.
    const menu::MblPage* bbs = page(w.flow, "at/ws/ws_bbs");
    CHECK(has_item(bbs, "mesh_racer_link") && has_item(bbs, "bbw001_s_link"));
    if (bbs)
        for (const menu::MblItem& it : bbs->items)
            if (it.name == "mesh_racer_link") CHECK(it.action == "set_focus_wheel_code" && it.link == "00cmn/ws_buy_sure");
    CHECK(has_item(page(w.flow, "at/ws/ws"), "mod_claudeforge"));
    const menu::MblPage* forge = page(w.flow, "at/ws/ws_mod_claudeforge");
    CHECK(forge && forge->back == "at/ws/ws" && forge->items.size() == 1 && has_item(forge, "star_link"));
    int enkei = 0;
    if (const menu::MblPage* e = page(w.flow, "at/ws/ws_enkei"))
        for (const menu::MblItem& it : e->items) enkei += it.name == "enw001_s_link";
    CHECK(enkei == 1);
    for (const menu::MblPage& p : w.flow.pages)
        CHECK(!has_item(&p, "car_only_link"));

    // The layouts: tiles with the mod's picture, the new maker's page and logo, the garage's tiles.
    menu::flow::MenuSet set(w.read, "gt_mode", text::language_code(w.vfs->language()));
    auto image_size = [](const menu::MenuScene& sc, const menu::Element* e) {
        return e && e->image && e->image->image < sc.images().size()
                   ? std::make_pair(sc.images()[e->image->image].width, sc.images()[e->image->image].height)
                   : std::make_pair(0u, 0u);
    };
    const menu::flow::MenuSet::PageRef b = set.page("at/ws/ws_bbs");
    CHECK(b);
    if (b) CHECK(image_size(*b.scene, hidden(b.layout(), "mesh_racer")) == std::make_pair(52u, 52u));
    const menu::flow::MenuSet::PageRef f = set.page("at/ws/ws_mod_claudeforge");
    CHECK(f);
    if (f) {
        CHECK(image_size(*f.scene, hidden(f.layout(), "star")) == std::make_pair(52u, 52u));
        CHECK(!hidden(f.layout(), "bbw001_s") && !hidden(f.layout(), "brw001_s")); // (none of its template's wheels)
        CHECK(hidden(f.layout(), "sold_out") != nullptr);
        bool name = false;
        for (const menu::Layer& l : f.layout().layers)
            for (const menu::Element& e : l.elements)
                if (e.name == "maker_name") name = e.text && e.text->text == "Claude Forge";
        CHECK(name);
    }
    const menu::flow::MenuSet::PageRef s = set.page("at/ws/ws");
    CHECK(s);
    if (s) {
        bool button = false, logo = false, text = false;
        for (const menu::Element& e : s.layout().items)
            if (e.name == "mod_claudeforge") button = e.text && e.text->text == "Claude Forge";
        for (const menu::Layer& l : s.layout().layers)
            for (const menu::Element& e : l.elements) {
                if (l.name == "m_mod_claudeforge") logo = image_size(*s.scene, &e) == std::make_pair(480u, 144u);
                if (l.name == "s_mod_claudeforge") text = e.text && e.text->text == "Claude Forge";
            }
        CHECK(button && logo && text);
    }
    const menu::flow::MenuSet::PageRef g = set.page("hm/hm_wheel");
    CHECK(g);
    if (g)
        for (const char* label : {"mesh_racer", "star", "car_only"})
            CHECK(image_size(*g.scene, hidden(g.layout(), label)) == std::make_pair(52u, 52u));
}

// A car on a glTF rim: the rim is placed as the disc's own (the same rim exported and loaded back
// lands where the GTTW one does on each wheel).
TEST(mod_wheel_disc_car_wears_glb) {
    Disc* disc = any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Volume& vol = disc->volume();
    const Bytes car = vol.read(*vol.find("cars/day/am0006"));
    const Bytes gttw = vol.read(*vol.find("wheel/enw001_s"));
    const Bytes glb = export_glb(load_wheel_model(gttw, {}, "rim"));
    auto rims = [&](const Bytes& wheel) {
        CarModelOptions o;
        o.reflection = false;
        o.wheel_override = wheel;
        scene::Model m = load_car_model(car, o, "am0006");
        scene::Model r = scene::extract(m, [](const scene::Mesh& x) { return x.name == "wheel_fl" || x.name == "wheel_rr"; });
        r.compute_bounds();
        return r;
    };
    const scene::Model a = rims(gttw), b = rims(glb);
    CHECK(!a.meshes.empty() && !b.meshes.empty());
    for (int i = 0; i < 3; ++i) {
        CHECK(std::fabs(a.bounds_min[i] - b.bounds_min[i]) < 1e-3f);
        CHECK(std::fabs(a.bounds_max[i] - b.bounds_max[i]) < 1e-3f);
    }
}
