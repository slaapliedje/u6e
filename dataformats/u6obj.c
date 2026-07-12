// SPDX-License-Identifier: GPL-2.0-or-later
/* u6obj.c — object placement decode. See u6obj.h / docs/formats/objblk.md. */
#include "u6obj.h"

#define MAX_PER_BLOCK 4096   /* sanity cap so a bad count can't run away */

static void decode_rec(const uint8_t *b, u6_object_t *ob)
{
    ob->status   = b[0];
    ob->x        = (uint16_t)(b[1] | ((b[2] & 0x03) << 8));
    ob->y        = (uint16_t)((b[2] >> 2) | ((b[3] & 0x0F) << 6));
    ob->z        = (uint8_t)(b[3] >> 4);
    ob->obj_n    = (uint16_t)(b[4] | ((b[5] & 0x03) << 8));
    ob->frame    = (uint8_t)(b[5] >> 2);
    ob->quantity = b[6];   /* qty byte FIRST, then quality (objblk.md §2, corrected) */
    ob->quality  = b[7];
    ob->parent   = U6OBJ_PARENT_NONE;
}

int32_t u6obj_decode(const uint8_t *blk, int32_t len, u6_object_t *out, int32_t max)
{
    int32_t p = 0, nout = 0;
    while (p + 2 <= len) {
        int count = blk[p] | (blk[p + 1] << 8);
        p += 2;
        if (count > MAX_PER_BLOCK) break;        /* misaligned/corrupt: stop */
        for (int o = 0; o < count && p + 8 <= len; o++) {
            const uint8_t *b = blk + p;
            p += 8;
            if (b[0] & 0x08) continue;            /* contained / inventory */
            if (nout < max) decode_rec(b, &out[nout++]);
        }
    }
    return nout;
}

int32_t u6obj_decode_world(const uint8_t *blk, int32_t len, u6_object_t *out, int32_t max)
{
    int32_t p = 0, nout = 0;
    static int32_t lmap[MAX_PER_BLOCK];          /* chunk-local record idx -> out idx (host tool: static ok) */
    while (p + 2 <= len) {
        int count = blk[p] | (blk[p + 1] << 8);
        p += 2;
        if (count > MAX_PER_BLOCK) break;        /* misaligned/corrupt: stop */
        for (int o = 0; o < count && p + 8 <= len; o++) {
            const uint8_t *b = blk + p;
            p += 8;
            lmap[o] = -1;
            int loc = b[0] & 0x18;
            if (loc != 0x00 && loc != 0x08) continue;   /* inventory/readied: not world */
            if (nout >= max) continue;
            u6_object_t *ob = &out[nout];
            decode_rec(b, ob);
            if (loc == 0x08) {                   /* contained: x/y bits are the parent link */
                int pl = ((ob->y & 0x3f) << 10) | ob->x;   /* record idx in THIS superchunk */
                if (pl >= o || lmap[pl] < 0) continue;     /* parent not emitted (inventory root / forward ref): drop */
                ob->parent = (uint16_t)lmap[pl];
            }
            lmap[o] = nout++;
        }
    }
    return nout;
}
