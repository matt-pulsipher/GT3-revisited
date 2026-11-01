# Your music

Your own MP3, FLAC and WAV files can play as race songs, in place of the
disc's or mixed with them. They play from where they are: nothing is copied,
converted or uploaded.

## Quick start

1. **Settings → Audio → Music folder**: choose the folder your songs are in
   (album folders inside it are fine). Or use **Open music folder** and copy
   your songs into the game's own `music/` folder.
2. **Songs**, on the same page, shows how many the game found.
3. On the title menu, open **Options → Favorite Music List** and choose the
   **Custom** tab (only your songs) or **Both** (yours and the disc's).
4. Race. On the pause screen, **L1 / R1** skip to the previous / next song.

## Adding songs

**Windows, macOS, Linux.** **Music folder** opens a folder dialog: pick a
folder you already have (your Music folder, say). **Open music folder** shows
the game's own `music/` folder, in the [user data
folder](../README.md#user-data-folder), in the file manager. **Use the game's
music folder** goes back to it after you chose another one.

**Android.** Either:

- **A folder on the device or its memory card** (Music, Download, ...):
  **Music folder** opens Android's folder picker; allow music access when the
  game asks. Folders inside cloud drive apps can't be used; download the songs
  to the device first. If you chose Don't allow, **Allow music access** opens
  the game's page in Android's settings: Permissions → Music and audio → Allow.
- **The game's own folder**: copy songs into **GT3 Revisited > music** with
  the Files app (**Open music folder** opens it there). No permission is needed.

**iPhone and iPad.** Either pick a folder with **Music folder** (On My iPhone,
iCloud Drive or another location in Files), or copy songs into **On My iPhone
› GT3 Revisited › music** with the Files app, or from a computer in Finder
(the device's Files tab).

**New songs** are found when the game starts, when it comes back to the
front, and with **Look for new songs** (Settings → Audio).

## Choosing what plays

The game's **Options → Favorite Music List** gets three tabs once the game has
found songs of yours:

| Tab | Plays |
|---|---|
| **Original** | the disc's race songs, as GT3 plays them |
| **Custom** | only your songs |
| **Both** | your songs and the disc's: **Order** takes turns (a disc song, then one of yours), **Shuffle** draws from all of them |

Reach the tabs with **Up** from the first song, or **L1 / R1**. Each tab's list
works like GT3's own:

- **Cross on a check mark**: takes the song into the list or out of it.
- **Cross on a title**: picks the song up; **Up / Down** move it; **Cross**
  puts it down.
- **Order / Shuffle**: at the bottom of the list, and at the right of the tab
  row.
- **L2 / R2** jump a screen of songs at a time; a drag or the mouse wheel
  scrolls.

**Settings → Audio → Next race starts with**: **The same song** (default) starts
each race, restart or replay with the song the last one ended on; **The next
song** takes a new song every time, as GT3 does.

## Skipping songs

While a race song plays (in races and their replays, not license tests or
the title screen's demo):

- **The pause screen** shows the song's title and artist, with **L1 / R1** to
  skip. The new song starts when the race goes on. Settings → Audio → **Song on
  the pause screen** turns this off.
- **While driving**: give **Next song** and **Previous song** a button
  combination in Settings → Controls → **Player 1 song controls** (or Player
  2's; none by default), for example Select + R1. See [INPUT](INPUT.md#song-controls).
- **Touch**: the Previous / Next song buttons are hidden at first; show them in
  Settings → Touch → **Customize layout**.

Previous goes back through the songs already played; Next then goes forward
through them again before new ones come from the list. The skips work with the
disc's songs too.

## Volume

Your songs play as loud as the disc's race songs: the game turns each one up
(12 dB at most, less where it would clip) or down (20 dB at most). It measures
a song the first time it plays (it eases to its level a moment after it
starts), and the next one in your list while it plays, and remembers both.
**Music volume** (Settings → Audio) sets yours and the disc's.

## Files

- **Formats**: MP3, FLAC and WAV. AAC (`.m4a`), Ogg, Opus, WMA and others don't
  play; **Songs** in Settings counts the files it skipped.
- **Titles and artists** come from the files' tags (ID3, FLAC comments, WAV
  INFO), otherwise from the file name.
- **Folders**: every folder inside is searched (32 levels deep at most), linked
  folders too; names starting with `.` are skipped. Up to 10,000 songs (the
  first ones in name order).
- **Moving an album's folder** keeps its songs' check marks and places.
- **A folder that can't be read** (a drive unplugged, access taken back): your
  list is kept for when it's back.
- **Replays** play your songs too; in the Video view mode, the camera cuts
  follow a steady beat rather than the song.
- The game keeps `music_playlist.txt` (your lists) and `music_songs.txt` (the
  songs found and how loud each is) in the user data folder; `[music]` and
  `[options]` in `config.ini` hold the settings ([README](../README.md#settings)).

## Troubleshooting

| Songs says | |
|---|---|
| None yet | no MP3, FLAC or WAV files in the folder: add some, then Look for new songs |
| Folder not found | the folder was moved or deleted, or its drive is unplugged |
| Music access is off | Android: Allow music access |
| *n* files skipped | files in other formats, or damaged ones |
| (the first ones kept) | more than 10,000 songs: only the first 10,000 are listed |

No tabs in the Favorite Music List: the game hasn't found songs yet (see
Songs); the disc's songs play.
