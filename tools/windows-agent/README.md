# Windows build agent

Optional developer scripts for a Windows PC that builds and tests GT3 Revisited natively
(MSVC), driven from another machine over SSH (OpenSSH server on Windows). They assume
everything lives under `D:\gt3r` on the PC; copy these scripts there and adjust the paths if
yours differ.

| Script | What it does |
|---|---|
| `install-tools.ps1` | Installs Git, CMake and Visual Studio 2022 Build Tools (C++ workload) from the official installers (run as administrator). `winget` does not work over SSH. |
| `build.ps1 [-Clean]` | Unpacks `D:\gt3r\gt3r-src.tar.gz` (a `git archive` of the branch) and builds `gt3r`, `gt3tool`, `gt3r_tests` and `gt3r-relay` with the Visual Studio generator, Release. |
| `build-angle.ps1` | Builds ANGLE (OpenGL ES on Direct3D 11) with vcpkg into `D:\gt3r\angle`: the DLLs `-DGT3R_ANGLE_DIR` copies next to the game, for the OpenGL ES renderer. |
| `test.ps1 [filter]` | Runs the unit tests with `D:\gt3r\GT3.iso` as the disc. |
| `race-shot.ps1` | Runs a `.cmd` in the logged-in user's desktop session (a one-off scheduled task) and waits for it. Programs started over SSH have no desktop, so the game cannot open a window or use the GPU there. |
| `race-shot.cmd` | A short time trial with a screenshot (`D:\gt3r\win-race.png`): checks the 3D renderer. |
| `flicker-shot.cmd` | 12 consecutive frames of a race behind the car (`D:\gt3r\win-flicker-<k>.png`, `GT3R_SHOT_COUNT`): reflection flicker checks. Run with `race-shot.ps1 -Script D:\gt3r\flicker-shot.cmd -Shot D:\gt3r\win-flicker-11.png`. |
| `capture-shot.cmd [car] [first]` | 30 consecutive frames of the race's 3D view (`D:\gt3r\win-cap-<k>.png`) with the car standing still at the start, via `GT3R_CAPTURE3D` (GPU copies read back at the end). Use this for flicker that comes from frames overlapping on the GPU: `--shot` waits for the GPU every frame and hides it. |
| `online-join.cmd <exe> <host> <frames>` | Joins an online battle at `<host>` and screenshots the race (`D:\gt3r\win-online.png`). |
| `input-diag.cmd <gt3tool.exe> <seconds>` | `gt3tool input` in the desktop session: every controller SDL sees and its presses (`D:\gt3r\input-diag.txt`). |
| `input-shot.cmd <exe> <tag>` | Screenshots the game's controller screen (`D:\gt3r\win-input-<tag>.png`). |
| `xinput-check.ps1` / `.cmd` | Asks XInput directly which of the four slots has a connected controller (`D:\gt3r\xinput.txt`). A paired Bluetooth Xbox controller stays listed in Device Manager while asleep; XInput says "not connected" (1167) then. |
| `add-key.ps1` | Installs an SSH public key for key logins (admin accounts read `administrators_authorized_keys`). |

From the other machine (`<user>@<pc>` is your account on the PC; `$C` holds any extra SSH
options, e.g. a ControlMaster socket):

```bash
git archive --format=tar.gz -o gt3r-src.tar.gz HEAD
scp $C gt3r-src.tar.gz <user>@<pc>:D:/gt3r/
ssh $C <user>@<pc> 'powershell -NoProfile -ExecutionPolicy Bypass -File D:\gt3r\build.ps1'
ssh $C <user>@<pc> 'powershell -NoProfile -ExecutionPolicy Bypass -File D:\gt3r\test.ps1'
ssh $C <user>@<pc> 'powershell -NoProfile -ExecutionPolicy Bypass -File D:\gt3r\race-shot.ps1 -Exe D:\gt3r\build\Release\gt3r.exe'
```

The PC's SSH shell is `cmd.exe`: multi-line commands stop at the first line break, so anything
longer goes into a script file.

Tested (2026-10-13, Windows 11, RTX 3080, MSVC 14.44): the build succeeds; every unit test
passes with the disc; the Vulkan renderer draws races; an online battle between the PC and a
Mac races both ways on a LAN.
