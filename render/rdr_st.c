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

static uint16_t  g_stpal[16];         /* ST/STE hardware palette words (packed by st_reg)      */
static uint8_t   g_lut[256];          /* active 256->16 remap (from U6WW)      */

/* STE 4096-colour mode (M7): a plain ST has 3 bits/channel (0..7), an STE 4 (0..15). When the engine
 * hands us STE 4-bit palettes (U6WS) on STE hardware it sets this; st_reg() then packs the 4-bit level
 * into the STE colour-register format, where each nibble is ROTATED so bit 3 holds the intensity LSB
 * ([2:0]=high 3 bits, [3]=LSB) — this is what keeps ST palettes forward-compatible on the STE. On a
 * plain ST (g_is_ste=0) we pack the classic 3-bit 0x0RGB. Setpalette writes these words either way. */
static int       g_is_ste;            /* 1 = pack 4-bit STE register format; 0 = 3-bit ST      */
void rdr_set_ste_mode(int on){ g_is_ste = on ? 1 : 0; }
int  rdr_is_ste(void){ return g_is_ste; }
static inline uint16_t st_reg(unsigned r, unsigned g, unsigned b)
{
    if (g_is_ste){
        #define STE_NIB(v) ((unsigned)((((v)&15u)>>1) | (((v)&1u)<<3)))   /* 4-bit level -> STE nibble */
        return (uint16_t)((STE_NIB(r) << 8) | (STE_NIB(g) << 4) | STE_NIB(b));
        #undef STE_NIB
    }
    return (uint16_t)(((r & 7u) << 8) | ((g & 7u) << 4) | (b & 7u));      /* classic ST 0x0RGB, 3-bit/ch */
}

/* --- band rasters (U6WX): up to MAXBANDS horizontal bands, each its own palette + LUT. The
 * Timer-B handler reloads the shifter registers at each band boundary; present() picks each
 * row's LUT from g_row_lut. nb<=1 => single palette, no raster (g_row_lut all -> g_lut). */
#define MAXBANDS 8
static int              g_nb;                        /* active band count (0/1 = no raster)  */
static uint8_t          g_band_y0[MAXBANDS];         /* first display row of each band       */
static uint16_t         g_band_reg[MAXBANDS][16];    /* per-band ST palette (0x0RGB) words   */
static uint8_t          g_band_lut[MAXBANDS][256];   /* per-band 256->16 remap               */
static const uint8_t   *g_row_lut[STH];              /* row -> that row's band LUT (present)  */
static volatile int     g_bidx;                      /* next band to load (Timer-B handler)  */
static int              g_raster_on;                 /* raster interrupts installed          */
static long raster_remove(void);                     /* defined below; used by rdr_shutdown  */

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
    for (int i = 0; i < STH; i++) g_row_lut[i] = g_lut;   /* single band until bands installed */

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
    if (g_raster_on) { (void)Supexec(raster_remove); g_raster_on = 0; }   /* stop the raster first */
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

/* Load the pack's host-quantised low-colour data: pal16 = 16 x {r,g,b} levels (3-bit on ST, 4-bit
 * on STE per g_is_ste; st_reg packs the right register format), lut256 = the 256->16 remap. */
void rdr_set_lowcolor(const uint8_t *pal16, const uint8_t *lut256)
{
    if (lut256) memcpy(g_lut, lut256, 256);
    if (pal16) {
        for (int i = 0; i < 16; i++)
            g_stpal[i] = st_reg(pal16[i*3+0], pal16[i*3+1], pal16[i*3+2]);
        Setpalette(g_stpal);
    }
    g_nb = 0;                                        /* single band: every row uses g_lut */
    for (int i = 0; i < STH; i++) g_row_lut[i] = g_lut;
}

