// SPDX-License-Identifier: GPL-2.0-or-later
/* platpsg.h — YM2149 PSG (3-voice square) music output (M10, plain-ST fallback).
 *
 * The base ST has no DMA sound, only the YM2149 PSG that every Atari carries: three
 * square-wave tone voices + noise + one envelope. The music manager's PSG backend
 * reduces U6's 9-voice OPL FM to these 3 voices (voice allocation lives in the
 * backend); this module is the raw hardware: it programs the PSG registers and owns
 * the note->tone-period table. Register access is direct (ISR-safe, no TOS), so the
 * Timer-driven music tick can call it. NOTE: the PSG register-select latch is shared
 * with TOS's floppy drive-select (port A); a foreground floppy access interrupted by
 * the music tick can glitch — a known ST hazard, negligible on hard-drive/CF setups.
 */
#ifndef U6E_PLATPSG_H
#define U6E_PLATPSG_H

void plat_psg_init(void);                          /* silence + set the mixer (noise off) */
void plat_psg_voice(int v, int midinote, int vol); /* voice 0..2 -> midinote, vol 0..15    */
void plat_psg_voice_off(int v);                    /* silence voice v                      */
void plat_psg_silence(void);                       /* all three voices off                 */

#endif /* U6E_PLATPSG_H */
