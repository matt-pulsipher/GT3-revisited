"""Identify Sony SDK / C runtime functions so they can be labelled and excluded.

Evidence used, strongest first:
  1. EE kernel syscall stubs (`li v1, N; syscall; jr ra; nop`) -> exact names.
  2. Strings a function references (library messages such as "libpad: ...",
     "sceCdRead ...", IRX module names, printf tables).
  3. Link order: libraries are linked as contiguous runs of functions, so
     unlabelled functions between two functions of the same library inherit it.
  4. Game C++ code has .eh_frame FDEs; libraries compiled as C/asm do not, so
     functions without FDEs after the last FDE-covered function are runtime code.
"""
import re

from .mips import Insn, JR_RA

SYSCALLS = {
    0x01: "ResetEE", 0x02: "SetGsCrt", 0x04: "Exit", 0x06: "LoadExecPS2", 0x07: "ExecPS2",
    0x10: "AddIntcHandler", 0x11: "RemoveIntcHandler", 0x12: "AddDmacHandler", 0x13: "RemoveDmacHandler",
    0x14: "_EnableIntc", 0x15: "_DisableIntc", 0x16: "_EnableDmac", 0x17: "_DisableDmac",
    0x18: "SetAlarm", 0x19: "ReleaseAlarm",
    0x20: "CreateThread", 0x21: "DeleteThread", 0x22: "StartThread", 0x23: "ExitThread",
    0x24: "ExitDeleteThread", 0x25: "TerminateThread", 0x27: "DisableDispatchThread",
    0x28: "EnableDispatchThread", 0x29: "ChangeThreadPriority", 0x2B: "RotateThreadReadyQueue",
    0x2D: "ReleaseWaitThread", 0x2F: "GetThreadId", 0x30: "ReferThreadStatus", 0x32: "SleepThread",
    0x33: "WakeupThread", 0x35: "CancelWakeupThread", 0x37: "SuspendThread", 0x39: "ResumeThread",
    0x3B: "JoinThread", 0x3C: "SetupThread", 0x3D: "SetupHeap", 0x3E: "EndOfHeap",
    0x40: "CreateSema", 0x41: "DeleteSema", 0x42: "SignalSema", 0x44: "WaitSema", 0x45: "PollSema",
    0x47: "ReferSemaStatus", 0x4A: "SetOsdConfigParam", 0x4B: "GetOsdConfigParam", 0x4C: "GetGsHParam",
    0x4D: "GetGsVParam", 0x4E: "SetGsHParam", 0x4F: "SetGsVParam",
    0x64: "FlushCache", 0x70: "GsGetIMR", 0x71: "GsPutIMR", 0x73: "SetVSyncFlag", 0x74: "SetSyscall",
    0x76: "SifDmaStat", 0x77: "SifSetDma", 0x78: "SifSetDChain", 0x79: "SifSetReg", 0x7A: "SifGetReg",
    0x7B: "ExecOSD", 0x7C: "Deci2Call", 0x7D: "PSMode", 0x7E: "MachineType", 0x7F: "GetMemorySize",
}
# Interrupt-context variants use negative numbers
ISYSCALLS = {0x14: "iEnableIntc", 0x15: "iDisableIntc", 0x16: "iEnableDmac", 0x17: "iDisableDmac",
             0x18: "iSetAlarm", 0x19: "iReleaseAlarm", 0x26: "iTerminateThread", 0x2A: "iChangeThreadPriority",
             0x2C: "iRotateThreadReadyQueue", 0x2E: "iReleaseWaitThread", 0x31: "iReferThreadStatus",
             0x34: "iWakeupThread", 0x36: "iCancelWakeupThread", 0x38: "iSuspendThread",
             0x3A: "iResumeThread", 0x43: "iSignalSema", 0x46: "iPollSema", 0x48: "iReferSemaStatus",
             0x76: "isceSifDmaStat", 0x77: "isceSifSetDma", 0x78: "isceSifSetDChain"}

