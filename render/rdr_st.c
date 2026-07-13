// SPDX-License-Identifier: GPL-2.0-or-later
/* rdr_st.c — ST-low 16-colour shifter backend (M7 headline: the "better than the
 * lazy EGA port" path).
 *
 * ST-low is 320x200, 16 colours, stored as 4 INTERLEAVED bitplanes: each group of
 * 16 pixels is 4 consecutive 16-bit words (one per plane); pixel p's colour bit b
 * lives in bit (15-p) of word b. A row is 20 groups = 80 words = 160 bytes; the
 * whole screen is 32000 bytes.
 *
 * The engine composites into an 8-bit indexed backbuffer exactly as on the TT/Falcon.
 * Present here = per pixel: LUT-remap 256->16 (the host-quantised map from the pack's
 * U6WW section, rdr_set_lowcolor), then chunky->planar into the 4-plane screen. ALL the
 * expensive colour work (choosing the optimal 16, building the LUT) happened HOST-side;
 * the ST does only the LUT lookup + c2p. 320x200 is the native UI size, so — unlike the
 * TT (320x480, 2x-doubled+letterboxed) — the present is 1:1: no doubling, no bands.
 *
 * TOS calls used (named for verification):
 *   Getrez()                 : XBIOS 4  — read current resolution (restore on exit).
 *   Setscreen(log,phys,rez)  : XBIOS 5  — point video at our ST-RAM buffer; rez 0 = ST-low.
 *   Setpalette(ptr)          : XBIOS 6  — load all 16 ST colour registers (0x0RGB, 3-bit/ch).
 *   Physbase()/Logbase()     : XBIOS 2/3, Vsync() : XBIOS 0x25.
 * Screen buffer is ST-RAM (plat_alloc_chip, 256-byte aligned) — the shifter DMA-fetches it.
 */
#include "render.h"
#include "platmem.h"

#include <osbind.h>
#include <mint/linea.h>     /* line-A: hide the AES mouse sprite */
#include <string.h>

#define STW 320
#define STH 200
#define ST_GROUPS   (STW/16)          /* 20 chunky groups / row                */
#define ST_ROWBYTES (STW/2)           /* 160 bytes/row (4bpp)                  */
#define ST_SCREEN_BYTES (ST_ROWBYTES*STH)   /* 32000                          */
#define ST_BACK_BYTES   (STW*STH)     /* 64000: chunky 8-bit backbuffer        */

static uint8_t  *g_screen;            /* planar screen A (ST-RAM)              */
static uint8_t  *g_back;              /* chunky 8-bit backbuffer               */
static uint8_t  *g_screen2;           /* planar screen B (double-buffer flip)  */
static uint8_t  *g_vis;               /* currently DISPLAYED buffer            */
static int       g_flip;              /* double-buffer (tear-free) mode active */

static uint16_t  g_stpal[16];         /* ST 0x0RGB hardware palette (3-bit/ch) */
static uint8_t   g_lut[256];          /* active 256->16 remap (from U6WW)      */

static short     g_old_rez;
static void     *g_old_phys, *g_old_log;

/* chunky 8-bit -> 4 interleaved ST bitplanes for `ngroups` 16-pixel groups, remapping each
 * index through the 256->16 LUT on the way. Branchless: 4 plane accumulators shifted a bit
 * per pixel (pixel 0 ends in bit 15). C REFERENCE — a hand-asm span can replace this later
 * (docs/design/performance.md), but correctness first: the base ST is CPU-bound here. */
static void c2p4_lut(const uint8_t *src, const uint8_t *lut, uint16_t *dst, int ngroups)
{
    for (int g = 0; g < ngroups; g++) {
        uint16_t p0=0,p1=0,p2=0,p3=0;
        for (int p = 0; p < 16; p++) {
            unsigned c = lut[src[p]] & 0x0F;
            p0=(uint16_t)((p0<<1)|(c&1));      p1=(uint16_t)((p1<<1)|((c>>1)&1));
            p2=(uint16_t)((p2<<1)|((c>>2)&1)); p3=(uint16_t)((p3<<1)|((c>>3)&1));
        }
        dst[0]=p0; dst[1]=p1; dst[2]=p2; dst[3]=p3;
        dst += 4; src += 16;
    }
}

