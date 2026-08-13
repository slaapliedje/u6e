// SPDX-License-Identifier: GPL-2.0-or-later
/* render.c — runtime renderer dispatcher (M8 unification).
 *
 * The engine calls the public rdr_* / scroll_* interface (render.h). This file provides those
 * symbols as a thin forwarder over ONE backend chosen at startup from the hardware detection, so a
 * single binary drives the whole matrix (goals #2 & #3 collapsed into one code path):
 *
 *   EdDI cookie present  -> graphics card / extended VDI (fVDI/NVDI)  -> rdr_be_vcard
 *   else _VDO (platform_detect):  Falcon -> rdr_be_falcon,  TT -> rdr_be_tt,  ST/STE -> rdr_be_st
 *
 * Rationale for EdDI-first: an extended VDI (a card, or NVDI on built-in video under AES/MiNT) means
 * we must present cooperatively through the VDI rather than Setscreen the display out from under it.
 * On bare single-TOS video there is no EdDI, so we take the direct-hardware path.
 *
 * Which backends are compiled in is a build choice (HAVE_RDR_* — the flagship links all four, the
 * memory-constrained ST build links only rdr_st); the SAME selection code serves every subset.
 *
 * Detection call: Getcookie('EdDI') (mint/cookie.h) + platform_detect() (_VDO cookie).
 */
#include "render_backend.h"
#include "platform.h"

#include <mint/cookie.h>

static const rdr_vtable *g_be;      /* the selected backend (NULL until rdr_select) */

/* Physical-cursor -> logical-UI map (render.h). The present transform differs per backend (the
 * Falcon VGA letterbox shifts the 320x200 UI down; the TT doubles it into 320x480); the ACTIVE
 * backend's rdr_init sets these so the engine can turn a line-A mouse (x,y) into UI coords:
 *   ui_y = (phys_y - rdr_ui_yoff) / rdr_ui_yscale ,  ui_x = phys_x .
 * Defaults are identity (plain ST 320x200); each backend overrides in its init. */
int rdr_ui_yoff = 0, rdr_ui_yscale = 1;

/* Pick the backend from the hardware. Idempotent + lazy: rdr_prefer_truecolor may run before
 * rdr_init, and both call this; platform_detect is deterministic so a second call is harmless. */
static void rdr_select(void)
{
    if (g_be) return;

#ifdef HAVE_RDR_VCARD
    {   long v;
        if (Getcookie(C_EdDI, &v) == C_FOUND) { g_be = &rdr_be_vcard; return; }
    }
#endif

    platform_caps_t caps;
    platform_detect(&caps);
    switch (caps.video) {
#ifdef HAVE_RDR_FALCON
    case VIDEO_FALCON: g_be = &rdr_be_falcon; return;
#endif
#ifdef HAVE_RDR_TT
    case VIDEO_TT:     g_be = &rdr_be_tt;     return;
#endif
#ifdef HAVE_RDR_ST
    case VIDEO_ST:
    case VIDEO_STE:    g_be = &rdr_be_st;     return;
#endif
    default: break;
    }

    /* No exact match (e.g. VIDEO_UNKNOWN, or that backend not built in): fall back to whatever IS
     * linked, most-capable first. At least one HAVE_RDR_* is always defined. */
#if defined(HAVE_RDR_TT)
    g_be = &rdr_be_tt;
#elif defined(HAVE_RDR_FALCON)
    g_be = &rdr_be_falcon;
#elif defined(HAVE_RDR_ST)
    g_be = &rdr_be_st;
#elif defined(HAVE_RDR_VCARD)
    g_be = &rdr_be_vcard;
#else
#error "render.c: no renderer backend compiled in (define at least one HAVE_RDR_*)"
#endif
}

/* Which backend got selected — for diagnostics / logging. 0=st 1=tt 2=falcon 3=vcard, -1=none. */
int rdr_active_backend(void)
{
    if (!g_be) return -1;
#ifdef HAVE_RDR_ST
    if (g_be == &rdr_be_st)     return 0;
#endif
#ifdef HAVE_RDR_TT
    if (g_be == &rdr_be_tt)     return 1;
#endif
#ifdef HAVE_RDR_FALCON
    if (g_be == &rdr_be_falcon) return 2;
#endif
#ifdef HAVE_RDR_VCARD
    if (g_be == &rdr_be_vcard)  return 3;
#endif
    return -1;
}

/* ---- public interface: select once, then forward to the chosen backend ---- */
bool     rdr_init(const platform_caps_t *c) { rdr_select(); return g_be->init(c); }
void     rdr_prefer_truecolor(int on)       { rdr_select(); g_be->prefer_truecolor(on); }

void     rdr_shutdown(void)                       { g_be->shutdown(); }
void     rdr_get_caps(rdr_caps *o)                { g_be->get_caps(o); }
int      rdr_width(void)                          { return g_be->width(); }
int      rdr_height(void)                         { return g_be->height(); }
uint8_t *rdr_backbuffer(void)                     { return g_be->backbuffer(); }
void     rdr_set_palette(const uint8_t *p, int n) { g_be->set_palette(p, n); }
void     rdr_set_lowcolor(const uint8_t *p16, const uint8_t *lut) { g_be->set_lowcolor(p16, lut); }
void     rdr_set_lowcolor_bands(int nb, const uint8_t *y0, const uint8_t *p, const uint8_t *lut)
                                                  { g_be->set_lowcolor_bands(nb, y0, p, lut); }
void     rdr_lowcolor_cycle(const uint8_t *p16)   { g_be->lowcolor_cycle(p16); }
void     rdr_set_ste_mode(int on)                 { g_be->set_ste_mode(on); }
int      rdr_is_ste(void)                         { return g_be->is_ste(); }
void     rdr_present(void)                        { g_be->present(); }
void     rdr_present_rect(int x, int y, int w, int h) { g_be->present_rect(x, y, w, h); }
void     rdr_present_doubled(int lh)              { g_be->present_doubled(lh); }
void     rdr_present_rect_doubled(int x, int y, int w, int h, int lh)
                                                  { g_be->present_rect_doubled(x, y, w, h, lh); }
void     rdr_flip_begin(void)                     { g_be->flip_begin(); }
void     rdr_flip_end(void)                       { g_be->flip_end(); }
void     rdr_test_hscroll(int v)                  { g_be->test_hscroll(v); }
bool     scroll_begin(const uint8_t *c, int cw, int ch) { return g_be->scrl_begin(c, cw, ch); }
void     scroll_pan(int px)                       { g_be->scrl_pan(px); }
void     scroll_end(void)                         { g_be->scrl_end(); }
void     rdr_scroll_left16(const uint8_t *c, int cw, int lh, int px) { g_be->scroll_left16(c, cw, lh, px); }
void     rdr_debug_info(uint32_t out[6])          { g_be->debug_info(out); }
