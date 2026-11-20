#include "formats/course/ai_lines.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gt3r::course {

AiLines AiLines::parse(ByteSpan d) {
    if (d.size() < 0x28) throw FormatError("AI lines: section too small");
    u32 size = rd32(d, 0), version = rd32(d, 4);
    if ((version != 7 && version != 8) || size > d.size() || size < 0x28) throw FormatError("AI lines: bad header");
    AiLines out;
    // Version 8 has the tool's build stamp after the offsets; version 7's first line starts there.
    if (version == 8) {
        if (size < 0x48) throw FormatError("AI lines: section too small");
        for (size_t i = 0x28; i < 0x48 && d[i] >= 0x20; ++i) out.build.push_back(char(d[i]));
    }
    for (int li = 0; li < 7; ++li) {
        AiLine line;
        u32 off = rd32(d, 0x0C + 4 * li);
        if (off) {
            if (off + 4 > size) throw FormatError("AI lines: line offset out of range");
            u32 n = rd32(d, off);
            if (u64(n) * 40 > size - off - 4) throw FormatError("AI lines: line too long");
            for (u32 k = 0; k < n; ++k) {
                size_t p = off + 4 + size_t(k) * 40;
                AiPoint pt;
                pt.flags = rd32(d, p);
                pt.pos.x = -float(s32(rd32(d, p + 4))) / 4096.0f;
                pt.pos.z = float(s32(rd32(d, p + 8))) / 4096.0f;
                pt.segment = rd32(d, p + 16);
                pt.lateral = float(s32(rd32(d, p + 24))) / 4096.0f;
                pt.packed = rd32(d, p + 36);
                pt.raw_x = s32(rd32(d, p + 4)), pt.raw_z = s32(rd32(d, p + 8)), pt.raw_radius = s32(rd32(d, p + 24));
                // Sim-frame heading (1/4096 turn); mirroring X negates the angle.
                pt.heading = -float(s16(pt.packed & 0xFFFF)) * float(2 * std::numbers::pi / 4096.0);
                line.points.push_back(pt);
            }
            if (line.points.size() >= 4) {
                const Vec3 &a = line.points.front().pos, &b = line.points.back().pos;
                line.closed = std::hypot(a.x - b.x, a.z - b.z) < 250.0f;
            }
        }
        out.lines.push_back(std::move(line));
    }
    return out;
}

void AiLines::project(const Road& road) {
    for (AiLine& l : lines)
        for (AiPoint& p : l.points)
            if (auto y = road.height_at(p.pos.x, p.pos.z)) p.pos.y = *y;
}

std::vector<Vec3> AiLine::sample(float step) const {
    std::vector<Vec3> out;
    size_t n = points.size();
    if (n < 2) {
        for (const AiPoint& p : points) out.push_back(p.pos);
        return out;
    }
    auto at = [&](long i) -> const Vec3& {
        if (closed) return points[size_t((i % long(n) + long(n)) % long(n))].pos;
        return points[size_t(std::clamp<long>(i, 0, long(n) - 1))].pos;
    };
    size_t spans = closed ? n : n - 1;
    for (size_t i = 0; i < spans; ++i) {
        const Vec3 &p0 = at(long(i) - 1), &p1 = at(long(i)), &p2 = at(long(i) + 1), &p3 = at(long(i) + 2);
        float len = std::hypot(p2.x - p1.x, p2.z - p1.z);
        int steps = std::max(1, int(len / step));
        for (int s = 0; s < steps; ++s) {
            float t = float(s) / float(steps), t2 = t * t, t3 = t2 * t;
            auto cr = [&](float a, float b, float c, float e) {
                return 0.5f * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - e) * t2 + (-a + 3 * b - 3 * c + e) * t3);
            };
            out.push_back({cr(p0.x, p1.x, p2.x, p3.x), cr(p0.y, p1.y, p2.y, p3.y), cr(p0.z, p1.z, p2.z, p3.z)});
        }
    }
    if (!closed) out.push_back(points.back().pos);
    return out;
}

} // namespace gt3r::course

