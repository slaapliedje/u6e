# Strings — BOOK.DAT (books / signs / plaques / readable text)

> Provenance
> - Derived from : refdata/dos/u6/BOOK.DAT inspected (2-byte LE offset index + NUL-terminated
>                  text entries); the object→entry mapping observed in nuvie U6UseCode::look_sign
>                  (READ to understand only; not transcribed). Readable obj_n from u6_objects.h.
> - References   : nuvie core/book.cpp (U6Lib_n open(...,2)), usecode/u6_usecode.cpp look_sign.
> - Tools        : python/xxd on BOOK.DAT; mkworld extract_books (build-time extraction).
> - Validated by : ON-TARGET (TOS 3.06 TT) — L)ook on a real world SIGN (obj_n 332, quality 0 →
>                  entry 126) renders "The Book of Spirituality" word-wrapped over the papyrus,
>                  with the entry's \n\n rendered as a blank line. build/read_screen.png.
> - Confidence   : confirmed (container + mapping + render).
> - Last updated : 2026-07-12

## What it is
`BOOK.DAT` holds all of U6's static readable text — signs, plaques, monoliths, tombstones,
graves, and the pages of in-world books/scrolls. It is a **library** file (docs/formats/
library.md): a 2-byte little-endian offset index followed by the entries.

## Container (confirmed)
- **Index**: the file opens with a run of `u16 LE` offsets. `offset[0]` = the byte where the
  first entry starts = the index size, so **entry count = offset[0] / 2**. Observed: `offset[0]`
  = 256 → **128 entries**. (This is the generic library format opened with a 2-byte offset width;
  the M2 library auto-detect already reports BOOK.DAT as 2/128.)
- **Entries**: each entry is a **NUL-terminated ASCII string**. `'\n'` is a hard line break;
  a blank line is `"\n\n"`. Entries are NOT LZW-compressed (the index offsets are plain).
- Endianness: source is little-endian; the engine target is big-endian 68000, so the offset
  index is LE→host converted at read (we re-pack big-endian at build time — see below).

Illustrative (short, synthetic-length) entries: `"A monolith."`, `"The bones of Zog."`,
`"Left: <x>.\n\nRight: <y>."` — i.e. plain prose with optional embedded newlines. (Bulk text is
NOT reproduced here or committed; it is extracted at build time — see Clean-room.)

## Read mapping (object → entry) — from nuvie look_sign
A readable object carries a **QUALITY** byte (we already ship per-object quality, keys work,
pack U6WT). To read it:
- `entry = quality - 1`.
- Special case: `quality == 0` → `entry = 126` (a default entry — validated: a quality-0 world
  sign reads entry 126 = "The Book of Spirituality").
- We render the FULL entry text (plain ASCII). RESOLVED: nuvie's `&data[1]`/`len-2` trim is
  case-specific (its britannian-font path) — for the plain-ASCII pool, NOT trimming is correct;
  entry 126 rendered with its complete first/last characters on-target.

### Readable object types (obj_n, from u6_objects.h)
| obj_n | object        | notes |
|-------|---------------|-------|
| 151   | BOOK          | read when in inventory or adjacent |
| 152   | SCROLL        | as book |
| 255   | TOMBSTONE     | fixed in world |
| 266   | GRAVE         | fixed in world |
| 332   | SIGN          | fixed in world |
| 333   | SIGN_ARROW    | fixed in world |
- SPECIAL (NOT plain BOOK.DAT text, out of scope for the string pool): CLOCK 159 / SUNDIAL 235
  (show the game time), SPELLBOOK 57 (opens the spell UI), BOOK_OF_PROPHECIES 60 /
  BOOK_OF_CIRCLES 61 (their own usecode). A sign with `quality == 0` that is not a BOOK/SCROLL
  is not readable (no text).

## Clean-room / build pipeline
BOOK.DAT text is COPYRIGHTED game content — like the intro narration and gypsy questions, it is
**extracted at build time by mkworld into the never-committed asset pack**, never hardcoded or
committed. Pack section (after the existing text sections): `[u16 nbook] + nbook × [u16 len][bytes]`
(len excludes the NUL; big-endian). The engine indexes it by `quality - 1`.

## Engine use
- Ship the readable obj_n set (a small table) so the engine flags readable world objects.
- R)ead (or U)se on a book/scroll, L)ook on a sign/tombstone/grave) → fetch entry `quality-1`
  and display it in a paged text view (the message scroll / a book background), wrapping to width.

## Open questions / remaining
- READING FROM INVENTORY: books/scrolls (obj_n 151/152) carried in the pack are read via the
  same entry mapping, but the current engine hook resolves MAP objects (L)ook/U)se on a world
  tile). An inventory read path (I)nventory → select → read) is TODO.
- Item/ground-tile description strings and the copy-protection "rune"/word-of-power pages —
  separate tables (LOOK.LZD object/terrain names are already done). TODO.

## Status
VERIFIED — BOOK.DAT container + quality→entry mapping + on-target paged read view all validated
(build/read_screen.png). mkworld extract_books ships the 128 entries in the never-committed pack;
render/gamedemo book_view/read_object renders them on L)ook/U)se of a readable world object.
