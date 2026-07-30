# SE / MD well-known object numbers (obj_n → identity)

Games: The Savage Empire (`savage`) and Martian Dreams (`martian`). Ultima VI's obj_n meanings do
NOT carry over — SE/MD renumber objects — so shared engine code reads per-game obj_n from
`GameDef.wko` (`engine/ruleset.h`) instead of U6 literals. This spec records the survey that fills it.

> Provenance
> - Derived from : the user's own SE data `refdata/dos/savage/SAVAGE/{BASETILE,LOOK.LZC}` and MD
>                  `refdata/dos/martian/MARTIAN/{BASETILE,LOOK.LZC}`, decoded with our own tools.
> - References   : none consulted for the mapping (derived from the primary data files). SE Avatar
>                  obj_n cross-checked against the captured objlist (memory se-actor-capture).
> - Tools        : `tools/objsurvey.c` (`make objsurvey-se` / `objsurvey-md`) = BASETILE (obj_n→tile,
>                  1024×u16 LE) composed with LOOK.LZC (`.LZC`→run-fill tile→name, see look.md).
> - Validated by : all 1024 obj_n resolve to a name; obj_n 310 = "modern man" == the SE Avatar the
>                  captured objlist reports (avatar=obj_n310); neighbours are the SE cast (professor
>                  311, reporter 312, shaman 314, chieftain 315).
> - Confidence   : SE = confirmed; MD = probable (avatar unconfirmed — no MD objlist captured yet).
> - Last updated : 2026-07-29

## Method
`obj_n → base tile` (BASETILE) then `tile → LOOK name` (LOOK.LZC run-fill). Re-run any time:
`make objsurvey-se` / `make objsurvey-md`, optionally `build/objsurvey <dir> <se|md> <keyword>`.

## Savage Empire — well-known obj_n (CONFIRMED)

| concept        | obj_n | LOOK name       | notes / GameDef.wko |
|----------------|------:|-----------------|---------------------|
| **Avatar**     |   310 | modern man      | `wko.avatar` (confirmed vs captured objlist) |
| **key**        |    53 | key             | `wko.key` |
| lockpick       |     — | (none)          | SE has no lockpick → `wko.lockpick = 0` |
| spellbook      |     — | (none)          | SE has no U6 magic → `wko.spellbook = 0` |
| reagents       |     — | (none)          | no U6 magic → `wko.reagent_base = 0` |
| gold           |    69 | gold            | no `wko` field (currency ≠ an obj_n check); nugget = 70 |

SE cast (context): 311 professor, 312 reporter, 313 woman, 314 shaman, 315 chieftain, 316/320 man.

### SE USE (behind the g_gd seam → `se_use_object` in gamedemo.c). Not `wko` (per-item behaviour).
- **containers → SEARCH/spill** (IMPLEMENTED, `se_is_container`): 183 basket, 184 pot, 188 crate,
  132 soft clay pot, 133 fired clay pot. USE spills the container's children onto the map (the shared,
  U6-proven `spill_container`). Validated: `SEUSE.BIN` self-test — a parked child spills alive.
- **food → EAT** (IMPLEMENTED, `se_is_food`): 103/110 meat, 197 fruit, 114 bunch of turnips. USE
  consumes one + feeds the Avatar (shared hunger model). Validated: `SEUSE.BIN` — qty 2→1→gone.
- **doors / doorways → OPEN/CLOSE** (IMPLEMENTED, data-driven — no per-object table). SE door FRAMES
  are per-object and share tile ranges with OTHER objects (`objsurvey ... frames`: 161's frames read as
  fountain/cave; 234 open 0/2 closed 1/3; 270 open 2 closed 0 with "table" tiles interleaved; 275 open 3;
  161/226/236/272 have NO open frame), so there is no uniform frame delta. Instead the toggle is derived
  from **passability**: an OPEN door tile is passable, a CLOSED one impassable (tileflags1, now shipped
  to the core pack via **CTF1**). USE flips to the NEAREST tile that is still "door"-named AND has the
  opposite passability (name-match skips the interleaved table/window tiles; tie-break keeps orientation).
  Doors with no opposite-passability door tile (161/226/236/272 arches, 160 trap door) → "won't budge"
  (safe). Validated: `SEUSE.BIN` — door 977 (closed) → USE → 976 (open, passable).
- **CTF1** also gives SE/MD **object passability in general** (walls/furniture/closed doors block) — it
  was entirely absent before (g_tflags1 was zeroed for core packs); the Avatar keeps 7/8 walkable
  neighbours at the SE start, so it's not boxed in.
- water 140/152 excluded from food (terrain water, not a gettable item).

## Martian Dreams — well-known obj_n (PROBABLE — not yet confirmed)
- **Avatar**: obj_n **318 / 319** both LOOK as "yourself" (MD's generic player marker; MD starts by
  picking one of several characters — Spector, Nellie Bly, …). UNCONFIRMED which frame/obj_n the
  engine uses as the player — no MD objlist captured yet — so `gamedef_md.wko.avatar` stays 0 until
  an MD save capture (à la SE) confirms it. Do NOT fill a guessed value (a wrong avatar draws the
  wrong tile at view centre).
- **currency**: 24 "dollar" (Victorian era; "piece of dirt money" = 303).
- MD items are Victorian (1 cap, 2 cowboy hat, 3 pith helmet, 4 military helmet, …) — a different
  numbering from both U6 and SE, confirming per-game renumbering.

## Consumers
- `engine/world.c` — `gamedef_se.wko` / `gamedef_md.wko`.
- `render/gamedemo.c` — `g_wko` (avatar draw, USE key/lockpick, magic seeding) + the `g_gd` USE seam.
- Future: SE/MD USE usecode (doors/containers/food) behind the per-game seam; SE inventory/get (gold 69).
