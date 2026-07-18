// SPDX-License-Identifier: GPL-2.0-or-later
/* rdr_tt.c — TT-low 256-color backend (M3 first backend).
 *
 * TT-low is 320x480, 256 colors, stored as 8 INTERLEAVED bitplanes: each group
 * of 16 pixels is 8 consecutive 16-bit words (one per plane); pixel p's colour
 * bit b lives in bit (15-p) of word b. Present = chunky -> this layout (c2p).
 *
 * TOS calls used (named for verification):
 *   EgetShift() / EsetShift(mode)   : XBIOS 0x51/0x50 — read/set TT video mode.
 *   Setscreen(log,phys,-1)          : XBIOS 5 — point video at our ST-RAM buffer.
 *   EsetPalette(0,256,ptr)          : XBIOS 0x52 — load the 256-entry TT palette.
 *   Physbase()/Logbase() (XBIOS 2/3), Vsync() (XBIOS 0x25).
 * Screen buffer is ST-RAM (plat_alloc_chip) — the shifter requires chip RAM.
 */
#include "render.h"
#include "platmem.h"

#include <osbind.h>
#include <mint/ostruct.h>   /* TT_LOW, ES_MODE */
#include <mint/linea.h>     /* line-A: hide the AES mouse sprite */
#include <string.h>         /* memcpy — row-double the planar output */

#define RDR_BACKEND tt        /* namespace this backend's entry points; emit rdr_be_tt (below) */
#include "render_backend.h"

/* Hand-asm chunky->planar (render/c2p_asm.s). Converts `ngroups` consecutive
 * 16-pixel groups (src) to plane words (dst, 8 u16/group, contiguous), byte-
 * identical to c2p_group(). The sanctioned asm hot path; movem amortised across
 * the span. See docs/design/performance.md. */
extern void c2p_span_asm(const uint8_t *src, uint16_t *dst, long ngroups);

#define TTW 320
#define TTH 480
#define TT_SCREEN_BYTES (TTW * TTH)     /* 8bpp planar == width*height bytes */

static uint8_t  *g_screen;              /* planar screen A (ST-RAM)        */
static uint8_t  *g_back;                /* chunky 8-bit backbuffer         */
static uint16_t  g_ttpal[256];          /* TT 0x0RGB palette               */
static uint8_t  *g_screen2;             /* planar screen B (double-buffer flip) */
static uint8_t  *g_vis;                 /* currently DISPLAYED buffer      */
static int       g_flip;                /* double-buffer (tear-free) mode active */

#ifdef BENCH
/* BENCH-only: split rdr_present_doubled's cost into the c2p+double loop vs the "output"
 * (letterbox clear + flip/Vsync), so the intro bench can attribute each to its phase. Coarse
 * 200Hz boundaries only (a handful of Supexec traps/frame — negligible vs a ~117ms present). */
extern uint32_t plat_ticks_200hz(void);
static uint32_t g_bench_c2p, g_bench_out;      /* cumulative 200Hz ticks */
void rdr_bench_present(uint32_t *c2p, uint32_t *out){ *c2p=g_bench_c2p; *out=g_bench_out; }
#endif

static short     g_old_shift;
static void     *g_old_phys, *g_old_log;

