# Format: Library container (offset-indexed block file)

Games: U6 / Savage / Martian — shared (verified on U6)
Implemented by: /dataformats (u6lib.c/.h)
Tracked in: /docs/INDEX.md  ·  Milestone: M2

> Provenance
> - Derived from : reading the user's own DOS U6 files (PORTRAIT.A, BOOK.DAT)
>                  byte-by-byte.
> - References   : community "Ultima 6 file formats" notes (read to understand;
>                  not transcribed).
> - Tools        : hexdump of /refdata; our /tools dftest harness.
> - Validated by  : dftest auto-detects PORTRAIT.A as 4-byte/98 entries and
>                  BOOK.DAT as 2-byte/128 entries; every present entry lies in
>                  bounds and the entries tile the file with no gap to EOF.
> - Confidence   : confirmed
> - Last updated : 2026-06-23

## 1. Overview
A "library" packs many variable-length sub-files into one file: an index of
offsets at the start, then the data blocks those offsets point to. Used for
portraits, books/strings, shapes, etc. Many libraries are themselves wrapped in
an [LZW container](lzw.md); decompress first, then parse the library.

## 2. Container facts
- **Index**: an array of offsets, each measured from the start of the index.
  Offsets are uniformly **2-byte OR 4-byte little-endian** within a given file.
- **Entry count is implicit**: the FIRST offset points to the first data block,
  i.e. it equals the index size. So `count = offset[0] / offset_width`.
  (PORTRAIT.A: offset[0]=392, 4-byte → 98 entries. BOOK.DAT: offset[0]=256,
  2-byte → 128 entries.)
- **Entry extent**: entry *i* spans `[offset[i], next_present_offset)`, where
  the last entry runs to end-of-file. An **offset of 0 marks an absent/empty
  entry** (skip it; its data, if any, belongs to the previous present entry's
  span up to the next present offset).
- **Optional size prefix**: some libraries begin with a 4-byte LE total-file-
  size word before the index; then offsets are measured from after it. (Neither
  PORTRAIT.A nor BOOK.DAT has one.)
- Endianness: little-endian offsets; reader converts explicitly (big-endian
  68000 safe).

### 2-byte vs 4-byte detection
Loaders normally KNOW each file's width (it's per-file-type knowledge). For
tools, `u6lib_open_auto` guesses: prefer the 4-byte reading when its first
offset is in range, divisible by 4, and the first several present offsets are
strictly increasing in bounds; else try 2-byte. (A 2-byte file misread as
4-byte yields a wildly out-of-range first offset; a 4-byte file misread as
2-byte yields interleaved zero entries — so the strictly-increasing test
disambiguates.)

## 3. Byte layout
| Offset | Size       | Type   | Endian | Field        | Meaning                          |
|--------|------------|--------|--------|--------------|----------------------------------|
| 0x00   | w          | uintW  | LE     | offset[0]    | start of block 0 = index size    |
| w      | w          | uintW  | LE     | offset[1]    | start of block 1 (0 = absent)    |
| …      | w          | uintW  | LE     | offset[n-1]  | start of block n-1               |
| off[i] | size_i     | bytes  | —      | block i      | size_i = next_present_off - off[i]|

(`w` = 2 or 4; `n` = offset[0]/w.)

## 4. Per-game deltas
Shared — no deltas observed (verified on U6).

## 5. Validation method
`make dftest`: auto-detect width + count against the known values for
PORTRAIT.A (4/98) and BOOK.DAT (2/128); assert every present entry is in bounds
and the blocks tile to EOF.

## 6. Open questions / unknown fields
- [ ] Catalogue which specific files use 2- vs 4-byte offsets and which have a
      size prefix (record per-file in the asset manifest / loaders).
- [ ] Confirm absent-entry (offset 0) semantics on a file that actually has gaps
      (PORTRAIT.A/BOOK.DAT are fully populated).

## 7. Status
VERIFIED
