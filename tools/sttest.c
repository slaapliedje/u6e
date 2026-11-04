// SPDX-License-Identifier: GPL-2.0-or-later
/* sttest.c — host test of engine/sttune.c (docs/formats/st_music.md).
 *   sttest                     a small made-up song: no game data needed
 *   sttest U.PRG [trace.txt]   finds the song; with a log of the original ("reg val pc", hex, one write a
 *                              line) replays each stretch of it and compares every register write */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sttune.h"

static int g_fail;
static void check(const char *what, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) g_fail++; }

static uint8_t g_reg[16];
typedef struct { uint8_t r, v; } wr_t;
static wr_t *g_out; static long g_nout, g_capout;
static void wr(int r, int v) { g_reg[r] = (uint8_t)v; if (g_nout < g_capout) { g_out[g_nout].r = (uint8_t)r; g_out[g_nout].v = (uint8_t)v; g_nout++; } }
static int rd(int r) { return r == 7 ? (g_reg[7] & 0x3F) | 0xC0 : g_reg[r]; }

static uint8_t g_song[ST_SONG_CAP];

/* a song with one position: voice A plays note 10 with a 3-tick speed, the others rest; loops */
static void synthetic(void)
{
    uint8_t *d = g_song; memset(d, 0, sizeof g_song);
    int tab = 6 + 128 + 10 + 3; tab += tab & 1;
    d[0] = 1; d[1] = 1; d[2] = 2; d[4] = (uint8_t)(tab >> 8); d[5] = (uint8_t)tab;
    d[6 + 10 * 2] = 0x01; d[6 + 10 * 2 + 1] = 0x00;          /* note 10: period 256 */
    uint8_t in[10] = { 0x40, 0xFF, 0x30, 0, 0x7F, 0, 0, 0, 0, 1 };
    memcpy(d + 6 + 128, in, 10);
    d[6 + 128 + 10] = 1;                                     /* position 0: A = pattern 1, B = C = 0 */
    int o = tab + 4;
    d[tab + 1] = (uint8_t)o; d[o + 4] = 63;                  /* pattern 0: rest */
    o += 5; d[tab + 2] = (uint8_t)(o >> 8); d[tab + 3] = (uint8_t)o;
    d[o] = 10; d[o + 1] = 1; d[o + 2] = 0xF; d[o + 3] = 3;   /* note, set speed 3 */
    d[o + 9] = 62;                                           /* then a long hold */
    static wr_t w[4096]; g_out = w; g_capout = 4096; g_nout = 0;
    st_player_t p;
    check("song reads", st_init(&p, d, wr, rd));
    st_start(&p, 0);
    for (int i = 0; i < 4; i++) st_tick(&p);
    check("silent before the first step", g_nout == 4 && w[3].r == 7 && (w[3].v & 0x3F) == 0x3F);
    st_tick(&p);
    check("the note sounds on the 5th tick", g_reg[0] == 0 && g_reg[1] == 1 && (g_reg[7] & 1) == 0 && (g_reg[7] & 2));
    check("first attack: volume $40 >> 3", g_reg[8] == 8);
    check("speed effect taken", p.speed == 3);
    st_tick(&p);
    check("attack reaches the peak", g_reg[8] == 15);
    for (int i = 0; i < 120; i++) st_tick(&p);
    check("decay stops at sustain", g_reg[8] == 6);
    for (int i = 0; i < 100; i++) st_tick(&p);
    check("loops: the note starts again", p.running && p.pos == 0 && g_reg[8] > 6);
    st_stop(&p);
    check("stop mutes", (g_reg[7] & 0x3F) == 0x3F && g_reg[8] == 0);
    check("tune table", st_tune_for("BRIT") == 1 && st_tune_for("stones") == 6 && st_tune_for("x") < 0);
}

