# Sequenced music and the SPU2 voice model

Behavioural spec of how GT3 plays its sequenced menu music and drives SPU2
voices, written from study of `CORE.GT3` (EE side) and the IOP modules
`PDISPU2.IRX` and `LIBSD.IRX` on the disc. File formats are in
[../formats/sound.md](../formats/sound.md). Implementation:
`src/audio/sequencer.*`, `src/audio/voice_mixer.*`, `src/audio/spu_reverb.*`.

Changelog: 2026-10-20, "Tracks": the music volume comes from each request's
level (music.md §1.1), not from an option. 2026-10-20 (tyre sounds): "Streamed
music and the output mix": the music streams play on two SPU2 voices at
level × 0x3FFF, so music and effects meet at equal weight [C, `PDISPU2.IRX`];
ours trims the stream like the voices (`VoiceMixer::kOutputTrim`,
`AudioEngine::stream_output_gain`).

## Where things run

The sequencer runs on the EE, in the game's sound thread, which is woken by
the vertical blank (60 Hz NTSC). Each wake-up it

1. advances every playing sequence (clock, track events, note-ons),
2. updates every sounding note (lengths, bend, volume, pan),
3. packs one 16-byte command per voice (key-on/off bits, pitch, volume,
   start address, ADSR) and sends the block to the IOP.

`PDISPU2.IRX` applies the block through libsd (`sceSdSetParam`,
`sceSdSetSwitch`, `sceSdSetAddr`). So events are quantised to 60 Hz updates
and everything the game does to a voice lands on an update boundary. Our
sequencer is driven by the mixer's tick hook every 800 output samples
(48000 / 60), which reproduces that quantisation sample-exactly; renders
can use a finer update rate.

## Clock

- A sequence has a tempo word `T` (microseconds per 480 ticks) and an
  accumulator that starts at `S = 480,000,000 / 60` (integer).
- Each update: if the accumulator is positive, subtract `T` until it is
  zero or negative, counting the subtractions as this update's elapsed
  ticks `E`; then add `S`. (So ticks per second are `480e6 / T` on
  average, and `E` varies between updates by one.)
- A tempo event replaces `T` immediately.

## Tracks (16 per sequence)

Track state: read position, wait counter, channel volume (curve value and
the product with the sequence volume), pitch bend, pan, program, a 4-deep
loop stack of (resume position, count).

At start: wait = the first delta; volume curve 0x4000 (full); volume =
sequence volume; bend 0; **pan 0** (hard left until a pan event; every
shipped track sets 0x40 first); program 0; loop stack empty.

Each update, for each track with a position: `wait -= E`; while `wait <= 0`
read an event, act, then read the delta that follows and `wait += delta`.
Event actions:

- note: start the note (below) with length `length + E + wait` (wait is
  the non-positive overshoot, so the note is timed from its exact tick).
- bend: set the channel bend.
- loop start: push (position after the event, count).
- loop end: if the top count is 0, pop and continue after the loop end;
  otherwise decrement it unless it is 0xFF and continue from the pushed
  position (its first byte is the delta after the loop start).
- program / pan: set.
- volume n: curve value `c = round(n * 16384 / 127)`; channel volume =
  `sequence_volume * c >> 14`.
- tempo: set `T`.
- end: the track stops (its sounding notes continue).
- bytes 0x07..0x1F: nothing (just read the next delta).

