# Character Creation (U6 "gypsy" virtue Q&A)

> Provenance
> - Derived from : nuvie data/scripts/u6/intro.lua (create_character, gypsy_ask_questions,
>                  gypsy_ab_select) READ to understand the flow; U.EXE inspected for the
>                  question-string location (offset only, text NOT reproduced).
> - References   : nuvie intro.lua + ScriptCutscene.cpp (fetched to build/); U6 virtue lore.
> - Tools        : build/introcomp / shapedump (assets), grep/python on U.EXE (offsets).
> - Validated by : NOT YET (spec ahead of implementation); will validate the tournament
>                  stat output against nuvie's tables + on-target Q&A capture.
> - Confidence   : probable (logic/tables transcribed from nuvie; question SOURCE confirmed).
> - Last updated : 2026-07-11

## System
The new-game character is built by the gypsy's virtue questionnaire. U6 has NO class — the
result is: **name, gender, portrait, and Str/Dex/Int**. (Per-game: U6 only.)

## The 8 virtues (index 0..7, the U6 order)
`0 Honesty, 1 Compassion, 2 Valor, 3 Justice, 4 Sacrifice, 5 Honor, 6 Spirituality, 7 Humility`

## Flow (nuvie create_character)
1. **Name** entry (≤13 chars).
2. **Gender** M/F.
3. **Virtue tournament** — single-elimination bracket of the 8 virtues:
   - `g_question_tbl = {0..7}`, shuffled.
   - `gypsy_ask_questions(4)` then `(2)` then `(1)` = **7 questions total** (8→4→2→1).
   - Each round-question pits `tbl[i*2]` vs `tbl[i*2+1]`; the winning virtue is written back
     to `tbl[i]` (compacting winners to the front for the next round). Final `tbl[0]` =
     dominant virtue (used by nuvie only for flavour; U6 stores just the stats).
4. **Portrait** pick — MONTAGE.SHP, 6 portraits per gender (`gender*6 + portrait_num`).
5. Output → new-game data: name, gender, portrait, str, dex, int.

## Question data
- **28 questions** = every unordered virtue pair C(8,2). Question id range `0x69..0x84`;
  text index = `id - 0x68` (1..28).
- **Pairing matrix** `Q[a*8 + b]` → question id (diagonal = -1). Symmetric.
- **A/B → virtue** (index by question text-index 1..28):
  - A-virtue: `{0,0,0,0,0,0,0,1, 1,1,1,1,1,2,2,2, 2,2,3,3,3,3,4,4, 4,5,5,6}`
  - B-virtue: `{1,2,3,4,5,6,7,2, 3,4,5,6,7,3,4,5, 6,7,4,5,6,7,5,6, 7,6,7,7}`
  - Choosing **A** ⇒ the A-virtue wins that question; **B** ⇒ the B-virtue.