bool rdr_init(const platform_caps_t *caps)
{
    (void)caps;   /* M3: TT backend only; other targets land later */

    /* Backbuffer prefers the fast (TT-RAM) pool for burst-mode composition. On a 4 MB-TT-RAM
     * machine the program's TPA also loads into TT-RAM (~1.3 MB), so the fast pool is only ~2.7 MB;
     * once the asset pack (~2.4 MB) + scratch are in, a full-screen backbuffer can just miss. Fall
     * back to the ST-RAM (chip) pool in that case — slower CPU access, but we boot instead of dying
     * with "no renderer". g_screen MUST stay chip (the shifter DMA-fetches it). */
    g_back = (uint8_t *)plat_alloc_fast(TT_SCREEN_BYTES);
    if (!g_back) g_back = (uint8_t *)plat_alloc_chip(TT_SCREEN_BYTES);
    g_screen = (uint8_t *)plat_alloc_chip(TT_SCREEN_BYTES);   /* chip RAM (shifter reads this) */
    if (!g_back || !g_screen) return false;
    g_vis = g_screen;                                          /* displayed buffer (single-buf) */

    /* save current video state, then switch to TT-low + our screen */
    g_old_shift = EgetShift();
    g_old_phys  = Physbase();
    g_old_log   = Logbase();

    EsetShift(TT_LOW);
    Setscreen(g_screen, g_screen, -1);    /* log+phys = our buffer, keep rez */

    /* Hide the AES mouse sprite. We run fullscreen non-GEM and point the video
     * at our own ST-RAM buffer; the still-active AES mouse VBL handler would
     * otherwise XOR its cursor into our planar screen using the desktop's old
     * geometry, leaving a moving garbage rectangle. line-A $A000 init + $A00A
     * (hide mouse) stop it without touching the AES. (We'll later draw our own
     * cursor into the backbuffer for the game's mouse UI — see milestone.) */
    linea0();
    lineaa();

    /* line-A's screen geometry still reflects the BOOT resolution (EsetShift switches
     * the shifter but does NOT reinit the VDI/line-A). The line-A mouse handler clamps
     * GCURX/GCURY to V_X_MAX/V_Y_MAX, so on a TT that booted ~200 rows tall the cursor
     * could only reach the top of our 320x480 screen. Point them at TT-low so the
     * pointer ranges the FULL screen. (read_mouse_logical un-doubles GCURY.) */
    V_X_MAX = TTW - 1;   /* 319 */
    V_Y_MAX = TTH - 1;   /* 479 */

    for (int32_t i = 0; i < TT_SCREEN_BYTES; i++) g_screen[i] = 0;
    for (int32_t i = 0; i < TT_SCREEN_BYTES; i++) g_back[i] = 0;
    return true;
}

void rdr_shutdown(void)
{
    linea9();                             /* line-A $A009: show mouse for the desktop */
    Setscreen(g_old_log, g_old_phys, -1);
    EsetShift(g_old_shift);
    Vsync();
}

void rdr_get_caps(rdr_caps *o) { o->width = TTW; o->height = TTH; o->colors = 256; o->indexed = true; }
int  rdr_width(void)  { return TTW; }
int  rdr_height(void) { return TTH; }
uint8_t *rdr_backbuffer(void) { return g_back; }

/* 6-bit channel (0..63) -> TT CLUT nibble. The TT 256-colour palette uses
 * STRAIGHT linear 4-bit values (0x0RGB) — verified with an on-screen gray ramp
 * (straight = smooth ramp; the STE bit-rotated form scrambled it). So just
 * take the top 4 bits. (The STE 16-colour shifter DOES use the rotated form;
 * that's the ste backend's concern, M7 — not the TT's.) */
static uint16_t hw_nibble(uint8_t v6)
{
    return (uint16_t)((v6 >> 2) & 0x0F);
}

void rdr_set_palette(const uint8_t *rgb6, int n)
{
    if (n > 256) n = 256;
    for (int i = 0; i < n; i++) {
        uint16_t r = hw_nibble(rgb6[i*3+0]);
        uint16_t g = hw_nibble(rgb6[i*3+1]);
        uint16_t b = hw_nibble(rgb6[i*3+2]);
        g_ttpal[i] = (uint16_t)((r << 8) | (g << 4) | b);
    }
    EsetPalette(0, n, g_ttpal);
}

/* no-op: the TT shows all 256 VGA colours, so the pack's low-colour (U6WW/U6WX) data is unused. */
void rdr_set_lowcolor(const uint8_t *pal16, const uint8_t *lut256) { (void)pal16; (void)lut256; }
void rdr_set_lowcolor_bands(int nb, const uint8_t *y0, const uint8_t *pal, const uint8_t *lut)
{ (void)nb; (void)y0; (void)pal; (void)lut; }
void rdr_lowcolor_cycle(const uint8_t *pal16) { (void)pal16; }   /* TT cycles the real 256 CLUT via rdr_set_palette */
void rdr_set_ste_mode(int on) { (void)on; }                      /* STE 4-bit path is shifter-only */
void rdr_prefer_truecolor(int on) { (void)on; }                  /* TT-low stays 8-bit indexed     */
int  rdr_is_ste(void) { return 0; }

