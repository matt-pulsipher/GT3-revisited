"""RTTI recovery for GCC 2.9x (old ABI, non-thunk vtables).

How GCC 2.9x lays it out (as observed in this program):
  * For every polymorphic type there is a "type-info function" `__tf<mangled>`
    that, on first call, fills in the type_info object `__ti<mangled>` (in
    .bss/.sdata) by calling one of the libgcc helpers:
        __rtti_user(ti, name)                  - no bases
        __rtti_si(ti, name, base_ti)           - single, public, non-virtual base
        __rtti_class(ti, name, base_list, n)   - anything else
    It calls the base types' __tf functions first.
  * A vtable is an array of 8-byte entries {s16 delta, s16 index, void* pfn}.
    Entry 0 points at the class's __tf function; the virtual methods follow.
    Constructors/destructors store the vtable address into the object.

A later compiler of the same family (other games on the engine) ends every vtable with a null entry and leaves most
functions without .eh_frame records, so some function starts are only known from the vtables. That layout is detected
from the tables themselves (`null_terminated`); the vtables are then read up to their null entry and the functions
are split at the slots' targets before the types are collected.
"""
import collections

from .demangle import demangle_type, looks_like_type_name
from .mips import Insn, JR_RA


class RttiClass:
    def __init__(self, ti):
        self.ti = ti
        self.tf = None
        self.mangled = None
        self.name = None
        self.kind = None        # user / si / class / ptr / other
        self.bases = []         # [(base ti addr, offset or None, virtual, access)]
        self.vtables = []       # [Vtable]
        self.ctor_dtor = set()  # functions storing a vtable address


class Vtable:
    def __init__(self, addr, cls):
        self.addr = addr
        self.cls = cls
        self.entries = []       # [(delta, index, pfn)]


