#!/usr/bin/env bash
# Synthetic fixtures, not taken from any disc; outputs are committed.
# Requires chdman.
#   synthetic.chd, synthetic-cd.chd   pattern() bytes as a DVD image and as a raw Mode 1 CD track
#   synthetic-fs-*.chd                a small ISO9660 file system (one file, README.TXT) in each layout chdman
#                                     makes: DVD, cooked Mode 1 (padded to 4 frames), raw Mode 2 with Form 2
#                                     postgap sectors after the volume, and the same with a pregap in the image
set -euo pipefail
out=$(cd -- "$(dirname -- "$0")" && pwd)
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT
python3 - "$scratch" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
# Keep in sync with pattern() in tests/test_chd_image.cpp.
data = bytes(0 if (i // 4096) % 3 == 0 else (i * 7 + i // 2048 + 1) & 255
             for i in range(512 * 1024))
(root / 'synthetic.iso').write_bytes(data)
def bcd(n):
    return (n // 10) * 16 + n % 10
def frame_header(frame, mode):
    msf = frame + 150
    return b'\x00' + b'\xff' * 10 + b'\x00' + bytes((bcd(msf // 4500), bcd(msf // 75 % 60), bcd(msf % 75), mode))
with (root / 'synthetic.bin').open('wb') as f:
    for frame in range(len(data) // 2048):
        f.write(frame_header(frame, 1))
        f.write(data[frame * 2048:(frame + 1) * 2048])
        f.write(bytes(288)) # EDC/ECC left zero: chdman stores these frames as they are
(root / 'synthetic.cue').write_text('FILE "synthetic.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n')

# The file system: PVD at 16, terminator at 17, root directory at 18, README.TXT at 19, filler to 37 sectors.
# Keep the volume id, file name and text in sync with tests/test_chd_image.cpp.
sectors = 37
text = b'GT3 Revisited CHD test file\n'
def both16(n):
    return n.to_bytes(2, 'little') + n.to_bytes(2, 'big')
def both32(n):
    return n.to_bytes(4, 'little') + n.to_bytes(4, 'big')
def record(lba, size, flags, name):
    length = 33 + len(name) + (len(name) + 1) % 2
    r = bytes((length, 0)) + both32(lba) + both32(size) + bytes(7) + bytes((flags, 0, 0)) + both16(1) + bytes((len(name),)) + name
    return r + bytes(length - len(r))
fs = bytearray(sectors * 2048)
pvd = bytearray(2048)
pvd[0:7] = b'\x01CD001\x01'
pvd[8:40] = b' ' * 32
pvd[40:72] = b'GT3R_CHD_TEST'.ljust(32)
pvd[80:88] = both32(sectors)
pvd[120:124] = both16(1)
pvd[124:128] = both16(1)
pvd[128:132] = both16(2048)
pvd[156:190] = record(18, 2048, 2, b'\x00')
pvd[881] = 1
fs[16 * 2048:17 * 2048] = pvd
fs[17 * 2048:17 * 2048 + 7] = b'\xffCD001\x01'
root_dir = record(18, 2048, 2, b'\x00') + record(18, 2048, 2, b'\x01') + record(19, len(text), 0, b'README.TXT;1')
fs[18 * 2048:18 * 2048 + len(root_dir)] = root_dir
fs[19 * 2048:19 * 2048 + len(text)] = text
for i in range(20 * 2048, len(fs)):
    fs[i] = (i * 13 + i // 2048) & 255
(root / 'fs.iso').write_bytes(fs)
(root / 'fs-mode1.cue').write_text('FILE "fs.iso" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n')
# Mode 2 Form 1 sectors (subheader submode 0x08), then three Form 2 postgap sectors (0x20).
def mode2_frames(first):
    frames = b''
    for s in range(sectors):
        frames += frame_header(first + s, 2) + bytes((0, 0, 0x08, 0) * 2) + fs[s * 2048:(s + 1) * 2048] + bytes(280)
    for s in range(sectors, sectors + 3):
        frames += frame_header(first + s, 2) + bytes((0, 0, 0x20, 0) * 2) + bytes(2328)
    return frames
(root / 'fs-mode2.bin').write_bytes(mode2_frames(0))
(root / 'fs-mode2.cue').write_text('FILE "fs-mode2.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n')
pregap = b''.join(frame_header(s, 2) + bytes((0, 0, 0x20, 0) * 2) + bytes(2328) for s in range(5))
(root / 'fs-pregap.bin').write_bytes(pregap + mode2_frames(5))
(root / 'fs-pregap.cue').write_text(
    'FILE "fs-pregap.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 00 00:00:00\n    INDEX 01 00:00:05\n')
PY
chdman createdvd -i "$scratch/synthetic.iso" -o "$out/synthetic.chd" -f
chdman createcd -i "$scratch/synthetic.cue" -o "$out/synthetic-cd.chd" -f
chdman createdvd -i "$scratch/fs.iso" -o "$out/synthetic-fs-dvd.chd" -f
chdman createcd -i "$scratch/fs-mode1.cue" -o "$out/synthetic-fs-mode1.chd" -f
chdman createcd -i "$scratch/fs-mode2.cue" -o "$out/synthetic-fs-mode2.chd" -f
chdman createcd -i "$scratch/fs-pregap.cue" -o "$out/synthetic-fs-pregap.chd" -f
