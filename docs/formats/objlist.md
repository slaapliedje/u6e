# Format: Actor table (OBJLIST savegame)

Games: U6 / Savage / Martian — verify per game (U6 decoded here)
Implemented by: /dataformats (u6actor.c/.h), /tools (npcsched)
Tracked in: /docs/INDEX.md  ·  Milestone: M6

> Provenance
> - Derived from : refdata/st/u6/SAVEGAME/OBJLIST (7539 B) decoded for the 256
>                  actor records (position + appearance).
> - References   : Nuvie ActorManager::load + PortraitU6::load READ to understand
>                  the fixed-offset arrays — implemented from this spec.
> - Tools        : /tools/npcsched (positions + obj_n/frame); /tools/statprobe
>                  (per-actor stat arrays, derived from a ScummVM save).
> - Validated by  : 256 actors decode; positions land NPCs in towns and combine
>                  with SCHEDULE to place them sensibly; obj_n/frame give each
>                  NPC's on-map tile (rendered in build/npcs.png). Stat arrays:
>                  HP@0xe00 for the party reads 90,90,90,90 — matching ScummVM's
>                  on-screen roster — and 255 for Lord British.
> - Confidence   : confirmed (position, appearance, HP, str/dex/int, party flag)
> - Last updated : 2026-06-25

## 1. What it is
OBJLIST is the savegame's **actor table**: a fixed-layout set of parallel arrays,
one slot per actor, indexed by actor number (0..255). Actor number == the NPC's
conversation index (see converse.md) and == portrait `actor_num` (portraits.md).
The game ships an initial OBJLIST as the starting state.

**ENDIANNESS (important):** the DOS save is little-endian, but the **Atari ST save
is BIG-ENDIAN** — the ST executable wrote its multi-byte fields big-endian. We
currently use the ST `SAVEGAME/OBJLIST` (the DOS data ships no SAVEGAME), so the
appearance word must be read big-endian; reading it little-endian yields garbage
obj_ns (every actor drew the wrong tile — bows/swords instead of figures). Verified:
big-endian gives actor 1 = obj_n 410 (the Avatar), 2/3/4 = human companions. The
loader must key endianness off the save's source (DOS vs ST).

Arrays we use (others exist for inventory/work flags):

