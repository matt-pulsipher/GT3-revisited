// The later builds' leaves; see leaves.h. The addresses are 2002 Tokyo-Geneva Europe's (Concept 2001 Tokyo: the maker
// 0x19C4E8, the init 0x19D1B8, the update 0x19D290, the shapes 0x19CE98 from the manager's constructor 0x19C240); the code
// is the same in every later build, its constants the build's (2002 Tokyo-Geneva's folded toward zero).

#include "sim/exact/leaves.h"

#include "base/ee_math.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

const F kZero = F::raw(0), kHalf = F::raw(0x3F000000), kOne = F::raw(0x3F800000), kTwo = F::raw(0x40000000);

// Europe 0x19CF10: shape k from the constructor's mass m and sides w, h.
LeafShape shape(const Behaviour& b, F m, F w, F h) {
    LeafShape s;
    s.mass = m, s.w = w, s.h = h;
    const F hw = w * kHalf, hh = h * kHalf;
    const F h2 = h * h;
    F d2 = w * w;
    d2 = d2 + h2;
    s.corners = {Vec3{-hw, kZero, -hh}, Vec3{hw, kZero, -hh}, Vec3{-hw, kZero, hh}, Vec3{hw, kZero, hh}};
    s.inv_mass = kOne / m;
    s.diag = ee::sqrt(d2);
    const F m2 = F::raw(0xC0000000);                                 // -2
    const F wm = w * m2;                                             // -2 w
    const F m12 = m * folded(b, 0x3DAAAAAB, 0x3DAAAAAA);             // (Europe 0x333C44) 1 / 12
    F sum = w + h;
    sum = sum * m2;
    F a = wm * w;
    const F wh = wm * h;
    F ix = m12 * h, iz = m12 * w;
    a = a * w;
    F c = wh * h;
    s.lift = wh;
    s.drag = sum * F::raw(0x3BA3D70A);                               // (0x333C4C) 0.005
    ix = ix * h;
    a = a * h;
    c = c * h;
    s.iy = m12 * d2;
    s.ix = ix;
    iz = iz * w;
    s.iz = iz;
    const F k12 = F::raw(0x41400000), k015 = F::raw(0x3C75C28F); // 12, (0x333C48) 0.015
    s.damp_z = (a / k12) * k015;
    s.damp_x = (c / k12) * k015;
    return s;
}

} // namespace

LeafRules leaf_rules(const Behaviour& b) {
    LeafRules r;
    // The manager's constructor (Europe 0x19C2D8): 1 g; shapes 1 .. 3 squares of 0.12, 0.13 and 0.14 m, 4 .. 8 of 0.1 m.
    const F m = folded(b, 0x3A83126F, 0x3A83126E);                   // (0x333BC4) 0.001
    const F tenth = folded(b, 0x3DCCCCCD, 0x3DCCCCCC);               // (0x333BC8) 0.1
    const F sides[3] = {F::raw(0x3DF5C28F), F::raw(0x3E051EB8), folded(b, 0x3E0F5C29, 0x3E0F5C28)}; // (0x333BCC ..) 0.12 .. 0.14
    for (size_t k = 0; k < 8; ++k) {
        const F s = k < 3 ? sides[k] : tenth;
        r.shapes[k] = shape(b, m, s, s);
    }
    r.carry = folded(b, 0x3F59999A, 0x3F599999);                     // (0x333BDC) 0.85
    r.scatter = folded(b, 0x3E4CCCCD, 0x3E4CCCCC);                   // (0x333BE0) 0.2
    r.yaw_spread = folded(b, 0x3F060A8B, 0x3F060A89);                // (0x333BE4) 30 degrees
    r.yaw_offset = folded(b, 0xBE860A8B, 0xBE860A89);                // (0x333BE8) -15 degrees
    r.pitch_spread = F::raw(0x3E32B8B0);                             // (0x333BEC) 10 degrees
    r.pitch_offset = folded(b, 0xBFBDE444, 0xBFBDE442);              // (0x333BF0) -85 degrees
    r.two_pi = folded(b, 0x40C90FDB, 0x40C90FDA);                    // (0x333C50 / 0x333C58 / 0x333C5C)
    r.gravity = folded(b, 0x411CF5C3, 0x411CF5C2);                   // (0x333C54) 9.81
    r.spin_keep = folded(b, 0x3F7FBE77, 0x3F7FBE76);                 // (0x333C60) 0.999
    r.light_base = folded(b, 0x3E99999A, 0x3E999999);                // (0x333C64) 0.3
    r.light_gain = F::raw(0x3F333333);                               // (0x333C68) 0.7
    return r;
}

