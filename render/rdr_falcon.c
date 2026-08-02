// SPDX-License-Identifier: GPL-2.0-or-later
/* rdr_falcon.c — Falcon030 VIDEL 320x240 256-color backend.
 *
 * Why this backend exists alongside rdr_tt.c: the TT's only practical 256-color
 * mode on a VGA monitor is TT-low 320x480, so the 320x200 game UI must be shown
 * 2x-vertically-doubled and letterboxed (see rdr_tt.c rdr_present_doubled). The
 * Falcon's VIDEL shows 320x240 natively (double-scanned by hardware on VGA), so
 * we present the 320x200 UI 1:1 — no software line-doubling. The screen is 240
 * lines though, so the UI is CENTRED with a 20px black letterbox band top and
 * bottom (g_yoff), rather than top-aligned with a grey strip below. The Falcon is
 * a first-class target (better sound + flexible resolutions); the TT is the
 * lowest-common-denominator fallback.
 *
 * Falcon 8bpp ("BPS8") is 8 INTERLEAVED bitplanes, byte-identical in layout to
 * TT-low (320 bytes/line, 8 u16 plane-words per 16-pixel group). So the present
 * path reuses render/c2p_asm.s (c2p_span_asm) unchanged.
 *
 * TOS/XBIOS calls used (named + opcode, for verification):
 *   VgetMonitor()                 : XBIOS 89 — detect VGA vs RGB/TV monitor.
 *   VsetMode(VM_INQUIRE)          : XBIOS 88 — read current VIDEL mode (save).
 *   VsetScreen(log,phys,3,mode)   : XBIOS 5  — set screen base + Falcon videl
 *                                   mode atomically (rez==3 => `mode` is a
 *                                   Falcon mode word).
 *   VsetRGB(0,256,argb)           : XBIOS 93 — load the 256-entry VIDEL palette
 *                                   (8-bit-per-channel 0x00RRGGBB entries).
 *   Vsync() (XBIOS 0x25), Logbase()/Physbase() (XBIOS 3/2).
 * Screen buffer is ST-RAM (plat_alloc_chip): VIDEL DMAs the screen from the
 * first RAM bank, not TT-RAM.
 */
#include "render.h"
#include "platmem.h"

#include <mint/osbind.h>
#include <mint/falcon.h>   /* VsetMode/VsetScreen/VsetRGB + mode bits (BPS8/VGA/VERTFLAG) */
#include <mint/linea.h>    /* line-A: hide the AES mouse sprite */
#include <string.h>

#define RDR_BACKEND falcon    /* namespace this backend's entry points; emit rdr_be_falcon (below) */
#include "render_backend.h"

extern void c2p_span_asm(const uint8_t *src, uint16_t *dst, long ngroups);

#define FW 320
#define FH 240
#define FALC_PIXELS (FW * FH)
#define UI_H 200                 /* the game's logical UI height (native U6). The VIDEL screen is
                                  * 240 lines on VGA, so we centre the 200-line image and leave a
                                  * black letterbox band above and below (20px each). */

static uint8_t  *g_screen;              /* visible screen (ST-RAM): 8bpp planar OR 16bpp chunky */
static uint8_t  *g_back;                /* chunky 8-bit backbuffer          */
static uint32_t  g_pal[256];            /* VIDEL 0x00RRGGBB entries (indexed mode)              */
static uint16_t  g_pal16[256];          /* RGB565 expand LUT (truecolor mode)                   */
static int       g_tc;                  /* 1 = 16-bit truecolor present active                  */
static int       g_want_tc;             /* rdr_prefer_truecolor request (before rdr_init)        */
static int32_t   g_screen_bytes;        /* visible screen size (depends on depth)               */
static int       g_yoff;                /* vertical letterbox offset = (FH - UI_H)/2 (0 if none) */

static short     g_old_mode;
static void     *g_old_phys, *g_old_log;

void rdr_prefer_truecolor(int on) { g_want_tc = on; }   /* honoured at rdr_init */

/* Zero the top + bottom letterbox bands (index 0 / RGB 0 = black — U6 palette entry 0 is black).
 * The image is presented into the middle UI_H rows; these bands are never otherwise written. */
