// SPDX-License-Identifier: GPL-2.0-or-later
/* png.h — minimal RGB PNG writer (HOST-ONLY, for /tools validation dumps).
 * Not engine code. Writes 8-bit truecolor (RGB) PNGs using stored (uncompressed)
 * DEFLATE blocks, so there's no zlib dependency. */
#ifndef U6E_PNG_H
#define U6E_PNG_H

#include <stdint.h>

/* rgb = width*height*3 bytes, row-major, R,G,B. Returns 0 on success. */
int png_write_rgb(const char *path, int width, int height, const uint8_t *rgb);

#endif
