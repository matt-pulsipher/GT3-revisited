// The bonnet mount over every car body on a real disc (GT3R_DISC): the mounts worked out from
// each LOD-0 mesh are on the bodywork, clear of it, and frame its edge — the cross-car check a
// driver's-eye offset cannot give. Needs a disc; skipped without one.

#include "test_framework.h"

#include "disc/volume.h"
#include "formats/model/car_model.h"
#include "formats/model/wheel_spec.h"
#include "game/data/car_database.h"
#include "race/camera_rig.h"
#include "race/custom_cameras.h"
#include "race/hood_camera.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::race;

namespace {

bool finite(const render::Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

struct ClipPlane {
    render::Vec3 normal;
    float offset = 0;
};

std::vector<render::Vec3> clip_polygon(const std::vector<render::Vec3>& input, const ClipPlane& plane) {
    std::vector<render::Vec3> out;
    if (input.empty()) return out;
    render::Vec3 a = input.back();
    float da = render::dot(a, plane.normal) + plane.offset;
    for (const render::Vec3& b : input) {
        float db = render::dot(b, plane.normal) + plane.offset;
        if ((da >= 0) != (db >= 0)) out.push_back(a + (b - a) * (da / (da - db)));
        if (db >= 0) out.push_back(b);
        a = b, da = db;
    }
    return out;
}

bool lower_view_has_body(const scene::Model& body, const CameraRig& cam, float body_lift, float aspect) {
    float ty = std::tan(cam.fovy() * 0.5f), tx = ty * aspect * CameraRig::kPixelAspect;
    std::array<ClipPlane, 6> planes{{
        {{0, 0, -1}, -cam.near_plane},
        {{0, 0, 1}, cam.far_plane},
        {{1, 0, -tx}, 0},
        {{-1, 0, -tx}, 0},
        {{0, 1, -ty}, 0},
        {{0, -1, 0}, 0},
    }};
    for (const scene::Mesh& mesh : body.meshes) {
        if (mesh.lod != 0 || mesh.name.starts_with("wheel_") || mesh.name.starts_with("node")) continue;
        render::Mat4 at = cam.view() * render::translate({0, body_lift, 0}) * render::Mat4::from(mesh.transform);
        for (const scene::Primitive& primitive : mesh.primitives) {
            if (primitive.flags & (scene::kAdditive | scene::kEnvironmentMap | scene::kDepthOnly)) continue;
            size_t end = std::min(mesh.indices.size(), size_t(primitive.first_index) + primitive.index_count);
            for (size_t i = primitive.first_index; i + 2 < end; i += 3) {
                std::vector<render::Vec3> polygon;
                for (size_t j = 0; j < 3; ++j) {
                    u32 index = mesh.indices[i + j];
                    if (index >= mesh.vertices.size()) break;
                    const auto& p = mesh.vertices[index].pos;
                    polygon.push_back(at.transform_point({p[0], p[1], p[2]}));
                }
                if (polygon.size() != 3 ||
                    !std::all_of(polygon.begin(), polygon.end(), [](const render::Vec3& v) { return finite(v); })) continue;
                for (const ClipPlane& plane : planes) polygon = clip_polygon(polygon, plane);
                if (polygon.size() < 3) continue;
                float area = 0;
                for (size_t j = 0; j < polygon.size(); ++j) {
                    const render::Vec3& a = polygon[j];
                    const render::Vec3& b = polygon[(j + 1) % polygon.size()];
                    area += (a.x * b.y - b.x * a.y) / (a.z * b.z * tx * ty);
                }
                if (std::fabs(area) > 1e-7f) return true;
            }
        }
    }
    return false;
}

// A Mount::Hood custom camera as the editor's "Bonnet" preset makes it.
CustomCamera bonnet() {
    CustomCamera c;
    c.id = 1, c.name = "Bonnet";
    c.mount = CustomCamera::Mount::Hood;
    c.offset = {};
    c.car = CustomCamera::OwnCar::Shown;
    c.incar_hud = c.mirror = true;
    c.fov = 55;
    return c;
}

}

TEST(hood_camera_disc_framing_predicate) {
    CameraRig camera;
    camera.set_shake(false);
    camera.set_car_points({0, 0, 0}, {0, -1.5f, -6});
    camera.update(sim::VehicleState{}, sim::VehicleSetup{}, PadState{}, 1.0 / 60.0);
    scene::Model body;
    scene::Mesh mesh;
    mesh.name = "body";
    mesh.vertices.resize(3);
    const std::array<render::Vec3, 3> points{{{-0.3f, -0.3f, -3}, {0.3f, -0.3f, -3}, {0, -0.6f, -3}}};
    for (size_t i = 0; i < points.size(); ++i) {
        mesh.vertices[i].pos[0] = points[i].x;
        mesh.vertices[i].pos[1] = points[i].y;
        mesh.vertices[i].pos[2] = points[i].z;
    }
    mesh.indices = {0, 1, 2};
    mesh.primitives.push_back({0, 3});
    body.meshes.push_back(mesh);
    CHECK(lower_view_has_body(body, camera, 0.13f, 16.0f / 9.0f));
    for (const render::Vec3& offset : {render::Vec3{10, 0, 0}, render::Vec3{0, 2, 0}, render::Vec3{0, 0, 6}}) {
        body.meshes[0].transform = render::translate(offset).m;
        CHECK(!lower_view_has_body(body, camera, 0.13f, 16.0f / 9.0f));
    }
}

TEST(hood_camera_all_models_disc) {
    Disc* disc = test::any_region_disc();
    if (!disc) {
        if (const char* configured = std::getenv("GT3R_DISC"); configured && *configured) {
            CHECK(false);
            return;
        }
        SKIP("needs GT3R_DISC");
    }
    data::CarDatabase db;
    bool loaded = db.load(disc->volume());
    CHECK(loaded);
    if (!loaded) return;
    constexpr std::array<const char*, 3> folders{"cars/day/", "cars/eve/", "cars/night/"};
    std::array<size_t, 3> discovered{}, examined{}, passed{}, without_spec{}, empty_placeholders{};
    std::set<std::string> unique_models;
    std::vector<std::pair<const Volume::Entry*, size_t>> entries;
    for (const Volume::Entry& entry : disc->volume().entries())
        for (size_t folder = 0; folder < folders.size(); ++folder) {
            std::string prefix = folders[folder];
            if (!entry.path.starts_with(prefix)) continue;
            std::string name = entry.path.substr(prefix.size());
            if (name.empty() || name[0] == '.' || name.find('/') != std::string::npos) continue;
            if (entry.size == 0) {
                ++empty_placeholders[folder];
                CHECK(db.car_by_model(name) == nullptr);
                std::printf("  empty unreferenced car placeholder: %s\n", entry.path.c_str());
                continue;
            }
            ++discovered[folder];
            unique_models.insert(name);
            entries.emplace_back(&entry, folder);
        }
    CHECK(!entries.empty());
    for (const auto& [entry, folder] : entries) {
        ++examined[folder];
        int before = test::failures();
        try {
            Bytes bytes = disc->volume().read(*entry);
            CHECK(CarFile::is_car(bytes));
            if (!CarFile::is_car(bytes)) throw FormatError("not a car file");
            std::string model = entry->path.substr(std::string(folders[folder]).size());
            std::optional<WheelSpec> wheels = wheel_spec_from_database(db, model);
            if (!wheels) {
                ++without_spec[folder];
                CHECK(db.car_by_model(model) == nullptr);
            }
            CarModelOptions options;
            options.wheels = false;
            options.effects = false;
            options.lod = 0;
            options.wheel_spec = wheels;
            std::vector<gtm1::MorphDraws> morphs;
            options.morphs = &morphs;
            scene::Model body = load_car_model(bytes, options, model);
            HoodCameraProfile profile = make_hood_camera_profile(body, wheels ? wheels->wheelbase : 0.0f);
            CHECK(profile.valid && finite(profile.bounds_min) && finite(profile.bounds_max));
            if (!profile.valid) throw FormatError("no valid hood profile");
            float length = profile.bounds_max.z - profile.bounds_min.z;
            CHECK(length > 0);
            sim::VehicleSetup setup;
            setup.length = length;
            setup.width = profile.bounds_max.x - profile.bounds_min.x;
            setup.height = profile.bounds_max.y - profile.bounds_min.y;
            setup.wheelbase = wheels ? wheels->wheelbase : length * 0.6f;
            for (auto& wheel : setup.wheels) wheel.ride_height = 0.13f;
            for (size_t i = 0; i < HoodCameraProfile::kMountCount; ++i) {
                const render::Vec3& eye = profile.eyes[i];
                CHECK(finite(eye) && std::isfinite(profile.surface_y[i]));
                CHECK(std::fabs(eye.x) < 1e-5f);
                CHECK(eye.z >= profile.bounds_min.z + length * 0.25f - 1e-4f);
                CHECK(eye.z <= profile.bounds_min.z + length * 0.55f + 1e-4f);
                CHECK(eye.y >= profile.surface_y[i] + 0.40f - 1e-4f);
                CHECK(std::isfinite(profile.pitch_down[i]) && profile.pitch_down[i] >= 0);
                CHECK(profile.pitch_down[i] <= 22.0f * (3.14159265f / 180.0f) + 1e-5f);
                if (i) CHECK(eye.z <= profile.eyes[i - 1].z + 1e-5f);
                for (float height : {-0.10f, 0.0f, 0.35f}) {
                    CustomCamera cam = bonnet();
                    cam.offset.z = -0.40f + float(i) * 0.05f;
                    cam.offset.y = height;
                    render::Vec3 adjusted = hood_camera_eye(profile, setup, cam);
                    CHECK(finite(adjusted));
                    CHECK(std::fabs(adjusted.z - eye.z) < 1e-4f);
                    CHECK(adjusted.y >= profile.surface_y[i] + 0.30f - 1e-4f);
                    CHECK(std::fabs(hood_camera_pitch(profile, cam) - profile.pitch_down[i]) < 1e-5f);
                }
            }
            CameraRig camera;
            camera.set_custom_cameras({bonnet()});
            camera.set_mode(CameraMode::Custom);
            camera.set_hood_profile(profile);
            camera.update(sim::VehicleState{}, setup, PadState{}, 1.0 / 60.0);
            CHECK(finite(camera.eye()) && !camera.hides_car()); // the bodywork shows ahead of it
            const render::Mat4 view = camera.view();
            CHECK(std::all_of(view.m.begin(), view.m.end(), [](float v) { return std::isfinite(v); }));
            bool visible = lower_view_has_body(body, camera, 0.13f, 16.0f / 9.0f);
            CHECK(visible);
            if (!visible)
                std::printf("  hood framing: %s eye=(%.3f %.3f %.3f) body_y=%.3f..%.3f body_z=%.3f..%.3f\n",
                            entry->path.c_str(), camera.eye().x, camera.eye().y, camera.eye().z,
                            profile.bounds_min.y, profile.bounds_max.y, profile.bounds_min.z, profile.bounds_max.z);
        } catch (const std::exception& e) {
            std::printf("  hood model exception: %s: %s\n", entry->path.c_str(), e.what());
            CHECK(false);
        }
        if (test::failures() == before) ++passed[folder];
        else std::printf("  hood model failed: %s\n", entry->path.c_str());
    }
    for (size_t folder = 0; folder < folders.size(); ++folder) {
        std::printf("  hood coverage %s: discovered=%zu examined=%zu passed=%zu unreferenced_without_spec=%zu "
                    "empty_placeholders=%zu\n", folders[folder], discovered[folder], examined[folder], passed[folder],
                    without_spec[folder], empty_placeholders[folder]);
        CHECK(discovered[folder] > 0);
        CHECK(examined[folder] == discovered[folder] && passed[folder] == discovered[folder]);
    }
    std::printf("  hood unique model IDs covered: %zu\n", unique_models.size());
}
