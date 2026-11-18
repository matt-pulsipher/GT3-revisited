#include "formats/sound/inst.h"

#include <string>

namespace gt3r::sound {

InstBank InstBank::parse(ByteSpan d) {
    if (!has_magic(d, "INST") || d.size() < 0x28) throw FormatError("not an INST bank");
    u32 data_size = rd32(d, 0x10);
    u32 data_off = rd32(d, 0x14);
    u32 nreg = rd32(d, 0x18), reg_tab = rd32(d, 0x1C);
    u32 nprog = rd32(d, 0x20), prog_tab = rd32(d, 0x24);
    if (u64(data_off) + data_size > d.size()) throw FormatError("INST: data out of range");
    if (nreg > 4096 || u64(reg_tab) + 24ull * nreg > d.size()) throw FormatError("INST: bad region table");
    if (nprog > 1024 || u64(prog_tab) + 4ull * nprog > d.size()) throw FormatError("INST: bad program table");

    InstBank b;
    b.adpcm = std::make_shared<const Bytes>(d.begin() + data_off, d.begin() + data_off + data_size);
    for (u32 i = 0; i < nreg; ++i) {
        size_t o = reg_tab + 24 * i;
        InstRegion r;
        r.address = rd32(d, o) * 8;
        r.volume = s16(rd16(d, o + 6));
        r.center = rd16(d, o + 8);
        r.duration = rd16(d, o + 0x0A);
        r.priority = d[o + 0x0C];
        r.pan = d[o + 0x0D];
        r.flags = d[o + 0x0E];
        r.key_lo = d[o + 0x10];
        r.key_hi = d[o + 0x11];
        r.bend_down = d[o + 0x12];
        r.bend_up = d[o + 0x13];
        r.adsr1 = rd16(d, o + 0x14);
        r.adsr2 = rd16(d, o + 0x16);
        if (r.address >= data_size) throw FormatError("INST: region address out of range");
        b.regions.push_back(r);
    }
    for (u32 i = 0; i < nprog; ++i) {
        u32 o = rd32(d, prog_tab + 4 * i);
        if (u64(o) + 4 > d.size()) throw FormatError("INST: bad program offset");
        InstProgram p;
        u8 n = d[o];
        p.pan = d[o + 1];
        p.volume = s16(rd16(d, o + 2));
        if (u64(o) + 4 + n > d.size()) throw FormatError("INST: program out of range");
        for (u8 k = 0; k < n; ++k) {
            u8 r = d[o + 4 + k];
            if (r >= nreg) throw FormatError("INST: program references missing region");
            p.regions.push_back(r);
        }
        b.programs.push_back(std::move(p));
    }
    return b;
}

std::vector<SoundEffect> parse_se_index(ByteSpan d) {
    if (!has_magic(d, "GTSE") || d.size() < 0x10) throw FormatError("not a GTSE index");
    u32 n = rd32(d, 0x0C);
    if (n > 4096 || 0x10ull + 8ull * n > d.size()) throw FormatError("GTSE: bad count");
    std::vector<SoundEffect> out;
    for (u32 i = 0; i < n; ++i) {
        u32 name_off = rd32(d, 0x10 + 8 * i);
        u32 v = rd32(d, 0x14 + 8 * i);
        if (name_off >= d.size()) throw FormatError("GTSE: bad name offset");
        SoundEffect e;
        for (size_t p = name_off; p < d.size() && d[p]; ++p) e.name.push_back(char(d[p]));
        e.bank = u8(v >> 24);
        e.first = u16((v >> 12) & 0xFFF);
        e.last = u16(v & 0xFFF);
        if (e.last < e.first) throw FormatError("GTSE: bad region range");
        out.push_back(std::move(e));
    }
    return out;
}

const char* se_bank_path(u8 bank) {
    switch (bank) {
    case 0: return "sound/gt3sys.ins";
    case 1: return "sound/gt3se01.ins";
    case 2: return "sound/gt3se02.ins";
    case 3: return "sound/gt3pod.ins";
    default: return nullptr;
    }
}

} // namespace gt3r::sound
