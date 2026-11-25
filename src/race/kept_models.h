#pragma once

// The opponents' models of a race's last build, for its next one (RaceScreen::load_opponent_cars): a start builds its
// field twice (start() and the restart() that ends it) and a retry once more, mostly with the same models. Kept by
// (model, paint) while the custom paints and the texture pack in use are those they were built with; the race screen
// lets them go at the next start.

#include <map>
#include <memory>
#include <string>
#include <utility>

namespace gt3r::race {

template <class Model>
class KeptModels {
public:
    // The model (a mod car's: its base car) and the paint, as load_opponent_cars tells its models apart.
    using Key = std::pair<std::string, int>;
    using Map = std::map<Key, std::shared_ptr<Model>>;

    // The kept models, handed over for a build with `paints` and `pack` (null: none): the last build's when it had the
    // same ones, else none (and those are let go here, before the build makes new ones).
    Map take(const std::shared_ptr<const void>& paints, const std::shared_ptr<const void>& pack) {
        Map models = std::exchange(models_, {});
        return same(paints_, paints) && same(pack_, pack) ? std::move(models) : Map{};
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
