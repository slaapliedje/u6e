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
#ifdef BENCH
#include "plattime.h"       /* plat_ticks_200hz for the BENCH selftest + planar microbench */
#endif

#include <osbind.h>
#include <mint/linea.h>     /* line-A: hide the AES mouse sprite */
#include <string.h>

#define RDR_BACKEND st        /* namespace this backend's entry points; emit rdr_be_st (see below) */
#include "render_backend.h"

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
static int       g_has_blitter;       /* Phase 3: BLiTTER present (from caps) -> masked-blit path */
static int       g_blit_compose;      /* use the Blitter in the live compose? default OFF (measured a wash:
                                       * the masked blit is a few ms of the ~400ms compose, dim dominates) */
static int       g_blit_present;      /* use the Blitter for the present? default ON where present (the
                                       * present is a big shifted block-copy the Blitter is built for) */

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
void rdr_prefer_truecolor(int on){ (void)on; }   /* shifter is 16-colour indexed; no truecolor */
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
 * per pixel (pixel 0 ends in bit 15). This is the C REFERENCE; the present path calls the
 * byte-identical hand-asm c2p4_lut_asm (render/c2p_st.s) — rdr_c2p_selftest() diffs them.
 * Kept compiled (used) as the correctness oracle + selftest source. */
extern void c2p4_lut_asm(const uint8_t *src, const uint8_t *lut, uint16_t *dst, int ngroups);
__attribute__((unused))
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
    g_has_blitter = caps && caps->has_blitter;   /* Phase 3: gate the Blitter masked-blit path */
    g_blit_present = g_has_blitter;              /* Blitter present is a clear win; on by default when present */

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
static void planar_mark_dirty(void);   /* defined with the planar block; LUT change -> planar caches stale */
void rdr_set_lowcolor(const uint8_t *pal16, const uint8_t *lut256)
{
    if (lut256){ memcpy(g_lut, lut256, 256); planar_mark_dirty(); }  /* new 256->16 LUT invalidates planar */
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
    planar_mark_dirty();                             /* the map's row-LUT (g_row_lut[PMAP_TOP]) changed */
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
        c2p4_lut_asm(px, g_row_lut[y], dst, ST_GROUPS);
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
        c2p4_lut_asm(px, g_row_lut[yy], dst, gn);
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
        c2p4_lut_asm(px, g_row_lut[y], d, ST_GROUPS);
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
        c2p4_lut_asm(px, g_lut, dst, gn);
    }
    /* no Vsync: caller batches */
}

/* ================= Native-planar compose (docs/design/native-planar-st.md) =================
 * Composite the map in ST-low planar and present with NO per-frame c2p. Two layers of primitive:
 *  - per-cell (Phase 1): chunky->planar+mask convert, the masked planar blit, a (tile,level) cache.
 *  - per-frame (Phase 2): an incrementally-maintained planar STATIC layer (g_pdimmed, the planar
 *    mirror of gamedemo's g_dimmed_map) + a per-frame planar composite (g_ptmp) presented as ONE
 *    shifted planar block-copy (the 8-px MAP_INSET). PREDIM-only.
 * The renderer owns the planar buffers + the 256->16 LUT; gamedemo drives WHICH cells change (the
 * shared compose logic stays chunky). Proven byte-identical to c2p by rdr_planar_selftest (Phase 1)
 * and rdr_pverify (Phase 2, BENCH: g_ptmp == c2p(the chunky composite)). */

#define PLANAR_TRANSPARENT 255          /* == gamedemo TILE_TRANSPARENT (not visible from here) */
typedef struct { uint16_t planar[64]; uint16_t mask[16]; uint16_t opaque; } planar_tile_t;
/* opaque = every mask word is 0xFFFF (no transparent pixel) -> the masked blit degenerates to a plain
 * copy. The Blitter path uses it to do a 1-pass copy for base terrain instead of the 2-pass cookie-cut. */

/* Convert a 256-byte chunky 16x16 tile (index; 255 = transparent) to planar (128 B) + 1bpp mask
 * (32 B), remapping opaque indices through the 256->16 lut EXACTLY as c2p4_lut (pixel 0 -> bit 15);
 * transparent pixels set mask bit 0. Feed it the already-dimmed chunky tile for a planar dim variant. */
static void rdr_chunky_tile_to_planar(const uint8_t *chunky, const uint8_t *lut, planar_tile_t *out)
{
    uint16_t allm = 0xFFFF;                                  /* AND of every mask word */
    for (int r = 0; r < 16; r++) {
        uint16_t p0=0,p1=0,p2=0,p3=0,m=0;
        const uint8_t *s = chunky + r*16;
        for (int c = 0; c < 16; c++) {
            uint8_t v = s[c]; int op = (v != PLANAR_TRANSPARENT);
            unsigned n = op ? (unsigned)(lut[v] & 0x0F) : 0u;
            p0=(uint16_t)((p0<<1)|(n&1));      p1=(uint16_t)((p1<<1)|((n>>1)&1));
            p2=(uint16_t)((p2<<1)|((n>>2)&1)); p3=(uint16_t)((p3<<1)|((n>>3)&1));
            m =(uint16_t)((m<<1)|(unsigned)op);
        }
        out->planar[r*4]=p0; out->planar[r*4+1]=p1; out->planar[r*4+2]=p2; out->planar[r*4+3]=p3;
        out->mask[r]=m; allm=(uint16_t)(allm & m);
    }
    out->opaque = (uint16_t)(allm == 0xFFFF);               /* fully opaque -> Blitter can 1-pass copy */
}

/* The one hot primitive: place a 16x16 planar tile into the 4-plane destination at pixel (x,y).
 * dstride_w = destination row stride in WORDS. Per row+plane d=(d&~m)|(s&m) so transparent pixels
 * leave dst untouched. sh==0 = fast group-aligned path (the compose case); (x&15)!=0 spans two
 * groups gx,gx+1 via a 32-bit shift window (the 8-px MAP_INSET). Byte-exact vs c2p over the same bg. */
static void rdr_blit_planar_masked(uint16_t *dst, int dstride_w, const planar_tile_t *pt, int x, int y)
{
    int gx = x >> 4, sh = x & 15;
    const uint16_t *tp = pt->planar, *tm = pt->mask;
    if (sh == 0) {
        for (int r = 0; r < 16; r++) {
            uint16_t m = tm[r];
            uint16_t *d = dst + (int32_t)(y+r)*dstride_w + gx*4;
            const uint16_t *s = tp + r*4;
            d[0]=(uint16_t)((d[0]&~m)|(s[0]&m)); d[1]=(uint16_t)((d[1]&~m)|(s[1]&m));
            d[2]=(uint16_t)((d[2]&~m)|(s[2]&m)); d[3]=(uint16_t)((d[3]&~m)|(s[3]&m));
        }
        return;
    }
    int rsh = 16 - sh;
    for (int r = 0; r < 16; r++) {
        uint32_t mm = (uint32_t)tm[r] << rsh;
        uint16_t mhi=(uint16_t)(mm>>16), mlo=(uint16_t)mm;
        uint16_t *d = dst + (int32_t)(y+r)*dstride_w + gx*4;
        const uint16_t *s = tp + r*4;
        for (int p = 0; p < 4; p++) {
            uint32_t ss = (uint32_t)s[p] << rsh;
            uint16_t shi=(uint16_t)(ss>>16), slo=(uint16_t)ss;
            d[p]  =(uint16_t)((d[p]  &~mhi)|(shi&mhi));
            d[p+4]=(uint16_t)((d[p+4]&~mlo)|(slo&mlo));
        }
    }
}

