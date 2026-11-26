// Building the opponents' models on several threads (RaceScreen::load_opponent_cars): the loop (parallel_for), the
// uploads left for the GPU's thread (render::DeferredUploads), and the car models themselves, which come out the same
// built at once as one after another (GT3R_DISC).

#include "test_framework.h"

#include "base/parallel_for.h"
#include "disc/disc.h"
#include "formats/model/car_model.h"
#include "formats/model/wheel_spec.h"
#include "game/data/car_database.h"
#include "render/deferred_uploads.h"
#include "vfs/vfs.h"

#include <atomic>
#include <cstddef>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace gt3r;

TEST(parallel_for_every_index_once) {
    for (unsigned threads : {0u, 1u, 2u, 4u, 64u}) {
        for (size_t n : {size_t(0), size_t(1), size_t(3), size_t(100)}) {
            std::vector<std::atomic<int>> calls(n);
            parallel_for(n, threads, [&](size_t k) { ++calls[k]; });
            for (size_t k = 0; k < n; ++k) CHECK(calls[k] == 1);
        }
    }
}

TEST(parallel_for_one_thread_stays_on_the_caller) {
    const std::thread::id caller = std::this_thread::get_id();
    std::vector<size_t> order;
    parallel_for(5, 1, [&](size_t k) {
        CHECK(std::this_thread::get_id() == caller);
        order.push_back(k);
    });
    CHECK(order == (std::vector<size_t>{0, 1, 2, 3, 4}));
}

// Four jobs each wait for the other three to have started: they only finish if they run together.
TEST(parallel_for_runs_together) {
    std::atomic<int> started{0}, together{0};
    parallel_for(4, 4, [&](size_t) {
        ++started;
        for (int i = 0; i < 2000 && started < 4; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (started == 4) ++together;
    });
    CHECK(together == 4);
}

TEST(deferred_uploads_run_in_order_once) {
    render::DeferredUploads uploads;
    std::vector<int> ran;
    uploads.add([&] { ran.push_back(1); });
    uploads.add([&] { ran.push_back(2); });
    CHECK(ran.empty());
    uploads.run();
    CHECK(ran == (std::vector<int>{1, 2}));
    uploads.run();
    CHECK(ran.size() == 2);
}

// Each thread sees only the one installed on it, and the scope puts back what was there before.
TEST(deferred_uploads_are_per_thread) {
    CHECK(render::DeferredUploads::current() == nullptr);
    render::DeferredUploads outer, inner;
    {
        render::DeferredUploads::Scope a(outer);
        CHECK(render::DeferredUploads::current() == &outer);
        {
            render::DeferredUploads::Scope b(inner);
            CHECK(render::DeferredUploads::current() == &inner);
        }
        CHECK(render::DeferredUploads::current() == &outer);
        std::thread([] { CHECK(render::DeferredUploads::current() == nullptr); }).join();
    }
    CHECK(render::DeferredUploads::current() == nullptr);
}

namespace {

bool same_model(const scene::Model& a, const scene::Model& b) {
    if (a.textures.size() != b.textures.size() || a.meshes.size() != b.meshes.size()) return false;
    for (size_t i = 0; i < a.textures.size(); ++i) {
        const scene::Texture &x = a.textures[i], &y = b.textures[i];
        if (x.width != y.width || x.height != y.height || x.rgba != y.rgba) return false;
    }
    for (size_t i = 0; i < a.meshes.size(); ++i) {
        const scene::Mesh &x = a.meshes[i], &y = b.meshes[i];
        if (x.name != y.name || x.lod != y.lod || x.indices != y.indices || x.vertices.size() != y.vertices.size() ||
            x.primitives.size() != y.primitives.size())
            return false;
        if (!x.vertices.empty() &&
            std::memcmp(x.vertices.data(), y.vertices.data(), x.vertices.size() * sizeof(scene::Vertex)) != 0)
            return false;
        for (size_t k = 0; k < x.primitives.size(); ++k) {
            const scene::Primitive &p = x.primitives[k], &q = y.primitives[k];
            if (p.first_index != q.first_index || p.index_count != q.index_count || p.texture != q.texture ||
                p.flags != q.flags || p.alpha_ref != q.alpha_ref || p.under_ref != q.under_ref ||
                p.under_alpha != q.under_alpha || p.env_rows != q.env_rows || p.env_kind != q.env_kind ||
                std::memcmp(&p.material, &q.material, offsetof(scene::Material, valid)) != 0 ||
                p.material.valid != q.material.valid || p.material.specular_local != q.material.specular_local)
                return false;
        }
    }
    return true;
}

} // namespace

// As RaceScreen::load_opponent_cars builds them: the files read on the builders, one at a time, and the models built
// at once.
TEST(car_models_built_at_once_are_the_same_disc) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    CHECK(db.load(vfs));
    struct Car {
        std::string model;
        int paint = 0;
        scene::Model alone, at_once;
    };
    std::vector<Car> cars;
    size_t i = 0;
    for (const data::Car& c : db.cars()) {
        if (i++ % 4 != 0) continue; // (a spread of the disc's cars)
        const std::string model = db.model_of(c);
        std::optional<Bytes> bytes = vfs.read("cars/day/" + model);
        if (bytes && !bytes->empty() && CarFile::is_car(*bytes)) cars.push_back({model, int(i % 3), {}, {}});
    }
    CHECK(cars.size() > 20);
    std::mutex reading;
    auto load = [&](const Car& c) {
        std::unique_lock lock(reading);
        const std::optional<Bytes> file = vfs.read("cars/day/" + c.model);
        lock.unlock();
        CarModelOptions opt;
        opt.lod = -1, opt.wheels = true, opt.effects = true;
        opt.wheel_spec = wheel_spec_from_database(db, c.model);
        opt.paint = c.paint;
        return load_car_model(*file, opt, c.model);
    };
    for (Car& c : cars) c.alone = load(c);
    parallel_for(cars.size(), 5, [&](size_t k) { cars[k].at_once = load(cars[k]); });
    for (const Car& c : cars) {
        const bool same = same_model(c.alone, c.at_once);
        if (!same) std::fprintf(stderr, "  %s: not the same\n", c.model.c_str());
        CHECK(same);
    }
    std::fprintf(stderr, "  %zu cars\n", cars.size());
}
