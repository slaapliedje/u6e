// SPDX-License-Identifier: GPL-2.0-or-later
/* firstrun.h — shared by the game and CONVERT.TOS for the first start (docs/design/first_boot_convert.md):
 * which game's DOS files are in the folder, the core files each conversion needs, a speed probe for the
 * time estimate, and the few AES calls that hand over from the game to the converter and back.
 *
 * TOS calls used (named for verification):
 *   Fsfirst 0x4E (GEMDOS) — does a file exist; Malloc 0x48 / Mfree 0x49 (GEMDOS) — the probe buffer;
 *   Supexec 38 (XBIOS) — read _hz_200 ($4BA) and the cookie jar (_p_cookies, $5A0), protected variables;
 *   AES (trap #2, d0 = 200): appl_init 10, appl_exit 19, shel_read 120, shel_write 121.
 *
 * shel_write(1, isgem, 0, cmd, tail) on a single-tasking AES (TOS 1.x-4.x) does not start anything at
 * once: the AES shell (GEMSHLIB.C sh_main) runs `cmd` after the calling program exits, instead of
 * returning to the desktop. That is what lets the game quit, free all its memory, and have the
 * converter start in its place; the converter does the same to start the game again. */
#ifndef U6E_FIRSTRUN_H
#define U6E_FIRSTRUN_H

#include <osbind.h>
#include <stdint.h>
#include <string.h>

enum { FR_NONE = 0, FR_U6, FR_SE, FR_MD };

/* the files a conversion cannot do without (mkworld/mksave stop without them); the rest of the DOS
 * set only adds front-end art and text, and mkworld goes without it */
static const char *const FR_CORE_COMMON[] = {
    "MAP", "CHUNKS", "LZOBJBLK", "LZDNGBLK", "MAPTILES.VGA", "OBJTILES.VGA", "TILEINDX.VGA",
    "MASKTYPE.VGA", "TILEFLAG", "BASETILE", "ANIMDATA", "SCHEDULE", 0 };
static const char *const FR_CORE_U6[] = { "U6PAL", "LOOK.LZD", "U6.CH", "PAPER.BMP", "PALETTES.INT", 0 };
static const char *const FR_CORE_SE[] = { "SEPAL", "LOOK.LZC", "SAVAGE.FNT", 0 };
static const char *const FR_CORE_MD[] = { "MDPAL", "LOOK.LZC", "FONTS.LZC", 0 };

static inline int fr_exists(const char *name)
{
    _DTA *old = Fgetdta(), dta;                 /* Fgetdta 0x2F / Fsetdta 0x1A: keep the caller's DTA */
    Fsetdta(&dta);
    int ok = Fsfirst(name, 0) == 0;
    Fsetdta(old);
    return ok;
}

/* the ST release instead, its files in a folder named ST (docs/formats/st_data.md) */
static const char *const FR_CORE_ST[] = { "ST\\MAP", "ST\\CHUNKS", "ST\\MAPTILES.EGA", "ST\\OBJTILES.EGA", "ST\\TILEINDX.EGA", "ST\\MASKTYPE.EGA", "ST\\TILEFLAG",
    "ST\\BASETILE", "ST\\ANIMDATA", "ST\\SCHEDULE", "ST\\U.PRG", "ST\\GAME.PRG", "ST\\END.PRG", "ST\\SAVEGAME\\OBJLIST", "ST\\LOOK.LZD", "ST\\U6.CH", "ST\\PAPER.BMP", 0 };
static inline int fr_is_st(void) { return !fr_exists("MAPTILES.VGA") && fr_exists("ST\\MAPTILES.EGA"); }

/* which game's DOS files are here: the game's own .EXE tells Savage Empire and Martian Dreams
 * apart (mksave sniffs the same way); Ultima VI by its map plus U6.CH */
