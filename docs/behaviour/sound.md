# Sound: voices, mixing, the sequencer, engine and race sounds, ambience

This document describes how the engine makes every sound except the choice
of music: the voice mixer and its reverb, the sequencer that plays the menu
music, sound effects, the engine note model, the other per-car race sounds,
and the course ambience. Which music plays when is in [music.md](music.md).
File layouts (`ENGN`, `INST`, `se.inf`, `SEQG`, `CRSS`) are in
[../formats/sound.md](../formats/sound.md).

Code: `src/audio/` (all of it), `src/race/race_audio.*`, `src/race/car_sfx.*`,
`src/race/race_sfx.*`, `src/app/screen_race_audio.cpp`,
`src/app/screen_race_drive.cpp` (where the race feeds the sound models),
`src/menu/flow/menu_sound.cpp`. Tests: `tests/test_sound.cpp`,
`tests/test_sound_seq.cpp`, `tests/test_sound_race.cpp` (run with
`GT3R_DISC` for the disc checks). Tools: `gt3tool engine-wav`, `se-wav`,
`seq-wav`, `engn`, `ins`, `se`; the sound test screen `gt3r --screen sound`.

## §1 Output and units

### §1.1 Two output streams

`AudioEngine` opens two output streams on the output device (Settings →
Audio, `[audio] device`): the system default, which follows the system's own
choice, or a device chosen by name on Windows, macOS and Linux (Android and the
browser route sound themselves). A chosen device that is not connected leaves
the sound on the default; plugging it in moves the sound there, unplugging it
moves it back (SDL's device added / removed events). Each stream has a device
of its own on the same hardware, so the race pause can pause the music alone;
a switch opens the new pair before moving the streams, keeps a pause, and runs
on a thread of its own (opening or closing a device takes up to ~300 ms on
macOS). Movies open their sound on the same device. With Settings → Audio
open, the `cursor` sound plays once a switch has landed, on the new device.

| Stream | Carries | Rate | Gain |
|---|---|---|---|
| music | streamed `.ads` music ([music.md §5.1](music.md#51-streams)) | the file's own | the request's level × Music volume × the output trim 0.5 ([music.md §6](music.md#6-volume-and-options)) |
| voices | the voice mixer: engine notes, effects, ambience, sequenced menu music | 48 kHz stereo | 1 (the volumes are the mixer's bus gains) |

Inside the mixer every voice plays on one of two **buses** (§2.4): the
**music bus** carries the sequenced menu music and its gain is the Music
volume; the **effects bus** carries every other voice and its gain is the
Sound effects volume. So the Sound effects volume never scales the music.
Both volumes are settings (Settings → Audio, `[audio] music_volume` and
`[audio] sfx_volume`, 0..1, default 0.8). The game thread starts, stops and
changes voices; the audio thread renders them, a block of a few milliseconds
at a time. Every mixer call is safe from either thread and never waits for a
block to render: a change made while one renders applies after it.

### §1.2 Units

The engine keeps the sound data's own units throughout:

- **Pitch**: a 16-bit value where `0x1000` plays a sample at **44.1 kHz**
  (its natural rate). Twice the value is an octave up.
- **Volume**: signed 16-bit, `0x3FFF` is full level (about 1.0); negative
  values invert the phase.
- **Pan**: 0..127, 0 hard left, 64 about the center, 127 hard right.
- **Q14**: fixed point with `0x4000` = 1.0.

### §1.3 Pan law and note pitch

The **pan law** is a sine table: for pan $p$ in 0..127, with
$t[i] = \mathrm{round}(16384 \sin(\tfrac{i}{127}\cdot\tfrac{\pi}{2}))$,

$$\text{left} = \frac{\text{vol}\cdot t[127-p]}{2^{14}},\qquad
\text{right} = \frac{\text{vol}\cdot t[p]}{2^{14}}$$

(`sound::pan_gains`). Pan 0 gives full left and silent right; pan 64 gives
about 0.703 / 0.711.

A **note** is converted to a pitch relative to a sample's center note
(`sound::note_to_pitch`); with note and center in 1/256 semitone,

$$\text{pitch} = \mathtt{0x1000}\cdot 2^{(\text{note}-\text{centre})/3072},$$

evaluated in 1/32-semitone steps through a 384-entry-per-octave table. A
note at the center plays at `0x1000`; an octave up at `0x2000`. Sound effects
are always keyed at their region's lowest key, so an effect region's pitch is
fixed by its center and key (`InstRegion::pitch`).

## §2 Voice mixer

`VoiceMixer` (`src/audio/voice_mixer.*`) is a software model of the PS2's
sound chip voices. Every sound except streamed music is a voice.

### §2.1 Voices and allocation

There are **46 voices**. `VoiceMixer::key_on` takes the sample data, start
offset, pitch, left/right volume, the two envelope words (ADSR1/ADSR2), the
reverb send switch, a **priority** value and the voice's bus (§2.4), and
returns a handle. Handles
of voices that have finished or been reused are ignored by every later call,
so callers never touch someone else's voice.

A new voice takes the first free voice. If none is free it **steals** one:
only voices whose priority value is **greater than or equal to** the
request's are candidates; of those the one with the highest priority value,
and among equals the **oldest**, is taken. If no voice qualifies the
request is dropped. The sample banks give every region priority 0, so in
practice the oldest voice is stolen; the tire loops (§7.6) and the course
ambience (§9.4) set their own priorities so that loud ones are hard to
steal. A playing voice's priority can be changed (`VoiceMixer::set_priority`);
these held loops re-send theirs every update as their loudness changes.

`key_off` sends a voice into its release phase; `stop` silences it at once.

### §2.2 Sample playback

Samples are PS-ADPCM: 16-byte frames of 28 samples, with flags in each
frame. A frame marked **loop start** latches its address as the loop point;
after a frame marked **end**, the voice jumps to the loop point if the frame
is also marked **repeat**, and otherwise stops (and frees itself). The
decoder's prediction history carries on across the jump, which is how the
loops on the disc are made to be seamless.

The game pitch is converted to the chip's 48 kHz pitch as

$$p_{hw} = \min\!\left(\left\lfloor \frac{p\cdot \mathtt{0xEB3}}{2^{12}}\right\rfloor,\ \mathtt{0x4000}\right)$$

(`VoiceMixer::hardware_pitch`; `0xEB3/0x1000` = 44100/48000), and the voice
advances $p_{hw}/\mathtt{0x1000}$ source samples per 48 kHz output sample, so
a voice reads at most 4 source samples per output sample (about 4.35 times
its natural 44.1 kHz rate). Between samples the
mixer interpolates with the chip's **4-point Gaussian** filter (a fixed
512-entry table indexed by the fractional position); a 4-point Catmull-Rom
interpolator can be selected for offline renders (`seq-wav --cubic`).

### §2.3 Envelope

Each voice has an ADSR envelope, level 0..0x7FFF, that scales its samples
($s\cdot\text{env}/2^{15}$). The fields of the two envelope words:

| Word | Bits | Field |
|---|---|---|
| ADSR1 | 15 | attack exponential (else linear) |
| ADSR1 | 8..14 | attack rate 0..127 |
| ADSR1 | 4..7 | decay rate (x4; decay is always exponential) |
| ADSR1 | 0..3 | sustain level $L$: decay ends at $(L+1)\cdot\mathtt{0x800}$ |
| ADSR2 | 15 | sustain exponential |
| ADSR2 | 14 | sustain direction: 1 decrease, 0 increase |
| ADSR2 | 6..12 | sustain rate 0..127 |
| ADSR2 | 5 | release exponential |
| ADSR2 | 0..4 | release rate (x4) |

Phases run attack → decay → sustain (until key off) → release → off. Each
phase steps the level by the chip's counter rule (`VoiceMixer::env_tick`):
for rate $r$ the base step is $7-(r \bmod 4)$ up or $-(8-(r \bmod 4))$
down; rates below 44 apply the step every sample, scaled by
$2^{11-\lfloor r/4\rfloor}$; rates from 44 apply the base step once every
$2^{\lfloor r/4\rfloor-11}$ samples; an attack, sustain or release rate with
all its bits set never moves. Exponential decrease scales the step by the current level; exponential
increase slows down above 0x6000. Sustain level 15 is above the maximum, so
such a decay ends at once at full level. When the release reaches 0 the
voice is off.

The default envelope for engine layers (`0x000F` / `0x0003`) is an instant
attack, full sustain, and a linear release of about 16 samples (a third of
a millisecond): voices start and stop without clicks but without audible
fades.

### §2.4 Volume and output

Volume changes move towards the requested value by at most
$\mathtt{0x3FFF}/256+1 = 64$ per output sample, so full scale takes about
5 ms. (The chip itself applies volume writes at once; the slew avoids
zipper noise from frame-rate updates. The engine-note model also limits its
own steps, §6.2.) Volumes may be negative, which inverts the voice's phase
(§6.5).

Per output sample the mixer sums $\text{env-scaled sample}\cdot\text{vol}/2^{14}$
over each **bus**'s voices separately (and, separately again, over the
voices whose reverb send is on), scales each bus's sums by its gain (0..1,
default 1; the Music volume for the music bus, the Sound effects volume for
the effects bus, §1.1) and adds the buses. The scaled reverb sends feed the
reverb (§3), so the reverb return follows the bus gains too. The mixer then
adds the reverb return, multiplies by the **output trim** 0.5
(`VoiceMixer::kOutputTrim`) and clamps to 16 bits. A bus gain change applies
from the next block the mixer renders.

The trim gives headroom: full-level voices add up past full scale as on the
PS2. The streamed music takes the same trim in its output gain
([music.md §6](music.md#6-volume-and-options)), so a full-level stream and a
full-level voice come out equally loud.

### §2.5 Tick hook

The mixer can call a hook every $N$ output samples, before rendering them,
so that changes the hook makes land exactly on that sample. The sequencer
uses it with $N = 48000/60 = 800$ (§4.1), so menu music keeps sample-exact
time whatever the audio buffer size.

## §3 Reverb

### §3.1 The reverb unit

`SpuReverb` models the chip's reverb. The voices whose reverb send is on are
summed (after their volumes), resampled to 24 kHz with a 39-tap half-band
filter, run through the chip's reverb network in a ring buffer
(same-side and cross-side reflections, four comb taps, two all-pass
filters), the left and right sides on alternate samples, resampled back to
48 kHz and scaled by the return volume **EVOL** (0x7FFF = 1.0). The result
is added to the dry mix (§2.4).

The register sets are the standard presets of the console's sound library:

| Mode | Name | Work area (bytes) |
|---|---|---|
| 0 | off | — |
| 1 | room | 0x26C0 |
| 2 | studio A | 0x1F40 |
| 3 | studio B | 0x4840 |
| 4 | **studio C** | 0x6FE0 |
| 5 | hall | 0xADE0 |
| 6 | space | 0xF6C0 |
| 7 | echo | 0x18040 |
| 8 | delay | 0x18040 |
| 9 | pipe | 0x3C00 |

At start-up the engine selects **studio C with EVOL 0x0FFF** (about 0.125)
on both sides, the game's boot setting. Selecting a mode clears the
buffer. Which voices are sent: every engine-note voice (§6.2), and every
effect or instrument region whose flag bit 0 is set (179 of the 214 music
regions; 26 of the 50 effect regions).

### §3.2 Race reverb ambience

During a race the reverb depth follows the road under the followed car
(`race::ReverbAmbience`). Every physics step (60 Hz), with the surface flags $f$ under the
car (the road triangle's flags byte, see `docs/dev/TRACK.md`):

| $f \mathbin{\&} \mathtt{0x18}$ | Target depth | Where |
|---|---|---|
| 0x08 | 0.5 | tunnels and covered sections |
| 0x10 or 0x18 | 0.25 | |
| otherwise | 0.125 | the open road (the boot setting) |

The depth moves towards the target by at most **1/36 per step**, and
$\text{EVOL} = \mathrm{round}(\text{depth}\cdot 32767)$. Entering a tunnel
therefore swells the reverb from 0.125 to 0.5 in 14 steps (about a quarter
of a second). Leaving the race or its results puts the depth back to
0x0FFF.

## §4 Sequencer

`Sequencer` (`src/audio/sequencer.*`) plays the menu music: the sequences of
`music/music.seq` with the programs (instruments) of `music/music.ins`, on
the voice mixer.

### §4.1 Clock

The sequencer is updated **60 times per second** (one game frame). A
sequence's **tempo word** $T$ is in microseconds per 480 ticks. An
accumulator gains $480\,000\,000/60 = 8\,000\,000$ per update; each update,
while the accumulator is positive, the update's tick count is

$$n = \left\lceil \frac{\text{acc}}{T}\right\rceil,\qquad \text{acc} \leftarrow \text{acc} - nT,$$

and then the gain is added again. On average an update runs $8\,000\,000/T$
ticks, i.e. $480\,000\,000/T$ ticks per second. The music is written with a
beat of 120 ticks, so the tempo is $240\,000\,000/T$ beats per minute
(for example sequence 0 at about 127 BPM). The accumulator starts full, so
the first update already runs ticks.

Each update: all 16 tracks run their due events (§4.3), then every sounding
note is updated (§4.4).

In the app the update comes from the mixer's tick hook (§2.5); offline
renders (`render_sequence`, `gt3tool seq-wav`) call update and then mix 800
samples, which gives the same result.

### §4.2 Starting and stopping

`play` stops every note of the previous sequence (release) and starts a
sequence from the top: each track reads its first delta time; channel
volumes start at full, pans at 0, pitch bend at 0, program 0. `stop`
releases every note and clears the tracks. The sequence's own volume (Q14,
0x4000 in all nine) times the **master volume** (set from each music
request's level, [music.md §6](music.md#6-volume-and-options)) gives the
sequence volume. The sequencer's voices play on the mixer's music bus
(§2.4).

### §4.3 Tracks and events

A track waits its delta time (in ticks), then runs events until one has a
non-zero delta. Events:

| Event | Effect |
|---|---|
| note | starts a note (§4.4) with its key, velocity and length |
| pitch bend | sets the channel's bend, −8192..8191; sounding notes follow it |
| program | selects the channel's program |
| volume $v$ | channel volume $c(v)$, sounding notes follow it |
| pan | channel pan 0..127; sounding notes follow it |
| tempo | replaces the tempo word |
| loop start ($k$) | pushes a loop with count $k$ (up to 4 nested; deeper ones are ignored) |
| loop end | count 0: the loop is popped and play continues; otherwise the count is decreased (unless it is 255, "endless") and play jumps back to just after the loop start |
| end | the track stops |

$c(v) = \mathrm{round}(\min(v,127)\cdot 16384/127)$ is the linear 0..127 →
Q14 curve, used for channel volume and velocity. Every track on the disc ends
with an endless loop: the part before its loop start plays once (up to about
16 s; music §1.4), then the loop repeats; the first track to pass through
its endless loop end counts the **loops completed** (used for renders and
the reported length). A track that runs 65,536 events without any time
passing is stopped and logged, as a guard against broken data.

### §4.4 Notes

A note on a channel plays **one voice per region** of the channel's program
whose key range contains the key (so a program can layer several samples).
Up to **47 note slots** are shared by all channels; a new note takes the
next free slot after the last one used (a slot frees when its voice has
ended or was stolen). With no free slot the region is not played.

For a region with center $C$, bend ranges $R_\downarrow$, $R_\uparrow$
(semitones) and channel bend $b$:

$$\text{pitch} = \mathrm{note\_to\_pitch}\!\left(256\cdot\text{key} + \frac{b\cdot R}{32},\ C\right),\quad R = R_\downarrow \text{ if } b<0 \text{ else } R_\uparrow$$

so a full bend is $R$ semitones. The note's volume, with all factors Q14:

$$v = \text{seq vol}\cdot c(\text{chan vol})\cdot \text{prog vol}\cdot c(\text{velocity})\cdot \text{region vol},\qquad v' = v^2$$

The **square** gives the volume curve its shape (and means the master
volume acts squared). $v'$ is split with the pan law (§1.3) at pan
$\mathrm{clamp}(\text{chan pan} + \text{prog pan} + \text{region pan} - 128,\ 0,\ 127)$
and each side is capped at 0x3FFF. A channel that never sets its pan stays
at 0, so with neutral program and region pans (64 each) it plays hard left.
The voice uses the region's envelope words and reverb flag.

There are no note-off events: a note has a **length**. With $e$ the update's
ticks and $w \le 0$ the overshoot of the note event within the update, the
note's counter starts at $L + 2e + w$ and every update, including the one it
starts in, subtracts that update's ticks; at zero or below the note is keyed
off and its envelope releases. While a note sounds, changes to its channel's
bend, volume or pan are applied to it every update.

### §4.5 Measured

`sound_seq_on_disc` renders sequence 5 through one pass of its loop:
43.640 s expected from the tempo, 43.633 s rendered (within one update),
peak 22885, at most 32 voices sounding. `docs/formats/sound.md`
("Verification") records the renders of all nine sequences: no clipping, no
clicks, pitches on the A440 grid within 3 cents.

## §5 Sound effects

### §5.1 Named effects

`sound/se.inf` names 42 effects, each a bank and a range of regions:
bank 0 `sound/gt3sys.ins` (menu sounds: `cursor`, `select`, `next`,
`cancel`, `ok`, `disable`, `buy`, ...), bank 1 `sound/gt3se01.ins` (race
effects: `clash*`, `count_d`, `count_s`, `on_skill`, `off_skill`, `rumble`,
`turbine`, `turbine2`, `in_car`, `b_off`, `rain_noiz`, ...), bank 2
`sound/gt3se02.ins` (ambience: `air`, `city`, `crowds`, `sea`, `tunnel`,
`tunnel2`). `gt3tool se` lists them. An effect's pitch is its region's
pitch at the region's lowest key (§1.3), times any factor the caller gives.

### §5.2 Menu sounds

`menu::flow::MenuSounds::play` plays a menu sound at volume 1.0:

- if the effect is a single region, the region is keyed **twice**, at pan 0
  and at pan 127 (hard left and hard right), so it sounds centered at the
  region's full level on each side;
- if it is a range, only its **first** region is keyed, at pan 64.

The volume is the region's volume (capped at 0x3FFF) through the pan law.
Earlier instances are not stopped, so fast cursor moves overlap. With the
options' **Menu SE** off (`[options] menu_se`), menu sounds are not played.

### §5.3 Race effects

`race::RaceSfx` plays effects in races in two ways:

- **One-shot** (`play`): every region of the effect's range is keyed (so a
  stereo pair plays both halves). Per region, pan = region pan $+ 63\cdot$pan
  (pan −1..1, clamped to 0..127); volume = region volume through the pan
  law, times the requested volume clamped to 0..4, each side capped at
  0x3FFF; pitch = region pitch times the factor, clamped to 1..16383.
  Requests at volume 0.01 or below are ignored.
- **Held loop** (`hold`, by slot name): keyed on when its volume rises above
  0.01, its volume, pitch and pan updated every frame while it plays, keyed
  off when the volume drops to 0.01 or below; if its voice was lost (stolen)
  it is keyed on again.

### §5.4 Looping effect voice

`LoopEffect` drives a single looping region the same way, with per-side
gains $g$: side volume $= \mathrm{clamp}\big(\lfloor g\cdot 16384\rfloor\cdot\text{region vol}/2^{14},\ -\mathtt{0x4000},\ \mathtt{0x3FFF}\big)$,
so a gain above 1 is louder than the region's own volume up to the 14-bit
cap, and a negative gain inverts that side's phase (the surround cue,
§6.5); pitch = region pitch $\times$ factor. It keys on when either side
becomes non-zero, updates while playing, keys off when both sides are 0, and
passes a stealing priority (§2.1), which it re-sends with every update while
the voice plays. Every car's effect slots (its tire loops, §7.6, and its
whine, spool and in-car loops, §7.3-§7.5) and the course ambience (§9) use
it; the slots are placed by §7.6's placement, so a faint loop still plays
(no 0.01 floor, unlike the held loops of §5.3).

## §6 Engine sound model

`EngineSound` (`src/audio/engine_sound.*`) plays a car's engine. Measured
behavior: `sound_engine_model`; render with
`gt3tool engine-wav <iso> <sound-id> <variant> out.wav [view]`.

### §6.1 Banks

A car plays **two** `ENGN` banks at once:

| Bank | Path | Character |
|---|---|---|
| engine | `sound/engine/<id>` | mechanical / intake |
| exhaust | `sound/<variant>/<id>` | exhaust; `normal0..3` or `turbo0..3` |

`<id>` is the car's engine sound id written with five digits (`%05d`): the
16-bit value at +0x16 of its engine part record. The variant is the byte at
+0x10 of its muffler part record (0..3), plus 4 when its turbo kit record
has a non-zero byte at +0x11 (`CarDatabase::engine_sound`). For example the
R34 plays id 21609 with `turbo0`. A car may lack one of the two banks; it
then plays the other alone. A mod car with an engine names its id with
`sound_id` (`docs/MODDING.md`).

Each bank holds 3..9 **layers**, each a looped recording at a base rpm with
an rpm range $[lo, hi]$, a volume and the pitch that reproduces the base rpm.

### §6.2 Layer player

Each bank has a **layer player** with two voices, updated once per game
frame (60 Hz; `EngineSound::update` runs up to four 60 Hz ticks to catch up
after a slow frame and drops any further backlog). Per tick, with the rpm
clamped to at least 200:

1. **Layer choice.** The first layer with $lo \le \text{rpm} \le hi$ plays
   alone. Otherwise the rpm lies in a gap $hi_{i-1} < \text{rpm} < lo_i$ and
   layers $i-1$ and $i$ both play.
2. **Crossfade.** In a gap, the upper layer's position is
   $x = 2^{14}(\text{rpm} - hi_{i-1})/(lo_i - hi_{i-1})$ and the lower
   layer's is the mirror, $2^{14}(lo_i - \text{rpm})/(lo_i - hi_{i-1})$; a
   layer playing alone has $x = 2^{14}$. The gain is
   $4096\sin(\tfrac{x}{2^{14}}\cdot\tfrac{\pi}{2})$ (an equal-power
   crossfade, 0.707 each in the middle of the gap).
3. **Pitch.** $p = \text{layer pitch}\cdot\text{rpm}/\text{base rpm}$, then
   times the Doppler factor ($p \cdot \lfloor 4096 D\rfloor / 4096$, §8),
   at least 1. For a voice that keeps playing, the new pitch is limited to
   between half and twice its previous value per tick. The pitch is capped
   at 0x3FFF.
4. **Volume.** Per side,
   $\text{vol} = \frac{\text{gain}\cdot \mathtt{0x3FFF}}{4096}\cdot\frac{\text{layer vol}}{2^{14}}\cdot\frac{\text{bank side}}{2^{14}}$,
   clamped to −0x4000..0x3FFF; for a voice that keeps playing, it may change
   by at most 0x1000 per tick.
5. **Voices.** A voice already playing a wanted layer is kept; the others
   are keyed off. A new layer is keyed on at its sample start with envelope
   `0x000F` / `0x0003` and the **reverb send on**.

So the sound changes layer without gaps: a new layer fades in from the edge
of its range while the old one fades out.

### §6.3 Bank levels

Each car has two levels, one per bank, in 0..127 units. Each tick, with
$t = \min(\text{throttle}, 1)$ (the car's own throttle after the race step's
aids and shift cut, `VehicleState::throttle`, the same value as the turbo's
processed throttle of §7.5):

$$\text{exhaust target} = \min\!\big(127,\ \lfloor (1+3t)\cdot 32\cdot K_1\rfloor\big)$$
$$\text{engine target} = \min\!\big(127,\ \lfloor (|t-0.375|+0.375)\cdot 127\cdot k\cdot K_2\rfloor\big)$$

with $K_1 = K_2 = 0.13$ and $k = 1$ ($k = 0.5$ while "drive engaged" is
false; the race always passes true). Each level then moves halfway to its
target, in integers: $\ell \leftarrow \lfloor(\ell + \text{target})/2\rfloor$.
Because of the rounding a rising level settles one below its target:

| Throttle | Engine target / settled | Exhaust target / settled |
|---|---|---|
| 0 | 12 / 11 | 4 / 3 |
| 0.375 | 6 / 6 (from above) | 8 / 7 |
| 1 | 16 / 15 | 16 / 15 |

(Measured with a scratch run of `EngineSound`.) So the exhaust grows about
four-fold with the throttle, while the engine bank is loudest when coasting
or at full throttle and dips at part throttle.

A bank's gain is $\ell/64 \cdot G_{view,bank}\cdot \text{volume}$ (volume is
the distance attenuation $\min(1,\ 32/d)$ of §6.5, for every car, the
followed one too), placed in stereo by §6.5 and converted to 14-bit side
volumes (each side capped at 0x3FFF).