/* ================= BLiTTER-accelerated masked blit (Phase 3, docs/design/native-planar-st.md) =====
 * The STE / Mega ST(E) / TT carry the Atari BLiTTER — a DMA engine that computes dst = OP(src,dst) a
 * word at a time with per-column endmasks and independent src/dst X/Y increments. It runs the SAME
 * masked planar blit as rdr_blit_planar_masked, but off the CPU and at bus speed. On the plain 8 MHz
 * ST (no Blitter) the CPU path (2.38 fps) stands; where a Blitter exists this offloads the compose.
 *
 * The register file ($FFFF8A00..$8A3D) is in the supervisor-only I/O area, so the whole tile queue is
 * drained in ONE Supexec (XBIOS 38) per layer — amortising the trap over every tile, not per blit.
 *
 * Cookie-cut (transparent tile) = 2 passes, exactly d=(d&~m)|(s&m):
 *   Pass A  OP=4 ((NOT src) AND dst)  src=mask  -> dst &= ~mask   (punch the hole)
 *   Pass B  OP=7 (src OR dst)         src=data  -> dst |= data    (data is pre-masked: transparent=0)
 * The mask is a single plane; feeding it with src-X-increment 0 replays the same mask word across all
 * four dst planes of a group, so no "fat mask" is needed. An OPAQUE tile (pt->opaque, no transparent
 * pixel) collapses to a 1-pass copy (OP=3). Group-aligned only (the production compose case); the
 * shifted 8-px inset stays on the CPU (BENCH/verify only).
 *
 * HOP=2 selects "source" (halftone ignored). HOG mode (CTRL bit6) lets the Blitter own the bus until a
 * tile finishes — a <=128-word burst (<=1 scanline), between which the Timer-B band raster is serviced,
 * so palette splits keep landing at their (deliberately quiet) rows. Every Blitter-touched buffer is
 * static BSS = ST-RAM on any ST/STE (Blitter-reachable); this backend never runs on a TT/Falcon with
 * Alt-RAM. Correctness is gated byte-for-byte against the CPU blit by rdr_blitter_selftest (BENCH). */
int  rdr_blitter_ok(void) { return g_has_blitter; }
/* Enable/disable the Blitter in the LIVE compose (rdr_pdyn_cell/rdr_pstatic_blit). OFF by default —
 * measured a wash on the STE (the masked blit is a few ms of the ~400ms compose), so the shipping
 * game keeps the CPU compose. The bench flips it on to measure; BLITTER.REQ '0' flips it back for A/B.
 * The selftest/microbench call the Blitter primitive directly and are gated only by g_has_blitter. */
void rdr_force_blitter(int on) { g_blit_compose = on ? 1 : 0; }
void rdr_force_blit_present(int on) { g_blit_present = on ? 1 : 0; }   /* BENCH A/B: Blitter present on/off */

#define BLT_SRC_XINC    (*(volatile int16_t  *)0xFFFF8A20UL)
#define BLT_SRC_YINC    (*(volatile int16_t  *)0xFFFF8A22UL)
#define BLT_SRC_ADDR    (*(volatile uint32_t *)0xFFFF8A24UL)
#define BLT_ENDMASK1    (*(volatile uint16_t *)0xFFFF8A28UL)
#define BLT_ENDMASK2    (*(volatile uint16_t *)0xFFFF8A2AUL)
#define BLT_ENDMASK3    (*(volatile uint16_t *)0xFFFF8A2CUL)
#define BLT_DST_XINC    (*(volatile int16_t  *)0xFFFF8A2EUL)
#define BLT_DST_YINC    (*(volatile int16_t  *)0xFFFF8A30UL)
#define BLT_DST_ADDR    (*(volatile uint32_t *)0xFFFF8A32UL)
#define BLT_XCOUNT      (*(volatile uint16_t *)0xFFFF8A36UL)
#define BLT_YCOUNT      (*(volatile uint16_t *)0xFFFF8A38UL)
#define BLT_HOP         (*(volatile uint8_t  *)0xFFFF8A3AUL)
#define BLT_OP          (*(volatile uint8_t  *)0xFFFF8A3BUL)
#define BLT_CTRL        (*(volatile uint8_t  *)0xFFFF8A3CUL)
#define BLT_SKEW        (*(volatile uint8_t  *)0xFFFF8A3DUL)
#define BLT_START       0xC0            /* BUSY(bit7) | HOG(bit6); smudge=0, line#=0 */

/* one queued group-aligned masked tile blit. dst = the group-origin word (dst buffer + y*stride + gx*4);
 * dyinc_b = per-row dst byte advance = stride_w*2 - 6 (3 plane steps of +2 already made in-row); tile =
 * index into the per-flush arena. The tile PIXELS are NOT referenced from the planar cache (whose
 * direct-mapped slots can be evicted by a later rdr_planar_get before this job drains) — instead each
 * DISTINCT tile is interned once into g_bltq_tiles and jobs reference it by index, so many cells sharing
 * a tile cost one copy, not one per cell. */
typedef struct { uint16_t *dst; int16_t dyinc_b; uint16_t opaque; int16_t tile; } blt_job_t;
#define BLT_QUEUE_MAX 512                           /* max queued placements per flush */
#define BLT_TILES_MAX 256                           /* max DISTINCT tiles per flush (arena) */
static blt_job_t     g_bltq[BLT_QUEUE_MAX];
static planar_tile_t g_bltq_tiles[BLT_TILES_MAX];   /* per-flush interned tile copies (eviction-proof) */
static int32_t       g_bltq_tkey[BLT_TILES_MAX];    /* their keys (intern lookup) */
static int           g_bltn, g_bltq_ntiles;

/* run one Blitter pass to completion. HOG mode: the CTRL write halts the CPU on its next bus access
 * until the blit finishes, so the poll reads BUSY=0 on the first try (and covers non-HOG too). */
static inline void blt_run(void){ BLT_CTRL = BLT_START; while (BLT_CTRL & 0x80) { } }

/* SUPERVISOR (Supexec target): drain the queued tile blits. Common registers are set once; each tile is
 * a 1-pass copy (opaque) or a 2-pass cookie-cut. XCOUNT is the per-row reload (untouched by the Blitter);
 * YCOUNT counts down to 0, so it is reloaded before every pass. */
