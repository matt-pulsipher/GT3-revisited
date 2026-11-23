// GT3's sparks and the later builds'; see sparks.h. Each line is one EE (or VU0) operation in the program's order; the
// constants are the tuning block 0x2C03B0 as the static constructor 0x19F898 fills it (from 0x34FE74 .. 0x34FEBC and
// immediates) and the routines' own (spark_rules) [presentation §L7, physics §14.3].

#include "sim/exact/sparks.h"

#include "base/ee_math.h"
#include "sim/exact/build_math.h"
#include "sim/exact/course_query.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

const F kZero = F::raw(0), kHalf = F::raw(0x3F000000), kOne = F::raw(0x3F800000);

// The tuning block's scalars P (0x2C03B0) and the routines' constants: GT3's, every later build's but 2002 Tokyo-Geneva's,
// whose compiler folded them toward zero (theirs second; Europe's block 0x2BAAF8 from 0x333B10 ..). P +0x28 500, +0x10 10,
// +0x98 100 and +0xA4 200 are exact everywhere, and so are +0x14 and +0x88 on Tokyo-Geneva.
// P +0xA8 (0) is the emitter's "a point above the road sparks too" switch: off on every build.
const F kSpeedScale = F::raw(0x44800000);  // 1024 (0x19E794)
const F kInvSpeedScale = F::raw(0x3A800000); // 1 / 1024 (0x19E934)

// 0x1EB400 .. 0x1EB520 at t = 0: a drawn value between the last two steps (the race-flow accumulator's share of the last).
F drawn(F prev, F cur, F flow) {
    const F t = kZero + flow;
    return (prev * t) + (cur * (kOne - t));
}

// The road materials that spark (0x1AAF68 .. 0x1AAF9C): 0 tarmac, 1 kerbs, 7 and 9 paved road (docs/formats/course.md).
bool sparking_material(u16 m) { return m == 0 || m == 1 || m == 7 || m == 9; }

} // namespace

void vu0_sin_cos(F a, F& s, F& c) {
    // (the routine's immediates: the same on every build)
    const F k90 = F::raw(0x42B40000), kInv180 = F::raw(0x3BB60B61), kPi = F::raw(0x40490FDB), kDegToRad = F::raw(0x3C8EFA35);
    // Both lanes reduced into [-pi/2, pi/2]: x from a - 90 (its sine is a's), y from a (its sine is a's cosine).
    const F b = a - k90;
    const F qa = a * kInv180, qb = b * kInv180;
    const u32 ma = u32(s32(a.bits) >> 31), mb = u32(s32(b.bits) >> 31); // the sign masks
    F ry = k90 - a, rx = k90 - b;
    const s32 na = ee::to_int(qa), nb = ee::to_int(qb);
    ry = ry * kDegToRad, rx = rx * kDegToRad;
    ry = ry - F::raw(ma & kPi.bits), rx = rx - F::raw(mb & kPi.bits);
    ry = ry + (ee::int_to_f(na) * kPi), rx = rx + (ee::int_to_f(nb) * kPi);
    ry = ry * F::raw((u32(na + s32(ma)) & 1u) << 31 | kOne.bits);
    rx = rx * F::raw((u32(nb + s32(mb)) & 1u) << 31 | kOne.bits);
    // The VU0 sine polynomial on each lane: x + c1 x^3 + c2 x^5 + c3 x^7 + c4 x^9, the accumulator started with the x^3 term.
    auto poly = [](F x) {
        const F x2 = x * x, x4 = x2 * x2, x3 = x2 * x, x5 = x4 * x, x7 = x4 * x3;
        F acc = x3 * F::raw(0xBE2AAAA4);
        const F x9 = x4 * x5;
        acc = ee::madd(acc, x5, F::raw(0x3C08873E));
        acc = ee::madd(acc, x7, F::raw(0xB94FB21F));
        acc = ee::madd(acc, x, kOne);
        return ee::madd(acc, x9, F::raw(0x362E9C14));
    };
    s = poly(rx), c = poly(ry);
}

Vu0Matrix Vu0Matrix::identity() {
    Vu0Matrix m;
    m.c[0] = {kOne, kZero, kZero}, m.c[1] = {kZero, kOne, kZero}, m.c[2] = {kZero, kZero, kOne}, m.c[3] = {kZero, kZero, kZero};
    return m;
}