void rdr_debug_info(uint32_t out[6])
{
    out[0] = (uint32_t)(uintptr_t)Physbase();
    out[1] = (uint32_t)(uintptr_t)Logbase();
    out[2] = (uint32_t)(uintptr_t)g_screen;
    out[3] = (uint32_t)(uint16_t)EgetShift();
    out[4] = (uint32_t)(uint16_t)Getrez();
    out[5] = (uint32_t)TT_SCREEN_BYTES;
}

/* C REFERENCE for c2p_span_asm (render/c2p_asm.s) — the asm must stay byte-identical
 * to this. Chunky 8-bit -> 8 plane words for one 16-pixel group (pixel 0 in bit 15);
 * 8 plane accumulators, one shift/or per pixel per plane. The present functions now use
 * the hand-asm span version (a uint64 bit-transpose was also tried and was SLOWER — the
 * 68020 is 32-bit, so 64-bit ops are emulated). Kept as the spec + fallback; see
 * docs/design/performance.md. */
__attribute__((unused))
static void c2p_group(const uint8_t *px, uint16_t out[8])
{
    uint16_t p0=0,p1=0,p2=0,p3=0,p4=0,p5=0,p6=0,p7=0;
    for (int p = 0; p < 16; p++) {
        unsigned c = px[p];
        p0=(uint16_t)((p0<<1)|(c&1));      p1=(uint16_t)((p1<<1)|((c>>1)&1));
        p2=(uint16_t)((p2<<1)|((c>>2)&1)); p3=(uint16_t)((p3<<1)|((c>>3)&1));
        p4=(uint16_t)((p4<<1)|((c>>4)&1)); p5=(uint16_t)((p5<<1)|((c>>5)&1));
        p6=(uint16_t)((p6<<1)|((c>>6)&1)); p7=(uint16_t)((p7<<1)|((c>>7)&1));
    }
    out[0]=p0;out[1]=p1;out[2]=p2;out[3]=p3;out[4]=p4;out[5]=p5;out[6]=p6;out[7]=p7;
}

/* chunky 8-bit -> 8 interleaved bitplanes (TT-low).
 * Branchless: 8 plane accumulators (one per plane) shifted a bit per pixel —
 * after 16 pixels each holds that plane's 16-bit word (pixel 0 in bit 15). No
 * per-bit branches; the 8 accumulators live in 68k data registers. ~3x faster
 * than the branchy version (measured). Produces byte-identical output. */
void rdr_present(void)
{
    const int groups = TTW / 16;
    for (int y = 0; y < TTH; y++) {
        const uint8_t *px  = g_back + (int32_t)y * TTW;
        uint16_t      *dst = (uint16_t *)(g_screen + (int32_t)y * TTW);
        c2p_span_asm(px, dst, groups);
    }
    Vsync();
}

/* c2p only the sub-rectangle [x,y,w,h). x,w must be multiples of 16. */
void rdr_present_rect_doubled(int x, int y, int w, int h, int lh)
{
    if (lh < 1) lh = 1;
    if (lh*2 > TTH) lh = TTH/2;
    int top = (TTH - lh*2) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > TTW) w = TTW - x;
    if (y + h > lh) h = lh - y;
    int g0 = x / 16, gn = w / 16;
#ifdef BENCH
    uint32_t tb = plat_ticks_200hz();
