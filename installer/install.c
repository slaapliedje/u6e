// SPDX-License-Identifier: GPL-2.0-or-later
/* install.c — floppy -> hard-drive installer for the Atari port.
 *
 * A tiny text-mode TOS program (compile to INSTALL.TOS) that reads a manifest
 * (INSTALL.INF) sitting next to it on disk 1 and copies the game onto a
 * hard-drive/CF folder, reassembling any pack files that were split across
 * several floppies (the full U6.PAK is 2.4 MB — bigger than a 1.44 MB floppy).
 *
 * It is GENERIC: it knows nothing about which game/target it is installing —
 * the disk-building script (scripts/make-st-disks.sh) writes the manifest that
 * drives it. This file ships no game data; it is pure installer logic.
 *
 * TOS/GEMDOS calls used (name + opcode, TRAP #1), for verification:
 *   Cconws 0x09, Cnecin 0x08, Cconrs 0x0A, Dgetdrv 0x19, Dcreate 0x39,
 *   Fopen 0x3D, Fcreate 0x3C, Fread 0x3F, Fwrite 0x40, Fclose 0x3E.
 * No AES/VDI, no XBIOS, no supervisor mode — plain GEMDOS, base-68000 safe.
 *
 * Manifest (INSTALL.INF) grammar, one directive per line (';'/'#' = comment):
 *   TITLE <free text>            banner shown to the user
 *   DEST  <path>                 default install folder, e.g.  C:\U6
 *   AUTO  1                      non-interactive: install to DEST without asking
 *   OUT   <destname>             begin an output file in DEST
 *   SRC   <disk#> <srcname>      a source part on floppy <disk#>, appended in
 *                                order to the current OUT (one SRC = whole file,
 *                                several SRC = a split file reassembled)
 *   END                          stop
 *
 * A command-line argument (INSTALL.TTP "C:\GAMES\U6") also sets the install
 * folder and runs non-interactively.
 */
#include <osbind.h>
#include <string.h>

/* GEMDOS Fopen mode / Fseek whence */
#define RD 0
#define WR 1

/* DMA-safe copy buffer: static => program BSS => ST-RAM (chip-accessible), so
 * GEMDOS floppy DMA into it is safe on every TOS (see platform/CLAUDE.md). */
#define IOBUF (32u * 1024u)
static unsigned char g_buf[IOBUF];

static char g_inf[16384];        /* the whole manifest, read once */
static char g_title[128] = "Atari port installer";
static char g_dest[80]  = "C:\\U6";
static char g_src_root[4] = "A:\\";  /* drive we were launched from */
static int  g_cur_disk = 1;          /* floppy currently in the source drive */
static int  g_auto = 0;              /* non-interactive install (AUTO / cmd-line) */

static void say(const char *s)  { Cconws(s); }
static void nl(void)            { Cconws("\r\n"); }

/* read a line of input with GEMDOS line editing; returns length (0 = just RET) */
static int ask(char *out, int max)
{
    unsigned char line[84];
    if (max > 80) max = 80;
    line[0] = (unsigned char)max;          /* Cconrs: [0]=capacity in, [1]=len out */
    Cconrs((char *)line);
    int n = line[1];
    if (n > max) n = max;
    memcpy(out, line + 2, (size_t)n);
    out[n] = '\0';
    nl();
    return n;
}

static void pause_key(const char *msg)
{
    if (msg) say(msg);
    Cnecin();
    nl();
}

static void die(const char *msg)
{
    nl(); say("*** "); say(msg); nl();
    pause_key("Press any key to quit.");
    Pterm0();
}

/* create DEST and every parent folder below the drive letter (Dcreate makes
 * only the last component). Existing folders return an error we ignore. */
static void make_dirs(const char *path)
{
    char tmp[80];
    strcpy(tmp, path);
    /* start after "X:\" if present, else after a leading '\' */
    char *p = tmp;
    if (tmp[0] && tmp[1] == ':') p = tmp + 2;
    if (*p == '\\') p++;
    for (; *p; p++) {
        if (*p == '\\') { char c = *p; *p = '\0'; Dcreate(tmp); *p = c; }
    }
    Dcreate(tmp);
}

/* join DEST + '\' + name into out */
static void dest_path(char *out, const char *name)
{
    strcpy(out, g_dest);
    int n = (int)strlen(out);
    if (n && out[n - 1] != '\\') { out[n++] = '\\'; out[n] = '\0'; }
    strcat(out, name);
}

/* source path on the current floppy: "<drive>:\NAME" */
static void src_path(char *out, const char *name)
{
    out[0] = g_src_root[0]; out[1] = ':'; out[2] = '\\'; out[3] = '\0';
    strcat(out, name);
}

/* make sure floppy `disk` is the one in the drive; prompt + wait on a swap */
static void ensure_disk(int disk)
{
    if (disk == g_cur_disk) return;
    char m[64];
    /* "Insert disk N and press a key." */
    strcpy(m, "\r\n>> Insert disk ");
    m[17] = (char)('0' + (disk % 10)); m[18] = '\0';
    say(m);
    pause_key(" and press a key...");
    g_cur_disk = disk;
}

