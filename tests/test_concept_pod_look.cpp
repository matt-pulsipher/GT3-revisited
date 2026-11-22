// The pod's look and sounds on each Gran Turismo Concept disc (GT3R_DISC_CONCEPT, disc images separated by ';'; skipped
// without it): its model's callback draws (the tail's lean and nod, the lamps' classes, the faces of the moods), the
// CLUT animations of its faces (a group per mood) and the effect of every mood in the disc's se.inf.

#include "test_framework.h"

#include "concept_discs.h"
#include "disc/disc.h"
#include "vfs/vfs.h"
#include "formats/model/car_model.h"
#include "formats/model/gtm1.h"
#include "formats/sound/inst.h"
#include "game/data/car_database.h"
#include "race/car_sfx.h"

#include <cmath>
#include <memory>
#include <set>

using namespace gt3r;
using namespace gt3r::test;

namespace {

// The vertices a set of meshes draws, and the vertex colours it uses.
size_t vertex_count(const std::vector<scene::Mesh>& ms) {
    size_t n = 0;
    for (const scene::Mesh& m : ms) n += m.vertices.size();
    return n;
}
std::set<u32> colours(const std::vector<scene::Mesh>& ms) {
    std::set<u32> out;
    for (const scene::Mesh& m : ms)
        for (const scene::Vertex& v : m.vertices) out.insert(v.color & 0xFFFFFF);
    return out;
}

} // namespace

TEST(concept_pod_look_disc) {
    const std::vector<std::string> discs = concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> disc = Disc::open(path);
        Vfs vfs(&disc->volume());
        data::CarDatabase db;
        CHECK(db.load(vfs));
        const data::CarVariant* pod = db.variant("gtcrr_pod_tmv_01");
        const data::Car* car = pod ? db.car(pod->car) : nullptr;
        CHECK(car != nullptr);
        if (!car) continue;
        const std::string model = db.model_of(*car);
        std::optional<Bytes> file = vfs.read("cars/day/" + model);
        CHECK(file.has_value());
        if (!file) continue;
        std::vector<gtm1::MorphDraws> morphs;
        std::vector<gtm1::ClutSet> cluts;
        CarModelOptions o;
        o.wheels = false;
        o.morphs = &morphs;
        o.clut_sets = &cluts;
        (void)load_car_model(*file, o, model);
        CHECK(!morphs.empty());
        if (morphs.empty()) continue;
        const gtm1::MorphDraws& md = morphs[0];
        std::printf("  %s: pod %s, %zu callback draws, phases", path.c_str(), model.c_str(), md.size());
        for (int i : md.ids()) std::printf(" %d", i);
        std::printf(", branches");
        for (int i : md.branch_ids()) std::printf(" %d", i);
        // The tail morphs by id 7 under its two branches; the lamps' five classes and the moods' faces are branches.
        CHECK(md.ids() == std::vector<int>{7});
        CHECK(md.branch_ids() == (std::vector<int>{7, 8, 10}));
        gtm1::CarCallback rest = gtm1::kCarCallbackDefaults;
        rest.branch[7] = 0, rest.phase[7] = 0;
        gtm1::CarCallback leant = rest;
        leant.phase[7] = 1;
        const std::vector<scene::Mesh> a = md.meshes(rest), b = md.meshes(leant);
        CHECK(vertex_count(a) == vertex_count(b) && vertex_count(a) > 0);
        float most = 0;
        for (size_t m = 0; m < a.size() && m < b.size(); ++m)
            for (size_t i = 0; i < a[m].vertices.size() && i < b[m].vertices.size(); ++i)
                for (int k = 0; k < 3; ++k) most = std::max(most, std::fabs(a[m].vertices[i].pos[k] - b[m].vertices[i].pos[k]));
        std::printf(", the tail moves %.3f m", most);
        CHECK(most > 0.05f && most < 1.0f);
        // Each lamp class its own colours; every mood but the rest draws its face.
        std::set<std::set<u32>> lamp_looks;
        for (int k = 0; k < 5; ++k) {
            gtm1::CarCallback v = rest;
            v.branch[8] = k;
            lamp_looks.insert(colours(md.meshes(v)));
        }
        std::printf(", %zu lamp looks", lamp_looks.size());
        CHECK(lamp_looks.size() >= 4); // (classes 0 and 3 share a colour)
        size_t faces = 0;
        for (int mood = 1; mood <= 10; ++mood) {
            gtm1::CarCallback v = rest;
            v.branch[9] = v.branch[10] = mood;
            if (vertex_count(md.meshes(v)) > vertex_count(a)) ++faces;
        }
        std::printf(", %zu moods with faces", faces);
        CHECK(faces == 10);
        // The faces' CLUT animations: a group per mood, 1 .. 10.
        std::set<int> groups;
        for (const gtm1::ClutSet& c : cluts)
            for (const Tex1::ClutAnimation& an : c.textures.player().animations()) groups.insert(an.group);
        std::printf(", CLUT groups");
        for (int gr : groups) std::printf(" %d", gr);
        std::printf("\n");
        CHECK(groups == (std::set<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
        // Every mood's effect is in the disc's se.inf, in the pod bank (3).
        std::optional<Bytes> inf = vfs.read("sound/se.inf");
        CHECK(inf.has_value());
        if (!inf) continue;
        const std::vector<sound::SoundEffect> effects = sound::parse_se_index(*inf);
        for (int mood = 1; mood <= 10; ++mood) {
            bool found = false;
            for (const sound::SoundEffect& e : effects)
                if (e.name == race::mood_effect(mood)) found = e.bank == 3;
            CHECK(found);
        }
        CHECK(race::mood_effect(0) == nullptr);
    }
}