/* ---- band rasters: Timer-B palette-swap chain (M7 "cheat past 16 colours") ------------------
 * The MFP (MC68901) Timer-B in EVENT-COUNT mode counts DE (display-enable) pulses = one per
 * displayed scanline. Program TBDR=N and it interrupts after N displayed lines. The VBL routine
 * loads band 0's 16 colours during vblank + arms Timer-B for the first boundary; each Timer-B
 * interrupt then loads the next band's colours straight into the shifter registers ($FF8240..)
 * and re-arms (stop/reload/restart, so an uneven interval takes effect immediately). Boundaries
 * are chosen at quiet (parchment) rows so a ±1-line timing slop is invisible. This is the classic
 * ST raster split — a few interrupts/frame, negligible cost. MFP: TBCR $FFFA1B, TBDR $FFFA21,
 * IERA/IMRA/ISRA bit0 = Timer-B (MFP channel 8, autovector $120). */
#define MFP_TBCR (*(volatile uint8_t *)0xFFFFFA1BUL)
#define MFP_TBDR (*(volatile uint8_t *)0xFFFFFA21UL)
#define MFP_IERA (*(volatile uint8_t *)0xFFFFFA07UL)
#define MFP_IMRA (*(volatile uint8_t *)0xFFFFFA13UL)
#define MFP_ISRA (*(volatile uint8_t *)0xFFFFFA0FUL)
#define MFP_IPRA (*(volatile uint8_t *)0xFFFFFA0BUL)
#define SHIFTER  ((volatile uint16_t *)0xFFFF8240UL)
#define TB_BIT   0x01

static void (**g_vblq)(void);        /* -> the system _vblqueue array         */
static int      g_vblslot = -1;      /* our slot in that array (-1 = none)    */
static uint32_t g_old_tbvec;

static inline void load_regs(const uint16_t *p)
{
    volatile uint16_t *s = SHIFTER;
    s[0]=p[0];   s[1]=p[1];   s[2]=p[2];   s[3]=p[3];   s[4]=p[4];   s[5]=p[5];   s[6]=p[6];   s[7]=p[7];
    s[8]=p[8];   s[9]=p[9];   s[10]=p[10]; s[11]=p[11]; s[12]=p[12]; s[13]=p[13]; s[14]=p[14]; s[15]=p[15];
}

/* Timer-B interrupt: load band g_bidx's palette, re-arm for the next boundary (or mask if last). */
static void tb_handler(void) __attribute__((interrupt_handler));
static void tb_handler(void)
{
    int i = g_bidx;
    if (i < g_nb) {
        load_regs(g_band_reg[i]);
        i++; g_bidx = i;
        if (i < g_nb) {
            MFP_TBCR = 0;                                          /* stop */
            MFP_TBDR = (uint8_t)(g_band_y0[i] - g_band_y0[i-1]);   /* lines to next boundary */
            MFP_TBCR = 0x08;                                       /* event-count restart */
        } else {
            MFP_IMRA = (uint8_t)(MFP_IMRA & ~TB_BIT);              /* done this frame: mask TB */
        }
    }
    MFP_ISRA = (uint8_t)~TB_BIT;                                   /* software EOI: clear in-service */
}

/* VBL queue routine (runs each frame during vblank): reload band 0 + arm the Timer-B chain. */
static void vbl_routine(void)
{
    if (g_nb <= 1) return;
    load_regs(g_band_reg[0]);
    g_bidx = 1;
    MFP_TBCR = 0;
    MFP_TBDR = g_band_y0[1];                        /* fire after y0[1] lines -> band 1 start */
    MFP_TBCR = 0x08;
    MFP_IPRA = (uint8_t)~TB_BIT;                    /* clear any pending */
    MFP_IERA = (uint8_t)(MFP_IERA | TB_BIT);
    MFP_IMRA = (uint8_t)(MFP_IMRA | TB_BIT);
}