static inline int fr_detect_game(void)
{
    if (fr_exists("SAVAGE.EXE"))  return FR_SE;
    if (fr_exists("MARTIAN.EXE")) return FR_MD;
    if (fr_exists("MAP") && fr_exists("U6.CH")) return FR_U6;
    if (fr_is_st()) return FR_U6;
    return FR_NONE;
}

static inline const char *fr_game_name(int g)
{
    return g == FR_SE ? "Savage Empire" : g == FR_MD ? "Martian Dreams" : "Ultima VI";
}

/* the missing core files, as a NULL-terminated list in out[] (up to cap-1 entries); returns the count */
static inline int fr_missing(int g, const char **out, int cap)
{
    if (fr_is_st()) { int m = 0; for (int i = 0; FR_CORE_ST[i]; i++) if (!fr_exists(FR_CORE_ST[i]) && m < cap - 1) out[m++] = FR_CORE_ST[i]; out[m] = 0; return m; }
    const char *const *lists[2] = { FR_CORE_COMMON, g == FR_SE ? FR_CORE_SE : g == FR_MD ? FR_CORE_MD : FR_CORE_U6 };
    int n = 0;
    for (int l = 0; l < 2; l++)
        for (int i = 0; lists[l][i]; i++)
            if (!fr_exists(lists[l][i]) && n < cap - 1) out[n++] = lists[l][i];
    out[n] = 0;
    return n;
}

/* free disk space the outputs need, in KB: the packs + SAVE.000 + the temporary object files */
static inline long fr_need_kb(int g) { return g == FR_U6 ? (fr_is_st() ? 6500 : 3500) : g == FR_SE ? 1500 : 1700; }

static long fr_read_hz200(void) { return *(volatile long *)0x4BAL; }
static inline long fr_ticks(void) { return Supexec(fr_read_hz200); }

/* Speed probe: byte reads spread over 16 KB (past a 68030's 256-byte caches, so it costs what the
 * converter's table and buffer work costs) for 40 ticks of the 200 Hz timer. It runs in supervisor
 * mode (Supexec) so each timer read is a plain memory read, not a trap. Returns 16-read passes per
 * tick, or 0 if the buffer can't be had. About 0.2 s. */
static volatile uint8_t *fr_probe_buf;
static long fr_speed_super(void)
{
    volatile long *hz = (volatile long *)0x4BAL;
    volatile uint8_t *buf = fr_probe_buf;
    long t = *hz; while (*hz == t) ;                        /* start on a tick edge */
    long t0 = *hz, passes = 0; unsigned idx = 0, sum = 0;
    while (*hz - t0 < 40) {
        for (int k = 0; k < 16; k++) { sum += buf[idx & 16383]; idx += 61; }
        passes++;
    }
    buf[0] = (uint8_t)sum;
    return passes / 40;
}
static inline long fr_speed(void)
{
    fr_probe_buf = (volatile uint8_t *)Malloc(16384);
    if ((long)fr_probe_buf <= 0) return 0;
    for (long i = 0; i < 16384; i++) fr_probe_buf[i] = (uint8_t)i;
    long v = Supexec(fr_speed_super);
    Mfree((void *)fr_probe_buf);
    return v;
}

/* the machine family from the _MCH cookie (cookie jar pointer _p_cookies at $5A0; no jar = an ST):
 * 0 ST/STE/Mega STE, 2 TT, 3 Falcon */
static long fr_read_mch(void)
{
    const long *jar = *(const long *const *)0x5A0L;
    for (; jar && jar[0]; jar += 2)
        if (jar[0] == 0x5F4D4348L) return jar[1] >> 16;      /* '_MCH' */
    return 0;
}
static inline int fr_machine(void) { long m = Supexec(fr_read_mch); return m == 2 || m == 3 ? (int)m : 0; }

/* The expected conversion time in seconds on this machine: the time measured on the stock machine of
 * its family (8 MHz ST, 32 MHz TT, 16 MHz Falcon), scaled by fr_speed() against that machine's reading
 * (the 68000 build on an ST, the 68020 build on a TT or Falcon), so a faster CPU card shortens it.
 * The probe alone can't stand in for the measured times: how much a 68030's caches help it depends on
 * the code's alignment, by up to 2x. Said as "about N minutes", so rough is fine. */
