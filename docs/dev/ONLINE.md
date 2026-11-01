# Online battle (i.LINK Battle over IP)

The original's arcade **i.LINK Battle** linked up to six PS2s with FireWire
cables, one car per console. This reimplementation does not run the original
program, so there is no i.LINK traffic to tunnel. The mode is rebuilt on our own
netcode over UDP instead. Choosing i.LINK Battle in the arcade menu opens the
online lobby (`src/app/screen_online.cpp`; also `gt3r --screen online`).

## Model: every player owns their car

- Each player runs **only their own car**, on the exact core (GT3's own race step,
  `sim::exact`; docs/dev/RACE.md "Races on the exact core"), at 60 steps a second, and
  sends that car's state after every step.
- An online race is a `race::ExactField` with the i.LINK battle's race rules
  (`Conductor::BattleMP`): start type 1 (four seconds on the grid), the lobby's laps, no
  tyre wear, no catch-up. Every machine runs **NTSC-U rules at 60 steps a second**,
  whatever disc it has (`ExactFieldOptions::rules`, set by `RaceScreen::exact_options` for
  online races; offline races follow their disc's release), so everyone in a race shares
  one physics.
- The other players' cars are **posed** cars of that race (`ExactRace::set_posed`,
  `ExactRace::pose`; docs/dev/RACE.md):
  - before every step their records take the prediction's motion (`pose_motion`:
    position, velocity, attitude and rates, body axes, speeds, the road segment, the
    steering, pedals, gear and engine speed);
  - their own phases are skipped;
  - they take part in phase 11 (our slipstream and the AI's view of them) and in the
    collision step, whose body starts back along the posed motion (the pose is the end
    of the step); the step's result for them is thrown away;
  - the race flow, the ranking and the gap matrices run on them as on every car: their
    laps, place and finish are what this machine sees. The results take the owners'
    finish times (Finish messages).
- A car–car contact is solved with both bodies as usual, but only our car keeps the
  result. The remote player's machine solves the same contact for its own car, so both
  sides feel the hit.
- `race::RemoteCar` predicts a remote car "now": a one-car exact race of that car runs
  on from each state with its owner's newest pad frame ("Feel and fairness" below). When
  a new state lands, the jump between the shown car and the new prediction is blended out
  over about 0.1 s, so cars glide instead of popping. Collisions use the unblended
  prediction.
- A machine whose disc program the exact core does not know (`ProgramData::load`
  refuses it) cannot race online: its lobby refuses Ready and Start with the reason.

Why owner authority and not lockstep (yet)? Online play was built on a physics that was
deterministic only on the same build (float results differ between compilers and CPUs), so a
lockstep game would have desynced across platforms. Owner authority tolerates different builds and
platforms and packet loss, adds no input delay, and scales to six cars. The cost is that
contacts are approximate (each side sees the other car up to one-way latency late) and
nothing stops a modified client from lying about its car.

### Later: input-only lockstep

The exact core changes the first reason. Its step is the same bit for bit on every
platform (`ee::F` is integer code; `ee_double_platform_hash` and the exact tests pin
arm64 against x86-64), so machines that start a race from the same setup and feed it the
same pad frames compute the same race. That would allow, later:
- **Lockstep:** every machine runs every car from all players' pad frames (each state
  already carries the newest three); a step runs once every frame for it is in. No
  prediction, contacts exactly alike everywhere, a few bytes per step; it costs input
  delay of about half the slowest round trip and a stall when frames are late.
- **Rollback:** run on with predicted frames and re-run from a copy of the race
  (`ExactRace` is copyable) when a real frame differs: no input delay, at about 20 µs per
  car and step re-run (M-series; the EE's divider, run digit by digit, is about half of it).
- What it needs: the same rules and program tables on every machine (NTSC-U rules, as
  now), the same race setup (each car's parameter block from each disc's database: the
  lobby already compares its digest exactly, "Regional discs" below), and the same
  exact-core build (a core version in Hello).
- In GT3's i.LINK battle every console's race starts its random draws from the session's
  shared seed, a hint that it ran lockstep itself.

Not built.

## State (protocol 4)

A car's state is 113 bytes on the wire with three frames of history when one repeats,
116 while steering (every frame different), at most 125. A race datagram carrying it is
about 132 bytes while steering (144 through the relay, plus 28 bytes of IP/UDP header),
sent 60 times a second: about 69 kbit/s per stream, 70 kbit/s at the busiest second; a host still
forwarding four other players' states sends up to 0.30 Mbit/s on one channel (measured in
`net_relay_traffic_per_link`, with pings and contacts; the relay's racing guard is
0.5 Mbit/s).

| field | encoding |
|---|---|
| player, tick | u8, u32 (the owner's step; newer replaces older) |
| race clock | s32, 1/3000 s (negative in the countdown) |
| flags, laps | u8 (rev limiter; bit 1 a respawn, never set: the game has none), u8 |
| entry | u8 pedal style (3 bits), steering-wheel type (3), ASM, TCS; u8 control source (2 bits), driver kind (4) |
| position, velocity | 3 × f32 each, the record's floats bit for bit (physics frame, Z up) |
| heading, pitch, roll, yaw rate | f32 each, bit for bit |
| engine speed, steering command, axle speeds | f32, f32, 2 × f32, bit for bit |
| pitch and roll rates | s16 each, 1/1024 rad/s |
| wheels | rim speed s16 (1/128 m/s), suspension coordinate s16 (1/65536 m) |
| gear, clutch state | u8, u8 |
| pad frames | count + up to 3 frames, newest first: flags u8, kind / shift / back / third bits u8, steer s16, throttle u16, brake u16, back u16 and third u16 when not 0; a frame equal to the one before it is one byte (0xFF) |

A state whose floats include an infinity or NaN (exponent 255) is refused.

Not sent; the receiver derives them (`race::complete_snapshot`): the race time, the
course-frame CoG and velocity, the heading's rotation and the speed (the cheat checks
read them), the brake lamp. Everything else a remote car needs to be drawn and heard
(wheels, suspension, smoke, engine) comes from its prediction shadow's own record.

The pad frames are exactly what the owner's car read: the shadow drives on with the
newest, and a lost datagram loses no frame (what a lockstep game would need).

## Session (`src/net/`)

- `socket.*`: non-blocking UDP (BSD sockets / Winsock). The host socket is
  dual-stack IPv6. Name resolution goes through `getaddrinfo`, preferring IPv4.
- `protocol.*`: the datagram format and messages (`Msg`). Every datagram is
  `"GT3N"`, protocol version, cumulative ack, then reliable messages
  (sequence numbered, resent until acked, delivered once in order) and
  unreliable ones (states, pings).
- `session.*`:
  - **Lobby (star).** The host is player 0; up to five clients join.
    Hello → Welcome (with a random session token) / Reject (full, racing,
    other protocol), Roster, Settings, Car (each player's car, colour,
    ready). The host's Load carries course, laps and grid. Each race screen
    sends Loaded once its scene is built. Go carries the countdown start in
    the host's clock.
  - **Settings:** course, laps, *ghost above* a round trip (default 150 ms;
    0 = always contact), and the car rules ("Lobby car rules" below).
  - **Race traffic (mesh).** Each Hello lists the client's LAN endpoints. The
    roster gives every player's endpoints: the address the host sees, then
    the LAN ones. Clients send "punch" datagrams (`"GT3P"` + session token) to
    every candidate of every other client at once, which opens both NAT
    mappings (UDP hole punching). Once both directions are heard, the pair
    has a direct link and reports it to the host. The host relays states
    only for pairs without one. A pair that fails within 6 s stays relayed
    and retries every 30 s. Lobby, finish and retire messages always go
    through the host.
  - **Receive thread.** Datagrams are read and timestamped on arrival, not
    when the game loop next polls. Two 60 Hz loops otherwise bias every
    measurement by up to a frame.
  - **Clock sync.** Ping/Pong carry four timestamps (NTP). The round trip
    excludes the other side's hold time. The offset comes from the
    minimum-round-trip sample of the last 16, taken every 250 ms. Over
    loopback with a 5 s skew: offset error 0.01 ms, start instants 0.01 ms
    apart. The race screen re-reads the start time every frame while the
    estimate improves. Once racing, a change is eased in (the race clock runs
    at most 5% fast or slow) rather than jumping: over a long link the
    estimate moves by a few ms whenever the best sample leaves the window.