| Offset | Per-slot | Field                                                   |
|--------|----------|---------------------------------------------------------|
| 0x100  | 3 bytes  | **position** (packed x/y/z, see below)                  |
| 0x400  | 2 bytes  | **appearance**: obj_n (10-bit) + frame (6-bit)          |
| 0x800  | 1 byte   | **status flags**: bit 0x80 ⇒ **in the party**; bits **0x60 = ALIGNMENT** (see below); bit **0x10 = DEAD** (the reference's ACTOR_STATUS_DEAD: a slain NPC, parked at (0,0,0), skipped on load; 2026-10-28) |
| 0x900  | 1 byte   | **strength**                                            |
| 0xa00  | 1 byte   | **dexterity**                                           |
| 0xb00  | 1 byte   | **intelligence**                                        |
| 0xc00  | 2 bytes  | **experience** (LE on DOS/ScummVM, BE on ST)            |
| 0xe00  | 1 byte   | **current hit points** (what the party roster displays) |
| 0x11f1 | 1 byte   | **worktype** (Nuvie actor_manager) = the actor's CURRENT worktype (schedule code or a conversation's WORKTYPE); combat: 0x03 FRONT, 0x07 COMBAT_RETREAT (coward), 0x08 ASSAULT, 0x9b ATTACK_PARTY, 0x99 brawl/attack, 0x9a flee; schedule: 0x90 WORK, 0x91 SLEEP, 0x8f WANDER, … u6e writes it on S)ave and re-applies a saved combat/flee code on load (docs/rules/usecode_vm.md §7) |
| 0x14f1 | 1 byte   | movement points (per-actor)                             |
| 0x17f1 | 1 byte   | per-NPC conversation talk-flags                         |
| 0x19f1 | 1 byte   | movement_flags (bits 0x60 = the "OLD" alignment; the LIVE one is in 0x800) |
| 0x1c17 | 1 byte   | **alcohol** — drinks consumed (wine / mead / ale); above 3 the Avatar staggers ("Hic!"); −1 per game hour (usecode_use.md Food / drink; 2026-10-28) |
| 0x1bf1 | 1 byte   | player **quest flag**  ·  0x1bf9 karma  ·  0x1c5f knows-gargish  ·  0x1c69 combat-mode  ·  0x1c6a solo-member  ·  0x1c71 gender |
| 0x1bf2 | 1 byte   | **rest counter**: hours until R)est may heal the party again (set to 12 by a healing camp, counted down once per game hour; rules/rest.md §4) |
| 0x1bf3 | 6 bytes  | **game time** (year/month/day/hour/min?)  ·  0x1bfa wind dir  ·  0x1c1b moonstone positions |
| 0x1c03 | 16 bytes | **effect timers**, one byte each, counted down per turn (Nuvie GameClock / the U6 script's timer_*): 0 Light spell, 1 Infravision, 13 = **magical storm** (0x1c10; the storm cloak — rules/equipment.md §8), 14 time stop (0x1c11), 15 eclipse (0x1c12); the rest unknown. u6e reads/writes 1 (Infravision), 13 (Negate Magic / the cloak), 14 (Time Stop), 15 (Eclipse) |
| 0x1c13 | 2 bytes  | the **vanished object** (Vanish / Reappear, rules/magic_skills.md §5f): u16 LE; 0 = nothing vanished. The reference writes obj_n + 1023 + frame for a framed object, which its own loader cannot take apart again (it reads every such value as object 1023); u6e stores **obj_n in bits 0-9 and the frame in bits 10-15** — identical for frame 0, unambiguous otherwise |
| 0x1c72 | 1 byte   | avatar's selected portrait number (see portraits.md)    |

### Alignment (status 0x800, bits 0x60) — the authentic hostility source (VERIFIED)
Nuvie's `ActorManager::loadActors` sets each actor's alignment from the status byte:
`alignment = ((status & 0x60) >> 5) + 1` → **1=NEUTRAL, 2=EVIL, 3=GOOD, 4=CHAOTIC**. An actor
is an ENEMY when EVIL or CHAOTIC — equivalently **`status & 0x20` is set** (EVIL=0x20,
CHAOTIC=0x60 both have bit 0x20; NEUTRAL=0x00, GOOD=0x40 do not). Verified on the sample save:
the party reads GOOD (status 0xc0), Lord British / town guards / townsfolk NEUTRAL (0x00), the
three throne gargoyles EVIL (0x20), and — crucially — a **citizen gargoyle elsewhere is NEUTRAL**.
So the **gargoyle "peace" is per-actor alignment**, not a global flag: post-treaty gargoyles carry
NEUTRAL/GOOD alignment in the save, hostile ones carry EVIL. This replaces our earlier
worktype-based hostility heuristic AND the separate `g_peace` toggle (both superseded).

### Gold (VERIFIED — coins in inventory, NOT a party field)
U6 has no party-gold field (Nuvie's `Party::split_gold/gather_gold` are stubs). Gold = **coins,
obj_n `OBJ_U6_GOLD` = 88 (0x58)**, carried in actors' inventories; the amount is
`inventory_count_object(88)` = Σ `get_total_qty` over the inventory (recursing into containers).
The party's spendable gold = Σ over party members. On the sample save: Shamino 95 (30 in a bag +
65 loose) + Iolo 15 = **110**. (Gold nugget = obj_n 89.)

The stat arrays were derived empirically from a ScummVM save (build/svm.objlist via
tools/statprobe): for the party (actors 1–4 = Avatar/Dupre/Shamino/Iolo) the value
at 0xe00 reads 90,90,90,90 — exactly the HP ScummVM shows — and Lord British (actor
5) reads 255; 0x900/0xa00/0xb00 hold sane str/dex/int spreads; and 0x800 is 0xc0 for
exactly those four actors (0 for everyone else). The 1-byte stat arrays (HP, str,
dex, int, status) are **endianness-agnostic** — identical on the ST and DOS saves;
only the multi-byte arrays (position, appearance, experience) need byte-order care.

A run `01 02 03 04` at 0xfe0 looks like a party-order list but its surrounding bytes
are ambiguous; we currently derive party membership from the 0x800 flag instead
(see Open questions).

## 2. Position (3 bytes at 0x100 + slot*3)
| Byte | Bits | Field                                                       |
|------|------|-------------------------------------------------------------|
| 0    | all  | x low 8                                                     |
| 1    | 0–1  | x high 2  → **x** = b0 \| ((b1 & 3) << 8)            (0–1023) |
| 1    | 2–7  | y low 6                                                     |
| 2    | 0–3  | y high 4  → **y** = ((b1 & 0xfc)>>2) \| ((b2 & 0xf)<<6)     |
| 2    | 4–7  | **z** (level; 0 = surface)                                  |

## 3. Appearance (2 bytes at 0x400 + slot*2)
The 16-bit word packs obj_n (10-bit) + frame (6-bit). **DOS (LE):**
`obj_n = b0 | ((b1 & 3) << 8)`, `frame = (b1 & 0xfc) >> 2`. **ST (BE, what we
use):** the bytes are swapped — `obj_n = b1 | ((b0 & 3) << 8)`,
`frame = (b0 & 0xfc) >> 2`. The on-map tile = `BASETILE[obj_n] + frame` (see
tiles.md / objblk.md); `direction = frame/4`. `obj_n == 0` marks an unused slot.

## 3b. Actor inventory + READIED gear (combat CB3b)
Inventory items are NOT in the actor arrays — they are 8-byte OBJECT records (same layout
as OBJBLK, see objblk.md / u6obj.c) living in the save's **actor-inventory chunk** (the FIRST
[u16 count][count*8] chunk of a ScummVM save; see nuvie_save.md). For an object record:
- `status = b[0]`. The **location** is `status & 0x18`: 0x00 on-map, 0x08 in-container,
  0x10 in-inventory (carried), **0x18 = READIED (equipped)**.
