// SPDX-License-Identifier: GPL-2.0-or-later
/* platmidi.h — MIDI OUT (M10 music, the MIDI-enabled path).
 *
 * Sends raw MIDI bytes out the Atari's MIDI OUT port (present on every ST/STE/
 * TT/Falcon). Used by the music sequencer's MIDI backend to drive an external
 * synth (e.g. a Roland MT-32) with U6's score. See docs/formats/music.md.
 */
#ifndef U6E_PLATMIDI_H
#define U6E_PLATMIDI_H

#include <stdint.h>

void plat_midi_init(void);                          /* prepare the MIDI port (no-op on TOS) */
void plat_midi_write(const uint8_t *buf, int len);  /* send `len` raw MIDI bytes            */
void plat_midi_all_off(void);                       /* panic: all-notes-off on every channel */

#endif /* U6E_PLATMIDI_H */