- Peers time out after 8 s of silence. Bye is sent on close.
- `net::online()` is the session shared by the lobby and the race screen.

## Cars

Each player's car travels as a `game::OnlineCar` descriptor (`src/game/net_car.*`;
`CarInfo` messages, relayed by the host, also to players who join later):

- **Disc car:** its database label.
- **Garage car (GT Mode):** the base car, its 27 fitted part ids, the user
  settings, the engine condition and the wheel (~480 bytes). Every machine
  rebuilds the car from its own disc: the race's parameter block from its
  equipment (`race::exact_online_source`), the spec the lobby's rules read
  (`game::online_car_spec`, the same path as GT Mode's `car_spec`). The lobby lists the GT Mode garage first. A
  garage car keeps its own paint. Remote cars use their real spec, since
  weight and size count in contact. Its parts and settings must be ones GT Mode
  can give the car ("Legal setups" below).
- **Mod car:** its label plus the SHA-256 and size of its package (below).
- **Arcade car:** a car of the arcade's class lists (database table 35, e.g.
  `nsx_s_zero_97_290`), with its base car. Every machine builds it as the arcade
  races it (`race::arcade_car`): the variant's parts with its arcade tyres for the
  course (stage 10 "racing"; dirt courses stage 12). Road cars' arcade tyres
  (`arc_normal`) equal their stock `normal` ones; race cars' `arc_racing` grip more
  than their stock tyres (GT-One: μ 1.70 against 1.59).

### Regional discs

Players on NTSC-U, PAL and NTSC-J discs share lobbies. Each machine builds the other
players' cars from its own disc, so a car races only where it is the same car.

