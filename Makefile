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
# Objects live in a PER-ARCH dir (obj_self / obj_self_020): the 68000 and -m68020-60 selftests
# used to share build/platform/*.o, so switching ARCH linked stale wrong-CPU objects — and with
# no header deps, a struct change in platform.h left detect.o and platmem.o disagreeing about
# platform_caps_t (audit 2026-09-20: "mxalloc: no" then a fatal Mxalloc call on TOS 1.04).
SELFTEST_OBJ := $(patsubst %.c,$(BUILD)/obj_self$(SUFFIX)/%.o,$(SELFTEST_SRC))
SELFTEST_PRG := $(BUILD)/selftest$(SUFFIX).prg

# ---- emulator (Hatari) ---------------------------------------------------
HATARI   ?= hatari
# Automated test/capture targets (bench*, shot*, check*, m*, read, status, trace, sbench, music-dma …)
# invoke Hatari as `$(HL) $(HATARI)`: a DUMMY SDL video driver on NO display, so the emulator renders
# fully offscreen — no window appears on your desktop, nothing steals mouse/keyboard focus, and a stray
# window can't perturb a captured run. AVI recording + BENCH.BIN + --conout all work headless (the AVI
# captures Hatari's emulated Atari screen, not the host window). Interactive run-* / music-st|tt keep a
# real window. Force a visible window for one target with `make <target> HL=`.
HL       ?= env -u WAYLAND_DISPLAY -u DISPLAY SDL_VIDEODRIVER=dummy
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

.PHONY: all clean run run-st run-ste run-tt run-falcon shot check check-st check-tt trace dftest mapdump objrender convspike convtest enginetest savetest wtrun pack rdemo run-rdemo bench bench-intro m3 world world-svm gamedata game run-game m5 m5-svm uipreview portdump bmpdump npcsheet convvm cvmtest npcsched refshot save-roundtrip save-check save-engine m6-anim m6-npc

all: $(SELFTEST_PRG)

# -MMD -MP: emit + include header dependencies, so a platform.h change rebuilds every object.
$(BUILD)/obj_self$(SUFFIX)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
-include $(SELFTEST_OBJ:.o=.d)