Sequence volume = `music_volume * record_volume >> 14`, where
`music_volume` is the global music level (0x3FFF at boot; every BGM request
for a sequence sets it as `min(int(level * 16384), 0x3FFF)` from the
request's level, 0x22B2F8: 13107 for the menus' 0.8, music.md §1.1; no
option sets it); changing it recomputes every channel volume, and sounding
notes pick that up on the next update. The SE master volume never scales
the sequencer's voices. Ours: the voices play on the mixer's music bus,
whose gain is our Music slider (the Sound effects slider is the effects
bus's gain).

## Notes

There are 47 note slots shared by everything the sequencer plays, used as a
ring: the search starts after the slot taken last and takes the first free
one; if none is free the note layer is dropped. A slot is free when it has
no voice (the voice ended, or was taken by another sound).

Note-on (key `k`, velocity `v`, length `L`) with the channel's current
program `P` (an INST program: pan, volume, region list):

- `a = channel_volume * P.volume >> 14`, then `a = a * curve(v) >> 14`
  (`curve` as for channel volume).
- For every region `R` of `P` whose key range contains `k` (all of them
  sound, one voice each):
  - slot := k, v, channel, program, region, current bend, the channel
    volume and pan as seen now, remaining = `L + E` (see "length" below);
  - pitch: `note = k << 8`; if the bend `b` is non-zero,
    `note += b * range >> 5` with range = R.bend_down for `b < 0`,
    R.bend_up for `b > 0` (8192 * range / 32 = range semitones);
    pitch = note-to-pitch(note, R.center) (formats doc);
  - volume: `x = a * R.volume >> 14`, then **squared**: `x = x * x >> 14`;
    pan `p = clamp(channel_pan + P.pan + R.pan - 0x80, 0, 127)`;
    left = `x * sin_table[127 - p] >> 14`, right = `x * sin_table[p] >> 14`
    (`sin_table[i] = round(16384 * sin(i / 127 * 90deg))`);
  - key a voice with the region's sample address, ADSR1/ADSR2, priority
    (+0x0C), reverb send (flags bit 0).

Note update, every update after all tracks ran, for each slot with a voice:

- if the sequence was stopped: key the voice off and free the slot;
- if remaining > 0: `remaining -= E`; when it reaches <= 0, key off
  (release phase; the slot stays until the voice has finished);
- if the channel bend differs from the slot's: recompute and set the pitch;
- if the channel volume or pan differs from the slot's snapshot: recompute
  the volume (same formula, current channel values) and set it.

Length: the note-on stores `length + E + wait` and adds `E` again when it
fills the slot, and the note update of the same frame subtracts `E` once, so
a note is keyed off one update later than its nominal length (16.7 ms at
60 Hz). This is the game's behaviour and is reproduced.

## Voices

The game's pool has 46 voices (the SPU2 has 48). Allocation: the first free
voice; if none, steal an active voice whose priority value is >= the
request's, preferring the highest value and then the oldest; voices with a
lower value are never taken. Every shipped region has priority 0; the race's
looping effects (tyre loops, course ambience) send 255 × (1 − min(1, gain))
every frame instead (race_sound.md §2), so the quiet ones are taken first.
The densest sequences want 47 voices at once.

Per-voice values sent each update: pitch in game units converted with
`pitch * 0xEB3 >> 12` (44.1 -> 48 kHz), volume L/R masked to 15 bits
("volume / 2" mode, 0x3FFF = full), ADSR1/2, start address. Command flag
bits: key off (0x01), key on (0x02), and the switch bits PMON (0x04), NON
(0x08), VMIXL (0x10), VMIXEL (0x20), VMIXR (0x40), VMIXER (0x80); the packer
sets 0xA0 (reverb send left/right) for voices keyed with the reverb flag.

Who sends to the reverb: sequenced notes and sound effects per region flag
bit 0; engine voices always.

## Effect (reverb) setup

- IOP init: both cores' effect attributes cleared (mode 0), work areas at
  0x1D8800..0x1DFFFF (core 0) and 0x1F8800..0x1FFFFF (core 1) in SPU2 RAM,
  dry mix switches on for all voices, wet switches off, MMIX 0x0FC0
  (core 0) / 0x0FCC (core 1): voices to the dry mix and to the effect, the
  streamed (ADMA) input to the dry mix, and on core 1 also core 0's output;
  EVOL 0x7FFF on both cores, AVOL (core 0 into core 1) 0x7FFF.
- Sound init on the EE (boot): effect mode 4 ("studio C"), depth (EVOL)
  0x0FFF on both sides and both cores, effect enable on.
- Races: the ambience routine re-targets EVOL every frame:
  `depth += clamp(target - depth, -1/36, +1/36)`, EVOL = `depth * 32767`.
  The target is 0.125 (= 0x0FFF) normally, 0.25 or 0.5 when a probe of the
  course finds surface attribute bits 0x18 = 0x10/0x18 (0.25) or 0x08
  (0.5) (tunnels and similar; for the race/track workstreams). Stopping the
  menu music can switch the effect off (mode 0) when a flag is set.

## Streamed music and the output mix [C, `PDISPU2.IRX` / `PDISTR.IRX`]

Read from the IOP modules on the disc (offsets are module-relative `.text`
addresses; libsd calls by export number: 4 Init, 5 SetParam, 9 SetAddr,
18 BlockTrans).

- **The music streams are voices.** `PDISTR.IRX` sets up two outputs over
  one buffer and picks one by the `SShd` codec when a stream starts (0x1CC):
  codec 1 (PCM) → the core's sound-data input (AutoDMA, BlockTrans at
  0x1554 / 0x15BC, volume to BVOL at 0x1684); codec 0x10 (PS-ADPCM) → a
  ring buffer `PDISPU2.IRX` plays on **core 1 voices 22 and 23** (object
  init 0x2A8: core 1, voice 0x16; SetAddr per half buffer, 0x304; pitch
  from the sample rate). All 66 `bgm/*.ads` are codec 0x10 at 44.1 kHz, so
  the game's music always takes the voice path.