bool rdr_init(const platform_caps_t *caps)
{
    (void)caps;

    /* Backbuffer prefers the fast pool (Alt-RAM if present, e.g. an accelerated ST); on a
     * base ST plat_alloc_fast falls back to ST-RAM. g_screen MUST be chip (shifter DMA). */
    g_back   = (uint8_t *)plat_alloc_fast(ST_BACK_BYTES);
    if (!g_back) g_back = (uint8_t *)plat_alloc_chip(ST_BACK_BYTES);
    g_screen = (uint8_t *)plat_alloc_chip(ST_SCREEN_BYTES);   /* chip RAM, 256-byte aligned */
    if (!g_back || !g_screen) return false;
    g_vis = g_screen;

    /* identity-ish default remap + a grey ramp, until rdr_set_lowcolor supplies the real
     * U6WW palette/LUT (so a present before that isn't total garbage). */
    for (int i = 0; i < 256; i++) g_lut[i] = (uint8_t)(i & 0x0F);
    for (int i = 0; i < 16;  i++){ unsigned v=(unsigned)(i*7/15); g_stpal[i]=(uint16_t)((v<<8)|(v<<4)|v); }

    g_old_rez  = Getrez();
    g_old_phys = Physbase();
    g_old_log  = Logbase();

    Setscreen(g_screen, g_screen, 0);     /* log+phys = our buffer, rez 0 = ST-low 320x200x16 */
    Setpalette(g_stpal);

    /* Hide the AES mouse sprite (see rdr_tt.c): we run fullscreen non-GEM pointing video at
     * our own ST-RAM buffer; the still-active AES mouse VBL handler would XOR garbage into it.
     * line-A $A000 init + $A00A hide-mouse stop it without touching the AES. */
    linea0();
    lineaa();
    V_X_MAX = STW - 1;   /* 319: let the pointer range the full ST-low screen */
    V_Y_MAX = STH - 1;   /* 199 */

    for (int32_t i = 0; i < ST_SCREEN_BYTES; i++) g_screen[i] = 0;
    for (int32_t i = 0; i < ST_BACK_BYTES;  i++) g_back[i]   = 0;
    return true;
}

void rdr_shutdown(void)
{
    linea9();                             /* line-A $A009: show mouse for the desktop */
    Setscreen(g_old_log, g_old_phys, g_old_rez);
    Vsync();
}

void rdr_get_caps(rdr_caps *o) { o->width = STW; o->height = STH; o->colors = 16; o->indexed = true; }
int  rdr_width(void)  { return STW; }
int  rdr_height(void) { return STH; }
uint8_t *rdr_backbuffer(void) { return g_back; }

/* The engine hands us 256-colour palettes (game + cutscenes + colour-cycling frames), but the
 * ST shows only the 16 chosen host-side. The active 16-colour palette + LUT come from the pack
 * via rdr_set_lowcolor; a 256-colour update can't be honoured on 16-colour hardware, so it is a
 * no-op here. (Per-scene low-colour palettes + reserving slots for colour cycling are the next
 * step — docs/design/palette_lowcolor.md.) */
void rdr_set_palette(const uint8_t *rgb6, int n) { (void)rgb6; (void)n; }

/* Load the pack's host-quantised low-colour data: pal16 = 16 x {r,g,b} ST 3-bit levels (pack
 * straight into a colour register), lut256 = the 256->16 remap used at present time. */
void rdr_set_lowcolor(const uint8_t *pal16, const uint8_t *lut256)
{
    if (lut256) memcpy(g_lut, lut256, 256);
    if (pal16) {
        for (int i = 0; i < 16; i++) {
            unsigned r = pal16[i*3+0] & 7, g = pal16[i*3+1] & 7, b = pal16[i*3+2] & 7;
            g_stpal[i] = (uint16_t)((r << 8) | (g << 4) | b);    /* ST register 0x0RGB, 3-bit/ch */
        }
        Setpalette(g_stpal);
    }
}

void rdr_debug_info(uint32_t out[6])
{
    out[0] = (uint32_t)(uintptr_t)Physbase();
    out[1] = (uint32_t)(uintptr_t)Logbase();
    out[2] = (uint32_t)(uintptr_t)g_screen;
    out[3] = 0;
    out[4] = (uint32_t)(uint16_t)Getrez();
    out[5] = (uint32_t)ST_SCREEN_BYTES;
}

/* Full-screen present: LUT-remap + c2p the whole 320x200 backbuffer to the 4-plane screen. */
void rdr_present(void)
{
    for (int y = 0; y < STH; y++) {
        const uint8_t *px  = g_back + (int32_t)y * STW;
        uint16_t      *dst = (uint16_t *)(g_screen + (int32_t)y * ST_ROWBYTES);
        c2p4_lut(px, g_lut, dst, ST_GROUPS);
    }
    Vsync();
}

/* c2p only the sub-rectangle [x,y,w,h). x,w must be multiples of 16. */
void rdr_present_rect(int x, int y, int w, int h)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > STW) w = STW - x;
    if (y + h > STH) h = STH - y;
    int g0 = x / 16, gn = w / 16;
    for (int yy = y; yy < y + h; yy++) {
        const uint8_t *px  = g_back + (int32_t)yy * STW + g0 * 16;
        uint16_t      *dst = (uint16_t *)(g_screen + (int32_t)yy * ST_ROWBYTES) + g0 * 4;
        c2p4_lut(px, g_lut, dst, gn);
    }
    /* no Vsync: caller may present several rects per frame */
}

