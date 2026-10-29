# Platforms

Status of each target, how to build and test it, and known issues. "Verified"
means built *and* run here; "built" means compiled and unit-tested but not
run interactively; "unverified" means neither.

## Status (2026-10-28)

| Target | Compiler | Status | Evidence |
|---|---|---|---|
| macOS 26 arm64 | Apple clang 21 | ✅ verified | Daily development platform; full game, Metal |
| macOS arm64 | Homebrew GCC 16 (libstdc++) | ✅ built, tests pass | `-Wall -Wextra` clean, `gt3r_tests` 0 failures |
| Linux x86_64/arm64 (Ubuntu 24.04) | GCC 13 | ✅ built, tests pass | Docker `ubuntu:24.04` with the CI apt list; SDL 3.4.12 fetched; warning-free; ctest passes |
| Linux (Ubuntu 24.04) | clang 18 | ✅ built, tests pass | Same container, `CC=clang CXX=clang++` |
| Linux desktop session | | ⚠️ unverified | Not run on a real X11/Wayland desktop (no display in the container) |
| Windows x64 | MinGW-w64 GCC 13 (cross) | ✅ built | All executables link; see below for the wine test run |
| Windows x64 | MSVC 2022 | ✅ verified | CI (windows-2022) and a real PC (Windows 11): all tests pass with the disc, 3D (Vulkan, RTX 3080) and online races verified |
| Android arm64 (API 36 emulator) | NDK 27 clang | ✅ verified | Boot → opening movie → title → Arcade → Time Trial → driving with Vulkan; touch controls, tilt, back, home/resume, rotation, file picker |
| Android x86_64 | NDK 27 clang | ✅ built | Part of the same APK; not run |
| Android real device | NDK 27 clang | ✅ verified | Galaxy A15 5G (Mali-G57 MC2, Android 16, 4 GB), the low-end baseline: menus, car select (~75 fps), Arcade races (35–55 fps). Races also on a Moto G Power 2024 (Imagination BXM), a Hiby R4 and a Galaxy S26 |
| Meta Quest 3 (VR) | NDK 27 clang | ✅ verified | The Quest APK (`-Pquest`, SDL 3.6 development branch): menus on a virtual screen, stereo races at a steady 90 fps; see [VR](VR.md) |
| iOS real device | Apple clang 21 (Xcode 27.1) | ✅ verified (disc and menus) | iPhone 15 Pro (A17 Pro, iOS 26): 3D renderer (SDL_GPU Metal, MSAA 2x, 72 pipelines warmed in 8 ms); the document picker opens a disc image in place from an SMB share (Files app), the game boots to GT Mode, and the next start reopens it by itself; the user data folder shows in Files. Landscape both ways, taps, home and back. Races, controllers and tilt not tried yet |
| iOS simulator (arm64) | Apple clang 21 (Xcode 27.1) | ⚠️ built | Warning-free (no unit tests on iOS); starts to the setup screen with the 2D renderer only (SDL_GPU does not accept the simulator's GPU) |

## Building and testing

### macOS

```bash
brew install sdl3 cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build
```

Second compiler (catches missing includes that libc++ hides):

```bash
brew install gcc
CC=gcc-16 CXX=g++-16 cmake -S . -B build-gcc -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-gcc -j -- -k
```

This needs Homebrew's SDL3: GCC cannot build SDL's Objective-C Cocoa
backend, so the FetchContent fallback does not work with GCC on macOS.

### Linux

Install the SDL build dependencies (the list CI uses), then build as usual:

```bash
sudo apt-get install ninja-build pkg-config libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
  libwayland-dev libdecor-0-dev libxkbcommon-dev libegl1-mesa-dev libdrm-dev libgbm-dev \
  libudev-dev libdbus-1-dev zlib1g-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build
```

SDL 3.4 needs `libxtst-dev` (configure fails without it). Without Wayland,
X11 or Vulkan dev packages SDL drops those backends silently.

From macOS, the same build runs in Docker (`colima start`, then
`docker run -v $PWD:/src:ro ubuntu:24.04 ...` with the commands above).

### Windows

Visual Studio 2022: `cmake -S . -B build` then build `gt3r` (or open the
folder in VS). SDL3 and zlib are fetched. The renderer uses Vulkan on
Windows (we ship SPIR-V and MSL only, no DXIL yet; see RENDERING.md), so a
GPU driver with Vulkan support is needed for 3D. Without it the game runs
2D-only.

MinGW cross build (Windows executables from macOS or Linux; `brew install mingw-w64` on
macOS), with the toolchain file in `cmake/mingw-w64.cmake`:

```bash
cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release \
      -DGT3R_FETCH_SDL=ON -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"
cmake --build build-mingw --target gt3r gt3tool gt3r_tests gt3r-relay
```

- `GT3R_FETCH_SDL=ON` is needed: without it CMake finds the host's own SDL3
  (e.g. Homebrew's).
- The static `gt3r.exe` needs only system DLLs (UCRT, built into Windows 10
  and 11).

Running the Windows build on a Mac with CrossOver's Wine (a separate bottle):

```bash
CX=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
$CX/cxbottle --bottle gt3r-test --create --template win10_64
GT3R_DISC='Z:\path\to\image.iso' $CX/wine --bottle gt3r-test --wait-children "$PWD/build-mingw/gt3r_tests.exe"
```

The full suite passes this way with the disc, including every online test
(Winsock, the receive thread, the relay). The game starts and draws its menus,
but 3D is unavailable: we ship SPIR-V and MSL only, and SDL's GPU backend does
not accept CrossOver's Vulkan (MoltenVK). A D3D12 path (DXIL shaders) would
render there through CrossOver's D3DMetal, and on Windows machines without
Vulkan.

### Android

See [android/README.md](../../android/README.md). Short version:

```bash
export JAVA_HOME=$(/usr/libexec/java_home -v 21)   # JDK 17 or 21
android/fetch-sdl.sh                                 # SDL 3.4.12 into android/third_party/SDL
cd android && ./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Meta Quest (VR): `android/fetch-sdl.sh --xr`, then `./gradlew assembleDebug -Pquest`
(writes `app-quest-debug.apk`); building, testing over adb and the design are in
[VR](VR.md).

Testing on an emulator without the file picker: the app reads extra
command-line arguments from `<user data>/args.txt`, one per line
(`--env NAME=value` sets a testing variable such as `GT3R_RACE`). On a
debug build:

```bash
adb shell "run-as org.gt3revisited.gt3r sh -c 'echo /data/local/tmp/gt3.iso > files/args.txt'"
```

The app can read world-readable files in `/data/local/tmp`. The full image
(3.9 GB) rarely fits on an emulator; a sparse copy that keeps the VOL
header and only the files a test needs works (create it with `truncate` on
the device and `dd ... conv=notrunc` the needed ranges in). Remove it and
uninstall the APK afterwards. `adb shell input keyevent` presses are
shorter than a frame; both keyboard and touch input latch such presses so
injected events register.

The user data folder is the app's files dir (private storage). The app's documents
provider (`GT3DocumentsProvider`, authority `<application id>.documents`) shows it in
the system Files app and file pickers as GT3 Revisited, so players can copy mods in
and screenshots out; the Mods screen's **Open folder** opens it there
(`GT3Activity.openUserFolder`). `GT3R_MOD_ZIP=<archive>` makes the Mods screen install
that archive as if picked, for testing without the file picker.

Useful checks: `adb emu sensor set acceleration x:y:z` for tilt steering,
`settings put system user_rotation N` (with `accelerometer_rotation 0`)
for orientation, `logcat --pid=$(pidof org.gt3revisited.gt3r)` for the
engine log ("Race perf" lines every 3 s while racing).

GPU driver crashes on phones we don't have: Firebase Test Lab's catalogue
(`gcloud firebase test android models list`) has older Mali phones, e.g. the
Motorola G20 (`java`, Android 11, Mali-G52, driver r25p1, the phone of public
issue 7), the Galaxy A10 (`a10`, Mali-G71), the Galaxy S9 (`starlte`,
Mali-G72) and the Galaxy Tab A8 (`gta8wifi`, Mali-G52 with a newer driver).
Start-up crashes need no disc: a Robo test of the APK reproduces them, and the
run's results bucket holds the tombstones and the logcat (Robo relaunches a
crashed app once).

```bash
gcloud firebase test android run --type robo --app app-debug.apk \
    --device model=java,version=30 --timeout 120s
```

To try several variants (shaders, pipeline state) in one run, use an
instrumentation test with the test orchestrator (`--use-orchestrator`): it
starts the game in a fresh process for each test, so a driver crash fails only
that test. Each test sets environment variables (`android.system.Os.setenv`),
starts `GT3Activity` and waits for a file the game writes once it gets far
enough. A forked child process per variant does not work: the Mali driver's
memory is not copied into the child, which then crashes in the driver whatever
it builds. This is how the Mali-G52 crash was traced to the `noperspective`
qualifier ([Rendering](RENDERING.md)).

### iOS

CMake's own iOS support with the Xcode generator (needed: it signs the app and compiles its
icon catalog); no Xcode project is kept in the repository. SDL is always built from source
(an installed one is a macOS build), and the MSL shaders are the ones macOS uses. iOS 16 or
later, iPhone and iPad, landscape. The tools and tests are not built, as on Android.

```bash
cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
      -DGT3R_IOS_TEAM=<team ID> -DGT3R_IOS_BUNDLE_ID=<your.own.identifier>
open build-ios/GT3Revisited.xcodeproj   # scheme gt3r, pick the device, Run
```

Signing is never committed. `GT3R_IOS_TEAM` is your Apple developer team ID; Xcode then
signs automatically. A free Apple ID is enough: signed in under Xcode → Settings → Accounts, it
has a personal team, whose ID is the `OU` of its certificate
(`security find-certificate -c "Apple Development" -p | openssl x509 -noout -subject`); a paid
membership shows it on developer.apple.com → Account → Membership details. With a free account
the installed app stops opening after 7 days (installing it again renews it; the user data
stays), and the iPhone needs Developer Mode (Settings → Privacy & Security) and the developer
trusted once (Settings → General → VPN & Device Management). A bundle identifier belongs to
the first team that registers it, so a signed build needs one of your own
(`GT3R_IOS_BUNDLE_ID`); the project's `org.gt3revisited.gt3r` is used only for unsigned builds. Without `GT3R_IOS_TEAM` the build is unsigned (CI), which no device installs.

From the command line, after configuring with your team:

```bash
cmake --build build-ios --config RelWithDebInfo --target gt3r -- -allowProvisioningUpdates
xcrun devicectl list devices
xcrun devicectl device install app --device <device> build-ios/RelWithDebInfo-iphoneos/gt3r.app
```

The user data folder (config, saves, mods, screenshots) is the app's Documents, which the
Files app shows as On My iPhone › GT3 Revisited and Finder lists under the iPhone's Files
(`UIFileSharingEnabled`). SDL has no file dialog on iOS, so **Open disc image** shows the
system's document picker (`platform/ios_files.h`): the image is read where it is, never copied
(iCloud Drive, On My iPhone, another app's folder such as a browser's downloads, other
providers), and a bookmark of it in the app's user defaults opens it again at the next start.
An image copied into the GT3 Revisited folder needs no
bookmark; its saved path is moved into the next install's folder, which iOS names anew
(`rebase_app_container_path`). No document type is declared: opening an image from the Files
app would copy it into the app.

The simulator needs its own build tree (the SDK is chosen at configure time):

```bash
cmake -S . -B build-ios-sim -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
      -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-ios-sim --config RelWithDebInfo --target gt3r
xcrun simctl install booted build-ios-sim/RelWithDebInfo-iphonesimulator/gt3r.app
xcrun simctl launch --console-pty booted org.gt3revisited.gt3r   # the engine log on the terminal
```

There it runs with the 2D renderer only (SDL_GPU rejects the simulator's GPU), and its virtual
gamepad keeps the touch controls hidden until the screen is touched.

### Pointer scripts

`GT3R_POINTER_SCRIPT` feeds a screen scripted pointer events, in UI units (640 × 360),
routed as the VR controller's pointer is (`App::pointer_script`). It tests mouse and
touch handling without a person at the machine, and `--shot` shows the pointer's arrow.
Steps are separated by `;`:

| Step | Event |
|---|---|
| `wait:<frames>` | wait that many frames |
| `move:<x> <y>` | the mouse moves there (hover) |
| `click:<x> <y>` | moves there, left button down and up |
| `rclick:<x> <y>` | right button down and up |
| `drag:<x0> <y0> <x1> <y1>` | press, move in 8 steps, release |
| `wheel:<steps> <x> <y>` | the wheel (+ = up / away) at the point |
| `tap:<x> <y>` | a finger's tap (no hover) |
| `pinch:<x> <y> <from> <to>` | two fingers centred on the point, moving from `from` to `to` units apart in 8 steps |

The log names each step with its frame. Example (the online lobby's car page, then the
classes, then Class B):

```bash
GT3R_POINTER_SCRIPT="wait:40;click:100 191;wait:15;rclick:5 5;wait:10;click:90 33;wait:10;click:320 100" \
GT3R_SHOT_FRAME=120 GT3R_ONLINE="host" ./build/gt3r.app/Contents/MacOS/gt3r disc.iso --screen online --shot out.png
```

## CI

`.github/workflows/build.yml`:

- **desktop** (ubuntu-24.04, windows-2022, macos-14): configure, build
  `gt3r gt3tool gt3r_tests`, run ctest (tests that need a disc skip).
- **android**: JDK 21, installs NDK 27.0.12077973 + CMake 3.22.1 with
  sdkmanager, fetches SDL, `assembleDebug`, uploads the APK.
- **ios** (macos-14): configures with the Xcode generator and builds the app for devices,
  unsigned (no artifact: an unsigned app installs nowhere).

Validated locally with `actionlint`. GitHub Actions runs it on every push and
pull request; `v*` tags also run the package jobs and `release` (below).

## Release packages

No game data is in any package. `cmake/Packaging.cmake` adds the icon, the
macOS `Info.plist` (`packaging/Info.plist.in`) and install rules;
`GT3R_FETCH_SDL=ON` builds SDL from source so the result doesn't depend on a
system or Homebrew SDL.

| Package | Contents | Build |
|---|---|---|
| macOS (arm64, macOS 12+) | `gt3r.app` (icon, Info.plist, `.iso` document type, ad-hoc signature), `gt3tool`, README, LICENSE, `Barlow-OFL.txt` | `cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release -DGT3R_FETCH_SDL=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0`, build `gt3r gt3tool`, `cmake --install build-rel --prefix dist`, `ditto -c -k --keepParent` |
| Windows x64 | `gt3r.exe` (icon resource), `gt3tool.exe`, `SDL3.dll`, README, LICENSE, `Barlow-OFL.txt` | same with `-DGT3R_SDL_SHARED=ON`; zip |
| Linux x86_64 | `bin/gt3r`, `bin/gt3tool`, `share/applications/gt3r.desktop`, icon, README, LICENSE, `Barlow-OFL.txt` (`share/doc/gt3r`) | same; `tar czf` (built on Ubuntu 22.04 for an older glibc) |
| Android | `gt3r-<tag>-android.apk`, signed with the project's release key (CI secrets; [android/README.md](../../android/README.md#release-signing)) | `./gradlew assembleRelease` |
| Meta Quest | `gt3r-<tag>-quest.apk`, the VR build, same key | `android/fetch-sdl.sh --xr`, `./gradlew assembleRelease -Pquest` ([VR](VR.md)) |

CI jobs `package`, `package-android` and `package-quest` build these on `v*` tags and manual
runs (`workflow_dispatch`) and upload them as artifacts. The macOS app is
only ad-hoc signed (no developer certificate, not notarized): the first launch
needs System Settings → Privacy & Security → Open Anyway (README, "Installing"). The
icon is original artwork drawn by `packaging/make_icon.py` (also writes the
Windows `.ico` and the Android launcher icons).

On a `v*` tag, job `release` collects the packages into a draft GitHub Release
under stable names (`gt3r-<platform>.<ext>`, e.g. `gt3r-windows-x64.zip`; see
[ONLINE](ONLINE.md#versions-and-updates), "Downloads"), with `SHA256SUMS` and build
provenance attestations; it is published after review.

Verified locally: the macOS package (self-contained: `otool -L` shows only
system frameworks; the bundle launches, `plutil -lint` OK); the Linux tarball
and the Windows zip from a MinGW cross build in Docker (see below for what
they contain). Released packages are the CI jobs' (the Windows zip there is
MSVC-built); an AppImage is not made.

## Windows / MSVC review

Checked by reading the code before the first MSVC build (which then built and
passed every test):

- Disc access goes through `SDL_IOFromFile` with 64-bit offsets, so the
  3.9 GB image is fine; host files use `SDL_LoadFile`/`SDL_SaveFile`
  (UTF-8 paths). No `<filesystem>`, `fstream`, `fseek`/`ftell` in the game.
- No POSIX-only headers or calls, no `M_PI` (code uses `std::numbers`), no
  `__builtin_*`/VLAs; `__attribute__` uses are guarded; `%zu`/`%llu` formats
  are correct for MSVC's UCRT; no `windows.h` includes (so `min`/`max`
  macros and `PadRumble::small` cannot clash).
- `_CRT_SECURE_NO_WARNINGS` is defined for MSVC (fopen/getenv/localtime
  deprecation noise).
- Dev tools only: `gt3tool` opens some paths with
  `fopen` (ANSI code page on Windows, so non-ASCII paths fail there).

## Known issues

- **Android frame time (emulator):** the API 36 arm64 emulator renders the
  race at 5–8 fps (120–200 ms/frame) at 2560×1600 with gfxstream Vulkan.
  CPU scene time is ~11 ms; MSAA 1 vs 4 makes no difference, so the
  emulator's GPU translation is the bottleneck. Not representative of real
  devices (see the table). `[video] render_scale` (auto ≈ 900 lines on
  mobile) brought the 20:9 phone-shaped emulator from ~150 to ~88 ms per
  frame.
- **Touch overlay vs HUD:** resolved. Controls use the side bars on wide
  phones, and the race HUD insets (and on 16:9 shrinks) its meters into
  `App::touch().safe_area()`. Menus accept direct taps (tap to focus, tap
  again to confirm) as well as the D-pad overlay.
- **Emulator "gamepad":** the Android emulator exposes its keyboard as a
  gamepad (`qwerty2`), so the overlay starts hidden there and appears on
  the first touch.
- **Portrait on Android 16 tablets:** the activity is landscape-only; when
  the system forces portrait the game is letterboxed (works, no crash).
- **Tilt direction on gamepads — verified on a DualSense (2026-10-11,
  Bluetooth, macOS):** resting on its side with the right grip down (turned
  clockwise as seen by the player), the accelerometer read
  (-8.78, 4.36, 0.97) m/s² and `steering_roll` gave +63°, i.e. steer right,
  as intended. DS4 and Switch Pro are not tried; they share SDL's axis
  convention.
- **Force feedback — partly verified:** on the same DualSense, SDL reported a
  PS5 pad with accelerometer, gyro and rumble, and accepted every command we
  send (`SDL_SendGamepadEffect` returned success for the brake resistance,
  the ABS vibration and the off packet; light bar and rumble also succeeded).
  How the effects *feel* has not been judged by a person yet. Xbox impulse
  triggers and wheel haptics are untested with real devices; the wheel force
  direction may need `[ffb] invert = on` on some wheels.
- Real Linux desktop sessions are unverified (see the table).
- **High-refresh displays:** every platform draws every display refresh by
  default. Settings → Display → Limit to 60 fps (`[video] limit_60fps = on`)
  draws every Nth refresh on 120 Hz and faster displays (120 Hz → 60 fps,
  144 → 72) through SDL's main loop rate; checked on a 120 Hz Mac display.
