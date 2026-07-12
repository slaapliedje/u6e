/* plattime.h — timing services (M1).
 *
 * The engine keeps simulation on a monotonic millisecond-ish clock and presents
 * on the VBL (see engine/CLAUDE.md main loop). The real refresh rate varies by
 * machine/monitor (50/60/~71 Hz), so we MEASURE it at startup rather than
 * hardcoding 50 Hz.
 */
#ifndef U6E_PLATTIME_H
#define U6E_PLATTIME_H

#include <stdint.h>

void     plat_time_init(void);    /* measure the refresh rate */

uint32_t plat_ticks_200hz(void);  /* raw 200 Hz system counter (_hz_200)     */
uint32_t plat_ticks_ms(void);     /* milliseconds (derived: 200Hz tick * 5)  */
uint32_t plat_vbl_count(void);    /* raw VBL interrupt counter (_vbclock)    */
void     plat_wait_vbl(void);     /* block until the next VBL (Vsync)        */
int      plat_refresh_hz(void);   /* measured frames/sec (50/60/~71)         */

#endif /* U6E_PLATTIME_H */
