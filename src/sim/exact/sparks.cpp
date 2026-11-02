// GT3's sparks; see sparks.h. Each line is one EE (or VU0) operation in the program's order; the constants are the tuning
// block as the static constructor fills it (and immediates) [presentation §L7].

#include "sim/exact/sparks.h"

#include "base/ee_math.h"
#include "sim/exact/course_query.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

const F kZero = F::raw(0), kHalf = F::raw(0x3F000000), kOne = F::raw(0x3F800000);

// The tuning block (P).
const F kDrag = F::raw(0x3EB33312);        // P +0x00 0.349999
const F kGravity = F::raw(0x411CF5C3);     // P +0x04 9.81
const F kBounce = F::raw(0x3EFFFFBD);      // P +0x08 / +0x0C 0.499998: restitution e and friction mu
const F kSpread = F::raw(0x41200000);      // P +0x10 10 (degrees)
const F kSpeedMin = F::raw(0x3E999956);    // P +0x14 0.299998
const F kSpeedMax = F::raw(0x3F333312);    // P +0x18 0.699998
const F kRise = F::raw(0x3E4CCC8A);        // P +0x1C 0.199999
const F kSizeMin = F::raw(0x3C23CEA7);     // P +0x20 0.009998
const F kSizeMax = F::raw(0x3CF5BE5E);     // P +0x24 0.029998
const F kDensity = F::raw(0x43FA0000);     // P +0x28 500: mass = 500 w^3
const F kLifeMin = F::raw(0x3F199989);     // P +0x2C 0.599999
const F kLifeMax = F::raw(0x3FFFFFE7);     // P +0x30 1.999997
const F kBounceLife = F::raw(0x3F199989);  // P +0x34 0.599999
const F kKickFrom = F::raw(0x3DB850DF);    // P +0x88 0.089998 s of age
const F kKickTo = F::raw(0x3DE14728);      // P +0x8C 0.109999
const F kKickChance = F::raw(0x3F666656);  // P +0x90 0.899999
const F kKick = F::raw(0x3CCCCAB4);        // P +0x94 0.024999
const F kMinSpeed = F::raw(0x42C80000);    // P +0x98 100 (km/h)
constexpr s32 kMinCount = 5, kMaxCount = 20; // P +0x9C / +0xA0
const F kPerMetre = F::raw(0x43480000);    // P +0xA4 200 particles per metre below the road
// P +0xA8 (0) is the emitter's "a point above the road sparks too" switch: off.

const F kDegToRad = F::raw(0x3C8EFA35);    // pi / 180
const F kKmhSq = F::raw(0x3D9E0652);       // 1 / 3.6^2
const F kRadToDeg = F::raw(0x42652EE1);    // 180 / pi
const F kLift = F::raw(0x3D4CCCCD);        // 0.05 m: a burst's height above the road
const F kSpeedScale = F::raw(0x44800000);  // 1024
const F kInvSpeedScale = F::raw(0x3A800000); // 1 / 1024

// At t = 0: a drawn value between the last two steps (the race-flow accumulator's share of the last).
F drawn(F prev, F cur, F flow) {
    const F t = kZero + flow;
    return (prev * t) + (cur * (kOne - t));
}

// The road materials that spark: 0 tarmac, 1 kerbs, 7 and 9 paved road (docs/formats/course.md).
bool sparking_material(u16 m) { return m == 0 || m == 1 || m == 7 || m == 9; }

} // namespace