class Rtti:
    def __init__(self, A, log=print):
        self.A = A
        self.log = log
        self.classes = {}       # ti -> RttiClass
        self.by_tf = {}         # tf function -> RttiClass
        self.helpers = {}       # helper func addr -> role
        self.vtables = {}       # addr -> Vtable
        self.null_terminated = False  # every vtable ends with a null entry
        self.recovered = 0      # function starts found through vtables / taken addresses

    def run(self):
        self._find_helpers()
        runs = self._vtable_runs()
        done = sum(1 for _vt, _slots, term in runs if term)
        self.null_terminated = len(runs) >= 8 and done * 2 > len(runs)
        if self.null_terminated:
            self._recover_starts(runs)
        self._collect()
        self._vtables()
        self._label()
        self.log("rtti: %d types (%d with vtables), %d vtables, helpers %s" % (
            len(self.classes), sum(1 for c in self.classes.values() if c.vtables), len(self.vtables),
            {"%08X" % a: r for a, r in self.helpers.items()}))
        if self.null_terminated:
            self.log("rtti: null-terminated vtables; %d function starts recovered" % self.recovered)
        return self

    def _sites_with_typename(self):
        A = self.A
        for f in A.functions.values():
            if f.end - f.start > 0x200:
                continue
            for a, tgt, args in A.call_sites(f):
                s = A.strings.get(args.get(5))
                if s and looks_like_type_name(s) and tgt is not None:
                    yield f, a, tgt, args, s

    def _find_helpers(self):
        stats = collections.defaultdict(lambda: collections.Counter())
        for f, a, tgt, args, s in self._sites_with_typename():
            st = stats[tgt]
            st["n"] += 1
            st["a2"] += 6 in args
            st["a3"] += 7 in args
        for tgt, st in stats.items():
            if st["n"] < 5:
                continue
            hf = self.A.functions.get(tgt)
            if hf is not None and st["n"] * 2 < len(hf.callers):
                continue  # a general routine that some callers hand a name-like string
            if st["a3"] > st["n"] // 2:
                role = "__rtti_class"
            elif st["a2"] > st["n"] // 2:
                role = "__rtti_si"
            else:
                role = "__rtti_user"
            if role in self.helpers.values():
                role += "_%08X" % tgt
            self.helpers[tgt] = role

    # ---------------------------------------------------------------- null-terminated layout
    def _vtable_runs(self):
        """[(vtable addr, [pfn...], ends with a null entry)] for the pointer runs in data whose first entry points at
        code that calls an RTTI helper (a type-info function, perhaps behind a start the analysis missed)."""
        A = self.A
        sites = sorted(a for h in self.helpers for a in self._jal_sites(h))
        out = []
        seen = set()
        for a in sites:
            f = A.func_at(a)
            if f is None:
                continue
            for x in range(max(f.start, a - 0x200), a + 4, 4):
                for r in A.refs_to.get(x, ()):
                    vt = r.frm - 4
                    if r.kind != "data" or vt in seen or (A.p.u32(vt) >> 16) not in (0, 0xFFFF) or self._is_eh(r.frm):
                        continue
                    seen.add(vt)
                    slots = []
                    b = vt
                    while A.p.contains(b + 8):
                        h, pfn = A.p.u32(b), A.p.u32(b + 4)
                        if (h >> 16) not in (0, 0xFFFF) or not A.in_text(pfn) or pfn & 3:
                            break
                        slots.append(pfn)
                        b += 8
                    term = A.p.contains(b + 8) and A.p.u32(b) == 0 and A.p.u32(b + 4) == 0
                    out.append((vt, slots, term))
        return out

    def _jal_sites(self, target):
        A = self.A
        hf = A.functions.get(target)
        for c in (hf.callers if hf else ()):
            f = A.functions[c]
            for a in range(f.start, f.end, 4):
                i = A.insns[a]
                if i.kind == Insn.JAL and i.target == target:
                    yield a

    def _after_empty_return(self, a):
        """True if `a` follows a `jr $ra` whose delay slot is empty (and padding): a start _is_boundary does not see."""
        A = self.A
        b = a - 4
        while b > A.text_start and A.word(b) == 0 and a - b <= 64:
            if A.word(b - 4) == JR_RA:
                return True
            b -= 4
        return False

    def _crossed(self, f, a):
        """True if a branch of function f goes from one side of `a` to the other (then `a` is inside one function)."""
        A = self.A
        for x in range(f.start, f.end, 4):
            i = A.insns[x]
            if i.kind == Insn.BRANCH and f.start <= i.target < f.end and (x < a) != (i.target < a) and i.target != a:
                return True
        return False

    def _recover_starts(self, runs):
        """Split functions at the targets of null-terminated vtables' slots, and at the code addresses taken in code or
        data that follow a return with an empty delay slot (no branch crossing them)."""
        A = self.A
        n = 0
        for _vt, slots, term in runs:
            if not term:
                continue
            for x in slots:
                if x not in A.functions and A.split_at(x) is not None:
                    n += 1
        for x in sorted(A.taken_code):
            if x in A.functions or not self._after_empty_return(x):
                continue
            f = A.func_at(x)
            if f is not None and not self._crossed(f, x) and A.split_at(x) is not None:
                n += 1
        self.recovered = n
        if n:
            A.reattribute()

    def _helper_sites(self):
        A = self.A
        seen = set()
        for h in self.helpers:
            hf = A.functions.get(h)
            if not hf:
                continue
            for c in hf.callers:
                if c in seen:
                    continue
                seen.add(c)
                f = A.functions[c]
                for a, tgt, args in A.call_sites(f):
                    if tgt in self.helpers:
                        s = A.p.cstring(args.get(5, 0))
                        if s:
                            yield f, a, tgt, args, s.decode("latin-1")

    def _collect(self):
        A = self.A
        for f, a, tgt, args, s in self._helper_sites():
            role = self.helpers.get(tgt)
            ti = args.get(4)
            if ti is None:
                continue
            c = self.classes.get(ti) or RttiClass(ti)
            self.classes[ti] = c
            c.tf = f.start
            c.mangled = s
            c.name = demangle_type(s)
            self.by_tf[f.start] = c
            if role.startswith("__rtti_user"):
                c.kind = "user"
            elif role.startswith("__rtti_si"):
                c.kind = "ptr" if s[0] in "PR" else "si"
                if 6 in args:
                    c.bases = [(args[6], 0, False, "public")]
            elif role.startswith("__rtti_class"):
                c.kind = "class"
                c.bases = self._base_list(f, args.get(6), args.get(7))
        # Second chance for MI classes whose base list is built on the stack: use the __tf calls made
        for c in self.classes.values():
            if c.kind == "class" and not c.bases:
                f = A.functions[c.tf]
                for g in sorted(f.calls):
                    b = self.by_tf.get(g)
                    if b and b is not c:
                        c.bases.append((b.ti, None, None, None))

    def _base_list(self, f, arr, n):
        """Read a static base_info array {type_info* base; u32 offset:29, virtual:1, access:2}."""
        A = self.A
        out = []
        if arr is None or n is None or not (A.p.base <= arr < A.p.end) or n > 16:
            return out
        for k in range(n):
            ptr = A.p.u32(arr + 8 * k)
            bits = A.p.u32(arr + 8 * k + 4)
            out.append((ptr, bits & 0x1FFFFFFF, bool(bits >> 29 & 1), ("?", "public", "protected", "private")[bits >> 30]))
        return out

    def _vtables(self):
        A = self.A
        fstarts = A.functions
        for tf, c in self.by_tf.items():
            for r in A.refs_to.get(tf, []):
                if r.kind != "data":
                    continue
                vt = r.frm - 4
                hdr = A.p.u32(vt)
                delta = hdr & 0xFFFF
                delta = delta - 0x10000 if delta & 0x8000 else delta
                # vtables are referenced from code (ctors/dtors) or are adjacent to others; the entry header
                # must be small. Exclude eh_frame records (pc_begin fields).
                if (hdr >> 16) not in (0, 0xFFFF) or abs(delta) > 0x4000:
                    continue
                if self._is_eh(r.frm):
                    continue
                v = Vtable(vt, c)
                a = vt
                while A.p.contains(a + 8):
                    h = A.p.u32(a)
                    pfn = A.p.u32(a + 4)
                    if self.null_terminated:
                        # the slots up to the null entry, which is not one of them
                        if (h >> 16) not in (0, 0xFFFF) or pfn not in fstarts:
                            break
                    else:
                        if a != vt and pfn in self.by_tf:
                            break  # next vtable
                        if a != vt and not (pfn in fstarts or pfn == 0):
                            break
                        if (h >> 16) not in (0, 0xFFFF) and a != vt:
                            break
                    d = h & 0xFFFF
                    d = d - 0x10000 if d & 0x8000 else d
                    v.entries.append((d, h >> 16, pfn))
                    a += 8
                self.vtables[vt] = v
                c.vtables.append(v)
                for cr in A.code_refs_to(vt):
                    fn = A.func_at(cr.frm)
                    if fn:
                        c.ctor_dtor.add(fn.start)
                # a vtable may also be addressed at entry 1 (some compilers point past the header)
                for cr in A.code_refs_to(vt + 8):
                    fn = A.func_at(cr.frm)
                    if fn:
                        c.ctor_dtor.add(fn.start)

    def _is_eh(self, ptr_addr):
        """True if ptr_addr is the pc_begin field of an .eh_frame FDE."""
        A = self.A
        rec = ptr_addr - 8
        if not A.p.contains(rec):
            return False
        cp = A.p.u32(rec + 4)
        return cp != 0 and (rec + 4 - cp) in A.cies

    def _label(self):
        A = self.A
        for tgt, role in self.helpers.items():
            f = A.functions.get(tgt)
            if f:
                f.name = role
                f.lib = f.lib or "libgcc"
        for c in self.classes.values():
            f = A.functions.get(c.tf)
            if f and not f.name:
                f.name = "__tf" + c.mangled
                f.kind = "typeinfo"
        for v in self.vtables.values():
            cname = v.cls.name
            for n, (d, idx, pfn) in enumerate(v.entries[1:], 1):
                f = A.functions.get(pfn)
                if f is None:
                    continue
                f.tags.add("virtual")
                if not f.name:
                    # Only name it after this class if no base class has the same pointer in its vtable
                    if not self._inherited(v.cls, pfn):
                        f.name = "%s::vf%d" % (cname, n)
        nvt = collections.Counter(s for c in self.classes.values() for s in c.ctor_dtor)
        for c in self.classes.values():
            for s in c.ctor_dtor:
                f = A.functions.get(s)
                # Functions installing several classes' vtables are usually static initialisers
                if f and not f.name and nvt[s] == 1:
                    f.name = "%s::ctor_or_dtor_%08X" % (c.name, s)
                if f:
                    f.tags.add("ctor/dtor:" + c.name)

    def _inherited(self, cls, pfn, depth=0):
        if depth > 16:
            return False
        for bti, *_ in cls.bases:
            b = self.classes.get(bti)
            if not b:
                continue
            for v in b.vtables:
                if any(e[2] == pfn for e in v.entries[1:]):
                    return True
            if self._inherited(b, pfn, depth + 1):
                return True
        return False

    # ------------------------------------------------------------------ views
    def base_names(self, c):
        out = []
        for bti, off, virt, acc in c.bases:
            b = self.classes.get(bti)
            out.append(b.name if b else "?ti_%08X" % bti)
        return out

    def children(self):
        ch = collections.defaultdict(list)
        for c in self.classes.values():
            for bti, *_ in c.bases:
                ch[bti].append(c)
        return ch
