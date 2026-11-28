#pragma once

// Car models of a race's last build, for its next one (the opponents': RaceScreen::load_opponent_cars; player 2's):
// a retry builds the field again, mostly with the same models. Kept by their key (model and paint, for
// player 2's car with its wheels) while the custom paints and the texture pack in use are those they were built with; the race screen
// lets them go at the next start, and when a start or a build fails (with the field they were kept for).

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

namespace gt3r::race {

template <class Model>
class KeptModels {
public:
    // The model (a mod car's: its base car; "/<wheel>" after it on a fitted wheel) and the paint, as load_opponent_cars
    // tells its models apart.
    using Key = std::pair<std::string, int>;
    using Map = std::map<Key, std::shared_ptr<Model>>;

    // The kept models of `wanted`, handed over for a build with `paints` and `pack` (null: none), when the last build had
    // the same ones; the others (all of them with other paints or another pack) are let go here, before the build makes
    // new ones, so the old and the new field are never held together.
    Map take(const std::shared_ptr<const void>& paints, const std::shared_ptr<const void>& pack,
             const std::set<Key>& wanted) {
        Map kept = std::exchange(models_, {}), models;
        if (same(paints_, paints) && same(pack_, pack))
            for (const Key& k : wanted)
                if (auto n = kept.extract(k)) models.insert(std::move(n));
        return models; // (the rest of `kept` let go)
    }
    // A build's models (only those: the ones it no longer has are let go), and what they were built with.
    void keep(Map models, const std::shared_ptr<const void>& paints, const std::shared_ptr<const void>& pack) {
        models_ = std::move(models), paints_ = paints, pack_ = pack;
    }
    void clear() { *this = KeptModels(); }

private:
    // Held weakly, so a pack or paint set let go is not kept alive; compared by owner, so a new one is never taken for
    // it, even at its address.
    static bool same(const std::weak_ptr<const void>& a, const std::shared_ptr<const void>& b) {
        return !a.owner_before(b) && !b.owner_before(a);
    }
    Map models_;
    std::weak_ptr<const void> paints_, pack_;
};

} // namespace gt3r::race