static void falcon_clear_bands(void)
{
    if (g_yoff <= 0) return;
    int32_t bpp  = g_tc ? 2 : 1;                         /* bytes per pixel                     */
    int32_t band = (int32_t)g_yoff * FW * bpp;           /* one band, in bytes                  */
    memset(g_screen, 0, (size_t)band);                                   /* top: rows [0, g_yoff)        */
    memset(g_screen + (int32_t)(g_yoff + UI_H) * FW * bpp, 0,
           (size_t)((int32_t)(FH - g_yoff - UI_H) * FW * bpp));          /* bottom: rows [yoff+UI_H, FH)  */
}

/* Choose the VIDEL mode word for 320x240 on the attached monitor, at 8bpp (indexed) or 16bpp
 * (truecolor per g_tc).
 *  - VGA: native is 480 lines; VERTFLAG double-scans so 240 logical lines fill the screen (each
 *    shown twice). 40-column (COL40) = 320 px wide. BPS8 = 256c, BPS16 = 16-bit direct colour.
 *  - RGB/TV: 320x240 via standard 40-col (PAL ~240 visible lines; NTSC ~200 — VERTFLAG would
 *    interlace, so leave it off here). */
static short falcon_mode(void)
{
    short depth = (short)(g_tc ? BPS16 : BPS8);
    short mon = VgetMonitor();
    if (mon == MON_VGA)
        return (short)(VGA | VERTFLAG | depth | COL40);    /* 8bpp 0x113 / 16bpp 0x114 */
    return (short)(TV | PAL | depth | COL40);              /* 8bpp 0x023 / 16bpp 0x024 */
}

bool rdr_init(const platform_caps_t *caps)
{
    (void)caps;

    g_tc = g_want_tc ? 1 : 0;                            /* truecolor requested before init? */
    g_screen_bytes = (int32_t)FALC_PIXELS * (g_tc ? 2 : 1);  /* 16bpp = 2 bytes/pixel        */

    g_back   = (uint8_t *)plat_alloc_fast(FALC_PIXELS);         /* backbuffer is always 8-bit  */
    if (!g_back) g_back = (uint8_t *)plat_alloc_chip(FALC_PIXELS);  /* fast pool tight -> ST-RAM */
    g_screen = (uint8_t *)plat_alloc_chip(g_screen_bytes);      /* ST-RAM for VIDEL             */
    if (!g_back || !g_screen) return false;

    for (int32_t i = 0; i < g_screen_bytes; i++) g_screen[i] = 0;
    for (int32_t i = 0; i < FALC_PIXELS;     i++) g_back[i]   = 0;

    /* Letterbox ONLY on a VGA monitor: there the VIDEL runs 240 double-scanned lines (falcon_mode
     * VGA|VERTFLAG), so the 200-line UI is centred with 20px black bands. On an RGB/TV monitor the
     * visible area is ~200 lines, so any down-shift pushes the bottom off-screen — present top-aligned
     * (g_yoff=0) there. (VgetMonitor XBIOS 89; same detection falcon_mode() uses for the mode word.) */
    g_yoff = (FH > UI_H && VgetMonitor() == MON_VGA) ? (FH - UI_H) / 2 : 0;

    /* Save current video state, then switch VIDEL to our mode + buffer. */
    g_old_mode = VsetMode(VM_INQUIRE);
    g_old_phys = Physbase();
    g_old_log  = Logbase();

    VsetScreen(g_screen, g_screen, 3, falcon_mode());   /* rez==3 => Falcon mode */

    /* Hide the AES mouse sprite: like the TT backend, we run fullscreen non-GEM
     * pointing VIDEL at our own ST-RAM buffer; the still-live AES mouse VBL
     * handler would XOR its cursor into our screen. line-A $A000 init + $A00A
     * hide stops it without touching the AES. */
    linea0();
    lineaa();
    V_X_MAX = FW - 1;   /* 319 */
    V_Y_MAX = FH - 1;   /* 239 */
    return true;
}

void rdr_shutdown(void)
{
    linea9();                                              /* show mouse for desktop */
    VsetScreen(g_old_log, g_old_phys, 3, g_old_mode);     /* restore mode + base */
    Vsync();
}

void rdr_get_caps(rdr_caps *o) { o->width = FW; o->height = FH; o->colors = 256; o->indexed = true; }
int  rdr_width(void)  { return FW; }
int  rdr_height(void) { return FH; }
uint8_t *rdr_backbuffer(void) { return g_back; }

/* 6-bit U6 channel (0..63) -> 8-bit VIDEL channel (0..255), bit-replicated so
 * full-scale maps to 0xFF (a plain <<2 would top out at 0xFC). */
static inline uint32_t exp6(uint8_t v6)
{
    uint32_t v = (uint32_t)(v6 & 0x3F);
    return (v << 2) | (v >> 4);
}

