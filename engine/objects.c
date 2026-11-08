// SPDX-License-Identifier: GPL-2.0-or-later
/* objects.c — world object store + inventory (M6). See objects.h. */
#include "objects.h"

void objworld_init(objworld_t *o, wobj_t *objs, int nobjs, const uint8_t *gettable)
{
    o->objs = objs;
    o->nobjs = nobjs;
    o->gettable = gettable;
    o->stackable = 0;                            /* set by the caller when it has the table */
    o->ninv = 0;
    o->add_owner = 0;
    o->away = 0;
}

int objworld_at(const objworld_t *o, int x, int y)
{
    int found = -1;                       /* last match = topmost (drawn last) */
    for (int i = 0; i < o->nobjs; i++)
        if (o->objs[i].alive && o->objs[i].x == x && o->objs[i].y == y)
            found = i;
    return found;
}

int objworld_can_get(const objworld_t *o, uint16_t obj_n)
{
    return (obj_n < 1024 && o->gettable && o->gettable[obj_n]) ? 1 : 0;
}

int objworld_get(objworld_t *o, int idx)
{
    if (idx < 0 || idx >= o->nobjs) return 0;
    wobj_t *w = &o->objs[idx];
    if (!w->alive || !objworld_can_get(o, w->obj_n)) return 0;
    uint16_t q = w->qty ? w->qty : 1;            /* take the whole stack (objblk.md qty16) */

    for (int i = 0; i < o->ninv; i++)            /* merge into an existing stack — same obj AND
                                                  * same quality (two different KEYS must not merge) */
        if (!o->no_merge && !o->inv[i].in && o->inv[i].obj_n == w->obj_n && o->inv[i].quality == w->quality && o->inv[i].owner == o->add_owner
            && (o->inv[i].tile == w->tile || (o->stackable && ((o->stackable[w->obj_n >> 3] >> (w->obj_n & 7)) & 1)))) {   /* a moonstone's frame is its phase */
            uint32_t t = (uint32_t)o->inv[i].qty + q;
            o->inv[i].qty = (uint16_t)(t > 65535u ? 65535u : t);
            w->alive = 0; w->dirty = 1; return 1;    /* taken -> re-serialise in-inventory on save */
        }

    if (o->ninv >= OBJ_INV_MAX) return 0;        /* inventory full */
    o->inv[o->ninv].obj_n   = w->obj_n;
    o->inv[o->ninv].tile    = w->tile;
    o->inv[o->ninv].qty     = q;
    o->inv[o->ninv].quality = w->quality;
    o->inv[o->ninv].owner   = o->add_owner;
    o->inv[o->ninv].in = 0; o->inv[o->ninv].id = 0; o->inv[o->ninv].seq = 0;
    o->ninv++;
    w->alive = 0; w->dirty = 1;                       /* taken -> re-serialise in-inventory on save */
    return 1;
}

int objworld_has(const objworld_t *o, uint16_t obj_n, uint8_t quality)
{
    for (int i = 0; i < o->ninv; i++)
        if (o->inv[i].obj_n == obj_n && o->inv[i].quality == quality && o->inv[i].qty > 0)
            return 1;
    return 0;
}

int objworld_count(const objworld_t *o, uint16_t obj_n)
{
    int total = 0;
    for (int i = 0; i < o->ninv; i++)
        if (o->inv[i].obj_n == obj_n && !(o->away && o->away[o->inv[i].owner])) total += o->inv[i].qty;
    return total;
}

int objworld_consume(objworld_t *o, uint16_t obj_n, int n)
{
    int removed = 0;
    for (int i = 0; i < o->ninv && removed < n; i++) {
        if (o->inv[i].obj_n != obj_n || o->inv[i].qty == 0 || (o->away && o->away[o->inv[i].owner])) continue;
        int take = n - removed;
        if (take > o->inv[i].qty) take = o->inv[i].qty;
        o->inv[i].qty -= (uint16_t)take;
        removed += take;
    }
    /* compact out any stacks emptied to 0 */
    int w = 0;
    for (int i = 0; i < o->ninv; i++)
        if (o->inv[i].qty > 0) o->inv[w++] = o->inv[i];
    o->ninv = w;
    return removed;
}
