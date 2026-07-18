// SPDX-License-Identifier: GPL-2.0-or-later
/* vcardrender.c — standalone test harness for the vcard renderer backend (rdr_vcard.c, M8).
 *
 * A GEM app that opens the card via rdr_vcard, fills the 320x240 backbuffer with a 256-colour test
 * pattern (a 16x16 index grid + a gradient palette), and presents it in a short loop so a headless
 * screenshot catches it. Validates the c2p -> vro_cpyfm present + palette on a real graphics-card VDI
 * (run under the ARAnyM+fVDI+AFROS rig, docs/design/vcard-aranym-rig.md). Reports over ARAnyM
 * NF_STDERR. NOT the game — just proves the backend paints correct colours at the right place.
 */
#include "render.h"
#include "platmem.h"
#include <gem.h>
#include <osbind.h>

/* ARAnyM NF_STDERR (host console) — see render/vcardtest.c */
static long NF_getid = 0x73004e75L, NF_call = 0x73014e75L;
#define nfGetID(n)  (((long (*)(const char *))&NF_getid) n)
#define nfCall(n)   (((long (*)(long, ...))&NF_call) n)
static long g_nf = 0;
static void say(const char *s) { if (g_nf) nfCall((g_nf, s)); }
static void sayn(const char *label, long v)
{
    char b[16]; int i = 15; b[i--] = 0; b[i--] = '\n'; b[i--] = '\r';
    unsigned u = v < 0 ? -v : v; if (!u) b[i--] = '0';
    while (u && i >= 0) { b[i--] = (char)('0' + u % 10); u /= 10; }
    if (v < 0) b[i--] = '-';
    say(label); say(&b[i + 1]);
}

int main(void)
{
    g_nf = nfGetID(("NF_STDERR"));
    say("vcardrender: start\r\n");

    /* Depth + truecolour layout auto-detect via vq_scrninfo (rdr_vcard.c). Set fVDI to a truecolour
     * mode (e.g. 640x480x16 or x32) and the expand path below drives it. `rdr_vcard_force_depth` can
     * still override the detected depth for testing, but it is not needed on a real truecolour mode. */
    platform_caps_t caps;
    platform_detect(&caps);
    if (!plat_mem_init(&caps)) { say("plat_mem_init FAILED\r\n"); return 1; }
    if (!rdr_init(&caps)) { say("rdr_init FAILED\r\n"); return 1; }
    say("rdr_init OK\r\n");
    sayn("  dispatcher backend (0=st 1=tt 2=falcon 3=vcard)=", (long)rdr_active_backend());
    { uint32_t dbg[6]; rdr_debug_info(dbg);
      sayn("  screen w=", (long)dbg[0]); sayn("  screen h=", (long)dbg[1]);
      sayn("  detected bpp=", (long)dbg[2]); sayn("  EdDI=", (long)dbg[3]);
      sayn("  truecolor=", (long)((dbg[5] >> 24) & 0xFF));
      sayn("  Rbits=", (long)((dbg[5] >> 16) & 0xFF));
      sayn("  Gbits=", (long)((dbg[5] >> 8) & 0xFF));
      sayn("  Bbits=", (long)(dbg[5] & 0xFF)); }

    uint8_t *bb = rdr_backbuffer();
    int w = rdr_width(), h = rdr_height();
    say(bb ? "backbuffer OK\r\n" : "backbuffer NULL\r\n");

    /* test pattern: a 16x16 grid of the 256 palette indices (each ~20x15 px cell). */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            bb[y * w + x] = (uint8_t)(((y * 16 / h) * 16 + (x * 16 / w)) & 0xFF);
    say("pattern fill done\r\n");

    /* gradient palette so each index is a distinct, checkable colour (6-bit channels). */
    static uint8_t pal[256 * 3];
    for (int i = 0; i < 256; i++) {
        pal[i*3+0] = (uint8_t)((i * 63) / 255);          /* R ramps 0..63 with the index   */
        pal[i*3+1] = (uint8_t)(((i & 0x0F) * 63) / 15);  /* G = low nibble (column)         */
        pal[i*3+2] = (uint8_t)(63 - (i * 63) / 255);     /* B inverse ramp                  */
    }
    say("palette built\r\n");
    rdr_set_palette(pal, 256);        /* now via the vdi() trap (libgem vs_color faults) */
    say("palette set\r\n");

    /* Present in a long loop so the frame stays on top for a headless screenshot. */
    for (int f = 0; f < 1200; f++) {  /* ~60 s @ 50 ms */
        rdr_present();
        evnt_timer(50UL);
    }
    say("presenting done\r\n");

    rdr_shutdown();
    say("vcardrender: done\r\n");
    return 0;
}