### §6.4 View gains

The camera decides how loud each bank is (`EngineSound::View`). Every car
has one view for all its sounds (engine, exhaust, wind, the extra loops of
§7, the tires), from the camera's **kind** and whether the car is the
camera's **target**, the car it follows (`race::sound_view`,
`CameraRig::sound_kind`):

| Camera kind | Cameras |
|---|---|
| car-mounted | the in-car views; in replays every car mount, inside or outside the car (view mode 1's mounts: in-car, bumpers, the driver's head, mirrors, roof, sills, outside points), and mode 2's on-board cut |
| chase | the chase views (also the driver's chase view in a replay) |
| other | every other camera: the TV camera, the replay's course shots and its other procedural cameras, the pre-race shot |

The target is the player's car in live play (in a 2P battle, each half's
own car from its own camera). In a replay or attract run on a field it is
the followed car, and no car at all while the cameras aim at a pair of cars
(a battle, presentation §G3).

| View | Used for | Engine | Exhaust |
|---|---|---|---|
| 0 onboard (own) | the target, car-mounted camera | 3.0 | 2.5 |
| 1 onboard (other) | other cars, car-mounted camera | 0.3 | 1.3 |
| 2 chase (own) | the target, chase camera | 1.0 | 3.0 |
| 3 chase (other) | other cars, chase camera | 0.3 | 1.3 |
| 4 replay / TV | every car, any other camera | 0.5 | 3.3 |

