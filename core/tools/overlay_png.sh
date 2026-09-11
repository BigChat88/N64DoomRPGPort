#!/usr/bin/env bash
# overlay_png.sh -- (re)generate the port's overlay BMPs from their source PNGs.
#
# The Help/About screens draw a single 320x240 picture that the engine blits 1:1
# (DoomCanvas_drawImage, no scaling), so every output is exactly 320x240.
#
# Overscan: MenuSystem_paint draws these two screens edge to edge, bypassing
# the small vertical safe margin (PD_SAFE_AREA_Y, DoomCanvas.c) that every
# other screen -- gameplay, HUD, the in-game pause menu -- gets baked into
# displayRect.  Left alone that made Help/About look inconsistent with the
# rest of the game: About came out full-bleed while the controller diagram
# (button labels sit right at the edges) got its own, much wider, hand-picked
# margin.  Both are now generated SAFE=1 with MARGIN_Y matching
# PD_SAFE_AREA_Y exactly, so their black top/bottom band lines up with the
# margin gameplay already reserves.  MARGIN_X has no engine equivalent
# (displayRect has zero horizontal inset at 320x240) but is kept small and
# non-zero so the safe box stays ~4:3 -- both source images are ~4:3 already,
# so at MARGIN_X=0 the aspect-preserving fit below would still pillarbox by
# almost the same amount on its own, just asymmetrically; picking it up front
# keeps the margin even on all four sides instead.
#
# Colour depth: DoomRPG-RE renders into a 16bpp framebuffer and the N64 display
# path further drops green's low bit, so whatever we feed it is shown as RGB555
# (32 levels per channel).  The old pipeline quantised to a 256-colour palette
# first and more than half of those entries collapsed onto the same RGB555
# value -- roughly 100 distinct colours reached the screen, with hard banding on
# the brick-wall gradient.  Instead we emit a 24-bit BMP whose pixels are
# Floyd-Steinberg dithered straight onto that RGB555 grid: pd_bmp.c reads 24-bit
# BMPs and SDL_CreateTextureFromSurface reduces them per pixel, so the engine's
# reduction becomes a no-op and the dithering hides the banding.  (DoomRPG.c's
# createImage skips its palette pre-round when the surface has no palette.)
#
#   assets/n64pad_src.png   ->  assets/overlay/n64pad.bmp    (controller diagram)
#   assets/aboutbg_src.png  ->  assets/overlay/aboutbg.bmp   (About background)
#
# tools/overlay_zip.py then folds assets/overlay/*.bmp into DoomRPG.zip.
#
#   bash tools/overlay_png.sh                 # convert whichever *_src.png exist
#   bash tools/overlay_png.sh in.png out.bmp  # one file (SAFE from basename)
#   DITHER=0 bash tools/overlay_png.sh        # flat RGB555 snap, no dithering
#   SAFE=0   bash tools/overlay_png.sh in.png out.bmp   # force full-bleed
#   MARGIN_X=20 MARGIN_Y=15 bash tools/overlay_png.sh   # widen the safe border
set -euo pipefail
cd "$(dirname "$0")/.."

DITHER="${DITHER:-1}"
MARGIN_X="${MARGIN_X:-11}"      # px trimmed from left AND right (keeps the safe box ~4:3)
MARGIN_Y="${MARGIN_Y:-8}"       # px trimmed from top  AND bottom -- matches PD_SAFE_AREA_Y

convert_one() {  # $1 = src png   $2 = dst bmp   $3 = safe (1/0)
    DITHER="$DITHER" MARGIN_X="$MARGIN_X" MARGIN_Y="$MARGIN_Y" SAFE="$3" \
    python3 - "$1" "$2" <<'PY'
import os, sys
import numpy as np
from PIL import Image, ImageFilter

src, dst = sys.argv[1], sys.argv[2]
dither = os.environ.get("DITHER", "1") != "0"
safe   = os.environ.get("SAFE", "0") != "0"
mx     = int(os.environ.get("MARGIN_X", "16"))
my     = int(os.environ.get("MARGIN_Y", "12"))

W, H = 320, 240
art = Image.open(src).convert("RGB")

if safe:
    bw, bh = W - 2 * mx, H - 2 * my          # TV-safe box
    # fit the whole artwork inside the box, preserve aspect
    s = min(bw / art.width, bh / art.height)
    rw, rh = max(1, round(art.width * s)), max(1, round(art.height * s))
    art = art.resize((rw, rh), Image.LANCZOS)
    img = Image.new("RGB", (W, H), (0, 0, 0))
    img.paste(art, ((W - rw) // 2, (H - rh) // 2))
else:
    img = art.resize((W, H), Image.LANCZOS)

# gentle sharpen: downscaling 4-5x softens the label text
img = img.filter(ImageFilter.UnsharpMask(radius=1.0, percent=55, threshold=3))

a = np.asarray(img, dtype=np.float32)
h, w, _ = a.shape

# Target grid: 5 bits/channel.  code = round(v * 31 / 255); the engine reads it
# back as v >> 3 and 8-bit representative code*255/31 floors to the same code,
# so this is exactly what ends up on screen.
def to555(v):
    code = np.clip(np.rint(v * (31.0 / 255.0)), 0, 31)
    return code * (255.0 / 31.0)

if dither:
    out = np.empty_like(a)                    # Floyd-Steinberg, serpentine
    for y in range(h):
        row = a[y]
        fwd = (y & 1) == 0
        rng = range(w) if fwd else range(w - 1, -1, -1)
        for x in rng:
            old = row[x].copy()
            new = to555(old)
            out[y, x] = new
            err = old - new
            nx = x + 1 if fwd else x - 1
            px = x - 1 if fwd else x + 1
            if 0 <= nx < w:
                row[nx] += err * (7 / 16)
            if y + 1 < h:
                if 0 <= px < w:
                    a[y + 1, px] += err * (3 / 16)
                a[y + 1, x] += err * (5 / 16)
                if 0 <= nx < w:
                    a[y + 1, nx] += err * (1 / 16)
    res = out
else:
    res = to555(a)

res = np.clip(np.rint(res), 0, 255).astype(np.uint8)
Image.fromarray(res, "RGB").save(dst, format="BMP")   # Pillow writes 24-bit BGR

shown = len(np.unique(res.reshape(-1, 3) >> 3, axis=0))
print(f"  {src} -> {dst}  (320x240, 24-bit, ~{shown} RGB555 colours"
      f"{', dithered' if dither else ''}{', TV-safe inset' if safe else ''})")
PY
}

# SAFE default: on for both overlays, so their margin matches PD_SAFE_AREA_Y
# instead of one being full-bleed and the other hand-tuned.
safe_for() { echo 1; }

if [ "$#" -eq 2 ]; then
    convert_one "$1" "$2" "${SAFE:-$(safe_for "$2")}"
    exit 0
fi

mkdir -p assets/overlay
shopt -s nullglob
found=0
for src in assets/*_src.png; do
    base="$(basename "${src%_src.png}")"
    convert_one "$src" "assets/overlay/$base.bmp" "${SAFE:-$(safe_for "$base")}"
    found=1
done
[ "$found" -eq 1 ] || { echo "no assets/*_src.png -- nothing to do"; exit 0; }
echo "done -- rebuild filesystem/DoomRPG.zip (libdragon make) to pack them"
