#include "formats/model/gtm1.h"

#include "formats/clut_animation.h"
#include "formats/gs_memory.h"
#include "formats/tex1.h"
#include "formats/texture_pack.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <tuple>

namespace gt3r::gtm1 {

namespace {

float rdf(ByteSpan d, size_t o) {
    u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

constexpr u32 kNoTexset = 0xFFFFFFFFu;
constexpr Matrix kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// a * b with row vectors (apply a, then b).
Matrix mul(const Matrix& a, const Matrix& b) {
    Matrix r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
            r[i * 4 + j] = s;
        }
    return r;
}

Matrix rotation(float ax, float ay, float az, float radians) {
    float len = std::sqrt(ax * ax + ay * ay + az * az);
    if (len == 0) return kIdentity;
    ax /= len, ay /= len, az /= len;
    float c = std::cos(radians), s = std::sin(radians), t = 1 - c;
    // Row-vector form of the standard axis-angle matrix (transpose of the column form).
    return {t * ax * ax + c,      t * ax * ay + s * az, t * ax * az - s * ay, 0,
            t * ax * ay - s * az, t * ay * ay + c,      t * ay * az + s * ax, 0,
            t * ax * az + s * ay, t * ay * az - s * ax, t * az * az + c,      0,
            0, 0, 0, 1};
}

// Operand bytes after each opcode for the fixed-size ops. -1 = variable or
// control flow handled explicitly, -2 = unknown.
int fixed_operand_bytes(u8 op) {
    switch (op) {
    case 0x01: case 0x0B: case 0x0C: case 0x0D: case 0x17: case 0x18: case 0x1A: case 0x1B: case 0x1D: case 0x1E:
    case 0x22: case 0x23: case 0x25: case 0x26: case 0x2D: case 0x2E: case 0x30:
        return 0;
    case 0x02: case 0x04: case 0x0E: case 0x19: case 0x1F: case 0x29: case 0x2A: case 0x2B: case 0x2F:
        return 1;
    case 0x03: case 0x05: case 0x1C: case 0x20: case 0x2C:
        return 2;
    case 0x14: case 0x15: case 0x16: case 0x21: case 0x24: case 0x27: case 0x32: case 0x34: case 0x35: case 0x36:
        return 4;
    case 0x11: case 0x12: case 0x31:
        return 12;
    case 0x13: case 0x28: case 0x33: case 0x37:
        return 16;
    case 0x0F: case 0x10:
        return 64;
    case 0x00: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A:
        return -1;
    default:
        return -2;
    }
}

// A GS vertex colour in our 0..255 terms, 0x80 = 1.0: alpha is a blend weight, RGB a factor (a
// texture's MODULATE, or the VU's lighting / reflection maths). `rgb_as_is`: the RGB bytes as
// they are: a pre-lit packet's colour that the VU passes straight to an untextured primitive,
// which the GS shows as it is (0x80 = mid grey: the licence course line's coasting dashes), or a
// packet with a factor past 0x80 (DecodedPacket::rgb_2x: the renderer applies 255/128).
u32 expand_color(const std::array<u32, 4>& c, bool rgb_as_is) {
    auto ch = [](u32 v) { return std::min<u32>(255, (v * 255 + 64) / 128); };
    auto rgb = [&](u32 v) { return rgb_as_is ? std::min<u32>(255, v) : ch(v); };
    return rgb(c[0]) | rgb(c[1]) << 8 | rgb(c[2]) << 16 | ch(c[3]) << 24;
}

} // namespace

bool Gtm1::is_gtm1(ByteSpan data) { return has_magic(data, "GTM1") && data.size() >= 0x40; }

Gtm1 Gtm1::parse(ByteSpan data) {
    if (!is_gtm1(data)) throw FormatError("not a GTM1 model");
    Gtm1 g;
    g.data = data;
    u32 node_count = rd16(data, 0x10), chunk_count = rd16(data, 0x12), texset_count = rd16(data, 0x16);
    u32 tex_lods = rd16(data, 0x18);
    u32 node_tab = rd32(data, 0x20), chunk_tab = rd32(data, 0x24), texset_tab = rd32(data, 0x2C);
    u32 sphere_tab = rd32(data, 0x30), lod_tab = rd32(data, 0x34);

    for (u32 i = 0; i < node_count; ++i) {
        g.nodes.push_back(rd32(data, node_tab + 4 * i));
        std::array<float, 4> s{0, 0, 0, 0};
        if (sphere_tab)
            for (int k = 0; k < 4; ++k) s[k] = rdf(data, sphere_tab + 16 * i + 4 * k);
        g.spheres.push_back(s);
    }

    u32 mat_count = rd16(data, 0x14), mat_tab = rd32(data, 0x28);
    auto read_materials = [&](u32 tab) {
        std::vector<Material> out;
        for (u32 i = 0; tab && i < mat_count; ++i) {
            Material m;
            size_t o = tab + 0x50 * size_t(i);
            if (o + 0x48 > data.size()) throw FormatError("GTM1 material out of range");
            for (int k = 0; k < 16; ++k) m.rows[k / 4][k % 4] = rdf(data, o + 4 * k);
            m.shininess = rdf(data, o + 0x40);
            m.flags = rd32(data, o + 0x44);
            out.push_back(m);
        }
        return out;
    };
    g.materials = read_materials(mat_tab);
    // Per-paint material sets: +0x38 -> u32 offsets[count at +0x1A], each a full array.
    u32 paint_sets = rd16(data, 0x1A), paint_tab = rd32(data, 0x38);
    for (u32 i = 0; paint_tab && i < paint_sets && paint_tab + 4 * i + 4 <= data.size(); ++i)
        g.paint_materials.push_back(read_materials(rd32(data, paint_tab + 4 * i)));
    for (u32 i = 0; i < chunk_count; ++i) {
        Chunk c;
        c.offset = rd32(data, chunk_tab + 4 * i);
        c.flags = rd16(data, c.offset + 8);
        u32 n = rd16(data, c.offset + 10);
        c.vertex_count = rd16(data, c.offset + 12);
        c.triangle_count = rd16(data, c.offset + 14);
        for (u32 k = 0; k < n; ++k) {
            size_t e = c.offset + 16 + 8 * size_t(k);
            Packet p;
            p.offset = c.offset + rd32(data, e);
            p.qwc = rd16(data, e + 4);
            p.mat = rd16(data, e + 6);
            if (p.offset > data.size() || size_t(p.qwc) * 16 > data.size() - p.offset)
                throw FormatError("GTM1 packet out of range");
            c.packets.push_back(p);
        }
        g.chunks.push_back(std::move(c));
    }

    // +0x40 skeleton: 4x4 matrices up to the next block of the file.
    if (u32 bones = data.size() >= 0x44 ? rd32(data, 0x40) : 0; bones && bones < data.size()) {
        u32 end = u32(data.size());
        for (u32 o : {node_tab, chunk_tab, mat_tab, texset_tab, sphere_tab, lod_tab, paint_tab, rd32(data, 0x3C)})
            if (o > bones) end = std::min(end, o);
        for (const Chunk& c : g.chunks)
            if (c.offset > bones) end = std::min(end, c.offset);
        for (u32 o = bones; o + 64 <= end; o += 64) {
            Matrix m;
            for (int k = 0; k < 16; ++k) m[size_t(k)] = rdf(data, o + 4 * size_t(k));
            g.inverse_bind.push_back(m);
        }
    }

    // +0x3C animation list: u32 count, u32 entry offsets[count].
    if (u32 list = rd32(data, 0x3C); list && list <= data.size() - 4) {
        u32 n = rd32(data, list);
        for (u32 k = 0; k < n && list + 8 + 4ull * k <= data.size(); ++k) {
            u32 e = rd32(data, list + 4 + 4 * k);
            if (e > data.size() - 0x10) throw FormatError("GTM1 animator out of range");
            Animator a;
            a.type = rd16(data, e);
            u32 count = rd16(data, e + 2);
            a.speed = rdf(data, e + 4);
            u32 targets = e + 0x10;
            if (a.type == 5) {
                a.first = rd16(data, e + 8), a.end = rd16(data, e + 10);
                a.start = rdf(data, e + 12);
                targets = e + 0x14;
            } else {
                a.start = rdf(data, e + 8);
            }
            if (targets + 4ull * count > data.size()) throw FormatError("GTM1 animator out of range");
            for (u32 j = 0; j < count; ++j) {
                u32 t = rd32(data, targets + 4 * j);
                if (t > data.size() - 4) throw FormatError("GTM1 animator target out of range");
                a.targets.push_back(t);
            }
            g.animators.push_back(std::move(a));
        }
    }

    auto set_index = [&](u32 off) -> u32 {
        if (off == 0) return kNoTexset; // unused slot (e.g. wheel/pd0004)
        auto it = std::find(g.texsets.begin(), g.texsets.end(), off);
        if (it != g.texsets.end()) return u32(it - g.texsets.begin());
        // No magic check: the game sets up whatever the offset holds (0x224600 -> 0x248DA8), and a
        // zeroed header (courses/EU/L_apricot1 slot 23) is a set without textures.
        if (off >= data.size()) throw FormatError("GTM1 texture set out of range");
        g.texsets.push_back(off);
        return u32(g.texsets.size() - 1);
    };
    std::vector<u32> base;
    for (u32 i = 0; i < texset_count; ++i) base.push_back(set_index(rd32(data, texset_tab + 4 * i)));
    g.texset_tables.push_back(base);
    if (lod_tab)
        for (u32 v = 0; v < tex_lods; ++v) {
            u32 tab = rd32(data, lod_tab + 4 * v);
            std::vector<u32> t;
            for (u32 i = 0; i < texset_count; ++i) t.push_back(set_index(rd32(data, tab + 4 * i)));
            if (v == 0) g.texset_tables[0] = t;
            else g.texset_tables.push_back(t);
        }
    return g;
}

std::vector<Draw> Gtm1::run(u32 node, const RunParams& params, Draw* state) const {
    std::vector<Matrix> stack{kIdentity};
    std::vector<Draw> out;
    run_at(node, params, stack, out, 0, state, kNoBranches);
    return out;
}

void Gtm1::run_at(u32 node, const RunParams& p, std::vector<Matrix>& stack, std::vector<Draw>& out, int depth,
                  Draw* state, const CallbackBranches& entered) const {
    if (node >= nodes.size()) throw FormatError("GTM1 node index out of range");
    if (depth > 16) throw FormatError("GTM1 node call depth exceeded");
    const std::vector<u32>& sets = texset_tables[std::clamp<size_t>(size_t(std::max(p.texture_lod, 0)), 0,
                                                                     texset_tables.size() - 1)];
    size_t pc = nodes[node];
    u32 texset = sets.empty() ? 0 : sets[0];
    bool matrix_used = false;
    u8 alpha_mode = 0x44;
    u32 fb_mask = 0;
    // Entry state: car/wheel programs end by restoring ATE on, (GREATER, 0x20), DATE off, so that is the
    // convention they are entered with.
    bool ate = true, date = false, zwrite = true, ztest = true, cull = true;
    u8 atst = 6, aref = 0x20, afail = 0;
    std::array<float, 4> env_c0{1, 1, 1, 1}, env_c1{0, 0, 0, 0}; // reflection weight rows; see Draw
    if (state) { // continue from the caller's state (Options::carry_state)
        alpha_mode = state->alpha_mode, fb_mask = state->fb_mask;
        ate = state->alpha_test, date = state->dest_alpha_test, zwrite = state->depth_write;
        ztest = state->depth_test, cull = state->cull;
        atst = state->alpha_test_method, aref = state->alpha_test_ref, afail = state->alpha_fail;
        env_c0 = state->env_c0, env_c1 = state->env_c1;
    }

    bool pano_set = false;
    std::array<float, 3> pano{0, 0, 0}; // op 0x31; see Draw
    float phase = 0;                                              // op 0x34 (or a car callback id)
    u8 phase_id = 0;                                              // the callback id that set it
    CallbackBranches when = entered;                              // RunParams::every_branch: the branches taken
    // Operands the +0x3C animators rewrite: their current value when given.
    auto operand32 = [&](size_t o) {
        if (p.operands)
            if (auto it = p.operands->find(u32(o)); it != p.operands->end()) return it->second;
        return rd32(data, o);
    };
    auto operand16 = [&](size_t o) -> u32 {
        if (p.operands)
            if (auto it = p.operands->find(u32(o)); it != p.operands->end()) return it->second & 0xFFFF;
        return rd16(data, o);
    };
    auto operand_f = [&](size_t o) {
        u32 v = operand32(o);
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    };
    // What a model does not have draws nothing (a modified game's models call nodes and draw chunks they lack, and use
    // opcodes no disc's model has): a draw of a chunk past the model's into a scratch draw, a call of a node past them
    // skipped, the node's program ended at an unknown opcode.
    Draw unknown_chunk;
    auto draw = [&](u32 chunk, size_t op) -> Draw& {
        if (chunk >= chunks.size()) return unknown_chunk = Draw{};
        Draw d;
        d.chunk = chunk;
        d.op = u32(op);
        d.texset = texset;
        d.matrix = stack.back();
        d.has_matrix = matrix_used;
        d.alpha_mode = alpha_mode;
        d.fb_mask = fb_mask;
        d.alpha_test = ate;
        d.alpha_test_method = atst;
        d.alpha_test_ref = aref;
        d.dest_alpha_test = date;
        d.alpha_fail = afail;
        d.depth_write = zwrite;
        d.depth_test = ztest;
        d.cull = cull;
        d.env_c0 = env_c0;
        d.env_c1 = env_c1;
        d.pano_set = pano_set;
        d.pano = pano;
        d.when = when;
        out.push_back(d);
        return out.back();
    };
    auto s16_at = [&](size_t o) { return s32(s16(rd16(data, o))); };
    // The node's end: the render state it leaves (Options::carry_state).
    auto leave = [&] {
        if (!state) return;
        state->alpha_mode = alpha_mode, state->fb_mask = fb_mask;
        state->alpha_test = ate, state->dest_alpha_test = date, state->depth_write = zwrite;
        state->depth_test = ztest, state->cull = cull;
        state->alpha_test_method = atst, state->alpha_test_ref = aref, state->alpha_fail = afail;
        state->env_c0 = env_c0, state->env_c1 = env_c1;
    };
    // Runs the program from `from` until it reaches `until` (true) or its end (false).
    int steps = 0;
    std::function<bool(size_t, size_t)> exec = [&](size_t from, size_t until) -> bool {
    pc = from;
    for (; steps < 100000; ++steps) {
        if (pc == until) return true;
        if (pc >= data.size()) throw FormatError("GTM1 program ran off the end");
        u8 op = data[pc];
        size_t a = pc + 1; // operand address
        switch (op) {
        case 0x00: leave(); return false;
        case 0x02: if (data[a] < nodes.size()) run_at(data[a], p, stack, out, depth + 1, nullptr, when); pc = a + 1; continue;
        case 0x03: if (rd16(data, a) < nodes.size()) run_at(rd16(data, a), p, stack, out, depth + 1, nullptr, when); pc = a + 2; continue;
        case 0x04: draw(data[a], pc); pc = a + 1; continue;
        case 0x05: draw(operand16(a), pc); pc = a + 2; continue;
        case 0x06: { // u16 callback id, u8 n, s16 rel[n]; rel relative to its own field
            u32 n = data[a + 2];
            size_t tab = a + 3;
            const u32 id = rd16(data, a);
            auto target = [&](u32 k) { return tab + 2 * k + s16_at(tab + 2 * k); };
            const size_t past = tab + 2 * n; // (out of range)
            int sel = id == 0 ? (p.brake ? 1 : 0) : p.selector; // id 0: brake now
            if (p.callback && id >= 1 && id < kCallbackIds) { // a car's callback (CarCallback)
                const CarCallback& cb = *p.callback;
                // Every branch, each up to where they meet again: the jump past the table that the out-of-range value
                // takes (the car models' every switch of these ids has one).
                size_t join = SIZE_MAX;
                if (p.every_branch && callback_branches(id) && past + 1 < data.size()) {
                    if (data[past] == 0x09 && past + 2 < data.size()) join = past + 1 + size_t(s16_at(past + 1));
                    else if (data[past] == 0x08) join = past + 1 + size_t(s32(std::int8_t(data[past + 1])));
                }
                if (join != SIZE_MAX) {
                    const s8 before = when[id];
                    const auto saved = std::make_tuple(texset, matrix_used, alpha_mode, fb_mask, ate, date, zwrite, ztest, cull,
                                                       atst, aref, afail, env_c0, env_c1, pano_set, pano, stack);
                    for (u32 k = 0; k < n; ++k) {
                        std::tie(texset, matrix_used, alpha_mode, fb_mask, ate, date, zwrite, ztest, cull, atst, aref, afail,
                                 env_c0, env_c1, pano_set, pano, stack) = saved;
                        when[id] = s8(k);
                        if (callback_sets_phase(id)) phase = std::clamp(cb.phase[id], 0.0f, 1.0f), phase_id = u8(id);
                        exec(target(k), join);
                    }
                    when[id] = before;
                    pc = join;
                    continue;
                }
                if (callback_sets_phase(id)) phase = std::clamp(cb.phase[id], 0.0f, 1.0f), phase_id = u8(id);
                sel = cb.branch[id];
            }
            pc = sel >= 0 && u32(sel) < n ? target(u32(sel)) : past;
            continue;
        }
        case 0x07: { // 16-byte bounding sphere, u8 n, s16 rel[n]
            u32 n = data[a + 16];
            if (n == 0) { pc = a + 17; continue; }
            u32 k = u32(std::clamp(p.lod, 0, int(n) - 1));
            size_t f = a + 17 + 2 * k;
            pc = f + s16_at(f);
            continue;
        }
        case 0x08: pc = a + s32(std::int8_t(data[a])); continue;
        case 0x09: pc = a + s16_at(a); continue;
        case 0x0A: pc = a + 1 + 12 * size_t(data[a]) + 2; continue; // bbox cull: never culled here
        case 0x0C: stack.push_back(stack.back()); pc = a; continue;
        case 0x0D: if (stack.size() > 1) stack.pop_back(); pc = a; continue;
        case 0x0F:
        case 0x10: {
            Matrix m;
            for (int i = 0; i < 16; ++i) m[i] = rdf(data, a + 4 * i);
            stack.back() = op == 0x0F ? m : mul(m, stack.back());
            matrix_used = true;
            pc = a + 64;
            continue;
        }
        case 0x11: {
            Matrix& m = stack.back();
            float v[3] = {rdf(data, a), rdf(data, a + 4), rdf(data, a + 8)};
            for (int j = 0; j < 4; ++j) m[12 + j] += v[0] * m[j] + v[1] * m[4 + j] + v[2] * m[8 + j];
            matrix_used = true;
            pc = a + 12;
            continue;
        }
        case 0x12: {
            Matrix& m = stack.back();
            for (int r = 0; r < 3; ++r)
                for (int j = 0; j < 4; ++j) m[r * 4 + j] *= rdf(data, a + 4 * r);
            matrix_used = true;
            pc = a + 12;
            continue;
        }
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16: {
            Matrix r;
            if (op == 0x13) r = rotation(rdf(data, a), rdf(data, a + 4), rdf(data, a + 8), rdf(data, a + 12));
            else {
                float rad = operand_f(a) * 3.14159265f / 180.0f;
                r = rotation(op == 0x14, op == 0x15, op == 0x16, rad);
            }
            stack.back() = mul(r, stack.back());
            matrix_used = true;
            pc = a + (op == 0x13 ? 16 : 4);
            continue;
        }
        case 0x20: alpha_mode = data[a]; pc = a + 2; continue;
        case 0x24: fb_mask = rd32(data, a); pc = a + 4; continue;
        case 0x1A: ate = true; pc = a; continue;
        case 0x1B: ate = false; pc = a; continue;
        case 0x1C: atst = data[a] & 7; aref = data[a + 1]; pc = a + 2; continue;
        case 0x1D: date = true; pc = a; continue;
        case 0x1E: date = false; pc = a; continue;
        case 0x2F: afail = data[a] & 3; pc = a + 1; continue;
        case 0x25: zwrite = false; pc = a; continue;
        case 0x26: zwrite = true; pc = a; continue;
        case 0x17: ztest = true; pc = a; continue;
        case 0x18: ztest = false; pc = a; continue;
        case 0x2D: cull = true; pc = a; continue;
        case 0x2E: cull = false; pc = a; continue;
        case 0x2B: texset = data[a] < sets.size() ? sets[data[a]] : texset; pc = a + 1; continue;
        case 0x2C: texset = rd16(data, a) < sets.size() ? sets[rd16(data, a)] : texset; pc = a + 2; continue;
        case 0x34: phase = std::clamp(operand_f(a), 0.0f, 1.0f), phase_id = 0; pc = a + 4; continue;
        case 0x35: { // animated chunk range: the frame and morph weight of the phase
            u32 base = rd16(data, a), count = rd16(data, a + 2);
            if (count) {
                float f = phase * float(count);
                s32 k = s32(f); // truncated
                if (!(k < s32(count))) k = s32(count) - 1;
                Draw& d = draw(base + u32(k), pc);
                d.range_base = base, d.range_count = count;
                d.morph = f - float(k);
                d.phase_id = phase_id;
            }
            pc = a + 4;
            continue;
        }
        case 0x32: { float f = rdf(data, a); env_c1 = {f, f, f, 0}; pc = a + 4; continue; }
        case 0x33: env_c1 = {rdf(data, a), rdf(data, a + 4), rdf(data, a + 8), rdf(data, a + 12)}; pc = a + 16; continue;
        case 0x36: { float f = rdf(data, a); env_c0 = {f, f, f, 1}; pc = a + 4; continue; }
        case 0x37: env_c0 = {rdf(data, a), rdf(data, a + 4), rdf(data, a + 8), rdf(data, a + 12)}; pc = a + 16; continue;
        case 0x31: pano_set = true, pano = {rdf(data, a), rdf(data, a + 4), rdf(data, a + 8)}; pc = a + 12; continue;
        default: {
            int n = fixed_operand_bytes(op);
            if (n < 0) {
                leave();
                return false;
            }
            pc = a + n; // render-state ops: not needed for geometry
            continue;
        }
        }
    }
    throw FormatError("GTM1 program does not terminate");
    };
    exec(nodes[node], SIZE_MAX);
}

std::vector<u32> Gtm1::animated_nodes() const {
    std::vector<std::pair<u32, u32>> starts; // program offset, node
    for (u32 i = 0; i < nodes.size(); ++i)
        if (nodes[i]) starts.push_back({nodes[i], i});
    std::sort(starts.begin(), starts.end());
    std::vector<u32> out;
    for (const Animator& a : animators)
        for (u32 t : a.targets) {
            auto it = std::upper_bound(starts.begin(), starts.end(), std::make_pair(t, ~0u));
            if (it != starts.begin()) out.push_back(std::prev(it)->second);
        }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

DecodedPacket Gtm1::decode_packet(const Packet& pk, bool prelit) const {
    DecodedPacket out;
    std::vector<ps2::VifCommand> cmds = ps2::parse_vif(data.subspan(pk.offset, size_t(pk.qwc) * 16));
    std::vector<std::array<float, 4>> pos, nrm, uv, envn, pos1, nrm1, uv1;
    std::vector<std::array<u32, 4>> col, col1;
    for (const ps2::VifCommand& c : cmds) {
        if (!c.is_unpack()) continue;
        ps2::VifUnpack u = c.unpack();
        if (u.addr == 0xC0 && u.count == 1) {
            out.tag = ps2::GifTag::decode(u.data);
            out.prim = ps2::Prim::decode(out.tag.prim);
        } else if (u.addr == 0x00 && u.vn == 2 && u.vl == 0) {
            pos = ps2::unpack_floats(u);
        } else if (u.addr == 0x40 && u.vn == 0 && u.vl == 2) {
            std::vector<std::array<u32, 4>> w = ps2::unpack_words(u);
            u32 n = w.empty() ? 0 : w[0][0];
            for (u32 i = 1; i <= n && i < w.size(); ++i) out.strips.push_back(w[i][0] / 3 + 2);
        } else if (u.addr == 0x80 && u.vn == 2 && u.vl == 0) {
            nrm = ps2::unpack_floats(u);
        } else if (u.addr == 0x80 && u.vn == 3 && u.vl == 2) {
            col = ps2::unpack_words(u);
        } else if (u.addr == 0x40 && u.vn == 1 && u.vl == 0) {
            uv = ps2::unpack_floats(u);
        } else if (u.addr == 0x40 && u.vn == 2 && u.vl == 0) {
            envn = ps2::unpack_floats(u);
        } else if (u.addr == 0x20 && u.vn == 2 && u.vl == 0) { // second key frame
            pos1 = ps2::unpack_floats(u), out.morph = true;
        } else if (u.addr == 0x60 && u.vn == 1 && u.vl == 0) {
            uv1 = ps2::unpack_floats(u), out.morph = true;
        } else if (u.addr == 0xA0 && u.vn == 2 && u.vl == 0) {
            nrm1 = ps2::unpack_floats(u), out.morph = true;
        } else if (u.addr == 0xA0 && u.vn == 3 && u.vl == 2) {
            col1 = ps2::unpack_words(u), out.morph = true;
        } else if (u.addr == 0x20 || u.addr == 0x60 || u.addr == 0xA0) {
            out.morph = true;
        }
    }
    u32 total = 0;
    for (u32 s : out.strips) total += s;
    // Skinned packets carry no positions: a vertex block follows the VIF data (gtm1.h "Skinned
    // models"). Its normals go where the header says: 0x80 (lit) or 0x40 (reflection pass).
    if (pos.empty() && total) {
        size_t p = size_t(pk.offset) + size_t(pk.qwc) * 16;
        if (p + 4 <= data.size() && rd16(data, p) == total && (rd16(data, p + 2) == 0x80 || rd16(data, p + 2) == 0x40)) {
            std::vector<std::array<float, 4>>& normals = rd16(data, p + 2) == 0x80 ? nrm : envn;
            p += 4;
            for (bool more = true; more && pos.size() < total;) {
                if (p + 8 > data.size()) throw FormatError("GTM1 skin block out of range");
                u32 count = rd16(data, p) & 0x7FFF;
                more = (rd16(data, p) & 0x8000) != 0;
                if (rd16(data, p + 2) != 4 || count > total - pos.size() || size_t(count) * 40 > data.size() - p - 8)
                    throw FormatError("GTM1 skin group out of range");
                SkinWeights w;
                for (int k = 0; k < 4; ++k) w.bones[k] = data[p + 4 + k];
                p += 8;
                for (u32 i = 0; i < count; ++i, p += 40) {
                    pos.push_back({rdf(data, p), rdf(data, p + 4), rdf(data, p + 8), 1.0f});
                    normals.push_back({rdf(data, p + 12), rdf(data, p + 16), rdf(data, p + 20), 0.0f});
                    for (int k = 0; k < 4; ++k) w.weights[k] = rdf(data, p + 24 + 4 * k);
                    out.skin.push_back(w);
                }
            }
        }
    }
    size_t n = pos.size();
    if (total != n) throw FormatError("GTM1 packet strip lengths do not match vertex count");
    auto check = [&](size_t sz) { if (sz && sz != n) throw FormatError("GTM1 packet attribute count mismatch"); };
    check(nrm.size()), check(col.size()), check(uv.size()), check(envn.size());
    check(pos1.size()), check(nrm1.size()), check(uv1.size()), check(col1.size());
    out.has_normals = !nrm.empty();
    out.has_colors = !col.empty();
    out.has_uv = !uv.empty();
    out.env_normals = !envn.empty();
    for (const auto* cs : {&col, &col1})
        for (const std::array<u32, 4>& c : *cs)
            out.raw_rgb_max = std::max({out.raw_rgb_max, c[0] & 0xFF, c[1] & 0xFF, c[2] & 0xFF});
    // A factor past 0x80 does not fit 0x80 = 255: such packets keep the GS bytes (rgb_2x).
    const bool as_is = prelit && !out.prim.tme;
    out.rgb_2x = !as_is && out.raw_rgb_max > 0x80;
    out.vertices.resize(n);
    for (size_t i = 0; i < n; ++i) {
        scene::Vertex& v = out.vertices[i];
        for (int k = 0; k < 3; ++k) v.pos[k] = pos[i][k];
        if (!nrm.empty()) for (int k = 0; k < 3; ++k) v.normal[k] = nrm[i][k];
        else if (!envn.empty()) for (int k = 0; k < 3; ++k) v.normal[k] = envn[i][k];
        if (!uv.empty()) v.uv[0] = uv[i][0], v.uv[1] = uv[i][1];
        if (!col.empty()) v.color = expand_color(col[i], as_is || out.rgb_2x);
    }
    if (!pos1.empty()) {
        out.key1 = out.vertices;
        for (size_t i = 0; i < n; ++i) {
            scene::Vertex& v = out.key1[i];
            for (int k = 0; k < 3; ++k) v.pos[k] = pos1[i][k];
            if (!nrm1.empty()) for (int k = 0; k < 3; ++k) v.normal[k] = nrm1[i][k];
            if (!uv1.empty()) v.uv[0] = uv1[i][0], v.uv[1] = uv1[i][1];
            if (!col1.empty()) v.color = expand_color(col1[i], as_is || out.rgb_2x);
        }
    }
    return out;
}

namespace {

// 4/8-bit indexed texture with a 32-bit CSM1 CLUT, honouring TEX0.CSA (the
// CLUT entry offset in units of 16). GsMemory::decode_rgba ignores CSA, and
// car paint variants depend on it.
std::vector<u32> decode_indexed_csa(const GsMemory& gs, const Tex0& t, u32 width, u32 height) {
    bool is4 = t.psm == u32(Psm::T4) || t.psm == u32(Psm::T4HL) || t.psm == u32(Psm::T4HH);
    u32 n = is4 ? 16 : 256;
    u32 palette[256];
    for (u32 i = 0; i < n; ++i) {
        u32 x, y;
        csm1_position((is4 ? t.csa * 16 : 0) + i, x, y);
        palette[i] = clut_rgba(gs.read_texel(t.cbp, 1, u32(Psm::CT32), x, y), t.cpsm);
    }
    std::vector<u32> out(size_t(width) * height);
    for (u32 y = 0; y < height; ++y)
        for (u32 x = 0; x < width; ++x) {
            u32 c = palette[gs.read_texel(t.tbp, t.tbw, t.psm, x, y) & (n - 1)];
            out[size_t(y) * width + x] = t.tcc ? c : (c | 0xFF000000u);
        }
    return out;
}

} // namespace

std::vector<PaintVariant> paint_variants(ByteSpan tex1) {
    std::vector<PaintVariant> out;
    if (!Tex1::is_tex1(tex1)) return out;
    u32 table = rd32(tex1, 0x20);
    if (table == 0) return out;
    u32 v = rd32(tex1, table);
    for (u32 i = 0; i < v; ++i) {
        u32 off = rd32(tex1, table + 4 + 4 * i);
        u32 n = rd32(tex1, off) & 0xFFFF;
        PaintVariant pv;
        for (u32 k = 0; k < n && off + 8 + 4 * k <= tex1.size(); ++k) {
            u32 e = rd32(tex1, off + 4 + 4 * k);
            pv.clut.push_back({e >> 23, (e >> 5) & 0x3FFF, e & 31, (e >> 19) & 0xF});
        }
        out.push_back(std::move(pv));
    }
    return out;
}

namespace {
double channel(u32 c, int k) { return double((c >> (8 * k)) & 0xFF); }
double luminance(u32 c) { return 0.299 * channel(c, 0) + 0.587 * channel(c, 1) + 0.114 * channel(c, 2); }
u32 pack_rgb(const double* c) {
    u32 v = 0;
    for (int k = 0; k < 3; ++k) v |= u32(std::clamp(std::lround(c[k]), 0L, 255L)) << (8 * k);
    return v;
}
} // namespace

std::vector<u32> paint_body_colours(const std::vector<std::vector<std::vector<u32>>>& textures) {
    size_t n = 0;
    for (const auto& t : textures) n = std::max(n, t.size());
    std::vector<double> sum(n * 3, 0);
    double count = 0;
    for (const std::vector<std::vector<u32>>& t : textures) {
        if (t.size() != n || n == 0) continue;
        bool same = true;
        for (const std::vector<u32>& v : t) same = same && v.size() == t[0].size();
        if (!same) continue;
        for (size_t i = 0; i < t[0].size(); ++i) {
            bool moves = false;
            for (size_t v = 1; v < n && !moves; ++v) moves = (t[v][i] & 0xFFFFFFu) != (t[0][i] & 0xFFFFFFu);
            if (!moves) continue;
            count += 1;
            for (size_t v = 0; v < n; ++v)
                for (int k = 0; k < 3; ++k) sum[v * 3 + size_t(k)] += channel(t[v][i], k);
        }
    }
    std::vector<u32> out(n, 0);
    if (count > 0)
        for (size_t v = 0; v < n; ++v) {
            double c[3];
            for (int k = 0; k < 3; ++k) c[k] = sum[v * 3 + size_t(k)] / count;
            out[v] = pack_rgb(c);
        }
    return out;
}

u32 computed_body_colour(const ComputedPaint& p, const std::vector<u32>& body) {
    // How much darker (or lighter) the paints look on the body than their carcolor.db colours, over every paint:
    // the new colour's body colour is its colour by that factor, as the disc's own paints are on average.
    double l_body = 0, l_label = 0;
    for (size_t v = 0; v < body.size() && v < p.paint_rgb.size(); ++v) l_body += luminance(body[v]), l_label += luminance(p.paint_rgb[v]);
    const double r = l_label > 1 ? std::clamp(l_body / l_label, 0.2, 2.0) : 1.0;
    double c[3];
    for (int k = 0; k < 3; ++k) c[k] = channel(p.rgb, k) * r;
    return pack_rgb(c);
}

std::vector<u32> computed_paint_texels(const std::vector<std::vector<u32>>& by_paint, const std::vector<u32>& body, u32 target,
                                       int finish) {
    const size_t n = by_paint.size();
    if (n == 0) return {};
    const size_t f = size_t(std::clamp(finish, 0, int(n) - 1));
    std::vector<u32> out = by_paint[f];
    if (body.size() != n) return out;
    for (const std::vector<u32>& v : by_paint)
        if (v.size() != out.size()) return out;
    // Paints nearer the new colour count for more (the palettes are not quite linear in the colour).
    std::vector<double> w(n);
    for (size_t v = 0; v < n; ++v) {
        double d = 0;
        for (int k = 0; k < 3; ++k) d += (channel(body[v], k) - channel(target, k)) * (channel(body[v], k) - channel(target, k));
        w[v] = 1.0 / (d + 400.0);
    }
    for (size_t i = 0; i < out.size(); ++i) {
        bool changes = false;
        for (size_t v = 0; v < n && !changes; ++v) changes = (by_paint[v][i] & 0xFFFFFFu) != (out[i] & 0xFFFFFFu);
        if (!changes) continue; // trim, lamps, glass: the same under every paint
        // texel = s x body colour + h, one s and h for all three channels (shading and a grey highlight).
        double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (size_t v = 0; v < n; ++v)
            for (int k = 0; k < 3; ++k) {
                const double x = channel(body[v], k), y = channel(by_paint[v][i], k);
                sw += w[v], sx += w[v] * x, sy += w[v] * y, sxx += w[v] * x * x, sxy += w[v] * x * y;
            }
        const double var = sxx - sx * sx / sw;
        const double s = var > 1e-9 ? (sxy - sx * sy / sw) / var : 0, h = (sy - s * sx) / sw;
        double c[3];
        for (int k = 0; k < 3; ++k) c[k] = s * channel(target, k) + h;
        out[i] = (out[i] & 0xFF000000u) | pack_rgb(c);
    }
    return out;
}

std::vector<u8> livery_coverage(const texture_pack::Image& pic, TemplateTexels templ) {
    std::vector<u8> cover(pic.rgba.size());
    for (u32 y = 0; y < pic.height; ++y)
        for (u32 x = 0; x < pic.width; ++x) {
            const u32 p = pic.rgba[size_t(y) * pic.width + x];
            double a = double(p >> 24);
            if (templ.rgba && templ.width && templ.height) {
                const u32 q = templ.rgba[size_t(u64(y) * templ.height / pic.height) * templ.width + size_t(u64(x) * templ.width / pic.width)];
                int d = 0;
                for (int k = 0; k < 3; ++k) d = std::max(d, std::abs(int((p >> (8 * k)) & 0xFF) - int((q >> (8 * k)) & 0xFF)));
                a *= std::clamp((d - 6) / 12.0, 0.0, 1.0);
            }
            cover[size_t(y) * pic.width + x] = u8(std::lround(a));
        }
    return cover;
}

void overlay_livery(scene::Texture& t, const texture_pack::Image& pic, TemplateTexels templ) {
    if (t.width == 0 || t.height == 0 || pic.width == 0 || pic.height == 0 || pic.rgba.size() != size_t(pic.width) * pic.height ||
        t.rgba.size() != size_t(t.width) * t.height)
        return;
    const std::vector<u8> cover = livery_coverage(pic, templ);
    if (std::all_of(cover.begin(), cover.end(), [](u8 c) { return c == 0; })) return;
    const u32 w = std::max(t.width, pic.width), h = std::max(t.height, pic.height);
    // A texel of `px` (sw x sh) at (x + 0.5) / w across: filtered (bilinear, edges clamped) or the nearest.
    auto sample = [&](const std::vector<u32>& px, u32 sw, u32 sh, u32 x, u32 y, bool filter) -> u32 {
        if (sw == w && sh == h) return px[size_t(y) * sw + x];
        const double fx = (double(x) + 0.5) * sw / w - 0.5, fy = (double(y) + 0.5) * sh / h - 0.5;
        if (!filter) {
            const u32 nx = std::min(sw - 1, u32(std::max(0.0, std::floor(fx + 0.5)))), ny = std::min(sh - 1, u32(std::max(0.0, std::floor(fy + 0.5))));
            return px[size_t(ny) * sw + nx];
        }
        const double x0 = std::floor(fx), y0 = std::floor(fy), ax = fx - x0, ay = fy - y0;
        auto at = [&](double xx, double yy) {
            const u32 cx = u32(std::clamp(xx, 0.0, double(sw - 1))), cy = u32(std::clamp(yy, 0.0, double(sh - 1)));
            return px[size_t(cy) * sw + cx];
        };
        const u32 c00 = at(x0, y0), c10 = at(x0 + 1, y0), c01 = at(x0, y0 + 1), c11 = at(x0 + 1, y0 + 1);
        u32 v = 0;
        for (int k = 0; k < 4; ++k) {
            auto ch = [&](u32 c) { return double((c >> (8 * k)) & 0xFF); };
            const double top = ch(c00) * (1 - ax) + ch(c10) * ax, bot = ch(c01) * (1 - ax) + ch(c11) * ax;
            v |= u32(std::clamp(std::lround(top * (1 - ay) + bot * ay), 0L, 255L)) << (8 * k);
        }
        return v;
    };
    // (a picture smaller than the texture: its coverage filtered like its colour)
    auto cover_at = [&](u32 x, u32 y) -> u32 {
        const double fx = (double(x) + 0.5) * pic.width / w - 0.5, fy = (double(y) + 0.5) * pic.height / h - 0.5;
        const double x0 = std::floor(fx), y0 = std::floor(fy), ax = fx - x0, ay = fy - y0;
        auto at = [&](double xx, double yy) {
            return double(cover[size_t(std::clamp(yy, 0.0, double(pic.height - 1))) * pic.width + size_t(std::clamp(xx, 0.0, double(pic.width - 1)))]);
        };
        const double top = at(x0, y0) * (1 - ax) + at(x0 + 1, y0) * ax, bot = at(x0, y0 + 1) * (1 - ax) + at(x0 + 1, y0 + 1) * ax;
        return u32(std::clamp(std::lround(top * (1 - ay) + bot * ay), 0L, 255L));
    };
    std::vector<u32> out(size_t(w) * h);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            const u32 base = sample(t.rgba, t.width, t.height, x, y, !t.nearest);
            const u32 alpha = sample(t.rgba, t.width, t.height, x, y, false) & 0xFF000000u;
            const u32 p = sample(pic.rgba, pic.width, pic.height, x, y, true);
            const u32 a = pic.width == w && pic.height == h ? cover[size_t(y) * w + x] : cover_at(x, y);
            u32 v = alpha;
            for (int k = 0; k < 3; ++k) {
                const u32 b = (base >> (8 * k)) & 0xFF, c = (p >> (8 * k)) & 0xFF;
                v |= ((b * (255 - a) + c * a + 127) / 255) << (8 * k);
            }
            out[size_t(y) * w + x] = v;
        }
    if (w != t.width || h != t.height) t.nearest = false;
    std::vector<u8> cov(size_t(w) * h);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x)
            cov[size_t(y) * w + x] = u8(pic.width == w && pic.height == h ? cover[size_t(y) * w + x] : cover_at(x, y));
    t.width = w, t.height = h, t.rgba = std::move(out);
    t.livery_cover = std::make_shared<const std::vector<u8>>(std::move(cov));
}