In the cockpit the mechanical note dominates; outside, the exhaust does. So
a replay's car-mount shots play the followed car's cockpit mix, turbo and
whine included (§7.1), and its trackside shots the TV mix.

### §6.5 Stereo placement and surround cue

A source at $(x, z)$ in the listener's frame (meters, $x$ to the right, $z$
forward), with $d = \max(\sqrt{x^2+z^2},\ 0.25)$:

$$\alpha = \mathrm{clamp}\!\left(\left(\tfrac{x}{d}+1\right)\tfrac{\pi}{4},\ 0,\ \tfrac{\pi}{2}\right),\qquad
\text{left} = g\cos\alpha,\quad \text{right} = g\sin\alpha$$

a constant-power pan, 0.7071 each at the center, and the distance
attenuation $\min(1,\ 32/d)$: full volume within 32 m, half at 64 m. Every
car's units and loops are placed and attenuated this way, the target's too,
so a car passing a trackside camera grows louder as it comes in and fades
as it leaves. In a car-mounted view the target's units sit at the camera
itself ($(0, 0)$: centered, full volume, and behind for the surround cue);
otherwise a car's offset from the camera is taken along the camera's right
and forward axes. The one-shots (blow-off, collisions) are placed at the
car's own offset in every view.

**Surround cue** (optional, off by default; Settings → Audio, `[audio]
surround_cue`): two per-source flags invert the left or right volume. In
front ($z > 0$) the flags are made equal ($x < 0$: right takes left's flag;
$x \ge 0$: left takes right's); behind they are made opposite ($x < 0$:
right = not left; $x \ge 0$: left = not right). A source behind the listener
then has its channels out of phase, which a matrix surround decoder steers
to the rear (`SurroundCue`; on plain stereo it smears the image, hence off
by default). The engine's flags follow its engine bank and apply to both
banks; the wind unit (§7.2) and each tire voice (§7.6) have their own.

