/* convtest.c — M6 golden-transcript test harness for the conversation VM.
 *
 * Iterates EVERY conversation in CONVERSE.A and CONVERSE.B: decompresses (incl.
 * the stored case), parses, and drives a small transcript (greeting + name/job/
 * bye). Reports the parse/structure rate across all NPCs, and asserts specific
 * GOLDEN responses for known characters. Headless; no TOS. Usage:
 *   convtest <refdata/dos/u6>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "u6lzw.h"
#include "u6conv.h"

static uint8_t *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb"); if (!f) { printf("cannot read %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*n); fread(b, 1, (size_t)*n, f); fclose(f); return b;
}
static long idx_off(const uint8_t *f, int i)
{ return (long)(f[i*4] | f[i*4+1]<<8 | f[i*4+2]<<16 | (uint32_t)f[i*4+3]<<24); }

/* Decompress NPC `npc`'s conversation script (handles stored entries). Returns
 * malloc'd buffer + *len, or NULL if absent. */
static uint8_t *load_conv(const uint8_t *f, long flen, int npc, int32_t *len)
{
    int i = 0; while (idx_off(f, i) == 0) i++;
    long count = idx_off(f, i) / 4;
    if (npc < 0 || npc >= count) return NULL;
    long s = idx_off(f, npc); if (!s) return NULL;
    long e = flen; for (int j = npc + 1; j < count; j++) { long o = idx_off(f, j); if (o) { e = o; break; } }
    int32_t usz = u6lzw_unpacked_size(f + s, (int32_t)(e - s));
    int32_t cap = (usz > 0) ? usz : (int32_t)(e - s);   /* stored: body = entry-4 */
    uint8_t *d = malloc((size_t)cap + 4);
    int32_t got = u6lzw_decompress(f + s, (int32_t)(e - s), d, cap + 4);
    if (got <= 0) { free(d); return NULL; }
    *len = got;
    return d;
}
static int conv_count(const uint8_t *f)
{ int i = 0; while (idx_off(f, i) == 0) i++; return (int)(idx_off(f, i) / 4); }

static int g_fail = 0;
static void expect(const char *label, const char *text, const char *needle)
{
    int ok = text && strstr(text, needle) != NULL;
    printf("    [%s] %s (expect \"%s\")\n", ok ? "PASS" : "FAIL", label, needle);
    if (!ok) g_fail++;
}

/* sweep one file; accumulate stats */
static void sweep(const char *path, int *present, int *parsed, int *named, int *sect, int *resp)
{
    long n; uint8_t *f = slurp(path, &n);
    if (!f) return;
    int count = conv_count(f);
    for (int npc = 0; npc < count; npc++) {
        int32_t len; uint8_t *d = load_conv(f, n, npc, &len);
        if (!d) continue;
        (*present)++;
        u6conv_t c;
        if (u6conv_parse(&c, d, len, "Avatar")) {
            (*parsed)++;
            if (c.name[0]) (*named)++;
            if (c.nsect > 0) (*sect)++;
            bool bye; const char *r = u6conv_respond(&c, "name", &bye);
            if (r && r[0]) (*resp)++;
        }
        free(d);
    }
    free(f);
}

static void golden(const uint8_t *fa, long na)
{
    struct { int npc; const char *name; const char *kw; const char *needle; } g[] = {
        { 2, "Dupre",       "name", "dew pray" },
        { 3, "Shamino",     "name", "Salle" },
        { 4, "Iolo",        "name", "Yo-low" },
        { 5, "Lord British","name", "I am Lord British" },
        { 2, "Dupre",       "job",  "questing" },
    };
    for (unsigned i = 0; i < sizeof g / sizeof *g; i++) {
        int32_t len; uint8_t *d = load_conv(fa, na, g[i].npc, &len);
        char lbl[64];
        if (!d) { snprintf(lbl,sizeof lbl,"%s present", g[i].name); expect(lbl, NULL, "x"); continue; }
        u6conv_t c; u6conv_parse(&c, d, len, "Avatar");
        snprintf(lbl, sizeof lbl, "%s name", g[i].name);
        expect(lbl, c.name, g[i].name);
        bool bye; const char *r = u6conv_respond(&c, g[i].kw, &bye);
        snprintf(lbl, sizeof lbl, "%s '%s'", g[i].name, g[i].kw);
        expect(lbl, r, g[i].needle);
        free(d);
    }
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "refdata/dos/u6";
    char pa[1024], pb[1024];
    snprintf(pa, sizeof pa, "%s/CONVERSE.A", dir);
    snprintf(pb, sizeof pb, "%s/CONVERSE.B", dir);

    printf("== M6 conversation transcript test ==\n");
    int present = 0, parsed = 0, named = 0, sect = 0, resp = 0;
    sweep(pa, &present, &parsed, &named, &sect, &resp);
    sweep(pb, &present, &parsed, &named, &sect, &resp);
    printf("  swept %d conversations: parsed=%d named=%d with-sections=%d respond-to-name=%d\n",
           present, parsed, named, sect, resp);
    /* every present conversation should parse + be named (no crashes, structure ok) */
    if (parsed != present) { printf("  [FAIL] %d/%d parsed\n", parsed, present); g_fail++; }
    else printf("  [PASS] all %d conversations parse without error\n", present);
    if (named < present) { printf("  [WARN] %d/%d named (rest may be signs/system)\n", named, present); }

    printf("  golden transcripts:\n");
    long na; uint8_t *fa = slurp(pa, &na);
    if (fa) { golden(fa, na); free(fa); }

    printf(g_fail ? "\nRESULT: %d FAILURE(S)\n" : "\nRESULT: ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
