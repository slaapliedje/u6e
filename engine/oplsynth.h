// SPDX-License-Identifier: GPL-2.0-or-later
/* oplsynth.h — portable software OPL2 (YM3812) FM synthesizer for the M10
 * OPL2->DMA music backend.
 *
 * Renders a DECOMPRESSED U6 `.M` OPL command stream (docs/formats/music.md) to
 * signed-16-bit mono PCM, running the same 9-voice 2-operator FM model the DOS
 * AdLib driver targeted. Pure integer C — no FPU, no TOS, no libm (the log-sine
 * and exponent tables are precomputed constants). The platform layer feeds the
 * rendered PCM to the machine's 8-bit DMA sound (STE/TT) or Falcon codec.
 *
 * This is the on-target port of the host de-risk proof (tools/m2wav.c); the FM
 * signal chain is written from the public OPL2 math + our own spec, not
 * transcribed. It embeds its own `.M` command walker (control flow mirrors
 * engine/music.c — the two share the format, not a live sequencer instance).
 */
#ifndef U6E_OPLSYNTH_H
#define U6E_OPLSYNTH_H

#include <stdint.h>

/* Begin playing `stream` (len bytes, already LZW-decompressed) at output sample
 * rate `rate` Hz (e.g. an STE/TT/Falcon DMA rate). `oversample` runs the FM
 * phase path N x internally and averages down to cut aliasing (1 = off, cheapest
 * for the 68000; 2/4 = cleaner, for machines with headroom). */
void opl_start(const uint8_t *stream, int32_t len, int32_t rate, int oversample);

/* Render `n` mono samples into `buf` (signed 16-bit), advancing the 60 Hz
 * sequencer clock by the equivalent time. Fills silence once the song ends
 * (songs normally loop, so this is rare). */
void opl_render(int16_t *buf, int n);

void opl_stop(void);          /* silence + stop the sequencer */
int  opl_playing(void);

/* --- loop-structure introspection (host pre-renderer) ----------------------
 * A U6 `.M` song is an intro played once, then a body that repeats from a loop
 * point (opcode 0xE; or the top of the stream if the song defines none). As
 * rendering advances, these report where playback RETURNED to that loop point,
 * so the host pre-renderer (tools/m2pcm.c) can place a seamless DMA loop:
 *   opl_sample_pos()   - output samples rendered since opl_start().
 *   opl_wrap_count()   - number of times playback has returned to the loop point.
 *   opl_wrap_sample(n) - opl_sample_pos at the n-th wrap (1-based) = the first
 *                        sample of the (n+1)-th pass through the loop body, or
 *                        -1 if that wrap has not happened yet.
 *   opl_loop_point_sample() - opl_sample_pos when the active loop point (opcode
 *                        0xE) was last set; 0 if the song has none (loops from
 *                        the top). This is where the FIRST body begins. */
int32_t opl_sample_pos(void);
int32_t opl_wrap_count(void);
int32_t opl_wrap_sample(int n);
int32_t opl_loop_point_sample(void);

#endif /* U6E_OPLSYNTH_H */
