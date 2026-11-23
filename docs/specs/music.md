# Music cues and states (BGM orchestration)

What music GT3 plays around races, results, licenses, prizes and menus,
from the program's BGM layer and its callers (`CORE.GT3`), with
`bgm/ads.inf` (MADS) and `music/music.inf` (MSEQ) for the track names.
Formats: [../formats/sound.md](../formats/sound.md). Our API:
`src/audio/bgm.h` (`bgm::Player`, the cue ids). Confidence: H unless marked.

## Changelog

- 2026-11-12 (public issue 305): §5 the page-music rule's default (a page that matches no test)
  stops the BGM; buying a car goes to such a page (`00cmn/cd_bought`) and stops the dealer music.
  Ours kept the music there; corrected for the GT-mode set (`page_music`).
- 2026-11-04 (public issue 199): §2 the attract demo's demo_a01 needs race +0x7C clear (`fn_0017DD80`); GT Mode's Preview
  sets it (gt_mode §11), so it plays the start jingle and the playlist's race songs at the replay level. Implemented
  (`RaceScreen::race_preview_`).
- 2026-10-27: §2 pause RETRY (presentation §E: exit reason 4, `vf42` tears the race down and sets
  it up again) stops the paused music like a pause EXIT (M: the same teardown; the stop itself
  not traced); the new run starts its own cues (the
  start jingle by type, silence over a flying run-up). Ours resumed the paused piece into the new
  run, so a failed licence test or voided time trial retried from its fail screen played "fail"
  over the retry. Fixed (`RaceScreen::update_pause`; test `sound_race_music_pause_retry`).
- 2026-10-22: §2.5 replays and demonstrations re-run the start sequence (the start jingle over
  the hold, the race song from the signal, at the replay level 0.7; jingles 1.0) and post the
  finish jingle when the car the cameras follow finishes; confirmed from the program and the
  PCSX2 footage (jg11 then the race song 10.28 s later in both replays, as in the live race).
  Implemented (ours started the race song with the replay and never held).
- 2026-10-22: §7 the race songs' cut markers (the ads.inf info blocks: 32 marker tracks; on
  the disc beats, phrases and the replay cameras' cut points), the race music object's
  per-frame marker tracking and the cut requests it feeds (presentation §G2a); §1.1 the
  requests made with the run's switch off (at level 0) are reproduced. Implemented
  (`sound::MadsMarkers`, `race::MusicMarkers`, `race::RaceMusicLevel`, `race::CutClock`).
- 2026-10-21: §2 the start signal of type 3 starts (presentation.md §B0): rolling races post it
  at the 300-frame hand-over, flying runs (time trial, flying licence test, free run) at the
  start-line crossing that ends the run-up; the race music (0x14) starts there, silence
  before. Implemented (ours started the race songs with the run).
- 2026-10-20: §2.1 the options commit runs on the music list page's Cancel (none with a
  song lifted) and always starts a new pass; §1.1 the preview is reproduced (menus.md §10.1);
  the option pages have no BGM (no caller asks for option01).
- 2026-10-20: §2 start sequence: the jingle is posted on the race loop's first frame, and the
  GT race driver runs the pre-race quick menu before the race's run, so a race entered
  through that menu starts its jingle after the menu closed (and stopped the BGM). Ours
  missed it there (the hold was already on when the menu closed); fixed in `RaceMusic::decide`.
- 2026-10-20: §1 request levels (menu 0.8, race songs 0.5 live / 0.7 in replays,
  jingles and the demo track 1.0), the on/off switches (replay BGM / SE at options
  +0x54 / +0x58) and the effects volume; §2.1 the race playlist (the options' music
  list); §3 the quick menus go through the menu path (Menu BGM switch, level 0.8);
  §5 / §6 the Replay Theater page plays 0x1A (not theater01); 0x1C has no caller.
  Implemented (`bgm::k*Volume`, `race::RacePlaylist`, `page_music`).

## 1. The BGM layer

One id selects all background music:

| id | what |
|---|---|
| 0x00..0x11 | a sequence, by the game's name table (alphabetical: 0 arcade01, 1 battle2p01, 2 dealer01, 3 free01, 4 gorace01, 5 gtmenu01, 6 home01, 7 license01, 8 license02, 9 machtest01, 10 main01, 11 mini01, 12 option01, 13 theater01, 14 timetrial01, 15 tune01, 16 wash01, 17 wheelshop01) |
| 0x12..0x22 | a stream: ads.inf group id − 0x12; track = index in the group, random for −1; `index | 0x1000` plays `bgm/i_<file>` (the intro version). A missing file plays sound effect 4 (`disable`) instead |
| other | stop |