static long blitter_drain(void)
{
    BLT_HOP = 2;                                            /* HOP=source (halftone unused) */
    BLT_ENDMASK1 = BLT_ENDMASK2 = BLT_ENDMASK3 = 0xFFFF;    /* 4 full words wide, no column masking */
    BLT_SKEW = 0;                                           /* group-aligned: no skew / FXSR / NFSR */
    BLT_XCOUNT = 4;                                         /* 4 plane words / row */
    BLT_DST_XINC = 2;
    for (int i = 0; i < g_bltn; i++) {
        blt_job_t *j = &g_bltq[i];
        const planar_tile_t *pt = &g_bltq_tiles[j->tile];
        uint32_t dst = (uint32_t)(uintptr_t)j->dst;
        BLT_DST_YINC = j->dyinc_b;
        if (j->opaque) {                                    /* 1-pass straight copy: dst = src */
            BLT_OP = 3;
            BLT_SRC_XINC = 2; BLT_SRC_YINC = 2; BLT_SRC_ADDR = (uint32_t)(uintptr_t)pt->planar;
            BLT_DST_ADDR = dst; BLT_YCOUNT = 16; blt_run();
        } else {                                            /* 2-pass cookie-cut */
            BLT_OP = 4;                                      /* A: dst &= ~mask (src-X-inc 0 = mask spans 4 planes) */
            BLT_SRC_XINC = 0; BLT_SRC_YINC = 2; BLT_SRC_ADDR = (uint32_t)(uintptr_t)pt->mask;
            BLT_DST_ADDR = dst; BLT_YCOUNT = 16; blt_run();
            BLT_OP = 7;                                      /* B: dst |= data (pre-masked) */
            BLT_SRC_XINC = 2; BLT_SRC_YINC = 2; BLT_SRC_ADDR = (uint32_t)(uintptr_t)pt->planar;
            BLT_DST_ADDR = dst; BLT_YCOUNT = 16; blt_run();
        }
    }
    return 0;
}

static void blitter_flush(void);   /* fwd: enqueue auto-flushes when the queue/arena fills */

/* enqueue a group-aligned masked tile blit into `dst` (stride_w words/row) at pixel (x,y), x&15 == 0.
 * `key` identifies the tile; it is interned into the per-flush arena so N cells sharing a tile cost ONE
 * copy (not N). A full queue or arena auto-flushes first (order preserved -> still correct). */
static void blitter_enqueue(uint16_t *dst, int stride_w, int32_t key, const planar_tile_t *pt, int x, int y)
{
    if (g_bltn >= BLT_QUEUE_MAX || g_bltq_ntiles >= BLT_TILES_MAX) blitter_flush();
    int ti = -1;                                           /* intern: reuse this flush's copy if seen */
    for (int i = 0; i < g_bltq_ntiles; i++) if (g_bltq_tkey[i] == key) { ti = i; break; }
    if (ti < 0) { ti = g_bltq_ntiles++; g_bltq_tiles[ti] = *pt; g_bltq_tkey[ti] = key; }  /* new: copy once */
    blt_job_t *j = &g_bltq[g_bltn++];
    j->dst     = dst + (int32_t)y*stride_w + (x>>4)*4;
    j->dyinc_b = (int16_t)(stride_w*2 - 6);
    j->opaque  = pt->opaque;
    j->tile    = (int16_t)ti;
}

/* flush the queue: ONE Supexec (trap to supervisor) drains every queued blit through the Blitter, then
 * resets the queue AND the per-flush tile arena. */
static void blitter_flush(void){ if (g_bltn){ (void)Supexec(blitter_drain); g_bltn = 0; g_bltq_ntiles = 0; } }

/* Planar tile/mask cache — the planar analogue of gamedemo's g_dimcache: a (tile,level) key maps to
 * a cached planar_tile_t, built once from the (already-dimmed) chunky source, reused every frame.
 * Direct-mapped, same Knuth hash. 512x162 B = 81 KB. */
#define PLANAR_SLOTS 512
static planar_tile_t g_pcache[PLANAR_SLOTS];
static int32_t       g_pkey[PLANAR_SLOTS];
static int           g_pcache_ready;
#ifdef BENCH
uint32_t g_pcache_calls, g_pcache_miss;   /* planar-cache instrumentation */
#endif
static const planar_tile_t *rdr_planar_get(int32_t key, const uint8_t *chunky, const uint8_t *lut)
{
    if (!g_pcache_ready){ for (int i=0;i<PLANAR_SLOTS;i++) g_pkey[i]=-1; g_pcache_ready=1; }
    int slot = (int)(((uint32_t)key * 2654435761u) >> 23) & (PLANAR_SLOTS-1);
#ifdef BENCH
    g_pcache_calls++;
#endif
    if (g_pkey[slot] != key){
#ifdef BENCH
        g_pcache_miss++;
#endif
        rdr_chunky_tile_to_planar(chunky, lut, &g_pcache[slot]); g_pkey[slot]=key; }
    return &g_pcache[slot];
}

/* LUT change (new scene palette) invalidates the post-LUT planar caches. The dynamic (tile,level)
 * cache is cleared here; the STATIC layer's staleness is signalled to gamedemo via rdr_planar_take_dirty
 * (it drives the rebuild through g_pd_valid). Colour CYCLING (rdr_lowcolor_cycle) rotates only the
 * hardware registers, not g_lut, so the index-based planar bits stay valid — no invalidation there. */
static int g_planar_dirty = 0;
static void planar_mark_dirty(void){ g_pcache_ready = 0; g_planar_dirty = 1; }
int  rdr_planar_take_dirty(void){ int d = g_planar_dirty; g_planar_dirty = 0; return d; }

/* The map content is presented at screen rows PMAP_TOP..PMAP_TOP+159 (== gamedemo MAP_INSET). Native
 * planar builds tiles with THAT region's 256->16 LUT (g_row_lut[PMAP_TOP]) so it matches the chunky
 * present exactly — even under a band raster, where the map sits in band 0 (a single LUT) and the OTHER
 * bands only swap hardware palette REGISTERS per row (Timer-B), which is orthogonal to the framebuffer
 * indices the planar path writes. Native planar is OK iff the whole map region uses one LUT. */
#define PMAP_TOP 8
#define PLANAR_LUT (g_row_lut[PMAP_TOP])
int  rdr_native_planar_ok(void){
    const uint8_t *l = g_row_lut[PMAP_TOP];
    for (int y = PMAP_TOP+1; y < PMAP_TOP+160; y++) if (g_row_lut[y] != l) return 0;  /* map must be one band */
    return 1;
}

/* --- Phase 2: planar map buffers + incremental static maintenance + present --- */
#define PMAP_GROUPS   11                       /* 176 px = 11 groups (== gamedemo TMP_T) */
#define PMAP_ROWS     176
#define PMAP_STRIDE_W (PMAP_GROUPS*4)          /* 44 words / row */
static uint16_t g_pdimmed[PMAP_ROWS*PMAP_STRIDE_W];  /* incremental planar dimmed STATIC layer */
static uint16_t g_ptmp   [PMAP_ROWS*PMAP_STRIDE_W];  /* per-frame planar composite (mirrors chunky tmp) */

/* full rebuild: c2p all 176 rows of the (opaque) dimmed chunky map into g_pdimmed (jump/first frame). */
void rdr_pstatic_rebuild(const uint8_t *dimmed, int cstride)
{
    blitter_flush();                            /* drain any queued static blits before overwriting g_pdimmed */
    const uint8_t *lut = PLANAR_LUT;
    for (int r = 0; r < PMAP_ROWS; r++)
        c2p4_lut_asm(dimmed + (int32_t)r*cstride, lut, g_pdimmed + (int32_t)r*PMAP_STRIDE_W, PMAP_GROUPS);
}
/* c2p one 16x16 cell (vx,vy) of the dimmed chunky map into g_pdimmed (group-aligned) — the only
 * per-frame convert on a walk (dirty cells: revealed edge + light-changed), mirroring dim_cell_into. */
