"""Small R5900 decoder used for analysis, plus pluggable text disassembly.

The analysis passes only need to know about control flow, address formation
(lui/addiu/ori), loads/stores and register moves, so this module decodes those
fields itself (no dependencies). Full text disassembly uses `rabbitizer` (proper
R5900 + MMI + COP2 support) when importable, then `capstone`, then plain `.word`.
"""

REG = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
       "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
       "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
       "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]

# Primary opcodes for loads and stores: name -> access size (bytes).
LOADS = {0x20: ("lb", 1), 0x24: ("lbu", 1), 0x21: ("lh", 2), 0x25: ("lhu", 2), 0x23: ("lw", 4),
         0x27: ("lwu", 4), 0x37: ("ld", 8), 0x1E: ("lq", 16), 0x31: ("lwc1", 4), 0x36: ("lqc2", 16),
         0x22: ("lwl", 4), 0x26: ("lwr", 4), 0x1A: ("ldl", 8), 0x1B: ("ldr", 8)}
STORES = {0x28: ("sb", 1), 0x29: ("sh", 2), 0x2B: ("sw", 4), 0x3F: ("sd", 8), 0x1F: ("sq", 16),
          0x39: ("swc1", 4), 0x3E: ("sqc2", 16), 0x2A: ("swl", 4), 0x2E: ("swr", 4), 0x2C: ("sdl", 8),
          0x2D: ("sdr", 8)}
COND_BRANCH_OPS = {0x04: "beq", 0x05: "bne", 0x06: "blez", 0x07: "bgtz",
                   0x14: "beql", 0x15: "bnel", 0x16: "blezl", 0x17: "bgtzl"}

JR_RA = 0x03E00008


def sext16(x):
    return x - 0x10000 if x & 0x8000 else x


class Insn:
    """Decoded fields of one instruction (only what the analysis needs)."""
    __slots__ = ("addr", "word", "op", "rs", "rt", "rd", "funct", "imm", "simm", "kind", "target", "mem")

    # kind values
    OTHER, LUI, ADDI, ORI, LOAD, STORE, JAL, J, JR, JALR, BRANCH, MOVE, SYSCALL = range(13)

    def __init__(self, addr, word):
        self.addr = addr
        self.word = word
        op = word >> 26
        self.op = op
        self.rs = (word >> 21) & 31
        self.rt = (word >> 16) & 31
        self.rd = (word >> 11) & 31
        self.funct = word & 63
        self.imm = word & 0xFFFF
        self.simm = sext16(self.imm)
        self.target = None
        self.mem = 0
        k = Insn.OTHER
        if op == 0x0F:
            k = Insn.LUI
        elif op in (0x09, 0x19):  # addiu, daddiu
            k = Insn.ADDI
        elif op == 0x0D:
            k = Insn.ORI
        elif op in LOADS:
            k = Insn.LOAD
            self.mem = LOADS[op][1]
        elif op in STORES:
            k = Insn.STORE
            self.mem = STORES[op][1]
        elif op == 0x03:
            k = Insn.JAL
            self.target = ((addr + 4) & 0xF0000000) | ((word & 0x3FFFFFF) << 2)
        elif op == 0x02:
            k = Insn.J
            self.target = ((addr + 4) & 0xF0000000) | ((word & 0x3FFFFFF) << 2)
        elif op == 0:
            f = self.funct
            if f == 0x08:
                k = Insn.JR
            elif f == 0x09:
                k = Insn.JALR
            elif f == 0x0C:
                k = Insn.SYSCALL
            elif f in (0x2D, 0x25, 0x21) and self.rt == 0:  # daddu/or/addu rd, rs, zero
                k = Insn.MOVE
        elif op in COND_BRANCH_OPS:
            k = Insn.BRANCH
            self.target = (addr + 4 + (self.simm << 2)) & 0xFFFFFFFF
        elif op == 0x01 and self.rt in (0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13):  # REGIMM bltz/bgez(al)(l)
            k = Insn.BRANCH
            self.target = (addr + 4 + (self.simm << 2)) & 0xFFFFFFFF
        elif op in (0x10, 0x11, 0x12) and self.rs == 0x08:  # bc0/bc1/bc2 (t/f/tl/fl)
            k = Insn.BRANCH
            self.target = (addr + 4 + (self.simm << 2)) & 0xFFFFFFFF
        self.kind = k

    def writes_reg(self):
        """Return the GPR this instruction writes (best effort), or None."""
        op = self.op
        if self.kind in (Insn.LUI, Insn.ADDI, Insn.ORI):
            return self.rt
        if self.kind == Insn.LOAD:
            return self.rt if op not in (0x31, 0x36) else None
        if self.kind == Insn.JAL:
            return 31
        if self.kind == Insn.JALR:
            return self.rd
        if op == 0:
            if self.funct in (0x08, 0x0C, 0x0D, 0x0F, 0x18, 0x19, 0x1A, 0x1B, 0x11, 0x13):
                return None  # jr, syscall, break, sync, mult/div, mthi/mtlo
            return self.rd
        if op == 0x1C:  # MMI: most write rd
            return self.rd
        if op in (0x08, 0x0A, 0x0B, 0x0C, 0x0E, 0x18):  # addi, slti, sltiu, andi, xori, daddi
            return self.rt
        if op == 0x11 and self.rs in (0x00, 0x02):  # mfc1, cfc1
            return self.rt
        if op == 0x12 and self.rs in (0x01, 0x02):  # qmfc2, cfc2
            return self.rt
        if op == 0x10 and self.rs == 0x00:  # mfc0
            return self.rt
        return None


class TextDisassembler:
    """Formats one instruction as text. Picks the best backend available."""

    def __init__(self, prefer=None):
        self.backend = None
        self._cap = None
        order = [prefer] if prefer else []
        order += ["rabbitizer", "capstone", "none"]
        for b in order:
            if b == "rabbitizer":
                try:
                    import rabbitizer
                    self._rab = rabbitizer
                    self._cat = rabbitizer.InstrCategory.R5900
                    rabbitizer.config.regNames_gprAbiNames = rabbitizer.Abi.O32
                    self.backend = "rabbitizer"
                    return
                except Exception:
                    continue
            if b == "capstone":
                try:
                    import capstone
                    self._cap = capstone.Cs(capstone.CS_ARCH_MIPS,
                                            capstone.CS_MODE_MIPS64 + capstone.CS_MODE_LITTLE_ENDIAN)
                    self.backend = "capstone"
                    return
                except Exception:
                    continue
            if b == "none":
                self.backend = "none"
                return

    def text(self, addr, word):
        if self.backend == "rabbitizer":
            ins = self._rab.Instruction(word, vram=addr, category=self._cat)
            if not ins.isValid():
                return ".word 0x%08X" % word
            s = ins.disassemble(immOverride=None)
            return " ".join(s.split())
        if self.backend == "capstone":
            for i in self._cap.disasm(word.to_bytes(4, "little"), addr):
                return ("%s %s" % (i.mnemonic, i.op_str)).strip()
            return ".word 0x%08X" % word
        return ".word 0x%08X" % word
