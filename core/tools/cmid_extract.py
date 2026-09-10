#!/usr/bin/env python3
"""
cmid_extract.py -- pull the 95 CMX "cmid" audio resources out of doomrpg.bar.

The BREW .bar stores each sound as a resource that begins with the 6-byte
magic  b"\\x00\\x00cmid"  followed by a big-endian uint32 body length.  They
appear in the archive in the same order as Sound.c's soundTable[], so we can
name each one by its engine resource id.

    python tools/cmid_extract.py doomrpg.zip            # -> assets/cmid/<id>.cmid
    python tools/cmid_extract.py doomrpg.bar out_dir

Output: assets/cmid/<id>.cmid  (raw CMX), plus a manifest.csv listing
id, kind (SONG = music / WAVE = sfx), size.

Next step (not done here): cmid -> .mid -> .wav -> assets/snd/<id>.wav ->
audioconv64 -> rom:/snd/<id>.wav64 .  See PORTING.md "Audio".
"""
import sys, os, io, re, struct, zipfile

# Order matches src/doomrpg/Sound.c  soundTable[MAX_AUDIOFILES].
SOUND_TABLE = [
    5039, 5040, 5042, 5043, 5044, 5045, 5046, 5047, 5048, 5049, 5050,
    5051, 5052, 5053, 5054, 5055, 5057, 5058, 5059, 5060, 5061, 5062,
    5063, 5064, 5065, 5066, 5067, 5068, 5069, 5070, 5071, 5072, 5073,
    5074, 5076, 5077, 5078, 5079, 5080, 5081, 5082, 5083, 5084, 5085,
    5086, 5087, 5088, 5089, 5090, 5091, 5092, 5093, 5094, 5095, 5096,
    5097, 5098, 5099, 5100, 5101, 5102, 5103, 5104, 5105, 5106, 5107,
    5108, 5109, 5110, 5111, 5112, 5113, 5114, 5115, 5116, 5117, 5118,
    5119, 5120, 5121, 5122, 5123, 5124, 5125, 5126, 5127, 5128, 5129,
    5130, 5131, 5133, 5134, 5136, 5137, 5138,
]

MAGIC = b"\x00\x00cmid"


def load_bar(path: str) -> bytes:
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:2] == b"PK":
        z = zipfile.ZipFile(io.BytesIO(blob))
        name = next((n for n in z.namelist() if n.lower().endswith(".bar")), None)
        if not name:
            sys.exit(f"{path}: zip has no .bar")
        return z.read(name)
    return blob


def cnts_kind(body: bytes) -> str:
    i = body.find(b"cnts")
    if i >= 0 and len(body) >= i + 10:
        return body[i + 6:i + 10].decode("latin1", "replace")
    return "?"


def main() -> None:
    src = sys.argv[1] if len(sys.argv) > 1 else "doomrpg.zip"
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join("assets", "cmid")

    bar = load_bar(src)
    offs = [m.start() for m in re.finditer(re.escape(MAGIC), bar)]
    if len(offs) != len(SOUND_TABLE):
        print(f"warning: found {len(offs)} cmid resources, "
              f"expected {len(SOUND_TABLE)} -- id mapping may be off")

    os.makedirs(out, exist_ok=True)
    rows = []
    for i, o in enumerate(offs):
        (body_len,) = struct.unpack_from(">I", bar, o + 6)
        end = o + 10 + body_len
        nxt = offs[i + 1] if i + 1 < len(offs) else len(bar)
        if end > nxt:                       # declared length ran past next resource
            end = nxt
        data = bar[o:end]
        rid = SOUND_TABLE[i] if i < len(SOUND_TABLE) else 90000 + i
        kind = cnts_kind(data)
        path = os.path.join(out, f"{rid}.cmid")
        with open(path, "wb") as f:
            f.write(data)
        rows.append((rid, kind, len(data)))

    with open(os.path.join(out, "manifest.csv"), "w") as f:
        f.write("id,kind,bytes\n")
        for rid, kind, n in rows:
            f.write(f"{rid},{kind},{n}\n")

    songs = [r for r in rows if r[1] == "SONG"]
    print(f"{src} -> {out}/  ({len(rows)} files)")
    print(f"  music (SONG): {[r[0] for r in songs]}")
    print(f"  sfx  (WAVE) : {len(rows) - len(songs)} files")


if __name__ == "__main__":
    main()
