# Input and controllers

## Model

GT3 was written for the **DualShock 2**: 16 buttons, 12 of them
pressure-sensitive (D-pad, face buttons, L1/R1/L2/R2, each reporting
0–255), two 8-bit analog sticks, and two rumble motors (one on/off, one
variable). The game uses pressure for throttle and brake by default.

The engine always reads a **virtual DualShock 2** per player
(`src/input/virtual_pad.h`). Physical devices are mapped onto it through
*bindings*. So original behaviour is preserved exactly, and any device can
drive any control. In particular, an **analog trigger can drive a
pressure-sensitive button**: the default modern race layout maps the right
trigger to the race's accelerate (Cross pressure) and the left trigger to
brake (Square pressure).

Each player has two such pads, each with its own bindings:

- the **menu pad**: the DualShock 2 itself, used by every menu (and, with the
  `gt3` race layout, by the race too);
- the **race pad**: what the race reads while driving. Its buttons always mean
  what they meant in GT3 (see [Race controls](#race-controls)); the player's
  race layout says which physical inputs drive them.

All devices go through SDL3, which gives native support for Xbox
(360/One/Series), PlayStation (DS4/DualSense), Nintendo Switch Pro and Joy-Con,
8BitDo, Steam Controller/Deck, Android and Apple MFi controllers, plus
generic HID joysticks, wheels and pedals. Devices hot-plug at any time.

## Native features

| Feature | How it is used |
|---|---|
| Rumble | DS2 large motor → low-frequency motor, small motor → high-frequency motor. In races, each player's **Rumble** (Settings → Rumble & feedback, `[playerN] rumble`) picks how the motors run: **GT3 original** (`gt3`) follows GT3's own vibration: each step's level (suspension movement, curbs under the front wheels, the front tires' drive and brake forces, wall hits) on the large motor as GT3 sends it (at most half strength), never the small motor; most controllers play its low levels much stronger than a DualShock 2, so it runs all the time there. **Modern** (`modern`) rumbles for what the car meets: a wall or car hit pulses the large motor for 0.25 s by its speed change, off-road, wheelspin and a slide (the tire squeal) hold it, and curbs pulse the small motor at a rate that follows the speed. **Auto** (`auto`, the default) is GT3 original on a DualSense and Modern on every other controller. Either way, a car the AI drives, as after the finish, does not vibrate |
| Phone vibration (Android, off by default) | `[player1] phone_vibration = on` (Settings → Rumble & feedback → Phone vibration): player 1's rumble on the device's own motor, only while the controls driving them have no rumble motor (built-in handheld controls, touch, a controller without motors). A controller that rumbles keeps it; the phone stays still |
| Impulse triggers (Xbox One/Series) | `InputManager::set_trigger_rumble`, for brake/ABS feedback |
| Player LEDs / light bars | Set to the player number |
| Button labels | The UI shows Cross/Circle on PlayStation pads and A/B on Xbox pads |
| Motion sensors | `tilt` source: steer by rolling the controller (DualSense, DS4, Switch Pro, or a phone's own accelerometer on Android) |
| Wheels and pedals | Raw joystick axes with `pedal` and `invert` options; the wheel layout |
| Keyboard "analog" | `ramp=` smooths digital keys into analog throttle/steering |

## Controller

Each player picks one **Controller** (Settings → Controls, `[playerN]
controller`): **Auto** (`auto`), a connected pad or wheel by name (`controller`
holds part of its name; one that isn't connected stays listed as "(not
connected)"), or **Keyboard only** (`keyboard`; offered only when a keyboard
is connected, never in VR builds). Left/Right changes it; Cross does nothing
there. Auto takes a device SDL identifies as a wheel, otherwise the first free
gamepad, otherwise any other joystick (SDL also lists flight sticks and some
keyboards, mice and macro pads as joysticks, so those never take a player's
gamepad); Settings shows it as "Auto: <name>". A device chosen by name is used
whatever its type.

The kind of device in use picks the buttons; players don't choose a layout:

| Kind | Buttons |
|---|---|
| Gamepad | DualShock 2 layout 1:1 (triggers → L2/R2 pressure); back paddles → R2 / L2 (right up, left down: GT3's shift buttons) |
| Wheel (a device SDL identifies as a wheel) | Axis 0 steering, axes 1/2 pedals (resting at +1); typical Logitech layout. Races use GT3's steering-wheel path |
| Joystick (any other device SDL doesn't map as a gamepad) | None: nothing is pressed until its inputs are bound in Settings → Controls (saved to `[playerN.joystick.*]`) |
| Keyboard (Keyboard only, or no device connected) | Keyboard only |

VR builds' gamepad layout has B as Triangle (back) and Y as Circle. Every
layout also includes the player's keyboard layout. Player 1's: arrows = D-pad,
W/S = Cross/Square (ramped), A/D = left stick, Enter = Cross, Space = Circle,
Esc/Backspace = Triangle, Q/E = L1/R1, Z/C = L2/R2, F/G = L3/R3, P = Start,
Tab = Select, Home/End = right stick X, Page Up/Page Down = right stick Y (the
race camera's look; they were I/J/K/L before 0.4, now player 2's). Player 2's
keys sit around I / J / K / L, apart from player 1's, so both can share one
keyboard: I/K/J/L = D-pad, O = Cross, U = Triangle, ; = Circle, H = Square,
comma/period = L1/R1, N/M = L2/R2, ' = Start, / = Select. No default key is in
both players' sets. (While driving with the modern layout the keyboard has its
own race keys, below.) On any screen, F11 toggles fullscreen and F12 saves a
screenshot.

**Keyboard** (`keyboard = off`, hidden under Keyboard only) stops the keyboard
also driving a player, except player 1 with buttons only the keyboard can
drive (no device connected, or custom bindings with only keys): their keyboard
stays live, so the game can't be left with no way to control it. Keyboard only
always drives the player with the keyboard.

Configs from before the Controller choice used `preset` (`racing`, `classic`,
`wheel`, `keyboard`) and `device`; they are read once: `racing` and `classic`
are the gamepad (with the paddles), `wheel` is Auto (so a wheel is preferred),
`keyboard` is Keyboard only (unless its custom bindings had controller inputs:
then its device stays), and a named `device` stays. `wheel` with a named
`device` also keeps the wheel layout for that device whatever SDL makes of it
(`[playerN] wheel_device`). Custom bindings go by input type: `pad.*` to the
gamepad's set, `joy.*` to the wheel's, keys, mouse buttons and `tilt` to the
keyboard's. The config still carries `preset` and `device` for older builds.

When nothing player 1 has can drive the menus (no device and no keyboard to
use, or nothing readable bound to Cross and Up / Down), the menus outside races
(ours and the game's) read every connected controller with the gamepad layout's
buttons and the keyboard with its default keys, as Settings does, so the game
can't be left without a way back. Races always read each player's own controls.

Settings (every page, from our main menu or from the game's Key Configuration)
also reads every connected controller with the gamepad layout's buttons and the
keyboard with its default keys, besides player 1's own controls, so a choice or
bindings that leave player 1 without a working controller can always be changed
back. The game's menus and races follow the player's controls only, and nothing
else reads while a binding is being captured.

## Remapping in the app

In Settings, choices, switches and sliders change only with Left/Right or a
tap on their arrows, switch or track; Cross never changes a setting (it runs
actions and starts a rebind).

**Settings → Controls** holds the controllers and their bindings, for player 1
and player 2 (split screen) alike: under each player's **Controller**, **Race
controls** and **Keyboard** rows come their bindings, inline:

- **Player N race controls**: the race actions in driving order (Accelerate,
  Brake, Shift up, Shift down, Handbrake, Reverse, Change view, Look back,
  Pause, Steer, Steer left / right, the performance overlay);
- **Player N menu buttons**: a reset row, then the DualShock 2's buttons and
  sticks, as the game's menus read them.

Each row has a live meter (the button's pressure, or the stick's position) and
two columns: the **controller's** inputs and the **keyboard's**. Select a row,
then:

- **Cross** (or a tap): release everything, then press the new input. A key
  replaces that action's keyboard input; any other input (a pad button,
  trigger or stick, a back paddle, a wheel axis or button) replaces its
  controller input. The other column keeps its input. Esc, Android Back or a
  tap / click cancels instead (so those keys can't be captured: choose the
  layout again to get them back), and mouse buttons aren't captured (set them
  in `config.ini`, e.g. `mouse.left`). A button or key on Steer or a stick axis
  is refused: it could push it only one way (bind it to Steer left / Steer
  right, or the D-pad).
- **Square**: add an input instead of replacing
- **Select** (Delete on the keyboard): clear the highlighted column
- **Left/Right**: switch between the two columns

While an input is being captured (up to 6 s) Settings takes nothing else, and
the captured press does not also act on the page.

A player's bindings are two sets. The **controller's** is one per kind of
controller (gamepad, wheel), saved to `[playerN.bindings]` / `[playerN.race]`
for a gamepad and `[playerN.wheel.bindings]` / `[playerN.wheel.race]` for a
wheel. The **keyboard's** is one per player whatever the controller (Keyboard
only included), saved to `[playerN.keyboard.bindings]` /
`[playerN.keyboard.race]`. Custom profiles from before the two sets are split
once by input type (above). The keyboard's set also holds `tilt`, so tilt
steering works whatever the controller, a phone with no pad included.

**Race controls** offers **Modern**, **GT3 original** and, once custom race
controls exist, **Custom**; the **Menu buttons** row offers the device kind's
layout and **Custom**. Choosing a layout keeps the custom set: it stays saved
(`[playerN] race_custom = off` / `menu_custom = off`) and choosing Custom brings
it back. Editing a row while a layout is chosen makes custom bindings from that
layout as it stands (both sets) plus the edit; if another custom set is kept,
Settings asks first ("Replace your custom race controls with GT3 original plus
this change?", Cancel focused).

Under **GT3 original** the race reads the menu pad, so the race list shows each
action's menu button ("Accelerate (Cross)") and the menu buttons' inputs.
Editing one there starts custom race controls from GT3 original's mapping plus
the change.

**Inputs from other devices.** Any binding can come from any connected
controller, for either player: a brake pedal set, a second pad's trigger, a
shifter. Capture takes every connected pad, wheel and joystick. An input from
the player's own Controller is saved as usual. One from another device names
that device and shows its name ("DualSense Wireless Controller 2: Left trigger",
"G29 Pedals: Axis 2"; numbered among connected devices of that name). The
binding's `device=` option holds the name, then what tells devices of that name
apart: the device's serial number when the system reports one (Sony pads give
their Bluetooth address), otherwise its USB port path, otherwise its order among
them (`#2`). The name must match first; a binding whose serial or port path
isn't connected does nothing (it never falls back to a look-alike), and a tag
never reads the player's own Controller. Auto never gives a named device to the
other player. The layouts' own buttons stay on the Controller. Races use GT3's
steering-wheel path when Steer (LX) is bound to a wheel, and wheel forces go to
that wheel; rumble stays on the Controller.

The back paddles are controls of their own: capture takes them like any
button and they show as **Right paddle**, **Left paddle** (and **Right paddle
2**, **Left paddle 2** for a second pair). A paddle does one thing: binding it to
an action (Reverse, say) takes it off the one it had (shift); clearing Shift up
or down, or rebinding it to another button, takes shift off the paddle.

The **Settings** screen (Controls, Rumble & feedback and Touch pages) edits the
`[playerN]` controller/keyboard/race/rumble keys and bindings (and `[player1]
phone_vibration` on Android), every `[ffb]` key and every `[touch]` key
described below, and applies them immediately.

In the game's own menus, Options → **Key Configuration** shows a **Controller
Settings** button in place of the DualShock 2 table: Cross or Circle (or a tap)
opens Settings on the Controls page over the game, with player 1's Controller
row selected and the bindings below it. Its back button then reads **Back to
game**; it, or Triangle / Esc, returns to the Key Configuration page as it was
from anywhere on the page (buttons still held at that moment are ignored until
released). Triangle / Esc on **Customize layout...** (Touch) also goes straight
back to the game; the layout editor's **Done** returns to Settings.

## Config syntax

```ini
[player1]
controller = auto      ; auto, part of a device name ("DualSense", "G29"), or keyboard (only)
keyboard = on
race = modern          ; race layout (modern | gt3), used when there is no [player1.race]
rumble = auto          ; race rumble: auto (GT3 original on a DualSense, else modern) | modern | gt3 (see Native features)
phone_vibration = off  ; Android, player 1 only: rumble on the device's own motor (see Native features)

[player1.bindings]     ; the menu pad, a gamepad's inputs ([player1.wheel.bindings], [player1.joystick.bindings])
Cross  = pad.south
LX     = pad.leftx deadzone=0.08

[player1.keyboard.bindings]  ; the menu pad, the keyboard's and tilt (whatever the controller)
Cross  = key.Return
LX     = tilt curve=1.5

[player1.race]         ; the race pad: targets are GT3's buttons (Cross = accelerate ...)
Cross  = pad.righttrigger curve=1.4
Square = pad.lefttrigger
LX     = pad.leftx deadzone=0.08

[player1.keyboard.race]
Cross  = key.W ramp=0.12
Square = key.S ramp=0.08
LX-    = key.A ramp=0.2
LX+    = key.D ramp=0.2
```

Each section replaces its set's layout when present and keeps only that set's
inputs: keys, mouse buttons and `tilt` in the `.keyboard` sections, everything
else in the others.

**Targets:** `Select L3 R3 Start Up Right Down Left L2 R2 L1 R1 Triangle
Circle Cross Square`, and the axes `LX LY RX RY`, optionally as halves
(`LX-`, `LX+`).

**Sources** (alternatives separated by `|`):

| Source | Meaning |
|---|---|
| `key.<name>` | Keyboard scancode by SDL name; spaces become `_` (`key.Left_Shift`) |
| `pad.<button>` | `south east west north` (or `cross circle square triangle`), `dpup dpdown dpleft dpright`, `leftshoulder rightshoulder`, `leftstick rightstick`, `start back guide`, `paddle1..4`, `misc1`, `touchpad` |
| `pad.<axis>[+/-]` | `leftx lefty rightx righty lefttrigger righttrigger`; `+`/`-` selects a half |
| `joy.axis<N>[+/-]` | Raw joystick axis (wheels and pedals) |
| `joy.button<N>` | Raw joystick button |
| `joy.hat<N>.<up/down/left/right>` | Joystick hat direction |
| `tilt[+/-]` | Controller roll from the accelerometer (±45° = full lock; turning clockwise = positive). On phones and tablets the reading is remapped to the current screen orientation |
| `mouse.<left/right/middle>` | Mouse button |

**Options** (after a source):

| Option | Effect |
|---|---|
| `deadzone=0.1` | Inner deadzone, as a fraction of the range |
| `saturation=0.9` | Input level that already counts as full |
| `curve=1.5` | Response exponent; >1 gives finer control near rest |
| `scale=0.8` | Output multiplier |
| `invert` | Flip the axis |
| `pedal` | Map a −1..1 axis to 0..1 (combine with `invert` for pedals resting at +1) |
| `ramp=0.2` | Seconds to reach full value (makes digital inputs analog) |
| `threshold=0.3` | Pressure at which an analog source counts as a digital press |
| `device=G29%20Pedals` | Read it from that device instead of the player's Controller (below; `%20` = space, `@s:` serial, `@p:` port path) |

When several bindings target the same button, the strongest value wins.
Axis contributions are summed and clamped. A binding whose source isn't there
(device not connected, no such axis or button, a key while the keyboard
doesn't drive the player) does nothing, whatever its options.

## Race controls

The race pad's buttons keep GT3's meaning, and the race layout (per player:
`[playerN] race`, or **Race controls** in Settings) chooses what drives them:

| Action (race pad button) | `modern` (default) | `gt3` (the original's DS2 layout) | Modern keyboard, player 1 | Modern keyboard, player 2 |
|---|---|---|---|---|
| Steer (LX, D-pad) | Left stick / D-pad | Left stick / D-pad | A/D (ramped), ←/→ | J/L (ramped) |
| Accelerate (Cross, pressure) | Right trigger | Cross | W / ↑ (ramped) | I (ramped) |
| Brake (Square, pressure) | Left trigger | Square | S / ↓ (ramped) | K (ramped) |
| Handbrake (Circle) | Circle / B | Circle | Space | M |
| Shift up / down (R2 / L2) | Cross / Square (A / X), back paddles | R2 / L2 | E / Q | O / U |
| Reverse (Triangle, pressure) | L1 (held) | Triangle | R | H |
| Change view (R1) | Triangle / Y | R1 | C | period |
| Look back, held (L1) | R3 | L1 | X | comma |
| Pause (Start) | Start | Start | Esc, P | ; |
| Performance overlay (R3, ours; developer tool) | unbound | unbound | unbound | unbound |

Players without these keys rebind them in Settings → Controls.

There is no respawn, as in GT3: a car stays where it is. The performance overlay
is off controllers by default (under `gt3` the menu pad's R3 does nothing in a
race); bind it under the race controls in **Settings → Controls** if wanted. In time
trials, free runs, machine tests and licence tests the controller's own Select (the
menu pad's: Back / Select, Tab on the keyboard) shows or hides the ghost, as in the
original.

Change view cycles in-car (with mirror), in-car without mirror, chase and far
chase; `[race] views = gt3` (Settings: Race views) keeps GT3's own two, in-car
and chase. Races start in the view last used, in-car at first.

With `gt3` the race reads the menu pad itself, so every button does what it
did on a DualShock 2. In the modern layout shifting down at a standstill also
selects reverse (as in GT3). Wheels keep their pedals and buttons in both
layouts, and the back paddles shift in both (under `gt3` they press R2 / L2).
Configs from before per-player layouts used
`[race] buttons = gt3 | ours | auto`; it is read once as the default
(`ours` → `modern`; `auto` → `gt3` with the old `classic` preset). The touch
controls press the race pad's buttons directly.

In every race the race pad goes through GT3's own pad read, as a DualShock 2 with pressure buttons (the D-pad's
pressure steers with GT3's digital turn-in, R2 / L2 shift; a wheel as
GT3's steering wheel). **Analog response** (Settings > Controls, `[race]
analog_response`): `linear` (default) makes every analog input, sticks, triggers
and pressure buttons, wheels, touch and tilt, steer, accelerate, brake and pull
the handbrake exactly as far as it is moved, after GT3's own shaping (from its
least step: 10 % of a pedal); `original` keeps GT3's DualShock 2 curves as they
are (no steering over the stick's inner 29 %, full lock from 88 %; full throttle
from 18 % of the pressure range, brake 33 %, handbrake 10 %). The physics is the
same either way, and replays keep what the race got. The automatic or manual
gearbox is the race entry's pedal style (the gearbox setting, as in GT3).

Replays, the pause window and the quick menus read the menu pad, as in GT3.
Start opens the pause, in replays too. In a replay, Cross switches between the
replay cameras and the view you drove with, and Up/Down change the followed car
(TARGET MANUAL); in the Video view mode (REPLAY OPTIONS) Cross, Circle, L1 and R1
cut to a new shot instead (presentation §G3). On a quick menu's main page,
Triangle or Square leave at once as its EXIT, except in championships
(presentation §C2).

## Force feedback (`ffb`)

**Settings → Rumble & feedback** holds each player's **Rumble**, **Phone
vibration** (Android, player 1) and **Test rumble**, then these effects for all
players under **Triggers and wheels**.

Beyond DS2 rumble, the race sends a per-frame driving state
(`InputManager::set_force_feedback`: speed, brake, ABS active, steering
force, impacts) and each player's device turns it into native feedback:

| Device | Effect |
|---|---|
| DualSense (PS5) | Adaptive trigger on the trigger bound to the brake (Square): resistance after 20 % of travel, lighter at low speed; an 18 Hz vibration while ABS is working. Sent with `SDL_SendGamepadEffect` only when the effect changes |
| Xbox One/Series | Impulse trigger on the brake trigger pulses at ~16 Hz while ABS is working (`SDL_RumbleGamepadTriggers`) |
| Wheels with force feedback | SDL haptics: a self-centring spring that firms up with speed (light when stopped, full from 120 km/h) and a constant force from the front tyres' lateral load (self-aligning feel) plus a kick on impacts. The wheel's own autocentre is switched off while driving |

Effects stop by themselves ~0.25 s after the last update (pause, menus).
The brake trigger is found from the race bindings: whichever of
`pad.lefttrigger` / `pad.righttrigger` drives Square (brake).

```ini
[ffb]
enabled = on
triggers = on          ; DualSense / Xbox trigger effects
wheel = on             ; wheel spring and steering force
strength = 1.00        ; overall wheel force, 0..2
spring = 0.60          ; centring spring at speed, 0..1
trigger_force = 0.60   ; DualSense brake resistance, 0..1
invert = off           ; flip the wheel's steering force if it pulls the wrong way
```

Implementation: `src/input/force_feedback.*` (effect shaping is unit-tested in
`tests/test_platform_ffb.cpp`; on a DualSense, SDL accepts every effect we send; Xbox impulse triggers and wheel
forces are not yet tested on hardware, see docs/dev/PLATFORMS.md).

## Touch controls (phones and tablets)

Without a controller, an on-screen overlay drives player 1's virtual
DualShock 2. It is merged into the pad like any other device, so everything
above (pressure, menus, rumble targets) behaves the same.

The overlay appears on the first touch (on Android and iOS it is shown at
start unless a gamepad is connected) and hides as soon as a gamepad or the
keyboard is used. The Android back and volume keys do not hide it.

Each screen picks a layout:

| Layout | Controls |
|---|---|
| Menu (default) | D-pad (slide between directions), Cross / Circle / Square / Triangle, L1, R1, Select, Start. Taps away from the controls go to the screen underneath |
| Driving (race screen, while racing; its pause and quick menus use Menu) | Steering zone on the left half: put a finger down anywhere and drag sideways (the touch-down point is centre). Gas (Cross) and brake (Square) pedals on the right: the higher the finger on the pedal, the more pressure; a finger can slide from brake to gas. Shift up / down above the pedals, handbrake (Circle), and pause (Start) and camera at the top; they press the race pad's buttons, so they work with any [race layout](#race-controls) |

```ini
[touch]
mode = auto            ; auto | on | off
steering = drag        ; drag | tilt (roll the device like a wheel, ±45° = full lock)
pedals = position      ; position | area (touch pressure/size, where the screen reports it) | full
pedal_min = 0.35       ; pressure at the bottom of a pedal (position mode)
steer_range = 0.14     ; drag distance for full lock, fraction of the screen width
tilt_deadzone = 0.04   ; fraction of full lock
opacity = 0.50
size = 1.00            ; 0.6 .. 1.6
left_handed = off      ; mirror the layout (pedals on the left)
drive.gas = left 60 bottom 120 size 1.2   ; the layout editor's changes (below)
drive.camera = hidden
```

### Customizing the layout

Settings → Touch → **Customize layout...** shows the controls on the whole
screen as they sit in a race (Driving) or the game's menus (Menus). Tap a
control to select it, drag it anywhere, resize it with a pinch, the mouse
wheel or − / + (50–200 %, on top of `size`), and hide it or bring it back.
Reset puts one control back; Reset layout the whole page. Every change is
saved at once; Done or the back key leaves. The toolbar hides while a control
is dragged, then takes the free spot nearest the middle (when there is none,
it covers as little as it can and never most of a control); drag its
background to put it somewhere else. The four face buttons move and
resize together. Hidden controls are not drawn and take no touches; a
control placed over the steering zone takes its touches first.

Each changed control is one `[touch]` key, `drive.<control>` or
`menu.<control>`:

| Layout | Controls |
|---|---|
| `drive.` | `gas`, `brake`, `up`, `down`, `handbrake`, `pause`, `camera` |
| `menu.` | `dpad`, `buttons` (Cross, Circle, Square, Triangle), `l1`, `r1`, `select`, `start` |

The value is `[left|center|right <x> top|bottom <y>] [size <s>] [hidden]`:
the control's centre `x` units in from the left or right edge (or from the
screen's centre line, + = right) and `y` in from the top or bottom edge of
the area the controls keep to (the screen less a 14-unit margin, the cutout
and the rounded corners; the game area is 640×360 units). Measured from the
nearest edge, a layout carries over to screens of other shapes; a control
that would not fit is moved back inside. Positions are for the right-handed
layout: `left_handed` mirrors a custom layout too. Controls without a key
stay where the layout puts them.

Controls anchor to the edges of the whole screen, not the letterboxed
16:9 game area, so on wide (20:9) phones they move into the side bars and on
16:10 tablets into the top and bottom bars. They keep clear of the display
cutout (`SDL_GetWindowSafeArea`) and, on phones, of the rounded screen
corners (`TouchControls::set_insets`, fed by `App::safe_area_window()`).
`opacity`, `size`, `left_handed` and the custom layout are previewed live on
the Settings → Touch page.

The frontend's own screens (first-run setup, main menu, Developer options, Settings) are
operated by tapping and dragging directly (tap a row, tab or button; drag a
list to scroll; drag a slider; the Android back key goes back), so they use
no on-screen controls (`TouchLayout::None`). `App::touch().safe_area()`
returns the part of the 640×360 game area that the visible controls leave
free (the whole area when they are hidden), so HUDs can inset themselves,
`App::touch().control_bounds()` lists the visible controls' drawn bounds (the
race HUD keeps its corner blocks in place unless one would cover them), and
`App::touch().clear_above()` moves a screen's own button up clear of them (the
title menu's "GT3R menu" button goes above the D-pad).

In GT3's own menus a tap on an item moves the cursor there and a second tap chooses it; a
mouse (or a VR controller's pointer) focuses the item it hovers over, so one click chooses
(a scrolling row such as the tune shop's parts holds still meanwhile; vertical scrolling
lists, which centre the cursor's item, still take a tap to move and one to choose). In the arcade course, class and
car pickers, tap an icon of the strip to move there and tap it again to choose; taps elsewhere
on the left / right third step through the entries. The online lobby's pages work the same
way: rows and tiles follow a hovering mouse, a finger's first tap focuses, "<" ">" and the
footer's actions are buttons (docs/dev/ONLINE.md "Pickers").

Screens choose their layout by overriding `Screen::touch_layout()`
(`TouchLayout::Menu`, `Driving` or `None`). On desktop, `GT3R_PHONE=1`
opens a window shaped like a 20:9 phone (cutout, rounded corners, phone UI
sizing, whole-window `--shot`s) and `GT3R_TOUCH_SHOW=1` shows the controls
without a touch, for screenshots. The logic is in
`src/input/touch_controls.*` (unit-tested in `tests/test_platform_touch.cpp`),
drawing and SDL event routing in `src/app/screen_touch_overlay.*`, the layout
editor in `src/app/screen_touch_editor.cpp`.

## Windows: Xbox controllers without XInput

Windows reads Xbox controllers through its Xbox driver (`xinputhid`, for Bluetooth pads).

**What happened.** The case seen: an Xbox Series X controller over Bluetooth LE on Windows
11 (045e:0b13, firmware 5.17).
- `xinputhid` was installed and attached to the controller's Bluetooth device.
- When the controller connected, `xinputhid` created its XInput device (`HID\…&IG_00`),
  configured it, then removed it and restarted the controller (setupapi.dev.log; Kernel-PnP
  event 430, "requires further installation").
- The controller came back as a plain `HID-compliant game controller` (`input.inf`) and
  stayed that way until it reconnected.

**Fix on the PC:** turn the controller off and on (or re-pair it). XInput then sees it
(`XInput#0`), and SDL treats it as a normal Xbox gamepad.

**Symptoms in that state:**
- the controller works as a generic HID game controller (`joy.cpl` shows it);
- XInput reports "not connected" (1167);
- SDL skips Xbox-branded devices on its other paths because it expects XInput to handle
  them, so the game sees no controller at all.

The game does not work around this state (a DirectInput fallback was tried and removed:
the triggers never reach DirectInput, and SDL's HIDAPI Xbox driver refuses `\\?\HID#` paths
on Windows). Reconnecting the controller is the fix.

If reconnecting does not help, re-pair it or update its firmware in Microsoft's Xbox
Accessories app.

Diagnostics:
- `gt3tool input <iso> [seconds]` lists what SDL sees, with presses and raw axes.
- Any XInput test tool (for example a short PowerShell script calling `XInputGetState`)
  shows whether XInput itself sees the controller.

