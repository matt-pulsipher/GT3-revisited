#pragma once

// Minimal R5900 (PS2 Emotion Engine) interpreter for the developer-only
// differential test harness. It runs isolated functions of the user's own
// CORE.GT3 so their results can be compared with our reimplementation.
// Never linked into the game. See docs/dev/DIFF_HARNESS.md.
//
// Scope: MIPS III integer ISA (64-bit GPRs, 128-bit for MMI), R5900
// three-operand MULT/MADD, pipeline-1 HI1/LO1 ops, MMI, COP1 with PS2
// float semantics (ps2float.h) and COP2 VU0 macro mode. No TLB, no
// interrupts, no DMA, no timing. Anything outside the implemented subset
// throws EeTrap with the PC.

#include "base/bytes.h"

#include <array>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gt3r::diff {

struct u128 {
    u64 lo = 0, hi = 0;
    u32 w(int i) const { return u32((i < 2 ? lo : hi) >> (32 * (i & 1))); }
    void set_w(int i, u32 v) {
        u64& d = i < 2 ? lo : hi;
        int s = 32 * (i & 1);
        d = (d & ~(u64(0xFFFFFFFF) << s)) | (u64(v) << s);
    }
};

enum class TrapKind { Unimplemented, Syscall, Break, Trap, BadAddress, Unaligned, CallNotAllowed, WriteToText, Budget };

struct EeTrap : std::runtime_error {
    TrapKind kind;
    u32 pc;
    u32 op;
    EeTrap(TrapKind k, u32 pc_, u32 op_, const std::string& what);
};

// Call argument per the PS2 GCC EABI: integers/pointers in a0-a3, t0-t3
// ($4-$11), floats independently in f12-f19. 64-bit integers take one GPR.
struct Arg {
    enum Kind : u8 { Int, Float } kind = Int;
    u64 i = 0;
    u32 f = 0; // PS2 float bits
    static Arg integer(s64 v) { return {Int, u64(v), 0}; }
    static Arg ptr(u32 a) { return {Int, a, 0}; }
    static Arg flt(float v);       // host float, converted with ps2f::from_host
    static Arg fbits(u32 bits) { return {Float, 0, bits}; }
};

struct CallResult {
    u128 v0, v1;
    u32 f0 = 0;    // PS2 float bits
    u64 instructions = 0;
    float f0_host() const;
    s32 v0_s32() const { return s32(u32(v0.lo)); }
};

class Ee {
public:
    // Memory map
    static constexpr u32 kRamSize = 32u << 20;
    static constexpr u32 kScratchBase = 0x70000000, kScratchSize = 0x4000;
    static constexpr u32 kVu0MemBase = 0x11004000, kVu0MemSize = 0x1000;
    static constexpr u32 kReturnAddr = 0x01FFFFF0; // sentinel $ra; never executed
    static constexpr u32 kHeapBase = 0x01000000, kHeapEnd = 0x01F00000;
    static constexpr u32 kStackTop = 0x01FFFF00;

    Ee();

    // Loads the program image at `load_address`, zeroes `bss`, sets $gp.
    // Keeps a pristine copy so reset() can restore all of RAM.
    void load_program(ByteSpan image, u32 load_address, u32 text_end, u32 bss_start, u32 bss_end, u32 gp);
    // Restores RAM to the freshly loaded state and clears the CPU and heap.
    void reset();

    // ---- calling original functions -------------------------------------
    CallResult call(u32 address, const std::vector<Arg>& args = {}, u64 max_instructions = 10'000'000);

    // HLE: when execution reaches `address`, `fn` runs instead and returns to $ra.
    void hle(u32 address, std::function<void(Ee&)> fn, std::string name = {});
    // Installs memcpy/memset/memmove/strlen HLE stubs at the addresses in the
    // known runtime library (see DIFF_HARNESS.md).
    void hle_libc_defaults();
    // Removes every HLE stub (other tests leave theirs installed; reinstall hle_libc_defaults() afterwards if needed).
    void clear_hle() { hle_.clear(); }
    // Whether `address` has an HLE stub; and its removal (the stubs a test puts in for one call).
    bool has_hle(u32 address) const { return hle_.count(address) != 0; }
    void unhle(u32 address) { hle_.erase(address); }
    // Observer: when execution reaches `address`, `fn` runs and then the original code executes normally (nothing is replaced).
    // (Address 0: a place the disc's program does not have; nothing is installed.)
    void probe(u32 address, std::function<void(Ee&)> fn) {
        if (address) probes_[address] = std::move(fn);
    }
    // Another observer at `address`: runs after the probe already there (if any).
    void add_probe(u32 address, std::function<void(Ee&)> fn) {
        if (!address) return;
        auto it = probes_.find(address);
        if (it == probes_.end()) return probe(address, std::move(fn));
        it->second = [first = std::move(it->second), fn = std::move(fn)](Ee& e) { first(e), fn(e); };
    }
    void clear_probes() { probes_.clear(); }
    // Debugging aid: every store (the program's and the harness's own) into the word at `watch` is printed with the
    // interpreted pc and $ra (0: off).
    u32 watch = 0;
    void note_watch(u32 a, u64 v);
    // Restrict calls (jal/jalr) to the listed addresses plus HLE stubs. Empty
    // set (default) = allow everything inside .text.
    void allow_calls(std::initializer_list<u32> addrs) { allowed_calls_.insert(addrs.begin(), addrs.end()); }
    void clear_allowed_calls() { allowed_calls_.clear(); }
    bool trace = false;       // print every instruction to stderr
    bool protect_text = true; // trap on stores into .text (catches runaway pointers)
    u32 cop0_random = 0;      // what mfc0 of COP0 Random (register 1) reads

