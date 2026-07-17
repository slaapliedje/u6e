# Milestones & Acceptance Criteria

Project: Ultima VI ST Engine (+ Savage Empire, Martian Dreams)
Targets: base ST (68000) → STE → TT030 → Falcon030 → 68000+video card

## How to use this file
- Work milestones top-to-bottom; later ones depend on earlier ones.
- A milestone is DONE only when ALL acceptance criteria pass.
- Tag a release at M6 and M8 (natural "usable" points).
- Update status markers: [ ] todo · [~] in progress · [x] done.
- Keep /docs/INDEX.md (spec status) in sync as formats/rules get verified.

## Cross-cutting rules (apply to EVERY milestone)
- Big-endian 68000; LE→host conversion only at the file-I/O boundary.
- NO FPU anywhere (base ST none; TT often none; Falcon needs mod). Integer/
  fixed-point only.
- Clean-room: implement from /docs specs, never transcribe reference source.
  Write/Update the spec (with provenance block) before code depends on it.
- Ship NO copyrighted assets; original data is user-provided via manifest.
- Engine asks platform/renderer by INTENT (alloc chip vs fast; caps query),
  never calls TOS traps directly outside /platform & /render.
- PERFORMANCE (ongoing, measured): `make bench` times compose vs c2p present on
  the TT. "Correct first, optimise measured hot paths." Status: the chunky→planar
  present bottleneck is ADDRESSED — windowed map present + animation gate + the
  hand-asm c2p (render/c2p_asm.s) cut the steady-state present from ~583 ms to
  ~52 ms (windowed, doubled): full present 266→117 ms, map window 120→52 ms (2.3×).
  Plan + numbers: /docs/design/performance.md. Re-bench after renderer changes.

---

## M0 — Scaffolding & "hello, hardware"
Status: [x] (done 2026-06-23)
Depends on: nothing
Goal: prove toolchain + build/verify loop before any game logic.

Tasks
- [x] Repo skeleton; all six CLAUDE.md files present; /docs/INDEX.md seeded.
- [x] Cross-compile trivial .PRG with existing m68k GCC.
      (Makefile; `make` -> build/selftest.prg, valid TOS exe magic 0x601A.)
- [x] Boot under Hatari (EmuTOS + GEMDOS HD emulation).
      (`make run`; --harddrive build/ + --auto autostart.)
- [x] platform self-test binary: detect via cookie jar (_VDO, _MCH, _CPU,
      _FPU, Sversion) and print caps. (platform/detect.c + selftest.c.)
- [x] Hatari automation: --run-vbls (`make check` text capture via --conout 2),
      --trace gemdos,xbios (`make trace`), headless visual capture
      (`make shot`, AVI — plain screenshot needs an interactive trigger).

Acceptance
- [x] Self-test prints correct, DISTINCT caps on emulated ST / STE / TT /
      Falcon. (Verified: ST/STE=68000; TT=68030+FPU; Falcon=68030 no-FPU;
      video classes 16/512, 16/4096, 256/4096, VIDEL respectively.)
- [x] Build → run → self-verify loop runs unattended (`make check`, no
      manual steps).

Next: M1 (platform foundation — memory pools, file I/O, timing). The
platform_caps struct is seeded; M1 adds RAM-kind detection + alloc-by-intent.

---

## M1 — Platform foundation (memory, file I/O, timing)
Status: [x] (done 2026-06-23)
Depends on: M0
Goal: correct services every later milestone leans on.

Tasks
- [x] plat_mem_init: grab large pools (ST + Alt where available).
      (platform/platmem.c. NOTE: no Mshrink — mintlib's startup already shrinks
      the TPA; verified Mxalloc(-1,..) reports several MB free at start.)
- [x] plat_alloc_chip (ST-RAM, Mxalloc MX_STRAM) for chip-accessed buffers.
- [x] plat_alloc_fast + address-range check. (NOTE: uses Mxalloc MX_TTRAM
      alt-only with ST-RAM fallback — deterministic, vs mode-3-then-check.
      plat_addr_is_fastram() classifies via the phystop sysvar for the
      acceptance/verification check.)
- [x] Bump-allocator scratch (plat_scratch_mark/alloc/reset).
- [x] has_mxalloc gate (Sversion >= 0x1900) + Malloc fallback. (Mxmask not
      needed: we set no protection bits, only MX_STRAM/MX_TTRAM.)
- [x] GEMDOS file I/O wrappers (Fopen/Fread/Fwrite/Fseek/Fclose/Fcreate) in
      platform/platfile.c, + plat_load_file (whole file -> pool).
- [x] Alt-RAM read policy DECIDED: bounce Alt-RAM dst reads/writes through a
      static ST-RAM buffer (DMA targets chip RAM). Verified via 40 KB
      multi-chunk round-trip on TT.
- [x] Timing: VBL heartbeat (Vsync/_vbclock) + 200Hz-derived ms tick
      (_hz_200) + measured refresh at startup (plattime.c).

Acceptance
- [x] On emulated TT: chip alloc in ST-RAM (0x000222EC), fast alloc in Alt-RAM
      (0x0106AB2C) — verified by address vs phystop. On ST, fast falls back to
      the ST-RAM pool. (`make check` runs both paths headless.)
- [x] 40 KB file written, read back into a pool, FNV checksum matches (both ST
      direct and TT Alt-RAM bounce paths).
- [x] Refresh measured = 60 Hz on ST and TT (EmuTOS NTSC default); VBL counter
      + ms clock advance consistently.

Next: M2 (data formats — LZW + library reader + MAP/CHUNKS + tiles, all
host-side-validated before any ST rendering). `plat_load_file` feeds the
loaders; specs land in /docs/formats first (clean-room).

---

## M2 — Data formats: foundational decode
Status: [x] core DONE 2026-06-23 (LZW, library, MAP/CHUNKS, palette, map tiles;
        compact/object tiles + tile metadata deferred to M2.x / later)
Depends on: M1
Goal: original files → in-memory structures, written from /docs specs.

Tasks
- [x] /docs/formats: LZW container + library reader specs. Both VERIFIED.
- [x] Implement LZW decompressor + library reader (dataformats/u6lzw, u6lib;
      portable C, builds for host AND m68k). Host harness: tools/dftest.
- [x] /docs/formats: MAP/CHUNKS spec (map_chunks.md) + decode (u6map.c) with
      toroidal wrap. KEY FINDING: surface is stored as an 8×8 grid of 16×16-
      chunk SUPERCHUNKS, 12-bit nibble-packed — not flat row-major.
- [x] /docs/formats: tile + palette specs (tiles.md, palette.md) + decoders
      (u6tile.c, u6pal.c). MAPTILES.VGA is LZW (not stored); TILEINDX.VGA is
      stored offsets in 16-byte units; U6PAL = 256 RGB6 triples.
- [x] Update /docs/INDEX.md statuses.

Acceptance
- [x] Host-side dump: **tiles → PNG and world map → image** (tools/mapdump;
      `make mapdump` → build/tilesheet.png + build/worldmap.png). Minimal PNG
      writer in tools/png.c (no zlib dep).
- [x] Diff against known-good: the rendered world map is a RECOGNIZABLE
      Britannia in correct palette colours; LZW decodes real files to declared
      sizes; library auto-detect matches PORTRAIT.A (4/98) & BOOK.DAT (2/128).
      All proven host-side before any ST rendering.
- [x] Every new spec has a filled provenance block + confidence level.

Deferred (open in tiles.md): compact/flat tile encoding, OBJTILES (object
tiles >255), tile metadata (BASETILE/TILEFLAG/ANIMDATA), dungeon-level chunk
ordering. These attach to later rendering/engine work, not the M2 foundation.

---

## M3 — Minimal renderer + first frame on screen
Status: [x] done 2026-06-23
Depends on: M2
Goal: a recognizable U6 map renders on one rich-color target.
NOTE: first "it's alive" moment — comes before hard color work & game logic.

Tasks
- [x] Renderer interface (render/render.h): caps / set_palette / get_backbuffer
      / present; canonical chunky 8-bit backbuffer.
- [x] FIRST backend: 256-color TT-low (320×480), no quantization (render/rdr_tt.c).
- [x] Detection → mode set (EsetShift TT_LOW) + Setscreen base + EsetPalette →
      per-tile blit into backbuffer.
- [x] Off-screen chunky backbuffer → present path (chunky→8-plane c2p).
- [x] Minimal /tools pass: tiles + palette → indexed256 asset pack (tools/mkpack).

Acceptance
- [x] Recognizable U6 map region renders on emulated TT (VGA). Golden-image
      comparison: the ST-composited backbuffer dump (OUT.RAW→PNG) is
      BYTE-IDENTICAL to the host-decoded golden region.
- [x] Screen buffer in ST-RAM (plat_alloc_chip) and displays correctly.
- Findings (see /docs/design/render-tt.md), all verified on emulated TT:
  - TT-low is 8 interleaved bitplanes → chunky→planar c2p at present.
  - TT CLUT uses STRAIGHT linear 4-bit nibbles (verified by on-screen gray ramp;
    the STE rotated form scrambles it).
  - Screen base MUST be 256-byte aligned or the shifter shears (left-edge
    garbage strip). Fixed: bump allocator aligns the ABSOLUTE address.
  - Use real per-machine TOS (TT=3.06) / current EmuTOS 1.4; the ancient EmuTOS
    mangled the 256-colour palette. Confirmed correct on TOS 3.06.

---

## M4 — Usecode SPIKE (DECISION GATE — de-risk the landmine)
Status: [x] GATE PASSED 2026-06-23
Depends on: M2 (decode), can overlap M3
Goal: PROVE the conversation system is solvable before full engine commit.
Throwaway code allowed. This is the project's single biggest risk.

Tasks
- [x] Decode control-byte structure for 1–2 simple NPCs. Done for Dupre (NPC 2)
      + verified on Shamino/Iolo/Lord British/Tholden/Sherry.
- [x] /docs/formats + /docs/rules usecode pair (converse.md, usecode_vm.md) with
      HONEST confidence (readable layer confirmed; expression opcodes deferred).
- [x] Minimal bytecode-VM prototype: dataformats/u6conv + tools/convspike.

Acceptance
- [x] One real NPC conversation runs correctly end to end (`make convspike`:
      greeting + name/job/quest/leave/default/bye, all real game text, PASS).

DECISION GATE
- [x] SOLVABLE → PROCEED; biggest risk retired early. Key insight: each NPC
      entry is a library entry + LZW (both solved in M2), and all spoken text is
      quoted, so the conversation loop runs without an expression evaluator.
      Decision recorded in /docs/design/m4-usecode-gate.md.
- Deferred to M6 (full VM, well-bounded): the usecode expression/flag/branch
  language (conditionals, vars, give/take item & gold, join/leave, set-flag,
  ask string/number), $G token, per-game opcode deltas.

---

## M5 — Core engine loop & world simulation
Status: [x] core done 2026-06-23 (objects/UI panels deferred to M6)
Depends on: M3 (render), M4 (gate passed)
Goal: a game, not a map viewer.

Tasks
- [x] Main loop: input → action/turn → compose → present (recompose only on a
      world change). LOS/lighting deferred to M5.x/M6. (render/gamedemo.c)
  - [x] LIGHTING + VISIBILITY + ROOFS DONE (docs/rules/lighting.md; pack U6WI/J/K).
        (a) day/night ambient gated by INDOOR detection (Nuvie roof_map_00.dat, exact
        ScummVM match; floor-tile fallback) + (b) light globes from emitter tiles
        (tileflags2 bits0-1) + the avatar, (c) C1 LINE-OF-SIGHT flood (boundaryFill: black
        behind walls, see-through windows/open doors), (d) per-pixel bilinear darken via
        16-level palette darkening LUTs at compose, (e) ROOF DRAWING (Nuvie drawRoofs):
        outside a building shows its rooftop, inside shows the interior. Validated vs the
        ScummVM headless harness: throne room (lit centre/dark corners), outdoor day
        (bright), night (dim + light pool), castle rooftop from outside.
- [x] World model + player movement + passability + turn clock (engine/world.c,
      toroidal). Tile interaction = passability; object interaction → M6.
- [x] GameDef/RuleSet C interface-struct scaffolding, U6 defaults
      (engine/ruleset.h, move_turn_cost hook).
- [x] plat_input_poll wired (platform/platinput.c, arrows/WASD/Q). Windowed UI:
      11×11 map window + right panel (title/portrait box/party) + bottom message
      panel + status line (pos/turn), embedded 8×8 font (render/u6font.h). Only
      the map window is recomposed+c2p'd per move (rdr_present_rect) — the perf
      fix (present 583→117 ms, ~5×; see docs/design/performance.md).
- [x] Deterministic RNG seam (engine/rng.c, xorshift32).

Acceptance
- [x] Avatar walks the U6 map; turns advance; blocked by water. (`make run-game`
      interactive; `make m5` headless auto-walk capture.) Objects → M6.
- [x] Headless logic tests pass with fixed RNG (movement, turn cost, water
      block, toroidal wrap, RNG determinism): `make enginetest` → ALL PASS.
- Notes: passability is a colour-heuristic PLACEHOLDER pending TILEFLAG decode
  (see /docs/rules/movement_turns.md); see /docs/design/engine-loop.md.

---

