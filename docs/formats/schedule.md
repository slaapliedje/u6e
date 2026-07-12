# Format: NPC schedules (SCHEDULE)

Games: U6 / Savage / Martian — verify per game (U6 decoded here)
Implemented by: /dataformats (u6actor.c/.h), /tools (npcsched)
Tracked in: /docs/INDEX.md  ·  Milestone: M6

> Provenance
> - Derived from : refdata/{dos,st}/u6/SCHEDULE (3329 B) decoded; entries cross-
>                  checked against known NPCs (Lord British's throne-room routine,
>                  shopkeepers at their shops by day / home by night).
> - References   : Nuvie ActorManager::loadActorSchedules + Actor::loadSchedule
>                  READ to understand the layout — implemented from this spec.
> - Tools        : /tools/npcsched (decodes + places all NPCs by game hour).
> - Validated by  : 157/256 actors place on the surface at noon; named NPCs'
>                  routines read sensibly (e.g. NPC 5 Lord British 08:00→throne
>                  area, 12:00→(316,367), ...; NPC 16 Gwenneth 09:00 shop / 19:00
>                  home). Starting party members (Dupre/Shamino/Iolo) have 0
>                  entries — correct, they follow the Avatar.
> - Confidence   : confirmed
> - Last updated : 2026-06-24

## 1. Container layout
A single file, all multi-byte values **little-endian**:
| Offset | Size            | Field                                              |
|--------|-----------------|----------------------------------------------------|
| 0x000  | 256 × u16       | `offset[npc]` — index of NPC npc's first entry     |
| 0x200  | u16             | `total` — number of schedule entries in the file   |
| 0x202  | total × 5 bytes | the schedule entries                               |

NPC `i`'s entries are the slice `[offset[i] .. offset[i+1])` of the entry array
(the last NPC, or any whose next offset is out of range, runs to `total`). An
`offset[i] > total-1` means that NPC has no schedule.

## 2. Schedule entry (5 bytes)
| Byte | Bits | Field                                                      |
|------|------|------------------------------------------------------------|
| 0    | 0–4  | **hour** (0–23)                                            |
| 0    | 5–7  | **day_of_week** (0 = any day, else 1–7)                    |
| 1    | all  | **worktype** (activity code: sleep/eat/work/wander/…)      |
| 2    | all  | x low 8 bits                                               |
| 3    | 0–1  | x high 2 bits  → **x** = byte2 \| ((byte3 & 3) << 8)   (0–1023) |
| 3    | 2–7  | y low 6 bits                                               |
| 4    | 0–3  | y high 4 bits  → **y** = ((byte3 & 0xfc)>>2) \| ((byte4 & 0xf)<<6) |
| 4    | 4–7  | **z** (level; 0 = surface)                                 |

(The x/y/z packing is identical to the OBJLIST position table — see objlist.md.)

## 3. Using it
Entries for an NPC are time-ordered. The NPC's location at game hour H is the
entry with the largest `hour <= H` (wrapping past midnight to the previous day's
last entry). `worktype` then drives behaviour at that spot (stand/work/sleep/…).
Day-of-week refinement (0 = any) is available but not yet applied.

### 3a. Worktype codes (U6 — activity classes)
Derived from the OBSERVED distribution across all 563 SCHEDULE entries (the codes
seen are `0x87`–`0x9a`), correlated with hour-of-day and named NPCs. No Nuvie
source was on hand to match, so only the sleep class is treated as confirmed.

| Code(s)        | Activity | Confidence | Evidence                                    |
|----------------|----------|------------|---------------------------------------------|
| `0x91`         | **sleep**  | confirmed | 167 entries, ~all at night (20h:109, 00h:33, 16h:24; ≈0 daytime). Every named NPC's home/night entry uses it (Lord British 21:00, Gwenneth 19:00 home, Jaana 23:00). |
| `0x90`         | **wander** | probable  | 120 entries, daytime "at this location" code used by townsfolk at/near home (Andrea 09:00, Jaana 12:00) but NOT by posted NPCs at their posts. Modelled as a small roam around the anchor. |
| others (`0x8f`,`0x92`,`0x93`,…) | **work** (stand at post) | default   | Posted/fixed behaviours: Lord British on the throne (`0x93`@316,367), Gwenneth at her shop counter (`0x92`). Walk to the spot and stand until per-worktype poses (sit/eat/sleep-in-bed) are added. |

