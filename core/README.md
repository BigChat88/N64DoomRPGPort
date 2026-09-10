# core — the Doom RPG N64 port

The engine and N64 backend. Licensed **GPL-3.0** (see `../LICENSE`): this tree
is compiled and linked with GPL-3.0 DoomRPG-RE into one executable.

```
core/
├── Makefile            libdragon build (produces doomrpg.z64 when assets are present)
├── PORTING.md          port notes / design
├── src/
│   ├── doomrpg/        vendored DoomRPG-RE (Erick194/DoomRPG-RE @ 8ff35da, GPL-3.0)
│   │                   unmodified except Main.c / SDL_Video.c are dropped
│   └── port/           the N64 backend
│       ├── main.c              entry point + game loop
│       ├── pd_video.c          libdragon display + RGB565->5551 present blit
│       ├── pd_sound.c          libdragon mixer + wav64 (SFX + streamed Opus music)
│       ├── pd_input.c          N64 controller -> AVK_* key events
│       ├── pd_save.c           cartridge SRAM save store
│       ├── pd_bmp.c            uncompressed BMP loader
│       ├── pd_rwops.c          SDL_RWops over DragonFS + the save store
│       ├── pd_perf.c           per-frame CPU-time profiler (r_perf overlay)
│       ├── pd_sys.c  pd_intro.c
│       ├── SDL_Video.h         replacement for the upstream header
│       └── shim/               <SDL.h> / <SDL_mixer.h> / <fluidsynth.h> / <zlib.h>
├── tools/              build-pipeline scripts (part of the GPL corresponding source)
│   ├── bar2zip.py             BREW .bar  -> DoomRPG.zip (game data)
│   ├── overlay_zip.py         fold core/assets/overlay/*.bmp into DoomRPG.zip
│   ├── overlay_png.sh         regenerate the overlay BMPs from *_src.png
│   ├── audio_setup.sh         BarToZip.exe -> assets/snd/*.wav (+ MIDI music)
│   ├── ost_setup.sh           per-level music WAVs from source tracks
│   ├── bartozip/              BarToZip.exe + zlib.dll (from the DoomRPG-RE release, GPL-3.0)
│   └── soundfont/             (empty) put a General MIDI .sf2 here for the MIDI music path
└── assets/
    ├── overlay/{n64pad,aboutbg}.bmp   the port's own Help/About screens
    └── {n64pad,aboutbg}_src.png       their high-res sources
```

## Build

```sh
git submodule update --init      # from repo root: fetches core/libdragon
cd core
libdragon make engine            # -> build/doomrpg.elf.stripped + .sym
```

`make engine` needs no BREW archive and no game assets. It produces the two
files `../app/build_rom.py` packs against. `libdragon make` (the default
target) builds the full `doomrpg.z64`, but that needs a `doomrpg.zip` in this
directory and the decoded audio under `assets/` — that path is what `app/`
automates.

## Save type

`N64_ROM_SAVETYPE = sram256k` (32 KiB cartridge SRAM). FlashRAM was tried first
but the EverDrive-64 X7 FlashRAM emulation did not honour the write protocol.