void rdr_pstatic_cell(const uint8_t *dimmed, int cstride, int vx, int vy)
{
    blitter_flush();                            /* drain queued static blits before touching g_pdimmed */
    const uint8_t *lut = PLANAR_LUT;
    for (int r = 0; r < 16; r++)
        c2p4_lut_asm(dimmed + (int32_t)(vy*16+r)*cstride + vx*16, lut,
                     g_pdimmed + (int32_t)(vy*16+r)*PMAP_STRIDE_W + vx*4, 1);
}
/* planar shift g_pdimmed by (dx,dy) CELLS so new[v]=old[v+(dx,dy)] (mirrors shift_static; the lead
 * band is left stale -> refreshed by the dirty-cell rdr_pstatic_cell). Whole-group units = bit-exact. */
void rdr_pstatic_shift(int dx, int dy)
{
    blitter_flush();                            /* drain queued static blits before shifting g_pdimmed */
    int sx = dx*4, sy = dy*16;                                     /* words, rows */
    int x0 = sx>0?0:-sx, ww = PMAP_STRIDE_W-(sx<0?-sx:sx); if (ww<=0) return;
    int hh = PMAP_ROWS-(sy<0?-sy:sy); if (hh<=0) return;
    if (sy>0){ for (int ny=0; ny<hh; ny++)
                 memmove(&g_pdimmed[ny*PMAP_STRIDE_W+x0], &g_pdimmed[(ny+sy)*PMAP_STRIDE_W+x0+sx], (size_t)ww*2); }
    else     { for (int ny=PMAP_ROWS-1; ny>=-sy; ny--)
                 memmove(&g_pdimmed[ny*PMAP_STRIDE_W+x0], &g_pdimmed[(ny+sy)*PMAP_STRIDE_W+x0+sx], (size_t)ww*2); }
}
/* begin the frame's planar composite: g_ptmp = the static layer (mirrors memcpy(tmp,g_dimmed_map)). Any
 * queued STATIC blits (rdr_pstatic_blit -> g_pdimmed) are drained first so the copy sees the final layer. */
void rdr_pframe_begin(void) { blitter_flush(); memcpy(g_ptmp, g_pdimmed, sizeof g_ptmp); }
/* end the frame's planar composite: drain the queued DYNAMIC blits (rdr_pdyn_cell -> g_ptmp) so g_ptmp is
 * complete before the caller reads it (present / INV2 verify). No-op on the CPU (non-Blitter) path. */
void rdr_pframe_end(void) { blitter_flush(); }
/* blit a dynamic cell (dimmed chunky tile, 255=transparent) into g_ptmp at view cell (vx,vy),
 * group-aligned, via the (tile,level) planar cache — mirrors draw_cell's masked chunky copy. With a
 * Blitter it enqueues the tile (drained by rdr_pframe_end via one Supexec); else it blits on the CPU. */
void rdr_pdyn_cell(int32_t key, const uint8_t *dimmed_tile, int vx, int vy)
{
    const planar_tile_t *pt = rdr_planar_get(key, dimmed_tile, PLANAR_LUT);   /* cache build: user mode */
    if (g_has_blitter && g_blit_compose) blitter_enqueue(g_ptmp, PMAP_STRIDE_W, key, pt, vx*16, vy*16);
    else                                 rdr_blit_planar_masked(g_ptmp, PMAP_STRIDE_W, pt, vx*16, vy*16);
}
/* Phase 4c: blit a pre-dimmed planar tile into the STATIC layer g_pdimmed at cell (vx,vy) — composes the
 * static layer directly from planar dimmed tiles (opaque base + masked objects), replacing the chunky
 * dim + per-cell c2p. Same masked blit; opaque base tiles (no 255) get mask all-1 -> full overwrite.
 * Enqueues to the Blitter (drained by rdr_pframe_begin) when present; else CPU. */
void rdr_pstatic_blit(int32_t key, const uint8_t *dimmed_tile, int vx, int vy)
{
    const planar_tile_t *pt = rdr_planar_get(key, dimmed_tile, PLANAR_LUT);
    if (g_has_blitter && g_blit_compose) blitter_enqueue(g_pdimmed, PMAP_STRIDE_W, key, pt, vx*16, vy*16);
    else                                 rdr_blit_planar_masked(g_pdimmed, PMAP_STRIDE_W, pt, vx*16, vy*16);
}
/* present CORE (CPU): shifted planar block-copy of g_ptmp's visible 160x160 span into `scr`'s map region
 * at the +8-px MAP_INSET — writes screen px 8..167 (border cols 0..7 / 168.. preserved). Per screen group
 * g the +8 shift splits each src group across two:
 *   screen[g] = (src[g-1]_lo8 << 8) | (src[g]_hi8);  grp 0 low-half only, grp 10 high-half only. */
#ifdef BENCH
static void ppresent_cpu(uint8_t *scr)      /* C reference for rdr_ppresent_selftest (asm ships) */
{
    /* Process a whole 4-plane GROUP per step (4 contiguous src words in, 4 contiguous dst words out) instead
     * of the 4x per-plane array-indexed passes — the same byte-merge (dst = prev_lo<<8 | cur_hi) but with
     * contiguous movem-friendly access and the previous group's planes carried in registers. Byte-identical
     * to the old per-plane form (rdr_present_selftest CPU-vs-Blitter stays 0). */
    for (int y = 0; y < 160; y++) {
        const uint16_t *s = g_ptmp + (int32_t)y*PMAP_STRIDE_W;             /* src groups 0..9 (4 words each) */
        uint16_t *d = (uint16_t *)(scr + (int32_t)(8+y)*ST_ROWBYTES);      /* screen row (group 0) */
        uint16_t p0=s[0], p1=s[1], p2=s[2], p3=s[3];                       /* group 0 = "prev" */
        d[0]=(uint16_t)((d[0]&0xFF00)|(p0>>8)); d[1]=(uint16_t)((d[1]&0xFF00)|(p1>>8));
        d[2]=(uint16_t)((d[2]&0xFF00)|(p2>>8)); d[3]=(uint16_t)((d[3]&0xFF00)|(p3>>8)); /* grp 0: low half */
        const uint16_t *sg=s+4; uint16_t *dg=d+4;
        for (int g = 1; g <= 9; g++) {
            uint16_t c0=sg[0], c1=sg[1], c2=sg[2], c3=sg[3];
            dg[0]=(uint16_t)((p0<<8)|(c0>>8)); dg[1]=(uint16_t)((p1<<8)|(c1>>8));
            dg[2]=(uint16_t)((p2<<8)|(c2>>8)); dg[3]=(uint16_t)((p3<<8)|(c3>>8));
            p0=c0; p1=c1; p2=c2; p3=c3; sg+=4; dg+=4;
        }
        dg[0]=(uint16_t)((dg[0]&0x00FF)|(p0<<8)); dg[1]=(uint16_t)((dg[1]&0x00FF)|(p1<<8));
        dg[2]=(uint16_t)((dg[2]&0x00FF)|(p2<<8)); dg[3]=(uint16_t)((dg[3]&0x00FF)|(p3<<8)); /* grp 10: high half */
    }
}
#endif /* BENCH (ppresent_cpu reference) */

