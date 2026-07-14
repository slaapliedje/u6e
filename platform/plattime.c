// SPDX-License-Identifier: GPL-2.0-or-later
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

/* ---- MFP Timer-A periodic interrupt (for the steady music tick) --------------
 * The 68901 MFP timer is clocked at 2.4576 MHz; TACR mode 7 divides by 200 (=>
 * 12288 Hz), then the data register divides again: rate = 12288 / data. Timer A is
 * MFP interrupt channel 13 => in-service register ISRA ($FFFFFA0F) bit 5. The MFP
 * runs in software-EOI mode, so the handler clears that bit before returning. */
static void (* volatile g_timer_cb)(void);

static void __attribute__((interrupt_handler)) tima_isr(void)
{
    void (*cb)(void) = g_timer_cb;
    if (cb) cb();
    *(volatile uint8_t *)0xFFFFFA0FUL = (uint8_t)~0x20;   /* clear Timer A in-service (ISRA bit 5) */
}

int plat_timer_start(void (*cb)(void), int hz)
{
    if (hz < 48)  hz = 48;
    if (hz > 250) hz = 250;
    int data = (12288 + hz / 2) / hz;                     /* 12288 Hz / hz, rounded */
    if (data < 1)   data = 1;
    if (data > 255) data = 255;
    g_timer_cb = cb;
    Xbtimer(0, 7, data, tima_isr);   /* XBIOS 31: Timer A, control 7 (/200), our ISR */
    return 1;
}

void plat_timer_stop(void)
{
    Xbtimer(0, 0, 0, tima_isr);      /* control 0 = stop Timer A */
    g_timer_cb = 0;
}

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
