# Transfer a Character: Ultima IV and V saved games

What the original's "Transfer a Character" reads and how it turns an earlier Avatar into a U6
one. Our implementation is `engine/transfer.c`; the screens are in `render/gamedemo.c`.

> Provenance
> - Derived from : the original front-ends (DOS U.EXE, ST U.PRG), read for their behaviour, and
>   real ST files: SAVED.GAM from the Ultima V Play disk, and a ROST.BIN written by Ultima IV for
>   a new character. No real DOS save has been tried; those two layouts rest on U.EXE alone.
> - Scope        : U6 only.
> - Last updated : 2026-11-03

## 1. Files

The DOS original asks for a drive letter and tries these, in order; the ST original asks for
Ultima IV or V and opens one path on drive A.

| Game | Machine | Paths |
|---|---|---|
| IV | DOS | `\PARTY.SAV`, `\ULTIMA\PARTY.SAV` |
| V | DOS | `\SAVED.GAM`, `\ULTIMA\SAVED.GAM` |
| IV | ST | `\ULTMAPP\ROST.BIN` |
| V | ST | `\PLAYDISK\SAVED.GAM` |

Only the start of each file matters. Words are little-endian in DOS files and big-endian in ST
files. A name is a NUL-terminated string of which only the first 8 bytes are used.

**ROST.BIN holds its numbers in BCD**: Int 27 is the byte 0x27, 200 HP is the word 0x0200. The ST
original reads them as plain binary, so it sees Int 39, HP 512 and level 5 for a new level 2 Mage,
and rejects any character with 2710 experience or more. We decode the BCD, which gives the result
the DOS original gets for the same character. A ROST.BIN byte that isn't BCD means no character.
The ST Ultima V file is plain binary.

## 2. Records (the first character)

| Field | IV DOS (`PARTY.SAV`) | IV ST (`ROST.BIN`) | V, both (`SAVED.GAM`) |
|---|---|---|---|
| name | +0x1C | +0x00 | +0x02 |
| sex | +0x2C, male = 0x0B | +0x10, male = 0x5C, female = 0x7B | +0x0B, male = 0x0B |
| class | +0x2D, 0..7 | +0x11, 0..7 | none: always Avatar |
| Str | +0x0E word | +0x13 byte | +0x0E byte |
| Dex | +0x10 word | +0x14 byte | +0x0F byte |
| Int | +0x12 word | +0x15 byte | +0x10 byte |
| magic | +0x14 word | +0x16 byte | +0x11 byte |
| HP | +0x08 word | +0x18 word | +0x12 word |
| max HP | +0x0A word | +0x1A word | +0x14 word |
| experience | +0x0C word | +0x1C word | +0x16 word |
| level | max HP / 100 | max HP / 100 | +0x18 byte |
| karma | +0x14E, 8 words | +0x100, 8 bytes | none |

- Classes are Mage, Bard, Fighter, Druid, Tinker, Paladin, Ranger, Shepherd. Ultima IV karma of
  zero in all eight virtues means the character became an Avatar, shown as class Avatar.
- The DOS original rejects a record whose stats are out of range: Str, Dex, Int over 99 (IV) or
  over 30 (V); HP, max HP or experience over 9999; class over 7 (IV). The rejection reads "No
  character found!".
- SAVED.GAM has no marker for its machine. We read it both ways and keep the one that passes the
  checks. If both pass, the one whose max HP is 30 x level wins (in Ultima V it always is, and
  the stock party shows it), then little-endian.

## 3. Conversion

Ultima V's Str, Dex and Int carry over unchanged. Ultima IV's run to 99, so they are scaled:

| Old value v | New value |
|---|---|
| v < 10 | v |
| 10 <= v < 30 | 10 + (v - 9) / 2 |
| v >= 30 | 20 + (v - 30) / 4 |

For both games, using integer division:

- experience = old experience / 10
- level starts at 1. Take k = old experience / 1000, and while k > 0 add 1 to the level and halve k.
- HP = max HP = level x 30, capped at 255
- magic shown on the screen = Int. What goes into the game is the usual 2 x Int.

The character then becomes the Avatar: class Avatar, name and sex from the file. Sex can still be
changed and a portrait chosen on the review screen. Nothing else transfers: no gold, items or
karma.

## 4. Screens

1. Prompt, title TRANSFER A CHARACTER, on the parchment.
2. Review, on the parchment: columns "Ultima IV" or "Ultima V" against "Ultima VI", rows Name,
   Sex, Class, Str, Dex, Int, Magic, Exp, Level. Live portrait at (195,118). Bottom row of
   buttons at y = 174: Sex (x 28), Portrait (x 95), Continue (x 154), Exit (x 221).
3. Continue returns to the title menu with the character kept. No gypsy, and no sacrifice opening:
   those are the ceremony of Create a Character. Journey Onward then starts the game with them.

The original refuses to create or transfer over a game in progress. We don't, in either case: the
new character only replaces a saved game when the player saves.

## 5. Differences from the original

Listed in [deviations.md](../rules/deviations.md):

- Any drive and any of the file locations work, whichever machine the save came from. Return uses
  the game's own folder, so a save can simply be copied in (SAVED.GAM, PARTY.SAV or ROST.BIN).
- Starting Create a Character after a transfer drops the transferred character.
- ROST.BIN is decoded as BCD (section 1), where the ST original misreads it.