int decode_texture_set(ByteSpan tex1, std::vector<scene::Texture>& out, const std::string& prefix, int paint,
                       ClutTextures* clut, bool use_pack, const ComputedPaint* computed, const texture_pack::Pack* livery,
                       bool gs_mip_levels) {
    Tex1 t = Tex1::parse(tex1);
    GsMemory gs;
    t.upload(tex1, gs);
    std::vector<PaintVariant> variants;
    if (paint > 0 || computed) variants = paint_variants(tex1);
    // A computed colour needs a variant for each of the car's paints; otherwise its finish paint.
    if (computed && (variants.empty() || variants.size() != computed->paint_rgb.size())) {
        paint = computed->finish;
        computed = nullptr;
    }
    const PaintVariant* pv = paint > 0 && size_t(paint) < variants.size() ? &variants[paint] : nullptr;
    int first = int(out.size());
    auto decode = [&](size_t i, const PaintVariant* v, bool* patched = nullptr) {
        const Tex1::Texture& tx = t.textures[i];
        Tex0 t0 = tx.tex0;
        if (v)
            for (const PaintVariant::Entry& e : v->clut)
                if (e.texture == i) {
                    t0.cbp = e.cbp, t0.csa = e.csa, t0.cpsm = e.cpsm;
                    if (patched) *patched = true;
                }
        // Always the full power-of-two size: model UVs address the whole texture.
        return indexed_ct32(t0) ? decode_indexed_csa(gs, t0, t0.width(), t0.height())
                                : gs.decode_rgba(t0, t0.width(), t0.height());
    };
    // A computed colour: every texture under every paint first (the paints' body colours are measured over the set).
    std::vector<std::vector<std::vector<u32>>> by_paint;
    std::vector<u32> body;
    u32 target = 0;
    if (computed) {
        by_paint.resize(t.textures.size());
        for (size_t i = 0; i < t.textures.size(); ++i) {
            bool recoloured = false;
            by_paint[i].resize(variants.size());
            for (size_t v = 1; v < variants.size(); ++v) by_paint[i][v] = decode(i, &variants[v], &recoloured);
            by_paint[i][0] = decode(i, nullptr);
            if (!recoloured) by_paint[i].resize(1); // the same under every paint
        }
        body = paint_body_colours(by_paint);
        target = computed_body_colour(*computed, body);
    }
    std::vector<u64> livery_keys; // per texture, with a livery
    std::vector<std::vector<u32>> livery_templates; // the textures in the livery's paint
    for (size_t i = 0; i < t.textures.size(); ++i) {
        const Tex1::Texture& tx = t.textures[i];
        scene::Texture st;
        st.name = prefix + std::to_string(i);
        st.width = tx.tex0.width();
        st.height = tx.tex0.height();
        if (!computed) st.rgba = decode(i, pv);
        // The livery's key: the texture in the livery's paint (the computed colour's finish).
        if (livery) {
            const std::vector<u32>& in_paint =
                !computed ? st.rgba
                          : by_paint[i][by_paint[i].size() == 1 ? 0 : size_t(std::clamp(computed->finish, 0, int(by_paint[i].size()) - 1))];
            livery_keys.push_back(texture_pack::key_of(st.width, st.height, in_paint.data()));
            livery_templates.push_back(in_paint);
        }
        if (computed && by_paint[i].size() == 1) st.rgba = std::move(by_paint[i][0]);
        else if (computed) st.rgba = computed_paint_texels(by_paint[i], body, target, computed->finish);
        st.clamp_u = tx.clamp.wms == 1 || tx.clamp.wms == 2;
        st.clamp_v = tx.clamp.wmt == 1 || tx.clamp.wmt == 2;
        st.nearest = ((tx.tex1 >> 5) & 1) == 0; // TEX1.MMAG
        st.no_mips = gs_mip_levels && ((tx.tex1 >> 2) & 7) == 0; // TEX1.MXL
        out.push_back(std::move(st));
    }
    // CLUT animations: the textures they feed show the animations' state (at their reset here).
    ClutTextures c;
    if (!t.clut_animations.empty() && !pv && !computed) {
        c = ClutTextures(tex1);
        for (u32 r : c.records())
            if (const std::vector<u32>* px = c.rgba(r); px && px->size() == out[size_t(first) + r].rgba.size())
                out[size_t(first) + r].rgba = *px;
    }
    // Texture pack pictures, keyed by the textures as decoded so far. A replaced texture no
    // longer follows its CLUT animations.
    if (use_pack && texture_pack::current_pack())
        for (size_t i = size_t(first); i < out.size(); ++i) {
            scene::Texture& st = out[i];
            std::shared_ptr<const texture_pack::Image> r = texture_pack::replacement(st.width, st.height, st.rgba.data());
            if (!r) continue;
            st.width = r->width, st.height = r->height, st.rgba = r->rgba, st.nearest = false, st.no_mips = false;
            c.erase(u32(i - size_t(first)));
        }
    // The livery's pictures over the paint (and the pack's picture). A covered texture no longer animates.
    if (livery)
        for (size_t i = 0; i < livery_keys.size(); ++i)
            if (std::shared_ptr<const texture_pack::Image> pic = livery->find(livery_keys[i])) {
                const Tex0& t0 = t.textures[i].tex0;
                overlay_livery(out[size_t(first) + i], *pic, {livery_templates[i].data(), t0.width(), t0.height()});
                out[size_t(first) + i].no_mips = false; // (a picture, often finer than the disc's texture)
                c.erase(u32(i));
            }
    if (clut && !c.empty()) *clut = std::move(c);
    return first;
}

