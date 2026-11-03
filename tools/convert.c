// SPDX-License-Identifier: GPL-2.0-or-later
/* convert.c — CONVERT.TOS: builds the game's data on the Atari from the player's own DOS files, the
 * same packs the PC build makes (docs/design/first_boot_convert.md). It runs mksave, nuviesave and
 * mkworld in turn (linked in, their main()s renamed) on whichever game's files are in the folder.
 *
 *   CONVERT.TOS                 convert the files in the current folder, then wait for a key
 *   CONVERT.TOS D:\U6\GAME.PRG  (from the game, via shel_write) convert in that program's folder,
 *                               then start it again
 *   CONVERT.TOS -c              (from the game, via Pexec) convert here and return to the caller
 *
 * Output: SAVE.000, U6.PAK (every game's core pack has this name; the game knows which game it is from
 * the pack's magic) and, for Ultima VI, U6FE.PAK. U6.PAK is written as U6.TMP and renamed last, so a
 * reset part-way leaves no U6.PAK and the game asks to convert again. The tools' own messages go to
 * CONVERT.LOG; the screen shows a 40-column progress display.
 *
 * TOS calls used (named for verification):
 *   Bconout 3 (BIOS) — the screen, device 2 (console), with VT-52 codes, never redirected;
 *   Bconin 2 / Bconstat 1 (BIOS) — wait for a key;
 *   Dsetdrv 0x0E, Dsetpath 0x3B, Dfree 0x36, Fdelete 0x41, Frename 0x56, Malloc 0x48,
 *   Fdup 0x45 / Fforce 0x46 / Fclose 0x3E — put handle 1 back after CONVERT.LOG (GEMDOS);
 *   Supexec 38 (XBIOS) — _hz_200; AES appl_init 10, shel_write 121, appl_exit 19 (firstrun.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <osbind.h>
#include "firstrun.h"

int mksave_main(int, char **);
int nuviesave_main(int, char **);
int mkworld_main(int, char **);

/* ---- screen: BIOS console output, VT-52 positioning, 40 columns (ST low resolution) ---- */
static void put(const char *s) { while (*s) Bconout(2, (unsigned char)*s++); }
static void at(int row, int col) { Bconout(2, 27); Bconout(2, 'Y'); Bconout(2, 32 + row); Bconout(2, 32 + col); }
static void clear_eol(void) { Bconout(2, 27); Bconout(2, 'K'); }
static void put_num(long v, int width)
{
    char b[12]; int n = 0;
    if (v < 0) v = 0;
    do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    while (width-- > n) Bconout(2, ' ');
    while (n) Bconout(2, b[--n]);
}
static void put_time(long s)                  /* m:ss */
{
    put_num(s / 60, 0); Bconout(2, ':'); Bconout(2, (char)('0' + s % 60 / 10)); Bconout(2, (char)('0' + s % 10));
}
static void wait_key(void)
{
    while (Bconstat(2)) (void)Bconin(2);      /* drop keys pressed during the run */
    (void)Bconin(2);
}

/* ---- progress: each stage's share of the run, as its time in tenths of a second on an 8 MHz ST ---- */
typedef struct { const char *label; int weight; } stage_t;
static const stage_t STAGES_U6[] = {
    { "Save template", 79 }, { "Object lists", 4 }, { "Map and objects", 22 }, { "Tiles", 130 },
    { "Indoor map", 189 }, { "World tables", 79 }, { "Main menu", 19 }, { "Intro scenes", 95 },
    { "Scroll", 5 }, { "Intro: TV", 46 }, { "Intro: moongate", 26 }, { "Portraits", 16 },
    { "Intro text", 62 }, { "Gypsy", 70 }, { "Woods", 27 }, { "Storm", 54 }, { "Stones", 32 },
    { "Into the gate", 76 }, { "Gargoyles", 45 }, { "Vellum", 20 }, { "Font", 5 }, { "Logos", 60 },
    { "Ending", 61 }, { "Pointer", 1 }, { "Colour tables", 1 }, { "Portrait palettes", 627 },
    { "Books", 1 }, { 0, 0 } };
static const stage_t STAGES_SEMD[] = {                          /* Savage Empire and Martian Dreams, averaged */
    { "Save template", 37 }, { "Object lists", 5 }, { "Map and objects", 26 }, { "Tiles", 112 },
    { "Portraits", 164 }, { "Conversation art", 50 }, { "People", 1 }, { 0, 0 } };