- **Question TEXTS live in U.EXE** (~byte 98000+, null-terminated C strings; NOT in a
  separate data file, NOT shipped in this repo). Extract at build time (mkworld reads the
  user's U.EXE) into the asset pack — a build artifact, never committed. Do NOT hardcode.

## Stat adjustment (Str/Dex/Int start at 0x0F=15; each of the 7 winners adds)
Indexed by winning **virtue** 0..7:
| virtue | 0 Hon | 1 Comp | 2 Val | 3 Jus | 4 Sac | 5 Hon | 6 Spir | 7 Hum |
|--------|------|------|------|------|------|------|------|------|
| +Str   | 0 | 0 | 2 | 0 | 1 | 1 | 1 | 0 |
| +Dex   | 0 | 2 | 0 | 1 | 1 | 0 | 1 | 0 |
| +Int   | 2 | 0 | 0 | 1 | 0 | 1 | 1 | 0 |

Final stats are `15 + Σ(adjustment for each of the 7 winning virtues)`.

## Assets
- **GYPSY.SHP** — the gypsy, her table, the vials + colored liquid ("pour"), speech bubbles.
- **MONTAGE.SHP** — 12 portraits (6 female @ 0..5, 6 male @ 6..11).
- **WOODS.SHP** — the woods/backdrop. Palettes: PALETTES.INT **4** and **5** (bit-packed).
- **BLOCKS.SHP shape 3** — the tall (4-line) parchment scroll for question text.

## Test hooks
- Host/headless: run the tournament with a fixed shuffle + scripted A/B answers, assert the
  Str/Dex/Int match nuvie's tables (deterministic).
- On-target: capture the gypsy scene + a question with A/B highlight; capture the final stats.

## Status
DONE (validated on TT) — the full flow works incl. the gypsy casting visuals (3c): the gypsy's arm/hand
POUR choreography (nuvie's drain model — the 8 virtue vials start FULL and drain into the central jar,
one per answered question) is a full pixel-faithful port, and the central jar BUBBLES (churning mix of
the poured virtue colours). Remaining = polish only (gypsy setup/closing narration from U.EXE).
- (3a) tournament logic + Str/Dex/Int output — DONE (run_chargen; single-elim 8→4→2→1).
- (3b) UI: name entry + gender + PORTRAIT pick (MONTAGE.SHP 0..11, shipped in the pack) +
  question display loading the REAL texts from U.EXE — DONE. Questions are extracted
  STRUCTURALLY at build time (mkworld extract_questions: the unique run of 28 consecutive
  "%c"-terminated pages containing " A) "/" B) "; located by structure, no hardcoded anchor)
  and shipped in the never-committed pack. Engine re-wraps them to 320px (cc_ask +
  draw_wrapped). Pair→question via cc_question_index (A = lower-indexed virtue, B = higher).
- New-game WIRING — DONE (apply_chargen, validated on TT via `make newgame`). Model matches
  nuvie create_character: the WORLD state is the game's SHIPPED start and character creation
  OVERLAYS just the six fields {name, gender, portrait, Str, Dex, Int} onto the Avatar (actor 1).
  - THE SHIPPED START ALREADY IS THE CANONICAL NEW GAME. The default (non-svm) mkworld build
    (`make gamedata`→`world`) bakes the original SAVEGAME/OBJLIST into U6.PAK with the Avatar at
    **Lord British's castle (315,367)**, noon, and the four-member party {Avatar, Dupre, Shamino,
    Iolo}. (The "demo SAVE.000 state" only ever applied to the `-svm` capture/test builds, e.g.
    `make m5-svm` — normal play was never the demo save.)
  - apply_chargen overlays: Str/Dex/Int → Avatar; name → roster + the conversation VM's player
    %token (g_avatar_name); **mana MAX recomputed** from the new Int (2×Int, magic_skills.md §3 —
    the load-time value from the shipped OBJLIST Int would otherwise be stale); **gender → the VM's
    avatar_sex** (system var 0x10) with the convention flip (chargen 1=male/0=female → VM 0=male/
    1=female). gender + portrait stay on g_cc_* for the status screen + save writer (objlist 0x1c72
    = avatar portrait). HP/level stay at the shipped-OBJLIST canonical start (U6 chargen does not
    set HP; it is not derived from Str/Dex/Int).
  - SPELLBOOK HANDOFF — DONE (magic_init): the Avatar carries the spellbook (obj_n 57) + 10 of each
    reagent as starting inventory, modeling Lord British handing over the spellbook at game start
    (the fresh save ships none — magic_skills.md §8).
  - MENU FLOW: "Create a Character" (run_chargen) now drops STRAIGHT into the new game on completion
    (run_menu returns Journey Onward when g_cc_done), the authentic U6 flow — not back to the menu.
  - VALIDATED (`make newgame`, CHARGEN.REQ='N' sample 'Milady' + NEWGAME.BIN conout assert): avatar
    'Milady' at (315,367), Str/Dex/Int 21/13/19, maxmana 38 (=2×19), sex 1 (female), party 4,
    spellbook held 1; the game screen renders LB's castle throne room with the roster
    {Milady,Dupre,Shamino,Iolo} and status "12:0 Lv3 MP38/38".
  - STATUS SCREEN — DONE (draw_char_view, validated on TT via `make status`). Clicking a party
    member in the right-panel roster opens their CHARACTER STATUS VIEW: portrait + name + (the
    Avatar's created) gender + Str/Dex/Int + HP/MP + Lev/Exp. The Avatar shows the CREATED montage
    face (g_cc_port[gender*6+portrait]) and "Male"/"Female" from g_cc_gender — so the chargen
    gender/portrait finally appear in-game; companions decode PORTRAIT.A/.B[actor-1]. Any panel
    click or key returns to the roster. Headless hook: STATUS.REQ (byte = actor id) opens it at
    boot. Validated (`make status`, CHARGEN.REQ='N'): the 'Milady' red-haired female portrait +
    "Female" + Str 21/Dex 13/Int 19 + HP 90/90 + MP 38/38 + Lev 3.
  - MAP-SPRITE GENDER SWAP — NOT A THING IN U6 (investigated, clean-room, 2026-07-12). The Avatar's
    on-map sprite is GENDER-NEUTRAL: obj_n 410 has a single BASETILE (1776) with no female variant,
    and nuvie's U6Actor derives the avatar tile from base_obj_n alone (its u6ActorTypes table has one
    OBJ_U6_AVATAR entry, no gender term; player.cpp uses gender only for the portrait + the honorific).
    So there is no authentic female-avatar tile to swap to — the current gender-neutral map figure IS
    correct. Fabricating one would violate the clean-room "match the original" rule.
  - GENDER HONORIFIC ($G) — DONE (the authentic gender-in-gameplay reflection). U6 addresses the
    Avatar by gender: male = "milord", female = "milady" (nuvie Player::get_gender_title; U6 gender
    0=male/1=female). apply_chargen sets g_avatar_title from g_cc_gender; both cvm_init calls pass it
    (was hardcoded "milord"), so NPC greetings using $G now say "milady" to a female Avatar. Validated
    (`make newgame`, Milady sample): NEWGAME title = milady.
  - STILL DEFERRED: HP/inventory beyond the spellbook+reagents = the shipped start.
