// SPDX-License-Identifier: GPL-2.0-or-later
/* platdma.h — STE/TT/Falcon 8-bit DMA sound: stream pre-rendered music PCM (M10).
 *
 * Real-time FM synthesis is far too slow on the 68000 (see `make sbench`), so the
 * host pre-renders each `.M` to signed-8-bit PCM (tools/m2pcm, U6PC format) and
 * this backend STREAMS that file to the machine's DMA sound hardware, which plays
 * it at ~0% CPU. A small ST-RAM ring buffer is looped by the DMA controller; EVERY
 * wait loop the program sits in (gameplay loop, front-end scenes, modal pages) calls
 * plat_dma_music_pump() to refill the just-played region from disk, wrapping the
 * file at its loop point — a loop that merely Vsync()s hears the 2.6 s ring repeat
 * (gamedemo's vsync_pump() bundles the two). Plain ST (no DMA sound) reports
 * unavailable and falls back to the PSG/MIDI backends.
 */
#ifndef U6E_PLATDMA_H
#define U6E_PLATDMA_H

#include <stdint.h>
#include "platform.h"

void plat_dma_init(const platform_caps_t *caps);  /* detect DMA sound + grab the ring */
int  plat_dma_available(void);                     /* 1 if 8-bit DMA sound is present  */

/* Start streaming a U6PC PCM file (build/music/NAME.PCM). Returns 1 on success. */
int  plat_dma_music_start(const char *path);
void plat_dma_music_pump(void);                    /* call from EVERY wait loop: refill */
void plat_dma_music_stop(void);
int  plat_dma_music_playing(void);
uint32_t plat_dma_music_elapsed(void);            /* frames played since the start, loops included */
int32_t  plat_dma_music_rate(void);               /* the playing song's rate (frames a second)      */

/* RESIDENT SONG CACHE (docs/design/memory_streaming.md "DMA music"): on a machine with RAM to
 * spare, hand the backend an arena (Fast RAM preferred — the DMA still plays from the ST-RAM
 * ring; the pump copies) and songs become resident as they are first heard: the first pass of a
 * song streams from disk as usual AND lands in the arena; from its first loop on — and on every
 * later replay — the ring is refilled by memcpy and the disk stays idle. Songs are placed in a
 * ring allocator (oldest evicted when full), so a 21 MB arena holds all of U6, an 8 MB one the
 * last few areas. bytes == 0 / not enough RAM = pure streaming (the stock 4 MB machines). */
void     plat_dma_music_cache_init(uint32_t bytes, int prefer_fast);
uint32_t plat_dma_music_cache_size(void);          /* arena bytes (0 = streaming only)   */
uint32_t plat_dma_music_cache_used(void);          /* bytes occupied by cached songs     */
int      plat_dma_music_cached(void);              /* 1 = the current song plays from RAM */

/* SOUND EFFECTS MIXED INTO THE MUSIC (docs/rules/se_sound_effects.md §7a): a bank of host-rendered effects (U6FX
 * format, tools/se2pcm) is loaded once, taken from the song arena's end when there is one (else a block of its own
 * if RAM allows), and an effect is ADDED into the ring just ahead of the play position, a few milliseconds at a time,
 * by plat_dma_fx_isr (call it from a steady interrupt, in supervisor mode, some hundreds of times a second). The ring
 * is filled seconds ahead, so the effect lands on the music already there; a stop fades the voice out over ~20 ms.
 * Effects sound only while the DMA plays (a song, or an ended song's looped silence). */
int  plat_dma_fx_load(const char *path);          /* 1 = the bank is in memory (already, or now)        */
int  plat_dma_fx_count(void);                     /* effects in the bank (0 = none loaded)              */
void plat_dma_fx_play(int n, int handle);         /* start effect n; a voice with the same handle is cut */
void plat_dma_fx_stop(int handle);                /* fade out the voice of handle (-1 = every voice)    */
void plat_dma_fx_song(int n);                     /* the effects' balance against song n's (-1: as rendered) */
void plat_dma_fx_isr(void);                       /* mix ahead: supervisor mode (an interrupt handler)  */
int  plat_dma_fx_sounding(void);

#endif /* U6E_PLATDMA_H */
