# soundfont/

The 3 BREW music tracks (5039 intro / 5040 menu / 5043 in-game) ship as MIDI.
build_rom.py renders them with FluidSynth against a General MIDI SoundFont:
the first `.sf2` it finds in this folder is used automatically, or pass
`--soundfont <file>` (or `--no-audio` to skip).

The original project rendered them with "SC-55 Deemster [GZDoom].sf2".  That
file is present here locally but is **git-ignored** -- SoundFonts (especially
SC-55 sample rips) are third-party and not redistributable in this repo.
Anyone building from a fresh clone supplies their own.