The 0x91→sleep and 0x90→wander bands are what the engine's `wt_activity()`
dispatch implements (render/gamedemo.c). Confirm/refine against the original game
or Nuvie's worktype enum when available (§7).

## 4. Per-game deltas
Verify — SE/MD use the same container; worktype codes differ per game.

## 5. Validation method
`make npcsched` decodes SCHEDULE + OBJLIST, prints several NPCs' routines (names
from CONVERSE.A), places all NPCs at a chosen hour, and renders the busiest town.

## 6. NPC schedule-walking (implemented — Stage 1)
NPCs no longer teleport on the hour: each game turn (the clock advances per avatar
move) every NPC takes ONE tile step toward its scheduled (x,y) for the current
hour, turning to FACE the step. Frame layout (from Nuvie U6Actor, read for
understanding): displayed tile = `basetile(obj_n) + direction*tiles_per_direction
(+ walk_frame)`, direction **N=0 E=1 S=2 W=3**; tiles_per_direction is derived in
the engine from the BASETILE span (16 frames → 4 dir humanoid, 8 → 2; other counts
keep the saved sprite). Stepping is greedy (dominant axis first), terrain-passable,
and avoids the avatar / other NPCs. Implemented in render/gamedemo.c
(init_npcs/npc_dest/step_npcs/npc_tile), turn-based like the original.
- Stage 2 (DONE): walk-frame GAIT — while moving, NPCs cycle the two walk frames
  (per-direction frames 1,2 for 4-frame humanoids; the 2 frames for 8-frame actors);
  frame 0 (standing) only when stopped. A separate g_amoved flag distinguishes
  "stepped this turn" from the gait phase. render/gamedemo npc_tile().
- Stage 3a (DONE): real passability from TILEFLAG tileflags1 — terrain table +
  an object-blocking bitmap so NPCs/player no longer clip through wall/furniture
  objects. (docs/rules/movement_turns.md, tileflag.md.)
- Stage 3b (DONE): bounded-BFS PATHFINDING (npc_pathstep) so NPCs route AROUND
  walls (33x33 window, returns the first step of the shortest route to the target,
  Avatar treated as blocked) instead of getting stuck; PARTY-FOLLOW — party members
  head toward the Avatar (stop alongside) via the same pathfinder, seeded beside him
  if the save gave no position. render/gamedemo step_npcs/npc_pathstep/is_party_member.
- Stage 3c WORKTYPE ACTIVITIES (DONE): the active entry's worktype drives behaviour
  via `wt_activity()` (§3a). WANDER (0x90) NPCs roam within a Chebyshev radius of their
  scheduled anchor (deterministic per clock+NPC, so headless runs reproduce); SLEEP
  (0x91) and all other codes walk to the spot and stand at post. Validated headless
  (WANDER.BIN): Jaana (62, worktype 0x90 at noon) roams (232,151)→(232,149)→(233,149)
  within radius 2; Lord British (5, worktype 0x93) stays fixed on the throne (316,367).
- Stage 3c REMAINDER (TODO): day-of-week schedule selection (needs a game-day counter —
  the clock currently wraps at 24 h); directional tileflags1 bits 4-7; longer-range
  pathfinding (targets beyond the 16-tile BFS window); per-worktype POSES (sit/eat/
  sleep-in-bed sprites) so sleep/sit read visually distinct from stand.

## 7. Open questions / next
- Full worktype code table: only 0x91=sleep is confirmed and 0x90=wander is probable
  (§3a). The daytime codes 0x8f/0x92/0x93/0x89/0x94… (eat/sit/guard/work variants)
  are not individually resolved — needs original-game observation or Nuvie's enum.
- day_of_week handling (field decoded, not yet applied — needs a day-of-week clock).

## 8. Status
VERIFIED (container + 5-byte entry). NPC schedule-WALKING implemented (Stages 1+2):
NPCs step one tile/turn toward their scheduled spot, FACE the move, and cycle WALK
frames (gait) while moving. Validated headless via NPC 17's midnight commute
((330,432)→(334,437) at hour 0): captured turns 74/75/76 show the NPC walking across
the path with its pose changing per step, cleanly rendered. (NB: the castle is a poor
demo — its NPCs are at-post all day, so an earlier "movement" reading there was just
fire colour-cycling; pick a commuter NPC/town to see walking.)