### §6.6 Electric motor sound (mod cars)

Electric mod cars (`docs/MODDING.md`) do not use a disc engine bank; the engine
synthesizes two banks in the same layout and plays them through
`EngineSound` unchanged, so rpm tracking, crossfades, Doppler, pan and the
throttle-driven levels all apply (`make_motor_banks`, `src/audio/motor_sound.*`).
This is a conventional model of our own, not a sound from the game; render
it with `gt3tool engine-wav <iso> ev[:order] ...`.

| | Engine bank: motor whine | Exhaust bank: inverter |
|---|---|---|
| tone | $f_0 = \text{order}\cdot 0.15\cdot\text{rpm}$ Hz | twice the whine order |
| partials (multiple: level) | 1: 1.0, 2: 0.35, 3: 0.12, 0.5: 0.2 | 1: 0.6, 1.5: 0.25, 4: 0.15 |
| noise | none | 0.5 of band-passed white noise (resonator at $\min(3f_0, 6000)$ Hz) |
| layer volume | $9000\cdot$ whine level | $6500\cdot$ inverter level |

Layers: base 1500 rpm (0..2500), 5000 (3500..7000), 12000 (9000..25500),
each a 0.4 s loop (630 frames) at pitch 0x1000, partials rounded to whole
cycles per loop so it loops seamlessly, partials above 8820 Hz dropped, and
normalized to a peak of 26000. Parameters from the car's `[sound]` section:
whine order (clamped 0.25..4), whine level and inverter level (nominally
0..2; the layer volume is capped at 16383).
Because the exhaust bank follows the throttle (§6.3), the inverter swells
under acceleration and fades on lift-off. Only the followed cars play it: an
electric mod car in the field has no engine note (§8).

