// SPDX-License-Identifier: GPL-2.0-or-later
/* platdma.h — STE/TT/Falcon 8-bit DMA sound: stream pre-rendered music PCM (M10).
 *
 * Real-time FM synthesis is far too slow on the 68000 (see `make sbench`), so the
 * host pre-renders each `.M` to signed-8-bit PCM (tools/m2pcm, U6PC format) and
 * this backend STREAMS that file to the machine's DMA sound hardware, which plays
 * it at ~0% CPU. A small ST-RAM ring buffer is looped by the DMA controller; the
 * main loop calls plat_dma_music_pump() to refill the just-played region from
 * disk, wrapping the file at its loop point. Plain ST (no DMA sound) reports
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
void plat_dma_music_pump(void);                    /* call from the main loop: refill  */
void plat_dma_music_stop(void);
int  plat_dma_music_playing(void);

#endif /* U6E_PLATDMA_H */
