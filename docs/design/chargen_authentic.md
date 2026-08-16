# U6 Authentic Front-End & Character-Creation Ceremony — fidelity target

> Provenance
> - Derived from : the user's OWN game files — `refdata/dos/u6/U.EXE` (DOS front-end
>                  program) inspected with `strings -a -t d` / `grep -abo`; every string
>                  below was read out of U.EXE at the byte offset given in §7, then
>                  REDACTED to opening-words + length anchors for this committed spec
>                  (full text = build artifact, extracted from the user's U.EXE).
>                  ST equivalent confirmed in `refdata/st/u6/U.PRG` (same text, different
>                  offsets). Sequence/flow understood by READING nuvie
>                  `data/scripts/u6/intro.lua` (create_character, gypsy_ask_questions,
>                  gypsy_ab_select, main_menu, play, intro, acknowledgements) — no nuvie
>                  code is transcribed; only the game DATA text (which lives in the user's
>                  U.EXE) is quoted.
> - References   : nuvie/nuvie intro.lua (flow only); prior specs
>                  docs/design/intro_sequence.md and docs/rules/character_creation.md.
> - Tools        : strings/grep on U.EXE + U.PRG; gh api on nuvie repo (flow); no decompiler.
> - Validated by : exact text = confirmed (byte-for-byte from U.EXE). Sequence/UI = confirmed
>                  against nuvie's reconstruction + the team's already-shipped, TT-validated
>                  front end. Two ordering points flagged "verify vs DOS capture" in §8.
> - Confidence   : confirmed (all quoted text; menu options; chargen step order; A/B & portrait
>                  UI) / probable (which cutscene the "Introduction" item replays; the
>                  "Welcome, O Seeker!" placement).
> - Last updated : 2026-08-15 (real-Falcon feedback: §3f question-text layout rule + pointer
>                  visibility/palette fix; shape transparency = block coverage, see
>                  docs/formats/shapes.md "Transparency"). 2026-08-12: §1B.1 INTRO.SHP catalog
>                  + gargoyle choreography (flow read from nuvie intro.lua, implemented from
>                  this spec).

## 0. Scope & how the pieces fit
This is the authoritative on-screen text + sequence for U6's **front end**: boot logos, the
attract/intro cutscene, the main menu, and the full **gypsy character-creation ceremony**,
plus the new-game (gargoyle sacrifice) opening it hands off to.

The whole front end is one DOS program, **`U.EXE`** (ST: **`U.PRG`**). It owns the title,
the intro, the main menu, character creation, and character transfer. The separate
**`GAME.EXE`** (ST `GAME.PRG`) is the game engine proper; it takes over on *Journey Onward*.
All the narration/prompt/question text is **hard-baked as C strings inside U.EXE** — it is NOT
in a separate data file. (Per clean-room rules it is extracted from the user's own U.EXE at
build time and never committed. See docs/rules/character_creation.md for the structural
extractor; §7 here gives the offsets.)

**Text-rendering convention (applies to every quote below):** in the raw U.EXE bytes a closing
double-quote is stored as **`%c`** (a printf slot filled at runtime with glyph **0x7F**, U6's
closing curly-quote `”`). nuvie writes the same glyph as `\127`. So a stored
`"...destiny,%c` displays as `"...destiny,”`. Player name is spliced in via `%s`.

**REDACTION (clean-room, per docs/CLAUDE.md "no original text dumps"):** each U.EXE string
below is identified by its **opening words + total length** — e.g. `Dazed, you emerge from
the …` (147 ch) — never quoted in full. The full text is a BUILD ARTIFACT: mkworld extracts
it from the user's own U.EXE (offsets in §6) into the never-committed pack. Short lines
(&lt;48 ch) appear whole as locate anchors, per the "short and illustrative" allowance.

**Advance model (whole front end):** everything is **input-advanced, not timed.** Each
narration/prompt page draws, then blocks until **any key OR a mouse button** (nuvie
`wait_for_input`). Animations (TV surf, lightning, gypsy pour, jar bubbles, moongate glow)
loop *under* the held page. Esc aborts/skips. This is the opposite of an auto-scroll — the
player reads at their own pace.

---

## 1. INTRO / boot cutscene (before the main menu)

There are TWO distinct cinematics people call "the intro." Keep them separate:

### 1A. The SUMMONING story — plays at boot, after the logos (nuvie `play()`)
Boot order:
1. **Logos.** `bootup.m` plays; two full-screen images from `INTRO_1.SHP` (shapes 0x45 then
   0x46) fade in/out — the Origin / "A Lord British Production" plate and the **Ultima VI**
   title art (the gargoyle-ankh logo). (See §2 for the credited "A Lord British Production"
   text, which is reused on the Acknowledgements parchment.)
2. **The story cutscene** — the iconic "how the Avatar is pulled back to Britannia" sequence.
   Full beat-by-beat visuals/assets/timing are already specified in
   **docs/design/intro_sequence.md** (living room → storm window → forest path → the stones →
   red moongate → the Avatar steps into the gate). That doc is the authority for the visuals;
   this doc supplies each narration page's **identity** (opening words + length; confirmed
   from U.EXE, full text extracted at build time), in display order:

   - **Living room (INTRO_1.SHP, PALETTES.INT 0)** — TV channel-surf + ticking clock loop:
     - P1: `Upon your world, five seasons …` (90 ch)
     - P2: `You have traded the Avatar's …` (181 ch)
     - P3 (as the room pans to the window): `Outside, a chill wind rises...`
   - **Storm window (INTRO_2.SHP, PALETTES.INT 1)** — lightning flashes:
     - `...and in moments, the storm is upon you.`
     - `Tongues of lightning lash the …` (83 ch)
     - `In a cataclysm of sound …` (81 ch)
     - `Lightning among the stones!` / `Is this a sign from distant Britannia?`
   - **Forest path** (window frame slides off the same bg):
     - `You bolt from your house, …` (131 ch)
   - **The stones (INTRO_3.SHP, PALETTES.INT 2)** — obsidian + hand:
     - `Near the stones, the smell …` (178 ch)
     - `Wondering, you pick it up....`
   - **Red moongate (INTRO_3.SHP, PALETTES.INT 2, crimson palette-cycle)** — gate rises, Avatar walks in:
     - `...and from the heart of …` (78 ch)
     - `Exultant memories wash over you …` (161 ch)
     - `But your joy soon gives way to apprehension.`
     - `The gate to Britannia has …` (73 ch)
     - `Abruptly, the portal quivers and …` (90 ch)
     - `Desperation makes the decision an easy one.`
   - Then fade → **main menu**.

