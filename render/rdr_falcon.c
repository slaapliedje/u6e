// SPDX-License-Identifier: GPL-2.0-or-later
/* rdr_falcon.c — Falcon030 VIDEL 320x240 256-color backend.
 *
 * Why this backend exists alongside rdr_tt.c: the TT's only practical 256-color
 * mode on a VGA monitor is TT-low 320x480, so the 320x200 game UI must be shown
 * 2x-vertically-doubled and letterboxed (see rdr_tt.c rdr_present_doubled). The
 * Falcon's VIDEL can show 320x240 natively (double-scanned by hardware on VGA),
 * so we present the UI 1:1 — no software doubling, no letterbox. The Falcon is a
 * first-class target (better sound + flexible resolutions); the TT is the
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

extern void c2p_span_asm(const uint8_t *src, uint16_t *dst, long ngroups);

#define FW 320
#define FH 240
#define FALC_SCREEN_BYTES (FW * FH)     /* 8bpp planar == width*height bytes */

static uint8_t  *g_screen;              /* visible planar screen (ST-RAM)  */
static uint8_t  *g_back;                /* chunky 8-bit backbuffer          */
static uint32_t  g_pal[256];            /* VIDEL 0x00RRGGBB entries         */

static short     g_old_mode;
static void     *g_old_phys, *g_old_log;

/* Choose the VIDEL mode word for 320x240x8bpp on the attached monitor.
 *  - VGA: native is 480 lines; VERTFLAG double-scans so 240 logical lines fill
 *    the screen (each shown twice). 40-column (COL40) = 320 px wide. BPS8 = 256c.
 *  - RGB/TV: 320x240 via standard 40-col BPS8 (PAL gives ~240 visible lines;
 *    NTSC ~200 — VERTFLAG would interlace, so leave it off here). */
static short falcon_mode(void)
{
    short mon = VgetMonitor();
    if (mon == MON_VGA)
        return (short)(VGA | VERTFLAG | BPS8 | COL40);     /* 0x113 */
    return (short)(TV | PAL | BPS8 | COL40);               /* 0x023 */
}

bool rdr_init(const platform_caps_t *caps)
{
    (void)caps;

    g_back   = (uint8_t *)plat_alloc_fast(FALC_SCREEN_BYTES);
    if (!g_back) g_back = (uint8_t *)plat_alloc_chip(FALC_SCREEN_BYTES);  /* fast pool tight -> ST-RAM */
    g_screen = (uint8_t *)plat_alloc_chip(FALC_SCREEN_BYTES);   /* ST-RAM for VIDEL */
    if (!g_back || !g_screen) return false;

    for (int32_t i = 0; i < FALC_SCREEN_BYTES; i++) g_screen[i] = 0;
    for (int32_t i = 0; i < FALC_SCREEN_BYTES; i++) g_back[i]   = 0;

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
void rdr_set_lowcolor_bands(int nb, const uint8_t *y0, const uint8_t *pal, const uint8_t *lut)
{ (void)nb; (void)y0; (void)pal; (void)lut; }

void rdr_debug_info(uint32_t out[6])
{
    out[0] = (uint32_t)(uintptr_t)Physbase();
    out[1] = (uint32_t)(uintptr_t)Logbase();
    out[2] = (uint32_t)(uintptr_t)g_screen;
    out[3] = (uint32_t)(uint16_t)VsetMode(VM_INQUIRE);
    out[4] = (uint32_t)(uint16_t)VgetMonitor();
    out[5] = (uint32_t)FALC_SCREEN_BYTES;
}

/* Full-screen present: chunky -> 8 interleaved bitplanes, 1:1 (no doubling). */
void rdr_present(void)
{
    const int groups = FW / 16;                 /* 20 */
    for (int y = 0; y < FH; y++)
        c2p_span_asm(g_back + (int32_t)y * FW,
                     (uint16_t *)(g_screen + (int32_t)y * FW), groups);
    Vsync();
}

/* Present sub-rectangle [x,x+w) x [y,y+h); x,w must be multiples of 16. */
void rdr_present_rect(int x, int y, int w, int h)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > FW) w = FW - x;
    if (y + h > FH) h = FH - y;
    int g0 = x / 16, gn = w / 16;
    for (int yy = y; yy < y + h; yy++)
        c2p_span_asm(g_back + (int32_t)yy * FW + g0 * 16,
                     (uint16_t *)(g_screen + (int32_t)yy * FW) + g0 * 8, gn);
}

/* On the Falcon the UI is shown 1:1 (no 2x vertical doubling / letterbox the TT
 * needs), so the "_doubled" entry points just present the region natively. `lh`
 * is the logical UI height; we present those rows top-aligned and leave any rows
 * below black (the backbuffer is zero-cleared at init and the UI is <= lh tall). */
void rdr_present_doubled(int lh)
{
    if (lh < 1) lh = 1;
    if (lh > FH) lh = FH;
    const int groups = FW / 16;
    for (int y = 0; y < lh; y++)
        c2p_span_asm(g_back + (int32_t)y * FW,
                     (uint16_t *)(g_screen + (int32_t)y * FW), groups);
    Vsync();
}

void rdr_present_rect_doubled(int x, int y, int w, int h, int lh)
{
    (void)lh;
    rdr_present_rect(x, y, w, h);
}

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
    if (lh > FH) lh = FH;
    int last = (FW/16 - 1) * 16;
    if (pan_x < 0) pan_x = 0;
    for (int y = 0; y < lh; y++) {
        uint8_t *r = g_screen + (int32_t)y * FW;
        memmove(r, r + 16, (size_t)(FW - 16));                  /* planar shift left one group */
        const uint8_t *px = chunky + (int32_t)y*cw + pan_x + last;
        c2p_span_asm(px, (uint16_t *)(r + last), 1);            /* c2p the new rightmost group */
    }
}

/* Double-buffer flip — not yet on the Falcon VIDEL backend (VsetScreen can flip the base
 * cleanly; a real impl is a small follow-up). No-op stubs keep single-buffer behaviour. */
void rdr_flip_begin(void) { }
void rdr_flip_end(void)   { }
