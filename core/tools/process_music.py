#!/usr/bin/env python3
"""
process_music.py -- turn the per-level music loops into the committed .wav64s.

The hand-trimmed source loops (assets/sound-example/<map>.wav, ~118 MB of
48 kHz PCM) are NOT committed: they are git-ignored, kept only by whoever
edits the music.  What the repo ships is their processed form,
assets/music/<map>.wav64 (~8 MB of streamed Opus), which core/Makefile and
app/build_rom.py copy into rom:/mus/ as-is -- so building a ROM needs neither
the WAVs nor ffmpeg.

Same idea as N64DoomPort's tools/render_music.py: process the music once,
host-side, then let the build just pack the result.  Per track:

    ffmpeg       <map>.wav -> mono, OUTPUT_RATE Hz PCM      (temp file)
    audioconv64  --wav-compress 3 --wav-mono --wav-loop true -> <map>.wav64

48000 Hz: an Opus native rate, so audioconv64 never resamples (its internal
resampler is what broke the loop point in N64DoomPort).  24 kHz would be about
a third smaller, but on the libdragon preview branch it crashed on real
hardware ("opus decode error: invalid argument" in waveform_opus_read): 48 kHz
uses one of Opus' static modes, while 24 kHz makes the runtime build and free
a custom mode per track.  Keep 48 kHz unless that is fixed upstream.  The N64
mixer runs at 44.1 kHz either way (pd_sound.c SND_FREQ).

The .wav64 layout belongs to the libdragon revision in core/libdragon: re-run
this whenever the submodule moves, or when a source loop changes.

    python tools/process_music.py                    # sound-example/ -> music/
    python tools/process_music.py --rate 24000       # smaller -- see above first
    python tools/process_music.py --src DIR --out DIR --tools-dir DIR
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CORE = Path(__file__).resolve().parent.parent
ROOT = CORE.parent

OUTPUT_RATE = 48000
AUDIOCONV_FLAGS = ["--wav-compress", "3", "--wav-mono", "--wav-loop", "true"]


def die(msg: str):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def exe(name: str) -> str:
    return name + (".exe" if os.name == "nt" else "")


def find_audioconv(tools_dir: Path | None) -> Path:
    cands: list[Path] = []
    if tools_dir:
        cands.append(tools_dir / exe("audioconv64"))
    n64_inst = os.environ.get("N64_INST")
    if n64_inst:
        cands.append(Path(n64_inst) / "bin" / exe("audioconv64"))
    cands.append(ROOT / "app" / "vendor" / "bin" / exe("audioconv64"))
    which = shutil.which("audioconv64")
    if which:
        cands.append(Path(which))
    for c in cands:
        if c.is_file():
            return c
    die("audioconv64 not found (looked in: "
        + ", ".join(str(c) for c in cands)
        + ").  Build it with app/vendor/bin/build-tools.sh or pass --tools-dir.")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--src", type=Path, default=CORE / "assets" / "sound-example",
                    help="dir of <map>.wav source loops (default: assets/sound-example)")
    ap.add_argument("--out", type=Path, default=CORE / "assets" / "music",
                    help="dir for the <map>.wav64 output (default: assets/music)")
    ap.add_argument("--rate", type=int, default=OUTPUT_RATE,
                    help=f"output sample rate, an Opus native rate (default: {OUTPUT_RATE})")
    ap.add_argument("--tools-dir", type=Path,
                    help="dir with audioconv64 (default: $N64_INST/bin, app/vendor/bin, PATH)")
    args = ap.parse_args()

    if args.rate not in (8000, 12000, 16000, 24000, 48000):
        die(f"--rate {args.rate}: not an Opus native rate (8000/12000/16000/24000/48000)")
    wavs = sorted(args.src.glob("*.wav")) if args.src.is_dir() else []
    if not wavs:
        die(f"no *.wav in {args.src}")
    ffmpeg = shutil.which("ffmpeg") or die("ffmpeg not found on PATH")
    audioconv = find_audioconv(args.tools_dir)

    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="drpg-mus-") as tmp:
        tmp = Path(tmp)
        for wav in wavs:
            pcm = tmp / wav.name
            subprocess.run([ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
                            "-i", str(wav), "-ac", "1", "-ar", str(args.rate), str(pcm)],
                           check=True)
            # audioconv64's `-o <dir> <file>` mode splits the basename on '/'
            # only, so pass the bare filename with cwd set (see build_rom.py).
            subprocess.run([str(audioconv), *AUDIOCONV_FLAGS, "-o", str(args.out.resolve()),
                            pcm.name], cwd=str(tmp), check=True, stdout=subprocess.DEVNULL)
            w64 = args.out / (wav.stem + ".wav64")
            if not w64.is_file():
                die(f"audioconv64 produced no {w64.name} from {wav.name}")
            print(f"  [MUS] {wav.stem:20s} {wav.stat().st_size / 1e6:6.1f} MB -> "
                  f"{w64.stat().st_size / 1e6:5.2f} MB")

    total = sum(f.stat().st_size for f in args.out.glob("*.wav64"))
    print(f"done: {len(wavs)} track(s) -> {args.out}  ({total / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
