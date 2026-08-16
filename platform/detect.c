// SPDX-License-Identifier: GPL-2.0-or-later
/* detect.c — hardware detection via the cookie jar + GEMDOS version.
 *
 * TOS/library calls used (named for verification, per project rules):
 *   - Getcookie(tag,&val)  : MiNT libc helper; walks the cookie jar (handles
 *                            supervisor access + the no-jar case). Returns
 *                            C_FOUND / C_NOTFOUND.
 *   - Sversion()           : GEMDOS opcode 0x30; GEMDOS version word.
 *
 * Cookie tags (mint/cookie.h): C__VDO, C__MCH, C__CPU, C__FPU.
 * Degrades safely on a base ST that predates the cookie jar.
 */
#include "platform.h"

#include <mint/cookie.h>
#include <osbind.h>

void platform_detect(platform_caps_t *caps)
{
    long val;

    caps->machine        = MACHINE_UNKNOWN;
    caps->video          = VIDEO_UNKNOWN;
    caps->cpu            = 0;            /* 68000 */
    caps->has_fpu        = false;
    caps->has_blitter    = false;
    caps->has_cookiejar  = false;

    /* GEMDOS version: major in low byte, minor in high byte.
     * Mxalloc needs GEMDOS >= 0.19, i.e. raw value >= 0x1900. */
    caps->gemdos_version = (uint16_t)Sversion();
    caps->has_mxalloc    = (caps->gemdos_version >= 0x1900);

    /* _VDO — video shifter class lives in the high word. */
    if (Getcookie(C__VDO, &val) == C_FOUND) {
        caps->has_cookiejar = true;
        caps->video = (video_class_t)((val >> 16) & 0xFFFFL);
    } else {
        /* No cookie jar (or no _VDO) => original ST shifter. */
        caps->video = VIDEO_ST;
    }

    /* _MCH — machine type in the high word; STE low word 0x10 == Mega STE. */
    if (Getcookie(C__MCH, &val) == C_FOUND) {
        caps->has_cookiejar = true;
        long hi = (val >> 16) & 0xFFFFL;
        long lo = val & 0xFFFFL;
        switch (hi) {
        case 0:  caps->machine = MACHINE_ST;  break;
        case 1:  caps->machine = (lo == 0x10) ? MACHINE_MEGASTE : MACHINE_STE; break;
        case 2:  caps->machine = MACHINE_TT;  break;
        case 3:  caps->machine = MACHINE_FALCON; break;
        default: caps->machine = MACHINE_UNKNOWN; break;
        }
    } else {
        caps->machine = MACHINE_ST; /* pre-jar machine: base ST */
    }

    /* _CPU — value is the model number: 0,10,20,30,40,60 => 68000…68060. */
    if (Getcookie(C__CPU, &val) == C_FOUND) {
        caps->has_cookiejar = true;
        caps->cpu = (int)val;
    }

    /* _FPU — 0 means none. We never depend on an FPU; this is informational. */
    if (Getcookie(C__FPU, &val) == C_FOUND) {
        caps->has_cookiejar = true;
        caps->has_fpu = (val != 0);
    }

    /* BLiTTER — the STE, Mega STE, TT and Falcon all ship the chip; a plain ST reports it only
     * if it's a Blitter-equipped Mega ST. Detect via Blitmode(-1) (XBIOS 64): passing -1 reads the
     * config without changing it; bit 1 of the result = "hardware present". Blitmode has existed
     * since TOS 1.02 (the Mega ST's ROM) and in EmuTOS, so it is safe on any machine that could
     * carry a Blitter; the pre-1.02 520/1040 ST has no Blitter anyway. Only /render's ST-shifter
     * backend uses this (TT/Falcon composite through their own backends). */
    switch (caps->machine) {
    case MACHINE_STE: case MACHINE_MEGASTE:
    case MACHINE_TT:  case MACHINE_FALCON:
        caps->has_blitter = true;                       /* always present on these */
        break;
    default:                                            /* ST: optional Mega ST Blitter */
        caps->has_blitter = ((Blitmode(-1) & 2) != 0);  /* XBIOS 64, bit1 = chip present */
        break;
    }
}

const char *platform_machine_name(machine_t m)
{
    switch (m) {
    case MACHINE_ST:      return "ST";
    case MACHINE_STE:     return "STE";
    case MACHINE_MEGASTE: return "Mega STE";
    case MACHINE_TT:      return "TT030";
    case MACHINE_FALCON:  return "Falcon030";
    default:              return "unknown";
    }
}

const char *platform_video_name(video_class_t v)
{
    switch (v) {
    case VIDEO_ST:     return "ST shifter (16/512)";
    case VIDEO_STE:    return "STE shifter (16/4096)";
    case VIDEO_TT:     return "TT shifter (256/4096)";
    case VIDEO_FALCON: return "VIDEL (256 / truecolor)";
    default:           return "unknown";
    }
}

int plat_cookie(uint32_t tag, uint32_t *val)
{
    long v = 0;
    if (Getcookie((long)tag, &v) != C_FOUND) return 0;      /* Getcookie: mint/cookie.h jar walk */
    if (val) *val = (uint32_t)v;
    return 1;
}