/* present CORE (BLiTTER): the same +8-px shifted planar copy, but off the CPU — the single biggest fixed
 * cost per frame (the CPU version is ~81 ms). The +8 shift == a RIGHT shift of the source by one byte
 * (skew 8). Interleaved planes force ONE blit per plane (source stride skips the other 3 planes): each
 * plane's 10 source words (groups 0..9) spread across 11 dest words (screen groups 0..10). NFSR makes the
 * 11th dest word consume no fresh source (only 10 exist); the shifter's garbage top/bottom byte at the
 * map edges is masked out by endmask1=$00FF (grp 0 keeps border px 0..7) / endmask3=$FF00 (grp 10 keeps
 * border px 168..). HOP=source, OP=replace. Group-aligned dest, so no dest skew. Supervisor (Supexec). */
static uint8_t *g_ppresent_dst;                 /* screen base for the Blitter present (Supexec reads it) */
/* Row-band the present so each HOG blit holds the bus < 1 scanline: a full 160-row plane blit would HOG
 * ~2 ms (~30 lines), starving the Timer-B band raster (delayed palette split = visible panel glitch). At
 * PBAND=4 rows (44 words ~ <1 line) the CPU regains the bus between bands and services the raster on time.
 * Skew is per-row, so banding rows is bit-exact vs the full blit. 160 = 40 bands x 4 rows exactly. */
#define PBAND 4
static long ppresent_blitter_super(void)
{
    BLT_HOP = 2; BLT_OP = 3;                                   /* HOP=source, OP=src (replace) */
    BLT_ENDMASK1 = 0x00FF; BLT_ENDMASK2 = 0xFFFF; BLT_ENDMASK3 = 0xFF00;  /* keep border bytes at grp 0/10 */
    BLT_SKEW = (uint8_t)(0x40 | 8);                            /* NFSR (bit6) + skew 8 (right shift 1 byte) */
    BLT_SRC_XINC = 8; BLT_SRC_YINC = 16;                       /* src: 10 words/row, stride 88 B => 9*8+16 */
    BLT_DST_XINC = 8; BLT_DST_YINC = 80;                       /* dst: 11 words/row, stride 160 B => 10*8+80 */
    BLT_XCOUNT = 11;
    for (int y = 0; y < 160; y += PBAND) {
        for (int p = 0; p < 4; p++) {                          /* one blit per bitplane per band */
            BLT_SRC_ADDR = (uint32_t)(uintptr_t)((uint8_t *)g_ptmp + (int32_t)y*(PMAP_STRIDE_W*2) + p*2);
            BLT_DST_ADDR = (uint32_t)(uintptr_t)(g_ppresent_dst + (int32_t)(8+y)*ST_ROWBYTES + p*2);
            BLT_YCOUNT = PBAND;
            blt_run();                                         /* short HOG: raster serviced between bands */
        }
    }
    return 0;
}

/* hand-asm +8px present (render/ppresent_st.s) — byte-identical to ppresent_cpu, ~2.4x faster on the
 * plain ST (no Blitter). ppresent_cpu is kept as the C reference for rdr_ppresent_selftest. */
extern void ppresent_st_asm(const uint16_t *src, uint8_t *dst, long src_stride_b, long dst_stride_b, long nrows);

/* present (render.h): NO c2p, NO Vsync (caller batches). Uses the Blitter when present + enabled, else the
 * hand-asm CPU present. */
void rdr_ppresent(void)
{
    blitter_flush();                            /* defensive: drain any pending dynamic blits into g_ptmp */
    if (g_has_blitter && g_blit_present) { g_ppresent_dst = g_screen; (void)Supexec(ppresent_blitter_super); }
    else ppresent_st_asm(g_ptmp, g_screen + 8*ST_ROWBYTES, PMAP_STRIDE_W*2, ST_ROWBYTES, 160);
}

/* --- Planar mouse cursor (native-planar path) --- The chunky path draws the cursor into g_back then
 * c2p's it; the native-planar present reads g_ptmp, not g_back, so the cursor must be drawn DIRECTLY
 * into the planar screen with save-under (the map/panel under it is restored on the next move). The
 * cursor sprite is chunky (w x h, 255 = transparent) and is coloured through its screen row's LUT.
 * The cursor is small so per-pixel plane read/write is cheap. */
#define PCUR_MAX 24
static uint8_t g_pcur_save[PCUR_MAX*PCUR_MAX];   /* 4-bit colours captured under the cursor */
static int     g_pcur_x = -1, g_pcur_y, g_pcur_w, g_pcur_h;
static inline int pget_px(int x, int y){
    const uint16_t *w = (const uint16_t *)(g_screen + (int32_t)y*ST_ROWBYTES) + (x>>4)*4;
    int b = 15-(x&15);
    return ((w[0]>>b)&1) | (((w[1]>>b)&1)<<1) | (((w[2]>>b)&1)<<2) | (((w[3]>>b)&1)<<3);
}
static inline void pset_px(int x, int y, int c){
    uint16_t *w = (uint16_t *)(g_screen + (int32_t)y*ST_ROWBYTES) + (x>>4)*4;
    uint16_t m = (uint16_t)(1u << (15-(x&15)));
    w[0]=(c&1)?(uint16_t)(w[0]|m):(uint16_t)(w[0]&~m); w[1]=(c&2)?(uint16_t)(w[1]|m):(uint16_t)(w[1]&~m);
    w[2]=(c&4)?(uint16_t)(w[2]|m):(uint16_t)(w[2]&~m); w[3]=(c&8)?(uint16_t)(w[3]|m):(uint16_t)(w[3]&~m);
}
void rdr_pcursor_hide(void){                      /* restore the screen pixels the cursor overwrote */
    if (g_pcur_x < 0) return;
    for (int y = 0; y < g_pcur_h; y++) for (int x = 0; x < g_pcur_w; x++)
        pset_px(g_pcur_x+x, g_pcur_y+y, g_pcur_save[y*g_pcur_w+x]);
    g_pcur_x = -1;
}
void rdr_pcursor_invalidate(void){ g_pcur_x = -1; }   /* forget the save-under (a full present erased it) */
void rdr_pcursor_show(const uint8_t *curpix, int w, int h, int ox, int oy){
    if (w > PCUR_MAX) w = PCUR_MAX;  if (h > PCUR_MAX) h = PCUR_MAX;
    const uint8_t *lut = g_row_lut[(oy>=0 && oy<STH) ? oy : 0];
    g_pcur_x = ox; g_pcur_y = oy; g_pcur_w = w; g_pcur_h = h;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++){
        int px = ox+x, py = oy+y;
        g_pcur_save[y*w+x] = (uint8_t)pget_px(px, py);          /* save-under */
        uint8_t c = curpix[y*w+x];
        if (c != PLANAR_TRANSPARENT) pset_px(px, py, lut[c] & 0x0F);
    }
}