## §7 Other per-car race sounds

Besides the engine, **every** car has its tire loops (`race::CarSfx`,
§7.6) and a set of extra sounds (`race::CarExtraSounds`, models in
`src/audio/car_sound_model.*`): the wind unit, the in-car loop, the
transmission whine, the turbo spool and the blow-off. The channel gains
(§7.1) leave most of them silent for other cars: the target plays them all,
other cars only their wind (the nearest two, §8) and, on the TV cameras, a
gearbox whine (the nearest five). The followed cars (player 1, and player 2
in a 2P battle) also have collision effects (§7.7).

### §7.1 Channel gains

Every race sound is scaled by a gain for its channel and the car's view
(`race::sfx_gain`; the views of §6.4): 0 car-mounted camera, target; 1
car-mounted, other car; 2 chase, target; 3 chase, other car; 4 any other
camera.

| Channel | 0 | 1 | 2 | 3 | 4 |
|---|---|---|---|---|---|
| engine | 3.0 | 0.3 | 1.0 | 0.3 | 0.5 |
| exhaust | 2.5 | 1.3 | 3.0 | 1.3 | 3.3 |
| turbo spool | 0.5 | 0 | 0 | 0 | 0 |
| blow-off | 0.2 | 0 | 0 | 0 | 0 |
| whine | 0.8 | 0 | 0.3 | 0 | 0.2 |
| clash | 0.3 | 0.3 | 0.5 | 0 | 0.5 |
| curb rumble | 1.4 | 0.2 | 1.0 | 0.2 | 0.2 |
| in-car | 0.6 | 0 | 0 | 0 | 0 |
| wind | 0 | 0.3 | 1.4 | 0.3 | 0.8 |
| road roar | 0.9 | 0.3 | 0.6 | 0.3 | 0.9 |
| rain | 0.9 | 0.6 | 0.9 | 0.6 | 1.0 |
| squeal (tarmac) | 0.6 | 0.3 | 0.6 | 0.3 | 0.5 |
| (unused) | 1.0 | 1.0 | 1.2 | 0.6 | 1.0 |
| slide (loose) | 0.8 | 0.6 | 0.6 | 0.6 | 0.3 |
| dirt | 1.4 | 1.0 | 1.0 | 1.0 | 1.0 |