namespace gt3r::course {

TrackFrame::TrackFrame(const Road& road) {
    points = road.centre_line();
    float acc = 0;
    for (size_t i = 0; i < points.size(); ++i) {
        if (i) acc += std::hypot(points[i].x - points[i - 1].x, points[i].z - points[i - 1].z);
        s.push_back(acc);
    }
    if (!points.empty()) acc += std::hypot(points.front().x - points.back().x, points.front().z - points.back().z);
    length = acc;
}

void TrackFrame::project(float x, float z, float& s_out, float& d_out) const {
    float best = 1e30f;
    s_out = d_out = 0;
    size_t n = points.size();
    for (size_t i = 0; i < n; ++i) {
        const Vec3 &a = points[i], &b = points[(i + 1) % n];
        float dx = b.x - a.x, dz = b.z - a.z, l2 = dx * dx + dz * dz;
        if (l2 < 1e-9f) continue;
        float t = std::clamp(((x - a.x) * dx + (z - a.z) * dz) / l2, 0.f, 1.f);
        float px = a.x + dx * t, pz = a.z + dz * t;
        float dist2 = (x - px) * (x - px) + (z - pz) * (z - pz);
        if (dist2 < best) {
            best = dist2;
            float l = std::sqrt(l2);
            s_out = s[i] + t * l;
            // Left of travel direction for right-handed Y-up: (dz, -dx) x ... use cross product sign.
            float cross = dx * (z - pz) - dz * (x - px);
            d_out = (cross >= 0 ? -1.f : 1.f) * std::sqrt(dist2);
        }
    }
}

Vec3 TrackFrame::at(float sq, float d) const {
    size_t n = points.size();
    if (n < 2 || length <= 0) return {};
    sq = std::fmod(sq, length);
    if (sq < 0) sq += length;
    size_t i = size_t(std::upper_bound(s.begin(), s.end(), sq) - s.begin());
    i = i ? i - 1 : 0;
    const Vec3 &a = points[i], &b = points[(i + 1) % n];
    float seg = (i + 1 < n ? s[i + 1] : length) - s[i];
    float t = seg > 0 ? (sq - s[i]) / seg : 0;
    float dx = b.x - a.x, dz = b.z - a.z, l = std::max(1e-6f, std::hypot(dx, dz));
    // Inverse of project(): positive d is the side where cross < 0.
    float nx = dz / l, nz = -dx / l;
    return {a.x + dx * t + nx * d, 0, a.z + dz * t + nz * d};
}

std::vector<Vec3> AiLine::sample_arcs(float step, bool loop) const {
    constexpr float kPi = std::numbers::pi_v<float>;
    std::vector<Vec3> out;
    size_t n = points.size();
    if (n == 0) return out;
    if (n == 1 || step <= 0) {
        for (const AiPoint& p : points) out.push_back({p.pos.x, 0, p.pos.z});
        return out;
    }
    size_t spans = loop ? n : n - 1;
    float dir_x = 0, dir_z = 0;
    bool have_dir = false;
    for (size_t i = 0; i < spans; ++i) {
        const AiPoint &p = points[i], &q = points[(i + 1) % n];
        float cx = q.pos.x - p.pos.x, cz = q.pos.z - p.pos.z, c = std::hypot(cx, cz);
        if (c < 1e-3f) continue;
        float r = p.radius();
        if (std::fabs(r) > 0.5f * c) {
            float h = std::sqrt(r * r - 0.25f * c * c);
            float lx = cz / c, lz = -cx / c; // left of the chord (world frame, Y up)
            float side = r > 0 ? 1.f : -1.f, rr = std::fabs(r);
            float best_ox = 0, best_oz = 0, best_a0 = 0, best_da = 0, best_dot = -1e30f;
            for (int major = 0; major < 2; ++major) {
                float sd = major ? -side : side;
                float ox = p.pos.x + cx * 0.5f + lx * h * sd, oz = p.pos.z + cz * 0.5f + lz * h * sd;
                float a0 = std::atan2(p.pos.z - oz, p.pos.x - ox), a1 = std::atan2(q.pos.z - oz, q.pos.x - ox);
                float da = a1 - a0;
                while (da > kPi) da -= 2 * kPi;
                while (da < -kPi) da += 2 * kPi;
                if (major) da += da > 0 ? -2 * kPi : 2 * kPi;
                float g = da > 0 ? 1.f : -1.f;
                float tx = -std::sin(a0) * g, tz = std::cos(a0) * g;
                float dot = have_dir ? tx * dir_x + tz * dir_z : (major ? -1.f : 1.f);
                if (dot > best_dot) best_dot = dot, best_ox = ox, best_oz = oz, best_a0 = a0, best_da = da;
            }
            int steps = std::max(1, int(std::fabs(best_da) * rr / step));
            for (int k = 0; k < steps; ++k) {
                float ang = best_a0 + best_da * float(k) / float(steps);
                out.push_back({best_ox + rr * std::cos(ang), 0, best_oz + rr * std::sin(ang)});
            }
            float ae = best_a0 + best_da, g = best_da > 0 ? 1.f : -1.f;
            dir_x = -std::sin(ae) * g, dir_z = std::cos(ae) * g;
        } else {
            int steps = std::max(1, int(c / step));
            for (int k = 0; k < steps; ++k) {
                float t = float(k) / float(steps);
                out.push_back({p.pos.x + cx * t, 0, p.pos.z + cz * t});
            }
            dir_x = cx / c, dir_z = cz / c;
        }
        have_dir = true;
    }
    if (!loop) out.push_back({points.back().pos.x, 0, points.back().pos.z});
    return out;
}

std::vector<Vec3> AiLine::sample(const Road& road, float step) const {
    size_t n = points.size();
    bool loop = closed;
    if (!loop && n >= 3) {
        // Lines whose ends are far apart (a long straight without points)
        // still loop when the points cover most of the lap.
        TrackFrame f(road);
        if (f.length > 0) {
            float covered = 0, s_prev = 0, d, s_first = 0;
            for (size_t i = 0; i < n; ++i) {
                float s;
                f.project(points[i].pos.x, points[i].pos.z, s, d);
                if (i) covered += std::fmod(s - s_prev + f.length, f.length);
                else s_first = s;
                s_prev = s;
            }
            loop = covered > 0.6f * f.length && std::fmod(s_first - s_prev + f.length, f.length) < 0.4f * f.length;
        }
    }
    std::vector<Vec3> out = sample_arcs(step, loop);
    float prev_y = 0;
    bool have = false;
    for (Vec3& p : out) {
        std::optional<float> y;
        if (have) y = road.height_at(p.x, p.z, prev_y + 3.0f);
        if (!y) y = road.height_at(p.x, p.z);
        p.y = y ? *y : prev_y;
        prev_y = p.y;
        have = true;
    }
    return out;
}

} // namespace gt3r::course
