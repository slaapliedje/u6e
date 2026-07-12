// SPDX-License-Identifier: GPL-2.0-or-later
/* u6shape.h — U6 SHAPE (*.shp) decoder (docs/formats/shapes.md).
 *
 * Portable C (host + m68k): operates on the DECOMPRESSED shape file — LZW-
 * decompress the raw .shp with u6lzw first, then call these. Pixels are U6PAL
 * palette indices; background/uncovered = 0.
 */
#ifndef U6E_U6SHAPE_H
#define U6E_U6SHAPE_H

#include <stdint.h>

/* Number of shapes in the decompressed file d[0..len), or 0 if malformed. */
int u6shape_count(const uint8_t *d, int32_t len);

/* Decode shape `idx` into out[] as w*h 8-bit palette indices (row-major, top-left).
 * Fills *w,*h. Returns 1 on success, 0 on error (bad idx / dims / out too small). */
int u6shape_decode(const uint8_t *d, int32_t len, int idx,
                   int *w, int *h, uint8_t *out, int32_t outcap);

#endif
