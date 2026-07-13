// SPDX-License-Identifier: GPL-2.0-or-later
/* render.h — platform-agnostic renderer interface (M3).
 *
 * The engine talks ONLY to this interface; all video-hardware divergence lives
 * in the backends (render/rdr_*.c). The canonical internal surface is an 8-bit
 * indexed (chunky) backbuffer; per-target present converts it to the screen's
 * real layout (e.g. interleaved bitplanes on the TT/Falcon shifter).
 *
 * Per project rules: VDI/XBIOS only for capability discovery / mode set /
 * palette / present — never per-tile blits (those go into the backbuffer).
 */
#ifndef U6E_RENDER_H
#define U6E_RENDER_H

#include <stdint.h>
#include <stdbool.h>
#include "platform.h"

typedef struct {
    int  width, height;   /* backbuffer / visible size in pixels */
    int  colors;          /* 256 for the M3 backend              */
    bool indexed;         /* true = palette-based                */
} rdr_caps;

/* Detect the target, set a 256-color mode, allocate screen (ST-RAM) + chunky
 * backbuffer. Returns false if no supported backend for this machine. Saves the
 * prior video state for rdr_shutdown(). */
bool     rdr_init(const platform_caps_t *caps);
void     rdr_shutdown(void);                 /* restore prior video mode */

void     rdr_get_caps(rdr_caps *out);
int      rdr_width(void);
int      rdr_height(void);
uint8_t *rdr_backbuffer(void);               /* chunky 8-bit, width*height */

/* rgb6 = n*3 bytes, 6-bit-per-channel (U6 palette format). */
void     rdr_set_palette(const uint8_t *rgb6, int n);

/* SHIFTER (16-colour) backend only: hand it the host-quantised low-colour data from the pack
 * (U6WW, docs/design/palette_lowcolor.md). pal16 = 16 x {r,g,b} as ST 3-bit levels (0..7);
 * lut256 = a 256->16 index map. The backend loads pal16 into the 16 hardware colour registers
 * and LUT-maps the 8-bit backbuffer at present. No-op on 256-colour backends (TT/Falcon/vcard,
 * which show all 256 VGA colours). Call once after loading the pack, before presenting. */
void     rdr_set_lowcolor(const uint8_t *pal16, const uint8_t *lut256);

/* SHIFTER band rasters (M7 "cheat past 16 colours"): install `nb` horizontal bands, each with
 * its OWN 16-colour palette (3-bit levels) + 256->16 LUT. y0[i] = first display row of band i
 * (ascending; y0[0] MUST be 0). pal = nb*48 bytes (16 x {r,g,b} per band), lut = nb*256 bytes.
 * The backend LUT-maps each row with its band's LUT at present, and reloads the shifter palette
 * at each band boundary via a Timer-B raster interrupt — so the frame shows up to nb*16 colours.
 * nb<=1 disables the raster (falls back to the single rdr_set_lowcolor palette). No-op on the
 * 256-colour backends. Timer-B counts DE (display-enable) events; see docs/design/palette_lowcolor.md. */
void     rdr_set_lowcolor_bands(int nb, const uint8_t *y0, const uint8_t *pal, const uint8_t *lut);

/* SHIFTER colour cycling (M7): re-push the 16 hardware colour registers from pal16 (16 x {r,g,b}
 * 3-bit levels) WITHOUT re-c2p'ing the backbuffer — the shifter re-reads the CLUT, so rotating a
 * few reserved slots animates fire/water for free. Updates band 0 (the map) too when a band raster
 * is active. Cheap (16 register writes); call each cycle tick. No-op on 256-colour backends. */
void     rdr_lowcolor_cycle(const uint8_t *pal16);

/* STE 4096-colour mode (M7): on an STE the shifter has 4 bits/channel (vs the ST's 3). When enabled,
 * the shifter backend interprets the pal16 arrays it is given as 4-bit levels (0..15) and packs the
 * STE register format; the engine passes the pack's U6WS 4-bit palettes instead of the 3-bit ones.
 * rdr_is_ste() lets the engine choose which palette array to hand over. No-op on TT/Falcon. */
void     rdr_set_ste_mode(int on);
int      rdr_is_ste(void);

/* Convert the backbuffer to the screen's native layout and display it. */
void     rdr_present(void);

/* Present only a sub-rectangle (x and w MUST be multiples of 16). This is the
 * key perf lever: a windowed map redraws just its region, not the whole screen. */
void     rdr_present_rect(int x, int y, int w, int h);

/* Present the top `lh`-row (320 x lh) region of the backbuffer, scaled 2x
 * vertically and vertically centred, into the (taller) physical screen with
 * black bands. Used on TT-low (320x480) to show the 320x200 UI at a sensible
 * size instead of a small letterboxed band. */
void     rdr_present_doubled(int lh);

/* Double-buffered (tear-free) present: between rdr_flip_begin/end, rdr_present_doubled draws
 * into a hidden buffer and flips the video base at the frame boundary — no visible c2p tear.
 * For full-screen animation like the intro. Outside this pair, presents are single-buffered. */
void     rdr_flip_begin(void);
void     rdr_flip_end(void);

/* LINCHPIN TEST (TT backend): write the STE $FF8265 horizontal fine-scroll register.
 * If TT-low honours it the image shifts left v (0..15) px. Gates the HW-scroll design. */
void     rdr_test_hscroll(int v);

/* HARDWARE HORIZONTAL SCROLL: c2p a cw x ch chunky frame into a wide planar buffer once,
 * then pan it with the video hardware (base-address + fine-scroll), no per-frame c2p.
 * scroll_begin returns false if unsupported/too big (caller falls back to CPU scroll). */
bool     scroll_begin(const uint8_t *chunky, int cw, int ch);
void     scroll_pan(int pan_x);   /* absolute horizontal offset, 0 .. (cw-320) */
void     scroll_end(void);        /* restore normal full-screen video */

/* Like rdr_present_doubled, but only the sub-rectangle [x,x+w) x [y,y+h) of the
 * backbuffer (logical coords; x and w MUST be multiples of 16). The perf lever for
 * animation/movement: present just the map window, not the whole doubled screen.
 * No Vsync (caller batches). */
void     rdr_present_rect_doubled(int x, int y, int w, int h, int lh);

/* INCREMENTAL horizontal scroll (CPU, no HW scroll): the visible screen currently shows the
 * `lh`-row 320-wide window of `chunky` (cw wide) at source-x = pan_x-16. Shift the displayed
 * image LEFT by 16 px (one c2p group) in place, then c2p only the newly-revealed rightmost
 * 16-px column from `chunky` at source-x = pan_x. ~1/20 the c2p of a full present per step.
 * The caller establishes the pan_x=0 baseline with a full present, then calls this for
 * pan_x = 16, 32, ... Single-buffer (writes the visible screen); no Vsync (caller paces). */
void     rdr_scroll_left16(const uint8_t *chunky, int cw, int lh, int pan_x);

/* Diagnostic: out[0]=Physbase, [1]=Logbase, [2]=our screen, [3]=EgetShift,
 * [4]=Getrez, [5]=our screen byte size. */
void     rdr_debug_info(uint32_t out[6]);

#endif
