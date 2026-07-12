#!/usr/bin/env python3
"""shot2png.py — convert the engine's SHOT.DAT backbuffer dump to a 1:1 PNG.

The game (render/gamedemo.c dump_shot) writes SHOT.DAT when SHOT.REQ is staged:
a pixel-perfect snapshot of the 320x200 chunky backbuffer + its palette, BEFORE
any emulator scaling/aspect/overscan. So the PNG is exactly logical-pixel == image
-pixel — the definitive rig for tile-alignment / bug-hunt work.

Format (big-endian, written by the 68000):
    magic 'U6SH', u16 w, u16 h, 256*3 bytes RGB6 palette, then w*h index bytes.
RGB6 = 6-bit-per-channel U6 palette; expanded to 8-bit by bit-replication.

Usage: shot2png.py SHOT.DAT out.png
"""
import sys, struct

def main():
    if len(sys.argv) != 3:
        sys.exit("usage: shot2png.py SHOT.DAT out.png")
    data = open(sys.argv[1], "rb").read()
    if data[:4] != b"U6SH":
        sys.exit("bad magic: not a SHOT.DAT (expected 'U6SH')")
    w, h = struct.unpack(">HH", data[4:8])
    pal6 = data[8:8 + 768]
    pix = data[8 + 768:8 + 768 + w * h]
    if len(pix) < w * h:
        sys.exit(f"short pixel data: got {len(pix)}, need {w*h}")
    # expand 6-bit -> 8-bit (bit-replicate so 0x3F -> 0xFF)
    exp = [((c & 0x3F) << 2) | ((c & 0x3F) >> 4) for c in pal6]
    try:
        from PIL import Image
        im = Image.frombytes("P", (w, h), pix)
        im.putpalette(exp)
        im.convert("RGB").save(sys.argv[2])
    except ImportError:
        # No PIL: emit a minimal PPM next to the requested path.
        out = sys.argv[2].rsplit(".", 1)[0] + ".ppm"
        with open(out, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (w, h))
            for idx in pix:
                f.write(bytes((exp[idx*3], exp[idx*3+1], exp[idx*3+2])))
        print(f"PIL absent; wrote {out}")
        return
    print(f"wrote {sys.argv[2]} ({w}x{h}, 1:1)")

if __name__ == "__main__":
    main()
