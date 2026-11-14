# Savage Empire: R)est, the meal and the dream

Games: Savage Empire. U6's camp (rest.md) is replaced as a whole: no campfire, no guard, no one comes in the
night, a meal that heals at once, and a dream that raises a level.
Implemented by: /render gamedemo.c (`se_rest`, `se_rest_tick`, `se_dream`, gl_se `.rest`/`.rest_tick`).

> Provenance
> - Derived from : SE GAME.EXE (DOS, Turbo C++), read in the RE workspace outside this repo: R)est 0x2a28b, its
>   tests 0x29e42 (foes) and 0x29ecb (noise, with the sight test 0x18da4), the meal's food finder 0x29e01/0x29da7
>   (list DS:0x286c), the level and dream 0x29f78 (table copied from DS:0x28a4), the remains routine 0x18764
>   (se_combat.md §3i), the maximum HP 0x1872a, the rest counter's hourly tick 0x4c51.
> - References   : none (ScummVM's SE rest is unwritten).
> - Tools        : objdump (i8086) on the unpacked image; our objname dumper.
> - Validated by  : SEUSE self-test wants "SE rest ..." (§7); NOT yet compared with DOS running.
> - Confidence   : probable (read from the code, not observed)
> - Last updated : 2026-11-13

## 1. Refusals (in this order)
The scroll shows `Rest`, then a new line and the reason:

| condition | message |
|---|---|
| combat mode is on | `Not in combat mode!` |
| a foe near: an actor 16-255 with an object, evil or chaotic (status bit 0x20), on the party's level, no more than 39 tiles from the party on each axis | `Not while foes are near!` |
| noise: an actor 1-255 not dead (0x10), not asleep (0x04), not of the party (0x80), seen (the sight test), on the party's level, no more than 5 tiles off on each axis | `It's too noisy to sleep here!` |
| solo mode | `Not in solo mode!` |
| aboard the raft (the leader's slot is past the party, se_use.md §5w) | `Not while aboard ship!` |
| any member a rider on a turtle (307) | `-Not on a turtle!` (on the `Rest` line itself, no new line) |

There is no test for towns, room, horses or others in general: the noise test stands for those.

## 2. How long
`Rest until sunrise? ` when the hour is before 7 or after 17, else `Rest until sunset? `, on a new line. One key:
- `N` or `n`: `N`, then on a new line `How many hours? ` and one key: `1`-`9` is echoed and is the hours;
  anything else prints `0` and the command ends.
- any other key: `Y`; the hours are 7 - hour before 7, 24 - hour + 7 after 17, else 18 - hour (the minutes
  are not counted).

## 3. The camp
The party gathers round the leader's tile as in U6 (rest.md §7): up to 20 rounds in which each member walks a
step to its place by member index (dx 0,0,1,-1,1,-1,1,-1; dy -1,1,0,0,-1,1,1,-1), one that doesn't move turning
to the middle; the rounds end when no one moved. No fire is laid.

## 4. The meal (only when the hours are more than 4)
Each member in party order, by name:
- actor 11: ` does not eat.`
- else one food is eaten, the first in the member's own pack, else the first in each member's pack in party
  order: ` has food.` Then, when the rest counter is 0 and the member's maximum HP (strength x 2 + level x 4,
  1-255) is at least its HP + 5, it gains 5 HP;
- else ` has no food.`

Food here: 61 corn stalk, 62 bean stalk, 93 corn meal, 102 tortilla, 103 meat, 104 berries, 105 cake,
106 dinosaur egg, 107 roast pteranodon, 108 corn, 109 beans, 110 meat, 111 dinosaur egg, 113 bananas,
114 turnips, 120 pepper, 197 fruit, 213 fish, 214 chops (the stalks are food here though U)se doesn't eat them).

If anyone gained, the rest counter becomes 12. It counts down once a game hour, as U6's does. A rest of 4
hours or fewer has no meal and heals no one.

## 5. The night
Every member but actor 11 lies down as its remains (the remains routine, se_combat.md §3i: a body for a person),
and a blanket (246) in frame 1 is laid on its tile. Then there are hours x 3 steps. Each step:
1. the clock moves 20 minutes and the world takes its turn (time-pass wear-offs, creatures, schedules);
2. when hours x 3 is more than the number in the party, and the step's number is below it, the member with
   that index has its level test (§6).

No one comes in the night and nothing breaks the rest.

## 6. The level and the dream (0x29f78)
A member levels up when its experience is at least the table's entry for its level:

| level | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| experience needed | 0 | 100 | 200 | 400 | 800 | 1600 | 3200 | 6400 | 32767 |

Experience stops at 9999, so level 8 is the last. One level per test at most. On a level up, the level goes up
by one and the member dreams. The text (with `He`, or `She` for actor 2, as the pronoun):

    <name> has a dream...

    He chases three animals through the jungle: a bear, a jaguar and a monkey.

    The path branches, each animal going a separate way.

    Does <name> follow the @bear, the @jaguar or the @monkey?
    Your choice:

(`@` marks a keyword drawn in the keyword colour.) A line of up to 12 characters is typed, then Enter. Its first
letter decides: `B`/`b` the bear, strength; `J`/`j` the jaguar, dexterity; `M`/`m` the monkey, intelligence; else
the member's highest of the three (strength when it is more than both others; else dexterity when it is more
than intelligence; else intelligence). The stat rises by 1 unless it is already 30. HP don't change (the maximum
rises with the level, §4). DOS shows the dream in a text window over the map with the dreamer's portrait in
the panel, and the deaths of a scene are silent meanwhile (DS:0xa39).

## 7. Waking
Each member takes its own object back, its blanket on its tile goes, and its movement points become its
dexterity. The party's place becomes the leader's tile and the view is drawn again.

## 8. Test hook
SEUSE self-test: `SE rest foes`, `SE rest noise`, `SE rest turtle`, `SE rest sunrise`, `SE rest hours`,
`SE rest short`, `SE rest meal`, `SE rest counter`, `SE rest blanket`, `SE rest dream`, `SE rest dream default`,
`SE rest wake`.

## 9. The engine
- The dream is printed in the message scroll and its answer typed on the prompt line; the window over the map
  and the portrait are item 8's (the panel and screens).
- The pictures at the top of the border that DOS changes as the party lies down, dreams and wakes (tiles
  0x164-0x165, 0x175-0x176, 0x173-0x174 drawn at x 64 and 80; probable) are item 8's.
- A step of the night takes 1/8 s, as U6's.
- Esc at the dream's prompt counts as an empty line (the highest stat).

## Status
DONE: §1-§7 built. §9's window and border pictures wait for item 8.