The engine and exhaust columns are the view gains of §6.4. Note the zeros:
the turbo, blow-off and in-car sounds are heard only from the target's
car-mounted views (your cockpit; in a replay every car-mount shot of the
followed car), and there is no wind noise there.

### §7.2 Wind and road noise

A third engine-style unit per car plays the shared bank `sound/rn_01.es`
(three layers recorded at 2500, 5000 and 7500 "rpm") through the same layer
player (`EngineUnit`). With $u$ the car's speed (m/s), $y$ the filtered
slipstream factor (0..1, drafting quietens the wind) and $C_d$ the car's
drag coefficient (byte +0x39 of its body record / 100, e.g. 0.35; default
0.35):

$$\text{rpm} = \mathrm{ramp}(|u|;\ 10\text{ km/h} \mapsto 2500,\ 450\text{ km/h} \mapsto 7500)$$
$$\text{gain} = \min(0.001\cdot C_d\cdot u^2\,(1-y),\ 0.5)$$

where ramp is linear between the two points and held beyond them. The unit
plays at gain $\times$ the wind channel gain (§7.1) $\times$ the distance
attenuation (§6.5), placed like the engine, its pitch times the car's
Doppler factor (§8).

### §7.3 In-car loop

The `in_car` loop: gain $\min(0.004\cdot C_d\cdot u^2(1-y),\ 2.0)$, pitch
factor $\mathrm{ramp}(|u|;\ 10\text{ km/h}\mapsto 0.9,\ 320\text{ km/h}\mapsto 1.5)$
times the Doppler factor. Its channel gain is non-zero only in the target's
car-mounted views.

### §7.4 Transmission whine

The `turbine2` loop, only for cars with a **non-stock gearbox** (byte +0x10
of the gear part record non-zero; for GT-mode garage cars, the fitted racing
gearbox) and only while the clutch is not open (locked, slipping or
shifting; physics §2.4). With $\tau$ the torque through the clutch (N m, its
size), $r_g$ the current gear's ratio (the reverse ratio's size in reverse),
$r_1$ first gear's ratio, and $\omega$ the driven axle's wheel speed (rad/s:
front for FF, the four-wheel mean for 4WD, rear otherwise):

$$\text{gain} = 0.3 + 0.7\cdot\frac{\min(|\tau|, 490)\cdot r_g}{490\cdot r_1},\qquad
\text{pitch} = 0.3 + 3.2\cdot\frac{|\omega|}{1047.2}$$

(490 N m is 50 kgf m; 1047.2 rad/s is 10,000 rpm.) The pitch is times the
Doppler factor. The whine, the spool and the in-car loop are the car's first
three effect slots: each plays its effect's first region as a looping effect
(§5.4), placed and attenuated like a tire voice (§7.6, at the car's units'
position, §6.5).

### §7.5 Turbo spool and blow-off

