#!/usr/bin/env bash
# build-tools.sh -- compile the 4 libdragon host tools build_rom.py needs,
# natively, straight from the core/libdragon submodule.
#
# n64tool, ed64romconfig and mkdfs are single-translation-unit C programs;
# audioconv64 is a small C/C++ project (plus the tools/common compressors),
# built here object by object in a temp dir.  This sidesteps
# core/libdragon/tools/Makefile and its MSYS2-environment checks, and never
# writes objects into the submodule.  n64elfcompress is NOT built here:
# core/Makefile's `engine` target already compresses doomrpg.elf.stripped.
#
#   ./build-tools.sh            # uses `gcc` / `g++` from PATH
#   CC=/path/to/gcc ./build-tools.sh      # CXX defaults to CC with gcc->g++
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TOOLS="$HERE/../../../core/libdragon/tools"
CC="${CC:-gcc}"
CXX="${CXX:-${CC%gcc}g++}"
COMMON="-O2 -I$TOOLS/../include -D__LIBDRAGON_INTERNAL_BUILD -DMSYS2_RUNTIME_PACMAN_AGE=0 -Wno-unused-result -Wno-error -static"
CF="-std=gnu11 $COMMON"
CXXF="-std=gnu++17 $COMMON -Wno-narrowing"
LIBS=""
"$CC" -dumpmachine | grep -qi mingw && LIBS="-lntdll"

[ -d "$TOOLS" ] || { echo "core/libdragon not checked out ($TOOLS)"; exit 1; }

OBJ="$(mktemp -d)"
trap 'rm -rf "$OBJ"' EXIT

set -x
"$CC" $CF -o "$HERE/n64tool.exe"        "$TOOLS/n64tool.c" $LIBS
"$CC" $CF -o "$HERE/ed64romconfig.exe"  "$TOOLS/ed64romconfig.c" $LIBS
"$CC" $CF -o "$HERE/mkdfs.exe"          "$TOOLS/mkdfs/mkdfs.c" $LIBS

A="$TOOLS/audioconv64"
for f in audioconv64 conv_wav64 conv_xm64 conv_ym64 conv_sf64 conv_mid64; do
    "$CXX" $CXXF -c -o "$OBJ/$f.o" "$A/$f.cpp"
done
"$CC" $CF -I"$A/vadpcm" -ffp-contract=off -fno-fast-math -c -o "$OBJ/libvadpcm.o" "$A/libvadpcm.c"
for f in libopus libsamplerate liblzh5 libxm libulc; do
    "$CC" $CF -c -o "$OBJ/$f.o" "$A/$f.c"
done
for f in binout assetcomp lz4_compress aplib_compress; do
    "$CC" $CF -c -o "$OBJ/common_$f.o" "$TOOLS/common/$f.c"
done
"$CXX" $CXXF -c -o "$OBJ/common_shrinkler_compress.o" "$TOOLS/common/shrinkler_compress.cpp"
"$CXX" -static -pthread -o "$HERE/audioconv64.exe" "$OBJ"/*.o $LIBS
set +x

echo "built: n64tool ed64romconfig mkdfs audioconv64  -> $HERE"
