// SPDX-License-Identifier: GPL-2.0-or-later
/* music.h — portable U6 music sequencer (M10).
 *
 * Ticks a DECOMPRESSED U6 `.M` OPL2 command stream (docs/formats/music.md) on a
 * 60 Hz clock and drives a pluggable backend with note/program events. Pure C,
 * integer only (no FPU, no TOS) — the same sequencer feeds the MIDI-OUT, OPL2->DMA
 * and YM2149-PSG backends. The MIDI-OUT backend maps notes via the MT-32 table
 * from MIDI.DAT; other backends can ignore program() and synth the notes.
 */
#ifndef U6E_MUSIC_H
#define U6E_MUSIC_H

#include <stdint.h>

/* Sink the sequencer drives. `ch` is the target MIDI channel (0..15) for the
 * MIDI backend; a synth backend can treat it as a voice group. */
typedef struct {
    void (*program)(int ch, int prog);            /* timbre for a channel (MIDI program 0..127) */
    void (*note_on)(int ch, int note, int vel);   /* note 0..127, vel 1..127                    */
    void (*note_off)(int ch, int note);
    void (*reset)(void);                           /* silence everything (song change / stop)    */
} music_backend_t;

/* Per-instrument routing: index -> (MIDI channel 0..15, MIDI program 0..127). Built from
 * MIDI.DAT (values there are 1-based; convert to 0-based before filling this). */
typedef struct { uint8_t ch, prog; } music_map_t;

typedef struct {
    const uint8_t *d; int32_t len, pos;           /* decompressed OPL stream + read cursor */
    const music_backend_t *be;
    music_map_t map[16]; int nmap;
    int8_t  note[9];                               /* current MIDI note per OPL channel (-1 = none) */
    uint8_t mch[9], inst[9], vel[9];               /* routed MIDI channel / instrument idx / velocity */
    int32_t loop_pos;
    struct { int32_t ret, start; int reps; } stk[8]; int sp;
    int32_t delay;                                 /* 60 Hz ticks until the next command */
    int playing;
} music_seq_t;

/* Begin playing `opl` (len bytes, already LZW-decompressed) through `be`, routing via `map`
 * (nmap entries). Emits the program-changes up front. */
void music_seq_start(music_seq_t *m, const uint8_t *opl, int32_t len,
                     const music_map_t *map, int nmap, const music_backend_t *be);
void music_seq_tick(music_seq_t *m, int nticks);   /* advance `nticks` 60 Hz ticks */
void music_seq_stop(music_seq_t *m);

#endif /* U6E_MUSIC_H */