#endif
    for (int yy = y; yy < y + h; yy++) {
        const uint8_t *px = g_back + (int32_t)yy * TTW + g0 * 16;
        uint8_t *r0 = g_screen + (int32_t)(top + yy*2)   * TTW + g0 * 16;
        uint8_t *r1 = g_screen + (int32_t)(top + yy*2+1) * TTW + g0 * 16;
        c2p_span_asm(px, (uint16_t *)r0, gn);   /* c2p the even row ... */
        memcpy(r1, r0, (size_t)gn * 16);        /* ... then duplicate it (2x vertical) */
    }
#ifdef BENCH
    g_bench_c2p += plat_ticks_200hz() - tb;     /* rect c2p+double (no letterbox/flip here) */
#endif
    /* no Vsync: caller batches / the main loop Vsyncs */
}

void rdr_present_rect(int x, int y, int w, int h)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > TTW) w = TTW - x;
    if (y + h > TTH) h = TTH - y;
    int g0 = x / 16, gn = w / 16;
    for (int yy = y; yy < y + h; yy++) {
        const uint8_t *px  = g_back + (int32_t)yy * TTW + g0 * 16;
        uint16_t      *dst = (uint16_t *)(g_screen + (int32_t)yy * TTW) + g0 * 8;
        c2p_span_asm(px, dst, gn);
    }
    /* no Vsync here: caller may present several rects per frame */
}

/* Present the top 320 x lh of the backbuffer scaled 2x vertically, centred in
 * the 320x480 TT screen (black bands top/bottom). Each logical row's 8 plane
 * words are computed once and written to TWO adjacent screen rows. */
void rdr_present_doubled(int lh)
{
    if (lh < 1) lh = 1;
    if (lh*2 > TTH) lh = TTH/2;
    int top = (TTH - lh*2) / 2;
    const int groups = TTW / 16;
    /* In flip (double-buffer) mode, c2p into the HIDDEN buffer, then swap the video base at
     * the frame boundary so the viewer never sees a half-converted screen (tear-free). */
    uint8_t *dst = (g_flip && g_vis == g_screen) ? g_screen2 : g_screen;
    if (g_flip && g_vis == g_screen2) dst = g_screen;
#ifdef BENCH
    uint32_t tb0 = plat_ticks_200hz();
#endif
    /* black letterbox bands above/below the doubled image */
    for (int32_t i = 0; i < (int32_t)top * TTW; i++) dst[i] = 0;
    for (int32_t i = (int32_t)(top + lh*2) * TTW; i < TT_SCREEN_BYTES; i++) dst[i] = 0;
#ifdef BENCH
    uint32_t tb1 = plat_ticks_200hz(); g_bench_out += tb1 - tb0;   /* letterbox = output */
#endif
    for (int y = 0; y < lh; y++) {
        const uint8_t *px = g_back + (int32_t)y * TTW;
        uint8_t *r0 = dst + (int32_t)(top + y*2)   * TTW;
        uint8_t *r1 = dst + (int32_t)(top + y*2+1) * TTW;
        c2p_span_asm(px, (uint16_t *)r0, groups);   /* c2p the even row ... */
        memcpy(r1, r0, TTW);                         /* ... then duplicate it (2x vertical) */
    }
#ifdef BENCH
    uint32_t tb2 = plat_ticks_200hz(); g_bench_c2p += tb2 - tb1;   /* c2p + line-double loop */
#endif
    if (g_flip) { Setscreen(dst, dst, -1); Vsync(); g_vis = dst;   /* flip + swap */
#ifdef BENCH
        g_bench_out += plat_ticks_200hz() - tb2;                   /* flip/Vsync = output */
#endif
        return; }
    Vsync();
#ifdef BENCH
    g_bench_out += plat_ticks_200hz() - tb2;
#endif
}

/* Incremental horizontal scroll (render.h): shift the doubled+letterboxed screen LEFT by one
 * 16-px group in place, then c2p only the new rightmost group from `chunky` at source-x=pan_x.
 * Per step this is ~1/20 the c2p of a full present (one group/row vs 20) plus two cheap block
 * moves. Single-buffer: writes g_screen (the visible buffer). No Vsync — the caller paces. */
