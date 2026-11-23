"""Loading the game program from a disc image or an unpacked core.bin.

Supported inputs:
  * an ISO9660 disc image containing CORE.GT3 (and optionally GT3.VOL or other *.VOL archives); a dual-layer image
    with one file system per layer is read as one set of files
  * a raw program image (as written by `gt3tool core`), loaded at 0x00100000
  * a still-packed CORE.GT3 (one segment, or several laid out as one image from the lowest load address)
"""
import os
import re
import struct
import zlib

SECTOR = 2048
DEFAULT_BASE = 0x00100000
DEFAULT_ENTRY = 0x00100008
VOL_MAGIC = 0xACB990AD


class Iso:
    """Minimal ISO9660 reader: enough to find files in the root directory.

    `base_sector` is the image sector that is the file system's sector 0 (0, or the start of a dual-layer image's
    second file system); offsets given to _read are relative to it."""

    def __init__(self, path, base_sector=0):
        self.path = path
        self.base_sector = base_sector
        self.f = open(path, "rb")
        pvd = self._read(16 * SECTOR, SECTOR)
        if pvd[1:6] != b"CD001":
            raise ValueError("not an ISO9660 image")
        self.volume_sectors = struct.unpack_from("<I", pvd, 80)[0]
        root = pvd[156:156 + 34]
        self.root_lba, self.root_size = struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0]

    def _read(self, off, size):
        self.f.seek(self.base_sector * SECTOR + off)
        return self.f.read(size)

    def root_entries(self):
        data = self._read(self.root_lba * SECTOR, self.root_size)
        pos = 0
        while pos < len(data):
            n = data[pos]
            if n == 0:  # records do not cross sector boundaries
                pos = (pos // SECTOR + 1) * SECTOR
                continue
            lba = struct.unpack_from("<I", data, pos + 2)[0]
            size = struct.unpack_from("<I", data, pos + 10)[0]
            nlen = data[pos + 32]
            name = data[pos + 33:pos + 33 + nlen].decode("ascii", "replace").split(";")[0]
            yield name, lba, size
            pos += n

    def find(self, name):
        for n, lba, size in self.root_entries():
            if n.upper() == name.upper():
                return lba, size
        return None

    def read_file(self, name):
        hit = self.find(name)
        if not hit:
            raise FileNotFoundError(name)
        return self._read(hit[0] * SECTOR, hit[1])


def iso_layers(path):
    """The image's file systems: the first and, on a dual-layer image, the second, whose primary volume descriptor is
    the sector where the first's volume ends."""
    layers = [Iso(path)]
    end = layers[0].volume_sectors
    if end >= 16 and layers[0]._read(end * SECTOR, 6)[1:6] == b"CD001":
        try:
            layers.append(Iso(path, end - 16))
        except (ValueError, struct.error):
            pass
    return layers


class Program:
    """The unpacked game program: bytes mapped at `base`."""

    def __init__(self, data, base=DEFAULT_BASE, entry=DEFAULT_ENTRY, source=""):
        self.data = bytes(data)
        self.base = base
        self.entry = entry
        self.end = base + len(self.data)
        self.source = source

    def contains(self, addr):
        return self.base <= addr < self.end

    def u32(self, addr):
        o = addr - self.base
        return struct.unpack_from("<I", self.data, o)[0]

    def u16(self, addr):
        return struct.unpack_from("<H", self.data, addr - self.base)[0]

    def bytes_at(self, addr, n):
        o = addr - self.base
        return self.data[o:o + n]

    def cstring(self, addr, limit=512):
        o = addr - self.base
        if o < 0 or o >= len(self.data):
            return None
        e = self.data.find(b"\0", o, o + limit)
        if e < 0:
            return None
        return self.data[o:e]


def unpack_core_gt3(raw):
    if len(raw) < 6 or struct.unpack_from("<H", raw, 0)[0] != 0x0101:
        raise ValueError("CORE.GT3: bad header")
    size = struct.unpack_from("<I", raw, 2)[0]
    d = zlib.decompressobj(-15)
    out = d.decompress(raw[6:]) + d.flush()
    if len(out) != size:
        raise ValueError("CORE.GT3: size mismatch %d != %d" % (len(out), size))
    # [0x104] segment count, [0x108] entry point, then per segment: load address, size, its bytes. GT3 has one segment;
    # other games on the engine have several, laid out here as one image from the lowest load address (gaps zero, a
    # later segment over an earlier one where they overlap).
    count, entry = struct.unpack_from("<II", out, 0x104)
    if not 0 < count <= 64:
        raise ValueError("CORE.GT3: bad segment count %d" % count)
    segs = []
    pos = 0x10C
    for _ in range(count):
        if pos + 8 > len(out):
            raise ValueError("CORE.GT3: segment table past the end")
        load, size = struct.unpack_from("<II", out, pos)
        if pos + 8 + size > len(out):
            raise ValueError("CORE.GT3: segment past the end")
        segs.append((load, out[pos + 8:pos + 8 + size]))
        pos += 8 + size
    if count == 1:
        return Program(segs[0][1], base=segs[0][0], entry=entry)
    lo = min(l for l, _ in segs)
    hi = max(l + len(b) for l, b in segs)
    if hi - lo > 0x02000000:
        raise ValueError("CORE.GT3: segments out of range")
    img = bytearray(hi - lo)
    for l, b in segs:
        img[l - lo:l - lo + len(b)] = b
    return Program(img, base=lo, entry=entry)


def _vol_index(name):
    m = re.search(r"(\d+)\.VOL$", name.upper())
    return int(m.group(1)) if m else -1


def vol_archives(layers):
    """The disc's asset archives as [(Iso, name, byte offset in its file system)]: GT3.VOL, else every *.VOL in the
    layers' roots that has the archive's magic, the one with the largest index first."""
    found = []
    for iso in layers:
        for name, lba, size in iso.root_entries():
            if name.upper().endswith(".VOL") and size >= 0x14 and \
                    struct.unpack_from("<I", iso._read(lba * SECTOR, 4), 0)[0] == VOL_MAGIC:
                found.append((iso, name, lba * SECTOR))
    gt3 = [v for v in found if v[1].upper() == "GT3.VOL"]
    if gt3:
        return gt3[:1]
    return sorted(found, key=lambda v: -_vol_index(v[1]))


def walk_vol(iso, base):
    """Yield (path, size, compressed, sector) for every file of the archive at byte offset `base` of `iso`."""
    hdr = iso._read(base, 0x14)
    if len(hdr) < 0x14 or struct.unpack_from("<I", hdr, 0)[0] != VOL_MAGIC:
        return
    toc_size = struct.unpack_from("<I", hdr, 8)[0]
    toc = iso._read(base, toc_size)
    out = []

    def name_at(off):
        e = toc.find(b"\xff", off)
        return bytes((~b) & 0xFF for b in toc[off:e]).decode("latin-1")

    def walk(node, prefix, depth):
        if depth > 32:
            return
        count = struct.unpack_from("<I", toc, node + 4)[0]
        for i in range(1, count):
            child = struct.unpack_from("<I", toc, node + 8 + i * 4)[0]
            word = struct.unpack_from("<I", toc, child)[0]
            flags = word >> 24
            name = prefix + name_at(word & 0xFFFFFF)
            if flags & 1:
                walk(child, name + "/", depth + 1)
            else:
                sector, size = struct.unpack_from("<II", toc, child + 4)
                out.append((name, size, bool(flags & 2), sector))

    walk(0x14, "", 0)
    yield from out


def vol_files(layers):
    """Every archive file of the disc as (path, size, compressed, Iso, byte offset); a path two archives have is the
    first's."""
    seen = set()
    for iso, _name, base in vol_archives(layers):
        for path, size, comp, sector in walk_vol(iso, base):
            if path not in seen:
                seen.add(path)
                yield path, size, comp, iso, base + sector * SECTOR


def parse_vol_toc(layers):
    """Return a list of (path, size, compressed) from the disc's archives, or [] when it has none."""
    if isinstance(layers, Iso):
        layers = [layers]
    return [(path, size, comp) for path, size, comp, _iso, _off in vol_files(layers)]


def read_core(layers):
    """The packed CORE.GT3 from whichever layer has it."""
    for iso in layers:
        if iso.find("CORE.GT3"):
            return iso.read_file("CORE.GT3")
    raise FileNotFoundError("CORE.GT3")


def load(path):
    """Load a Program (and VOL listing when given an ISO). Returns (program, vol_entries)."""
    with open(path, "rb") as f:
        f.seek(0x8001)
        is_iso = f.read(5) == b"CD001"
    if is_iso:
        layers = iso_layers(path)
        prog = unpack_core_gt3(read_core(layers))
        prog.source = os.path.basename(path)
        vol = parse_vol_toc(layers)
        return prog, vol
    with open(path, "rb") as f:
        data = f.read()
    if len(data) > 6 and data[:2] == b"\x01\x01":  # packed CORE.GT3
        prog = unpack_core_gt3(data)
    else:
        prog = Program(data)
    prog.source = os.path.basename(path)
    return prog, []
