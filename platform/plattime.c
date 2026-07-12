/* plattime.c — 200Hz / VBL timing.
 *
 * TOS calls + sysvars used (named for verification):
 *   _hz_200  (0x4ba) : 200 Hz system timer counter (long).
 *   _vbclock (0x462) : VBL interrupt counter (long).
 *   Vsync()  (XBIOS 0x25) : wait for the next vertical blank.
 *   Supexec  (XBIOS 0x26) : read the protected sysvars in supervisor mode
 *                           (required under MiNT memory protection).
 */
#include "plattime.h"

#include <osbind.h>

static int g_refresh_hz = 50;   /* until measured */

static long read_hz200(void)  { return (long)*(volatile unsigned long *)0x4baL; }
static long read_vbclock(void){ return (long)*(volatile unsigned long *)0x462L; }

uint32_t plat_ticks_200hz(void) { return (uint32_t)Supexec(read_hz200); }
uint32_t plat_vbl_count(void)   { return (uint32_t)Supexec(read_vbclock); }

/* 200 Hz -> ms: each tick is 5 ms. Wraps with the 32-bit counter (~248 days). */
uint32_t plat_ticks_ms(void)    { return plat_ticks_200hz() * 5u; }

void plat_wait_vbl(void) { Vsync(); }

int plat_refresh_hz(void) { return g_refresh_hz; }

void plat_time_init(void)
{
    /* Count 200Hz ticks across N VBLs. Both clocks advance in EMULATED time,
     * so the ratio is valid even under Hatari fast-forward. N is large enough
     * that integer-tick quantization stays well under 1 Hz (at 60 Hz, 100
     * frames ~= 333 ticks => rounding error < 0.2 Hz).
     *   frames/sec = N * 200 / (ticks elapsed) */
    enum { N = 100 };
    uint32_t t0 = plat_ticks_200hz();
    for (int i = 0; i < N; i++) Vsync();
    uint32_t dt = plat_ticks_200hz() - t0;

    if (dt == 0u) { g_refresh_hz = 50; return; }
    g_refresh_hz = (int)((N * 200u + dt / 2u) / dt);   /* rounded */
}
