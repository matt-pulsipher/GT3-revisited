// Mod cars and makers in the GT3 menus (docs/MODDING.md "Dealers"), as menu content
// patches (menu/flow/menu_set.h MenuPatches) applied while the menu sets load:
//
//   GT Mode (gt_mode.mbl / gt_mode*.pmb)
//     - each mod car for sale joins its maker's dealer page: a disc dealer whose cars
//       have the same maker, or the dealer of a mod maker. Its strip button, full-screen
//       picture, name logo, description and price are cloned from the page's first car
//       with the mod's own pictures (PNG) and text;
//     - each mod maker with cars gets a dealer on its region's page (cloned from the
//       region's first dealer, its own icon, backdrop, logo, name and description) and
//       the dealer and showroom pages behind it;
//     - pages showing car name logos (hidden "<car>" elements: showroom, garage) get the
//       mod cars' logos.
//   Arcade (arcade.mbl / arcade*.pmb)
//     - car pickers get a strip icon ("<car>_mini") for each mod car in arcade mode.
//
// A missing picture leaves the element empty; a missing name logo is drawn as the
// name in the menu font. Nothing from the disc is copied into mods: every picture is
// the mod's own, and text is the mod's own, drawn with the game's fonts at run time.

#include "base/file.h"
#include "base/log.h"
#include "formats/png_reader.h"
#include "game/data/car_database.h"
#include "menu/flow/menu_set.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace gt3r::game {