#ifdef BENCH
/* BENCH-only: the intro-bench harness in gamedemo.c splits present into c2p vs output on the TT.
 * The ST present is 1:1 (no letterbox/doubling/flip) — effectively all c2p — so report zeros; the
 * game-frame bench (`make bench-st`) times the whole rect present directly and doesn't use this. */
void rdr_bench_present(uint32_t *c2p, uint32_t *out){ *c2p = 0; *out = 0; }
/* c2p selftest: run the C reference and the asm on a 256-pixel pattern (every index value, all 16
 * bit positions) through the live LUT and return the mismatched plane-word count (0 = byte-identical
 * -> the asm is a safe drop-in for c2p4_lut). Called from the bench harness (make bench-st). */
int rdr_c2p_selftest(void)
{
    uint8_t  src[256]; for (int i = 0; i < 256; i++) src[i] = (uint8_t)i;   /* 16 groups */
    uint16_t a[64], b[64];                                                  /* 16 x 4 planes */
    c2p4_lut    (src, g_lut, a, 16);
    c2p4_lut_asm(src, g_lut, b, 16);
    int diff = 0; for (int i = 0; i < 64; i++) if (a[i] != b[i]) diff++;
    return diff;
}

/* Phase-0 native-planar microbench (docs/design/native-planar-st.md): on the plain ST (no Blitter),
 * is a masked planar tile blit cheaper than the c2p? Builds one planar (4x16 plane words) + mask tile,
 * then times N iters of (a) c2p the 176x176 map area (today's present), (b) masked-planar-blit an
 * 11x11 tile grid GROUP-ALIGNED, (c) the same at the 8-px map inset (x=8, each plane spans 2 groups).
 * Returns 200Hz ticks for N iters in out[0..2]. Writes into g_screen (bench: contents don't matter). */
void rdr_planar_microbench(uint32_t out[3], int N)
{
    static uint16_t ptile[64], pmask[16];
    uint8_t ctile[256];
    for (int i = 0; i < 256; i++) ctile[i] = (uint8_t)((i * 7) & 0x0F);   /* opaque 4bpp pattern */
    ctile[3] = 255; ctile[200] = 255;                                    /* 255 = transparent px  */
    for (int r = 0; r < 16; r++) {
        uint16_t p0=0,p1=0,p2=0,p3=0,m=0;
        for (int c = 0; c < 16; c++) {
            uint8_t v = ctile[r*16+c]; int op = (v != 255); unsigned n = op ? (v & 0x0F) : 0;
            p0=(uint16_t)((p0<<1)|(n&1));      p1=(uint16_t)((p1<<1)|((n>>1)&1));
            p2=(uint16_t)((p2<<1)|((n>>2)&1)); p3=(uint16_t)((p3<<1)|((n>>3)&1));
            m=(uint16_t)((m<<1)|op);
        }
        ptile[r*4]=p0; ptile[r*4+1]=p1; ptile[r*4+2]=p2; ptile[r*4+3]=p3; pmask[r]=m;
    }
    const int DW = ST_ROWBYTES/2;                 /* 80 words/row */
    uint16_t *scr = (uint16_t *)g_screen;

    uint32_t t0 = plat_ticks_200hz();             /* (a) c2p 176x176 (11 groups x 176 rows) */
    for (int it = 0; it < N; it++)
        for (int y = 0; y < 176; y++)
            c2p4_lut_asm(g_back + (int32_t)y*STW, g_lut, scr + (int32_t)y*DW, 11);
    out[0] = plat_ticks_200hz() - t0;

    uint32_t t1 = plat_ticks_200hz();             /* (b) aligned masked planar blit, 11x11 grid */
    for (int it = 0; it < N; it++)
        for (int cy = 0; cy < 11; cy++) for (int cx = 0; cx < 11; cx++)
            for (int r = 0; r < 16; r++) {
                uint16_t m = pmask[r]; uint16_t *d = scr + (cy*16+r)*DW + cx*4; const uint16_t *s = ptile + r*4;
                d[0]=(uint16_t)((d[0]&~m)|(s[0]&m)); d[1]=(uint16_t)((d[1]&~m)|(s[1]&m));
                d[2]=(uint16_t)((d[2]&~m)|(s[2]&m)); d[3]=(uint16_t)((d[3]&~m)|(s[3]&m));
            }
    out[1] = plat_ticks_200hz() - t1;

    uint32_t t2 = plat_ticks_200hz();             /* (c) offset (x=8) masked planar blit, 10x11 grid */
    for (int it = 0; it < N; it++)
        for (int cy = 0; cy < 11; cy++) for (int cx = 0; cx < 10; cx++) {
            int x = 8 + cx*16, gx = x>>4, sh = x&15;   /* sh = 8; each plane spans groups gx, gx+1 */
            for (int r = 0; r < 16; r++) {
                uint16_t m = pmask[r]; uint32_t mm = (uint32_t)m << (16-sh);
                uint16_t mhi=(uint16_t)(mm>>16), mlo=(uint16_t)mm;
                uint16_t *d0 = scr + (cy*16+r)*DW + gx*4; const uint16_t *s = ptile + r*4;
                for (int p=0;p<4;p++){ uint32_t ss=(uint32_t)s[p]<<(16-sh); uint16_t shi=(uint16_t)(ss>>16), slo=(uint16_t)ss;
                    d0[p]=(uint16_t)((d0[p]&~mhi)|(shi&mhi)); d0[p+4]=(uint16_t)((d0[p+4]&~mlo)|(slo&mlo)); }
            }
        }
    out[2] = plat_ticks_200hz() - t2;
}

/* Phase-1 GATE: prove the planar path is byte-identical to c2p. Invariant, for any chunky tile ct over
 * any opaque background bg at any x-offset:
 *     blit_planar_masked( planar(ct), x )  onto  c2p(bg)   ==   c2p( bg with ct overlaid at x, masked )
 * i.e. a masked planar blit produces the SAME plane words as c2p-ing the composited chunky. Exercised
 * across 4 tile patterns (opaque / sparse-transparent / left+top-edge-transparent = the multi-tile
 * bleed / a synthetic-dimmed variant = the planar dim_tile path) x 2 offsets (aligned x=0, inset x=8),
 * building the planar tile THROUGH the cache. Returns the mismatched plane-word count (0 = identical). */