static const stage_t *g_stages;
static int  g_total, g_done_w, g_cur = -1, g_finished;
static long g_stdout = -1;                     /* handle 1 before it was pointed at CONVERT.LOG */
static long g_t0, g_last_draw, g_est_s;
static const char *g_label = "";

static long elapsed_s(void) { return (fr_ticks() - g_t0) / 200; }

static void draw_status(void)
{
    long el = elapsed_s();
    int pct = g_total ? (int)((long)g_done_w * 100 / g_total) : 0;
    at(5, 1); put(g_label); clear_eol();
    at(7, 1); Bconout(2, '[');
    for (int i = 0; i < 30; i++) Bconout(2, i < pct * 30 / 100 ? '#' : '.');
    put("] "); put_num(pct, 3); Bconout(2, '%');
    at(9, 1); put("Elapsed "); put_time(el); clear_eol();
    at(10, 1);
    long left = -1;
    if (pct >= 10) left = el * (100 - pct) / pct;     /* from the run so far */
    else if (g_est_s > 0) left = g_est_s - el;        /* from the speed probe until then */
    if (left >= 0) { put("About "); put_time(left < 0 ? 0 : left); put(" left"); }
    clear_eol();
    g_last_draw = fr_ticks();
}

void cv_step(const char *label)
{
    if (g_cur >= 0) g_done_w += g_stages[g_cur].weight;
    g_cur = -1;
    for (int i = 0; g_stages[i].label; i++)
        if (!strcmp(g_stages[i].label, label)) { g_cur = i; break; }
    g_label = label;
    printf("== %s at %ld.%02ld s\n", label, (fr_ticks() - g_t0) / 200, (fr_ticks() - g_t0) % 200 / 2);
    fflush(stdout);
    draw_status();
}

void cv_tick(void) { if (fr_ticks() - g_last_draw >= 200) draw_status(); }

/* point handle 1 back where it was (Fforce 0x46): under MiNT a program started with shel_write
 * inherits our handles, and the game's console lines would go on into CONVERT.LOG */
static void restore_stdout(void)
{
    fflush(stdout);
    if (g_stdout >= 0) { Fforce(1, (short)g_stdout); Fclose((short)g_stdout); g_stdout = -1; }
}

/* the screen when something stopped the run (a tool failed or called exit()) */
static void on_exit_screen(void)
{
    restore_stdout();
    if (g_finished) return;
    Fdelete("U6.TMP");
    at(12, 1); put("Conversion stopped at:"); clear_eol();
    at(13, 1); put(g_label); clear_eol();
    at(15, 1); put("See CONVERT.LOG for why."); clear_eol();
    at(17, 1); put("Press a key."); clear_eol();
    wait_key();
}

static int fail(const char *a, const char *b)
{
    put("\r\n "); put(a); if (b) put(b); put("\r\n\r\n Press a key.\r\n");
    g_finished = 1; wait_key(); return 1;
}

/* change to the folder of path "D:\DIR\FILE" (Dsetdrv 0x0E, Dsetpath 0x3B) */
static void chdir_to_file(const char *path)
{
    char dir[128]; int cut = -1, n = (int)strlen(path);
    if (n >= (int)sizeof dir) return;
    for (int i = 0; i < n; i++) if (path[i] == '\\' || path[i] == ':') cut = i;
    if (cut < 0) return;
    if (path[1] == ':') Dsetdrv((path[0] & ~0x20) - 'A');
    if (path[cut] == ':') return;
    memcpy(dir, path, (size_t)cut); dir[cut] = 0;
    Dsetpath(dir[1] == ':' ? dir + 2 : dir);
}