/* append the whole source file (on `disk`) to the already-open dst handle */
static void append_part(long dst, int disk, const char *name)
{
    char path[84];
    long src;
    for (;;) {
        ensure_disk(disk);
        src_path(path, name);
        src = Fopen(path, RD);
        if (src >= 0) break;
        say("\r\n*** Cannot read "); say(name);
        say(" on this disk.");
        pause_key("\r\nCheck the disk and press a key to retry.");
        g_cur_disk = -1;               /* force a re-verify on the retry */
    }
    for (;;) {
        long got = Fread((short)src, (long)IOBUF, g_buf);
        if (got <= 0) break;
        long put = Fwrite((short)dst, got, g_buf);
        if (put != got) { Fclose((short)src); Fclose((short)dst); die("write failed (disk full?)"); }
        say(".");
    }
    Fclose((short)src);
}

/* read INSTALL.INF from the source drive into g_inf (NUL-terminated) */
static void load_manifest(void)
{
    char path[84];
    src_path(path, "INSTALL.INF");
    long f = Fopen(path, RD);
    if (f < 0) die("INSTALL.INF not found on this disk.");
    long n = Fread((short)f, (long)sizeof(g_inf) - 1, g_inf);
    Fclose((short)f);
    if (n < 0) n = 0;
    g_inf[n] = '\0';
}

/* first word of a line -> keyword; returns pointer past the keyword+spaces */
static char *skip_ws(char *p) { while (*p == ' ' || *p == '\t') p++; return p; }

/* trim trailing CR/LF/space in place */
static void rstrip(char *s)
{
    int n = (int)strlen(s);
    while (n && (s[n-1] == '\r' || s[n-1] == '\n' || s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = '\0';
}

/* Pass 1: pull TITLE + DEST out of the manifest (a copy, so pass 2 sees it fresh) */
static void scan_header(void)
{
    char work[16384];
    memcpy(work, g_inf, sizeof(work));
    char *save = work, *line;
    for (line = work; *line; ) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';
        char *p = skip_ws(line);
        rstrip(p);
        if (*p && *p != ';' && *p != '#') {
            if (!strncmp(p, "TITLE", 5)) { char *v = skip_ws(p + 5); if (*v) strncpy(g_title, v, sizeof(g_title)-1); }
            else if (!strncmp(p, "DEST", 4)) { char *v = skip_ws(p + 4); if (*v) strncpy(g_dest, v, sizeof(g_dest)-1); }
            else if (!strncmp(p, "AUTO", 4)) { g_auto = 1; }
        }
        if (!eol) break;
        line = eol + 1;
    }
    (void)save;
}

/* Pass 2: execute OUT/SRC and copy everything into DEST */
static void run_manifest(void)
{
    long dst = -1;
    char outname[32] = "";
    int files = 0;

    char *line;
    for (line = g_inf; *line; ) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';
        char *p = skip_ws(line);
        rstrip(p);

        if (*p && *p != ';' && *p != '#') {
            if (!strncmp(p, "OUT", 3) && (p[3] == ' ' || p[3] == '\t')) {
                if (dst >= 0) { Fclose((short)dst); say(" ok\r\n"); }
                char *v = skip_ws(p + 3);
                strncpy(outname, v, sizeof(outname)-1); outname[sizeof(outname)-1] = '\0';
                char full[84];
                dest_path(full, outname);
                dst = Fcreate(full, 0);
                if (dst < 0) { say("\r\ncannot create "); say(full); die("Fcreate failed."); }
                say("  "); say(outname); say(" ");
                files++;
            } else if (!strncmp(p, "SRC", 3) && (p[3] == ' ' || p[3] == '\t')) {
                if (dst < 0) die("SRC before OUT in manifest.");
                char *v = skip_ws(p + 3);
                int disk = 0;
                while (*v >= '0' && *v <= '9') disk = disk*10 + (*v++ - '0');
                v = skip_ws(v);
                append_part(dst, disk ? disk : 1, v);
            } else if (!strncmp(p, "END", 3)) {
                break;
            }
        }
        if (!eol) break;
        line = eol + 1;
    }
    if (dst >= 0) { Fclose((short)dst); say(" ok\r\n"); }

    nl();
    if (files == 0) die("manifest listed no files.");
}

int main(int argc, char **argv)
{
    g_src_root[0] = (char)('A' + Dgetdrv());   /* the drive we launched from */

    say("\r\n=====================================================\r\n");
    load_manifest();
    scan_header();
    say("  "); say(g_title); nl();
    say("=====================================================\r\n\r\n");

    /* a command-line path overrides DEST and installs non-interactively */
    if (argc > 1 && argv[1] && argv[1][0]) { strncpy(g_dest, argv[1], sizeof(g_dest)-1); g_auto = 1; }

    if (!g_auto) {
        say("This will copy the game to your hard drive / CF card.\r\n");
        say("Install folder [ "); say(g_dest); say(" ]\r\n");
        say("Press RETURN to accept, or type a new path: ");
        char answer[80];
        if (ask(answer, sizeof(answer) - 1) > 0) strncpy(g_dest, answer, sizeof(g_dest)-1);
    }

    say("\r\nInstalling to "); say(g_dest); say(" ...\r\n\r\n");
    make_dirs(g_dest);
    run_manifest();

    say("Done. To play:\r\n");
    say("  open "); say(g_dest); say(" on the desktop and run the .PRG.\r\n\r\n");
    pause_key("Press any key to finish.");
    return 0;
}