- **Parameter block digest.** Every descriptor carries the SHA-256 of the car's parameter
  block P as an online race builds it (`game::online_car_params` / `online_car_digest`:
  `sim::exact::race_params` of the descriptor's equipment through `exact_online_source` and
  `exact_equipment`, with NTSC-U's rules at 60 steps a second as every online race, a road
  course, ASM and TCS on, the descriptor's engine condition). P is what the race reads of the
  car: mass and extents, engine curves, gearing, tyres, suspension, aids. Names, prices and
  model ids are not in it, so a car whose records differ only there is the same car.
- **Compared exactly.** The exact core builds P bit for bit alike on every platform: the
  digests of 316 cars of the NTSC-U disc fold to the same value on arm64 and x86-64, and so do
  the PAL and NTSC-J discs' (`net_car_params_disc`), and a synthetic set is pinned
  (`net_car_params_digest_platform_hash`), with no rounding. The lobby's digest is the race's own: the block a remote car gets in an online
  race equals it (`net_exact_lobby_digest_is_the_race_params`).
- **The check** (`game::online_car_here`, tested in `net_car_here_regions`,
  `net_car_here_disc`). For every other player's car each machine reports one of:
  - **not on this disc**: the car, a part or its arcade car is not in this database;
  - **different on this disc**: this machine's digest differs: this disc's data for the car
    differs where it reaches P (another engine curve, body size, gear set, tyre, suspension);
  - **no model on this disc**: no `cars/day/<model>` for the body the race would draw;
  - **not a legal setup**: a garage car with a part or a setting GT Mode cannot give it,
    with the reason ("Bob's car is not a legal setup: front spring rate out of range";
    "Legal setups" below). It is checked after the digest, so another region's car that
    builds differently reads as different;
  - fine.
  A mod car not installed here is not refused (download, else a stand-in). Once installed it
  is checked like the others: its package fixes the definition, but it is built over its
  donor from this disc's database.
- **Arcade cars** are found by their arcade label. Their base car may have another label on
  another disc (`viper_gtsr_oreca_00_597`: `dodge_viper_gtsr_oreca_00` on NTSC-U,
  `viper_gtsr_oreca_00` on PAL and NTSC-J). A descriptor with a digest builds on this disc's
  base car, and races when the digests match, drawn as this disc's base car
  (`game::online_car_base`); one without a digest is refused.
- **Wire.** The digest trails the descriptor (32 bytes). Older games stop reading before it;
  an older game's descriptor has none and is not compared.

What the discs build differently (`net_car_params_regions` with `GT3R_COMPARE_DISC`: every
car, every arcade car, and every car with each of its upgrade parts alone, 9,962 builds):
- Course physics (road, walls, checkpoints, grid: course slot 28), the AI driver sets and the
  i.LINK course list are identical on every disc.
- NTSC-U 1.10 against 1.00, and NTSC-U against PAL: every block alike (PAL's car data
  differs only in names, prices and model ids, none of which reach P).
- NTSC-J: 7 cars and arcade cars build another P, and 265 garage builds of them: four engines
  (the Impreza WRX under 3 labels and the Integra Type R rev lower, 7,500 / 8,000 rpm; the
  arcade `impreza_wrx_00_281` and `integra_r_98_200` with them) and one collision box
  (`s2000_e_99` is 1 cm shorter). `celica_ss2_97` differs only in its catalogue power, which is
  not in P: it races. `s2000_a_99_ext` and `s2000_e_99_ext` are not on it.
- PAL has no body model for 17 cars: `_996_gt3`, `diablo_gt_00`, `stratos_00`,
  `stratos_wrc_00`, `lotus_99t`, `mclaren_mp4_5b`, `williams_fw11`, `williams_fw16b`,
  `sample002`..`sample010`. Its arcade cars `vauxhall_vx220_00_147` and
  `vauxhall_astra_race_00_461` are not on the other discs.

Cars that race in a mixed lobby:

| discs | cars | arcade cars |
|---|---|---|
| NTSC-U + PAL | 227 of 244 | 72 of 72 |
| NTSC-U + NTSC-J | 237 of 242 | 70 of 72 |
| all three | 220 of 242 | 70 of 72 |

### Legal setups

A garage car's descriptor names its parts and settings, and every machine builds the car
from them, the prediction shadow included. A part or setting value that GT Mode can
never give the car would race as sent. So each machine checks a received garage car
against its own disc's GT Mode rules (`game::garage_setup_problem` in
`src/game/gt_setting.cpp`; `online_car_here` refuses the car, and `online_car_facts` /
`car_meets_rules` refuse the player's own car the same way):
- **Parts.** Each of the 27 slots holds the car's stock part or the record GT Mode
  fits for a goods the car can own (`game::fittable_parts`): the tune shop's goods for the
  car (`find_part`: the table's first record with +0x08 = the car and +0x10 = the stage),
  bought and fitted, a tire set's rear goods with its front ones, and the stock parts'
  own goods, which the change-parts pages fit back. Dirt tires are never fitted (the race
  puts them on); the wheel slot is 0 or stock. An NA tune and a turbo kit are never both
  off stock: fitting one restores the other's stock part.
- **Settings.** Each word is either its parts' stock value (`sim::stock_settings`) or a
  value one of the Settings page sliders that sets it can give. That slider's page must
  open (the locks), it must have two values or more, and the value must lie in its range
  from the fitted part records (`slider_range`, GT Mode §14). No page sets the motion
  ratios, the turbo words or the ASM gains past the first, so those must stay stock.
- **Gears.** A gearbox with stored ratios keeps them. A computed box's ratios come from
  the generator (physics §6.7) with the parts fitted when the box was fitted or its auto
  level last moved. Parts fitted later leave the ratios as they are. The gear sliders keep
  each ratio within the ranges around a reference final drive. Neither the reference final
  nor those parts travel with the car. So each gear's bounds are the generator's over every
  rev adder (engine balancing, an NA tune or a turbo kit), drivetrain and driven tire the
  car can have, and every final the final slider allows. The generator grows with revs and
  tire size and falls with the final, so the ends of each range are enough. The reverse
  uses the stock auto level and final.
- **Engine condition** needs no check: any value gives an output factor (97 % to 100 %
  for wear and running in, times 100 % to 105 % for fresh oil) that a GT Mode car reaches.
- **Tested.** `net_car_setup_rules` covers an invented car (no disc). `net_car_setup_disc`
  covers every disc car on NTSC-U, PAL and NTSC-J: stock, with each tune-shop upgrade
  alone, and with the adjustable gearbox and its sliders at their top ends, then every
  other upgrade, then every slider at its bottom end (8,765 garage cars on NTSC-U). All
  of them are legal. Each car with a spring, a gear ratio or another car's suspension
  past them is refused (724 of 724).

## Pickers (`src/app/online_pickers.cpp`)

Cross on the lobby's Car row opens the car page, on the Course row (host) the course
page; Left / Right on the rows still step through the lists. Both follow GT3's arcade
pickers with the disc's own art, loaded from the menu sets:

- The car page starts on one of two tile pages, switched with L1 / R1 or their tabs. It
  opens on the page that lists the lobby's car.
- Makers: GT Mode's dealers (`cd/cd` -> `cd/cd_<region>` in gt_mode.mbl; each `dealer<n>`
  item links `cd/cd_<region>_<maker>`, its layer holds the maker's `logo<n>`, the region
  page its `flag`), in the dealers' region order; the garage first. Makers without a
  dealer use the tune shop's logo of the same name (ts/ts), else their name. Groups are
  keyed by the maker name without spaces (the database has "CHEVROLET" two ways).
  Every disc and mod car, as the database builds it.
- Classes: the arcade's class page (`ar/select_class`): Class C, B, A, S and rally cars
  with its `class_<x>_logo` art, then the garage (the page's `home_garage`). Every car is
  on one, each car once (`game::car_class`):
  - **The class lists' cars** (gcf tag 14), also the ones arcade mode keeps locked (its
    unlock levels do not apply online); only labels this disc's region lacks are left out
    (NTSC-U: the two Vauxhalls). They race as arcade cars ("Cars" above). On NTSC-U the
    lists hold 66 cars: C 11, B 11, A 14, S 20, rally 10. Mod cars with an `[arcade]`
    class are in theirs (and under their makers).
  - **Every other car** (178 of the 244) is classed as GT3 classes a garage car in Single
    Race (docs/behaviour/gt_mode.md §16.1, `data::class_by_weight`): on a road course by
    its weight per power (the spec's mass less 120 kg over its computed power: below 3.0
    kg/PS S, 5.0 A, 6.3 B, else C), on a dirt course a rally car. The stock car of a
    listed arcade car is on the class page as that arcade car only (and under its maker
    as itself, with its weight's class).
  - On NTSC-U a road course gives C 41, B 51, A 89 and S 53 cars, and the 10 rally cars; a
    dirt course 188 rally cars. The weight rule puts 52 of the 56 listed road cars in
    their own list's class; the misses sit near a boundary (the Silvia Spec R Aero, 4.96
    kg/PS, is listed in B).
  - The car page's specs give each car's class ("Class A, 4.6 kg/PS").
- **Allowed cars only** (the switch at the top right, Square): only the cars the
  lobby's rules allow, on both pages; tiles left empty are hidden. On by default. The
  page and the switch are remembered (`[online] car_page`, `car_page_filter`).
- Car page: the `<car>_mini` strip of `ar/select_car` (250 icons; mod cars in arcade mode
  get theirs from the mod patches), the car turning under `menu/showroom_single.fbi`'s
  lights (render/showroom.h, shared with the GT-mode menus) with the page's reflection
  map and a floor reflection, loaded once the choice rests a few frames; the paint column
  with each paint's colour and carcolor.sdb name; specs; the lobby's rule warning.
- Course page: the course's still (stream/course.ipic) behind, `<course>_logo` and the
  `<course>` map of `ar/select_course`, the `<course>_mini` strip, the length from the
  arcade course lists. Courses without arcade art show their names.
- Lobby: the course still beside the settings, the car's icon and paint chip in their
  rows, a paint chip per player.

**Pointers.** Every lobby page takes the mouse, touch and the VR controller's pointer
(which `App::xr_pointer` sends as mouse events):
- **Rows** (start page, lobby, rules) focus under a hovering mouse or VR pointer, and a
  click acts as Cross. A finger has no hover: its first tap focuses, a second acts (as
  in the game's menus).
- The **"<" and ">"** around a value are buttons.
- The **footer's actions** are buttons ("Cross: choose", "Triangle: back").
- The right button goes back on the car, course and rules pages (not in the lobby:
  that would leave the session).
- **Text rows** (code, address, name, relay server) start typing on a click; a click
  elsewhere finishes the text.
- A click on the **session code** copies it.
- The **course picture** opens the course page (host).
- **Car page:**
  - Maker and class tiles focus under the pointer and open on a click.
  - Strip icons move the cursor on the first click and choose on the second (as the
    arcade pickers).
  - The wheel and a drag along the strip move along it; the wheel over the paint
    column changes the paint.
  - Paint chips and the strip's arrows are buttons.
- **Course page:** its strip, arrows and wheel work the same way.
- **The mod car prompt** has Download / No buttons.

Scripted screenshots: `GT3R_ONLINE="host,page=cars|makers|classes|courses|rules,..."`
opens a page; `class=B`, `category=production|race`, `drive=FR`, `aspiration=na|turbo`,
`maxps=300`, `stock=1` and `mods=0` set the host's rules. `GT3R_POINTER_SCRIPT` clicks
through the pages (docs/dev/PLATFORMS.md "Pointer scripts").

## Mod cars

A mod car is shared as a **package** (`src/game/mod_package.*`). That is its
`cars/<label>.ini` plus the files it references (glTF body, pictures), with
mod-relative paths, as one blob whose SHA-256 is its identity. The owner's
lobby builds and validates its own package when the car is chosen (a car
that would not pass is shown to the others as a stand-in, with a note to its
owner).

**Download.** When the lobby allows mod cars and another player races one this
machine lacks, the lobby asks once per package: "Download it? It is checked,
then added to your mods" (Cross / Circle; Circle = a stand-in car).
- The request goes to the host, which serves the package if it has it, or
  fetches it from the owner, verifies it, and keeps a copy for the next player.
- Chunks are 1 KB on the reliable channel, at most 24 in flight per
  connection. They only flow in the lobby, never during a race.
- On arrival: hash check, `parse_mod_package` and `validate_mod_package`
  against this machine's database. Then `install_mod_package` writes
  `mods/dl-<12 hash digits>/` (mod.ini with priority −100, so the user's own
  mods win), and the lobby rescans mods and reloads its car list. The race
  screen loads its own database, so the car is there with its real body,
  rim and physics.
- Opponents (online players and the AI alike) now draw a mod car's own body
  (`body_mod_for_car`), as the player's car always did.

**Validation** (everything checked before anything is written or loaded):
- **Structure:** magic, at most 8 files and 32 MB, per-type caps (`.ini`
  64 KB, `.glb` 16 MB, `.png` 4 MB).
- **Paths:** plain relative (letters, digits, `. _ -`, at most 3 levels, no
  `..`, absolute or hidden parts, no duplicates even by case).
- **Allowed files:** exactly one `cars/<label>.ini`, otherwise only `.glb`
  and `.png`. A package can never replace a game file.
- **The definition:** it parses and names the advertised label, and its donor
  is on this disc. Its values are in plausible ranges: weight 300–5000 kg,
  dimensions, power up to 1500 kW, torque up to 3000 N·m, rpm, gear ratios
  0.2–20 (EV reductions are ~9), aero, grip 50–150 %, at most 64 raw pokes
  (whose offsets `add_mod_car` already bounds-checks).
- **References:** the files it references are exactly the package's other
  files.
- **Content:** PNGs at most 2048 × 2048, checked from the header before
  decoding, then decoded. The GLB must load, with embedded textures at most
  2048 × 2048 and at most 500,000 vertices.
- **Hardened readers:**
  - glTF sizes, offsets and counts are range-checked (a huge accessor count
    used to overflow the bounds check);
  - out-of-range JSON numbers no longer convert to `int` (undefined
    behaviour);
  - `decode_png` takes a size cap.
- **Real cars must pass:** `GT3R_MODS_DIR=<mods folder>` runs the
  `net_mod_packages_of_installed_mods` test, which builds and validates every
  installed mod car. The EV pack: all 5 pass, 0.3–4.5 MB each.

## Fuzzing

`tests/fuzz/fuzz.cpp` has a fuzz target for every reader that sees untrusted
bytes:
- **Mod car files:** `png`, `glb`, `json`, `mod_ini`, `package`.
- **Network input:** `snapshot`, `online_car`, `rendezvous` (including
  VersionInfo and Error(UpdateRequired)), `relay_server` (datagrams into a
  `RelayServer` with min_online set, optionally with a valid cookie filled
  in; VersionQuery included), `versions` (version strings and the relay's
  versions file).

Build it in a separate tree with sanitizers:

```bash
cmake -S . -B build-fuzz -DGT3R_FUZZ=ON -DGT3R_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined"
cmake --build build-fuzz --target gt3r_fuzz
build-fuzz/gt3r_fuzz all 60      # every target for 60 s (built-in mutation fuzzer, works with Apple clang)
```

- **Built-in fuzzer:** it mutates valid seeds (bit flips, interesting
  integers, truncation, splices, huge numbers in text). The current input is
  kept in `fuzz-last-input.bin`, so a sanitizer report can be replayed with
  `gt3r_fuzz <target> 0 fuzz-last-input.bin`.
- **libFuzzer (coverage guided):** on Linux, add `-DGT3R_LIBFUZZER=ON` and
  set `GT3R_FUZZ_TARGET=<target>`. Apple's clang does not ship libFuzzer.

First results (Apple clang, ASan + UBSan, built-in fuzzer):
- **Found and fixed:** the car definition parser converted numbers like
  `2.4e12` to `int` (undefined behaviour). Numbers are now limited to ±1e9 and
  must be finite.
- **Clean:** every target then ran 0.5–1.2 M inputs (30–40 s each) without a
  sanitizer report. That includes the glTF reader after its hardening.

## Feel and fairness

### Physics-based prediction
Each remote car has a **shadow**: a one-car exact race of that car (its equipment, its
entry flags and grid slot) on our course (`race::exact_shadow`; no laps to finish, no
tyre effects; with the owner's autopilot, configuration 3, the race's AI drives it too).
On each state the shadow is synced (`race::sync_motion`, `RemoteCar::receive`):
- **Set from the state:** position, velocity, attitude and its rates, engine speed,
  steering command, axle and wheel speeds, suspension, gear and clutch state (then the
  body axes and hub offsets from the attitude); the pedal style; the driver kind (the AI
  driving it on after its finish); the start countdown from the owner's clock.
- **Kept:** the rest of its record (tyre and brake heat, wear, turbo, the driveline's
  locks, the AI driver's plan, filters).

It then catches up to "now" with the owner's newest pad frame (at most 0.25 s) and steps
once per step after that. Corrections are blended over 0.1 s. `GT3R_NET_PREDICT=dead`
keeps the synced record moving along its velocity instead (dead reckoning). The step
never throws; a shadow whose step fails anyway (out of memory) falls back to dead reckoning.

Measured on Trial Mountain, two machines in one process, states through the wire format
(`net_exact_prediction_beats_dead_reckoning`: 40 s of a line-following driver with a
steering weave; `net_exact_duel`: a whole lap):

| latency | dead reckoning rms / max | exact shadow rms / max |
|---|---|---|
| 17 ms (both cars on the race's AI) | | 0.1 cm / 7 cm |
| one step ahead, two laps on the race's AI, 18 / 21 contacts | | 0.0 cm / 2 cm |
| 100 ms | 6.4 cm / 13 cm | 1.9 cm / 7 cm |
| 150 ms | 12.3 cm / 25 cm | 4.5 cm / 17 cm |
| 100 ms, a whole lap | | 1.8 cm / 81 cm |

The worst moments are the latency window right after the driver changes input (the
shadow still applies the old one), the known trade of input-based prediction. A shadow
step costs 12.5 µs (M-series), so 5 remote cars catching up 6 steps each is 0.44 ms per
frame (`net_exact_shadow_cost`).

### Contacts
A hit our machine sees between our car and a remote car (the collision step's car
responses, `ExactRace::car_contacts`) goes to that car's owner (`Contact`: point and the
impulse the remote car felt, course frame). It travels over the direct link, or through
the host. The owner applies it to its car's record (`ExactRace::apply_impulse`: the
velocity by J / m, the yaw rate by (r × J) / (10 I_z), as the collision step's response
does) only when its own machine saw no contact with us within 0.2 s and the car is not a
ghost to it. Impulses are capped at 60,000 N·s.

### Ghosting
A remote car is a ghost (drawn at 50 %, no contact) when:
- its round trip is above the lobby's limit;
- it is within the first 3 s after the start signal;
- it is lapping us or being lapped (race distances more than 0.9 laps apart).

### Lobby car rules
The lobby's **Car rules** row sums them up ("Class B, FR, at most 300 PS"); Cross opens
the rules page, where the host changes them and the others read them. The page counts
how many of the player's cars are allowed and says why the chosen one is not.
Each rule is "any" by default (`game::CarRules`, `game::car_meets_rules`; tested in
`net_car_rules_disc`, `net_car_rules_arcade_classes`):
- **Mod cars:** allowed (default) or not.
- **Tuned cars:** allowed, or stock cars only. A garage car counts as tuned when a part
  or a setting differs from the stock car's, as GT Mode's own test (`GtMode::tuned`);
  an untuned garage car races in a stock lobby.
- **Class:** one of the arcade's classes C, B, A, S or rally cars: only the cars of that
  class, as the car page's classes give them (every car has one: its list's, else by its
  weight per power, a rally car on dirt). A garage car's class is its weight per power as
  tuned, as GT3 classes it.
- **Type:** production cars, or race and rally cars (car +0xF1, as GT3's events tell
  them apart).
- **Drivetrain:** FF, FR, 4WD, MR or RR (GT3's events' coding), from the car's
  drivetrain part.
- **Aspiration:** NA or turbo (turbo kit record +0x11, as GT3's events read it).
- **Power limit:** off, or 200–700 PS at the crank, from the car's own spec
  (the R34 V-spec rates 336 PS).
- **The course:** as the arcade's class page, the listed road classes' cars are refused
  on dirt courses and the listed rally cars on road courses ("Class S cars are not
  available on dirt courses"). Every other car races anywhere (on dirt as a rally car).
- **Who checks:** each player's lobby refuses Ready for a car that breaks
  them. The host checks every car it can rebuild before the start. A mod car the
  host lacks cannot be checked there; only the mod cars rule applies to it. A garage
  car that is not a legal setup ("Legal setups" above) breaks every lobby's rules.
- **Other players' cars** ("Regional discs" above): a player whose machine cannot race
  another player's car cannot be ready ("Bob's car has no model on this disc"), and drops
  Ready when such a car appears. The host refuses the start for its own view the same way.
  The car's name is red in the player list, the reason beside Ready / Start race.
- **Wire:** the Settings message carries the class, type, drivetrain and aspiration
  after the power limit (an older host's message without them reads as "any").

### Cheat warnings
The warnings cannot see a car whose descriptor makes it faster: the prediction shadow is
built from the same descriptor, so it agrees with the owner's machine. That case is
stopped in the lobby instead: a garage car's parts and settings must be ones GT Mode can
give it ("Legal setups" above), and the other cars are built from each machine's own disc.

Every machine runs `race::Plausibility` on every remote car:
- **Speed:** above its geared top speed × 1.15 + 4 m/s for 0.5 s.
- **Teleport:** a jump its motion does not explain (more than 6 m).
- **Acceleration:** a speed gain over 2.5 g in both quarters of a half
  second (engines ramp; a shove is one step, and contacts with us are
  excluded).
- **Motion:** physics disagreeing by more than 5 m for 2 s.
- **Finish time:** a claimed finish more than 2 s from when we saw it cross
  the line.

Each kind fires once per car per race and goes to the host (`Flag`). The host
gets an on-screen warning naming who reported it, and a "!" next to the player
in the lobby. The **Remove a player** row lists each player's warnings and
removes them (Cross twice; they are told "removed by the host"). Flag, don't
ban.

False positives:
- The exact duels (`net_exact_duel*`: line drivers, a ram, the race's AI, 0-150 ms
  late, with the exact shadow) raise nothing.
- A 2:50 two-instance race on Trial Mountain through the public relay raised
  nothing.

## Race screen (online mode)

- The step count comes from the wall clock since the synchronised start, not
  the frame accumulator, so a stalled frame catches up instead of leaving the
  car behind everyone's race clock. Snapshot age is then simply local race
  time minus the snapshot's race time. The blend between the last two ticks
  comes from that clock as well (`FixedStep::follow`): blended by frame times,
  above 60 fps every other frame could be drawn half a tick back.
  A state from ahead of our race clock (our
  frames stalled and the steps catch up in a burst, or the other machine runs a step
  early) waits until our race reaches its time, so the car is posed where it was at
  our race time: posed from the newest state instead, a car 50 ms ahead stood up to
  1.5 m off and collided where its owner saw no contact (`net_exact_stalled_machine`).
- The pose is always a step of the shadow's own from the state (a state is synced at
  the coming step's start, never at its end), so its road segment hint and ground come
  with it; a sync also moves the hint (`CourseCollision::road_segment`) and the wheels'
  ground points with the car. With a hint left behind, the race flow counted phantom
  laps for the remote car (its finish seen mid-lap) and the collision step's segment
  filter dropped its contacts (`net_exact_duel_one_step_ahead`).
- Pause opens GT3's race window (CONTINUE / EXIT, "Exit race?") with a note
  that the race goes on. The car keeps running (no input) and the camera keeps
  following it: the race does not stop for anyone. EXIT retires from the race
  (back to the lobby). Select's longer list is Resume, Camera, Gearbox and
  Retire; restart and replays are not offered. There are no respawns, as in GT3: a car
  stays where it is.
- Each player's finish time is the owner's (reliable Finish message). A
  finished car turns into a ghost (no contact). So does a car whose round trip
  to us is above the lobby's ghost threshold. It is drawn at 50 % alpha and
  turns solid again when the round trip drops. The results use player names.
  After the results everyone returns to the lobby (the host's return sends
  Lobby, which ends the race for anyone still in it).
- A car this machine lacks and does not download (or that fails validation) is
  shown as your own car (a stand-in); physics stay the owner's. A remote car whose
  spec this machine refuses is its stock base car; the lobby checks keep both from
  happening to disc, garage and arcade cars.

## Server (`tools/gt3r-relay`)

A small rendezvous / relay server makes internet play work without port
forwarding. The server is `net::RelayServer` (`src/net/relay_server.*`); its
wire format is in `src/net/rendezvous.*`.

- **Session codes.** "Create a session" registers the host and gets an
  8-character code (alphabet without 0/O/1/I/L), shown as `ABCD-EFGH`. Players
  type it under "Join with a code", with or without the dash.
- **Endpoint exchange.** On a join the server sends the client the host's
  addresses (public as the server sees it, plus LAN) and the host the
  client's, with a random join nonce. Both punch with the nonce at once. If
  the client hears the host's acknowledgement it sends Hello straight to it.
- **Relay fallback (as TURN).** After 2.5 s without a direct path, the client
  binds the nonce's relay channel and connects through it. (The host binds
  every announced join right away and releases the binding when the player
  came in directly.) Two players who fail to punch each other bind
  `pair_key(session token, a, b)`. The server only accepts that key with the
  token the host registered. A relayed connection is an ordinary connection
  whose datagrams travel as `"GT3R" + key + datagram` via the server.
- **Link slots:** a race whose pairs go through the relay reserves them when
  it starts; a full relay queues the lobby ("Link slots and the queue" below).
- **Limits:** see "Abuse protection" below.
- **The host** sends a keepalive every 5 s, which also keeps its NAT mapping
  open. Bindings are refreshed every 20 s.
- **The lobby** shows the code in large type for the host to read out. Each
  player's link is shown as *direct*, *relay* (through the server) or *via
  host* (direct-IP sessions only).
- `GT3R_NET_FORCE_RELAY=1` disables punching (as behind a symmetric NAT), for
  testing the relay path.

Direct-IP sessions ("Host by address" / "Join by address") still work with no
server: on a LAN, or with UDP 47630 forwarded. In those sessions the host
relays for pairs that cannot punch.

### Protocol (rendezvous version 3)

The message list is in `src/net/rendezvous.h`. Control datagrams start with
`"GT3S"`, u16 version, u8 type. Relayed data is `"GT3R"`, u64 key, then one
game datagram.

- **Requests** (Register, Keepalive, Lookup, Bind, Unregister, Reserve, Release) carry a u64
  cookie right after the header, then the game's version (u16 major, minor,
  patch), and are zero-padded to 64 bytes. The server drops shorter ones.
- **VersionQuery / VersionInfo** (the update check, "Versions and updates"
  below): the game sends its version and a platform tag, padded to 160 bytes;
  the server answers the latest release, min_online and the release page URL
  (at most 128 characters, so at most 148 bytes).
- **Cookie.** SipHash-2-4 over (source IP, source port, 30 s time window),
  keyed with a random per-process secret. It is valid for the current and the
  previous window. A request with a missing or stale cookie gets only a
  16-byte Challenge (request type, cookie), and the game resends the request
  with it. The first request of a session costs one extra round trip, and so
  does the first request after the cookie expires (30–60 s). Unregister and Release are not
  checked: they create nothing and send nothing, and the session token authorizes them.
  Neither is VersionQuery: it creates nothing, and its answer is smaller than it.
- **Slots** (below): Reserve (host: code, token, number of links) is answered
  with Reservation: granted, queued (position, estimated wait in seconds or
  unknown), released, or refused (with a fixed reason). The server also answers
  a Keepalive with Reservation while the session holds or waits for slots, and
  sends one unasked when queued slots are granted. Release frees the slots or
  leaves the queue.
- **Version.** A request with another version gets Error(Version) in the
  requester's own header (the Error layout is unchanged since version 1). It
  is sent only if it is no larger than the request. Older games (versions 1
  and 2) show
  "please update the game to play online" (or a shorter "update the game"
  when the request was short). A newer game talking to an older
  server says "the relay server is out of date", and an older game talking to
  a newer server says "the relay server needs a newer version of the game".
- Version 2 changes: cookies, padding, Challenge, 8-character codes, and link
  slots (Reserve, Reservation, Release).
- Version 3 changes: the game's version in every request, VersionQuery /
  VersionInfo, and Error(UpdateRequired) (code 6: fixed text, then the latest
  version and the release page).
- **Why a new version rather than extending 2.** Nothing public speaks
  version 2. A version 2 request has no version field: its zero padding would
  read as 0.0.0, and long requests none at all. With version 3 every request
  has the field at a fixed place, and version 2 games already get Error(Version),
  which they show as "please update the game to play online": the same
  outcome min_online gives them. Accepting version 2 for a while would have
  needed replies in each requester's header version, which the server does not
  do. So there is no grace window. Deploy the relay together with the first
  version 3 release (the owner's version 2 builds stop playing online until
  updated).

### Link slots and the queue

The relay's capacity is counted in **links**. A link is one relay channel that
carries a race between two players: a player's join channel to the host, or
the channel of a player pair that could not punch. The server carries at most
`--relay-links` (default 300) racing links at once. Pairs that connect directly
never use one.

- **Only while racing.** Channels are still bound in the lobby (a player who
  cannot reach the host directly has the whole lobby on its join channel), but
  a channel outside a reservation is a *lobby* channel: no slot, and a tight
  rate limit (256 kbit/s each way, 20 Mbit/s for all lobby channels together).
  That carries lobby messages, and mod car downloads at about 30 s per MB.
- **Reservation at the start.** When the host starts the race, it counts the
  links its lobby needs (`Session::relay_links_needed`): players connected to it
  through the relay, plus every pair of players that do not both report a
  direct link. A pair still punching counts; players report a pair through a
  relay channel as relayed. A lobby that needs none loads at once and never
  queues. Otherwise the host sends Reserve and the Load goes out when the slots
  are granted, usually one round trip later.
- **Which channels.** After the grant, each of the session's channels takes a
  slot when it first carries data, up to the granted number, and gets the
  racing flood guard (0.5 Mbit/s each way, about twice the busiest legitimate
  channel: a host still forwarding four players' states, 0.26 Mbit/s). A
  channel beyond the reservation (a pair that lost its direct link mid-race)
  keeps the lobby rate, which still carries a race link.
- **Release.** The host sends Release when the race ends (back to the lobby).
  The server also frees the slots when the session closes or expires, and when
  the reservation's channels carry nothing for 10 s. A grant the host no longer
  wants (it cancelled, and that Release was lost) is released again when its
  keepalive's answer reports it.
- **Queue.** A Reserve that does not fit queues the lobby, first come first
  served. A smaller request may start ahead of the head only if the head would
  still start within 60 s. That is estimated from the running reservations'
  expected ends (start plus the typical race length: the average slot hold
  time, 4 min until races have measured it). When that is unknown (every race
  that would free enough slots is overdue), smaller requests may go first only
  while the head has waited less than 60 s.
- **Waiting.** A queued host sends its keepalive every 2 s (instead of 5) and
  must keep doing so: 15 s without one loses the place. Nobody waits more than
  10 min, and at most 100 lobbies wait (`--max-queue`). Beyond that, or for a
  race larger than the whole relay, Reserve is refused ("the relay is busy: try
  again in a few minutes"). Each keepalive's answer carries the position and an
  estimated wait (the same estimate, in order).
- **Lobby.** The host's Start race row then reads, for example, "Relay busy:
  2nd in line, about 3 min - the race starts by itself", and Cross cancels
  (Release). The roster carries the waiting state, so players see the same
  line. The wait is shown in minutes only when it is at least a minute and
  known. The race starts by itself when the grant arrives. A player who stops
  being ready cancels the wait. If the server never answers the Reserve (10 s),
  the race starts anyway (its channels then run at the lobby rate).
- **Bounded:** reservations (one per session, freed as above), queue entries
  (capped, expire). The per-session caps on channels still apply. Reserve needs
  the cookie, like every request that creates state.

### Abuse protection

The server is public, so it must not be usable to aim traffic at anyone, and
no table may grow without bound.

- **Return routability.** Every request that creates state or makes the server
  send to someone else (Register, Keepalive, Lookup, Bind) needs a valid
  cookie. A spoofed source gets one Challenge per request, never larger than
  the request. Both ends of a relay channel have therefore proved their
  address, so a channel cannot be pointed at a victim.
- **Replies to unverified sources.** These are only Challenge, Error(Version)
  and VersionInfo, each no larger than the request (a VersionQuery shorter
  than 160 bytes gets nothing). Every other Error comes after the cookie check
  and carries fixed text: it never echoes the request. Error(UpdateRequired)
  adds the configured latest version and release page, so it can be larger
  than the request, but only goes to a source whose cookie proved it receives
  there. Unknown datagrams get no reply.
- **Rate limits.** Defaults are in `RelayServer::Limits`. Most can be changed
  with `gt3r-relay` flags:

  | Limit | Default | Flag |
  |---|---|---|
  | Control datagrams per source IP (IPv6: per /64; keyed-hash buckets, fixed memory) | 10/s, burst 40 | `--ip-rate`, `--ip-burst` |
  | Control datagrams, all sources (a CPU guard) | 20000/s | |
  | Lookups of unknown codes per source IP | 10/min | `--misses-per-min` |
  | Lookups of unknown codes, all sources | 200/s | `--misses-per-s` |
  | Joins (Incoming to the host) per session | burst 10, then 1 per 2 s | |
  | Pending joins (nonces): per session / total | 16 / 8192 | `--max-pending` |
  | Sessions: total / per source IP | 4000 / 8 | `--max-sessions`, `--sessions-per-ip` |
  | Relay channels: total / per session | 20000 / 15 pairs + its joins | `--max-channels` |
  | Racing links (link slots) / lobbies waiting for them | 300 / 100 | `--relay-links`, `--max-queue` |
  | Racing channel, per direction (a flood guard) | 0.5 Mbit/s | `--channel-mbit` |
  | Lobby channel, per direction / all lobby channels | 256 kbit/s / 20 Mbit/s | `--idle-channel-kbit`, `--idle-mbit` |

  `--bench` (load testing only) lets loopback sources (127.0.0.0/8, ::1) past
  the per-IP limits. Nothing else changes, and it is never on by default.

  When a lookup limit is hit, every lookup from that source is refused,
  including one with the right code. The refusal happens before the lookup, so
  it reveals nothing about which codes exist.
- **Code guessing.** There are 31^8 ≈ 8.5·10^11 codes. Even at the global miss
  limit, with the server full (4000 sessions), a guess hits about once in 12
  days. That rate also needs over 1000 source addresses. With 6 characters it
  was once every 20 minutes. A hit only reveals the host's address and lets
  the guesser into the lobby, where the host can remove them.
- **Bounded state.** Sessions, joins and channels have caps and time out
  (60 s without keepalive, 120 s, 60 s idle). So do reservations and the
  queue (above). Per-IP session counts follow the
  sessions. The rate buckets are a fixed array. Codes and nonces come from
  SipHash in counter mode, so earlier ones do not predict later ones.
- **Logs.** stdout (journald) gets one line when a session is registered,
  closed or expires (code, and a tag for the host's IP on registration), one line per
  join (code and a tag for the joining player's IP), one when a race's slots
  are queued, granted or freed, and a stats line every minute. Version checks
  and refusals for an old version are only counted in that stats line (per
  platform), never logged one by one. A tag is a keyed hash of the IP (IPv6: its
  /64): the same all UTC day, with a new random key each day or restart, so tags
  cannot be reversed or matched across days. A server run with `--log-ips` logs
  the IPs (no port) instead; the startup lines say which. With `--geoip` each tag
  is followed by the player's country (`a1b2c3d4 (BR)`, `??` when unknown), from
  DB-IP's [IP to Country Lite](https://db-ip.com) (CC BY 4.0), which
  `tools/gt3r-relay/update-geoip.sh` fetches monthly. Nothing else is recorded, and
  nothing goes to disk. The systemd unit gives the relay its own journal
  (`journalctl --namespace=gt3r-relay`), capped at 1 GB
  (`journald@gt3r-relay.conf`), so it never pushes the system's logs out.

### Versions and updates

The game's version has one source: `project(GT3Revisited VERSION x.y.z)` in
`CMakeLists.txt`. CMake writes it to `gt3r_version.h` (the window and app
metadata, the main menu, Settings, the online checks, the macOS bundle).
Android's `build.gradle` reads the same line: versionName `x.y.z`, versionCode
x·10000 + y·100 + z. A local build may add `-DGT3R_VERSION_SUFFIX=-dev`.

**Comparison rules** (`net::GameVersion`, tested in `net_version_compare`):
- Online play needs x.y.z ≥ the relay's **min_online**. The suffix does not
  count: a 0.2.0-dev build plays where 0.2.0 does. A min_online of 0.0.0 (not
  set) allows every version.
- The **update notice** shows when the relay's **latest** is newer: latest >
  x.y.z for a release, latest ≥ x.y.z for a suffixed build (0.2.0-dev is older
  than the 0.2.0 release).

**The update check** (`net::VersionCheck`, `src/app/updates.*`):
- When: at startup unless `[online] update_check = off` (Settings → Online),
  and on Settings → Online → Check now.
- How: one VersionQuery to `[online] server` on a background thread (the name
  lookup can block). Up to 3 tries a second apart, then it gives up. It never
  holds up the menus. What is sent: the game's version and platform (windows,
  macos, linux, android), nothing else.
- Cache: the answer is kept in config.ini (`update_latest`,
  `update_min_online`, `update_url`, `update_checked`), so a start without a
  network still knows. `--shot` runs (screenshots, scripted tests) use only
  the cache unless `GT3R_UPDATE_CHECK=1`, so they never reach a live server.

**The update dialog** (`src/app/updates.*`; drawn by App over any screen,
modal; pad, keyboard, mouse and touch):
- **Update available** (latest is newer): "GT3 Revisited <latest> is
  available. You have <ours>."
- **Update required to play online** (below min_online): "Online play needs
  GT3 Revisited <min_online> or newer. You have <ours>. Offline play is not
  affected."
- Buttons: **Download update** (default focus) opens this platform's package
  (below), **Later** closes, and a **Release notes** link opens the release
  page. Circle / Triangle / Esc mean Later.
- **At startup** it shows once per release, over the main menu, the first-run
  screen or the game's title page (`Screen::update_prompts`). Later stores the
  release in `[online] update_dismissed`; it shows again only for a newer one
  (`net::show_startup_prompt`, tested in `net_update_prompt_rules`).
- **Online battle** opens the "required" dialog every time it is entered while
  this game is below min_online, when Create a session / Join with a code is
  chosen anyway, and when the relay answers UpdateRequired. After Later the
  lobby stays open for Host / Join by address.
- **Afterwards:** a small badge beside the version in the main menu's title bar
  ("Update available" / "Update required for online") opens the dialog (tap,
  click, or Up from the top row and Cross). Settings → Online has **Download
  update** (the dialog) and **Release notes**.

**Downloads.** Every GitHub Release carries its packages under
stable names, so `https://github.com/<owner>/<repo>/releases/latest/download/<name>`
is always the newest:

| Platform | Stable asset |
|---|---|
| Windows x64 | `gt3r-windows-x64.zip` |
| macOS arm64 | `gt3r-macos-arm64.zip` |
| Linux x86_64 | `gt3r-linux-x86_64.tar.gz` |
| Android (arm64, x86_64) | `gt3r-android.apk` (only when the APK was built) |
| Meta Quest (the VR build, `GT3R_XR`) | `gt3r-quest.apk` (likewise) |

The relay's `release_url` may hold an `{asset}` placeholder
(`net::update_links`, `net::release_asset`; tested in
`net_release_asset_per_platform` and `net_update_links`):
- **Download update** opens the URL with `{asset}` replaced by this
  platform's name.
- **Release notes** opens the URL cut at `/download/`, i.e.
  `.../releases/latest` on GitHub.
- A platform without a package (Windows arm64, Intel macOS, Linux arm64, iOS)
  gets the release page for both. So does a URL without the placeholder.
- **Caveat:** if a release lacks a platform's stable asset, that platform's
  direct link answers 404. Without the Android signing key there is no APK, so
  set the key up before the first release that players update to.

**Enforcement on the relay** (`net_relay_min_online_enforced`):
- Requests that create state from a game below min_online get
  Error(UpdateRequired): Register, Lookup, Reserve, and a Bind that would add a
  channel or a channel end.
- This happens after the cookie check, like every other error. The reply
  carries fixed text, the latest version and the release page.
- Not refused (they create nothing new): Keepalive, Unregister, Release, and a
  Bind that refreshes an end the sender already holds.
- So a lobby made before min_online was raised keeps running and finishes its
  race. Its next race start (Reserve) is refused: the host's lobby shows
  "Relay server: update required to play online: version x.y.z" and stays open.
- A game refused while hosting or joining ends that attempt with the same
  text. The lobby then switches to its "update required" state, with the
  release page from the error.

**Direct-IP sessions** (Host / Join by address, no relay) need the same x.y.z
on both sides (the suffix does not count), on top of the game protocol check.
- The host refuses a different version in Hello. The joiner's message says
  who must update: "update required: the host has version 0.2.0 (you have
  0.1.0)", or "the host has an older version (…): the host needs to update".
- Welcome carries the host's version (older games ignore the extra string). A
  joiner leaves a host whose version differs, or a host too old to send one.
- Through a relay, the relay's min_online decides instead (`net_direct_ip_needs_same_version`).

**Relay configuration:**
- Flags `--latest`, `--min-online`, `--release-url`, or a versions file
  (`--versions-file`; the systemd unit uses `/etc/gt3r-relay/versions.conf`).
  The file holds `latest = x.y.z`, `min_online = x.y.z` and
  `release_url = https://...` lines (at most 128 characters; `{asset}`
  allowed, see "Downloads"), which win over the flags.
- `systemctl reload gt3r-relay` (SIGHUP) re-reads the file. Sessions,
  channels and reservations stay. A file that does not parse (bad version,
  URL, unknown key, min_online above latest) is refused and the previous
  values stay; the log says so.
- Unconfigured (no flags, no file), every version may play and the check
  reports no release.

**Announcing a release from your relay:**
1. Publish the release with its stable-name packages (the workflow's
   `release` job attaches them).
2. If the rendezvous protocol or the relay changed, deploy the relay first:
   `tools/gt3r-relay/deploy.sh root@<server>`. An update keeps
   `/etc/gt3r-relay/versions.conf`.
3. `tools/gt3r-relay/set-versions.sh root@<server> <x.y.z> [min_online] [url]`.
   min_online defaults to the new version. The URL is kept when left out; the
   first run needs it, e.g.
   `https://github.com/<owner>/<repo>/releases/latest/download/{asset}`.
   The script writes the file, reloads the relay, and prints its "reload:" log
   line with the new values. Games older than min_online can no longer create
   or join sessions; they show the update dialog.

### Running your own relay

The game's default server is `gt3r.gameversions.com` (`kDefaultServer` in
`src/app/screen_online.h`). Anyone can run a relay of their own: players
reach it by entering its name or address under "Relay server" in the lobby
(`[online] server`), and a build of your own can make it the default by
changing `kDefaultServer`. Give it a DNS name that points straight at the
server (UDP 47631), not through an HTTP proxy, since the relay is plain UDP.
Deploy a new relay before releasing a game build with a new rendezvous
version: older games then get "please update the game".

For players spread over a continent, run one server near its middle: in the
US, coast-to-coast players then see roughly 15–35 ms extra on
a relayed pair, and nothing on direct pairs. Any small Linux VPS works (1 vCPU
/ 512 MB). Relay traffic is only for pairs that cannot connect directly.

**Capacity and cost, rule of thumb** (measured; IP/UDP headers included):

- One link (a relayed pair, both directions): 60 datagrams/s each way,
  0.15 Mbit/s into the relay and 0.15 Mbit/s out.
- A fully relayed 2-player race is 1 link: 0.15 Mbit/s each way. A fully
  relayed 6-player race is 15 links: 2.2 Mbit/s each way.
- 300 slots in use: 45 Mbit/s each way, 36,000 datagrams/s relayed.
- Traffic: each slot in use around the clock is about 100 GB a month (in
  plus out; half of it each way): about $1 at a typical $0.01/GB beyond a
  plan's included transfer. Real use is far lower: slots are only held while racing.
- CPU: measure the server with the benchmark below. On an M-series Mac the
  relay used about 8.4 µs of CPU per relayed datagram, so 300 links took
  ~30 % of a core (indicative only: macOS syscalls differ from Linux, and a
  VPS core is slower).

Use a server with an IPv4 address (IPv6-only servers can't be reached by
IPv4-only players), e.g. Debian or Ubuntu. Then, from this repository:

```bash
tools/gt3r-relay/deploy.sh root@<server IP>   # builds and installs over SSH; run again to update
tools/gt3r-relay/deploy.sh root@<server IP> --when-idle   # an update that restarts only with no sessions open
```

The script copies only the relay's own sources (20 KB, no game code or
data), installs g++ / cmake, and builds on the server. It installs
`/usr/local/bin/gt3r-relay` and the hardened systemd unit, opens UDP 47631 in
ufw if ufw is active, and starts the service. Alternatives:

```bash
# Docker (from the repository root)
docker build -f tools/gt3r-relay/Dockerfile -t gt3r-relay .
docker run -d --name gt3r-relay --restart unless-stopped -p 47631:47631/udp gt3r-relay

# or plain: build without SDL or the game, install with systemd
cmake -S tools/gt3r-relay -B build-relay -DCMAKE_BUILD_TYPE=Release && cmake --build build-relay
sudo cp build-relay/gt3r-relay /usr/local/bin/
sudo cp tools/gt3r-relay/gt3r-relay.service /etc/systemd/system/
sudo systemctl enable --now gt3r-relay
```

- Open UDP 47631 in the VPS firewall.
- Point a DNS name at the server (e.g. `relay.example.org`).
- Either set `kDefaultServer` in `src/app/screen_online.h` before building
  the game, or have players enter it under "Relay server" (`[online] server`).
  The update check asks the same server.
- The server logs one line per session event, plus a stats line every minute
  (sessions, channels, pending joins, links in use / capacity, racing and
  queued lobbies, relayed / dropped, the busiest channel, challenges, rate
  limited, the typical race length, version checks per platform, requests
  refused as outdated). At startup and on each reload it logs the versions in
  force. Limits are flags (`gt3r-relay --help`); the systemd unit passes the
  main ones. Read the log with `journalctl --namespace=gt3r-relay`.
- It asks for 4 MB socket buffers (`--buffers-kb`). Linux caps that at
  `net.core.rmem_max` / `wmem_max` (often 208 KB); the startup line shows what
  it got. Raise them for bursts:
  `sysctl -w net.core.rmem_max=4194304 net.core.wmem_max=4194304`.

### Benchmark

`gt3r-relay-bench` (`tools/gt3r-relay/bench.cpp`, built by the standalone relay
CMake and installed by `deploy.sh`; no SDL) runs simulated relayed races
against a relay. It uses the real protocol (cookies, register, lookups, binds,
reservations, keepalives). A lobby is six sockets with 15 links, and every
player sends a 126-byte datagram on each of its channels at 60 Hz, like a
fully relayed six-player race. Each step reports:
- links racing and lobbies queued;
- datagrams/s and Mbit/s into and out of the relay;
- loss;
- relay-added latency p50 / p99 (send → relay → receive, timestamps in the
  payload);
- with `--pid`, the relay's CPU time during the step (Linux `/proc/<pid>/stat`,
  macOS `proc_pid_rusage`): CPU seconds per million relayed datagrams, and the
  datagram rate and link count 70 % of one such core would carry;
- the generator's own CPU.

When both run on one machine they share its CPU (the output says so). On
Linux the generator batches with `sendmmsg` / `recvmmsg`. The relay must run
with `--bench`, and with enough slots for the ramp. Never use `--bench` on the
public port.

```bash
# on the server (a second relay on another port; the live one keeps running,
# but shares the CPU: run it at a quiet time, or stop the service first)
gt3r-relay --port 47700 --bench --relay-links 10000 --quiet &
gt3r-relay-bench --server 127.0.0.1:47700 --ramp 50,100,200,300,400,600 --duration 10 --pid $!
kill %1
```

Local run (M-series Mac, macOS, relay and generator on the same machine;
indicative only):

| links | datagrams/s in = out | Mbit/s each way | loss | p50 / p99 ms | relay CPU | CPU s per M datagrams | links at 70 % of a core |
|---|---|---|---|---|---|---|---|
| 50 | 6,000 | 7.4 | 0 | 0.11 / 0.67 | 6 % | 10.7 | 545 |
| 100 | 12,000 | 14.8 | 0 | 0.14 / 5.4 | 12 % | 9.9 | 589 |
| 200 | 23,800 | 29.3 | 0 | 2.9 / 33 | 23 % | 9.6 | 607 |
| 300 | 36,000 | 44.4 | 0 | 1.8 / 3.7 | 30 % | 8.4 | 695 |
| 400 | 48,000 | 59.1 | 0 | 1.6 / 3.9 | 40 % | 8.4 | 698 |
| 600 | 72,000 | 88.7 | 0 | 1.2 / 5.1 | 58 % | 8.1 | 725 |

An earlier run went further: 1000 links used 94 % of the relay's core (no
loss, p50 25 ms), and 1500 links saturated it (33 % loss). The latency spikes
at 100–200 links are the busy desktop, not the relay.

