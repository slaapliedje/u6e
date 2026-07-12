/* u6tile.h — Ultima VI tile bitmaps (portable, M2). See /docs/formats/tiles.md.
 *
 * Tiles are 16x16, 8 bits/pixel (palette indices). TILEINDX.VGA gives each
 * tile's data offset in 16-byte units into the (LZW-decompressed) MAPTILES.VGA;
 * a full tile is 256 bytes. Map geography uses tile indices 0..255 only (chunk
 * tile bytes are 8-bit), which is what the world-map render needs.
 */
#ifndef U6E_U6TILE_H
#define U6E_U6TILE_H

#include <stdint.h>
#include <stdbool.h>

#define U6_TILE_W 16
#define U6_TILE_H 16
#define U6_TILE_BYTES (U6_TILE_W * U6_TILE_H)

/* MASKTYPE.VGA per-tile encoding (one byte per tile): */
#define U6TILE_PLAIN 0x0   /* plain 256-byte bitmap                          */
#define U6TILE_TRANS 0x5   /* plain 256-byte bitmap, index 255 = transparent */
#define U6TILE_PBLCK 0xA   /* pixel-block RLE (runs + transparent gaps)      */

typedef struct {
    const uint8_t *bitmaps;     /* decompressed MAPTILES.VGA ++ raw OBJTILES */
    int32_t        bitmaps_len;
    const uint8_t *index;       /* TILEINDX.VGA (2 bytes LE per tile)   */
    int32_t        tile_count;  /* entries in TILEINDX                  */
    const uint8_t *masktype;    /* MASKTYPE.VGA (1 byte/tile) or NULL   */
} u6tileset_t;

bool u6tile_init(u6tileset_t *ts, const uint8_t *maptiles, int32_t maptiles_len,
                 const uint8_t *tileindx, int32_t tileindx_len);

/* Provide the per-tile MASKTYPE table (decompressed MASKTYPE.VGA) so that
 * transparent / pixel-block-encoded tiles decode correctly. Without it, all
 * tiles are treated as plain 256-byte bitmaps (wrong for PBLCK tiles). */
void u6tile_set_masktype(u6tileset_t *ts, const uint8_t *masktype);

/* Decode tile i into out[256] (16x16, top-left, 8bpp; 255 = transparent),
 * honouring its MASKTYPE. This is the correct way to read a tile's bitmap. */
void u6tile_decode(const u6tileset_t *ts, int tile, uint8_t out[256]);

/* Byte offset of tile i's bitmap in `bitmaps` (= TILEINDX[i] * 16). */
int32_t u6tile_offset(const u6tileset_t *ts, int tile);

/* Palette index of pixel (x,y) in tile i, or 0 if out of range. MASKTYPE-aware. */
uint8_t u6tile_pixel(const u6tileset_t *ts, int tile, int x, int y);

/* A representative palette index for tile i (its center pixel) — used to paint
 * one pixel per tile on the overview world map. */
uint8_t u6tile_repr(const u6tileset_t *ts, int tile);

#endif