static long raster_install(void)                   /* supervisor (Supexec) */
{
    g_old_tbvec = *(volatile uint32_t *)0x120UL;
    *(volatile uint32_t *)0x120UL = (uint32_t)(uintptr_t)&tb_handler;
    int nvbls = *(volatile int16_t *)0x454UL;       /* _nvbls */
    g_vblq = *(void (***)(void))0x456UL;            /* _vblqueue */
    g_vblslot = -1;
    for (int i = 0; i < nvbls; i++) if (!g_vblq[i]) { g_vblq[i] = &vbl_routine; g_vblslot = i; break; }
    return 0;
}
static long raster_remove(void)                    /* supervisor (Supexec) */
{
    MFP_IMRA = (uint8_t)(MFP_IMRA & ~TB_BIT);
    MFP_IERA = (uint8_t)(MFP_IERA & ~TB_BIT);
    if (g_vblslot >= 0) { g_vblq[g_vblslot] = 0; g_vblslot = -1; }
    if (g_old_tbvec) *(volatile uint32_t *)0x120UL = g_old_tbvec;
    return 0;
}

void rdr_set_lowcolor_bands(int nb, const uint8_t *y0, const uint8_t *pal, const uint8_t *lut)
{
    if (nb > MAXBANDS) nb = MAXBANDS;
    if (nb < 2 || !y0 || !pal || !lut) return;      /* nothing to raster: keep the single palette */

    for (int b = 0; b < nb; b++) {
        g_band_y0[b] = y0[b];
        for (int i = 0; i < 16; i++)
            g_band_reg[b][i] = st_reg(pal[b*48+i*3+0], pal[b*48+i*3+1], pal[b*48+i*3+2]);
        memcpy(g_band_lut[b], lut + (int32_t)b*256, 256);
    }
    for (int b = 0; b < nb; b++) {                  /* row -> band LUT for present-time c2p */
        int r0 = g_band_y0[b], r1 = (b+1 < nb) ? g_band_y0[b+1] : STH;
        if (r1 > STH) r1 = STH;
        for (int y = r0; y < r1; y++) g_row_lut[y] = g_band_lut[b];
    }
    g_nb = nb;
    for (int i = 0; i < 16; i++) g_stpal[i] = g_band_reg[0][i];
    Setpalette(g_stpal);                            /* band 0 = resting palette (XBIOS: supervisor-safe;
                                                     * a direct $FF8240 write here would bus-error in user mode) */

    if (!g_raster_on) { (void)Supexec(raster_install); g_raster_on = 1; }
    if (g_vblslot < 0) {                            /* no free vbl slot -> fall back to single band */
        g_nb = 0;
        for (int i = 0; i < STH; i++) g_row_lut[i] = g_lut;
        rdr_set_lowcolor(NULL, NULL);               /* re-assert the single palette */
    }
}

/* Colour cycling (M7): re-push the 16 shifter registers from pal16 — NO LUT change, NO c2p. The
 * engine rotates a few reserved slots (U6WZ) each tick; the shifter re-reads the CLUT so fire/water
 * animates for free (docs/design/palette_lowcolor.md). When a band raster is active the cycled ramp
 * lives in band 0 (the map), so update g_band_reg[0] too — the VBL reloads it next frame; Setpalette
 * sets the vblank resting palette (and drives the single-band case directly). XBIOS Setpalette is
 * supervisor-safe from user mode (a direct $FF8240 write would bus-error). */
void rdr_lowcolor_cycle(const uint8_t *pal16)
{
    if (!pal16) return;
    for (int i = 0; i < 16; i++) {
        uint16_t v = st_reg(pal16[i*3+0], pal16[i*3+1], pal16[i*3+2]);
        g_stpal[i] = v;
        if (g_nb >= 2) g_band_reg[0][i] = v;        /* band 0 = the map; the VBL reloads it next frame */
    }
    Setpalette(g_stpal);
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
        c2p4_lut(px, g_row_lut[y], dst, ST_GROUPS);
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
        c2p4_lut(px, g_row_lut[yy], dst, gn);
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
        c2p4_lut(px, g_row_lut[y], d, ST_GROUPS);
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
        c2p4_lut(px, g_row_lut[y], r + lastw, 1);           /* c2p the new rightmost group */
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
