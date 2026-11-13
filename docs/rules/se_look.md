# Savage Empire: L)ook, the search and G)et

Games: Savage Empire. U6's L)ook says what a thing weighs, does and reads; SE's says only what it is, with a few
additions, and searches only containers and bodies. SE's G)et has its own cases (the body searched as it is taken,
the torch into a hand, the egg, the skin, the diamonds, the bulb).
Implemented by: /render gamedemo.c (game layer hooks `look`, `look_pack`, `get`, `counted`: `se_look`,
`se_look_pack`, `se_get`, `se_counted`).

> Provenance
> - Derived from : SE GAME.EXE (DOS, Turbo C++), read in the RE workspace outside this repo: the cursor's thing
>   0x6dc0 with its picker 0x1810:0xd5b and test 0x18da4, L)ook 0x1ee8d, its search 0x1ec8a, the thing under
>   0x1ec00, the count or article 0x1eb7b (count kinds 0xa5d:0x213e, DS table 0x806), the article 0x1eb3c, the
>   name 0xa5d:0x3a76, the containers 0x1e5d2 (DS 0x1122), the count in a container 0xa5d:0x2406, G)et 0x1fbba,
>   the random range 0x2b57:0x1f.
> - References   : none.
> - Tools        : objdump (i8086) on the unpacked image; our string and table dumpers.
> - Validated by  : SEUSE self-test wants "SE look ..." and "SE get ..." (§6); NOT yet compared with DOS running.
> - Confidence   : probable (read from the code, not observed)
> - Last updated : 2026-11-13

## 1. What the cursor names (0x6dc0, 0x1810:0xd5b)

On the map, a tile drawn black (darkness) names nothing: L)ook says `You see darkness.` Otherwise the things on the
tile are taken top first. A thing is *visible* unless it is invisible (status 0x02; members of the party see each
other), hidden (an actor with flag 0x10 in its own table) or *ignored* (tileflags3 bit 0x10 of its tile), and unless
it is on another level. The cursor names the first living actor among the visible things, else the first visible
thing, else the first thing of all. A piece of a big creature (se_big.md §1) names its creature (its quality).

## 2. L)ook (0x1ee8d)