Vec3 Vu0Matrix::point(F x, F y, F z) const {
    auto lane = [&](F Vec3::*k) {
        F acc = c[0].*k * x;
        acc = ee::madd(acc, c[1].*k, y);
        acc = ee::madd(acc, c[2].*k, z);
        return ee::madd(acc, c[3].*k, kOne);
    };
    return {lane(&Vec3::x), lane(&Vec3::y), lane(&Vec3::z)};
}

void Vu0Matrix::translate(F x, F y, F z) { c[3] = point(x, y, z); }

namespace {
// The rotations' common step: columns (i, j) to (i cos + j sin, j cos - i sin), each lane a multiply then a multiply-add;
// the new i is made first into a scratch register and moved into place last (x 1). `j_first`: its sum starts with j's
// product (RotY's order).
void turn(Vec3& i, Vec3& j, F s, F c, bool j_first) {
    const F ms = kZero - s;
    auto lane = [&](F Vec3::*k) {
        const F ik = i.*k, jk = j.*k;
        if (j_first) { // RotY: c2' = c0 s + c2 c, c0' = c0 c - c2 s (i = column 2, j = column 0)
            F acc = jk * s;
            const F ni = ee::madd(acc, ik, c);
            acc = jk * c;
            j.*k = ee::madd(acc, ik, ms);
            i.*k = ni * kOne;
        } else { // RotX / RotZ: i' = i c + j s, j' = j c - i s
            F acc = ik * c;
            const F ni = ee::madd(acc, jk, s);
            acc = jk * c;
            j.*k = ee::madd(acc, ik, ms);
            i.*k = ni * kOne;
        }
    };
    lane(&Vec3::x), lane(&Vec3::y), lane(&Vec3::z);
}
} // namespace

void Vu0Matrix::rotate_x(F deg) {
    F s, cs;
    vu0_sin_cos(deg, s, cs);
    turn(c[1], c[2], s, cs, false);
}

void Vu0Matrix::rotate_y(F deg) {
    F s, cs;
    vu0_sin_cos(deg, s, cs);
    turn(c[2], c[0], s, cs, true);
}

void Vu0Matrix::rotate_z(F deg) {
    F s, cs;
    vu0_sin_cos(deg, s, cs);
    turn(c[0], c[1], s, cs, false);
}

F particle_dt(const Behaviour& b) {
    if (gt3_program(b.rule(Rule::Leaves))) return F::raw(0x3C888889);
    if (b.pal_rate()) return F::raw(0x3CA3D70A);       // (2002 Tokyo-Geneva Europe 0x333A00) 1/50
    return folded(b, 0x3C888889, 0x3C888888);           // (2001 Tokyo 0x331F60, 2002 Tokyo-Geneva Asia 0x3341E8) 1/60
}

SparkRules spark_rules(const Behaviour& b) {
    auto f = [&b](u32 x, u32 z) { return folded(b, x, z); };
    SparkRules r;
    r.drag = f(0x3EB33312, 0x3EB33311);        // P +0x00 (0x34FE88) 0.349999
    r.gravity = f(0x411CF5C3, 0x411CF5C2);     // P +0x04 (0x34FE8C) 9.81
    r.bounce = f(0x3EFFFFBD, 0x3EFFFFBC);      // P +0x08 / +0x0C (0x34FE78) 0.499998: restitution e and friction mu
    r.spread = F::raw(0x41200000);             // P +0x10 10 (degrees)
    r.speed_min = F::raw(0x3E999956);          // P +0x14 (0x34FE90) 0.299998
    r.speed_max = f(0x3F333312, 0x3F333311);   // P +0x18 (0x34FE80) 0.699998
    r.rise = f(0x3E4CCC8A, 0x3E4CCC89);        // P +0x1C (0x34FE7C) 0.199999
    r.size_min = f(0x3C23CEA7, 0x3C23CEA6);    // P +0x20 (0x34FE84) 0.009998
    r.size_max = f(0x3CF5BE5E, 0x3CF5BE5D);    // P +0x24 (0x34FE94) 0.029998
    r.density = F::raw(0x43FA0000);            // P +0x28 500: mass = 500 w^3
    r.life_min = f(0x3F199989, 0x3F199988);    // P +0x2C (0x34FE74) 0.599999
    r.life_max = f(0x3FFFFFE7, 0x3FFFFFE6);    // P +0x30 (0x34FE98) 1.999997
    r.bounce_life = r.life_min;                // P +0x34 (0x34FE74) 0.599999
    r.kick_from = F::raw(0x3DB850DF);          // P +0x88 (0x34FE9C) 0.089998 s of age
    r.kick_to = f(0x3DE14728, 0x3DE14727);     // P +0x8C (0x34FEA0) 0.109999
    r.kick_chance = f(0x3F666656, 0x3F666655); // P +0x90 (0x34FEA4) 0.899999
    r.kick = f(0x3CCCCAB4, 0x3CCCCAB3);        // P +0x94 (0x34FEA8) 0.024999
    r.min_speed = F::raw(0x42C80000);          // P +0x98 100 (km/h)
    r.min_count = 5, r.max_count = 20;         // P +0x9C / +0xA0
    r.per_metre = F::raw(0x43480000);          // P +0xA4 200 particles per metre below the road
    r.deg_to_rad = f(0x3C8EFA35, 0x3C8EFA34);  // 0x34FE70 pi / 180 (the burst's)
    r.kmh_sq = F::raw(0x3D9E0652);             // 0x34FF64 1 / 3.6^2 (the same on every build)
    r.rad_to_deg = f(0x42652EE1, 0x42652EE0);  // 0x34FF68 180 / pi (the emission's and the body plane's)
    r.lift = f(0x3D4CCCCD, 0x3D4CCCCC);        // 0x34FF6C 0.05 m: a burst's height above the road
    // (2002 Tokyo-Seoul v2.00's emission and later: its own rule)
    r.body_plane = concept_program(b.rule(Rule::SparkEmit));
    return r;
}

