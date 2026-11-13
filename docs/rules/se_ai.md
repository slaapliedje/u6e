# Savage Empire: what creatures and companions do on their turns

Games: Savage Empire. U6's turn order and worktypes are docs/rules/combat.md §8b; SE keeps the turn
order and most worktypes and changes the ones below. The orchid heads (worktype 13) are se_eggs.md §6.
Implemented by: /render gamedemo.c (gl_se `wt_act` = `se_wt_act`; the combat modes `cmode_cycle`/`cmode_name`).

> Provenance
> - Derived from : SE GAME.EXE (DOS), read in the RE workspace outside this repo: the worktype switch 0x17211,
>   assault 0x1c86d, ranged 0x1cba9, wild 0x1ce85, flee 0x1d1ca, 0xa 0x1d33e, 0xb 0x1d470, 0xe 0x1d5a9,
>   wander 0x16de6, the post 0x16a20, the foe finder 0x1c491, the weapon choices 0x1c377 and 0x1ca68, the
>   area target 0x1c661, the distance 0x1be13 (table DS:0xde6; for creatures not of normal size the box round the head, se_big.md §8), the combat-mode cycle in 0x6fb1 and its names
>   (DS:0x8a5) drawn by 0xe29e.
> - References   : none.
> - Tools        : objdump (i8086); our senames/objname dumpers.
> - Validated by  : SEUSE self-test wants "SE ai ..." (§9); NOT yet compared with DOS running.
> - Confidence   : probable (read from the code)
> - Last updated : 2026-11-12

## 1. The switch (0x17211)

An actor whose object status has 0x80 (wild: Motazz's frenzy, se_invoke.md §4) always fights WILD (§4). Otherwise its worktype picks:

| worktype | SE does |
|---|---|
| 4 RANGE | §3 |
| 6 WILD | §4 |
| 7, 9, 0x13, 0x9a | FLEE, §5 |
| 8, 0x9b CLOSE | §2 (0x9b exactly as 8: no neutral rule) |
| 0xa | as U6, but the walk is to the leader's tile, not the party's centre |
| 0xb | as U6: a member within 2, 1 in 8 it turns to 8 and fights (§2) |
| 0xc, 0x8f | wander, as U6 (a party member not on the leader's tile walks to it 1 in 4) |
| 0xd | the orchid head (se_eggs.md §6) |
| 0xe | §6 |
| 0xf, 0x10, 0x8b-0x8e | the dance, §7 |
| 0x82-0x85, 0x90, 0x94, 0x96, 0x99 | as U6 (the walked path, the post, the meeting, the brawl) |
| anything else (3, 5, 0x12, 0x97, 0x98 among them) | pays 5 |

When the routine spent no points it pays 10, as in U6.

**Combat modes.** A companion's combat mode is its worktype: CMND (2), RANGE (4), WILD (6), FLEE (7), CLOSE (8).
The inventory's mode button steps CMND, RANGE, FLEE, CLOSE and round (3 becomes 4, 5 becomes 7, 9 becomes 2); WILD
is never chosen, it is shown for a wild companion.

**The foe** of an actor is SE's nearest foe (se_eggs.md §6: within 11, by alignment, a tie to the bigger blow).
**The distance** is U6's table (combat.md §6c), the same numbers.

## 2. CLOSE (8, 0x9b; 0x1c86d)

1. Its foe, and the weapon it would use on it (0x1c377: the readied item of most damage whose range reaches the
   foe, else its hands).
2. With a grenade (137, 138) the target becomes the area target for grenades, with a device (240, 241) the area
   target for devices (§8).
3. No target: it wanders.
4. In range: one beside it, or one the line of fire reaches, is struck, paying 10. A blocked line sidesteps: a coin
   picks one of the two tiles at a right angle to the target, and it walks there.
5. Out of range: it walks at the target.

Slip 17: DOS walks to the sidestep tile twice (its code falls into the walk at the target, which is that tile
again); the engine walks once, as RANGE's sidestep does.

## 3. RANGE (4; 0x1cba9)

1. Its foe; its ranged weapon (0x1ca68): among its readied items of range 3 or more, while none found yet
   reaches the foe the longer range wins; once one reaches, the one of more damage wins. None: no ranged weapon.
2. A foe within 2 tiles on both axes: it steps directly away (one walk to the tile opposite the foe).
3. No ranged weapon: `<name> has no ranged weapons!` on its own line, and nothing else.
4. More than 5 east-west or 4 north-south from the leader: it walks to the leader.
5. Otherwise as CLOSE from step 2 with that weapon (the area targets, wander with no target, strike, sidestep once,
   walk at it).

## 4. WILD (6, and every wild actor; 0x1ce85)

U6's BERSERK: the living, seen actor within 8 with the highest strength + dexterity + intelligence (the first
found on a tie), by alignment (good: only the evil; evil: only the good; chaotic: neither chaotic nor neutral;
neutral: anyone). Its weapon as CLOSE, the area targets as CLOSE. In range and clear: strike and pay 10; blocked:
a sidestep; out of range: it walks at the target. A failed walk gives its points back and it fights as CLOSE.
No target: it fights as CLOSE. It always pays 5 more.

## 5. FLEE (7, 9, 0x13, 0x9a; 0x1d1ca)

1. More than 7 from the leader: its turn ends (points to 0).
2. Its foe; none: it wanders.
3. A foe within 7 on both axes: it walks directly away; a failed walk gives its points back (it never fights).
4. Otherwise a good one walks to the leader, anyone else wanders.
5. Not in the party (actor status 0x80): 1 in 4 it heals 1 if below its maximum, and once its hit points are at
   least a quarter of the maximum it takes back its saved worktype.

## 6. Worktype 0xe (0x1d5a9)

A neutral one pays 5. With a grenade or device it throws at the area target (§8). Otherwise 16 tries at a tile
up to 4 away on each axis: a seen actor there of another, not neutral, alignment is struck, paying 10; none, it
pays 5.

Slip 18: DOS looks its target up before choosing the tile (with coordinates not yet set), and then strikes the
chosen tile whenever that earlier target counts, so it mostly swings at empty ground. The engine strikes only an
actor standing on the chosen tile, as U6 does.

## 7. The dance (0xf, 0x10, 0x8b-0x8e)

It does not move: it takes the dancer's look (329) with a new frame each turn, 0-3 for the people (310-326),
4-7 for anyone else, and keeps its own look under it; another worktype gives its look back.

Slip 19: DOS adds the new frame to the frame it already shows instead of replacing it, so the frames run on past
the dancer's eight; the engine shows the new frame.

## 8. The area target (0x1c661)

Over the 11 x 9 tiles around the leader, each tile with a living actor is scored by the actors in the 3 x 3
around it: for grenades a good, living actor or the thrower itself there rules the tile out, and every evil or
chaotic living actor counts 1 (2 on the middle tile); for devices nothing rules a tile out and only the myrmidex
(323, 328) count. The best score wins, a tie by a coin (a tile scoring 0 can win). The target is the actor on
that tile.

## 9. Test hook

SEUSE self-test: `SE ai close strikes`, `SE ai close walks`, `SE ai range steps away`, `SE ai range none`,
`SE ai flee runs`, `SE ai dance`, `SE ai modes`.

## 10. Not built / open
- The black staff (21) set to quality 1 against the myrmidex in each weapon choice (se_combat.md has its blow).
- Where SE's objlist keeps the combat modes (item 10).

## Status
IN PROGRESS: §1-§8 built (slips 17-19 as the intent).
