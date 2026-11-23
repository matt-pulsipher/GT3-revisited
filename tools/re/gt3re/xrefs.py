"""String cross-reference index and magic-number detection."""
import re
import zlib

from .image import SECTOR, iso_layers, vol_files

KNOWN_MAGICS = ["Tex1", "MLTX", "FT00", "IMGS", "GTTW", "GTTR", "ENGN", "PRZ0", "IDDB", "SShd", "SSbd",
                "MADS", "Base"]

EXT_RE = re.compile(r"\.[A-Za-z0-9]{1,4}$")
FMT_RE = re.compile(r"%[-+ 0#]*\d*(?:\.\d+)?[hlL]*[sdiuxXcfp]")


def vol_magics(iso_path, vol_entries, limit_bytes=16):
    """Read the first bytes of every VOL file. Returns {path: first bytes}."""
    if not iso_path or not vol_entries:
        return {}
    out = {}
    for name, _size, comp, iso, off in vol_files(iso_layers(iso_path)):
        raw = iso._read(off, SECTOR if comp else limit_bytes)
        if comp:
            try:
                raw = zlib.decompressobj(16 + 15).decompress(raw, limit_bytes)
            except zlib.error:
                raw = b""
        out[name] = raw[:limit_bytes]
    return out


def printable4(b):
    return len(b) == 4 and all(0x20 < c < 0x7F for c in b)


def fmt_to_regex(s):
    """Turn a printf-style path pattern into a regex over VOL paths."""
    parts = []
    pos = 0
    for m in FMT_RE.finditer(s):
        parts.append(re.escape(s[pos:m.start()]))
        c = m.group()[-1]
        parts.append(r"\d+" if c in "diu" else r"[0-9A-Fa-f]+" if c in "xX" else r"[^/]*")
        pos = m.end()
    parts.append(re.escape(s[pos:]))
    return re.compile("".join(parts), re.I)


class VolIndex:
    def __init__(self, entries):
        self.paths = [e[0] for e in entries]
        self.set = set(p.lower() for p in self.paths)
        dirs = set()
        for p in self.paths:
            parts = p.split("/")
            for k in range(1, len(parts)):
                dirs.add("/".join(parts[:k]).lower())
        self.dirs = dirs
        self.exts = set()
        for p in self.paths:
            m = EXT_RE.search(p)
            if m:
                self.exts.add(m.group().lower())

    def classify(self, s):
        """Return (category, detail) for a string with respect to the VOL contents, or None."""
        if not self.paths or len(s) < 2 or len(s) > 200:
            return None
        t = s.lower().lstrip("/")
        if t in self.set:
            return ("vol-path", s)
        if t.rstrip("/") in self.dirs:
            return ("vol-dir", s)
        if "%" in s and "/" in s:
            rx = fmt_to_regex(t)
            hits = [p for p in self.paths if rx.fullmatch(p.lower())]
            if hits:
                return ("vol-pattern", "%d files, e.g. %s" % (len(hits), hits[0]))
            # prefix match: pattern used as directory
            hits = [p for p in self.paths if rx.match(p.lower())]
            if hits:
                return ("vol-pattern", "%d files (prefix), e.g. %s" % (len(hits), hits[0]))
        if "/" in t:
            top = t.split("/")[0]
            if top in self.dirs:
                return ("vol-like", s)
        if t in self.exts or (t.startswith(".") and t in self.exts):
            return ("extension", s)
        return None


def magic_set(vol_heads):
    """Printable 4-byte file magics found in VOL files, with counts."""
    counts = {}
    for path, head in vol_heads.items():
        h = head[:4]
        if printable4(h):
            m = h.decode("ascii")
            counts.setdefault(m, []).append(path)
    for m in KNOWN_MAGICS:
        counts.setdefault(m, [])
    return counts