void colour_cut_texels(scene::Texture& t) { colour_cut_texels(t, !t.clamp_u, !t.clamp_v); }

void colour_cut_texels(scene::Texture& t, bool wrap_u, bool wrap_v) {
    const s32 w = s32(t.width), h = s32(t.height);
    if (w <= 0 || h <= 0 || t.rgba.size() != size_t(w) * size_t(h)) return;
    std::vector<u8> done(t.rgba.size());
    std::vector<size_t> todo;
    for (size_t i = 0; i < t.rgba.size(); ++i)
        if (t.rgba[i] >> 24) done[i] = 1;
        else todo.push_back(i);
    if (todo.size() == t.rgba.size()) return;
    std::vector<std::pair<size_t, u32>> ring;
    while (!todo.empty()) {
        ring.clear();
        std::vector<size_t> rest;
        for (size_t i : todo) {
            const s32 x = s32(i % size_t(w)), y = s32(i / size_t(w));
            u32 sum[3] = {0, 0, 0}, n = 0;
            for (s32 dy = -1; dy <= 1; ++dy)
                for (s32 dx = -1; dx <= 1; ++dx) {
                    s32 nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= w) {
                        if (!wrap_u) continue;
                        nx = (nx + w) % w;
                    }
                    if (ny < 0 || ny >= h) {
                        if (!wrap_v) continue;
                        ny = (ny + h) % h;
                    }
                    const size_t j = size_t(ny) * size_t(w) + size_t(nx);
                    if (!done[j]) continue;
                    for (int k = 0; k < 3; ++k) sum[k] += (t.rgba[j] >> (8 * k)) & 0xFF;
                    ++n;
                }
            if (!n) {
                rest.push_back(i);
                continue;
            }
            u32 rgb = 0;
            for (int k = 0; k < 3; ++k) rgb |= ((sum[k] + n / 2) / n) << (8 * k);
            ring.emplace_back(i, rgb);
        }
        if (ring.empty()) break; // cut texels no kept one reaches (a clamped corner): left as they are
        for (const auto& [i, rgb] : ring) t.rgba[i] = (t.rgba[i] & 0xFF000000u) | rgb, done[i] = 1;
        todo = std::move(rest);
    }
}

