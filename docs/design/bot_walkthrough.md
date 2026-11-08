# The bot plays the game to the end

> Provenance
> - Derived from: the main-quest order in public walkthroughs (the Ultima Codex walkthrough and
>   transcript, StrategyWiki, Wyvern's walkthrough; plot facts only), checked against our own data
>   (the object lists in LZOBJBLK/LZDNGBLK, the conversation scripts) and our rule specs.
> - Last updated: 2026-11-07 (start to all eight moonstones played; Gargish next)

## 1. Why
The release needs proof that the whole game can be finished. The bot (tools/bot) plays with real
keys, as a player would; today it covers the opening hours (the castle to the first shrine). This
extends it to the ending. Where it fails, the fault is found and fixed in the game first.

## 2. Checkpoints
A full run takes hours, so the game is played in chapters. A chapter ends with S)ave to slot 1;
the host then copies `SAVED.000` and `SAVES.DAT` from the drive folder to `<out>/ckpt/<name>/`.
`run.py <drive> <out> --from <name> <segments...>` copies a checkpoint into the drive folder before
boot, and after boot restores it (Ctrl-R, slot 1, Enter), so any chapter can be run again alone.
The test stock (MAGIC.REQ: spells and reagents) stays on, as for the other segments.

## 3. The route
In order; each line is a segment. Places are checked in our data unless marked (walkthrough).

| Chapter | What | Our data |
|---|---|---|
| start | castle, bridge, the Orb from Lord British (existing segments) | |
| compassion | Ariana's rune, the Compassion shrine (existing `shrine`) | moonstone fr 1 at (503,359) |
| justice | Yew: Lenora (mantra BEH, letter), Pridgarm (jail key), Boskin (`lie`), the rune under the plant in the Slaughtered Lamb; give the key back | rune 245 at (237,156) |
| honor | Trinsic: Whitsaber (SUMM), the rune from the pedestal | rune 247 at (411,739) |
| spirituality | Skara Brae: Marney (`marn`, `poem`, `bask`), the rune in her chest; mantra OM | rune 248 in a chest at (81,493) |
| sacrifice | Minoc: Tara (CAH); a yew log (Ben, Yew), a board (Aaron), panpipes (Julia), the tune (Gwenno), Selganor | rune 246 from Selganor |
| humility | New Magincia: Conor (LUM), Antonio (`humb`, `conor`) | rune 249 from Antonio |
| valor | Sherry (cheese) joins; Jhelom: Culham (RA); Sherry alone through the mouse hole | rune 244 at (157,843) |
| honesty | Moonglow: Manrel (the crypt key); into the catacombs through the Blue Bottle (below); down to Beyvin's crypt; a glass sword on level 1 (walkthrough) | rune 242 on a body at (219,147) level 2 |
| shrines | the seven other shrines by Orb: the guardians, the rune and mantra at the field, the moonstone | 8 fields (obj 51, quality = virtue) on the 8 moonstones |
| gargish | Hythloth from (948,930) to Captain John on level 4: the scroll; Beh Lem at the exit (220,219) level 5 at noon | |
| gargoyles | Valkadesh; Draxinusom (the Amulet of Submission); Naxatilor (No to destroying the Codex); the Hall of Knowledge (the Book of Ritual, the broken lens); the lensmaker | broken lens 395 at (124,194) level 5 |
| lenses | Ephemerides (955,405): the repaired lens, then a glass sword: both lenses | 394 concave, 396 convex |
| cube | Stonegate: Papa's fish (a fishing pole), the cellar key, the cellar to level 4 | cube 62 at (147,57) level 4 |
| balloon | Sutek's castle and catacombs: the plans on the balloonist; basket, thread, cloth, bag, rope, cauldron; U)se the plans | plans 270 on a body at (179,221) level 4 |
| singularity | the balloon from the Gargoyle Altar north to the Temple (128,46) level 5: `all`; the statues of Control, Passion, Diligence (UN, OR, US); the Temple again: `unorus` | |
| ending | the Orb to the Shrine of the Codex; the lenses on (921,851) and (925,851); the moonstones and the cube; U)se the cube | Codex 59 at (923,851); usecode_use.md (Vortex cube) |

**The Blue Bottle's back room** (worked out in DOS U6, 2026-11-07; the walkthroughs' version doesn't fit the
rules: a thing can't be pushed onto the one pushing it). Shamino picks the steel door (a pick breaks on
d30 < (45-dex)/2, so the best hands). The Avatar alone then: the milk bottle east onto the table; L)ook north
finds the secret door at (941,523), step in; the north-east barrel north onto the ladder's tile; the middle
barrel east; step north to (941,522); open the west barrel; the plant to the north south-west into it; the
barrel on the ladder west into the plant's place; U)se the ladder diagonally (north-east). The Lycaeum's
ladder (891,435) reaches the same level but its room is sealed from above.