## M6 — Full conversation VM + game systems  ← RELEASE TAG
Status: [x] ACCEPTANCE-COMPLETE (2026-07-12 pass): criteria 1-3 verified —
        convtest ALL PASS, save round-trips (host byte-identical + on-target
        ScummVM-loadable, 12 modeled bytes), completable vertical slice (VSLICE
        4/4 live). Cross-cutting re-checked: both selftest paths (ST 68000 +
        TT 020) ALL PASS, enginetest ALL PASS, perf unregressed (map-window
        present 49.5 ms). Only open item: the literal release tag (repo not
        under git — user's call). Feature history below.
DONE: conversation VM; object placement/render; NPC
        schedules + clock + walking/gait/pathfinding (Stages 1-3b) + facing/walk
        frames + seated NPCs (throne/chairs); look/get/use(doors) + inventory;
        authentic UI (map window / portraits / command bar / roster / cursor);
        actor stats + party from OBJLIST; ScummVM/Nuvie save READ + WRITE
        (byte-identical round-trip, ScummVM loads our writes); tile animation
        (ANIMDATA) + colour cycling (CLUT, fire); LIGHTING + visibility (LOS,
        incl. object sight-blockers) + ROOFS (matches ScummVM); **COMBAT — DONE**
        (CB1–CB4 melee/ranged/gear/AI + coward + aggro-on-sight + fleeing +
        exp/leveling + training gate + party leveling + stat trainers, with
        hostility/gold/gargoyle-peace reworked to the AUTHENTIC per-actor data
        from ScummVM/Nuvie source — alignment=status 0x800 bit 0x20, gold=obj_n-88
        coins, pack U6WR); Falcon VIDEL target (rdr_falcon 320×240 1:1, TOS 4.04
        auto-boot, pixel-perfect shot rig) + hybrid 020/no-FPU builds for game AND
        dev tools (bench/rdemo/selftest).
        CONTAINERS + ITEM STACKS — DONE (pack U6WS: contained objects with parent
        links + 16-bit stack quantities; U)se/L)ook search-spills, G)et takes the
        stack; spec qty/quality byte-order bug found+fixed). KEYS + LOCKS — DONE
        (pack U6WT ships per-object quality = key id; locked doors frame 9/11 +
        chests frame 2 unlock via the matching KEY obj 64; magic locks refused).
        MAGIC — CORE DONE (2026-07-05): spec (docs/rules/magic_skills.md) written
        clean-room (reagent obj_n 65-72 + spellbook 57 confirmed from the user's
        LOOK.LZD via tools/magicprobe; full 79-spell circle/reagent table from
        public U6 references). Engine module engine/magic.c: the spell table +
        mana-cost=circle + the cast gate-check (known/circle/reagents/mana, in
        that order), unit-tested (`make magictest` ALL PASS). engine/objects.c
        gained reagent count/consume. gamedemo wires it: per-actor mana (max =
        Intelligence), a seeded spellbook + reagents, the C)ast command-bar verb,
        and effects for Heal/Great Heal (HP), Light/Great Light (avatar light
        globe), Harm/Magic Arrow/Fireball (ranged damage). Validated on TOS 3.06
        TT (COMBAT.BIN mode 16 → `--cast`, build/magic.png): "Fireball slays acid
        slug! +9exp", Avatar healed 50→70, lit interior; console proves mana
        max=int(15), castable circle=3, all-reagent mask, an 8th-circle spell
        refused, and mana/reagents spent (Light-1,Heal-1,Fireball-3; ginseng
        10→9). SPELL-SELECTION UI DONE: C)ast opens a numbered spell menu (typed
        number or name-prefix; self-spells cast now, offensive arm a direction) —
        on-target build/magic_menu.png lists 6 castable spells with Great Heal
        (circle 4) correctly gated out at Int 15. EFFECTS wired = 14: the seven
        above + Create Food + Unlock Magic + a FIELD-TILE LAYER (Fire/Poison/
        Sleep/Energy Field + Dispel Field): temporary field objects (obj 317/318/
        320/51) that damage/sleep actors on them, energy fields block passage
        (folded into world_is_passable), all age out; validated on the TT (mode
        19, build/magic_field.png: fire burns a slug 30→22, energy blocks 1→0,
        dispel frees 0→1) + a STATUS-EFFECT layer (Poison/Sleep/Paralyze/Curse/
        Charm/Protection/Mass Protect): per-actor condition timers ticked each turn
        (poison DoT, decay), hooked into combat (protected defender halves damage,
        cursed attacker rolls a penalty, charmed actor reads not-hostile) —
        validated on the TT (mode 20, build/magic_status.png: Charm pacifies a slug
        1→0, Poison 30→29, Paralyze 12 turns, Protection dmg 10→5) + a SUMMONING
        layer (Conjure/Insect Swarm/Clone/Summon): spawns creature actors (obj
        343/344/345/368 from LOOK) into free slots with a lifetime; an ALLIED summon
        runs ally_step (mirrors hostile_step — hunts the nearest hostile, attacks
        via party_attack), a HOSTILE summon (Clone of an enemy) reuses the assault
        AI; summons age out + free their slot on death — validated on the TT (mode
        21, build/magic_summon.png: 2 allies, an ally damages a slug 30→4, Clone
        makes hostiles 1→2, all expire) + a TRAVEL layer (Help + Gate Travel):
        teleport_party relocates the Avatar + members + forces a full recompose;
        Help recalls to Lord British (found via the trainer flag), Gate Travel reads
        the 8 moonstone positions from the save's OBJLIST (0x1c1b, [x,y,z] u16 LE)
        and gates to one — validated on the TT (mode 22, build/magic_travel.png:
        recall beside LB, then moongate to moonstone (935,262), party adjacent) +
        a DETECTION/UTILITY layer (Locate/Detect Magic/X-ray/Telekinesis): Locate
        prints the position, Detect Magic counts nearby magic locks, X-ray shorts
        the LOS flood so the map window reveals all cells for a while, Telekinesis
        applies the Use dispatcher to a distant object (ranged door-open) —
        validated on the TT (mode 23, build/magic_util.png: coords (315,367), door
        4→0 from afar, xray 30 turns, 1 aura sensed, mana 12). 29 spell effects
        total. SAVE-BACKED MANA (2026-07-08): MAX mana now uses the AUTHENTIC U6
        formula from a reference — 2×Int (Avatar) / Int/2 (companion) — derived
        from the save's Intelligence (engine/magic.c magic_max_mana, unit-tested;
        fixes the Avatar 15→30, validated on the TT mode 16: mana max 30). CURRENT
        mana + KNOWN-SPELLS are NOT loadable from the sample save: it is the
        GAME-START state where Lord British hands the Avatar the spellbook, so the
        Avatar carries no spellbook(57)/spell(58)/reagent(65-72) in the sample save.
        CURRENT-MANA + KNOWN-SPELL LOADERS — DONE (the formats were read from Nuvie
        source, clean-room, then implemented + validated on TT): current mana = objlist
        **0x13f1** (pack U6WC, 5 B/actor; patched Avatar → ztats MP 12/30); known spells =
        spellbook (obj 57) → spell (obj 58) by `quality` = U6 spell_num, `quality 255` =
        all (SPAM-cheat state), mkworld scan_spellbook → pack U6WR → engine reverse-maps
        via magic_spell_num into g_known (injected test save: 5 spells → known=5; q255 →
        known=79). ENCODING CONFIRMED against a REAL mid-game save (user's "MYST", post-
        gargoyle): the spellbook LB drops ON-MAP at (300,367) holds 10 spells with
        quality={1,3,6,7,9,11,14,18,24,28} (circles 1-2) = quality is the spell_num.
        REAGENTS — DONE (mkworld scan_reagents sums the Avatar's obj 65-72 up the container
        chain → pack U6WR → engine replaces the seeded stock; injected qtys 3,5,7,2,4,6,8,1
        load exactly). In MYST the Avatar carries none (spellbook on the ground, reagents in
        shops), so all three loaders report present=0 there — they light up on pickup.
        PEER / WIZARD EYE / REVEAL — DONE: Peer (circle 3) opens a modal 88x88-tile map
        OVERVIEW (2 px/tile, terrain + actor dots + Avatar cross); Wizard Eye (circle 7)
        is the same overview but PANNABLE (arrows scout) — an overview-based remote view,
        not a full-res flying eye (that camera decouple is deferred). Reveal (circle 5) +
        Invisibility/Mass Invisibility model a per-actor g_ainvis[] timer (compose_map
        skips invisible actors). Validated on TT (MAGIC.BIN): Invisibility on an adjacent
        NPC → ainvis=60 (vanishes) → Reveal → ainvis=0 (reappears); Peer/Eye render.
        REMAINING magic: full-res flying-eye Wizard Eye (camera decouple) — optional polish.
        HUNGER + DRAWBRIDGES — DONE. Hunger: per-actor g_afood[] ebbs (−1/4 turns), warns
        at the Hungry threshold, starves (−1 HP/8 turns) at 0; eating restores (+40/+15);
        a Food row on ztats (Full/Fed/Hungry/Starving). Validated (HUNGER.BIN): food 5→0 →
        −3 HP → eat → 40. Drawbridges: a crank raises/lowers a nearby obj-269 span (frames
        6-8 raised/impassable ↔ 9-11 down/passable, the ±3 pair derived from tileflag
        passability); validated (DBTEST.BIN) frame 6→9.
        REMAINING for the release tag: nothing gameplay-blocking — the acceptance-criteria
        pass + tag. DONE since this note: current-mana + known-spell + reagent loaders;
        Peer/Wizard Eye/Reveal; hunger + drawbridges; USE
        readables + lockpicks + food/drink +
        levers/switches/cranks; string pool (books/signs);
        full-state on-target save writer; NPC Stage 3c worktype activities (wander/
        sleep — rest of 3c: day-of-week, directional tileflags, long-range path,
        poses); UI/VM wiring (VM gold/inventory<->engine, karma UI, $T token).
        Still open: conversation-keyword mouse clicks.
        PERF: smoothness pass + asm c2p (2.3×) + object spatial index +
        uniform-light composite fast path + INCREMENTAL MAP-SCROLL (byte-identical
        diff-tested) — per-move compose 242→34 ms outdoor / 100 ms interior;
        per-move total ~83 ms (~12 fps) outdoor, ~149 ms (~7 fps) interior.
        Incremental PRESENT evaluated + deferred by decision (performance.md).
Depends on: M5
Goal: U6 genuinely playable; first taggable release.
NOTE: the largest milestone — done in parts; this is the running tracker.

Tasks
- [x] Promote spike → full usecode VM (engine/converse_vm.c). DONE.
  - [x] Conversation FLOW layer hardened across ALL NPCs (name / look /
        greeting / keyword sections / bye), tokens ($P,$G) handled, stored +
        compressed entries, no crashes. (dataformats/u6conv.c)
  - [x] Full bytecode VM: tokenise (data-push 0xD2/D3/D4), expression eval/evop
        (compare/arith/logic + FLAG/VAR/INPARTY/RAND/… queries), IF/ELSE/ENDIF
        frames, SETF/CLEARF/ASSIGN, ASK/KEYWORDS/ANSWER keyword loop, JUMP loop,
        BYE; game-state model (talk flags/vars/party/RNG). Runs NPCs 2..40 with
        no desync; Dupre/Iolo/Lord British (incl. Compendium gate) correct.
        `make convvm`. (docs/rules/usecode_vm.md, docs/formats/converse.md)
  - [x] VM runs ON-TARGET: pack U6W6 ships the demo NPC's full decompressed
        script; gamedemo runs the VM on the TT (greeting + "name" keyword
        response in the scroll). Captured (build/game_screen.png).
  - [x] Game-mutation opcodes act on the VM's game-state model (cvm_game_t):
        SETF/CLEARF, GIVE/NEW/DELETE (player inventory + gold), ADDKARMA/SUBKARMA,
        JOIN/LEAVE (party), HEAL/CURE/RESURRECT, PORTRAIT, WORKTYPE, WAIT. Demo:
        talk to Jaana (NPC 62) → "join" → she joins the party; Lord British sets
        his "met" flag. A per-run step guard (CVM_MAXSTEPS) prevents malformed
        JUMP loops from hanging. (`make convvm`, state dump.)
  - [x] Data block + input: DPRINT/DATA read the conversation's data block
        (strings + 2-byte integers; string→`$n` svar), and INPUT/INPUTNUM pause
        for a value stored into the declared var. Merchants work — Gwenneth's bow
        shop lists wares ("1) Bow 2) Crossbow ..."), takes an item number, quotes
        the DATA price ("30 gold"), and ends cleanly. Two VM bugs fixed en route:
        string-ASSIGN was clobbering the numeric `var[]` (infinite shop loops),
        and SETNAME was aliased to ASSIGN. (NPCs 2..98 all terminate.)
  - [x] INDEXOF (name→data-block index): pick a shop item / topic by NAME; the
        ASK/INPUT value is stored into `$Z` (svar 0x23) which INDEXOF searches.
        Text tokens $P/$G/$N/$n/$Y/$Z + #n resolve. Unit-tested (`make cvmtest`).
  - [x] System numeric vars: the script reads game state through fixed VAR
        indices (sex 0x10, karma 0x14, gargish 0x15, party-live 0x17, party-all
        0x18, hp 0x19, quest 0x1A, worktype 0x20) so karma/party/quest-gated
        conversations branch correctly. Unit-tested (`make cvmtest`).
  - [x] Interactive keyword/number typing ON-TARGET: the conversation panel now
        has a live input box ("> typed_"). When the VM pauses (CVM_ASK), gamedemo
        routes the keyboard to text entry (printable chars / Backspace / Enter /
        Esc-to-leave) via plat_input_raw + plat_input_map, feeds the line to
        cvm_input, resumes cvm_run, and shows the NPC's reply — staying open while
        the VM still wants input. Two-way play; restore_panel() repaints the
        papyrus each keystroke so text never ghosts. Captured on TOS 3.06 TT
        (`make m5`): the avatar talks Lord British, types a reply, and the engine
        runs his actual Compendium copy-protection gate live (on-target output ==
        host `make convvm`).
  - [x] Wire cvm_game_t to the broader engine. DONE for the PARTY: g_cg is now
        persistent (flags/karma/gold survive across talks, not re-init per talk);
        sync_party_in() pushes the real roster into the VM's in_party[] before a
        conversation (so INPARTY / party-count checks branch right), and
        sync_party_out() pulls JOIN/LEAVE back into the roster. Demo: talk Jaana
        (NPC 62) → "join" → OP_JOIN sets in_party[62] → she appears as a 5th party
        member on-target (`make m5`).
        DONE for INVENTORY/GOLD: sync_inv_in() loads the engine's object-world
        inventory (g_ow.inv[]) into the VM's player_obj[obj_n&0xff] counts before a
        talk; sync_inv_out() reconciles the delta back (OP_GIVE/NEW/DELETE on
        player_obj[] → inv_add_objn/inv_remove_objn), and gold is the single shared
        pool g_cg.gold (VM + stat trainers), shown on the inventory panel. Validated
        headless (INVSYNC.BIN hook): seed 3 ginseng + 1 food + 50 gold → VM sees
        ginseng=3 → NPC gives 2 ginseng, takes the food, sells for 15 → reconciled
        engine state ginseng=5, food=0 (empty stack compacted), gold=35.
        DONE for KARMA/$T: the avatar's karma (objlist 0x1bf9) is extracted into the
        pack (U6WR) and seeds g_cg.karma; ADDKARMA/SUBKARMA move it; the ztats screen
        shows a "Kar" row (validated on-TT: svm avatar shows Kar 75). The `$T` text
        token resolves to the time-of-day greeting word (morning/afternoon/evening)
        by the game-clock hour — g_cg.hour tracks g_hour before each talk; unit-tested
        across all three bands in `make cvmtest` (ALL PASS).
  - [x] String pool / book & sign text — DONE + on-TT validated (docs/formats/strings.md).
        BOOK.DAT decoded (128-entry 2-byte-LE library of NUL-terminated text, \n line breaks);
        readable object → text = BOOK.DAT[quality-1] (quality 0 → 126). mkworld extract_books ships
        the entries in the never-committed pack (copyrighted text, like intro/questions); the engine
        (render/gamedemo book_view/read_object) shows the paged text over the papyrus when the player
        L)ooks/U)ses a readable world object (SIGN/BOOK/SCROLL/TOMBSTONE/GRAVE, obj_n 151/152/255/266/
        332/333). Validated: L)ook a world sign → "The Book of Spirituality" word-wrapped (READ.BIN
        hook, build/read_screen.png). TODO: reading books/scrolls from INVENTORY; item/ground-tile
        description strings.
