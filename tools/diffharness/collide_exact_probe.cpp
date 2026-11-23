// Collision manager records <-> sim::collide::exact::Body; see collide_exact_probe.h.

#include "collide_exact_probe.h"
#include "program_map.h"

#include <cstdio>

namespace gt3r::diff::collide_exact {

namespace {

constexpr Pa kInit = 0x001FB140, kEnable = 0x001FB1F0, kGhost = 0x001EAB70, kSeparate = 0x001FCC98, kPersist = 0x001FBCE8,
              kHold = 0x001FCF48, kEndPose = 0x001FB268, kResetHit = 0x001FB308, kWallSweep = 0x001FD720, kPairSweep = 0x001FBF50,
              kAdvance = 0x001FB328, kPointResponse = 0x001FDEC0, kCornerResponse = 0x001FE3A0, kPairResponse = 0x001FC5C0,
              kStuck = 0x001FB740, kPushOut = 0x001FDE08, kCommit = 0x001FB6D8, kWallsStep = 0x001FACB8, kEffects = 0x001FB890;

std::string hex(u32 v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

struct Cmp {
    std::string out;
    void f(const std::string& name, ex::F a, ex::F b) {
        if (out.empty() && a.bits != b.bits) out = name + " " + hex(a.bits) + " vs " + hex(b.bits) + " (" + std::to_string(a.host()) + " vs " + std::to_string(b.host()) + ")";
    }
    void i(const std::string& name, long long a, long long b) {
        if (out.empty() && a != b) out = name + " " + std::to_string(a) + " vs " + std::to_string(b);
    }
};

} // namespace

u32 road_block(Ee& ee, u32 course) { return ee.r32(ee.r32(course) + 0x70); }
u32 wall_table(Ee& ee, u32 course) { return ee.r32(road_block(ee, course) + 0x3C); }

ex::Body read_body(Ee& ee, u32 b, u32 walls, const ex::Course* course) {
    ex::Body r;
    r.course = course;
    r.behaviour = disc_behaviour(); // the disc's release (program_map.h)
    auto f = [&](u32 o) { return ex::F::raw(ee.r32(b + o)); };
    auto s = [&](u32 o) { return int(s32(ee.r32(b + o))); };
    auto w = [&](u32 o) {
        u32 p = ee.r32(b + o);
        return p ? int((p - walls) / 16) : -1;
    };
    auto on = [&](u32 o) { return ee.r32(b + o) != 0; };
    r.im = f(0);
    r.ii = f(4);
    for (u32 k = 0; k < 8; ++k) {
        u32 P = 8 + 0x44 * k;
        ex::Point& q = r.p[k];
        q.lx = f(P), q.ly = f(P + 4), q.ux = f(P + 8), q.uy = f(P + 0xC), q.sx = f(P + 0x10), q.sy = f(P + 0x14), q.ex = f(P + 0x18),
        q.ey = f(P + 0x1C);
        q.flags = ee.r32(b + P + 0x20);
        q.hit = on(P + 0x24);
        q.t = f(P + 0x28);
        q.wall = w(P + 0x2C);
        q.tx = f(P + 0x30), q.ty = f(P + 0x34);
        q.other = s(P + 0x3C), q.edge = s(P + 0x40);
        u32 E = 0x228 + 0x2C * k;
        ex::Edge& e = r.e[k];
        e.sx = f(E), e.sy = f(E + 4), e.ex = f(E + 8), e.ey = f(E + 0xC), e.nx = f(E + 0x10), e.ny = f(E + 0x14), e.d = f(E + 0x18);
        e.hit = on(E + 0x20);
        e.t = f(E + 0x24);
        e.wall = w(E + 0x28);
    }
    r.radius = f(0x388);
    r.ts = f(0x38C), r.sx = f(0x390), r.sy = f(0x394), r.sz = f(0x398), r.sh = f(0x39C);
    r.te = f(0x3A0), r.ex = f(0x3A4), r.ey = f(0x3A8), r.ez = f(0x3AC), r.eh = f(0x3B0);
    r.dx = f(0x3B4), r.dy = f(0x3B8), r.dz = f(0x3BC), r.dh = f(0x3C0);
    r.vx = f(0x3C4), r.vy = f(0x3C8), r.vz = f(0x3CC), r.w = f(0x3D0);
    r.bx0 = f(0x3D4), r.by0 = f(0x3D8), r.bx1 = f(0x3DC), r.by1 = f(0x3E0), r.bw = f(0x3E4), r.bh = f(0x3E8);
    r.have_points = on(0x3EC), r.pair = on(0x3F0), r.wpoint = on(0x3F4), r.wcorner = on(0x3F8);
    r.t = f(0x3FC);
    r.sensor = on(0x400);
    r.sensor_t = f(0x404);
    r.hit_edge = s(0x408), r.other = s(0x40C), r.vertex = s(0x410);
    r.cpx = f(0x414), r.cpy = f(0x418);
    r.wk = s(0x41C);
    r.ux = f(0x420), r.uy = f(0x424);
    r.wseg = w(0x428);
    r.we = s(0x42C);
    r.enabled = on(0x430), r.front = on(0x434), r.rear = on(0x438), r.wall_hit = on(0x43C), r.car_hit = on(0x440),
    r.sensor_hit = on(0x444);
    r.strength = f(0x448);
    r.car = s(0x44C);
    r.cx = f(0x454), r.cy = f(0x458), r.cz = f(0x45C), r.cvx = f(0x460), r.cvy = f(0x464), r.cvz = f(0x468), r.ch = f(0x46C),
    r.cw = f(0x470);
    r.hint = s(0x474);
    r.mass = f(0x488);
    for (u32 j = 0; j < u32(ex::kGt3Bodies); ++j) r.rsum[j] = f(0x49C + 4 * j), r.r2[j] = f(0x4BC + 4 * j);
    r.dt = f(0x4DC);
    r.rumble_steps = s(0x4E0), r.rumble_level = f(0x4E4), r.sound_steps = s(0x4E8), r.sound_level = f(0x4EC);
    u32 car = ee.r32(b + 0x450);
    if (car) {
        r.dt_in = ex::F::raw(ee.r32(car + 0xEC0));
        r.steer = s8(ee.r8(car + car_off(0x14A7)));
        r.ghost = ee.r8(car + car_off(0x136E)) != 0;
        r.control = s8(ee.r8(car + car_off(0x136D)));
    }
    return r;
}

void write_body(Ee& ee, u32 b, const ex::Body& r, u32 walls) {
    auto f = [&](u32 o, ex::F v) { ee.w32(b + o, v.bits); };
    auto s = [&](u32 o, long long v) { ee.w32(b + o, u32(s32(v))); };
    auto w = [&](u32 o, int i) { ee.w32(b + o, i < 0 ? 0 : walls + 16 * u32(i)); };
    f(0, r.im), f(4, r.ii);
    for (u32 k = 0; k < 8; ++k) {
        u32 P = 8 + 0x44 * k;
        const ex::Point& q = r.p[k];
        f(P, q.lx), f(P + 4, q.ly), f(P + 8, q.ux), f(P + 0xC, q.uy), f(P + 0x10, q.sx), f(P + 0x14, q.sy), f(P + 0x18, q.ex),
            f(P + 0x1C, q.ey);
        s(P + 0x20, q.flags), s(P + 0x24, q.hit), f(P + 0x28, q.t), w(P + 0x2C, q.wall), f(P + 0x30, q.tx), f(P + 0x34, q.ty);
        s(P + 0x3C, q.other), s(P + 0x40, q.edge);
        u32 E = 0x228 + 0x2C * k;
        const ex::Edge& e = r.e[k];
        f(E, e.sx), f(E + 4, e.sy), f(E + 8, e.ex), f(E + 0xC, e.ey), f(E + 0x10, e.nx), f(E + 0x14, e.ny), f(E + 0x18, e.d);
        s(E + 0x20, e.hit), f(E + 0x24, e.t), w(E + 0x28, e.wall);
    }
    f(0x388, r.radius);
    f(0x38C, r.ts), f(0x390, r.sx), f(0x394, r.sy), f(0x398, r.sz), f(0x39C, r.sh);
    f(0x3A0, r.te), f(0x3A4, r.ex), f(0x3A8, r.ey), f(0x3AC, r.ez), f(0x3B0, r.eh);
    f(0x3B4, r.dx), f(0x3B8, r.dy), f(0x3BC, r.dz), f(0x3C0, r.dh);
    f(0x3C4, r.vx), f(0x3C8, r.vy), f(0x3CC, r.vz), f(0x3D0, r.w);
    f(0x3D4, r.bx0), f(0x3D8, r.by0), f(0x3DC, r.bx1), f(0x3E0, r.by1), f(0x3E4, r.bw), f(0x3E8, r.bh);
    s(0x3EC, r.have_points), s(0x3F0, r.pair), s(0x3F4, r.wpoint), s(0x3F8, r.wcorner);
    f(0x3FC, r.t), s(0x400, r.sensor), f(0x404, r.sensor_t);
    s(0x408, r.hit_edge), s(0x40C, r.other), s(0x410, r.vertex), f(0x414, r.cpx), f(0x418, r.cpy), s(0x41C, r.wk);
    f(0x420, r.ux), f(0x424, r.uy), w(0x428, r.wseg), s(0x42C, r.we);
    s(0x430, r.enabled), s(0x434, r.front), s(0x438, r.rear), s(0x43C, r.wall_hit), s(0x440, r.car_hit), s(0x444, r.sensor_hit);
    f(0x448, r.strength), s(0x44C, r.car);
    f(0x454, r.cx), f(0x458, r.cy), f(0x45C, r.cz), f(0x460, r.cvx), f(0x464, r.cvy), f(0x468, r.cvz), f(0x46C, r.ch), f(0x470, r.cw);
    s(0x474, r.hint), f(0x488, r.mass);
    for (u32 j = 0; j < u32(ex::kGt3Bodies); ++j) f(0x49C + 4 * j, r.rsum[j]), f(0x4BC + 4 * j, r.r2[j]);
    f(0x4DC, r.dt);
    s(0x4E0, r.rumble_steps), f(0x4E4, r.rumble_level), s(0x4E8, r.sound_steps), f(0x4EC, r.sound_level);
}

std::vector<ex::Body> read_bodies(Ee& ee, u32 cm, u32 walls, const ex::Course* course) {
    std::vector<ex::Body> v;
    int n = int(ee.r32(cm + 4));
    for (int i = 0; i < n; ++i) v.push_back(read_body(ee, body_at(cm, i), walls, course));
    return v;
}

std::string first_difference(const ex::Body& a, const ex::Body& b) {
    Cmp c;
    c.f("im", a.im, b.im), c.f("ii", a.ii, b.ii);
    for (size_t k = 0; k < 8; ++k) {
        const std::string p = "p[" + std::to_string(k) + "].";
        const ex::Point &x = a.p[k], &y = b.p[k];
        c.f(p + "lx", x.lx, y.lx), c.f(p + "ly", x.ly, y.ly), c.f(p + "ux", x.ux, y.ux), c.f(p + "uy", x.uy, y.uy);
        c.f(p + "sx", x.sx, y.sx), c.f(p + "sy", x.sy, y.sy), c.f(p + "ex", x.ex, y.ex), c.f(p + "ey", x.ey, y.ey);
        c.i(p + "flags", x.flags, y.flags), c.i(p + "hit", x.hit, y.hit), c.f(p + "t", x.t, y.t), c.i(p + "wall", x.wall, y.wall);
        c.f(p + "tx", x.tx, y.tx), c.f(p + "ty", x.ty, y.ty), c.i(p + "other", x.other, y.other), c.i(p + "edge", x.edge, y.edge);
    }
    for (size_t k = 0; k < 8; ++k) {
        const std::string p = "e[" + std::to_string(k) + "].";
        const ex::Edge &x = a.e[k], &y = b.e[k];
        c.f(p + "sx", x.sx, y.sx), c.f(p + "sy", x.sy, y.sy), c.f(p + "ex", x.ex, y.ex), c.f(p + "ey", x.ey, y.ey);
        c.f(p + "nx", x.nx, y.nx), c.f(p + "ny", x.ny, y.ny), c.f(p + "d", x.d, y.d), c.i(p + "hit", x.hit, y.hit);
        c.f(p + "t", x.t, y.t), c.i(p + "wall", x.wall, y.wall);
    }
    c.f("radius", a.radius, b.radius);
    c.f("ts", a.ts, b.ts), c.f("sx", a.sx, b.sx), c.f("sy", a.sy, b.sy), c.f("sz", a.sz, b.sz), c.f("sh", a.sh, b.sh);
    c.f("te", a.te, b.te), c.f("ex", a.ex, b.ex), c.f("ey", a.ey, b.ey), c.f("ez", a.ez, b.ez), c.f("eh", a.eh, b.eh);
    c.f("dx", a.dx, b.dx), c.f("dy", a.dy, b.dy), c.f("dz", a.dz, b.dz), c.f("dh", a.dh, b.dh);
    c.f("vx", a.vx, b.vx), c.f("vy", a.vy, b.vy), c.f("vz", a.vz, b.vz), c.f("w", a.w, b.w);
    c.f("bx0", a.bx0, b.bx0), c.f("by0", a.by0, b.by0), c.f("bx1", a.bx1, b.bx1), c.f("by1", a.by1, b.by1), c.f("bw", a.bw, b.bw),
        c.f("bh", a.bh, b.bh);
    c.i("have_points", a.have_points, b.have_points), c.i("pair", a.pair, b.pair), c.i("wpoint", a.wpoint, b.wpoint),
        c.i("wcorner", a.wcorner, b.wcorner);
    c.f("t", a.t, b.t), c.i("sensor", a.sensor, b.sensor), c.f("sensor_t", a.sensor_t, b.sensor_t);
    c.i("hit_edge", a.hit_edge, b.hit_edge), c.i("other", a.other, b.other), c.i("vertex", a.vertex, b.vertex);
    c.f("cpx", a.cpx, b.cpx), c.f("cpy", a.cpy, b.cpy), c.i("wk", a.wk, b.wk), c.f("ux", a.ux, b.ux), c.f("uy", a.uy, b.uy);
    c.i("wseg", a.wseg, b.wseg), c.i("we", a.we, b.we), c.i("enabled", a.enabled, b.enabled), c.i("front", a.front, b.front),
        c.i("rear", a.rear, b.rear), c.i("wall_hit", a.wall_hit, b.wall_hit), c.i("car_hit", a.car_hit, b.car_hit),
        c.i("sensor_hit", a.sensor_hit, b.sensor_hit);
    c.f("strength", a.strength, b.strength), c.i("car", a.car, b.car);
    c.f("cx", a.cx, b.cx), c.f("cy", a.cy, b.cy), c.f("cz", a.cz, b.cz), c.f("cvx", a.cvx, b.cvx), c.f("cvy", a.cvy, b.cvy),
        c.f("cvz", a.cvz, b.cvz), c.f("ch", a.ch, b.ch), c.f("cw", a.cw, b.cw);
    c.i("hint", a.hint, b.hint), c.f("mass", a.mass, b.mass);
    for (size_t j = 0; j < size_t(ex::kGt3Bodies); ++j)
        c.f("rsum[" + std::to_string(j) + "]", a.rsum[j], b.rsum[j]), c.f("r2[" + std::to_string(j) + "]", a.r2[j], b.r2[j]);
    c.f("dt", a.dt, b.dt);
    c.i("rumble_steps", a.rumble_steps, b.rumble_steps), c.f("rumble_level", a.rumble_level, b.rumble_level);
    c.i("sound_steps", a.sound_steps, b.sound_steps), c.f("sound_level", a.sound_level, b.sound_level);
    return c.out;
}

std::string first_difference(const std::vector<ex::Body>& a, const std::vector<ex::Body>& b) {
    if (a.size() != b.size()) return "body count " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        std::string d = first_difference(a[i], b[i]);
        if (!d.empty()) return "body " + std::to_string(i) + " " + d;
    }
    return {};
}

std::vector<u8> snapshot(Ee& ee, u32 cm) {
    u32 n = ee.r32(cm + 4);
    std::vector<u8> v(kBody0 + n * kBodySize);
    ee.read_bytes(cm, v.data(), u32(v.size()));
    return v;
}

void restore(Ee& ee, u32 cm, const std::vector<u8>& bytes) { ee.write_bytes(cm, ByteSpan(bytes.data(), bytes.size())); }

std::string drill_step(Ee& ee, u32 cm, const std::vector<u8>& pre, const ex::Course* course) {
    restore(ee, cm, pre);
    const int n = int(ee.r32(cm + 4));
    const u32 walls = wall_table(ee, ee.r32(body_at(cm, 0) + 0x478));
    std::vector<ex::Body> ours = read_bodies(ee, cm, walls, course);
    std::string fail;
    auto check = [&](const std::string& what) {
        if (!fail.empty()) return false;
        std::string d = first_difference(ours, read_bodies(ee, cm, walls));
        if (!d.empty()) fail = what + ": " + d;
        return fail.empty();
    };
    auto B = [&](int i) { return Arg::ptr(body_at(cm, i)); };
    for (int i = 0; i < n; ++i) {
        u32 car = ee.r32(body_at(cm, i) + 0x450);
        ee.call(kInit, {B(i), Arg::fbits(ee.r32(car + 0xEC0))});
        ex::fn::init(ours[size_t(i)], ours[size_t(i)].dt_in);
        bool en = ee.call(kGhost, {Arg::ptr(car)}).v0.lo == 0;
        ee.call(kEnable, {B(i), Arg::integer(en)});
        ex::fn::enable(ours[size_t(i)], !ours[size_t(i)].ghost);
    }
    if (!check("init/enable")) return fail;
    ex::F last = ex::F::raw(0x3F800000);
    for (int round = 0; round < 2;) {
        const std::string r = "round " + std::to_string(round) + " ";
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                if (i != j) {
                    ee.call(kSeparate, {B(i), Arg::integer(j)});
                    ex::fn::separate(ours, i, j);
                    if (!check(r + "separate " + std::to_string(i) + "," + std::to_string(j))) return fail;
                }
        for (int i = 0; i < n; ++i) {
            ee.call(kPersist, {B(i)});
            ex::fn::persist(ours, i);
            if (!check(r + "persist " + std::to_string(i))) return fail;
            ee.call(kHold, {B(i)});
            ex::fn::hold(ours[size_t(i)]);
            if (!check(r + "hold " + std::to_string(i))) return fail;
        }
        for (int i = 0; i < n; ++i) {
            ee.call(kEndPose, {B(i)});
            ex::fn::end_pose(ours[size_t(i)]);
        }
        if (!check(r + "end pose")) return fail;
        int hit = -1;
        ex::F best = ex::F::raw(0x40000000);
        for (int i = 0; i < n; ++i) {
            ex::Body& o = ours[size_t(i)];
            ee.call(kResetHit, {B(i)});
            ex::fn::reset_hit(o);
            bool theirs = ee.call(kWallSweep, {B(i)}).v0.lo != 0;
            bool mine = ex::fn::wall_sweep(o);
            if (theirs != mine) return r + "wall sweep " + std::to_string(i) + ": hit " + std::to_string(mine) + " vs " + std::to_string(theirs);
            if (!check(r + "wall sweep " + std::to_string(i))) return fail;
            if (mine && o.t < best) best = o.t, hit = i + 0x200;
            for (int j = 0; j < n; ++j)
                if (j != i) {
                    ee.call(kPairSweep, {B(i), Arg::integer(j)});
                    ex::fn::pair_sweep(ours, i, j);
                    if (!check(r + "pair sweep " + std::to_string(i) + "," + std::to_string(j))) return fail;
                }
            if (o.pair && o.t < best) best = o.t, hit = i;
        }
        if (hit < 0) break;
        last = best;
        for (int i = 0; i < n; ++i) {
            ee.call(kAdvance, {B(i), Arg::fbits(best.bits)});
            ex::fn::advance(ours[size_t(i)], best);
        }
        if (!check(r + "advance")) return fail;
        ++round;
        if (hit & 0x200) {
            int i = hit & ~0x200;
            ex::Body& o = ours[size_t(i)];
            const bool point = o.wpoint;
            ee.call(point ? kPointResponse : kCornerResponse, {B(i)});
            if (point) ex::fn::point_response(o);
            else ex::fn::corner_response(o);
            if (!check(r + (point ? "point response " : "corner response ") + std::to_string(i))) return fail;
        } else {
            ee.call(kPairResponse, {B(hit)});
            ex::fn::pair_response(ours, hit);
            if (!check(r + "pair response " + std::to_string(hit))) return fail;
        }
    }
    for (int i = 0; i < n; ++i) {
        ex::Body& o = ours[size_t(i)];
        ee.call(kStuck, {B(i), Arg::fbits(last.bits)});
        ex::fn::stuck(o, last);
        if (!check("stuck " + std::to_string(i))) return fail;
        ee.call(kPushOut, {B(i)});
        ex::fn::push_out(o);
        if (!check("push-out " + std::to_string(i))) return fail;
        ee.call(kCommit, {B(i)});
        ex::fn::commit(o);
        if (!check("commit " + std::to_string(i))) return fail;
        ee.call(kEffects, {B(i), Arg::ptr(ee.r32(body_at(cm, i) + 0x450))});
        ex::fn::effects(o);
        if (!check("effects " + std::to_string(i))) return fail;
    }
    return {};
}

std::string check_walls_step(Ee& ee, u32 cm, const std::vector<u8>& pre, int i, const ex::Course* course) {
    restore(ee, cm, pre);
    const u32 walls = wall_table(ee, ee.r32(body_at(cm, 0) + 0x478));
    std::vector<ex::Body> ours = read_bodies(ee, cm, walls, course);
    ee.call(kWallsStep, {Arg::ptr(cm), Arg::integer(i)});
    ex::step_walls(ours, i);
    return first_difference(ours, read_bodies(ee, cm, walls));
}

} // namespace gt3r::diff::collide_exact
