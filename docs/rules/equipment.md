# Equipment: readying weapons, armour and worn items (U6)

Games: Ultima VI (Savage/Martian have their own readiable sets — RuleSet hook later).
Implemented by: /engine (slot model + combat fold), /render gamedemo.c (R)eady UI, /tools mkworld (U6WQ).

> Provenance
> - Derived from : READING ScummVM/Nuvie `actors/u6_actor_types.h` (readiable_objects table),
>   `actors/actor.cpp` (add_readied_object / remove_readied_object slot placement) to understand
>   the original rules; our own OBJLIST/inventory format knowledge (objlist.md §3b) for the save
>   representation. No code transcribed; tables re-expressed from the observed rules.
> - References   : Nuvie as above; docs/rules/combat.md §4/§5 (weapon damage + armour class values,
>   already VERIFIED — they equal Nuvie's per-item attack/defence columns).
> - Tools        : none (rule spec)
> - Validated by  : headless captures (READY.REQ modes) + combat asserts (readied sword changes
>   avatar damage; readied armour changes AC) — see §7.
> - Confidence   : confirmed (slot rules + readiable set, ids cross-checked two ways); §5 UI is OUR design.
> - Last updated : 2026-09-20

## 1. Body slots

Eight readied-object slots per actor:

| # | slot  | holds |
|---|-------|-------|
| 0 | HEAD  | helms |
| 1 | NECK  | amulets, spiked collar |
| 2 | BODY  | body armour, clothes, cloaks, belts |
| 3 | ARM   | main hand: one-handed weapons, shields, tools, torches |
| 4 | ARM2  | off hand: second one-handed item — OR occupied by a two-handed weapon |
| 5 | HAND  | rings |
| 6 | HAND2 | second ring |
| 7 | FOOT  | boots |

## 2. Placement rules (the original's, observed via Nuvie)

An object's TABLE CLASS (§3) names its preferred slot; placement then follows:

- class ARM (one-handed): goes to ARM; if ARM is taken, goes to ARM2 — **unless** ARM holds a
  two-handed weapon (then refuse).
- class TWO-HANDED (bows, crossbows, 2H hammer/axe/sword, halberd, magic bow, triple crossbow):
  requires BOTH ARM and ARM2 empty; occupies ARM and marks it two-handed (ARM2 stays blocked).
- class HAND (rings): HAND, else HAND2.
- classes HEAD / NECK / BODY / FOOT: exactly that slot; refuse if taken.
- an obj_n not in the table is NOT readiable ("Thou canst not ready that!").

Unreadying any item simply frees its slot(s) and returns the item to the pack.

## 3. Readiable objects (obj_n → class) — CONFIRMED ids

| class | obj_n |
|---|---|
| HEAD | 1 leather helm, 2 chain coif, 3 iron helm, 4 spiked helm, 5 winged helm, 6 brass helm, 7 gargoyle helm, 8 magic helm |
| ARM (shields) | 9 wooden shield, 10 curved heater, 11 winged shield, 12 kite shield, 13 spiked shield, 14 black shield, 15 door shield, 16 magic shield |
| BODY | 17 cloth, 18 leather, 19 ring mail, 20 scale mail, 21 chain mail, 22 plate mail, 23 magic armour, 25 guild belt, 26 gargoyle belt, 81 storm cloak, 124 tunic, 126 dress, 137 pants |
| NECK | 24 spiked collar, 74 ankh amulet, 75 snake amulet, 76 amulet of submission |
| FOOT | 27 leather boots, 28 swamp boots |
| ARM (weapons/tools) | 33 sling, 34 club, 35 main gauche, 36 spear, 37 throwing axe, 38 dagger, 39 mace, 40 morning star, 43 sword, 48 glass sword, 49 boomerang, 57 spellbook, 78 staff, 79 lightning wand, 80 fire wand, 83 flask of oil, 90 torch, 91 zu ylem, 100 scythe, 101 pitchfork, 102 rake, 103 pick, 104 shovel, 105 hoe, 109 rolling pin, 113 cleaver, 114 knife, 158 lute, 203 pliers, 204 hammer |
| TWO-HANDED | 41 bow, 42 crossbow, 44 two-handed hammer, 45 two-handed axe, 46 two-handed sword, 47 halberd, 50 triple crossbow, 54 magic bow |
| HAND (rings) | 82 ring, 256 protection ring, 257 regeneration ring, 258 invisibility ring |

(Every id cross-checked against the reference object list AND our combat.md §4/§5 tables —
e.g. 43 sword dmg 15, 47 halberd dmg 30, 22 plate AC 7 already VERIFIED in engine/combat.c.)

## 4. Combat fold (unchanged semantics, live inputs)

As combat.md CB3b, now recomputed whenever an actor's readied set changes:
- `g_aweapon[a]` = the readied item with the LARGEST `weapon_dmg` (0 = unarmed);
- `g_aac[a]` = Σ `armour_ac(obj_n)` over all readied items.

## 5. The status display, as DOS has it (2026-11-04)

> Provenance: the U6 manual's "party display", "inventory display" and keyboard list; READING GAME.EXE 4.5's display
> routines (d25:0..1738) for the layout in screen pixels. Re-expressed, no code transcribed.

Each party member **carries its own things**: a pack row has an owner (the record's owner on load, on save). A thing
got goes to the one played. The gold alone stays one purse, the leader's, and is drawn in its pack.

**Party display** (the default): the sky and the roster, five rows; `+` / `-` or the arrows at (168,24), (168,96) scroll a
longer one. Health in red below 10, green if poisoned. A click on a figure opens that member's inventory, on the name its
portrait. `F1-F8` show the Nth member's portrait, `F10` returns here, `*` flips portrait and inventory, `+` / `-` step
through members. `Tab` puts the crosshair in the display (arrows move it, Enter selects), and again back on the map. Esc from
there also returns it. The display stays up through verbs and walking. (`F9`, not a DOS key, is the music source, F1 in the
menus.)

**Inventory** (all at x,y): the name centred on the top row; the figure, tiles 368-371, at (192,32); eight slots with the empty
ring (tile 410): head (200,16), neck (176,24), weapon hand (224,24), first ring (176,40), body (224,40), other hand (176,56),
second ring (224,56), feet (200,64); the member's sprite at (272,16); the pack, four across and three down from (248,32),
an arrow glyph at column 8 of text rows 3 and 8 when more lies that way; `E:n/ms` (readied weight / strength) and `I:n/ms`
(all carried / twice strength, stones) on text row 9; the buttons along y=88: previous (176; tile 387, not for the first),
party (192; 384), portrait (208; 385), next (224; 388, not for the last), and for a companion the combat mode (240; 391) with its
name to the right on text row 11. A selected thing with no verb armed is readied, or taken off; a verb armed acts on it
(L)ook, U)se, D)rop then its "Location: ", M)ove).