void Sparks::reset() {
    for (int i = 0; i < kPool; ++i) pool_[size_t(i)].next = s16(i + 1 < kPool ? i + 1 : -1);
    free_ = 0, active_ = -1;
    frames_ = 0;
    leaf_.fill(0);
}

void Sparks::set_leaves(const Behaviour& b) {
    leaves_on_ = !gt3_program(b.rule(Rule::Leaves));
    if (leaves_on_) rules_ = exact::leaf_rules(b);
}

void Sparks::leaf(const TyreEffect& q, const Vec3& vel, Vu0Random& rng) {
    if (!leaves_on_) return;
    F amount;
    if (!leaf_amount(q, rng, amount)) return;
    const int k = alloc(); // (the record's constructor: the pool's next free one, onto the active list)
    if (k < 0) return;
    leaf_[size_t(k)] = 1;
    leaf_serial_[size_t(k)] = ++serial_;
    leaf_throw(q, vel, amount, rules_, rng, leaves_[size_t(k)]);
}

int Sparks::alloc() {
    const int p = free_;
    if (p < 0) return -1;
    Spark& s = pool_[size_t(p)];
    free_ = s.next;
    s.next = active_;
    active_ = s16(p);
    return p;
}

void Sparks::remove(int p, int prev) {
    Spark& s = pool_[size_t(p)];
    if (prev >= 0) pool_[size_t(prev)].next = s.next;
    else active_ = s.next;
    s.next = free_;
    free_ = s16(p);
}

int Sparks::active() const {
    int n = 0;
    for (int p = active_; p >= 0; p = pool_[size_t(p)].next) ++n;
    return n;
}