    // ---- memory -------------------------------------------------------
    u8* host_ptr(u32 addr, u32 size); // throws on bad range
    u8 r8(u32 a) { return *host_ptr(a, 1); }
    u16 r16(u32 a) { return load<u16>(a); }
    u32 r32(u32 a) { return load<u32>(a); }
    u64 r64(u32 a) { return load<u64>(a); }
    float rf(u32 a);
    void w8(u32 a, u8 v) { *host_ptr(a, 1) = v; }
    void w16(u32 a, u16 v) { store<u16>(a, v); }
    void w32(u32 a, u32 v) { store<u32>(a, v); }
    void w64(u32 a, u64 v) { store<u64>(a, v); }
    void wf(u32 a, float v);
    void write_bytes(u32 a, ByteSpan data);
    void read_bytes(u32 a, u8* out, u32 n);
    std::string read_cstr(u32 a, u32 max = 4096);
    // Bump allocator in [kHeapBase, kHeapEnd); reset() frees everything.
    u32 alloc(u32 size, u32 align = 16);
    u32 alloc_cstr(std::string_view s);
    u32 alloc_bytes(ByteSpan data, u32 align = 16);
    void free_all() { heap_ = kHeapBase; }

    // ---- state (public for HLE stubs and the instruction files) ---------
    std::array<u128, 32> gpr{};
    u128 hi{}, lo{}; // HI1/LO1 are the upper 64 bits
    u64 sa = 0;
    std::array<u32, 32> fpr{};
    u32 facc = 0;
    u32 fcr31 = 0;
    std::array<std::array<u32, 4>, 32> vf{}; // x y z w as PS2 float bits
    std::array<u16, 16> vi{};
    std::array<u32, 4> vacc{};
    u32 vq = 0, vp = 0, vi_reg = 0, vr = 0; // Q, P, I, R
    u32 vstatus = 0, vmac = 0, vclip = 0;
    u32 pc = 0, npc = 0;
    u64 icount = 0;
    u32 cur_pc = 0; // address of the instruction being executed
    u32 text_begin_ = 0, text_end_ = 0;

    // Argument registers by index (for HLE stubs).
    u64 a(int i) const { return gpr[4 + i].lo; }
    void set_v0(u64 v) { gpr[2].lo = v; }

    // GPR helpers: 32-bit results are sign-extended to 64 bits; upper 64 untouched.
    void set32(int r, u32 v) { if (r) gpr[r].lo = u64(s64(s32(v))); }
    void set64(int r, u64 v) { if (r) gpr[r].lo = v; }
    u32 g32(int r) const { return u32(gpr[r].lo); }
    u64 g64(int r) const { return gpr[r].lo; }

    [[noreturn]] void trap(TrapKind k, const std::string& msg);
    [[noreturn]] void unimplemented(u32 op, const char* group);

    template <typename T> T load(u32 a);
    template <typename T> void store(u32 a, T v);
    void check_write(u32 a) {
        u32 p = a & 0x1FFFFFFF;
        if (protect_text && p >= text_begin_ && p < text_end_) trap(TrapKind::WriteToText, "store into .text");
    }
    u128 load128(u32 a);
    void store128(u32 a, u128 v);

    // Instruction groups implemented in separate files.
    void exec_mmi(u32 op);  // opcode 0x1C (ee_mmi.cpp)
    void exec_cop1(u32 op); // opcode 0x11 (ee_fpu.cpp)
    void exec_cop2(u32 op); // opcode 0x12 (ee_vu0.cpp)

private:
    void step();
    void exec(u32 op);
    void exec_special(u32 op);
    void exec_regimm(u32 op);
    void branch(bool taken, s32 off, bool likely);
    void check_call(u32 target);

    Bytes ram_, pristine_;
    std::array<u8, kScratchSize> scratch_{};
    std::array<u8, kVu0MemSize> vu0mem_{};
    u32 heap_ = kHeapBase;
    u32 gp_ = 0;
    struct Hle { std::function<void(Ee&)> fn; std::string name; };
    std::unordered_map<u32, Hle> hle_;
    std::unordered_map<u32, std::function<void(Ee&)>> probes_;
    std::unordered_set<u32> allowed_calls_;
};

template <typename T> T Ee::load(u32 a) {
    if (a & (sizeof(T) - 1)) trap(TrapKind::Unaligned, "unaligned load");
    T v;
    std::memcpy(&v, host_ptr(a, sizeof(T)), sizeof(T));
    return v;
}

template <typename T> void Ee::store(u32 a, T v) {
    if (a & (sizeof(T) - 1)) trap(TrapKind::Unaligned, "unaligned store");
    if (watch && (a & ~3u) == watch) note_watch(a, u64(v));
    check_write(a);
    std::memcpy(host_ptr(a, sizeof(T)), &v, sizeof(T));
}

std::string hex32(u32 v);

} // namespace gt3r::diff
