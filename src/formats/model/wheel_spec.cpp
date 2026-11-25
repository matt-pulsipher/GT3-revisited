#include "formats/model/wheel_spec.h"

#include "base/file.h"
#include "base/log.h"
#include "game/data/car_database.h"

#include <map>
#include <memory>

namespace gt3r {

namespace {

u64 part_id(const data::Car& car, data::TableId table) {
    for (size_t i = 0; i < data::kCarPartTables.size(); ++i)
        if (data::kCarPartTables[i] == int(table)) return car.parts[i];
    return 0;
}

} // namespace

namespace {
std::optional<WheelSpec> wheel_spec_of(const data::CarDatabase& db, const data::Car* car);
}

std::optional<WheelSpec> wheel_spec_from_database(const data::CarDatabase& db, const std::string& model) {
    return wheel_spec_of(db, db.car_by_model(model));
}

std::optional<WheelSpec> wheel_spec_for_car(const data::CarDatabase& db, const std::string& car_label) {
    return wheel_spec_of(db, db.car(car_label));
}

CarBodyMod body_mod_for_car(const data::CarDatabase& db, const std::string& car_label, int paint,
                            data::CarScene scene) {
    CarBodyMod out;
    const data::ModCar* m = db.mod_car(car_label);
    if (!m) return out;
    out.paint_key = "mod:" + m->label; // (race::paint_key)
    const auto& f = m->fit;
    BodyFit fit{f.donor_length, f.donor_wheelbase, f.donor_width, f.donor_height, f.length, f.wheelbase, f.width, f.height};
    if (const std::string& file = m->model_file(scene); !file.empty()) {
        // Cached per file: the race, garage and showroom ask for it repeatedly.
        static std::map<std::string, std::shared_ptr<const Bytes>> cache;
        auto& slot = cache[file];
        if (!slot)
            if (auto b = read_host_file(file)) slot = std::make_shared<const Bytes>(std::move(*b));
        out.glb = slot;
        if (!out.glb) LOG_WARN("Mod car %s: cannot read %s; using the donor's body", car_label.c_str(), file.c_str());
        else if (f.active()) out.fit = BodyFit{fit.from_length, fit.from_wheelbase, fit.from_width, fit.from_height,
                                               fit.to_length, fit.to_wheelbase, fit.to_width, fit.to_height}; // shadows
        // A custom colour or livery (paint indices past its own, race/custom_paints.h) over one of its paints.
        const std::optional<ResolvedPaint> custom = resolve_paint(out.paint_key, paint);
        const int own = std::max(custom ? custom->paint : paint, 0);
        if (!m->paints.empty()) out.paint = m->paints[size_t(own) % m->paints.size()].second;
        if (custom && custom->livery) out.livery = custom->livery, out.livery_paint = out.paint;
        if (custom && custom->computed) {
            out.paint.emplace();
            for (int k = 0; k < 3; ++k) (*out.paint)[size_t(k)] = float(custom->computed->rgb >> (8 * k) & 0xFF) / 255.0f;
        }
        // Stock front ride height (suspension +0x54, mm): the race lifts the body frame by it.
        if (const data::Car* c = db.car(car_label))
            if (const ByteSpan* su = db.record(data::TableId::Suspension, part_id(*c, data::TableId::Suspension));
                su && su->size() >= 0x56)
                out.glb_ground = rd16(*su, 0x54) * 0.001f;
        out.glb_ground += m->model_lower;
    }
    if (!out.glb && f.active()) out.fit = fit;
    out.wheel = m->wheel;
    return out;
}

namespace {
std::optional<WheelSpec> wheel_spec_of(const data::CarDatabase& db, const data::Car* car) {
    if (!car) return std::nullopt;
    const ByteSpan* ch = db.record(data::TableId::Chassis, part_id(*car, data::TableId::Chassis));
    const ByteSpan* rm = db.record(data::TableId::RacingModify, part_id(*car, data::TableId::RacingModify));
    if (!ch || !rm || ch->size() < 0x1A || rm->size() < 0x36) return std::nullopt;
    WheelSpec w;
    w.wheelbase = rd16(*ch, 0x18) * 0.001f;
    // The hubs sit the tread's per-side offset (s8 mm at +0x3D / +0x3E) inboard of it.
    for (int a = 0; a < 2; ++a) {
        int offset = rm->size() > size_t(0x3D + a) ? int(static_cast<signed char>((*rm)[size_t(0x3D + a)])) : 0;
        w.track[a] = float(int(rd16(*rm, size_t(0x32 + 2 * a))) - 2 * offset) * 0.001f;
    }
    const data::TableId tire_tables[2] = {data::TableId::FrontTire, data::TableId::RearTire};
    for (int a = 0; a < 2; ++a) {
        const ByteSpan* t = db.record(tire_tables[a], part_id(*car, tire_tables[a]));
        if (!t || t->size() < 0x20) return std::nullopt;
        const ByteSpan* sz = db.record(data::TableId::TireSize, rd64(*t, 0x18));
        if (!sz || sz->size() < 0x0B) return std::nullopt;
        float rim_in = (*sz)[0x08], aspect = (*sz)[0x09] * 5.0f, width_mm = (*sz)[0x0A] * 5.0f;
        w.width[a] = width_mm * 0.001f;
        w.sidewall[a] = w.width[a] * aspect * 0.01f;
        w.radius[a] = rim_in * 0.0254f * 0.5f + w.sidewall[a];
    }
    if (w.wheelbase < 1.0f || w.track[0] < 0.8f || w.radius[0] < 0.15f) return std::nullopt;
    return w;
}
} // namespace

} // namespace gt3r