void rdr_set_palette(const uint8_t *rgb6, int n)
{
    if (n > 256) n = 256;
    if (g_tc) {
        /* truecolor: build the RGB565 expand LUT (no hardware CLUT). U6 channels are 6-bit:
         * green maps 1:1 to the 6-bit field; red/blue drop 1 bit. The next present picks it up. */
        for (int i = 0; i < n; i++) {
            uint16_t r5 = (uint16_t)((rgb6[i*3+0] & 0x3F) >> 1);   /* 6-bit -> 5-bit */
            uint16_t g6 = (uint16_t)( rgb6[i*3+1] & 0x3F);         /* 6-bit -> 6-bit */
            uint16_t b5 = (uint16_t)((rgb6[i*3+2] & 0x3F) >> 1);   /* 6-bit -> 5-bit */
            g_pal16[i] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
        return;
    }
    for (int i = 0; i < n; i++) {
        uint32_t r = exp6(rgb6[i*3+0]);
        uint32_t g = exp6(rgb6[i*3+1]);
        uint32_t b = exp6(rgb6[i*3+2]);
        g_pal[i] = (r << 16) | (g << 8) | b;       /* 0x00RRGGBB */
    }
    VsetRGB(0, n, g_pal);
}

/* no-op: the Falcon shows all 256 VGA colours (VIDEL 8-bit / truecolor), so the pack's
 * low-colour (U6WW/U6WX) data is unused here. */
void rdr_set_lowcolor(const uint8_t *pal16, const uint8_t *lut256) { (void)pal16; (void)lut256; }
void rdr_lowcolor_cycle(const uint8_t *pal16) { (void)pal16; }   /* Falcon cycles the real CLUT via rdr_set_palette */
void rdr_set_ste_mode(int on) { (void)on; }                      /* STE 4-bit path is shifter-only */
int  rdr_is_ste(void) { return 0; }
void rdr_set_lowcolor_bands(int nb, const uint8_t *y0, const uint8_t *pal, const uint8_t *lut)
{ (void)nb; (void)y0; (void)pal; (void)lut; }

void rdr_debug_info(uint32_t out[6])
{
    out[0] = (uint32_t)(uintptr_t)Physbase();
    out[1] = (uint32_t)(uintptr_t)Logbase();
    out[2] = (uint32_t)(uintptr_t)g_screen;
    out[3] = (uint32_t)(uint16_t)VsetMode(VM_INQUIRE);
    out[4] = (uint32_t)(uint16_t)VgetMonitor();
    out[5] = (uint32_t)g_screen_bytes;
}

/* truecolor: expand `npix` indexed pixels src[] -> 16-bit RGB565 dst[] via the palette LUT */
static inline void tc_span(const uint8_t *src, uint16_t *dst, int npix)
{
    for (int i = 0; i < npix; i++) dst[i] = g_pal16[src[i]];
}

/* Full-screen present: the logical UI is UI_H tall; present it centred (offset g_yoff) with black
 * letterbox bands. truecolor = per-pixel 565 expand; else chunky -> 8 interleaved bitplanes. */
void rdr_present(void)
{
    if (g_tc) {
        tc_span(g_back, (uint16_t *)g_screen + (int32_t)g_yoff * FW, FW * UI_H);
        falcon_clear_bands();
        Vsync();
        return;
    }
    const int groups = FW / 16;                 /* 20 */
    for (int y = 0; y < UI_H; y++)
        c2p_span_asm(g_back + (int32_t)y * FW,
                     (uint16_t *)(g_screen + (int32_t)(y + g_yoff) * FW), groups);
    falcon_clear_bands();
    Vsync();
}

/* Present sub-rectangle [x,x+w) x [y,y+h); x,w must be multiples of 16. Coords are in the logical
 * UI (0..UI_H); the destination row is shifted down by g_yoff so the whole UI stays letterboxed. */
void rdr_present_rect(int x, int y, int w, int h)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > FW) w = FW - x;
    if (y + h > UI_H) h = UI_H - y;
    if (h < 0) h = 0;
    if (g_tc) {
        for (int yy = y; yy < y + h; yy++)
            tc_span(g_back + (int32_t)yy * FW + x,
                    (uint16_t *)g_screen + (int32_t)(yy + g_yoff) * FW + x, w);
        return;
    }
    int g0 = x / 16, gn = w / 16;
    for (int yy = y; yy < y + h; yy++)
        c2p_span_asm(g_back + (int32_t)yy * FW + g0 * 16,
                     (uint16_t *)(g_screen + (int32_t)(yy + g_yoff) * FW) + g0 * 8, gn);
}