LIB_PATTERNS = [
    ("libpad", re.compile(r"libpad|padman|scePad", re.I)),
    ("libmc", re.compile(r"libmc|mcserv|mcman|sceMc", re.I)),
    ("libcdvd", re.compile(r"libcdvd|sceCd|cdvdfsv|CdSearchFile|CD_Init", re.I)),
    ("libgraph", re.compile(r"sceGs|libgraph", re.I)),
    ("libdma", re.compile(r"sceDma|libdma", re.I)),
    ("libvu0", re.compile(r"sceVu0", re.I)),
    ("libipu", re.compile(r"sceIpu|libipu", re.I)),
    ("libmpeg", re.compile(r"sceMpeg|libmpeg", re.I)),
    ("libsdr", re.compile(r"sceSd|libsdr|sdrdrv", re.I)),
    ("libscf", re.compile(r"sceScf|libscf", re.I)),
    ("libkernl", re.compile(r"sceSif|libkernl|TTY:|sif_|SifInit|_sceSif|deci2", re.I)),
    ("libc", re.compile(r"^\(null\)$|0123456789abcdef|0123456789ABCDEF|^inf$|^nan$|Assertion|abort", re.I)),
    ("libgcc", re.compile(r"pure virtual|__builtin|bad_alloc|bad_cast|terminate|exception|type_info", re.I)),
]
NAME_RE = re.compile(r"^(_?sce[A-Z][A-Za-z0-9_]+|isce[A-Z][A-Za-z0-9_]+|Sif[A-Z][A-Za-z0-9_]+)\s*[:( ]")


class Sdk:
    def __init__(self, A, log=print):
        self.A = A
        self.log = log
        self.evidence = {}  # func start -> list of evidence strings

    def note(self, f, lib, why, name=None):
        if lib and not f.lib:
            f.lib = lib
        if name and not f.name:
            f.name = name
        self.evidence.setdefault(f.start, []).append(why)

    def run(self):
        A = self.A
        self._syscalls()
        self._strings()
        # Region after the last FDE: runtime/library code compiled without exceptions
        last_fde_end = max(A.fdes.values()) if A.fdes else A.text_start
        self.runtime_start = last_fde_end
        for s, f in A.functions.items():
            if s >= last_fde_end and not f.lib:
                self.note(f, "runtime", "after last .eh_frame FDE (C/asm library region)")
        self._fill_gaps()
        n = sum(1 for f in A.functions.values() if f.lib)
        self.log("sdk: %d functions labelled as library/runtime (runtime region from 0x%08X)" % (
            n, self.runtime_start))
        return self

    def _syscalls(self):
        A = self.A
        for f in A.functions.values():
            if f.end - f.start > 0x20:
                continue
            ins = [A.insns[a] for a in range(f.start, f.end, 4)]
            for k, i in enumerate(ins):
                if i.kind == Insn.SYSCALL and k > 0:
                    p = ins[k - 1]
                    if p.op == 0x09 and p.rs == 0 and p.rt == 3:  # addiu v1, zero, N
                        n = p.simm
                        nm = SYSCALLS.get(n) if n >= 0 else ISYSCALLS.get(-n)
                        nm = nm or ("syscall_%s%X" % ("m" if n < 0 else "", abs(n)))
                        if any(x.word == JR_RA for x in ins[k:]):
                            self.note(f, "libkernl", "syscall stub %d" % n, nm)
                    break

    def _strings(self):
        A = self.A
        for f in A.functions.values():
            names = set()
            for to in f.refs:
                s = A.strings.get(to)
                if not s:
                    continue
                for lib, rx in LIB_PATTERNS:
                    if rx.search(s):
                        if lib == "libgcc" and f.start < (max(A.fdes.values()) if A.fdes else 0) and "eh" in f.tags:
                            continue
                        self.note(f, lib, "string %r" % s[:60])
                        break
                m = NAME_RE.match(s)
                if m:
                    names.add(m.group(1))
            if len(names) == 1 and f.lib and f.lib != "game":
                n = names.pop()
                if not f.name:
                    f.name = n
                    self.evidence.setdefault(f.start, []).append("named from message string")

    def _fill_gaps(self):
        A = self.A
        order = sorted(A.functions)
        labelled = [(k, A.functions[s].lib) for k, s in enumerate(order) if A.functions[s].lib not in (None, "runtime")]
        for (k1, l1), (k2, l2) in zip(labelled, labelled[1:]):
            if l1 != l2 or k2 - k1 > 40:
                continue
            gap = [A.functions[order[k]] for k in range(k1 + 1, k2)]
            if any("eh" in g.tags for g in gap):
                continue
            for g in gap:
                if g.lib in (None, "runtime"):
                    g.lib = None
                    self.note(g, l1, "between two %s functions (link order)" % l1)

    def summary_lines(self):
        return ["runtime       library region (no .eh_frame) from 0x%08X" % self.runtime_start]

    def write(self, W):
        A = self.A
        with W.open("sdk_functions.tsv") as o:
            o.write("start\tend\tlib\tname\tevidence\n")
            for s in sorted(A.functions):
                f = A.functions[s]
                if not f.lib:
                    continue
                o.write("%08X\t%08X\t%s\t%s\t%s\n" % (s, f.end, f.lib, f.label(),
                                                     " | ".join(self.evidence.get(s, []))[:300]))
