// SPDX-License-Identifier: GPL-2.0-or-later
/* sttune.h — the ST release's own YM2149 music: find it in the player's U.PRG, and play it
 * (docs/formats/st_music.md). Pure: the chip is reached through the two callbacks, which the
 * Atari side points at the registers and the host test at a log. Interrupt-safe: st_tick calls
 * nothing but them. */
#ifndef U6E_STTUNE_H
#define U6E_STTUNE_H

#include <stdint.h>

#define ST_TUNES 7
#define ST_SONG_CAP 12288                       /* the song in this release takes about 10.5 KB */

typedef struct {                                /* one voice's state */
    uint8_t  sounding, mask, off;               /* off = the enable bits it clears */
    uint8_t  hold, shape, fx, par, vol, peak, note, noise, rate, vdelay, vsweep, env, hwenv;
    int16_t  vstep, voff;
    uint16_t period;
    const uint8_t *row, *ins;
} st_voice_t;

typedef struct {
    const uint8_t *data;                        /* made by st_song_find: see sttune.c */
    uint8_t  npos, nins, npat, pos, step, count, speed, running;
    const uint8_t *periods, *ins, *seq;
    const uint8_t *pats;                        /* npat u16 offsets from `data` */
    st_voice_t v[3];
    void (*wr)(int reg, int val);
    int  (*rd)(int reg);
} st_player_t;

/* copy the song out of a program image into buf (cap >= ST_SONG_CAP): its size, or 0 if none */
int32_t st_song_find(const uint8_t *img, int32_t n, uint8_t *buf, int32_t cap);
/* ready a player on a copied song; 1 if it reads well */
int  st_init(st_player_t *p, const uint8_t *song, void (*wr)(int, int), int (*rd)(int));
void st_start(st_player_t *p, int tune);        /* tune 0..ST_TUNES-1 */
void st_stop(st_player_t *p);                   /* silence, and writes the chip */
void st_tick(st_player_t *p);                   /* one vertical blank's worth */
/* the tune for a song named as on DOS ("BRIT"), or -1 */
int  st_tune_for(const char *base);

#endif /* U6E_STTUNE_H */
