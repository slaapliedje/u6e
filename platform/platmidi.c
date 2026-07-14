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
 */
#include "platmidi.h"

#include <osbind.h>

void plat_midi_init(void) { /* MIDI ACIA is set up by TOS; nothing to do */ }

void plat_midi_write(const uint8_t *buf, int len)
{
    if (len > 0) Midiws(len - 1, (const void *)buf);   /* XBIOS 12: sends len bytes */
}

/* All-notes-off + all-sound-off on every MIDI channel (sent on stop / song change). */
void plat_midi_all_off(void)
{
    for (int ch = 0; ch < 16; ch++) {
        uint8_t m[6] = { (uint8_t)(0xB0 | ch), 123, 0,     /* CC 123 all-notes-off */
                         (uint8_t)(0xB0 | ch), 120, 0 };   /* CC 120 all-sound-off */
        plat_midi_write(m, 6);
    }
}
