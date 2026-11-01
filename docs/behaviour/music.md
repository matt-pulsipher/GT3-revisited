# Music: selection and playback rules

This document describes which music the engine plays, when it starts, stops
or changes, and how a request becomes sound. Code comments cite its sections as
`music §n`. The voice mixer, the sequencer and the other
sound machinery are described in [sound.md](sound.md); the file layouts of
`bgm/ads.inf`, `music/music.inf`, `music/music.seq` and `music/music.ins` are
in [../formats/sound.md](../formats/sound.md) and [../FORMATS.md](../FORMATS.md).

Code: `src/audio/bgm.*` (the BGM layer), `src/audio/audio_engine.*` (output),
`src/race/race_audio.*` (`RaceMusic`), `src/race/race_playlist.*`
(`RacePlaylist`), `src/race/music_markers.*` (`MusicMarkers`, §8),
`src/formats/sound/mads.*` (the songs' marker tracks), `src/app/screen_race_audio.cpp`
(`RaceScreen::music_cues`), `src/app/screen_race_post_run.cpp`
(`RaceScreen::start_auto_replay`, the replay after a run), `src/menu/flow/game_flow.cpp` (`page_music`),
`src/menu/flow/music_list_page.*` (`MusicListPage`, the Options music list),
`src/app/screen_gt3menu.cpp` (applying menu music requests),
`src/app/screen_jukebox.cpp` (§9). Tests:
`sound_bgm_ids`, `sound_race_music_cues`, `sound_race_music_start_after_quick_menu`,
`sound_race_music_replay_start`, `sound_race_music_level`,
`sound_mads_markers_synthetic`, `sound_mads_markers_disc`,
`sound_music_request_levels`, `sound_music_and_effects_trim`, `sound_race_playlist`,
`sound_replay_theater_music` (`tests/test_sound_race.cpp`), `race_music_markers`
(`tests/test_race_presentation.cpp`), `sound_seq_music_bus_and_level`
(`tests/test_sound_seq.cpp`), `tests/test_music_list.cpp`.

There are two kinds of music on the disc:

- **Streamed music**: `.ads` files under `bgm/` (PS-ADPCM, decoded as they
  play). The race songs, the jingles, the result and quick-menu music and the
  presentation music are all streams.
- **Sequenced music**: the menu music, nine sequences in `music/music.seq`
  played note by note with the instruments of `music/music.ins` (the
  sequencer, [sound.md §4](sound.md#4-sequencer)).

Only one piece of background music plays at a time: starting a stream stops
the sequence and starting a sequence stops the stream.

## §1 The BGM layer

Race music, the menu's streamed pages and the music list's preview ask for
music through one object, `bgm::Player` (owned by `AudioEngine` and shared, so
that "is this the same piece?" sees a single state). The menu pages'
sequences (§4.1), the Settings preview (§6) and the jukebox (§9) start their
music on `AudioEngine` directly. A request is a **BGM id**, an
optional **index**, a **loop** flag and a **level** (§6).

### §1.1 BGM ids

| Id | Kind | What it selects |
|---|---|---|
| 0x00..0x11 | sequence | the `music.inf` entry whose name is the id's entry in the alphabetical name table (below) |
| 0x12..0x22 | stream | group `id - 0x12` of `bgm/ads.inf` (17 groups on the US disc) |

Sequence ids, in order: 0 `arcade01`, 1 `battle2p01`, 2 `dealer01`,
3 `free01`, 4 `gorace01`, 5 `gtmenu01`, 6 `home01`, 7 `license01`,
8 `license02`, 9 `machtest01`, 10 `main01`, 11 `mini01`, 12 `option01`,
13 `theater01`, 14 `timetrial01`, 15 `tune01`, 16 `wash01`, 17 `wheelshop01`.
Several names share one sequence (for example `arcade01`, `gorace01` and
`main01` are all sequence 6), see the table in
[../formats/sound.md](../formats/sound.md) "MSEQ music index".

Stream ids and what the engine uses them for (the titles are the working
titles stored in `bgm/ads.inf`):

| Id | Name in code | File / title | Used for |
|---|---|---|---|
| 0x12 | `kStartLong` | `str01a` "jg11" | standing start with the long hold (start type 2), §2.2 |
| 0x13 | `kFinish` | `str02a` "jg12" | the finish jingle, §3.1 |
| 0x14 | `kRace` | `race_a01` .. `race_a25` | the race songs, §2.3; their intros in the Options music list, §4.4 |
| 0x15 | `kQuickMenu` | `str10a` "shortmenu-2" | pre-race, time trial, license and machine test quick menus, §3.3 |
| 0x16 | `kResult` | `str05a` "result" | race result quick menus, §3.3 |
| 0x17 | `kMiniPrize` | `str14a` "jg05" | arcade trophy, GT-mode trophy and consolation prize pages, §4.2 |
| 0x18 | `kTrophy` | `str12a` "champ-3" | championship trophy presentation, §4.2 |
| 0x19 | `kCarGet` | `str13a` "getacar" | prize car presentation (plays once), §4.2 |
| 0x1A | `kReplayTheater` | `str91a` "Are You Gonna Go My Way" (GT remix) | the Replay Theater page, §4.1 |
| 0x1B | `kStartShort` | `str03a` "jg04e" | standing start with the short hold (start type 1), §2.2 |
| 0x1C | `kJg01` | `str04a` "jg01" | no caller known |
| 0x1D | `kLicenseGold` | `str06a` "jg06" | license gold medal, §3.2 |
| 0x1E | `kLicenseSilver` | `str07a` "jg05" | license silver medal, §3.2 |
| 0x1F | `kLicenseBronze` | `str08a` "bronze" | license bronze medal, §3.2 |
| 0x20 | `kFail` | `str09a` "fail" | license failed, time trial voided, §3.1 |
| 0x21 | `kLicenseGet` | `str11a` "champ-2" | license card presentation, §4.2 |
| 0x22 | `kDemo` | `demo_a01` | the attract-mode race, §2.3 |

`gt3tool bgm` lists the groups' files, titles and artists from the disc.

### §1.2 Choosing the track within a stream group

For a stream id the index picks the track within the group:

- index 0..n-1 plays that track;
- index -1 (or any index past the end of the group) picks a track **uniformly at
  random** from the group. Jingles and the demo track are requested this way
  (their groups hold one track each); the race songs are not: they take
  their index from the race playlist (§2.3);
- index with bit 0x1000 set plays the **intro** variant of the track, the
  file `bgm/i_<file>` instead of `bgm/<file>` (the Options music list's
  preview, §4.4).

If the chosen file does not exist, the music stops and nothing plays
(`bgm::Player::play` logs a warning). Mods can replace any `.ads` file
through the VFS; the replacement plays wherever the disc's file would.

### §1.3 Play and play-if-different

There are two ways to ask for music (`bgm::Player::play`,
`bgm::Player::play_if_different`):

- **Play** always starts the piece from its beginning, even if the same piece
  is already playing.
- **Play if different** keeps the current music when it is "the same piece":
  the same index (not random, not an intro request), the same kind (sequence
  or stream), music still sounding, and the same identity (for sequences the
  sequence number, so two names sharing a sequence count as the same piece;
  for streams the file path). Otherwise it behaves like Play. A piece that
  is kept keeps the level it was started at.

Quick menus (§3.3), the menu's streamed pages (the Replay Theater, §4.1, and
the presentations, §4.2) and the music list's preview (§4.4) use
play-if-different; menu pages have their own rule (§4.1). The other race
cues use Play (§2, §3). Since an intro request never counts as the same
piece, every preview request starts its intro from the top.

**Stop** stops both the stream and the sequence at once; there are no fades.
A sequence that is stopped lets its sounding notes enter their release phase
(the instruments' own release, a fraction of a second), so it does not cut
off with a click.

### §1.4 Looping

- **Sequences always loop.** Every sequence ends with an endless loop
  (see [sound.md §4.3](sound.md#43-tracks-and-events)): the part before the
  loop start plays once (up to about 16 s; under 1.5 s in five of the nine),
  then the loop repeats.
- **Streams loop or play once** according to the request's loop flag. A
  looping stream starts again from the beginning of its file when it reaches
  the end (the loop points stored in the `.ads` header are not used). A
  stream played once ends; the BGM layer then reports "not playing", which
  is how the race music knows to start the next song (§2.3).

## §2 Race music: the start and the race songs

The race screen decides its music once per frame (`RaceScreen::music_cues`
fills a `RaceMusic::Cues`; `RaceMusic::decide` turns it into BGM requests,
which `RaceMusic::cues` applies). `decide` is a pure function of the cues,
whether music is currently sounding, and a little remembered state, which is
what the test `sound_race_music_cues` exercises.

### §2.1 Inputs

| Cue | Meaning |
|---|---|
| music on | live runs: the race's music switch and the options' **Race BGM** (the quick menu's OPTION → Music switches it during the visit); replays, demos and attract runs: the race's music switch and the replay settings' BGM (forced on in replay view mode 1) |
| menu music on | the race's music switch and the options' **Menu BGM**: the quick menus' music (§3.3) |
| replay | a replay of the run: race songs at the replay level (§6) |
| songs | the race playlist has a song in the list (§2.3) |
| kind | Race (one-player races and solo runs), TwoPlayer (2P battle), License (a license test), LicenseReplay (a license run replayed, or the disc's license demo), Demo (attract mode and demo races) |
| paused | the pause window is open |
| start type | the event's start type for races; the time trial's start type otherwise |
| holding | the run is before its start signal: a standing start's hold (race time below zero, or the time-trial hold), a rolling start's 300 frames of computer driving, a flying start's run-up to the line (not Demo); replays re-run it (§2.5) |
| finished | the player (or, in 2P, either player) has finished; in a replay of a one-player race, the car the cameras follow (§2.5) |
| failed | a license run without a medal (FAIL or a time slower than bronze; in a replay, the live run's; in a demo, its judged result), or a voided time trial |
| medal | the license medal shown, 0 gold, 1 silver, 2 bronze, none |
| since finish | seconds since the finish |
| menu | the quick menu on screen: none, Short or Result (§3.3) |

"Music on" does not change which requests are made (`RaceMusic::decide`
ignores it): with it off, every jingle, race song and demo track is still
requested, at level 0, so it plays silently. The race playlist therefore
advances (and is saved) as usual, and the songs' markers (§8) keep timing the
replay cameras' cuts. The level (`RaceMusicLevel`):

- A request plays at its kind's level (§6) while music is on, at 0 while it
  is off.
- Every frame the race's own stream (not a quick menu's) plays at the last
  request's level while music is on and at 0 while it is off, so switching
  it off silences the music at once and switching it back on brings it back.
- A request made while music was off stays at level 0, also after music is
  switched back on, and so does every later request, until the music is
  stopped (a quick menu closing, the start signal of a run without race
  songs, a replay starting, leaving the race); the next request after the stop plays at its
  own level.

The quick menus' music follows "menu music on" instead (§3.3).

### §2.2 The start jingle

At a **standing start**, the moment the cars start being held the engine
plays a start jingle once (not looping):

| Start type | Hold before the signal | Jingle |
|---|---|---|
| 1 | 4 s | 0x1B "jg04e" |
| 2 | 10 s | 0x12 "jg11" |
| 0 | the event's own (byte 0x1F2 of its record, in seconds; none in demo races) | none |
| 3 (rolling, flying) | their run-up (below) | none |

No start jingle plays in Demo runs. If the jingle ends before the signal,
the remaining hold is silent. At the **start signal** the jingle is replaced:
in races the first race song starts at once; in license tests (and license
replays) the music simply stops, because license tests have no race music.
It also stops when the race playlist has no song in the list (§2.3).

**Rolling and flying starts** have no jingle; their run-up holds the music
like a standing start's hold, in silence. A rolling start's signal is the
hand-over, after the computer has driven the player's car for 300 frames
(5 s): the first race song starts there. A flying start's signal (time
trials, flying license tests, free runs) is the start-line crossing that ends
the computer's run-up, where `count_s` sounds and START shows
(`RaceScreen::flying_start_signal`): in time trials and free runs the first
race song starts there; a license test stays silent. In Demo runs a flying
start holds nothing.

**After a quick menu.** A run entered through a quick menu (the pre-race
menu's Start, the result menu's Retry) is already being held when the menu
closes. On that frame the menu's music stops (§3.3) and, if the start type
has a jingle, the jingle starts at once, as at a direct start. Closing the
menu into a hold whose start type has no jingle (a rolling or flying start's
run-up) only stops the music; the race songs follow at the signal. Closing
it into a run with no hold stops the menu's music, and the first race song
follows on the next frame.

The start type comes from the event (`music_start_type_`, set from the
event's start record in `screen_race_setup.cpp`) and defaults to 2.

### §2.3 Race songs

After the start, whenever no jingle is pending, the cars are not being held
and nothing is playing, the engine plays the **next song of the race
playlist** (id 0x14, the song's track), **once**, at level 0.5 in a live run
and 0.7 in a replay (§6). When a song ends the next one starts the following
frame, for as long as the race goes on. This applies to Race and TwoPlayer
runs and their replays.

- License tests and license replays have no race songs.
- Demo (attract) runs play the demo track (0x22) once and then stay silent.
- With no song in the list there are no race songs; the jingles still play.

**The race playlist** (`race::RacePlaylist`) is the Options music list
(§4.4) kept as a playlist that carries over from race to race. It holds up
to 64 race songs, each identified by the id code of its file name
(`db::id_hash("race_a01.ads")`), with an "in the list" mark, a "played" mark
and its place in the current order, plus a cursor, a shuffle switch (Order /
Shuffle) and a 16-bit seed.

- **Next song.** The song whose place in the order is the cursor is taken
  and the cursor moves on; a song already played in this pass, or not in the
  list, is skipped. When the cursor passes the end (also right after the
  pass's last song) the order is rebuilt and the cursor starts again at 0.
  So one pass plays every listed song once.
- **Rebuild.** Every listed song becomes unplayed and every other song
  played. With Order the order is the list order. With Shuffle, for
  $i = 0..n-1$ a draw $k = \lfloor u\cdot n\rfloor$ ($u$ a unit draw of the
  game's generator seeded with the stored seed) gives place $i$ to song $k$,
  or to the first song from $k$ on (wrapping) that has no place yet; one more
  draw stores the next seed, $\lfloor u\cdot 32768\rfloor$. The same seed
  gives the same order; each pass gets a new one.
- **Track.** The song's track is the place of its id code in the race-song
  group (`RacePlaylist::track_index`); a song that is not on the disc plays
  track 0.
- **Stored.** The playlist is kept as text in `[options] music_playlist`
  and saved every time a race song is taken, so the next race goes on with
  the same pass. Without a valid stored list (none, or text that does not
  parse) the engine makes a new one: every race song on the disc in group
  order, all in the list, Shuffle on, seeded with an integer draw on the
  shared generator. The options menu stores such a new list as soon as it
  makes one. A stored order with no song at the cursor's place starts a new
  pass instead of waiting.

**The player's own songs (ours,** `race/custom_music.h`**).** MP3, FLAC and
WAV files in the player's music folder play as race songs too, chosen on the
Options music list's tabs (menus §10.1, `[options] music_source`):

- **Original**: the race playlist above, unchanged.
- **Custom**: a playlist of the same kind over the player's songs (up to
  10,000, kept in `music_playlist.txt` in the user data folder), with its own
  Order / Shuffle; songs added to the folder join at the end in the list,
  songs gone leave, and either starts a new pass. A song's id is a hash of its
  folder's name, its file name and its size, so a moved album folder keeps
  its songs' places and marks; a folder that can't be read (a drive
  unplugged) leaves the playlist as it is.
- **The scan** (`MusicLibrary::scan`, on a thread of its own at start, on a
  folder change, back in the foreground and on request): every folder in name
  order, linked folders too, each once (a link back up can't loop), 32 deep
  at most, names starting with `.` skipped, unreadable folders passed over,
  a file reached through two links once, files that don't open counted and
  left out, the first 10,000 songs in that order kept. A new scan asked for
  while one runs stops it and starts after it.
- **Both**: each next song comes from one of the two playlists. With Shuffle
  (`[options] music_mix`) the player's playlist is chosen with the chance
  of its songs still to come in its pass among all songs still to come;
  with Order the two take turns. A playlist with no song in its list is
  passed over.
- Without songs in the folder Original plays, whatever was chosen. The
  player's songs play at the race songs' levels, with fallback markers for
  the replay cameras (§8).
- **Loudness.** The player's songs are brought to the disc's race songs'
  loudness: integrated loudness after ITU-R BS.1770-4 (`audio/loudness.h`)
  of every disc race song (with mods), averaged, measured once on a thread
  of the library between races, and of each of the player's songs as it
  comes: when it starts, if it isn't measured yet (it plays as it is and
  eases to its level, 6 dB a second, once measured), and the next song of
  its list while one plays. A song plays at the average's level over its
  own, from -20 to +12 dB, and at most so far up that its loudest sample
  matches a full-scale disc song at the replay level, the loudest the game
  streams music at. The disc's songs play as GT3 plays them. The songs found and their loudness are kept in
  `music_songs.txt` (the disc's average with its songs' names and sizes),
  so the next start has them before its scan lands.
- **Next race (ours,** `[music] next_race`**).** With `same`, the default, a
  run (a race, a restart, the replay) starts with the race song the last
  run ended on, from its start (`[music] race_song`, saved whenever a race
  song starts, not by demos), instead of taking the next song; it is passed
  over when the source playing doesn't play it, it is out of its list, or
  its file is gone. With `next` every run takes a new song, GT3's rule.
- **Skips.** Next / Previous song (INPUT "Song controls") stop the song and
  play another, only while a race song is heard (not a jingle, the demo
  track, the quick menus' music, nor with the run's BGM switch off). The
  songs played are remembered: Previous goes back through them (the first
  one starts over), and Next or a song's end goes forward through them
  before the playlists give new ones. A skip on the pause screen starts the
  new song paused: it plays when the race goes on (§2.4).

### §2.4 Pause

Opening the pause window pauses the streamed music where it is; closing it
resumes it (`AudioEngine::pause_music` / `resume_music`). While paused no
start, finish or song cue is acted on (a quick menu opening still is). Starting a new piece always un-pauses the output, so
leaving a race from the pause menu does not leave the next music silent;
the one exception is ours, a song skipped to on the pause screen (§2.3),
which stays paused until the race goes on.

### §2.5 The replay after a run

Every run except a license test and the free practice before a GT race is
followed by its replay, which plays by itself, whether the run finished,
failed or was left with the pause menu's EXIT (after the prize page, arcade
win cup or name entry, if any; their music is §4.2's). Its music:

- **The race-exit transition** that ends the live run (its last frame held,
  then the gray wash) does not update the music: whatever was sounding (a race
  song, the finish jingle) goes on, and after a pause EXIT it stays paused.
- **The replay re-runs the start sequence.** When it starts, the run's music
  is stopped and its jingle state cleared (`RaceScreen::start_auto_replay`).
  It follows the replay settings' BGM (§2.1: off, its requests play at
  level 0). The replay holds like the live run (§2.1 "holding"): a standing
  start's hold plays the start type's jingle (§2.2); at the replayed start
  signal (the end of the hold, a rolling start's hand-over, a flying start's
  line crossing) the playlist's next song (§2.3) starts, at the replay level
  0.7 (§6), and songs follow one another as in a live run. A license run's
  replay stops the music at the signal, as the live test does.
- **The finish.** A one-player race's replay re-runs the field, and "jg12"
  (§3.1) plays when the car the cameras follow finishes (`race::FinishCue`):
  the car followed at the moment it crosses the line after its laps. A car
  that finished while another one was followed does not count when the
  cameras turn to it later; the jingle plays once per replay, and the music
  then stays silent. A 2P battle's replay plays "jg12" at the first
  finisher's finish. A time trial's, free run's or machine test's replay has
  no finish of its own, and its songs go on to its end.
- Only Start opens the replay's pause, which pauses the music (§2.4). At the
  replay's end, or at its pause's EXIT, the same transition runs over its last
  frame, again leaving the music as it is; then the quick menu opens and its
  music starts (§3.3).

The quick menu's REPLAY item plays the same way, except that closing the menu
stops its music (§3.3) before the replay's start sequence. After a
license run left with EXIT, or after a DEMONSTRATION, REPLAY is disabled
until the next run and plays nothing.

## §3 The finish, results and quick menus

### §3.1 Finish and fail jingles

On the frame the run becomes finished:

| Situation | Music |
|---|---|
| failed (a license run without a medal, live, replayed or demonstrated; a voided time trial) | 0x20 "fail", once |
| license test passed with a medal | silence; the medal jingle follows (§3.2) |
| any race or solo run, whatever the place | 0x13 "jg12", once |
| 2P battle | "jg12" when the first player finishes (the engine treats the first finisher as the winner) |
| a one-player race's replay | "jg12" when the car the cameras follow finishes (§2.5) |
| license replay / license demo of a run with a medal | "jg12" at the replayed run's end, no medal jingle |
| Demo | nothing |

The jingle replaces whatever was playing (the race song stops). After it,
the music stays silent until the next piece starts (a presentation's,
§4.2, the run's replay, §2.5, or a quick menu's, §3.3); no race song restarts.
If the run is restarted (finished goes back to false), the jingle state
clears and the race songs resume after the next start.

For a license replay, "finished" is the moment the replay reaches the live
run's end time (or the demo's judge ends the run). "Failed" is what the live
run played; for a demo, its own judged result: no medal (slower than bronze or
out of the course) plays "fail", a medal "jg12" (`race::license_fail_jingle`,
test `sound_race_licence_jingles`).

### §3.2 License medal jingles

After a license pass, once the medal is known and **37 frames (37/60 s)**
have passed since the finish (the end of the result screen's dimming), the
engine plays the medal's jingle once: gold 0x1D, silver 0x1E, bronze 0x1F.
It plays one per finish.

### §3.3 Quick menus

When a quick menu opens, its music starts looping (play-if-different, at the
menu level 0.8, §6):

| Quick menu | Music |
|---|---|
| race results (races and 2P battles that have a result) | 0x16 "result" |
| everything else: the pre-race menu, time trials, license tests, machine tests | 0x15 "shortmenu-2" |

The quick menus' music follows the options' **Menu BGM** switch ("menu music
on", §2.1), not the run's music switch: with Race BGM off a quick menu still
plays its music, and with Menu BGM off opening a quick menu stops the music
instead.

The race that runs behind the time trial, free run and machine test menus and
the GT race and championship pre-race menus (the last run replayed over and
over) adds nothing to it: no race songs, no jingles and no sound effects; only
the menu's music plays. The result menus have no race behind them.

Opening a quick menu clears the jingle state. Closing the quick menu
(menu goes back to none) **stops** the music; the next run's cues then start
again from §2, and when the run is already held the start jingle follows on
the same frame (§2.2).

## §4 Menu music

### §4.1 Music by page

Each time the menu opens a top-level page, the engine picks the page's music
(`page_music` in `src/menu/flow/game_flow.cpp`). The pages of the option set
play no music (the music list's preview, §4.4, is the only music there);
otherwise the rules are tested in this order and, where two rules match, the
later one wins:

| Page | Music (`music.inf` name) |
|---|---|
| any page of the option set | silence |
| `cd/...`, `00cmn/cd_ride_sure` (car dealers) | `dealer01` |
| `at/...`, `00cmn/ws_fit_sure` (car wash, wheel shop) | `wash01` |
| `hm/...` (home) | `home01` |
| `lc/...` (license center) | `license01` |
| `r/...`, `00cmn/race_info` (races) | `gorace01` |
| `replay_theater` | stream 0x1A "Are You Gonna Go My Way" (GT remix), looping |
| `ts/...` (tune shops) | `tune01` |
| `map` (the GT-mode world map) | `gtmenu01` |
| `mt/...` (machine test) | `machtest01` |
| `ar/...` (arcade) | `arcade01` |
| `root` (title) | silence |
| any other page | keep the current music |

The menu only asks for music when the page's music differs from the
current one, so moving between pages with the same name does not
restart the music. A different name starts its sequence from the top, even
when it shares the sequence (for example `arcade01` → `gorace01`). Sequences
play at the menu level 0.8 (§6). The Replay Theater's stream is requested
like a presentation's (§4.2, §5.2): play-if-different, index 0, menu level.
Starting a race clears the menu music; the race plays its own (§2).

### §4.2 Presentations

The prize and award pages play streamed music instead:

| Page | Music |
|---|---|
| `license_get` (license card) | 0x21 "champ-2", looping |
| `trophy_get` (championship trophy) | 0x18 "champ-3", looping |
| `car_get` (prize car) | 0x19 "getacar", **once** |
| `arcade_mini`, `gtmode_mini`, `consolation_prize` | 0x17 "jg05", looping |

These are requested with play-if-different, index 0, at the menu level 0.8
(§6). A presentation's
music **stops when the presentation closes**, before the next page or
presentation opens.

### §4.3 Menu BGM switch

With the options' **Menu BGM** off, menu music requests stop the music
instead of playing it. Switching it back on resumes the current page's
music. Like Menu SE, it applies at once. The same switch decides the race's
quick-menu music (§3.3). The music list's preview (§4.4), and the
presentations a race shows after its run (§2.5), play whatever the switch
says.

### §4.4 Options music list

The options' music list page (`option.mbl` page `music/music`,
`MusicListPage`; its keys and pointer handling are in
[menus.md §10.1](menus.md#101-the-music-list)) shows and edits the race playlist (§2.3): one row
per song in the playlist's list order, with its check mark and
"title / artist", then an Order / Shuffle row. The options' top page shows
the same list without a cursor and plays nothing.

- **Preview.** While the cursor is on a song that is in the list, that
  song's **intro** plays, looping (stream 0x14, the song's track with the
  intro flag, §1.2, at level 1.0). It starts when the page opens (the cursor
  starts on the first song) and starts again from the top each time the
  cursor reaches another listed song, or a song is checked back into the
  list. On a song out of the list, or on the Order / Shuffle row, the music
  stops. Moving between a row's check mark and its title does not restart
  it; a song lifted to be moved keeps playing while it is carried, and
  putting it down starts its intro again.
- **Leaving the page** (its Cancel) stops the music and keeps the edit: the
  songs in the page's order with their check marks, and the Order / Shuffle
  choice, replace the playlist's, which then starts a new pass (cursor 0, a
  new order) and is stored. With a song lifted the edit is dropped.

## §5 Playback

### §5.1 Streams

`AudioEngine::play_music` plays an `.ads` stream on its own output stream.
An uncompressed file from the disc image is read in small pieces as it plays
(about 10 MB per song is never loaded at once); a modded or compressed file
is read into memory first. The output takes the file's own rate and channel
count; the platform resamples. Its gain is set from the request's level
when it starts (§6). Position and length are reported in seconds of the
file.

### §5.2 Sequences

`AudioEngine::play_sequence` plays a `music.inf` name: it loads
`music/music.inf`, `music.seq` and `music.ins` on first use, finds the
name's sequence, stops any stream, sets the sequencer's music volume from
the request's level (§6) and starts the sequence from the top on the voice
mixer, whose **music bus** carries its voices
([sound.md §2.4](sound.md#24-volume-and-output)). The sequencer runs at the game's 60 updates per second, timed
from the mixer's own sample clock so its timing is sample-exact
([sound.md §4](sound.md#4-sequencer)). The reported length of a sequence is
one pass of its endless loop.

A request in the form `bgm:<id>` (or `bgm:<id>:once`) instead goes to the
BGM layer as a play-if-different request for stream `<id>`, index 0,
looping unless `:once` is given, at the request's level; this is how the
Replay Theater page (§4.1) and the menu's presentation pages (§4.2) ask for
streamed music.

## §6 Volume and options

Every music request carries a **level** (`bgm::k*Volume`):

| Level | Value | Requests |
|---|---|---|
| menu (`kMenuVolume`) | 0.8 | menu pages' sequences, the Replay Theater and presentation streams, the race quick menus |
| race song, live (`kRaceSongVolume`) | 0.5 | race songs in a live run |
| race song, replay (`kReplaySongVolume`) | 0.7 | race songs in a replay |
| jingle (`kJingleVolume`) | 1.0 | start, finish, medal and fail jingles, the demo track |
| — | 1.0 | the Options music list's preview (§4.4) |
| — | 0 | the race's requests while its music switch is off (§2.1) |

How the level becomes loudness depends on the kind of music:

- **Streams** play at an output gain of
  $\text{level}\cdot\text{Music volume}\cdot 0.5$
  (`bgm::stream_gain`, `AudioEngine::stream_output_gain`). The 0.5 is the
  voice mixer's output trim ([sound.md §2.4](sound.md#24-volume-and-output)),
  so a full-level stream and a full-level voice come out equally loud. For
  example a live race song at a Music volume
  of 0.8 plays at 0.5 · 0.8 · 0.5 = 0.2.
- **Sequences** take the level as the sequencer's music volume,
  $\min(\lfloor\text{level}\cdot 16384\rfloor, \mathtt{0x3FFF})$ in Q14
  (`bgm::sequence_volume`; 0.8 gives 13107). It enters each note's volume
  before the sequencer squares it ([sound.md §4.4](sound.md#44-notes)), so
  the menu level gives about 0.64 in amplitude. Their voices play on the
  mixer's music bus, whose gain is the Music volume, then through the
  mixer's output trim.

| Setting | Where | Effect |
|---|---|---|
| Music volume | Settings → Audio, `[audio] music_volume` (default 0.8) | the streams' gain (above) and the gain of the mixer's music bus; changes apply at once |
| Sound effects | Settings → Audio, `[audio] sfx_volume` (default 0.8) | the gain of the mixer's effects bus only: it never scales the music |
| Menu BGM | Options, `[options] menu_bgm` | §4.3; the race quick menus, §3.3 |
| Race BGM | Options, `[options] race_bgm` | §2.1, read when a race starts (the quick menu's OPTION → Music then switches it): off, the race's requests play at level 0 |
| Replay BGM | Options / REPLAY OPTIONS, `[options] replay_bgm` | §2.1 for replays, demos and attract runs: off, their requests play at level 0 |
| Music list | Options, `[options] music_playlist` | the race playlist, §2.3, §4.4 |

Changing the Music volume on the Settings → Audio screen while no music is
playing starts a preview, `gtmenu01` at the menu level, so the slider is
heard as in the menus; it stops when the screen or its Audio tab is left. Movies play their audio at the music volume
(`docs/dev/MOVIES.md`).

## §7 Approximations and open points

- Whether stream id 0x1C ("jg01") has a caller is not settled; the engine
  does not play it. No page (§4.1) asks for `battle2p01`, `free01`,
  `license02`, `main01`, `mini01`, `option01`, `theater01`, `timetrial01` or
  `wheelshop01`.
- There are no fades: stops are immediate (apart from the sequence notes'
  release), and the sequencer's fade-out is not reproduced.
- The menu page rules (§4.1) match pages by name prefix and meaning; they
  cover the pages the menus have, and other pages keep the music they find.

## §8 Song markers

The race songs carry **marker tracks** that time the replay cameras' cuts in
view mode 1 (Video; the cuts themselves are in
[presentation.md §G2](presentation.md#g2-view-modes), `race::CutClock`).

**On the disc.** Each `bgm/ads.inf` record may point to an info block (layout
in [../FORMATS.md](../FORMATS.md) "bgm/ads.inf"; `sound::MadsIndex::parse_markers`,
`sound::MadsMarkers`): the sample rate, the length in samples and up to 32
tracks of ascending sample positions. On the disc only the 25 race songs
and `demo_a01` have markers, all in tracks 1..4: 1 the beats, 2 the phrases,
3 and 4 the same list of cut points. `race_a01` has 612, 126, 205 and 205;
its cut points start at 1791, 162047, 322560 (0.041, 3.675, 7.314 s at
44100 Hz). `demo_a01` has 88 cut points. Jingles, the result and quick-menu
music and the presentation streams have none.

**Which piece.** `bgm::Player::current_track` is the record of the stream
now playing (none for a sequence, an intro, or nothing), and
`bgm::Player::generation` counts every play and stop, so a new piece, or the
same one started again, is told apart. The race music (`RaceMusic`) takes the
markers of a race song, or of the demo track in an attract run, that it
requested itself; a jingle, a quick menu's music, a stop or a missing file
leaves no markers. A race song playing at level 0 (§2.1) keeps its markers.

**Every frame.** Once per 60 Hz frame, before the cameras, live and in
replays (`RaceMusic::step_markers`, `MusicMarkers::step`), the markers follow
the song's **audible position** (`AudioEngine::music_audible_position`: the
decoded position less what still waits in the output stream and one device
buffer, never below 0). With $now$ = audible position + 1/30 s (two frames
ahead), for every track with markers:

- the next marker $t$ (the one at the track's cursor, in seconds) has passed
  when $t < now$: the track's **event** fires and the cursor moves on, at
  most one marker per track per frame (a jump past two fires one now and one
  on the next frame);
- the track's **interval** is $t - now$, the seconds to the next marker
  (slightly negative on the frame a marker fires); past the last marker the
  next one is the first plus the song's length, as if it looped;
- on a new piece's first frame every marked track fires once, the cursors go
  back to the start and the interval is that to the first marker.

Without a marked song (jingles, license runs, an empty playlist, silence)
no event fires and every interval is "none".

**Fallback markers (ours).** A race song without markers of its own (the
player's music, §2.3) gets generated ones (`race::fallback_markers`), so the
replay cameras keep cutting at the disc songs' pace instead of on their
2-second countdown (presentation §G14). The disc's race songs have a median
beat of 0.46 s, phrases of four beats (1.82 s) and cut points a median 1.46 s
apart (mean 1.56 s). The fallback is a steady 130 beats a minute from the first
beat after the song's start: track 1 every beat, track 2 every four beats, and
tracks 3 and 4 the same cut points 1, 2, 4 or 8 beats apart (15 / 25 / 45 / 15 %,
a mean of 1.69 s), drawn on the song's id so a song always gets the same ones.
They follow the song's length (30 minutes when the file doesn't say). They keep
a musical rhythm but aren't timed to the song itself.

## §9 Music jukebox

The home menu's **Music jukebox** (`src/app/screen_jukebox.cpp`, `gt3r
--screen jukebox`) lists every sequence of `music/music.seq` (one row each,
with the `music.inf` names that use it), then every stream of `bgm/ads.inf`
whose file exists, mods included. Cross plays the row: a sequence loops; a
stream plays once and the next row starts when it ends, wrapping round to the
first. Both play at full level (1.0, §6), outside the BGM layer and the Menu
BGM switch. Circle stops; Left / Right change the Music volume in steps of
0.05 (stored when Triangle leaves the screen, which also stops the music);
Square turns the mixer's reverb off or back to its boot setting.
