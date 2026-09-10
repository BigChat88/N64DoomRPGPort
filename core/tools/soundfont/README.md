# soundfont/

The 3 BREW music tracks (5039 intro / 5040 menu / 5043 in-game) ship as MIDI.
build_rom.py renders them with FluidSynth against a General MIDI SoundFont:
the first `.sf2` it finds in this folder is used automatically, or pass
`--soundfont <file>` (or `--no-audio` to skip).

This project renders them with **`SC-55 Deemster [GZDoom].sf2`**, which is
committed here so a fresh clone (and the GitHub release bundle) builds music
with no extra downloads.  It is a third-party General MIDI SoundFont (an SC-55
sample set from the GZDoom community); it is redistributed here for convenience
only -- swap in your own `.sf2` with `--soundfont <file>` if you prefer.