- [x] Combat (docs/rules/combat.md) — DONE (CB1–CB4). CB1+CB2 DONE: player melee A)ttack + enemy AI
      (hostile actors path toward & attack the nearest party member). Hit check (dex vs
      dex + d30) + damage (weapon roll − armour) + HP/death + roster sync + messages; math
      in engine/combat.c (unit-tested `make combattest`). Validated on-target: "Killed acid
      slug!" (build/game_combat.png) + a hostile slug wounding Dupre 90->83 (build/game_cb2.png).
      CB3 DONE: weapon/range/armour tables (engine/combat.c, unit-tested) + RANGED weapons
      (bow line-scan + wall blocking; build/game_cb3.png). CB3b DONE: readied weapon/armour
      decoded from the save's actor-inventory chunk (objlist.md §3b; mkworld build_actor_gear),
      shipped per-actor (U6WL) — real gear (Dupre AC 13, Iolo crossbow); armour soaks damage
      (armoured party shrugs off the slug, build/game_cb3b.png). CB4 DONE: per-obj_n alignment
      (pack U6WM, 101/256 types hostile by default) + C)ombat-mode toggle ('c') with party
      auto-fight + wounded-FLEE (HP ≤ 1/4 starting → retreats from the party) + per-kill exp.
      Validated on-target (build/cb4.png): a wounded acid slug flees south while an alignment-
      hostile gargoyle assaults the party. COWARD worktype DONE: per-actor WORKTYPE_U6_COMBAT_
      RETREAT (0x07, objlist +0x11f1, shipped as pack U6WN) flees the party on sight and turns
      to ASSAULT when struck — validated (COMBAT.BIN byte 5: fled y355→357, then after a hit
      closed back in). EXP-BASED LEVELING DONE: maxHP=level×30, doubling thresholds (L2=100…
      L8=6400), per-actor exp from objlist +0xc00 (pack U6WO); a kill raises the avatar's level
      (+30 HP) and the status line shows "Lv N" — validated (avatar level 3/exp 370 → +40 exp →
      level 4/HP 120). TRAINING GATE DONE: kills only accrue exp; Lord British (obj_n 409,
      trainer flag pack U6WP) advances thy level on T)alk when earned (+30 HP) — validated
      (kill doesn't level; talk one-short does nothing; at threshold → level 4). ALIGNMENT
      TUNING DONE: the per-obj_n rule (101/256, see above) wrongly flagged the party, stationary
      guards and idle wanderers; the tuned rule (mkworld wt_aggressive) marks an actor hostile
      only if it is a monster TYPE + in a COMBAT worktype (0x03–0x06/0x08/0x0b/0x9b) + not in
      party, and adjacent_hostile skips party members — 101→3 hostiles on the sample save (only
      the live ASSAULT gargoyles near the throne), validated in-engine (COMBAT.BIN byte 8:
      total=3, near=3, party-flagged=0). AGGRO-ON-SIGHT DONE: hostiles lurk (no omniscient
      pursuit) until they perceive the party — within 5 tiles AND line-of-sight (has_los +
      blocks_sight); a g_alert latch then commits them, and a struck foe alerts instantly.
      Per-actor, not per-type (the same gargoyle obj_n is a WORK citizen in one slot and an
      ASSAULT attacker in another), so peaceful creatures never wake as enemies. Validated
      (COMBAT.BIN byte 9): far d=8 unaware/no-approach, near d=4+LOS alerts+closes in, all 3
      real throne gargoyles alert. LOS OBJECT SIGHT-BLOCKERS DONE: has_los blocks on
      sight-blocking OBJECTS (closed doors) via a g_sightblk bitmap built in build_blocked
      (rebuilds on door open/close), not just base-map walls — validated (COMBAT.BIN byte 10:
      379 sight-blockers in the world; a door moved between hostile+party drops perception
      1->0, restores to 1 when removed). COMBAT REWARD/PROGRESSION FOLLOW-ONS DONE: exp-award
      table (kill_exp); PARTY LEVELING (per-actor exp+level, the slayer earns it, a training
      visit advances every earned member — companion validated 3->4); STAT TRAINERS (train_stat:
      100 gold raises the lowest stat, cap 30). REWORKED FROM SCUMMVM/NUVIE SOURCE: (a) hostility
      is now the AUTHENTIC per-actor U6 alignment — objlist status 0x800 bit 0x20 = EVIL/CHAOTIC
      = enemy (pack U6WM); supersedes the obj_n+worktype heuristic. (b) GARGOYLE PEACE needs no
      flag — it's per-actor alignment (validated byte 13: enemy gargoyle EVIL=hostile, citizen
      gargoyle NEUTRAL=friendly, struck=defends); the g_peace toggle was removed. (c) REAL GOLD
      loaded — party's obj_n-88 coins summed by mkworld (pack U6WR) = 110 on the sample save,
      into the shared g_cg.gold. Deferred: training in LB's Lua-script conversation; dedicated
      stat-trainer NPCs (U6 combat/exp/trainer logic is in Nuvie's Lua data scripts, not the C++).
- [~] Inventory/containers (containers + item stacks DONE — see the object-
      interaction section; keys/locked chests + container UI view remain).
- [~] MAGIC / spellcasting (docs/rules/magic_skills.md) — CORE DONE. Clean-room
      spec (reagents obj_n 65-72 + spellbook 57 confirmed from the user's LOOK.LZD;
      79-spell circle/reagent table from public U6 refs). engine/magic.c = spell
      table + mana-cost(=circle) + cast gate-check (known→circle→reagents→mana),
      unit-tested (`make magictest`). engine/objects.c reagent count/consume.
      gamedemo: per-actor mana (max=Int), seeded spellbook+reagents, C)ast verb,
      29 effects — Heal/Great Heal + Light/Great Light + Harm/Magic Arrow/Fireball
      + Create Food (conjures obj-129 meat) + Unlock Magic (dispels a door frame
      13/15 or chest frame 3 magic lock; `drive.sh --cast-more`, mode 18) + the
      FIELD-TILE LAYER (Fire/Poison/Sleep/Energy Field + Dispel Field; §5a) — a
      g_fields[] list of temporary field objects that damage/sleep actors standing
      on them, tick + age out in step_npcs, render under actors in compose_map, and
      (energy) fold into the build_blocked passability bitmap (`drive.sh
      --cast-field`, mode 19: fire burns a slug 30→22, energy blocks 1→0, Dispel
      frees 0→1) + the STATUS-EFFECT LAYER (Poison/Sleep/Paralyze/Curse/Charm/
      Protection/Mass Protect; §5b) — per-actor condition timers (g_poison/paralyze/
      protect/curse/charm) ticked in status_tick, hooked into the 3 combat paths
      (status_dmg halves a protected defender's damage; status_hit_role penalises a
      cursed attacker; charmed reads not-hostile) + sleep/paralyze skip the NPC turn
      (`drive.sh --cast-status`, mode 20: Charm pacifies 1→0, Poison 30→29, Paralyze
      12t, Protection dmg 10→5) + the SUMMONING LAYER (Conjure/Insect Swarm/Clone/
      Summon; §5c) — summon_creature places creature actors (obj 343/344/345/368)
      into free high slots with g_summon/g_summon_life; allied summons run ally_step
      (mirror of hostile_step, hunting hostiles); hostile summons (Clone of an enemy)
      reuse the assault path; a slain/expired summon frees its slot (`drive.sh
      --cast-summon`, mode 21: 2 allies placed, an ally damages a slug 30→4, Clone
      makes hostiles 1→2, all expire by lifetime) + the TRAVEL LAYER (Help + Gate
      Travel; §5d) — teleport_party() moves the Avatar + re-seeds members beside the
      destination + invalidates the scroll cache (full recompose on a jump); Help
      recalls to Lord British (trainer flag), Gate Travel reads the 8 moonstone
      positions from the save template's OBJLIST at 0x1c1b ([x,y,z] u16 LE) — no
      pack plumbing, the engine already holds SAVE.000 (`drive.sh --cast-travel`,
      mode 22: recall beside LB, moongate to (935,262), party adjacent, mana 8)
      + the DETECTION/UTILITY LAYER (Locate/Detect Magic/X-ray/Telekinesis; §5e) —
      Locate prints w.px/py; Detect Magic scans objects within 10 tiles for magic
      locks; X-ray shorts compute_visibility (all cells visible) for g_xray_turns;
      Telekinesis line-scans + applies use_object to a distant object (`drive.sh
      --cast-util`, mode 23: coords (315,367), door 4→0 from afar, xray 30t, 1 aura,
      mana 12).
      MOVEMENT/INPUT (2026-07-08): held-arrow movement was sluggish — the BIOS
      Bconin input delivered held keys at the slow TOS auto-repeat (~0.5 s initial
      delay). Fixed: plat_input_init() sets a fast Kbrate(6,2) (XBIOS 35: ~120 ms
      initial, ~40 ms repeat), and the main loop DRAINS the key backlog to the
      newest key during gameplay so a held arrow never lags behind a slow compose
      (text entry keeps every keystroke). CLICK-TO-TRAVEL: a plain map click now
      sets a destination and the avatar AUTO-WALKS there one tile/loop via
      npc_pathstep (any key cancels) instead of stepping a single tile — validated
      headlessly (`drive.sh` mode 25: 315→318, 3 steps, arrived).
      PLAYABILITY (2026-07-08): a magic-using play session is now sustainable —
      R)est (the 'r' key + the Rest command-bar icon 8) camps ~6 game-hours,
      re-places NPCs by the new hour (init_npcs), and restores every party member's
      HP + magic points to max; PASSIVE REGEN in step_npcs (+1 mana / 8 turns,
      +1 HP / 24 turns) recovers while walking; and the status line now shows the
      Avatar's "MP nn/mm" (replacing the placeholder Wind). Validated on the TT
      (`drive.sh --rest`, mode 24, build/play.png: mana 30→20 after casts, +2 after
      16 turns of regen, Rest restores 30 mana + 90 HP; the status line reads
      "20:40 Lv3 MP30/30" with "Thou campest…").
      SPELL-SELECTION UI DONE (2026-07-05): C)ast ('c'/'C' key OR the Cast
      command-bar icon) opens a spell MENU — a numbered list of the Avatar's
      known + circle-castable spells with the conversation-style "> _" input box;
      typing a number OR a name-prefix selects (cast_menu_parse), self/party
      spells cast at once, offensive spells arm a direction ("Cast X where?").
      Combat-mode toggle moved off 'c' → the 'b' key + the Combat command-bar
      icon. Validated on the TT (COMBAT.BIN mode 17, `drive.sh --cast-menu` →
      build/magic_menu.png): the menu lists 6 spells (Great Heal, circle 4, is
      correctly gated out at Int 15), console proves parse '3'→Light,
      'fire'→Fireball, and the gated spell is unselectable.
      On-target casting: `drive.sh --cast` (mode 16) → Fireball slays a slug,
      Heal 50→70, lit map (build/magic.png). REMAINING: more spell effects
      beyond the nine wired (fields/status/utility need new systems); save-backed
      spellbook/mana (the objlist mana offset is an OPEN QUESTION — magic max is
      modeled = Intelligence).
