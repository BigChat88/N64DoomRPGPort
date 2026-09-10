#!/usr/bin/env bash
# ost_setup.sh -- prepare the per-level background music for the N64 build.
#
# The build container has no ffmpeg, so this host-side step turns the source
# tracks in assets/doomOST/*.mp3 into the trimmed mono WAVs the Makefile feeds
# to audioconv64:
#
#   assets/doomOST/<map>.mp3  ->  assets/doomOST/<map>.wav
#     - cut to MAXLEN seconds (the track loops in-game)
#     - downmixed to mono, resampled to 48 kHz (audioconv64's Opus rate)
#     - 1 s fade-in and a fade-out just before the cut, so the loop seam
#       does not click
#
# The Makefile then runs:
#   audioconv64 --wav-compress 3 --wav-mono --wav-loop true
#     assets/doomOST/<map>.wav  ->  filesystem/mus/<map>.wav64
# and pd_sound.c streams rom:/mus/<map>.wav64 for the matching map.
#
#   bash tools/ost_setup.sh                # 120 s loops (default)
#   MAXLEN=90 bash tools/ost_setup.sh      # shorter loops, smaller ROM
#
# Map basenames must match the .bsp files:
#   intro junction junction_destroyed level01..level07 reactor
set -euo pipefail
cd "$(dirname "$0")/.."

SRC=assets/doomOST
MAXLEN="${MAXLEN:-120}"          # seconds kept from each track
RATE=48000                       # audioconv64 resamples Opus to 48 kHz anyway
FADE_OUT=3                       # seconds

command -v ffmpeg >/dev/null || { echo "ffmpeg not found on PATH"; exit 1; }
shopt -s nullglob
mp3s=("$SRC"/*.mp3)
[ ${#mp3s[@]} -gt 0 ] || { echo "no $SRC/*.mp3 -- nothing to do"; exit 0; }

fade_start=$(awk "BEGIN{ s = $MAXLEN - $FADE_OUT; print (s > 0 ? s : 0) }")

for mp3 in "${mp3s[@]}"; do
    name="$(basename "${mp3%.mp3}")"
    out="$SRC/$name.wav"
    echo "  [OST] $name  (<=${MAXLEN}s, mono, ${RATE}Hz)"
    ffmpeg -y -v error -i "$mp3" -t "$MAXLEN" -ac 1 -ar "$RATE" \
        -af "afade=t=in:d=1,afade=t=out:st=${fade_start}:d=${FADE_OUT}" \
        "$out"
done

echo "done -- now run:  libdragon make"