`You see ` and then:
- **Darkness** (§1): `darkness.`
- **An actor:** a party member (status 0x80) by its own name; anyone else with the article of the tile it shows
  (0x1eb3c, tileflags3 bits 0xc0: `a `, `an `, `the `) and a name: actors 0-15 the name of the tile they show, unless
  in the party; actors 16 and up, asleep or dead (status 0x14), the name of their saved object (se_combat.md §3i);
  otherwise the tile's name. The tyrannosaurus (294) wearing the bell (se_use.md §5i) adds ` with a bell around its
  neck`. Then `.` and the portrait view (as U6).
- **Nothing visible, or an ignored or invisible thing chosen:** the map tile, with its article: `a tree.` (Off the map
  window: `nothing.`.) No search.
- **A thing:** its count or article (§4) and name, then:
  - the headless metal statue (247) whose quality is 0: `\nSearching here, you find a piece of paper with notes
    written on it:\n\n` and the note (four lines, a quotation ending `...&`), from any distance, as often as looked;
  - the metal bucket (136; its name by frame already says `of tar` / `of water`) in frame 1: ` holding N pint\s of tar`, frame 2: `... of water`, N the count of tar (139) or
    water (140) inside it;
  - then `.`;
  - the sundial (146) while the hour is 5 to 19: a new line, then `\n\nThe time is ` and `H:MM A.M.` / `12:MM P.M.` /
    `H-12:MM P.M.`;
  - the search (§3) when it is a container (59, 60, 258, 181, 188, 183, 182, 184, 97) or a body (249).

A thing in the pack is looked at the same way. Its stored position is its holder, never beside the party, so a
carried thing is never searched.

## 3. The search (0x1ec8a)

Only beside the leader (one tile either way); only a container or a body (§2); an ignored thing gives way to the thing
under it (0x1ec00: the next thing on the tile, not ignored, not an actor). A crate (188) not in frame 0 (shut) and the
empty jar (97) are not searched. With nothing inside: `\nSearching here, you find nothing.` Else
`\nSearching here, you find `, and each thing inside, first first, goes out onto the container's tile and is named
(count or article, name): `A`, `A and B`, `A, B and C`, then `.`.

## 4. Counts (0xa5d:0x213e, 0x1eb7b)

The 47 kinds in DS table 0x806 (36, 45, 41, 54-56, 72, 70, 73, 74, 102-105, 108-110, 210, 132, 44, 133, 115, 120, 123,
129, 137, 180, 130, 192, 134, 139, 140, 191, 204, 206, 214, 93, 48, 10, 47, 26, 240, 211, 252, 27, 208, 69; 137 is
listed twice) are named with their count, `1` included (a count of 0 is made 1 first); anything else takes its
article. The name takes the plural by the count.

## 5. G)et (0x1fbba)

The leader takes. The cursor's thing (§1) is replaced, when it is an actor or ignored, by the thing under it (§3).
None, or a thing inside something: `nothing!`. Else its count or article and name, a new line, then:
1. Not beside the leader: `Out of range!`.
2. Its weight: a body's own; anything else its own and what it holds. A weight of 0 for a thing that can't be taken,
   or of 255, or an actor: `Not possible.`
3. The leader's load and this over 20 times the leader's strength: `\nThe total is too heavy.`
4. The take costs the leader 3 movement points (0x14ebc: actors 0-15 pay 2 less outside combat), as U6's.
5. By kind:
   - a counted kind (§4): the whole stack to the leader;
   - a body (249): what it holds goes out onto its tile, `\nSearching the body, you find A, B and C.` (or
     `... nothing.`), then the body is taken;
   - a lit torch (209): into the leader's first free hand (`Your hands are full.` with none), readied, quality (fuel)
     100;
   - a dinosaur egg (111): it goes, the leader gets one dinosaur egg (106);
   - a skin (245): it goes, the leader gets one folded skin (92);
   - a pile of diamonds (71): it goes, the leader gets R diamonds (72), R a random 3 to 11, or 10 times the
     leader's room under its limit when that is less;
   - an orchid bulb (78): taken; an egg (230) on its tile whose first thing inside is a carnivorous orchid (292)
     loses it and is deleted (the bulb's egg, se_eggs.md §2);
   - anything else: taken.
   A take the pack refuses: `You are carrying too much already.`

## 6. Test hook

SEUSE self-test: `SE look count`, `SE look bucket`, `SE look sundial`, `SE look statue`, `SE look search`,
`SE look shut crate`, `SE look map`, `SE get body`, `SE get torch`, `SE get egg`, `SE get diamonds`, `SE get range`
(and the older `SE gold` wants for the pooled gold). Not tested by a want: a big creature's piece and a sleeper.

## 7. Slips and engine choices
- Slip 27: actors 0-15 asleep or dead are named by the tile they show (their remains: `a body`), not by their saved
  object as actors 16 and up are. The engine names them by their saved object.
- Slip 28: a lit torch taken by a leader other than the member whose inventory is shown goes to no hand but is still
  marked readied. The engine puts it in the leader's first free hand.
- The statue's note shows from any distance (DOS, kept).
- Engine choice: gold taken goes to the party's pool, as everywhere in the engine (DOS: a counted thing in the pack).
- Engine choice: the dead leave a map object (se_deviations.md), so looking at a dead creature names its remains
  object, not the creature; its things are searched from that object as from a body.
- Not built: L)ook and G)et also turn the leader to face an adjacent target (0x142e4); the engine does not track the
  leader's facing for this.

## Status
DONE: §1-§5 built; §7 open with the user.