void rdr_scroll_left16(const uint8_t *chunky, int cw, int lh, int pan_x)
{
    if (lh < 1) lh = 1;
    if (lh*2 > TTH) lh = TTH/2;
    int top  = (TTH - lh*2) / 2;
    int last = (TTW/16 - 1) * 16;            /* byte offset of the rightmost group (304) */
    if (pan_x < 0) pan_x = 0;
    for (int y = 0; y < lh; y++) {
        uint8_t *r0 = g_screen + (int32_t)(top + y*2)   * TTW;
        uint8_t *r1 = g_screen + (int32_t)(top + y*2+1) * TTW;
        memmove(r0, r0 + 16, (size_t)(TTW - 16));               /* planar shift left one group */
        const uint8_t *px = chunky + (int32_t)y*cw + pan_x + last;
        c2p_span_asm(px, (uint16_t *)(r0 + last), 1);           /* c2p the new rightmost group */
        memcpy(r1, r0, TTW);                                     /* duplicate to the odd row (2x) */
    }
}

/* Enter double-buffered (tear-free) mode: rdr_present_doubled draws into a hidden buffer and
 * flips at the frame boundary. Used for full-screen animation (the intro). No-op if a second
 * ST-RAM buffer can't be allocated (stays single-buffered). */
void rdr_flip_begin(void)
{
    if (!g_screen2) { g_screen2 = (uint8_t *)plat_alloc_chip(TT_SCREEN_BYTES);
                      if (g_screen2) for (int32_t i=0;i<TT_SCREEN_BYTES;i++) g_screen2[i]=0; }
    if (g_screen2) g_flip = 1;
}

/* Leave double-buffered mode: make g_screen the displayed buffer again (the game's normal
 * single-buffer path draws there), copying the last visible frame across if needed. */
void rdr_flip_end(void)
{
    if (!g_flip) return;
    g_flip = 0;
    if (g_vis != g_screen) memcpy(g_screen, g_vis, TT_SCREEN_BYTES);
    Setscreen(g_screen, g_screen, -1); Vsync();
    g_vis = g_screen;
}

/* --- LINCHPIN TEST: does TT-low (8-plane) honour the STE $FF8265 fine-scroll register? ---
 * Writes the horizontal fine-scroll register in supervisor mode. If TT-low honours it, the
 * displayed image shifts left by v (0..15) pixels. Decides whether hardware fine-scroll is
 * available on the TT or we must fall back to coarse (base-addr) + software fine. */
static volatile int g_test_hs;
static long rdr__set_hs(void){ *(volatile uint8_t *)0xFFFF8265UL = (uint8_t)(g_test_hs & 0x0F); return 0; }
void rdr_test_hscroll(int v){ g_test_hs = v; (void)Supexec(rdr__set_hs); }

/* ---- HARDWARE HORIZONTAL SCROLL (TT backend) -------------------------------------
 * A wide (SCW x SCH) 8-plane planar buffer. c2p the wide chunky frame into it ONCE
 * (scroll_begin), then pan with pure hardware (scroll_pan): base-address for the 16px
 * coarse part, $FF8265 for the 0-15px fine part, line-offset $FF820F so the 320px screen
 * window sits inside the SCW-wide buffer. Registers are written via Supexec (supervisor)
 * right after Vsync so the shifter latches them atomically at the top of frame (no tear). */
/* $FF820F is "scanline width in WORDS - 1" (byte, max 255 = 256 words). In TT-low 8-plane
 * that caps the line at 256 words = 512 px. So the wide buffer is 512 px (192 px of pan). */
#define SCW 512                              /* wide buffer width  (byte line-width limit) */
#define SCH TTH                              /* == 480 (doubled image + letterbox)      */
static uint8_t         *g_wide;              /* wide planar scroll buffer (ST-RAM)      */
static volatile uint32_t g_sc_base;          /* pending video base address              */
static volatile uint8_t  g_sc_hs, g_sc_loff; /* pending fine-scroll + line-offset words */

