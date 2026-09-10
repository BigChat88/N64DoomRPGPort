# app/vendor/bin

The libdragon host tools `build_rom.py` shells out to:

| tool | job |
|---|---|
| `mkdfs` | pack the DragonFS filesystem image |
| `n64tool` | assemble ELF + sym + DFS into the `.z64` |
| `audioconv64` | WAV -> `.wav64` (VADPCM for SFX, Opus for streamed music) |
| `ed64romconfig` | stamp the homebrew ROM header (save type) |

`build_rom.py` finds tools in this order: `--tools-dir`, `$N64_INST/bin`, this
folder, then `PATH`. Drop platform-native binaries here if they are not
already on your system. (`n64elfcompress` is not needed -- `core/Makefile`'s
`engine` target already compresses `doomrpg.elf.stripped`.)

## Building them

They are small single-file C programs; the libdragon Docker toolchain is not
required. With any native C compiler:

```sh
./build-tools.sh                 # gcc on PATH
CC=/c/Users/you/scoop/apps/gcc/current/bin/gcc ./build-tools.sh
```

The prebuilt `.exe`s here were made this way with mingw-w64 gcc 15 on Windows
x64. They are not portable across OS/arch -- rebuild for yours, or delete them
and rely on `$N64_INST/bin`.