# Link the DEFAULT 68000 soft-float libc (no checkcpu FPU gate). The objects keep their
# per-ARCH codegen — so the TT/Falcon (-m68020-60) selftest is hybrid 020/no-FPU like
# game.prg, and the ST (-m68000) build is unchanged. See docs/design/toolchain.md.
$(SELFTEST_PRG): $(SELFTEST_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "built $@"

# ---- MIDITEST.PRG: stand-alone MIDI port/cable check (plain 68000, runs on any Atari) ----
# OUT: GM reset + a piano scale/chord on channel 1; IN: decodes what the instrument sends;
# T = thru; Esc quits. See platform/miditest.c. Copy build/MIDITEST.PRG next to the game.
miditest: $(BUILD)/MIDITEST.PRG
$(BUILD)/MIDITEST.PRG: platform/miditest.c
	@mkdir -p $(BUILD)
	$(CC) -m68000 -std=gnu11 -Os -Wall -Wextra -o $@ $<
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

# ---- M8 vcard: VDI / graphics-card capability probe (GEM app) -------------
# Detects EdDI + queries vq_extnd for the current screen format. Pure 68000 + libgem16 (the "68000
# ST + 256c card" path is a target). Run under ARAnyM/fVDI for a real card, or Hatari for a built-in
# VDI smoke test. ARANYM overridable; set it to your aranym-mmu/aranym binary.
ARANYM ?= aranym
$(BUILD)/vcardtest.prg: render/vcardtest.c
	@mkdir -p $(BUILD)
	$(CC) -m68000 -std=gnu11 -Os -Wall -o $@ $< -lgem16
	@echo "built $@ (GEM VDI probe, 68000 + libgem16)"
vcardtest: $(BUILD)/vcardtest.prg
# M8 vcard RENDERER test: rdr_vcard.c (VDI card backend) + a test-pattern harness. GEM app, pure
# 68000 + libgem16 + the portable C c2p (no 68020 asm). Validate under ARAnyM+fVDI+AFROS.
$(BUILD)/vcardrender.prg: render/vcardrender.c render/render.c render/rdr_vcard.c platform/detect.c platform/platmem.c
	@mkdir -p $(BUILD)
	$(CC) -m68000 -std=gnu11 -Os -Wall -DHAVE_RDR_VCARD -Irender -Iplatform -o $@ $^ -lgem16
	@echo "built $@ (rdr_vcard VDI backend via render.c dispatch + test pattern, 68000 + libgem16)"
vcardrender: $(BUILD)/vcardrender.prg
# Hatari smoke test (built-in TT VDI: no EdDI, but exercises the vq_extnd path + reports the mode).
vcardtest-tt: $(BUILD)/vcardtest.prg
	$(HL) $(HATARI) --machine tt --tos '$(TOS_TT)' $(TT_FLAGS) --harddrive $(BUILD) --auto 'c:\vcardtest.prg' \
	          --conout 2 --fast-forward on --run-vbls 1500 2>/dev/null || true
# ARAnyM + fVDI (real graphics-card driver). Mounts build/ as C:; you provide fVDI + the card mode
# in your ARAnyM config. Prints the probe via --emutos console or the mounted output.
vcardtest-aranym: $(BUILD)/vcardtest.prg
	$(ARANYM) -d C:$(BUILD) --autorun C:\\vcardtest.prg 2>/dev/null || \
	  echo "adjust the ARANYM invocation to your aranym/fVDI config (see the target)"

# headless TEXT self-verify (unattended): autostart, mirror the VT-52 console to
# host stdout, fast-forward, quit. `check` runs both the ST (fallback) and TT
# (Alt-RAM) paths so M1's chip-vs-fast acceptance is covered.
check: check-st check-tt

check-st: all
	@echo "### self-test: ST (ST-RAM fallback path) ###"
	$(HL) $(HATARI) --machine st --tos '$(TOS_ST)' $(HATARI_COMMON) \
	          --conout 2 --fast-forward on --run-vbls 2500 2>/dev/null

# check-tt builds its own -m68020-60 selftest via ONE recursive ARCH=tt make (SUFFIX/_020 and the
# per-arch object dir come from that invocation) — `make check` is self-contained from clean. The
# recursive rule exists only in non-020 invocations, so the sub-make links via the normal rule.
ifneq ($(SUFFIX),_020)
$(BUILD)/selftest_020.prg: selftest-020-force
	@$(MAKE) --no-print-directory ARCH=tt all
.PHONY: selftest-020-force
selftest-020-force: ;
endif

check-tt: $(BUILD)/selftest_020.prg
	@echo "### self-test: TT (Alt-RAM path) ###"
	$(HL) $(HATARI) --machine tt --tos '$(EMUTOS)' $(TT_FLAGS) \
	          --harddrive $(BUILD) --auto 'c:\selftest_020.prg' \
	          --conout 2 --fast-forward on --run-vbls 4000 2>/dev/null

# headless VIDEO capture (for golden-image/video comparison later). Plain
# --screenshot-dir needs an interactive trigger, so we record an AVI instead.
shot: all
	$(HL) $(HATARI) --machine st --tos '$(EMUTOS)' $(HATARI_COMMON) \
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

# M9: validate the SE/MD ".LZC" flagged-library container (docs/formats/lzc.md)
# against the user's real Savage/Martian files, decoding with our own u6lzw.
LZC_SRC := dataformats/u6lzc.c dataformats/u6lzw.c
SE_REFDATA ?= refdata/dos/savage/SAVAGE
MD_REFDATA ?= refdata/dos/martian/MARTIAN
$(BUILD)/lzctest: tools/lzctest.c $(LZC_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^

# M9 obj_n survey: map each SE/MD object number to its LOOK name (host-side, clean-room; no data
# shipped) to identify the well-known objects (avatar/gold/key/container/door/food) for GameDef.wko
# + per-game USE. `make objsurvey-se` / `objsurvey-md` dump obj_n -> name to stdout.
$(BUILD)/objsurvey: tools/objsurvey.c $(LZC_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
objsurvey-se: $(BUILD)/objsurvey
	$(BUILD)/objsurvey $(SE_REFDATA) se
objsurvey-md: $(BUILD)/objsurvey
	$(BUILD)/objsurvey $(MD_REFDATA) md
# M9 dungeon-transition probe: dump U6 ladders/holes across levels + test vertical alignment
# (feeds docs/rules/map_levels.md). Clean-room; ships nothing.
$(BUILD)/dungobj: tools/dungobj.c dataformats/u6lzw.c dataformats/u6obj.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
dungobj: $(BUILD)/dungobj
	$(BUILD)/dungobj refdata/dos/u6

lzctest: $(BUILD)/lzctest
	$(BUILD)/lzctest $(SE_REFDATA) se
	@echo
	$(BUILD)/lzctest $(MD_REFDATA) md

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

# M10 OPL2->DMA de-risk: HOST software YM3812 (OPL2) FM synth renders a .M straight to WAV, so we can
# confirm BY EAR that our .M -> OPL-register translation (instrument 0x83 11-byte layout, F-number/
# block, feedback/connection) reproduces the authentic DOS AdLib sound BEFORE porting the synth + 8-bit
# DMA output to the 68000. WAV of the copyrighted score -> build/ (gitignored). M2WAV_SONG picks the .M.
$(BUILD)/m2wav: tools/m2wav.c dataformats/u6lzw.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^ -lm
M2WAV_SONG ?= ULTIMA
M2WAV_SECS ?= 30
.PHONY: m2wav
m2wav: $(BUILD)/m2wav
	@mkdir -p $(BUILD)/wav
	$(BUILD)/m2wav "$(REFDATA)/$(M2WAV_SONG).M" "$(BUILD)/wav/$(M2WAV_SONG).wav" $(M2WAV_SECS)
	@echo "play: aplay $(BUILD)/wav/$(M2WAV_SONG).wav   (or any WAV player)"

# M10 music: HOST unit test for the on-target sequencer (engine/music.c) — runs it through a capture
# backend into an SMF (follows the real loop/branch structure). A clean run over all 12 .M validates it.
$(BUILD)/musictest: tools/musictest.c engine/music.c dataformats/u6lzw.c
	@mkdir -p $(BUILD)/mid
	$(HOSTCC) $(HOSTCFLAGS) -Iengine -o $@ $^ -lm
.PHONY: musictest
musictest: $(BUILD)/musictest
	@for f in $(REFDATA)/*.M; do $(BUILD)/musictest "$$f" $(BUILD)/mid/`basename "$$f" .M`_seq.mid 60; done

# M10 OPL2->DMA: HOST test for the ON-TARGET FM synth (engine/oplsynth.c) — renders a .M to WAV through
# the same integer/no-libm engine code the 68000 runs, to confirm it matches the m2wav de-risk proof
# before wiring it to DMA sound. OPL_SONG picks the .M; OPL_RATE/OPL_OS = DMA rate + oversampling.
$(BUILD)/opltest: tools/opltest.c engine/oplsynth.c dataformats/u6lzw.c
	@mkdir -p $(BUILD)/wav
	$(HOSTCC) $(HOSTCFLAGS) -Iengine -o $@ $^
OPL_SONG ?= ULTIMA
OPL_SECS ?= 30
OPL_RATE ?= 49716
OPL_OS   ?= 4
.PHONY: opltest
opltest: $(BUILD)/opltest
	$(BUILD)/opltest "$(REFDATA)/$(OPL_SONG).M" "$(BUILD)/wav/$(OPL_SONG)_engine.wav" $(OPL_SECS) $(OPL_RATE) $(OPL_OS)
	@echo "play: aplay $(BUILD)/wav/$(OPL_SONG)_engine.wav"

# M10 OPL2->DMA pre-render: host-render every .M to signed-8-bit PCM (engine/oplsynth, 4x oversampled)
# for the Atari to STREAM to DMA sound. Emits build/music/*.PCM (U6PC format; gitignored, derived from
# the copyrighted score). PCM_RATE = DMA rate. m2pcm auto-detects each song's loop and renders intro +
# two loop bodies for a seamless DMA loop; PCM_SECS is only a SAFETY CAP (large so long loops aren't cut).
$(BUILD)/m2pcm: tools/m2pcm.c engine/oplsynth.c dataformats/u6lzw.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -Iengine -o $@ $^
PCM_RATE ?= 12517
PCM_SECS ?= 240
.PHONY: m2pcm
m2pcm: $(BUILD)/m2pcm
	@mkdir -p $(BUILD)/music
	@for f in $(REFDATA)/*.M; do b=$$(basename "$$f" .M); $(BUILD)/m2pcm "$$f" "$(BUILD)/music/$$b.PCM" $(PCM_SECS) $(PCM_RATE); done

# M10 music ON-TARGET: both targets drive the SAME runtime backend selector (SONG.REQ = a song base
# name; MIDI.CFG byte '1'/'0' sets the toggle) — the game then picks MIDI vs DMA itself.
#
# music-dma: MIDI off => 8-bit DMA sound. Renders the PCM fresh, stages it, boots game.prg on the TT
# (STE-compatible DMA sound), records the emulated audio (Hatari AVI), extracts + checks it is real.
DMA_SONG ?= ULTIMA
.PHONY: music-dma
music-dma: gamedata $(BUILD)/game.prg $(BUILD)/m2pcm
	$(BUILD)/m2pcm "$(REFDATA)/$(DMA_SONG).M" "$(BUILD)/$(DMA_SONG).PCM" 240 12517
	printf '$(DMA_SONG)' > $(BUILD)/SONG.REQ
	printf '0' > $(BUILD)/MIDI.CFG
	rm -f $(BUILD)/dma.avi
	-$(HL) $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga --tos '$(TOS_TT)' \
	          --sound 50066 --avirecord --avi-vcodec png --avi-file $(BUILD)/dma.avi \
	          --conout 2 --fast-forward on --run-vbls 3500 --harddrive $(BUILD) --auto 'c:\game.prg' 2>&1 | grep -iaE "music backend" || true
	rm -f $(BUILD)/SONG.REQ $(BUILD)/MIDI.CFG
	@if command -v ffmpeg >/dev/null && [ -f $(BUILD)/dma.avi ]; then \
	   ffmpeg -y -i $(BUILD)/dma.avi -vn $(BUILD)/wav/$(DMA_SONG)_dma.wav >/dev/null 2>&1; \
	   echo "extracted audio -> $(BUILD)/wav/$(DMA_SONG)_dma.wav (play it to verify DMA output)"; \
	 else echo "AVI at $(BUILD)/dma.avi (install ffmpeg to auto-extract the audio track)"; fi

# music-tt: MIDI on => MIDI OUT. Stages the .M + MIDI.DAT + MIDI.CFG='1' and launches interactively;
# with your hatari.cfg [Midi] sMidiOutPortName pointing at your MT-32, the ENGINE plays it on the synth.
MUSIC_SONG ?= ULTIMA
.PHONY: music-tt
music-tt: gamedata $(BUILD)/game.prg
	@b=$$(basename '$(MUSIC_SONG)' .M); cp "$(REFDATA)/$$b.M" '$(REFDATA)/MIDI.DAT' $(BUILD)/; printf "$$b" > $(BUILD)/SONG.REQ
	printf '1' > $(BUILD)/MIDI.CFG
	-$(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga --midi on \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\game.prg'
	rm -f $(BUILD)/SONG.REQ $(BUILD)/MIDI.CFG
	@echo "played $(MUSIC_SONG) out MIDI (routed by your hatari.cfg [Midi] port)"

# music-st: plain ST => YM2149 PSG (3-voice square). No DMA on the ST, so auto-select picks PSG.
# Stages the .M + SONG.REQ and launches gamest.prg on a 4 MB ST with sound so you hear the chiptune.
PSG_SONG ?= BRIT
.PHONY: music-st
music-st: gamedata-st $(BUILD)/gamest.prg
	@b=$$(basename '$(PSG_SONG)' .M); cp "$(REFDATA)/$$b.M" $(BUILD)/; printf "$$b" > $(BUILD)/SONG.REQ
	-$(HATARI) --machine st --memsize 4 --monitor rgb --sound 44100 \
	          --tos '$(TOS_ST)' --harddrive $(BUILD) --auto 'c:\gamest.prg'
	rm -f $(BUILD)/SONG.REQ $(BUILD)/$(PSG_SONG).M
	@echo "played $(PSG_SONG) on the YM2149 PSG (plain ST)"

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

# save-template shape patchers (record deletion + hull qty): synthetic buffer, no game data
$(BUILD)/savetest: tools/savetest.c engine/save.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -Iengine -Iplatform -o $@ $^
savetest: $(BUILD)/savetest
	$(BUILD)/savetest

# run one real U6 conversation through the VM on the host and report its WORKTYPE effects
# (usecode_vm.md §6b): make wtrun NPC=49 KEYWORDS="name job gyps"
$(BUILD)/wtrun: tools/wtrun.c engine/converse_vm.c engine/converse_vm.h dataformats/u6lzw.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -Iengine -Idataformats -o $@ tools/wtrun.c dataformats/u6lzw.c
wtrun: $(BUILD)/wtrun
	$(BUILD)/wtrun $(REFDATA) $(NPC) $(KEYWORDS)

# equipment slot-model unit test (docs/rules/equipment.md)
$(BUILD)/equiptest: tools/equiptest.c engine/equip.c engine/combat.c engine/rng.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -Iengine -o $@ $^
equiptest: $(BUILD)/equiptest
	$(BUILD)/equiptest

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
             render/render.c render/rdr_tt.c render/c2p_asm.s render/rdemo.c
# Hybrid 020/no-FPU build (same as game.prg): runs on a stock FPU-less TT/Falcon.
$(BUILD)/rdemo.prg: $(RDEMO_SRC)
	$(call HYBRID_BUILD,$@,$^,obj_rdemo,-DHAVE_RDR_TT)

rdemo: $(BUILD)/rdemo.prg

# ---- performance benchmark: time compose vs c2p present on the TT ------------
BENCH_SRC := platform/detect.c platform/platmem.c platform/platfile.c platform/platinput.c \
             platform/plattime.c platform/platmidi.c platform/platdma.c platform/platpsg.c \
             engine/world.c engine/rng.c engine/converse_vm.c engine/objects.c \
             engine/save.c engine/combat.c engine/magic.c engine/music.c engine/oplsynth.c \
             dataformats/u6map.c dataformats/u6actor.c dataformats/u6lzw.c dataformats/u6obj.c \
             render/render.c render/rdr_tt.c render/c2p_asm.s render/gamedemo.c
# Hybrid 020/no-FPU build (same as game.prg) + -DBENCH: runs on a stock FPU-less TT.
$(BUILD)/bench.prg: $(BENCH_SRC)
	$(call HYBRID_BUILD,$@,$^,obj_bench,-DBENCH -DHAVE_RDR_TT)

# run the bench headless; BENCH.BIN = 5 big-endian u32 matching gamedemo.c's harness:
# [0]=SCROLL compose (walking), [1]=FULL compose (no cache), [2]=present map-window (c2p+double),
# [3]=unused(0), [4]=N. --conout 2 also prints the per-phase compose split (ph0..ph3).
# --ttram 16 (not 4): the full 256-colour U6.PAK (~2.5 MB) + TPA overflow a 4 MB TT fast pool, so the
# pack load fails and bench.prg aborts with "unknown game magic" (== NULL pak) BEFORE writing BENCH.BIN.
# 8 MB is the threshold; 16 gives margin. (A real 4 MB TT needs a slim/streamed pack — a deployment
# concern, see the fast-pool-tight-on-tt note; the bench measures COMPUTE, so more RAM is fine here.)
bench: world $(BUILD)/bench.prg
	rm -f $(BUILD)/BENCH.BIN
	$(HL) $(HATARI) --machine tt --ttram 16 --addr24 off --monitor vga \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\bench.prg' \
	          --fast-forward on --run-vbls 12000 </dev/null >/dev/null 2>&1 || true
	@od -An -tu4 --endian=big -w80 $(BUILD)/BENCH.BIN 2>/dev/null | awk '{n=$$5; if(n>0) printf "TT (32MHz 68030, 256-col)  SCROLL compose: %.1f ms   FULL compose: %.1f ms   present c2p+double: %.1f ms   (per frame, n=%d)\n", $$1*5.0/n, $$2*5.0/n, $$3*5.0/n, n; else print "no BENCH.BIN (TT bench needs --ttram >=8 to fit the full pack)"}' || echo "no BENCH.BIN"

# ---- Falcon frame bench: the SAME harness as `bench`, but the Falcon VIDEL backend (rdr_falcon.c,
# 16-bit truecolor present = NO c2p) on a 68030 @ 16 MHz. Confirms the Falcon frame rate.
BENCH_FALCON_SRC := $(patsubst render/rdr_tt.c,render/rdr_falcon.c,$(BENCH_SRC))
$(BUILD)/benchf.prg: $(BENCH_FALCON_SRC)
	$(call HYBRID_BUILD,$@,$^,obj_benchf,-DBENCH -DHAVE_RDR_FALCON)
bench-falcon: world $(BUILD)/benchf.prg $(BUILD)/NEWDESK.INF
	rm -f $(BUILD)/BENCH.BIN
	-$(HL) $(HATARI) --machine falcon --fast-boot on --monitor vga \
	          --tos '$(TOS_FALC)' --harddrive $(BUILD) --auto 'C:\BENCHF.PRG' \
	          --fast-forward on --run-vbls 12000 </dev/null >/dev/null 2>&1 || true
	@od -An -tu4 --endian=big -w80 $(BUILD)/BENCH.BIN 2>/dev/null | awk '{n=$$5; if(n>0) printf "Falcon (68030) SCROLL compose: %.1f ms   present: %.1f ms   => frame %.1f ms = %.2f fps (n=%d)\n", $$1*5.0/n, $$3*5.0/n, $$1*5.0/n+$$3*5.0/n, 1000.0/($$1*5.0/n+$$3*5.0/n), n; else print "no BENCH.BIN"}' || echo "no BENCH.BIN"

# ---- ST frame bench: the SAME harness as `bench`, but the ST backend (rdr_st.c, 4-plane C c2p)
# on a real 8 MHz 68000 (`--machine st`), so we measure the ACTUAL ST cost the TT bench can't.
# BENCH.BIN is the same 5 u32 [scroll-compose, full-compose, present-c2p, unused, N]. Set
# START_MIN=<0..1439> to force night (exercises the lighting path); stage LIGHTSTY.REQ ('0'/'1'/'2')
# in build/ before running to pick SMOOTH/DITHER/BINARY. Loads the same U6.PAK + U6FE.PAK as the game.
BENCH_ST_SRC := $(filter-out render/rdr_tt.c render/c2p_asm.s,$(BENCH_SRC)) render/rdr_st.c render/c2p_st.s render/ppresent_st.s
$(BUILD)/benchst.prg: $(BENCH_ST_SRC)
	@mkdir -p $(BUILD)/obj_benchst
	@for s in $^; do o=$(BUILD)/obj_benchst/`echo $$s | tr / _`.o; \
	  $(CC) -m68000 -std=gnu11 -O2 -Wall -DBENCH -DHAVE_RDR_ST $(HYBRID_INC) -c $$s -o $$o || exit 1; done
	$(CC) -m68000 -std=gnu11 -O2 -o $@ $(BUILD)/obj_benchst/*.o
	@echo "built $@ (pure 68000, ST-low 16-colour bench)"
# BLIT=1 adds Hatari's --blitter on so a plain-ST run emulates a Blitter-equipped Mega ST (8 MHz +
# Blitter), isolating the Blitter win at the SAME CPU speed as the no-Blitter bench-st baseline.
BLIT ?=
BLIT_FLAG := $(if $(filter 1 on yes,$(BLIT)),--blitter on,)
bench-st: gamedata-st $(BUILD)/benchst.prg
	rm -f $(BUILD)/BENCH.BIN
	$(HL) $(HATARI) --machine st $(BLIT_FLAG) --memsize 4 --monitor rgb \
	          --tos '$(TOS_ST)' --harddrive $(BUILD) --auto 'c:\benchst.prg' \
	          --fast-forward on --run-vbls 20000 </dev/null >/dev/null 2>&1 || true
	@od -An -tu4 --endian=big -w80 $(BUILD)/BENCH.BIN 2>/dev/null | awk '{n=$$5; if(n>0){ printf "ST (8MHz 68000)  SCROLL compose: %.1f ms   FULL compose: %.1f ms   present c2p: %.1f ms   (per frame, n=%d)\n", $$1*5.0/n, $$2*5.0/n, $$3*5.0/n, n; printf "  scroll phases  ph0 static: %.1f  ph1 actors+fg: %.1f  ph2 light+vis: %.1f  ph3 composite: %.1f ms\n", $$6*5.0/n, $$7*5.0/n, $$8*5.0/n, $$9*5.0/n; if($$11>0) printf "  BLITTER  selftest diff: %d   masked blit 11x11 grid: CPU %.1f ms -> Blitter %.1f ms  (%.2fx)\n", $$10, $$12*5.0/n, $$13*5.0/n, ($$13>0?$$12/$$13:0); else print "  BLITTER  absent (CPU blit path; add BLIT=1 to emulate a Mega ST Blitter)"; if($$15>0) printf "  NATIVE-PLANAR INV2 (g_ptmp vs c2p): diff %d over %d frames\n", $$14, $$15; if($$11>0) printf "  PRESENT  selftest diff: %d   present: CPU %.1f ms -> Blitter %.1f ms  (%.2fx)\n", $$16, $$17*5.0/n, $$18*5.0/n, ($$18>0?$$17/$$18:0); } else print "no BENCH.BIN"}' || echo "no BENCH.BIN"

# ---- STE frame bench: same benchst.prg (pure 68000) on an emulated STE, which HAS a Blitter built in.
# This is the real Phase-3 target: 8 MHz 68000 + Blitter. The BENCH harness auto-detects the Blitter
# (rdr_blitter_ok) and reports the CPU-vs-Blitter masked-blit microbench + the byte-identical selftest.
.PHONY: bench-ste
bench-ste: gamedata-st $(BUILD)/benchst.prg
	rm -f $(BUILD)/BENCH.BIN
	$(HL) $(HATARI) --machine ste --memsize 4 --monitor rgb \
	          --tos '$(TOS_STE)' --harddrive $(BUILD) --auto 'c:\benchst.prg' \
	          --fast-forward on --run-vbls 20000 </dev/null >/dev/null 2>&1 || true
	@od -An -tu4 --endian=big -w80 $(BUILD)/BENCH.BIN 2>/dev/null | awk '{n=$$5; if(n>0){ printf "STE (8MHz 68000 + Blitter)  SCROLL compose: %.1f ms   FULL compose: %.1f ms   present: %.1f ms   (per frame, n=%d)\n", $$1*5.0/n, $$2*5.0/n, $$3*5.0/n, n; printf "  scroll phases  ph0 static: %.1f  ph1 actors+fg: %.1f  ph2 light+vis: %.1f  ph3 composite: %.1f ms\n", $$6*5.0/n, $$7*5.0/n, $$8*5.0/n, $$9*5.0/n; if($$11>0) printf "  BLITTER  selftest diff: %d   masked blit 11x11 grid: CPU %.1f ms -> Blitter %.1f ms  (%.2fx)\n", $$10, $$12*5.0/n, $$13*5.0/n, ($$13>0?$$12/$$13:0); else print "  BLITTER  NOT DETECTED (expected on STE!)"; if($$15>0) printf "  NATIVE-PLANAR INV2 (g_ptmp vs c2p): diff %d over %d frames\n", $$14, $$15; if($$11>0) printf "  PRESENT  selftest diff: %d   present: CPU %.1f ms -> Blitter %.1f ms  (%.2fx)\n", $$16, $$17*5.0/n, $$18*5.0/n, ($$18>0?$$17/$$18:0); } else print "no BENCH.BIN"}' || echo "no BENCH.BIN"

# ---- M10 OPL2 synth throughput probe: measure how many mono samples/sec the software FM synth
# can render on each machine (all 9 voices sounding), to decide real-time synth vs host pre-rendered
# PCM per target. Needs no game data. SBENCH_OS = oversampling factor (1 = cheapest). Runs on TT
# (68030) and STE (68000) for both data points. BENCH.BIN = 4 u32: samples, ticks, samples/sec, rate.
SBENCH_OS ?= 1
.PHONY: sbench
sbench: $(BUILD)/bench.prg
	@printf '$(SBENCH_OS)' > $(BUILD)/SBENCH.REQ
	@for m in tt ste; do \
	  rm -f $(BUILD)/BENCH.BIN; \
	  args=""; [ $$m = tt ] && args="--machine tt --ttram 4 --addr24 off --monitor vga --tos '$(TOS_TT)'"; \
	  [ $$m = ste ] && args="--machine ste --tos '$(TOS_STE)'"; \
	  eval $(HL) $(HATARI) $$args --harddrive $(BUILD) --auto 'c:\\bench.prg' \
	       --fast-boot on --fast-forward on --run-vbls 6000 </dev/null >/dev/null 2>&1 || true; \
	  printf "%-4s: " $$m; \
	  od -An -tu4 --endian=big -w80 $(BUILD)/BENCH.BIN 2>/dev/null | awk '{if($$3>0) printf "%d samples/sec (%d samples in %d ticks) vs DMA rate %d => %.1fx realtime headroom\n",$$3,$$1,$$2,$$4,$$3/$$4.0; else print "no BENCH.BIN"}'; \
	done
	@rm -f $(BUILD)/SBENCH.REQ

# ---- INTRO render-cost bench: run the living-room scene (0) through the REAL run_intro and
# report per-frame cost split HOLD vs PAN x compose/c2p+double/output. BENCH.BIN = 8 big-endian
# u32: HOLD{compose,c2p,out,frames} PAN{compose,c2p,out,frames}. (Hatari --run-vbls is flaky at
# self-terminating here, so bound wall-clock with timeout -s KILL; BENCH.BIN is written by the
# program before it returns, so a hard kill after it completes is fine.)
bench-intro: world $(BUILD)/bench.prg
	rm -f $(BUILD)/BENCH.BIN
	printf 1 > $(BUILD)/IBENCH.REQ
	timeout -s KILL 120 env SDL_RENDER_DRIVER=software $(HL) $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\bench.prg' \
	          --fast-boot on --fast-forward on --run-vbls 6000 </dev/null >/dev/null 2>&1 || true
	-pkill -9 hatari 2>/dev/null; rm -f $(BUILD)/IBENCH.REQ
	@od -An -tu4 --endian=big -w80 $(BUILD)/BENCH.BIN 2>/dev/null | awk '{hn=$$4; pn=$$8; \
	  if(hn>0){ printf "HOLD (n=%d): compose %.1f  c2p+dbl %.1f  output %.1f  => %.1f ms/frame (%.1f fps)\n", \
	    hn, $$1*5.0/hn, $$2*5.0/hn, $$3*5.0/hn, ($$1+$$2+$$3)*5.0/hn, 1000.0/(($$1+$$2+$$3)*5.0/hn+0.001) } \
	  if(pn>0){ printf "PAN  (n=%d): compose %.1f  c2p+dbl %.1f  output %.1f  => %.1f ms/frame (%.1f fps)\n", \
	    pn, $$5*5.0/pn, $$6*5.0/pn, $$7*5.0/pn, ($$5+$$6+$$7)*5.0/pn, 1000.0/(($$5+$$6+$$7)*5.0/pn+0.001) } \
	  if(hn==0&&pn==0) print "no BENCH.BIN (intro bench did not complete — raise --run-vbls?)" }' || echo "no BENCH.BIN"

# ---- M5 game demo: a walkable world on the TT --------------------------------
# Default start = the CANONICAL U6 NEW GAME: the pristine OBJLIST puts the Avatar at
# (307,352) — before Lord British's throne (LB sits at 307,348) — with the clock at
# 08:00 (mkworld takes the time of day from the OBJLIST too, so LB is ON the throne).
# The old default (315,367) was LB's NOON dining spot in the great hall, chosen so a
# headless capture could auto-talk him; that scene is still available for the harness:
#   make m5 GX=315 GY=367 START_MIN=720
# (perf figures in docs/design/performance.md were measured on that noon scene).
GX ?= 307
GY ?= 352
MKWORLD_SRC := dataformats/u6lzw.c dataformats/u6lzc.c dataformats/u6lib.c dataformats/u6pal.c dataformats/u6tile.c dataformats/u6obj.c dataformats/u6shape.c dataformats/u6map.c engine/combat.c engine/rng.c
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

# `world` writes the PAIR every target loads: U6.PAK (the gameplay core, ~1.1 MB) + U6FE.PAK (the
# sectioned front-end/intro art, ~1.8 MB) — one data set for ST/TT/Falcon; the engine keeps the
# front-end resident or streams it by section group depending on the machine's RAM
# (docs/design/memory_streaming.md). world-st / world-tt are aliases kept for the older targets.
# The pack is built FROM THE PRISTINE NEW-GAME SAVE (mksave synthesizes it from LZOBJBLK +
# LZDNGBLK, the same inputs Nuvie's new game reads): objects, actors, party inventories and
# readied gear, gold, spellbook, reagents all come from that one state, and the SAME file ships
# as the SAVE.000 template the on-target writer patches. GX/GY override the objlist start.
world: $(BUILD)/mkworld $(BUILD)/nuviesave save-template roof-extract
	$(BUILD)/nuviesave $(BUILD)/SAVE.000 $(BUILD)/new.objblk $(BUILD)/new.objlist $(BUILD)/new.inv
	$(BUILD)/mkworld $(REFDATA) $(BUILD)/U6.PAK $(GX) $(GY) $(BUILD)/new.objblk $(BUILD)/new.objlist $(BUILD)/new.inv
world-st world-tt: world
gamedata-st gamedata-tt: world

# M9: emit a CORE world pack for Savage / Martian from the user's SE/MD data.
# Reuses the U6 loaders (MAP/CHUNKS/tileset/palette/passable/objects); SE/MD wrap
# the compressible core files in a single-item .LZC which mkworld unwraps.
# SE_REFDATA / MD_REFDATA are defined with the lzctest target above.
# default start = the dense village cluster coredump found (SE Kurak-valley huts),
# so a boot lands on recognizable land rather than open ocean.
SE_X ?= 380
SE_Y ?= 555
MD_X ?= 124
MD_Y ?= 168
world-se: $(BUILD)/mkworld
	MKWORLD_GAME=savage $(BUILD)/mkworld $(SE_REFDATA) $(BUILD)/SE.PAK $(SE_X) $(SE_Y)
world-md: $(BUILD)/mkworld
	MKWORLD_GAME=martian $(BUILD)/mkworld $(MD_REFDATA) $(BUILD)/MD.PAK $(MD_X) $(MD_Y)

# SE with its initial ACTOR ROSTER (CACT). SE ships no savegame/objlist, so we
# capture it from a ScummVM new-game save (docs/design/multigame.md P2 +
# memory se-actor-capture): nuviesave extracts the objlist, then mkworld emits a
# CACT section from it so the Kurak village boots populated with NPCs + party.
SE_SCUMMVM_SAVE ?= $(HOME)/.local/share/scummvm/saves/thesavageempire_enh.001
world-se-svm: $(BUILD)/mkworld $(BUILD)/nuviesave
	$(BUILD)/nuviesave "$(SE_SCUMMVM_SAVE)" $(BUILD)/se.objblk $(BUILD)/se.objlist
	MKWORLD_GAME=savage $(BUILD)/mkworld $(SE_REFDATA) $(BUILD)/SE.PAK $(SE_X) $(SE_Y) $(BUILD)/se.objlist

# host-validate a core pack: render its densest object cluster (terrain+objects)
COREDUMP_SRC := dataformats/u6pal.c dataformats/u6map.c tools/png.c
$(BUILD)/coredump: tools/coredump.c $(COREDUMP_SRC)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
coredump-se: $(BUILD)/coredump world-se
	$(BUILD)/coredump $(BUILD)/SE.PAK $(BUILD)/coredump-se.png
coredump-md: $(BUILD)/coredump world-md
	$(BUILD)/coredump $(BUILD)/MD.PAK $(BUILD)/coredump-md.png

# decode a ScummVM/Nuvie U6 save -> extract surface objects + objlist (host)
$(BUILD)/nuviesave: tools/nuviesave.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
# synthesize the PRISTINE new-game save (ScummVM layout) from the user's LZOBJBLK + LZDNGBLK:
# the SAVE.000 template the on-target writer patches, and the state the shipped pack is built from
$(BUILD)/mksave: tools/mksave.c dataformats/u6lzw.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^
# Always regenerated (ms): `world-svm` / `save-engine` overwrite build/SAVE.000 with the user's
# ScummVM save, and the pristine pack must never be built from that by accident.
.PHONY: save-template
save-template: $(BUILD)/mksave
	$(BUILD)/mksave $(REFDATA) $(BUILD)/SAVE.000
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
	cp '$(SCUMMVM_SAVE)' $(BUILD)/SAVE.000          # the matching save template (pack + template = one state)

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
            platform/plattime.c platform/platmidi.c platform/platdma.c platform/platpsg.c \
            engine/world.c engine/rng.c engine/converse_vm.c engine/objects.c engine/save.c \
            engine/combat.c engine/magic.c engine/music.c engine/equip.c engine/monsters.c \
            dataformats/u6map.c dataformats/u6actor.c dataformats/u6lzw.c dataformats/u6obj.c \
            render/render.c render/rdr_tt.c render/c2p_asm.s render/gamedemo.c
# 020-OPTIMIZED, FPU-FREE build (resolves the no-FPU vs -m68020-60 conflict).
# Compile each source with -m68020-60 (68020+ integer codegen — bitfield ops, 32-bit
# mul/div, richer addressing; the engine uses NO floats so it emits ZERO FPU ops), but
# LINK against the DEFAULT 68000 soft-float libc. Rationale: the m68020-60 libc's startup
# (checkcpu.o) aborts with "This program requires a 68881..." on an FPU-less machine; the
# 68000 libc has no such gate AND contains no FPU instructions, so it runs everywhere a
# 68000 binary does. Net: 020-fast hot paths (compose/blit) + a libc that runs on a stock
# Falcon030 / FPU-less TT030. (Hand-asm c2p stays 68000.) See docs/design/toolchain.md.
# $(1)=output .prg  $(2)=sources  $(3)=obj subdir  $(4)=extra CFLAGS  $(5)=extra link libs
HYBRID_INC := -Iplatform -Irender -Iengine -Idataformats
define HYBRID_BUILD
	@mkdir -p $(BUILD)/$(3)
	@for s in $(2); do o=$(BUILD)/$(3)/`echo $$s | tr / _`.o; \
	  a=-m68020-60; \
	  case $$s in *plattime.c|*rdr_st.c) a=-m68000;; esac; \
	  $(CC) $$a -std=gnu11 -O2 -Wall $(HYBRID_INC) $(4) -c $$s -o $$o || exit 1; done
	$(CC) -std=gnu11 -O2 -o $(1) $(BUILD)/$(3)/*.o $(5)
	@echo "built $(1) (68020 codegen + 68000 soft-float libc; runs FPU-free)"
endef
# rdr_st.c is forced to -m68000 for the same reason as plattime.c: its Timer-B raster
# interrupt_handler would, under the hard-float -m68020-60 multilib, save FPU regs in its
# prologue (illegal on a stock FPU-less machine). Portable C, so 68000 codegen is fine.
# NOTE: plattime.c is forced to -m68000. Its MFP Timer-A interrupt handler
# (interrupt_handler attribute) would, under the hard-float -m68020-60 multilib,
# emit an FPU register save (fmovem fp0-fp1) in its prologue — an illegal
# instruction on a stock FPU-less TT/Falcon. -m68000 has no FPU so the ISR saves
# only d0-d1/a0-a1. Safe because the whole engine is FPU-free (no FP state to keep).

# UNIFIED flagship (M8): ONE binary that links every backend and picks it at runtime from the
# hardware detection (render.c: EdDI cookie -> vcard/VDI, else _VDO -> Falcon/TT/ST). Covers the whole
# 68020+ tier — TT, Falcon (built-in VIDEL), AND a graphics card / NVDI — from a single .prg. Links
# libgem16 for the VDI/AES path (unused, harmless, on the direct paths). The plain-68000 ST/STE
# (gamest.prg) and the 68000+card (gamevc.prg) stay separate binaries only because a 68020-codegen
# image can't run on a 68000 — the SAME render.c dispatcher drives all of them. See docs/design/renderer-unification.md.
HAVE_ALL := -DHAVE_RDR_ST -DHAVE_RDR_TT -DHAVE_RDR_FALCON -DHAVE_RDR_VCARD
GAME_SRC_UNI := $(GAME_SRC) render/rdr_st.c render/c2p_st.s render/ppresent_st.s render/rdr_falcon.c render/rdr_vcard.c
$(BUILD)/game.prg: $(GAME_SRC_UNI)
	$(call HYBRID_BUILD,$@,$^,obj_tt,$(HAVE_ALL),-lgem16)
game: $(BUILD)/game.prg

# Falcon-only test build (VIDEL 320x240, rdr_falcon.c). Redundant with the unified game.prg (which
# also auto-selects Falcon) but kept as a smaller single-backend image for isolated Falcon testing.
# 8.3 name (gamef.prg) for Hatari GEMDOS-HDD --auto. Same hybrid 020/no-FPU build.
GAME_SRC_FALCON := $(patsubst render/rdr_tt.c,render/rdr_falcon.c,$(GAME_SRC))
$(BUILD)/gamef.prg: $(GAME_SRC_FALCON)
	$(call HYBRID_BUILD,$@,$^,obj_falcon,-DHAVE_RDR_FALCON)
game-falcon: $(BUILD)/gamef.prg

# ST/STE variant (M7): the 16-colour shifter backend (rdr_st.c) + the pack's host-quantised
# palette (U6WW). PURE 68000 (NOT the 020 hybrid) — a plain ST/STE has a 68000, so we compile
# for -m68000 and link the stock 68000 libc. Drops c2p_asm.s (the TT's 8-plane asm); rdr_st uses
# its own 4-plane c2p — hand-asm c2p_st.s (C reference c2p4_lut kept). 8.3 name for Hatari --auto.
GAME_SRC_ST := $(filter-out render/rdr_tt.c render/c2p_asm.s,$(GAME_SRC)) render/rdr_st.c render/c2p_st.s render/ppresent_st.s
# Loads U6.PAK (core) + U6FE.PAK (front-end, streamed by section group on a 4 MB ST) like every target.
$(BUILD)/gamest.prg: $(GAME_SRC_ST)
	@mkdir -p $(BUILD)/obj_st
	@for s in $^; do o=$(BUILD)/obj_st/`echo $$s | tr / _`.o; \
	  $(CC) -m68000 -std=gnu11 -O2 -Wall -DHAVE_RDR_ST $(HYBRID_INC) -c $$s -o $$o || exit 1; done
	$(CC) -m68000 -std=gnu11 -O2 -o $@ $(BUILD)/obj_st/*.o
	@echo "built $@ (pure 68000, ST-low 16-colour shifter via render.c dispatch)"
game-st: $(BUILD)/gamest.prg

# ---- TT single-backend build: the 256-colour TT backend (rdr_tt.c, 8-plane c2p) alone (the unified
# game.prg also covers the TT). Same data + memory policy as every target: U6.PAK core + the U6FE.PAK
# front-end, resident or streamed by section group from the machine's free RAM at boot
# (docs/design/memory_streaming.md) — this is what lets a STOCK 4 MB TT boot the whole game.
$(BUILD)/gamett.prg: $(GAME_SRC)
	$(call HYBRID_BUILD,$@,$^,obj_tt_slim,-DHAVE_RDR_TT)
game-tt: $(BUILD)/gamett.prg

# TT shot: prove the 256-colour build renders correctly AND fits at the STOCK --ttram 4.
shot-tt: gamedata-tt $(BUILD)/gamett.prg
	rm -f $(BUILD)/gamett.avi $(BUILD)/gamett_screen.png
	$(HL) $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\gamett.prg' \
	          --fast-forward on --run-vbls 8000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/gamett.avi 2>/dev/null || true
	-ffmpeg -y -i $(BUILD)/gamett.avi -update 1 $(BUILD)/gamett_screen.png 2>/dev/null
	@echo "wrote $(BUILD)/gamett_screen.png (TT 256-colour @ stock 4 MB)"

# GRAPHICS-CARD variant (M8): the VDI card backend (rdr_vcard.c) instead of the direct-hardware
# ST/TT/Falcon ones. Runs as a GEM app (rdr_vcard does appl_init + takes the screen with wind_update)
# and presents the 320x240 frame centred on the card via VDI (c2p -> vr_trnfm -> vro_cpyfm). PURE
# 68000 (a card can sit on any Atari, incl. a plain-68000 ST) + libgem16 (VDI/AES) + rdr_vcard's own
# portable C c2p (drops c2p_asm.s). Full pack (a card machine has the RAM); no ST-stream front-end.
# 256-colour card path is validated; run a truecolour-capable card in a 256-colour mode (see
# docs/design/vcard-aranym-rig.md). Test under ARAnyM+fVDI+AFROS.
GAME_SRC_VCARD := $(filter-out render/rdr_tt.c render/c2p_asm.s,$(GAME_SRC)) render/rdr_vcard.c
$(BUILD)/gamevc.prg: $(GAME_SRC_VCARD)
	@mkdir -p $(BUILD)/obj_vc
	@for s in $^; do o=$(BUILD)/obj_vc/`echo $$s | tr / _`.o; \
	  $(CC) -m68000 -std=gnu11 -O2 -Wall -DHAVE_RDR_VCARD $(HYBRID_INC) -c $$s -o $$o || exit 1; done
	$(CC) -m68000 -std=gnu11 -O2 -o $@ $(BUILD)/obj_vc/*.o -lgem16
	@echo "built $@ (pure 68000 + card; VDI backend via render.c dispatch, libgem16)"
game-vcard: $(BUILD)/gamevc.prg

# 68000 graphics-card variant (M8): the card backend for a Mega ST/STe + a VDI card (e.g. ATW800/2 with
# NVDI). Same as gamevc.prg — kept as a separate 8.3 image name for the 4 MB Mega install; the memory
# policy (streamed front-end + on-demand libraries on tight RAM) is the same runtime code in every
# build now (docs/design/memory_streaming.md). Gameplay tiles are 8-bit, so the card shows full 256
# colour (or the card's truecolour mode).
GAME_SRC_VCARD_ST := $(filter-out render/rdr_tt.c render/c2p_asm.s,$(GAME_SRC)) render/rdr_vcard.c
$(BUILD)/gamevcs.prg: $(GAME_SRC_VCARD_ST)
	@mkdir -p $(BUILD)/obj_vcs
	@for s in $^; do o=$(BUILD)/obj_vcs/`echo $$s | tr / _`.o; \
	  $(CC) -m68000 -std=gnu11 -O2 -Wall -DHAVE_RDR_VCARD $(HYBRID_INC) -c $$s -o $$o || exit 1; done
	$(CC) -m68000 -std=gnu11 -O2 -o $@ $(BUILD)/obj_vcs/*.o -lgem16
	@echo "built $@ (pure 68000 + card; libgem16)"
game-vcard-st: $(BUILD)/gamevcs.prg

# (world-st / gamedata-st: aliases of `world`, defined next to it.)

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
	rm -f $(BUILD)/gamef.avi $(BUILD)/gamef_screen.png $(BUILD)/SAVEDEMO.REQ   # no save demo => static render
	$(HL) $(HATARI) --machine falcon --fast-boot on --conout 2 \
	          --tos '$(TOS_FALC)' --harddrive $(BUILD) --auto 'C:\GAMEF.PRG' \
	          --fast-forward on --run-vbls 3500 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/gamef.avi 2>/dev/null || true
	-ffmpeg -y -i $(BUILD)/gamef.avi -update 1 $(BUILD)/gamef_screen.png 2>/dev/null
	@echo "wrote $(BUILD)/gamef_screen.png (Falcon VIDEL 320x240, ScummVM-save state)"

# M8: Falcon 16-bit TRUECOLOR capture. Same rig as m5-svm-falcon but stages VIDEO.CFG='T' so the
# engine calls rdr_prefer_truecolor -> VIDEL BPS16 (0x114). The AVI captures the REAL VIDEL output
# (16-bit direct colour), so this validates the 565 expand-present path (not just the backbuffer).
m5-svm-falcon-tc: world-svm $(BUILD)/gamef.prg $(BUILD)/NEWDESK.INF
	cp '$(REFDATA)/CONVERSE.A' '$(REFDATA)/CONVERSE.B' '$(REFDATA)/PORTRAIT.A' '$(REFDATA)/PORTRAIT.B' $(BUILD)/
	rm -f $(BUILD)/gameftc.avi $(BUILD)/gameftc_screen.png $(BUILD)/SAVEDEMO.REQ   # no save demo => static render
	printf 'T' > $(BUILD)/VIDEO.CFG                                            # request truecolor present
	$(HL) $(HATARI) --machine falcon --fast-boot on --conout 2 \
	          --tos '$(TOS_FALC)' --harddrive $(BUILD) --auto 'C:\GAMEF.PRG' \
	          --fast-forward on --run-vbls 3500 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/gameftc.avi 2>/dev/null || true
	@rm -f $(BUILD)/VIDEO.CFG                                                  # un-stage (default = indexed)
	-ffmpeg -y -i $(BUILD)/gameftc.avi -update 1 $(BUILD)/gameftc_screen.png 2>/dev/null
	@echo "wrote $(BUILD)/gameftc_screen.png (Falcon VIDEL 320x240 16-bit TRUECOLOR)"

# PIXEL-PERFECT screenshot: stage SHOT.REQ so the engine dumps its 320x200 chunky
# backbuffer (+palette) to SHOT.DAT, then convert host-side. NO emulator scaling/
# aspect/overscan in the imaging path — image pixel == logical pixel. The clean
# rig for tile-alignment / bug-hunt work. Runs on the stock Falcon (no FPU).
shot-falcon: world-svm $(BUILD)/gamef.prg $(BUILD)/NEWDESK.INF
	cp '$(REFDATA)/CONVERSE.A' '$(REFDATA)/CONVERSE.B' '$(REFDATA)/PORTRAIT.A' '$(REFDATA)/PORTRAIT.B' $(BUILD)/
	rm -f $(BUILD)/SHOT.DAT $(BUILD)/shot.png $(BUILD)/SAVEDEMO.REQ
	printf 'x' > $(BUILD)/SHOT.REQ                         # stage the dump sentinel (non-empty: load_file rejects 0-byte)
	-$(HL) $(HATARI) --machine falcon --fast-boot on --conout 2 \
	          --tos '$(TOS_FALC)' --harddrive $(BUILD) --auto 'C:\GAMEF.PRG' \
	          --fast-forward on --run-vbls 4000 2>/dev/null
	@rm -f $(BUILD)/SHOT.REQ                               # un-stage (normal runs don't dump)
	@test -f $(BUILD)/SHOT.DAT || { echo "FAIL: engine did not write SHOT.DAT"; exit 1; }
	python3 tools/shot2png.py $(BUILD)/SHOT.DAT $(BUILD)/shot.png
	@echo "wrote $(BUILD)/shot.png (pixel-perfect 320x200, 1:1)"

# M9 P2: boot The Savage Empire from the SHARED Falcon binary + its CORE pack, and
# pixel-dump the map window. Stages SE.PAK as the loaded pack name (PACK_NAME=U6.PAK);
# gamef.prg detects the "SEWT" core magic (gamedef_se) and stubs the U6-only UI tail.
# No CONVERSE/PORTRAIT (SE has none) -> idle conversation panel; that's expected.
shot-se: world-se $(BUILD)/gamef.prg $(BUILD)/NEWDESK.INF
	cp $(BUILD)/SE.PAK $(BUILD)/U6.PAK                     # engine loads PACK_NAME (=U6.PAK)
	rm -f $(BUILD)/SHOT.DAT $(BUILD)/shot-se.png $(BUILD)/SAVEDEMO.REQ
	printf 'x' > $(BUILD)/SHOT.REQ
	-$(HL) $(HATARI) --machine falcon --fast-boot on --conout 2 \
	          --tos '$(TOS_FALC)' --harddrive $(BUILD) --auto 'C:\GAMEF.PRG' \
	          --fast-forward on --run-vbls 4000 2>/dev/null
	@rm -f $(BUILD)/SHOT.REQ
	@test -f $(BUILD)/SHOT.DAT || { echo "FAIL: engine did not write SHOT.DAT (SE boot)"; exit 1; }
	python3 tools/shot2png.py $(BUILD)/SHOT.DAT $(BUILD)/shot-se.png
	@echo "wrote $(BUILD)/shot-se.png (Savage Empire, shared binary + core pack)"

# As shot-se, but on the SE pack WITH the captured actor roster (CACT): boots the
# Kurak village populated with NPCs + party. Verifies the CACT load + actor render.
shot-se-svm: world-se-svm $(BUILD)/gamef.prg $(BUILD)/NEWDESK.INF
	cp $(BUILD)/SE.PAK $(BUILD)/U6.PAK
	rm -f $(BUILD)/SHOT.DAT $(BUILD)/shot-se-svm.png $(BUILD)/SAVEDEMO.REQ
	printf 'x' > $(BUILD)/SHOT.REQ
	-$(HL) $(HATARI) --machine falcon --fast-boot on --conout 2 \
	          --tos '$(TOS_FALC)' --harddrive $(BUILD) --auto 'C:\GAMEF.PRG' \
	          --fast-forward on --run-vbls 4000 2>/dev/null
	@rm -f $(BUILD)/SHOT.REQ
	@test -f $(BUILD)/SHOT.DAT || { echo "FAIL: engine did not write SHOT.DAT (SE+CACT boot)"; exit 1; }
	python3 tools/shot2png.py $(BUILD)/SHOT.DAT $(BUILD)/shot-se-svm.png
	@echo "wrote $(BUILD)/shot-se-svm.png (Savage Empire village, populated via CACT)"

# M7 headless verify: render the game view on an emulated PLAIN ST (68000, ST-low 16-colour
# shifter) and capture the real 4-plane video output, on a colour (rgb) monitor for ST-low. This
# is the headline "optimised 16 vs the lazy EGA port" shot. Runs on a REAL 4 MB ST: the slim pack
# (U6.PAK core + streamed U6FE.PAK) + libs + buffers leave headroom (see the MEMINFO diagnostic).
shot-st: gamedata-st $(BUILD)/gamest.prg
	rm -f $(BUILD)/gamest.avi $(BUILD)/gamest_screen.png
	-$(HL) $(HATARI) --machine st --memsize 4 --monitor rgb \
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
	-$(HL) $(HATARI) --machine st --memsize 4 --monitor rgb --conout 2 \
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
	-$(HL) $(HATARI) --machine st --memsize 4 --monitor rgb --conout 2 \
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
	-$(HL) $(HATARI) --machine ste --memsize 4 --monitor rgb --fast-boot on \
	          --tos '$(TOS_STE)' --harddrive $(BUILD) --auto 'c:\gamest.prg' \
	          --fast-forward on --run-vbls 8000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/gamest.avi 2>/dev/null
	-ffmpeg -y -i $(BUILD)/gamest.avi -update 1 $(BUILD)/gamest_ste.png 2>/dev/null
	@echo "wrote $(BUILD)/gamest_ste.png (emulated STE, 16 colours from the 4096 gamut)"

shot-ste-conv: gamedata-st $(BUILD)/gamest.prg
	rm -f $(BUILD)/gamest.avi $(BUILD)/gamest_ste_conv.png
	python3 -c "open('$(BUILD)/CONVPAL.BIN','wb').write(bytes([$(CONVPAL_ID) & 255]))"
	-$(HL) $(HATARI) --machine ste --memsize 4 --monitor rgb --conout 2 --fast-boot on \
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
	-$(HL) $(HATARI) --machine st --memsize 4 --monitor rgb --conout 2 \
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
	-$(HL) $(HATARI) --machine st --memsize 4 --monitor rgb \
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
	$(HL) $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\game.prg' \
	          --fast-forward on --run-vbls 5000 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/game.avi 2>/dev/null || true
	-ffmpeg -y -i $(BUILD)/game.avi -update 1 $(BUILD)/game_screen.png 2>/dev/null
	@echo "wrote $(BUILD)/game_screen.png"

# NEW-GAME overlay validation (front-end -> gameplay). The DEFAULT non-svm build (gamedata ->
# world) bakes the shipped U6 start — before LB's throne @ (307,352), 08:00, four-member party — into
# U6.PAK; character creation OVERLAYS name/gender/portrait/Str/Dex/Int onto the Avatar. Stage
# CHARGEN.REQ='N' (applies the deterministic 'Milady' sample, no blocking UI) + NEWGAME.BIN (the
# engine prints the assembled new-game state). --conout 2 mirrors the assertions to this terminal.
.PHONY: newgame
newgame: gamedata $(BUILD)/game.prg
	rm -f $(BUILD)/game.avi $(BUILD)/game_screen.png $(BUILD)/SAVEDEMO.REQ
	printf N > $(BUILD)/CHARGEN.REQ                 # apply the 'Milady' sample character (non-blocking)
	printf 1 > $(BUILD)/NEWGAME.BIN                 # non-empty: plat_load_file rejects 0-byte sentinels
	-$(HL) $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga --conout 2 \
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
	rm -f $(BUILD)/game.avi $(BUILD)/status_screen.png $(BUILD)/SAVEDEMO.REQ
	printf N > $(BUILD)/CHARGEN.REQ                 # apply the 'Milady' sample character
	printf '\001' > $(BUILD)/STATUS.REQ             # open the status view for actor 1 (the Avatar)
	-$(HL) $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
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
	rm -f $(BUILD)/game.avi $(BUILD)/pour_frame.png $(BUILD)/SAVEDEMO.REQ
	printf P > $(BUILD)/CHARGEN.REQ                 # held mid-pour frame (P); use q or D for the others
	-$(HL) $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
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
	rm -f $(BUILD)/game.avi $(BUILD)/read_screen.png $(BUILD)/SAVEDEMO.REQ
	printf '\000' > $(BUILD)/READ.BIN               # 0 = read the first readable object in the world
	-$(HL) $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga --conout 2 \
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
	-$(HL) $(HATARI) --machine st --memsize 4 --monitor rgb --conout 2 \
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
	-$(HL) $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga --conout 2 \
	          --tos '$(TOS_TT)' --harddrive $(BUILD) --auto 'c:\game.prg' \
	          --fast-boot on --fast-forward on --run-vbls 3000 2>/dev/null | grep LOWCOL
	rm -f $(BUILD)/LOWCOL.BIN

# same capture, but built from the ScummVM SAVE state (world-svm) for comparison
m5-svm: world-svm $(BUILD)/game.prg
	cp '$(REFDATA)/CONVERSE.A' '$(REFDATA)/CONVERSE.B' '$(REFDATA)/PORTRAIT.A' '$(REFDATA)/PORTRAIT.B' $(BUILD)/
	rm -f $(BUILD)/game.avi $(BUILD)/game_screen.png $(BUILD)/SAVEDEMO.REQ   # no save demo => static render (matches ScummVM)
	$(HL) $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
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
	printf '1' > $(BUILD)/SAVEDEMO.REQ              # hook: walk + door + takes, then write SAVED.000
	rm -f $(BUILD)/game.avi $(BUILD)/game_screen.png $(BUILD)/SAVED.000
	$(HL) $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
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
	rm -f $(BUILD)/SAVEDEMO.REQ $(BUILD)/game.avi $(BUILD)/anim_phase_1.png $(BUILD)/anim_phase_2.png
	$(HL) $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
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
	rm -f $(BUILD)/SAVEDEMO.REQ
	printf '\112' > $(BUILD)/NPCWALK.BIN            # turn 74 (mid-walk for NPC 17)
	rm -f $(BUILD)/game.avi
	$(HL) $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga --tos '$(TOS_TT)' \
	          --harddrive $(BUILD) --auto 'c:\game.prg' --fast-forward on --run-vbls 7500 \
	          --avirecord --avi-vcodec png --avi-file $(BUILD)/game.avi 2>/dev/null || true
	-ffmpeg -y -i $(BUILD)/game.avi -update 1 $(BUILD)/npc_t74.png 2>/dev/null
	printf '\114' > $(BUILD)/NPCWALK.BIN            # turn 76 (two steps later)
	rm -f $(BUILD)/game.avi
	$(HL) $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga --tos '$(TOS_TT)' \
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
	$(HL) $(HATARI) --machine tt --ttram 4 --addr24 off --monitor vga \
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
	$(HL) $(HATARI) --machine st --tos '$(EMUTOS)' $(HATARI_COMMON) \
	          --fast-forward on --run-vbls 2500 \
	          --trace gemdos,xbios --trace-file $(BUILD)/trace.log 2>/dev/null
	@echo "wrote $(BUILD)/trace.log"

# ---- Distribution: Atari .ST floppy images + hard-drive installer -----------
# Build a redistributable floppy set from YOUR OWN game data. The script splits
# the (oversized) packs across 1.44 MB floppies and ships INSTALL.TOS, which
# reassembles them onto a hard drive / CF card on the target machine. Ships no
# data — reads the packs it just built from $(BUILD). Pass extra flags via
# STDISK_ARGS, e.g.:  make st-disks-falcon STDISK_ARGS='--media /path/to/gotek'
.PHONY: st-disks st-disks-falcon st-disks-st st-disks-tt
st-disks: st-disks-falcon
st-disks-falcon:
	scripts/make-st-disks.sh --target falcon $(STDISK_ARGS)
st-disks-st:
	scripts/make-st-disks.sh --target st $(STDISK_ARGS)
st-disks-tt:
	scripts/make-st-disks.sh --target tt $(STDISK_ARGS)

clean:
	rm -rf $(BUILD)