namespace {

// Whether the texels just past a primitive's edge at UV `bound` on `axis` (upper: its max) differ
// from the ones just inside, over the rows / columns `other_lo`..`other_hi` of the other axis: a
// different picture packed beside it, not the rest of a continuous one (mean RGBA difference).
bool edge_differs(const scene::Texture& t, int axis, float bound, bool upper, float other_lo, float other_hi) {
    const s32 w = s32(t.width), h = s32(t.height);
    if (w <= 0 || h <= 0 || t.rgba.size() != size_t(w) * size_t(h)) return false;
    const s32 n = axis == 0 ? w : h, m = axis == 0 ? h : w;
    const float e = bound * float(n);
    // Pictures packed into a texture start and stop on whole texels (the billboards: texel 192 of
    // 256); a face that ends inside a continuous picture ends anywhere.
    if (std::fabs(e - std::round(e)) > 1.0f / 16.0f) return false;
    const s32 in = std::clamp(upper ? s32(std::ceil(e - 1e-3f)) - 1 : s32(std::floor(e + 1e-3f)), 0, n - 1);
    const s32 out = upper ? in + 1 : in - 1;
    if (out < 0 || out >= n) return false;
    s32 r0 = 0, r1 = m - 1;
    if (other_lo >= 0.0f && other_hi <= 1.0f)
        r0 = std::clamp(s32(std::floor(other_lo * float(m))), 0, m - 1),
        r1 = std::clamp(s32(std::ceil(other_hi * float(m))) - 1, r0, m - 1);
    auto texel = [&](s32 a, s32 b) { return axis == 0 ? t.rgba[size_t(b) * size_t(w) + size_t(a)] : t.rgba[size_t(a) * size_t(w) + size_t(b)]; };
    u64 sum = 0;
    for (s32 r = r0; r <= r1; ++r) {
        const u32 a = texel(in, r), b = texel(out, r);
        for (int k = 0; k < 32; k += 8) sum += u32(std::abs(s32((a >> k) & 0xFF) - s32((b >> k) & 0xFF)));
    }
    return sum > u64(24) * u64(r1 - r0 + 1);
}

} // namespace

int mark_inside_uvs(scene::Mesh& mesh, const std::vector<scene::Vertex>* key1, const std::vector<scene::Texture>* textures) {
    int marked = 0;
    for (scene::Primitive& p : mesh.primitives) {
        if (p.texture < 0 || p.env_kind != 0 || (p.flags & scene::kEnvironmentMap) || p.index_count == 0) continue;
        float lo[2] = {1, 1}, hi[2] = {0, 0};
        auto take = [&](const scene::Vertex& v) {
            for (int k = 0; k < 2; ++k) lo[k] = std::min(lo[k], v.uv[k]), hi[k] = std::max(hi[k], v.uv[k]);
        };
        for (u32 i = p.first_index; i < p.first_index + p.index_count && i < mesh.indices.size(); ++i) {
            const u32 v = mesh.indices[i];
            if (v < mesh.vertices.size()) take(mesh.vertices[v]);
            if (key1 && v < key1->size()) take((*key1)[v]); // (morphs blend between the two)
        }
        // (A float step past an edge counts as on it: the Astra Touring Car's grille spans
        // -6e-8..0.99999988. Well under 1/16 of a texel, the GS's UV step, for 512 texels.)
        constexpr float kSlack = 1e-4f;
        const u32 before = p.flags;
        for (int k = 0; k < 2; ++k) {
            if (lo[k] < -kSlack || hi[k] > 1.0f + kSlack) continue;
            p.flags |= k == 0 ? scene::kClampU : scene::kClampV;
        }
        // Stopping inside the texture beside a different picture: the region (both axes, as the
        // UVs' box; an axis that spans the texture or repeats gets none).
        const scene::Texture* t = textures && size_t(p.texture) < textures->size() ? &(*textures)[size_t(p.texture)] : nullptr;
        bool region = false;
        for (int k = 0; k < 2 && t; ++k) {
            if (!(p.flags & (k == 0 ? scene::kClampU : scene::kClampV)) || hi[k] <= lo[k]) continue;
            region |= (lo[k] > kSlack && edge_differs(*t, k, lo[k], false, lo[1 - k], hi[1 - k])) ||
                      (hi[k] < 1.0f - kSlack && edge_differs(*t, k, hi[k], true, lo[1 - k], hi[1 - k]));
        }
        for (int k = 0; k < 2 && region; ++k)
            if ((p.flags & (k == 0 ? scene::kClampU : scene::kClampV)) && hi[k] > lo[k] && (lo[k] > kSlack || hi[k] < 1.0f - kSlack))
                p.uv_region[k] = std::max(lo[k], 0.0f), p.uv_region[k + 2] = std::min(hi[k], 1.0f);
        marked += p.flags != before;
    }
    return marked;
}

namespace {

// GS alpha test of one texel (alpha expanded to 0..255; GS units are 0x80 = 1.0).
bool gs_alpha_pass(u32 a255, u8 method, u8 ref) {
    u32 a = (a255 * 128 + 127) / 255;
    switch (method & 7) {
    case 0: return false;
    case 1: return true;
    case 2: return a < ref;
    case 3: return a <= ref;
    case 4: return a == ref;
    case 5: return a >= ref;
    case 6: return a > ref;
    default: return a != ref;
    }
}

u32 texel_alpha(const scene::Texture& t, float u, float v) {
    if (t.width == 0 || t.height == 0) return 255;
    s32 x = s32(std::floor(u * float(t.width))) % s32(t.width);
    s32 y = s32(std::floor(v * float(t.height))) % s32(t.height);
    if (x < 0) x += s32(t.width);
    if (y < 0) y += s32(t.height);
    return t.rgba[size_t(y) * t.width + size_t(x)] >> 24;
}

// Frame-buffer alpha a pass left at a vertex position (see build_model).
struct Written {
    u32 weight = 0;   // texel alpha x vertex alpha at this vertex (0..255)
    int texture = -1; // the pass's texture (its alpha is the per-pixel reflectivity)
    float uv[2] = {0, 0};
    u32 vertex_alpha = 255;
    // An alpha-tested pass: texels with alpha <= under_ref (0..1) wrote nothing, so `under` (0..255,
    // what lay there before) stays in them (scene::Primitive::under_ref). < 0: none.
    float under_ref = -1;
    u32 under = 0;
    int pass = -1;          // the draw that wrote it (its index in the run)
    bool masked = false;    // a pass with the alpha write-masked: the frame alpha is not its own
    bool reflection = false; // a reflection pass (which leaves the frame alpha at 0x80)
};

// Ray t from `o` along `d` into triangle k (Moller-Trumbore), its barycentrics in `bary`; < -1 = none.
float ray_triangle(const float* o, const float* d, const std::array<std::array<float, 3>, 3>& k, float* bary) {
    float a[3], b[3], p[3], t[3], q[3];
    for (int j = 0; j < 3; ++j) a[j] = k[1][j] - k[0][j], b[j] = k[2][j] - k[0][j], t[j] = o[j] - k[0][j];
    p[0] = d[1] * b[2] - d[2] * b[1], p[1] = d[2] * b[0] - d[0] * b[2], p[2] = d[0] * b[1] - d[1] * b[0];
    float det = a[0] * p[0] + a[1] * p[1] + a[2] * p[2];
    if (std::fabs(det) < 1e-12f) return -2;
    float u = (t[0] * p[0] + t[1] * p[1] + t[2] * p[2]) / det;
    if (u < 0 || u > 1) return -2;
    q[0] = t[1] * a[2] - t[2] * a[1], q[1] = t[2] * a[0] - t[0] * a[2], q[2] = t[0] * a[1] - t[1] * a[0];
    float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) / det;
    if (v < 0 || u + v > 1) return -2;
    bary[0] = 1 - u - v, bary[1] = u, bary[2] = v;
    return (b[0] * q[0] + b[1] * q[1] + b[2] * q[2]) / det;
}

// v' = v M (row vectors) for the position, M's rotation part for the (renormalised) normal.
void place_vertex(const Matrix& m, scene::Vertex& v) {
    float p[3], n[3];
    for (int j = 0; j < 3; ++j) {
        p[j] = v.pos[0] * m[j] + v.pos[1] * m[4 + j] + v.pos[2] * m[8 + j] + m[12 + j];
        n[j] = v.normal[0] * m[j] + v.normal[1] * m[4 + j] + v.normal[2] * m[8 + j];
    }
    float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    for (int j = 0; j < 3; ++j) v.pos[j] = p[j], v.normal[j] = len > 0 ? n[j] / len : 0;
}

} // namespace

