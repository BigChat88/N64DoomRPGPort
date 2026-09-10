#!/usr/bin/env python3
"""
build_rom.py -- turn an original Doom RPG BREW archive into a Nintendo 64 ROM.

This is the "packer": it does everything the core Makefile does *except* compile
the engine.  The compiled engine (doomrpg.elf.stripped + .sym) is the only
input that is not user-supplied; everything about the game itself comes out of
your own doomrpg.zip.

    python build_rom.py                       # <repo>/input/*.zip -> <repo>/output/doomrpg.z64
    python build_rom.py --bar X.zip --out Y.z64

Pipeline (mirrors core/Makefile):
    1. bar2zip.py         doomrpg.zip        -> DoomRPG.zip           (game data)
    2. overlay_zip.py     DoomRPG.zip        += n64pad/aboutbg BMPs
    3. BarToZip.exe       doomrpg.bar        -> <id>.wav / <id>.mid   (audio)
    4. (optional) FluidSynth+ffmpeg  <id>.mid -> <id>.wav            (engine music)
    5. audioconv64        snd/*.wav          -> snd/*.wav64
       audioconv64        <bgm>/*.wav        -> mus/*.wav64           (optional)
    6. mkdfs              <fs tree>          -> doomrpg.dfs
    7. n64tool --toc      elf + sym + dfs    -> doomrpg.z64
    8. ed64romconfig      --savetype ...     stamp the homebrew header

Requires the libdragon host tools (mkdfs, n64tool, audioconv64,
n64elfcompress, ed64romconfig) -- found on PATH, in $N64_INST/bin, or in
app/vendor/bin/.  See app/README.md.
"""
from __future__ import annotations

import argparse
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
CORE = ROOT / "core"


def project_version() -> str:
    f = ROOT / "VERSION"
    try:
        return f.read_text(encoding="utf-8").strip() or "0.0.0"
    except OSError:
        return "0.0.0"

# n64elfcompress is not needed here: core/Makefile's `engine` target already
# compresses doomrpg.elf.stripped, so the packer only assembles + stamps.
TOOL_NAMES = ("mkdfs", "n64tool", "audioconv64", "ed64romconfig")


# --------------------------------------------------------------------------- util
def log(msg: str) -> None:
    print(msg, flush=True)


