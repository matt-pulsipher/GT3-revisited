// The collision step in EE arithmetic; see collision_exact.h and physics §5.3. Each function follows one routine of the
// program statement by statement: every operator is one EE operation on the operands the program uses (add and multiply
// are commutative bit for bit, so only the grouping matters). Comparisons are the program's
// strict / non-strict ones; where it branches past a test the code below says so.

#include "sim/collision_exact.h"

#include "base/ee_math.h"
#include "sim/exact/build_math.h"

namespace gt3r::sim::collide::exact {

namespace {

const F kZero = F::raw(0), kOne = F::raw(0x3F800000), kHalf = F::raw(0x3F000000), kTwo = F::raw(0x40000000);
const F kQuarter = F::raw(0x3E800000), kTen = F::raw(0x41200000), kHundred = F::raw(0x42C80000),
        kTwenty = F::raw(0x41A00000), kMinusOneAndHalf = F::raw(0xBFC00000);
// The program's constants.
const F kChamfer = F::raw(0x3E4CCCCD);       // 0.2, the corner chamfer per metre of width
const F kPi = F::raw(0x40490FDB);
const F kTwoPi = F::raw(0x40C90FDB);
const F kMinusTwoPi = F::raw(0xC0C90FDB);
const F kMaxYaw = F::raw(0x427B53D2);        // 20 pi rad/s
const F kMinYaw = F::raw(0xC27B53D2);
const F kEarly = F::raw(0x3DCCCCCD);         // 0.1, the stuck nudge's "last hit early in the step"
const F kPairStrength = F::raw(0x466A6000);  // 15000
const F kSoundDecay = F::raw(0x3F7AE148);    // 0.98, the hit sound level's decay per step
const F kPointBounce = F::raw(0xBF866666);   // -(1 + 0.05)
const F kPointStrength = F::raw(0x47C35000); // 1e5
const F kCornerBounce = F::raw(0xBF866666);
const F kCornerStrength = F::raw(0x47C35000);

F neg_if_negative(F v) { return v < kZero ? -v : v; }

bool linked(const Body& t, int a, int b) { return !t.course || t.course->linked(a, b); }

// The points at the start / end pose (sincosf).
void start_points(Body& b) {
    F s, cs;
    ee::sincos(b.sh, s, cs);
    for (Point& q : b.p) {
        q.sx = ((cs * q.lx) - (s * q.ly)) + b.sx;
        q.sy = ((s * q.lx) + (cs * q.ly)) + b.sy;
    }
}
void end_points(Body& b) {
    F s, cs;
    ee::sincos(b.eh, s, cs);
    for (Point& q : b.p) {
        q.ex = ((cs * q.lx) - (s * q.ly)) + b.ex;
        q.ey = ((s * q.lx) + (cs * q.ly)) + b.ey;
    }
}

// The box swept from the start to the end pose, grown by the radius.
void sweep_box(Body& b) {
    F lo = b.sx, hi = b.ex;
    if (b.dx < kZero) lo = b.ex, hi = b.sx;
    b.bx0 = lo - b.radius;
    b.bx1 = hi + b.radius;
    lo = b.sy, hi = b.ey;
    if (b.dy < kZero) lo = b.ey, hi = b.sy;
    b.by0 = lo - b.radius;
    b.by1 = hi + b.radius;
    b.bw = b.bx1 - b.bx0;
    b.bh = b.by1 - b.by0;
}

// Moves the start pose and the start-world points.
void shift(Body& b, F dx, F dy) {
    b.sx = b.sx + dx;
    b.sy = b.sy + dy;
    for (Point& q : b.p) {
        q.sx = q.sx + dx;
        q.sy = q.sy + dy;
    }
}

// (2002 Tokyo-Geneva's 20 pi two units lower: Europe)
F clamp_yaw(F w, const Behaviour& h) {
    const F hi = sim::exact::folded(h, kMaxYaw.bits, 0x427B53D0), lo = sim::exact::folded(h, kMinYaw.bits, 0xC27B53D0);
    if (ee::abs(w) < hi) return w;
    return w < kZero ? lo : hi;
}

// The body's end points for the wall lines' side tests (dx (ey - ay)) - (dy (ex - ax)) where their signs are certain
// (ee::side_sign): most of a car's points are far from most candidate segments' lines, and a test that cannot pass is left
// out (the same outcome). `ok`: every coordinate is a side_sign operand.
struct EndPoints {
    float x[8], y[8];
    bool ok = false;
    void load(const Body& b) {
        ok = true;
        for (size_t k = 0; k < 8; ++k)
            if (!ee::side_operand(b.p[k].ex, x[k]) || !ee::side_operand(b.p[k].ey, y[k])) ok = false;
    }
    // Bit k of pos: point k's test is certainly >= 0, of neg: certainly <= 0 (neither: not certain).
    void sides(F dx_, F dy_, F ax_, F ay_, unsigned& pos, unsigned& neg) const {
        pos = neg = 0;
        float dx, dy, ax, ay;
        if (!ok || !ee::side_operand(dx_, dx) || !ee::side_operand(dy_, dy) || !ee::side_operand(ax_, ax) ||
            !ee::side_operand(ay_, ay))
            return;
        int sg[8];
        for (int k = 0; k < 8; ++k) sg[k] = ee::side_sign(dx, y[k], ay, dy, x[k], ax);
        unsigned p = 0, n = 0;
        for (int k = 0; k < 8; ++k) p |= unsigned(sg[k] > 0) << k, n |= unsigned(sg[k] < 0) << k;
        pos = p, neg = n;
    }
};

void min_max(F v, F& lo, F& hi) {
    if (v < lo) lo = v;
    if (hi < v) hi = v;
}

// A wall segment (record P -> next) against the body's unflagged points (end beyond the wall, start inside,
// the motion crossing the segment), then a corner vertex P against the car's edges. Keeps each point's / edge's
// earliest crossing; sensor segments only record the body's earliest sensor crossing.
void sweep_segment(Body& b, const WallPt* W, int P, const EndPoints& ends) {
    int N = wall_next(W, P);
    F by = -W[N].z, bx = W[N].x, ay = -W[P].z, ax = W[P].x;
    bool sensor = (W[P].flags & 2) != 0;
    F dx = bx - ax, dy = by - ay;
    unsigned inside, beyond;
    ends.sides(dx, dy, ax, ay, inside, beyond);
    for (size_t k = 0; k < 8; ++k) {
        Point& q = b.p[k];
        if (q.flags) continue;
        if (inside >> k & 1) continue; // (s1 >= 0, below)
        F s1 = (dx * (q.ey - ay)) - (dy * (q.ex - ax));
        if (kZero <= s1) continue;
        F ux = q.sx - ax, uy = q.sy - ay;
        F s0 = (dx * uy) - (dy * ux);
        if (s0 <= kZero) continue;
        F mx = q.ex - q.sx, my = q.ey - q.sy;
        if ((mx * uy) - (my * ux) < kZero) continue;
        if (kZero < (mx * (q.sy - by)) - (my * (q.sx - bx))) continue;
        F den = s0 - s1, t = kOne;
        if (!(den == kZero)) t = s0 / den;
        if (!(t < q.t)) continue;
        if (sensor) {
            if (t < b.sensor_t) {
                b.sensor_t = t;
                b.sensor = true;
            }
            continue;
        }
        F len = ee::sqrt((dx * dx) + (dy * dy));
        q.t = t;
        q.hit = true;
        q.wall = P;
        if (len == kZero) {
            q.ty = kZero;
            q.tx = kOne;
        } else {
            q.ty = dy / len;
            q.tx = dx / len;
        }
    }
    if (!(W[P].flags & 1)) return;
    for (int j = 0; j < 8; ++j) { // edge j - 1: point j - 1 -> point j
        const Point& p1 = b.p[size_t((j + 7) & 7)];
        const Point& p2 = b.p[size_t(j)];
        F e1 = ((p2.ex - p1.ex) * (ay - p1.ey)) - ((p2.ey - p1.ey) * (ax - p1.ex));
        if (e1 <= kZero) continue;
        F ry = ay - p1.sy, rx = ax - p1.sx, ex = p2.sx - p1.sx, ey = p2.sy - p1.sy;
        F e0 = (ex * ry) - (ey * rx);
        if (kZero <= e0) continue;
        F m1x = p1.ex - p1.sx, m1y = p1.ey - p1.sy, m2x = p2.ex - p2.sx, m2y = p2.ey - p2.sy;
        F c1 = (m1x * ey) - (m1y * ex);
        F a1 = (m1x * ry) - (m1y * rx);
        F a2 = (m2x * (ay - p2.sy)) - (m2y * (ax - p2.sx));
        if (c1 < kZero) {
            if (kZero < a1) continue;
            if (kZero < a2) continue;
        }
        if ((m2x * ey) - (m2y * ex) < kZero) {
            if (a1 < kZero) continue;
            if (a2 < kZero) continue;
        }
        if (a1 < kZero) continue;
        if (kZero < a2) continue;
        F den = e0 - e1, t = kOne;
        if (!(den == kZero)) t = e0 / den;
        if (sensor) {
            if (t < b.sensor_t) {
                b.sensor_t = t;
                b.sensor = true;
            }
            continue;
        }
        Edge& e = b.e[size_t((j + 7) & 7)];
        if (t < e.t) {
            e.t = t;
            e.wall = P;
            e.hit = true;
        }
    }
}

// The end-of-step push-out against one wall segment: each unflagged point whose end position is beyond the
// wall while a neighbour point (the previous one in the first pass, the next in the second) is inside, the car edge
// between them crossing the segment. The end pose moves at once (the next tests see it); true when it moved.
bool push_segment(Body& b, const WallPt* W, int P, const EndPoints& ends) {
    int N = wall_next(W, P);
    F by = -W[N].z, ax = W[P].x, ay = -W[P].z, bx = W[N].x;
    F dx = bx - ax, dy = by - ay, dx2 = dx * dx, dy2 = dy * dy;
    // With every point certainly on one side of the line (all >= 0, or all <= 0), no point is beyond the wall with a
    // neighbour inside: nothing below happens.
    {
        unsigned inside, beyond;
        ends.sides(dx, dy, ax, ay, inside, beyond);
        if (inside == 0xFF || beyond == 0xFF) return false;
    }
    // Each point's side of the wall line, (dx (ey - ay)) - (dy (ex - ax)), as the tests below compute it (the same
    // operations give the same bits: kept until the end pose moves).
    F side[8];
    auto sides = [&] {
        for (size_t k = 0; k < 8; ++k) side[k] = (dx * (b.p[k].ey - ay)) - (dy * (b.p[k].ex - ax));
    };
    sides();
    bool moved = false;
    for (int a0 = 0; a0 < 16; ++a0) {
        Point& q = b.p[size_t(a0 & 7)];
        if (q.flags) continue;
        if (kZero <= side[size_t(a0 & 7)]) continue;
        const size_t on = size_t(a0 < 8 ? (a0 + 7) & 7 : (a0 + 1) & 7);
        if (side[on] <= kZero) continue;
        const Point& o = b.p[on];
        F qx = q.ex - ax, qy = q.ey - ay;
        F oy = o.ey - ay, ox = o.ex - ax;
        F cx = q.ex - o.ex, cy = q.ey - o.ey;
        if ((cx * oy) - (cy * ox) < kZero) continue;
        F obx = o.ex - bx, oby = o.ey - by;
        if (kZero < (cx * oby) - (cy * obx)) continue;
        F ur = (qx * dx) + (qy * dy);
        F d2 = dx2 + dy2;
        if (ur < kZero || d2 < ur) {
            // Beyond an end of the segment: move so the car edge runs through that end.
            F rx = ur < kZero ? ox : obx, ry = ur < kZero ? oy : oby;
            F c2 = (cx * cx) + (cy * cy), lam = kZero;
            if (!(c2 == kZero)) lam = (((-rx) * cx) - (ry * cy)) / c2;
            F mx = (lam * cx) + rx, my = (lam * cy) + ry;
            b.ex = b.ex - mx;
            b.ey = b.ey - my;
            end_points(b);
            sides();
            moved = true;
            continue;
        }
        F inv = kOne, u = ur;
        const bool jp = b.behaviour.rule(Rule::PushSegment) == Program::NtscJ; // the NTSC-J program takes the root of a zero length too
        if (jp) inv = ee::rsqrt(kOne, d2);
        if (!(d2 == kZero)) {
            if (!jp) inv = ee::rsqrt(kOne, d2);
            u = ur / d2;
        }
        F px = (u * dx) - qx, py = (u * dy) - qy;
        if (kQuarter < (px * px) + (py * py)) {
            // More than 0.5 m: slide to the segment's far end instead.
            F f = a0 < 8 ? kOne - u : -u;
            b.ex = b.ex + (f * dx);
            b.ey = b.ey + (f * dy);
            end_points(b);
            sides();
            moved = true;
            continue;
        }
        b.ex = b.ex + px;
        b.ey = b.ey + py;
        end_points(b);
        sides();
        moved = true;
        q.wall = P;
        q.flags |= 1;
        q.ty = dy * inv;
        q.tx = dx * inv;
    }
    return moved;
}

// Friction of a wall impulse J at a point moving with vp: against the velocity's part off J (and the
// vertical velocity), min(0.5 |J|, m |that velocity|).
void friction(const Body& b, F jx, F jy, F vpx, F vpy, F& fx, F& fy, F& fz) {
    const bool jp = b.behaviour.rule(Rule::WallFriction) == Program::NtscJ; // the NTSC-J program has neither zero test
    F j2 = (jx * jx) + (jy * jy), lam = kZero;
    if (jp || !(j2 == kZero)) lam = ((vpx * jx) + (vpy * jy)) / j2;
    F ty = (lam * jy) - vpy, tx = (lam * jx) - vpx, tz = -b.vz;
    F t2 = ((tx * tx) + (ty * ty)) + (tz * tz);
    if (!jp && t2 == kZero) {
        fx = fy = fz = kZero;
        return;
    }
    F inv = ee::rsqrt(kOne, t2);
    F mag = (ee::sqrt(j2) * kHalf) * inv;
    if (b.mass < mag) mag = b.mass;
    fz = mag * tz;
    fx = mag * tx;
    fy = mag * ty;
}

// The vertical velocity after a wall impulse never changes sign.
void vertical(Body& b, F fz) {
    bool down = b.vz < kZero;
    F vz = b.vz + (fz * b.im);
    b.vz = vz;
    if (down ? kZero < vz : vz < kZero) b.vz = kZero;
}

// The car-car impulse at A's crossing point on its edge against B's vertex, normal = A's edge normal:
// j = -1.5 (v_rel . n) / K. J (on A) = j n.
F pair_impulse(const Body& A, const Body& B, const F* ra, const F* rb, F& jx, F& jy) {
    const Edge& ae = A.e[size_t(A.hit_edge)];
    const Edge& bv = B.e[size_t(A.vertex)];
    F ncx = -A.cpx;
    F nx = (ra[0] * ae.nx) + (ra[1] * ae.ny);
    F t17 = ra[1] * ncx, t14 = ra[3] * ncx;
    F ny = (ra[2] * ae.nx) + (ra[3] * ae.ny);
    F nbx = (rb[0] * nx) + (rb[2] * ny);
    F nby = (rb[1] * nx) + (rb[3] * ny);
    F ray = (ra[0] * A.cpy) + t17;   // A's contact arm, world: (rax, ray); here ray and -rax
    F nrax = (ra[2] * A.cpy) + t14;
    F vax = A.vx - (A.w * ray), vay = A.vy - (A.w * nrax);
    F nlx = -bv.sx;
    F rby = (rb[0] * bv.sy) + (rb[1] * nlx);
    F nrbx = (rb[2] * bv.sy) + (rb[3] * nlx);
    F relx = vax - (B.vx - (B.w * rby));
    F rely = vay - (B.vy - (B.w * nrbx));
    F ka = (-((A.cpx * ae.ny) - (A.cpy * ae.nx))) * A.ii;
    F kb = (-((bv.sx * nby) - (bv.sy * nbx))) * B.ii;
    F sy = (ka * ray) + (kb * rby), sx = (ka * nrax) + (kb * nrbx);
    F k = (A.im + B.im) + ((sy * nx) + (sx * ny));
    F j = kZero;
    if (!(k == kZero)) j = (((relx * nx) + (rely * ny)) * kMinusOneAndHalf) / k;
    jx = j * nx;
    jy = j * ny;
    return j;
}

// If B's vertex is still outside A's edge line after the response's pose (the rigid pose differs from the
// straight crossing), half the gap each, along A's edge normal.
void touch_up(Body& A, Body& B, const F* ra, const F* rb) {
    const Edge& bv = B.e[size_t(A.vertex)];
    const Edge& ae = A.e[size_t(A.hit_edge)];
    F rbx = (rb[0] * bv.sx) + (rb[1] * bv.sy);
    F rby = (rb[2] * bv.sx) + (rb[3] * bv.sy);
    F qx = (B.sx - A.sx) + rbx, qy = (B.sy - A.sy) + rby;
    F nx = (ra[0] * ae.nx) + (ra[1] * ae.ny);
    F ny = (ra[2] * ae.nx) + (ra[3] * ae.ny);
    F gap = ((nx * qx) + (ny * qy)) + ae.d;
    if (gap <= kZero) return;
    F ng = -gap;
    F my = (ng * ny) * kHalf, mx = (ng * nx) * kHalf;
    B.sx = B.sx + mx;
    B.sy = B.sy + my;
    A.sx = A.sx - mx;
    A.sy = A.sy - my;
}

// After a point-wall impulse, a point the advance left beyond the wall's line goes back onto it (the pose
// moves, start-world points recomputed).
void correct_point(Body& b) {
    const WallPt* W = b.course->walls();
    F ax = W[b.wseg].x, ay = -W[b.wseg].z;
    const Point& q = b.p[size_t(b.wk)];
    F dx = q.sx - ax, dy = q.sy - ay;
    if (!(kZero < (dx * b.uy) - (dy * b.ux))) return;
    F u2 = (b.ux * b.ux) + (b.uy * b.uy), u = kOne;
    if (!(u2 == kZero)) u = ((dx * b.ux) + (dy * b.uy)) / u2;
    F mx = ((u * b.ux) + ax) - q.sx, my = ((u * b.uy) + ay) - q.sy;
    b.sx = b.sx + mx;
    b.sy = b.sy + my;
    start_points(b);
}

// Each tangent the wall hold walks to: E / |E| ((1, 0) for a zero-length segment).
void set_tangent(Point& q, F ex, F ey) {
    F len = ee::sqrt((ex * ex) + (ey * ey));
    if (len == kZero) {
        q.ty = kZero;
        q.tx = kOne;
        return;
    }
    F inv = kOne / len;
    q.ty = ey * inv;
    q.tx = ex * inv;
}

} // namespace

int wall_next(const WallPt* pts, int i) { return pts[i].link >= 0 ? i + 1 : i + 1 + pts[i].link; }
int wall_prev(const WallPt* pts, int i) { return pts[i].link <= 0 ? i - 1 : i - 1 + pts[i].link; }

void build(Body& b, F a, F rear, F width, F mass, F iz, int car) {
    const Behaviour behaviour = b.behaviour;
    b = Body{};
    b.behaviour = behaviour;
    // (Rule::BodyBuild: 2002 Tokyo-Geneva's constant is one unit lower, Europe, Asia)
    const F chamfer = behaviour.rule(Rule::BodyBuild) >= Program::ConceptGenevaEu ? F::raw(0x3E4CCCCC) : kChamfer;
    F d1 = width * chamfer, d2 = width * chamfer;
    F nb = -rear, h = width * kHalf, iz10 = iz * kTen;
    b.im = kOne / mass;
    F nh = -h;
    b.ii = kOne / iz10;
    F p0y = a - d1, p5x = nh + d2, p4y = nb + d2, p6x = h - d2, p2x = nh + d1, p1x = h - d1;
    const F lx[8] = {h, p1x, p2x, nh, nh, p5x, p6x, h};
    const F ly[8] = {p0y, a, a, p0y, p4y, nb, nb, p4y};
    for (int k = 0; k < 8; ++k) {
        b.p[size_t(k)].lx = lx[k];
        b.p[size_t(k)].ly = ly[k];
    }
    b.car = car;
    b.mass = mass;
    F r2 = kZero;
    for (int k = 0; k < 8; ++k) {
        Point& q = b.p[size_t(k)];
        const Point& n = b.p[size_t((k + 1) & 7)];
        F l = ee::sqrt((q.lx * q.lx) + (q.ly * q.ly));
        q.ux = q.lx / l;
        q.uy = q.ly / l;
        Edge& e = b.e[size_t(k)];
        e.sx = q.lx;
        e.sy = q.ly;
        e.ex = n.lx;
        e.ey = n.ly;
        F ex = n.lx - q.lx, ey = n.ly - q.ly, nex = -ex;
        F len = ee::sqrt((ey * ey) + (nex * nex));
        e.nx = ey / len;
        e.ny = nex / len;
        e.d = ((-e.nx) * q.lx) - (e.ny * q.ly);
        F d = (q.lx * q.lx) + (q.ly * q.ly);
        if (r2 < d) r2 = d;
    }
    b.radius = ee::sqrt(r2);
    b.enabled = true;
}

void pair_tables(std::vector<Body>& bodies) {
    for (Body& b : bodies)
        for (size_t j = 0; j < bodies.size() && j < size_t(kMaxBodies); ++j) {
            b.rsum[j] = b.radius + bodies[j].radius;
            b.r2[j] = b.rsum[j] * b.rsum[j];
        }
}

namespace fn {

// The sweep from the committed state; the start-world points carry over from the last step's end points.
void init(Body& b, F dt) {
    b.dt = dt;
    b.te = kOne;
    b.sx = b.cx;
    b.sy = b.cy;
    b.sz = b.cz;
    b.sh = b.ch;
    b.vx = b.cvx;
    b.vy = b.cvy;
    b.vz = b.cvz;
    b.w = b.cw;
    b.front = b.rear = b.car_hit = b.wall_hit = b.sensor_hit = false;
    b.strength = kZero;
    b.ts = kZero;
    if (b.have_points) {
        for (Point& q : b.p) {
            q.sx = q.ex;
            q.sy = q.ey;
        }
    } else {
        b.have_points = true;
        start_points(b);
    }
}

// Switching car-car contact off drops the points' car flags.
void enable(Body& b, bool on) {
    if (b.enabled && !on)
        for (Point& q : b.p) q.flags &= ~2u;
    b.enabled = on;
}

// Static separation: o's start-world points inside t's outline; the one that is deepest along the line of
// centres (its distance to the boundary crossing of that line through it) moves both bodies half that each.
void separate(std::vector<Body>& bd, int it, int io) {
    Body& t = bd[size_t(it)];
    if (!t.enabled) return;
    Body& o = bd[size_t(io)];
    if (!o.enabled) return;
    if (t.hint != o.hint && !linked(t, t.hint, o.hint)) return;
    F dcx = o.sx - t.sx, dcy = o.sy - t.sy;
    if (t.r2[size_t(io)] < (dcx * dcx) + (dcy * dcy)) return;
    F best = kZero, bx = kZero, by = kZero;
    for (const Point& v : o.p) {
        bool found = false;
        int inside = 0;
        F px = kZero, py = kZero, depth = kZero;
        for (int k = 0; k < 8; ++k) {
            const Point& p0 = t.p[size_t((k + 7) & 7)];
            const Point& p1 = t.p[size_t(k)];
            F wx = v.sx - p0.sx, wy = v.sy - p0.sy, ex = p1.sx - p0.sx, ey = p1.sy - p0.sy;
            if ((ex * wy) - (ey * wx) < kZero) break;
            if (!found) {
                F a = (dcx * wy) - (dcy * wx);
                if (!(a < kZero)) {
                    F bb = (dcx * (v.sy - p1.sy)) - (dcy * (v.sx - p1.sx));
                    if (!(kZero < bb)) {
                        F den = a - bb, s = kZero;
                        if (!(den == kZero)) s = a / den;
                        found = true;
                        py = (s * ey) - wy;
                        px = (s * ex) - wx;
                        depth = (px * dcx) + (py * dcy);
                    }
                }
            }
            ++inside;
        }
        if (inside == 8 && best < depth) {
            best = depth;
            bx = px;
            by = py;
        }
    }
    if (kZero < best) {
        by = by * kHalf;
        bx = bx * kHalf;
        shift(o, bx, by);
        shift(t, -bx, -by);
    }
}

// A vertex flagged on another body's edge (start-world points) keeps its flag while it projects inside the
// edge and is not outside it, and both poses then move half the gap each; projecting inside, while the bodies approach
// the relative velocity loses its part across the edge (half each).
void persist(std::vector<Body>& bd, int i) {
    Body& me = bd[size_t(i)];
    if (!me.enabled) return;
    for (Point& q : me.p) {
        if (!(q.flags & 2)) continue;
        Body& o = bd[size_t(q.other)];
        const Point& p = o.p[size_t(q.edge)];
        const Point& n = o.p[size_t((q.edge + 1) & 7)];
        F wx = q.sx - p.sx, wy = q.sy - p.sy, ey = n.sy - p.sy, ex = n.sx - p.sx;
        F inv = kOne / ((ex * ex) + (ey * ey));
        F u = ((wx * ex) + (wy * ey)) * inv;
        if (u < kZero || kOne < u) {
            q.flags &= ~2u;
            continue;
        }
        if (kZero < (wx * ey) - (wy * ex)) {
            q.flags &= ~2u; // outside the edge: the flag goes, the velocity part below still runs
        } else {
            F mx = ((ex * u) - wx) * kHalf, my = ((ey * u) - wy) * kHalf;
            me.sx = me.sx + mx;
            me.sy = me.sy + my;
            o.sx = o.sx - mx;
            o.sy = o.sy - my;
        }
        F rvx = me.vx - o.vx, rvy = me.vy - o.vy;
        if ((rvx * ey) - (rvy * ex) < kZero) {
            F f = ((rvx * ex) + (rvy * ey)) * inv;
            F ax = ((ex * f) - rvx) * kHalf, ay = ((ey * f) - rvy) * kHalf;
            me.vx = me.vx + ax;
            me.vy = me.vy + ay;
            o.vx = o.vx - ax;
            o.vy = o.vy - ay;
        }
    }
}

// A point flagged on a wall walks to the neighbouring segment it projects onto (one step), then, while the
// car moves into the wall and the point is beyond it, the pose moves the point back onto the line, the velocity
// becomes its projection on the body's wall tangent and the yaw rate loses the moment of that change.
void hold(Body& b) {
    if (!b.course) return;
    const WallPt* W = b.course->walls();
    for (Point& q : b.p) {
        if (!(q.flags & 1)) continue;
        int v = q.wall;
        F dx = q.sx - W[v].x, dy = q.sy + W[v].z;
        F s = (dx * q.tx) + (dy * q.ty);
        int to = -1;
        if (s < kZero) {
            int pv = wall_prev(W, v);
            set_tangent(q, W[v].x - W[pv].x, (-W[v].z) + W[pv].z);
            to = pv;
        } else {
            int n = wall_next(W, v);
            if (((W[n].x - W[v].x) * q.tx) - ((W[n].z - W[v].z) * q.ty) < s) {
                int nn = wall_next(W, n);
                set_tangent(q, W[nn].x - W[n].x, (-W[nn].z) + W[n].z);
                to = n;
            }
        }
        if (to >= 0) {
            v = to;
            dx = q.sx - W[v].x;
            dy = q.sy + W[v].z;
            q.wall = v;
            s = (dx * q.tx) + (dy * q.ty);
        }
        if (!(kZero <= (b.vx * q.ty) - (b.vy * q.tx)) || !(kZero < (dx * q.ty) - (dy * q.tx))) {
            q.flags &= ~1u;
            continue;
        }
        F mx = (W[v].x + (q.tx * s)) - q.sx;
        F my = ((-W[v].z) + (q.ty * s)) - q.sy;
        b.sx = b.sx + mx;
        b.sy = b.sy + my;
        F ovx = b.vx, ovy = b.vy;
        F dot = (b.ux * b.vx) + (b.uy * b.vy);
        b.vy = dot * b.uy;
        b.vx = dot * b.ux;
        F dvx = ovx - b.vx, dvy = ovy - b.vy;
        F rx = q.sx - b.sx, ry = q.sy - b.sy;
        b.w = b.w - ((((rx * dvy) - (ry * dvx)) * b.mass) * b.ii);
    }
}

// The end pose from the start pose and the velocities over the rest of the step.
void end_pose(Body& b) {
    F f = (b.te - b.ts) * b.dt;
    b.dx = b.vx * f;
    b.dy = b.vy * f;
    b.dz = b.vz * f;
    b.dh = b.w * f;
    b.ex = b.sx + b.dx;
    b.ey = b.sy + b.dy;
    b.ez = b.sz + b.dz;
    b.eh = b.sh + b.dh;
    sweep_box(b);
    end_points(b);
}

void reset_hit(Body& b) {
    b.pair = false;
    b.sensor_t = kTwo;
    b.wpoint = false;
    b.wcorner = false;
    b.sensor = false;
}

// The body's walls: the candidates of the box of its start and end points, then its earliest point or
// corner crossing (a point first on a tie).
bool wall_sweep(Body& b) {
    F xmin = b.p[0].sx, xmax = xmin, ymin = b.p[0].sy, ymax = ymin;
    for (size_t k = 0; k < 8; ++k) {
        Point& q = b.p[k];
        min_max(q.sx, xmin, xmax);
        min_max(q.sy, ymin, ymax);
        min_max(q.ex, xmin, xmax);
        min_max(q.ey, ymin, ymax);
        q.hit = false;
        q.t = kTwo;
        b.e[k].hit = false;
        b.e[k].t = kTwo;
    }
    if (b.course) {
        static thread_local std::vector<int> cand;
        cand.clear();
        b.course->wall_candidates(xmin, -ymax, xmax, -ymin, b.hint, cand);
        const WallPt* W = b.course->walls();
        EndPoints ends;
        ends.load(b);
        for (int P : cand) sweep_segment(b, W, P, ends);
    }
    F best = kTwo;
    int hit = -1;
    for (int k = 0; k < 8; ++k) {
        const Point& q = b.p[size_t(k)];
        if (q.hit && !q.flags && q.t < best) {
            best = q.t;
            hit = k;
        }
        const Edge& e = b.e[size_t(k)];
        if (e.hit && e.t < best) {
            best = e.t;
            hit = k + 8;
        }
    }
    if (hit < 0) return false;
    b.t = ((b.te - b.ts) * best) + b.ts;
    b.wall_hit = true;
    if (hit < 8) {
        const Point& q = b.p[size_t(hit)];
        b.wpoint = true;
        b.wk = hit;
        b.wseg = q.wall;
        b.ux = q.tx;
        b.uy = q.ty;
        if (hit < 4) b.front = true;
        else b.rear = true;
    } else {
        // The program's side test reads the edge index + 8 here: corner hits set neither side.
        b.wcorner = true;
        b.we = hit - 8;
    }
    return true;
}

// B's unflagged vertices against A's edges, in A's frame at the start and at the end pose (each vertex on a
// straight line between them), after a box test and the closest approach of the two centres' straight paths. The
// earliest crossing is A's hit unless A already holds an earlier car or point-wall hit.
bool pair_sweep(std::vector<Body>& bd, int ia, int ib) {
    Body& A = bd[size_t(ia)];
    if (!A.enabled) return false;
    Body& B = bd[size_t(ib)];
    if (!B.enabled) return false;
    if (A.hint != B.hint && !linked(A, A.hint, B.hint)) return false;
    F gx = B.bx1 - A.bx0, gy = B.by1 - A.by0;
    if (gx < kZero || gy < kZero) return false;
    if ((B.bw + A.bw) < gx || (B.bh + A.bh) < gy) return false;
    F dx = B.dx - A.dx, dy = B.dy - A.dy, d2 = (dx * dx) + (dy * dy);
    F c0x = B.sx - A.sx, c0y = B.sy - A.sy;
    F f = ((-c0x) * dx) - (c0y * dy), cx = c0x, cy = c0y;
    if (!(f <= kZero)) {
        if (d2 <= f) {
            cx = c0x + dx;
            cy = c0y + dy;
        } else {
            F q = f / d2;
            cy = c0y + (dy * q);
            cx = c0x + (dx * q);
        }
    }
    if (A.r2[size_t(ib)] < (cx * cx) + (cy * cy)) return false;
    F dx1 = B.ex - A.ex, dy1 = B.ey - A.ey, rs = B.sh - A.sh, re = B.eh - A.eh;
    F s0 = ee::sin(-A.sh), c0 = ee::cos(-A.sh), s1 = ee::sin(-A.eh), c1 = ee::cos(-A.eh);
    F p0y = (s0 * c0x) + (c0 * c0y), p0x = (c0 * c0x) - (s0 * c0y);
    F p1x = (c1 * dx1) - (s1 * dy1), p1y = (s1 * dx1) + (c1 * dy1);
    F ss = ee::sin(rs), cs = ee::cos(rs), se = ee::sin(re), ce = ee::cos(re);
    F best = kTwo, cpx, cpy;
    int bv = -1, be = -1;
    for (int j = 0; j < 8; ++j) {
        if (B.p[size_t(j)].flags) continue;
        const Edge& v = B.e[size_t(j)]; // its start is vertex j
        F x0 = ((cs * v.sx) - (ss * v.sy)) + p0x, y0 = ((ss * v.sx) + (cs * v.sy)) + p0y;
        F x1 = ((ce * v.sx) - (se * v.sy)) + p1x, y1 = ((se * v.sx) + (ce * v.sy)) + p1y;
        for (int i = 0; i < 8; ++i) {
            const Edge& e = A.e[size_t(i)];
            F d1 = ((x1 * e.nx) + (y1 * e.ny)) + e.d, d0 = ((x0 * e.nx) + (y0 * e.ny)) + e.d;
            if (kZero < d1 || d0 < kZero) continue;
            F mx = x1 - x0, my = y1 - y0;
            F sa = ((e.sx - x0) * my) - ((e.sy - y0) * mx), sb = ((e.ex - x0) * my) - ((e.ey - y0) * mx);
            if (sa < kZero && sb < kZero) continue;
            if (kZero < sa && kZero < sb) continue;
            F den = d0 - d1, fr = kOne;
            if (!(den == kZero)) fr = d0 / den;
            F t = ((A.te - A.ts) * fr) + A.ts;
            if (t < kZero || kOne < t) continue;
            if (t < best) {
                best = t;
                bv = j;
                be = i;
                cpx = x0 + (mx * fr);
                cpy = y0 + (my * fr);
            }
        }
    }
    if (bv < 0) return false;
    if ((A.pair || A.wpoint) && A.t < best) return false;
    A.hit_edge = be;
    A.t = best;
    A.other = ib;
    A.vertex = bv;
    A.pair = true;
    A.cpx = cpx;
    A.cpy = cpy;
    A.car_hit = true;
    if (be < 3) A.front = true;
    else if (unsigned(be - 4) < 3) A.rear = true;
    B.car_hit = true;
    if (bv < 4) B.front = true;
    else B.rear = true;
    return true;
}

// A sensor crossing before `t` sets the body's sensor flag.
void sensor_check(Body& b, F t) {
    if (b.sensor && ((b.te - b.ts) * b.sensor_t) + b.ts < t) b.sensor_hit = true;
}

// The start pose to sweep time t, along the displacement.
void advance(Body& b, F t) {
    F span = b.te - b.ts, f = kZero;
    if (!(span == kZero)) f = (t - b.ts) / span;
    b.dx = b.dx * f;
    b.dy = b.dy * f;
    b.dz = b.dz * f;
    b.dh = b.dh * f;
    b.sx = b.sx + b.dx;
    b.sy = b.sy + b.dy;
    b.sz = b.sz + b.dz;
    b.sh = b.sh + b.dh;
    b.ts = t;
    start_points(b);
}

// Point wk on wall wseg (tangent u, normal
// n = (-u.y, u.x)): j = -1.05 (v_p . n) / K, friction, the point flagged on the wall, the moment of J alone.
void point_response(Body& b, Trace* trace) {
    F cs = ee::cos(b.sh), sn = ee::sin(b.sh), msn = -sn;
    const F r[4] = {cs, msn, sn, cs};
    const Edge& e = b.e[size_t(b.wk)]; // starts at point wk
    F nlx = -e.sx;
    F rpy = (cs * e.sy) + (msn * nlx);
    F vpx = b.vx - (b.w * rpy);
    F nrx = (sn * e.sy) + (cs * nlx);
    F vpy = b.vy - (b.w * nrx);
    F nx = -b.uy, ny = b.ux;
    F lnx = (r[0] * nx) + (r[2] * ny), lny = (r[1] * nx) + (r[3] * ny);
    F rwy = (r[0] * e.sy) + (r[1] * nlx), nrwx = (r[2] * e.sy) + (r[3] * nlx);
    F k = b.im + (((rwy * nx) + (nrwx * ny)) * ((-((e.sx * lny) - (e.sy * lnx))) * b.ii));
    F j = kZero;
    if (!(k == kZero)) j = (((vpx * nx) + (vpy * ny)) * kPointBounce) / k;
    F jx = j * nx, jy = j * ny;
    b.strength = b.strength + (neg_if_negative(j) / kPointStrength);
    F fx, fy, fz;
    friction(b, jx, jy, vpx, vpy, fx, fy, fz);
    F jlx = (r[0] * jx) + (r[2] * jy), jly = (r[1] * jx) + (r[3] * jy);
    F sx = jx + fx, sy = jy + fy;
    correct_point(b);
    b.vx = b.vx + (sx * b.im);
    b.vy = b.vy + (sy * b.im);
    b.p[size_t(b.wk)].flags |= 1;
    vertical(b, fz);
    b.w = b.w + (((e.sx * jly) - (e.sy * jlx)) * b.ii);
    if (trace) {
        const Point& q = b.p[size_t(b.wk)];
        trace->push_back({Response::Point, b.car, -1, b.wk, 0, q.sx.value(), q.sy.value(), nx.value(), ny.value(),
                          neg_if_negative(j).value(), b.ts.value(), neg_if_negative(j)});
    }
}

// A wall corner on edge we: n = the edge's normal (world), the
// arm from the CoG to the corner, j = -1.05 (v_p . n) / K, friction; no flag, no correction.
void corner_response(Body& b, Trace* trace) {
    F cs = ee::cos(b.sh), sn = ee::sin(b.sh);
    const Edge& e = b.e[size_t(b.we)];
    const WallPt* W = b.course->walls();
    F rx = W[e.wall].x - b.sx, ry = (-W[e.wall].z) - b.sy;
    F vpy = b.vy + (b.w * rx), vpx = b.vx - (b.w * ry);
    F nx = (cs * e.nx) + ((-sn) * e.ny), ny = (sn * e.nx) + (cs * e.ny);
    F cr = (rx * ny) - (ry * nx);
    F k = b.im + ((cr * cr) * b.ii);
    F j = kZero;
    if (!(k == kZero)) j = (((vpx * nx) + (vpy * ny)) * kCornerBounce) / k;
    F jx = j * nx, jy = j * ny;
    b.strength = b.strength + (neg_if_negative(j) / kCornerStrength);
    F fx, fy, fz;
    friction(b, jx, jy, vpx, vpy, fx, fy, fz);
    F tq = (rx * jy) - (ry * jx);
    F sx = jx + fx, sy = jy + fy;
    b.vx = b.vx + (sx * b.im);
    b.vy = b.vy + (sy * b.im);
    b.w = b.w + (tq * b.ii);
    vertical(b, fz);
    if (trace)
        trace->push_back({Response::Corner, b.car, -1, b.we, 0, W[e.wall].x.value(), -W[e.wall].z.value(), nx.value(), ny.value(),
                          neg_if_negative(j).value(), b.ts.value(), neg_if_negative(j)});
}

// B's vertex on A's edge: the impulse, the touch-up, A's and B's
// velocities and moments, B's vertex flagged on A's edge.
void pair_response(std::vector<Body>& bd, int ia, Trace* trace) {
    Body& A = bd[size_t(ia)];
    Body& B = bd[size_t(A.other)];
    Point& bp = B.p[size_t(A.vertex)];
    F ca = ee::cos(A.sh), sa = ee::sin(A.sh);
    const F ra[4] = {ca, -sa, sa, ca};
    F cb = ee::cos(B.sh), sb = ee::sin(B.sh);
    const F rb[4] = {cb, -sb, sb, cb};
    F jx, jy;
    F j = pair_impulse(A, B, ra, rb, jx, jy);
    F s = neg_if_negative(j) / kPairStrength;
    A.strength = A.strength + s;
    B.strength = B.strength + s;
    touch_up(A, B, ra, rb);
    F jlx = (ra[0] * jx) + (ra[2] * jy), jly = (ra[1] * jx) + (ra[3] * jy);
    A.vy = A.vy + (jy * A.im);
    A.vx = A.vx + (jx * A.im);
    A.w = A.w + (((A.cpx * jly) - (A.cpy * jlx)) * A.ii);
    bp.flags |= 2;
    bp.other = A.car;
    bp.edge = A.hit_edge;
    B.vx = B.vx - (jx * B.im);
    B.vy = B.vy - (jy * B.im);
    F jbx = (rb[0] * jx) + (rb[2] * jy), jby = (rb[1] * jx) + (rb[3] * jy);
    B.w = B.w - (((bp.lx * jby) - (bp.ly * jbx)) * B.ii);
    if (trace) {
        const Edge& e = A.e[size_t(A.hit_edge)];
        const double c = ca.value(), s = sa.value(), px = A.cpx.value(), py = A.cpy.value(), nx = e.nx.value(), ny = e.ny.value();
        trace->push_back({Response::Car, A.car, B.car, A.hit_edge, A.vertex, A.sx.value() + c * px - s * py, A.sy.value() + s * px + c * py,
                          c * nx - s * ny, s * nx + c * ny, neg_if_negative(j).value(), A.ts.value(), neg_if_negative(j)});
    }
}

// A car nearly at rest whose last hit came early in the step turns by its steering: yaw rate +=
// steer / 100 x 20 x dt, end pose recomputed. (Rule::StuckNudge: the later builds', Concept 2001 Tokyo, only for a
// pad car (control source 0) and only while it touches something: a point or corner hit, a wall hit, a point held on a
// wall or crossing one this round, an edge's corner crossing.)
void stuck(Body& b, F last) {
    if (!sim::exact::gt3_program(b.behaviour.rule(Rule::StuckNudge))) {
        if (b.control != 0) return;
        bool touch = b.wpoint || b.wcorner || b.wall_hit;
        for (size_t k = 0; k < 8 && !touch; ++k) touch = (b.p[k].flags & 1) || b.p[k].hit || b.e[k].hit;
        if (!touch) return;
    }
    if (!(ee::abs(b.vx) < kOne) || !(ee::abs(b.vy) < kOne) || !(last < sim::exact::folded(b.behaviour, kEarly.bits, 0x3DCCCCCC))) return; // (Europe's)
    b.w = b.w + (((ee::int_to_f(b.steer) / kHundred) * kTwenty) * b.dt);
    end_pose(b);
}

// The end-of-step push-out against the candidates of the box of end points 0-6.
void push_out(Body& b) {
    F xmin = b.p[0].ex, xmax = xmin, ymin = b.p[0].ey, ymax = ymin;
    for (size_t k = 0; k < 7; ++k) {
        min_max(b.p[k].ex, xmin, xmax);
        min_max(b.p[k].ey, ymin, ymax);
    }
    if (!b.course) return;
    static thread_local std::vector<int> cand;
    cand.clear();
    b.course->wall_candidates(xmin, -ymax, xmax, -ymin, b.hint, cand);
    const WallPt* W = b.course->walls();
    EndPoints ends;
    ends.load(b);
    for (int P : cand)
        if (push_segment(b, W, P, ends)) ends.load(b); // (the end pose moved)
}

void commit(Body& b) {
    b.cx = b.ex;
    b.cy = b.ey;
    b.cz = b.ez;
    b.cvx = b.vx;
    b.cvy = b.vy;
    b.cvz = b.vz;
    if (b.behaviour.rule(Rule::CollisionCommit) == Program::NtscJ) { // the NTSC-J program: no wrap, no clamp
        b.ch = b.eh;
        b.cw = b.w;
        return;
    }
    b.ch = wrap_heading(b.eh, b.behaviour);
    b.cw = clamp_yaw(b.w, b.behaviour);
}

// The hit sound's hold (60 steps from a stronger hit, its level decaying by 0.98 a step otherwise), the strength
// capped at 1, the rumble (24 steps from a stronger hit, then the car's value cleared). Its calls are given
// kind -1, which does nothing.
void effects(Body& b) {
    // PAL's 50 steps a second: 20; 2002 Tokyo-Geneva's one step shorter (Europe: 19, Asia: 23)
    const s32 rumble_count = (b.behaviour.pal_rate() ? 20 : 24) - (sim::exact::toward_zero(b.behaviour) ? 1 : 0);
    F s = b.strength;
    const bool louder = kZero < s && (b.sound_steps == 0 || b.sound_level < s);
    if (louder) {
        b.sound_steps = 60;
        b.sound_level = b.strength;
    } else {
        s32 n = b.sound_steps - 1;
        b.sound_steps = n < 0 ? 0 : n;
        b.sound_level = b.sound_level * sim::exact::folded(b.behaviour, kSoundDecay.bits, 0x3F7AE147); // (Europe's)
    }
    s = b.strength;
    if (kOne < s) b.strength = s = kOne;
    if (b.rumble_steps > 0) {
        if (b.rumble_level < s) {
            if (b.rumble && *b.rumble < s) *b.rumble = s;
            b.rumble_level = s;
            b.rumble_steps = rumble_count;
        } else if (--b.rumble_steps == 0 && b.rumble) {
            *b.rumble = kZero;
        }
    } else if (kZero < s) {
        if (b.rumble) *b.rumble = s;
        b.rumble_steps = rumble_count;
        b.rumble_level = b.strength;
    }
}

// The heading less whole turns once it is 8 turns or more from zero.
// (2002 Tokyo-Geneva's pi and 2 pi one unit lower: Europe)
F wrap_heading(F h, const Behaviour& bh) {
    const F pi = sim::exact::folded(bh, kPi.bits, 0x40490FDA), two_pi = sim::exact::folded(bh, kTwoPi.bits, 0x40C90FDA);
    const F minus_two_pi = sim::exact::folded(bh, kMinusTwoPi.bits, 0xC0C90FDA);
    s32 n = ee::to_int((ee::abs(h) + pi) / two_pi);
    if (n < 8) return h;
    F nf = ee::int_to_f(n);
    return h < kZero ? h + (nf * two_pi) : h + (nf * minus_two_pi);
}

} // namespace fn

void step_all(std::vector<Body>& bd, Trace* trace, int rounds) {
    using namespace fn;
    const int n = int(bd.size());
    for (Body& b : bd) {
        init(b, b.dt_in);
        enable(b, !b.ghost);
    }
    F last = kOne;
    for (int round = 0; round < rounds && round < 2;) {
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                if (i != j) separate(bd, i, j);
        for (int i = 0; i < n; ++i) {
            persist(bd, i);
            hold(bd[size_t(i)]);
        }
        for (Body& b : bd) end_pose(b);
        int hit = -1;
        F best = kTwo;
        for (int i = 0; i < n; ++i) {
            Body& b = bd[size_t(i)];
            reset_hit(b);
            if (wall_sweep(b) && b.t < best) {
                best = b.t;
                hit = i + 0x200;
            }
            for (int j = 0; j < n; ++j)
                if (j != i) pair_sweep(bd, i, j);
            if (b.pair && b.t < best) {
                best = b.t;
                hit = i;
            }
        }
        // The program's sensor check loop here reads the body after the last one (its loop index ends at the
        // count): no body's sensor flag is set by this step.
        if (hit < 0) break;
        last = best;
        for (Body& b : bd) advance(b, best);
        ++round;
        if (hit & 0x200) {
            Body& b = bd[size_t(hit & ~0x200)];
            if (b.wpoint) point_response(b, trace);
            else corner_response(b, trace);
        } else {
            pair_response(bd, hit, trace);
        }
    }
    for (Body& b : bd) {
        stuck(b, last);
        push_out(b);
        commit(b);
        effects(b);
    }
}

void step_walls(std::vector<Body>& bd, int i, Trace* trace, int rounds) {
    using namespace fn;
    Body& b = bd[size_t(i)];
    init(b, b.dt_in);
    enable(b, false);
    F last = kOne;
    for (int round = 0; round < rounds && round < 2; ++round) {
        persist(bd, i);
        hold(b);
        end_pose(b);
        reset_hit(b);
        if (!wall_sweep(b)) {
            sensor_check(b, kTwo);
            break;
        }
        last = b.t;
        sensor_check(b, last);
        advance(b, last);
        if (b.wpoint) point_response(b, trace);
        else corner_response(b, trace);
    }
    stuck(b, last);
    push_out(b);
    commit(b);
    effects(b);
}

} // namespace gt3r::sim::collide::exact
