// SPDX-License-Identifier: GPL-2.0-or-later
/* u6obj.h — Ultima VI object placement (OBJBLK / LZOBJBLK), portable, M6.
 * See /docs/formats/objblk.md.
 *
 * Objects are stored per superchunk as [u16 count][count x 8-byte records].
 * The DOS LZOBJBLK is all blocks LZW'd together; the ST keeps them as separate
 * OBJBLKxx files. Coordinates are WORLD-ABSOLUTE (so the superchunk storage
 * order doesn't affect placement). Multi-byte fields are little-endian.
 */
#ifndef U6E_U6OBJ_H
#define U6E_U6OBJ_H

#include <stdint.h>

#define U6OBJ_PARENT_NONE 0xFFFF

typedef struct {
    uint16_t x, y;     /* world tile coords (0..1023); raw link bits if contained */
    uint8_t  z;        /* level: 0 = surface            */
    uint16_t obj_n;    /* object number (0..1023)       */
    uint8_t  frame;    /* frame / orientation           */
    uint8_t  quality;  /* per-object datum              */
    uint8_t  quantity; /* stack count (low byte)        */
    uint8_t  status;   /* status flags                  */
    uint16_t parent;   /* out-index of the containing object, or U6OBJ_PARENT_NONE */
} u6_object_t;

/* Decode ON-MAP objects (status bit 0x08 = contained/inventory is skipped) from
 * the decompressed OBJBLK stream. Returns the number written (<= max). */
int32_t u6obj_decode(const uint8_t *blk, int32_t len, u6_object_t *out, int32_t max);

/* Decode the WORLD objects: on-map records PLUS in-container records whose
 * container chain roots on the map (chest contents, bag-in-chest, ...). A
 * contained record's `parent` = the OUT-array index of its container, resolved
 * from the per-superchunk record-index link `((y&0x3f)<<10)|x` (objblk.md §2);
 * on-map records get U6OBJ_PARENT_NONE. Inventory/readied records (and any
 * contained record whose parent didn't resolve) are skipped. */
int32_t u6obj_decode_world(const uint8_t *blk, int32_t len, u6_object_t *out, int32_t max);

/* BASETILE: object number -> base tile index (the object's tile is this + frame). */
static inline uint16_t u6_basetile(const uint8_t *basetile, uint16_t obj_n)
{
    return (uint16_t)(basetile[obj_n * 2] | (basetile[obj_n * 2 + 1] << 8));
}

#endif
