// SPDX-License-Identifier: GPL-2.0-or-later
/* vcardtest.c — VDI / graphics-card capability probe (M8, vcard backend groundwork).
 *
 * Runs as a GEM application (the vcard present path lives IN GEM, unlike the direct-hardware
 * ST/TT/Falcon backends). It detects the EdDI cookie (extended-VDI / graphics-card driver) and
 * queries the VDI for the CURRENT screen's format via vq_extnd, so we learn — at runtime, from the
 * card's own driver (fVDI/NVDI) — the resolution, colour depth (planes), indexed-vs-truecolor, and
 * off-screen backing-buffer size. That decides the vcard backend's present path (v_opnbm off-screen
 * bitmap + vro_cpyfm). We do NOT switch the card's mode (the user sets it via the card ACC); we
 * adapt to whatever mode is current. See render/CLAUDE.md "Hardware Detection (runtime — NO card list)".
 *
 * TOS/AES/VDI calls (named for verification):
 *   Getcookie('EdDI')      : cookie jar — extended VDI present + dispatcher address.
 *   appl_init / appl_exit  : AES 10 / 19 — register/deregister the app.
 *   graf_handle            : AES 77 — the AES physical workstation handle.
 *   vq_extnd(h,0/1,out)    : VDI 102 — owflag 0 = open-workstation info, 1 = EXTENDED inquiry.
 *   Getrez / Cconws / Crawcin.
 * Output is GEMDOS Cconws (mirrored to the host via Hatari/ARAnyM --conout 2).
 */
#include <gem.h>
#include <osbind.h>
#include <mint/cookie.h>
#include <stdint.h>

static void put(const char *s) { Cconws(s); }
static void nl(void)           { Cconws("\r\n"); }
static void put_int(int n)
{
    char b[12]; int i = 11; int neg = n < 0; unsigned u = neg ? (unsigned)(-n) : (unsigned)n;
    b[i--] = 0; if (!u) b[i--] = '0';
    while (u && i >= 0) { b[i--] = (char)('0' + u % 10u); u /= 10u; }
    if (neg && i >= 0) b[i--] = '-';
    put(&b[i + 1]);
}
static void put_hex32(uint32_t v)
{
    static const char h[] = "0123456789ABCDEF"; char b[11];
    b[0] = '0'; b[1] = 'x'; for (int i = 0; i < 8; i++) b[2 + i] = h[(v >> ((7 - i) * 4)) & 0xF];
    b[10] = 0; put(b);
}
/* dump `n` shorts of a VDI work_out array as "idx:val" (4 per line) for empirical field ID */
static void dump_wo(const short *wo, int n)
{
    for (int i = 0; i < n; i++) {
        put("  ["); put_int(i); put("]="); put_int(wo[i]);
        if ((i & 3) == 3) nl();
    }
    nl();
}

int main(void)
{
    put("=== u6e vcard VDI probe (M8) ===\r\n");

    /* 1. EdDI cookie — is an extended VDI / graphics-card driver installed? */
    long val;
    if (Getcookie(C_EdDI, &val) == C_FOUND) {
        put("EdDI: PRESENT, dispatcher="); put_hex32((uint32_t)val);
        put(" (>=1.00 => v_opnbm off-screen bitmaps)\r\n");
    } else {
        put("EdDI: not present (built-in TOS VDI; no third-party card driver)\r\n");
    }
    put("Getrez()="); put_int((int)Getrez()); nl();

    /* 2. AES up? Open our OWN virtual workstation cloned from the AES physical one. vq_extnd needs a
     * properly OPENED workstation — graf_handle alone gives the AES handle, but v_opnvwk gives us a
     * clean virtual one whose work_out already carries the resolution + colour count. */
    if (appl_init() < 0) { put("appl_init FAILED (no AES) — cannot probe VDI\r\n"); Crawcin(); return 1; }
    short wc, wh, ww, wb;
    short handle = graf_handle(&wc, &wh, &ww, &wb);   /* AES physical workstation handle */
    put("graf_handle -> AES handle="); put_int(handle); nl();

    short work_in[16], wo[64];
    work_in[0] = (short)(Getrez() + 2);               /* current VDI device id (Getrez+2) */
    for (int i = 1; i < 10; i++) work_in[i] = 1;      /* default line/marker/text/fill attrs */
    work_in[10] = 2;                                  /* 2 = raster coordinate system */
    v_opnvwk(work_in, &handle, wo);                   /* open virtual ws -> new handle in `handle` */
    if (handle == 0) { put("v_opnvwk FAILED\r\n"); appl_exit(); Crawcin(); return 1; }
    put("v_opnvwk -> virtual handle="); put_int(handle); nl();

    /* 3. Standard open-workstation info (from v_opnvwk work_out): resolution + colours. */
    put("--- v_opnvwk info ---\r\n");
    put("resolution: "); put_int(wo[0] + 1); put(" x "); put_int(wo[1] + 1);
    put("   colours(wo[13])="); put_int(wo[13]); nl();
    dump_wo(wo, 45);

    /* 4. EXTENDED inquiry (owflag=1): planes, palette-vs-truecolour, off-screen buffer. This is only
     * MEANINGFUL when an extended VDI (EdDI / fVDI / NVDI) is installed — base TOS VDI leaves it
     * unfilled (so we pre-zero to make "unsupported" obvious). Under fVDI, field indices to confirm
     * from the real dump: [4]=planes, [5]=palette-based flag (1 palette / 0 truecolour), off-screen
     * backing-buffer size = a LONG in the high entries. */
    short ext[64];
    for (int i = 0; i < 64; i++) ext[i] = 0;
    vq_extnd(handle, 1, ext);
    put("--- vq_extnd(1) extended info (needs EdDI/fVDI to be valid) ---\r\n");
    put("planes(ext[4])="); put_int(ext[4]);
    put("   palette-based?(ext[5])="); put_int(ext[5]); nl();
    put("off-screen buf size(ext[12..13] LONG)=");
    put_hex32(((uint32_t)(uint16_t)ext[12] << 16) | (uint16_t)ext[13]); nl();
    dump_wo(ext, 45);

    v_clsvwk(handle);
    appl_exit();
    put("=== probe done ===\r\n");
    Crawcin();
    return 0;
}
