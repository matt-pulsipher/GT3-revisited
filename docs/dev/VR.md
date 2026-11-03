# VR headsets (Meta Quest)

The game runs in a VR headset through OpenXR. Menus, movies and replays show on a
large virtual screen in front of the player; while driving, the race is drawn in
stereo from the player's tracked head, seated in the car (in-car views) or floating
behind it (chase views), with the HUD on a panel in front. The Touch controllers
drive the same virtual DualShock 2 as any pad.

Builds with `GT3R_XR` (the Quest APK, `-Pquest`) contain it; every other build
compiles none of it.

## Status (2026-10-19)

| | |
|---|---|
| Meta Quest 3 (standalone APK) | ✅ verified: boot movie, menus on the virtual screen, controllers (buttons, sticks, analog triggers), stereo races in the in-car view with the HUD panel and mirror (played by hand, and auto-driven over adb). A steady 90 fps at the recommended 1680x1760 per eye, 4x MSAA (Trial Mountain, in-car): 6.5 ms of GPU and ~3 ms of CPU per frame |
| Chase views, steering wheel mode, haptics, the menus' pointer | ✅ verified on the Quest 3 (2026-10-19). The pointer's hover focus and the arcade pickers' strip taps (one click chooses) came after that test: covered by disc tests, not yet re-tried in the headset |
| Disc from the system file picker (the release APK has no `run-as`) | ✅ verified on the Quest 3 (2026-10-19): Settings > Disc & saves > Browse for a disc image opens the picker over the game; a disc in Download opens through its `content://` URI |
| Quest 2 / 3S / Pro | ⚠️ unverified (same APK; Quest 2 has less GPU headroom: lower `[vr] resolution` if it misses frames) |
| Linux + Monado (simulated headset) | ✅ the session, swapchains, frames and stereo races run under Monado's null compositor (below); used for testing without a headset |
| PC VR (SteamVR, Quest Link) | ⚠️ unverified: desktop builds with `-DGT3R_XR=ON` use the active OpenXR runtime when there is one (Vulkan only: Windows and Linux) |
| Car body in the in-car view (`[vr] car_body`) | ❌ off by default: GT3's car models have inward-facing cabin panels, which wall the eye in black |

## Building

SDL_GPU gained OpenXR in SDL 3.6 (its development branch at the time of writing),
so VR builds use SDL at a pinned commit (`GT3R_SDL_TAG` in
`cmake/Dependencies.cmake`; the same commit in `android/fetch-sdl.sh`). All other
builds stay on SDL 3.4.

Quest APK:

```bash
android/fetch-sdl.sh --xr
```
```bash
cd android && ./gradlew assembleDebug -Pquest
```
```bash
adb install -r app/build/outputs/apk/debug/app-quest-debug.apk
```

`-Pquest` switches the build to `third_party/SDL-xr` (or `-PSDL_XR_DIR=...`),
arm64 only, `-DGT3R_XR=ON`, the Khronos OpenXR loader package
(`org.khronos.openxr:openxr_loader_for_android`, at the OpenXR version of SDL's
headers) and the Quest manifest `android/app/src/quest/AndroidManifest.xml`
(VR launch category, head tracking, OpenXR broker queries, optional hand tracking
so a Bluetooth pad can start it). Same application id as the phone APK.

Release APK: `./gradlew assembleRelease -Pquest` with the release key
(`android/README.md`, Release signing); tags build it in CI as `gt3r-quest.apk`.
It shares the phone APK's application id and key, so a debug build installed over
adb must be uninstalled first (which deletes its saves: back up `files/saves/`).

Desktop (PC VR, or Monado for testing): `cmake -S . -B build-xr -DGT3R_XR=ON`
(fetches SDL at the pinned commit). Without an OpenXR loader or runtime it opens
the normal window.

## How it works

```
App::init
  GpuContext::create(xr)       device created through OpenXR (xr::Session::prepare_device:
                               SDL's XR properties + optional extensions the runtime has)
                               2D renderer offscreen: SDL_CreateGPURenderer(device, nullptr)
  xr::Session::create          session, LOCAL / VIEW spaces, swapchains (sRGB), canvases,
                               controller actions, the virtual gamepad, the present pipeline
App::iterate
  xr->poll()                   session states (begin / end / exit), focus, re-centring
  xr->begin_frame()            xrWaitFrame + xrBeginFrame, eye poses at the display time
  xr->update_input()           actions -> virtual gamepad axes / buttons; pointer
  ...screen update...
  SDL_SetRenderTarget(canvas)  the frame is drawn as always, into the 1280x720 canvas
  screen->render               RaceScreen while driving: render_vr_eyes (stereo)
  SDL_RenderPresent            submits the 2D renderer's work (offscreen renderers may present
                               with a target set)
  xr->end_frame()              present pass: canvases -> swapchain images; layers -> xrEndFrame
```