static long sc_commit(void)                  /* supervisor: latch base + offset + hscroll */
{
    volatile uint8_t *b = (volatile uint8_t *)0xFFFF8200UL;
    b[0x01] = (uint8_t)(g_sc_base >> 16);    /* $FF8201 video base high   */
    b[0x03] = (uint8_t)(g_sc_base >> 8);     /* $FF8203 video base mid    */
    b[0x0D] = (uint8_t)(g_sc_base & 0xFE);   /* $FF820D video base low (STE/TT), keep even */
    b[0x0F] = g_sc_loff;                     /* $FF820F line offset (extra words/line)    */
    *(volatile uint8_t *)0xFFFF8265UL = g_sc_hs & 0x0F;   /* $FF8265 horiz fine scroll     */
    return 0;
}

/* c2p a cw x ch chunky frame into the wide buffer (doubled 2x vertically, centred with
 * black letterbox), point the video at it, set the line offset. Returns false if too big. */
bool scroll_begin(const uint8_t *chunky, int cw, int ch)
{
    /* VERIFIED on Hatari TT (2026-07-10): TT-low (8-plane/256-colour) honours the STE
     * horizontal fine-scroll register $FF8265 but does NOT honour the $FF820F line-width
     * register — a wider-than-screen buffer garbles regardless of the value (both 640px
     * offset=160 and 512px words-1=255 tried). TT-specific resolutions appear to use a
     * fixed line width. So hardware horizontal scroll is unavailable in TT-low: return
     * false and let the caller use the CPU scroll. The register path below is correct for
     * modes that DO support line-width (STE 4-plane / Falcon) and is kept for those. */
    (void)chunky; (void)cw; (void)ch;
    return false;
#if 0
    if (ch*2 > SCH) return false;
    if (!g_wide) { g_wide = (uint8_t *)plat_alloc_chip(SCW * SCH); if (!g_wide) return false; }
    for (int32_t i = 0; i < SCW * SCH; i++) g_wide[i] = 0;   /* black (incl. letterbox) */
    int cw_use = cw > SCW ? SCW : cw;                        /* clamp copy width to the buffer */
    int top = (SCH - ch*2) / 2, groups = cw_use / 16;
    for (int y = 0; y < ch; y++) {
        const uint8_t *px = chunky + (int32_t)y * cw;        /* read at the SOURCE stride (cw) */
        uint8_t *r0 = g_wide + (int32_t)(top + y*2)   * SCW;
        uint8_t *r1 = g_wide + (int32_t)(top + y*2+1) * SCW;
        c2p_span_asm(px, (uint16_t *)r0, groups);            /* even row (cw_use px) ... */
        memcpy(r1, r0, SCW);                                 /* ... duplicated (2x vertical) */
    }
    g_sc_loff = (uint8_t)(SCW / 16 * 8 - 1);                 /* scanline width in WORDS - 1 = 255 */
    g_sc_base = (uint32_t)(uintptr_t)g_wide;
    g_sc_hs   = 0;
    Vsync(); (void)Supexec(sc_commit);
    return true;
#endif
}

/* pan to absolute horizontal offset pan_x (0 .. SCW-320) — coarse via base, fine via $FF8265 */
void scroll_pan(int pan_x)
{
    if (pan_x < 0) pan_x = 0; if (pan_x > SCW - TTW) pan_x = SCW - TTW;
    g_sc_base = (uint32_t)(uintptr_t)g_wide + (uint32_t)(pan_x >> 4) * 16;  /* 16 bytes / 16px */
    g_sc_hs   = (uint8_t)(pan_x & 0x0F);
    Vsync(); (void)Supexec(sc_commit);
}

/* restore normal video: line offset 0, fine 0, base back to the standard screen buffer */
void scroll_end(void)
{
    g_sc_loff = 0; g_sc_hs = 0; g_sc_base = (uint32_t)(uintptr_t)g_screen;
    Vsync(); (void)Supexec(sc_commit);
    Setscreen(g_screen, g_screen, -1);
}

RDR_MAKE_VTABLE(tt);          /* -> const rdr_vtable rdr_be_tt */