/* ST-low is 320x200 == the native UI, so "doubled" collapses to a straight 1:1 present of the
 * top `lh` logical rows (no 2x-vertical / letterbox that the taller TT needs). */
void rdr_present_doubled(int lh)
{
    if (lh < 1) lh = 1;
    if (lh > STH) lh = STH;
    /* In flip mode, c2p into the HIDDEN buffer, then swap the video base at the frame boundary. */
    uint8_t *dst = (g_flip && g_vis == g_screen) ? g_screen2 : g_screen;
    if (g_flip && g_vis == g_screen2) dst = g_screen;
    for (int y = 0; y < lh; y++) {
        const uint8_t *px = g_back + (int32_t)y * STW;
        uint16_t     *d   = (uint16_t *)(dst + (int32_t)y * ST_ROWBYTES);
        c2p4_lut(px, g_lut, d, ST_GROUPS);
    }
    if (g_flip) { Setscreen(dst, dst, -1); Vsync(); g_vis = dst; return; }
    Vsync();
}

/* Sub-rectangle 1:1 present (the movement/animation perf lever — c2p just the map window). */
void rdr_present_rect_doubled(int x, int y, int w, int h, int lh)
{
    (void)lh;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > STW) w = STW - x;
    if (y + h > STH) h = STH - y;
    int g0 = x / 16, gn = w / 16;
    for (int yy = y; yy < y + h; yy++) {
        const uint8_t *px = g_back + (int32_t)yy * STW + g0 * 16;
        uint16_t     *dst = (uint16_t *)(g_screen + (int32_t)yy * ST_ROWBYTES) + g0 * 4;
        c2p4_lut(px, g_lut, dst, gn);
    }
    /* no Vsync: caller batches */
}

/* Incremental horizontal scroll (render.h): shift the visible screen LEFT by one 16-px group in
 * place (a planar block move of 8 bytes/plane-word-group per row), then c2p only the newly-
 * revealed rightmost group from `chunky` at source-x = pan_x. ~1/20 the c2p of a full present. */
void rdr_scroll_left16(const uint8_t *chunky, int cw, int lh, int pan_x)
{
    if (lh < 1) lh = 1;
    if (lh > STH) lh = STH;
    int lastw = (ST_GROUPS - 1) * 4;             /* word offset of the rightmost group (76) */
    int lastp = (ST_GROUPS - 1) * 16;            /* chunky px offset of that group (304)    */
    if (pan_x < 0) pan_x = 0;
    for (int y = 0; y < lh; y++) {
        uint16_t *r = (uint16_t *)(g_screen + (int32_t)y * ST_ROWBYTES);
        memmove(r, r + 4, (size_t)(ST_ROWBYTES - 8));       /* planar shift left one 4-word group */
        const uint8_t *px = chunky + (int32_t)y*cw + pan_x + lastp;
        c2p4_lut(px, g_lut, r + lastw, 1);                  /* c2p the new rightmost group */
    }
}

/* Enter double-buffered (tear-free) mode for full-screen animation (the intro). No-op if a
 * second ST-RAM buffer can't be allocated (stays single-buffered). */
void rdr_flip_begin(void)
{
    if (!g_screen2) { g_screen2 = (uint8_t *)plat_alloc_chip(ST_SCREEN_BYTES);
                      if (g_screen2) for (int32_t i=0;i<ST_SCREEN_BYTES;i++) g_screen2[i]=0; }
    if (g_screen2) g_flip = 1;
}

void rdr_flip_end(void)
{
    if (!g_flip) return;
    g_flip = 0;
    if (g_vis != g_screen) memcpy(g_screen, g_vis, ST_SCREEN_BYTES);
    Setscreen(g_screen, g_screen, -1); Vsync();
    g_vis = g_screen;
}

/* Hardware horizontal scroll: STE supports it ($FF820F line-offset + $FF8265 fine-scroll), a
 * plain ST does not. Conservatively unsupported for now → the caller uses the CPU scroll
 * (rdr_scroll_left16). A dedicated STE backend can enable the register path later. */
bool scroll_begin(const uint8_t *chunky, int cw, int ch) { (void)chunky; (void)cw; (void)ch; return false; }
void scroll_pan(int pan_x) { (void)pan_x; }
void scroll_end(void) { }

/* STE $FF8265 fine-scroll linchpin test — not used on the plain-ST path. */
void rdr_test_hscroll(int v) { (void)v; }
