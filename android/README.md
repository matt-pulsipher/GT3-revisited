# Android build

Requirements: Android SDK with NDK 27.0.12077973 (the version `app/build.gradle`
pins) and CMake 3.22.1 or later (install both from the SDK Manager), and
**JDK 17 or 21**. The Gradle wrapper (8.12) does not run on JDK 24 or later.
The APK runs on Android 7.0 (API 24) and later.

```bash
android/fetch-sdl.sh
```
```bash
cd android && ./gradlew assembleDebug
```

The APK is written to `app/build/outputs/apk/debug/`. Install it with
`adb install -r app/build/outputs/apk/debug/app-debug.apk`.

`fetch-sdl.sh` checks out SDL3 into `android/third_party/SDL`. That single
checkout supplies both SDL's Java glue (`org.libsdl.app`) and the native
library, so the two always match. The engine's root `CMakeLists.txt` builds
`libmain.so`, which `GT3Activity` loads.

## Using it

1. Copy your GT3 `.iso` or `.chd` to the device (or keep it on an SD card or USB drive). Create the CHD from the ISO with `chdman createdvd`.
2. Launch the app and tap **Open disc image**. Android's file picker
   opens and the app reads the image through the Storage Access Framework, so
   no storage permission is needed.
3. Bluetooth and USB controllers (DualSense, Xbox, Switch Pro, 8BitDo and
   others) work immediately. The phone's accelerometer is available as the
   `tilt` binding source for steering.

The Android back button acts as Triangle (back). Without a controller,
on-screen touch controls appear: a D-pad and face buttons in menus, and a
steering zone (drag, or tilt with `[touch] steering = tilt`), pressure
pedals and shift buttons while racing. They hide when a controller is used.
See [docs/INPUT.md](../docs/INPUT.md#touch-controls-phones-and-tablets).
**Settings → Controls → Phone vibration** (off by default) sends player 1's
rumble to the device's own motor while the controls in use have none (touch,
or a controller without rumble).

Phones and tablets start with dynamic resolution on and 2× MSAA
(**Settings → Display**).

Tested on the Pixel emulators (API 36, arm64): Vulkan renderer, boot →
opening movie → title → Arcade → Time Trial → driving with touch controls
and tilt, back button, home and resume (the race pauses), rotation,
and the system file picker. On real devices: menus and races on a Galaxy A15
5G (the low-end baseline) and other Android devices. Status and testing tips:
[docs/dev/PLATFORMS.md](../docs/dev/PLATFORMS.md).

## Meta Quest (VR)

The same game as an immersive VR app for Meta Quest headsets: menus on a virtual
screen, races in stereo from the driver's seat. It needs SDL's development branch
(SDL_GPU's OpenXR support):

```bash
android/fetch-sdl.sh --xr
```
```bash
cd android && ./gradlew assembleDebug -Pquest
```
```bash
adb install -r app/build/outputs/apk/debug/app-quest-debug.apk
```

The headset must be in developer mode. Releases carry it signed as
`gt3r-quest.apk` (`./gradlew assembleRelease -Pquest` with the release key below).
Players copy their disc image into the headset's Download folder and pick it with
**Open disc image** (the system file picker opens over the game). Details, controls
and testing tips: [docs/dev/VR.md](../docs/dev/VR.md).

## Release signing

Release APKs are signed with the project's own key, so every release can
update an installed copy. Create it once with `android/make-release-key.sh`
(the certificate names the project, not a person), keep the `.jks` and its
password in a password manager, and add two GitHub Actions secrets:

```bash
base64 -i gt3r-release.jks | gh secret set ANDROID_KEYSTORE_BASE64
```
```bash
gh secret set ANDROID_KEYSTORE_PASSWORD
```

Tagged builds (`v*`) then run `assembleRelease` with it. Locally, set
`GT3R_KEYSTORE` (path) and `GT3R_KEYSTORE_PASSWORD` before
`./gradlew assembleRelease`; without them the release APK is unsigned.
Debug builds keep using the local debug key.