- (3c) gypsy VISUALS — ✅ DONE + validated on TT.
  - The GYPSY.SHP "casting" scene is the question backdrop. mkworld bakes the EMPTY interior ONCE
    (GYPSY.SHP 0 @0,-20 + jar 16 @142,56 + 8 empty vials: glass 20/23 at nuvie sprite x's; hotspots 0)
    into a 320x200 canvas + ships PALETTES.INT 5 + the tall question parchment BLOCKS.SHP[3] (320x77).
    Engine: run_chargen sets palette 5 for the tournament (restores g_basepal after); cc_ask calls
    draw_gypsy_bg() + the scroll (@y120) + the real prose (ink 0x3e) + "Question N of 7" + [A]/[B].
  - **WOODS.SHP forest-path pan lead-in** (cc_woods_intro, PALETTES.INT 4: WOODS[0] 320 + WOODS[1]
    191 @320, panned left) + a brief "casting" lead-in (cc_gypsy_intro) — DONE.
  - **AUTHENTIC POUR CHOREOGRAPHY (drain model) — DONE + validated on TT** (2026-07-12; full
    pixel-faithful port of nuvie create_character gypsy_vial_anim). CORRECTED from the earlier
    fill-from-empty deviation: vials START FULL (level 3) and the gypsy DRAINS the winning virtue's
    vial one level into the central jar per question (vial ↓, jar ↑) — nuvie's real semantics. Over
    the 7-question single-elim tournament the CHAMPION's vial (3 wins) ends EMPTY, the finalist at 1,
    the two semifinalists at 2, the four first-round losers still FULL; the jar accumulates all 7
    pours (level 7).
    - PACK: mkworld now ships the RAW GYPSY.SHP shape table (all 200 shapes, matching nuvie's
      image_load_all) + a bg-only scene (shape 0 @ 0,-20, no baked vials/jar) + pal5 + the question
      parchment. Layout `[768 pal5][u16 gw][u16 gh][bg][u16 sw][u16 sh][scroll][u16 nsh]+nsh×[u16 w][u16 h][w*h]`.
      +~250KB (pack 2.13→2.38MB, safe under the ~4MB TT-RAM fast pool).
    - ENGINE (render/gamedemo.c): draw_gypsy_scene composites bg → jar_liquid(shape 8+lvl @146,83−lvl,
      behind glass) → jar glass(16 @142,56) → each vial's liquid(0x42+GV_IMG_OFF[v]+(lvl−3) @ liq_x,77)
      + glass(20 left/23 right @ vial_x,66); the poured vial's glass is hidden (carried by the hand).
      gypsy_pour(winner) = the 17-frame (idx 0..16) arm/hand path: reach to the vial, lift it, tip it
      over the jar (gypsy_start_pour shows the pour STREAM + advances the jar; gvl_liquid tracks the
      lifted vial's liquid following the hand), drain one level at idx 11, then set it back. All the
      nuvie tables ported verbatim (arm 1..6, hand 9+n, pour_img/pour_x/y, vial_liquid 96-table,
      hand_x/hand_y offsets) with 1-based→0-based index conversion. Wired: run_chargen calls
      gypsy_pour(win) after each cc_ask; cc_gypsy_intro sets vials FULL.
    - VALIDATED on TOS 3.06 TT: `CHARGEN.REQ='P'` (build/pour_frame.png — the arm+hand tip a vial over
      the jar mid-pour), `'q'` (build/pour_drain.png — 8 vials at distinct drain levels + jar), `'D'`
      (build/pour_seq.png — 3 sequential full pours drain vial 0 to 0, jar to 3; the animation
      completes cleanly with no hang/crash).
  - **JAR BUBBLES — DONE + validated on TT** (2026-07-12; nuvie image_bubble_effect). As virtues pour
    in, the jar essence CHURNS: every 4th update, each non-transparent liquid pixel is re-randomised to
    one of the poured virtues' colours (nuvie vial_colors {239,14,231,103,228,5,15,219}, palette-5
    indices). gypsy_start_pour accumulates the poured virtue's colour (bubble_add_color, max 8);
    bubble_effect speckles a WORKING COPY (g_jarbub) so the shared pack shape is never mutated;
    gypsy_update_bubbles fires it on the 4-count cadence. Runs during the pour (gypsy_anim_frame) AND
    while the player reads each question (cc_ask's wait loop now re-presents each idle frame). Validated:
    CHARGEN.REQ 'P' (single-hue blue churn) + 'q' (4-hue confetti mix, animated). Colour count grows as
    the tournament proceeds, so the mix richens toward the end.
  - TODO polish: authentic gypsy setup/closing narration from U.EXE (needs clean-room extraction).
  - Headless capture hook: CHARGEN.REQ byte selects a sub-screen (n/g/p/q/s/G/W/P/D), 'N' applies a
    sample character to the new game, default '1' = full flow. 'q'/'G'/'W'/'P'/'D' set the scene palette first.
