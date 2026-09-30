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

## 5. u6e UI mapping (OUR design — the original used a per-member paper-doll)

u6e keeps the party pack as ONE pooled list (established deviation). Equipment therefore:

- Each actor's READIED set is per-actor (8 slots, §1), initialised from the pack (§6).
- **E)quip** (key E; the original had no top-level verb — readying lived in the inventory screen;
  E is free in our dispatcher) opens the pack list in EQUIP mode: ↑/↓ select an item,
  **1..5 readies it to that party member** (roster order), Esc closes. Success prints
  "<name> readies the <item>."; failures: "Thou canst not ready that!", "<name>'s hands are full.",
  "<slot> is already worn."
- The **character status view** (click a roster member) lists that member's readied items;
  pressing the item's digit UNREADIES it back to the pack.
- Readying consumes one from the pooled stack (qty-1, row removed at 0); unreadying returns one.

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

## Status
VERIFIED 2026-09-20 — engine/equip.c (22 host asserts, `make equiptest`); U6WQ ships from the save
(18 actors / 68 items on the canonical save; folds match the old U6WL values); READY.REQ modes 1-3
validated on emulated TT (fold prints + the status-view gear list + digit unready). Save
persistence of ready CHANGES deferred (§6).
