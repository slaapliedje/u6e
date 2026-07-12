/* u6shape.c — U6 SHAPE decoder. See u6shape.h + docs/formats/shapes.md.
 * Clean-room: implemented from our shapes.md spec (container [u32 size][N u32
 * offsets], shape hdr rightX/leftX/upperY/lowerY, plain/RLE pixel blocks). */
#include "u6shape.h"

static uint32_t le32(const uint8_t *p){ return (uint32_t)(p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24)); }
static uint16_t le16(const uint8_t *p){ return (uint16_t)(p[0]|(p[1]<<8)); }
static int16_t  le16s(const uint8_t *p){ return (int16_t)(p[0]|(p[1]<<8)); }

int u6shape_count(const uint8_t *d, int32_t len)
{
    if (!d || len < 8) return 0;
    uint32_t off0 = le32(d + 4);
    if (off0 < 8 || off0 > (uint32_t)len) return 0;
    return (int)((off0 - 4) / 4);
}

int u6shape_decode(const uint8_t *d, int32_t len, int idx,
                   int *w, int *h, uint8_t *out, int32_t outcap)
{
    int n = u6shape_count(d, len);
    if (idx < 0 || idx >= n) return 0;
    uint32_t off = le32(d + 4 + idx * 4);
    if (off + 8 > (uint32_t)len) return 0;
    const uint8_t *sh = d + off, *end = d + len;
    int rX = le16(sh), lX = le16(sh + 2), uY = le16(sh + 4), lY = le16(sh + 6);
    int W = lX + rX + 1, H = uY + lY + 1;
    if (W <= 0 || H <= 0 || W > 2048 || H > 2048) return 0;
    if ((int32_t)W * H > outcap) return 0;

    for (int32_t i = 0; i < (int32_t)W * H; i++) out[i] = 0;   /* background */
    const uint8_t *p = sh + 8;
    while (p + 6 <= end) {
        uint16_t dl = le16(p); p += 2;
        if (dl == 0) break;
        int blen = dl >> 1, flag = dl & 1;
        int px = lX + le16s(p); p += 2;
        int py = uY + le16s(p); p += 2;
        int wrote = 0;
        while (wrote < blen && p < end) {
            if (flag) {                              /* RLE */
                uint8_t b = *p++; int cnt = b >> 1;
                if (b & 1) { uint8_t v = (p < end) ? *p++ : 0;
                    for (int i = 0; i < cnt; i++) { if (px >= 0 && px < W && py >= 0 && py < H) out[py * W + px] = v; px++; } }
                else       { for (int i = 0; i < cnt && p < end; i++) { uint8_t v = *p++;
                    if (px >= 0 && px < W && py >= 0 && py < H) out[py * W + px] = v; px++; } }
                wrote += cnt;
            } else {                                 /* plain literal */
                uint8_t v = *p++; if (px >= 0 && px < W && py >= 0 && py < H) out[py * W + px] = v; px++; wrote++;
            }
        }
    }
    *w = W; *h = H;
    return 1;
}
