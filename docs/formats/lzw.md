# Format: LZW container (compressed data files)

Games: U6 / Savage / Martian — shared (same engine lineage; verified on U6)
Implemented by: /dataformats (u6lzw.c/.h; the loop in 68000 asm in u6lzw_68k.s on the Atari), /tools (dftest)
Tracked in: /docs/INDEX.md  ·  Milestone: M2

> Provenance
> - Derived from : reading the user's own DOS U6 files (MASKTYPE.VGA,
>                  MAPTILES.VGA, ANIMMASK.VGA, CONVERSE.A) byte-by-byte +
>                  general public knowledge of variable-width LZW.
> - References   : community "Ultima 6 file formats" descriptions (read to
>                  understand the variant; NOT transcribed).
> - Tools        : hexdump of /refdata; our /tools dftest harness.
> - Validated by  : dftest decodes each real file to EXACTLY its self-declared
>                  size (3968 / 117408 / 2048), and an independent encoder
>                  round-trips through our decoder. The decoder PARAMETERS were
>                  found empirically by requiring the real files to decode.
> - Confidence   : confirmed
> - Last updated : 2026-06-23

## 1. Overview
Many U6 data files are wrapped in an LZW container: a 4-byte little-endian
"uncompressed size" header followed by a variable-width LZW bit stream. The
engine/tools decompress to a flat buffer, which is then parsed by the relevant
inner format (often a [library](library.md)).

## 2. Container facts
- **Header**: 4 bytes, **little-endian uint32** = decompressed size in bytes.
- **Stored escape**: if that size == 0, there is NO LZW stream — the bytes
  after the header are the literal uncompressed data. (Confirmed: CONVERSE.A
  has header 0 and 162611 bytes of body.)
- **Bit stream** (when size > 0): variable-width LZW, codes packed
  **LSB-first** (first code occupies the low bits of byte 0).
  - Initial code width: **9 bits**, growing by 1 up to **12 bits**.
  - Width grows when the next dictionary code to assign equals `1 << width`
    (i.e. NO "early change"). (Decoder rule; a matching encoder bumps one
    entry earlier — see u6lzw / dftest.)
  - **Code 256 = clear**: reset the dictionary and width back to 9.
  - **Code 257 = end-of-information** (reserved, GIF-style). Not needed for
    decoding because the output size is known up front; decoding stops when the
    declared size is reached.
  - **First string code assigned after a clear = 258** (codes 0–255 literals,
    256 clear, 257 EOI).
- Endianness: source is little-endian; the decoder reads the LE header and the
  LSB-first stream explicitly, so it is correct on the big-endian 68000.
- The very first emitted code is normally 256 (clear): for a 9-bit LSB-first
  code that is byte0 = 0x00, byte1 bit0 = 1. (Observed in MASKTYPE/MAPTILES.)

## 3. Byte layout
| Offset | Size | Type      | Endian | Field            | Meaning                         |
|--------|------|-----------|--------|------------------|---------------------------------|
| 0x00   | 4    | uint32    | LE     | unpacked_size    | decompressed length; 0 = stored |
| 0x04   | …    | bitstream | LSB    | lzw_data / body  | LZW codes, or literal if size 0 |

## 4. Per-game deltas
Shared — no deltas observed (verified on U6; assume same for Savage/Martian
until their files are checked).

## 5. Validation method
`make dftest` (tools/dftest.c): decode each real LZW file and assert the output
length equals the header's declared size; plus an independent-encoder
round-trip on synthetic data. Decode-correctness is proven host-side here
before any on-target use.

## 6. Open questions / unknown fields
- [ ] Confirm 12-bit is the true max for all U6 files (none seen needing a
      mid-stream clear yet; large files may exercise it).
- [ ] Spot-check a Savage/Martian compressed file to confirm "shared".

## 7. Status
VERIFIED