namespace {

using menu::Element;
using menu::Layer;
using menu::MblFile;
using menu::MblItem;
using menu::MblPage;
using menu::MenuImage;
using menu::MenuScene;
using menu::Page;

bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Region item names on cd/cd -> region page suffixes.
const char* region_page(const std::string& region) {
    static const std::map<std::string, const char*> k = {{"jp", "jpn"}, {"us", "usa"}, {"gr", "ger"}, {"fr", "frc"},
                                                         {"it", "itl"}, {"uk", "eng"}, {"bel", "bel"}, {"aus", "aus"}};
    auto it = k.find(region);
    return it == k.end() ? nullptr : it->second;
}

// What the flow patch decided, for the layout patches of the same set.
struct DealerAdd {
    std::string page;     // dealer page, e.g. "cd/cd_usa_ford"
    std::string template_page; // a disc dealer page to clone (new makers), else = page
    std::vector<const data::ModCar*> cars;
    const data::ModMaker* maker = nullptr; // new maker's dealer
};

struct Plan {
    std::shared_ptr<data::CarDatabase> db;
    std::vector<DealerAdd> dealers;
    // Region page -> new makers on it.
    std::map<std::string, std::vector<const data::ModMaker*>> regions;
    std::string list; // the mod list it was made from (reload when it changes)
};

Plan& plan() {
    static Plan p;
    return p;
}

// The database with mod cars, loaded through the set's reader (cached per mod list).
const data::CarDatabase* database(const menu::flow::FileReader& read) {
    Plan& p = plan();
    auto list = read(Vfs::kModCarsList);
    std::string text = list ? std::string(list->begin(), list->end()) : std::string();
    if (p.db && text == p.list) return p.db.get();
    p = Plan{};
    p.list = text;
    if (text.empty()) return nullptr;
    auto db = std::make_shared<data::CarDatabase>();
    if (!db->load(read)) return nullptr;
    p.db = db;
    return p.db.get();
}

// --- images ------------------------------------------------------------------------------

// A mod picture as a scene image; -1 when the file is missing or broken.
int add_picture(MenuScene& scene, const std::string& path) {
    if (path.empty()) return -1;
    auto bytes = read_host_file(path);
    if (!bytes) {
        LOG_WARN("mod menus: missing picture %s", path.c_str());
        return -1;
    }
    try {
        PngImage png = decode_png(*bytes);
        return int(scene.add_image(MenuImage{png.width, png.height, std::move(png.rgba)}));
    } catch (const std::exception& e) {
        LOG_WARN("mod menus: %s: %s", path.c_str(), e.what());
        return -1;
    }
}

// Points an image element (and its focused look) at a mod picture; without one the
// element shows nothing.
void set_picture(Element& e, int image) {
    auto apply = [&](Element& x) {
        if (image >= 0 && x.image) x.image->image = u32(image);
        else if (image < 0) x.image.reset(), x.type = u16(x.type & ~Element::kImage);
    };
    apply(e);
    for (Element& a : e.alt) apply(a);
}

// Turns a name-logo image element into the name in the menu font (no artwork).
void set_name_text(Element& e, const std::string& name) {
    e.image.reset();
    e.colors.reset();
    e.type = u16((e.type & ~(Element::kImage | Element::kColor)) | Element::kText);
    menu::TextStyle t;
    t.unk0 = 1;
    t.align = 4; // centred
    t.color = {0xFF, 0xFF, 0xFF, 0x80};
    t.font = "sw_20#1";
    t.text = name;
    e.text = t;
    e.alt.clear();
}

void set_text(Element& e, const std::string& text) {
    if (e.text) e.text->text = text;
}

Layer* find_layer(Page& p, const std::string& name) {
    for (Layer& l : p.layers)
        if (l.name == name) return &l;
    return nullptr;
}

const Element* find_item(const Page& p, const std::string& name) {
    for (const Element& e : p.items)
        if (e.name == name) return &e;
    return nullptr;
}

// --- flow -----------------------------------------------------------------------------------

void patch_gt_flow(const menu::flow::FileReader& read, MblFile& mbl) {
    const data::CarDatabase* db = database(read);
    if (!db || db->mod_cars().empty()) return;
    Plan& p = plan();
    p.dealers.clear();
    p.regions.clear();
    // Maker -> disc dealer page (the page holding most of that maker's cars).
    std::map<std::string, std::map<std::string, int>> votes;
    for (const MblPage& pg : mbl.pages) {
        if (!starts(pg.name, "cd/")) continue;
        for (const MblItem& it : pg.items) {
            if (it.action != "set_car_code") continue;
            std::string label = it.name;
            if (!db->car(label) && !label.empty() && label[0] == '_') label.erase(0, 1);
            if (const data::Car* c = db->car(label); c && !db->mod_car(label)) ++votes[lower(c->maker)][pg.name];
        }
    }
    auto disc_dealer = [&](const std::string& maker) -> std::string {
        auto it = votes.find(lower(maker));
        if (it == votes.end()) return {};
        return std::max_element(it->second.begin(), it->second.end(),
                                [](const auto& a, const auto& b) { return a.second < b.second; })
            ->first;
    };
    auto dealer_for = [&](const std::string& page) -> DealerAdd& {
        for (DealerAdd& d : p.dealers)
            if (d.page == page) return d;
        p.dealers.push_back({page, page, {}, nullptr});
        return p.dealers.back();
    };
    for (const data::ModCar& m : db->mod_cars()) {
        if (!m.for_sale) continue;
        const data::Car* c = db->car(m.label);
        if (!c) continue;
        const data::ModMaker* maker = nullptr;
        for (const data::ModMaker& mk : db->mod_makers())
            if (lower(mk.name) == lower(c->maker)) maker = &mk;
        if (maker) {
            const char* rg = region_page(maker->region);
            if (!rg) continue;
            std::string region = std::string("cd/cd_") + rg;
            int rp = mbl.find_page(region);
            if (rp < 0 || mbl.pages[size_t(rp)].items.empty()) continue;
            DealerAdd& d = dealer_for(region + "_" + maker->id);
            d.template_page = mbl.pages[size_t(rp)].items.front().link;
            d.maker = maker;
            d.cars.push_back(&m);
            auto& list = p.regions[region];
            if (std::find(list.begin(), list.end(), maker) == list.end()) list.push_back(maker);
        } else if (std::string page = disc_dealer(c->maker); !page.empty()) {
            dealer_for(page).cars.push_back(&m);
        } else {
            LOG_WARN("mod menus: %s (%s): no dealer sells this maker; add a makers/*.ini to the mod", m.label.c_str(),
                     c->maker.c_str());
        }
    }
    // New makers: an item on the region page, a dealer page and a showroom page.
    for (const auto& [region, makers] : p.regions) {
        MblPage& rp = mbl.pages[size_t(mbl.find_page(region))];
        for (const data::ModMaker* mk : makers) {
            std::string page = region + "_" + mk->id;
            MblItem it = rp.items.back();
            it.name = "mod_" + mk->id;
            it.link = page;
            it.str40 = "m_mod_" + mk->id;
            it.str80 = "s_mod_" + mk->id;
            rp.items.push_back(it);
            const DealerAdd* d = nullptr;
            for (const DealerAdd& x : p.dealers)
                if (x.page == page) d = &x;
            int tp = d ? mbl.find_page(d->template_page) : -1;
            int tc = d ? mbl.find_page(d->template_page + "_car") : -1;
            if (tp < 0 || tc < 0) continue;
            MblPage dealer = mbl.pages[size_t(tp)];
            dealer.name = page;
            dealer.back = region;
            dealer.items.clear();
            MblPage car = mbl.pages[size_t(tc)];
            car.name = page + "_car";
            car.back = page;
            mbl.pages.push_back(std::move(dealer));
            mbl.pages.push_back(std::move(car));
        }
    }
    // The cars on their dealer pages.
    for (const DealerAdd& d : p.dealers) {
        int dp = mbl.find_page(d.page), tp = mbl.find_page(d.template_page);
        if (dp < 0 || tp < 0 || mbl.pages[size_t(tp)].items.empty()) continue;
        MblItem tmpl = mbl.pages[size_t(tp)].items.front();
        for (const data::ModCar* m : d.cars) {
            MblItem it = tmpl;
            it.name = m->label;
            it.action = "set_car_code";
            it.link = d.page + "_car";
            it.str40 = "m_mod_" + m->label;
            it.str80 = "s_mod_" + m->label;
            mbl.pages[size_t(dp)].items.push_back(std::move(it));
        }
        LOG_INFO("mod menus: %zu mod car(s) at %s", d.cars.size(), d.page.c_str());
    }
}

// --- layouts --------------------------------------------------------------------------------

// The disc layout page a dealer page is drawn from (its own, or its template's).
Page* layout_page(MenuScene& scene, const std::string& name) {
    int i = scene.file().find_page(name);
    return i < 0 ? nullptr : &scene.mutable_file().pages[size_t(i)];
}

// The first car's (or maker's) items and layers on a dealer (region) page: the template.
struct Slot {
    const Element* item = nullptr;
    const Layer* m = nullptr; // m_01: full-screen picture
    const Layer* s = nullptr; // s_01: texts, logo, price
};

Slot first_slot(const Page& pg, const MblPage& flow) {
    Slot s;
    if (flow.items.empty()) return s;
    const MblItem& it = flow.items.front();
    s.item = find_item(pg, it.name);
    for (const Layer& l : pg.layers) {
        if (l.name == it.str40) s.m = &l;
        if (l.name == it.str80) s.s = &l;
    }
    return s;
}

void add_car_slot(MenuScene& scene, Page& pg, const Slot& t, const data::ModCar& m, const data::CarDatabase& db) {
    const data::Car* c = db.car(m.label);
    std::string name = c ? c->name : m.label;
    if (t.item) {
        Element e = *t.item;
        e.name = m.label;
        for (Element& a : e.alt) a.name = m.label + "_a";
        set_picture(e, add_picture(scene, m.thumb));
        pg.items.push_back(std::move(e));
    }
    if (t.m) {
        Layer l = *t.m;
        l.name = "m_mod_" + m.label;
        for (Element& e : l.elements)
            if (e.image) set_picture(e, add_picture(scene, m.picture));
        pg.layers.push_back(std::move(l));
    }
    if (t.s) {
        Layer l = *t.s;
        l.name = "s_mod_" + m.label;
        for (Element& e : l.elements) {
            if (e.text) set_text(e, m.description);
            else if (e.image) {
                int img = add_picture(scene, m.logo);
                if (img >= 0) set_picture(e, img);
                else set_name_text(e, name);
            }
        }
        for (Element& w : l.widgets)
            if (w.widget_class && *w.widget_class == "goods_car_price") w.name = m.label + "_price";
        pg.layers.push_back(std::move(l));
    }
}

void add_maker_slot(MenuScene& scene, Page& pg, const Slot& t, const data::ModMaker& mk) {
    if (t.item) {
        Element e = *t.item;
        e.name = "mod_" + mk.id;
        for (Element& a : e.alt) a.name = e.name + "_a";
        set_picture(e, add_picture(scene, mk.icon));
        pg.items.push_back(std::move(e));
    }
    if (t.m) {
        Layer l = *t.m;
        l.name = "m_mod_" + mk.id;
        for (Element& e : l.elements)
            if (e.image) set_picture(e, add_picture(scene, mk.background));
        pg.layers.push_back(std::move(l));
    }
    if (t.s) {
        Layer l = *t.s;
        l.name = "s_mod_" + mk.id;
        // The region page's maker layer: logo (image), description (smaller text), name.
        bool first_text = true;
        for (Element& e : l.elements) {
            if (e.image) {
                int img = add_picture(scene, mk.logo);
                if (img >= 0) set_picture(e, img);
                else set_name_text(e, mk.name);
            } else if (e.text) {
                // Disc order: the description first, then the name.
                set_text(e, first_text ? mk.description : mk.name);
                first_text = false;
            }
        }
        pg.layers.push_back(std::move(l));
    }
}

// Name logos of every mod car next to the disc cars' ("<car>" in a hidden layer).
void add_logos(MenuScene& scene, Page& pg, const data::CarDatabase& db) {
    Layer* hidden = find_layer(pg, "hidden");
    if (!hidden) return;
    const Element* tmpl = nullptr;
    for (const Element& e : hidden->elements)
        if (e.image && db.car(e.name) && !db.mod_car(e.name)) {
            tmpl = &e;
            break;
        }
    if (!tmpl) return;
    Element base = *tmpl;
    for (const data::ModCar& m : db.mod_cars()) {
        Element e = base;
        e.name = m.label;
        int img = add_picture(scene, m.logo);
        if (img >= 0) set_picture(e, img);
        else set_name_text(e, db.car(m.label) ? db.car(m.label)->name : m.label);
        hidden->elements.push_back(std::move(e));
    }
}

// Arcade car pickers: a strip icon for each mod car in arcade mode.
void add_arcade_icons(MenuScene& scene, Page& pg, const data::CarDatabase& db) {
    Layer* hidden = find_layer(pg, "hidden");
    if (!hidden) return;
    const Element* tmpl = nullptr;
    for (const Element& e : hidden->elements)
        if (e.image && e.name.size() > 5 && e.name.compare(e.name.size() - 5, 5, "_mini") == 0) {
            tmpl = &e;
            break;
        }
    if (!tmpl) return;
    // The name logo shown above the car ("<car>", the size of the disc's 320x100 logos).
    const Element* logo_tmpl = nullptr;
    std::string first_car = tmpl->name.substr(0, tmpl->name.size() - 5);
    for (const Element& e : hidden->elements)
        if (e.name == first_car && e.image) logo_tmpl = &e;
    Element base = *tmpl;
    std::optional<Element> logo_base;
    if (logo_tmpl) logo_base = *logo_tmpl;
    for (const data::ModCar& m : db.mod_cars()) {
        if (m.arcade_class.empty()) continue;
        Element e = base;
        e.name = m.label + "_mini";
        set_picture(e, add_picture(scene, m.thumb));
        hidden->elements.push_back(std::move(e));
        if (logo_base) {
            Element l = *logo_base;
            l.name = m.label;
            int img = add_picture(scene, m.logo);
            if (img >= 0) set_picture(l, img);
            else set_name_text(l, db.car(m.label) ? db.car(m.label)->name : m.label);
            hidden->elements.push_back(std::move(l));
        }
    }
}

void patch_gt_layout(const menu::flow::FileReader& read, MenuScene& scene, const MblFile* mbl) {
    const data::CarDatabase* db = database(read);
    if (!db || db->mod_cars().empty()) return;
    Plan& p = plan();
    auto flow_page = [&](const std::string& name) -> const MblPage* {
        int i = mbl ? mbl->find_page(name) : -1;
        return i < 0 ? nullptr : &mbl->pages[size_t(i)];
    };
    // New makers: clone their template dealer and showroom pages into this file first.
    for (const DealerAdd& d : p.dealers) {
        if (!d.maker || d.page == d.template_page) continue;
        if (scene.file().find_page(d.page) >= 0) continue;
        for (const char* suffix : {"", "_car"}) {
            int t = scene.file().find_page(d.template_page + suffix);
            if (t < 0) continue;
            Page copy = scene.file().pages[size_t(t)];
            copy.name = d.page + suffix;
            if (!*suffix) { // the dealer page: no disc cars
                copy.items.clear();
                copy.layers.erase(std::remove_if(copy.layers.begin(), copy.layers.end(),
                                                 [](const Layer& l) {
                                                     return starts(l.name, "m_") || starts(l.name, "s_");
                                                 }),
                                  copy.layers.end());
            } else if (Layer* hidden = find_layer(copy, "hidden")) {
                // The showroom: the template's car logos stay as clone sources for add_logos.
                (void)hidden;
            }
            scene.mutable_file().pages.push_back(std::move(copy));
        }
    }
    for (const DealerAdd& d : p.dealers) {
        Page* pg = layout_page(scene, d.page);
        const Page* tp = layout_page(scene, d.template_page);
        const MblPage* tf = flow_page(d.template_page);
        if (!pg || !tp || !tf) continue;
        Page tcopy = *tp; // the template may be the page itself: copy before adding
        Slot t = first_slot(tcopy, *tf);
        for (const data::ModCar* m : d.cars) add_car_slot(scene, *pg, t, *m, *db);
    }
    for (const auto& [region, makers] : p.regions) {
        Page* pg = layout_page(scene, region);
        const MblPage* rf = flow_page(region);
        if (!pg || !rf) continue;
        Page tcopy = *pg;
        Slot t = first_slot(tcopy, *rf);
        for (const data::ModMaker* mk : makers) add_maker_slot(scene, *pg, t, *mk);
    }
    for (Page& pg : scene.mutable_file().pages) add_logos(scene, pg, *db);
}

void patch_arcade_layout(const menu::flow::FileReader& read, MenuScene& scene) {
    const data::CarDatabase* db = database(read);
    if (!db || db->mod_cars().empty()) return;
    for (Page& pg : scene.mutable_file().pages)
        if (pg.name == "ar/select_car" || pg.name == "ar/select_car_2p") add_arcade_icons(scene, pg, *db);
}

// The patched gt_mode flow, for the layout pass (page names -> template items).
MblFile& gt_flow() {
    static MblFile f;
    return f;
}

} // namespace

void install_mod_menu_patches() {
    menu::flow::MenuPatches& mp = menu::flow::menu_patches();
    mp.flow = [](const std::string& set, const menu::flow::FileReader& read, MblFile& mbl) {
        if (set != "gt_mode") return;
        patch_gt_flow(read, mbl);
        gt_flow() = mbl;
    };
    mp.layout = [](const std::string& set, int, const menu::flow::FileReader& read, MenuScene& scene) {
        if (set == "gt_mode") patch_gt_layout(read, scene, &gt_flow());
        else if (set == "arcade") patch_arcade_layout(read, scene);
    };
}

} // namespace gt3r::game
