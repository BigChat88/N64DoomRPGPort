#!/usr/bin/env python3
"""
bar2zip.py -- build DoomRPG.zip (the archive DoomRPG-RE expects) from the
original BREW distribution.

Input may be either:
  * doomrpg.bar                          (the BREW asset archive), or
  * a .zip containing doomrpg.bar        (as commonly distributed)

The .bar stores each asset as a gzip member whose original name is carried in
the gzip FNAME field (entities.db, sintable.bin, palettes.bin, *.bsp, *.bmp,
help.txt, ...).  We recover every gzip member and repackage them, by name, into
a normal DEFLATE zip that Z_Zip.c / readZipFileEntry() can read on the N64.

NOT yet handled: the ~95 'application/octet-stream' PMD sound resources.  Those
need a PMD decoder and a resource-id -> name map; milestone-1 audio is silent,
and Z_Zip.c returns NULL for the missing '<id>.wav' / '<id>.mid' entries.

Usage:
    python tools/bar2zip.py doomrpg.zip            # -> filesystem/DoomRPG.zip
    python tools/bar2zip.py doomrpg.bar out.zip
"""
import sys, os, io, gzip, struct, zipfile, zlib


def load_bar(path: str) -> bytes:
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:2] == b"PK":
        z = zipfile.ZipFile(io.BytesIO(blob))
        name = next((n for n in z.namelist() if n.lower().endswith(".bar")), None)
        if not name:
            sys.exit(f"{path}: zip contains no .bar file")
        return z.read(name)
    return blob


def extract_gzip_members(bar: bytes):
    """Yield (name, data) for every gzip member found in the BAR."""
    out, i, n = [], 0, len(bar)
    while True:
        j = bar.find(b"\x1f\x8b\x08", i)
        if j < 0:
            break
        flg = bar[j + 3]
        if flg & 0xE0:                      # reserved bits set -> false match
            i = j + 1
            continue
        try:
            pos = j + 10
            if flg & 4:                     # FEXTRA
                xlen = struct.unpack_from("<H", bar, pos)[0]
                pos += 2 + xlen
            name = None
            if flg & 8:                     # FNAME
                end = bar.index(0, pos)
                name = bar[pos:end].decode("latin1")
                pos = end + 1
            if flg & 16:                    # FCOMMENT
                pos = bar.index(0, pos) + 1
            if flg & 2:                     # FHCRC
                pos += 2
            dec = zlib.decompressobj(-15)
            data = dec.decompress(bar[pos:]) + dec.flush()
            trailer = bar[pos + (len(bar) - pos - len(dec.unused_data)):]
            crc, isize = struct.unpack_from("<II", trailer, 0)
            if isize != (len(data) & 0xFFFFFFFF) or zlib.crc32(data) != crc:
                raise ValueError("gzip trailer mismatch")
            if not name:
                raise ValueError("gzip member without FNAME")
            out.append((name, data))
            i = pos + (len(bar) - pos - len(dec.unused_data)) + 8
        except Exception as ex:             # noqa: BLE001
            print(f"  skip @0x{j:06x}: {ex}")
            i = j + 1
    return out


def main() -> None:
    src = sys.argv[1] if len(sys.argv) > 1 else "doomrpg.zip"
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join("filesystem", "DoomRPG.zip")

    bar = load_bar(src)
    members = extract_gzip_members(bar)
    if not members:
        sys.exit(f"{src}: no gzip members found -- not a Doom RPG .bar?")

    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in members:
            z.writestr(name, data)

    total = sum(len(d) for _, d in members)
    print(f"{src} -> {dst}")
    print(f"  {len(members)} entries, {total} bytes uncompressed, "
          f"{os.path.getsize(dst)} bytes packed")
    for name, data in sorted(members):
        print(f"    {len(data):9d}  {name}")


if __name__ == "__main__":
    main()
