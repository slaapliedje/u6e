// SPDX-License-Identifier: GPL-2.0-or-later
/* platmidi.c — MIDI OUT via XBIOS (M10).
 *
 * TOS calls used (named for verification, per platform/CLAUDE.md):
 *   Midiws(cnt, ptr)  : XBIOS opcode 12 — write cnt+1 bytes from ptr to the MIDI
 *                       ACIA (68901/6850). Blocks until each byte is sent; MIDI
 *                       is 31250 baud (~3125 bytes/s), so a chord of note events
 *                       (a few dozen bytes) at 60 Hz is comfortably within budget.
 *
 * The MIDI port exists on every Atari (ST/STE/TT/Falcon) and TOS initialises the
 * ACIA at boot, so no setup is needed. This is the period-correct way to drive an
 * external MT-32/GM synth.
 *
 * INTERRUPT-SAFE PATH: the music tick runs from a Timer interrupt (plattime), which
 * must NOT call Midiws (XBIOS is not re-entrant). So the sequencer QUEUES bytes into
 * a ring and the ISR DRAINS them straight to the MIDI 6850 ACIA:
 *   $FFFFFC04  MIDI ACIA status  (bit 1 = TDRE, transmit data register empty)
 *   $FFFFFC06  MIDI ACIA data    (write a byte here once TDRE is set)
 * This is exactly what Midiws does internally, minus the trap — safe from an ISR.
 */
#include "platmidi.h"

#include <osbind.h>

#define MIDI_ACIA_CTRL  ((volatile uint8_t *)0xFFFFFC04UL)
#define MIDI_ACIA_DATA  ((volatile uint8_t *)0xFFFFFC06UL)
#define ACIA_TDRE       0x02                              /* transmit data register empty */

/* TX ring. Sized to hold a comfortable burst; written by the sequencer (queue),
 * read by the timer ISR (drain). Both happen at interrupt level OR while the timer
 * is stopped (song start/stop), never concurrently, so no locking is needed. */
#define MIDI_RING 512
static volatile uint8_t g_ring[MIDI_RING];
static volatile uint16_t g_head, g_tail;                  /* head=write, tail=read */

void plat_midi_init(void) { g_head = g_tail = 0; }        /* ACIA set up by TOS */

void plat_midi_write(const uint8_t *buf, int len)
{
    if (len > 0) Midiws(len - 1, (const void *)buf);   /* XBIOS 12: sends len bytes */
}

void plat_midi_queue(const uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++) {
        uint16_t nh = (uint16_t)((g_head + 1) % MIDI_RING);
        if (nh == g_tail) break;                          /* ring full: drop (should not happen) */
        g_ring[g_head] = buf[i];
        g_head = nh;
    }
}

/* Send up to `budget` queued bytes to the ACIA. Blocks briefly on TDRE per byte
 * (~32 us at 31250 baud); `budget` bounds the ISR time. 52 bytes ~= the per-60Hz-tick
 * MIDI bandwidth, so draining that much each tick keeps pace without overrunning. */
void plat_midi_drain(int budget)
{
    while (budget-- > 0 && g_tail != g_head) {
        while (!(*MIDI_ACIA_CTRL & ACIA_TDRE)) { }        /* wait for TX ready */
        *MIDI_ACIA_DATA = g_ring[g_tail];
        g_tail = (uint16_t)((g_tail + 1) % MIDI_RING);
    }
}

void plat_midi_flush(void) { plat_midi_drain(MIDI_RING); }  /* main context: send everything queued */

/* All-notes-off + all-sound-off on every MIDI channel — QUEUED so it is ISR-safe
 * (the sequencer's reset() calls this; flushed by the ISR or plat_midi_flush). */
void plat_midi_all_off(void)
{
    for (int ch = 0; ch < 16; ch++) {
        uint8_t m[6] = { (uint8_t)(0xB0 | ch), 123, 0,     /* CC 123 all-notes-off */
                         (uint8_t)(0xB0 | ch), 120, 0 };   /* CC 120 all-sound-off */
        plat_midi_queue(m, 6);
    }
}
