# Doom RPG N64 Port

A Nintendo 64 (libdragon) port of **Doom RPG**, plus a tool that turns your own
copy of the original BREW game into a runnable `.z64`.

No copyrighted game data is in this repository. You supply your own
`doomrpg.zip`; the tool decodes it and builds the ROM.


## The `doomrpg.zip` you need

The **BREW build of Doom RPG** — the 2005 mobile game by JAMDAT / id Software /
EA (BREW, *not* the Java/J2ME version). You must own it; it is not distributed
here.

The build accepts, dropped into `input/`:

* **`doomrpg.zip`** — how the game is usually found: a ZIP that contains
  `doomrpg.bar` (any casing) somewhere inside.
* **`doomrpg.bar`** — the raw BREW asset archive itself.

A valid `.bar` holds gzip members named `entities.db`, `sintable.bin`,
`palettes.bin`, `mappings.bin`, `bitshapes.bin`, `wtexels.bin`, `stexels.bin`,
every `*.bsp` map (`intro`, `junction`, `level01`…`level07`, `reactor`, `menu`,
…), the menu `*.bmp` images, `help.txt`, and ~95 PMD sound resources. The
builder fails early with a clear message if the file is not a Doom RPG `.bar`.


## What do you need

* **Python 3.8+** on your `PATH` (`python --version`).
* **Your own `doomrpg.zip`** (or `doomrpg.bar`) — see the section above.
* **`ffmpeg`** and **`fluidsynth`** on your `PATH`. They render the three BREW
  MIDI tracks (intro / menu / in-game) into the ROM. The SoundFont they use is
  already bundled in `core/tools/soundfont/`.

### Installing FFmpeg on Windows

* Easiest, with a package manager (run in PowerShell/Terminal):
  * `winget install Gyan.FFmpeg` &nbsp;— or —&nbsp; `choco install ffmpeg-full`
  * Open a **new** terminal afterwards so the updated `PATH` takes effect.
* Manual: download a build from
  [gyan.dev](https://www.gyan.dev/ffmpeg/builds/) (get "ffmpeg-release-full")
  or [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds/releases),
  unzip it somewhere permanent (e.g. `C:\ffmpeg`), then add the `bin` folder
  (`C:\ffmpeg\bin`, the one containing `ffmpeg.exe` and `ffprobe.exe`) to your
  `PATH`: *Start → "Edit the system environment variables" → Environment
  Variables → select `Path` → Edit → New*.
* Verify: open a new terminal and run `ffmpeg -version` and `ffprobe -version`.

### Installing FluidSynth on Windows

* Easiest, with a package manager (run in PowerShell/Terminal):
  * `winget install FluidSynth.FluidSynth` &nbsp;— or —&nbsp; `choco install fluidsynth`
  * Open a **new** terminal afterwards so the updated `PATH` takes effect.
* Manual: download the latest `fluidsynth-*-win10-x64.zip` from
  [FluidSynth releases](https://github.com/FluidSynth/fluidsynth/releases),
  unzip it somewhere permanent (e.g. `C:\fluidsynth`), then add its `bin`
  folder (`C:\fluidsynth\bin`, the one containing `fluidsynth.exe` and the
  `libfluidsynth-*.dll`) to your `PATH`: *Start → "Edit the system environment
  variables" → Environment Variables → select `Path` → Edit → New*.
* Verify: open a new terminal and run `fluidsynth --version`.

## How to Build

1. Put your `doomrpg.zip` (or `doomrpg.bar`) in `input/`.
2. Run the builder:
   * **Windows:** double-click `build.cmd` (or run it from a terminal).
   * **Any platform:** `python app/build_rom.py`
3. Wait for `OK -> output/doomrpg.z64`. That file is your ROM — run it in an
   emulator or flash it to an EverDrive-64.

Extra flags pass straight through, e.g. `build.cmd --no-audio` for a fast
silent ROM or `build.cmd --soundfont "C:\path\to\GM.sf2"`. See `python
app/build_rom.py --help` or `app/README.md` for every option.

## Acknowledgements

Huge thanks to **[Erick194](https://github.com/Erick194)** for
**[DoomRPG-RE](https://github.com/Erick194/DoomRPG-RE)** — the from-scratch
reverse-engineered source port of *Doom RPG*. This project vendors that engine
(upstream `8ff35da`, GPL-3.0) essentially unmodified and simply gives it a
Nintendo 64 backend; without that work none of this would exist.

Thanks also to the **[libdragon](https://github.com/DragonMinded/libdragon)**
team for the open-source N64 SDK, and to id Software / JAMDAT / EA for the
original 2005 game.

## AI Note

The application was developed using AI. I'm just an enthusiast who wanted to create interesting projects. In this case, how it was achieved is not relevant to me.
