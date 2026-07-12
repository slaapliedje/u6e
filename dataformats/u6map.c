/* u6map.c — MAP + CHUNKS decode. See /docs/formats/map_chunks.md. */
#include "u6map.h"

/* Unpack `count` 12-bit values (2 per 3 bytes, little-endian nibble order)
 * from src into dst. Returns bytes consumed. */
static int32_t unpack12(const uint8_t *src, uint16_t *dst, int32_t count)
{
    int32_t si = 0, di = 0;
    while (di + 1 < count) {
        uint8_t b0 = src[si], b1 = src[si + 1], b2 = src[si + 2];
        dst[di++] = (uint16_t)(b0 | ((b1 & 0x0F) << 8));
        dst[di++] = (uint16_t)((b1 >> 4) | (b2 << 4));
        si += 3;
    }
    if (di < count) {                 /* odd trailing value (half a triple) */
        uint8_t b0 = src[si], b1 = src[si + 1];
        dst[di++] = (uint16_t)(b0 | ((b1 & 0x0F) << 8));
        si += 2;
    }
    return si;
}

bool u6map_load(u6map_t *m, const uint8_t *map, int32_t map_len,
                const uint8_t *chunks, int32_t chunks_len)
{
    const int32_t surf = U6_SURFACE_CHUNKS * U6_SURFACE_CHUNKS;        /* 16384 */
    const int32_t dung = U6_DUNGEON_CHUNKS * U6_DUNGEON_CHUNKS;        /* 1024  */
    const int32_t need = (surf * 3) / 2 + U6_DUNGEON_LEVELS * (dung * 3) / 2;
    if (!map || !chunks || map_len < need) return false;
    if (chunks_len < U6_CHUNK_COUNT * 64) return false;

    m->chunks = chunks;
    m->chunks_len = chunks_len;

    int32_t off = 0;
    off += unpack12(map + off, m->surface, surf);
    for (int lvl = 0; lvl < U6_DUNGEON_LEVELS; lvl++)
        off += unpack12(map + off, m->dungeon[lvl], dung);

    m->max_chunk_index = 0;
    for (int32_t i = 0; i < surf; i++)
        if (m->surface[i] > m->max_chunk_index) m->max_chunk_index = m->surface[i];

    return true;
}

uint8_t u6map_surface_tile(const u6map_t *m, int32_t x, int32_t y)
{
    x &= (U6_SURFACE_TILES - 1);          /* toroidal wrap (1024 = 2^10) */
    y &= (U6_SURFACE_TILES - 1);
    int32_t cx = x >> 3, cy = y >> 3;     /* chunk coords (0..127)   */
    int32_t wx = x & 7,  wy = y & 7;      /* within-chunk (0..7)     */

    /* The surface is stored as an 8x8 grid of "superchunks", each 16x16 chunks
     * (8*16 = 128). MAP lists superchunks row-major, chunks row-major within.
     * So the flat file index is blocked, not a simple cy*128+cx. */
    int32_t scx = cx >> 4, scy = cy >> 4; /* superchunk (0..7)       */
    int32_t icx = cx & 15, icy = cy & 15; /* chunk within (0..15)    */
    int32_t lin = (scy * 8 + scx) * 256 + (icy * 16 + icx);

    uint16_t cidx = m->surface[lin];
    if (cidx >= U6_CHUNK_COUNT) return 0;
    return m->chunks[cidx * 64 + wy * 8 + wx];
}
