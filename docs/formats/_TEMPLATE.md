# Format: <NAME> (<original filename(s)>)

Games: U6 / Savage / Martian — <shared | per-game deltas noted below>
Implemented by: </dataformats, /tools, /engine — list the subsystems>
Tracked in: /docs/INDEX.md  ·  Milestone: <Mx>

> Provenance
> - Derived from : <original files inspected | game observed running | refs read>
> - References   : <Nuvie / U6Edit / Codex page / etc. — what was READ, not copied>
> - Tools        : <Ghidra / Hatari debugger / hex editor / our /tools dumper>
> - Validated by  : <golden diff against X | NOT YET validated>
> - Confidence   : guessed | probable | confirmed
> - Last updated : YYYY-MM-DD

## 1. Overview
<One paragraph: what this file holds and how the engine uses it.>

## 2. Container facts
- Compression: <none | LZW (see lzw.md) | bit-shuffled | both>
- Endianness: source is **little-endian**; engine target is **big-endian 68000**
  — every multi-byte field needs explicit LE->host conversion at load.
- Alignment / padding: <…>

## 3. Byte layout
| Offset | Size | Type | Endian | Field | Meaning |
|--------|------|------|--------|-------|---------|
| 0x00   |      |      | LE     |       |         |

<Add sub-tables per record/block as needed. Keep hex examples SHORT and
illustrative — never bulk dumps of game content (clean-room).>

## 4. Per-game deltas
- U6: <…>
- Savage Empire: <…>
- Martian Dreams: <…>
(If identical across all three, write "shared — no deltas observed".)

## 5. Validation method
<What known-good output we diff against: tiles->PNG, map->image, round-trip,
checksum vs community-tool extraction, etc. Decode-correctness must be proven
host-side BEFORE any on-target rendering depends on it (milestone M2).>

## 6. Open questions / unknown fields
- [ ] <…>

## 7. Status
TODO | IN PROGRESS | VERIFIED   (mirror this into /docs/INDEX.md)
