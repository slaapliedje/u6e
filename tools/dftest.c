/* dftest.c — host-side validation harness for /dataformats decoders (M2).
 *
 * Runs on the dev PC (NOT the ST). Proves decode-correctness against the user's
 * own original files in /refdata BEFORE any of this is trusted on hardware
 * (milestone M2). Reads files; ships/commits nothing. Usage:
 *
 *   dftest <path-to-refdata/dos/u6>
 *
 * Exits non-zero if any check fails (so it slots into an automated build).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "u6lzw.h"
#include "u6lib.h"

static int g_fail = 0;
static void check(const char *what, int ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fail++;
}

static unsigned char *slurp(const char *path, long *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)n);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (out_len) *out_len = n;
    return b;
}

static char *join(const char *dir, const char *name)
{
    static char buf[1024];
    snprintf(buf, sizeof buf, "%s/%s", dir, name);
    return buf;
}

/* ---- LZW: decode a real file, assert it reaches its declared size ------- */
static void test_lzw_file(const char *dir, const char *name, int32_t expect)
{
    long n; unsigned char *src = slurp(join(dir, name), &n);
    if (!src) { printf("  (skip %s: not found)\n", name); return; }
    int32_t want = u6lzw_unpacked_size(src, (int32_t)n);
    int32_t cap = (want > 0) ? want : (int32_t)n;
    unsigned char *dst = malloc((size_t)cap + 16);
    int32_t got = u6lzw_decompress(src, (int32_t)n, dst, cap + 16);
    char msg[128];
    snprintf(msg, sizeof msg, "LZW %s -> %d bytes (declared %d)", name, got, want);
    check(msg, got == want && (expect < 0 || got == expect));
    free(src); free(dst);
}

/* ---- LZW: synthetic round-trip via a reference GIF-style encoder --------
 * An independent encoder (clear=256, EOI unused, first=258, no early change,
 * LSB-first) confirms the decoder is self-consistent on data we control. */
static void put_bits(unsigned char *out, int32_t *bitpos, int code, int width)
{
    for (int i = 0; i < width; i++) {
        int32_t bp = *bitpos + i;
        if (code & (1 << i)) out[bp >> 3] |= (unsigned char)(1 << (bp & 7));
    }
    *bitpos += width;
}
static int32_t lzw_encode(const unsigned char *in, int32_t n, unsigned char *out)
{
    /* dictionary: map (prefix<<8 | byte) -> code, via linear scan (test only) */
    static int32_t ent_prefix[4096]; static int ent_byte[4096];
    int width = 9, next = 258; int32_t bitpos = 0;
    put_bits(out, &bitpos, 256, width);                 /* initial clear */
    int cur = -1;
    for (int32_t i = 0; i < n; i++) {
        int b = in[i];
        int found = -1;
        if (cur >= 0)
            for (int c = 258; c < next; c++)
                if (ent_prefix[c] == cur && ent_byte[c] == b) { found = c; break; }
        if (cur < 0) { cur = b; continue; }
        if (found >= 0) { cur = found; continue; }
        put_bits(out, &bitpos, cur, width);
        if (next < 4096) {
            ent_prefix[next] = cur; ent_byte[next] = b; next++;
            /* Matched empirically to the decoder's bump rule (the decoder lags
             * the encoder by one entry, so this encoder bumps at +1). */
            if (next == (1 << width) + 1 && width < 12) width++;
        }
        cur = b;
    }
    if (cur >= 0) put_bits(out, &bitpos, cur, width);
    return (bitpos + 7) / 8;
}
static void test_lzw_roundtrip(void)
{
    enum { N = 9000 };
    unsigned char *orig = malloc(N), *packed = malloc(N * 2 + 64), *back = malloc(N + 16);
    for (int i = 0; i < N; i++)                      /* compressible-ish pattern */
        orig[i] = (unsigned char)((i * 7) ^ (i >> 3) ^ (i / 200));
    memset(packed, 0, N * 2 + 64);
    packed[0] = N & 0xFF; packed[1] = (N >> 8) & 0xFF;
    packed[2] = (N >> 16) & 0xFF; packed[3] = (N >> 24) & 0xFF;
    int32_t plen = lzw_encode(orig, N, packed + 4);
    int32_t got = u6lzw_decompress(packed, plen + 4, back, N + 16);
    check("LZW synthetic round-trip (encode->decode)", got == N && memcmp(orig, back, N) == 0);
    free(orig); free(packed); free(back);
}

/* ---- library: structural validation of a real library file -------------- */
static void test_lib_file(const char *dir, const char *name, int expect_width, int32_t expect_count)
{
    long n; unsigned char *buf = slurp(join(dir, name), &n);
    if (!buf) { printf("  (skip %s: not found)\n", name); return; }

    u6lib_t lib;
    int opened = u6lib_open_auto(&lib, buf, (int32_t)n);
    char msg[160];
    snprintf(msg, sizeof msg, "lib %s: auto-detect width=%d count=%d (expect %d/%d)",
             name, opened ? lib.offset_width : -1, opened ? lib.count : -1,
             expect_width, expect_count);
    check(msg, opened && lib.offset_width == expect_width && lib.count == expect_count);
    if (!opened) { free(buf); return; }

    /* Every present entry must lie within the file, and the entries must tile
     * the buffer up to its end (last present entry ends at len). */
    int in_bounds = 1, present = 0; int32_t last_end = 0;
    for (int32_t i = 0; i < lib.count; i++) {
        int32_t sz; const uint8_t *e = u6lib_entry(&lib, i, &sz);
        if (!e) continue;
        present++;
        int32_t off = (int32_t)(e - buf);
        if (off < 0 || off + sz > (int32_t)n) in_bounds = 0;
        if (off + sz > last_end) last_end = off + sz;
    }
    check("  all present entries within file bounds", in_bounds);
    check("  entries tile to end of file",            last_end == (int32_t)n);
    snprintf(msg, sizeof msg, "  (%d/%d entries present)", present, lib.count);
    printf("%s\n", msg);
    free(buf);
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "refdata/dos/u6";
    printf("== dftest: validating /dataformats against real U6 data in %s ==\n", dir);

    printf("[lzw]\n");
    test_lzw_file(dir, "MASKTYPE.VGA", 3968);
    test_lzw_file(dir, "MAPTILES.VGA", 117408);
    test_lzw_file(dir, "ANIMMASK.VGA", 2048);
    test_lzw_roundtrip();

    printf("[library]\n");
    test_lib_file(dir, "PORTRAIT.A", 4, 98);   /* 4-byte offsets */
    test_lib_file(dir, "BOOK.DAT",   2, 128);  /* 2-byte offsets */

    printf(g_fail ? "\nRESULT: %d FAILURE(S)\n" : "\nRESULT: ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