scene::Model build_model(const Gtm1& g, const Options& opt, const std::string& name) {
    scene::Model model;
    model.name = name;
    std::map<u32, int> set_first; // texset -> first model texture index (-1 = failed)
    std::map<u32, u32> set_size;
    // Texture -> smallest texel alpha (alpha tests): over every frame of a CLUT animation.
    std::map<int, u32> min_alpha;
    auto set_without_textures = [&](u32 texset) {
        return texset < g.texsets.size() && !Tex1::is_tex1(g.data.subspan(g.texsets[texset]));
    };
    auto texture_for = [&](u32 texset, u32 index) -> int {
        if (!opt.textures || texset >= g.texsets.size()) return -1;
        auto it = set_first.find(texset);
        if (it == set_first.end()) {
            ByteSpan sub = g.data.subspan(g.texsets[texset]);
            size_t before = model.textures.size();
            ClutTextures clut;
            // A set that is not a Tex1 has no textures. The game then binds none (0x24CF40 skips TEX0
            // without a texture table) and the GS keeps an earlier draw's texture.
            const std::string prefix = "set" + std::to_string(texset) + "_";
            int first = set_without_textures(texset) ? -1 : decode_texture_set(sub, model.textures, prefix, opt.paint, &clut, true, opt.computed_paint, opt.livery,
                                                                                                    opt.gs_mip_levels);
            it = set_first.emplace(texset, first).first;
            set_size[texset] = u32(model.textures.size() - before);
            if (!clut.empty()) {
                for (u32 r : clut.records()) min_alpha[first + int(r)] = clut.min_alpha(r);
                if (opt.clut_sets) opt.clut_sets->push_back({first, std::move(clut)});
            }
        }
        return index < set_size[texset] ? it->second + int(index) : -1;
    };
    // The runtime slot's texture (Options::runtime_set): its entry of the texture-LOD table in use.
    auto runtime_texture = [&](u32 texset) -> int {
        if (opt.runtime_set >= 0 && !g.texset_tables.empty()) {
            const std::vector<u32>& t =
                g.texset_tables[std::clamp<size_t>(size_t(std::max(opt.run.texture_lod, 0)), 0, g.texset_tables.size() - 1)];
            if (size_t(opt.runtime_set) < t.size())
                if (int i = texture_for(t[size_t(opt.runtime_set)], 0); i >= 0) return i;
        }
        return texture_for(texset, 0);
    };
    auto lowest_alpha = [&](int tex) {
        auto mt = min_alpha.find(tex);
        if (mt == min_alpha.end()) {
            u32 lo = 255;
            for (u32 c : model.textures[size_t(tex)].rgba) lo = std::min(lo, c >> 24);
            mt = min_alpha.emplace(tex, lo).first;
        }
        return mt->second;
    };
    // Alpha-tested copies of textures: (texture, method, ref) -> texture with binary alpha.
    std::map<std::array<int, 3>, int> cutouts;
    // The copies of point-filtered textures: their cut texels are coloured once every face is made
    // (colour_cut_texels), across an edge only where some face repeats the texture (`repeats`: per
    // texture, whether a face samples it repeating on U / V).
    std::vector<int> nearest_cuts;
    std::map<int, std::array<bool, 2>> repeats;
    auto cutout_for = [&](int tex, u8 method, u8 ref) -> int {
        std::array<int, 3> key{tex, method, ref};
        auto it = cutouts.find(key);
        if (it != cutouts.end()) return it->second;
        scene::Texture t = model.textures[size_t(tex)];
        bool all_pass = true;
        for (u32& c : t.rgba) {
            bool pass = gs_alpha_pass(c >> 24, method, ref);
            all_pass &= pass;
            c = (c & 0x00FFFFFFu) | (pass ? 0xFF000000u : 0u);
        }
        int idx = tex; // nothing cut away: keep the original
        if (!all_pass) {
            t.name += "_cut";
            model.textures.push_back(std::move(t));
            idx = int(model.textures.size() - 1);
            if (model.textures.back().nearest) nearest_cuts.push_back(idx);
        }
        cutouts.emplace(key, idx);
        return idx;
    };
    // Whether cut-out texture `cut` (cutout_for) has a hole in the texels the UVs `uv` x 3 span (their
    // box; true when it wraps): a summed-area table of the holes per texture.
    std::map<int, std::vector<u32>> hole_sums;
    auto has_holes = [&](int cut, const std::array<std::array<float, 2>, 3>& uv) {
        const scene::Texture& t = model.textures[size_t(cut)];
        if (t.width == 0 || t.height == 0) return false;
        float lo[2] = {uv[0][0], uv[0][1]}, hi[2] = {uv[0][0], uv[0][1]};
        for (const auto& q : uv)
            for (int j = 0; j < 2; ++j) lo[j] = std::min(lo[j], q[size_t(j)]), hi[j] = std::max(hi[j], q[size_t(j)]);
        const u32 w = t.width, h = t.height;
        const float fx0 = std::floor(lo[0] * float(w)), fx1 = std::floor(hi[0] * float(w));
        const float fy0 = std::floor(lo[1] * float(h)), fy1 = std::floor(hi[1] * float(h));
        if (fx0 < 0 || fy0 < 0 || fx1 >= float(w) || fy1 >= float(h)) return true; // wraps: assume so
        auto it = hole_sums.find(cut);
        if (it == hole_sums.end()) {
            std::vector<u32> s(size_t(w + 1) * (h + 1), 0);
            for (u32 y = 0; y < h; ++y)
                for (u32 x = 0; x < w; ++x)
                    s[size_t(y + 1) * (w + 1) + x + 1] = (t.rgba[size_t(y) * w + x] >> 24 ? 0u : 1u) + s[size_t(y) * (w + 1) + x + 1] +
                                                         s[size_t(y + 1) * (w + 1) + x] - s[size_t(y) * (w + 1) + x];
            it = hole_sums.emplace(cut, std::move(s)).first;
        }
        const std::vector<u32>& s = it->second;
        const size_t x0 = size_t(fx0), x1 = size_t(fx1) + 1, y0 = size_t(fy0), y1 = size_t(fy1) + 1;
        return s[y1 * (w + 1) + x1] - s[y0 * (w + 1) + x1] - s[y1 * (w + 1) + x0] + s[y0 * (w + 1) + x0] > 0;
    };

    // Reflection pass (ALPHA Cs*Ad + Cd) over vertices that earlier passes wrote: the
    // triangles whose three corners come from the same textured pass become a primitive
    // with that texture and its UVs, so the renderer can weight the reflection per pixel
    // with the texel alpha (holes = 0, no reflection). Other triangles keep a per-vertex
    // weight in the vertex alpha. All are kAdditive | kEnvironmentMap | kUnlit | kNoDepthWrite.
    // `reflected` holds the triangles earlier reflection passes drew (their corners, sorted):
    // each leaves the frame alpha at 0x80, so a kDestAlphaTest pass draws none of them again.
    // `written_tris`: the frame alpha by whole triangle (its sorted corners), each corner's Written in
    // that order: what the last pass that drew exactly this triangle left. A reflection triangle
    // found there is weighted with that pass's texture and UVs. The corner lookup alone would also
    // match a triangle spanning a hole the pass never drew (glass reflected on the alpha-only shell,
    // whose corners the window frame's body pass shares: the frame's UVs stretched over the window
    // read its cut-out texels, so the glass lost its reflection).
    // Every triangle drawn leaves the frame alpha at 0x80 (the reflection's vertex alpha) in
    // `written_tris`, written by draw `pass`: a later reflection over it adds at full weight. A pass
    // with the alpha write-masked (the glass tint) keeps that alpha: a reflection over it with an
    // earlier reflection's triangle right behind it (`behind(corners)` >= 0) adds at full weight
    // too (the Celica GT-FOUR's rear glass: its second reflection pass, 5 mm inside the first's).
    using Corners = std::array<std::array<float, 3>, 3>;
    using TriWritten = std::array<Written, 3>;
    auto emit_weighted_reflection = [](scene::Mesh& mesh, const scene::Primitive& prim,
                                       const std::map<std::array<float, 3>, Written>& written,
                                       std::map<Corners, TriWritten>& written_tris,
                                       std::set<Corners>& reflected, int pass, const auto& behind) {
        constexpr u32 kFlags = scene::kAdditive | scene::kEnvironmentMap | scene::kUnlit | scene::kNoDepthWrite;
        std::vector<u32> tris(mesh.indices.begin() + prim.first_index,
                              mesh.indices.begin() + prim.first_index + prim.index_count);
        mesh.indices.resize(prim.first_index);
        auto info = [&](u32 vi) -> const Written* {
            const float* p = mesh.vertices[vi].pos;
            auto it = written.find({p[0], p[1], p[2]});
            return it == written.end() ? nullptr : &it->second;
        };
        // (texture, under_ref, under) -> indices
        std::map<std::tuple<int, float, u32>, std::vector<u32>> groups;
        for (size_t t = 0; t + 2 < tris.size(); t += 3) {
            Corners c;
            for (int k = 0; k < 3; ++k) std::copy_n(mesh.vertices[tris[t + size_t(k)]].pos, 3, c[size_t(k)].begin());
            std::sort(c.begin(), c.end());
            if (!reflected.insert(c).second && (prim.flags & scene::kDestAlphaTest)) continue;
            const Written* w[3] = {info(tris[t]), info(tris[t + 1]), info(tris[t + 2])};
            if (auto it = written_tris.find(c); it != written_tris.end())
                for (int k = 0; k < 3; ++k)
                    for (int j = 0; j < 3; ++j)
                        if (std::equal(c[size_t(j)].begin(), c[size_t(j)].end(), mesh.vertices[tris[t + size_t(k)]].pos))
                            w[k] = &it->second[size_t(j)];
            Written over_reflection;
            if ((w[0] && w[0]->masked) || (w[1] && w[1]->masked) || (w[2] && w[2]->masked))
                if (float a = behind(c); a >= 0) {
                    over_reflection.weight = u32(a + 0.5f);
                    for (const Written*& x : w) x = &over_reflection;
                }
            bool same = w[0] && w[1] && w[2] && w[0]->texture >= 0 && w[1]->texture == w[0]->texture &&
                        w[2]->texture == w[0]->texture;
            std::vector<u32>& out = groups[same ? std::tuple<int, float, u32>{w[0]->texture, w[0]->under_ref, w[0]->under}
                                               : std::tuple<int, float, u32>{-1, -1.0f, 0u}];
            for (int k = 0; k < 3; ++k) {
                u32 vi = tris[t + k];
                if (!same) { // per-vertex weight on the shared vertex
                    u32 a = w[k] ? std::min<u32>(255, w[k]->weight) : 0u;
                    mesh.vertices[vi].color = (mesh.vertices[vi].color & 0x00FFFFFFu) | (a << 24);
                    out.push_back(vi);
                    continue;
                }
                scene::Vertex v = mesh.vertices[vi]; // own copy: UVs of the base pass
                v.uv[0] = w[k]->uv[0], v.uv[1] = w[k]->uv[1];
                v.color = (v.color & 0x00FFFFFFu) | (std::min<u32>(255, w[k]->vertex_alpha) << 24);
                out.push_back(u32(mesh.vertices.size()));
                mesh.vertices.push_back(v);
            }
            Written full;
            full.weight = full.vertex_alpha = 255;
            full.pass = pass;
            full.reflection = true;
            written_tris[c] = {full, full, full};
        }
        for (auto& [key, idx] : groups) {
            scene::Primitive p;
            p.first_index = u32(mesh.indices.size());
            p.index_count = u32(idx.size());
            p.texture = std::get<0>(key);
            p.under_ref = std::get<1>(key);
            p.under_alpha = float(std::get<2>(key)) / 255.0f;
            p.flags = kFlags | (prim.flags & (scene::kVertexColor2x | scene::kDestAlphaTest));
            p.env_rows = prim.env_rows;
            std::copy(std::begin(prim.env_c0), std::end(prim.env_c0), p.env_c0);
            std::copy(std::begin(prim.env_c1), std::end(prim.env_c1), p.env_c1);
            mesh.indices.insert(mesh.indices.end(), idx.begin(), idx.end());
            if (p.index_count) mesh.primitives.push_back(p);
        }
    };

    int gs_texture = -1; // the texture the GS holds (TEX0 stays set from packet to packet)
    // Emits one node's draws into `mesh`, vertices placed by the draws' matrices. `ranges` (when
    // given) receives where each draw's vertices, indices and primitives start, `key1` every
    // vertex's key frame 1 (its key frame 0 where the packet has no second key).
    struct Range {
        size_t vertex = 0, index = 0, primitive = 0;
    };
    auto emit = [&](const std::vector<Draw>& draws, scene::Mesh& mesh, std::vector<Range>* ranges,
                    std::vector<scene::Vertex>* key1) {
        // What earlier passes left in the frame buffer's alpha, by vertex position: the
        // reflectivity that later Cs*Ad+Cd reflection passes are weighted with.
        std::map<std::array<float, 3>, Written> written_alpha;
        std::map<Corners, TriWritten> written_tris;
        float box_lo[3] = {1e30f, 1e30f, 1e30f}, box_hi[3] = {-1e30f, -1e30f, -1e30f}; // of written_tris
        // The frame alpha an earlier pass left at point q of triangle k (as the renderer weights it).
        auto alpha_at = [&](const TriWritten& k, const float* bary) {
            float va = 0, uv[2] = {0, 0};
            for (int j = 0; j < 3; ++j) {
                va += bary[j] * float(k[size_t(j)].vertex_alpha);
                uv[0] += bary[j] * k[size_t(j)].uv[0], uv[1] += bary[j] * k[size_t(j)].uv[1];
            }
            float ta = 255;
            if (k[0].texture >= 0) {
                ta = float(texel_alpha(model.textures[size_t(k[0].texture)], uv[0], uv[1]));
                if (k[0].under_ref >= 0 && ta <= k[0].under_ref * 255.0f) ta = float(k[0].under);
            }
            return std::min(255.0f, va * ta / 255.0f);
        };
        // written_tris flat, with each triangle's box (map nodes stay put; a written triangle is
        // only ever replaced in place), rebuilt when the pass looking changes: what the pass itself
        // adds meanwhile is never behind it.
        struct Behind {
            float lo[3], hi[3];
            const Corners* k;
            const TriWritten* tw;
        };
        std::vector<Behind> behind_list;
        int behind_pass = -1;
        // What lies behind triangle c where its pass draws nothing (alpha-tested holes): the alpha
        // of the nearest triangle an earlier pass wrote within 0.6 m, looking from c into the car
        // (towards the middle of what has been drawn) at its centre and halfway to each corner. On
        // the GS the hole keeps that per pixel (the Celica GT-FOUR's rear window frame: the cabin
        // 5-33 cm behind it). `shell`: only an earlier reflection's triangles within 1 cm on either
        // side (a layer over the same pixels: the Celica's glass, 5 mm inside the first reflection's
        // triangles). < 0: nothing there.
        auto alpha_behind = [&](const Corners& c, int pass, bool shell = false) -> float {
            const float near = shell ? -0.01f : -0.002f, reach = shell ? 0.01f : 0.6f;
            if (behind_pass != pass) {
                behind_pass = pass;
                behind_list.clear();
                for (const auto& [k, tw] : written_tris) {
                    Behind b{{k[0][0], k[0][1], k[0][2]}, {k[0][0], k[0][1], k[0][2]}, &k, &tw};
                    for (const auto& p : k)
                        for (int j = 0; j < 3; ++j) b.lo[j] = std::min(b.lo[j], p[size_t(j)]), b.hi[j] = std::max(b.hi[j], p[size_t(j)]);
                    behind_list.push_back(b);
                }
            }
            float e1[3], e2[3], n[3], mid[3], to_centre = 0;
            for (int j = 0; j < 3; ++j) {
                e1[j] = c[1][j] - c[0][j], e2[j] = c[2][j] - c[0][j];
                mid[j] = (c[0][j] + c[1][j] + c[2][j]) / 3;
            }
            n[0] = e1[1] * e2[2] - e1[2] * e2[1], n[1] = e1[2] * e2[0] - e1[0] * e2[2], n[2] = e1[0] * e2[1] - e1[1] * e2[0];
            const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len == 0 || box_lo[0] > box_hi[0]) return -1;
            for (int j = 0; j < 3; ++j) to_centre += n[j] * ((box_lo[j] + box_hi[j]) * 0.5f - mid[j]);
            for (float& x : n) x /= to_centre < 0 ? -len : len;
            float q[4][3], best[4], alpha[4] = {0, 0, 0, 0};
            for (int s = 0; s < 4; ++s) {
                best[s] = 1e30f;
                for (int j = 0; j < 3; ++j) q[s][j] = s == 0 ? mid[j] : (mid[j] + c[size_t(s - 1)][size_t(j)]) * 0.5f;
            }
            float lo[3], hi[3]; // the box of the four rays
            for (int j = 0; j < 3; ++j) {
                lo[j] = hi[j] = q[0][j];
                for (int s = 0; s < 4; ++s)
                    for (float t : {near, reach})
                        lo[j] = std::min(lo[j], q[s][j] + t * n[j]), hi[j] = std::max(hi[j], q[s][j] + t * n[j]);
            }
            for (const Behind& b : behind_list) {
                if (b.hi[0] < lo[0] || b.lo[0] > hi[0] || b.hi[1] < lo[1] || b.lo[1] > hi[1] || b.hi[2] < lo[2] || b.lo[2] > hi[2])
                    continue;
                const TriWritten& tw = *b.tw;
                if (tw[0].masked || tw[0].pass == pass || (shell && !tw[0].reflection)) continue;
                for (int s = 0; s < 4; ++s) {
                    float bary[3];
                    float t = ray_triangle(q[s], n, *b.k, bary);
                    // (from just in front of the plane: a shell under the same surface)
                    if (t >= near && t <= reach && std::fabs(t) < std::fabs(best[s])) best[s] = t, alpha[s] = alpha_at(tw, bary);
                }
            }
            float sum = 0;
            int hits = 0;
            for (int s = 0; s < 4; ++s)
                if (best[s] < 1e30f) sum += alpha[s], ++hits;
            return hits ? sum / float(hits) : -1.0f;
        };
        // Records triangle (p, w) x 3 in written_tris; `keep`: a pass with the alpha write-masked
        // only fills a triangle nothing wrote before. `tested`: the pass's GS alpha test (an opaque
        // textured pass), whose failing texels keep the alpha this triangle held before.
        auto write_tri = [&](const std::array<const float*, 3>& p, const std::array<Written, 3>& w, bool keep,
                             const Draw* tested, const std::array<u32, 3>& before = {}) {
            std::array<int, 3> o{0, 1, 2};
            std::sort(o.begin(), o.end(), [&](int a, int b) {
                return std::lexicographical_compare(p[size_t(a)], p[size_t(a)] + 3, p[size_t(b)], p[size_t(b)] + 3);
            });
            Corners c;
            TriWritten tw;
            for (size_t k = 0; k < 3; ++k) {
                std::copy_n(p[size_t(o[k])], 3, c[k].begin());
                tw[k] = w[size_t(o[k])];
                tw[k].masked = keep;
            }
            // GS ATST 5 GEQUAL / 6 GREATER AREF (0x80 = 1.0): the texels failing it have alpha <= ref.
            const u8 method = tested ? tested->alpha_test_method : 0;
            const float ref = tested ? (float(tested->alpha_test_ref) + (method == 6 ? 0.5f : -0.5f)) / 128.0f : -1.0f;
            // (cutout_for keeps the texture itself when no texel fails: then there is nothing under it)
            const int cut = tested && tw[0].texture >= 0 && (method == 5 || method == 6) && ref >= 0
                                ? cutout_for(tw[0].texture, method, tested->alpha_test_ref)
                                : -1;
            if (cut >= 0 && cut != tw[0].texture) {
                // The pass that last drew exactly this triangle; else what lies behind its holes;
                // else the corners' alpha before this pass (`before`).
                u32 under = (before[0] + before[1] + before[2]) / 3;
                if (auto it = written_tris.find(c); it != written_tris.end() && !it->second[0].masked)
                    under = (it->second[0].weight + it->second[1].weight + it->second[2].weight) / 3;
                else if (float a = has_holes(cut, {{{tw[0].uv[0], tw[0].uv[1]}, {tw[1].uv[0], tw[1].uv[1]}, {tw[2].uv[0], tw[2].uv[1]}}})
                                       ? alpha_behind(c, tw[0].pass)
                                       : -1.0f;
                         a >= 0)
                    under = u32(a + 0.5f);
                under = std::min<u32>(255, (under + 32) / 64 * 64); // five values: few extra primitives
                for (Written& x : tw) x.under_ref = ref, x.under = under;
            }
            for (const auto& corner : c)
                for (int j = 0; j < 3; ++j)
                    box_lo[j] = std::min(box_lo[j], corner[size_t(j)]), box_hi[j] = std::max(box_hi[j], corner[size_t(j)]);
            if (keep) written_tris.emplace(c, tw);
            else written_tris[c] = tw;
        };
        std::set<Corners> reflected; // the triangles reflection passes drew (frame alpha 0x80)
        int pass = -1;               // the draw being emitted (Written::pass)
        // Base-pass vertex alpha by position, for the road sheen mask. Made from these draws the first time a pass
        // needs it: most nodes have no sheen, and making it decodes each of their packets once more.
        std::map<std::array<float, 3>, u32> base_alpha;
        bool base_alpha_made = false;
        auto make_base_alpha = [&] {
            if (base_alpha_made) return;
            base_alpha_made = true;
            for (const Draw& d : draws) {
                if ((g.chunks[d.chunk].flags & 0x1F) == 4 || d.fb_mask) continue; // not the sheen itself
                for (const Packet& pk : g.chunks[d.chunk].packets) {
                    DecodedPacket bp = g.decode_packet(pk);
                    // ALPHA 0x58 only matters with blending on (PRIM.ABE): an opaque packet drawn
                    // while the register still holds 0x58 is an ordinary base pass.
                    if (d.alpha_mode == 0x58 && bp.prim.abe) continue;
                    for (const scene::Vertex& v : bp.vertices) base_alpha[{v.pos[0], v.pos[1], v.pos[2]}] = v.color >> 24;
                }
            }
        };
        // static_env_maps: the last alpha-only textured pass (a destination-alpha mask) as a
        // white texture carrying the mask alpha plus one triangle of its geometry for the
        // planar position -> UV mapping.
        struct AlphaMask {
            int texture = -1;
            float pos[3][3] = {};
            float uv[3][2] = {};
        } mask;
        auto mask_uv = [&](const float* p, float* uv) {
            float e1[3], e2[3], d[3];
            for (int k = 0; k < 3; ++k)
                e1[k] = mask.pos[1][k] - mask.pos[0][k], e2[k] = mask.pos[2][k] - mask.pos[0][k], d[k] = p[k] - mask.pos[0][k];
            auto dot = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
            float a11 = dot(e1, e1), a12 = dot(e1, e2), a22 = dot(e2, e2), b1 = dot(d, e1), b2 = dot(d, e2);
            float det = a11 * a22 - a12 * a12;
            float s = det != 0 ? (b1 * a22 - b2 * a12) / det : 0, t = det != 0 ? (b2 * a11 - b1 * a12) / det : 0;
            for (int k = 0; k < 2; ++k)
                uv[k] = mask.uv[0][k] + s * (mask.uv[1][k] - mask.uv[0][k]) + t * (mask.uv[2][k] - mask.uv[0][k]);
        };
        auto place = [](const Draw& d, float* p) {
            if (!d.has_matrix) return;
            const Matrix& m = d.matrix;
            float q[3];
            for (int j = 0; j < 3; ++j) q[j] = p[0] * m[j] + p[1] * m[4 + j] + p[2] * m[8 + j] + m[12 + j];
            for (int j = 0; j < 3; ++j) p[j] = q[j];
        };
        for (const Draw& d : draws) {
            ++pass;
            if (ranges) ranges->push_back({mesh.vertices.size(), mesh.indices.size(), mesh.primitives.size()});
            // Cars: FBMSK 0xFFFFFFFF writes only the depth (glass drawn without depth writes leaves
            // its depth so, after its reflection): scene::kDepthOnly.
            const bool depth_only = opt.weighted_reflection && d.fb_mask == 0xFFFFFFFFu;
            if (depth_only && !d.depth_write) continue;
            // Alpha-only frame buffer passes (shadow masks) produce no colour.
            const bool alpha_only = (d.fb_mask & 0x00FFFFFFu) == 0x00FFFFFFu && !depth_only;
            // Cars: an alpha-only pass sets the reflectivity (the frame alpha, texel x vertex alpha)
            // that the reflection passes after it over the same vertices are weighted with. It is
            // drawn over the glass, which keeps whatever alpha lay behind it.
            if (alpha_only && opt.weighted_reflection) {
                for (const Packet& pk : g.chunks[d.chunk].packets) {
                    u32 tex = pk.mat & 0x1FF;
                    DecodedPacket dp = g.decode_packet(pk);
                    int t = dp.prim.tme && dp.has_uv && tex != 0 && tex != 0x1FF ? texture_for(d.texset, tex - 1) : -1;
                    std::vector<scene::Vertex> placed;
                    std::vector<Written> ws;
                    for (scene::Vertex v : dp.vertices) {
                        if (d.has_matrix) place_vertex(d.matrix, v);
                        Written w;
                        w.pass = pass;
                        w.vertex_alpha = v.color >> 24;
                        w.weight = w.vertex_alpha;
                        w.texture = t;
                        w.uv[0] = v.uv[0], w.uv[1] = v.uv[1];
                        if (t >= 0) w.weight = w.weight * texel_alpha(model.textures[size_t(t)], v.uv[0], v.uv[1]) / 255;
                        written_alpha[{v.pos[0], v.pos[1], v.pos[2]}] = w;
                        placed.push_back(v);
                        ws.push_back(w);
                    }
                    size_t s0 = 0;
                    for (u32 len : dp.strips) {
                        for (size_t i = 0; i + 2 < len && s0 + i + 2 < placed.size(); ++i) {
                            size_t a = s0 + i, b = a + 1, c = a + 2;
                            write_tri({placed[a].pos, placed[b].pos, placed[c].pos}, {ws[a], ws[b], ws[c]}, false, nullptr);
                        }
                        s0 += len;
                    }
                }
                continue;
            }
            if (alpha_only && !opt.static_env_maps && !opt.frame_alpha) continue;
            if (alpha_only && !opt.frame_alpha) {
                for (const Packet& pk : g.chunks[d.chunk].packets) {
                    u32 tex = pk.mat & 0x1FF;
                    DecodedPacket dp = g.decode_packet(pk);
                    if (!dp.prim.tme || !dp.has_uv || tex == 0 || tex == 0x1FF || dp.vertices.size() < 3) continue;
                    int src = texture_for(d.texset, tex - 1);
                    if (src < 0) continue;
                    scene::Texture t = model.textures[size_t(src)];
                    for (u32& c : t.rgba) c |= 0x00FFFFFFu;
                    t.name += "_mask";
                    model.textures.push_back(std::move(t));
                    mask.texture = int(model.textures.size() - 1);
                    for (int k = 0; k < 3; ++k) {
                        std::memcpy(mask.pos[k], dp.vertices[size_t(k)].pos, 12);
                        place(d, mask.pos[k]);
                        mask.uv[k][0] = dp.vertices[size_t(k)].uv[0], mask.uv[k][1] = dp.vertices[size_t(k)].uv[1];
                    }
                    break;
                }
                continue;
            }
            for (const Packet& pk : g.chunks[d.chunk].packets) {
                u32 tex = pk.mat & 0x1FF;       // texture index + 1, 0x1FF = runtime slot
                u32 mat = pk.mat >> 9;          // material index + 1, 0x7F = runtime slot
                // The chunk kind (flag bits 0-4) picks the VU program: 0 = pre-lit (the vertex colour
                // passed through), 4 = the road sheen's specular-only lighting
                // (scene::Material::specular_local).
                const u32 kind = g.chunks[d.chunk].flags & 0x1F;
                DecodedPacket dp = g.decode_packet(pk, kind == 0);
                // Course passes blended with ALPHA 0x58 (Cs*Ad + Cd): the road sheen (kind 4) and
                // other blended packets drawn with it, added weighted by the frame-buffer alpha the
                // base pass left. Opaque packets drawn while ALPHA still holds 0x58 (e.g. the
                // Laguna / Rome road base passes) are ordinary opaque geometry.
                const bool sheen = opt.road_sheen && kind == 4;
                const bool dest_add = opt.road_sheen && (sheen || (d.alpha_mode == 0x58 && dp.prim.abe));
                if (dest_add) make_base_alpha();
                // Cars: specular passes drawn with ALPHA 0x58 add over the body weighted by the
                // alpha its pass wrote (the per-vertex Written weight): the kind-4 glints and the lit
                // textured overlays lit by their specular term only (Ma = Md = 0: the Calsonic's
                // headlamp lenses, the Escudo's grilles, the 787B's decks). Not reflection passes.
                const bool car_glint =
                    opt.weighted_reflection && !dp.env_normals && d.alpha_mode == 0x58 && dp.prim.abe;
                // Cars: ALPHA 0x48 (Cs*As + Cd) adds to what is behind. Lamp glows drawn so: the RX-8's lit
                // brake-lamp rings (a red ring on black, issue 379), the dusk and night cars' lamp lenses.
                const bool car_add =
                    opt.weighted_reflection && !dp.env_normals && d.alpha_mode == 0x48 && dp.prim.abe;
                // Reflection passes carry a normal in the UV slot; the VU derives the env-map coordinates.
                bool env = dp.env_normals;
                if (env && !opt.reflection) continue;
                bool weighted = opt.weighted_reflection && env && d.alpha_mode == 0x58;
                u32 base = u32(mesh.vertices.size());
                auto prepare = [&](scene::Vertex v, const scene::Vertex& k0) {
                    if (car_glint) {
                        auto it = written_alpha.find({k0.pos[0], k0.pos[1], k0.pos[2]});
                        u32 a = it != written_alpha.end() ? std::min<u32>(255, it->second.weight) : 0u;
                        v.color = (v.color & 0x00FFFFFFu) | (a << 24);
                    }
                    if (dest_add) {
                        auto it = base_alpha.find({k0.pos[0], k0.pos[1], k0.pos[2]});
                        v.color = (v.color & 0x00FFFFFFu) | ((it != base_alpha.end() ? it->second : 0u) << 24);
                    }
                    if (d.has_matrix) place_vertex(d.matrix, v);
                    return v;
                };
                for (size_t i = 0; i < dp.vertices.size(); ++i) {
                    mesh.vertices.push_back(prepare(dp.vertices[i], dp.vertices[i]));
                    if (key1) key1->push_back(dp.key1.empty() ? mesh.vertices.back() : prepare(dp.key1[i], dp.vertices[i]));
                }
                scene::Primitive prim;
                prim.first_index = u32(mesh.indices.size());
                u32 strip_start = base;
                for (u32 len : dp.strips) {
                    for (u32 i = 0; i + 2 < len; ++i) {
                        u32 a = strip_start + i, b = strip_start + i + 1, c = strip_start + i + 2;
                        if (i & 1) std::swap(a, b);
                        auto degenerate = [&](const std::vector<scene::Vertex>& vs) {
                            const float* pa = vs[a].pos;
                            const float* pb = vs[b].pos;
                            const float* pc = vs[c].pos;
                            return std::memcmp(pa, pb, 12) == 0 || std::memcmp(pb, pc, 12) == 0 ||
                                   std::memcmp(pa, pc, 12) == 0;
                        };
                        if (degenerate(mesh.vertices) && (!key1 || dp.key1.empty() || degenerate(*key1)))
                            continue; // degenerate stitching triangle (in both key frames)
                        mesh.indices.insert(mesh.indices.end(), {a, b, c});
                    }
                    strip_start += len;
                }
                prim.index_count = u32(mesh.indices.size()) - prim.first_index;
                if (dp.prim.abe) prim.flags |= scene::kAlphaBlend;
                // ALPHA 0x89 = (Cd - 0) * As + 0: darkens what is behind by the fragment alpha.
                if (dp.prim.abe && d.alpha_mode == 0x89) prim.flags = scene::kMultiplyDest | scene::kNoDepthWrite;
                if (env) {
                    prim.flags |= scene::kEnvironmentMap;
                    prim.env_rows = true;
                    for (int k = 0; k < 3; ++k) prim.env_c0[k] = d.env_c0[size_t(k)], prim.env_c1[k] = d.env_c1[size_t(k)];
                }
                if (dp.has_colors && !dp.has_normals && !env) prim.flags |= scene::kUnlit;
                // Lit packets: the VU lights them with the selected paint's material (GL
                // defaults when the packet has none); chunk header byte 9 bit 0 = 255 scale.
                if (dp.has_normals && !dp.has_colors && !env) {
                    const std::vector<Material>& mats = g.materials_for(opt.computed_paint ? opt.computed_paint->finish : opt.paint);
                    scene::Material& sm = prim.material;
                    sm.valid = true;
                    if (mat != 0 && mat != 0x7F && mat - 1 < mats.size()) {
                        const Material& m = mats[mat - 1];
                        for (int k = 0; k < 3; ++k) {
                            sm.ambient[k] = m.rows[0][k], sm.diffuse[k] = m.rows[1][k];
                            sm.specular[k] = m.rows[2][k];
                            // Lamp materials: emission x (lamps on / 4), applied per draw.
                            sm.emission[k] = (m.flags & 1) ? 0.0f : m.rows[3][k];
                            sm.lamp_emission[k] = (m.flags & 1) ? m.rows[3][k] : 0.0f;
                        }
                        sm.shininess = m.shininess;
                    }
                    if ((g.chunks[d.chunk].flags >> 8) & 1) sm.scale = 255.0f / 128.0f;
                    sm.specular_local = kind == 4;
                }
                // 0x1FF is a runtime texture slot (op 0x29; the game fills it, e.g. the wheel rim image):
                // opt.runtime_set's image, else the set's first texture, the closest static stand-in.
                if (dp.prim.tme && dp.has_uv && tex != 0 && !env) {
                    // A set that is not a Tex1 binds no texture and the GS draws with an earlier draw's.
                    // At courses/EU/L_apricot1's reflection dome GT3's cars then reflect no sky (the UK
                    // copy's dome is textured): not drawn, so the reflection map's black stays.
                    if (opt.textures && set_without_textures(d.texset)) {
                        mesh.indices.resize(prim.first_index);
                        continue;
                    }
                    prim.texture = tex == 0x1FF ? runtime_texture(d.texset) : texture_for(d.texset, tex - 1);
                }
                if (opt.course_env_maps && env && (kind == 2 || kind == 3) && dp.prim.tme && tex != 0) {
                    // The packet's own texture; the runtime slot 0x1FF is empty while the course is
                    // drawn, so the GS keeps the texture the packet before bound.
                    prim.texture = tex != 0x1FF ? texture_for(d.texset, tex - 1) : gs_texture;
                    prim.env_kind = u8(kind);
                    prim.env_alpha[0] = d.env_c0[3], prim.env_alpha[1] = d.env_c1[3];
                    prim.env_point = d.pano_set;
                    for (int k = 0; k < 3; ++k) prim.env_point_pos[k] = d.pano[size_t(k)];
                }
                if (dp.prim.tme && tex != 0 && tex != 0x1FF && prim.texture >= 0) gs_texture = prim.texture;
                if (opt.static_env_maps) {
                    // Env-map pass whose sphere map is in the file (prize scenes): bind it; the
                    // renderer derives the UVs from the view-space normal.
                    if (env && dp.prim.tme && tex != 0 && tex != 0x1FF) prim.texture = texture_for(d.texset, tex - 1);
                    // ALPHA 0x48 (Cs*As + Cd) and 0x58 (Cs*Ad + Cd) add to the frame buffer.
                    if (dp.prim.abe && (d.alpha_mode == 0x48 || d.alpha_mode == 0x58))
                        prim.flags = (prim.flags & ~u32(scene::kAlphaBlend)) | scene::kAdditive | scene::kNoDepthWrite;
                    // ALPHA 0x54 ((Cs - Cd) * Ad + Cd) over a destination-alpha mask (the license
                    // card emblem): drawn as the mask texture (white, mask alpha) on the pass's
                    // geometry, coloured with the pass texture's mean colour (its env-map shine
                    // is constant over a flat face).
                    if (dp.prim.abe && d.alpha_mode == 0x54 && mask.texture >= 0) {
                        u32 tint = 0xFFFFFFFFu;
                        if (prim.texture >= 0) {
                            const scene::Texture& t = model.textures[size_t(prim.texture)];
                            u64 sum[3] = {0, 0, 0};
                            for (u32 c : t.rgba)
                                for (int k = 0; k < 3; ++k) sum[k] += (c >> (8 * k)) & 0xFF;
                            size_t n = std::max<size_t>(1, t.rgba.size());
                            tint = 0xFF000000u | u32(sum[0] / n) | u32(sum[1] / n) << 8 | u32(sum[2] / n) << 16;
                        }
                        for (u32 vi = base; vi < mesh.vertices.size(); ++vi) {
                            scene::Vertex& v = mesh.vertices[vi];
                            mask_uv(v.pos, v.uv);
                            u32 out = 0xFF000000u;
                            for (int k = 0; k < 3; ++k)
                                out |= (((v.color >> (8 * k)) & 0xFF) * ((tint >> (8 * k)) & 0xFF) / 255) << (8 * k);
                            v.color = out;
                        }
                        prim.texture = mask.texture;
                        prim.flags = scene::kAlphaBlend | scene::kUnlit;
                    }
                }
                // Frame-buffer alpha this pass leaves behind (texel alpha x vertex alpha): the weight
                // for later reflection passes over the same vertices. Passes with the alpha channel
                // write-masked (car glass) leave whatever lies behind them, which a per-vertex model
                // cannot know; their own alpha is used as the estimate unless a real write exists.
                if (opt.weighted_reflection && !env && !depth_only) {
                    bool masked = (d.fb_mask & 0xFF000000u) != 0;
                    std::vector<Written> ws;
                    std::vector<u32> before; // the frame alpha at each vertex before this pass
                    // An opaque pass the GS alpha-tests (as cut out below).
                    const Draw* tested =
                        opt.program_alpha_test && d.alpha_test && prim.texture >= 0 && !dp.prim.abe ? &d : nullptr;
                    for (u32 vi = base; vi < mesh.vertices.size(); ++vi) {
                        const scene::Vertex& v = mesh.vertices[vi];
                        Written w;
                        w.pass = pass;
                        w.vertex_alpha = v.color >> 24;
                        w.weight = w.vertex_alpha;
                        w.texture = prim.texture;
                        w.uv[0] = v.uv[0], w.uv[1] = v.uv[1];
                        if (prim.texture >= 0)
                            w.weight = w.weight * texel_alpha(model.textures[size_t(prim.texture)], v.uv[0], v.uv[1]) / 255;
                        std::array<float, 3> key{v.pos[0], v.pos[1], v.pos[2]};
                        auto prev = written_alpha.find(key);
                        before.push_back(prev != written_alpha.end() ? std::min<u32>(255, prev->second.weight) : 0u);
                        if (masked) written_alpha.emplace(key, w);
                        else written_alpha[key] = w;
                        ws.push_back(w);
                    }
                    for (u32 k = prim.first_index; k + 2 < prim.first_index + prim.index_count; k += 3) {
                        u32 a = mesh.indices[k], b = mesh.indices[k + 1], c = mesh.indices[k + 2];
                        write_tri({mesh.vertices[a].pos, mesh.vertices[b].pos, mesh.vertices[c].pos},
                                  {ws[a - base], ws[b - base], ws[c - base]}, masked, tested,
                                  {before[a - base], before[b - base], before[c - base]});
                    }
                }
                if (opt.program_alpha_test && d.alpha_test && prim.texture >= 0 && !dp.prim.abe) {
                    int cut = cutout_for(prim.texture, d.alpha_test_method, d.alpha_test_ref);
                    if (cut != prim.texture) prim.texture = cut, prim.flags |= scene::kAlphaTest;
                }
                // Car decals are drawn with depth writes off (op 0x25), colour only: the body's depth
                // stays under them, so the reflection passes after them add over the decal as over
                // the paint around it (weighted by the body's alpha, which the decal keeps).
                if (opt.weighted_reflection && !d.depth_write) prim.flags |= scene::kNoDepthWrite;
                bool gs_tested = false;
                if (opt.gs_state) {
                    if (!d.depth_write) prim.flags |= scene::kNoDepthWrite;
                    if (!d.cull) prim.flags |= scene::kDoubleSided;
                    // GS alpha test of the fragment alpha (texel x vertex alpha, 0x80 = 1.0):
                    // GREATER AREF / GEQUAL AREF as a "discard alpha <= ref" threshold.
                    u8 m = d.alpha_test_method;
                    if (d.alpha_test && prim.texture >= 0 && (m == 5 || m == 6)) {
                        float ref = (float(d.alpha_test_ref) + (m == 6 ? 0.5f : -0.5f)) / 128.0f;
                        if (ref < 0) {
                            gs_tested = true; // GEQUAL 0: every fragment passes
                        } else if (dp.prim.abe && d.alpha_fail == 3) {
                            prim.flags |= scene::kAlphaTest | scene::kAlphaFailBlend;
                            prim.alpha_ref = ref;
                            gs_tested = true;
                        } else if (d.alpha_fail == 0) {
                            // Only where something can fail (keeps early depth for solid packets).
                            u32 cut = u32(ref * 255.0f);
                            bool fails = lowest_alpha(prim.texture) <= cut;
                            for (u32 vi = base; vi < mesh.vertices.size() && !fails; ++vi)
                                fails = (mesh.vertices[vi].color >> 24) <= cut;
                            if (fails) prim.flags |= scene::kAlphaTest, prim.alpha_ref = ref;
                            gs_tested = true;
                        }
                    }
                }
                // Textures with transparent texels: the GS alpha test keeps those pixels out of the
                // colour and depth buffers (foliage cut-outs, fences, decal edges). With the GS state,
                // an opaque packet whose program turned the test off draws every texel: its low texel
                // alpha only reaches the frame alpha (Route 11's perforated panels: the reflectivity
                // of their kind-2 pass; Laguna's wall banners).
                const bool gs_whole = opt.gs_state && !d.alpha_test && !dp.prim.abe;
                if (!gs_tested && !gs_whole && opt.alpha_test && prim.texture >= 0 && lowest_alpha(prim.texture) < 128)
                    prim.flags |= scene::kAlphaTest;
                if (mat != 0 && mat != 0x7F && mat - 1 < g.materials.size() && (g.materials[mat - 1].flags & 1)) {
                    prim.flags |= kTintable;
                    if (opt.tint)
                        for (u32 vi = base; vi < mesh.vertices.size(); ++vi) {
                            u32& c = mesh.vertices[vi].color;
                            u32 out = c & 0xFF000000u;
                            for (int k = 0; k < 3; ++k) {
                                float ch = float((c >> (8 * k)) & 0xFF) * std::clamp((*opt.tint)[k], 0.f, 1.f);
                                out |= u32(ch + 0.5f) << (8 * k);
                            }
                            c = out;
                        }
                }
                if (dest_add) prim.flags = (prim.flags & ~u32(scene::kAlphaBlend)) | scene::kAdditive | scene::kDestAlphaAdd |
                                           scene::kNoDepthWrite;
                if (sheen) prim.flags &= ~u32(scene::kUnlit);
                if (car_glint) prim.flags = (prim.flags & ~u32(scene::kAlphaBlend)) | scene::kAdditive | scene::kNoDepthWrite;
                if (car_add) prim.flags = (prim.flags & ~u32(scene::kAlphaBlend)) | scene::kAdditive;
                if (opt.frame_alpha) {
                    if (alpha_only) { // writes only the frame alpha (texel x vertex alpha)
                        // (an alpha test only when the node program enables one: not the generic
                        // cut-out guess, which would drop the masks' low-alpha texels)
                        u32 keep = scene::kDoubleSided | (prim.alpha_ref >= 0 ? scene::kAlphaTest : 0u);
                        prim.flags = (prim.flags & keep) | scene::kAlphaOnly | scene::kUnlit | scene::kNoDepthWrite;
                    }
                    else if ((d.fb_mask & 0xFF000000u) == 0xFF000000u)
                        prim.flags |= scene::kKeepAlpha;
                }
                if (depth_only) prim.flags = (prim.flags & (scene::kAlphaTest | scene::kDoubleSided)) | scene::kDepthOnly | scene::kUnlit;
                if (dp.rgb_2x) prim.flags |= scene::kVertexColor2x;
                if (weighted) {
                    // TEST.DATE (op 0x1D): GT3 draws these passes with DATM 0, where the frame alpha < 0x80.
                    if (d.dest_alpha_test) prim.flags |= scene::kDestAlphaTest;
                    emit_weighted_reflection(mesh, prim, written_alpha, written_tris, reflected, pass,
                                             [&](const Corners& c) { return alpha_behind(c, pass, true); });
                    continue;
                }
                if (prim.index_count) mesh.primitives.push_back(prim);
            }
        }
        mark_inside_uvs(mesh, key1, &model.textures);
        for (const scene::Primitive& p : mesh.primitives)
            if (p.texture >= 0 && p.env_kind == 0 && !(p.flags & scene::kEnvironmentMap)) { // (reflections: alpha only)
                std::array<bool, 2>& r = repeats[p.texture];
                r[0] |= !(p.flags & scene::kClampU), r[1] |= !(p.flags & scene::kClampV);
            }
    };

    // A part: one draw's vertices, indices and primitives of an emit() into `mesh` / `key1`,
    // between two of its ranges, in their own numbering.
    auto split = [](auto& part, const scene::Mesh& mesh, const std::vector<scene::Vertex>& key1, const Range& r0,
                    const Range& r1) {
        part.key0.assign(mesh.vertices.begin() + std::ptrdiff_t(r0.vertex), mesh.vertices.begin() + std::ptrdiff_t(r1.vertex));
        part.key1.assign(key1.begin() + std::ptrdiff_t(r0.vertex), key1.begin() + std::ptrdiff_t(r1.vertex));
        for (size_t k = r0.index; k < r1.index; ++k) part.indices.push_back(u32(mesh.indices[k] - r0.vertex));
        for (size_t k = r0.primitive; k < r1.primitive; ++k) {
            scene::Primitive q = mesh.primitives[k];
            q.first_index -= u32(r0.index);
            part.primitives.push_back(q);
        }
    };

    // Nodes the +0x3C animators move: drawn by `opt.animated`, from parts. Car morph draws: by
    // `opt.morphs`.
    const std::vector<u32> animated = opt.animated ? g.animated_nodes() : std::vector<u32>{};
    std::vector<Draw> morph_draws;
    auto morphed = [&](const Draw& d) {
        return d.phase_id != 0 && d.range_count != 0 && d.range_base + d.range_count <= g.chunks.size();
    };
    auto moving = [&](const Draw& d) {
        return opt.morphs && (morphed(d) || std::any_of(d.when.begin(), d.when.end(), [](s8 b) { return b >= 0; }));
    };
    Draw carried; // Options::carry_state: the render state the previous node left
    for (u32 node = 0; node < g.nodes.size(); ++node) {
        // With carry_state, nodes left out here (another opt.node, the animated ones) still run for
        // the state they leave. (The animated nodes' own parts start from the default state: the
        // course line, the one carry_state model, has no animators.)
        const bool skip = (opt.node >= 0 && u32(opt.node) != node) ||
                          std::binary_search(animated.begin(), animated.end(), node);
        if (skip && !opt.carry_state) continue;
        std::vector<Draw> draws = g.run(node, opt.run, opt.carry_state ? &carried : nullptr);
        if (skip) continue;
        if (opt.morphs) {
            for (const Draw& d : draws)
                if (moving(d)) morph_draws.push_back(d);
            draws.erase(std::remove_if(draws.begin(), draws.end(), moving), draws.end());
        }
        if (draws.empty()) continue;
        scene::Mesh mesh;
        mesh.name = "node" + std::to_string(node);
        mesh.node = int(node);
        mesh.lod = opt.run.lod;
        emit(draws, mesh, nullptr, nullptr);
        if (!mesh.primitives.empty()) model.meshes.push_back(std::move(mesh));
    }

    if (opt.animated) {
        AnimatedNodes& an = *opt.animated;
        an = AnimatedNodes{};
        // The programs run again every frame: a copy of the block up to their end.
        std::vector<u32> starts;
        for (u32 off : g.nodes) starts.push_back(off);
        std::sort(starts.begin(), starts.end());
        size_t end = 0;
        for (u32 n : animated) {
            auto next = std::upper_bound(starts.begin(), starts.end(), g.nodes[n]);
            end = std::max<size_t>(end, next == starts.end() ? g.data.size() : *next);
        }
        an.programs_ = std::make_shared<const Bytes>(g.data.begin(), g.data.begin() + std::ptrdiff_t(end));
        an.g_ = g;
        an.g_.data = *an.programs_;
        an.run_ = opt.run;
        an.run_.operands = nullptr;
        an.animators_ = g.animators;
        an.reset();
        // Chunk numbers the type 5 animators write (op 0x05 operands): every one they can draw.
        std::map<u32, std::pair<u32, u32>> flip;
        for (const Gtm1::Animator& a : g.animators)
            if (a.type == 5)
                for (u32 t : a.targets) flip[t] = {a.first, a.end};
        RunParams run = an.run_;
        run.operands = &an.operands_;
        for (u32 node : animated) {
            // Every chunk each draw can draw (the animated chunk range, the flip-book's frames),
            // in its own space with the draw's state: the parts.
            std::vector<Draw> parts;
            std::set<AnimatedNodes::Key> seen;
            for (const Draw& d : g.run(node, run)) {
                std::vector<u32> chunks{d.chunk};
                if (d.range_count) {
                    chunks.clear();
                    for (u32 k = 0; k < d.range_count; ++k) chunks.push_back(d.range_base + k);
                } else if (auto f = flip.find(d.op + 1); f != flip.end()) {
                    chunks.clear();
                    for (u32 c = f->second.first; c < f->second.second && c < g.chunks.size(); ++c) chunks.push_back(c);
                }
                for (u32 c : chunks) {
                    Draw p = d;
                    p.chunk = c;
                    p.matrix = kIdentity, p.has_matrix = false, p.morph = 0;
                    AnimatedNodes::Key k = AnimatedNodes::key(p);
                    if (!an.parts_.count(k) && seen.insert(k).second) parts.push_back(p);
                }
            }
            if (parts.empty()) {
                an.nodes_.push_back(node);
                continue;
            }
            scene::Mesh mesh;
            std::vector<Range> ranges;
            std::vector<scene::Vertex> key1;
            emit(parts, mesh, &ranges, &key1);
            ranges.push_back({mesh.vertices.size(), mesh.indices.size(), mesh.primitives.size()});
            for (size_t i = 0; i + 1 < ranges.size(); ++i) {
                AnimatedNodes::Part part;
                split(part, mesh, key1, ranges[i], ranges[i + 1]);
                an.parts_[AnimatedNodes::key(parts[i])] = std::move(part);
            }
            an.nodes_.push_back(node);
        }
    }

    if (opt.morphs) {
        MorphDraws& md = *opt.morphs;
        md = MorphDraws{};
        md.lod_ = opt.run.lod;
        // Every chunk of each draw's range (a draw whose phase is not the callback's: its own chunk), in its own space
        // with the draw's state.
        std::vector<Draw> parts;
        for (const Draw& d : morph_draws) {
            const u32 n = morphed(d) ? d.range_count : 1;
            for (u32 k = 0; k < n; ++k) {
                Draw p = d;
                if (morphed(d)) p.chunk = d.range_base + k;
                p.matrix = kIdentity, p.has_matrix = false, p.morph = 0;
                parts.push_back(p);
            }
        }
        scene::Mesh mesh;
        std::vector<Range> ranges;
        std::vector<scene::Vertex> key1;
        emit(parts, mesh, &ranges, &key1);
        ranges.push_back({mesh.vertices.size(), mesh.indices.size(), mesh.primitives.size()});
        size_t r = 0;
        for (const Draw& d : morph_draws) {
            MorphDraws::Entry e;
            e.id = morphed(d) ? d.phase_id : 0;
            e.when = d.when;
            e.morph = morphed(d) ? 0.0f : d.morph;
            e.matrix = d.matrix, e.has_matrix = d.has_matrix;
            e.chunks.resize(morphed(d) ? d.range_count : 1);
            for (MorphDraws::Part& part : e.chunks) split(part, mesh, key1, ranges[r], ranges[r + 1]), ++r;
            md.draws_.push_back(std::move(e));
        }
    }
    for (int i : nearest_cuts) {
        scene::Texture& t = model.textures[size_t(i)];
        const std::array<bool, 2> r = repeats.count(i) ? repeats[i] : std::array<bool, 2>{false, false};
        colour_cut_texels(t, r[0] && !t.clamp_u, r[1] && !t.clamp_v);
    }
    model.compute_bounds();
    return model;
}

