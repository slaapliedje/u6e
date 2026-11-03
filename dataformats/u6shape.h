// SPDX-License-Identifier: GPL-2.0-or-later
/* u6shape.h — U6 SHAPE (*.shp) decoder (docs/formats/shapes.md).
 *
 * Portable C (host + m68k): operates on the DECOMPRESSED shape file — LZW-
 * decompress the raw .shp with u6lzw first, then call these. Pixels are U6PAL
 * palette indices. A shape is a set of pixel BLOCKS; canvas pixels not covered
 * by any block are TRANSPARENT and come back as U6SHAPE_TRANSPARENT (255 — the
 * same key the tiles/cursor use). Block pixels are literal, so index 0 inside a
 * block is opaque black. (Verified: no U6 .SHP uses 255 inside a block.)
 */
#ifndef U6E_U6SHAPE_H
#define U6E_U6SHAPE_H

#include <stdint.h>

#define U6SHAPE_TRANSPARENT 255

/* Number of shapes in the decompressed file d[0..len), or 0 if malformed. */
int u6shape_count(const uint8_t *d, int32_t len);

/* Decode shape `idx` into out[] as w*h 8-bit palette indices (row-major, top-left).
 * Fills *w,*h. Returns 1 on success, 0 on error (bad idx / dims / out too small). */
int u6shape_decode(const uint8_t *d, int32_t len, int idx,
                   int *w, int *h, uint8_t *out, int32_t outcap);

/* Canvas size of shape idx without decoding it (what u6shape_decode needs in out[]).
 * Returns 1 on success, 0 on a bad idx / dims. */
int u6shape_size(const uint8_t *d, int32_t len, int idx, int *w, int *h);

/* Hotspot (origin) of shape idx = canvas pixel (leftX, upperY): draw at (x - hx, y - hy).
 * Returns 1 on success. */
int u6shape_hotspot(const uint8_t *d, int32_t len, int idx, int *hx, int *hy);

#endif