Streamed groups (file, working title from ads.inf):

| id | file | title | id | file | title |
|---|---|---|---|---|---|
| 0x12 | str01a | jg11 | 0x1B | str03a | jg04e |
| 0x13 | str02a | jg12 | 0x1C | str04a | jg01 |
| 0x14 | race_a01..25 | race tracks | 0x1D | str06a | jg06 |
| 0x15 | str10a | shortmenu-2 | 0x1E | str07a | jg05 |
| 0x16 | str05a | result | 0x1F | str08a | bronze |
| 0x17 | str14a | jg05 | 0x20 | str09a | fail |
| 0x18 | str12a | champ-3 | 0x21 | str11a | champ-2 |
| 0x19 | str13a | getacar | 0x22 | demo_a01 | (Lenny Kravitz, demo) |
| 0x1A | str91a | Are You Gonna Go My Way (GT remix) | | | |

Entry points:
- **play** (restart): stops the current music and starts the id.
- **play if different**: keeps the current music when it is the same
  piece: same index (not −1, not an intro request), same class, and the
  same sequence number (sequences) or the same file (streams). Menus use
  this, so moving between pages that share a piece does not restart it.
- **stop**: immediate (the stream stops; a sequence's notes are keyed off
  and release). There are no fades in the BGM layer and no caller fades
  the BGM volume.
