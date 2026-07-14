# SPDX-License-Identifier: GPL-2.0-or-later
# ===========================================================================
# u6e — top-level build (m68k-atari-mint cross-toolchain)
# ===========================================================================
# Toolchain + rationale: docs/design/toolchain.md
#
#   make                  build the M0 self-test .PRG for base ST (68000)
#   make ARCH=tt          build the -m68020-60 variant (TT/Falcon)
#   make run              build (EmuTOS) + launch under Hatari, ST machine
#   make run-st|ste|tt|falcon   launch under the matching emulated machine
#   make shot             headless: run N VBLs, save a screenshot, quit
#   make clean
# ===========================================================================

CROSS  := m68k-atari-mint-
CC     := $(CROSS)gcc

# ---- target architecture -------------------------------------------------
# Base ST 68000 is the floor; TT/Falcon use the 68020-60 multilib.
ARCH ?= st
ifneq (,$(filter $(ARCH),tt falcon))
  ARCHFLAGS := -m68020-60
  SUFFIX    := _020
else
  ARCHFLAGS := -m68000
  SUFFIX    :=
endif

CFLAGS := $(ARCHFLAGS) -std=gnu11 -Os -Wall -Wextra -Iplatform
LDFLAGS :=

BUILD := build

# ---- self-test (M0 detect + M1 mem/file/timing) --------------------------
SELFTEST_SRC := platform/detect.c platform/platmem.c platform/platfile.c \
                platform/plattime.c platform/selftest.c
SELFTEST_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(SELFTEST_SRC))
SELFTEST_PRG := $(BUILD)/selftest$(SUFFIX).prg

# ---- emulator (Hatari) ---------------------------------------------------
HATARI   ?= hatari
TOS_DIR  ?= /usr/share/hatari
# Real per-machine TOS for authentic testing (the ROMs the games shipped against).
# Override on the command line if your paths differ.
TOS_ST   ?= $(TOS_DIR)/TOS v1.04 (1989)(Atari)(US)[Rainbow TOS][Mega ST].img
TOS_STE  ?= $(TOS_DIR)/TOS v2.06 (1991)(Atari Corp)(Mega-STE)(US).img
TOS_TT   ?= $(TOS_DIR)/TOS v3.06 (1991)(Atari)(GB)[TT].img
TOS_FALC ?= $(TOS_DIR)/TOSv4.04.img
# EmuTOS (updated 1.4) for the portable cross-machine dev path.
EMUTOS   ?= $(HOME)/Downloads/emutos-512k-1.4/etos512us.img
# Atari path to the autostarted program on the GEMDOS-emulated drive C:.
AUTO_PRG := c:\\$(notdir $(SELFTEST_PRG))

# common Hatari invocation: mount build/ as C:, autostart the .PRG
HATARI_COMMON = --harddrive $(BUILD) --auto '$(AUTO_PRG)'

.PHONY: all clean run run-st run-ste run-tt run-falcon shot check check-st check-tt trace dftest mapdump objrender convspike convtest enginetest pack rdemo run-rdemo bench bench-intro m3 world world-svm gamedata game run-game m5 m5-svm uipreview portdump bmpdump npcsheet convvm cvmtest npcsched refshot save-roundtrip save-check save-engine m6-anim m6-npc

