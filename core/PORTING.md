# DoomRPG → Nintendo 64 (libdragon) port

Port of [Erick194/DoomRPG-RE](https://github.com/Erick194/DoomRPG-RE) (GPL-3.0
reverse-engineered *Doom RPG*) to the Nintendo 64 using **libdragon**.

## Status — milestone 1: builds & links

`libdragon make` produces `doomrpg.z64`. The full DoomRPG-RE engine compiles for
MIPS against a thin compatibility layer; SDL2 / SDL2_mixer / FluidSynth are
replaced by shims. Not yet run on hardware/emulator.

Target: **8 MB (Expansion Pak required)**, internal resolution 320×240, 16-bit.

## Layout

```
src/doomrpg/     vendored DoomRPG-RE sources (upstream 8ff35da), unmodified
                 except that Main.c / SDL_Video.c are NOT included
src/port/        the N64 backend
  shim/SDL.h            types, libc mapping, byte-swap (N64 is big-endian!),
                       SDL_RWops, render/messagebox decls
  shim/SDL_mixer.h     SFX calls → pd_sound.c (libdragon mixer)
  shim/fluidsynth.h    music player calls → pd_sound.c; synth ctors are stubs
  shim/zlib.h          redirects <zlib.h> → bundled miniz (raw inflate only)
  SDL_Video.h          replacement for the upstream header (globals + protos)
  pd_video.c           libdragon display; RGB565 framebuffer → RGBA5551;
                       software image blit (src rect clamped), lines, rects
  pd_rwops.c           SDL_RWops over DragonFS (read); the four save slots
                       route to pd_save.c, other writes are discarded
  pd_save.c            N64 SRAM driver + directory store for the
                       Config / Player / Player2 / World save slots
  pd_bmp.c             1/4/8/24/32-bit uncompressed BMP loader + surface/texture
  pd_input.c           N64 controller → AVK_* key events
  pd_sound.c           libdragon mixer + wav64 backend for the audio shims
  pd_intro.c           libdragon logo splash before the game boots
  pd_sys.c             SDL_Init/GetTicks/Delay/Log/MessageBox
  main.c               entry point (replaces Main.c)
filesystem/      DragonFS image root — put the original DoomRPG.zip here
```

## Building

```
libdragon make            # or: libdragon make -j4
```

Requires the `libdragon` CLI (`npm i -g libdragon`) and Docker. First run builds
libdragon into the toolchain container (`libdragon install`).

## Assets

The original BREW distribution ships `doomrpg.bar` (usually inside a `.zip`
alongside `doomrpg.mod` / `doomrpg.mif`). `doomrpg.bar` stores each asset as a
gzip member whose name is in the gzip FNAME field.

Put that archive (`doomrpg.zip` **or** `doomrpg.bar`) in the repo root. The
Makefile runs `tools/bar2zip.py` on it to produce `filesystem/DoomRPG.zip` — the
43-entry archive (`sintable.bin`, `palettes.bin`, `entities.db`, `wtexels.bin`,
`stexels.bin`, `bitshapes.bin`, `mappings.bin`, every `*.bsp`, the `*.bmp`
images, `help.txt`) that `Z_Zip.c` reads. It is packed into the ROM as a
DragonFS image and inflated to RAM on demand.

`filesystem/DoomRPG.zip` and the source archive are git-ignored (copyrighted
game data).

Port-only overlay images live in `assets/overlay/` and are merged into
`DoomRPG.zip` by `tools/overlay_zip.py` after `bar2zip.py`:

- `n64pad.bmp` — the N64 controller diagram for the Help screen (else a plain
  text list). Falls back gracefully if absent.
- `aboutbg.bmp` — brick background for the About screen (else the logo).

Both are authored 320x240 (the engine blits them 1:1). `tools/overlay_png.sh`
regenerates them from `assets/*_src.png`:

- It emits a **24-bit BMP** whose pixels are Floyd–Steinberg dithered onto the
  RGB555 grid the engine's framebuffer actually shows, so the brick gradient
  stays smooth instead of banding into the ~100 colours an 8-bit palette
  collapsed to. `pd_bmp.c` reads 24-bit BMPs and `DoomRPG.c`'s `createImage`
  skips its palette pre-round when the surface has no palette.
- The engine draws these edge to edge with no safe-area margin, so `n64pad`
  (which has button labels near the edges) is scaled into a ~5%-per-side
  TV-safe box and centred on a black canvas; `aboutbg` stays full-bleed. Tune
  with `MARGIN_X` / `MARGIN_Y`, or `SAFE=0/1`.

