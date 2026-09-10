#!/usr/bin/env python3
"""
overlay_zip.py -- fold the port's extra assets into filesystem/DoomRPG.zip.

bar2zip.py rebuilds DoomRPG.zip straight from the original BREW archive, so any
asset the N64 port adds (or wants to override) has to be merged back in
afterwards.  Every regular file in the overlay dir is written into the zip,
replacing an existing entry of the same name.

    python3 tools/overlay_zip.py filesystem/DoomRPG.zip assets/overlay
"""
import os
import sys
import zipfile


def main() -> None:
    zip_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join("filesystem", "DoomRPG.zip")
    overlay_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join("assets", "overlay")

    if not os.path.isdir(overlay_dir):
        print(f"{overlay_dir}: no overlay dir, nothing to do")
        return

    names = sorted(f for f in os.listdir(overlay_dir)
                   if os.path.isfile(os.path.join(overlay_dir, f)))
    if not names:
        print(f"{overlay_dir}: empty, nothing to do")
        return

    with zipfile.ZipFile(zip_path) as z:
        kept = [(i.filename, z.read(i.filename))
                for i in z.infolist() if i.filename not in names]

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for filename, data in kept:
            z.writestr(filename, data)
        for name in names:
            with open(os.path.join(overlay_dir, name), "rb") as f:
                z.writestr(name, f.read())
            print(f"  overlay + {name}")

    print(f"{zip_path}: {len(kept)} kept, {len(names)} overlaid")


if __name__ == "__main__":
    main()
