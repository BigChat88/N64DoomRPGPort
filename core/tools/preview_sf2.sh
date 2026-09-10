#!/usr/bin/env bash
# preview_sf2.sh -- render the Doom RPG MIDI cue set with one or more
# SoundFonts into audio_preview/<fontname>/ so you can A/B and pick one.
# Uses the exact same mapping + post-processing as tools/midi2snd.sh, so what
# you hear is what the ROM would get.
#
#   bash tools/preview_sf2.sh tools/MT6276.sf2 tools/Nokia_S30.sf2 ...
#
# Needs fluidsynth (auto-found under scoop) or falls back to the JDK/Gervill
# renderer used by midi2snd.sh.
set -euo pipefail
cd "$(dirname "$0")/.."

MIDI=java-version
RATE=44100
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

# --- fluidsynth / java locate (same logic as midi2snd.sh) ----------------
FS=""
for c in "$(command -v fluidsynth || true)" \
         "$HOME/scoop/apps/fluidsynth/current/bin/fluidsynth.exe" \
         "$HOME/scoop/shims/fluidsynth.exe"; do
  [ -n "$c" ] && [ -x "$c" ] && { FS="$c"; break; }
done
JAVA=""; for c in "${JAVA21:-}" "$HOME/scoop/apps/temurin21-jdk/current/bin/java" "$(command -v java || true)"; do
  [ -n "$c" ] && [ -x "$c" ] && { JAVA="$c"; break; }
done
JVMEXP=(--add-exports java.desktop/com.sun.media.sound=ALL-UNNAMED)
[ -n "$JAVA" ] && { mkdir -p tools/classes; "${JAVA%java}javac" "${JVMEXP[@]}" -d tools/classes tools/Midi2Wav.java 2>/dev/null || true; }

render() {  # $1 sf2  $2 in.mid  $3 out.wav(mono $RATE)
  if [ -n "$FS" ]; then
    "$FS" -nli -q -g 0.8 -r "$RATE" -O s16 -T wav -F "$TMP/s.wav" "$1" "$2"
    ffmpeg -nostdin -hide_banner -loglevel error -y -i "$TMP/s.wav" -ac 1 -ar "$RATE" "$3"
  elif [ -n "$JAVA" ]; then
    "$JAVA" "${JVMEXP[@]}" -cp tools/classes Midi2Wav "$1" "$2" "$3" "$RATE"
  else
    echo "no fluidsynth or JDK found"; exit 1
  fi
}

MAP=$(cat <<'EOF'
5039  i.mid   1   intro-theme
5040  e.mid   1   main-menu
5043  t.mid   1   ingame-theme
5090  2.mid   0   weapon-fire
5091  7.mid   0   explosion
5058  9.mid   0   pain-A
5059  10.mid  0   pain-B
5062  n.mid   0   death
5046  m.mid   0   menu-move
5042  p.mid   0   menu-select
5065  6.mid   0   pickup
5066  1.mid   0   door
5067  3.mid   0   event-A
5068  4.mid   0   event-B
5060  0.mid   0   misc-A
5061  8.mid   0   misc-B
EOF
)

echo "renderer: ${FS:-$JAVA}"
for sf2 in "$@"; do
  [ -f "$sf2" ] || { echo "skip missing $sf2"; continue; }
  name=$(basename "$sf2"); name=${name%.*}
  out="audio_preview/$name"; mkdir -p "$out"
  echo "=== $name ==="
  echo "$MAP" | while read -r id midi loop rest; do
    [ -z "${id:-}" ] && continue
    src="$MIDI/$midi"; [ -f "$src" ] || continue
    render "$sf2" "$src" "$TMP/r.wav"
    if [ "$loop" = 1 ]; then
      filt="aresample=$RATE"
    else
      filt="silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.01,areverse,silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.03,areverse,aresample=$RATE"
    fi
    ffmpeg -nostdin -hide_banner -loglevel error -y -i "$TMP/r.wav" -ac 1 -ar "$RATE" -af "$filt" "$out/${id}_${rest}.wav"
    printf '  %s\n' "${id}_${rest}.wav"
  done
done
echo "done -> audio_preview/"
