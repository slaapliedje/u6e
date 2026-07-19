# LOOK.LZD — object/terrain "look" names (U6)

Games: Ultima VI (`LOOK.LZD`). Savage Empire / Martian Dreams use `LOOK.LZC`
(a 4-byte library whose item 0 is the same payload) — same inner format.

> Provenance
> - Derived from : refdata/dos/u6/LOOK.LZD (4193 bytes) inspected; decompressed
>   via our u6lzw; format understood by READING Nuvie's Look::init/get_description.
> - References   : Nuvie Look.cpp (read, not copied).
> - Tools        : our tools/mkworld (decompress + parse), hex inspection.
> - Validated by  : tile→name lookups render correct names on-target (e.g. the
>   throne-room furniture); spot-checked against the game.
> - Confidence   : confirmed
> - Last updated : 2026-06-24

## Container
- **U6**: whole-file **LZW** (the standard U6 container: 4-byte LE decompressed
  size header, then the bit stream — see lzw.md). Decompress before parsing.
- **Savage / Martian**: `LOOK.LZC`, a single-item [.LZC](lzc.md) container
  (SE item flag 0x02 stored / MD 0xE0 stored) whose item 0 IS the decompressed
  payload below — VERIFIED: SE decodes to authentic SE names (e.g. "atl atl",
  "obsidian knife", "totem of Aphazz", "roast pteranodon"), MD likewise.

## Decompressed payload — a run-filled table keyed by TILE NUMBER
The names are indexed by **tile number** (0..2047), NOT by object number. (An
object's name is therefore the name of its current tile = basetile(obj_n)+frame;
consecutive frames of one object share a name via the run-fill below.)

The payload is a sequence of entries, each:

| size | field  | meaning                                   |
|------|--------|-------------------------------------------|
| 2    | index  | u16 LE — the tile number this string is for |
| n    | string | ASCII                                     |
| 1    | NUL    | terminator                                |

Parse loop (mirrors the original):
- Read `index`. If `index >= 2048`, stop.
- The string applies to `index`, AND fills every still-unset tile from the
  previous index up to `index` (a run). So one string can name a contiguous
  block of tiles.
- After the last entry, any remaining tiles get tile 0's string ("nothing").

## Singular / plural markers
A string may encode both number forms inline:
- `\` — the following run of alphabetic chars belongs to the **plural** form only.
- `/` — the following run of alphabetic chars belongs to the **singular** form only.
- All other chars are common to both.

Examples: `torch\es` → singular "torch", plural "torches"; `loa/f\ves` →
"loaf" / "loaves". To render one form, walk the string copying common chars,
including `/`-run chars only when singular and `\`-run chars only when plural.

## Use in the engine
- /tools (mkworld) decompresses LOOK.LZD/`.LZC` and ships the raw payload in the
  world pack; the runtime builds a `char *name[2048]` table by the parse above and
  formats the singular form for Look / inventory. (render/gamedemo)
- U6 full pack: in the "object verbs" section. SE/MD CORE pack: in the optional
  magic-guarded `CLOK` section `[4 "CLOK"][u32 look_len][look][gettable 1024]`
  (docs/design/multigame.md P2). Same parse; host-validated by tools/coredump,
  which labels the rendered village objects with these names.
- The same lookup names terrain (tiles 0..511) and objects (512..2047), so the
  Look verb works on everything under/around the avatar.

## Status: VERIFIED
