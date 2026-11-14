/* portdither.h — ordered dithering of a portrait onto 16-colour hardware (ST/STE).
 *
 * Spec: docs/design/palette_lowcolor.md "Portrait dithering". Plain C, no OS calls: the engine
 * (render/gamedemo.c, ST build) and the host converter's preview (tools/mkworld.c) share it, so
 * what the preview shows is what the ST draws.
 *
 * The backbuffer is 8-bit chunky and the ST backend maps it to 16 slots through a 256->16 LUT
 * at present. A portrait pixel of source colour c is drawn as one of TWO slots, A or B, picked
 * per screen position by a 4x4 Bayer threshold: B where bayer < t, so t/16 of the pixels show B.
 * The pair and t are chosen per colour so that A + t/16 (B - A) lands nearest c, with a flat
 * penalty on the pair's distance (a pattern of two distant colours reads as noise, not a blend).
 * The chosen slots are written to the backbuffer as a chunky index that the LUT maps to them
 * (inv[]), so the backend needs no change. */
#ifndef PORTDITHER_H
#define PORTDITHER_H
#include <stdint.h>

typedef struct {
    uint8_t ca[256], cb[256], t[256];  /* per source colour: chunky index for slot A / slot B, B's share 0..16 */
    uint8_t ok;                        /* 1 = built */
} pdith_t;

#ifndef PDITH_PEN
#define PDITH_PEN 6            /* mixing penalty: PDITH_PEN/32 of |B-A|^2. 2 speckled the backdrop, 4 left dark-red
                                  * specks in eye sockets, 8 barely dithered (tools/mkworld.c MKWORLD_DITHERPREVIEW) */
#endif
static const uint8_t pdith_bayer[4][4] = { {0,8,2,10}, {12,4,14,6}, {3,11,1,9}, {15,7,13,5} };

/* weighted squared distance in RGB6 x16 units (eye: green > red > blue) */
static inline int32_t pdith_d2(int dr, int dg, int db)
{ return (int32_t)3*((int32_t)dr*dr) + (int32_t)4*((int32_t)dg*dg) + (int32_t)2*((int32_t)db*db); }

/* Build the table.
 *   rgb6   : the 256-colour source palette, 0..63 per component
 *   lvl    : the 16 hardware slots as levels 0..maxlvl (7 on ST, 15 on STE)
 *   lut    : the 256->16 map the backend presents through
 *   used   : nonzero for each source colour the portrait contains (others: left undithered)
 * Colour 255 is never dithered (left as is). */
static inline void pdith_build(pdith_t *d, const uint8_t *rgb6, const uint8_t *lvl, int maxlvl,
                               const uint8_t *lut, const uint8_t *used)
{
    int16_t s[16][3]; uint8_t inv[16]; int have[16];
    for (int i = 0; i < 16; i++) { have[i] = 0; inv[i] = 0;
        for (int k = 0; k < 3; k++) s[i][k] = (int16_t)(((int)lvl[i*3+k]*63*16 + maxlvl/2) / maxlvl); }
    for (int c = 0; c < 255; c++) { int sl = lut[c] & 15; if (!have[sl]) { have[sl] = 1; inv[sl] = (uint8_t)c; } }
    for (int c = 0; c < 256; c++) {
        d->ca[c] = (uint8_t)c; d->cb[c] = (uint8_t)c; d->t[c] = 0;
        if (c == 255 || !used[c]) continue;
        int cr = rgb6[c*3]*16, cg = rgb6[c*3+1]*16, cbl = rgb6[c*3+2]*16;
        /* the two nearest available slots are the candidates for A */
        int n0 = -1, n1 = -1; int32_t e0 = 0x7fffffff, e1 = 0x7fffffff;
        for (int i = 0; i < 16; i++) { if (!have[i]) continue;
            int32_t e = pdith_d2(s[i][0]-cr, s[i][1]-cg, s[i][2]-cbl);
            if (e < e0) { e1 = e0; n1 = n0; e0 = e; n0 = i; } else if (e < e1) { e1 = e; n1 = i; } }
        if (n0 < 0) continue;
        int ba = n0, bb = n0, bt = 0; int32_t best = e0;          /* solid nearest */
        for (int ai = 0; ai < 2; ai++) { int a = ai ? n1 : n0; if (a < 0) continue;
            for (int b = 0; b < 16; b++) { if (b == a || !have[b]) continue;
                int dr = s[b][0]-s[a][0], dg = s[b][1]-s[a][1], db = s[b][2]-s[a][2];
                int32_t len = pdith_d2(dr, dg, db); if (len <= 0) continue;
                int32_t dot = (int32_t)3*dr*(cr-s[a][0]) + (int32_t)4*dg*(cg-s[a][1]) + (int32_t)2*db*(cbl-s[a][2]);
                if (dot <= 0) continue;
                int32_t tq = (dot*16 + len/2) / len; if (tq > 16) tq = 16; if (tq < 1) continue;
                if (tq == 16) continue;                            /* that is solid B, covered by n0/n1 */
                int mr = s[a][0] + (int)((dr*tq) >> 4), mg = s[a][1] + (int)((dg*tq) >> 4), mb = s[a][2] + (int)((db*tq) >> 4);
                int32_t e = pdith_d2(mr-cr, mg-cg, mb-cbl);
                /* mixing penalty: a flat share of the pair's own distance |B-A|^2, whatever t is, so a
                 * sparse sprinkle of a far colour costs as much as an even mix of it */
                e += (int32_t)((len * PDITH_PEN) >> 5);
                if (e < best) { best = e; ba = a; bb = b; bt = (int)tq; } } }
        d->ca[c] = inv[ba]; d->cb[c] = inv[bb]; d->t[c] = (uint8_t)bt;
    }
    d->ok = 1;
}

/* the chunky index to draw for source colour c at screen position (x,y) */
static inline uint8_t pdith_px(const pdith_t *d, uint8_t c, int x, int y)
{ return (pdith_bayer[y & 3][x & 3] < d->t[c]) ? d->cb[c] : d->ca[c]; }

#endif