int rdr_planar_selftest(void)
{
    int diff = 0;
    for (int pat = 0; pat < 4; pat++) {
        uint8_t ct[256], bg[32*16];
        for (int i = 0; i < 256; i++) {
            uint8_t v = (uint8_t)((i*11 + pat*3) & 0x0F);               /* opaque base index 0..15 */
            if (pat==1 && (i%5)==0)                        v = 255;     /* sparse transparency      */
            if (pat==2 && (((i&15)<2) || ((i>>4)<2)))      v = 255;     /* left/top 2px transparent */
            if (pat==3)                                    v = (uint8_t)((v*5+1) & 0x0F); /* stand-in for a dim remap */
            ct[i] = v;
        }
        for (int i = 0; i < 32*16; i++) bg[i] = (uint8_t)((i*7) & 0x0F);    /* opaque background */
        const planar_tile_t *pt = rdr_planar_get((int32_t)(0x1000 | pat), ct, g_lut);
        for (int xi = 0; xi < 2; xi++) {
            int x = xi ? 8 : 0;
            uint16_t dst[16*8], ref[16*8];
            for (int r = 0; r < 16; r++) c2p4_lut(bg + r*32, g_lut, dst + r*8, 2);   /* dst = c2p(bg)  */
            rdr_blit_planar_masked(dst, 8, pt, x, 0);                                /* + planar tile  */
            for (int r = 0; r < 16; r++) {                                           /* ref = c2p(bg+ct) */
                uint8_t line[32];
                for (int c = 0; c < 32; c++) line[c] = bg[r*32 + c];
                for (int c = 0; c < 16; c++){ uint8_t v = ct[r*16 + c]; if (v != 255) line[x+c] = v; }
                c2p4_lut(line, g_lut, ref + r*8, 2);
            }
            for (int i = 0; i < 16*8; i++) if (dst[i] != ref[i]) diff++;
        }
    }
    return diff;
}

/* Phase-2 GATE (INV2): after a compose, the planar composite g_ptmp must equal c2p of the chunky
 * composite `tmp` over the visible 160x160 span. If this is 0 across the walk, the whole native-planar
 * path (incremental static + dynamic blits + present) is byte-identical to today's chunky+c2p. */
int rdr_pverify(const uint8_t *tmp, int cstride)
{
    blitter_flush();                            /* ensure g_ptmp is complete before comparing (Blitter path) */
    int diff = 0; uint16_t ref[40]; const uint8_t *lut = PLANAR_LUT;   /* 10 groups; map's row-LUT */
    for (int y = 0; y < 160; y++) {
        c2p4_lut_asm(tmp + (int32_t)y*cstride, lut, ref, 10);
        const uint16_t *g = g_ptmp + (int32_t)y*PMAP_STRIDE_W;
        for (int i = 0; i < 40; i++) if (g[i] != ref[i]) diff++;
    }
    return diff;
}

/* Phase-4c INV (static): save=1 snapshots g_pdimmed; save=0 returns the plane-word count where g_pdimmed
 * now differs from the snapshot. Used to prove the planar-DIRECT static build == the chunky-dim + c2p one. */
static uint16_t g_psnap[PMAP_ROWS*PMAP_STRIDE_W];
int rdr_pstatic_snapshot_and_diff(int save)
{
    blitter_flush();                            /* ensure the planar-direct static build is applied first */
    if (save){ memcpy(g_psnap, g_pdimmed, sizeof g_psnap); return 0; }
    int diff = 0;
    for (int i = 0; i < PMAP_ROWS*PMAP_STRIDE_W; i++) if (g_pdimmed[i] != g_psnap[i]) diff++;
    return diff;
}

/* Phase-4c GATE (planar dimming): the whole point of composing the static layer from PRE-DIMMED planar
 * tiles (dropping the per-cell chunky-dim + c2p) rests on one identity — a per-pixel darklut COMMUTES
 * with tile overlay. This proves it byte-for-byte for the base/object transparency asymmetry:
 *     blit( planar(dim_base(base)) )  then  blit( planar(dim_obj(obj)) )   [planar-dimmed-direct]
 *   ==  c2p( darklut[ fill(base, 255->0)  overlaid  obj(255 transparent) ] )   [today: chunky dim then c2p]
 * where dim_base maps 255->0 THEN darklut (opaque base, like fill_tiles), dim_obj preserves 255 THEN
 * darklut (masked). `dark` is a synthetic 256->256 darkening — commutativity holds for ANY LUT, so this
 * validates the mechanism the integration will use. 3 tile patterns; returns the mismatched-word count. */
int rdr_planar_dim_selftest(void)
{
    uint8_t dark[256]; for (int i=0;i<256;i++) dark[i] = (uint8_t)((i*3)>>2);   /* darken (any LUT works) */
    int diff = 0;
    for (int pat = 0; pat < 3; pat++) {
        uint8_t base[256], obj[256], comp[256], db[256], dob[256];
        for (int i = 0; i < 256; i++) {
            base[i] = (uint8_t)((i*7) & 0x7F);
            if (pat>=1 && (i%9)==0) base[i] = 255;                  /* holes in the base terrain     */
            obj[i]  = (uint8_t)((i*13+3) & 0x7F);
            if ((i%3)!=0) obj[i] = 255;                            /* object is a sparse overlay    */
            if (pat==2 && i<32) obj[i] = 255;                      /* object top 2 rows transparent */
        }
        for (int i = 0; i < 256; i++) {
            uint8_t b = (base[i]==255)?0:base[i];                  /* fill_tiles: transparent -> 0  */
            uint8_t v = (obj[i]!=255)?obj[i]:b;                    /* object over base              */
            comp[i] = dark[v];                                     /* today: dim the composite      */
            db[i]   = dark[b];                                     /* dim_base: 255->0 then dim (opaque) */
            dob[i]  = (obj[i]==255)?255:dark[obj[i]];              /* dim_obj: preserve 255 then dim */
        }
        uint16_t ref[64], dst[64];
        for (int r = 0; r < 16; r++) c2p4_lut(comp + r*16, g_lut, ref + r*4, 1);   /* c2p(dim composite) */
        for (int i = 0; i < 64; i++) dst[i] = 0;
        planar_tile_t pb, po;
        rdr_chunky_tile_to_planar(db,  g_lut, &pb);                /* db has no 255 -> opaque (mask all 1) */
        rdr_chunky_tile_to_planar(dob, g_lut, &po);               /* dob has 255 -> masked                */
        rdr_blit_planar_masked(dst, 4, &pb, 0, 0);                /* base first (overwrite) */
        rdr_blit_planar_masked(dst, 4, &po, 0, 0);                /* object over (masked)   */
        for (int i = 0; i < 64; i++) if (dst[i] != ref[i]) diff++;
    }
    return diff;
}

/* Phase-3 GATE: the BLiTTER masked blit must be byte-identical to the CPU rdr_blit_planar_masked. For
 * each tile pattern (opaque / sparse-transparent / edge-transparent / dim-remap) over an opaque
 * background, blit group-aligned into two 2-group-wide buffers — one via the CPU, one via the Blitter
 * queue — and diff. Group 1 (never touched) also proves the Blitter honours the stride and does not
 * scribble the neighbour. Buffers are static (ST-RAM, Blitter-reachable). Returns the mismatched
 * plane-word count (0 = identical); guard the call with rdr_blitter_ok(). */
