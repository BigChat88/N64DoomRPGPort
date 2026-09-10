#!/usr/bin/env bash
# audio_setup.sh -- populate assets/snd/ with the real Doom RPG audio.
#
# Uses BarToZip.exe (from the DoomRPG-RE v0.2.1+ release) which decodes the
# BREW .bar's PMD sound resources: 92 SFX come out as PCM WAV and the 3 music
# resources as Standard MIDI, all named by engine resource id.
#
#   SFX  <id>.wav  -> assets/snd/<id>.wav                (as-is; 8 kHz mono)
#   MID  <id>.mid  -> fluidsynth + SoundFont -> assets/snd/<id>.wav
#
# The Makefile then runs audioconv64 on assets/snd/*.wav -> rom:/snd/*.wav64,
# which pd_sound.c loads by id.  This replaces tools/midi2snd.sh (which guessed
# a mapping from the smaller J2ME MIDI set).
#
#   bash tools/audio_setup.sh
#   SF2=/path/bank.sf2 bash tools/audio_setup.sh      # pick the music bank
set -euo pipefail
cd "$(dirname "$0")/.."

BAR_SRC="$(ls doomrpg.bar doomrpg.zip DoomRPG.bar DoomRPG.zip 2>/dev/null | head -1 || true)"
[ -n "$BAR_SRC" ] || { echo "put doomrpg.bar or doomrpg.zip in the repo root"; exit 1; }
[ -x tools/bartozip/BarToZip.exe ] || { echo "tools/bartozip/BarToZip.exe missing"; exit 1; }

OUT=assets/snd
SFX_RATE=8000        # BarToZip's sfx are 8 kHz; keep them
MUS_RATE=22050      # music: N64 ROM budget -- resample down
MUS_MAXLEN=60       # music: trim to this many seconds (loops in-engine)
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"

# --- 1. run BarToZip on the .bar -----------------------------------------
python3 - "$BAR_SRC" "$TMP/doomrpg.bar" <<'PY'
import sys, io, zipfile
src, dst = sys.argv[1], sys.argv[2]
blob = open(src, "rb").read()
if blob[:2] == b"PK":
    z = zipfile.ZipFile(io.BytesIO(blob))
    name = next(n for n in z.namelist() if n.lower().endswith(".bar"))
    blob = z.read(name)
open(dst, "wb").write(blob)
PY
cp tools/bartozip/BarToZip.exe tools/bartozip/zlib.dll "$TMP/"
( cd "$TMP" && MSYS_NO_PATHCONV=1 ./BarToZip.exe >/dev/null 2>&1 )
[ -f "$TMP/DoomRPG.zip" ] || { echo "BarToZip produced no DoomRPG.zip"; exit 1; }

# --- 2. locate fluidsynth + a music SoundFont ---------------------------
FS=""
for c in "$(command -v fluidsynth || true)" \
         "$HOME/scoop/apps/fluidsynth/current/bin/fluidsynth.exe"; do
  [ -n "$c" ] && [ -x "$c" ] && { FS="$c"; break; }
done
: "${SF2:=tools/soundfont/SC-55 Deemster [GZDoom].sf2}"
[ -f "$SF2" ] || SF2="$(ls tools/soundfont/*.sf2 2>/dev/null | head -1 || true)"
[ -n "$FS" ] && [ -f "$SF2" ] || { echo "need fluidsynth + a .sf2 for the 3 music tracks"; exit 1; }
echo "music bank: $SF2"

# --- 3. split BarToZip's zip into assets/snd/ --------------------------
rm -f "$OUT"/*.wav
python3 - "$TMP/DoomRPG.zip" "$TMP" <<'PY'
import sys, zipfile, os
z = zipfile.ZipFile(sys.argv[1]); tmp = sys.argv[2]
sfx = mus = 0
for n in z.namelist():
    if n.endswith(".wav"):
        open(os.path.join("assets/snd", os.path.basename(n)), "wb").write(z.read(n))
        sfx += 1
    elif n.endswith(".mid"):
        open(os.path.join(tmp, os.path.basename(n)), "wb").write(z.read(n))
        mus += 1
print(f"  {sfx} sfx wav, {mus} music mid")
PY

for mid in "$TMP"/*.mid; do
  id="$(basename "$mid" .mid)"
  "$FS" -nli -q -g 0.6 -r 44100 -O s16 -T wav -F "$TMP/m.wav" "$SF2" "$mid"
  # trim to a loopable length, resample down, soften the loop seam, limit
  ffmpeg -nostdin -hide_banner -loglevel error -y -i "$TMP/m.wav" \
    -t "$MUS_MAXLEN" -ac 1 -ar "$MUS_RATE" \
    -af "afade=t=out:st=$(awk "BEGIN{print $MUS_MAXLEN-0.25}"):d=0.25,alimiter=limit=0.97:level=disabled" \
    "$OUT/$id.wav"
  echo "  music $id  <- $(basename "$mid")  (${MUS_MAXLEN}s @ ${MUS_RATE}Hz)"
done

echo "done -> $OUT/   (now: libdragon make)"