**Portrait and stats:** name on the top row; the portrait at (176,16); from column 10: `STR:n`, `DEX:n`, `INT:n` on rows 2-4,
`Magic` and `n/m` on 6-7, `Health` and `n/m` on 8-9, `Lev/Exp` and `n/m` on 10-11; the same buttons, the third with tile 386.

**M)ove** of a carried thing: `To `, a crosshair: another of the party takes the stack (`To Dupre.`, 5 points), `To yourself.`,
`To nobody.`, or `Only within the party!`. A parchment goes straight into the book (§4 of magic_skills.md).

**Containers in the pack (DOS, read 2026-11-05).** Chest 98, backpack 99, spellbook 57, barrel 186, bag 188, basket 191, crate 192, dead body 339 and the
vortex cube 62 hold things. Selected with no verb armed, any of these but the book opens: its contents fill the pack area, its picture replaces the
member's sprite and its name the member's; a locked chest (frame bit 1) says `It's locked.`, a closed one opens first. A click on the picture goes
back up a level. M)ove's `To ` on a thing in the pack: `not a container`; `Can't move to another person's bag.`; `How can a container go
into itself!`; `Only spells can go into the spellbook.`; `Only moonstones can go into the vortex cube.`; from inside it: `Spells are not suppose to be
taken out from the Spellbook.`, else `To out of bag.`, one level up; else `To into bag.`; 5 points. On the picture of the open one: out of it.
A container taken from the map keeps what it holds and weighs what it holds; dropped, it keeps it too. **New things go to the front of a list,** the order of the pack and of the weapons in A)ttack.
Each member's arrows and bolts are its own. A person outside the party has a pack too, read in when it first needs it (`npc_pack`).

**Weight:** G)et refuses at `The total is too heavy.` when what the leader carries plus the thing passes 2 stones per point of
strength. Reagents are the caster's own. Keys, scripts and the rest still look through the whole party.

**E)quip** (key E, not DOS, kept): the pack in order, E steps, 1-5 readies on that member.

## 6. Pack + save representation

- **U6WQ pack section** (magic-guarded, after U6WL): `[4 "U6WQ"][u16 n]` + n × `[u8 actor][u8 count]
  [count × u16 obj_n]` — each actor's readied items from the source save (objlist.md §3b:
  status&0x18==0x18, owner = x field). mkworld keeps U6WL (folded weapon/AC) for older engines;
  the engine prefers U6WQ when present and re-folds §4 itself.
- **Save writer** (2026-09-29): do_save reconciles readied status into SAVED.000 — every party
  readied record is demoted to carried, then one carried record per live slot item is marked
  readied by its wearer (save_load.md §5 step 3). Round-trip VERIFIED by LOAD.REQ mode '2'
  (helm moved from member 2 to the avatar survives two save→load cycles).

## 7. Test hooks

`READY.REQ` (1 byte) headless modes: '1' = ready a staged sword+chain-mail to the avatar, print
`READY weapon=… ac=…` to the console and open the status view; '2' = two-handed rule (halberd
then shield refused); '3' = unready flow. Combat assert: after '1', avatar hits use sword damage.

## 8. Effects of readied magic items (IMPLEMENTED 2026-10-27)
> Derived from: READING Nuvie `usecode/u6_usecode.cpp` magic_ring / storm_cloak /
> amulet_of_submission / cannot_unready, and the U6 script `u6/actor.lua` advance_time (the
> per-turn readied-item loop: vanishing odds, regeneration, the storm timer) — to understand;
> re-expressed. Implemented in /render gamedemo.c (ready_fx_on/off/tick), save.c (timer 13).

| item | readied effect |
|---|---|
| protection ring 256 | +5 armour class (combat.md §4 table — the fold already counts it); never vanishes |
| regeneration ring 257 | the wearer regains 1 HP per turn, up to their maximum |
| invisibility ring 258 | the wearer is invisible while it is worn (one ring suffices); taken off → visible unless a second ring stays on |
| storm cloak 81 | a MAGICAL STORM: objlist timer 13 (0x1c10, objlist.md) is set to 20 on readying, counts down a turn at a time but is held at ≥1 every turn the cloak is worn, and is cleared when it comes off. In the original the storm keeps foes from firing their magic charges; u6e tracks and saves the timer — no foe wields charges yet |
| amulet of submission 76 | cannot be taken off: "Magical energy prevents you from removing the amulet." |

- **Vanishing**: each turn, every readied regeneration ring, invisibility ring and storm cloak of a
  party member has a 1-in-1001 chance to VANISH — "A ring has vanished!" / "A cloak has vanished!"
  — the item is gone (not returned to the pack); a vanished invisibility ring reveals its wearer
  (unless a second is worn), a vanished cloak ends the storm.
- The effects apply to PARTY members (the original's loop is `in_party`); an NPC wearing a ring
  (Lord British wears the protection and regeneration rings in the starting data) keeps the AC only.
- Hooked at every ready/unready path: E)quip, the status view digit (§5), the pool absorb at load.
- Test hook `READYFX.REQ` mode '1' (usecode_use.md): the invisibility ring hides the Avatar while worn
  and not after; the regeneration ring heals 1/turn; the cloak sets the storm to 20, counts it down,
  clears it when removed; the amulet refuses removal; a worn ring vanishes within a long run.

## Status
§8 VERIFIED 2026-10-27: READYFX.REQ '1' identical on emulated TT and the stock 4 MB Falcon — the
invisibility ring hides the Avatar on readying and across 5 turns, removal reveals; the regeneration
ring restores 5 HP in 5 turns; the storm cloak sets timer 13 to 20, 17 after 3 turns, held at 1 while
worn, 0 on removal, and a set value reaches objlist 0x1c10 on S)ave; the amulet of submission stays on
("Magical energy prevents you from removing the amulet."); a worn regeneration ring vanished after
149 turns ("A ring has vanished!").
VERIFIED 2026-09-20 — engine/equip.c (22 host asserts, `make equiptest`); U6WQ ships from the save
(18 actors / 68 items on the canonical save; folds match the old U6WL values); READY.REQ modes 1-3
validated on emulated TT (fold prints + the status-view gear list + digit unready). Save
persistence of ready CHANGES deferred (§6).

## 9. Readiable objects as DOS has them (read 2026-11-05; overrides §2 and §3 where they differ)

> Provenance: READING GAME.EXE 4.5, the R)eady command and the slot routine it calls (ranges of tile numbers, then a
> 33-tile hand list), its hand placement and its messages. Re-expressed; checked by comparing every object's class
> against the §3 table. Confidence: confirmed (reading).

DOS keys the slot by the object's TILE (base tile + frame), but every frame of a readiable object gives the same slot.
Against §3:
- **Off hand first** (a new class, OFF-HAND): the shields 9-16 and the main gauche 35 go to ARM2, else ARM; DOS's
  weapons go to ARM, else ARM2. Both refuse while a two-handed weapon is held. (§3 put the shields with the weapons.)
- **Readiable in a hand** besides §3's list: 115 wine, 116 mead, 141 decorative sword.
- **Not readiable:** 203 pliers, 204 hammer (§3 had them). `Can't be readied!`
- **26 gargoyle belt goes on the NECK**: DOS lists its tile under both neck and body, and asks neck first.
- Messages: `Can't be readied!`, `Too heavy!` (the leader's carried weight plus the thing over STR x 10 counts),
  `No place to put!` when the slot it lands in is taken.

So every object with a damage or armour row is readiable, as tools/equiptest asks.
