/* u6actor.h — Ultima VI NPC actors + daily schedules (portable, M6).
 *
 * Two original data sources (decoded clean-room from our /docs specs; format
 * understood by reading Nuvie's ActorManager/Actor):
 *   - OBJLIST (the savegame's actor table): each of 256 actors has a packed
 *     position (10-bit x, 10-bit y, 4-bit z) and an appearance (obj_n + frame).
 *   - SCHEDULE: per-actor daily routine entries (hour, day, worktype, location).
 *
 * Portable C: no TOS, no host-endianness assumptions (explicit LE reads).
 */
#ifndef U6E_U6ACTOR_H
#define U6E_U6ACTOR_H

#include <stdint.h>
#include <stdbool.h>

#define U6_MAX_ACTORS 256

typedef struct {
    uint8_t  hour;       /* 0..23                                  */
    uint8_t  day;        /* 0 = any day, else 1..7                 */
    uint8_t  worktype;   /* activity code (see U6 worktypes)        */
    int16_t  x, y;       /* scheduled location (10-bit each)        */
    uint8_t  z;          /* level (0 = surface)                     */
} u6sched_entry_t;

typedef struct {
    int16_t        x, y;     /* OBJLIST position (10-bit)           */
    uint8_t        z;
    uint16_t       obj_n;    /* appearance object number (10-bit)   */
    uint8_t        frame;    /* tile frame (also encodes direction) */
    const uint8_t *sched;    /* pointer to this actor's 5-byte entries (or NULL) */
    int            nsched;   /* number of schedule entries          */
} u6actor_t;

/* Decode OBJLIST: positions (offset 0x100, 3 bytes/actor) + appearance
 * (offset 0x400, 2 bytes/actor) into actors[U6_MAX_ACTORS]. */
bool u6actor_load_objlist(u6actor_t *actors, const uint8_t *objlist, int32_t len);

/* Attach SCHEDULE entries (offsets[256] u16, total u16, then total*5 bytes) to
 * each actor (sets sched pointer + nsched). Safe to call after _load_objlist. */
bool u6actor_load_schedule(u6actor_t *actors, const uint8_t *schedule, int32_t len);

/* Decode one 5-byte schedule entry. */
void u6sched_decode(const uint8_t *entry, u6sched_entry_t *out);

/* Index of the actor's schedule entry active at game `hour` (0..23), or -1 if
 * the actor has no schedule. (Picks the latest entry with hour <= the given
 * hour, wrapping past midnight; day-of-week is not yet considered.) */
int u6sched_active(const u6actor_t *a, int hour);

#endif