- **Stream volume.** A five-word Q15 volume object per stream (init 0x1C8C:
  0x8000 ×4, gate 0): side = clamp(((w0 × w_side >> 15) × w3 >> 15) × gate
  >> 15, −0x8000, 0x7FFF) (0x1C30). PDISTR sets only w0, from the EE's
  int(clamp(v, −1, 1) × 32768) (export 10, 0x1D84); the sides and w3 stay
  0x8000; the gate is 0x8000 while the stream plays (0x156C / 0x00E0) and 0
  when it stops. The voice-stream callback (0x424) writes **VOLL of voice 22
  and VOLR of voice 23 = (side >> 1) & 0x3FFF**: a request level v plays at
  voice volume v × 0x3FFF, linear, exactly like an effect voice at volume v.
- **Mix.** IOP init (0x1F28): MVOL 0x3FFF on both cores, AVOL (core 0 into
  core 1) 0x7FFF, BVOL 0 (no sound-data input), MMIX 0x0FC0 / 0x0FCC. The
  stream voices, the effect voices and the sequenced music all sum in the
  SPU2 voice mix at unity master and clip together at 16 bits: **music and
  effects meet at equal weight**. (EVOL is 0x7FFF at IOP init and set to
  0x0FFF by the EE's sound init, "Effect (reverb) setup".)

Ours: the voice mixer's 0.5 output trim (headroom) applied to the voices
only put every effect 6 dB below the streamed music. The trim is now a named
constant (`VoiceMixer::kOutputTrim`) that the streamed music takes too
(`AudioEngine::stream_output_gain` = level × Music volume × trim), so the
balance matches the PS2's. The two outputs still have their own volume
settings (Music, Sound effects).

## Hardware model

| Part | Ours | Status |
|---|---|---|
| ADPCM decode, loop flags, loop history carry-over | as hardware | exact |
| Pitch counter (12-bit fraction, step limited to 0x4000) | as hardware | exact |
| 44.1 -> 48 kHz pitch conversion | the game's packer formula | exact |
| Interpolation | 4-point Gaussian, hardware table, per-tap `>> 15` | exact (Catmull-Rom selectable) |
| ADSR | counter model: rates < 44 step every sample with the step scaled by `2^(11 - rate/4)`; higher rates step every `2^(rate/4 - 11)` samples; exponential decrease scales by level; exponential increase above 0x6000 slows by 4 (split between step and interval for rates 40..43); all-ones rates never move; sustain level `(SL + 1) * 0x800` | hardware behaviour as documented by emulator research; the decay-to-sustain check happens before stepping (so SL 15 holds full level), not verified on hardware |
| Volume | 15-bit "volume / 2", applied as `e * vol >> 14` | exact, except register writes are slewed over ~5 ms (the SPU2 applies them instantly) |
| Voice pool and stealing | 46 voices, game's priority rule | as the game; "oldest" uses key-on order, the game uses its own per-voice update counter |
| Reverb | PS1/SPU2 formula, libsd presets (offsets x4 = word units), work area `size * 4` words (0x37F0 for studio C), half-rate processing alternating L/R samples, 39-tap half-band resampling in and out, EVOL output scale | algorithm and presets as hardware; the two cores' identical reverbs are merged into one (linear, so equivalent except for saturation); echo/delay mode parameters (delay, feedback) are not applied (unused by GT3) |
| Mixing | sum of voices + reverb, clamped to s16 after a 0.5 trim | the game's master volume is 0x3FFF (full); the trim is ours for headroom, and the streamed music takes it too (the game plays it on voices) |
| Timing | 60 Hz updates, sample-exact in the mixer | as the game |
| Fades | not modelled | the game can fade a sequence's volume linearly over N frames |

## Verification

- `tests/test_sound_seq.cpp`: event decoding, the tick clock (8 ticks per
  update at T = 1,000,000; note on at the second update, off one update
  late, at the exact sample), loops, bend maths, envelope rates, reverb
  impulse response, and (with the disc) all 9 sequences: every program
  index valid, loop periods within one update of the tempo maths, no
  clipping, at most 46 voices.
- Renders of all 9 sequences: see the formats doc (loop repeatability 0.99,
  tuning on the A440 grid within 3 cents, no clicks).
