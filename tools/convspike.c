/* convspike.c — M4 DECISION-GATE spike: run a real U6 NPC conversation.
 *
 * Pipeline proven here: CONVERSE.A (library) -> per-NPC entry (LZW) -> decode
 * -> u6conv parse -> interactive keyword/response loop. Default run drives a
 * scripted dialog with Dupre (NPC 2) and asserts expected lines, so it doubles
 * as a repeatable gate test. Usage: convspike <CONVERSE.A> [npc] [-i]
 *   -i = interactive (read keywords from stdin instead of the script).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "u6lzw.h"
#include "u6conv.h"

static uint8_t *slurp(const char *p, long *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*n);
    if (b && fread(b, 1, (size_t)*n, f) != (size_t)*n) { free(b); b = NULL; }
    fclose(f); return b;
}
static long idx_off(const uint8_t *f, int i)
{ return (long)(f[i*4] | f[i*4+1]<<8 | f[i*4+2]<<16 | (uint32_t)f[i*4+3]<<24); }

static int g_fail = 0;
static void expect(const char *label, const char *text, const char *needle)
{
    int ok = text && strstr(text, needle) != NULL;
    printf("    [%s] %s (expect \"%s\")\n", ok ? "PASS" : "FAIL", label, needle);
    if (!ok) g_fail++;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "refdata/dos/u6/CONVERSE.A";
    int npc = (argc > 2 && argv[2][0] != '-') ? atoi(argv[2]) : 2;  /* Dupre */
    int interactive = (argc > 2 && !strcmp(argv[argc-1], "-i"));

    long n; uint8_t *f = slurp(path, &n);
    if (!f) { printf("cannot read %s\n", path); return 2; }

    /* index size = first nonzero offset; entry = [off[npc], next nonzero) */
    int i = 0; while (idx_off(f, i) == 0) i++;
    long count = idx_off(f, i) / 4;
    if (npc < 0 || npc >= count) { printf("npc %d out of range (0..%ld)\n", npc, count-1); return 2; }
    long start = idx_off(f, npc);
    if (start == 0) { printf("npc %d has no conversation\n", npc); return 2; }
    long end = n; for (int j = npc + 1; j < count; j++) { long o = idx_off(f, j); if (o) { end = o; break; } }

    int32_t usz = u6lzw_unpacked_size(f + start, (int32_t)(end - start));
    uint8_t *script = malloc(usz > 0 ? usz : 1);
    int32_t got = u6lzw_decompress(f + start, (int32_t)(end - start), script, usz);

    u6conv_t c;
    int parsed = u6conv_parse(&c, script, got, "Avatar");
    if (!parsed) { printf("parse failed for npc %d\n", npc); return 2; }

    printf("== M4 spike: conversation with NPC %d ==\n", npc);
    printf("\nYou see %s.\n", c.desc[0] ? c.desc : c.name);
    printf("%s: %s\n", c.name, c.greeting);

    if (interactive) {
        char line[64];
        printf("\n(type keywords; 'bye' to end)\n");
        while (printf("you say: "), fflush(stdout), fgets(line, sizeof line, stdin)) {
            line[strcspn(line, "\r\n")] = '\0';
            if (!line[0]) continue;
            bool bye = false;
            const char *r = u6conv_respond(&c, line, &bye);
            printf("%s: %s\n", c.name, r ? r : "...");
            if (bye) break;
        }
        free(f); free(script);
        return 0;
    }

    /* scripted gate dialog */
    static const char *script_words[] = { "name", "job", "quest", "leave", "xyzzy", "bye" };
    printf("\n-- scripted dialog --\n");
    for (size_t k = 0; k < sizeof script_words / sizeof *script_words; k++) {
        bool bye = false;
        const char *r = u6conv_respond(&c, script_words[k], &bye);
        printf("you say: %s\n%s: %s\n", script_words[k], c.name, r ? r : "...");
        if (bye) break;
    }

    printf("\n-- gate assertions --\n");
    expect("npc is Dupre",        c.name, "Dupre");
    expect("greeting uses $P",    c.greeting, "Avatar");
    bool b;
    expect("name response",       u6conv_respond(&c, "name", &b),  "dew pray");
    expect("job response",        u6conv_respond(&c, "job", &b),   "questing");
    expect("quest response",      u6conv_respond(&c, "quest", &b), "fine ones");
    expect("leave response",      u6conv_respond(&c, "leave", &b), "strict orders");
    expect("unknown -> default",  u6conv_respond(&c, "xyzzy", &b), "Ask Iolo");
    const char *byeresp = u6conv_respond(&c, "bye", &b);
    expect("bye response",        byeresp, "action");
    printf("    [%s] bye ends conversation\n", b ? "PASS" : "FAIL"); if (!b) g_fail++;

    printf(g_fail ? "\nGATE: %d FAILURE(S) — NOT solvable as-is\n"
                  : "\nGATE: PASS — usecode conversation is decodable end-to-end\n", g_fail);
    free(f); free(script);
    return g_fail ? 1 : 0;
}
