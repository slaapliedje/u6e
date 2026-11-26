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
 * context and go through Supexec (XBIOS 38).
 *
 * Voice C shared with the sound effects (2026-11-25, notes heard hanging on real machines): U6's effects
 * play from the main program on voice C (registers 4, 5, 10 and voice C's bits of 7) while the music's
 * interrupt writes all three voices. Before, the effect read voice C, played over the music's writes and
 * then put back what it had read, stale by then, so a note could stay at the effect's start pitch until
 * the tune next set it; and an interrupt landing between an effect's select and write sent the effect's
 * value to the music's register. Now: plat_psg_fx_begin() gives voice C to the effect; the music's
 * writes to it go to a shadow only; plat_psg_fx_end() writes the shadow back. The effect's own writes
 * run with interrupts masked (IPL 7), so a select and its write are never split. */
static uint8_t g_shadow[16];            /* the music's last value of each register */
static volatile uint8_t g_fx_lock;      /* voice C belongs to an effect            */
static uint8_t g_fx_r7c = 0x24;         /* the effect's voice C bits of R7 (bit 2 tone off, bit 5 noise off) */
static void psg_write(int reg, int val){
    g_shadow[reg & 15] = (uint8_t)val;
    if (g_fx_lock) {
        if (reg == 4 || reg == 5 || reg == 10) return;
        if (reg == 7) val = (val & ~0x24) | g_fx_r7c;
    }
    *PSG_SEL = (uint8_t)reg; *PSG_WR = (uint8_t)val;
}
void plat_psg_write(int reg, int val){ psg_write(reg, val); }
int  plat_psg_read(int reg){
    if (g_fx_lock && (reg == 4 || reg == 5 || reg == 10 || reg == 7)) return g_shadow[reg];
    *PSG_SEL = (uint8_t)reg; return *PSG_SEL;
}
static uint16_t ipl7(void){ uint16_t sr; __asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) :: "memory"); return sr; }
static void ipl_back(uint16_t sr){ __asm__ volatile("move.w %0,%%sr" :: "d"(sr) : "memory"); }
/* (supervisor) the effect's side: begin, write, end */
void plat_psg_fx_begin(void){
    uint16_t sr = ipl7();
    if (!g_fx_lock) {
        *PSG_SEL = 7; uint8_t r7 = *PSG_SEL;            /* what the chip has now (the music's or ours) */
        g_shadow[7] = r7;
        *PSG_SEL = 4; g_shadow[4] = *PSG_SEL; *PSG_SEL = 5; g_shadow[5] = *PSG_SEL; *PSG_SEL = 10; g_shadow[10] = *PSG_SEL;
        g_fx_r7c = 0x20;                                /* voice C: tone on, noise off */
        g_fx_lock = 1;
        *PSG_SEL = 7; *PSG_WR = (uint8_t)((r7 & ~0x24) | g_fx_r7c);
    }
    ipl_back(sr);
}
void plat_psg_fx_write(int reg, int val){
    uint16_t sr = ipl7();
    if (reg == 7) { g_fx_r7c = (uint8_t)(val & 0x24); val = (g_shadow[7] & ~0x24) | g_fx_r7c; }
    *PSG_SEL = (uint8_t)reg; *PSG_WR = (uint8_t)val;
    ipl_back(sr);
}
int plat_psg_fx_read(int reg){
    uint16_t sr = ipl7(); *PSG_SEL = (uint8_t)reg; int v = *PSG_SEL; ipl_back(sr); return v;
}
void plat_psg_fx_end(void){
    uint16_t sr = ipl7();
    if (g_fx_lock) {
        g_fx_lock = 0;
        *PSG_SEL = 4; *PSG_WR = g_shadow[4]; *PSG_SEL = 5; *PSG_WR = g_shadow[5];
        *PSG_SEL = 10; *PSG_WR = g_shadow[10]; *PSG_SEL = 7; *PSG_WR = g_shadow[7];
    }
    ipl_back(sr);
}

/* R7 = port dir + noise off (bits3-5) + current tone-disable mask */
static void psg_mixer(void){ psg_write(7, PORT_DIR | 0x38 | g_tone_off); }

static long psg_do_init(void){     /* (main program, under Supexec: masked so the music's interrupt can't split a write) */
    uint16_t sr = ipl7();
    g_fx_lock = 0;
    g_tone_off = 0x07;                       /* all voices muted                    */
    psg_write(8,0); psg_write(9,0); psg_write(10,0);   /* zero the three volumes    */
    psg_mixer();
    ipl_back(sr);
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

/* voice v at a raw tone period (1-4095; 2 MHz / 16 / period Hz), or off for period 0 (interrupt level) */
void plat_psg_tone(int v, int period, int vol){
    if(v<0 || v>2) return;
    if(period<=0){ plat_psg_voice_off(v); return; }
    if(period>4095) period=4095;
    psg_write(v*2, period & 0xFF); psg_write(v*2+1, (period>>8) & 0x0F);
    psg_write(8+v, vol<0?0:vol>15?15:vol);
    if(g_tone_off & (1<<v)){ g_tone_off &= ~(1<<v); psg_mixer(); }
}

/* voice v's volume only (0-15), pitch and mixer untouched (interrupt level: the music's envelopes) */
void plat_psg_volume(int v, int vol){
    if(v<0 || v>2) return;
    psg_write(8+v, vol<0?0:vol>15?15:vol);
}

void plat_psg_voice_off(int v){
    if(v<0 || v>2) return;
    psg_write(8+v, 0);                       /* volume 0                            */
    g_tone_off |= (1<<v);                    /* mute the tone                       */
    psg_mixer();
}

static long psg_do_silence(void){
    uint16_t sr = ipl7();
    g_fx_lock = 0;
    psg_write(8,0); psg_write(9,0); psg_write(10,0);
    g_tone_off = 0x07;
    psg_mixer();
    ipl_back(sr);
    return 0;
}
void plat_psg_silence(void){ (void)Supexec(psg_do_silence); }