static long g_n; static wr_t *g_log; static uint32_t *g_pc;
static void load_log(const char *path)
{
    FILE *f = fopen(path, "r"); if (!f) { perror(path); exit(2); }
    long cap = 1 << 20; g_log = malloc((size_t)cap * sizeof *g_log); g_pc = malloc((size_t)cap * sizeof *g_pc);
    unsigned r, v, pc;
    while (fscanf(f, "%x %x %x", &r, &v, &pc) == 3) {
        if (g_n == cap) { cap *= 2; g_log = realloc(g_log, (size_t)cap * sizeof *g_log); g_pc = realloc(g_pc, (size_t)cap * sizeof *g_pc); }
        g_log[g_n].r = (uint8_t)r; g_log[g_n].v = (uint8_t)v; g_pc[g_n] = pc; g_n++;
    }
    fclose(f);
}

/* how many of the log's writes from `at` the tune reproduces in order */
static long match(const uint8_t *song, int tune, int speed, long at, int *ok_speed)
{
    static wr_t buf[64]; st_player_t p;
    st_init(&p, song, wr, rd); p.speed = (uint8_t)speed; st_start(&p, tune);
    memset(g_reg, 0, sizeof g_reg);
    long i = at;
    for (long t = 0; t < 2000000 && i < g_n; t++) {
        g_out = buf; g_capout = 64; g_nout = 0;
        st_tick(&p);
        for (long k = 0; k < g_nout; k++, i++) {
            if (i >= g_n || g_log[i].r != buf[k].r || g_log[i].v != buf[k].v) { *ok_speed = p.speed; return i - at; }
        }
    }
    *ok_speed = p.speed;
    return i - at;
}

static void replay(const uint8_t *song, const char *path)
{
    load_log(path);
    printf("log: %ld writes\n", g_n);
    long at = 0, whole = 0, other = 0; int stretches = 0, speed = 5;
    while (at < g_n) {
        /* a stretch starts after a write that isn't the player's tick pattern: try each tune here */
        long best = 0; int bt = -1, bs = speed, es = speed;
        for (int t = 0; t < ST_TUNES; t++) for (int s = 5; s <= 15; s++) {
            int e; long m = match(song, t, s, at, &e);
            if (m > best) { best = m; bt = t; bs = s; es = e; }
        }
        if (best < 64) {                                /* between tunes: only idle ticks and stops are allowed */
            if (!((g_log[at].r == 7 && (g_log[at].v & 0x3F) == 0x3F) || (g_log[at].r >= 8 && g_log[at].r <= 10 && g_log[at].v == 0))) {
                if (other++ < 8) printf("  at write %8ld: reg %x = %02x pc %x\n", at, g_log[at].r, g_log[at].v, (unsigned)g_pc[at]); }
            at++; continue; }
        printf("  at write %8ld: tune %d (speed %2d at start) matches %ld writes", at, bt, bs, best);
        at += best; whole += best; stretches++; speed = es;
        printf(at < g_n ? ", then the log has reg %x = %02x\n" : ", to the end of the log\n", at < g_n ? g_log[at].r : 0, at < g_n ? g_log[at].v : 0);
    }
    printf("%d stretches, %ld of %ld writes reproduced\n", stretches, whole, g_n);
    printf("%ld writes that are neither the tune, an idle tick nor a stop\n", other);
    check("every write is the tune, an idle tick or a stop", other == 0);
}

int main(int argc, char **argv)
{
    if (argc < 2) synthetic();
    else {
        FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 2; }
        static uint8_t img[1 << 20]; long n = (long)fread(img, 1, sizeof img, f); fclose(f);
        int32_t sz = st_song_find(img, (int32_t)n, g_song, ST_SONG_CAP);
        printf("song found: %d bytes\n", (int)sz);
        check("song found", sz > 0);
        if (sz && argc > 2) replay(g_song, argv[2]);
    }
    printf(g_fail ? "\nRESULT: %d FAILURE(S)\n" : "\nRESULT: ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