std::array<Vec3, 3> leaf_matrix(const Vec3& angle) {
    F sa, ca, sb, cb, sc, cc;
    ee::sincos(angle.x, sa, ca);
    ee::sincos(angle.y, sb, cb);
    ee::sincos(angle.z, sc, cc);
    const F sasc = sa * sc, sacc = sa * cc;
    const F sbsc = sb * sc;
    const F mca = -ca;
    const F sasccb = sasc * cb, sacccb = sacc * cb;
    const F cbcc = cb * cc, cbsc = cb * sc;
    const F sascsb = sasc * sb;
    const F sbcc = sb * cc;
    const F saccsb = sacc * sb;
    const F m20 = mca * sb, m22 = ca * cb;
    std::array<Vec3, 3> m;
    m[1].z = sbsc - sacccb;
    m[0].x = cbcc - sascsb;
    m[0].z = sbcc + sasccb;
    m[2].x = m20, m[2].y = sa, m[2].z = m22;
    m[1].x = cbsc + saccsb;
    m[1].y = ca * cc;
    m[0].y = mca * sc;
    return m;
}

bool leaf_amount(const TyreEffect& q, Vu0Random& rng, F& amount) {
    F r = vu0_random(rng);
    r = r * F::raw(0x3EE66666);           // (0x333BD8) 0.45
    const F slip = q.rim_speed - q.hub_vx; // request +0x38 - +0x34
    r = r + kHalf;
    amount = slip * r;
    const F a = amount < kZero ? -amount : amount;
    return !(a < kTwo);
}

void leaf_throw(const TyreEffect& q, const Vec3& vel, F amount, const LeafRules& rr, Vu0Random& rng, Leaf& p) {
    const F speed = amount < kZero ? -amount : amount;
    // The heading: the wheel's, turned by -15 .. 15 degrees.
    F r = vu0_random(rng);
    F yaw = q.yaw + (r * rr.yaw_spread);
    yaw = yaw + rr.yaw_offset;
    const F cy = ee::cos(yaw), sy = ee::sin(yaw);
    // The ground's slope along it: (n . heading, n.y) made a unit.
    const F nx = q.normal.x * sy, nz = q.normal.z * cy;
    const F ny = q.normal.y;
    const F ny2 = ny * ny;
    F along = nx + nz;
    F len = along * along;
    len = len + ny2;
    const F inv = ee::rsqrt(kOne, len);
    F up = ny * inv;
    r = vu0_random(rng);
    along = along * inv;
    // The throw's angle below the slope: 75 .. 85 degrees (the other way round for a negative slip).
    F pitch = r * rr.pitch_spread;
    pitch = pitch + rr.pitch_offset;
    if (amount < kZero) pitch = -pitch;
    const F cp = ee::cos(pitch), sp = ee::sin(pitch);
    const F t2 = sp * up, t1 = cp * along, t0 = sp * along, t3 = cp * up;
    const F cx = vel.x * rr.carry, ch = vel.y * rr.carry, cz = vel.z * rr.carry;
    const F flat = t1 - t2;
    F rise = t0 + t3;
    F vz = flat * cy, vx = flat * sy;
    rise = rise * speed;
    vz = vz * speed, vx = vx * speed;
    const Vec3 v{vx + cx, rise + ch, vz + cz};
    // The position: the hub's, each axis spread by +-0.1 m; the spin: +-10, +-4, +-10 rad/s; the shape.
    Vec3 at;
    r = vu0_random(rng);
    at.x = q.x + ((r - kHalf) * rr.scatter);
    r = vu0_random(rng);
    at.y = q.h + ((r - kHalf) * rr.scatter);
    r = vu0_random(rng);
    at.z = q.z + ((r - kHalf) * rr.scatter);
    Vec3 w;
    r = vu0_random(rng);
    w.x = (r - kHalf) * F::raw(0x41A00000); // 20
    r = vu0_random(rng);
    w.y = (r - kHalf) * F::raw(0x41000000); // 8
    r = vu0_random(rng);
    w.z = (r - kHalf) * F::raw(0x41A00000);
    r = vu0_random(rng);
    s32 k = ee::to_int(r * F::raw(0x41000000));
    if (!(k < 8)) k = 7;
    // The init (Europe 0x19D230): the record, the plane lowered by the shape's diagonal, three random angles.
    const LeafShape& s = rr.shapes[size_t(k)];
    p.pos = at, p.vel = v, p.spin = w;
    p.normal = q.normal;
    p.shade = q.attr_a;
    p.shape = u8(k);
    p.plane_d = q.plane_d + s.diag;
    r = vu0_random(rng);
    p.angle.x = r * rr.two_pi;
    r = vu0_random(rng);
    p.angle.y = r * rr.two_pi;
    r = vu0_random(rng);
    p.angle.z = r * rr.two_pi;
    p.last_pos = p.pos, p.last_angle = p.angle;
}

