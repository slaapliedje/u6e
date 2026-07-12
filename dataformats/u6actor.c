/* u6actor.c — NPC actor table (OBJLIST) + daily schedules (SCHEDULE).
 * See u6actor.h and /docs/formats/schedule.md, /docs/formats/objlist.md. */
#include "u6actor.h"

/* x/y/z are packed identically in OBJLIST positions and SCHEDULE entries:
 *   x = b0 | ((b1 & 0x03) << 8)              (10-bit)
 *   y = ((b1 & 0xfc) >> 2) | ((b2 & 0x0f) << 6)  (10-bit)
 *   z = (b2 & 0xf0) >> 4                     (4-bit) */
static void unpack_xyz(const uint8_t *p, int16_t *x, int16_t *y, uint8_t *z)
{
    *x = (int16_t)(p[0] | ((p[1] & 0x03) << 8));
    *y = (int16_t)(((p[1] & 0xfc) >> 2) | ((p[2] & 0x0f) << 6));
    *z = (uint8_t)((p[2] & 0xf0) >> 4);
}

bool u6actor_load_objlist(u6actor_t *a, const uint8_t *o, int32_t len)
{
    if (!a || !o || len < 0x600) return false;
    for (int i = 0; i < U6_MAX_ACTORS; i++) {
        unpack_xyz(o + 0x100 + i*3, &a[i].x, &a[i].y, &a[i].z);   /* position table */
        const uint8_t *q = o + 0x400 + i*2;                        /* appearance table */
        /* ST SAVEGAME is BIG-ENDIAN: the obj_n/frame word is [hi,lo]. */
        a[i].obj_n = (uint16_t)(q[1] | ((q[0] & 0x03) << 8));
        a[i].frame = (uint8_t)((q[0] & 0xfc) >> 2);
        a[i].sched = 0; a[i].nsched = 0;
        if (a[i].obj_n == 0) { a[i].x = a[i].y = a[i].z = 0; }     /* unused/Exodus slot */
    }
    return true;
}

bool u6actor_load_schedule(u6actor_t *a, const uint8_t *s, int32_t len)
{
    if (!a || !s || len < 514) return false;
    uint16_t total = (uint16_t)(s[512] | (s[513] << 8));
    const uint8_t *entries = s + 514;
    if ((int32_t)514 + (int32_t)total*5 > len) return false;
    for (int i = 0; i < U6_MAX_ACTORS; i++) {
        uint16_t off  = (uint16_t)(s[i*2] | (s[i*2+1] << 8));
        if (total == 0 || off > total - 1) { a[i].sched = 0; a[i].nsched = 0; continue; }
        uint16_t next = total;                       /* default: runs to the end */
        if (i < U6_MAX_ACTORS - 1) {
            uint16_t o2 = (uint16_t)(s[(i+1)*2] | (s[(i+1)*2+1] << 8));
            if (o2 <= total - 1) next = o2;
        }
        a[i].sched  = entries + off*5;
        a[i].nsched = (int)(next - off);
    }
    return true;
}

void u6sched_decode(const uint8_t *e, u6sched_entry_t *out)
{
    out->hour     = (uint8_t)(e[0] & 0x1f);
    out->day      = (uint8_t)(e[0] >> 5);
    out->worktype = e[1];
    unpack_xyz(e + 2, &out->x, &out->y, &out->z);
}

int u6sched_active(const u6actor_t *a, int hour)
{
    if (!a || a->nsched <= 0) return -1;
    int best = -1, best_h = -1;
    for (int i = 0; i < a->nsched; i++) {
        u6sched_entry_t e; u6sched_decode(a->sched + i*5, &e);
        if ((int)e.hour <= hour && (int)e.hour > best_h) { best_h = e.hour; best = i; }
    }
    if (best < 0) best = a->nsched - 1;   /* before the first entry -> previous day's last */
    return best;
}