## Testing

- Unit tests: `net_*` (the wire format bit for bit, loopback sessions with and
  without 30 % packet loss, rejection, an older protocol told to update:
  `net_old_protocol_told_to_update`).
- Disc tests (`GT3R_DISC`; tests/test_net_exact.cpp): two players' machines in one
  process, each its own `ExactField` with the other car posed from the other's
  states through the wire format:
  - `net_exact_posed_car_contact`: a posed car driven into our parked car pushes it,
    the hit for its owner points against its motion, the posed record keeps the
    pose, a ghost drives through;
  - `net_exact_contact_impulse`: the other machine's impulses on our record;
  - `net_exact_duel`, `net_exact_duel_contact`, `net_exact_duel_ai`,
    `net_exact_duel_autopilot`: a lap of Trial Mountain 0-150 ms apart (line drivers,
    a ram at the start, the race's AI from the start, the autopilot's configuration 3
    with states out of order): both finish, each sees the other's finish where its
    owner had it, no cheat warning;
  - `net_exact_stalled_machine`, `net_exact_prediction_beats_dead_reckoning`,
    `net_exact_shadow_cost` (the figures above).
  The `net_relay_*` tests cover the server: code joins, relay fallback, cookie
  challenges and rollover, spoofed binds, rate limits, join caps, and old
  protocol versions. They also cover link slots:
  - grant and release (Release, unregister, 10 s without traffic);
  - queue order and the 60 s skip rule (known and unknown estimates);
  - the queue cap, expiry, and keepalives to hold a place;
  - `--bench` (loopback only);
  - a direct-only lobby never queues;
  - a lobby that waits for a full relay, starts by itself, cancels;
  - per-link traffic of a fully relayed six-player race
    (`net_relay_traffic_per_link`, the figures above).
