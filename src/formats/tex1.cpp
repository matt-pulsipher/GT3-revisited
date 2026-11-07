#include "formats/tex1.h"

#include <algorithm>
#include <cstring>

namespace gt3r {

bool Tex1::is_tex1(ByteSpan data) { return has_magic(data, "Tex1") && data.size() >= 0x20; }

namespace {

// The CLUT animation table (header +0x24). A table that does not fit the set is ignored.
std::vector<Tex1::ClutAnimation> parse_clut_animations(ByteSpan d) {
    std::vector<Tex1::ClutAnimation> out;
    if (d.size() < 0x28) return out;
    u32 table = rd32(d, 0x24);
    if (table == 0 || table > d.size() - 8) return out;
    u32 count = rd32(d, table) & 0xFFFF, entries = rd32(d, table + 4);
    if (entries > d.size() || (d.size() - entries) / 0x20 < count) return {};
    auto fits = [&](u32 off, u64 bytes) { return off <= d.size() && bytes <= d.size() - off; };
    for (u32 i = 0; i < count; ++i) {
        u32 e = entries + 0x20 * i;
        Tex1::ClutAnimation a;
        a.group = rd16(d, e), a.mode = rd16(d, e + 2), a.words = rd16(d, e + 4);
        u32 frames = rd16(d, e + 6);
        a.target = rd32(d, e + 8);
        u32 table_off = rd32(d, e + 12);
        if (frames == 0 || a.words == 0 || !fits(a.target, 4ull * a.words) || !fits(table_off, 8ull * frames)) return {};
        for (u32 f = 0; f < frames; ++f) {
            u32 bits = rd32(d, table_off + 8 * f);
            Tex1::ClutAnimation::Frame fr;
            std::memcpy(&fr.seconds, &bits, 4);
            fr.source = rd32(d, table_off + 8 * f + 4);
            if (!fits(fr.source, 4ull * a.words)) return {};
            for (u32 w = 0; w < a.words; ++w) a.colours.push_back(rd32(d, fr.source + 4 * w));
            a.frames.push_back(fr);
        }
        out.push_back(std::move(a));
    }
    return out;
}

} // namespace

bool indexed_ct32(const Tex0& t) {
    bool idx = t.psm == u32(Psm::T4) || t.psm == u32(Psm::T4HL) || t.psm == u32(Psm::T4HH) ||
               t.psm == u32(Psm::T8) || t.psm == u32(Psm::T8H);
    return idx && (t.cpsm == u32(Psm::CT32) || t.cpsm == u32(Psm::CT24)) && t.csm == 0;
}

void csm1_position(u32 e, u32& x, u32& y) {
    x = (e & 7) | ((e & 0x10) >> 1);
    y = ((e >> 5) << 1) | ((e >> 3) & 1);
}

u32 clut_rgba(u32 c, u32 cpsm) {
    u32 a = std::min<u32>(255, ((c >> 24) * 255 + 64) / 128);
    return (c & 0x00FFFFFFu) | (cpsm == u32(Psm::CT24) ? 0xFF000000u : a << 24);
}

Tex1 Tex1::parse(ByteSpan data) {
    if (!is_tex1(data)) throw FormatError("not a Tex1 texture set");
    Tex1 t;
    u32 file_size = rd32(data, 0x0C);
    if (file_size > data.size()) throw FormatError("Tex1 truncated");
    u16 ntex = rd16(data, 0x14);
    u16 nbuf = rd16(data, 0x16);
    u32 tex_off = rd32(data, 0x18);
    u32 buf_off = rd32(data, 0x1C);
    for (u32 i = 0; i < ntex; ++i) {
        size_t o = tex_off + size_t(i) * 0x28;
        Texture tx;
        tx.tex0 = Tex0::decode(rd64(data, o));
        tx.tex1 = rd64(data, o + 0x08);
        tx.clamp = Clamp::decode(rd64(data, o + 0x20));
        t.textures.push_back(tx);
    }
    for (u32 i = 0; i < nbuf; ++i) {
        size_t o = buf_off + size_t(i) * 12;
        Buffer b;
        b.offset = rd32(data, o);
        b.dbp = rd16(data, o + 4);
        b.dbw = data[o + 6];
        b.dpsm = data[o + 7];
        b.width = rd16(data, o + 8);
        b.height = rd16(data, o + 10);
        t.buffers.push_back(b);
    }
    t.clut_animations = parse_clut_animations(data.first(file_size));
    return t;
}

void Tex1::upload(ByteSpan data, GsMemory& gs) const {
    for (const Buffer& b : buffers) {
        size_t bytes = size_t(b.width) * b.height * psm_bits(b.dpsm) / 8;
        if (b.offset > data.size() || data.size() - b.offset < bytes) throw FormatError("Tex1 buffer out of range");
        gs.upload(b.dbp, b.dbw, b.dpsm, b.width, b.height, data.subspan(b.offset, bytes));
    }
}

Tex1::Region Tex1::region(size_t i) const {
    const Texture& t = textures.at(i);
    Region r{0, 0, t.tex0.width(), t.tex0.height()};
    if (t.clamp.wms == 2 && t.clamp.maxu >= t.clamp.minu) r.x = t.clamp.minu, r.width = t.clamp.maxu - t.clamp.minu + 1;
    if (t.clamp.wmt == 2 && t.clamp.maxv >= t.clamp.minv) r.y = t.clamp.minv, r.height = t.clamp.maxv - t.clamp.minv + 1;
    return r;
}

Tex1::Image Tex1::decode(const GsMemory& gs, size_t i) const {
    const Region r = region(i);
    Image img;
    img.width = r.width;
    img.height = r.height;
    img.rgba = gs.decode_rgba(textures.at(i).tex0, img.width, img.height, r.x, r.y);
    return img;
}

std::vector<size_t> find_tex1_sets(ByteSpan data) {
    std::vector<size_t> out;
    for (size_t o = 0; o + 0x20 <= data.size(); o += 16) { // sets are 16-byte aligned
        if (std::memcmp(&data[o], "Tex1", 4) != 0) continue;
        u32 size = rd32(data, o + 0x0C);
        if (size >= 0x20 && size <= data.size() - o) out.push_back(o);
    }
    return out;
}

} // namespace gt3r