all: $(SELFTEST_PRG)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Link the DEFAULT 68000 soft-float libc (no checkcpu FPU gate). The objects keep their
# per-ARCH codegen — so the TT/Falcon (-m68020-60) selftest is hybrid 020/no-FPU like
# game.prg, and the ST (-m68000) build is unchanged. See docs/design/toolchain.md.
$(SELFTEST_PRG): $(SELFTEST_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "built $@"

# --- run targets: EmuTOS works on every machine; real TOS for accuracy ---
run: run-st

run-st: all
	$(HATARI) --machine st     --tos '$(TOS_ST)' $(HATARI_COMMON)

run-ste: all
	$(HATARI) --machine ste    --tos '$(TOS_STE)' $(HATARI_COMMON)

# TT/Falcon: 32-bit addressing + TT-RAM so the Alt-RAM allocator path is live.
TT_FLAGS := --ttram 4 --addr24 off

run-tt: $(BUILD)/selftest_020.prg
	$(HATARI) --machine tt     --tos '$(TOS_TT)' $(TT_FLAGS) --harddrive $(BUILD) --auto 'c:\selftest_020.prg'

run-falcon: $(BUILD)/selftest_020.prg
	$(HATARI) --machine falcon --tos '$(EMUTOS)' --harddrive $(BUILD) --auto 'c:\selftest_020.prg'

# headless TEXT self-verify (unattended): autostart, mirror the VT-52 console to
# host stdout, fast-forward, quit. `check` runs both the ST (fallback) and TT
# (Alt-RAM) paths so M1's chip-vs-fast acceptance is covered.
check: check-st check-tt

check-st: all
	@echo "### self-test: ST (ST-RAM fallback path) ###"
	$(HATARI) --machine st --tos '$(TOS_ST)' $(HATARI_COMMON) \
	          --conout 2 --fast-forward on --run-vbls 2500 2>/dev/null

check-tt: $(BUILD)/selftest_020.prg
	@echo "### self-test: TT (Alt-RAM path) ###"
	$(HATARI) --machine tt --tos '$(EMUTOS)' $(TT_FLAGS) \
	          --harddrive $(BUILD) --auto 'c:\selftest_020.prg' \
	          --conout 2 --fast-forward on --run-vbls 4000 2>/dev/null

# headless VIDEO capture (for golden-image/video comparison later). Plain
# --screenshot-dir needs an interactive trigger, so we record an AVI instead.
shot: all
	$(HATARI) --machine st --tos '$(EMUTOS)' $(HATARI_COMMON) \
	          --fast-forward on --run-vbls 2000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/selftest.avi

# ---- host-side tools (run on the DEV PC, not the ST) ---------------------
# Portable /dataformats decoders compiled with the HOST compiler for fast
# validation against real data in /refdata (decode-correctness before hardware).
HOSTCC     ?= cc
HOSTCFLAGS ?= -O2 -std=gnu11 -Wall -Wextra -Idataformats
DF_SRC     := dataformats/u6lzw.c dataformats/u6lib.c
REFDATA    ?= refdata/dos/u6

$(BUILD)/dftest: tools/dftest.c $(DF_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^

# validate the data-format decoders against real U6 files
dftest: $(BUILD)/dftest
	$(BUILD)/dftest $(REFDATA)

# decode the world + tiles and render PNGs (visual proof of M2 decode)
MAPDUMP_SRC := dataformats/u6lzw.c dataformats/u6pal.c dataformats/u6map.c \
               dataformats/u6tile.c tools/png.c
$(BUILD)/mapdump: tools/mapdump.c $(MAPDUMP_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^

mapdump: $(BUILD)/mapdump
	$(BUILD)/mapdump $(REFDATA) $(BUILD)

# M7 low-colour quantiser: 256-colour VGA art -> optimised 16-colour ST/STE palette (snapped to
# the 512/4096 hardware gamut), with a side-by-side QA PNG + RMS error vs a naive EGA-16 baseline.
# splitcheck: HOST experiment — does the right PANEL (parchment / conversation portrait) want a
# different 16 than the MAP's tileset palette? (i.e. is a mid-line palette split worth it?) Prints
# RMS + writes build/splitcheck.png (portrait: VGA | via terrain-16 | via a portrait-16).
SPLITCHECK_SRC := dataformats/u6lzw.c dataformats/u6lib.c tools/png.c
$(BUILD)/splitcheck: tools/splitcheck.c $(SPLITCHECK_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -Itools -o $@ $^ -lm
splitcheck: $(BUILD)/splitcheck world
	$(BUILD)/splitcheck $(REFDATA) $(BUILD)/U6.PAK $(BUILD)/splitcheck.png

QUANTIZE_SRC := dataformats/u6lzw.c dataformats/u6pal.c dataformats/u6map.c dataformats/u6tile.c tools/png.c
$(BUILD)/quantize: tools/quantize.c $(QUANTIZE_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^ -lm
quantize: $(BUILD)/quantize
	$(BUILD)/quantize $(REFDATA) $(BUILD)

# M6 object render validation: decode objects + draw a region with objects
OBJR_SRC := dataformats/u6lzw.c dataformats/u6pal.c dataformats/u6map.c \
            dataformats/u6tile.c dataformats/u6obj.c tools/png.c
$(BUILD)/objrender: tools/objrender.c $(OBJR_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
objrender: $(BUILD)/objrender
	$(BUILD)/objrender $(REFDATA) $(BUILD)/objrender.png

# M10 music format decode: LZW-decompress a .M music file + disassemble the OPL2 command stream
# (docs/formats/music.md). `make mdump` disassembles all 12 U6 .M files in summary mode — a CLEAN
# END on each validates the opcode/operand table. `build/mdump FILE.M` shows the full disassembly.
$(BUILD)/mdump: tools/mdump.c dataformats/u6lzw.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
.PHONY: mdump
mdump: $(BUILD)/mdump
	@for f in $(REFDATA)/*.M; do $(BUILD)/mdump -q "$$f" | grep SUMMARY; done

# M10 note extraction: .M OPL stream -> Standard MIDI File (host validation of the pitch/timing
# decode BY EAR + the note-extraction logic the MIDI-OUT backend reuses). Exports every .M to
# build/mid/*.mid; if a soundfont is found, renders a few to WAV via fluidsynth so you can listen.
# Outputs derive from the copyrighted score -> build/ (gitignored), never committed.
$(BUILD)/m2mid: tools/m2mid.c dataformats/u6lzw.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^ -lm
SF2 ?= /usr/share/scummvm/Roland_SC-55.sf2
.PHONY: m2mid
m2mid: $(BUILD)/m2mid
	@mkdir -p $(BUILD)/mid
	@for f in $(REFDATA)/*.M; do b=$$(basename "$$f" .M); $(BUILD)/m2mid "$$f" "$(BUILD)/mid/$$b.mid"; done
	@if [ -f "$(SF2)" ] && command -v fluidsynth >/dev/null; then \
	   for m in ULTIMA BRIT STONES INTRO; do fluidsynth -ni -F "$(BUILD)/mid/$$m.wav" "$(SF2)" "$(BUILD)/mid/$$m.mid" >/dev/null 2>&1; done; \
	   echo "rendered $(BUILD)/mid/{ULTIMA,BRIT,STONES,INTRO}.wav (soundfont $(SF2))"; \
	 else echo "no soundfont at $(SF2); play $(BUILD)/mid/*.mid with your own synth"; fi

# M10 music: HOST unit test for the on-target sequencer (engine/music.c) — runs it through a capture
# backend into an SMF (follows the real loop/branch structure). A clean run over all 12 .M validates it.
$(BUILD)/musictest: tools/musictest.c engine/music.c dataformats/u6lzw.c
	@mkdir -p $(BUILD)/mid
	$(HOSTCC) $(HOSTCFLAGS) -Iengine -o $@ $^ -lm
.PHONY: musictest
musictest: $(BUILD)/musictest
	@for f in $(REFDATA)/*.M; do $(BUILD)/musictest "$$f" $(BUILD)/mid/`basename "$$f" .M`_seq.mid 60; done

# M10 ON-TARGET music: stage a .M + MIDI.DAT + MUSIC.REQ and launch the game in Hatari with MIDI on.
# With your hatari.cfg [Midi] sMidiOutPortName pointing at your MT-32 (e.g. the UM-ONE), the ENGINE
# plays U6's score on the real synth via the emulated ST MIDI OUT. MUSIC_SONG picks the .M.
MUSIC_SONG ?= ULTIMA.M
.PHONY: music-tt
music-tt: gamedata $(BUILD)/game.prg
	cp '$(REFDATA)/$(MUSIC_SONG)' '$(REFDATA)/MIDI.DAT' $(BUILD)/
	printf '$(MUSIC_SONG)' > $(BUILD)/MUSIC.REQ
	-$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga --midi on \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\game.prg'
	rm -f $(BUILD)/MUSIC.REQ
	@echo "played $(MUSIC_SONG) out MIDI (routed by your hatari.cfg [Midi] port)"

# M4 usecode spike: run a real NPC conversation end-to-end (decision gate)
CONVSPIKE_SRC := dataformats/u6lzw.c dataformats/u6conv.c
$(BUILD)/convspike: tools/convspike.c $(CONVSPIKE_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^

convspike: $(BUILD)/convspike
	$(BUILD)/convspike $(REFDATA)/CONVERSE.A

# M6 full conversation VM: drive a real NPC through engine/converse_vm
$(BUILD)/convvm: tools/convvm.c dataformats/u6lzw.c engine/converse_vm.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -Iengine -o $@ $^
convvm: $(BUILD)/convvm
	$(BUILD)/convvm $(REFDATA)/CONVERSE.A 5

# conversation-VM unit test (data-block INDEXOF lookup); includes the engine TU
$(BUILD)/cvmtest: tools/cvmtest.c engine/converse_vm.c engine/converse_vm.h
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -Iengine -o $@ tools/cvmtest.c
cvmtest: $(BUILD)/cvmtest
	$(BUILD)/cvmtest

# M6 portrait decode: PORTRAIT.A/.B/.Z (4-byte lib of LZW 56x64) -> contact sheet
PORT_SRC := dataformats/u6lib.c dataformats/u6lzw.c dataformats/u6pal.c tools/png.c
$(BUILD)/portdump: tools/portdump.c $(PORT_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
portdump: $(BUILD)/portdump
	$(BUILD)/portdump $(REFDATA) $(BUILD)/portraits.png

# M6 background/border decode: PAPER.BMP (whole-file LZW U6Bmp) -> PNG
$(BUILD)/bmpdump: tools/bmpdump.c dataformats/u6lzw.c dataformats/u6pal.c tools/png.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
bmpdump: $(BUILD)/bmpdump
	$(BUILD)/bmpdump $(REFDATA)/PAPER.BMP $(REFDATA)/U6PAL $(BUILD)/paper.png

# M6 front-end: decode U6 SHAPE files (*.shp) -> PNG (title/menu/intro; shapes.md)
$(BUILD)/shapedump: tools/shapedump.c dataformats/u6lzw.c dataformats/u6pal.c dataformats/u6shape.c tools/png.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -Itools -o $@ $^
shapedump: $(BUILD)/shapedump
	$(BUILD)/shapedump $(REFDATA)/MAINMENU.SHP $(REFDATA)/U6PAL $(BUILD)/menu
	$(BUILD)/shapedump $(REFDATA)/TITLES.SHP  $(REFDATA)/U6PAL $(BUILD)/titles

# M6 NPC actors + schedules: place NPCs by game time, render a town, report routines
OBJLIST ?= refdata/st/u6/SAVEGAME/OBJLIST
NPCSCHED_SRC := dataformats/u6actor.c dataformats/u6lzw.c dataformats/u6conv.c \
                dataformats/u6pal.c dataformats/u6map.c engine/world.c engine/rng.c tools/png.c
$(BUILD)/npcsched: tools/npcsched.c $(NPCSCHED_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -Iengine -o $@ $^
npcsched: world $(BUILD)/npcsched
	$(BUILD)/npcsched $(REFDATA)/SCHEDULE $(OBJLIST) $(REFDATA)/BASETILE $(REFDATA)/U6PAL \
	    $(REFDATA)/CONVERSE.A $(BUILD)/U6.PAK $(BUILD)/npcs.png 12

# M6 NPC# -> portrait + name mapping: who's-who sheet (real portraits + names)
NPCSHEET_SRC := dataformats/u6lib.c dataformats/u6lzw.c dataformats/u6pal.c dataformats/u6conv.c tools/png.c
$(BUILD)/npcsheet: tools/npcsheet.c $(NPCSHEET_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
npcsheet: $(BUILD)/npcsheet
	$(BUILD)/npcsheet $(REFDATA) $(BUILD)/npcsheet.png

# M6 conversation transcript test: parse + drive ALL NPCs in CONVERSE.A/.B
$(BUILD)/convtest: tools/convtest.c dataformats/u6lzw.c dataformats/u6conv.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
convtest: $(BUILD)/convtest
	$(BUILD)/convtest $(REFDATA)

# M5 engine logic test (headless): scripted moves -> world-state assertions
ENGINE_SRC := engine/world.c engine/rng.c
ENGINETEST_SRC := dataformats/u6lzw.c dataformats/u6pal.c dataformats/u6map.c \
                  dataformats/u6tile.c $(ENGINE_SRC)
$(BUILD)/enginetest: tools/enginetest.c $(ENGINETEST_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -Iengine -Itools -o $@ $^

enginetest: $(BUILD)/enginetest
	$(BUILD)/enginetest $(REFDATA)

# combat math unit test (docs/rules/combat.md)
$(BUILD)/combattest: tools/combattest.c engine/combat.c engine/rng.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -Iengine -o $@ $^
combattest: $(BUILD)/combattest
	$(BUILD)/combattest

$(BUILD)/magictest: tools/magictest.c engine/magic.c engine/objects.c engine/magic.h engine/objects.h
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -Iengine -o $@ tools/magictest.c engine/magic.c engine/objects.c
magictest: $(BUILD)/magictest
	$(BUILD)/magictest

# ---- M3 render demo: first frame on screen (TT-low 256-color) -------------
OX ?= 336
OY ?= 336

# host: asset pack + golden image + backbuffer->png converter
MKPACK_SRC := dataformats/u6lzw.c dataformats/u6pal.c dataformats/u6map.c \
              dataformats/u6tile.c tools/png.c
$(BUILD)/mkpack: tools/mkpack.c $(MKPACK_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
$(BUILD)/raw2png: tools/raw2png.c dataformats/u6pal.c tools/png.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^

pack: $(BUILD)/mkpack
	$(BUILD)/mkpack $(REFDATA) $(BUILD)/u6.pak $(BUILD)/region_golden.png $(OX) $(OY)

# target: the render-demo .PRG (TT/Falcon arch). Links platform + render.
RDEMO_SRC := platform/detect.c platform/platmem.c platform/platfile.c \
             render/rdr_tt.c render/c2p_asm.s render/rdemo.c
# Hybrid 020/no-FPU build (same as game.prg): runs on a stock FPU-less TT/Falcon.
$(BUILD)/rdemo.prg: $(RDEMO_SRC)
	$(call HYBRID_BUILD,$@,$^,obj_rdemo)

rdemo: $(BUILD)/rdemo.prg

# ---- performance benchmark: time compose vs c2p present on the TT ------------
BENCH_SRC := platform/detect.c platform/platmem.c platform/platfile.c platform/platinput.c \
             platform/plattime.c engine/world.c engine/rng.c engine/converse_vm.c engine/objects.c \
             engine/save.c engine/combat.c engine/magic.c dataformats/u6map.c dataformats/u6actor.c dataformats/u6lzw.c \
             render/rdr_tt.c render/c2p_asm.s render/gamedemo.c
# Hybrid 020/no-FPU build (same as game.prg) + -DBENCH: runs on a stock FPU-less TT.
$(BUILD)/bench.prg: $(BENCH_SRC)
	$(call HYBRID_BUILD,$@,$^,obj_bench,-DBENCH)

# run the bench headless; BENCH.BIN = 5 big-endian u32 (compose, present-full, map-doubled,
# map-1x, N). run-vbls must outlast the bench loop (compose is heavier now: lighting+visibility).
bench: world $(BUILD)/bench.prg
	rm -f $(BUILD)/BENCH.BIN
	$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\bench.prg' \
	          --fast-forward on --run-vbls 9000 </dev/null >/dev/null 2>&1 || true
	@od -An -tu4 --endian=big -w64 $(BUILD)/BENCH.BIN 2>/dev/null | awk '{n=$$5; if(n>0) printf "compose: %.1f ms   present FULL: %.1f ms   MAP-WINDOW doubled: %.1f ms   MAP-WINDOW 1x: %.1f ms   (per frame, n=%d)\n", $$1*5.0/n, $$2*5.0/n, $$3*5.0/n, $$4*5.0/n, n; else print "no BENCH.BIN"}' || echo "no BENCH.BIN"

# ---- INTRO render-cost bench: run the living-room scene (0) through the REAL run_intro and
# report per-frame cost split HOLD vs PAN x compose/c2p+double/output. BENCH.BIN = 8 big-endian
# u32: HOLD{compose,c2p,out,frames} PAN{compose,c2p,out,frames}. (Hatari --run-vbls is flaky at
# self-terminating here, so bound wall-clock with timeout -s KILL; BENCH.BIN is written by the
# program before it returns, so a hard kill after it completes is fine.)
bench-intro: world $(BUILD)/bench.prg
	rm -f $(BUILD)/BENCH.BIN
	printf 1 > $(BUILD)/IBENCH.REQ
	timeout -s KILL 120 env SDL_RENDER_DRIVER=software $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\bench.prg' \
	          --fast-boot on --fast-forward on --run-vbls 6000 </dev/null >/dev/null 2>&1 || true
	-pkill -9 hatari 2>/dev/null; rm -f $(BUILD)/IBENCH.REQ
	@od -An -tu4 --endian=big -w64 $(BUILD)/BENCH.BIN 2>/dev/null | awk '{hn=$$4; pn=$$8; \
	  if(hn>0){ printf "HOLD (n=%d): compose %.1f  c2p+dbl %.1f  output %.1f  => %.1f ms/frame (%.1f fps)\n", \
	    hn, $$1*5.0/hn, $$2*5.0/hn, $$3*5.0/hn, ($$1+$$2+$$3)*5.0/hn, 1000.0/(($$1+$$2+$$3)*5.0/hn+0.001) } \
	  if(pn>0){ printf "PAN  (n=%d): compose %.1f  c2p+dbl %.1f  output %.1f  => %.1f ms/frame (%.1f fps)\n", \
	    pn, $$5*5.0/pn, $$6*5.0/pn, $$7*5.0/pn, ($$5+$$6+$$7)*5.0/pn, 1000.0/(($$5+$$6+$$7)*5.0/pn+0.001) } \
	  if(hn==0&&pn==0) print "no BENCH.BIN (intro bench did not complete — raise --run-vbls?)" }' || echo "no BENCH.BIN"

# ---- M5 game demo: a walkable world on the TT --------------------------------
# Default start: just west of Lord British's noon throne spot (316,367) so the
# headless capture auto-talks him (real portrait + name + greeting via the VM).
GX ?= 315
GY ?= 367
MKWORLD_SRC := dataformats/u6lzw.c dataformats/u6lib.c dataformats/u6pal.c dataformats/u6tile.c dataformats/u6obj.c dataformats/u6shape.c dataformats/u6map.c engine/combat.c engine/rng.c
$(BUILD)/mkworld: tools/mkworld.c $(MKWORLD_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -Itools -Iengine -o $@ $^ -lm
# Nuvie/ScummVM roof data: indoor/outdoor for lighting comes from roof_map_00.dat,
# extracted host-side from the user's ScummVM install (ultima.dat). NOT shipped — we
# transcode it into our own indoor bitmap; mkworld falls back to floor-tiles if absent.
ULTIMA_DAT ?= /usr/share/scummvm/ultima.dat
.PHONY: roof-extract
roof-extract:
	@mkdir -p $(BUILD)
	@if [ -f "$(ULTIMA_DAT)" ]; then \
	   unzip -o -j "$(ULTIMA_DAT)" "ultima6/maps/u6/roof_map_00.dat" "ultima6/images/roof_tiles.bmp" -d $(BUILD) >/dev/null 2>&1 \
	   && echo "roof: extracted roof_map_00.dat + roof_tiles.bmp from $(ULTIMA_DAT)" \
	   || echo "roof: extract FAILED -> mkworld uses floor-tile fallback, no roofs"; \
	 else echo "roof: $(ULTIMA_DAT) not found -> mkworld uses floor-tile fallback, no roofs"; fi

world: $(BUILD)/mkworld roof-extract
	$(BUILD)/mkworld $(REFDATA) $(BUILD)/U6.PAK $(GX) $(GY)

# decode a ScummVM/Nuvie U6 save -> extract surface objects + objlist (host)
$(BUILD)/nuviesave: tools/nuviesave.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
# dump the objlist's per-actor stat arrays (HP/str/dex/int/party) for verification
$(BUILD)/statprobe: tools/statprobe.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
# WRITE a ScummVM/Nuvie save (re-serialize surface chunks + actor arrays, preserve rest)
$(BUILD)/nuviewrite: tools/nuviewrite.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
# analyse the 64 surface super-chunks (partition formula + container/relocation)
$(BUILD)/objprobe: tools/objprobe.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
# build the world from a ScummVM SAVE state (objects+actors+start match ScummVM)
SCUMMVM_SAVE ?= $(HOME)/.local/share/scummvm/saves/ultima6.000
world-svm: $(BUILD)/mkworld $(BUILD)/nuviesave roof-extract
	$(BUILD)/nuviesave "$(SCUMMVM_SAVE)" $(BUILD)/svm.objblk $(BUILD)/svm.objlist $(BUILD)/svm.inv
	$(BUILD)/mkworld $(REFDATA) $(BUILD)/U6.PAK 0 0 $(BUILD)/svm.objblk $(BUILD)/svm.objlist $(BUILD)/svm.inv

# save-WRITE validation: byte-identical round-trip + a move-avatar modification,
# then prove ScummVM loads the written save (swaps it in, captures, restores).
save-roundtrip: $(BUILD)/nuviewrite
	$(BUILD)/nuviewrite "$(SCUMMVM_SAVE)" $(BUILD)/ultima6_rt.000
	cmp "$(SCUMMVM_SAVE)" $(BUILD)/ultima6_rt.000 && echo "round-trip: BYTE-IDENTICAL"
	$(BUILD)/nuviewrite "$(SCUMMVM_SAVE)" $(BUILD)/ultima6_moved.000 --move-avatar 320 360
save-check: save-roundtrip
	sh tools/savecheck.sh $(BUILD)/ultima6_moved.000 $(BUILD)/svm_loaded_moved.png

# Stage the original conversation + portrait libraries onto C: (build/) so the
# game .PRG can load them at runtime (we ship none; these are the user's files).
# GEMDOS 8.3 names: CONVERSE.A / PORTRAIT.A all fit without clipping.
gamedata: world
	cp '$(REFDATA)/CONVERSE.A' '$(REFDATA)/CONVERSE.B' \
	   '$(REFDATA)/PORTRAIT.A' '$(REFDATA)/PORTRAIT.B' $(BUILD)/

# host: render the windowed UI (real font + palette) to a PNG for layout
# iteration against reference screenshots, no emulator needed.
UIPREV_SRC := dataformats/u6pal.c dataformats/u6map.c dataformats/u6lib.c dataformats/u6lzw.c \
              dataformats/u6conv.c engine/world.c engine/rng.c tools/png.c
$(BUILD)/uipreview: tools/uipreview.c $(UIPREV_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -Iengine -Itools -o $@ $^
uipreview: world $(BUILD)/uipreview
	$(BUILD)/uipreview $(BUILD)/U6.PAK $(REFDATA)/U6PAL $(BUILD)/uipreview.png
	@echo "wrote $(BUILD)/uipreview.png"

GAME_SRC := platform/detect.c platform/platmem.c platform/platfile.c platform/platinput.c \
            platform/plattime.c platform/platmidi.c \
            engine/world.c engine/rng.c engine/converse_vm.c engine/objects.c engine/save.c \
            engine/combat.c engine/magic.c engine/music.c \
            dataformats/u6map.c dataformats/u6actor.c dataformats/u6lzw.c \
            render/rdr_tt.c render/c2p_asm.s render/gamedemo.c
# 020-OPTIMIZED, FPU-FREE build (resolves the no-FPU vs -m68020-60 conflict).
# Compile each source with -m68020-60 (68020+ integer codegen — bitfield ops, 32-bit
# mul/div, richer addressing; the engine uses NO floats so it emits ZERO FPU ops), but
# LINK against the DEFAULT 68000 soft-float libc. Rationale: the m68020-60 libc's startup
# (checkcpu.o) aborts with "This program requires a 68881..." on an FPU-less machine; the
# 68000 libc has no such gate AND contains no FPU instructions, so it runs everywhere a
# 68000 binary does. Net: 020-fast hot paths (compose/blit) + a libc that runs on a stock
# Falcon030 / FPU-less TT030. (Hand-asm c2p stays 68000.) See docs/design/toolchain.md.
# $(1)=output .prg  $(2)=sources  $(3)=obj subdir
HYBRID_INC := -Iplatform -Irender -Iengine -Idataformats
define HYBRID_BUILD
	@mkdir -p $(BUILD)/$(3)
	@for s in $(2); do o=$(BUILD)/$(3)/`echo $$s | tr / _`.o; \
	  $(CC) -m68020-60 -std=gnu11 -O2 -Wall $(HYBRID_INC) $(4) -c $$s -o $$o || exit 1; done
	$(CC) -std=gnu11 -O2 -o $(1) $(BUILD)/$(3)/*.o
	@echo "built $(1) (68020 codegen + 68000 soft-float libc; runs FPU-free)"
endef

$(BUILD)/game.prg: $(GAME_SRC)
	$(call HYBRID_BUILD,$@,$^,obj_tt)
game: $(BUILD)/game.prg

# Falcon variant: same engine + assets, VIDEL 320x240 backend (rdr_falcon.c) instead of
# TT-low rdr_tt.c — presents the 320x200 UI 1:1 (no 2x doubling / letterbox the TT needs).
# 8.3 name (gamef.prg) — Hatari GEMDOS-HDD --auto can't match a >8-char name. Same hybrid
# 020/no-FPU build as game.prg (above).
GAME_SRC_FALCON := $(patsubst render/rdr_tt.c,render/rdr_falcon.c,$(GAME_SRC))
$(BUILD)/gamef.prg: $(GAME_SRC_FALCON)
	$(call HYBRID_BUILD,$@,$^,obj_falcon)
game-falcon: $(BUILD)/gamef.prg

# ST/STE variant (M7): the 16-colour shifter backend (rdr_st.c) + the pack's host-quantised
# palette (U6WW). PURE 68000 (NOT the 020 hybrid) — a plain ST/STE has a 68000, so we compile
# for -m68000 and link the stock 68000 libc. Drops c2p_asm.s (the TT's 8-plane asm); rdr_st uses
# its own 4-plane C c2p. 8.3 name (gamest.prg) for Hatari GEMDOS --auto.
GAME_SRC_ST := $(filter-out render/rdr_tt.c render/c2p_asm.s,$(GAME_SRC)) render/rdr_st.c
# -DPACK_NAME loads the SLIM pack (U6ST.PAK, front-end art omitted) so it fits a real 4 MB ST.
$(BUILD)/gamest.prg: $(GAME_SRC_ST)
	@mkdir -p $(BUILD)/obj_st
	@for s in $^; do o=$(BUILD)/obj_st/`echo $$s | tr / _`.o; \
	  $(CC) -m68000 -std=gnu11 -O2 -Wall -DPACK_NAME='"U6ST.PAK"' -DST_STREAM=1 $(HYBRID_INC) -c $$s -o $$o || exit 1; done
	$(CC) -m68000 -std=gnu11 -O2 -o $@ $(BUILD)/obj_st/*.o
	@echo "built $@ (pure 68000, ST-low 16-colour shifter, slim pack)"
game-st: $(BUILD)/gamest.prg

# SLIM ST world pack (U6ST.PAK): same as `world` but MKWORLD_SLIM omits the ~1.35 MB front-end/intro
# art so the pack + libs + buffers fit a real 4 MB ST. The ST capture targets depend on this.
world-st: $(BUILD)/mkworld roof-extract
	MKWORLD_SLIM=1 $(BUILD)/mkworld $(REFDATA) $(BUILD)/U6ST.PAK $(GX) $(GY)
gamedata-st: world-st

# interactive: walk the U6 world on an emulated TT
run-game: gamedata $(BUILD)/game.prg
	rm -f $(BUILD)/COMBAT.BIN $(BUILD)/NPCWALK.BIN     # ensure no headless scenario
	printf 1 > $(BUILD)/MENU.REQ                       # interactive boot -> title/main menu (must be non-empty)
	$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\game.prg'

# NEWDESK.INF with a "#Z 01 C:\GAMEF.PRG@" auto-run entry: TOS 4.04's desktop
# ignores Hatari --auto, but DOES auto-launch the #Z application named in the
# NEWDESK.INF it finds on the boot drive (C:\). This is how a real Falcon would
# autostart the game too. (Mechanism learned from the falcon-port project.)
$(BUILD)/NEWDESK.INF:
	@mkdir -p $(BUILD)
	@printf '#a000000\r\n#b000000\r\n#c7770007000600070055200505552220770557075055507703111103\r\n#d                                             \r\n#Z 01 C:\\GAMEF.PRG@ \r\n#E 18 01 00 06 00 23 00 00 00 00 \r\n#M 00 01 00 FF C HARD DISK@ @ \r\n#M 00 00 00 FF A FLOPPY DISK@ @ \r\n#T 00 03 02 FF   TRASH@ @ \r\n' > $@

# interactive: walk the U6 world on an emulated stock Falcon (VIDEL 320x240, real
# TOS 4.04, NO FPU). --conout 2 mirrors the game's console to this terminal.
run-game-falcon: gamedata $(BUILD)/gamef.prg $(BUILD)/NEWDESK.INF
	$(HATARI) --machine falcon --fast-boot on --conout 2 \
	          --tos '$(TOS_FALC)' --harddrive $(BUILD) --auto 'C:\GAMEF.PRG'

# headless Falcon capture from the ScummVM SAVE state — the clean native-320x240
# bug-hunt rig (no TT letterbox / 2x doubling). Stock Falcon, no FPU, real TOS 4.04.
m5-svm-falcon: world-svm $(BUILD)/gamef.prg $(BUILD)/NEWDESK.INF
	cp '$(REFDATA)/CONVERSE.A' '$(REFDATA)/CONVERSE.B' '$(REFDATA)/PORTRAIT.A' '$(REFDATA)/PORTRAIT.B' $(BUILD)/
	rm -f $(BUILD)/gamef.avi $(BUILD)/gamef_screen.png $(BUILD)/SAVE.000   # no template => static render
	$(HATARI) --machine falcon --fast-boot on --conout 2 \
	          --tos '$(TOS_FALC)' --harddrive $(BUILD) --auto 'C:\GAMEF.PRG' \
	          --fast-forward on --run-vbls 3500 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/gamef.avi 2>/dev/null || true
	-ffmpeg -y -i $(BUILD)/gamef.avi -update 1 $(BUILD)/gamef_screen.png 2>/dev/null
	@echo "wrote $(BUILD)/gamef_screen.png (Falcon VIDEL 320x240, ScummVM-save state)"

# PIXEL-PERFECT screenshot: stage SHOT.REQ so the engine dumps its 320x200 chunky
# backbuffer (+palette) to SHOT.DAT, then convert host-side. NO emulator scaling/
# aspect/overscan in the imaging path — image pixel == logical pixel. The clean
# rig for tile-alignment / bug-hunt work. Runs on the stock Falcon (no FPU).
shot-falcon: world-svm $(BUILD)/gamef.prg $(BUILD)/NEWDESK.INF
	cp '$(REFDATA)/CONVERSE.A' '$(REFDATA)/CONVERSE.B' '$(REFDATA)/PORTRAIT.A' '$(REFDATA)/PORTRAIT.B' $(BUILD)/
	rm -f $(BUILD)/SHOT.DAT $(BUILD)/shot.png $(BUILD)/SAVE.000
	printf 'x' > $(BUILD)/SHOT.REQ                         # stage the dump sentinel (non-empty: load_file rejects 0-byte)
	-$(HATARI) --machine falcon --fast-boot on --conout 2 \
	          --tos '$(TOS_FALC)' --harddrive $(BUILD) --auto 'C:\GAMEF.PRG' \
	          --fast-forward on --run-vbls 4000 2>/dev/null
	@rm -f $(BUILD)/SHOT.REQ                               # un-stage (normal runs don't dump)
	@test -f $(BUILD)/SHOT.DAT || { echo "FAIL: engine did not write SHOT.DAT"; exit 1; }
	python3 tools/shot2png.py $(BUILD)/SHOT.DAT $(BUILD)/shot.png
	@echo "wrote $(BUILD)/shot.png (pixel-perfect 320x200, 1:1)"

# M7 headless verify: render the game view on an emulated PLAIN ST (68000, ST-low 16-colour
# shifter) and capture the real 4-plane video output, on a colour (rgb) monitor for ST-low. This
# is the headline "optimised 16 vs the lazy EGA port" shot. Runs on a REAL 4 MB ST: the slim pack
# (U6ST.PAK, front-end art omitted) + libs + buffers leave ~900 KB free (see the MEMINFO diagnostic).
shot-st: gamedata-st $(BUILD)/gamest.prg
	rm -f $(BUILD)/gamest.avi $(BUILD)/gamest_screen.png
	-$(HATARI) --machine st --memsize 4 --monitor rgb \
	          --tos '$(TOS_ST)' --harddrive $(BUILD) --auto 'c:\gamest.prg' \
	          --fast-forward on --run-vbls 5000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/gamest.avi 2>/dev/null
	-ffmpeg -y -i $(BUILD)/gamest.avi -update 1 $(BUILD)/gamest_screen.png 2>/dev/null
	@echo "wrote $(BUILD)/gamest_screen.png (plain ST, ST-low 16-colour optimised palette)"

# M7 band-raster MECHANISM check: stage RASTER.BIN so the engine installs a 2-band Timer-B split
# whose band 1 (command bar) has R and B swapped. If the raster fires, the bottom band's colours
# are visibly swapped vs the map above — proof the palette split works before real band data.
shot-st-raster: gamedata-st $(BUILD)/gamest.prg
	rm -f $(BUILD)/gamest.avi $(BUILD)/gamest_raster.png
	printf 1 > $(BUILD)/RASTER.BIN
	-$(HATARI) --machine st --memsize 4 --monitor rgb --conout 2 \
	          --tos '$(TOS_ST)' --harddrive $(BUILD) --auto 'c:\gamest.prg' \
	          --fast-forward on --run-vbls 5000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/gamest.avi 2>/dev/null
	-ffmpeg -y -i $(BUILD)/gamest.avi -update 1 $(BUILD)/gamest_raster.png 2>/dev/null
	rm -f $(BUILD)/RASTER.BIN
	@echo "wrote $(BUILD)/gamest_raster.png (band-raster mechanism check: bottom band R<->B swapped)"

# M7 CONVERSATION-palette check: open a conversation (nearest NPC = Lord British at the default
# start) so the ST swaps to that portrait's U6WY palette, and capture the frame. The portrait should
# render with true skin/grey tones instead of the muddy brown the terrain-16 gives (splitcheck).
# CONVPAL_ID overrides the NPC id (0 = nearest adjacent).
CONVPAL_ID ?= 0
shot-st-conv: gamedata-st $(BUILD)/gamest.prg
	rm -f $(BUILD)/gamest.avi $(BUILD)/gamest_conv.png
	python3 -c "open('$(BUILD)/CONVPAL.BIN','wb').write(bytes([$(CONVPAL_ID) & 255]))"   # one raw byte = NPC id (0 = nearest)
	-$(HATARI) --machine st --memsize 4 --monitor rgb --conout 2 \
	          --tos '$(TOS_ST)' --harddrive $(BUILD) --auto 'c:\gamest.prg' \
	          --fast-forward on --run-vbls 5000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/gamest.avi 2>/dev/null
	-ffmpeg -y -i $(BUILD)/gamest.avi -update 1 $(BUILD)/gamest_conv.png 2>/dev/null
	rm -f $(BUILD)/CONVPAL.BIN
	@echo "wrote $(BUILD)/gamest_conv.png (conversation portrait under its U6WY palette)"

# M7 STE 4096-colour variant: same slim pack + PURE-68000 gamest.prg, but on an emulated STE the
# engine detects VIDEO_STE (cookie _VDO) + the pack's U6WS 4-bit palettes and switches rdr_st to the
# STE colour-register format (4 bits/channel, rotated-LSB). Captures the default map (shot-ste) and a
# conversation portrait (shot-ste-conv), where the finer gamut most reduces skin/gradient banding.
# TOS 2.06 (STE). Compare gamest_ste*.png vs the plain-ST gamest_*.png.
shot-ste: gamedata-st $(BUILD)/gamest.prg
	rm -f $(BUILD)/gamest.avi $(BUILD)/gamest_ste.png
	-$(HATARI) --machine ste --memsize 4 --monitor rgb --fast-boot on \
	          --tos '$(TOS_STE)' --harddrive $(BUILD) --auto 'c:\gamest.prg' \
	          --fast-forward on --run-vbls 8000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/gamest.avi 2>/dev/null
	-ffmpeg -y -i $(BUILD)/gamest.avi -update 1 $(BUILD)/gamest_ste.png 2>/dev/null
	@echo "wrote $(BUILD)/gamest_ste.png (emulated STE, 16 colours from the 4096 gamut)"

shot-ste-conv: gamedata-st $(BUILD)/gamest.prg
	rm -f $(BUILD)/gamest.avi $(BUILD)/gamest_ste_conv.png
	python3 -c "open('$(BUILD)/CONVPAL.BIN','wb').write(bytes([$(CONVPAL_ID) & 255]))"
	-$(HATARI) --machine ste --memsize 4 --monitor rgb --conout 2 --fast-boot on \
	          --tos '$(TOS_STE)' --harddrive $(BUILD) --auto 'c:\gamest.prg' \
	          --fast-forward on --run-vbls 8000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/gamest.avi 2>/dev/null
	-ffmpeg -y -i $(BUILD)/gamest.avi -update 1 $(BUILD)/gamest_ste_conv.png 2>/dev/null
	rm -f $(BUILD)/CONVPAL.BIN
	@echo "wrote $(BUILD)/gamest_ste_conv.png (STE conversation portrait, 4-bit palette)"

# M7 PER-SCENE palette check (chargen): drive the streamed front-end to a gypsy-parlour chargen
# sub-screen so the ST activates that scene's OWN 16-colour palette (build_scene_lc, quantized from
# PALETTES.INT 5 weighted by the scene's pixels) instead of the terrain-16. GYPSY_REQ picks the
# frame: 'G' clean lead-in (raw scene), 'P' held mid-pour, 'q' a virtue question. The chargen hook
# draws once then blocks on input, so the AVI's final frame holds the scene under its palette.
GYPSY_REQ ?= G
shot-st-gypsy: gamedata-st $(BUILD)/gamest.prg
	rm -f $(BUILD)/gamest.avi $(BUILD)/gamest_gypsy.png
	printf '$(GYPSY_REQ)' > $(BUILD)/CHARGEN.REQ            # one byte selects the gypsy chargen sub-screen
	-$(HATARI) --machine st --memsize 4 --monitor rgb --conout 2 \
	          --tos '$(TOS_ST)' --harddrive $(BUILD) --auto 'c:\gamest.prg' \
	          --fast-forward on --run-vbls 6000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/gamest.avi 2>/dev/null
	-ffmpeg -y -i $(BUILD)/gamest.avi -update 1 $(BUILD)/gamest_gypsy.png 2>/dev/null
	rm -f $(BUILD)/CHARGEN.REQ
	@echo "wrote $(BUILD)/gamest_gypsy.png (gypsy chargen scene under its per-scene U6FE palette)"

# M7 COLOUR-CYCLING proof (ST): the throne-room braziers are the ONLY U6PAL 224-231 pixels; on the
# 16-colour shifter their flame pixels map to the reserved cycle slots (U6WZ), which the engine
# rotates each tick via rdr_lowcolor_cycle (no tile redraw). Capture the AVI, extract two frames at
# different cycle phases, and diff — only the brazier flames change colour, proving palette cycling
# works on 16-colour hardware (the ST counterpart of the TT's cyc2_diff). (docs/formats/palette_cycling.md)
# FIRE_START lets you place the avatar AT a brazier if the default start doesn't show one.
FIRE_A ?= 4000
FIRE_B ?= 4030
shot-st-fire: gamedata-st $(BUILD)/gamest.prg
	rm -f $(BUILD)/gamest.avi $(BUILD)/fire_phase_1.png $(BUILD)/fire_phase_2.png $(BUILD)/fire_st.png $(BUILD)/fire_diff.png
	-$(HATARI) --machine st --memsize 4 --monitor rgb \
	          --tos '$(TOS_ST)' --harddrive $(BUILD) --auto 'c:\gamest.prg' \
	          --fast-forward on --run-vbls 5000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/gamest.avi 2>/dev/null
	-ffmpeg -y -i $(BUILD)/gamest.avi -vf "select='eq(n\,$(FIRE_A))+eq(n\,$(FIRE_B))'" -vsync 0 $(BUILD)/fire_phase_%d.png 2>/dev/null
	-compare -metric AE $(BUILD)/fire_phase_1.png $(BUILD)/fire_phase_2.png $(BUILD)/fire_diff.png 2>&1 | sed 's/$$/ px differ between two cycle phases (fire)/'
	-montage $(BUILD)/fire_phase_1.png $(BUILD)/fire_phase_2.png $(BUILD)/fire_diff.png -tile 3x1 -geometry +3+3 $(BUILD)/fire_st.png 2>/dev/null
	@echo "wrote $(BUILD)/fire_st.png (fire cycle on ST: phaseA | phaseB | diff)"

# headless M5 verify: auto-walk + capture a frame (avatar on the scrolled map)
m5: gamedata $(BUILD)/game.prg
	rm -f $(BUILD)/game.avi $(BUILD)/game_screen.png
	$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\game.prg' \
	          --fast-forward on --run-vbls 5000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/game.avi 2>/dev/null || true
	-ffmpeg -y -i $(BUILD)/game.avi -update 1 $(BUILD)/game_screen.png 2>/dev/null
	@echo "wrote $(BUILD)/game_screen.png"

# NEW-GAME overlay validation (front-end -> gameplay). The DEFAULT non-svm build (gamedata ->
# world) bakes the shipped U6 start — Lord British's castle @ (315,367), four-member party — into
# U6.PAK; character creation OVERLAYS name/gender/portrait/Str/Dex/Int onto the Avatar. Stage
# CHARGEN.REQ='N' (applies the deterministic 'Milady' sample, no blocking UI) + NEWGAME.BIN (the
# engine prints the assembled new-game state). --conout 2 mirrors the assertions to this terminal.
.PHONY: newgame
newgame: gamedata $(BUILD)/game.prg
	rm -f $(BUILD)/game.avi $(BUILD)/game_screen.png $(BUILD)/SAVE.000
	printf N > $(BUILD)/CHARGEN.REQ                 # apply the 'Milady' sample character (non-blocking)
	printf 1 > $(BUILD)/NEWGAME.BIN                 # non-empty: plat_load_file rejects 0-byte sentinels
	-$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga --conout 2 \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\game.prg' \
	          --fast-forward on --run-vbls 5000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/game.avi 2>/dev/null
	-ffmpeg -y -i $(BUILD)/game.avi -update 1 $(BUILD)/game_screen.png 2>/dev/null
	rm -f $(BUILD)/CHARGEN.REQ $(BUILD)/NEWGAME.BIN
	@echo "wrote $(BUILD)/game_screen.png (new game at Lord British's castle, character 'Milady')"

# CHARACTER STATUS VIEW validation (front-end -> in-game): open the Avatar's status panel
# (portrait + gender + Str/Dex/Int + HP/MP + Lev/Exp) over a fresh game. CHARGEN.REQ='N' applies
# the 'Milady' sample character; STATUS.REQ's byte = the actor id to show (1 = the Avatar).
.PHONY: status
status: gamedata $(BUILD)/game.prg
	rm -f $(BUILD)/game.avi $(BUILD)/status_screen.png $(BUILD)/SAVE.000
	printf N > $(BUILD)/CHARGEN.REQ                 # apply the 'Milady' sample character
	printf '\001' > $(BUILD)/STATUS.REQ             # open the status view for actor 1 (the Avatar)
	-$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\game.prg' \
	          --fast-boot on --fast-forward on --run-vbls 5000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/game.avi 2>/dev/null
	-ffmpeg -y -i $(BUILD)/game.avi -update 1 $(BUILD)/status_screen.png 2>/dev/null
	rm -f $(BUILD)/CHARGEN.REQ $(BUILD)/STATUS.REQ
	@echo "wrote $(BUILD)/status_screen.png (Avatar status view: portrait + gender + stats)"

# GYPSY POUR CHOREOGRAPHY validation (character_creation.md 3c): the full pixel-faithful port of
# nuvie's gypsy_vial_anim. CHARGEN.REQ byte selects the frame: 'P' = held mid-pour (arm tips a vial
# over the jar), 'q' = 8 vials at distinct drain levels + jar, 'D' = 3 full pours (vial 3->0, jar 0->3).
.PHONY: gypsy-pour
gypsy-pour: gamedata $(BUILD)/game.prg
	rm -f $(BUILD)/game.avi $(BUILD)/pour_frame.png $(BUILD)/SAVE.000
	printf P > $(BUILD)/CHARGEN.REQ                 # held mid-pour frame (P); use q or D for the others
	-$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\game.prg' \
	          --fast-boot on --fast-forward on --run-vbls 4000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/game.avi 2>/dev/null
	-ffmpeg -y -i $(BUILD)/game.avi -update 1 $(BUILD)/pour_frame.png 2>/dev/null
	rm -f $(BUILD)/CHARGEN.REQ
	@echo "wrote $(BUILD)/pour_frame.png (gypsy mid-pour: arm tips a vial over the jar)"

# READABLE TEXT validation (docs/formats/strings.md): READ.BIN byte 0 = find the first readable
# world object (sign/book/plaque) and open its BOOK.DAT text; byte>0 = show entry (byte-1) directly.
.PHONY: read
read: gamedata $(BUILD)/game.prg
	rm -f $(BUILD)/game.avi $(BUILD)/read_screen.png $(BUILD)/SAVE.000
	printf '\000' > $(BUILD)/READ.BIN               # 0 = read the first readable object in the world
	-$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga --conout 2 \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\game.prg' \
	          --fast-boot on --fast-forward on --run-vbls 4500 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/game.avi 2>/dev/null
	-ffmpeg -y -i $(BUILD)/game.avi -update 1 $(BUILD)/read_screen.png 2>/dev/null
	rm -f $(BUILD)/READ.BIN
	@echo "wrote $(BUILD)/read_screen.png (paged BOOK.DAT text over the papyrus page)"

# BOOKS-on-ST validation (M7): the BOOK.DAT text is now a RESIDENT core section (U6BK) instead of the
# front-end block the ST frees — so in-game reading survives on the ST. Stage READ.BIN and capture on
# a 4 MB ST: the book text pages over the papyrus. READ_ENTRY>0 shows entry-1 directly. (strings.md)
READ_ENTRY ?= 1
read-st: gamedata-st $(BUILD)/gamest.prg
	rm -f $(BUILD)/gamest.avi $(BUILD)/read_st.png
	python3 -c "open('$(BUILD)/READ.BIN','wb').write(bytes([$(READ_ENTRY) & 255]))"
	-$(HATARI) --machine st --memsize 4 --monitor rgb --conout 2 \
	          --tos '$(TOS_ST)' --harddrive $(BUILD) --auto 'c:\gamest.prg' \
	          --fast-forward on --run-vbls 5000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/gamest.avi 2>/dev/null
	-ffmpeg -y -i $(BUILD)/gamest.avi -update 1 $(BUILD)/read_st.png 2>/dev/null
	rm -f $(BUILD)/READ.BIN
	@echo "wrote $(BUILD)/read_st.png (in-game BOOK.DAT text on the ST — resident U6BK)"

# LOW-COLOUR (M7 U6WW) load check: confirm the pack's host-quantised 16-colour ST palette + 256->16
# LUT parsed to the exact byte (LOWCOL have=1 => the whole front-end section chain was consumed), and
# echo the palette as ST shifter registers (r<<8|g<<4|b). --conout 2 mirrors the assertions here.
.PHONY: lowcol
lowcol: gamedata $(BUILD)/game.prg
	printf 1 > $(BUILD)/LOWCOL.BIN                  # non-empty sentinel (plat_load_file rejects 0-byte)
	-$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga --conout 2 \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\game.prg' \
	          --fast-boot on --fast-forward on --run-vbls 3000 2>/dev/null | grep LOWCOL
	rm -f $(BUILD)/LOWCOL.BIN

# same capture, but built from the ScummVM SAVE state (world-svm) for comparison
m5-svm: world-svm $(BUILD)/game.prg
	cp '$(REFDATA)/CONVERSE.A' '$(REFDATA)/CONVERSE.B' '$(REFDATA)/PORTRAIT.A' '$(REFDATA)/PORTRAIT.B' $(BUILD)/
	rm -f $(BUILD)/game.avi $(BUILD)/game_screen.png $(BUILD)/SAVE.000   # no template => static render (matches ScummVM)
	$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\game.prg' \
	          --fast-forward on --run-vbls 5000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/game.avi 2>/dev/null || true
	-ffmpeg -y -i $(BUILD)/game.avi -update 1 $(BUILD)/game_screen.png 2>/dev/null
	@echo "wrote $(BUILD)/game_screen.png (ScummVM-save state)"

# on-target save writer: stage the user's save as a TEMPLATE on C:, boot the engine
# on the TT (big-endian) — it walks the avatar then writes SAVED.000 (little-endian)
# via GEMDOS — then verify the write is localized + ScummVM LOADS the engine's save.
save-engine: world-svm $(BUILD)/game.prg
	cp '$(REFDATA)/CONVERSE.A' '$(REFDATA)/CONVERSE.B' '$(REFDATA)/PORTRAIT.A' '$(REFDATA)/PORTRAIT.B' $(BUILD)/
	cp '$(SCUMMVM_SAVE)' $(BUILD)/SAVE.000          # save template for the on-target writer
	rm -f $(BUILD)/game.avi $(BUILD)/game_screen.png $(BUILD)/SAVED.000
	$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\game.prg' \
	          --fast-forward on --run-vbls 5000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/game.avi 2>/dev/null || true
	-ffmpeg -y -i $(BUILD)/game.avi -update 1 $(BUILD)/game_screen.png 2>/dev/null
	@test -f $(BUILD)/SAVED.000 || { echo "FAIL: engine did not write SAVED.000"; exit 1; }
	@echo "=== engine-written SAVED.000 vs template SAVE.000 (diff should be localized) ==="
	cmp -l $(BUILD)/SAVE.000 $(BUILD)/SAVED.000 | head; \
	  echo "diff bytes: $$(cmp -l $(BUILD)/SAVE.000 $(BUILD)/SAVED.000 | wc -l)"
	@echo "=== does ScummVM load the engine's save? ==="
	sh tools/savecheck.sh $(BUILD)/SAVED.000 $(BUILD)/svm_loaded_engine.png

# tile-animation demo (ANIMDATA): build a world starting in OPEN WATER (animated
# tiles fill the view), capture two frames on the TT, and diff them to prove the
# water/fires/fountains cycle (and that the blank animated slots are now filled,
# fixing the "missing chunks"). (docs/formats/animdata.md)
ANIM_X ?= 64
ANIM_Y ?= 32
m6-anim: $(BUILD)/mkworld $(BUILD)/game.prg
	$(BUILD)/mkworld $(REFDATA) $(BUILD)/U6.PAK $(ANIM_X) $(ANIM_Y) | grep -E "animdata|start\("
	rm -f $(BUILD)/SAVE.000 $(BUILD)/game.avi $(BUILD)/anim_phase_1.png $(BUILD)/anim_phase_2.png
	$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\game.prg' \
	          --fast-forward on --run-vbls 4000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/game.avi 2>/dev/null || true
	-ffmpeg -y -i $(BUILD)/game.avi -vf "select='eq(n\,3400)+eq(n\,3460)'" -vsync 0 $(BUILD)/anim_phase_%d.png 2>/dev/null
	-compare -metric AE $(BUILD)/anim_phase_1.png $(BUILD)/anim_phase_2.png $(BUILD)/anim_engine_diff.png 2>&1 | sed 's/$$/ px differ between two frames (animation)/'
	-montage $(BUILD)/anim_phase_1.png $(BUILD)/anim_phase_2.png $(BUILD)/anim_engine_diff.png -tile 3x1 -geometry +3+3 $(BUILD)/anim_engine.png 2>/dev/null
	@echo "wrote $(BUILD)/anim_engine.png (water animation: phaseA | phaseB | diff)"

# NPC schedule-WALKING validation. Scene centres on NPC 17's commute ((330,432)->
# (334,437) at hour 0); the engine advances to two consecutive MID-WALK turns (74,76
# via the NPCWALK.BIN sentinel = turn count) so the montage shows the NPC walking with
# its pose changing. (Use a COMMUTER scene — Lord British's castle NPCs are at-post all
# day, so they don't move.) (docs/formats/schedule.md)
NPC_X ?= 332
NPC_Y ?= 434
m6-npc: $(BUILD)/mkworld $(BUILD)/game.prg
	$(BUILD)/mkworld $(REFDATA) $(BUILD)/U6.PAK $(NPC_X) $(NPC_Y) | grep -E "start\("
	cp '$(REFDATA)/CONVERSE.A' '$(REFDATA)/CONVERSE.B' '$(REFDATA)/PORTRAIT.A' '$(REFDATA)/PORTRAIT.B' $(BUILD)/
	rm -f $(BUILD)/SAVE.000
	printf '\112' > $(BUILD)/NPCWALK.BIN            # turn 74 (mid-walk for NPC 17)
	rm -f $(BUILD)/game.avi
	$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga --tos '$(TOS_TT)' \
	          --harddrive $(BUILD) --auto 'c:\game.prg' --fast-forward on --run-vbls 7500 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/game.avi 2>/dev/null || true
	-ffmpeg -y -i $(BUILD)/game.avi -update 1 $(BUILD)/npc_t74.png 2>/dev/null
	printf '\114' > $(BUILD)/NPCWALK.BIN            # turn 76 (two steps later)
	rm -f $(BUILD)/game.avi
	$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga --tos '$(TOS_TT)' \
	          --harddrive $(BUILD) --auto 'c:\game.prg' --fast-forward on --run-vbls 7500 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/game.avi 2>/dev/null || true
	-ffmpeg -y -i $(BUILD)/game.avi -update 1 $(BUILD)/npc_t76.png 2>/dev/null
	rm -f $(BUILD)/NPCWALK.BIN
	-compare -metric AE $(BUILD)/npc_t74.png $(BUILD)/npc_t76.png $(BUILD)/npc_diff.png 2>&1 | sed 's/$$/ px differ (NPC walk + water + clock)/'
	-montage $(BUILD)/npc_t74.png $(BUILD)/npc_t76.png -tile 2x1 -geometry +3+3 $(BUILD)/npc_walk_montage.png 2>/dev/null
	@echo "wrote $(BUILD)/npc_walk_montage.png (walking NPC: turn 74 | turn 76)"

# interactive: render the frame on an emulated TT (VGA monitor)
run-rdemo: pack $(BUILD)/rdemo.prg
	$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\rdemo.prg'

# headless M3 verify: render, capture screen (AVI->frame) + dump backbuffer->PNG
m3: pack $(BUILD)/rdemo.prg $(BUILD)/raw2png
	rm -f $(BUILD)/rdemo.avi $(BUILD)/OUT.RAW $(BUILD)/screen.png $(BUILD)/backbuffer.png
	$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\rdemo.prg' \
	          --fast-forward on --run-vbls 3200 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/rdemo.avi 2>/dev/null || true
	# AVI is force-quit (no index), so decode sequentially; -update keeps the
	# LAST frame = the held, rendered map.
	-ffmpeg -y -i $(BUILD)/rdemo.avi -update 1 $(BUILD)/screen.png 2>/dev/null
	-[ -f $(BUILD)/OUT.RAW ] && $(BUILD)/raw2png $(BUILD)/OUT.RAW $(REFDATA)/U6PAL 320 480 $(BUILD)/backbuffer.png
	@echo "m3 outputs: $(BUILD)/region_golden.png (host), $(BUILD)/backbuffer.png (ST composite), $(BUILD)/screen.png (ST screen)"

# ---- U6 reference screenshots from ScummVM (visual debugging aid) -----------
# Boots U6 in ScummVM headless (Xvfb) + captures gameplay/menu to refshots/.
# OBSERVING the reference on the user's data (clean-room OK). 640x400 = 2x native.
.PHONY: refshot
refshot:
	sh tools/refshot.sh $(REFDATA) refshots

# trace which GEMDOS/XBIOS calls we actually issue (verification aid)
trace: all
	$(HATARI) --machine st --tos '$(EMUTOS)' $(HATARI_COMMON) \
	          --fast-forward on --run-vbls 2500 \
	          --trace gemdos,xbios --trace-file $(BUILD)/trace.log 2>/dev/null
	@echo "wrote $(BUILD)/trace.log"

clean:
	rm -rf $(BUILD)
