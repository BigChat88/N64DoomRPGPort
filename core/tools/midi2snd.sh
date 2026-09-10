#!/usr/bin/env bash
# DEPRECATED -- use tools/audio_setup.sh instead.  That runs BarToZip.exe which
# decodes every BREW PMD resource (92 sfx as WAV + 3 music as MIDI) by exact
# resource id.  This script rendered the smaller J2ME MIDI set and guessed a
# mapping; kept only for reference.
#
# midi2snd.sh -- render the J2ME "Doom RPG" MIDIs (java-version/*.mid) into
# assets/snd/<resourceID>.wav so the Makefile packs them as rom:/snd/*.wav64.
#
# Renderer, best first:
#   1. fluidsynth + tools/soundfont/Doom.sf2            (if `fluidsynth` is on PATH)
#   2. JDK / Gervill synth + tools/soundfont/Doom.sf2   (needs a Java 17-21 in PATH or
#      $JAVA21 / scoop temurin21) -- no install beyond the JDK
#   3. ffmpeg via libmodplug                  (thin/harsh, last resort)
#
# Override the bank with  SF2=/path/to/bank.sf2 .
#
# The id<-midi mapping is a best guess from each MIDI's length / GM programs and
# where the engine calls Sound_playSound().  Music (5039/5040/5043) is solid;
# tweak the SFX rows by ear and re-run.
set -euo pipefail
cd "$(dirname "$0")/.."

MIDI=java-version
OUT=assets/snd
RATE=44100
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"

# --- soundfont -----------------------------------------------------------
: "${SF2:=tools/soundfont/SC-55 Deemster [GZDoom].sf2}"
[ -f "$SF2" ] || { echo "SF2 not found: $SF2"; SF2=""; }

# --- locate fluidsynth (PATH or scoop) ---------------------------------------
FS=""
for cand in "$(command -v fluidsynth || true)" \
            "$HOME/scoop/apps/fluidsynth/current/bin/fluidsynth.exe" \
            "$HOME/scoop/shims/fluidsynth.exe"; do
  [ -n "$cand" ] && [ -x "$cand" ] && { FS="$cand"; break; }
done

# --- locate a JDK that still exposes com.sun.media.sound (<= 21) ---------
JAVA=""
for cand in "${JAVA21:-}" \
            "$HOME/scoop/apps/temurin21-jdk/current/bin/java" \
            "$HOME/scoop/apps/temurin17-jdk/current/bin/java" \
            "$(command -v java || true)"; do
  [ -n "$cand" ] && [ -x "$cand" ] && { JAVA="$cand"; break; }
  command -v "$cand" >/dev/null 2>&1 && { JAVA="$cand"; break; }
done
JVMEXP=(--add-exports java.desktop/com.sun.media.sound=ALL-UNNAMED)

# --- choose renderer ---------------------------------------------------------
RENDERER=modplug
if [ -n "$SF2" ] && [ -n "$FS" ]; then
  RENDERER=fluidsynth
elif [ -n "$SF2" ] && [ -n "$JAVA" ]; then
  RENDERER=java
  "$JAVA" "${JVMEXP[@]}" -version >/dev/null 2>&1 || { echo "java too new for internal synth; falling back"; RENDERER=modplug; }
  if [ "$RENDERER" = java ]; then
    mkdir -p tools/classes
    "${JAVA%java}javac" "${JVMEXP[@]}" -d tools/classes tools/Midi2Wav.java
  fi
fi
echo "renderer: $RENDERER${SF2:+  (bank: $SF2)}${JAVA:+  (java: $JAVA)}"

render() {  # $1 = src .mid   $2 = dst wav (mono, $RATE)  -- kept in $TMP/g.wav pre-limit
  case "$RENDERER" in
    fluidsynth)
      "$FS" -nli -q -g 0.55 -r "$RATE" -O s16 -T wav -F "$TMP/s.wav" "$SF2" "$1"
      cp "$TMP/s.wav" "$2" ;;
    java)
      "$JAVA" "${JVMEXP[@]}" -cp tools/classes Midi2Wav "$SF2" "$1" "$2" "$RATE" ;;
    *)
      ffmpeg -nostdin -hide_banner -loglevel error -y -i "$1" "$2" ;;
  esac
}

# resourceID  midi     loop  what it is
#   music ids (i/e/t) are solid.  SFX roles below come from the user's notes on
#   what each engine id actually is; the exact .mid per id is still a partly
#   informed guess -- edit freely and re-run.
MAP=$(cat <<'EOF'
5039  i.mid   1   music: intro / story
5040  e.mid   1   music: main menu
5043  t.mid   1   music: in-game
5042  9.mid   0   pick up item
5065  6.mid   0   ray / plasma gun
5058  10.mid  0   dog A
5059  11.mid  0   dog B
5060  0.mid   0   axe swing
5066  1.mid   0   fire extinguisher
5067  7.mid   0   barrel explosion
5068  3.mid   0   machine gun
5090  2.mid   0   weapon fire / pistol
5091  8.mid   0   entity / monster
5061  4.mid   0   misc (Game.c:199)
5062  n.mid   0   player sustained (Player.c:854)
5046  m.mid   0   menu select / action click
EOF
)

echo "$MAP" | while read -r id midi loop rest; do
  [ -z "${id:-}" ] && continue
  src="$MIDI/$midi"
  [ -f "$src" ] || { echo "skip $id: $src missing"; continue; }

  render "$src" "$TMP/r.wav"

  # mono + $RATE, trim dead air at start (and end for SFX), brickwall limiter
  # as a safety net against the odd inter-sample peak.  No dynamic loudnorm --
  # it pumps the short cues; the mixer sets overall level in-engine.
  lim="alimiter=level_in=1:limit=0.97:attack=1:release=20:level=disabled"
  if [ "$loop" = 1 ]; then
    filt="aresample=$RATE,$lim"
  else
    filt="silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.01,areverse,silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.03,areverse,aresample=$RATE,$lim"
  fi
  ffmpeg -nostdin -hide_banner -loglevel error -y -i "$TMP/r.wav" \
    -ac 1 -ar "$RATE" -af "$filt" "$OUT/$id.wav"
  printf '  %-6s <- %-7s %s\n' "$id" "$midi" "$rest"
done

echo "done -> $OUT/   (now: libdragon make)"
