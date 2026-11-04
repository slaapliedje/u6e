// SPDX-License-Identifier: GPL-2.0-or-later
/* sttune.c — the ST release's YM2149 music (docs/formats/st_music.md).
 *
 * The copied song, big-endian: [u8 npos][u8 nins][u8 npat][u8 0][u16 offset of the patterns' table]
 * [128 bytes of periods][nins x 10 instruments][npos x 3 positions](pad)[npat x u16 offsets][rows] */
#include "sttune.h"
#include <string.h>

#define RD16(p) ((uint16_t)(((p)[0] << 8) | (p)[1]))
#define RD32(p) (((uint32_t)(p)[0] << 24) | ((uint32_t)(p)[1] << 16) | ((uint32_t)(p)[2] << 8) | (p)[3])
#define HDR 6
#define MAX_PERIOD_NOTE 63

enum { ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN };

static int32_t scan(const uint8_t *b, int32_t n, int32_t from, const char *s, int len)
{
    for (int32_t i = from; i + len <= n; i++) if (!memcmp(b + i, s, (size_t)len)) return i;
    return -1;
}

int32_t st_song_find(const uint8_t *b, int32_t n, uint8_t *buf, int32_t cap)
{
    int32_t t = scan(b, n, 0, "\x4B\xF9\x00\xFF\x88\x00\x13\xFC\x00\x3F", 10);
    if (t < 0) return 0;
    int32_t c = scan(b, n, t, "\x4D\xFA", 2), s = scan(b, n, t, "\x70\x00\x10\x39", 4);
    if (c < 0 || s < 4) return 0;
    int32_t pt = scan(b, n, s, "\x43\xF9", 2);
    if (pt < 0) return 0;
    int32_t m = scan(b, n, pt, "\x23\xCA", 2), i = scan(b, n, t, "\xC0\xFC\x00\x0A", 4), pr = scan(b, n, t, "\xD0\x40\x41\xFA", 4);
    if (m < 0 || i < 4 || pr < 0 || m + 6 > n || pt + 6 > n || c + 4 > n || pr + 6 > n) return 0;
    int32_t voice = c + 2 + (int16_t)RD16(b + c + 2);
    int32_t delta = voice + 0x22 - (int32_t)RD32(b + m + 2);            /* file offset = absolute + delta */
    int32_t seq = (int32_t)RD32(b + s - 4) + delta, pats = (int32_t)RD32(b + pt + 2) + delta, ins = (int32_t)RD32(b + i - 4) + delta;
    int32_t per = pr + 4 + (int16_t)RD16(b + pr + 4);
    if (seq < 1 || seq >= n || pats < 0 || ins < 0 || per < 0 || per + 128 > n || ins >= seq) return 0;
    int npos = b[seq - 1], nins = (seq - 1 - ins) / 10, npat = 0;
    if (npos < 1 || nins < 1 || nins > 32 || seq + npos * 3 > n) return 0;
    for (int k = 0; k < npos * 3; k++) if (b[seq + k] >= npat) npat = b[seq + k] + 1;
    if (pats + npat * 4 > n) return 0;
    int32_t head = HDR + 128 + nins * 10 + npos * 3, tab = head + (head & 1), o = tab + npat * 2;
    if (o > cap) return 0;
    memset(buf, 0, (size_t)o);
    memcpy(buf + HDR, b + per, 128);
    memcpy(buf + HDR + 128, b + ins, (size_t)(nins * 10));
    memcpy(buf + HDR + 128 + nins * 10, b + seq, (size_t)(npos * 3));
    for (int k = 0; k < npat; k++) {                    /* each pattern, its rows: 64 steps' worth */
        int32_t a = (int32_t)RD32(b + pats + k * 4) + delta, start = o;
        for (int st = 0; st < 64; ) {
            if (a < 0 || a + 5 > n || o + 5 > cap) return 0;
            memcpy(buf + o, b + a, 5); st += b[a + 4] + 1; a += 5; o += 5;
        }
        buf[tab + k * 2] = (uint8_t)(start >> 8); buf[tab + k * 2 + 1] = (uint8_t)start;
    }
    buf[0] = (uint8_t)npos; buf[1] = (uint8_t)nins; buf[2] = (uint8_t)npat;
    buf[4] = (uint8_t)(tab >> 8); buf[5] = (uint8_t)tab;
    return o;
}

int st_init(st_player_t *p, const uint8_t *d, void (*wr)(int, int), int (*rd)(int))
{
    memset(p, 0, sizeof *p);
    if (!d || !d[0] || !d[1] || !d[2]) return 0;
    p->data = d; p->npos = d[0]; p->nins = d[1]; p->npat = d[2];
    p->periods = d + HDR; p->ins = d + HDR + 128; p->seq = p->ins + p->nins * 10;
    p->pats = d + RD16(d + 4);
    p->wr = wr; p->rd = rd; p->speed = 5;
    for (int c = 0; c < 3; c++) p->v[c].mask = (uint8_t)(9 << c);
    return 1;
}

static void load_position(st_player_t *p)
{
    p->step = 0;
    if (p->pos == p->npos) p->pos = 0;
    for (int c = 0; c < 3; c++) {
        int pat = p->seq[p->pos * 3 + c];
        st_voice_t *v = &p->v[c];
        v->row = p->data + RD16(p->pats + pat * 2);
        v->hold = v->row[4];
    }
}

void st_start(st_player_t *p, int tune)
{
    if (tune < 0 || tune >= ST_TUNES || !p->data) return;
    p->pos = (uint8_t)(tune * 4); p->count = 0; p->running = 1;
    load_position(p);
    for (int c = 0; c < 3; c++) { p->v[c].off = 0; p->v[c].sounding = 0; }
}

