// SPDX-License-Identifier: GPL-2.0-or-later
/* u6tile.c — tile bitmap access. See /docs/formats/tiles.md. */
#include "u6tile.h"

bool u6tile_init(u6tileset_t *ts, const uint8_t *maptiles, int32_t maptiles_len,
                 const uint8_t *tileindx, int32_t tileindx_len)
{
    if (!maptiles || !tileindx || tileindx_len < 2) return false;
    ts->bitmaps = maptiles;
    ts->bitmaps_len = maptiles_len;
    ts->index = tileindx;
    ts->tile_count = tileindx_len / 2;
    ts->masktype = 0;
    return true;
}

void u6tile_set_masktype(u6tileset_t *ts, const uint8_t *masktype)
{
    ts->masktype = masktype;
}

/* Pixel-block RLE (MASKTYPE 0xA): output is 256 bytes pre-filled with 255
 * (transparent); the stream is a series of runs [u16 LE disp][u8 len][len
 * pixels], a write cursor advancing by `x` (a transparent gap) then `len`
 * opaque pixels each run, terminated by len==0. The gap formula and the
 * cursor's run-to-run accumulation match the original's decoder. */
static void decode_pblck(const uint8_t *src, int32_t src_avail, uint8_t out[256])
{
    for (int i = 0; i < 256; i++) out[i] = 255;
    if (src_avail < 4) return;
    int32_t p = 1;                       /* skip byte 0 (block count, unused) */
    int pos = 0;
    for (;;) {
        if (p + 3 > src_avail) break;
        int disp = src[p] | (src[p + 1] << 8);
        int len  = src[p + 2];
        if (len == 0) break;
        int x = (uint16_t)disp % 160u + (disp >= 1760 ? 160 : 0);   /* 16-bit: one divu.w on the 68000 */
        pos += x;
        if (p + 3 + len > src_avail) break;
        for (int k = 0; k < len; k++) {
            int o = pos + k;
            if (o >= 0 && o < 256) out[o] = src[p + 3 + k];
        }
        pos += len;
        p += 3 + len;
    }
}

void u6tile_decode(const u6tileset_t *ts, int tile, uint8_t out[256])
{
    int32_t base = u6tile_offset(ts, tile);
    if (base < 0 || base >= ts->bitmaps_len) { for (int i=0;i<256;i++) out[i]=0; return; }
    int mt = ts->masktype ? ts->masktype[tile] : U6TILE_PLAIN;
    if (mt == U6TILE_PBLCK) {
        decode_pblck(ts->bitmaps + base, ts->bitmaps_len - base, out);
    } else {                              /* PLAIN or TRANS: 256-byte bitmap */
        int32_t n = ts->bitmaps_len - base; if (n > 256) n = 256;
        const uint8_t *b = ts->bitmaps + base;
        for (int i = 0; i < n; i++) out[i] = b[i];
        for (int i = (int)n; i < 256; i++) out[i] = 0;
    }
}

int32_t u6tile_offset(const u6tileset_t *ts, int tile)
{
    if (tile < 0 || tile >= ts->tile_count) return -1;
    uint32_t units = (uint32_t)ts->index[tile * 2] |
                     ((uint32_t)ts->index[tile * 2 + 1] << 8);
    return (int32_t)(units * 16u);
}

uint8_t u6tile_pixel(const u6tileset_t *ts, int tile, int x, int y)
{
    if (x < 0 || x >= U6_TILE_W || y < 0 || y >= U6_TILE_H) return 0;
    /* PBLCK tiles must be decoded as a whole; plain tiles read directly. */
    if (ts->masktype && tile >= 0 && tile < ts->tile_count &&
        ts->masktype[tile] == U6TILE_PBLCK) {
        uint8_t buf[256];
        u6tile_decode(ts, tile, buf);
        return buf[y * U6_TILE_W + x];
    }
    int32_t base = u6tile_offset(ts, tile);
    if (base < 0) return 0;
    int32_t idx = base + y * U6_TILE_W + x;
    if (idx < 0 || idx >= ts->bitmaps_len) return 0;
    return ts->bitmaps[idx];
}

uint8_t u6tile_repr(const u6tileset_t *ts, int tile)
{
    /* Center pixel; representative for solid terrain tiles. */
    return u6tile_pixel(ts, tile, U6_TILE_W / 2, U6_TILE_H / 2);
}