bool leaf_step(Leaf& p, const LeafShape& s, const LeafRules& rr, F dt) {
    p.last_pos = p.pos, p.last_angle = p.angle;
    const std::array<Vec3, 3> m = leaf_matrix(p.angle);
    const F ix = s.ix, iy = s.iy, iz = s.iz;
    const F wx = p.spin.x, wy = p.spin.y, wz = p.spin.z;
    // The velocity in the leaf's frame (0x19D1D8), the air's force there (along the plane, along the normal), back to the
    // course's frame (0x19D180); gravity.
    auto row = [&](const Vec3& r, const Vec3& v) {
        F x = v.x * r.x;
        const F y = v.y * r.y, z = v.z * r.z;
        x = x + y;
        return x + z;
    };
    const Vec3 l{row(m[0], p.vel), row(m[1], p.vel), row(m[2], p.vel)};
    const Vec3 t{s.drag * l.x, s.lift * l.y, s.drag * l.z};
    auto col = [&](F Vec3::*k) {
        F x = t.x * (m[0].*k);
        const F y = t.y * (m[1].*k), z = t.z * (m[2].*k);
        x = x + y;
        return x + z;
    };
    Vec3 f{col(&Vec3::x), col(&Vec3::y), col(&Vec3::z)};
    f.y = f.y + ((-s.mass) * rr.gravity);
    // The air's torque about the plane's axes; Euler's equations.
    const F tx = s.damp_x * wx, tz = s.damp_z * wz;
    F sa, ca, sc, cc;
    ee::sincos(p.angle.x, sa, ca);
    ee::sincos(p.angle.z, sc, cc);
    F ex = iy - iz, ez = ix - iy, ey = iz - ix;
    ex = ex * wy, ez = ez * wx, ey = ey * wz;
    ex = ex * wz, ez = ez * wy, ey = ey * wx;
    F dx = tx + ex, dz = tz + ez, dy = kZero + ey;
    dx = dx * dt, dz = dz * dt, dy = dy * dt;
    dx = dx / ix, dz = dz / iz, dy = dy / iy;
    p.spin.x = wx + dx, p.spin.z = wz + dz, p.spin.y = wy + dy;
    // The angles from the spin before this update (none while cos a is 0), each wrapped once into 0 .. 2 pi.
    if (!(ca == kZero)) {
        F k = wx * sc;
        const F ky = wy * cc;
        F q = wx * cc;
        const F qy = wy * sc;
        k = k - ky;
        q = q + qy;
        k = k / ca;
        q = -q;
        q = q * dt;
        F c = k * sa;
        k = k * dt;
        p.angle.x = p.angle.x + q;
        c = c + wz;
        p.angle.y = p.angle.y + k;
        c = -c;
        c = c * dt;
        p.angle.z = p.angle.z + c;
    }
    for (F Vec3::*a : {&Vec3::x, &Vec3::y, &Vec3::z}) {
        F x = p.angle.*a;
        if (x < kZero) x = x + rr.two_pi, p.angle.*a = x;
        if (!(rr.two_pi <= x)) continue;
        p.angle.*a = x - rr.two_pi;
    }
    // The motion; the spin damped; gone below the plane.
    p.pos.x = p.pos.x + (p.vel.x * dt);
    p.pos.y = p.pos.y + (p.vel.y * dt);
    p.pos.z = p.pos.z + (p.vel.z * dt);
    p.vel.x = p.vel.x + ((f.x * s.inv_mass) * dt);
    p.vel.y = p.vel.y + ((f.y * s.inv_mass) * dt);
    F d = p.normal.x * p.pos.x;
    const F dyn = p.normal.y * p.pos.y, dzn = p.normal.z * p.pos.z;
    p.spin.x = p.spin.x * rr.spin_keep;
    d = d + dyn;
    p.spin.z = p.spin.z * rr.spin_keep;
    p.spin.y = p.spin.y * rr.spin_keep;
    d = d + dzn;
    d = d + p.plane_d;
    p.vel.z = p.vel.z + ((f.z * s.inv_mass) * dt);
    return !(d < kZero);
}

} // namespace gt3r::sim::exact