def die(msg: str):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def run(cmd: list[str], **kw) -> None:
    log("  $ " + " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def exe(name: str) -> str:
    return name + (".exe" if os.name == "nt" else "")


def find_tool(name: str, extra_dir: Path | None) -> Path:
    cands: list[Path] = []
    if extra_dir:
        cands.append(extra_dir / exe(name))
    n64_inst = os.environ.get("N64_INST")
    if n64_inst:
        cands.append(Path(n64_inst) / "bin" / exe(name))
    cands.append(HERE / "vendor" / "bin" / exe(name))
    which = shutil.which(name)
    if which:
        cands.append(Path(which))
    for c in cands:
        if c.is_file():
            return c
    die(
        f"host tool '{name}' not found.\n"
        f"  looked in: {', '.join(str(c) for c in cands) or '(nowhere)'}\n"
        f"  install the libdragon toolchain, set N64_INST, pass --tools-dir,\n"
        f"  or drop the tool binaries in {HERE / 'vendor' / 'bin'}"
    )


def find_engine(engine_dir: Path | None) -> tuple[Path, Path]:
    dirs = []
    if engine_dir:
        dirs.append(engine_dir)
    dirs += [HERE / "vendor" / "engine", CORE / "build"]
    for d in dirs:
        elf = d / "doomrpg.elf.stripped"
        sym = d / "doomrpg.elf.sym"
        if elf.is_file() and sym.is_file():
            return elf, sym
    die(
        "compiled engine not found (doomrpg.elf.stripped + doomrpg.elf.sym).\n"
        f"  looked in: {', '.join(str(d) for d in dirs)}\n"
        "  build it with `libdragon make` in core/, or pass --engine-dir,\n"
        f"  or drop the two files in {HERE / 'vendor' / 'engine'}"
    )


# --------------------------------------------------------------- BREW .bar reader
def load_bar_bytes(path: Path) -> bytes:
    """Return the raw .bar blob whether `path` is a .bar or a .zip wrapping one."""
    blob = path.read_bytes()
    if blob[:2] == b"PK":
        z = zipfile.ZipFile(io.BytesIO(blob))
        name = next((n for n in z.namelist() if n.lower().endswith(".bar")), None)
        if not name:
            die(f"{path}: zip contains no .bar file")
        return z.read(name)
    return blob


# --------------------------------------------------------------- BarToZip (audio)
def run_bartozip(bar_blob: bytes, work: Path) -> Path:
    """Run BarToZip.exe on the .bar; return the DoomRPG.zip it produces."""
    btz_dir = CORE / "tools" / "bartozip"
    btz = btz_dir / "BarToZip.exe"
    if not btz.is_file():
        die(f"{btz} missing -- it ships with the DoomRPG-RE release")

    run_dir = work / "bartozip"
    run_dir.mkdir()
    (run_dir / "doomrpg.bar").write_bytes(bar_blob)
    shutil.copy2(btz, run_dir / "BarToZip.exe")
    zlibdll = btz_dir / "zlib.dll"
    if zlibdll.is_file():
        shutil.copy2(zlibdll, run_dir / "zlib.dll")

    if os.name == "nt":
        cmd = [str(run_dir / "BarToZip.exe")]
    else:
        wine = shutil.which("wine")
        if not wine:
            die("BarToZip.exe needs Windows or Wine (install `wine`)")
        cmd = [wine, str(run_dir / "BarToZip.exe")]
    log("  $ " + " ".join(cmd) + f"   (cwd={run_dir})")
    try:
        # stdin=DEVNULL: BarToZip.exe does a "press any key" getchar() at the
        # end; with the terminal's stdin attached it blocks forever.  Feeding
        # EOF makes it return immediately.  Capture output for diagnosis.
        p = subprocess.run(cmd, cwd=run_dir, timeout=180,
                           stdin=subprocess.DEVNULL,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, errors="replace")
    except subprocess.TimeoutExpired:
        die(f"BarToZip.exe timed out (180s).  work dir: {run_dir}")
    if p.returncode != 0:
        die(f"BarToZip.exe exited {p.returncode}\n{p.stdout}")

    out = run_dir / "DoomRPG.zip"
    if not out.is_file():
        die(f"BarToZip produced no DoomRPG.zip in {run_dir}\n"
            f"  (its output was:\n{p.stdout})")
    return out


def split_bartozip_audio(btz_zip: Path, snd_wav_dir: Path, mid_dir: Path) -> tuple[int, int]:
    snd_wav_dir.mkdir(parents=True, exist_ok=True)
    mid_dir.mkdir(parents=True, exist_ok=True)
    sfx = mus = 0
    with zipfile.ZipFile(btz_zip) as z:
        for n in z.namelist():
            base = os.path.basename(n)
            if n.lower().endswith(".wav"):
                (snd_wav_dir / base).write_bytes(z.read(n))
                sfx += 1
            elif n.lower().endswith(".mid"):
                (mid_dir / base).write_bytes(z.read(n))
                mus += 1
    return sfx, mus


def audioconv_dir(tool: Path, wav_dir: Path, out_dir: Path, flags: list[str]) -> int:
    """Convert every *.wav in wav_dir to *.wav64 in out_dir.

    audioconv64's `-o <dir> <file>` mode splits the basename on '/' only, so a
    Windows absolute path becomes a garbage output name.  Run it with cwd set
    to the input dir and pass the bare filename.
    """
    n = 0
    for wav in sorted(wav_dir.glob("*.wav")):
        run([str(tool), *flags, "-o", str(out_dir.resolve()), wav.name],
            cwd=str(wav_dir))
        w64 = out_dir / (wav.stem + ".wav64")
        if not w64.is_file():
            die(f"audioconv64 produced no {w64.name} from {wav.name}")
        n += 1
    return n


def render_engine_music(mid_dir: Path, snd_wav_dir: Path, soundfont: Path,
                        maxlen: int = 60, rate: int = 22050) -> int:
    fs = shutil.which("fluidsynth")
    ff = shutil.which("ffmpeg")
    if not fs or not ff:
        log("  ! fluidsynth/ffmpeg not on PATH -- skipping engine MIDI music")
        return 0
    n = 0
    for mid in sorted(mid_dir.glob("*.mid")):
        raw = mid.with_suffix(".raw.wav")
        run([fs, "-nli", "-q", "-g", "0.6", "-r", "44100", "-O", "s16",
             "-T", "wav", "-F", str(raw), str(soundfont), str(mid)])
        fade_st = max(0.0, maxlen - 0.25)
        run([ff, "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
             "-i", str(raw), "-t", str(maxlen), "-ac", "1", "-ar", str(rate),
             "-af", f"afade=t=out:st={fade_st}:d=0.25,alimiter=limit=0.97:level=disabled",
             str(snd_wav_dir / (mid.stem + ".wav"))])
        raw.unlink(missing_ok=True)
        n += 1
    return n


# ------------------------------------------------------------------------- packer
def main() -> None:
    ap = argparse.ArgumentParser(
        description="Build a Doom RPG N64 ROM from your own doomrpg.zip.")
    ap.add_argument("--version", action="version",
                    version=f"N64DoomRPGPort {project_version()}")
    ap.add_argument("--bar", type=Path, default=None,
                    help="the BREW archive: doomrpg.bar, or a .zip containing "
                         "it.  Default: the first .zip/.bar found in input/")
    ap.add_argument("--out", type=Path, default=None,
                    help="output ROM path (default: output/doomrpg.z64)")
    ap.add_argument("--title", default="Doom RPG", help='ROM title (max 20 chars)')
    ap.add_argument("--savetype", default="sram256k",
                    help="cartridge save type for the homebrew header "
                         "(default: sram256k -- matches core/Makefile)")
    ap.add_argument("--engine-dir", type=Path,
                    help="dir with doomrpg.elf.stripped + doomrpg.elf.sym "
                         "(default: app/vendor/engine, then core/build)")
    ap.add_argument("--tools-dir", type=Path,
                    help="dir with the libdragon host tools "
                         "(default: PATH, $N64_INST/bin, app/vendor/bin)")
    ap.add_argument("--soundfont", type=Path,
                    help="General MIDI .sf2 to render the 3 BREW MIDI tracks "
                         "(intro / menu / in-game); needs fluidsynth + ffmpeg. "
                         "Default: first .sf2 in core/tools/soundfont/, else "
                         "no engine music.")
    ap.add_argument("--bgm-dir", type=Path,
                    help="dir of <map>.wav files for streamed per-level music "
                         "(intro, junction, junction_destroyed, level01..07, "
                         "reactor).  Default: core/assets/sound-example/ if it "
                         "has .wav files, else no per-level music.")
    ap.add_argument("--no-audio", action="store_true",
                    help="skip all audio (SFX + music); build a silent ROM fast")
    ap.add_argument("--keep-work", action="store_true",
                    help="keep the temporary work directory")
    args = ap.parse_args()

    log(f"N64DoomRPGPort {project_version()}")

    if args.bar is None:
        indir = ROOT / "input"
        cands = sorted(indir.glob("*.zip")) + sorted(indir.glob("*.bar"))
        if not cands:
            die(f"no BREW archive.\n"
                f"  put your doomrpg.zip (or doomrpg.bar) in {indir}\n"
                f"  or pass --bar <file>.  See README.md for what that file is.")
        args.bar = cands[0]
        log(f"input  : {args.bar.name}  (from input/)")
    if not args.bar.is_file():
        die(f"{args.bar}: no such file")

    if args.out is None:
        args.out = ROOT / "output" / "doomrpg.z64"

    elf, sym = find_engine(args.engine_dir)
    tools = {t: find_tool(t, args.tools_dir) for t in TOOL_NAMES}
    log(f"engine : {elf}")
    for t, p in tools.items():
        log(f"tool   : {t:14s} {p}")

    bar2zip = CORE / "tools" / "bar2zip.py"
    overlay_zip = CORE / "tools" / "overlay_zip.py"
    overlay_dir = CORE / "assets" / "overlay"
    logo_sprite = CORE / "filesystem" / "libdragon_logo.sprite"

    work = Path(tempfile.mkdtemp(prefix="drpg-pack-"))
    fsroot = work / "fs"
    fsroot.mkdir()
    try:
        bar_blob = load_bar_bytes(args.bar)

        # 1-2. game data zip + port overlay images
        log("[1/7] bar2zip -> DoomRPG.zip")
        drpg_zip = fsroot / "DoomRPG.zip"
        run([sys.executable, str(bar2zip), str(args.bar), str(drpg_zip)])
        if overlay_dir.is_dir() and any(overlay_dir.iterdir()):
            log("[2/7] overlay_zip += n64pad / aboutbg")
            run([sys.executable, str(overlay_zip), str(drpg_zip), str(overlay_dir)])

        # libdragon splash logo: from a full `libdragon make`, else the bundled
        # copy, else nothing (pd_intro.c skips the splash if it is missing).
        logo_bundled = HERE / "vendor" / "fs" / "libdragon_logo.sprite"
        logo_src = logo_sprite if logo_sprite.is_file() else logo_bundled
        if logo_src.is_file():
            shutil.copy2(logo_src, fsroot / "libdragon_logo.sprite")
        else:
            log("  ! no libdragon_logo.sprite -- intro splash will be skipped")

        # any other loose files bundled for the DFS root
        for extra in sorted((HERE / "vendor" / "fs").glob("*")):
            if extra.suffix != ".md" and extra.name != "libdragon_logo.sprite":
                shutil.copy2(extra, fsroot / extra.name)

        # 3-5. audio
        if args.no_audio:
            log("[3/7] audio: skipped (--no-audio)")
        else:
            log("[3/7] BarToZip -> SFX wav + MIDI")
            btz_zip = run_bartozip(bar_blob, work)
            snd_wav = work / "snd_wav"
            mid_dir = work / "mid"
            sfx, mus = split_bartozip_audio(btz_zip, snd_wav, mid_dir)
            log(f"      {sfx} sfx, {mus} midi")

            log("[4/7] engine music")
            sf2 = args.soundfont
            if sf2 is None:
                # auto: use a .sf2 dropped in core/tools/soundfont/
                found = sorted((CORE / "tools" / "soundfont").glob("*.sf2")) \
                        + sorted((CORE / "tools" / "soundfont").glob("*.SF2"))
                if found:
                    sf2 = found[0]
                    log(f"      auto soundfont: {sf2.name}")
            if sf2:
                if not sf2.is_file():
                    die(f"{sf2}: no such file")
                n = render_engine_music(mid_dir, snd_wav, sf2)
                log(f"      rendered {n} track(s)")
            else:
                log("      no soundfont (--soundfont / core/tools/soundfont/) "
                    "-- intro/menu/in-game music omitted")

            log("[5/7] audioconv64 -> wav64")
            snd_out = fsroot / "snd"
            snd_out.mkdir()
            n = audioconv_dir(tools["audioconv64"], snd_wav, snd_out,
                              ["--wav-compress", "0"])
            log(f"      {n} sfx -> wav64")
            bgm_dir = args.bgm_dir
            if bgm_dir is None:
                # auto: streamed per-level loops in core/assets/sound-example/
                auto = CORE / "assets" / "sound-example"
                if auto.is_dir() and any(auto.glob("*.wav")):
                    bgm_dir = auto
                    log(f"      auto per-level music: {bgm_dir}")
            if bgm_dir:
                if not bgm_dir.is_dir():
                    die(f"{bgm_dir}: not a directory")
                mus_out = fsroot / "mus"
                mus_out.mkdir()
                n = audioconv_dir(tools["audioconv64"], bgm_dir, mus_out,
                                  ["--wav-compress", "3", "--wav-mono",
                                   "--wav-loop", "true"])
                log(f"      {n} music -> wav64")

        # 6. DragonFS image
        log("[6/7] mkdfs")
        dfs = work / "doomrpg.dfs"
        run([tools["mkdfs"], str(dfs), str(fsroot)])

        # 7. assemble + stamp
        log("[7/7] n64tool + ed64romconfig")
        args.out.parent.mkdir(parents=True, exist_ok=True)
        run([tools["n64tool"], "--title", args.title[:20], "--toc",
             "--output", str(args.out),
             "--align", "256", str(elf),
             "--align", "8", str(sym),
             "--align", "16", str(dfs)])
        run([tools["ed64romconfig"], "--savetype", args.savetype, str(args.out)])

        size = args.out.stat().st_size
        log(f"\nOK -> {args.out}  ({size/1024/1024:.1f} MiB)")
    finally:
        if args.keep_work:
            log(f"work dir kept: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
