// The opponents' models kept from a race's last build for its next one (race::KeptModels, src/race/kept_models.h):
// handed back only for the same custom paints and texture pack, only the last build's, and let go when not.

#include "test_framework.h"

#include "race/kept_models.h"

#include <memory>
#include <set>
#include <string>

using namespace gt3r;

namespace {

struct Model {
    std::string name;
};
using Kept = race::KeptModels<Model>;

Kept::Map models(std::initializer_list<std::pair<Kept::Key, std::string>> l) {
    Kept::Map m;
    for (const auto& [key, name] : l) m[key] = std::make_shared<Model>(Model{name});
    return m;
}

// Every key a field could ask for in these tests.
const std::set<Kept::Key> kAll{{"ty0005", 3}, {"ho0040", 0}, {"bm0006", 5}, {"ma0031", 1}};

std::shared_ptr<int> set_of(int v) { return std::make_shared<int>(v); }

// A set made in one place every time: a set let go and the next one made have the same address, other owners.
std::shared_ptr<int> set_in_place(int v) {
    static int place;
    place = v;
    return std::shared_ptr<int>(&place, [](int*) {});
}

} // namespace

// The last build's models for a build with the same paints and pack (no pack: the same too), the same objects, handed
// over: the build keeps them again.
TEST(kept_models_same_set_up) {
    auto paints = set_of(1), pack = set_of(2);
    Kept kept;
    CHECK(kept.take(paints, pack, kAll).empty()); // nothing built yet
    const Kept::Map built = models({{{"ty0005", 3}, "a"}, {{"ho0040", 0}, "b"}});
    kept.keep(built, paints, pack);
    Kept::Map again = kept.take(paints, pack, kAll);
    CHECK(again.size() == 2);
    CHECK(again.at({"ty0005", 3}) == built.at({"ty0005", 3}) && again.at({"ho0040", 0}) == built.at({"ho0040", 0}));
    CHECK(kept.take(paints, pack, kAll).empty()); // handed over: none until the build keeps them
    kept.keep(std::move(again), paints, pack);
    CHECK(kept.take(paints, pack, kAll).size() == 2);

    Kept no_pack;
    no_pack.keep(built, paints, nullptr);
    CHECK(no_pack.take(paints, nullptr, kAll).size() == 2);
    no_pack.keep(built, paints, nullptr);
    CHECK(no_pack.take(paints, pack, kAll).empty()); // a pack put in since
    Kept nothing;
    nothing.keep(built, nullptr, nullptr);
    CHECK(nothing.take(nullptr, nullptr, kAll).size() == 2);
    nothing.keep(built, nullptr, nullptr);
    CHECK(nothing.take(paints, nullptr, kAll).empty());
}

// Other paints or another pack: none, and the kept models are let go before the build makes new ones.
TEST(kept_models_other_set_up) {
    auto paints = set_of(1), pack = set_of(2);
    Kept kept;
    const auto check_none = [&](const std::shared_ptr<int>& p, const std::shared_ptr<int>& k) {
        Kept::Map m = models({{{"ty0005", 3}, "a"}});
        std::weak_ptr<Model> old = m.begin()->second;
        kept.keep(std::move(m), paints, pack);
        CHECK(kept.take(p, k, kAll).empty());
        CHECK(old.expired()); // (nothing else held it)
    };
    check_none(set_of(1), pack);
    check_none(paints, set_of(2));
    check_none(paints, nullptr);
    check_none(nullptr, pack);
    check_none(nullptr, nullptr);
}

// A set let go and a new one made at its address (a colour received online swaps the paints for a new set): never
// taken for the old one.
TEST(kept_models_set_replaced) {
    Kept kept;
    std::shared_ptr<int> paints = set_in_place(1);
    const int* const address = paints.get();
    kept.keep(models({{{"ty0005", 3}, "a"}}), paints, nullptr);
    paints.reset(); // the old set let go ...
    paints = set_in_place(1); // ... and a new one at the same address
    CHECK(paints.get() == address);
    CHECK(kept.take(paints, nullptr, kAll).empty());
    kept.keep(models({{{"ty0005", 3}, "a"}}), paints, nullptr);
    CHECK(kept.take(paints, nullptr, kAll).size() == 1); // (the new set itself: kept for it)
    // Kept again with another set: the earlier set's models are not given for it.
    auto a = set_of(1), b = set_of(2);
    kept.keep(models({{{"ty0005", 3}, "a"}}), a, nullptr);
    kept.keep(models({{{"ty0005", 3}, "b"}}), b, nullptr);
    CHECK(kept.take(a, nullptr, kAll).empty());
    kept.keep(models({{{"ty0005", 3}, "b"}}), b, nullptr);
    const Kept::Map with_b = kept.take(b, nullptr, kAll);
    CHECK(with_b.size() == 1 && with_b.at({"ty0005", 3})->name == "b");
}

// take() hands over only the models the next field asks for; the others are let go before it builds its own.
TEST(kept_models_unwanted_let_go) {
    auto paints = set_of(1);
    Kept kept;
    Kept::Map built = models({{{"ty0005", 3}, "a"}, {{"ho0040", 0}, "b"}});
    std::weak_ptr<Model> wanted = built.at({"ty0005", 3}), unwanted = built.at({"ho0040", 0});
    kept.keep(std::move(built), paints, nullptr);
    Kept::Map got = kept.take(paints, nullptr, {{"ty0005", 3}, {"bm0006", 5}});
    CHECK(got.size() == 1 && got.at({"ty0005", 3}) == wanted.lock());
    CHECK(unwanted.expired()); // let go at take(), not when the build keeps its own
    CHECK(kept.take(paints, nullptr, kAll).empty());
}

// keep() holds the given build's models only: a model the next field no longer has is let go; one that could not be
// built is kept empty (built again next time); clear() lets all go.
TEST(kept_models_last_build_only) {
    auto paints = set_of(1);
    Kept kept;
    Kept::Map first = models({{{"ty0005", 3}, "a"}, {{"ho0040", 0}, "b"}});
    std::weak_ptr<Model> dropped = first.at({"ho0040", 0});
    kept.keep(std::move(first), paints, nullptr);
    CHECK(!dropped.expired()); // held for the next build

    Kept::Map second = kept.take(paints, nullptr, kAll);
    second.erase({"ho0040", 0});
    second[{"bm0006", 5}] = std::make_shared<Model>(Model{"c"});
    second[{"ma0031", 1}] = nullptr; // a model that could not be built
    kept.keep(std::move(second), paints, nullptr);
    CHECK(dropped.expired());
    Kept::Map now = kept.take(paints, nullptr, kAll);
    CHECK(now.size() == 3 && now.count({"ty0005", 3}) && now.count({"bm0006", 5}));
    CHECK(now.at({"ma0031", 1}) == nullptr);

    std::weak_ptr<Model> held = now.at({"ty0005", 3});
    kept.keep(std::move(now), paints, nullptr);
    CHECK(!held.expired());
    kept.clear();
    CHECK(held.expired()); // clear() held the last of them
    CHECK(kept.take(paints, nullptr, kAll).empty());
}