void Sparks::emit(const Car& c, std::span<const SparkPoint> points, const CourseQuery& road, bool drawn_car, Vu0Random& rng) {
    if (!drawn_car || points.empty()) return;
    // The world velocity in the course frame (x, height, z); none below 100 km/h (3-D speed).
    const Vec3 v{c.vel.x, c.vel.z, -c.vel.y};
    F sq = v.x * v.x;
    const F sh = v.y * v.y, sz = v.z * v.z;
    sq = sq + sh;
    F lim = sr_.min_speed * sr_.min_speed;
    lim = lim * sr_.kmh_sq;
    sq = sq + sz;
    if (sq < lim) return;
    // From 2002 Tokyo-Seoul v2.00 on (0x1A9D50, Seoul v2.00's; Rule::SparkEmit): the plane of the car body's drawn ground point
    // whose normal is the drawn body's up (its heading, the ground's pitch and roll under it), where the bursts start and
    // bounce instead of the road's.
    Vec3 bn;
    F bd;
    if (sr_.body_plane) {
        Vu0Matrix b = Vu0Matrix::identity();
        b.rotate_y(drawn(c.prev.heading, c.heading, c.flow) * sr_.rad_to_deg);
        b.rotate_x(drawn(c.prev.ground_pitch, c.ground_pitch, c.flow) * sr_.rad_to_deg);
        b.rotate_z(-(drawn(c.prev.ground_roll, c.ground_roll, c.flow) * sr_.rad_to_deg));
        bn = b.c[1];
        const F gx = drawn(c.draw_ground_prev.x, c.draw_ground.x, c.flow), gy = drawn(c.draw_ground_prev.y, c.draw_ground.y, c.flow),
                gz = drawn(c.draw_ground_prev.z, c.draw_ground.z, c.flow);
        F e = bn.x * gx;
        const F ey = bn.y * gy, ez = bn.z * gz;
        e = e + ey;
        e = e + ez;
        bd = -e;
    }
    // The car's matrix as its body is drawn (0x1AA3A0): the drawn body point, then the drawn heading, pitch and roll.
    Vu0Matrix m = Vu0Matrix::identity();
    m.translate(drawn(c.draw_pos_prev.x, c.draw_pos.x, c.flow), drawn(c.draw_pos_prev.y, c.draw_pos.y, c.flow),
                drawn(c.draw_pos_prev.z, c.draw_pos.z, c.flow));
    const Vec3 at = m.c[3];
    m.rotate_y(drawn(c.prev.heading, c.heading, c.flow) * sr_.rad_to_deg);
    m.rotate_x(drawn(c.prev.pitch, c.pitch, c.flow) * sr_.rad_to_deg);
    m.rotate_z(-(drawn(c.prev.roll, c.roll, c.flow) * sr_.rad_to_deg));
    // 0x18FC18: the road at the body point, from the car's road segment hint; off the road, none; the triangle's material.
    Ground g;
    g.x = at.x, g.h = at.y, g.z = at.z, g.seg = c.road_seg;
    u16 material = 0;
    road.ground(g, &material);
    if (g.miss) return;
    if (!sparking_material(material)) return;
    const Vec3 n = g.normal;
    const F d = g.plane_d;
    // A plane's height under a point: -(nx x + nz z + d) / ny.
    auto height = [](const Vec3& pn, F pd, const Vec3& p) {
        F h = pn.x * p.x;
        const F hz = pn.z * p.z;
        h = h + hz;
        h = h + pd;
        h = -h;
        return h / pn.y;
    };
    for (const SparkPoint& sp : points) {
        const Vec3 p = m.point(sp.x, sp.y, sp.z);
        // A point below the road bursts, 200 particles a metre down (5 .. 20), from 0.05 m above the road (from v2.00 on:
        // above the body's plane, bouncing on it).
        const F h = height(n, d, p);
        if (!(p.y < h)) continue;
        const F depth = h - p.y;
        s32 count = ee::to_int(depth * sr_.per_metre);
        if (count < sr_.min_count) count = sr_.min_count;
        if (sr_.max_count < count) count = sr_.max_count;
        if (sr_.body_plane) burst({p.x, height(bn, bd, p) + sr_.lift, p.z}, v, bn, bd, count, rng);
        else burst({p.x, h + sr_.lift, p.z}, v, n, d, count, rng);
    }
}

void Sparks::burst(const Vec3& pos, const Vec3& vel, const Vec3& n, F d, s32 count, Vu0Random& rng) {
    F hs = vel.x * vel.x;
    const F zs = vel.z * vel.z;
    hs = hs + zs;
    const F h = ee::sqrt(hs); // the horizontal speed
    for (s32 made = 0; made < count;) {
        const int k = alloc();
        if (k < 0) return;
        ++made;
        // A turn of +-5 degrees about the vertical, the speed scaled by 0.3 .. 0.7.
        F a = vu0_random(rng) - kHalf;
        a = a * sr_.deg_to_rad;
        a = a * sr_.spread;
        F r = vu0_random(rng);
        F sc = r * (sr_.speed_max - sr_.speed_min);
        sc = sc + sr_.speed_min;
        const F sn = ee::sin(a) * sc;
        const F cs = ee::cos(a) * sc;
        Vec3 v;
        v.x = (cs * vel.x) - (sn * vel.z);
        // Up or down by a triangular +-0.1 of the horizontal speed.
        F t = vu0_random(rng);
        t = t + vu0_random(rng);
        const F czv = cs * vel.z, sxv = sn * vel.x;
        t = t * kHalf;
        t = t - kHalf;
        v.z = sxv + czv;
        t = t * h;
        t = t * sr_.rise;
        v.y = vel.y + t;
        // The size w (0.01 .. 0.03 m), the mass 500 w^3, the life (0.6 .. 2 s).
        r = vu0_random(rng);
        F w = r * (sr_.size_max - sr_.size_min);
        w = w + sr_.size_min;
        F mass = w * w;
        mass = mass * w;
        mass = mass * sr_.density;
        r = vu0_random(rng);
        F life = r * (sr_.life_max - sr_.life_min);
        life = life + sr_.life_min;
        // 0x19E6F0
        leaf_[size_t(k)] = 0;
        Spark& p = pool_[size_t(k)];
        p.pos = pos, p.last = pos;
        p.inv_mass = kOne / mass;
        p.vel = v;
        p.size = w;
        p.normal = n, p.plane_d = d;
        p.total = life, p.life = life;
        F sp = v.x * v.x;
        const F sy = v.y * v.y, sz = v.z * v.z;
        sp = sp + sy;
        sp = sp + sz;
        const s32 q = ee::to_int(ee::sqrt(sp) * kSpeedScale);
        p.speed0 = s16(0x7FFF < q ? 0x7FFF : q);
        p.serial = ++serial_;
    }
}

