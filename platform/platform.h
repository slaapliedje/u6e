// SPDX-License-Identifier: GPL-2.0-or-later
/* platform.h — hardware-agnostic services exposed to /engine.
 *
 * M0 scope: hardware DETECTION only (the platform_caps struct + platform_detect).
 * Memory, file I/O, input, and timing land in M1. The engine reads caps and
 * asks platform_* by INTENT; it never issues a TOS trap itself.
 *
 * See platform/CLAUDE.md for the subsystem rules.
 */
#ifndef U6E_PLATFORM_H
#define U6E_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

/* Video shifter class — mirrors the _VDO cookie's high word. /render branches
 * on this. */
typedef enum {
    VIDEO_ST     = 0,   /* original ST shifter: 16 colors from 512        */
    VIDEO_STE    = 1,   /* STE shifter: 16 colors from 4096               */
    VIDEO_TT     = 2,   /* TT shifter: 256 colors from 4096               */
    VIDEO_FALCON = 3,   /* VIDEL: 256 indexed or 16-bit truecolor         */
    VIDEO_UNKNOWN = -1
} video_class_t;

/* Machine type — derived from the _MCH cookie. */
typedef enum {
    MACHINE_ST = 0,
    MACHINE_STE,
    MACHINE_MEGASTE,
    MACHINE_TT,
    MACHINE_FALCON,
    MACHINE_UNKNOWN
} machine_t;

/* Capabilities the rest of the program reads instead of probing hardware.
 * Grows over later milestones (RAM pools, blitter, monitor type, …). */
typedef struct {
    machine_t     machine;
    video_class_t video;
    int           cpu;            /* 0/10/20/30/40/60  => 68000…68060      */
    bool          has_fpu;        /* we never depend on it; informational  */
    bool          has_blitter;    /* BLiTTER chip present (STE/Mega/TT; opt on Mega ST) */
    bool          has_cookiejar;  /* false on pre-1.06 ST => assume base ST */
    uint16_t      gemdos_version; /* raw Sversion(): major=lo, minor=hi byte */
    bool          has_mxalloc;    /* GEMDOS >= 0.19 (gates Mxalloc in M1)   */
} platform_caps_t;

/* Fill *caps via the cookie jar + GEMDOS version. Safe on a base ST with no
 * cookie jar (degrades to MACHINE_ST / VIDEO_ST / cpu 68000). */
void platform_detect(platform_caps_t *caps);

/* Raw cookie-jar lookup (Getcookie via the MiNT libc helper): 1 + *val if `tag` (a 4-char
 * FOURCC packed big-endian, e.g. 0x4E564449 'NVDI') is present, else 0. For diagnostics
 * (which system extensions are loaded: NVDI/EdDI/MiNT/MagX/CT60 ...) — engine logic keys off
 * platform_caps, not raw cookies. */
int  plat_cookie(uint32_t tag, uint32_t *val);

/* Human-readable names for the self-test / logging. */
const char *platform_machine_name(machine_t m);
const char *platform_video_name(video_class_t v);

#endif /* U6E_PLATFORM_H */
