/* platmem.h — memory pools with alloc-by-INTENT (M1).
 *
 * The engine never calls a TOS allocator; it asks for memory by what the
 * memory is FOR:
 *   - plat_alloc_chip : ST-RAM, safe for the Shifter/VIDEL + DMA (screen,
 *                       sound, disk buffers).
 *   - plat_alloc_fast : Alt/TT-RAM when present (big CPU speedup), else falls
 *                       back to ST-RAM transparently.
 *   - scratch         : a bump region for per-scene churn (mark / reset).
 *
 * We grab a FEW LARGE pools up front (via Mxalloc) and sub-allocate ourselves,
 * because period TOS Malloc is unreliable with many small blocks. Allocations
 * are monotonic (bump) — individual frees aren't supported; pools are released
 * wholesale at shutdown. See platform/CLAUDE.md.
 */
#ifndef U6E_PLATMEM_H
#define U6E_PLATMEM_H

#include <stdint.h>
#include <stdbool.h>
#include "platform.h"

typedef uint32_t plat_mark;   /* opaque scratch position */

typedef struct {
    void    *chip_base;   uint32_t chip_size,   chip_used;
    void    *fast_base;   uint32_t fast_size,   fast_used;
    void    *scratch_base; uint32_t scratch_size, scratch_used;
    bool     have_altram; /* fast pool is genuine Alt/TT-RAM, not ST-RAM       */
    bool     used_mxalloc;/* false => old GEMDOS, Malloc fallback path         */
    uint32_t phystop;     /* top of ST-RAM; addr >= this is Alt-RAM            */
} plat_mem_info_t;

/* Grab pools sized to free RAM. Returns false if the ST-RAM pool can't be
 * obtained (fatal). Safe on machines with no Alt-RAM and on pre-Mxalloc TOS. */
bool  plat_mem_init(const platform_caps_t *caps);
void  plat_mem_shutdown(void);

void *plat_alloc_chip(uint32_t n);   /* ST-RAM (chip/DMA-safe), 256-byte aligned */
void *plat_alloc_fast(uint32_t n);   /* Alt-RAM preferred, ST-RAM fallback        */

plat_mark plat_scratch_mark(void);
void     *plat_scratch_alloc(uint32_t n);
void      plat_scratch_reset(plat_mark mark);

void  plat_mem_get_info(plat_mem_info_t *out);
bool  plat_addr_is_fastram(const void *p);   /* via phystop */

#endif /* U6E_PLATMEM_H */
