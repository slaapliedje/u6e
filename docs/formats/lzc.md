# Format: LZC container (Savage / Martian `*.LZC` flagged library)

Games: Savage / Martian — SE/MD only (U6 uses [library](library.md) + [lzw](lzw.md))
Implemented by: /dataformats (u6lzc.c/.h), /tools (lzctest)
Tracked in: /docs/INDEX.md  ·  Milestone: M9

> Provenance
> - Derived from : reading the user's own DOS Savage Empire (`SAVAGE/*.LZC`)
>                  and Martian Dreams (`MARTIAN/*.LZC`) files byte-by-byte
>                  (headers, indices, and item bodies) across every `.LZC` in
>                  both games.
> - References   : community "Ultima 6 engine file formats" notes for the
>                  SE/MD library variant (read to UNDERSTAND; not transcribed).
> - Tools        : hexdump of /refdata; scratchpad probes (index tiling, flag
>                  histograms, per-entry LZW-signature correlation); our
>                  /tools lzctest harness decoding with our own u6lzw.
> - Validated by  : lzctest parses every present entry in bounds with the index
>                  tiling to EOF (0 bad spans over all SE+MD `.LZC`), and our
>                  u6lzw decoder expands every compressed-flag item to EXACTLY
>                  its declared size; SE TALK items decode to readable dialogue,
>                  MD TALK items are readable stored text.
> - Confidence   : confirmed
> - Last updated : 2026-07-18

## 1. Overview
`.LZC` is the Savage Empire / Martian Dreams replacement for U6's separate
[library](library.md) + [LZW](lzw.md) files. It is one **flagged library**: a
mandatory 4-byte total-size header, an index of fixed 4-byte entries, then the
item bodies. Each index entry carries a **3-byte offset + 1-byte flag**; the
flag says whether that item is LZW-compressed or stored, and (for stored items)
loosely tags its content type. Compressed items are ordinary [lzw.md]
containers (their own 4-byte declared-size header + LSB-first bit stream).
Stored items are raw bytes parsed by the relevant inner format (often a nested
[library](library.md), or plain text for conversations).

It replaces, per game:
- U6 `CONVERSE.A/.B` (library+LZW)          → SE/MD `TALK.LZC`
- U6 ground/object look strings             → SE/MD `LOOK.LZC`
- U6 `PORTRAIT.A/.B/.Z` (portrait shapes)   → SE `FACES.LZC` / MD `MDFACES.LZC`
- plus SYSTEM, MUSIC, cutscene (MIDGAME*, SCENE*), fonts, etc.

## 2. Container facts
- **Compression: per-entry.** An entry is either LZW-compressed (an embedded
  [lzw.md] container) or stored raw. The 1-byte flag selects which.
- **Endianness:** source is **little-endian**; the header size is LE uint32 and
  each offset is an LE 24-bit value — read with explicit LE→host conversion so
  the big-endian 68000 is correct.
- **Alignment:** none; items are packed back-to-back, index entry *i*'s offset
  is measured from the **start of the file** (byte 0).

## 3. Byte layout

### 3.1 Header + index
| Offset | Size | Type   | Endian | Field        | Meaning                                  |
|--------|------|--------|--------|--------------|------------------------------------------|
| 0x00   | 4    | uint32 | LE     | file_size    | total file length in bytes (== on-disk)  |
| 0x04   | 4    | entry  | LE     | entry[0]     | first item: 3-byte offset + 1-byte flag  |
| …      | 4    | entry  | LE     | entry[i]     | …                                        |

Each **entry** is 4 bytes:
| Sub | Size | Type   | Endian | Field  | Meaning                                    |
|-----|------|--------|--------|--------|--------------------------------------------|
| +0  | 3    | uint24 | LE     | offset | item start, from byte 0 of the file        |
| +3  | 1    | uint8  | —      | flag   | compression / content tag (see §3.3)       |

- **Entry count is implicit**: `entry[0].offset` is where item 0 begins, i.e.
  the end of the index. The index starts at byte 4, so
  `count = (entry[0].offset - 4) / 4`.
  (SE TALK: offset[0]=0x138=312 → (312-4)/4 = **77** entries. SE SYSTEM:
  0x14=20 → **4**. MD TALK: 0x1E8=488 → **121**.)