- For an in-inventory/readied object, the `x` field holds the **OWNER actor number**, not a
  map coord: `owner = b[1] | ((b[2] & 0x03) << 8)` (actors are 0-255, so effectively b[1]).
  (Confirmed via Nuvie ObjManager: load adds such objects to `get_actor_inventory(obj->x)`;
  save writes `obj->x = holder->actor_num`.)
- `obj_n = b[4] | ((b[5]&0x03)<<8)`, `frame = b[5]>>2`, `quantity = b[6]`, `quality = b[7]`
  (CORRECTED: qty byte first — see objblk.md §2; stackables fold qty16 = (b7<<8)|b6).

So an actor's combat gear (combat.md §4-6): scan the inventory chunk for records with
`(status & 0x18) == 0x18` and that owner; the readied WEAPON = the one with weapon_dmg>0
(its obj_n gives damage + range), and ARMOUR CLASS = Σ armour_ac(obj_n) over readied armour.
We compute these host-side (mkworld) and ship per-actor {weapon obj_n, AC}.

## 4. Per-game deltas
SE/MD: same array scheme, a different `temp_actor` base and extra arrays.

## 5. Validation method
`make npcsched` decodes the table and (with SCHEDULE) places + renders NPCs.

## 6. Open questions / next
- Inventory arrays, obj_flags (0x000), work/combat-mode flags, and the remaining
  1-byte arrays between the decoded ones are not yet mapped.
- **Party order**: the 0x800 flag gives membership but not display order. A run
  `01 02 03 04` at 0xfe0 is a likely party-order list, but the bytes immediately
  after it (`04 05 ...`) don't yet parse cleanly, so order is currently taken in
  ascending actor-number order (matches the sample save). Confirm the 0xfe0
  structure (count? terminator?) before relying on it.
- **HP vs max-HP**: 0xe00 is *current* HP. U6 max-HP = 30 × level and level derives
  from experience (0xc00); we display current HP only. Map the level/max-HP path
  when the status screen needs it.
- The 3-byte position packing on the **ST** save still needs big-endian verification
  (we decode positions from the LE ScummVM save today; see nuvie_save.md).

## 7. Status
VERIFIED for position, appearance, current-HP (0xe00), strength/dex/int
(0x900/0xa00/0xb00) and the in-party flag (0x800). Inventory + remaining flag
arrays TODO.
