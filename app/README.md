# app — the ROM builder

`build_rom.py` takes your own **doomrpg.zip** (the BREW distribution of Doom
RPG) and the compiled engine, and produces a runnable **doomrpg.z64**. It does
everything `core/Makefile` does except compile the engine.

```
python build_rom.py                              # ../input/*.zip -> ../output/doomrpg.z64
python build_rom.py --bar X.zip --out Y.z64      # explicit paths
```

Most users run `build.cmd` in the repo root instead, which just calls this with
no arguments.

## What it needs

| Input | Where it comes from |
|---|---|
| the game archive | first `*.zip` / `*.bar` in `../input/`, or `--bar <file>` |
| the compiled engine | `app/vendor/engine/`, else `core/build/` (`libdragon make engine`) |
| libdragon host tools | `app/vendor/bin/`, `$N64_INST/bin`, or PATH |
| `BarToZip.exe` | `core/tools/bartozip/` (ships with the DoomRPG-RE release) |
| a `.sf2` for the 3 MIDI tracks (optional) | the bundled `core/tools/soundfont/*.sf2`, or `--soundfont`; needs fluidsynth + ffmpeg, skipped without them |
| per-level music | pre-processed `core/assets/music/*.wav64` (see `core/tools/process_music.py`), or `--bgm-dir <dir>` of `.wav64`/`.wav` |

The output goes to `../output/doomrpg.z64` unless `--out` says otherwise.

### Host tools

The libdragon tools `mkdfs`, `n64tool`, `audioconv64`, `n64elfcompress`,
`ed64romconfig` are plain C programs. Get them by:

* using the prebuilt Windows `.exe`s already in `app/vendor/bin/`, **or**
* building them with `app/vendor/bin/build-tools.sh` (any native gcc/g++), **or**
* installing the libdragon SDK (they land in `$N64_INST/bin`).

They must match the `core/libdragon` revision the engine was built with.

### The engine

`build_rom.py` never compiles anything. It expects
`doomrpg.elf.stripped` + `doomrpg.elf.sym`, produced by `libdragon make` in
`core/`. For a release, copy them into `app/vendor/engine/`.

### BarToZip.exe

`BarToZip.exe` decodes the BREW `.bar`, including the 92 PMD sound resources
that `bar2zip.py` does not handle. It runs natively on Windows; on Linux/macOS
`build_rom.py` calls it through `wine`.

## Options

```
--bar FILE          doomrpg.bar, or a .zip containing it
                                       (default: first *.zip / *.bar in input/)
--out FILE          output ROM                  (default: output/doomrpg.z64)
--title STR         ROM header title, <=20 chars              (default: Doom RPG)
--savetype TYPE     homebrew-header save type                 (default: sram256k)
--engine-dir DIR    where doomrpg.elf.stripped / .sym are
--tools-dir DIR     where the libdragon host tools are
--soundfont FILE    render the 3 BREW MIDI tracks with this .sf2  (needs
                    fluidsynth + ffmpeg on PATH; default: the bundled one)
--bgm-dir DIR       per-level music as <map>.wav64 (copied) or <map>.wav
                    (converted): intro, junction, junction_destroyed,
                    level01..level07, reactor  (default: core/assets/music/)
--no-audio          silent ROM, fast
--keep-work         keep the temp build dir for inspection
```

## Examples

```sh
# default: input/*.zip -> output/doomrpg.z64, with SFX, per-level music and
# (if fluidsynth + ffmpeg are installed) the MIDI themes
python build_rom.py

# the MIDI themes with a different SoundFont
python build_rom.py --soundfont "GeneralUser GS.sf2"

# your own per-level soundtrack loops instead of the bundled ones
python build_rom.py --bgm-dir ./my-ost-loops

# fast silent ROM
python build_rom.py --no-audio
```

## Shipping as a single .exe

`build_rom.py` is stdlib-only. Freeze it with PyInstaller:

```sh
pyinstaller --onefile --name doomrpg-n64-builder build_rom.py
```

and bundle `core/tools/`, `core/assets/overlay/`, `core/assets/music/`,
`app/vendor/engine/`, `app/vendor/bin/`, `app/vendor/fs/` alongside it. (Rewriting it in Go for a smaller static
binary is also an option — the logic is ~300 lines of orchestration.)

## Licensing

The ROM this produces contains GPL-3.0 code (DoomRPG-RE + the port). If you
distribute the ROM or a bundle containing `doomrpg.elf.stripped`, you must make
the corresponding source (this repository) available to recipients. It contains
no Doom RPG game data.