- [~] Objects/NPCs from savegame object data (OBJBLK) — placement + render.
  - [x] Object PLACEMENT decode (dataformats/u6obj): OBJBLK/LZOBJBLK, world
        coords, 8329 surface objects, 99.4% region-validated. (objblk.md)
  - [x] Combined 2048-tile set (MAPTILES 0–511 + OBJTILES raw 512–2047) +
        BASETILE obj→tile + transparency. (tiles.md)
  - [x] RENDER: host `make objrender` (furnished building) + on-target game
        draws objects over terrain (world pack v2 + gamedemo).
  - [~] NPC/actor records + schedules (SCHEDULE file) — vs static objects.
        - [x] Decode OBJLIST actor table (256 actors: packed 10/10/4 position +
              obj_n/frame appearance) and SCHEDULE (per-NPC 5-byte entries:
              hour/day/worktype/location). Portable dataformats/u6actor.c.
              (docs/formats/objlist.md, schedule.md)
        - [x] Place every NPC at its scheduled location for a given game hour
              (157/256 on the surface at noon); names cross-referenced from
              CONVERSE. `make npcsched` reports routines (Lord British's throne-
              room day, shopkeepers shop→home) + renders the busiest town with
              NPCs drawn at their positions (build/npcs.png).
        - [x] On-target: pack U6W7 ships per-actor appearance tiles (from OBJLIST)
              + the raw SCHEDULE; gamedemo places NPCs by game hour (noon) via
              u6actor and draws them in the map window. Captured on TOS 3.06 TT —
              a town interior with NPCs at their scheduled spots (tables at noon).
        - [x] Time-of-day clock DRIVES placement: gamedemo derives the game hour
              from world.clock (1 tick = 1 game-minute, START 12:00; status shows
              HH:MM) and re-places every NPC by that hour each turn. Walking spends
              clock time (move = 10 min), so the world re-times and NPCs shift to
              their next scheduled spot as the avatar moves.
        - [x] NPC schedule-WALKING (Stage 1): NPCs no longer teleport on the hour —
              each turn (clock advance) every NPC steps ONE tile toward its scheduled
              (x,y), turning to FACE the move. Frame layout from Nuvie U6Actor (read for
              understanding): tile = basetile + dir*tiles_per_direction (+walk), dir
              N=0 E=1 S=2 W=3; tiles_per_direction derived from the BASETILE span (16→4,
              8→2, else keep saved sprite). Greedy step (dominant axis), terrain-
              passable, avoids avatar/other NPCs, turn-based. render/gamedemo
              init_npcs/npc_dest/step_npcs/npc_tile. Validated: `make m6-npc` (60-turn
              advance vs static baseline shows NPCs moved, no corruption).
        - [x] NPC walking Stage 2: walk-frame GAIT — while moving, NPCs cycle the two
              per-direction walk frames (1,2), standing (frame 0) only when stopped (a
              g_amoved flag separates "stepped this turn" from the gait phase). Validated
              via NPC 17's midnight commute (turns 74/75/76 show it walking with the pose
              changing per step). Corrects an earlier note: the castle scene is at-post
              all day, so its apparent "movement" was fire colour-cycling, not NPCs.
        - [x] NPC walking Stage 3a: REAL PASSABILITY from TILEFLAG (pack U6WG). Terrain
              table now derived from tileflags1 (Impassable|Wall = blocked) instead of the
              water-colour heuristic — blocks mountains/walls/bushes too (tools/
              passability.h compute_passable_tflag; fixes movement_turns.md placeholder).
              Impassable OBJECTS (wall/furniture/closed-door tiles) are tracked in a
              1024x1024 bitmap (render/gamedemo build_blocked, rebuilt on door open/close);
              world_is_passable (engine/world.c) checks terrain AND the object bitmap, so
              the player + NPCs no longer clip through wall/furniture objects. enginetest
              passes on real passability; ~3039 castle/town objects mark as blocking.
        - [x] NPC walking Stage 3b: bounded-BFS PATHFINDING (render/gamedemo npc_pathstep,
              33x33 window) so NPCs route AROUND walls instead of getting stuck; the first
              step of the shortest route to the target (Avatar treated as blocked).
              PARTY-FOLLOW: party members head toward the Avatar via the same pathfinder
              (stop alongside; seeded beside him if the save gave no position) — validated
              (save-engine: a companion follows the Avatar down the throne-room carpet;
              on-target save still 1-byte-localized). NPC schedule-walkers unaffected.
        - [~] NPC walking Stage 3c: WORKTYPE ACTIVITY TABLE done — the active schedule
              entry's worktype drives behaviour via wt_activity() (render/gamedemo).
              Codes derived from the observed distribution (docs/formats/schedule.md §3a):
              SLEEP 0x91 (confirmed: 167 entries, night-dominant), WANDER 0x90 (probable:
              the daytime "at-location" code) → NPC roams within Chebyshev radius 2 of its
              anchor (deterministic per clock+NPC); all other codes = WORK (walk to spot,
              stand at post). Validated headless (WANDER.BIN): Jaana (62, 0x90 @noon) roams
              (232,151)→(232,149)→(233,149) inside radius 2; Lord British (5, 0x93) stays
              fixed on the throne (316,367). REMAINING: day-of-week selection (needs a game-
              day counter — clock wraps at 24h); directional tileflags1 bits 4-7; long-range
              pathfinding (beyond the 16-tile window); per-worktype poses (sit/eat/sleep-in-bed).
  - [~] Object interaction (look/get/use) + inventory.
        - [x] Object identity + names: pack U6W8 ships obj_n per surface object,
              the LOOK.LZD name payload (tile-indexed run-fill table), and a
              precomputed gettable[obj_n] from TILEFLAG weights (docs/formats/
              look.md, tileflag.md). Portable object/inventory store in
              engine/objects.c (objworld_at / can_get / get, stacks by obj_n).
        - [x] Verbs on-target (render/gamedemo): **L**ook names the object/terrain
              in a direction; **G**et removes a gettable object from the world and
              adds it to the inventory; **I** shows the inventory list; verb→
              direction prompts ("Look-(dir)?"). Captured on TOS 3.06 TT
              (`make m5`): auto-demo gets a nearby object and shows it held.
        - [~] **U**se → real effects, dispatched by object number (U6 object-use
              is engine logic, not a bytecode file — docs/rules/usecode_use.md).
              DOORS done: pack U6W10 ships BASETILE so the engine recovers a
              door's frame (tile-base); Use toggles open/close (frame +/-4 => tile
              +/-4), captured on TT (`make m5`: a castle door opens). DONE since:
              containers/stacks (search+spill+get), keys/locks, LOCKPICKS (obj 63
              opens any non-magic lock on doors/chests), READABLES (L/U pages the
              text), and FOOD/DRINK (USE consumes one; obj_n set derived from the
              LOOK names — grapes/bread/meat/cheese/…; hunger effect TODO). Validated
              headless (USEFX.BIN): forced-locked door + lockpick → frame 9→5; a loaf
              (obj 128, qty 2) eaten twice → qty 2→1→removed. LEVERS/SWITCHES/CRANKS
              done: link derived clean-room (MECHDUMP) — levers/switches toggle a field
              by QUALITY (confirmed 1:1: every quality Q≥1 = one lever + one force field,
              often far apart), cranks/pull-chains raise the NEAREST portcullis by
              position. Validated (LEVER.BIN): a quality-1 lever toggles its force field
              1→0→1; a crank raises a portcullis 4 tiles off (frame 1→0). Still TODO:
              drawbridges (multi-tile), the hunger model, multi-level stairs.
        - [x] CONTAINERS + ITEM STACKS (2026-07-02). Format decoded from ScummVM/
              Nuvie source (objblk.md §2): an in-container record's coordinate bits
              are a PARENT LINK ((y&0x3f)<<10|x = record index in the same
              superchunk); stackables (LOOK plural '\' on the base tile) carry a
              16-bit qty16=(quality<<8)|qty. FIXED en route: our objblk spec had
              qty/quality byte order SWAPPED (qty is b6). mkworld ships 12-byte
              object records {x,y,tile,obj_n,qty16,parent} incl. the 369 contained
              objects (pack U6WS); the engine loads them off-map (alive=0). U)se on
              chest/crate/barrel toggles open/close (chest frame>=2 = locked, key
              TODO), opening/L)ooking SEARCHES: "Searching here, you find a, b." +
              direct children spill onto the container's tile (chained bags spill
              intact); G)et takes the WHOLE stack ("You get 30 gold."), inventory
              merges by obj_n (qty display already present). party_gold now resolves
              in-container coins via the parent chain. Validated headlessly
              (COMBAT.BIN byte 14): castle chest opens 1→0, spills 3 items incl. a
              5-gold stack (tile 1→4), Get banks qty 5 as one stack, closes 0→1;
              locked chest refuses; a corpse loots 150 gold via search.
        - [x] KEYS + LOCKED CHESTS/DOORS (2026-07-02). Semantics from Nuvie
              U6UseCode (usecode_use.md): the KEY is obj 64; a lock's key id =
              the object's QUALITY, opened by a key with matching quality. Chest
              frames 2/3 = locked/magic (unlock 2→1); door frames 9/11 locked,
              13/15 magic (unlock = frame−4). Pack U6WT ships per-object quality
              (14-byte records); wobj/invitem carry quality (two different keys
              never merge as a stack; objworld_has does the key lookup); SEARCH
              only spills an OPEN chest/crate/barrel (per-frame usecode table).
              Validated (COMBAT.BIN byte 15) on the castle's real data — locked
              door (296,357) keyid 14 + its key lying at (298,355): use w/o key
              stays locked; Get key; use → unlocked 11→7; use → open 7→3; the
              keyid-0 locked chest replies "No effect" and Look won't spill it.
              Lockpicks (obj 63) + magic unlock spell = TODO (magic milestone).
  - [~] Tile ANIMATION (water/fountains/fields/fire).
        - [x] ANIMDATA decoded: 29 animated tiles, each a BLANK map slot filled from
              a frame source `src..src+frames-1`, cycled on a timer. KEY: this also
              explains "missing chunks" — un-animated, these slots draw as holes.
              Decode + render-frames + ScummVM frame-diff ground truth (tools/
              animprobe, tilestrip, animobserve). (docs/formats/animdata.md)
        - [x] On-target: pack U6WD ships the table; the engine substitutes the
              current frame at every blit site (terrain/object/NPC) and advances a
              ~6fps anim counter. Demonstrated on the TT in open water — two frames
              differ by ~75k px across the ocean and the blank water slots are now
              filled (`make m6-anim` → build/anim_engine.png).
        - [x] OBJECT frame_n is NOT animation: KEY FINDING — frame_n encodes STATE/
              ORIENTATION (fireplace = 4 wall facings, campfire/torch = lit/unlit,
              brazier = colour TYPE), so auto-cycling it is wrong ("rotating fireplace").
              The objanim mechanism (pack U6WE) stays but the set is EMPTY; fire/glow is
              done by colour cycling instead. (docs/rules/object_anim.md)
        - [x] COLOR CYCLING (CLUT rotation): U6 animates fire/glow by rotating
              contiguous u6pal ramps in the palette registers — no recompose/present,
              just re-upload the CLUT (cheap; helps perf). Range derived by OBSERVING
              ScummVM (pixels that change colour, not shape, map to u6pal idx 224–231 =
              red→orange→yellow, rotating). gamedemo cycle_palette() rotates it ~8fps;
              validated on the TT — brazier flames shimmer with frame-anim OFF
              (build/cyc2_diff.png). ANIMMASK.VGA was RULED OUT as the source (it's a
              per-tile map, not a cycle-range table). Cycle ranges identified by a
              usage test (narrow ramp + special tiles = cycle; wide ramp = general
              shading, must NOT cycle): ship 224–231 (fire, CONFIRMED). 232–239
              (magic/energy: force field/moongate/magic items) is a strong candidate
              but UNCONFIRMED — tried to confirm by save-injecting magic objects via
              nuviewrite --set-obj-at, but ScummVM won't render hand-injected surface
              objects (objlist edits like --move-avatar DO work), so it's held back
              until seen at a real moongate/force-field in gameplay. Rejected
              32–47/48–63/116–127/248–255 (hundreds of tiles = general; water is
              FRAME-animated not colour-cycled). Tooling: tools/cycleobserve.sh
              (writes a save via nuviewrite, loads it in ScummVM, captures frames),
              tools/cycleprobe. (docs/formats/palette_cycling.md)
        - [x] SPRITE LAYERING from TILEFLAG (pack U6WF), matching Nuvie's MapWindow
              draw order (read for understanding): compose_map draws objects in a LOWER
              pass (non-toptile tiles) → actors+avatar → TOP pass (toptile tiles), with
              bit4 **Foreground/toptile** decided **PER TILE** (not per object). So a
              throne's back/seat (non-fg) draw behind a seated NPC while its arms/front
              (fg) draw over him — he sits IN it; NO sprite offset is used. bits6-7
              **multi-tile size**: double-width=+tile-1 left, double-height=+tile-1 up,
              2×2=+tile-1/-2/-3 (anchor=bottom-right), exactly as Nuvie drawTile. Size
              encoding + convention derived from the real TILEFLAG/LOOK/bitmaps (tools/
              tflagprobe); size-flagged objects are single entries (no double-draw).
              mkworld ships tileflags2[2048]; render/gamedemo draw_multi()/draw_cell()
              with a per-tile pass gate. (docs/formats/tileflag.md)
        - [x] Seated-NPC "shifted / transparent square" (LB drifting east off his throne)
              — ROOT CAUSE was the CLOCK, now FIXED. We hardcoded START=noon, but the save
              is at 08:00; at noon LB's SCHEDULE sends him to (316,367), so step_npcs walked
              him east off his throne (drawn with his saved seated frame). Fix: read the
              save's real game-time from the OBJLIST clock (objlist+0x1bf3 = minute, +0x1bf4
              = hour), ship it as u16 start_min in the pack header (U6WH), and use it for the
              game clock (render/gamedemo g_start_min) instead of the hardcoded noon. Now
              every NPC loads at its schedule-correct spot — LB sits centred on his throne,
              matching the ScummVM ground-truth render (xvfb + `scummvm -x 0`). Method note:
              we now have a ScummVM headless ground-truth harness for pixel comparisons.
        - [x] Seated SIT-FRAME (2026-06-28): the later directional-animation npc_tile()
              recomputed every humanoid to a standing frame, regressing LB to standing on
              his throne. Fix: npc_sit_tile() (render/gamedemo) — an actor on the LEFT tile
              of a double-width THRONE (obj 327, anchor one tile east) draws the south sit
              frame `base + dir_S(2)*tpd + 3` (Nuvie U6Actor::sit_on_chair → 8+3). Verified:
              actor 5 (Lord British) at (307,348) now draws tile 1771 (sit) not 1768 (stand),
              matching ScummVM (build/lb_sit_cmp.png). CHAIRS DONE: an actor on a CHAIR (obj
              252, single tile) draws `base + chairdir*tpd + 3`, where chairdir = chair tile −
              base(252) (no wobj frame field needed — derived from the chair's tile). Validated
              against the save: all 8 chair-seated NPCs probed have computed sit frame == their
              SAVED frame (build/tavern.png shows three NPCs seated round a table, each facing
              its chair direction). obj_n via Nuvie u6_objects.h (CHAIR 252, THRONE 327).
  - [x] On-target filenames are 8.3 (real-TOS safe w/o MiNT/MagiC): the world pack
        is **U6.PAK** (was U6.WORLD — 5-char ext, invalid 8.3); SAVE.000/SAVED.000/
        CONVERSE.A/PORTRAIT.A all conform.
- [x] FRONT-END: title / main menu / intro / character creation — COMPLETE
      (2026-07-12; full detail in docs/design/intro_sequence.md +
      docs/rules/character_creation.md + memory/front-end-status.md).
      Goal: boot to the authentic U6 front-end, then "Journey Onward" into the
      game (which already loads the original SAVEGAME start state via mkworld
      $(REFDATA)). PHASE 0 DONE — the U6 **SHAPE** format (*.shp) is decoded +
      golden-validated (docs/formats/shapes.md, tools/shapedump, `make shapedump`):
      whole-file LZW → [u32 size][N×u32 offsets] (N=(offset[0]-4)/4) → per-shape
      [rightX,leftX,upperY,lowerY] hdr + plain/RLE pixel blocks (dl>>1 len, dl&1
      flag) → U6PAL. MAINMENU.SHP renders the real menu ("Introduction / Create a
      Character / Transfer a Character / Acknowledgements / Journey Onward") and
      TITLES.SHP the "Ultima VI" logo (build/menu_00.png, build/titles_00.png).
      PHASE 1 DONE (2026-07-08) — on-target TITLE + MAIN MENU. mkworld decodes
      MAINMENU.SHP + TITLES.SHP shape-0 (via dataformats/u6shape.c, shared with
      shapedump) and ships them in the pack (U6WU: [u16 w][u16 h][pixels]×2). The
      engine's run_menu() (gated on a MENU.REQ sentinel so plain headless scene
      captures still render the game) blits the "Ultima VI" title + the menu, draws
      a highlight box on the selected option (arrows move, Enter/Space select),
      and returns on "Journey Onward" → play begins; the leftover Jaana dev-boot is
      removed. `make run-game` stages MENU.REQ; `drive.sh --menu` captures it —
      validated on the TT (build/menu_box.png: title logo + all 5 options in the
      gold gothic font, box on Journey Onward). Gotcha fixed: MENU.REQ must be
      NON-EMPTY (plat_load_file returns NULL for a 0-byte file — `touch` skipped
      the menu). PHASE 2 DONE (2026-07-08) — INTRO CUTSCENE. Scenes are COMPOSITED
      at build time (mkworld compose_shapes): each is a whole 320×200 image built
      from its base + overlay shapes, since the intro draws sprites onto the base
      (the black-box "avatar" was the missing chair/avatar overlay). KEY PALETTE
      FIND: intro art is NOT U6PAL — it uses PALETTES.INT, which is 8 palettes of
      0x240 bytes, each 256×3 6-bit channels BIT-PACKED (not byte-aligned), value
      = DAC level (nuvie ScriptCutscene::set_palette). Living room = palette 1,
      stones/moongate = palette 3 (per nuvie data/scripts/u6/intro.lua). mkworld
      load_intpal() unpacks it; each U6WV scene ships its own palette: [u8 n] +
      n×[u16 w][u16 h][768B pal(6-bit)][w*h idx]. Scenes: INTRO_1[0,13,14]=living
      room (bg+chair/avatar+remote), INTRO_3[0]=standing stones, INTRO_3[5]=crimson
      moongate. Overlay sprites carry a nuvie PLACEMENT offset (avatar @(0,63),
      remote @(143,91)) — without it the avatar composited ~63px too high (user
      caught this); compose_shapes applies per-layer (dx,dy). run_intro() sets each
      scene's palette (rdr_set_palette), blits it opaque, overlays the REAL U6
      narration ("Upon your world, five seasons have passed…") on a dark strip (idx
      254 black / 255 white, forced in the palette), and RESTORES the game palette on
      exit. Also drains stale boot keystrokes at entry (headless auto-boot injects
      keys that were auto-navigating the menu). MENU (Phase 1) selection: every
      option is TINTED BY DISTANCE from the selected one — selection brightest (yellow
      14), ramping outward to orange (36) via MENU_RAMP {14,33,34,35,36} — the real U6
      behaviour (NOT a highlight box, and NOT a time cycle; corrected per user). `drive.sh --intro` (INTRO.REQ) plays it
      headless. Provenance: nuvie intro.lua + ScriptCutscene.cpp READ to understand
      composition/palette; user's reference screenshot as golden. ANIMATION: the living
      room now PANS (nuvie scroll_lounge) — U6WV ships it as a 640-wide panorama
      (field 15@210 + room bg 0 + bg1 1@320) + a transparent foreground (avatar 13,
      remote 14) that scrolls at 2x for parallax; run_intro fades each scene in, holds
      ~6s (or key) with narration, pans the room away, fades out (pack per scene:
      [u8 type][768 pal][u16 w][u16 h][pix] + type-1 [u16 fw][fg]). Validated on TT
      (mid-pan bg→bg1 with the window/cloth-map revealed). NARRATION SCROLL: the real
      singed-parchment banner (BLOCKS.SHP shape 2, 320x48) is shipped shared in U6WV and
      drawn at the bottom over the scene with red ink (idx 0x3e); it stays visible during
      the pan (nuvie keeps the scroll up while scroll_lounge runs). TV ANCHOR: the
      living-room TV plays the nuvie "tv anchor" program — base 57x37 (INTRO_1.SHP 31) @
      (0xe5,0x32) + a talking mouth cycling 3 frames (32,33,34 @ +9,+7) at ~7fps; shipped
      after the scroll, drawn over the panorama and scrolls 1x with the room. Validated on
      TT (anchor on the TV; pan progresses frame-to-frame). CLOCK: the stereo LED clock
      ticks — mkworld blanks the 4 digit cells over the baked "88:88" (to idx 0=black) and
      ships INTRO_1.SHP 2..12 (digits 0-9 + blank, 3x5); the engine draws a live HH:MM
      (from plat_ticks_200hz) over the black cells, scrolling with the room (validated:
      reads 10:46 → later, clean digits). CAPTURE: drive.sh now passes `--fast-boot on`
      (skips the 8MB memtest) so intro scenes land at low, deterministic VBL counts.
      SCROLL PERF (IN PROGRESS): replacing the per-frame CPU redraw + full chunky→planar
      c2p pan with HARDWARE fine-scroll. VERIFIED (2026-07-10, on-target Hatari TT): TT-low
      (8-plane/256-colour) DOES honour the STE horizontal fine-scroll register $FF8265 — a
      stripe pattern shifted left 8px when $FF8265 was set to 8 (rdr_test_hscroll linchpin
      test). HARDWARE-SCROLL OUTCOME (VERIFIED on Hatari TT): $FF8265 fine-scroll WORKS in TT-low, but
      the $FF820F line-width register does NOT — a wider-than-screen buffer garbles at every
      value (640px offset=160 AND 512px words-1=255 both tried; base is 24-bit ST-RAM, ruled
      out). $FF820F is a byte = max 256 words = 512px in 8-plane anyway (640px can't fit).
      Conclusion: TT-low (TT-specific resolution) uses a FIXED line width, so wide-buffer
      hardware horizontal scroll is NOT possible in TT-low. The intro therefore uses the CPU
      scroll (per-frame c2p) on the TT — fine on the 32MHz/030 and in emulation. The
      scroll_* register path (rdr_tt.c, #if 0'd for TT) is correct for line-width-capable
      modes and is kept for the FALCON backend (VIDEL $FF8210 line width + $FF8265) and a
      future STE 4-plane backend (160 words fits the byte). To get hardware scroll ON THE TT
      the intro would need to run in an ST/STE-compatible 4-plane (16-colour) mode. Register
      writes go through Supexec; VBL-commit discipline per user's guide. rdr_test_hscroll()
      is the retained linchpin probe. DECISION (user, 2026-07-10): OPTION A — CPU scroll on
      the TT for now (fine on 32MHz/030); wire the real hardware path into the FALCON backend
      later (VIDEL line-width $FF8210 is a WORD register — no 256-word byte cap like TT's
      $FF820F — so the Falcon CAN do wide-buffer hardware h-scroll). rdr_falcon.c currently
      STUBS scroll_begin (returns false → CPU scroll) to keep the Falcon build (gamef.prg)
      linking; a real VIDEL scroll backend there is the next "take full advantage of Falcon"
      task. FORWARD INTENT: the engine will eventually target ST/STe too (4-plane, where
      line-width fits the byte and the same scroll_* path applies), while taking full
      advantage of TT/Falcon — so scroll_* stays a per-backend abstraction, CPU fallback
      shared. Both game.prg (TT) and gamef.prg (Falcon) build clean with the CPU-scroll path.
      INTRO OPTIMISATION (TT, 2026-07-10): (1) DOUBLE-BUFFERING — rdr_flip_begin/end wrap
      the intro; rdr_present_doubled now c2p's into a HIDDEN ST-RAM buffer (g_screen2) and
      flips the video base at the frame boundary (Setscreen + Vsync), so the viewer never
      sees a half-converted screen — fixes the "redraws in chunks" tearing. Flip mode is
      OFF for the game's normal single-buffer path (gated on g_flip) so partial rect-presents
      are unaffected. (2) ~2x FRAMERATE — removed the redundant explicit Vsync() after
      rdr_present_doubled in the hold/pan loops (present already Vsyncs), so the pan/anim run
      up to 60 fps instead of 30. Validated on TT: all three scenes render correctly across
      buffer swaps. Falcon stubs rdr_flip_begin/end (single-buffer for now; VsetScreen can
      flip — small follow-up). This is the practical ceiling for TT-low (no hardware scroll);
      further smoothness needs the Falcon VIDEL hardware path.
      INTRO FIDELITY (2026-07-10): (a) WINDOW — bg1's window is a transparent hole; the
      twilight field (INTRO_1.SHP shape 15) is baked BEHIND it (panorama 450,20) so it
      shows the outside, not black (nuvie floats it as a slow-parallax layer; we bake it
      static). (b) MOONGATE RISE — new intro scene TYPE 2: the gate (INTRO_3.SHP shape 2,
      74x98 @ x=124) ascends y 0x64->0x5 over the stones (shape 0) with the ground cover
      (shape 4 @ 94,102) drawn OVER it so it emerges from the earth (nuvie stones_sequence).
      Gate/cover shipped as shared U6WV assets after the clock; run_intro animates g_gate_y
      after fade-in, then holds. Bugs fixed: U6WV read consumed an fg layer for ANY non-zero
      type (only type 1/pan has one) -> pack misalignment/garbage; and draw_intro_frame gated
      the pan/TV/clock block on truthy type (drew TV/clock on the gate scene) -> both now
      key on type==1. Palette cycling on the gate (stones_rotate_palette 144..159) still TODO. Moongate/stones pulse is SHAPE-animated (gate rises frame-by-frame; the
      144..159 red ramp isn't a static-shape range), so not a cheap CLUT rotation.
      MATCH-THE-ORIGINAL REBUILD DONE (2026-07-11/12; spec docs/design/intro_sequence.md,
      VERIFIED end-to-end on TT). The intro was reverse-engineered from the ORIGINAL DOS
      run (DOSBox) and rebuilt PAGE-DRIVEN (input-advanced, not auto-timed): 15 narration
      pages across the beats living-room(2) → window/STORM scene (INTRO_2.SHP curtained
      window onto a stormy pine forest, random bolts 11-18 + big strikes 19-23 + white
      window-flash) → FOREST reveal (the window frame slides off over the frameless forest)
      → STONES with an obsidian-PICKUP (hand rises to grab the stone) → MOONGATE rise +
      crimson glow-cycle (pal 144-159) + clutch-the-stone memory pages → AVATAR-INTO-GATE
      CLIMAX (INTRO_3 frames 7-26 walk up + dither-DISSOLVE into the gate, then it sinks).
      LIGHTNING LAYERING FIX (user-reported): storm bolts/strike are composited BEHIND the
      window overlay (storm_compose_ex layers forest→strike→frame), so the mullions occlude
      the flash — matches nuvie. PHASE 4 DONE — all captions are the REAL narration extracted
      from the user's U.EXE at build time (clean-room mkworld extract_intro, structural — no
      hardcoded anchor), rendered with a PROPORTIONAL U6.CH caption renderer so long pages
      fit the parchment; committed paraphrase fallback if U.EXE absent. INTRO PERF (details
      performance.md): dirty-rect HOLD (238→~14 ms/redraw incl. TV) + incremental SCROLL
      (221→24 ms/step) + a TV CHANNEL CYCLER (news/talk-show/drama + static). CHARACTER
      CREATION COMPLETE (Phase 3, spec docs/rules/character_creation.md VERIFIED on TT):
      name/gender/portrait pick + the 8-virtue single-elim tournament (7 A/B questions,
      REAL U.EXE question text, extracted structurally) + Str/Dex/Int stat math, wired into
      the new game (apply_chargen). Gypsy VISUALS (3c): WOODS.SHP forest-path pan lead-in →
      the GYPSY.SHP casting parlour where the 8 virtue vials FILL to graded levels (per-virtue
      liquid colours) AND the central JAR fills with the mixed essence (drawn behind the
      translucent glass) as each question is answered.
      NEW-GAME OVERLAY DONE (2026-07-12; character_creation.md "New-game WIRING", validated on TT
      via `make newgame`). Model matches nuvie create_character: the WORLD state is the game's SHIPPED
      start — the default non-svm mkworld build (`gamedata`→`world`) already bakes the original
      SAVEGAME/OBJLIST into U6.PAK with the Avatar at **Lord British's castle (315,367)**, noon, and
      the four-member party {Avatar,Dupre,Shamino,Iolo} (the "demo SAVE.000 state" only ever applied
      to the `-svm` capture builds; normal play was never the demo save). Character creation OVERLAYS
      six fields onto the Avatar (apply_chargen): Str/Dex/Int, name, gender→VM avatar_sex (convention
      flip), portrait (kept for the status screen), and mana MAX recomputed from the new Int (fixed a
      stale-mana bug). SPELLBOOK HANDOFF: magic_init carries the spellbook (obj 57) + reagents as
      starting inventory (LB's gift). MENU: "Create a Character" now drops straight into the new game.
      Validated (NEWGAME.BIN conout assert + screenshot): 'Milady' at (315,367), 21/13/19, maxmana 38,
      party 4, spellbook 1, status "12:0 Lv3 MP38/38" in LB's throne room.
      STATUS SCREEN DONE (2026-07-12; character_creation.md, validated on TT via `make status`).
      Clicking a party member in the roster opens draw_char_view — their portrait + name + (the
      Avatar's created) gender + Str/Dex/Int + HP/MP + Lev/Exp; the Avatar shows the CREATED montage
      face + "Male/Female", so the chargen gender/portrait now appear in-game (companions decode
      PORTRAIT.A/.B). Any panel click/key returns; STATUS.REQ byte = actor id (headless hook).
      GENDER FULLY REFLECTED IN-GAME (2026-07-12). Investigated the "map-sprite gender swap": U6's
      Avatar map sprite is GENDER-NEUTRAL (obj_n 410 = a single BASETILE 1776; nuvie's U6Actor derives
      it from base_obj_n with no gender term — its actor-type table has one avatar entry), so there is
      no authentic female tile to swap to; fabricating one would break clean-room. The authentic
      gender-driven thing was the $G HONORIFIC — DONE: apply_chargen sets g_avatar_title milord/milady
      by gender (nuvie get_gender_title) and both cvm_init calls pass it (was hardcoded "milord"), so
      NPCs address a female Avatar as "milady" (validated `make newgame`: title=milady). Gender now
      shows in-game via the portrait/status view + the honorific — the authentic surface area.
      GYPSY POUR CHOREOGRAPHY DONE (2026-07-12; character_creation.md 3c, validated on TT). Full
      pixel-faithful port of nuvie create_character gypsy_vial_anim: the authentic DRAIN model
      (vials start FULL; the gypsy lifts each winning vial + tips it over the central jar — vial
      drains, jar fills, one per question). mkworld ships the raw GYPSY.SHP shape table (all 200
      shapes, matching image_load_all); the engine's draw_gypsy_scene + gypsy_pour composite the
      17-frame arm/hand/pour/jar animation from nuvie's exact position tables (arm 1-6, hand 9+n,
      pour_img/x/y, the 96-entry vial_liquid table, hand offsets — index-converted). Validated
      (CHARGEN.REQ 'P' mid-pour, 'q' drain levels, 'D' 3-pour sequence: vial 3→0, jar 0→3).
      JAR BUBBLES DONE (2026-07-12): nuvie image_bubble_effect — the jar essence churns (every 4th
      update each non-transparent liquid pixel re-randomises to a poured virtue's colour), speckling a
      working copy so the pack shape is never mutated, animated during the pour AND while reading each
      question; validated on TT ('P' single-hue, 'q' 4-hue confetti mix).
      DEFERRED POLISH (not blocking; noted in the specs): intro clouds/rain + curtain
      pan-out; the gypsy's setup/closing narration from U.EXE; Falcon real VIDEL hardware scroll (the
      "take full advantage of Falcon" task).
- [~] Authentic UI (use ORIGINAL interface assets, match U6's real layout).
  - [x] Real game font: it is **U6.CH** (2048B raw, 8×8 MSB-left, glyph
        index == ASCII; runic/gargish at 128–255), NOT U6.SET. Shipped in the
        world pack (U6W2→U6W3) + rendered by `ui_text`. (docs/formats/fonts.md)
  - [x] Layout matched to real U6 from reference screenshots, geometry measured
        from the DOS/ST shots (map window 160×160 TL with avatar centred + edges
        clipped; party roster w/ portrait+name+right-HP + slider TR; status strip
        + command-icon row bottom-left; message/conversation scroll bottom-right;
        papyrus border). FIDELITY MODEL: author at U6's native 320×200 (1:1 with
        the original). ST/Falcon display 320×200 natively; the TT (TT-low 320×480)
        presents it **2×-vertically-doubled** (rows drawn twice → 400 rows, centred,
        ~40px black bands) via rdr_present_doubled(). (Letterbox 1:1 was tried first
        but looked small/squished; doubling fills the screen. TODO: resolution
        detection / config to pick scaling per target.) Host preview `make uipreview`
        renders the 320×200 and the 320×480 doubled PNGs. (tools/uipreview.c)
  - [x] Decode ORIGINAL portraits (PORTRAIT.A/.B/.Z): 4-byte-offset libraries
        of LZW 56×64 8-bit images (.A=98 + .B=96 = 194 NPC; .Z=12 avatar).
        `make portdump` contact sheet (32/32 decode correct); a real portrait is
        shown in the conversation panel of the UI preview. NPC#→portrait mapping
        pending the actor model. (docs/formats/portraits.md, tools/portdump.c)
  - [x] Decode + use ORIGINAL border art: PAPER.BMP (U6Bmp = whole-file LZW,
        [u16 w][u16 h][pixels], 320×200, U6PAL). It's the papyrus page with
        frayed edges + the unfurled-scroll message box. `make bmpdump` decodes
        it; the UI preview now composites the map/portrait/text over the real
        background (no procedural frame). (docs/formats/background.md, tools/bmpdump.c)
  - [x] Port the authentic layout on-target (render/gamedemo.c): pack U6W4 ships
        the decoded PAPER.BMP (320×200) + 16 sample portraits; the TT build blits
        the papyrus background, composites the 144×144 map window + a real 56×64
        portrait + U6.CH text, and presents the 320×200 UI 2×-doubled into TT-low
        320×480 (rows drawn twice, centred, black bands). Captured on TOS 3.06 TT
        (`make m5` → build/game_screen.png): papyrus + map + portrait + scroll all
        correct on hardware.
  - [x] Map window = U6's authentic **11×11** MapWindow (176×176), avatar centred.
        Terrain is blitted INSET by 8px so the outer ring stays PAPYRUS; the
        frayed-papyrus border TILES (432-439) are overlaid TRANSPARENT on the ring
        (Nuvie MapWindow::drawBorder) so their fray bites the terrain edge while
        their transparent outer shows papyrus — a torn page, not a frame. The
        border tiles are mostly transparent with only a thin inner fray (verified
        by dumping them), so the terrain MUST run under the fray but NOT to the
        screen edge: rendering terrain full-bleed made it "tiles over the UI block,
        fringe on top" (user-flagged); an earlier 9×9 crop with the fray over bare
        papyrus made the fray invisible = a "bevel box". Validated pixel-for-pixel
        against ScummVM (`make refshot` → refshots/u6_gameplay.png). The date/wind
        status sits on the bottom border (left), like U6 — not the panel. (Finding
        the border tiles earlier also fixed a MASKTYPE.VGA pixel-block decode bug:
        tiles 432-439 + much of 256..511 had decoded as garbage; now u6tile_decode/
        decode_pblck. docs/formats/tiles.md §5a; gamedemo draw_border_tiles)
  - [x] Command-icon bar: the original's 10 icons are TILES 400..409 (Attack,
        Cast, Talk, Look, Get, Drop, Move, Use, Rest, Combat) — already in our
        tileset, no new art. gamedemo renders them as a bottom strip and boxes the
        armed verb. (CommandBar.cpp / U6_mode_tbl.)
  - [~] Mouse support (the ST original HAD it: click command icons, click-to-move
        on the map, click conversation keywords):
        - [x] Stop the system cursor corrupting our screen: we run fullscreen
              non-GEM and own the video buffer, so the still-active AES mouse-
              sprite VBL handler XORs garbage into our planar screen. rdr_tt hides
              it via line-A $A000/$A00A (linea0/lineaa) at init, restores with
              $A009 (linea9) on exit.
        - [x] Authentic cursor: U6MCGA.PTR (LZW → 4-byte shape library); mkworld
              decodes item 0 (the arrow, 11×15 hot(1,1)) to a flat bitmap+hotspot
              and ships it (U6W9). gamedemo composites it into the backbuffer with
              a save-under so it scales through the c2p/doubling. (cursor.md)
        - [x] Read the mouse via line-A (GCURX/GCURY + MOUSE_BT, plat_mouse) and
              un-double screen→logical coords; the cursor follows the pointer.
              (Needs interactive/hardware verification — headless can't move it.)
        - [x] Click routing: command-bar click arms that verb (Talk/Look/Get/Move/
              Use; others say "not yet"); map click applies the armed verb to that
              tile (or steps toward it by default); ROSTER click opens that member's
              CHARACTER STATUS VIEW (draw_char_view: portrait + gender + Str/Dex/Int +
              HP/MP + Lev/Exp; the Avatar shows the chargen face + gender — a panel
              click/key returns). conversation-keyword clicks TODO.
        - [ ] Cursor is vertically 2× (matches the doubled UI); a 1:1 cursor needs
              drawing into the physical (planar) screen post-present. Mouse coord
              range assumes EsetShift updated the line-A bounds — verify on HW.
        - [x] Mouse cursor only reached the TOP HALF — FIXED (confirmed interactively:
              the pointer now reaches the lower half). Cause: the line-A mouse handler
              clamps GCURX/GCURY to V_X_MAX/V_Y_MAX, which still held the BOOT resolution
              (~200 rows) after EsetShift(TT_LOW), so GCURY capped low and read_mouse_logical
              (logical_y=(sy-40)/2) only reached the top. Fix: rdr_init sets V_X_MAX=319,
              V_Y_MAX=479 (TT-low) after linea0/lineaa so the pointer ranges the full
              320x480 screen. (render/rdr_tt.c.)
  - [~] Party roster (top-right, where U6 shows it): each member's sprite icon
        (g_atile) + name + right-aligned HP, plus a day/night SKY BAR (sky-blue by
        day / dark by night, with the sun arcing across by the game clock; U6PAL
        indices sampled from the pack). Names come from the game's own CONVERSE
        data at runtime (resolve_name → the VM's SIDENT), so no game text is baked
        into our source; membership is a demo choice (Avatar + Dupre/Shamino/Iolo
        by actor number). Replaces the idle help; the NPC portrait takes this area
        while talking. Validated against ScummVM (refshots/u6_gameplay.png).
        - [x] Roster icons + avatar/NPC map sprites now CORRECT — root cause was
              ENDIANNESS: the Atari ST SAVEGAME/OBJLIST is big-endian, but we read
              the appearance word little-endian (DOS), so every actor drew the wrong
              tile (bows/swords). Reading it big-endian gives actor 1 = obj_n 410
              (Avatar), 2/3/4 = the human companions; mkworld + u6actor fixed.
              (docs/formats/objlist.md §Endianness)
        - [x] HP + stats from the OBJLIST stat block: HP @0xe00, strength @0x900,
              dex @0xa00, int @0xb00 (1-byte arrays, endianness-agnostic), and the
              in-party flag @0x800 (bit 0x80). The roster shows real HP and the saved
              party. Derived empirically via tools/statprobe (party HP reads
              90/90/90/90 = ScummVM's on-screen numbers). (docs/formats/objlist.md)
  - [x] NPC# → portrait + name + greeting mapping, proven with real data:
        conversation library index == NPC/actor number (name+greeting from
        CONVERSE.A[N]); portrait index == N-1 (PORTRAIT.A/.B). `make npcsheet`
        renders a who's-who grid (NPC 2=Dupre, 3=Shamino, 4=Iolo, 5=Lord British,
        9=Sherry-the-mouse — portraits match names). The conversation view (host
        uipreview + on-target gamedemo, pack U6W5) shows a real NPC: Lord
        British's portrait + name + word-wrapped greeting on the TT.
  - [x] Full per-NPC conversation on-target, loaded on demand at talk time:
        gamedemo loads the originals' CONVERSE.A/.B + PORTRAIT.A/.B at runtime
        (staged onto C: by `make gamedata`; we ship none). `talk(id)` decompresses
        that NPC's conversation (u6lzw) into a scratch buffer, runs it through the
        engine VM (greeting + auto "name"), and decodes its 56×64 portrait — so
        ANY of the ~194 NPCs works, not a baked demo. Walk with the arrows; press
        **T** next to someone to talk (find_adjacent_npc keys off the live game
        hour). Captured on TOS 3.06 TT (`make m5`): the avatar beside Lord British
        at noon auto-talks him → real portrait + name + greeting, clock at 12:00.
- [~] Save/restore — DECISION: adopt ScummVM's **Nuvie save format as OUR format**
      (read + write), so saves interoperate with ScummVM rather than inventing an
      incompatible one. (docs/formats/nuvie_save.md; supersedes our_savefmt.md)
  - [x] READ: decode a ScummVM `.000` (header + super-chunks + objlist) → the 64
        surface super-chunks (= OBJBLK stream) + the objlist. tools/nuviesave.
  - [x] Build the world from a SAVE state: mkworld svm-mode → pack U6WC (live actor
        positions + HP/str/dex/int + party list); the engine renders ScummVM's exact
        throne-room state — map + 4-member roster (HP 90s) match ScummVM (refshots/
        u6_gameplay.png vs build/game_screen.png). `make m5-svm`.
  - [x] WRITE: re-serialize the modeled regions — ALL 64 surface super-chunks (8343
        object records) + the objlist actor arrays — back to little-endian, preserve
        the rest byte-for-byte. Validated: full decode→encode round-trip is
        BYTE-IDENTICAL; a `--move-avatar` edit changes only the avatar's packed
        bytes; and ScummVM LOADS our reconstructed save and renders it correctly.
        tools/nuviewrite, `make save-roundtrip` / `make save-check`
        (build/svm_loaded_moved.png). Save-format order/anomalies documented
        (row-major superchunks; contained/relocated objects) via tools/objprobe.
  - [x] On-target (engine) save writer: the engine (TT, **big-endian 68000**) loads
        the user's `.000` as a TEMPLATE (SAVE.000 staged on C:), patches the modeled
        state (avatar objlist position) with **byte-wise little-endian** encoding —
        correct with no swapping — and writes SAVED.000 via GEMDOS. Validated: the
        engine walks 5 tiles S then saves → exactly 1 byte changes (avatar 307,352 →
        307,357) and ScummVM LOADS the engine's save and renders the moved avatar.
        engine/save.c, `make save-engine` (build/svm_loaded_engine.png).
  - [~] Extend on-target writer to the FULL state. OBJLIST/ACTOR STATE DONE + validated on TT
        (engine/save.c, do_save): every PARTY member's position (not just the avatar) + the GAME
        CLOCK (objlist 0x1bf3 minute / 0x1bf4 hour) + per-actor HP/Str/Dex/Int (0xe00/0x900/0xa00/
        0xb00) + experience (0xc00, LE) + the created-avatar identity (gender 0x1c71 / portrait
        0x1c72 / karma 0x1bf9). Validated: the engine walks 5 tiles then saves → SAVED.000 differs
        from the ScummVM template by exactly 6 bytes (4 party positions + clock minute), all at the
        correct objlist offsets with valid values, and ScummVM LOADS it with no error (`make
        save-engine`; savecheck.sh log clean). FIXED en route: a fast-pool regression — the +25 KB
        BOOK.DAT section tipped the fast TT-RAM pool so the 174 KB SAVE.000 template failed to load;
        the read-mostly template now loads to CHIP RAM (plat_load_file arg 1→0), sparing the fast
        pool (normal non-svm play never stages a template, so it is unaffected).
        OBJECT-WORLD state STARTED + validated on TT. KEY FINDING: U6's object DB is a FLAT FIXED
        record set — open/close/take/spill change a record's frame/status/coords IN PLACE, never
        add/remove records, so superchunk counts + all following offsets are preserved and the writer
        patches in place (NO full save-buffer rebuild needed). DOOR/CONTAINER FRAME + item GET
        persistence DONE (engine/save.c sav_surface_off + sav_patch_obj_frame + sav_patch_obj_taken).
        A world object is marked DIRTY when changed (use_object wrapper; objworld_get). do_save
        re-serialises each dirty object IN PLACE: a door/container's new frame into its row-major
        super-chunk record (byte 5); a TAKEN item's record flipped to in-inventory (status loc
        0x00→0x10) owned by the Avatar (record x→1). CARRIED items live in the separate first chunk,
        BUT Nuvie loads objects by STATUS not physical chunk (nuvie_save.md), so the taken record
        stays in its super-chunk yet loads into inventory — no cross-chunk move / rebuild. Validated:
        the engine opens a closed door (frame 5→1) AND takes an on-map item, then saves → SAVED.000
        differs from the template by exactly 10 bytes — 6 objlist (party pos + clock) + 1 door frame
        + 3 for the taken item (status + x→owner) — all in-place, no offset shift, and ScummVM loads
        it with no error. FIX en route: the 174 KB SAVE.000 template now loads to CHIP RAM (the +25 KB
        BOOK.DAT section had tipped the fast pool; normal non-svm play stages no template).
        SPILL / contained-item GET — DONE (engine/save.c sav_patch_contained_taken). Taking an item
        spilled from a container now persists: the child's SAVE.000 record is IN-CONTAINER (status
        loc 0x08) with parent-link coords (not a map position), so instead of position-matching, the
        writer (1) locates the CONTAINER's on-map record to learn its index P in the super-chunk, then
        (2) finds the in-container child whose parent-link == P and obj_n match and flips it to
        in-inventory (0x08→0x10, owner=Avatar). No pack change / stream-index shipping needed — the
        container link is preserved through spill via wobj_t.origin (parent is cleared on spill so the
        item behaves on-map; origin survives get()). Validated on TT (`make save-engine` + a crate/child
        step): opening a crate (obj_n 192) and taking a spilled child (obj_n 275) adds exactly 2 bytes
        to SAVED.000 (status 0x08→0x10 + x→owner at offset 17827) — total diff 12 bytes (6 objlist +
        1 door + 3 on-map take + 2 contained take) — and ScummVM loads it with no error.
        TODO: the opaque eggs/dungeon chunks.

Acceptance
- [x] Golden-transcript tests pass across many NPCs: `make convtest` parses all
      200 conversations (CONVERSE.A+.B) + golden text for Dupre/Shamino/Iolo/
      Lord British. ALL PASS.
- [x] Save/load round-trips correctly: host-side BYTE-IDENTICAL round-trip of a
      ScummVM save (rebuild all ~8369 surface objects + the objlist → 0 bytes differ,
      `make save-roundtrip`); the ON-TARGET engine writes a ScummVM-loadable save (big-endian
      68000 → little-endian, verified ScummVM loads the engine's SAVED.000). We adopt
      ScummVM's Nuvie format as ours (nuvie_save.md). The on-target writer serializes the FULL
      OBJLIST/ACTOR state (party positions + clock + HP/stats/exp + avatar identity) AND the
      OBJECT-WORLD state IN PLACE — door/container frames + taken items (→ in-inventory by status,
      which Nuvie loads regardless of physical chunk) re-serialised into the super-chunk stream
      (no rebuild). Validated (acceptance pass): from the throne-room start, walk + open a door +
      take an on-map item + take a CONTAINED item, save → SAVED.000 diffs the template by exactly
      12 modeled bytes (6 objlist = 4 party positions + clock; 6 surface = door frame + on-map take
      + contained take), and ScummVM loads it clean. The opaque eggs / dungeon-level chunks aren't
      MODELLED but round-trip VERBATIM (the byte-identical host round-trip covers the whole file).
- [x] A vertical slice of U6 is completable. Every beat of the "U6 opening" slice is
      implemented + validated: front-end (intro→menu→chargen→new-game, front-end memory);
      movement + NPC schedules/worktypes (m6-npc, WANDER); combat (COMBAT.BIN CB1-4);
      conversation (convtest: 200 NPCs + golden transcripts, JOIN demo m5); GET/inventory/
      gold/reagents (m5, MYST1); magic (spellbook+mana+reagent loaders, C)ast menu from a
      real spellbook, MAGIC.BIN); USE (doors/containers/lockpicks/levers/cranks/drawbridges/
      food, USEFX/LEVER/DBTEST); hunger (HUNGER.BIN); save/load (12-byte round-trip, ScummVM
      loads). The core loop COMPOSES live: VSLICE.BIN runs MOVE+GET+CAST+USE in one session →
      4/4 pass, coherent frame (clock advances, MP 29/30 after Light, "You open the oaken
      door."). (No single scripted playthrough-to-a-named-goal harness, but each beat has a
      passing test and they interoperate in one running game.)
- [~] TAG RELEASE: "complete single-game engine, rich-color target." Criteria 1-3 PASS →
      M6 is acceptance-complete. The repo is not under git, so the actual tag (git init +
      tag, or the project's release marker) is the user's call.

---

## M7 — Low-color palette enhancement (HEADLINE FEATURE)
Status: [x] DONE 2026-07-13 — quantiser + the 4-plane ST shifter backend (rdr_st.c) SHIP and render on
        a real 4 MB ST; plus BONUS work beyond the original scope: band-palette rasters (>16 on-screen
        colours), per-portrait conversation palettes, a slim ST pack that boots on a real 4 MB ST, and a
        streaming memory manager (front-end loaded off HD + freed for gameplay). Colour cycling on ST
        (fire, reserved-slot rotation U6WZ) and the STE 4096-colour variant (U6WS 4-bit palettes + STE
        register format) are both DONE + validated, as is the title-screen per-scene palette (blue
        logo, U6WU) and BOOKS-on-ST (BOOK.DAT moved to the resident U6BK core section so in-game
        reading survives). Remaining low-colour follow-ups (per-context dungeon/town palettes, water
        cycling) are deferred, not acceptance-blocking. (docs/design/palette_lowcolor.md)
Depends on: M6 (pipeline + engine must exist)
Goal: base ST/STE looks BETTER than the original lazy port.

Default art path = VGA (256-colour): we use the .VGA tile files (MAPTILES.VGA,
OBJTILES.VGA, TILEINDX.VGA, MASKTYPE.VGA), U6PAL, and the .BMP backgrounds on
TT/Falcon/256-colour cards. The originals also ship .EGA/.CGA/.TGA variants —
those are for the low-colour path below.

Tasks
- [~] /tools quantizer: 256→16 against REAL ST 512 / STE 4096 gamut (NOT EGA).
      CORE DONE (tools/quantize.c, `make quantize`): median-cut → 16, snapped to the 3-bit ST /
      4-bit STE gamut, luma-weighted remap, RMS metric + a naive EGA-16 baseline. Validated on
      the 512 map tiles (211 source colours): our ST-16 RMS 26.1 / STE-16 25.4 vs EGA-16 33.5 —
      ~22% better + visibly faithful where EGA is garish (build/quant_qa.png). PER-REGION DONE:
      4 tile-varied surface regions each get their own STE-16 vs one global — per-region 11%
      lower error, visible in region highlights (build/quant_regions.png). PER-SCENE (front-end)
      DONE: each intro cutscene + gypsy/woods/storm chargen scene ships its OWN ST-16 + LUT
      (build_scene_lc, quantised from that scene's PALETTES.INT palette) in U6FE, swapped in on
      scene entry — validated on a 4 MB ST (`make shot-st-gypsy`: skin/headscarf/vest/vials the
      terrain-16 can't afford). DEFERRED (2026-07-13, user call): per-context palettes for GAMEPLAY
      dungeon + town — the engine is SURFACE-ONLY (world_tile masks to the 1024² surface; save.c
      skips the 5 dungeon chunks), so dungeons aren't navigable and there's no context to detect or
      validate against; U6 towns are surface regions on the same tileset/U6PAL (a "town palette" is
      really an indoor/region CLUT swap, modest win). Both wait until dungeon-map navigation lands.
- [x] Snap candidate colors to hardware-representable values BEFORE measuring
      error. (quantize.c snaps the 16 centroids to the ST/STE gamut, then measures.)
- [x] Reserve palette slots for color-cycling ranges (preserve animation) — DONE for FIRE.
      mkworld holds the fire ramp (224–231) out of the median-cut and pins ST_CYC_SLOTS(=3) reserved
      slots for it (U6WZ section {first_slot, nslots}); the engine rotates them each tick and rdr_st
      re-pushes the registers (+ band 0). Global surface 16 → 13 general colours (RMS 25.2→28.0, still
      < EGA's 33.5). Validated on a 4 MB ST: `make GX=228 GY=167 shot-st-fire` — only the 4 brazier
      flames animate (760 px, 4 clean dots), no tile redraw. TODO: flag over-budget scenes; water once
      confirmed. (docs/design/palette_lowcolor.md, docs/formats/palette_cycling.md)
- [x] Shifter renderer backend (4-plane interleaved) — render/rdr_st.c: 320x200x16 ST-low,
      256->16 LUT + chunky->4-plane c2p, Setscreen mode 0. `make game-st` / `make shot-st`.
      Renders on a plain 8 MHz 68000 ST in the optimised 16 (not garish EGA).
- [x] BONUS: band-palette rasters (U6WX, Timer-B) for >16 on-screen colours; per-portrait
      conversation palettes (U6WY); slim ST pack that fits a real 4 MB ST; streaming memory manager
      (plat_chip_mark/reset + U6FE.PAK) that loads the front-end off HD and frees it for gameplay.
- [ ] LATER: DETECT RAM per Atari system + tier the memory strategy at RUNTIME (not a per-target
      build flag). plat_mem_get_info already reports ST-RAM + Alt/TT-RAM pool sizes + phystop; use
      that to CHOOSE residency: on a RAM-tight box take the ST's conserving path — slim core pak +
      STREAM the front-end (U6FE) off the HD and free it, and consider streaming other transient
      data (conversation libs, BOOK.DAT, per-scene art) on demand; where RAM is ample, keep it all
      resident (faster, no reload). This applies to ANY machine, incl. a TT/Falcon whose Alt-RAM
      pool is small — it should then behave like the ST, not assume the full pack fits. TARGETS:
      do NOT assume 4 MB (ST RAM upgrades aren't just plugging in SIMMs). The original ST port ran
      in 1 MB; aim to fit ~2 MB (room for MIDI + drivers), with 1 MB as a stretch goal. So U6FE
      streaming + the slim pack become a runtime capability keyed on detected free RAM, and the
      `-DST_STREAM` compile split folds into that. (Foundation exists: plat_chip_mark/reset,
      U6FE.PAK, the NOFE marker + parse_frontend — all already runtime-capable.)
- [~] QA: side-by-side PNG (original VGA vs our 16-color) per scene +
      perceptual-error metric. DONE for the tilesheet (4-panel VGA|ST16|STE16|EGA16 + RMS);
      extend to per-scene as the per-region palettes land.

TITLE SCREEN polish on the ST (user-requested 2026-07-13) — DONE:
- The "Ultima VI" title/menu now has its OWN per-scene ST-16 (build_scene_lc on MAINMENU.SHP +
  TITLES.SHP via U6PAL, shipped in the U6WU front-end block, activated as g_menu_lc each menu frame).
  The logo renders in its blue metallic gradient instead of the terrain-16's dithered grey; the gold
  menu-highlight ramp (14 + 33..36) is up-weighted at quantise time to keep its yellow→orange spread.
  Validated on a 4 MB ST (before/after menu capture). Same mechanism as the intro/chargen scenes.

EXTRA FEATURE (later, optional) — native EGA assets for 16-colour targets:
- The user noted U6 ships dedicated 16-colour **.EGA** art (MAPTILES.EGA,
  OBJTILES.EGA, portraits' EGA path, etc.). For the ST/STE 16-colour desktop we
  can OPTIONALLY load the original EGA assets directly instead of quantising the
  VGA art down — a cleaner low-colour look and a build-time choice. Decode the
  .EGA tile/portrait variants (likely the same containers with 4-bit pixels) and
  add an asset-variant selector to the pipeline. Slotted here as an enhancement;
  the quantiser path above is the baseline.

Acceptance
- [x] U6 runs on base ST with VISIBLY improved palette vs original port. DONE — renders on a real
      4 MB ST in the optimised 16 (build/gamest_screen.png; TT-256 vs ST-16 comparison confirms it).
- [ ] Color cycling still animates after 256→16 reduction. TODO — water/fire CLUT rotation isn't
      wired on the ST yet (rdr_set_palette(256) is a no-op on 16-colour hw); needs reserved slots.
- [x] STE variant uses the 4096 gamut for richer selection — DONE. mkworld ships a PARALLEL 4-bit
      palette set (U6WS: game + 2 bands + conv, sharing the 3-bit LUTs); rdr_st's st_reg() packs the
      STE rotated-LSB register format (nibble = (v>>1)|((v&1)<<3)) when the engine detects VIDEO_STE
      (cookie _VDO) + U6WS and calls rdr_set_ste_mode. The 3-bit ST path is untouched. Validated on an
      emulated 4 MB STE (TOS 2.06): make shot-ste-conv. (docs/design/palette_lowcolor.md)

---

## M8 — Truecolor + graphics-card path (goals #2 & #3)  ← RELEASE TAG
Status: [~] IN PROGRESS 2026-07-16 — Falcon 16-bit truecolor present DONE + validated in Hatari.
Depends on: M7 (or at least M6 + M3 backend work)
Goal: complete the hardware matrix from ONE detection routine.

Tasks
- [x] Falcon 16-bit truecolor present (palette-expand). DONE. rdr_falcon gains a runtime-selectable
      BPS16 path (VGA 0x114 / TV 0x024) beside the BPS8 indexed default: 2 bytes/pixel screen, an
      RGB565 expand LUT built in rdr_set_palette (U6 6-bit green maps 1:1, R/B drop a bit), and a
      per-pixel 8->16 expand present (present/rect/doubled/scroll). The backbuffer stays 8-bit indexed
      so the engine is unchanged; requested via rdr_prefer_truecolor (render.h, no-op on ST/TT),
      selected by VIDEO.CFG='T'. Validated (`make m5-svm-falcon-tc`): the throne-room scene renders
      correctly in 16-bit and is pixel-equivalent to the 256-colour present across the whole game area
      (diff = only the imperceptible 6->5-bit R/B reduction + the VIDEL border outside the active
      display + animated map-edge cursors). Completes goal #2 and de-risks the direct-colour present
      the vcard backend will reuse.
- [ ] vcard backend: EdDI/vq_extnd detection → off-screen-bitmap +
      vro_cpyfm present.
- [ ] Verify 68000-ST + 256-color video card path.

Acceptance
- [ ] U6 runs across FULL matrix: base ST (16c improved), STE (4096),
      TT (256), Falcon (256 + truecolor), 68000-ST + emulated 256c card.
- [ ] All targets driven by the SAME detection routine (goals #2 & #3
      confirmed collapsed into one code path).
- [ ] TAG RELEASE: "full hardware matrix on U6."

---

## M9 — The other two games (goal #1)
Status: [ ]
Depends on: M6 (engine), M8 (matrix); benefits from all prior work
Goal: prove the engine is genuinely multi-game (mostly data + small deltas).

Tasks
- [ ] GameDef/RuleSet for Savage Empire: asset manifest; level-mapping policy
      (uses 4 of 6 levels; level 6 = gargoyle-world variant); rule deltas
      (tribal skills).
- [ ] GameDef/RuleSet for Martian Dreams: asset manifest; toroidal world
      policy; era mechanics deltas.
- [ ] Decode any per-game format/usecode deltas; update /docs.

Acceptance
- [ ] All three games boot from the SAME engine binary + per-game data.
- [ ] Vertical slice of each game runs.
- [ ] Per-game rule deltas live in RuleSet overrides (NOT engine forks).

---

## M10 — Audio: music (HEADLINE FEATURE — "fixes" the silent ST port)
Status: [~] IN PROGRESS 2026-07-16 — format DECODED + validated; THREE backends DONE (MIDI-OUT,
        OPL2→DMA, YM2149 PSG) + runtime selection + Timer-A tempo + exit cleanup + per-area hooks +
        SEAMLESS DMA loops + CORRECT song selection (group model from Nuvie: random wandering pool
        {BRIT,FOREST,STONES,ULTIMA} + combat/dungeon/boat/gargoyle groups — docs/rules/music_areas.md).
        Substantively COMPLETE. Deferred cleanliness (not gaps): pack MIDI.DAT/.M into U6.PAK (they load
        loose today — a tagged-section add on both host packer + engine reader + slim-pack retest, best
        done with the broader release-packaging work); wire boat/dungeon/gargoyle triggers when those
        game systems exist. Sound EFFECTS are a separate track (no milestone yet).
        (1) MIDI-OUT (real MT-32, validated in Hatari); (2) OPL2→DMA on STE/TT/Falcon — real-time
        FM synth proved INFEASIBLE on-target (a 32 MHz TT does 0.1× real-time; `make sbench`), so the
        host pre-renders each .M to 8-bit PCM (tools/m2pcm, our validated FM synth engine/oplsynth at
        4× oversampling = the clean AdLib sound) and platform/platdma STREAMS it from disk to the DMA
        sound chip at ~0 % CPU — validated end-to-end in Hatari (captured audio is real music advancing
        through the file). The MIDI path's tempo is now driven by a steady MFP Timer-A 60 Hz interrupt
        (was coupled to the ~6-8 fps main loop → choppy/slow), with an ISR-safe queue→ACIA MIDI path;
        validated in Hatari (smooth musical timing, no crash). Exit cleanup (uninstall the Timer-A ISR
        before Pterm + all-notes-off) fixes a hung MT-32 note + a latent post-exit crash. A runtime
        music manager picks the backend 3-way (MIDI.CFG '1'=MIDI, '2'=DMA, '3'=PSG, else auto: DMA if
        present else PSG); all three validated in Hatari. THREE backends now DONE: MIDI OUT, OPL2->DMA,
        and YM2149 PSG (plain ST: U6's 9 OPL voices folded to 3 squares; validated on --machine st as a
        real melody). NOTE: ST I/O is supervisor-only — main-context register access goes via Supexec
        (fixed the PSG + a latent MIDI-flush bus-error). Per-area hooks now drive the music from game
        context (music_update_area each turn: auto-starts the overworld theme, swaps to the battle
        theme in combat; combat/dungeon/overworld table, docs/rules/music_areas.md) — validated on TT
        (auto-started ULTIMA, MIDI map matched). SEAMLESS DMA LOOPS now done: each .M is an intro then a
        body that repeats from a loop point (opcode 0xE, or the top); the synth exposes where playback
        returns there (engine/oplsynth opl_wrap_* / opl_loop_point_sample), and m2pcm renders intro +
        TWO passes of the body and stamps loop_start = start of the 2nd (warmed) body into the U6PC
        header, which platdma already loops on. Songs whose two bodies exceed the render cap (BOOTUP,
        STONES) trim to exactly one aligned body. Verified: the loop boundary matches the engine's own
        live sequencer (engine/music.c control flow is byte-identical, cross-checked vs mdump), and the
        seam discontinuity measured on the real PCM (which models platdma's exact play-[0,n)-then-loop-
        [loop_start,n) frame sequence) is 1-13/255 — the size of one ordinary sample step, i.e. inaudible.
        On-target smoke test: the new non-zero-loop_start PCM loads + streams real audio on the TT
        (backend=2), no format regression. Remaining polish: verify the overworld/town song assignment
        against the original.
Depends on: M1 (platform timing/IRQ) + M6 (game loop to drive it). Independent of
the M7/M8 video work — schedulable alongside them.
Goal: in-game music the original ST port strangely shipped WITHOUT, by playing
the DOS version's score on Atari hardware. A marketed enhancement in the same
spirit as M7's palette fix.

Background: U6's DOS release includes a real soundtrack; the ST port omitted it.
We pull the music data from the user's DOS copy (we ship none) and play it. The
DOS data carries one small sequence per area/scene — BRIT.M (towns), FOREST.M,
DUNGEON.M, ENGAGE.M (combat), STONES.M / ULTIMA.M (theme), INTRO.M, END.M,
BOOTUP.M, CREATE.M, GARGOYLE.M, HORNPIPE.M — plus a per-device driver:
U6ROLAND.DRV (Roland MT-32), U6ADLIB.DRV (AdLib/OPL2), U6TANDY/U6CMS/U6COVOX/
U6INNOVA/U6TMUS. Origin/Miles AIL.

**FORMAT FINDING (docs/formats/music.md, CONFIRMED):** the `.M` files are U6 LZW
containers wrapping an **AdLib / OPL2 (YM3812) FM command stream** — NOT MIDI/XMIDI.
The default DOS sound is OPL2; the DOS build ALSO supports Roland MT-32 (the same
note stream re-timbred by U6ROLAND.DRV). So the original "XMIDI → MIDI-OUT" premise
is superseded. The stream is note-level (note-on/off + packed frequency + instrument
index + 60 Hz delays), so a note sequence is recoverable for MIDI and PSG too.

**Approach (user decision 2026-07-13): several backends + RUNTIME detection**, with a
user option to enable MIDI. Behind a small `plat_audio_*`/`plat_music_*` seam:
  - **MIDI enabled** → extract notes, map OPL instrument → MT-32/GM program
    (U6ROLAND.DRV timbre map or a GM approx), send note-on/off + program-change out
    the Atari **MIDI OUT** port (Bconout(3,..) / Midiws). Any Atari; needs an external
    MT-32/GM synth. (Reference sound: the DOS MT-32 recording the user linked.)
  - **MIDI off, STE / TT / Falcon** → **OPL2 → DMA sound**: a software YM3812 FM
    synth renders PCM to the 8-bit DMA sound path. The authentic AdLib soundtrack.
  - **MIDI off, plain ST** → **OPL → YM2149 PSG**: downmix to the 3-voice PSG (no DMA
    sound on a base ST). Lower fidelity but universal.

Tasks
- [x] Decode the DOS music data → docs/formats/music.md. DONE + validated: `.M` = U6
      LZW (u6lzw) → OPL2 command stream; opcode+operand table confirmed by a byte-accurate
      host disassembler (`tools/mdump`) reaching a CLEAN END on all 12 `.M` files; 60 Hz
      timing. Remaining detail: the 24-entry F-number pitch table + 11-byte operator layout
      (pinned when we synthesize).
- [x] Host-side playback validation: DONE. `tools/m2mid` (make m2mid) extracts the note stream
      to a Standard MIDI File — note counts match the disassembler, renders a correct melody via
      a Roland SC-55 soundfont. Cross-validated the whole format against ScummVM/Nuvie's
      `midiparser_m`/`mididrv_m_*` (opcode table + 60 Hz + the FPU-free NOTE_MAP all agree).
- [x] The MT-32 instrument/timbre map — DONE. It's the authentic Origin **MIDI.DAT** (plain text,
      per song `(midi_channel/program,...)` indexed by instrument), matching ScummVM's fallback.
      (U6ROLAND.DRV not needed — MIDI.DAT + the MT-32's built-in patches, no SysEx.)
- [x] Portable sequencer in /engine — DONE. `engine/music.c`: pure integer (no FPU/TOS) OPL2
      command-stream sequencer, 60 Hz clock, follows the .M loop/branch structure, drives a
      pluggable `music_backend_t`. Host unit test `make musictest`.
- [x] Platform audio backends behind the `music_backend_t` seam (all three DONE):
      - [x] **MIDI OUT** (all Ataris, user-enabled): `platform/platmidi.c` (XBIOS Midiws) + a MIDI
            backend in gamedemo. DONE + VALIDATED end-to-end: the engine emits the correct
            program-changes (ULTIMA's MIDI.DAT map) + notes/velocity out the emulated ST MIDI port,
            captured via Hatari PortMidi → an ALSA loopback → aseqdump. `make music-tt` plays it on
            a real MT-32 through your hatari.cfg MIDI port.
      - [x] **OPL2 → DMA sound** (STE/TT/Falcon): DONE. Real-time FM proved infeasible on 68000, so
            the host FM synth (engine/oplsynth, 4× OS) pre-renders each .M to 8-bit PCM (tools/m2pcm,
            U6PC) and platform/platdma STREAMS it via the $FFFF89xx DMA-sound registers (base/end/mode,
            programmed via Supexec; Falcon routes with XBIOS Devconnect 139). SEAMLESS loops: m2pcm
            renders intro + 2 body passes and stamps loop_start (start of the warmed 2nd body) into the
            header; platdma's ring wraps the file there. Seam measured at 1-13/255 (inaudible).
      - [x] **OPL → YM2149 PSG** (plain ST): DONE. 9 OPL voices folded to 3 squares; register writes
            ($FFFF8800/02) from the Timer-A ISR (init/silence via Supexec). Validated on --machine st.
- [ ] Extract MIDI.DAT/.M into the asset pack (like BOOK.DAT) instead of runtime file loads.
- [x] Runtime detection picks DMA vs PSG from the machine (M1 caps); MIDI.CFG enables the MIDI-out
      path instead (3-way: '1'=MIDI '2'=DMA '3'=PSG, else auto). Per-area hooks start/stop/loop by
      game context (music_update_area; combat→ENGAGE, docs/rules/music_areas.md). DONE.
- [x] Move the 60 Hz music tick to a timer interrupt (MFP Timer-A) so it keeps playing during blocking
      waits; the main-loop pump only advances the DMA refill. DONE (plat_timer_start, -m68000 ISR).

Acceptance
- [ ] U6 on Atari plays its (DOS) score — a feature the original ST port lacked.
- [ ] STE/TT/Falcon play the authentic OPL2 music via DMA sound; a plain ST plays the PSG
      downmix; the MIDI-out option drives an external MT-32/GM synth. Correct branch chosen
      at runtime from the detected machine + the MIDI setting.
- [ ] Sequencer timing stays correct under load (60 Hz clock, not frame rate).
- [ ] Same path proven reusable for Savage Empire / Martian Dreams music.

---

## Risk register (review each milestone)
| Risk | Mitigation | Milestone |
|------|------------|-----------|
| Usecode control bytes intractable | Spike + decision gate early | M4 |
| ST-RAM/Alt-RAM misallocation on TT | Alloc-by-intent + addr check | M1 |
| Mode-set breaks AES / no restore | Save+restore mode around game | M3/M8 |
| Palette worse-than-original risk | Per-scene quant + QA images | M7 |
| Per-game divergence forces forks | Data-driven GameDef/RuleSet | M9 |
| Decode wrong but blamed on render | Host-side golden diffs first | M2 |
| AIL/.M music format harder than MIDI | Host event-dump + spec before target sequencer; MT-32/MIDI-OUT first | M10 |

## Release tags
- M6: complete single-game (U6) engine on a rich-color target.
- M8: full hardware matrix on U6 — good point to share with the Ultima
      community for hardware testing.
- M9: all three games — project goals #1–3 met.