namespace {

// Appends a part to `mesh`, its key frames blended by `w` ((1 - w) key 0 + w key 1) and placed by
// the draw's matrix.
template <class P>
void append_part(scene::Mesh& mesh, const P& p, float w, const Matrix& m, bool has_matrix) {
    const u32 base = u32(mesh.vertices.size()), first = u32(mesh.indices.size());
    for (size_t k = 0; k < p.key0.size(); ++k) {
        scene::Vertex v = p.key0[k];
        if (w != 0) {
            const scene::Vertex& b = p.key1[k];
            for (int j = 0; j < 3; ++j)
                v.pos[j] += (b.pos[j] - v.pos[j]) * w, v.normal[j] += (b.normal[j] - v.normal[j]) * w;
            for (int j = 0; j < 2; ++j) v.uv[j] += (b.uv[j] - v.uv[j]) * w;
            u32 c = 0;
            for (int s = 0; s < 32; s += 8) {
                float c0 = float((v.color >> s) & 0xFF), c1 = float((b.color >> s) & 0xFF);
                c |= u32(c0 + (c1 - c0) * w) << s;
            }
            v.color = c;
        }
        if (has_matrix) place_vertex(m, v);
        mesh.vertices.push_back(v);
    }
    for (u32 i : p.indices) mesh.indices.push_back(base + i);
    for (scene::Primitive q : p.primitives) {
        q.first_index += first;
        mesh.primitives.push_back(q);
    }
}

} // namespace