void st_stop(st_player_t *p)
{
    p->running = 0;
    p->wr(7, p->rd(7) | 0x3F);
    for (int c = 0; c < 3; c++) { p->wr(8 + c, 0); p->v[c].sounding = 0; p->v[c].off = 0; }
}

static uint16_t note_period(const st_player_t *p, int note)
{
    if (note > MAX_PERIOD_NOTE) note = MAX_PERIOD_NOTE;
    return RD16(p->periods + note * 2);
}

static void read_row(st_player_t *p, st_voice_t *v)
{
    const uint8_t *r = v->row;
    if (r[0]) {
        v->note = r[0]; v->period = note_period(p, r[0]);
        if (r[1]) {
            int k = (r[1] - 1) & 0x1F;
            if (k >= p->nins) k = p->nins - 1;
            const uint8_t *in = p->ins + k * 10;
            v->sounding = 1; v->ins = in; v->noise = in[3]; v->vol = 0; v->off = 0;
            if (in[9] & 1) v->off |= v->mask & 7;
            if (in[9] & 2) v->off |= v->mask & 0x38;
            v->env = ENV_ATTACK; v->peak = in[4];
            if (in[9] & 4) { v->hwenv = 1; v->shape = in[0]; v->rate = in[4]; v->vol = 0x80; }
            else {
                int sw = in[6] & 0x7F;
                v->hwenv = 0; v->vdelay = in[5]; v->vsweep = (uint8_t)((sw + 1) >> 1);
                v->vstep = (int16_t)((in[8] << 8) | in[7]); v->voff = 0;
            }
        }
    }
    v->fx = r[2]; v->par = r[3];
}

static void step_voice(st_player_t *p, st_voice_t *v)
{
    read_row(p, v);
    if (v->hold-- == 0) { v->row += 5; v->hold = v->row[4]; }
}

static void next_position(st_player_t *p) { p->pos++; load_position(p); }

static void run_effect(st_player_t *p, st_voice_t *v)
{
    int fx = v->fx;
    switch (fx) {
    case 0:
        if (v->par && p->count && p->count <= 5) {
            int add = (p->count == 1 || p->count == 4) ? v->par >> 4 : (p->count == 3) ? 0 : v->par & 15;
            v->period = note_period(p, v->note + add);
        }
        return;
    case 1: v->period = (uint16_t)(v->period - v->par); return;
    case 2: v->period = (uint16_t)(v->period + v->par); return;
    case 9: p->wr(6, (p->rd(6) & 0x1F) + v->par); return;
    case 8: case 0xA: case 0xB: case 0xC: case 0xD: case 0xF: v->fx = 0xFF; break;
    default: return;
    }
    switch (fx) {
    case 8: p->wr(6, v->par); break;
    case 0xB: p->pos = v->par; load_position(p); break;
    case 0xC: v->peak = v->par; if (v->env == ENV_SUSTAIN) v->vol = v->par; break;
    case 0xD: next_position(p); break;
    case 0xF: p->speed = v->par & 15; break;
    default: break;
    }
}

static void advance(st_voice_t *v)
{
    if (v->noise && --v->noise == 0) v->off &= 7;
    if (v->vdelay != 0xFF && (!v->vdelay || --v->vdelay == 0)) {
        v->voff = (int16_t)(v->voff + v->vstep);
        if (--v->vsweep == 0) {
            uint8_t sw = v->ins[6];
            if (sw & 0x80) v->vdelay = 0xFF;
            else if (sw) { v->vsweep = sw; v->vstep = (int16_t)-v->vstep; }
        }
    }
    if (v->hwenv) return;
    if (v->env == ENV_ATTACK) {
        v->vol = (uint8_t)(v->vol + v->ins[0]);
        if ((v->vol & 0x80) || (int8_t)v->peak <= (int8_t)v->vol) { v->vol = v->peak; v->env = ENV_DECAY; }
    } else if (v->env == ENV_DECAY) {
        v->vol = (uint8_t)(v->vol + v->ins[1]);
        if ((v->vol & 0x80) || (int8_t)v->ins[2] >= (int8_t)v->vol) { v->vol = v->ins[2]; v->env = ENV_SUSTAIN; }
    }
}

void st_tick(st_player_t *p)
{
    int mix = 0x3F;
    if (p->running) {
        if (++p->count == p->speed) {
            p->count = 0;
            for (int c = 0; c < 3; c++) step_voice(p, &p->v[c]);
            if (++p->step == 0x40) next_position(p);
        }
        for (int c = 0; c < 3; c++) run_effect(p, &p->v[c]);
        for (int c = 0; c < 3; c++) {
            st_voice_t *v = &p->v[c];
            if (!v->sounding) continue;
            advance(v);
            uint16_t t = (uint16_t)(v->period + v->voff);
            p->wr(c * 2, t & 0xFF); p->wr(c * 2 + 1, t >> 8);
            p->wr(8 + c, v->vol >> 3);
            p->wr(13, v->shape); p->wr(11, v->rate);
            mix &= ~v->off;
        }
    }
    p->wr(7, (p->rd(7) & 0xC0) | mix);
}

int st_tune_for(const char *base)
{
    static const struct { char name[10]; int8_t tune; } T[12] = {
        {"ULTIMA", 3}, {"BOOTUP", 0}, {"INTRO", 5}, {"CREATE", 2}, {"FOREST", 4}, {"HORNPIPE", 1},
        {"ENGAGE", 3}, {"STONES", 6}, {"DUNGEON", 3}, {"BRIT", 1}, {"GARGOYLE", 2}, {"END", 1} };
    for (int i = 0; i < 12; i++) {
        const char *a = base, *b = T[i].name;
        while (*a && *b && (*a & ~0x20) == *b) { a++; b++; }
        if (!*a && !*b) return T[i].tune;
    }
    return -1;
}