int rdr_blitter_selftest(void)
{
    static uint16_t cpu[16*8], blt[16*8];
    int diff = 0;
    for (int pat = 0; pat < 4; pat++) {
        uint8_t ct[256], bg[32*16];
        for (int i = 0; i < 256; i++) {
            uint8_t v = (uint8_t)((i*11 + pat*3) & 0x0F);              /* opaque base index 0..15 */
            if (pat==1 && (i%5)==0)                    v = 255;        /* sparse transparency      */
            if (pat==2 && (((i&15)<2) || ((i>>4)<2)))  v = 255;        /* left/top 2px transparent */
            if (pat==3)                                v = (uint8_t)((v*5+1) & 0x0F); /* dim-remap stand-in */
            ct[i] = v;
        }
        for (int i = 0; i < 32*16; i++) bg[i] = (uint8_t)((i*7) & 0x0F);   /* opaque background */
        const planar_tile_t *pt = rdr_planar_get((int32_t)(0x3000 | pat), ct, g_lut);
        for (int r = 0; r < 16; r++){ c2p4_lut(bg + r*32, g_lut, cpu + r*8, 2);   /* both start = c2p(bg) */
                                      c2p4_lut(bg + r*32, g_lut, blt + r*8, 2); }
        rdr_blit_planar_masked(cpu, 8, pt, 0, 0);                     /* CPU reference */
        g_bltn = g_bltq_ntiles = 0;
        blitter_enqueue(blt, 8, (int32_t)(0x3000|pat), pt, 0, 0); blitter_flush();  /* Blitter path */
        for (int i = 0; i < 16*8; i++) if (cpu[i] != blt[i]) diff++;
    }
    return diff;
}

/* Phase-3 microbench: time the CPU vs the Blitter over the same 11x11 grid of group-aligned masked tile
 * blits (a full map-window compose), N iters. out[0]=CPU 200Hz ticks, out[1]=Blitter (INCLUDING the
 * per-iter queue build + one Supexec). Uses one masked tile (the 2-pass cookie-cut = the realistic
 * dynamic-cell cost). Writes into g_screen (bench: contents don't matter). Requires a Blitter. */
void rdr_blitter_microbench(uint32_t out[2], int N)
{
    static uint8_t ctile[256];
    for (int i = 0; i < 256; i++) ctile[i] = (uint8_t)((i * 7) & 0x0F);
    ctile[3] = 255; ctile[200] = 255;                                 /* transparent -> 2-pass path */
    static planar_tile_t bt; rdr_chunky_tile_to_planar(ctile, g_lut, &bt);
    const int DW = ST_ROWBYTES/2;                                     /* 80 words/row */
    uint16_t *scr = (uint16_t *)g_screen;

    uint32_t t0 = plat_ticks_200hz();                                 /* (a) CPU masked blit, 11x11 grid */
    for (int it = 0; it < N; it++)
        for (int cy = 0; cy < 11; cy++) for (int cx = 0; cx < 11; cx++)
            rdr_blit_planar_masked(scr, DW, &bt, cx*16, cy*16);
    out[0] = plat_ticks_200hz() - t0;

    uint32_t t1 = plat_ticks_200hz();                                 /* (b) Blitter masked blit, same grid */
    for (int it = 0; it < N; it++){
        g_bltn = g_bltq_ntiles = 0;
        for (int cy = 0; cy < 11; cy++) for (int cx = 0; cx < 11; cx++)
            blitter_enqueue(scr, DW, (int32_t)cx, &bt, cx*16, cy*16);  /* 11 distinct keys -> ~11 tile copies */
        blitter_flush();
    }
    out[1] = plat_ticks_200hz() - t1;
}

/* Phase-3 present GATE: the BLiTTER present must be byte-identical to the CPU present (ppresent_cpu),
 * INCLUDING the preserved border bytes. Fill g_ptmp with a pseudo-random pattern + two scratch screens
 * with a border sentinel, present each way, diff all 32000 bytes (0 = identical). Requires a Blitter. */
int rdr_present_selftest(void)
{
    static uint8_t sa[ST_SCREEN_BYTES], sb[ST_SCREEN_BYTES];
    for (int i = 0; i < PMAP_ROWS*PMAP_STRIDE_W; i++)
        g_ptmp[i] = (uint16_t)(((uint32_t)i * 2654435761u) >> 15);          /* arbitrary planar content */
    for (int i = 0; i < ST_SCREEN_BYTES; i++) { uint8_t v=(uint8_t)(i*7+1); sa[i]=v; sb[i]=v; }  /* border sentinel */
    ppresent_cpu(sa);                                                       /* CPU reference */
    g_ppresent_dst = sb; (void)Supexec(ppresent_blitter_super);            /* Blitter */
    int diff = 0; for (int i = 0; i < ST_SCREEN_BYTES; i++) if (sa[i] != sb[i]) diff++;
    return diff;
}

/* asm present GATE (no Blitter needed — runs on the plain ST too): ppresent_st_asm must be byte-identical to
 * the C reference ppresent_cpu, INCLUDING the preserved border bytes. 0 = identical. */
int rdr_ppresent_asm_selftest(void)
{
    static uint8_t sa[ST_SCREEN_BYTES], sb[ST_SCREEN_BYTES];
    for (int i = 0; i < PMAP_ROWS*PMAP_STRIDE_W; i++)
        g_ptmp[i] = (uint16_t)(((uint32_t)i * 2654435761u) >> 15);
    for (int i = 0; i < ST_SCREEN_BYTES; i++) { uint8_t v=(uint8_t)(i*7+1); sa[i]=v; sb[i]=v; }  /* border sentinel */
    ppresent_cpu(sa);                                                       /* C reference */
    ppresent_st_asm(g_ptmp, sb + 8*ST_ROWBYTES, PMAP_STRIDE_W*2, ST_ROWBYTES, 160);   /* asm */
    int diff = 0; for (int i = 0; i < ST_SCREEN_BYTES; i++) if (sa[i] != sb[i]) diff++;
    return diff;
}

/* asm-vs-C present microbench: N iters each into g_screen. out[0]=C ticks, out[1]=asm. */
void rdr_ppresent_asm_microbench(uint32_t out[2], int N)
{
    for (int i = 0; i < PMAP_ROWS*PMAP_STRIDE_W; i++) g_ptmp[i] = (uint16_t)(((uint32_t)i*40503u)>>3);
    uint32_t t0 = plat_ticks_200hz();
    for (int it = 0; it < N; it++) ppresent_cpu(g_screen);
    out[0] = plat_ticks_200hz() - t0;
    uint32_t t1 = plat_ticks_200hz();
    for (int it = 0; it < N; it++) ppresent_st_asm(g_ptmp, g_screen + 8*ST_ROWBYTES, PMAP_STRIDE_W*2, ST_ROWBYTES, 160);
    out[1] = plat_ticks_200hz() - t1;
}

/* Phase-3 present microbench: CPU vs Blitter present, N iters into g_screen. out[0]=CPU ticks, out[1]=Blitter. */
void rdr_present_microbench(uint32_t out[2], int N)
{
    for (int i = 0; i < PMAP_ROWS*PMAP_STRIDE_W; i++) g_ptmp[i] = (uint16_t)(((uint32_t)i*40503u)>>3);
    uint32_t t0 = plat_ticks_200hz();
    for (int it = 0; it < N; it++) ppresent_cpu(g_screen);
    out[0] = plat_ticks_200hz() - t0;
    uint32_t t1 = plat_ticks_200hz();
    g_ppresent_dst = g_screen;
    for (int it = 0; it < N; it++) (void)Supexec(ppresent_blitter_super);
    out[1] = plat_ticks_200hz() - t1;
}
#endif

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
        c2p4_lut_asm(px, g_row_lut[y], r + lastw, 1);           /* c2p the new rightmost group */
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

RDR_MAKE_VTABLE(st);          /* -> const rdr_vtable rdr_be_st */