Left out at first: the tablet and pirate treasure chain (Mariah, Zoltan, Homer, the nine map
pieces, the Pirate Cave). The walkthroughs show no hard dependency on it; the fan it gives is
replaced by the Wind Change spell. If a conversation later turns out to need it, it is added.

## 4. What the bot found in the game
- A load gave every wounded party member a lower max HP (the save's HP taken as max); a member's max HP
  is now level × 30 on load (save_load.md).
- DOS walks eight ways (Home PgUp End PgDn, the keypad), refuses a diagonal between two blocked tiles,
  and a big object blocks on all its cells; Alt+letter sets the default command. u6e had none of these
  (movement_turns.md §4, tileflag.md, usecode_use.md).
- Saving kept only the template's party bits: a companion who joined (Sherry) fell off the roster at the
  next load, with what they carried (save_load.md).
- A moonstone G)otten merged into a moonstone already carried (same object, same quality), losing its
  phase: the eight never stayed eight. Pack rows now merge only with the same frame or a stackable type.
- A mouse alone (Sherry, solo) couldn't go through mouse holes: the leader always walked as the Avatar.
- G)et, U)se and M)ove act at once on a direction key in DOS (no Enter); u6e walked a crosshair.
- A stationary creature (a hatched tangle vine) struck party members up to four tiles away, bare-handed: the
  Avatar died at 90 HP beside a vine patch. It now strikes only what its weapon reaches (eggs.md §3b).
- Poison that took the Avatar to 0 killed him as a creature: his pack (the Orb, the moonstones, the runes)
  went into a body far away and only then did Lord British recall him (combat.md §3d).
- The recall moved the party to the surface before raising the dead, so a companion who fell in a dungeon
  rose without what its body held: the party lost its armour and weapons in Hythloth (combat.md §3d).
- A script's write to the quest flag (var 0x1A) was kept in a local variable: the Singularity's "Your final
  quest has begun!" never opened the way to the Codex (usecode_vm.md). Found by trying Iolo's cheat menu,
  whose "Set Quest Flag" read back 0; the menu's "Get items" also crashed the host test tool (the VM's game
  record left its host hooks unset). Checked in the game by the `cheat` segment: the menu reads the flag back
  and the way to the Codex then lets the party by.

- U)se of a pack torch always put it in the Avatar's hand, whoever carried it; with sword and shield he had no
  hand free, the dungeon stayed dark, and a spell's crosshair found "nothing" on a companion. The reference lights
  it in the carrier's hand (usecode_use.md). In Hythloth the bot casts Light (sulfurous ash from Rudyom).
- Dispel Field saw only fields made by spells: the protection field in the Hall of Knowledge's door (127,184,
  level 5) stayed, and the Vortex Lens was out of reach. DOS removes a fire, poison, protection or sleep field
  object lying on the tile (magic_skills.md §5a).

## 5. Open
- Strength for the Gargoyle half: a level-3 party dies in Hythloth (drakes cast Fireball). The user's choice
  (2026-11-07): earn it. The `train` chapter hunts at surface eggs of evil creatures (only an evil kill
  gives experience) and meditates at a shrine when a member has the experience for the next level. The sites are
  worked out at the start: the new-game eggs (objfind) of creatures worth experience, reachable on foot within 90
  steps of an Orb gate (a walk over the game's own pass map). What hatches keeps to its egg, so the bot goes to it.
  It camps with a guard: an unguarded camp that is ambushed sleeps on and the Avatar dies in his sleep (rest.md §7).
  First hour: Dupre to level 4, Sherry to 2, about 40 experience per site visit. After 2.5 hours (2026-11-08) the
  Avatar at 7, Dupre 4, Iolo and Shamino 3 was enough: with Light, 30 reagents and fighting rather than sneaking,
  the party reached Captain John and Beh Lem, losing only Sherry.
- U)se on a tile where a party member stands names the member ("Use-a gargoyle") but works the lever under them
  (Stonegate's lever cell). If DOS takes the actor first, as u6_use_at's comment says, it should answer
  `Not usable` and leave the lever. A DOS check (Dupre on the castle lever at (303,383)) didn't load; to settle.
- The party's healing on a long run: the test stock's reagents run out; a restock (Xiao, Nicodemus,
  Horance, Rudyom sell them) or a healer is needed between chapters.

## 6. Done when
`run.py` plays from a new game to "The Codex has vanished!" and the ending with no issue logged
that is the game's fault, and each chapter can be run again from its checkpoint.