### 1B. The GARGOYLE SACRIFICE opening — the new-game cinematic (nuvie `intro()`)
This is the "shrine / winged nightmare / dagger / rescue" scene. Assets: **INTRO.SHP**
(different, smaller file than INTRO_1/2/3), **PALETTES.INT 7**, music `intro.m`. Beats:
gargoyle hordes close in from both sides, the Avatar is bound to a rune-struck altar, a winged
priest reads from a brass-bound book and raises a dagger — then a crossbow bolt kills the
priest and Dupre/Shamino/Iolo pull the Avatar out through a fresh moongate. The 20 pages
(U.EXE, in order; opening words + length):

- `Dazed, you emerge from the …` (147 ch)
- `At first the plain is …` (168 ch)
- `...but you have no place to go.`
- `Before you can offer a …` (94 ch)
- `With unearthly strength, the monsters …` (66 ch)
- `Kneeling, the hordes sway and …` (80 ch)
- `The leader unwraps a velvet …` (102 ch)
- `Shouts and jeers explode from …` (128 ch)
- `You close your eyes. A …` (73 ch)
- `Catcalls, the dagger, a scream, Death....`
- `Pandemonium. Shrieks of rage, of terror.`
- `From the inevitable, an impossibility emerges.`
- `You are still alive.`
- `Silent red light fills the …` (145 ch)
- `Friendly faces vault from a …` (181 ch)
- `"Quickly, old friend! To the …` (165 ch)
- `Snatching the fallen priest's book, …` (151 ch)
- `The gate wanes rapidly as …` (57 ch)
- `...but not rapidly enough.`
- `From the mob's vanguard, three …` (166 ch)

