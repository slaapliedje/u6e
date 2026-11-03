// SPDX-License-Identifier: GPL-2.0-or-later
/* sparsebits.h — a 1024x1024 bit-per-tile map that costs memory only where bits are set.
 *
 * The map is 1024 pages of 32x32 tiles (128 bytes each), found through a table of page numbers;
 * page 0 means "all clear" and has no storage. U6's three such maps use 100-135 pages each, so 15-20 KB
 * instead of 128 KB, and clearing one touches only the table (docs/design/memory_streaming.md). */
#ifndef U6E_SPARSEBITS_H
#define U6E_SPARSEBITS_H

#include <stdint.h>
#include <string.h>

#define SB_PAGE_BYTES 128

typedef struct {
    uint16_t page[1024];    /* tile page -> 1-based index into store; 0 = all clear */
    uint8_t *store;         /* cap pages of SB_PAGE_BYTES */
    uint16_t npages, cap;
} sparsebits_t;

#define SB_PAGE_OF(x, y)   ((((y) >> 5 & 31) << 5) | ((x) >> 5 & 31))
#define SB_BYTE_OF(x, y)   (((y) & 31) * 4 + (((x) & 31) >> 3))

static inline int sb_get(const sparsebits_t *s, int x, int y)
{
    unsigned p = s->page[SB_PAGE_OF(x, y)];
    if (!p) return 0;
    return (s->store[(int32_t)(p - 1) * SB_PAGE_BYTES + SB_BYTE_OF(x, y)] >> (x & 7)) & 1;
}

static inline void sb_clear(sparsebits_t *s)
{
    memset(s->page, 0, sizeof s->page);
    s->npages = 0;
}

/* the storage byte for a tile, its page made if need be; NULL if the store is full */
static inline uint8_t *sb_byte(sparsebits_t *s, int x, int y)
{
    unsigned i = SB_PAGE_OF(x, y), p = s->page[i];
    if (!p) {
        if (s->npages >= s->cap) return 0;
        p = ++s->npages; s->page[i] = (uint16_t)p;
        memset(s->store + (int32_t)(p - 1) * SB_PAGE_BYTES, 0, SB_PAGE_BYTES);
    }
    return s->store + (int32_t)(p - 1) * SB_PAGE_BYTES + SB_BYTE_OF(x, y);
}

/* setting a bit with the store full drops it: the caps are about 2.5x what the whole game uses */
static inline void sb_set(sparsebits_t *s, int x, int y)
{
    uint8_t *b = sb_byte(s, x, y);
    if (b) *b |= (uint8_t)(1u << (x & 7));
}

#endif /* U6E_SPARSEBITS_H */