**Canvases.** The 2D renderer has no window: it draws into SDL render-target
textures, the 1280x720 canvas (the game's 640x360 UI at 2x; `App::layout_ui`
sets the logical presentation on it) and one canvas per eye at the swapchain
size. `end_frame` draws each into its swapchain image with `present.frag`.

**Colour.** GT3's colours are display-referred (8-bit sRGB values blended in that
space, as the PS2 does). The headset composites sRGB swapchains in linear light,
so the present pass writes each canvas pixel sRGB-decoded and the image's sRGB
encode stores the original byte. Runtimes without sRGB swapchains get the bytes
as they are (`mode.y`). The HUD panel is premultiplied: its colour is decoded
unpremultiplied.

**Why a render pass, not a copy.** A transfer copy into the Quest's swapchain
images cost 1.6 ms of GPU and stalled the CPU in the submit for ~7 ms a frame,
which serialized CPU and GPU (76-85 fps). Copies into ordinary textures and the
render pass have neither cost.

**Eyes.** `RaceScreen::render_vr_eyes` (`screen_race_vr.cpp`) places the seated
space in the world (`vr_base`): at the car's camera point 0 with the car's
attitude (in-car; `[vr] level_horizon` keeps only its heading), or at the chase
camera, level, along its lagged heading. Each eye's view is that placement times
the eye's tracked pose; its projection is the eye's own off-centre frustum
(`render::frustum`). The draw items are collected once for both eyes from the
head; each eye renders straight into its canvas (`View3D::draw_to`, the MSAA
picture resolved into it), then the sun glare, lamp glows and wet-road composite
use the eye's frustum (`render::Frustum`). The canvas is cleared transparent and
the HUD, mirror (`draw_mirror`, its usual place at the top of the panel), pause
menu and results are drawn on it as usual.

Not in stereo (they stay on the virtual screen): replays, the attract demo, the
licence demonstrations, the pre-race shot, 2P split screen, the Analyzer and the
debug orbit / TV cameras.

**Layers.** Screen frames: one opaque quad layer, `[vr] screen_width` wide,
`screen_distance` ahead of the seated origin. Stereo frames: the projection layer
plus the canvas as a premultiplied quad (`hud_width`, `hud_distance`, `hud_drop`
below eye level, tilted to face the eyes). Both are in the seated space, so in
the in-car view the HUD rides with the car.

**Seated space.** The runtime's LOCAL space re-centred on the head (position and
heading) on the first tracked frame and by Settings > VR > Recentre view. The
system's own re-centring (hold the Meta button) resets it to LOCAL's new origin.

