// SPDX-License-Identifier: GPL-2.0-or-later
/* u6map.h — Ultima VI world geography (MAP + CHUNKS), portable, M2.
 * See /docs/formats/map_chunks.md.
 *
 * The world is a grid of 8x8-tile chunks. CHUNKS is 1024 chunk templates
 * (64 8-bit tile indices each). MAP holds 12-bit chunk indices, nibble-packed
 * 2-per-3-bytes: a 128x128 surface followed by 5 dungeon levels of 32x32.
 * The surface wraps toroidally on both axes (1024x1024 tiles).
 */
#ifndef U6E_U6MAP_H
#define U6E_U6MAP_H

#include <stdint.h>
#include <stdbool.h>

#define U6_SURFACE_CHUNKS 128            /* chunks per side, surface          */
#define U6_SURFACE_TILES  (U6_SURFACE_CHUNKS * 8)   /* 1024 tiles per side    */
#define U6_DUNGEON_CHUNKS 32
#define U6_DUNGEON_LEVELS 5
#define U6_CHUNK_COUNT    1024

typedef struct {
    const uint8_t *chunks;               /* CHUNKS buffer: 1024 * 64 bytes    */
    int32_t        chunks_len;
    uint16_t       surface[U6_SURFACE_CHUNKS * U6_SURFACE_CHUNKS];  /* chunk idx */
    uint16_t       dungeon[U6_DUNGEON_LEVELS][U6_DUNGEON_CHUNKS * U6_DUNGEON_CHUNKS];
    uint16_t       max_chunk_index;      /* for validation                    */
} u6map_t;

/* Decode MAP + CHUNKS. Returns false on size mismatch. */
bool u6map_load(u6map_t *m, const uint8_t *map, int32_t map_len,
                const uint8_t *chunks, int32_t chunks_len);

/* Surface tile index at (x,y), toroidally wrapped. 0..255. */
uint8_t u6map_surface_tile(const u6map_t *m, int32_t x, int32_t y);

#endif
