# BDMusic: MIDI -> Black Desert composer song

A console program that turns a MIDI file into a song the in-game composer can open and play.
Everything the MIDI sets is used unless you override it with an option: tempo, time signature,
instruments, velocities, volume, pedals, bends, effects and the song name.

Created by flockenberger.

## Convert

    BDMusic "song.mid"

- Writes into `Documents\Black Desert\Music\` (the game's song folder), or next to the MIDI if that folder doesn't exist.
- The song is named after the MIDI file (without `.mid`). Use `--name "My Song"` for another name.
- An existing song with the same name is only replaced with `--force`.
- The author defaults to a placeholder. Set your own with `--author "YourName" --user-no <your user number>`.

## Look first

    BDMusic --parts "song.mid"        what the MIDI sets, its parts (numbered) and the instrument each one goes to
    BDMusic --instruments             composer instruments: numbers, note ranges, articulations
    BDMusic --dump "<song file>" 20   what a saved song holds, with the first 20 notes of each track
    BDMusic --version                 the program's version

## Options

| Option | Effect |
|---|---|
| `-o, --out <path>` | file or folder to write to |
| `--name <text>` | song name (= file name) |
| `--force` | overwrite an existing song |
| `--author <text>`, `--composer <text>`, `--user-no <n>` | author, last composer, user number |
| `--part <i>=<instrument>` | play part *i* (from `--parts`) on that instrument ([ids](#instruments-and-articulations)); `none` drops the part |
| `--program <gm>=<instrument>` | same for every part using that General MIDI program (0-127) |
| `--articulation <i>=<id>` | every note of part *i* gets that articulation ([ids per instrument](#instruments-and-articulations)) |
| `--no-articulations` | plain notes only |
| `--plain-defaults` | notes the MIDI implies no technique for stay Sustain (no Short or Mute for staccato, no clarinet/horn dynamics, no pedal on unpedalled piano) |
| `--transpose <±semitones>` | shift everything except drums |
| `--bpm <n>`, `--beat 3\|4\|6` | override the tempo and time signature |
| `--trim` | start at the first note |
| `--no-sustain` | ignore the sustain and sostenuto pedals |
| `--no-dynamics` | velocities as written, without expression or volume changes |
| `--same-off-velocity` | off velocity = on velocity, the way the composer writes it |
| `--volume <0-100>` | every track's volume |
| `--reverb`, `--delay`, `--chorus <0-100>` | every track's effect sends |
| `--reverb-time`, `--delay-feedback`, `--chorus-feedback`, `--chorus-depth`, `--chorus-rate <0-100>` | the song-wide effect settings |

## Examples

    BDMusic "Bad Apple.mid" --name "Bad Apple" --author "YourName" --user-no 123456
    BDMusic "song.mid" --part 2=16 --part 5=none      part 2 on Advanced Harp, drop part 5
    BDMusic "song.mid" --program 0=17                 every acoustic grand piano part on Advanced Piano
    BDMusic "song.mid" --articulation 3=5             part 3 as pizzicato
    BDMusic "song.mid" --transpose -12 --trim --bpm 90

## Instruments and articulations

These ids are what `--part` / `--program` (the instrument) and `--articulation` (the technique) take,
e.g. `--part 0=18 --articulation 0=5` plays part 0 on the Advanced Violin as pizzicato.
`BDMusic --instruments` prints the same list.

- **Notes** are MIDI key numbers (60 = middle C). Notes outside an instrument's range are moved by octaves until they fit.
- **–** means the instrument only plays plain notes, so `--articulation` does nothing for it.
- If you ask an instrument for an articulation it doesn't have, it plays its default (the first one listed).
- On the drum instruments, each note picks a different drum sound.

| Id | Instrument | Notes | Articulations |
|---|---|---|---|
| 0 | Acoustic Guitar | 12-119 | 0 Sustain |
| 1 | Flute | 12-107 | 0 Sustain |
| 2 | Recorder | 36-83 | – |
| 4 | Hand Drum | 60-79 (each note a sound) | – |
| 5 | Piatti Cymbals | 60-71 (each note a sound) | – |
| 6 | Harp | 36-95 | – |
| 7 | Piano | 12-107 | – |
| 8 | Violin | 36-83 | – |
| 10 | Advanced Acoustic Guitar | 36-88 | 0 Sustain, 3 Slide Up, 12 Slide Down, 13 Mute, 14 Harmonics, 15 Triplet |
| 11 | Advanced Flute | 48-88 | 0 Sustain, 1 Tab, 2 Cut, 3 Slide Up, 4 Short, 15 Triplet |
| 13 | Advanced Drum Set | 48-64 (each note a sound) | – |
| 14 | Advanced Electric Bass | 28-64 | 0 Sustain, 3 Slide Up, 12 Slide Down, 13 Mute, 14 Harmonics, 16 Glissando, 22 Slap, 23 Gliss Up, 24 X-notes |
| 15 | Advanced Contrabass | 28-64 | 0 Sustain, 3 Slide Up, 12 Slide Down, 13 Mute, 14 Harmonics, 23 Gliss Up |
| 16 | Advanced Harp | 12-90 | 0 Sustain, 9 Chord Major, 10 Chord Minor, 16 Glissando |
| 17 | Advanced Piano | 12-107 | 0 Sustain, 11 Sustain Pedal |
| 18 | Advanced Violin | 43-88 | 0 Sustain, 1 Tab, 2 Cut, 3 Slide Up, 4 Short, 5 Pizzicato, 6 Tremolo, 7 Trill Minor, 8 Trill Major |
| 19 | Advanced Hand Pan | 45-88 | – |
| 20-35 | Synths: 20 Sine, 21 Sine Stereo, 22 Sine Super, 23 Sine Super Octave; 24-27 Triangle, 28-31 Square, 32-35 Saw (same four each) | 12-100 | 0 Sustain, 1 Tab, 2 Cut, 3 Slide Up, 4 Short, 5 Pizzicato, 6 Tremolo, 7 Trill Minor, 8 Trill Major, 17 Vibrato, 18 Marcato, 19 Filter Sustain, 20 Filter Brassy, 21 Filter Pluck |
| 36 | Electric Guitar Clean | 24-95 | 0 Sustain, 6 Tremolo, 13 Mute, 14 Harmonics, 25 FX (C2 - G2) |
| 37 | Electric Guitar Drive | 24-95 | 0 Sustain, 6 Tremolo, 13 Mute, 14 Harmonics, 25 FX (C2 - G2) |
| 38 | Electric Guitar Distortion | 24-95 | 0 Sustain, 6 Tremolo, 13 Mute, 14 Harmonics, 25 FX (C2 - G2) |
| 39 | Advanced Clarinet | 24-95 | 0 Sustain, 4 Short, 7 Trill Minor, 8 Trill Major, 15 Triplet, 26 SusPiano, 27 SusMezzoForte, 28 SusForte |
| 40 | Advanced Horn | 24-95 | 0 Sustain, 3 Slide Up, 4 Short, 12 Slide Down, 26 SusPiano, 27 SusMezzoForte, 28 SusForte |

## Good to know

- **Articulations:** MIDI has no field for them, so they come from what the MIDI implies:
  - technique sounds (muted guitar, harmonics, slap bass, tremolo/pizzicato strings)
  - bends of a semitone or more -> Slide Up / Slide Down
  - modulation -> Vibrato
  - sustain pedal -> Sustain Pedal

  Notes the MIDI implies nothing for get what a player would use:
  - staccato (an eighth note or shorter, let go well before the part's next note) -> Short, or Mute on the drive and distortion guitars
  - Advanced Clarinet and Advanced Horn -> SusPiano, SusMezzoForte or SusForte by velocity (below 64, 64-95, 96 and up)
  - Advanced Piano, when the MIDI never uses the sustain pedal on that part -> Sustain Pedal

  Everything else plays **Sustain**. `--plain-defaults` turns off this second list, and `--articulation` sets one articulation for a whole part, using the ids in the table above.
- Drums (channel 10, bank 127 or GS rhythm parts) go to the drum set or hand drum.
- Above 200 bpm the composer shows 200, but the notes still play at their real times.
- The composer lets you write up to 10,000 notes per track. The converter warns when a track has more (the file itself can hold them).

## Building

> [!IMPORTANT]
> **`IceKeyHelper.h` and `IceKeyHelper.cpp` (in `Modules/BDMusic/Source/Crypt/`) are not part of this repository.**
> Black Desert encrypts its song files with ICE, and these two files hold the game's key, which is not shared here.
> BDMusic does not build without them. If you have the key, create both files yourself:
>
> ```cpp
> // IceKeyHelper.h
> #pragma once
> #include "Core/Base.h"
>
> extern const flib::UInt8 BDO_ICE_KEY[8];
> ```
>
> ```cpp
> // IceKeyHelper.cpp
> #include "IceKeyHelper.h"
>
> const flib::UInt8 BDO_ICE_KEY[8] = { /* the 8 key bytes */ };
> ```

1. Run `Setup-Windows.bat`. It generates `BDMusic.sln` for Visual Studio 2022 with the bundled premake.
2. Open `BDMusic.sln` and build `Release` (x64, C++20).
3. The program is written to `Binaries/windows-x86_64/Release/BDMusic/BDMusic.exe`.

## License

Copyright (C) 2026 flockenberger

BDMusic is free software under the [GNU Affero General Public License v3.0](LICENSE). You may use, change and
share it, but whatever you build on it has to stay under the same license, with its source available to
everyone who gets it, or who uses it over a network, and with this copyright notice kept. Songs you
make with BDMusic are yours; the license covers the program, not what it writes.

Parts that come from others keep their own licenses:

- `Modules/BDMusic/Source/Midi/`: [midifile](https://github.com/craigsapp/midifile) by Craig Stuart Sapp, BSD-2-Clause (see `COPYRIGHT.md` in that folder)
- `Modules/BDMusic/Source/Crypt/IceKey.*`: the ICE cipher, after Matthew Kwan's public-domain implementation
- `Vendor/Binaries/Premake/`: [premake](https://premake.github.io), BSD-3-Clause (see its `LICENSE.txt`)
