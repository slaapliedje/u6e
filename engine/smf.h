// SPDX-License-Identifier: GPL-2.0-or-later
/* smf.h — Savage Empire's MIDI-file sequencer (docs/formats/se_music.md).
 *
 * Plays one Standard MIDI File (format 1) with SE's 0xFE events (jump back, exit point, song cue)
 * on a 60 Hz clock, the way GAME.EXE's player schedules it (§3.7), through a music_backend_t.
 * Pure C, integer only, ISR-safe (no TOS, no allocation). */
#ifndef U6E_SMF_H
#define U6E_SMF_H
#include <stdint.h>
#include "music.h"

#define SMF_MAXTR 24
typedef struct {
    const uint8_t *d; int32_t len;
    const music_backend_t *be;
    int ntr, div;
    int32_t pos[SMF_MAXTR], end[SMF_MAXTR], wait[SMF_MAXTR];
    uint8_t run[SMF_MAXTR], live[SMF_MAXTR];
    uint32_t scale, acc;          /* 60 Hz interrupts per MIDI tick (16.16); the accumulator */
    int ending;                   /* the game asked for the song to end at its next exit point */
    int now;                      /* the last 0xFE type-3 number (the "now playing" cue) */
    int playing;
} smf_seq_t;

int  smf_start(smf_seq_t *s, const uint8_t *d, int32_t len, const music_backend_t *be);   /* 0 = not a usable file */
void smf_tick(smf_seq_t *s);      /* one 60 Hz interrupt */
void smf_stop(smf_seq_t *s);
#endif