Then the game map begins. (The team's canonical new-game state drops the party at **Lord
British's castle throne room** — see docs/rules/character_creation.md.)

#### 1B.1 Scene composition — INTRO.SHP catalog + choreography

**Shape catalog** (our own dump of the user's INTRO.SHP, rendered with PALETTES.INT bank 7 —
13 shapes, whole-file LZW, identified visually):

| # | size | content |
|---|---|---|
| 0 | 99x93 | Dupre bust (blond knight, armor) |
| 1 | 59x89 | Iolo bust (bearded, red vest) |
| 2 | 63x87 | Shamino bust (green hood) |
| 3 | 147x48 | gargoyle horde row, far/small (enters screen-left) |
| 4 | 150x125 | gargoyle horde, near/large (enters screen-right) |
| 5 | 320x88 | the rune-struck altar (foreground slab, full width) |
| 6 | 320x200 | background: desolate plain, starry sky, two moons, moonlit fog |
| 7 | 82x77 | the moongate (concentric red rings; colours live in palette 232..239) |
| 8 | 168x121 | winged priest body, dagger raised + brass book (head is separate) |
| 9 | 213x73 | the Avatar bound, lying (drawn BEHIND the altar top edge) |
| 10 | 36x50 | priest head, quarrel in forehead (post-crossbow) |
| 11 | 36x50 | priest head, mouth open (chanting) |
| 12 | 16x68 | the binding ropes strap (drawn over the altar face) |

**Sprite list, in DRAW ORDER** (later entries occlude earlier; the altar after the Avatar is
what makes the Avatar "lie on" it). Positions are top-left screen px; flow per nuvie
`intro()` (read for understanding; reimplemented from this spec):

| sprite | shape | initial (x,y) | initially visible |
|---|---|---|---|
| bg | 6 | (0,0) | yes |
| moongate | 7 | (120,120) → rises to (120,58) | no |
| horde-left | 3 | (-84,109) | yes |
| horde-right | 4 | (326,199) | yes |
| priest body | 8 | (213,141) | no |
| priest head | 11 (→10 after crossbow) | (291,155) | no |
| Iolo | 1 | (168,202) | no |
| Shamino | 2 | (68,122) | no |
| Dupre | 0 | (-32,122) | no |
| Avatar | 9 | (49,68) | no |
| altar | 5 | (0,112) | yes |
| ropes | 12 | (210,132) | no |

**Narration scrolls** (BLOCKS.SHP): pages print with parchment ink 0x3e on shape 2 ("medium",
at (1,152)), shape 1 ("short", at (1,160)), or shape 0 ("small", at (33,157)); the four
black-interlude pages use shape 1 at (1,80), vertically centred on a black screen.

**Beats** (input = any key/click advances or skips the running animation; Esc aborts the
scene; every text hold after the priest arrives runs the idle sway + gate cycle):
1. Fade in on bg+altar+hordes(off-screen). Page 1 "Dazed…" (b2), page 2 "At first…" (b2).
2. **Horde approach**, 96 steps: left moves per 6-step table x{+1,+1,0,+1,+1,+1}
   y{0,0,-1,0,0,-1}; right (from step 24) x−2/step, y per table {-1,-1,0,-1,-1,-1}x2.
   Skip snaps: left (-4,77), right (182,79). Page "…no place to go." (b0).
3. Page "Before you can offer a protest…" (b2).
4. Page "With unearthly strength…" (b1) — **Avatar + ropes appear**.
5. Hordes shift +4y. **Priest walk-in**: body/head from (213,141)/(291,155), 23 steps of
   (−3,−3) then 14 of (0,−3); snap body (144,30), head (222,44).
6. Pages "Kneeling…" (b1), "The leader unwraps…" (b2), "Shouts and jeers…" (b2) — during
   every hold from here the hordes **sway** (12-phase ±3px x-wobble, each horde on its own
   phase, ~3/4 chance to advance per frame).
7. Page "You close your eyes…" (b1): **moongate rises** y 120→58 (1px/frame), with the
   **gate palette cycle** — rotate palette entries 232..239 one step every 4th frame — which
   keeps running through every later beat until the gate sinks. Hold, cycling, until input.
8. **Fade to black; four interlude pages** (b1 @ (1,80), each fade-in → wait → fade-out):
   "Catcalls…", "Pandemonium…", "From the inevitable…", "You are still alive."
9. **Fade the full scene back in** (all sprites incl. risen gate). Priest head swaps to
   shape 10 (quarrel in forehead). Page "Silent red light…" (b2).
10. **Priest falls**: 43 steps, body+head y+3/step (x jitters −1..+2); then hidden.
11. **Companions vault in** (83 steps): Dupre from (−32,122) x+1 (+2 after step 33), y−1 →
    (99,40); Shamino (from step 13) x+1 (+2 after 36), y−1 → (183,53); Iolo (from step 10)
    x+1, y−2 (−1 the first 4) → (240,62). Page "Friendly faces vault…" (b2).
12. **Ropes vanish** (bonds cut). **Retreat** (83 steps): each companion x−1,y+2; the Avatar
    y+1/step (slides down behind the altar = pulled off it). Pages "Quickly, old friend!…"
    (b2), "Snatching the fallen priest's book…" (b2).
13. Page "The gate wanes rapidly…" (b1) holds while the **gate sinks**: 151 steps, y+1 every
    2nd step (cycle still running). Then "…but not rapidly enough." (b0) and "From the mob's
    vanguard…" (b2) hold with sway only.
14. Fade out; palette restored; gameplay begins.

Engine note: shipped as the RAW 13-shape table (like GYPSY.SHP) + PALETTES.INT 7 + BLOCKS.SHP
0/1 (2 already ships); the engine composites the sprite list per frame — no baked scenes,
because nearly every layer moves independently. Since 2026-08-15 the compositing is
INCREMENTAL (dirty rects): each frame recomposes and presents only the union of the old+new
rectangles of the sprites that moved (plus a caption that appeared/vanished, and the gate's
rect after a ring-colour cycle on the Falcon's truecolor path, where a palette write only
shows at the next present). A full recomposite + 320×200 present cost ~200 ms per step on a
16 MHz Falcon (the real-hardware "really slow" report); a moving horde or companion touches
~15–25 % of the screen. Shape pixels: covered index 0 is opaque black, uncovered = 255
transparent (docs/formats/shapes.md "Transparency" — the boot-sole/eye/altar-band holes seen
on hardware were the old 0-key).

> ⚠️ TRIGGER UNCERTAINTY (probable, not confirmed): In nuvie, `play()` (boot) shows **1A**,
> and the **"Introduction" menu item** replays **1B** (the gargoyle scene). On the original
> DOS release the gargoyle sacrifice is the *new-game* opening played after *Journey Onward*;
> whether the "Introduction" menu item replays 1A or 1B on real DOS should be confirmed with a
> DOSBox capture before we lock our menu wiring. The **text** for both is confirmed either way.

---

## 2. MAIN MENU

**Layout** (nuvie `main_menu_load`, assets from `TITLES.SHP` + `MAINMENU.SHP`, music
`ultima.m`):
- **Title** — `TITLES.SHP` shape 0 ("Ultima VI" logo) at pixel (19, 0).
- **Subtitle** — `TITLES.SHP` shape 1 ("The False Prophet") at (59, 47).
- **Menu block** — `MAINMENU.SHP` shape 0 at (49, 83). This single sprite contains **all five
  option lines rendered as ART** (that is why the labels are NOT ASCII strings anywhere in
  U.EXE — only the filenames `mainmenu.shp`/`titles.shp` appear). Each line is drawn in its
  own palette index — the set `{14, 33, 34, 35, 36}`, one per line — so highlighting is done
  purely by **animating those palette entries** to brighten the selected line.

**The five options, in order (index 0–4):**
1. **Introduction**       (hotkey `i`)
2. **Create a Character** (hotkey `c`)
3. **Transfer a Character** (hotkey `t`)
4. **Acknowledgements**   (hotkey `a`)
5. **Journey Onward**     (hotkey `j`)

**Input model — BOTH keyboard and mouse:**
- **Keyboard hotkeys:** the first letter selects directly (`i` `c` `t` `a` `j`).
- **Keyboard navigation:** Up/Down move the highlight (index 0..4, clamped); **Enter**
  activates the highlighted line.
- **Mouse:** moving the pointer over a line highlights it (hit-test x∈(56,264), each line a
  ~21px y-band from y≈86 down); **clicking** the line activates it. Selection is shown by the
  palette-brighten highlight described above (no cursor box; the text itself lights up).
- The menu holds the title+subtitle+menu sprites visible throughout; selecting Introduction /
  Create / Acknowledgements runs that sub-flow and returns to the menu; **Journey Onward**
  fades out and returns "J" (start game). **Transfer a Character** is a real option (see §6).

**Acknowledgements (option 4)** — a 4-page credits scroll drawn onto the `VELLUM1.SHP`
parchment (shape 0). Every page carries the header `Ultima VI` / `A Lord British Production`
plus a rule line, then a section of credits; each page waits for input. Section layout by
page (names extracted from U.EXE at build time, offsets 94300–95067 per §6): **P1** Produced
by (2 names, leading with *Richard Garriott*) / Executive Producer (1) / Programming (4);
**P2** Writing (9); **P3** Art (6) + Music (4); **P4** Quality Assurance (4) + Additional
Support (7).

---

## 3. CREATE A CHARACTER — the gypsy ceremony (step by step, exact text)

Selecting **Create a Character** fades the menu out and enters `create_character` (music
`create.m`). The whole ceremony builds exactly six values: **name, gender, portrait, Str, Dex,
Int** (U6 has no class). It is done entirely on-screen; there is no disk step. Order:

### 3a. Name entry
- Background: `VELLUM1.SHP` shape 0 (parchment).
- Prompt printed on the parchment (y≈24): **`By what name shalt thou be called?`**
- The player **types** the name (max **13 chars**; letters/digits/space/`' , - .`). Editing:
  Backspace deletes; Up/Down cycle the current letter alphabetically (typewriter-wheel style);
  Right appends `a`. **Enter** confirms (only when non-empty). Esc aborts back to the menu.
- The typed name is drawn **centered** on the parchment (`x = 16 + (284 − text_width)/2`),
  first at y≈120, later repositioned to y≈89 for the portrait screen.

### 3b. Gender prompt
- Same parchment. Prompt added below the name (y≈52): **`And art thou Male, or Female?`**
- Input: press **`M`/`m`** = Male, **`F`/`f`** = Female; OR Up/Down toggles the shown letter
  `M`/`F` and **Enter** confirms. Esc aborts.
- Gender encoding: **1 = Male, 0 = Female** (note the flip vs. the in-game VM convention; see
  character_creation.md §apply_chargen).

### 3c. Portrait selection (and re-pick gender)
- The parchment redraws (VELLUM1.SHP shape 0) with a horizontal rule and **four labelled
  buttons** (each an art label from VELLUM1.SHP), stacked vertically, plus the live portrait:
  - **Sex** (toggle) — VELLUM1.SHP shape 3, at (94,112)
  - **Portrait** (cycle) — shape 0x12, at (62,129)
  - **Continue** — shape 0x10, at (86,146)
  - **Exit** (abort) — shape 4, at (80,163)
  - **Portrait image** — `MONTAGE.SHP`, shown at (195,118). Index = `gender*6 + portrait_num`,
    so **Female portraits are MONTAGE shapes 0..5, Male are 6..11** — **6 portraits per
    gender.**
- The currently-active button carries a 2px **underline highlight** (palette color 248).
- **Mechanics:**
  - **Portrait** cycles `portrait_num` 0→1→…→5→0 (wraps; forward only). Hotkey `p`/`P`.
  - **Sex** toggles Male/Female *here too* — so **yes, you can go back and change gender on
    this screen**, and it re-selects the matching 6-portrait set. Hotkey `s`/`S`.
  - **Continue** = confirm and proceed. Hotkey `c`/`C` (or Enter when Continue is highlighted).
  - **Exit** = abort to menu. Esc (or Enter when Exit is highlighted).
  - Keyboard Up/Down move the button highlight (0..3); mouse clicks the button's rect.
- On Continue: fade out.

### 3d. The approach (woods → wagon), scrolling narration
- Fade in on **`WOODS.SHP`** (shapes 0 + 1 form a wide forest-path panorama), **PALETTES.INT
  4**. Narration is drawn on a `BLOCKS.SHP` parchment "scroll" at the bottom; each page **waits
  for input** (click/keypress) — it does **not** auto-advance. Pages, in nuvie's order:
  1. (scroll BLOCKS.SHP shape 1, y≈160): **`"Welcome, O Seeker!"`**
  2. — the woods **pan left** (auto, ~95 steps of 2px; a keypress skips it) revealing the wagon —
  3. (scroll BLOCKS.SHP shape 2): **`A lonely stroll along an …` (133 ch)**
  4. (scroll shape 2): **`A woman's voice rings out …` (132 ch)**
  - Then fade to the gypsy interior.
  > ⚠️ Two text quirks, reproduced verbatim because they are the real game data: (i) `"Welcome,
  > O Seeker!"` is shown **first**, before the approach narration — narratively odd (it reads
  > like the gypsy's voice heard from the path); verify placement vs a DOS capture. (ii)
  > "...beckoning you into across the wagon's threshold..." is missing a connective; the
  > original likely means "...beckoning you in. You step across the wagon's threshold..." Keep
  > the data as-is unless a capture shows otherwise.

### 3e. Entering the wagon — the gypsy's greeting
- Fade in on **`GYPSY.SHP`** interior (shape 0 bg at (0,−20)), **PALETTES.INT 5**. On the
  table: the **8 virtue vials** (glass shapes 20 left-set / 23 right-set; liquid shapes at
  0x42+offset, level 0..3) and the central **jar** (glass shape 16; liquid shape 8+level). All
  8 vials start **FULL** (level 3); the jar starts empty.
- Narration on the tall scroll (BLOCKS.SHP shape 3), two pages, each waits for input:
  - Page 1: **`"At last thou hast come …` (99 ch)** followed by **`"Sit before me now, and …` (89 ch)**
  - Page 2: **`On a wooden table eight …` (69 ch)** followed by **`"Behold the Virtues of the …` (79 ch)**

### 3f. The virtue (tarot) questions
- **Exactly 7 questions**, a single-elimination bracket over the **8 virtues** (order:
  0 Honesty, 1 Compassion, 2 Valor, 3 Justice, 4 Sacrifice, 5 Honor, 6 Spirituality,
  7 Humility): shuffle 8 → ask 4 → shuffle winners → ask 2 → ask 1. Each question pits two
  virtues; the answer picks which virtue wins and advances.
- **Presentation:** the question prose is drawn on the tall parchment scroll (BLOCKS.SHP shape
  3, at y=120) with the cutscene text rule (nuvie `image_print(scroll, q, 7, 303, 8, 9)`):
  proportional U6 font, pen starts at (8, 120+9), words filled greedily onto a line while it
  fits a **303-px** line width measured from x=7, every FULL line **justified** (slack split
  evenly between its words), the last line left-aligned, **8-px** line advance, ink 0x3e.
  (Our first pass centred a 38-column monospaced wrap, which is wider than the proportional
  font and ran under the buttons on real hardware — fixed 2026-08-15.) Two **on-screen
  buttons** appear at the lower-right: **`A`** = GYPSY.SHP shape 7 at
  (279,174), **`B`** = GYPSY.SHP shape 8 at (296,174), with a 2px underline highlight (color
  248) under the selected one. **The mouse pointer (U6MCGA.PTR arrow) is shown for the
  questions only** — nuvie `mouse_cursor_visible(true)` right before the A/B buttons appear
  and `false` after the 7th answer; it is hidden for the rest of character creation and the
  intro, and shown on the main menu. Before showing it over the gypsy palette nuvie patches
  palette entries **19 → (200,200,200)** and **27 → (68,68,68)** ("fix mouse cursor": the
  arrow's grey pixels use those indices, which PALETTES.INT 5 colours differently). Any
  "wait for input" page in the ceremony advances on a key OR a mouse click (nuvie
  `wait_for_input()`). There is **no "question N of 7"
  counter in the original** (our engine adds one — a deliberate, non-authentic aid; drop it for
  strict fidelity).
- **Input — BOTH keyboard and mouse:** `A`/`a` or `B`/`b` choose directly; Left/Right move the
  highlight between the two buttons and **Enter** picks the highlighted one; a **mouse click**
  in the button rect (A: x 278–296, B: x 296–313, y 173–190) picks it. Between/under questions
  the jar **bubbles** and, on each answer, the gypsy's arm **pours** the winning virtue's vial
  one level into the central jar (drain model — see character_creation.md §3c).
- **Stat effect:** Str/Dex/Int each start at **15 (0x0F)**; each of the 7 winning virtues adds
  per the tables in docs/rules/character_creation.md §"Stat adjustment". Final values are
  written to the new-game data.
- **The 28 question texts** (every unordered virtue pair, C(8,2)=28; only 7 are asked per
  playthrough). Identified by opening words + length, in the game's internal id order (id
  0x69..0x84, text-index 1..28); full text extracted from U.EXE at build time (§6 offset
  98193). Each already contains its own " A) …; or B) …?" clause:

  1. `"Entrusted to deliver an uncounted …` (241 ch)
  2. `"Thou has been prohibited by …` (228 ch)
  3. `"A merchant owes thy friend …` (213 ch)
  4. `"Thee and thy friend are …` (265 ch)
  5. `"Thou art sworn to protect …` (232 ch)
  6. `"Thy friend doth seek admittance …` (236 ch)
  7. `"Thy Lord doth mistakenly believe …` (192 ch)
  8. `"Thou dost manage to disarm …` (183 ch)
  9. `"After twenty years thou hast …` (238 ch)
  10. `"Thee and thy friends hath …` (228 ch)
  11. `"Thou art sworn to uphold …` (232 ch)
  12. `"Thou hast been taught to …` (229 ch)
  13. `"The Captain of the King's …` (220 ch)
  14. `"Thou hast been sent to …` (221 ch)
  15. `"A burly knight accosts thee …` (156 ch)
  16. `"During battle thou art ordered …` (204 ch)
  17. `"A local bully pushes for …` (152 ch)
  18. `"Although a simple fisherman, thou …` (262 ch)
  19. `"During a pitched battle, thou …` (228 ch)
  20. `"Thou hast sworn to do …` (221 ch)
  21. `"Thou dost believe that virtue …` (200 ch)
  22. `"Unwitnessed, thou hast slain a …` (225 ch)
  23. `"Thou art a bounty hunter …` (230 ch)
  24. `"Thou hast spent thy life …` (243 ch)
  25. `"Thou art an elderly, wealthy …` (228 ch)
  26. `"In thy youth thou didst …` (226 ch)
  27. `"Though thou art but a …` (239 ch)
  28. `"Thy parents wish thee to …` (192 ch)

  For each question, **A** selects the lower-indexed virtue in the pair and **B** the
  higher-indexed one; the exact A/B→virtue lookup and the pair→question-id matrix are in
  docs/rules/character_creation.md §"Question data". (They are consistent with the wording:
  the virtue named in the "A)" clause is the A-virtue, the "B)" clause names the B-virtue.)

### 3g. The flask, the sleep, the awakening (chargen outro)
After the 7th answer the A/B buttons vanish and the closing passage plays. Text verbatim from
U.EXE (`%s` = the player's name):

- Tall scroll (BLOCKS.SHP 3), holds while the jar bubbles: **`"The path of the Avatar …` (236 ch)**
- Fade → **PALETTES.INT 6**, GYPSY.SHP shape 198 (a bubbling flask close-up): **`As you drink from the …` (147 ch)**
- Fade (bg color 0x75) → GYPSY.SHP shape 199, three pages (each waits for input):
  - **`You wake in a different …` (175 ch)**
  - **`The sagas of Ultima IV …` (147 ch)**
  - **`Finally, tempered by your struggles …` (130 ch)**
- Fade to black; the six new-game values (name, gender, portrait, Str, Dex, Int) are committed.

**What happens next:** creation returns to *Journey Onward* — the game starts. The player is
NOT dropped straight onto the map: the **gargoyle sacrifice opening (§1B)** plays first, ending
with the companions dragging the Avatar through the collapsing moongate; only then does
gameplay begin (the team's canonical start = Lord British's castle throne room, party of
Avatar/Dupre/Shamino/Iolo). The gypsy's flask does not "teleport to Lord British's castle" by
itself — it bridges into the sacrifice cinematic which delivers the party to Britannia.

---

## 4. TRANSFER A CHARACTER (option 3) — for completeness
Imports a character from an Ultima IV or Ultima V save. U.EXE prompts (title
`TRANSFER A CHARACTER`): **`Enter the letter of the …` (105 ch)** and, on failure, **`No character found! If you …` (126 ch)** Not part of the gypsy ceremony; noted so our menu wiring is complete. (nuvie
stubs this; low priority for us.)

---

## 5. Assets used by the ceremony (all from the user's data)
| Asset | Role |
|---|---|
| `TITLES.SHP` | shape 0 = "Ultima VI" logo, shape 1 = "The False Prophet" subtitle |
| `MAINMENU.SHP` | shape 0 = the 5 menu-option lines (art; highlighted via palette idx 14/33/34/35/36) |
| `INTRO_1/2/3.SHP` | boot summoning cutscene (living room / storm / stones+moongate); INTRO_1 shapes 0x45,0x46 = logo/title |
| `INTRO.SHP` | gargoyle-sacrifice opening (altar, gargoyles, companions, moongate) |
| `WOODS.SHP` | shapes 0+1 = forest-path panorama (chargen approach), PALETTES.INT 4 |
| `GYPSY.SHP` | gypsy interior + 8 vials + jar + arm/hand pour frames; shape 7=A btn, 8=B btn, 16=jar glass, 198=flask close-up, 199=sleep bg; PALETTES.INT 5 (interior), 6 (flask) |
| `MONTAGE.SHP` | 12 portraits: Female 0..5, Male 6..11 (index = gender*6+num) |
| `VELLUM1.SHP` | parchment bg for name/gender/portrait screen + Acknowledgements; shapes 3/0x12/0x10/4 = Sex/Portrait/Continue/Exit button labels |
| `BLOCKS.SHP` | narration scrolls: shape 1 short, 2 medium, 3 tall (4-line, questions/gypsy) |
| `PALETTES.INT` | palette banks: 0 boot, 4 woods, 5 gypsy interior, 6 flask, 7 gargoyle intro |
| Music `.M` | `bootup.m` (logos), `ultima.m` (menu), `intro.m` (gargoyle), `create.m` (chargen), `forest.m`, `stones.m`, `gargoyle.m`, … |
| Font | text via a proportional `U6.CH` renderer; inks: 0x3e parchment prose, 0x48 form text, 0x3d/0xC ack header/section |

## 6. Where the TEXT lives (extract clean-room at build time)
All front-end text is inside the front-end executable, byte-for-byte as quoted above:
- **DOS:** `refdata/dos/u6/U.EXE` (verified). Key offsets (`strings -a -t d`):

  | Offset | Text |
  |---|---|
  | 91496 | asset-filename block starts (`mainmenu.cga`… `blocks.shp`) |
  | 93933 | `By what name shalt thou be called?` |
  | 93979 | `And art thou Male, or Female?` |
  | 94300–95067 | Acknowledgements credits (`A Lord British Production` … `Cheryl Neeld`) |
  | 96114–97504 | Boot summoning narration (`Upon your world, five seasons…` … `Desperation makes the decision…`) |
  | 97548 | `"Welcome, O Seeker!` |
  | 97570 | `A lonely stroll along an …` (48 ch) |
  | 97706 | `A woman's voice rings out with friendship…` |
  | 97841 | `"At last thou hast come to fulfill thy destiny,` |
  | 97945 | `"Sit before me now, and …` (57 ch) |
  | 98037 | `On a wooden table eight bottles stand…` |
  | 98110 | `"Behold the Virtues of the Avatar,` / `"Let us begin the casting!` |
  | 98193–104297 | the 28 virtue questions (contiguous) |
  | 104493 | `"The path of the Avatar …` (57 ch) |
  | 104554 | `…she passes you the flask …` (48 ch) |
  | 104647 | `"Drink of these waters and …` (53 ch) |
  | 104735 | `As you drink from the …` (52 ch) |
  | 104885 | `You wake in a different time…` |
  | 105063 | `The sagas of Ultima IV and Ultima V…` |
  | 105213 | `Finally, tempered by your struggles…` |
  | 105345–107317 | Gargoyle sacrifice opening (`Dazed, you emerge…` … `portal's last handspan of light!`) |

- **ST:** `refdata/st/u6/U.PRG` (same strings; different offsets — e.g. `shalt thou`@11979,
  `Welcome, O Seeker`@41153, `fulfill thy destiny`@41473, `Let us begin the casting`@41773,
  `flask`@48279, `epic challenge of Ultima VI`@48987).
- The menu OPTION LABELS are **not text** — they are art baked into `MAINMENU.SHP` shape 0
  (extract as an image, not a string).
- Extraction is structural (filename-block landmark + punctuation grouping), never a hardcoded
  copyrighted anchor, and the extracted text is a build artifact that is never committed — see
  docs/rules/character_creation.md §3b and docs/design/intro_sequence.md "Narration text".

## 7. Confidence & open questions
- **Confirmed:** every quoted line (byte-for-byte from U.EXE); the 5 menu options + order +
  hotkeys + dual keyboard/mouse model + palette-highlight; the chargen step order; the A/B
  button geometry and input; the 6-per-gender portrait set; the 7-question single-elim; the
  flask/sleep/wake outro; stat base 15 + adjustment source.
- **Probable / verify against a DOS capture:**
  1. Which cutscene the **"Introduction"** menu item replays (nuvie = gargoyle 1B; the original
     may replay the summoning 1A). Text for both is confirmed regardless.
  2. The **"Welcome, O Seeker!"** page appearing *before* the approach narration (nuvie order).
  3. The missing connective in "…beckoning you into across the wagon's threshold…" (data quirk).
- **Non-authentic in our current build (drop for strict fidelity):** the "Question N of 7"
  counter our engine draws (§3f) has no original counterpart.

## 8. Related specs
- docs/design/intro_sequence.md — boot cutscene visuals/assets/timing (authority for §1A).
- docs/rules/character_creation.md — tournament logic, stat tables, pour/bubble visuals,
  new-game wiring, status screen (authority for the mechanics behind §3f–§3g).