AnimatedNodes::Key AnimatedNodes::key(const Draw& d) {
    auto bits = [](float f) {
        u32 v;
        std::memcpy(&v, &f, 4);
        return v;
    };
    Key k{d.chunk, d.texset, d.alpha_mode, d.fb_mask, d.alpha_test, d.alpha_test_method, d.alpha_test_ref,
          d.dest_alpha_test, d.alpha_fail, d.depth_write, d.depth_test, d.cull};
    for (int i = 0; i < 4; ++i) k.push_back(bits(d.env_c0[size_t(i)])), k.push_back(bits(d.env_c1[size_t(i)]));
    k.push_back(d.pano_set);
    for (float f : d.pano) k.push_back(bits(f));
    return k;
}

void AnimatedNodes::reset() {
    values_.assign(animators_.size(), 0.0f);
    for (size_t i = 0; i < animators_.size(); ++i) values_[i] = animators_[i].start;
    update(0.0f);
}

void AnimatedNodes::update(float seconds) {
    for (size_t i = 0; i < animators_.size(); ++i) {
        const Gtm1::Animator& a = animators_[i];
        float& v = values_[i];
        u32 out = 0;
        if (a.type >= 1 && a.type <= 4) { // an angle, wrapped once into 0..360
            v += a.speed * seconds;
            if (v > 360.0f) v -= 360.0f;
            else if (v < 0.0f) v += 360.0f;
            std::memcpy(&out, &v, 4);
        } else if (a.type == 5) { // a chunk number: the phase rounded, first..end-1
            v += a.speed * seconds;
            s32 frame = s32(double(v) + 0.5);
            if (frame >= s32(a.end)) {
                v -= float(s32(a.end) - s32(a.first));
                frame -= s32(a.end) - s32(a.first);
            }
            out = u32(frame) & 0xFFFF;
        } else if (a.type == 7) { // a phase, wrapped into 0..1
            v += a.speed * seconds;
            if (v < 0.0f) {
                do v += 1.0f;
                while (v < 0.0f);
            } else {
                while (v >= 1.0f) v -= 1.0f;
            }
            std::memcpy(&out, &v, 4);
        } else {
            continue; // (type 6: none on the disc)
        }
        for (u32 t : a.targets) operands_[t] = out;
    }
}

