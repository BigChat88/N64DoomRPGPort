V ?= 1
BUILD_DIR = build
include $(N64_INST)/include/n64.mk

N64_ROM_TITLE = "Doom RPG"
# Config / Player / Player2 / World are persisted to cartridge SRAM (32 KiB)
# by src/port/pd_save.c.  This also stamps the EverDrive romconfig so the
# flashcart allocates and writes back the save.  SRAM (not FlashRAM): the N64
# FlashRAM write protocol was not honoured by the EverDrive-64 X7 emulation --
# the .fla file was created but never filled.  SRAM is a plain memory window
# with no command protocol and saves reliably on the same carts as EEPROM.
N64_ROM_SAVETYPE = sram256k

# --- source ---------------------------------------------------------------
GAME_SRC  = $(wildcard src/doomrpg/*.c)
PORT_SRC  = $(wildcard src/port/*.c)
GAME_OBJS = $(GAME_SRC:%.c=$(BUILD_DIR)/%.o)
PORT_OBJS = $(PORT_SRC:%.c=$(BUILD_DIR)/%.o)
OBJS      = $(GAME_OBJS) $(PORT_OBJS)

# Shim headers (<SDL.h>, <SDL_mixer.h>, <fluidsynth.h>, <zlib.h>) must win
# over anything else, then the port dir (SDL_Video.h), then the game dir.
CFLAGS += -Isrc/port/shim -Isrc/port -Isrc/doomrpg

# The vendored DoomRPG-RE tree targets MSVC and leans on a lot that libdragon's
# strict -Wall -Werror rejects (implicit decls, missing parens, fallthrough
# return paths).  Relax just those objects; port code stays strict.
$(GAME_OBJS): CFLAGS += -Wno-error -Wno-implicit-function-declaration \
          -Wno-parentheses -Wno-return-type -Wno-missing-braces \
          -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function \
          -Wno-sign-compare -Wno-format-truncation -Wno-format \
          -Wno-declaration-missing-parameter-type -Wno-builtin-declaration-mismatch \
          -Wno-implicit-int -Wno-error=implicit-int -Wno-old-style-definition \
          -Wno-pointer-sign -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
          -Wno-stringop-overflow -Wno-stringop-truncation -Wno-array-bounds \
          -fpermissive -Wno-error=implicit-function-declaration

# Phase 2 (perf) was tried twice: -O3 + -funroll-loops on Render.c / pd_video.c
# regressed rnd by ~2 ms both times.  The VR4300's I-cache is only 16 KiB and
# the extra code size thrashes it more than the unrolling helps, and the hot
# loops turned out to be memory-latency bound, not instruction bound.  So the
# renderer stays at the -O2 base.  pd_video.c keeps -fno-strict-aliasing only,
# which blit565()'s u16<->u32 SWAR puns require to be well-defined (no cost).
$(BUILD_DIR)/src/port/pd_video.o: CFLAGS += -fno-strict-aliasing

all: doomrpg.z64
.PHONY: all

# Asset pipeline: if the original BREW archive is present in the repo root,
# (re)generate filesystem/DoomRPG.zip from it with tools/bar2zip.py.
BAR_SRC := $(firstword $(wildcard doomrpg.zip doomrpg.bar DoomRPG.bar DoomRPG.zip))
# Port-only assets (controller help image, overridden help.txt, ...) that are
# not in the BREW archive.  tools/overlay_zip.py merges them in after bar2zip.
OVERLAY_FILES := $(wildcard assets/overlay/*)
ifneq ($(BAR_SRC),)
filesystem/DoomRPG.zip: $(BAR_SRC) tools/bar2zip.py tools/overlay_zip.py $(OVERLAY_FILES)
	python3 tools/bar2zip.py $(BAR_SRC) $@
	python3 tools/overlay_zip.py $@ assets/overlay
endif

# libdragon splash logo -> RGBA16 sprite in the DragonFS image
LOGO_SRC := libdragon/website/logo2.png
filesystem/libdragon_logo.sprite: $(LOGO_SRC)
	$(N64_MKSPRITE) -f RGBA16 -o filesystem $(LOGO_SRC)
	mv filesystem/logo2.sprite $@

# Audio: drop pre-converted PCM WAVs named <resourceID>.wav in assets/snd/ and
# they get packed as rom:/snd/<resourceID>.wav64 (VADPCM).  Nothing there yet
# just means the game boots silent -- pd_sound.c handles missing entries.
SFX_WAV   := $(wildcard assets/snd/*.wav)
SFX_WAV64 := $(SFX_WAV:assets/snd/%.wav=filesystem/snd/%.wav64)

filesystem/snd/%.wav64: assets/snd/%.wav
	@mkdir -p $(dir $@)
	$(N64_AUDIOCONV) --wav-compress 0 -o $(dir $@) $<

# Per-level background music: rom:/mus/<map>.wav64, one streamed Opus loop per
# map, mixed on its own channel by pd_sound.c.  <map> matches the .bsp basenames
# (intro, junction, junction_destroyed, level01..level07, reactor).
#
# Source WAVs live in assets/new/ -- hand-trimmed 48 kHz mono loops, already
# edited so the loop seam sounds clean.  They are used as-is: audioconv64 only
# Opus-compresses and marks the whole file as looping, it does NOT trim them.
# (tools/ost_setup.sh is the old ffmpeg auto-cut path and is no longer used.)
MUS_WAV   := $(wildcard assets/new/*.wav)
MUS_WAV64 := $(MUS_WAV:assets/new/%.wav=filesystem/mus/%.wav64)

filesystem/mus/%.wav64: assets/new/%.wav
	@mkdir -p $(dir $@)
	$(N64_AUDIOCONV) --wav-compress 3 --wav-mono --wav-loop true -o $(dir $@) $<

doomrpg.z64: $(BUILD_DIR)/doomrpg.dfs
$(BUILD_DIR)/doomrpg.dfs: filesystem/DoomRPG.zip filesystem/libdragon_logo.sprite $(SFX_WAV64) $(MUS_WAV64) $(wildcard filesystem/*)
$(BUILD_DIR)/doomrpg.elf: $(OBJS)

# Engine-only build.  Produces build/doomrpg.elf.stripped + build/doomrpg.elf.sym
# -- the two inputs ../app/build_rom.py needs -- WITHOUT a BREW archive or any
# game assets (the full `doomrpg.z64` target needs those).
#
# n64.mk applies the N64 toolchain (compiler, -march=vr4300, libdragon include
# path, ...) only as target-specific variables on `%.z64` -- which propagate to
# its prerequisites.  A target outside that chain compiles with the host `cc`
# and no libdragon.h, so the same assignments are repeated here.
.PHONY: engine
engine: CC=$(N64_CC)
engine: CXX=$(N64_CXX)
engine: AS=$(N64_AS)
engine: LD=$(N64_LD)
engine: CFLAGS+=$(N64_CFLAGS)
engine: CXXFLAGS+=$(N64_CXXFLAGS)
engine: ASFLAGS+=$(N64_ASFLAGS)
engine: RSPASFLAGS+=$(N64_RSPASFLAGS)
engine: LDFLAGS+=$(N64_LDFLAGS)
engine: $(BUILD_DIR)/doomrpg.elf.stripped $(BUILD_DIR)/doomrpg.elf.sym

$(BUILD_DIR)/doomrpg.elf.sym: $(BUILD_DIR)/doomrpg.elf
	$(N64_SYM) $< $@

$(BUILD_DIR)/doomrpg.elf.stripped: $(BUILD_DIR)/doomrpg.elf
	cp $< $@
	$(N64_STRIP) -s $@
	$(N64_ELFCOMPRESS) -o $(dir $@) -c $(N64_ROM_ELFCOMPRESS) $@

clean:
	rm -rf $(BUILD_DIR) doomrpg.z64
.PHONY: clean

-include $(wildcard $(BUILD_DIR)/*.d)
-include $(wildcard $(BUILD_DIR)/src/*/*.d)
