// Mod events in GT Mode's menus (docs/MODDING.md "Adding events"), as menu content patches
// (menu/flow/menu_set.h MenuPatches) applied while gt_mode.mbl / gt_mode*.pmb load.
//
// The leagues come off the disc: the series pages (items with set_race_code) name their league
// page as their back page, and the league pages name the race map. Each event goes on the league
// page its file names (league = r/beginner/beginner) when the disc has that page; otherwise on a
// Custom Events league, a clone of the map's first league page added below the map's own leagues.
//
// On its league page an event is an item like the disc's series (its strip icon, full-screen
// flyer and title), cloned from the page's first series. Behind it, its series page and entry
// page are clones of a disc series with room for its races (the same kind: single races or a
// championship; the league's own series first, so the page's header is the league's), with the
// race items renamed to the event's codes and the rest dropped. Everything the disc's pages draw
// from the race records (course logos and names, results, race information) works on the
// event's own records (game/data/mod_events.h). Pictures are the mod's own (PNG); without one
// the event's title is drawn in the menu font.

#include "game/gt_mode_mods.h"

#include "base/log.h"

#include <algorithm>
#include <map>
#include <set>

namespace gt3r::game {

using namespace mod_menu;
using menu::Element;
using menu::Layer;
using menu::MblFile;
using menu::MblItem;
using menu::MblPage;
using menu::MenuScene;
using menu::Page;

namespace {

constexpr const char* kCustomLeague = "r/custom/custom";
constexpr const char* kCustomItem = "custom_events";
constexpr const char* kCustomLayer = "s_mod_custom";
constexpr const char* kCustomTitle = "Custom Events";

// What the flow patch decided, for the layout patches of the same set.
struct EventAdd {
    const data::ModEvent* ev = nullptr;
    std::string league;        // the league page it is listed on
    std::string league_from;   // the page whose first item its league item copies
    std::string page, entry;   // its series and entry pages
    std::string tmpl, tmpl_entry; // the disc pages they are cloned from
    bool own_league_header = true; // the template is one of its league's own series
    std::map<std::string, std::string> renamed; // template item -> its item ("" = dropped)
};

struct EventPlan {
    std::vector<EventAdd> adds;
    std::string custom_from; // the league page the Custom Events league is cloned from ("" = none made)
    std::string map;         // the race map page
    std::string map_from;    // the map item the Custom Events item copies
};

EventPlan& event_plan() {
    static EventPlan p;
    return p;
}

std::string dir_of(const std::string& page) { return page.substr(0, page.find_last_of('/')); }
std::string m_layer(const data::ModEvent& e) { return "m_mod_" + e.series; }
std::string s_layer(const data::ModEvent& e) { return "s_mod_" + e.series; }

const MblPage* page_of(const MblFile& mbl, const std::string& name) {
    int i = mbl.find_page(name);
    return i < 0 ? nullptr : &mbl.pages[size_t(i)];
}

int race_items(const MblPage& p) {
    return int(std::count_if(p.items.begin(), p.items.end(), [](const MblItem& it) { return it.action == "set_race_code"; }));
}
bool has_champ(const MblPage& p) {
    return std::any_of(p.items.begin(), p.items.end(), [](const MblItem& it) { return it.action == "set_race_code_champ"; });
}

} // namespace

const char* const kCustomLeaguePage = kCustomLeague;

std::vector<std::string> league_pages(const MblFile& mbl, std::string* map) {
    std::set<std::string> leagues;
    for (const MblPage& p : mbl.pages)
        if (race_items(p) > 0 && starts(p.name, "r/") && !p.back.empty()) leagues.insert(p.back);
    std::string m;
    for (const std::string& l : leagues)
        if (const MblPage* lp = page_of(mbl, l); lp && !lp->back.empty()) {
            m = lp->back;
            break;
        }
    if (map) *map = m;
    std::vector<std::string> out;
    if (const MblPage* mp = page_of(mbl, m))
        for (const MblItem& it : mp->items)
            if (leagues.count(it.link) && std::find(out.begin(), out.end(), it.link) == out.end()) out.push_back(it.link);
    return out;
}

void patch_event_flow(const menu::flow::FileReader& read, MblFile& mbl) {
    EventPlan& plan = event_plan();
    plan = EventPlan{};
    const data::CarDatabase* db = database(read);
    if (!db || db->mod_events().empty()) return;

    // The disc's series pages, their league pages and the race map, from the flow itself.
    std::vector<std::string> series;
    for (const MblPage& p : mbl.pages)
        if (race_items(p) > 0 && starts(p.name, "r/") && !p.back.empty()) series.push_back(p.name);
    // The map's leagues in its order: the first is the Custom Events league's model.
    const std::vector<std::string> map_leagues = league_pages(mbl, &plan.map);
    const std::set<std::string> leagues(map_leagues.begin(), map_leagues.end());
    if (const MblPage* mp = page_of(mbl, plan.map))
        for (const MblItem& it : mp->items)
            if (leagues.count(it.link)) plan.map_from = it.name;
    if (map_leagues.empty()) {
        LOG_WARN("mod events: this disc's GT Mode menus have no league pages; no events listed");
        return;
    }

    auto add_custom_league = [&] {
        if (!plan.custom_from.empty()) return;
        const MblPage* from = page_of(mbl, map_leagues.front());
        int mi = mbl.find_page(plan.map);
        if (!from || mi < 0) return;
        MblPage c = *from;
        c.name = kCustomLeague;
        c.items.clear();
        plan.custom_from = from->name;
        MblPage& map = mbl.pages[size_t(mi)];
        auto last = std::find_if(map.items.begin(), map.items.end(), [&](const MblItem& it) { return it.name == plan.map_from; });
        if (last == map.items.end()) return;
        MblItem item = *last;
        item.name = kCustomItem;
        item.link = kCustomLeague;
        item.str80 = kCustomLayer;
        map.items.push_back(std::move(item));
        mbl.pages.push_back(std::move(c));
    };

    for (const data::ModEvent& ev : db->mod_events()) {
        EventAdd a;
        a.ev = &ev;
        const bool on_disc = !ev.def.league.empty() && leagues.count(ev.def.league);
        if (!on_disc) add_custom_league();
        a.league = on_disc ? ev.def.league : std::string(kCustomLeague);
        a.league_from = on_disc ? ev.def.league : plan.custom_from;
        if (mbl.find_page(a.league) < 0 || mbl.find_page(a.league_from) < 0) continue;

        // The template: a disc series of the same kind with room for every race, the league's
        // own first, then the one with the fewest races to drop.
        const int n = int(ev.races.size());
        const bool champ = !ev.champ.empty();
        std::set<std::string> own;
        if (const MblPage* lp = page_of(mbl, a.league_from))
            for (const MblItem& it : lp->items) own.insert(it.link);
        const MblPage* t = nullptr;
        std::pair<int, int> best{2, 1000};
        for (const std::string& name : series) {
            const MblPage* p = page_of(mbl, name);
            const int k = p ? race_items(*p) : 0;
            if (!p || k < n || has_champ(*p) != champ) continue;
            const std::pair<int, int> score{own.count(name) ? 0 : 1, k - n};
            if (score < best) best = score, t = p;
        }
        if (!t) {
            LOG_WARN("mod events: %s: no series on this disc's menus holds %d %s", ev.series.c_str(), n,
                     champ ? "championship rounds" : "races");
            continue;
        }
        a.tmpl = t->name;
        a.own_league_header = best.first == 0 && a.league == a.league_from;
        a.page = dir_of(a.league) + "/" + lower(ev.series);
        a.entry = a.page + "_entry";
        if (mbl.find_page(a.page) >= 0) continue; // (two mods with the same codes: the first stays)

        // The series page: the template's items under the event's codes.
        MblPage sp = *t;
        sp.name = a.page;
        sp.back = a.league;
        sp.items.clear();
        size_t k = 0;
        for (const MblItem& it : t->items) {
            MblItem x = it;
            if (it.action == "set_race_code_info") x.name = ev.races.front() + "_info";
            else if (it.action == "set_race_code_champ") x.name = ev.champ;
            else if (it.action == "set_race_code") {
                if (k >= ev.races.size()) {
                    a.renamed[it.name] = "";
                    continue;
                }
                x.name = ev.races[k++];
            }
            if (a.tmpl_entry.empty() && it.action == "set_race_code") a.tmpl_entry = it.link;
            a.renamed[it.name] = x.name;
            sp.items.push_back(std::move(x));
        }
        // (Race items before the champ item: point every race item at the entry page.)
        for (MblItem& x : sp.items)
            if (x.action == "set_race_code" || x.action == "set_race_code_champ") x.link = a.entry;
        const MblPage* te = page_of(mbl, a.tmpl_entry);
        if (!te) continue;
        MblPage ep = *te;
        ep.name = a.entry;
        ep.back = a.page;

        // Its item on the league page: the page's first series item, renamed.
        const MblPage* lf = page_of(mbl, a.league_from);
        if (!lf || lf->items.empty()) continue;
        MblItem li = lf->items.front();
        li.name = ev.item();
        li.link = a.page;
        li.str40 = m_layer(ev);
        li.str80 = s_layer(ev);
        mbl.pages[size_t(mbl.find_page(a.league))].items.push_back(std::move(li));
        mbl.pages.push_back(std::move(sp));
        mbl.pages.push_back(std::move(ep));
        LOG_INFO("mod events: '%s' on %s (%s, from %s)", ev.def.title.c_str(), a.league.c_str(), a.page.c_str(), a.tmpl.c_str());
        plan.adds.push_back(std::move(a));
    }
}

namespace {

// The event's title in the menu font over a picture-less backdrop: centred, large.
Element title_text(const Element& box_from, const std::string& title, float y) {
    Element e;
    e.name = "mod_event_title";
    e.type = Element::kText;
    e.flags = 0x001C; // (as the disc's text elements: fades in with its layer)
    e.x = 0;
    e.y = y;
    e.z = box_from.z + 0.5f;
    e.w = 640;
    e.h = 40;
    menu::TextStyle t;
    t.unk0 = 1;
    t.align = 4; // centred
    t.color = {0xFF, 0xFF, 0xFF, 0x80};
    t.font = "sw_20#1";
    t.text = title;
    e.text = t;
    return e;
}

// A line of the event's description, in the bar style of the disc's texts.
Element description_text(const Element& box_from, const std::string& text, float y) {
    Element e = title_text(box_from, text, y);
    e.name = "mod_event_description";
    e.h = 24;
    e.text->font = "sw_16#1";
    e.text->color = {0xCC, 0xCC, 0xCC, 0x7F};
    return e;
}

// The full-screen picture of a layer or page (an image element or widget the size of the screen).
template <typename F>
void each_backdrop(std::vector<Element>& v, F&& f) {
    for (Element& e : v)
        if (e.image && e.w >= 600 && e.h >= 400) f(e);
}

// A backdrop without a picture: a dark gradient for the title to stand on.
void dark_backdrop(Element& e) {
    e.image.reset();
    e.type = u16((e.type & ~Element::kImage) | Element::kColor);
    const menu::Rgba top{0x3A, 0x3E, 0x48, 0xFF}, bottom{0x10, 0x11, 0x16, 0xFF};
    e.colors = std::array<menu::Rgba, 4>{bottom, bottom, top, top}; // (bottom-left, bottom-right, top-right, top-left)
    e.alt.clear();
}

// The event's picture on a backdrop element; without one the backdrop is dark and the title is
// drawn over it. Returns whether the picture is there.
bool picture_backdrop(MenuScene& scene, std::vector<Element>& elements, const data::ModEvent& ev, int& image) {
    if (image == -2) image = add_picture(scene, ev.def.picture);
    bool any = false;
    each_backdrop(elements, [&](Element& e) {
        if (image >= 0) set_picture(e, image);
        else dark_backdrop(e);
        any = true;
    });
    return any && image >= 0;
}

// A league page's header ("category": the league's name as artwork) as text.
void header_text(Page& pg, const std::string& text) {
    for (Layer& l : pg.layers)
        for (Element& e : l.elements)
            if (e.name == "category") set_name_text(e, text);
}

void patch_series_pages(MenuScene& scene, const EventAdd& a, const MblFile& mbl) {
    const data::ModEvent& ev = *a.ev;
    int image = -2;
    // The series page.
    if (scene.file().find_page(a.page) < 0)
        if (const Page* tp = layout_page(scene, a.tmpl)) {
            Page pg = *tp;
            pg.name = a.page;
            std::vector<Element> items;
            for (const Element& e : pg.items) {
                auto it = a.renamed.find(e.name);
                if (it == a.renamed.end()) {
                    items.push_back(e);
                } else if (!it->second.empty()) {
                    items.push_back(e);
                    items.back().name = it->second;
                }
            }
            pg.items = std::move(items);
            bool shown = false;
            for (Layer& l : pg.layers)
                if (l.name == "defaultLayer") {
                    shown = picture_backdrop(scene, l.elements, ev, image);
                    if (!shown && !l.elements.empty()) l.elements.push_back(title_text(l.elements.front(), ev.def.title, 300));
                }
            if (!a.own_league_header) header_text(pg, a.league == kCustomLeague ? kCustomTitle : "");
            scene.mutable_file().pages.push_back(std::move(pg));
        }
    // The entry page.
    if (scene.file().find_page(a.entry) < 0)
        if (const Page* tp = layout_page(scene, a.tmpl_entry)) {
            Page pg = *tp;
            pg.name = a.entry;
            for (Layer& l : pg.layers)
                if (l.name == "defaultLayer") {
                    bool shown = picture_backdrop(scene, l.elements, ev, image);
                    if (!shown && !l.elements.empty()) l.elements.push_back(title_text(l.elements.front(), ev.def.title, 300));
                }
            if (!a.own_league_header) header_text(pg, a.league == kCustomLeague ? kCustomTitle : "");
            scene.mutable_file().pages.push_back(std::move(pg));
        }
    (void)mbl;
}

// The event's item on its league page: the strip icon, the flyer (m_ layer) and the title and
// result marks (s_ layer), copied from the page's first series.
void add_league_item(MenuScene& scene, Page& pg, const Page& from, const MblPage& from_flow, const EventAdd& a) {
    const data::ModEvent& ev = *a.ev;
    if (find_item(pg, ev.item())) return;
    const MblItem& first = from_flow.items.front();
    const Element* item = find_item(from, first.name);
    const Layer* m = nullptr;
    const Layer* s = nullptr;
    for (const Layer& l : from.layers) {
        if (l.name == first.str40) m = &l;
        if (l.name == first.str80) s = &l;
    }
    const int image = add_picture(scene, ev.def.picture);
    if (item) {
        Element e = *item;
        e.name = ev.item();
        for (Element& alt : e.alt) alt.name = e.name + "_a";
        set_picture(e, image);
        pg.items.push_back(std::move(e));
    }
    if (m) {
        Layer l = *m;
        l.name = m_layer(ev);
        bool flyer = false;
        auto put = [&](Element& e) {
            if (image >= 0) set_picture(e, image);
            else dark_backdrop(e);
            flyer = true;
        };
        each_backdrop(l.elements, put);
        each_backdrop(l.widgets, put);
        const Element& ref = !l.widgets.empty() ? l.widgets.front() : !l.elements.empty() ? l.elements.front() : Element{};
        if (flyer && image < 0) l.elements.push_back(title_text(ref, ev.def.title, 300));
        if (!ev.def.description.empty()) l.elements.push_back(description_text(ref, ev.def.description, 150));
        pg.layers.push_back(std::move(l));
    }
    if (s) {
        Layer l = *s;
        l.name = s_layer(ev);
        const std::string tag = l.name.substr(2); // "event_status<tag>" belongs to layer "s_<tag>"
        for (Element& w : l.widgets) {
            if (w.widget_class && *w.widget_class == "event_status") w.name = "event_status" + tag;
            else if (w.widget_class && *w.widget_class == "need_license") w.name = "need_license" + tag;
            else if (w.text) set_text(w, ev.def.title);
        }
        for (Element& e : l.elements)
            if (e.text) set_text(e, ev.def.title);
        pg.layers.push_back(std::move(l));
    }
}

// The Custom Events league: its page (a disc league page without its series, the header as text)
// and its item on the race map (the map's last league, one row further, its name as text).
void add_custom_league(MenuScene& scene, const EventPlan& plan, const MblFile& mbl) {
    if (plan.custom_from.empty()) return;
    if (scene.file().find_page(kCustomLeague) < 0)
        if (const Page* from = layout_page(scene, plan.custom_from)) {
            Page pg = *from;
            pg.name = kCustomLeague;
            pg.items.clear();
            pg.layers.erase(std::remove_if(pg.layers.begin(), pg.layers.end(),
                                           [](const Layer& l) { return starts(l.name, "m_") || starts(l.name, "s_"); }),
                            pg.layers.end());
            header_text(pg, kCustomTitle);
            scene.mutable_file().pages.push_back(std::move(pg));
        }
    Page* map = layout_page(scene, plan.map);
    const MblPage* mf = page_of(mbl, plan.map);
    if (!map || !mf || find_item(*map, kCustomItem)) return;
    // The map's league rows step by the same offset: the new row is one more step past the last.
    std::vector<const Element*> rows;
    for (const MblItem& it : mf->items)
        if (it.name != kCustomItem)
            if (const Element* e = find_item(*map, it.name)) rows.push_back(e);
    const MblItem* last = nullptr;
    for (const MblItem& it : mf->items)
        if (it.name == plan.map_from) last = &it;
    const Element* le = last ? find_item(*map, last->name) : nullptr;
    if (!le) return;
    float dx = 0, dy = -40;
    if (rows.size() >= 2) {
        const Element* prev = nullptr;
        for (const Element* r : rows)
            if (r != le && (!prev || std::abs(r->y - le->y) < std::abs(prev->y - le->y))) prev = r;
        if (prev) dx = le->x - prev->x, dy = le->y - prev->y;
    }
    Element row = *le;
    row.name = kCustomItem;
    row.x += dx, row.y += dy;
    // The disc's row is a picture of its name: ours is the name in the menu font, with the
    // focused look's colours.
    std::optional<std::array<menu::Rgba, 4>> focus_colors;
    if (!row.alt.empty()) focus_colors = row.alt.front().colors;
    std::vector<Element> alt = row.alt;
    set_name_text(row, kCustomTitle);
    row.text->font = "sw_20#1";
    if (!alt.empty()) {
        Element a = row;
        a.name = std::string(kCustomItem) + "_a";
        if (focus_colors) a.text->color = {(*focus_colors)[0].r, (*focus_colors)[0].g, (*focus_colors)[0].b, 0x80};
        a.z = alt.front().z;
        row.alt.push_back(std::move(a));
        row.type = u16(row.type | Element::kAlt);
    }
    map->items.push_back(std::move(row));
    // Its backdrop: the last league's.
    for (const Layer& l : map->layers)
        if (l.name == last->str80) {
            Layer c = l;
            c.name = kCustomLayer;
            map->layers.push_back(std::move(c));
            break;
        }
}

} // namespace

void patch_event_layout(const menu::flow::FileReader& read, MenuScene& scene, const MblFile& mbl) {
    const EventPlan& plan = event_plan();
    if (plan.adds.empty() || !database(read)) return;
    add_custom_league(scene, plan, mbl);
    for (const EventAdd& a : plan.adds) {
        patch_series_pages(scene, a, mbl);
        Page* pg = layout_page(scene, a.league);
        const Page* from = layout_page(scene, a.league_from);
        const MblPage* ff = page_of(mbl, a.league_from);
        if (!pg || !from || !ff || ff->items.empty()) continue;
        // (Custom Events: its own first item is an event; the disc league's first series is the model.)
        if (a.league == kCustomLeague) {
            const MblPage* disc = page_of(mbl, plan.custom_from);
            const Page* dp = layout_page(scene, plan.custom_from);
            if (!disc || !dp || disc->items.empty()) continue;
            Page copy = *dp;
            add_league_item(scene, *pg, copy, *disc, a);
        } else {
            Page copy = *from;
            add_league_item(scene, *pg, copy, *ff, a);
        }
    }
}

} // namespace gt3r::game
