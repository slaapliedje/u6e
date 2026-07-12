/* u6pal.h — Ultima VI palette decoder (portable, M2). See /docs/formats/palette.md.
 *
 * U6PAL holds 256 colors as RGB triples with 6-bit VGA components (0..63). We
 * expand to 8-bit RGB for rendering/conversion. (Only the first 768 bytes are
 * the palette; any trailing bytes are not palette data.) */
#ifndef U6E_U6PAL_H
#define U6E_U6PAL_H

#include <stdint.h>
#include <stdbool.h>

typedef struct { uint8_t r, g, b; } u6_rgb;   /* 8-bit per channel */

typedef struct { u6_rgb color[256]; } u6pal_t;

/* Parse U6PAL bytes (>=768) into 8-bit RGB. Returns false if too short. */
bool u6pal_parse(u6pal_t *pal, const uint8_t *data, int32_t len);

#endif
