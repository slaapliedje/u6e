// SPDX-License-Identifier: GPL-2.0-or-later
/* u6pal.c — U6PAL palette decode. See /docs/formats/palette.md. */
#include "u6pal.h"

/* 6-bit (0..63) VGA component -> 8-bit (0..255), replicating high bits. */
static uint8_t expand6(uint8_t v) { return (uint8_t)((v << 2) | (v >> 4)); }

bool u6pal_parse(u6pal_t *pal, const uint8_t *data, int32_t len)
{
    if (!data || len < 768) return false;
    for (int i = 0; i < 256; i++) {
        pal->color[i].r = expand6(data[i * 3 + 0] & 0x3F);
        pal->color[i].g = expand6(data[i * 3 + 1] & 0x3F);
        pal->color[i].b = expand6(data[i * 3 + 2] & 0x3F);
    }
    return true;
}