For turbo cars (`CarFacts::turbo`: the engine has boost), `car_sound::Turbo`
runs after every race step with the car's own turbo state from the race's
telemetry (`CarExtraSounds::step_inputs`): the **processed throttle**
(`VehicleTelemetry::throttle_processed`: after the pedal shaping, traction
control and the shift cut, so 0 through an upshift; not the pad's), the
turbo load of the previous step $\lambda$ (`VehicleTelemetry::turbo_load`),
the torque through the clutch $\tau$ (its size) and the two stages' lag
states (`turbo_lag`; physics §2.3):

- **Spool level.** While $\lambda > 0.75$ and $\tau > 0$: level
  $\mathrm{clamp}(\lfloor 128 v^2\rfloor, 1, 127)$ with
  $v = (\lambda - 0.75)\cdot\min(\tau/588, 1)$; otherwise level 1. Right
  after every step the level is scaled by 0.2 (truncated), so a level below
  5 reads 0: the spool is silent unless the load is above 0.75 and the
  engine pulls. The scale runs once per step (GT3's 60 Hz frame), whatever
  the display rate; the sounds read the level once per displayed frame. The
  `turbine` loop plays at gain level/128 and pitch factor
  $\max(\text{lag}_0, \text{lag}_1)/6144$ (no Doppler).
- **Blow-off.** Armed at start and whenever the throttle is above 90 %;
  when an armed step sees the throttle under 75 % it fires once with
  strength $\max(\lambda - 0.8, 0)/256$ and disarms. The strongest firing
  since the last displayed frame is kept and played once on the next one,
  whatever the display rate: the `b_off` one-shot at volume channel gain
  $\times\ 2.5\cdot$strength $\times\ 32/d$, panned from the car ($d$ the
  car's own horizontal distance from the camera in its frame, at least
  0.25 m, also in the cockpit; this $32/d$ is not capped at 1).

Because the processed throttle drops to 0 in a shift, every upshift made
with the load above 0.8 fires the blow-off, even with the pedal held flat
(`sound_turbo_blow_off_on_upshift`: the R34 `r34_gtr_v_99` at full
throttle on the machine test's straight makes 3 upshifts and 3 blow-offs, up
to about 0.056 of the region's level in the cockpit with the car 1 m from
the eye, above the 0.01 threshold of §5.3; the pad's throttle held at 1
fires none).

### §7.6 Tires

Every car (the followed cars and every other car of the field) runs its
tire loops after every race step (`RaceScreen::update_tyre_sounds`, which
also plays the followed cars' collision one-shots, §7.7). Each
**wheel** has its own voice for each of six loops (`race::TyreLoop`), played
on the loop's effect's first region as a looping effect (§5.4); a loop keys
on only while it is audible. The surface under the wheel (the material,
remapped on wet triangles: materials 0 and 7 become 12, 1 becomes 13, 9
becomes 14; `sim::surface_index`, names from `docs/dev/TRACK.md`)
selects the loops that sound:

| Surface | Loops |
|---|---|
| 0 tarmac, 7 paving, 8 loose, 9 rough | squeal `on_skill` and road roar `on_road` |
| 1 curb, 13 wet curb | curb rumble `rumble` |
| 2 grass, 3 gravel, 4 sand | slide `off_skill` |
| 5 dirt | `off_roadnoiz` |
| 6, 12, 14 (wet) | `rain_noiz` |

The wheel's other loops are silent, and no loop sounds on the other
surfaces.

**Slip loops** (squeal, curb rumble, slide, dirt, rain): volume = the
loop's channel gain (§7.1) $\times$ the wheel's **slip intensity** $S$, not
clamped (a big slide goes past 1); pitch = Doppler $\times$ the wheel's
**skid pitch** $P$. $S$ and $P$ are the race step's own per-wheel values
from the race's telemetry (`VehicleTelemetry::smoke`, `skid_pitch`); their
curves by surface class, with the curbs' load and rim-speed terms, are in
physics §8.3 and §8.3c. A wheel that leaves the ground keeps $S$ and $P$
following its motion for 5 steps; from its 6th step in the air $S$ is 0 and
$P$ is 1. A car without race telemetry plays no tire loops
(`sound_car_sfx_per_wheel_voices`).

**Road roar** (`race::road_noise`) sounds on surfaces 0, 7, 8 and 9 while
the wheel is on the ground and for 5 steps after it leaves it. With the rim
speed $w = |\omega|\cdot\text{radius}$ (m/s) and the tire width $W$ (m):

$$\text{volume} = \min\!\big(0.3,\ 2.55\,(w/100)^2\cdot\mathrm{ramp}(W;\ 0.135\mapsto 0.5,\ 0.355\mapsto 1.0)\big)$$
$$\text{pitch} = \mathrm{ramp}(w;\ 8.333\mapsto 0.5,\ 41.667\mapsto 1.0)\quad\text{(30 to 150 km/h)}$$

(ramp as in §7.2). The loop plays at its channel gain $\times$ the volume,
pitch Doppler $\times$ the pitch.

**Placement** (`race::place_voice`). Each voice sits at its wheel's contact
point, taken in the camera's frame ($x$ along the camera's right axis, $z$
along its forward axis). With $d = \sqrt{x^2+z^2}$ (a source nearer than
0.25 m is first moved back along $z$ by $0.25 - d$) and $q$ the loop's
volume:

$$g = \min(1,\ 32/d)\cdot q,\qquad
\alpha = \mathrm{clamp}\!\left(\left(\tfrac{x}{d}+1\right)\tfrac{\pi}{4},\ 0,\ \tfrac{\pi}{2}\right),\qquad
\text{left} = g\cos\alpha,\quad \text{right} = g\sin\alpha$$

so a wheel is heard at full volume within 32 m and at half at 64 m, from its
own side. Its stealing priority (§2.1) is $\lfloor 255\,(1-|g|)\rfloor$, and 0
when $|g| \ge 1$: the louder the loop, the fewer voices may take it. With the
surround cue on (§6.5) each voice has its own pair of flags.

**Views and Doppler.** Each car's wheels use the car's view (§6.4; player 2
from its own camera). The Doppler factor is that of §8, with the listener
moving with the camera: a car riding along with the camera (in the cockpit,
chased) plays at about 1, a car passing a still trackside camera rises and
falls in pitch. Player 2's wheels have no Doppler.

### §7.7 Collisions

Only the followed cars' collisions are heard (not other cars' contacts with
each other), and player 1's are not heard in replays. The inputs are the
race step's collision hits on the car (physics §5.3): for walls $j$ is the
sum of the step's wall impulses, for cars the largest car impulse (N s).
Each race step, the stronger of the two decides:

- wall: strength $\min(|j|/50000, 1)$, effect `clash`;
- car: strength $\min(|j|/7500, 1)$, effect `clash2` (car wins a tie);
- strengths below 0.2 are ignored.

The one-shot plays centered at volume
$\min(1,\ G_{clash}\cdot\text{strength}\cdot 0.6\cdot 32/\max(d, 0.25))$, with
$d$ the car's horizontal distance from the camera in its frame. It is keyed again on every step with such
a hit; there is no cooldown. A wall hit becomes audible from
$|j| = 10\,000$ N s. In the cockpit $d$ is
about a meter, so any audible hit plays at full volume. The `clash3`
(objects) effect is not played.

### §7.8 Countdown

At a standing start `count_d` plays at 3, 2 and 1 seconds before the signal
and `count_s` at the signal; at a rolling start only `count_s`, at the
hand-over to the player; at a flying start (time trials, flying license
tests, free runs) only `count_s`, when the car crosses the line that ends
the computer's run-up. Not in replays (nor, for a flying start, in demos
and attract runs). The countdown plays even when the
race SE option is off.

## §8 Other cars' engines

`race::OpponentEngines` gives the other cars their engine notes. Every frame:

- the cars are sorted by distance from the camera; the **nearest 5** get an
  engine note (4 voices each, which keeps the total inside 46 voices), and
  the nearest 2 of those also their wind unit (§7.2, at their view's wind
  gain); notes of cars that drop out of the set are keyed off;
- **attenuation** $\min(1,\ 32/\max(d_h, 0.25))$ with $d_h$ the horizontal
  distance to the camera in its frame (along its right and forward axes):
  full volume within 32 m, half at 64 m;
- **Doppler** along the unit direction from the listener to the car, with
  the source and listener velocity components clamped to $\pm c/2$:

  $$D = \mathrm{clamp}\!\left(\frac{c + v_{lis}}{c + v_{src}},\ 0.25,\ 3.99\right),\qquad c = 331.45\text{ m/s}$$

  The listener moves with the camera: $v_{lis}$ is the eye's velocity, its
  displacement over the last frame (GT3 takes the camera's and the car's
  displacements over the frame; `CameraRig::eye_velocity`), and 0 on the
  frame of a cut, where the eye jumps. A car closing at 30 m/s on a still
  camera is raised by about 10 % (1.0995); passing a trackside camera at
  180 km/h it drops by about 5 semitones (1.178 coming in, 0.869 going
  away). The followed car's engine, wind and loops take the same factor
  (about 1 while the camera rides with it);