bool Sparks::step(Spark& p, F dt, Vu0Random& rng) {
    const F life = p.life - dt;
    p.life = life;
    if (life < kZero) return false;
    p.last = p.pos;
    const F vx = p.vel.x, vy = p.vel.y, vz = p.vel.z;
    // The drag K v |v| per axis, K = -(w^2) 0.35 (dt / m); gravity on the height.
    const F mw = -p.size;
    const F mx = vx * dt;
    const F idt = p.inv_mass * dt;
    F k = mw * p.size;
    const F mz = vz * dt;
    p.pos.x = p.pos.x + mx;
    k = k * sr_.drag;
    const F my = vy * dt;
    k = k * idt;
    p.pos.y = p.pos.y + my;
    p.pos.z = p.pos.z + mz;
    const F age = p.total - life;
    F dx = (k * vx) * vx;
    if (vx < kZero) dx = -dx;
    const F g = sr_.gravity * dt;
    F dy = (k * vy) * vy;
    if (vy < kZero) dy = -dy;
    dy = dy - g;
    F dz = (k * vz) * vz;
    if (vz < kZero) dz = -dz;
    // The kick: once (or twice) in the age window 0.09 .. 0.11 s, nine times in ten a lift from its first speed.
    if (sr_.kick_from < age && age < sr_.kick_to && vu0_random(rng) < sr_.kick_chance) {
        F kick = sr_.kick * ee::int_to_f(p.speed0);
        kick = kick * kInvSpeedScale;
        kick = kick * idt;
        dy = dy + kick;
    }
    const F nvx = vx + dx, nvy = vy + dy, nvz = vz + dz;
    p.vel = {nvx, nvy, nvz};
    // Below the plane: back onto it, the life x 0.6, the velocity reflected (restitution e) with friction on the rest.
    const Vec3& n = p.normal;
    F dist = p.pos.x * n.x;
    const F dyn = p.pos.y * n.y, dzn = p.pos.z * n.z;
    dist = dist + dyn;
    dist = dist + dzn;
    dist = dist + p.plane_d;
    if (!(dist < kZero)) return true;
    F vn = nvx * n.x;
    const F vyn = nvy * n.y, vzn = nvz * n.z;
    vn = vn + vyn;
    vn = vn + vzn;
    p.life = life * sr_.bounce_life;
    p.pos.x = p.pos.x - (dist * n.x);
    p.pos.y = p.pos.y - (dist * n.y);
    p.pos.z = p.pos.z - (dist * n.z);
    const F tx = nvx - (vn * n.x), ty = nvy - (vn * n.y), tz = nvz - (vn * n.z);
    F f = sr_.bounce + sr_.bounce;
    const F ty2 = ty * ty;
    F t2 = tx * tx;
    f = f * sr_.bounce;
    const F tz2 = tz * tz;
    t2 = t2 + ty2;
    const F me = -sr_.bounce;
    f = f * vn;
    t2 = t2 + tz2;
    const F rz = me * nvz, rx = me * nvx, ry = me * nvy;
    f = ee::rsqrt(f, t2);
    const F s = kOne - f;
    if (s < kZero) {
        p.vel = {rx, ry, rz};
        return true;
    }
    p.vel = {(tx * s) + rx, (ty * s) + ry, (tz * s) + rz};
    return true;
}

void Sparks::update(F dt, Vu0Random& rng) {
    ++frames_;
    int prev = -1;
    for (int p = active_; p >= 0;) {
        const int next = pool_[size_t(p)].next;
        const bool alive = leaf_[size_t(p)] ? leaf_step(leaves_[size_t(p)], rules_.shapes[leaves_[size_t(p)].shape], rules_, dt)
                                            : step(pool_[size_t(p)], dt, rng);
        if (alive) prev = p;
        else remove(p, prev);
        p = next;
    }
}

} // namespace gt3r::sim::exact
