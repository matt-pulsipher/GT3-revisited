# Disc compatibility

Any image that contains `CORE.GT3` and a valid `GT3.VOL` is accepted.
Images not in this list run with a warning until someone verifies them.

| Serial (BOOT2) | Version | Region / notes | Status |
|---|---|---|---|
| `PBPX_955.03` | 1.10 | NTSC-U/C (menus for BR/FR/GR/IT/JP/SP/UK/US present on disc) | ✅ verified |
| `SCUS_971.02` | 1.00 | NTSC-U retail. Same game as 1.10: CORE.GT3 and GT3.VOL are byte-identical; only the boot files' product code (SYSTEM.CNF, boot ELF string) and the disc headers differ | ✅ verified |
| `SCES_502.94` | 2.00 | PAL (Europe, Australia; En/Fr/De/Es/It). Loads the `_eu` database and `product_eu.gcf`, metric units; movies 640x512 at 25 fps | ✅ boots, races, GT Mode menus (disc tests tuned to NTSC-U data still differ) |
| `PAPX_…` / `SCPS_…` | — | NTSC-J | ❓ untested |

To verify a new version, run `gt3tool info`, `gt3tool bgm` and a few
`gt3tool tex` commands, and check the asset browser. Then add its serial to
`kKnownDiscs` in `src/disc/disc.cpp`.

Known regional differences to account for: Japanese releases use 100× credit
values (a new GT Mode game starts with 1,500,000 Cr in NTSC-J vs 18,000 Cr in
NTSC-U, with the baseline also retuned for the international release).

Only `.iso` images (2048-byte sectors) are supported for now; `.bin/.cue` (2352-byte) and CHD support are planned.
