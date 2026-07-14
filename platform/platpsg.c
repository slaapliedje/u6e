// SPDX-License-Identifier: GPL-2.0-or-later
/* platpsg.c — YM2149 PSG register programming (M10). See platpsg.h.
 *
 * Hardware (ST/STE/TT/Falcon, byte registers):
 *   $FFFF8800  register-select (write reg#) / read selected reg
 *   $FFFF8802  register-write  (write data to the selected reg)
 * PSG registers used: R0/1 R2/3 R4/5 = 12-bit tone period for voices A/B/C; R7 =
 * mixer (bits0-2 tone-disable A/B/C, bits3-5 noise-disable, bits6-7 port dir); R8/9/10
 * = voice A/B/C volume (0-15, bit4 would select the envelope — we don't). We keep noise
 * disabled and port-direction bits set (0xC0: ports A/B are always outputs on every ST,
 * driving floppy-select / printer — must not clear them). All access is direct register
 * writes, safe from the music Timer interrupt.
 */
#include "platpsg.h"
#include <stdint.h>
#include <osbind.h>

#define PSG_SEL  ((volatile uint8_t *)0xFFFF8800UL)
#define PSG_WR   ((volatile uint8_t *)0xFFFF8802UL)
#define PORT_DIR 0xC0        /* R7 bits 6-7: ports A/B output (invariant on the ST)   */

/* note -> 12-bit tone period: TP = 125000 / freq (2 MHz PSG clock / 16), clamped
 * to [1,4095]. Precomputed on the host (no FPU on target). */
static const uint16_t PSG_PERIOD[128] = {
    4095,4095,4095,4095,4095,4095,4095,4095,4095,4095,4095,4095,
    4095,4095,4095,4095,4095,4095,4095,4095,4095,4095,4095,4050,
    3822,3608,3405,3214,3034,2863,2703,2551,2408,2273,2145,2025,
    1911,1804,1703,1607,1517,1432,1351,1276,1204,1136,1073,1012,
     956, 902, 851, 804, 758, 716, 676, 638, 602, 568, 536, 506,
     478, 451, 426, 402, 379, 358, 338, 319, 301, 284, 268, 253,
     239, 225, 213, 201, 190, 179, 169, 159, 150, 142, 134, 127,
     119, 113, 106, 100,  95,  89,  84,  80,  75,  71,  67,  63,
      60,  56,  53,  50,  47,  45,  42,  40,  38,  36,  34,  32,
      30,  28,  27,  25,  24,  22,  21,  20,  19,  18,  17,  16,
      15,  14,  13,  13,  12,  11,  11,  10,
};

static uint8_t g_tone_off = 0x07;   /* R7 tone-disable bits: 1 = voice muted        */

/* ST I/O ($FF8xxx) is SUPERVISOR-ONLY — a user-mode write bus-errors. plat_psg_voice /
 * plat_psg_voice_off are called only from the music Timer interrupt (already supervisor),
 * so they write the registers directly; plat_psg_init / plat_psg_silence run in main (user)
 * context and go through Supexec (XBIOS 38). */
static void psg_write(int reg, int val){ *PSG_SEL = (uint8_t)reg; *PSG_WR = (uint8_t)val; }

/* R7 = port dir + noise off (bits3-5) + current tone-disable mask */
static void psg_mixer(void){ psg_write(7, PORT_DIR | 0x38 | g_tone_off); }

static long psg_do_init(void){
    g_tone_off = 0x07;                       /* all voices muted                    */
    psg_write(8,0); psg_write(9,0); psg_write(10,0);   /* zero the three volumes    */
    psg_mixer();
    return 0;
}
void plat_psg_init(void){ (void)Supexec(psg_do_init); }

void plat_psg_voice(int v, int midinote, int vol){
    if(v<0 || v>2) return;
    if(midinote<0) midinote=0;
    if(midinote>127) midinote=127;
    if(vol<0) vol=0;
    if(vol>15) vol=15;
    int p = PSG_PERIOD[midinote];
    psg_write(v*2,   p & 0xFF);              /* tone period fine                    */
    psg_write(v*2+1, (p>>8) & 0x0F);         /* tone period coarse (4 bits)         */
    psg_write(8+v,   vol);                   /* volume (fixed level, no envelope)   */
    g_tone_off &= ~(1<<v);                   /* enable this voice's tone            */
    psg_mixer();
}

void plat_psg_voice_off(int v){
    if(v<0 || v>2) return;
    psg_write(8+v, 0);                       /* volume 0                            */
    g_tone_off |= (1<<v);                    /* mute the tone                       */
    psg_mixer();
}

static long psg_do_silence(void){
    psg_write(8,0); psg_write(9,0); psg_write(10,0);
    g_tone_off = 0x07;
    psg_mixer();
    return 0;
}
void plat_psg_silence(void){ (void)Supexec(psg_do_silence); }
