/* passability.h — host-side derivation of a per-tile passability table (M5).
 *
 * PLACEHOLDER heuristic: a tile is impassable if its average colour is
 * water-blue (deep/shallow ocean), else walkable. Proper passability comes from
 * decoding TILEFLAG later (see /docs/rules/movement_turns.md). Host-only.
 */
#ifndef U6E_PASSABILITY_H
#define U6E_PASSABILITY_H

#include <stdint.h>
#include "u6tile.h"
#include "u6pal.h"

/* Real passability from TILEFLAG (tileflags1 block, indexed by tile). A tile is
 * blocked for walking if Impassable (bit1) or Wall (bit2) is set — 0x06. (Water
 * carries Impassable; shore is "Wet" only, so walkable; swamp is Damaging but
 * passable.) `tflag` is the raw 7168-byte TILEFLAG; tileflags1 is its first block.
 * Confirmed against real tiles (tools/passprobe). See docs/formats/tileflag.md. */
static inline void compute_passable_tflag(const uint8_t *tflag, uint8_t out[256])
{
    for (int t = 0; t < 256; t++) out[t] = (tflag[t] & 0x06) ? 0 : 1;
}

/* PLACEHOLDER colour heuristic (kept for reference); prefer compute_passable_tflag.
 * Fill out[256]: 1 = walkable, 0 = blocked. */
static inline void compute_passable(const u6tileset_t *ts, const u6pal_t *pal,
                                    uint8_t out[256])
{
    for (int t = 0; t < 256; t++) {
        long r = 0, g = 0, b = 0;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                u6_rgb c = pal->color[u6tile_pixel(ts, t, x, y)];
                r += c.r; g += c.g; b += c.b;
            }
        r /= 256; g /= 256; b /= 256;
        int water = (b > r + 16) && (b > g + 4);   /* blue-dominant => water */
        out[t] = water ? 0 : 1;
    }
}

#endif