void vu0_sin_cos(F a, F& s, F& c) {
    const F k90 = F::raw(0x42B40000), kInv180 = F::raw(0x3BB60B61), kPi = F::raw(0x40490FDB);
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

void Sparks::reset() {
    for (int i = 0; i < kPool; ++i) pool_[size_t(i)].next = s16(i + 1 < kPool ? i + 1 : -1);
    free_ = 0, active_ = -1;
    frames_ = 0;
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
    F lim = kMinSpeed * kMinSpeed;
    lim = lim * kKmhSq;
    sq = sq + sz;
    if (sq < lim) return;
    // The car's matrix as its body is drawn: the drawn body point, then the drawn heading, pitch and roll.
    Vu0Matrix m = Vu0Matrix::identity();
    m.translate(drawn(c.draw_pos_prev.x, c.draw_pos.x, c.flow), drawn(c.draw_pos_prev.y, c.draw_pos.y, c.flow),
                drawn(c.draw_pos_prev.z, c.draw_pos.z, c.flow));
    const Vec3 at = m.c[3];
    m.rotate_y(drawn(c.prev.heading, c.heading, c.flow) * kRadToDeg);
    m.rotate_x(drawn(c.prev.pitch, c.pitch, c.flow) * kRadToDeg);
    m.rotate_z(-(drawn(c.prev.roll, c.roll, c.flow) * kRadToDeg));
    // The road at the body point, from the car's road segment hint; off the road, none; the triangle's material.
    Ground g;
    g.x = at.x, g.h = at.y, g.z = at.z, g.seg = c.road_seg;
    u16 material = 0;
    road.ground(g, &material);
    if (g.miss) return;
    if (!sparking_material(material)) return;
    const Vec3 n = g.normal;
    const F d = g.plane_d;
    for (const SparkPoint& sp : points) {
        const Vec3 p = m.point(sp.x, sp.y, sp.z);
        // The road's height under the point: -(nx x + nz z + d) / ny; a point below it bursts, 200 particles a metre down
        // (5 .. 20), from 0.05 m above the road.
        F h = n.x * p.x;
        const F hz = n.z * p.z;
        h = h + hz;
        h = h + d;
        h = -h;
        h = h / n.y;
        if (!(p.y < h)) continue;
        const F depth = h - p.y;
        s32 count = ee::to_int(depth * kPerMetre);
        if (count < kMinCount) count = kMinCount;
        if (kMaxCount < count) count = kMaxCount;
        burst({p.x, h + kLift, p.z}, v, n, d, count, rng);
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
        a = a * kDegToRad;
        a = a * kSpread;
        F r = vu0_random(rng);
        F sc = r * (kSpeedMax - kSpeedMin);
        sc = sc + kSpeedMin;
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
        t = t * kRise;
        v.y = vel.y + t;
        // The size w (0.01 .. 0.03 m), the mass 500 w^3, the life (0.6 .. 2 s).
        r = vu0_random(rng);
        F w = r * (kSizeMax - kSizeMin);
        w = w + kSizeMin;
        F mass = w * w;
        mass = mass * w;
        mass = mass * kDensity;
        r = vu0_random(rng);
        F life = r * (kLifeMax - kLifeMin);
        life = life + kLifeMin;
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
    k = k * kDrag;
    const F my = vy * dt;
    k = k * idt;
    p.pos.y = p.pos.y + my;
    p.pos.z = p.pos.z + mz;
    const F age = p.total - life;
    F dx = (k * vx) * vx;
    if (vx < kZero) dx = -dx;
    const F g = kGravity * dt;
    F dy = (k * vy) * vy;
    if (vy < kZero) dy = -dy;
    dy = dy - g;
    F dz = (k * vz) * vz;
    if (vz < kZero) dz = -dz;
    // The kick: once (or twice) in the age window 0.09 .. 0.11 s, nine times in ten a lift from its first speed.
    if (kKickFrom < age && age < kKickTo && vu0_random(rng) < kKickChance) {
        F kick = kKick * ee::int_to_f(p.speed0);
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
    p.life = life * kBounceLife;
    p.pos.x = p.pos.x - (dist * n.x);
    p.pos.y = p.pos.y - (dist * n.y);
    p.pos.z = p.pos.z - (dist * n.z);
    const F tx = nvx - (vn * n.x), ty = nvy - (vn * n.y), tz = nvz - (vn * n.z);
    F f = kBounce + kBounce;
    const F ty2 = ty * ty;
    F t2 = tx * tx;
    f = f * kBounce;
    const F tz2 = tz * tz;
    t2 = t2 + ty2;
    const F me = -kBounce;
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
        if (step(pool_[size_t(p)], dt, rng)) prev = p;
        else remove(p, prev);
        p = next;
    }
}

} // namespace gt3r::sim::exact