int main(int argc, char **argv)
{
    const char *game_prg = NULL; int child = 0;
    if (argc > 1 && !strcmp(argv[1], "-c")) child = 1;
    else if (argc > 1) { game_prg = argv[1]; chdir_to_file(game_prg); }

    Bconout(2, 27); Bconout(2, 'E');                /* clear, home */
    Bconout(2, 27); Bconout(2, 'f');                /* cursor off */
    int g = fr_detect_game();
    if (g == FR_NONE)
        return fail("No DOS game files here. Copy", "\r\n your DOS Ultima VI, Savage Empire\r\n or Martian Dreams files to this\r\n folder first.");
    at(1, 1); put(fr_game_name(g)); put(": converting");
    at(2, 1); put("the DOS files. This is done once.");

    const char *miss[24];
    if (fr_missing(g, miss, 24)) {
        at(4, 1); put("These files are missing:\r\n");
        for (int i = 0; miss[i]; i++) { put("  "); put(miss[i]); put("\r\n"); }
        return fail("Copy them here and run again.", NULL);
    }

    /* disk space: what the outputs need, less what an earlier, interrupted run already wrote */
    _DISKINFO di;
    if (Dfree(&di, 0) == 0) {
        unsigned long cl = di.b_secsiz * di.b_clsiz;           /* bytes per cluster; no 32-bit overflow */
        long kb = (long)(cl >= 1024 ? di.b_free * (cl / 1024) : di.b_free / (1024 / (cl ? cl : 1)));
        static const char *const OLD[] = { "SAVE.000", "U6FE.PAK", "U6.TMP", 0 };
        for (int i = 0; OLD[i]; i++) {
            _DTA *o = Fgetdta(), dta; Fsetdta(&dta);
            if (Fsfirst(OLD[i], 0) == 0) kb += dta.dta_size / 1024;
            Fsetdta(o);
        }
        if (kb < fr_need_kb(g)) {
            at(4, 1); put("Not enough disk space: needs ");
            put_num(fr_need_kb(g), 0); put(" KB,"); at(5, 1); put("only "); put_num(kb, 0); put(" KB free.");
            return fail("", NULL);
        }
    }
    if ((long)Malloc(-1L) < 1100L * 1024L)
        return fail("Not enough memory: the conversion", "\r\n needs about 1.2 MB free.");

    atexit(on_exit_screen);
    g_stdout = Fdup(1);                             /* Fdup 0x45: keep handle 1 to put back */
    if (!freopen("CONVERT.LOG", "w", stdout)) return fail("Cannot write CONVERT.LOG.", NULL);
    setvbuf(stdout, NULL, _IOFBF, 4096);
    g_stages = g == FR_U6 ? STAGES_U6 : STAGES_SEMD;
    for (int i = 0; g_stages[i].label; i++) g_total += g_stages[i].weight;
    g_est_s = fr_estimate_s(g, fr_speed());
    g_t0 = fr_ticks();
    at(15, 1); put("Messages: CONVERT.LOG");

    if (g != FR_U6) setenv("MKWORLD_GAME", g == FR_SE ? "savage" : "martian", 1);
    int rc;
    cv_step("Save template");
    { char *a[] = { "mksave", ".", "SAVE.000", 0 }; rc = mksave_main(3, a); }
    if (rc) exit(rc);
    cv_step("Object lists");
    { char *a[] = { "nuviesave", "SAVE.000", "CV_OBB.TMP", "CV_OBL.TMP", "CV_INV.TMP", 0 }; rc = nuviesave_main(5, a); }
    if (rc) exit(rc);
    if (g == FR_U6) { char *a[] = { "mkworld", ".", "U6.TMP", "0", "0", "CV_OBB.TMP", "CV_OBL.TMP", "CV_INV.TMP", 0 }; rc = mkworld_main(8, a); }
    else            { char *a[] = { "mkworld", ".", "U6.TMP", "0", "0", "CV_OBL.TMP", 0 }; rc = mkworld_main(6, a); }
    if (rc) exit(rc);
    fflush(stdout);

    Fdelete("U6.PAK");
    if (Frename(0, "U6.TMP", "U6.PAK") != 0) { g_label = "Renaming U6.TMP"; exit(2); }
    Fdelete("CV_OBB.TMP"); Fdelete("CV_OBL.TMP"); Fdelete("CV_INV.TMP");
    g_finished = 1;
    g_done_w = g_total; g_label = "Done"; draw_status();
    printf("== done in %ld s\n", elapsed_s());
    restore_stdout();

    if (child) return 0;                            /* the game Pexec'd us and carries on booting */
    if (game_prg && fr_appl_init()) {               /* start the game again once we exit */
        char tail[2] = { 0, 0 };
        fr_shel_write(1, game_prg, tail);
        fr_appl_exit();
        at(12, 1); put("Starting the game...");
        return 0;
    }
    at(12, 1); put("Finished. Press a key.");
    wait_key();
    return 0;
}
