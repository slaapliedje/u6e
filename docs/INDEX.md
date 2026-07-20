# Spec Index — Status & Confidence Tracker

Tracks every format and rules spec. Keep in sync as decode work proceeds
(referenced by /docs/design/milestones.md, esp. M2/M4/M9).

Status:     TODO · IN PROGRESS · PROBABLE · VERIFIED
Confidence: guessed · probable · confirmed
Games:      U6 = Ultima VI · SE = Savage Empire · MD = Martian Dreams
Per-game:   "shared" if identical across all three; else note deltas.

## Format specs (/docs/formats/)

| Spec file            | Describes                          | Games   | Per-game | Status | Confidence | Impl. subsystem        | Milestone |
|----------------------|------------------------------------|---------|----------|--------|------------|------------------------|-----------|
| lzw.md               | LZW container (LE size hdr, reset) | all     | shared   | VERIFIED | confirmed | /dataformats, /tools   | M2 |
| library.md           | Library/index file structure       | all     | shared   | VERIFIED | confirmed | /dataformats           | M2 |
| lzc.md               | SE/MD ".LZC" flagged library (per-entry LZW/stored) | SE/MD | SE marker 0x01, MD 0x20 | VERIFIED | confirmed (every SE+MD *.LZC tiles to EOF + all LZW items decode to declared size via our u6lzw; SE/MD TALK → readable dialogue) | /dataformats (u6lzc), /tools (lzctest) | M9 |
| map_chunks.md        | MAP + CHUNKS world geography       | all     | shared   | VERIFIED | confirmed | /dataformats, /engine  | M2 |
| tiles.md             | tileindx/maptiles/masktype/objtiles| all     | verify   | VERIFIED | confirmed (incl. pixel-block) | /dataformats, /tools | M2 |
| palette.md           | u6pal / palettes.int / convert.pal | all     | verify   | VERIFIED | confirmed (u6pal) | /tools, /render | M2/M7 |
| palette_cycling.md   | Color-cycling ranges (CLUT rotation)| all    | verify   | VERIFIED | shipped+confirmed (fire 224-231); 232-239 candidate UNCONFIRMED (held back); wide ramps rejected by usage test | /render | M6/M7 |
| objblk.md            | Object placement blocks            | all     | verify   | VERIFIED | confirmed (surface placement + re-serialization; quad order DOS/ST vs row-major in ScummVM save) | /dataformats, /engine | M6 |
| animdata.md          | Tile animation table (ANIMDATA)    | all     | verify   | VERIFIED | confirmed (dest/src/frames + the rule; on-target water animates); D=speed probable | /tools, /render | M6 |
| look.md              | LOOK.LZD object/terrain names      | all     | verify   | VERIFIED | confirmed | /tools, /engine        | M6 |
| tileflag.md          | TILEFLAG per-tile flags + weights  | all     | verify   | VERIFIED | confirmed (weights/gettable) | /tools, /engine | M6 |
| savegame_objects.md  | In-savegame object/positions data  | all     | verify   | SUPERSEDED | n/a | — (folded into objblk.md + objlist.md + nuvie_save.md) | M6 |
| converse.md          | Conversation/usecode container     | all     | verify   | VERIFIED | confirmed (readable layer) | /dataformats | M4/M6 |
| portraits.md         | portrait.a/.b/.z (4B lib, LZW 56x64)| all     | verify   | VERIFIED | confirmed | /dataformats, /tools   | M6 |
| fonts.md             | U6.CH 8x8 game font (+runic/gargish)| all     | verify   | VERIFIED | confirmed | /tools, /render        | M6 |
| shapes.md            | *.shp (intro/end/menu bitmaps)     | all     | verify   | VERIFIED | confirmed (container + shape hdr + RLE blocks; golden MAINMENU/TITLES) | /tools, /render | M6 |
| cursor.md            | U6MCGA.PTR mouse cursors + .shp frame | all  | verify   | VERIFIED | confirmed (arrow) | /tools, /render     | M6 |
| background.md        | PAPER.BMP (U6Bmp: whole-LZW image) | all     | verify   | VERIFIED | confirmed | /dataformats, /tools, /render | M6 |
| schedule.md          | NPC daily schedules (SCHEDULE)     | all     | verify   | VERIFIED | confirmed | /dataformats, /tools   | M6 |
| objlist.md           | Actor table (OBJLIST savegame)     | all     | verify   | VERIFIED | confirmed (pos/appearance/HP/str/dex/int/party) | /dataformats, /tools | M6 |
| nuvie_save.md        | ScummVM/Nuvie save = OUR save fmt (R/W) | all | verify | VERIFIED | confirmed (read + host byte-identical round-trip + ON-TARGET engine writer; ScummVM loads our writes) | /tools, /engine | M6 |
| strings.md           | Books/signs/item/ground text tables| all     | verify   | VERIFIED (books/signs) | confirmed (BOOK.DAT container + quality→entry + on-TT paged read view; item/ground strings TODO) | /tools, /render | M6 |
| our_savefmt.md       | OUR versioned save format (write)  | all     | shared   | SUPERSEDED | n/a | — (we adopt ScummVM's Nuvie fmt → nuvie_save.md) | M6 |
| music.md             | Music (.M) = LZW + OPL2 FM cmd stream | all  | verify   | VERIFIED | confirmed end-to-end: container + OPL2-not-MIDI + opcode/operand table + pitch/operator layout, via mdump on all 12 .M AND the working sequencer+synth+3 backends (make musictest, cross-checked vs ScummVM/Nuvie) | /tools (mdump) + /engine (music,oplsynth) + /platform (midi,dma,psg) | M10 |
| music_areas.md       | Per-area music selection (which track where) | all | per-game area_group hook | VERIFIED | U6 group model confirmed from Nuvie native-U6 loader; overworld/town = random pool (no per-town song); combat→ENGAGE wired; boat/dungeon/gargoyle deferred to those systems | /render (gamedemo music_*) | M10 |
| vdi_scrninfo.md      | VDI vq_scrninfo Device struct + graphics-card blit MFDB (truecolour layout, fd_wdwidth) | n/a (host video) | shared | VERIFIED | confirmed (rdr_vcard truecolour pixel-correct 16/32bpp on the card) | /render (rdr_vcard) | M8 |

## Rules specs (/docs/rules/)

| Spec file            | System                             | Games   | Per-game (RuleSet hook)        | Status | Confidence | Milestone |
|----------------------|------------------------------------|---------|--------------------------------|--------|------------|-----------|
| usecode_vm.md        | Usecode opcode semantics           | all     | verify deltas                  | VERIFIED | confirmed (eval/IF/keyword loop) / partial (mutation ops stubbed) | M4/M6 |
| usecode_use.md       | USE verb object effects (doors...) | all     | use dispatcher (per-game)      | IN PROGRESS | confirmed (doors, containers/stacks, keys/locks, lockpicks, food/hunger, readables, levers/switches/cranks, drawbridges); multi-level stairs scoped | M6 |
| object_anim.md       | Object frame_n animation           | all     | per-game animated-object set    | IN PROGRESS | frame_n = state/orientation (NOT animation); fire glows via colour cycling instead — set currently empty | M6 |
| combat.md            | Combat resolution                  | all     | on_combat_hit (SE/MD deltas)   | VERIFIED | confirmed (CB1-CB4 DONE + on-target: melee/ranged/enemy-AI/readied-gear/alignment/combat-mode/flee/leveling; LB-plot training deferred on VM stat ops) | M6 |
| schedules.md         | NPC daily schedules (behavior)     | all     | verify                         | IN PROGRESS | placement+walk+wander/sleep confirmed; other worktype codes probable | M6 |
| magic_skills.md      | U6 magic / SE skills / MD mechanics| all     | per-game rule set              | IN PROGRESS | confirmed (reagent/spellbook obj_n, mana-cost=circle) / probable (spell table) / guessed (save mana offset, effect magnitudes) | M6/M9 |
| lighting.md          | Day-night / light sources / LOS    | all     | verify                         | VERIFIED | confirmed (stages B/C1/C2/D/E DONE + tuned vs ScummVM: indoor bitmap, LOS flood, ambient+globe light w/ bilinear smoothing, day/night, roof draw; outdoor/night behaviour-validated, some radii still tunable) | M5/M6 |
| map_levels.md        | Level semantics & layout policy    | all     | U6 3D / SE 4-of-6+gargoyle / MD torus | TODO | probable | M2/M9 |
| movement_turns.md    | Movement, turn costs, time advance | all     | verify                         | IN PROGRESS | confirmed (mechanics) / placeholder (passability) | M5 |
| character_creation.md| Gypsy virtue Q&A → name/gender/portrait/Str-Dex-Int | U6 | (U4/U5 differ; U6 only) | VERIFIED | DONE + on-TT validated: name/gender/portrait pick, 7 A/B questions (REAL U.EXE text), tournament+stat math; gypsy casting visuals (3c) — woods pan + 8 virtue vials + central jar that FILL as questions are answered; NEW-GAME overlay (`make newgame`) — shipped start = Lord British's castle (315,367) + 4-member party, chargen overlays the 6 fields onto the Avatar (mana recomputed, gender→VM sex, spellbook handoff obj 57), "Create a Character" → straight into play; STATUS SCREEN (`make status`) — click a roster member to view portrait + gender + Str/Dex/Int + HP/MP + Lev/Exp (Avatar shows the created face/gender); $G HONORIFIC — NPCs address the Avatar milord/milady by gender (get_gender_title). Map-sprite gender swap = N/A (U6 avatar sprite is gender-neutral: single obj_n 410 tile). GYPSY POUR CHOREOGRAPHY — full pixel-faithful port of nuvie gypsy_vial_anim (drain model: vials start full, gypsy tips each winning vial over the jar; 17-frame arm/hand/pour, raw GYPSY.SHP shape table) + JAR BUBBLES (nuvie image_bubble_effect — the essence churns with the poured virtue colours). Polish TODO: gypsy setup narration | front-end |
| design/intro_sequence.md | Full intro cutscene sequence (reverse-engineered from DOS + nuvie) | U6 | (U6 only) | VERIFIED | confirmed + TT-validated end-to-end: living room→window/storm→forest→stones→moongate→avatar-climax, all 4 build phases DONE incl. real U.EXE narration (clean-room extract) + proportional U6.CH captions | front-end |
| design/original-st-renderer.md | Original ST-port renderer architecture (RE) — validates our incremental/windowed/double-buffered model; isolates the headroom the original left (Blitter, STe/Falcon HW fine-scroll) | U6 | (reference) | IN PROGRESS | probable (structure) / guessed (full-refresh tile pitch); converges with measured performance.md; per-address map in external RE workspace | render / M7 |
| design/multigame.md  | M9 plan: run SE + MD from the same binary via data + RuleSet deltas | all | per-game deltas | IN PROGRESS | thesis validated (SE+MD MAP/CHUNKS decode with unmodified U6 decoders); core formats shared; deltas = .LZC/palettes/obj_n/rules; P0 GameDef+manifest next | engine (GameDef/RuleSet), tools (mkworld), M9 |
| design/native-planar-st.md | Native-planar ST renderer — composite planar tiles directly, delete the per-frame c2p (the plain ST's 444 ms / half-frame wall) | U6 (ST/STE target) | (plain ST perf; Falcon/TT keep chunky) | IN PROGRESS | Phases 0–2c DONE + byte-verified vs c2p. P0 thesis (blit 2.8–7.7× cheaper); P1 primitives (`rdr_planar_selftest diff=0`); P2 integrated compose — incremental planar static + dynamic blits + no-c2p present (INV2 `rdr_pverify diff=0` over 41 frames), plain-ST frame 933→619 ms (1.51×), present 444→81 ms (5.5×); P2c LIVE (`present_map`), band-raster-safe LUT + palette-change invalidation; P4a planar cursor (gated the live win); P4b dropped the chunky composite (`g_chunky_dyn=0`) → 1.62 fps; capstone: ST defaults to PREDIM (native planar now the shipping ST renderer); **P4c planar dimming — static composed directly from pre-dimmed planar tiles, whole chunky static dropped → 2.38 fps live (compose 538→339), byte-verified (static-direct/INV2/dim-commute all diff=0) + pixel A/B on real ST identical.** MILESTONE COMPLETE: plain 8 MHz ST **1.07 → 2.38 fps** (2.2×). Future: Blitter (P3, STE/Mega) | render (rdr_st), M7 |

## High-risk items (watch these)
- converse.md + usecode_vm.md — the documented wall; gated at M4.
- palette_cycling.md — must be preserved through 256→16 quantization (M7).
- map_levels.md — the main per-game divergence (M9).

## Notes
- "verify" in Per-game = assume shared until decode proves a delta; if a
  delta is found, record it here and route it to a RuleSet hook (rules) or a
  per-game branch in the loader (formats).
- Confidence starts "guessed"/"probable"; only mark "confirmed" with a stated
  validation method (golden diff / observation) in the spec's provenance block.
- Update this table in the SAME commit that changes a spec's status.