- **Item extent**: item *i* spans `[offset[i], offset[j])` where *j* is the next
  **present** entry (flag ≠ 0xFF); the last present item runs to `file_size`.

### 3.2 Item body
- **Compressed item** (compressed flag): a standard [lzw.md] container — 4-byte
  LE declared-unpacked-size, then the variable-width LSB-first LZW stream
  (9→12-bit, code 256 = clear). Decompress with the shared LZW decoder.
- **Stored item** (any other present flag): raw bytes. The inner format depends
  on the file (e.g. a nested [library](library.md) of shapes for FACES; plain
  0x0D/0x0A-delimited script text for MD TALK; a look-string table for LOOK).

### 3.3 The flag byte
The flag is **per-entry** (a single file may mix flags — e.g. MD `MIDGAME*.LZC`
interleave 0xC1 image items and 0xE0 text items). Observed values:

| Game | Flag | Meaning                     | Example files                          |
|------|------|-----------------------------|----------------------------------------|
| both | 0xFF | **absent/empty** slot       | trailing/inner gaps (skip; see below)  |
| SE   | 0x01 | **LZW-compressed**          | TALK, SYSTEM, MUSIC                     |
| SE   | 0x02 | stored                      | FACES, LOOK, CREATE, TITLE, MIDGAME*   |
| MD   | 0x20 | **LZW-compressed**          | MDD_MUS, FONTS, SCENETXT, STRX_MUS     |
| MD   | 0xC1 | stored (image/shape)        | MDFACES, most cutscene art             |
| MD   | 0xE0 | stored (text)               | TALK, LOOK, MDTEXT, SYSTEM, MUSIC      |

- **Compression is selected by a per-game marker value**: SE `0x01`, MD `0x20`.
  Any other present flag is stored. This was verified by correlating the flag
  against the item body over **every** entry in both games: the marker items
  are 100% LZW-shaped and non-marker items are 100% not (see §5).
- The remaining stored-flag distinction (MD 0xC1 vs 0xE0) tags content type
  (image vs text) and is **not needed to unpack the container** — a reader only
  needs "compressed vs stored vs absent".
- **Absent (0xFF)**: an empty index slot (e.g. an unused NPC conversation
  number). Its offset repeats the neighbouring present offset; it holds no
  bytes. Skip it and let the previous present item's extent run to the next
  present offset.
- Loader guidance: the container reader is told the game's compressed-marker
  byte (from the GameDef profile). As an independent cross-check, a compressed
  item's body always begins with the [lzw.md] signature — a 4-byte declared
  size `s` with `s > span-4`, followed by `0x00` then a byte with bit 0 set —
  and no stored item does; a tool may sniff this to self-verify.

## 4. Per-game deltas
- **U6**: does **not** use `.LZC` — it uses standalone [library](library.md)
  files, some individually wrapped in an [lzw](lzw.md) container. `.LZC` is the
  SE/MD unification of those two layers into one flagged container.
- **Savage Empire**: compressed marker = **0x01**; stored = 0x02.
- **Martian Dreams**: compressed marker = **0x20**; stored = 0xC1 (image) or
  0xE0 (text). MD stores its TALK script as plain text (uncompressed), whereas
  SE LZW-compresses TALK.

## 5. Validation method
`make lzctest` (tools/lzctest.c) over the user's SE and MD `.LZC` files:
1. Header `file_size` equals the actual file length (all files).
2. `count = (offset[0]-4)/4`; every present entry's `[offset,next)` span is
   in bounds and the present items tile to EOF with **no gap/overlap**
   (0 bad spans across all SE+MD `.LZC`).
3. Every **compressed-marker** item decompresses with our `u6lzw` to EXACTLY
   its declared size (no decoder error, no size mismatch).
4. Spot content check: an SE `TALK.LZC` item decompresses to readable
   conversation text; an MD `TALK.LZC` stored item is readable text — proving
   the extent boundaries and compression flag are right, not just self-consistent.

## 6. Open questions / unknown fields
- [ ] Exact semantics of the stored-flag high bits (MD 0xC1 vs 0xE0 beyond
      "image vs text"); not required to unpack, so left as a content-type tag.
- [ ] Whether any SE/MD `.LZC` ever needs a mid-stream LZW clear at the 12-bit
      ceiling (none observed failing; shared with U6's open question in lzw.md).

## 7. Status
VERIFIED
