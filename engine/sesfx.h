// SPDX-License-Identifier: GPL-2.0-or-later
/* sesfx.h — Savage Empire's sound effects (docs/rules/se_sound_effects.md).
 *
 * The effect engine of §3: an effect is a note borrowed from the music's MIDI channels 2-9 (0-based 1-8), with
 * 32 slots, a free and a used channel list, priorities, and the music's channel claiming (formats/se_music.md
 * §3.6), which sits between the song sequencer (engine/smf.c) and the music's output as a music_backend_t gate.
 * The PC speaker driver of §4.3 is here too (sespk_*), as a backend the effects can be sent to.
 * Pure C, integer only, ISR-safe: all output happens in sesfx_tick (the 60 Hz interrupt); the game's calls only
 * queue requests. Channels are 0-based as in the MIDI status byte. */
#ifndef U6E_SESFX_H
#define U6E_SESFX_H
#include <stdint.h>
#include "music.h"

/* one step of an effect (§3.1) */
typedef struct { uint8_t flags, prog1, key, vel; uint16_t dur; uint8_t glide, pad; } sesfx_desc_t;
#define SESFX_N 34
extern const sesfx_desc_t SESFX_GAME[SESFX_N];   /* GAME.EXE's table (DS:0x2366), §5 */
extern const sesfx_desc_t SESFX_SPARK;           /* SAVAGE.EXE's fireworks (DS:0x200e), §6 */

/* mo = the music's output; fo = where effects go (the same backend on MIDI, the speaker model on the PSG) */
void sesfx_init(const music_backend_t *mo, const music_backend_t *fo);
extern const music_backend_t sesfx_gate;          /* give this to smf_start: the music's events pass through it */
/* queued (safe from the main program while the interrupt runs) */
void sesfx_play(const sesfx_desc_t *d, int vol, int pan, int handle, int prio);
void sesfx_stop_handle(int handle);               /* §3.6 stop one (by handle), the channel handed back to the music */
void sesfx_stop_all(void);                        /* §3.6 stop-all */
void sesfx_tick(void);                            /* the 60 Hz tick, after the song's events (§3.4) */
int  sesfx_sounding(void);                        /* any effect slot in use */
void sesfx_forget(void);                          /* everything was silenced outside (song stop): clear the slots
                                                   * and the ownership without output; call with the interrupt off */

/* the PC speaker driver (§4.3), a music_backend_t for the effects; sespk_run advances it n PIT ticks (1/7680 s) */
int  sespk_load(const uint8_t *item2, int32_t len);   /* SAVAGE.TIM item 2: 0 = not usable */
extern const music_backend_t sespk_be;
void sespk_run(int ticks);
int  sespk_freq(void);                            /* what the speaker plays now: Hz, 0 = off */
#endif
