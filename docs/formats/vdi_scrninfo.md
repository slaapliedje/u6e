# VDI `vq_scrninfo` + graphics-card blit format (for rdr_vcard truecolour)

> Provenance
> - Derived from : running the ARAnyM+fVDI+AFROS rig and observing rdr_vcard's own output
> - References   : EdDI/NVDI VDI docs (vq_scrninfo, MFDB); READ fVDI source (include/fvdi.h `Device`
>                  struct, drivers/aranym/spec.c mode table, engine/workstn.c, drivers/common/init.c)
>                  and ARAnyM src/natfeat/nfvdi_soft.cpp `blitArea_M2S` to UNDERSTAND the layout — none copied
> - Tools        : m68k-atari-mint-gcc, ARAnyM NF_STDERR console, headless screenshots
> - Validated by  : rdr_vcard truecolour present renders pixel-correct at 640x480x16 (RGB565) and x32
>                  (0x00RRGGBB) — test grid AND full game — with the layout auto-detected via vq_scrninfo
> - Confidence   : confirmed (for the fVDI/NVDI + ARAnyM path)
> - Last updated : 2026-07-17

Clean-room spec written from the EdDI/NVDI VDI documentation and by *reading*
fVDI + ARAnyM's NatFeat VDI driver to understand the on-the-wire layout. No code
is transcribed; `rdr_vcard.c` is implemented from this spec. Implemented by: render/rdr_vcard.c.

## Why we need it
On a truecolour graphics card the VDI screen is direct-colour (16/24/32 bpp
chunky), not palette-indexed. To present our 8-bit backbuffer we must expand each
index to a *device pixel* in the card's exact bit layout, put those pixels into a
source raster, and `vro_cpyfm` it to the screen. Two things must be exactly right
or the frame garbles: (a) the pixel bit layout, (b) the source MFDB `fd_wdwidth`.

## `vq_scrninfo` — exact pixel format (opcode 102, sub 1)
`vq_scrninfo` is bound to VDI opcode **102** (shared with `vq_extnd`) and
selected by the sub-function + flag:

    contrl[0] = 102        ; opcode
    contrl[1] = 0          ; l_ptsin
    contrl[3] = 1          ; l_intin
    contrl[5] = 1          ; sub-function  (1 = vq_scrninfo)
    contrl[6] = handle     ; our virtual workstation
    intin[0]  = 2          ; flag (2 = return screen info)
    -> vdi()

Returns a `Device` struct into `intout` (272 words). Field layout, as **word
offsets in `intout`**:

| words     | field                | meaning                                        |
|-----------|----------------------|------------------------------------------------|
| [0]       | `format`             | 0 = interleaved planes, 2 = packed pixels      |
| [1]       | `clut`               | 1 = hardware CLUT (indexed), **2 = no CLUT (true colour)** |
| [2]       | `bit_depth`          | real bits/pixel — **authoritative depth** (line-A VPLANES lies on a card) |
| [3]       | `dummy1`             | high word of colour count                      |
| [4]       | `colours`            | low word of colour count                       |
| [5]       | `byte_width`         | screen bytes per scan line                     |
| [6..7]    | `address`            | screen base (long)                             |
| [8]       | `bits.red`           | number of RED bits                             |
| [9]       | `bits.green`         | number of GREEN bits                           |
| [10]      | `bits.blue`          | number of BLUE bits                            |
| [11]      | `bits.alpha`         | number of ALPHA bits                           |
| [12]      | `bits.genlock`       | genlock bits                                   |
| [13]      | `bits.unused`        | unused bits                                    |
| [14]      | `bits.organization`  | pixel bit organisation (0x01 usual, 0x80 Intel)|
| [15]      | `dummy2`             | —                                              |
| [16..31]  | `bitnumber.red[16]`  | screen bit position of red bit *j* (j=0 = LSB) |
| [32..47]  | `bitnumber.green[16]`| screen bit position of green bit *j*           |
| [48..63]  | `bitnumber.blue[16]` | screen bit position of blue bit *j*            |
| [64..79]  | `bitnumber.alpha[16]`| screen bit position of alpha bit *j*           |

(When `clut != 2` the same union is a `vdi2pix[256]` index map instead; we only
read `bitnumber.*` in the true-colour branch.)

### Building a device pixel (device-independent)
For an 8-bit channel value `v8` with `nbits = bits.<ch>`:

    cv = v8 >> (8 - nbits)                 ; top nbits bits of the channel
    for j in 0..nbits-1:
        if cv & (1<<j): pixel |= 1 << bitnumber.<ch>[j]

This reconstructs *any* layout (565, 555, 0x00RRGGBB, byte-swapped, non-contiguous)
straight from the card's own report — no hard-coded masks.

### Concrete values reported by the fVDI/ARAnyM rig
- **16-bit, usual order**: red=5 @ {11,12,13,14,15}, green=6 @ {5..10}, blue=5 @ {0..4} → RGB **565**.
- **32-bit, usual order**: red=8 @ {16..23}, green=8 @ {8..15}, blue=8 @ {0..7} → **0x00RRGGBB**.
- **24-bit**: same bit numbers as 32 but packed 3 bytes/pixel (R,G,B big-endian).
- The "fb layout" fVDI option (Intel byte order, `org=0x80`) yields non-contiguous
  bit numbers; the per-bit assembly above handles it without special-casing.

## Source MFDB for the present blit (the `fd_wdwidth` trap)
The host blit (`vro_cpyfm`, S_ONLY) reads the source raster like this:

    planes = MFDB.fd_nplanes        ; 15 is treated as 16
    pitch  = MFDB.fd_wdwidth * planes * 2      ; BYTES per scan line

so **`fd_wdwidth` is the per-plane width in 16-bit words = pixels/16**, and the
driver multiplies by `planes*2` itself. It must NOT be pre-scaled by the depth.
For a 320-wide frame `fd_wdwidth = 20` at *every* depth (8/16/24/32). Setting it
to `pixels*bpp/16` makes `pitch` 16–32× too large → the whole frame garbles.

For `fd_nplanes` in {16,24,32} the source is read as **chunky** directly (no
`fd_stand` flag needed): 16 → 2 bytes/pixel, 24 → 3 bytes/pixel, 32 → 4
bytes/pixel. (For 8-bit the driver only treats it as chunky if `fd_stand & 0x100`;
otherwise it does a bitplane→chunky conversion — which is why our **indexed** path
c2p's to planes + `vr_trnfm`, while the **truecolour** path writes chunky pixels
directly.)

So the truecolour present is: expand each index → device pixel (bit layout above)
into a chunky raster with row stride `pixels * bpp/8`, MFDB `fd_wdwidth = pixels/16`,
`fd_nplanes = bpp`, `fd_stand = 0`, then `vro_cpyfm` to the screen. No `v_opnbm`
required (that is an alternative that lets fVDI allocate the device bitmap; the
chunky-source path is simpler and equally correct here).
