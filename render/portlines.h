/* portlines.h — portrait LINE COLOURS on 16-colour hardware (ST/STE).
 *
 * Spec: docs/design/palette_lowcolor.md "Portrait line colours". Plain C, no OS calls; shared by the
 * engine (render/gamedemo.c) and the host converter (tools/mkworld.c), which builds the data and
 * previews it with the same mapping the ST uses.
 *
 * During a conversation the screen shows 12 fixed colours (slots 0..11). Slots 12..15 are reloaded
 * on every row of the portrait by a Timer-B interrupt, so each portrait row sees the 12 fixed colours
 * plus 4 of its own. The data per portrait (one record of the U6FE.PAK section FES_PLINE):
 *   [48]  the 16 as ST 3-bit levels {r,g,b} (slots 12..15 unused)
 *   [48]  the same as STE 4-bit levels
 *   [256] the 256->16 LUT for everything but the portrait (maps into 0..11)
 *   [PL_ROWS x 4 x u16] each row's 4 colours, big-endian 0x0RGB with 4-bit STE levels; a plain ST
 *         shows each level >> 1.
 * The section is [u16 nport][u16 record size] then nport records. */
#ifndef PORTLINES_H
#define PORTLINES_H
#include <stdint.h>
#include "../dataformats/mul16.h"

#define PL_ROWS  64
#define PL_W     56
#define PL_FIXED 12
#define PL_LINES (PL_ROWS*4*2)
#define PL_REC   (48+48+256+PL_LINES)

/* weighted squared distance in RGB6 x16 units (the same weights as portdither.h; differences within
 * +-1008, so one MULS.W per component on a 68000) */
static inline int32_t pl_d2(int dr, int dg, int db)
{ int32_t r = muls16((int16_t)dr,(int16_t)dr), g = muls16((int16_t)dg,(int16_t)dg), b = muls16((int16_t)db,(int16_t)db);
  return r + r + r + (g << 2) + (b << 1); }

/* The 16 colours one portrait row sees, in RGB6 x16 units: the fixed slots from lvl (0..maxlvl),
 * slots 12..15 from the row's words (4-bit; >>1 when maxlvl is 7). */
static inline void pl_row_colours(const uint8_t *lvl, int maxlvl, const uint8_t *words, int16_t c[16][3])
{
    for (int i = 0; i < PL_FIXED; i++) for (int k = 0; k < 3; k++)
        c[i][k] = (int16_t)(((int)lvl[i*3+k]*63*16 + maxlvl/2) / maxlvl);
    for (int j = 0; j < 4; j++) {
        unsigned w = ((unsigned)words[j*2] << 8) | words[j*2+1];
        for (int k = 0; k < 3; k++) {
            int L = (int)((w >> (8 - 4*k)) & 15u);
            if (maxlvl == 7) L >>= 1;
            c[PL_FIXED+j][k] = (int16_t)((L*63*16 + maxlvl/2) / maxlvl); } }
}

/* The slot (0..15) for each pixel of one portrait row: the nearest of the row's 16 colours. Fixed
 * slots not in `avail` (bit i) are skipped. Colour 255 maps to slot 255 (left alone). */
static inline void pl_map_row(const uint8_t *row, int w, const uint8_t *rgb6, int16_t c[16][3],
                              unsigned avail, uint8_t *out)
{
    uint8_t seen[256], slot[256];
    for (int i = 0; i < 256; i++) seen[i] = 0;
    for (int x = 0; x < w; x++) {
        int v = row[x];
        if (v == 255) { out[x] = 255; continue; }
        if (!seen[v]) {
            int r = rgb6[v*3]*16, g = rgb6[v*3+1]*16, b = rgb6[v*3+2]*16, best = 0; int32_t be = 0x7fffffff;
            for (int i = 0; i < 16; i++) {
                if (i < PL_FIXED && !(avail & (1u << i))) continue;
                int32_t e = pl_d2(c[i][0]-r, c[i][1]-g, c[i][2]-b);
                if (e < be) { be = e; best = i; } }
            seen[v] = 1; slot[v] = (uint8_t)best; }
        out[x] = slot[v];
    }
}
#endif
