// SPDX-License-Identifier: GPL-2.0-or-later
/* selftest.c — M0/M1 "hello, hardware" + platform-foundation self-verify.
 *
 * Prints detected hardware (M0) and then exercises the M1 services — memory
 * pools (chip vs fast + scratch), GEMDOS file I/O (write/read-back/checksum),
 * and timing (measured refresh) — emitting PASS/FAIL lines so the headless
 * `make check` run is a real regression test, not just a demo.
 *
 * Output via GEMDOS Cconws (0x09) only — no stdio — so the exact calls we make
 * stay explicit and the binary stays lean.
 */
#include "platform.h"
#include "platmem.h"
#include "platfile.h"
#include "plattime.h"
#include <osbind.h>   /* Cconws (0x09), Crawcin (0x07) */

/* ---- tiny console helpers (no stdio) ------------------------------------ */
static void put(const char *s) { Cconws(s); }
static void nl(void)           { Cconws("\r\n"); }

static void put_int(int n)
{
    char buf[12];
    int i = (int)sizeof(buf) - 1;
    int neg = (n < 0);
    unsigned u = neg ? (unsigned)(-n) : (unsigned)n;
    buf[i--] = '\0';
    if (u == 0u) buf[i--] = '0';
    while (u > 0u && i >= 0) { buf[i--] = (char)('0' + (u % 10u)); u /= 10u; }
    if (neg && i >= 0) buf[i--] = '-';
    put(&buf[i + 1]);
}

static void put_hex32(uint32_t v)
{
    static const char hexd[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++) buf[2 + i] = hexd[(v >> ((7 - i) * 4)) & 0xF];
    buf[10] = '\0';
    put(buf);
}

static int g_fail = 0;
static void check(const char *label, int ok)
{
    put(ok ? "  [PASS] " : "  [FAIL] ");
    put(label); nl();
    if (!ok) g_fail++;
}

static void put_cpu(int cpu) { put("680"); if (cpu < 10) put("0"); put_int(cpu); }