/* On the Falcon the UI is shown 1:1 (no 2x vertical doubling the TT needs), but the 200-line UI is
 * CENTRED in the 240-line VIDEL screen with black letterbox bands (20px top + 20px bottom) so the
 * VGA display isn't top-aligned with a grey strip below. `lh` is the logical UI height; we present
 * those rows at offset g_yoff and clear the bands. */
void rdr_present_doubled(int lh)
{
    if (lh < 1) lh = 1;
    if (lh > FH - g_yoff) lh = FH - g_yoff;
    if (g_tc) {
        tc_span(g_back, (uint16_t *)g_screen + (int32_t)g_yoff * FW, FW * lh);
        falcon_clear_bands();
        Vsync();
        return;
    }
    const int groups = FW / 16;
    for (int y = 0; y < lh; y++)
        c2p_span_asm(g_back + (int32_t)y * FW,
                     (uint16_t *)(g_screen + (int32_t)(y + g_yoff) * FW), groups);
    falcon_clear_bands();
    Vsync();
}

void rdr_present_rect_doubled(int x, int y, int w, int h, int lh)
{
    (void)lh;
    rdr_present_rect(x, y, w, h);
}

#ifdef BENCH
/* BENCH-only: the intro bench splits present into c2p vs output; the Falcon present is a truecolor
 * expand+blit (no separate c2p pass), so report zeros. The game-frame bench (make bench-falcon) times
 * the whole rect present directly and doesn't use this. */
void rdr_bench_present(uint32_t *c2p, uint32_t *out){ *c2p = 0; *out = 0; }
#endif

/* Hardware horizontal scroll — NOT YET implemented on the Falcon VIDEL backend, so
 * scroll_begin returns false and the intro uses the CPU scroll (same as the TT). Unlike
 * TT-low, the Falcon CAN do wide-buffer hardware scroll: VIDEL line-width $FF8210 is a WORD
 * register (no 256-word byte cap like the TT's $FF820F) and $FF8265 fine-scroll works — so a
 * real VIDEL scroll backend here is the next step to take full advantage of the Falcon.
 * rdr_test_hscroll is a stub (the linchpin probe is TT-specific). */
bool scroll_begin(const uint8_t *chunky, int cw, int ch) { (void)chunky; (void)cw; (void)ch; return false; }
void scroll_pan(int pan_x)  { (void)pan_x; }
void scroll_end(void)       { }
void rdr_test_hscroll(int v) { (void)v; }

/* Incremental horizontal scroll (render.h): shift the 1:1 screen LEFT by one 16-px group in
 * place, then c2p only the new rightmost group from `chunky` at source-x=pan_x. (VIDEL is also
 * 8 interleaved planes, so the same c2p applies; no 2x doubling/letterbox here.) */
void rdr_scroll_left16(const uint8_t *chunky, int cw, int lh, int pan_x)
{
    if (lh < 1) lh = 1;
    if (lh > FH - g_yoff) lh = FH - g_yoff;
    int last = (FW/16 - 1) * 16;
    if (pan_x < 0) pan_x = 0;
    if (g_tc) {
        for (int y = 0; y < lh; y++) {
            uint16_t *r = (uint16_t *)g_screen + (int32_t)(y + g_yoff) * FW;
            memmove(r, r + 16, (size_t)(FW - 16) * 2);          /* shift left one 16-px group  */
            tc_span(chunky + (int32_t)y*cw + pan_x + last, r + last, 16);  /* expand new group */
        }
        return;
    }
    for (int y = 0; y < lh; y++) {
        uint8_t *r = g_screen + (int32_t)(y + g_yoff) * FW;
        memmove(r, r + 16, (size_t)(FW - 16));                  /* planar shift left one group */
        const uint8_t *px = chunky + (int32_t)y*cw + pan_x + last;
        c2p_span_asm(px, (uint16_t *)(r + last), 1);            /* c2p the new rightmost group */
    }
}

/* Double-buffer flip — not yet on the Falcon VIDEL backend (VsetScreen can flip the base
 * cleanly; a real impl is a small follow-up). No-op stubs keep single-buffer behaviour. */
void rdr_flip_begin(void) { }
void rdr_flip_end(void)   { }

RDR_MAKE_VTABLE(falcon);      /* -> const rdr_vtable rdr_be_falcon */