- A play flag passed to the IOP streamer: 1 for menus and presentations
  (they repeat), 0 for race tracks and jingles (they play once) (M: the
  flag's meaning is inferred from its use; sequences always loop).
- Global switches (options): "menu BGM" gates every menu call (the quick
  menus included); the race uses its own switches, "race BGM" and "replay
  BGM" (§1.1).

### 1.1 Levels and switches

The options hold **on/off switches only** (options block, gt_mode.md §12):
menu BGM +0x04, menu SE +0x08, race BGM +0x0C, race SE +0x10, replay BGM
+0x54, replay SE +0x58. There is no volume option: the BGM master
(f32 0x352B6C) and the SE master stay 1.0 (their setters 0x22B7D8 and
0x22C620 have no caller).

Every BGM request carries a volume v (f32). The layer (0x22B5B8) uses
v × BGM master:
- a **sequence** sets the sequencer's global music volume to
  min(int(v × 16384), 0x3FFF) (0x22B2F8), which enters every note before
  the square (docs/specs/sound_sequencer.md "Notes"): v = 0.8 gives 13107,
  about 0.64 in amplitude;
- a **stream** sends int(clamp(v, −1, 1) × 32768) to the IOP streamer as
  its volume (0x25C9F8; linear).

The levels by caller:

| caller | level | switch |
|---|---|---|
| every menu request: pages, presentations, quick menus (0x1D27F0 play if different, f32 0x3500AC; 0x1D2858 play, f32 0x3500B0) | 0.8 | menu BGM (+0x04): off = no request |
| options music list preview (PlayMusicBox, 0x169D48): the selected race song's intro (index | 0x1000), looping, play if different | 1.0 | none |
| race songs (RaceBGMPS2::vf5 0x17E160, id 0x14) | 0.5 in play mode 0 (live), 0.7 otherwise (replays, f32 0x34FCE4) | see below |
| other race requests: start / finish / medal / fail jingles, the demo track 0x22 | 1.0 | see below |

The race object's base level (0x17E0D8) is its last request's volume
(+0x190) when the run's switch is on, else 0; the switch is race BGM
(+0x0C) in play modes 0 and 3, replay BGM (+0x54) otherwise (play mode
`race+0x8000+0x1784`, presentation.md: 0 live, 1 / 2 replays, 2 the attract
demo). A request takes 1.0 when that base is non-zero, 0 otherwise, then
the factors above, and stores the result as the new +0x190 (a stop,
RaceBGMPS2::vf8, sets it back to 1.0). Every frame
(RaceBGMPS2::vf2) the stream volume is set to the base, so switching the
race BGM off silences the music at once. With the switch off the requests
still happen at volume 0 (the playlist, §2.1, still advances).

Sound effects: the effect's own volume × SE master (0x22BBF8); the SE
switches gate them. The SE master never scales sequenced voices.

Ours: `bgm::kMenuVolume` / `kRaceSongVolume` / `kReplaySongVolume` /
`kJingleVolume`, passed with every request (`bgm::Player::play*`,
`AudioEngine::play_music` / `play_sequence`). Our Music and Sound effects
sliders (a modern addition) sit on top: a stream plays at level × Music
slider; a sequence sets the sequencer's music volume from its level and its
voices play on the mixer's music bus, whose gain is the Music slider; the
Sound effects slider is the gain of the mixer's effects bus only. The switch
rule is reproduced (`race::RaceMusicLevel`): with the switch off the race's
requests still play, at level 0 (the playlist advances, the song's markers
keep timing the replay cuts, §7), switching it off silences the stream at once,
and +0x190 stays 0 until the next stop after a request made with the switch off. The PlayMusicBox preview (menus.md
§10.1) goes through `bgm::Player::play_if_different` at level 1.0, not gated.

## 2. In the race (event queue, BGM event 0x14)

Race code posts BGM events to the race's music object:

| when | event value | effect |
|---|---|---|
| race start, standing start type 1 (the 240-frame hold: licence tests from rest) | 0x1B | "jg04e" start jingle, played once while the cars are held |
| race start, standing start type 2 (the 600-frame hold: every single race) | 0x12 | "jg11" start jingle, played once while the cars are held |
| race start, start type 0 (demo / machine test) or 3 (rolling / flying) | – | no start jingle; silence until the start signal (type 3: the rolling start's hand-over after 300 frames, a flying start's crossing of the line; presentation.md §B0) |
| the start signal, and restarts (M: the posting routine is shared by the start sequence and the race's restart path) | 0x14 | race music: the next race track from the race playlist (§2.1), played once (it replaces a start jingle still playing); when it ends the next one starts (except in demo races) |
| pause / resume | −2 / −3 | pause / resume the stream (it continues where it stopped); a stop or the next play ends the pause (leaving a race from its pause menu: −2, then the stop, and the next race's music plays normally; RETRY alike, M: the run's teardown stops it) |
| attract-mode (demo) race | 0x14 is replaced by 0x22 | demo_a01 (play mode 2 with race +0x7C clear, `fn_0017DD80`; GT Mode's Preview sets it, so it keeps the race songs, at the replay level) |
| **the player crosses the finish line, any place** (one-player display modes: races against other cars, time trials) | 0x13 | "jg12" finish jingle (stops the race music). The place is not checked. Precisely: the race display's car (`D+0x2469`, set every frame from the camera's target car by `RaceSinglePlayer::vf29`; live the player's) finishing (event 5, `RaceDisplay::vf3` -> `vf17`), once per display set-up (`D+0x2471`); in replays the car the cameras follow then (§2.5) |
| license test passed | – | no finish jingle: the pass handler posts no finish event (event 5) in a live run (only in replays / demo runs, which show no medal), so the licence display's "jg12" post does not run; the medal jingle follows |
| license test replayed, or its demo run, reaching the finish | 0x13 | "jg12" (event 5 is posted there; no medal jingle) |
| 2P battle (display modes 7..8, the split-screen layouts): a player crosses the line | 0x13 only when that player is 1st | "jg12" for the winner; the loser's finish posts nothing |
| license test: medal | 0x1D / 0x1E / 0x1F | gold "jg06" / silver "jg05" / bronze "bronze", posted by the medal presentation about 37 frames after the pass (docs/specs/presentation.md A) |
| license test failed | 0x20 | "fail" only (event 9; no "jg12") |
| time trial run voided | 0x20 | "fail" (M: the trigger is a time-trial display event; not traced further) |
| any other id | 0..0x22 | played once with a random track of the group |

### 2.1 The race playlist (the options' music list)

The music list is a playlist saved in the options block at +0x228
(0x410 bytes): u32 song count, u16 cursor, s8 shuffle switch, s8 "no song
listed", u16 seed, +0x0A..+0x0F unused, then 64 entries of 16 bytes at
+0x10 {u64 id code of the file name (db id hash of "race_aNN.ads"), s8
played, u8 in the list, s16 order, +0x0C..+0x0F unused}.

- **Run start** (RacePS2Base::vf9 0x17D580): the 0x410 bytes are copied
  from the options into the race (+0xBE88) and on into the race BGM object
  (+0x5A8). **Run end** (RacePS2Base::vf36 0x17D7E0): copied back to the
  options. So the playlist carries over between runs, restarts and races,
  and is saved with the game.
- **Next song** (0x21AEE8), on every race-song request (event 0x14 and the
  next song after one ends): loop { if "no song listed" → nothing (the
  request becomes a stop); find the first entry k < count whose order equals
  the cursor (s16); cursor += 1; if entry k is not played: mark it played and
  take it; if cursor ≥ count: rebuild (cursor 0); if a song was taken, store
  the cursor and return it }. (No entry with that order makes the loop spin
  for ever; a rebuilt list always has one.)
- **Song → track** (0x21AB08): the index of the file in group 0x14 whose id
  code matches; none → index 0. The song plays once (play flag 0).
- **Rebuild** (0x21AD00): first (0x21AC98) every entry k < count gets
  played = (in the list == 0), and "no song listed" = no entry in the list.
  Then, shuffle off (0x21AD38): cursor 0, order k = k. Shuffle on
  (0x21AD90): every order = −1, cursor 0; a 32-bit state = the u16 seed;
  for i = 0 .. count − 1: k = (unsigned) trunc(unit × float(count)) (EE
  multiply, rounding towards zero; unit = the gt_mode.md §20.1 generator
  on that state); if order[k] ≥ 0 step k forward (wrapping to 0) to the
  first order < 0 (at most count − 1 steps); order[k] = i. Finally one more
  unit gives the new seed u16(trunc(unit × 32768)) (also with count 0).
- **New list** (0x21ABB0; the race BGM object's constructor and the new
  game's options): seed = the shared state's integer draw (gt_mode.md
  §20.1) stored as u16; all 64 entries {id 0, played 0, in the list 1};
  count = the songs of group 0x14 in order, each entry's id = the id code of
  its file; then shuffle on and a rebuild (0x21ACE0).
- **Options commit** (PlayList::vf18 → 0x16AD00, Cancel on the music list page
  without a lifted song; menus.md §10.1): the list widget's entries (in the
  player's order, with the check marks; played / order as they were) are copied
  back, then the shuffle switch is set and the list rebuilt (0x21ACE0). Every
  visit of the page therefore ends with a new pass (cursor 0, a new order from the
  seed), changed or not.
- A new game saved by the original stores shuffle 1, seed 0x456 and the 25
  songs in a shuffled order. A fresh shared state (0x87654321) gives exactly
  that: its first integer draw as the seed, shuffled, leaves 0x456 (checked:
  `gtmode_disc_ps2_new_game_layout`; the order bytes themselves were not
  compared with an original save).

Ours: `race::RacePlaylist` (`src/race/race_playlist.*`), stored as text in
config.ini `[options] music_playlist` (our options live there), saved each
time a song is taken; a missing or unreadable entry makes a new list (the
options page keeps the one it makes there). One config.ini serves every disc,
so a stored list takes the songs of the disc in use (`stored_or_new` through
`sync`). Edited on the Options music list
page (`menu::flow::MusicListPage`, `RacePlaylist::commit`). The
PS2 save import keeps the original's options block as it is (round trip);
our playlist is not taken from or written into it. A save that never came
from the PS2 exports a new game's list (`default_ps2_options`). A damaged stored order
(no entry at the cursor) is rebuilt instead of spinning.

Start sequence: the race-information object (`fn_0017A578` at race setup,
`fn_0017A5E8` each frame) reads the event's start type (+0x1F3) and, for
types 1 and 2, posts the start jingle and holds the cars 4 s / 10 s (a
frame counter of seconds × 60); at the end of the hold it posts the start
event. A separate state (−1) posts the race music at once, with no jingle.
The jingle (and the hold) start on the **race loop's first frame**: setup
(`fn_0017A578`, from RaceBase::vf9) sets a start-pending flag (+0x50) and the
start type (+0x54); the first per-frame call (`fn_0017A5E8`, from
RaceBase::vf30) clears it and posts by start type. The GT race driver
(`0x132BA0`) runs the pre-race quick menu (`0x12E2C8`, `qm_gt_event`) first
and only then the race's run (vtable +0x150, between the condition copies
`0x12EE78` / `0x12EED8`); the quick menu stops the BGM when it closes (§3).
So a race entered through the pre-race menu (and, M, a retry, which runs the race again) plays
its start jingle at the start of its hold, after the menu's stop, exactly as a
race started directly.
No table indexed by finishing place exists: the finish handler
(`RaceDisplay` slot 17) checks the place only in the 2P display modes, and
the result screens and quick menus play the same "result" music for every
place (§3). Neither the GT-mode / arcade split, the difficulty nor the
championship round changes the race cues.

License tests (RaceLicenseBGM) turn the race-music request (0x14) into a
stop: **license runs have no race tracks**; the start jingle (it stops at
the signal) and the medal / fail jingles play.

After the race: the jingle (or the race track) is replaced by the quick
menu's music when the quick menu opens (§3); nothing else changes in
between (no fade).

### 2.5 Replays and demonstrations [C]

Every race set-up (`RaceBase::vf9`, live runs, every replay kind: the automatic replay after a
run, the REPLAY item, Replay Theater files and demonstrations, and the attract demo) arms the
start sequence (`fn_0017A578`: start request +0x50 = 1, the event's start type +0x1F3 at +0x54
(then replaced by the conductor's class: 0 -> -1, 2 -> 3, 3 -> -1; 1 keeps it), its countdown
+0x1F2), and `RaceBase::vf30`, the per-frame update of live runs and replays alike, runs it
(`fn_0017A5E8`, jump table 0x327640 by type + 1): type 1 the 4 s hold with 0x1B "jg04e", type 2
the 10 s hold with 0x12 "jg11", type -1 0x14 at once, types 0 / 3 nothing at set-up; at the
hold's end event 4, the start signal, whose handler (`RaceBase::vf33`) posts 0x14. The race
events go to the music object through `RaceBase::vf28` (type 0x14: -2 pause, -3 resume, -1
stop, else `RaceBGMPS2::vf5`) with no play-kind test. So a replay plays its run's start: the
start jingle over the hold, then the next playlist song at the signal; the attract demo's 0x14
becomes demo_a01 (§2). Levels as §1.1: race songs 0.7 outside live play (`race+0x8000+0x1784`
!= 0), jingles 1.0, the switch replay BGM. The finish jingle follows the race display's car
(above): in a replay the car the cameras follow when it crosses the line after its laps (a car
that finished while another was followed posts nothing later), once per replay.

Footage (`pcsx2/orig-replays.mp4`, onset cross-correlation of the clip's audio with the decoded
streams): replay 1 (an arcade standing start at Super Speedway, first frame ~27.6 s): "jg11"
from 27.86 s, `race_a09` from 38.14 s; the live race 2: "jg11" from 284.59 s, `race_a10` from
294.87 s; replay 2: "jg11" from 414.68 s, then `race_a14` (weak at its quiet start, clear after
the REPLAY OPTIONS pause). The song follows the jingle by 10.28 s in all three: the 600-frame
hold. Levels against the decoded streams: "jg11" 1.18 / 1.32 / 1.18 (replay 1, live, replay 2:
equal), the race song 0.84 in replay 1 against 0.48 live (lower quartiles; 0.7 / 0.5 expected,
the engines add to the live figure's noise). Both footage runs were left from the pause before
their finish, so the replayed finish jingle is from the code only.

Ours: `RaceScreen::music_cues` holds in replays as live (`c.holding` no longer excludes
replays), and a field replay's finish is `race::FinishCue` on the followed car.

## 3. Quick menus after (and before) races

Each quick menu plays its music with "play if different", looping, and
stops the BGM when it closes. The quick-menu runner (0x12E2C8) asks
through the menu path (0x1D27F0): level 0.8 and the **menu BGM** switch,
not the race's:

| quick menu | music |
|---|---|
| `qm_arc_result`, `qm_arc_2p_result` (arcade / 2P race result) | 0x16 "result" |
| `qm_gt_result`, `qm_gt_result_champ` (GT race / championship result) | 0x16 "result" |
| `qm_arc_tt_free`, `qm_arc_tt_ltd` (arcade time trial) | 0x15 "shortmenu-2" |
| `qm_gt_event`, `qm_gt_event_champ` (GT pre-race) | 0x15 "shortmenu-2" |
| `qm_gt_license` (after a license test) | 0x15 "shortmenu-2" |
| `qm_gt_mt_400/_1000/_max` (machine test) | 0x15 "shortmenu-2" |

## 4. Presentations (GT mode and arcade)

| screen | music | notes |
|---|---|---|
| license card (`license_get`, LicenseGetControl) | 0x21 "champ-2" | loops; stopped when the screen closes |
| championship trophy (`trophy_get`, TrophyGetControl) | 0x18 "champ-3" | loops; stopped on close |
| prize car (`car_get`, CarGetControl) | 0x19 "getacar" | **plays once**; stopped on close |
| GT-mode trophy (GTmodeTrophyControl, mini prize) | 0x17 "jg05" | loops; stopped on close |
| arcade trophy (ArcadeTrophyControl, page "arcade_mini") | 0x17 "jg05" | loops; stopped on close |
| consolation prize (ConsolationControl, "consolation_prize") | 0x17 "jg05" | **restarted** even if playing; stopped on close |
| endings (`stream/ending_short_full.pss`, `_long_full.pss`) | the movie's own audio | |

## 5. Menus

Menu pages pick their music by page (see docs/specs/menus.md §6 and
`page_music` in game_flow) with "play if different" at level 0.8; the
Replay Theater page (`replay_theater`, 0x1C4D88 at 0x1C4F24) plays stream
0x1A (str91a, the "Are You Gonna Go My Way" remix), index 0, looping; the
arcade sub-menus
use arcade01 (top), timetrial01, free01, battle2p01 (H for the ids,
`ArcadeControl`). Returning from a race: the quick menu stopped the BGM,
so the next menu page starts its sequence from the beginning; pages that
share a sequence then keep it running.

The page-music rule (0x1C4D88) runs only when a page is built at level 0 (page manager +0xAE4
= 0 and +0xAFC = 0, at 0x1B7594 in the page build 0x1B7268). It keeps the last id at +0xAF8
and acts only when the new id differs; its default, for a name that matches no test, is 0x23
(stop, 0x22BA18). In the GT-mode menu the one go-to (item kind 0) into such a page is
"Purchase?"'s YES (`00cmn/cd_buy_sure` → `00cmn/cd_bought`), so buying a car stops the dealer
music; "Get in now?" opens as a popup over it (no rule), and the next `cd/` page asks for
dealer01 again, which starts from its beginning. Checked in PCSX2 (the music cut as
"purchased" opens) and in RAM (+0xAE4 = 0, +0xAF8 = 0x23 on "purchased"; 1 and 2 on
"Purchase?").

## 6. Not found

No caller plays 0x1C "jg01" as BGM in the program, nor the sequence 0x0C option01
(the option pages' page music is stop, menus.md §10). (0x1A is the Replay
Theater page's music, §5; 0x12 "jg11" and 0x1B "jg04e" are the start
jingles, §2.)

## 7. The songs' markers (the replay cuts)

Every `ads.inf` record's fifth word is the offset of its **info block** (docs/FORMATS.md):
sample rate, length in samples and 32 **marker tracks** (sample positions, ascending; the
block's offsets are relocated at load, `fn_0022AA78`). On the US disc only the 25 race songs
and `demo_a01` have markers, in tracks 1..4: track 1 every beat (race_a01 0.453 s, 612
markers), track 2 the phrase starts (every 4 or 8 beats), tracks 3 and 4 the same list of
hand-placed cut points (phrase starts plus extra hits and bursts; gaps 0.05 .. 9.7 s, the
first between 0.0 and 4.1 s; race_a01 track 3: 1791, 162047, 322560, ... = 0.041, 3.675,
7.314 s; demo_a01 88 markers). Tracks 0 and 5..31 are empty everywhere, so the events 5..9 the
camera code tests never fire.

**Current song** (`RaceBGMPS2+0x08`) [C]: the record of the race song playing (event 0x14,
also 0x14 turned into 0x22 in the demo race: demo_a01's record); 0 for any other request
(jingles, medal, fail, 0x22 asked directly) and after a stop. Every play and stop clears the
cursors, intervals and events and the last stream id (`fn_0017DDB0`).

**Per frame** (`RaceBGMPS2::vf2` 0x17DDF0, before the simulation and the cameras) [C]: now =
the IOP stream position (µs) / 1e6 + 1/30 s; for each track with markers: t = marker[cursor] /
rate; t < now: event, cursor + 1 (one marker per track per frame); past the last marker t =
the first + the length (as if the song looped); a new stream id (a new song) fires every
marked track once with cursor 0 and t = the first marker; interval = t - now (FLT_MAX for a
track without markers, slightly negative on the frame a marker fires). A marked song that
ended starts the next playlist song (except in the demo). The camera manager reads the
intervals (vtable +0x1C) and events (+0x24): presentation §G2a.

Ours: `sound::MadsIndex::parse_markers`, `bgm::Player::current_track` / `generation`,
`race::MusicMarkers` stepped once per 60 Hz camera frame (`RaceMusic::step_markers`) at
`AudioEngine::music_audible_position` (the decoded position less what is still queued for the
device) + 1/30 s. Not settled [?]: the IOP status holds position -1 right after a play request
until the sound thread polls it; a double event in the first frame or two is not reproduced.