/* FNV-1a checksum for the file round-trip test. */
static uint32_t checksum(const uint8_t *p, int32_t n)
{
    uint32_t h = 2166136261u;
    for (int32_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

/* ---- M0: hardware ------------------------------------------------------- */
static void report_hardware(const platform_caps_t *c)
{
    put("u6e platform self-test (M0+M1)"); nl();
    put("=============================="); nl();
    put("machine : "); put(platform_machine_name(c->machine)); nl();
    put("video   : "); put(platform_video_name(c->video)); nl();
    put("cpu     : "); put_cpu(c->cpu); nl();
    put("fpu     : "); put(c->has_fpu ? "present" : "none (not used)"); nl();
    put("blitter : "); put(c->has_blitter ? "present" : "none (CPU blit)"); nl();
    put("cookies : "); put(c->has_cookiejar ? "present" : "absent (base ST)"); nl();
    put("gemdos  : "); put_hex32(c->gemdos_version); nl();
    put("mxalloc : "); put(c->has_mxalloc ? "yes" : "no (Malloc fallback)"); nl();
}

/* ---- M1: memory --------------------------------------------------------- */
static void test_memory(void)
{
    nl(); put("[memory]"); nl();
    plat_mem_info_t mi;
    plat_mem_get_info(&mi);

    put("  phystop (ST top): "); put_hex32(mi.phystop); nl();
    put("  chip pool : "); put_hex32((uint32_t)(uintptr_t)mi.chip_base);
    put("  size "); put_int((int)mi.chip_size); nl();
    put("  fast pool : "); put_hex32((uint32_t)(uintptr_t)mi.fast_base);
    put("  size "); put_int((int)mi.fast_size);
    put(mi.have_altram ? "  (Alt-RAM)" : "  (ST-RAM fallback)"); nl();

    void *c = plat_alloc_chip(4096);
    void *f = plat_alloc_fast(4096);
    put("  chip alloc: "); put_hex32((uint32_t)(uintptr_t)c);
    put(plat_addr_is_fastram(c) ? "  FAST" : "  CHIP"); nl();
    put("  fast alloc: "); put_hex32((uint32_t)(uintptr_t)f);
    put(plat_addr_is_fastram(f) ? "  FAST" : "  CHIP"); nl();

    check("chip alloc returned a buffer",            c != 0);
    check("fast alloc returned a buffer",            f != 0);
    check("chip buffer is in ST-RAM (chip-safe)",    c && !plat_addr_is_fastram(c));
    /* On an Alt-RAM machine, fast MUST be Alt-RAM; otherwise it falls back. */
    if (mi.have_altram)
        check("fast buffer is in Alt-RAM",           f && plat_addr_is_fastram(f));
    else
        check("fast buffer falls back to ST-RAM",    f && !plat_addr_is_fastram(f));

    /* scratch mark/reset reclaims */
    plat_mark m = plat_scratch_mark();
    void *s1 = plat_scratch_alloc(8192);
    plat_scratch_reset(m);
    void *s2 = plat_scratch_alloc(8192);
    check("scratch reset reclaims the same address", s1 && s1 == s2);
}

/* ---- M1: file I/O (write -> read-back -> checksum) ----------------------- */
static void test_files(void)
{
    nl(); put("[file i/o]"); nl();
    enum { SIZE = 40000 };   /* > 2x the 16K bounce buffer => multi-chunk path */
    const char *path = "u6e_test.dat";

    uint8_t *src = (uint8_t *)plat_alloc_fast(SIZE);
    if (!src) { check("alloc source buffer", 0); return; }
    for (int32_t i = 0; i < SIZE; i++) src[i] = (uint8_t)(i * 73u + 19u);
    uint32_t want = checksum(src, SIZE);

    plat_file w = plat_fcreate(path);
    int wrote_ok = (w != PLAT_BADFILE) && (plat_fwrite(w, src, SIZE) == SIZE);
    if (w != PLAT_BADFILE) plat_fclose(w);
    check("create + write large file", wrote_ok);

    int32_t got_len = 0;
    uint8_t *back = (uint8_t *)plat_load_file(path, /*fast=*/true, &got_len);
    check("load_file read it back",     back != 0 && got_len == SIZE);
    check("round-trip checksum matches", back && checksum(back, got_len) == want);
}

/* ---- M1: timing --------------------------------------------------------- */
static void test_timing(void)
{
    nl(); put("[timing]"); nl();
    int hz = plat_refresh_hz();
    put("  refresh   : "); put_int(hz); put(" Hz"); nl();

    uint32_t v0 = plat_vbl_count();
    uint32_t t0 = plat_ticks_ms();
    for (int i = 0; i < 10; i++) plat_wait_vbl();
    uint32_t dv = plat_vbl_count() - v0;
    uint32_t dms = plat_ticks_ms() - t0;
    put("  10 vbl -> "); put_int((int)dv); put(" vbls, ");
    put_int((int)dms); put(" ms"); nl();

    check("refresh in 49..72 Hz range", hz >= 49 && hz <= 72);
    check("VBL counter advanced ~10",   dv >= 9 && dv <= 12);
    check("ms clock advanced",          dms > 0u);
}

int main(void)
{
    platform_caps_t caps;
    platform_detect(&caps);
    report_hardware(&caps);

    if (!plat_mem_init(&caps)) {
        nl(); put("FATAL: plat_mem_init failed"); nl();
        Crawcin();
        return 1;
    }
    plat_time_init();

    test_memory();
    test_files();
    test_timing();

    nl();
    put(g_fail == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES = ");
    if (g_fail) put_int(g_fail);
    nl();

    plat_mem_shutdown();

    put("-- press a key to exit --"); nl();
    Crawcin();
    return g_fail == 0 ? 0 : 1;
}