std::vector<scene::Mesh> AnimatedNodes::meshes() const {
    RunParams r = run_;
    r.operands = &operands_;
    std::vector<scene::Mesh> out;
    for (u32 n : nodes_) {
        scene::Mesh mesh;
        mesh.name = "node" + std::to_string(n);
        mesh.node = int(n);
        mesh.lod = run_.lod;
        std::vector<Draw> draws;
        try {
            draws = g_.run(n, r);
        } catch (const FormatError&) {
            continue; // (a value its data does not cover, e.g. a chunk number past the table)
        }
        for (const Draw& d : draws)
            if (auto it = parts_.find(key(d)); it != parts_.end()) append_part(mesh, it->second, d.morph, d.matrix, d.has_matrix);
        if (!mesh.primitives.empty()) out.push_back(std::move(mesh));
    }
    return out;
}

void AnimatedNodes::rebase_textures(int base) {
    for (auto& [k, p] : parts_)
        for (scene::Primitive& q : p.primitives)
            if (q.texture >= 0) q.texture += base;
}

std::vector<int> MorphDraws::ids() const {
    std::vector<int> out;
    for (const Entry& e : draws_)
        if (e.id) out.push_back(e.id);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<int> MorphDraws::branch_ids() const {
    std::vector<int> out;
    for (const Entry& e : draws_)
        for (u32 i = 0; i < kCallbackIds; ++i)
            if (e.when[i] >= 0) out.push_back(int(i));
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<scene::Mesh> MorphDraws::meshes(const CarCallback& values) const {
    scene::Mesh mesh;
    mesh.name = "morph";
    mesh.lod = lod_;
    for (const Entry& e : draws_) {
        bool taken = true;
        for (u32 i = 0; i < kCallbackIds; ++i)
            if (e.when[i] >= 0 && e.when[i] != values.branch[i]) taken = false;
        if (!taken) continue;
        if (e.id == 0) { // the draw's own chunk
            append_part(mesh, e.chunks[0], e.morph, e.matrix, e.has_matrix);
            continue;
        }
        // As op 0x35: chunk min(phase x count, count - 1) (truncated), morphed by the rest.
        const float phase = std::clamp(values.phase[e.id], 0.0f, 1.0f);
        const s32 count = s32(e.chunks.size());
        const float f = phase * float(count);
        s32 k = s32(f);
        if (!(k < count)) k = count - 1;
        append_part(mesh, e.chunks[size_t(k)], f - float(k), e.matrix, e.has_matrix);
    }
    if (mesh.primitives.empty()) return {};
    return {std::move(mesh)};
}

void MorphDraws::rebase_textures(int base) {
    for (Entry& e : draws_)
        for (Part& p : e.chunks)
            for (scene::Primitive& q : p.primitives)
                if (q.texture >= 0) q.texture += base;
}

std::vector<int> MorphDraws::textures() const {
    std::vector<int> out;
    for (const Entry& e : draws_)
        for (const Part& p : e.chunks)
            for (const scene::Primitive& q : p.primitives)
                if (q.texture >= 0) out.push_back(q.texture);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void MorphDraws::map_textures(const std::vector<int>& map) {
    for (Entry& e : draws_)
        for (Part& p : e.chunks)
            for (scene::Primitive& q : p.primitives)
                if (q.texture >= 0 && size_t(q.texture) < map.size()) q.texture = map[size_t(q.texture)];
}

scene::Model decode_gtm1(ByteSpan gtm1, const Options& opt, const std::string& name) {
    return build_model(Gtm1::parse(gtm1), opt, name);
}

} // namespace gt3r::gtm1