### Audio

The N64 audio **backend**: `src/port/pd_sound.c` drives libdragon's `mixer` +
`wav64`, and `shim/SDL_mixer.h` / `shim/fluidsynth.h` forward the engine's SFX
and music-player calls to it. `main.c` pumps `PD_SoundUpdate()` each loop;
`SDL_InitAudio()` brings the DACs up.

**Assets** come from `bash tools/audio_setup.sh`. It runs `BarToZip.exe` (the
DoomRPG-RE v0.2.1+ tool, bundled with `zlib.dll` in `tools/bartozip/`) on
`doomrpg.bar` — that decodes the BREW PMD sound resources into **92 PCM WAV
sfx** and **3 Standard-MIDI music tracks**, all named by engine resource id.
The script drops the sfx straight into `assets/snd/<id>.wav` (8 kHz mono) and
renders the 3 MIDIs with `fluidsynth` + a SoundFont
(`tools/soundfont/*.sf2`, default SC-55 Deemster), trimmed to 60 s and
resampled to 22 kHz.

The Makefile runs `audioconv64` on `assets/snd/*.wav` → `rom:/snd/<id>.wav64`
in the DFS. `PD_SoundLoad()` loads by resource id and returns NULL (silent)
for the handful of `soundTable` ids that have no resource. With `assets/snd/`
empty the game boots silent and `PD_SoundInit()` leaves the audio subsystem
down.

`tools/midi2snd.sh` (earlier approach: render the smaller J2ME MIDI set and
guess a resource-id mapping) is **superseded** by `audio_setup.sh` and kept
only for reference. `../CmidConverter/` was a from-scratch cmid decoder,
likewise superseded by BarToZip for the music.

### Per-level background music

Optional. One WAV per map lives in `assets/sound-example/<map>.wav` (the `.bsp`
basenames: `intro`, `junction`, `junction_destroyed`, `level01`..`level07`,
`reactor`) — hand-trimmed 48 kHz mono 16-bit PCM loops, already edited so the
loop seam sounds clean. They are used as-is: the Makefile runs `audioconv64
--wav-compress 3 --wav-loop true` on each → `rom:/mus/<map>.wav64` (streamed
Opus), which only compresses and flags the whole file as looping and does
**not** trim it. `tools/ost_setup.sh` (the old host-`ffmpeg` path that cut each
MP3 in `assets/doomOST/` to a ~120 s loop) is no longer used.
`src/port/pd_sound.c` plays the matching track on its own mixer channel when a
map loads (`PD_LevelMusicForMap`, called from `DoomCanvas_loadMedia`); it is
independent of the engine's SFX and its intro/menu music. The audio menu gains
a **Music: on/off** item (`doomCanvas->musicEnabled`, persisted in `Config`,
`CONFIG_VERSION` 24). With `assets/sound-example/` empty the game builds and runs exactly
as before.

- `src/doomrpg/Z_Zip.c` `readZipFileEntry()` is still patched to **return NULL**
  for a missing entry instead of calling `DoomRPG_Error`. Harmless now that the
  audio loader no longer goes through the zip; keep it until every asset that
  the engine might request is guaranteed present.
- `src/doomrpg/Sound.c` load/free was rerouted to `PD_SoundLoad` /
  `PD_SoundFree` (marked `[n64 port]`).

## Known next steps

- **Endianness**: `SDL_SwapLE*` now really swaps, and the engine already calls it
  for `sintable.bin`; other binary asset reads need auditing on big-endian.
- **Alignment**: MIPS traps on unaligned access — watch packed reads out of
  asset buffers.
- **Audio**: done — `bash tools/audio_setup.sh` (BarToZip decodes all 95).
- **Saves**: done — `Config` / `Player` / `Player2` / `World` persist to
  cartridge SRAM via `src/port/pd_save.c` (`N64_ROM_SAVETYPE = sram256k`).
  A 32 KiB RAM shadow fronts the window; a save rewrites the whole window
  and reads it back to confirm. SRAM, not FlashRAM: the FlashRAM write
  protocol was not honoured by the EverDrive-64 X7 emulation (the `.fla`
  was created but stayed empty). With no SRAM present the store still works
  for the session and logs a warning. Once the four slots exist, the main
  menu's "Start Game" offers **Continue** (the load path).
- **Colour**: convert the palette straight to RGBA5551 to drop the per-frame
  565→5551 pass.
- **Input**: proper rebinding screen; tune the default N64 mapping.
