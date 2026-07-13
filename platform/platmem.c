// SPDX-License-Identifier: GPL-2.0-or-later
/* platmem.c — pool grab + bump sub-allocators.
 *
 * TOS calls used (named for verification):
 *   - Mxalloc(amt,mode)  : XBIOS-era GEMDOS 0x44; mode MX_STRAM/MX_TTRAM (size
 *                          query when amt == -1). Needs GEMDOS >= 0.19.
 *   - Malloc(amt)        : GEMDOS 0x48 fallback when Mxalloc absent.
 *   - Mfree(p)           : GEMDOS 0x49.
 *   - Supexec(fn)        : XBIOS 0x26, to read phystop (protected sysvar).
 * mintlib's startup already Mshrinks the TPA, so GEMDOS has free memory to
 * hand back here — we do NOT Mshrink ourselves. (Verified: Mxalloc(-1,...) at
 * startup reports several MB free.)
 */
#include "platmem.h"

#include <osbind.h>
#include <mint/ostruct.h>   /* MX_STRAM, MX_TTRAM */

/* Leave headroom so GEMDOS still has ST-RAM for its internal file DMA buffers. */
#define ST_HEADROOM     (128u * 1024u)
#define ALT_HEADROOM    (64u  * 1024u)
/* Scratch is carved from the FAST pool at init, when it's still empty, so it
 * permanently shrinks the fast pool available to the game (which never uses
 * scratch — only the selftest does, and only 8 KB of it). On a 4 MB-TT-RAM
 * machine the fast pool is only ~2.7 MB (the TPA also lives in TT-RAM), so an
 * over-large carve here can starve rdr_init's 150 KB backbuffer of fast RAM and
 * push it into slow ST-RAM. Keep this small; grow it only alongside an actual
 * per-scene scratch consumer AND pool headroom to spare. */
#define SCRATCH_SIZE    (64u  * 1024u)

typedef struct { uint8_t *base; uint32_t size; uint32_t off; } bump_t;

static bump_t  g_chip;
static bump_t  g_fast_alt;     /* used only when real Alt-RAM exists */
static bump_t *g_fast;         /* -> g_fast_alt, or -> g_chip on ST   */
static bump_t  g_scratch;
static bool    g_have_alt;
static bool    g_used_mxalloc;
static uint32_t g_phystop;

static long read_phystop(void) { return *(long *)0x42eL; }

static void *bump_alloc(bump_t *b, uint32_t n, uint32_t align)
{
    /* Align the ABSOLUTE address, not just the offset: the pool base from
     * Mxalloc is only word/paragraph aligned, so aligning the offset alone
     * leaves screen/DMA buffers mis-aligned (the shifter then mis-fetches). */
    uintptr_t base    = (uintptr_t)b->base;
    uintptr_t aligned = (base + b->off + (align - 1)) & ~(uintptr_t)(align - 1);
    uint32_t  off     = (uint32_t)(aligned - base);
    if (n == 0u || off + n > b->size) return (void *)0;   /* out of pool */
    b->off = off + n;
    return b->base + off;
}

/* Grab one pool of (largest-free - headroom) in the requested RAM kind.
 * Returns base (and sets *got_size) or NULL. */
static uint8_t *grab_pool(int mx_mode, uint32_t headroom, uint32_t *got_size,
                          bool use_mxalloc)
{
    long avail = use_mxalloc ? Mxalloc(-1L, mx_mode)
                             : Malloc(-1L);             /* size of largest block */
    if (avail <= 0) { *got_size = 0; return (void *)0; }

    uint32_t want = (uint32_t)avail;
    if (want <= headroom) { *got_size = 0; return (void *)0; }
    want -= headroom;

    long base = use_mxalloc ? Mxalloc((long)want, mx_mode)
                            : Malloc((long)want);
    if (base <= 0) { *got_size = 0; return (void *)0; }
    *got_size = want;
    return (uint8_t *)base;
}

bool plat_mem_init(const platform_caps_t *caps)
{
    g_used_mxalloc = caps->has_mxalloc;
    g_phystop = (uint32_t)Supexec(read_phystop);

    /* Chip pool: ST-RAM. Fatal if we can't get it. */
    g_chip.base = grab_pool(MX_STRAM, ST_HEADROOM, &g_chip.size, g_used_mxalloc);
    g_chip.off  = 0;
    if (!g_chip.base) return false;

    /* Fast pool: prefer genuine Alt/TT-RAM. */
    g_have_alt = false;
    if (g_used_mxalloc) {
        g_fast_alt.base = grab_pool(MX_TTRAM, ALT_HEADROOM, &g_fast_alt.size, true);
        g_fast_alt.off  = 0;
        if (g_fast_alt.base) g_have_alt = true;
    }
    g_fast = g_have_alt ? &g_fast_alt : &g_chip;   /* fall back to ST-RAM pool */

    /* Scratch: carve a fixed region out of the fast pool. */
    uint32_t sc = SCRATCH_SIZE;
    if (sc > g_fast->size - g_fast->off) sc = (g_fast->size - g_fast->off) / 2u;
    g_scratch.base = bump_alloc(g_fast, sc, 16u);
    g_scratch.size = g_scratch.base ? sc : 0u;
    g_scratch.off  = 0;
    return true;
}

void plat_mem_shutdown(void)
{
    if (g_chip.base)     { Mfree((void *)g_chip.base);     g_chip.base = 0; }
    if (g_fast_alt.base) { Mfree((void *)g_fast_alt.base); g_fast_alt.base = 0; }
    g_fast = 0;
}

void *plat_alloc_chip(uint32_t n) { return bump_alloc(&g_chip, n, 256u); }
void *plat_alloc_fast(uint32_t n) { return bump_alloc(g_fast,  n, 16u);  }

plat_mark plat_scratch_mark(void)          { return g_scratch.off; }
void     *plat_scratch_alloc(uint32_t n)   { return bump_alloc(&g_scratch, n, 16u); }
void      plat_scratch_reset(plat_mark m)  { if (m <= g_scratch.size) g_scratch.off = m; }

/* Scoped (LIFO) region free on the chip pool — the streaming primitive (see platmem.h). */
plat_mark plat_chip_mark(void)             { return g_chip.off; }
void      plat_chip_reset(plat_mark m)     { if (m <= g_chip.size) g_chip.off = m; }

bool plat_addr_is_fastram(const void *p)
{
    return (uint32_t)(uintptr_t)p >= g_phystop;
}

void plat_mem_get_info(plat_mem_info_t *out)
{
    out->chip_base    = g_chip.base;     out->chip_size    = g_chip.size;     out->chip_used    = g_chip.off;
    out->fast_base    = g_fast->base;    out->fast_size    = g_fast->size;    out->fast_used    = g_fast->off;
    out->scratch_base = g_scratch.base;  out->scratch_size = g_scratch.size;  out->scratch_used = g_scratch.off;
    out->have_altram  = g_have_alt;
    out->used_mxalloc = g_used_mxalloc;
    out->phystop      = g_phystop;
}
