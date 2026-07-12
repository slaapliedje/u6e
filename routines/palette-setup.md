# Routine: ST palette setup

Address: <<0x...>> (entry), called from <<0x...>>
Status: ANALYZING · Confidence: probable

## What it does (prose — PREFERRED over raw asm)
Reads <N> color entries from <source>, writes them to the ST Shifter palette
registers via <Setpalette / direct $FF8240 writes>. Appears to map source
256-color values down to 16 fixed entries — candidate evidence for the
"EGA-palette reuse" criticism (see rules/palette behavior).

## Pseudocode (our reconstruction, not transcribed)
for i in 0..15:
    src = palette_source[i]
    hw[i] = to_st_rgb(src)   # note: only 3 bits/channel used
set_st_palette(hw)

## Evidence / how derived
- Hatari breakpoint at <<addr>>; dumped registers + the 16 words written.
- Compared written values to the ST 512-color gamut → confirms 3-bit/channel.
- <<Ghidra/IDA>> cross-refs show callers during room/scene transitions.

## Open questions
- Is there per-scene palette swapping, or one global 16? (impacts /tools
  quantizer strategy in M7)

## Feeds
- /docs/formats/palette.md, /docs/rules (palette behavior), informs M7.