static inline long fr_estimate_s(int g, long speed)
{
    static const long stock_s[3][4] = {                     /* [family][game], measured (first_boot_convert.md §2a) */
        { 0, 185, 37, 41 },                                 /* ST */
        { 0, 17, 4, 5 },                                    /* TT */
        { 0, 36, 9, 10 } };                                  /* Falcon */
    static const long stock_speed[3] = { 29, 186, 120 };    /* fr_speed() on each stock machine */
    int m = fr_machine(), f = m == 2 ? 1 : m == 3 ? 2 : 0;
    long s = stock_s[f][g < FR_U6 || g > FR_MD ? FR_U6 : g];
    if (speed <= 0) return s;
    if (speed < stock_speed[f] / 2) speed = stock_speed[f] / 2;      /* a probe gone wrong: stay near stock */
    return s * stock_speed[f] / speed;
}

/* ---- minimal AES binding (trap #2, d0 = 200), so the 68000 builds need no GEM library ---- */
static short fr_aes_control[5], fr_aes_global[15], fr_aes_intin[16], fr_aes_intout[7];
static const void *fr_aes_addrin[2];
static void *fr_aes_addrout[1];
static inline int fr_aes(int op, int nin, int nout, int nain)
{
    static void *pb[6] = { fr_aes_control, fr_aes_global, fr_aes_intin, fr_aes_intout,
                           (void *)fr_aes_addrin, fr_aes_addrout };
    fr_aes_control[0] = (short)op; fr_aes_control[1] = (short)nin; fr_aes_control[2] = (short)nout;
    fr_aes_control[3] = (short)nain; fr_aes_control[4] = 0;
    register void *a __asm__("d1") = pb;
    __asm__ volatile("move.w #200,%%d0\n\ttrap #2" : : "r"(a) : "d0", "d2", "a0", "a1", "a2", "memory", "cc");
    return fr_aes_intout[0];
}

/* appl_init (AES 10): 1 if an AES answered (global[0] = its version), else 0 (AUTO folder, no GEM) */
static inline int fr_appl_init(void)
{
    fr_aes_global[0] = 0; fr_aes_intout[0] = -1;
    int id = fr_aes(10, 0, 1, 0);
    return id >= 0 && fr_aes_global[0] != 0;
}
static inline void fr_appl_exit(void) { fr_aes(19, 0, 1, 0); }

/* shel_read (AES 120): the command the shell started us with (the program's path) */
static inline int fr_shel_read(char *cmd, char *tail)
{
    fr_aes_addrin[0] = cmd; fr_aes_addrin[1] = tail;
    return fr_aes(120, 0, 1, 2);
}

/* shel_write (AES 121) with doex = 1: run `cmd` (a full path) once this program exits.
 * tail is a GEMDOS command tail: [length byte][text]. */
static inline int fr_shel_write(int isgem, const char *cmd, const char *tail)
{
    fr_aes_intin[0] = 1; fr_aes_intin[1] = (short)isgem; fr_aes_intin[2] = 0;
    fr_aes_addrin[0] = cmd; fr_aes_addrin[1] = tail;
    return fr_aes(121, 3, 1, 2);
}

/* "D:\PATH\" of the current directory into out (Dgetdrv 0x19, Dgetpath 0x47) */
static inline void fr_cwd(char *out, int cap)
{
    char p[128];
    int d = Dgetdrv();
    if (Dgetpath(p, 0) < 0) p[0] = 0;
    int n = 0;
    out[n++] = (char)('A' + d); out[n++] = ':';
    for (int i = 0; p[i] && n < cap - 2; i++) out[n++] = p[i];
    if (n == 2 || out[n-1] != '\\') out[n++] = '\\';
    out[n] = 0;
}

#endif /* U6E_FIRSTRUN_H */
