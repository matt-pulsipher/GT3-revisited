#include <cstdlib>
#if !defined(_WIN32)
#include <execinfo.h>
#endif
#include "ee.h"

#include "program_map.h"
#include "ps2float.h"

#include <cstdio>
#include <cstring>

namespace gt3r::diff {

std::string hex32(u32 v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

static const char* trap_name(TrapKind k) {
    switch (k) {
    case TrapKind::Unimplemented: return "unimplemented opcode";
    case TrapKind::Syscall: return "syscall";
    case TrapKind::Break: return "break";
    case TrapKind::Trap: return "trap instruction";
    case TrapKind::BadAddress: return "bad address";
    case TrapKind::Unaligned: return "unaligned access";
    case TrapKind::CallNotAllowed: return "call not allowed";
    case TrapKind::WriteToText: return "write to .text";
    case TrapKind::Budget: return "instruction budget exhausted";
    }
    return "?";
}

EeTrap::EeTrap(TrapKind k, u32 pc_, u32 op_, const std::string& what)
    : std::runtime_error(std::string(trap_name(k)) + " at pc " + hex32(pc_) + " (op " + hex32(op_) + "): " + what),
      kind(k), pc(pc_), op(op_) {}

Arg Arg::flt(float v) { return {Float, 0, ps2f::from_host(v)}; }

float CallResult::f0_host() const { return ps2f::to_host(f0); }

Ee::Ee() : ram_(kRamSize, 0) {}

void Ee::load_program(ByteSpan image, u32 load_address, u32 text_end, u32 bss_start, u32 bss_end, u32 gp) {
    if (load_address + image.size() > kRamSize || bss_end > kRamSize) throw std::runtime_error("program does not fit");
    std::fill(ram_.begin(), ram_.end(), 0);
    std::memcpy(ram_.data() + load_address, image.data(), image.size());
    std::memset(ram_.data() + bss_start, 0, bss_end - bss_start);
    text_begin_ = load_address;
    text_end_ = text_end;
    gp_ = gp;
    pristine_ = ram_;
    reset();
}

void Ee::reset() {
    if (!pristine_.empty()) ram_ = pristine_;
    scratch_.fill(0);
    vu0mem_.fill(0);
    gpr = {};
    hi = lo = {};
    sa = 0;
    fpr = {};
    facc = fcr31 = 0;
    vf = {};
    vf[0][3] = 0x3F800000;
    vi = {};
    vacc = {};
    vq = vp = vi_reg = 0;
    vr = 0x3F800000;
    vstatus = vmac = vclip = 0;
    heap_ = kHeapBase;
}

u8* Ee::host_ptr(u32 addr, u32 size) {
    if (addr >= kScratchBase && addr < kScratchBase + kScratchSize) {
        if (addr + size > kScratchBase + kScratchSize) trap(TrapKind::BadAddress, "scratchpad overrun " + hex32(addr));
        return scratch_.data() + (addr - kScratchBase);
    }
    if (addr >= kVu0MemBase && addr < kVu0MemBase + kVu0MemSize) {
        if (addr + size > kVu0MemBase + kVu0MemSize) trap(TrapKind::BadAddress, "VU0 memory overrun " + hex32(addr));
        return vu0mem_.data() + (addr - kVu0MemBase);
    }
    u32 p = addr & 0x1FFFFFFF; // kseg0/kseg1 mirrors
    if (addr >= 0x20000000 && addr < 0x30000000) p = addr - 0x20000000; // uncached mirror
    if (addr >= 0x30000000 && addr < 0x32000000) p = addr - 0x30000000; // uncached accelerated
    if ((addr < 0x80000000u && addr >= 0x02000000 && !(addr >= 0x20000000 && addr < 0x32000000)) || p >= kRamSize ||
        p + size > kRamSize)
        trap(TrapKind::BadAddress, "access to " + hex32(addr));
    return ram_.data() + p;
}

u128 Ee::load128(u32 a) {
    a &= ~15u;
    u128 v;
    std::memcpy(&v.lo, host_ptr(a, 16), 8);
    std::memcpy(&v.hi, host_ptr(a, 16) + 8, 8);
    return v;
}

void Ee::store128(u32 a, u128 v) {
    a &= ~15u;
    check_write(a);
    std::memcpy(host_ptr(a, 16), &v.lo, 8);
    std::memcpy(host_ptr(a, 16) + 8, &v.hi, 8);
}

float Ee::rf(u32 a) { return ps2f::to_host(r32(a)); }
void Ee::wf(u32 a, float v) { w32(a, ps2f::from_host(v)); }

void Ee::write_bytes(u32 a, ByteSpan data) {
    if (!data.empty()) std::memcpy(host_ptr(a, u32(data.size())), data.data(), data.size());
}

void Ee::read_bytes(u32 a, u8* out, u32 n) {
    if (n) std::memcpy(out, host_ptr(a, n), n);
}

std::string Ee::read_cstr(u32 a, u32 max) {
    std::string s;
    for (u32 i = 0; i < max; ++i) {
        u8 c = r8(a + i);
        if (!c) break;
        s.push_back(char(c));
    }
    return s;
}

u32 Ee::alloc(u32 size, u32 align) {
    u32 p = (heap_ + align - 1) & ~(align - 1);
    if (p + size > kHeapEnd) throw std::runtime_error("diff harness heap exhausted");
    heap_ = p + size;
    std::memset(ram_.data() + p, 0, size);
    return p;
}

u32 Ee::alloc_cstr(std::string_view s) {
    u32 p = alloc(u32(s.size()) + 1);
    std::memcpy(ram_.data() + p, s.data(), s.size());
    ram_[p + s.size()] = 0;
    return p;
}

u32 Ee::alloc_bytes(ByteSpan data, u32 align) {
    u32 p = alloc(u32(data.size()), align);
    write_bytes(p, data);
    return p;
}

void Ee::hle(u32 address, std::function<void(Ee&)> fn, std::string name) {
    hle_[address] = {std::move(fn), std::move(name)};
}

void Ee::hle_libc_defaults() {
    // Addresses of the C runtime in the NTSC-U program (sdk_functions.tsv / labels.tsv), translated to the disc's (pa()).
    hle(pa(0x0027D990), [](Ee& e) { // memcpy(dst, src, n) -> dst
        u32 d = u32(e.a(0)), s = u32(e.a(1)), n = u32(e.a(2));
        for (u32 i = 0; i < n; ++i) e.w8(d + i, e.r8(s + i));
        e.set_v0(e.a(0));
    }, "memcpy");
    hle(pa(0x0027DA3C), [](Ee& e) { // memmove
        u32 d = u32(e.a(0)), s = u32(e.a(1)), n = u32(e.a(2));
        std::vector<u8> tmp(n);
        e.read_bytes(s, tmp.data(), n);
        for (u32 i = 0; i < n; ++i) e.w8(d + i, tmp[i]);
        e.set_v0(e.a(0));
    }, "memmove");
    hle(pa(0x0027DB38), [](Ee& e) { // memset(dst, c, n) -> dst
        u32 d = u32(e.a(0)), n = u32(e.a(2));
        for (u32 i = 0; i < n; ++i) e.w8(d + i, u8(e.a(1)));
        e.set_v0(e.a(0));
    }, "memset");
    hle(pa(0x00269A68), [](Ee& e) { // strlen
        u32 s = u32(e.a(0)), n = 0;
        while (e.r8(s + n)) ++n;
        e.set_v0(n);
    }, "strlen");
}

void Ee::trap(TrapKind k, const std::string& msg) {
#if !defined(_WIN32)
    // GT3R_TRAP_BACKTRACE=1: the host's call stack (a trap from the harness's own read or write reports the last
    // interpreted pc; the stack says which line of the test it was).
    if (std::getenv("GT3R_TRAP_BACKTRACE")) {
        void* frames[32];
        backtrace_symbols_fd(frames, backtrace(frames, 32), 2);
        std::fprintf(stderr, "EE: pc %08X ra %08X sp %08X a0 %08X a1 %08X a2 %08X a3 %08X s0 %08X s1 %08X s2 %08X s3 %08X; return addresses on the stack:",
                     cur_pc, u32(gpr[31].lo), u32(gpr[29].lo), u32(gpr[4].lo), u32(gpr[5].lo), u32(gpr[6].lo), u32(gpr[7].lo), u32(gpr[16].lo),
                     u32(gpr[17].lo), u32(gpr[18].lo), u32(gpr[19].lo));
        for (u32 a = u32(gpr[29].lo) & ~7u, n = 0; a + 8 <= kStackTop && n < 24; a += 8) {
            u32 w = 0;
            std::memcpy(&w, ram_.data() + (a & 0x1FFFFFFF), 4);
            if (w >= text_begin_ && w < text_end_ && !(w & 3)) std::fprintf(stderr, " %08X", w), ++n;
        }
        std::fprintf(stderr, "\n");
    }
#endif
    u32 op = 0;
    u32 p = cur_pc & 0x1FFFFFFF;
    if (p + 4 <= kRamSize) std::memcpy(&op, ram_.data() + p, 4);
    throw EeTrap(k, cur_pc, op, msg);
}

void Ee::note_watch(u32 a, u64 v) {
    std::fprintf(stderr, "watch: %08X <- %llX at pc %08X ($ra %08X)\n", a, static_cast<unsigned long long>(v), cur_pc, u32(gpr[31].lo));
}

void Ee::unimplemented(u32 op, const char* group) {
    char b[96];
    std::snprintf(b, sizeof b, "%s op=%08X (primary %u, funct %u)", group, op, op >> 26, op & 63);
    trap(TrapKind::Unimplemented, b);
}

void Ee::check_call(u32 target) {
    if (hle_.count(target)) return;
    if (target == kReturnAddr) return;
    if (!allowed_calls_.empty() && !allowed_calls_.count(target))
        trap(TrapKind::CallNotAllowed, "call to " + hex32(target));
    if (allowed_calls_.empty() && (target < text_begin_ || target >= text_end_))
        trap(TrapKind::CallNotAllowed, "call outside .text to " + hex32(target));
}

CallResult Ee::call(u32 address, const std::vector<Arg>& args, u64 max_instructions) {
    int ni = 0, nf = 0;
    for (const Arg& a : args) {
        if (a.kind == Arg::Int) {
            if (ni >= 8) throw std::runtime_error("more than 8 integer arguments (stack args unsupported)");
            gpr[4 + ni++] = {a.i, 0};
        } else {
            if (nf >= 8) throw std::runtime_error("more than 8 float arguments");
            fpr[12 + nf++] = a.f;
        }
    }
    gpr[0] = {};
    gpr[28].lo = gp_;
    gpr[29].lo = kStackTop;
    gpr[31].lo = kReturnAddr;
    pc = address;
    npc = address + 4;
    u64 start = icount;
    cur_pc = address;
    if (!hle_.count(address) && (address < text_begin_ || address >= text_end_))
        trap(TrapKind::CallNotAllowed, "entry outside .text " + hex32(address));
    while (true) {
        if (pc == kReturnAddr) break;
        auto h = hle_.find(pc);
        if (h != hle_.end()) {
            cur_pc = pc;
            h->second.fn(*this);
            pc = u32(gpr[31].lo);
            npc = pc + 4;
            continue;
        }
        if (!probes_.empty()) {
            auto p = probes_.find(pc);
            if (p != probes_.end()) {
                cur_pc = pc;
                p->second(*this);
            }
        }
        if (icount - start >= max_instructions) {
            cur_pc = pc;
            trap(TrapKind::Budget, std::to_string(max_instructions) + " instructions");
        }
        step();
    }
    CallResult r;
    r.v0 = gpr[2];
    r.v1 = gpr[3];
    r.f0 = fpr[0];
    r.instructions = icount - start;
    return r;
}

void Ee::step() {
    cur_pc = pc;
    if (pc & 3) trap(TrapKind::Unaligned, "misaligned pc");
    if (pc < text_begin_ || pc >= text_end_) trap(TrapKind::BadAddress, "execution outside .text");
    u32 op;
    std::memcpy(&op, ram_.data() + pc, 4);
    if (trace) std::fprintf(stderr, "%08X: %08X\n", pc, op);
    pc = npc;
    npc += 4;
    ++icount;
    exec(op);
    gpr[0] = {};
}

void Ee::branch(bool taken, s32 off, bool likely) {
    if (taken) {
        npc = cur_pc + 4 + u32(off << 2);
    } else if (likely) {
        pc = npc; // nullify the delay slot
        npc += 4;
    }
}

static inline s32 simm(u32 op) { return s32(s16(op & 0xFFFF)); }

void Ee::exec(u32 op) {
    u32 rs = (op >> 21) & 31, rt = (op >> 16) & 31;
    u32 imm = op & 0xFFFF;
    switch (op >> 26) {
    case 0x00: exec_special(op); break;
    case 0x01: exec_regimm(op); break;
    case 0x02: npc = (cur_pc & 0xF0000000) | ((op & 0x03FFFFFF) << 2); break;
    case 0x03: {
        u32 t = (cur_pc & 0xF0000000) | ((op & 0x03FFFFFF) << 2);
        check_call(t);
        gpr[31].lo = cur_pc + 8;
        npc = t;
        break;
    }
    case 0x04: branch(g64(rs) == g64(rt), simm(op), false); break;
    case 0x05: branch(g64(rs) != g64(rt), simm(op), false); break;
    case 0x06: branch(s64(g64(rs)) <= 0, simm(op), false); break;
    case 0x07: branch(s64(g64(rs)) > 0, simm(op), false); break;
    case 0x08: // addi (overflow trap not modelled)
    case 0x09: set32(rt, g32(rs) + u32(simm(op))); break;
    case 0x0A: set64(rt, s64(g64(rs)) < s64(simm(op)) ? 1 : 0); break;
    case 0x0B: set64(rt, g64(rs) < u64(s64(simm(op))) ? 1 : 0); break;
    case 0x0C: set64(rt, g64(rs) & imm); break;
    case 0x0D: set64(rt, g64(rs) | imm); break;
    case 0x0E: set64(rt, g64(rs) ^ imm); break;
    case 0x0F: set32(rt, imm << 16); break;
    case 0x10: { // COP0: only the harmless subset
        if (rs == 0x00) { // mfc0
            u32 rd = (op >> 11) & 31;
            set32(rt, rd == 9 ? u32(icount) : rd == 15 ? 0x2E20 : rd == 1 ? cop0_random : 0);
        } else if (rs == 0x04) { // mtc0: ignored
        } else if (rs == 0x10 && ((op & 63) == 0x38 || (op & 63) == 0x39)) { // ei / di
        } else {
            unimplemented(op, "COP0");
        }
        break;
    }
    case 0x11: exec_cop1(op); break;
    case 0x12: exec_cop2(op); break;
    case 0x14: branch(g64(rs) == g64(rt), simm(op), true); break;
    case 0x15: branch(g64(rs) != g64(rt), simm(op), true); break;
    case 0x16: branch(s64(g64(rs)) <= 0, simm(op), true); break;
    case 0x17: branch(s64(g64(rs)) > 0, simm(op), true); break;
    case 0x18: // daddi
    case 0x19: set64(rt, g64(rs) + u64(s64(simm(op)))); break;
    case 0x1A: { // ldl
        u32 a = g32(rs) + u32(simm(op));
        u32 sh = (a & 7) * 8;
        u64 m = load<u64>(a & ~7u);
        u64 keep = sh == 56 ? 0 : (~u64(0) >> (sh + 8));
        set64(rt, (g64(rt) & keep) | (m << (56 - sh)));
        break;
    }
    case 0x1B: { // ldr
        u32 a = g32(rs) + u32(simm(op));
        u32 sh = (a & 7) * 8;
        u64 m = load<u64>(a & ~7u);
        u64 keep = sh == 0 ? 0 : (~u64(0) << (64 - sh));
        set64(rt, (g64(rt) & keep) | (m >> sh));
        break;
    }
    case 0x1C: exec_mmi(op); break;
    case 0x1E: { // lq
        u32 a = g32(rs) + u32(simm(op));
        if (rt) gpr[rt] = load128(a);
        break;
    }
    case 0x1F: store128(g32(rs) + u32(simm(op)), gpr[rt]); break; // sq
    case 0x20: set64(rt, u64(s64(std::int8_t(load<u8>(g32(rs) + u32(simm(op))))))); break;
    case 0x21: set64(rt, u64(s64(s16(load<u16>(g32(rs) + u32(simm(op))))))); break;
    case 0x22: { // lwl
        u32 a = g32(rs) + u32(simm(op));
        u32 sh = (a & 3) * 8;
        u32 m = load<u32>(a & ~3u);
        u32 keep = sh == 24 ? 0 : (0xFFFFFFFFu >> (sh + 8));
        set32(rt, (g32(rt) & keep) | (m << (24 - sh)));
        break;
    }
    case 0x23: set32(rt, load<u32>(g32(rs) + u32(simm(op)))); break;
    case 0x24: set64(rt, load<u8>(g32(rs) + u32(simm(op)))); break;
    case 0x25: set64(rt, load<u16>(g32(rs) + u32(simm(op)))); break;
    case 0x26: { // lwr
        u32 a = g32(rs) + u32(simm(op));
        u32 sh = (a & 3) * 8;
        u32 m = load<u32>(a & ~3u);
        if (sh == 0) {
            set32(rt, m); // full word: sign-extends like lw
        } else {
            u32 keep = 0xFFFFFFFFu << (32 - sh);
            u32 v = (g32(rt) & keep) | (m >> sh);
            if (rt) gpr[rt].lo = (gpr[rt].lo & 0xFFFFFFFF00000000ull) | v; // upper bits unchanged
        }
        break;
    }
    case 0x27: set64(rt, load<u32>(g32(rs) + u32(simm(op)))); break;
    case 0x28: store<u8>(g32(rs) + u32(simm(op)), u8(g64(rt))); break;
    case 0x29: store<u16>(g32(rs) + u32(simm(op)), u16(g64(rt))); break;
    case 0x2A: { // swl
        u32 a = g32(rs) + u32(simm(op));
        u32 sh = (a & 3) * 8;
        u32 m = load<u32>(a & ~3u);
        u32 keep = sh == 24 ? 0 : (0xFFFFFFFFu << (sh + 8));
        store<u32>(a & ~3u, (m & keep) | (g32(rt) >> (24 - sh)));
        break;
    }
    case 0x2B: store<u32>(g32(rs) + u32(simm(op)), g32(rt)); break;
    case 0x2C: { // sdl
        u32 a = g32(rs) + u32(simm(op));
        u32 sh = (a & 7) * 8;
        u64 m = load<u64>(a & ~7u);
        u64 keep = sh == 56 ? 0 : (~u64(0) << (sh + 8));
        store<u64>(a & ~7u, (m & keep) | (g64(rt) >> (56 - sh)));
        break;
    }
    case 0x2D: { // sdr
        u32 a = g32(rs) + u32(simm(op));
        u32 sh = (a & 7) * 8;
        u64 m = load<u64>(a & ~7u);
        u64 keep = sh == 0 ? 0 : (~u64(0) >> (64 - sh));
        store<u64>(a & ~7u, (m & keep) | (g64(rt) << sh));
        break;
    }
    case 0x2E: { // swr
        u32 a = g32(rs) + u32(simm(op));
        u32 sh = (a & 3) * 8;
        u32 m = load<u32>(a & ~3u);
        u32 keep = sh == 0 ? 0 : (0xFFFFFFFFu >> (32 - sh));
        store<u32>(a & ~3u, (m & keep) | (g32(rt) << sh));
        break;
    }
    case 0x2F: break; // cache
    case 0x31: fpr[rt] = load<u32>(g32(rs) + u32(simm(op))); break; // lwc1
    case 0x33: break;                                                // pref
    case 0x36: { // lqc2
        u128 v = load128(g32(rs) + u32(simm(op)));
        if (rt) {
            for (int i = 0; i < 4; ++i) vf[rt][i] = v.w(i);
        }
        break;
    }
    case 0x37: set64(rt, load<u64>(g32(rs) + u32(simm(op)))); break; // ld
    case 0x39: store<u32>(g32(rs) + u32(simm(op)), fpr[rt]); break;  // swc1
    case 0x3E: { // sqc2
        u128 v;
        for (int i = 0; i < 4; ++i) v.set_w(i, vf[rt][i]);
        store128(g32(rs) + u32(simm(op)), v);
        break;
    }
    case 0x3F: store<u64>(g32(rs) + u32(simm(op)), g64(rt)); break; // sd
    default: unimplemented(op, "primary");
    }
}

void Ee::exec_special(u32 op) {
    u32 rs = (op >> 21) & 31, rt = (op >> 16) & 31, rd = (op >> 11) & 31, sh = (op >> 6) & 31;
    switch (op & 63) {
    case 0x00: set32(rd, g32(rt) << sh); break;
    case 0x02: set32(rd, g32(rt) >> sh); break;
    case 0x03: set32(rd, u32(s32(g32(rt)) >> sh)); break;
    case 0x04: set32(rd, g32(rt) << (g32(rs) & 31)); break;
    case 0x06: set32(rd, g32(rt) >> (g32(rs) & 31)); break;
    case 0x07: set32(rd, u32(s32(g32(rt)) >> (g32(rs) & 31))); break;
    case 0x08: npc = g32(rs); break; // jr
    case 0x09: {                     // jalr
        u32 t = g32(rs);
        check_call(t);
        set64(rd, cur_pc + 8);
        npc = t;
        break;
    }
    case 0x0A: if (g64(rt) == 0) set64(rd, g64(rs)); break; // movz
    case 0x0B: if (g64(rt) != 0) set64(rd, g64(rs)); break; // movn
    case 0x0C: trap(TrapKind::Syscall, "syscall " + std::to_string(g32(3)));
    case 0x0D: trap(TrapKind::Break, "break " + std::to_string((op >> 6) & 0xFFFFF));
    case 0x0F: break; // sync
    case 0x10: set64(rd, hi.lo); break;
    case 0x11: hi.lo = g64(rs); break;
    case 0x12: set64(rd, lo.lo); break;
    case 0x13: lo.lo = g64(rs); break;
    case 0x14: set64(rd, g64(rt) << (g32(rs) & 63)); break;
    case 0x16: set64(rd, g64(rt) >> (g32(rs) & 63)); break;
    case 0x17: set64(rd, u64(s64(g64(rt)) >> (g32(rs) & 63))); break;
    case 0x18: { // mult (R5900: also writes rd = LO)
        s64 p = s64(s32(g32(rs))) * s64(s32(g32(rt)));
        lo.lo = u64(s64(s32(u32(p))));
        hi.lo = u64(s64(s32(u32(u64(p) >> 32))));
        set64(rd, lo.lo);
        break;
    }
    case 0x19: { // multu
        u64 p = u64(g32(rs)) * u64(g32(rt));
        lo.lo = u64(s64(s32(u32(p))));
        hi.lo = u64(s64(s32(u32(p >> 32))));
        set64(rd, lo.lo);
        break;
    }
    case 0x1A: { // div
        s32 n = s32(g32(rs)), d = s32(g32(rt));
        if (d == 0) {
            lo.lo = u64(s64(n < 0 ? 1 : -1));
            hi.lo = u64(s64(n));
        } else if (n == INT32_MIN && d == -1) {
            lo.lo = u64(s64(INT32_MIN));
            hi.lo = 0;
        } else {
            lo.lo = u64(s64(n / d));
            hi.lo = u64(s64(n % d));
        }
        break;
    }
    case 0x1B: { // divu
        u32 n = g32(rs), d = g32(rt);
        if (d == 0) {
            lo.lo = ~u64(0);
            hi.lo = u64(s64(s32(n)));
        } else {
            lo.lo = u64(s64(s32(n / d)));
            hi.lo = u64(s64(s32(n % d)));
        }
        break;
    }
    case 0x20: // add (overflow trap not modelled)
    case 0x21: set32(rd, g32(rs) + g32(rt)); break;
    case 0x22: // sub
    case 0x23: set32(rd, g32(rs) - g32(rt)); break;
    case 0x24: set64(rd, g64(rs) & g64(rt)); break;
    case 0x25: set64(rd, g64(rs) | g64(rt)); break;
    case 0x26: set64(rd, g64(rs) ^ g64(rt)); break;
    case 0x27: set64(rd, ~(g64(rs) | g64(rt))); break;
    case 0x28: set64(rd, sa); break; // mfsa
    case 0x29: sa = g64(rs) & 0xF; break; // mtsa
    case 0x2A: set64(rd, s64(g64(rs)) < s64(g64(rt)) ? 1 : 0); break;
    case 0x2B: set64(rd, g64(rs) < g64(rt) ? 1 : 0); break;
    case 0x2C: // dadd
    case 0x2D: set64(rd, g64(rs) + g64(rt)); break;
    case 0x2E: // dsub
    case 0x2F: set64(rd, g64(rs) - g64(rt)); break;
    case 0x30: if (s64(g64(rs)) >= s64(g64(rt))) trap(TrapKind::Trap, "tge"); break;
    case 0x31: if (g64(rs) >= g64(rt)) trap(TrapKind::Trap, "tgeu"); break;
    case 0x32: if (s64(g64(rs)) < s64(g64(rt))) trap(TrapKind::Trap, "tlt"); break;
    case 0x33: if (g64(rs) < g64(rt)) trap(TrapKind::Trap, "tltu"); break;
    case 0x34: if (g64(rs) == g64(rt)) trap(TrapKind::Trap, "teq"); break;
    case 0x36: if (g64(rs) != g64(rt)) trap(TrapKind::Trap, "tne"); break;
    case 0x38: set64(rd, g64(rt) << sh); break;
    case 0x3A: set64(rd, g64(rt) >> sh); break;
    case 0x3B: set64(rd, u64(s64(g64(rt)) >> sh)); break;
    case 0x3C: set64(rd, g64(rt) << (sh + 32)); break;
    case 0x3E: set64(rd, g64(rt) >> (sh + 32)); break;
    case 0x3F: set64(rd, u64(s64(g64(rt)) >> (sh + 32))); break;
    default: unimplemented(op, "SPECIAL");
    }
}

void Ee::exec_regimm(u32 op) {
    u32 rs = (op >> 21) & 31, rt = (op >> 16) & 31;
    s64 v = s64(g64(rs));
    s32 off = simm(op);
    switch (rt) {
    case 0x00: branch(v < 0, off, false); break;
    case 0x01: branch(v >= 0, off, false); break;
    case 0x02: branch(v < 0, off, true); break;
    case 0x03: branch(v >= 0, off, true); break;
    case 0x10: gpr[31].lo = cur_pc + 8; branch(v < 0, off, false); break;
    case 0x11: gpr[31].lo = cur_pc + 8; branch(v >= 0, off, false); break;
    case 0x12: gpr[31].lo = cur_pc + 8; branch(v < 0, off, true); break;
    case 0x13: gpr[31].lo = cur_pc + 8; branch(v >= 0, off, true); break;
    case 0x18: sa = ((g32(rs) & 0xF) ^ (op & 0xF)) & 0xF; break; // mtsab: byte shift amount
    case 0x19: sa = (((g32(rs) & 0x7) ^ (op & 0x7)) * 2) & 0xF; break; // mtsah: halfword amount
    default: unimplemented(op, "REGIMM");
    }
}

} // namespace gt3r::diff