**Input.** OpenXR actions on `/interaction_profiles/oculus/touch_controller`
feed an SDL virtual gamepad, "Meta Quest Touch": sticks, analog triggers, A B X Y,
grips as shoulders, stick clicks, Menu as Start; its rumble goes to the
controllers' haptics, both hands at the stronger motor's level (a controller in each
hand turns any split between the motors into a side). VR builds' gamepad layout (`quest`) is the gamepad
layout with B as Triangle (back) and Y as Circle. The race's modern layout then gives
right trigger throttle, left trigger brake, A/X shift up/down, Y view, B
handbrake, left grip reverse, right stick click look back (the right stick's look around is off
in stereo: the head looks around). With
`[vr] steering = wheel` both grips squeezed grab a steering wheel: the roll of
the line between the hands steers (`wheel_degrees` lock to lock) and the grips are
no longer shoulders. In menus the right controller's aim ray hits the screen: a
dot follows it and the trigger clicks (mouse events in UI coordinates; the pad's
right trigger reads 0 while it points there). Like a mouse it hovers: in the game's menus the
item under it is focused (fixed pages, horizontal scrolling rows, which then hold still, the
GT Mode garage rows and wheel shop tiles), so one click chooses; on vertical scrolling
selectors and the arcade pickers' strip a click moves the cursor and a second chooses.
The online lobby's pages take it the same way (docs/dev/ONLINE.md "Pickers": rows and
tiles follow it, "<" ">" and the footer's actions are buttons). So do our own pages, in a
headset only (a desktop mouse still only clicks): the row or tile under it is focused
(Settings, Mods, the save editor, Quick Race, the main menu, the disc page; the jukebox,
movie theater, car database and time trial lists, which hold still under it), and buttons and
tabs under it light up. A pressed over anything the page takes clicks on (a header's Back, a
tab, a row, the title's GT3R menu button: `Screen::clickable`) clicks it as the trigger does;
elsewhere A is the pad's Cross. Pulling the trigger tips the controller: from the start of a
pull (or an A click) the pointer holds still until the ray moves 3 degrees away, so the click
lands where it was. The race's menus take it too (docs/dev/RACE.md "Pointers on these
menus"); in a stereo race the pause window is on the HUD panel, so the ray aims at that panel
there (`hud_distance`, `hud_drop`, `hud_width`) instead of the screen. The online start page's
text (session code, name, address, relay server) is typed on the game's own keyboard,
VR builds only: Android's screen keyboard does not open over an immersive app
(`SDL_HINT_ENABLE_SCREEN_KEYBOARD` is off), and the controllers have no D-pad.

Two Android details: SDL drops joystick input while its window lacks keyboard
focus, which the invisible window in an immersive app never has
(`SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS`); and the Quest exposes its
controllers and a system device as Android gamepads (vendor 0x2833), which the
input manager ignores in VR builds.

**Frame pacing.** `xrWaitFrame` paces the loop; the step is the difference of
predicted display times. The runtime starts at CPU / GPU level 2: the session asks
for sustained-high (`XR_EXT_performance_settings`). `[vr] refresh` picks among
the rates the headset offers (`XR_FB_display_refresh_rate`, 90 Hz default).

## Settings ([vr] in config.ini, Settings > VR)

| Key | Default | |
|---|---|---|
| `enabled` | `on` | `off` (or `--no-vr`): the window, as on a phone |
| `stereo` | `on` | races in 3D; `off`: on the virtual screen |
| `level_horizon` | `off` | in-car view follows the car's heading only |
| `car_body` | `off` | the own car's body around the in-car view (see Status) |
| `steering` | `stick` | `wheel`: grips grab a steering wheel |
| `wheel_degrees` | `180` | wheel rotation lock to lock |
| `screen_width`, `screen_distance` | `2.6`, `2.4` | metres |
| `hud_width`, `hud_distance`, `hud_drop` | `1.5`, `1.3`, `0.12` | metres |
| `refresh` | `90` | Hz (nearest offered) |
| `resolution` | `1.0` | eye size x the recommended size (0.5..1.5); restart |

## Testing

**On a Quest** (developer mode, adb). Copy the disc into the debug APK's storage
and point `args.txt` at it; `--env NAME=value` lines set testing variables:

```bash
adb push gt3.iso /data/local/tmp/gt3.iso
```
```bash
adb shell "run-as org.gt3revisited.gt3r sh -c 'mkdir -p files && cp /data/local/tmp/gt3.iso files/'"
```
```bash
adb shell "run-as org.gt3revisited.gt3r sh -c 'printf \"%s\n\" /data/user/0/org.gt3revisited.gt3r/files/gt3.iso --screen race --env GT3R_RACE=course=trialmt,car=r34_gtr_v_99,auto=1,cam=2 --env GT3R_VR_TIMING=1 > files/args.txt'"
```

Without wearing the headset, keep it awake with
`adb shell am broadcast -a com.oculus.vrpowermanager.prox_close` (`prox_far` to
undo). A screenshot of the headset view:
`adb shell am startservice -n com.oculus.metacam/.capture.CaptureService -a TAKE_SCREENSHOT`
(into `/sdcard/Oculus/Screenshots/`). Frame rate and GPU time: the runtime's
`VrApi` logcat lines (`FPS=`, `App=` GPU ms, `GPU%`, `CPU4/GPU=` levels);
`GT3R_VR_TIMING=1` logs the loop's split (wait / app / present / xrEndFrame). A
launch from adb while the headset shows a system dialog (e.g. "controllers
required") is held until it is dismissed in the headset.

**Without a headset** (Linux, e.g. the OrbStack VM): Monado's null compositor
with its simulated HMD.

```bash
sudo apt-get install monado-service libopenxr-loader1 libopenxr1-monado
```
```bash
sleep 600 | XRT_COMPOSITOR_NULL=1 SIMULATED_ENABLE=1 monado-service &
```
```bash
XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json GT3R_VR_EYE_SIZE=720 GT3R_RACE=course=trialmt,car=r34_gtr_v_99,auto=1,cam=2 GT3R_SHOT_FRAME=300 xvfb-run -a ./build-xr/gt3r --screen race --shot out.png
```

(`monado-service` needs a pollable stdin, hence the pipe.) In a stereo frame
`--shot` writes the canvas (the HUD over transparency) and `out-eyes.png` (both
eyes). The simulated HMD recommends 128x128 eyes, where distant textures fall to
their smallest mip levels and look black: `GT3R_VR_EYE_SIZE` overrides the size.

## Open items

- The own car's body in the in-car view: hide its cabin panels (the inward-facing
  primitives) and keep the bonnet, wings and mirrors
- Passthrough or a modelled room behind the menus' screen (`XR_FB_passthrough`)
- A curved screen (`XR_KHR_composition_layer_cylinder`), and the HUD's elements as
  separate panels (gauges low, lap times high)
- A fade between the screen and the stereo views (the pre-race shot cuts to the seat)
- Foveated rendering (`XR_FB_foveation`) and multiview, if a heavier course needs them
- Replays in stereo from the TV cameras (comfort: cuts and pans)