- Versions: `net_version_compare` (the rules), `net_relay_version_query`
  (round trip, reply no larger than the query, no cookie, counted not logged,
  rate limited), `net_relay_min_online_enforced` (each state-creating request
  below / at / above min_online), `net_relay_versions_reload` (the file,
  reload keeps sessions), `net_relay_unconfigured_allows_all`,
  `net_relay_update_required_client` and
  `net_relay_update_required_at_race_start` (the game's handling),
  `net_version_check_roundtrip` (the background check, and its time-out),
  `net_direct_ip_needs_same_version`.
- The loopback tests do not depend on how fast the machine runs them (they
  used to fail now and then while other builds loaded the CPU):
  - Simulated clock: `net::set_clock` swaps `now_ms()` for the sessions, their
    receive threads and the relay server. The tests' `FakeClock` moves 5 ms per
    pump step (with a real 5 ms sleep), so timers such as resends, punch
    give-up, relay fallback, time-outs, rate limits and cookie windows count
    pump steps rather than wall time.
  - Link slot tests: the server runs on `receive` / `tick` with the test's
    `now`. Its replies go to in-memory mailboxes (`RelayServer::set_send_hook`),
    because loopback UDP is delivered asynchronously on macOS.
  - Waiting for datagrams uses bounded loops that stop as soon as the
    condition holds, not fixed sleeps.
- Two instances on one machine, directly or through a relay on localhost (for test
  runs, use two portable copies of the app, `portable.ini` in each one's data folder,
  with `[audio] music_volume = 0.00` and `sfx_volume = 0.00` in its config.ini;
  `GT3R_NET_DEBUG=1` logs our car, the posed car and the newest state twice a
  second; each contact is logged, and a summary at the finish):

```bash
GT3R_RACE="auto=1,cam=1" GT3R_ONLINE="host,players=2,course=trialmt,laps=2,car=r34_gtr_v_99" \
  gt3r disc.iso --screen online
GT3R_RACE="auto=1,cam=1" GT3R_ONLINE="join=127.0.0.1,car=supra_rz_97,name=Two" gt3r disc.iso --screen online
# through a relay: build/gt3r-relay --port 47751, then
#   host: GT3R_ONLINE="host,...,server=127.0.0.1:47751,codefile=/tmp/code.txt"
#   join: GT3R_ONLINE="server=127.0.0.1:47751,codefile=/tmp/code.txt,car=supra_rz_97,name=Two"
```

  Measured on an M-series Mac with `auto=1` (the race's AI drives both cars),
  through the relay, two laps of Trial Mountain: both finished (207.809 s and 210.766 s)
  and each machine saw the other's finish at its owner's time; about 12,700 states each;
  the cars touched side by side at 165 km/h for 0.7 s (13 and 18 responses), seen by
  both machines at the same steps with the same cars' positions to 2 cm and impulses
  within 4 % (3.18 / 3.22 kN·s the first), so no Contact message had to be applied; no
  warnings. A one-lap race with no contact finished 107.30 s / 107.46 s, as
  `net_exact_duel_autopilot` (107.30 s / 107.49 s).

## Plan

1. ~~Netcode core: compact state with inputs, client mesh with hole punching
   and relay fallback, receive thread, continuous clock sync, ghost
   threshold.~~ (done)
2. ~~Rendezvous / relay server (`tools/gt3r-relay`): session codes, endpoint
   exchange (no port forwarding), per-pair relay fallback.~~ (done; deploy it:
   "Running your own relay")
3. ~~Cars: garage cars as descriptors; mod cars as validated, content-addressed
   packages offered for download.~~ (done; fuzz targets: "Fuzzing" above)
   - **Package validation:**
     - allowed file types only (`.ini`, `.glb`, `.png`);
     - a package may not replace game files;
     - relative, plain paths only;
     - size and count caps;
     - strict parsing with limits (image size, glTF counts and bounds);
     - `.ini` keys and physics values checked against plausible ranges;
     - fuzz targets for the PNG / glTF / JSON / ini readers under
       AddressSanitizer.
4. ~~Feel and fairness: physics-based prediction, contact messages, ghosting at
   the start and for lapped cars, lobby car rules, cheat warnings and removing
   players.~~ (done)
5. ~~Races on the exact core: posed remote cars, exact prediction shadows,
   protocol 4.~~ (done)
6. Later: input-only lockstep or rollback ("Later: input-only lockstep" above).

## Known issues

- Remote players' engine notes use their car's opponent engine sound; turbo /
  whine extras are not sent.
- No online replays yet (they would need the remote snapshots recorded too).
- No 2P split screen combined with online players.
- Two game versions whose parameter block builder differs see each other's cars as
  "different on this disc" (the digest compares builds, not only discs).
- A remote car is drawn and heard from its prediction shadow's record (its own tyre
  smoke, wheels and engine as the shadow runs them), not its owner's.
- Players' IP addresses are visible to the other players (inherent to peer
  links; `GT3R_NET_FORCE_RELAY=1` sends everything through the server instead).
- The relay is built and run on Linux (Debian, g++) and macOS (clang); the
  Windows build compiles but is not run as a server.