- each car's view (§6.4): in a field replay the followed car is the target
  among them; in a car-mounted view the target's units sit at the camera;
- placement and surround cue as §6.5, from the car's offset along the
  camera's axes; rpm and throttle are the car's own.

Other cars have their tire loops (§7.6, every car, not only the nearest
five) and their extra sounds (§7; `RaceScreen::update_car_sounds`): their
turbo model runs after every race step, and the camera's target among them
plays all of them, the nearest five otherwise only a gearbox whine on the
other cameras (channel gain 0.2; the other-car views' turbo, blow-off and
in-car gains are 0). Their collisions are not heard. Online, remote
players' cars are field cars and are heard the same way
(`docs/dev/ONLINE.md`).

## §9 Course ambience

Each course file may carry a `CRSS` section (75 of the 80 US course files;
not the four `L_circle*` license layouts nor `L_slalom80r4`), a set of **emitters** on five channels
(`CourseAmbience`, `src/audio/course_ambience.*`):

| Channel | Loop (`gt3se02.ins`) |
|---|---|
| 0 | `air` |
| 1 | `city` |
| 2 | unused |
| 3 | `crowds` |
| 4 | `sea` |

An emitter is a list of points (position and weight) and two **gate**
points. Emitters whose two gates are the same point (in practice air, city
and sea) are **ungated**; the others (crowds along a stretch of track) are
gated. `sound_crss_on_disc` counts 253 emitters, 167 gated. A gated
emitter whose gates do not both lie on the road plays as ungated.

### §9.1 Emitter gains

At load, each gated emitter's gates are projected onto the road to two track
distances; the emitter's span is the **shorter arc** between them, which may
wrap over the start line. Every frame, each emitter's target gain is:

- ungated: 1;
- gated: $\min\!\big(1,\ \sum_{\text{cars } k \text{ on the span}} 0.8^{k}\big)$,
  with $k$ the car's index in the race (player first, then player 2, then
  the opponents in entry order).

The gain moves towards its target by at most **1/60 per frame**, so a crowd
fades in over a second as cars arrive and out as they leave. All emitters
start at 0, so the ambience also fades in over the first second.

### §9.2 Listening

There is one listener per screen: the camera's position and right axis, the
followed car's velocity, and the surface flags under the followed car. Per
listener and channel, each point of each emitter contributes, with $\hat u$
the unit direction from listener to point and $r$ its distance:

$$w = \text{weight}\cdot\text{gain}\cdot\begin{cases}1 & r < 32\text{ m}\\ 32/r & \text{otherwise}\end{cases}$$
$$p = \mathrm{clamp}\!\left(\tfrac{\hat u\cdot\hat r + 1}{2}, 0, 1\right),\quad
L \mathrel{+}= w\sin\!\big((1-p)\tfrac{\pi}{2}\big),\quad R \mathrel{+}= w\sin\!\big(p\tfrac{\pi}{2}\big)$$

and a Doppler offset $\big(c/(c - \hat u\cdot\vec v_{lis}) - 1\big)\,w$
($c$ = 331.45 m/s), averaged over the emitter's points and then over the
channel's emitters; the channel's pitch is 1 plus that offset. L and R are
clamped to ±1. With two listeners (split screen) their outputs add, both
sides then take the larger of L and R, and the pitch is 1.

### §9.3 Tunnels

Separately, the surface flags under each listener's car switch two stereo
loops on at full level: $f \mathbin{\&} \mathtt{0x18} = \mathtt{0x08}$ turns
on `tunnel` (regions 2/3), $= \mathtt{0x10}$ turns on `tunnel2` (regions 4/5).
0x18 turns on neither (it only raises the reverb, §3.2). The same flags drive
the reverb.

### §9.4 Playback

`CourseAmbiencePlayer` plays each channel as a looping effect (§5.4) on the
effect's first region, at L/R times the SE master volume (1.0) and the
channel's pitch, with stealing priority
$255\cdot\mathrm{clamp}(1 - \max(|L|, |R|), 0, 1)$: the louder the
ambience, the lower its priority value, the fewer voices may steal it. The
tunnel loops play their left region with the left gain and their right
region with the right gain, with the same priority rule.

## §10 When race sounds play

- **Race SE** (`[options] race_se`) switches the engines, tires, collisions
  and extra sounds of live runs; **Replay SE** (`replay_se`, also in REPLAY
  OPTIONS) does the same for replays, demos and attract runs. The countdown
  and the course ambience are not switched by them.
- The replay that runs behind a quick menu (the menu backdrop) is silent: no engines, tires, extra sounds or ambience, whatever
  the options.
- Opening the pause window or a quick menu, the end of a replay or demo,
  and leaving the race **silence** every race sound at once (`RaceScreen::silence`): engine
  notes, other cars, every car's tire loops, extra loops, one-shots,
  ambience. They key on again when the race updates resume.
- Engine notes, the wind, in-car, whine and turbo loops, the blow-off and
  the course ambience are computed once per displayed frame from the
  interpolated car state; every car's tire loops, the collision effects, the
  countdown, the reverb depth and the turbo model (its step and the spool
  level's scale) run with each physics step (60 Hz).

## §11 Approximations and open points

- **Throttle as the bank-level input.** The bank levels (§6.3) are driven by
  the throttle and "drive engaged" is always true; the input behaves like a
  throttle, but this is the engine's inference.
- **Other cars' engines** are voiced only for the nearest five (§8), with
  their wind for the nearest two and their whine for the five; with every
  car voiced the voice allocator would have to drop the quiet ones. Their
  tire loops play for every car and rely on the stealing priorities (§7.6)
  instead.
- `body_noiz` and `clash3` are not played.
- The camera kinds (§6.4) are GT3's camera types for its own cameras; ours
  map onto them by what they show (the free orbit and our fallback TV
  cameras count as other cameras).
- **The listener at a cut.** GT3 takes the camera's displacement over the
  frame; whether its previous position follows a cut is not known. Ours
  restarts the eye's velocity at every cut (0 for that frame), so a cut
  never bends the pitch.
- The mixer's volume slew (§2.4) and output trim are our own additions for
  clean output; the music stream takes the same trim (§2.4) to keep the
  balance between music and effects.
- Unknown: what the `rn_01.es` unit's rpm input is in every state (the
  engine uses the speed ramp of §7.2), and the sequencer's fade, which the
  engine does not reproduce.
