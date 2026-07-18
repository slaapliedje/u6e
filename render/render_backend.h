// SPDX-License-Identifier: GPL-2.0-or-later
/* render_backend.h — the renderer BACKEND vtable + backend namespacing (M8 unification).
 *
 * The engine talks to the public rdr_* interface (render.h). render.c implements that interface as a
 * thin dispatcher over ONE selected backend, chosen at runtime from the hardware detection
 * (EdDI cookie -> graphics card / extended VDI; _VDO -> built-in ST/STE/TT/Falcon video). Each
 * backend (render/rdr_*.c) exports exactly one symbol — its rdr_vtable — with all of its entry
 * points file-local, so every backend can link into the SAME binary without symbol clashes.
 *
 * A backend .c does, right after its normal #includes:
 *     #define RDR_BACKEND st            // st | tt | falcon | vcard
 *     #include "render_backend.h"
 * which (a) macro-renames its 25 public entry points to  <RDR_BACKEND>_<name>  (unique globals) and
 * (b) provides RDR_MAKE_VTABLE(st) to emit the  rdr_be_st  vtable at the bottom of the file.
 */
#ifndef U6E_RENDER_BACKEND_H
#define U6E_RENDER_BACKEND_H

#include "render.h"

/* The full public renderer surface, as function pointers (signatures mirror render.h). */
typedef struct rdr_vtable {
    bool     (*init)(const platform_caps_t *);
    void     (*shutdown)(void);
    void     (*get_caps)(rdr_caps *);
    int      (*width)(void);
    int      (*height)(void);
    uint8_t *(*backbuffer)(void);
    void     (*set_palette)(const uint8_t *, int);
    void     (*set_lowcolor)(const uint8_t *, const uint8_t *);
    void     (*set_lowcolor_bands)(int, const uint8_t *, const uint8_t *, const uint8_t *);
    void     (*lowcolor_cycle)(const uint8_t *);
    void     (*set_ste_mode)(int);
    int      (*is_ste)(void);
    void     (*prefer_truecolor)(int);
    void     (*present)(void);
    void     (*present_rect)(int, int, int, int);
    void     (*present_doubled)(int);
    void     (*present_rect_doubled)(int, int, int, int, int);
    void     (*flip_begin)(void);
    void     (*flip_end)(void);
    void     (*test_hscroll)(int);
    /* NB: these three fields are named scrl_* (not scroll_*) on purpose — the backend namespace
     * macros below #define scroll_begin/pan/end, which would otherwise rewrite the field designators. */
    bool     (*scrl_begin)(const uint8_t *, int, int);
    void     (*scrl_pan)(int);
    void     (*scrl_end)(void);
    void     (*scroll_left16)(const uint8_t *, int, int, int);
    void     (*debug_info)(uint32_t *);
} rdr_vtable;

/* One per backend. Only those compiled into a given binary are defined; render.c references each
 * behind its HAVE_RDR_* guard so a subset build (e.g. the slim ST) still links. */
extern const rdr_vtable rdr_be_st;
extern const rdr_vtable rdr_be_tt;
extern const rdr_vtable rdr_be_falcon;
extern const rdr_vtable rdr_be_vcard;

/* ---- backend-side namespacing (only active inside a backend TU) ---------- */
#ifdef RDR_BACKEND
#define RDR__P2(p, s) p##_##s
#define RDR__P1(p, s) RDR__P2(p, s)
#define RDR__P(s)     RDR__P1(RDR_BACKEND, s)

#define rdr_init                 RDR__P(rdr_init)
#define rdr_shutdown             RDR__P(rdr_shutdown)
#define rdr_get_caps             RDR__P(rdr_get_caps)
#define rdr_width                RDR__P(rdr_width)
#define rdr_height               RDR__P(rdr_height)
#define rdr_backbuffer           RDR__P(rdr_backbuffer)
#define rdr_set_palette          RDR__P(rdr_set_palette)
#define rdr_set_lowcolor         RDR__P(rdr_set_lowcolor)
#define rdr_set_lowcolor_bands   RDR__P(rdr_set_lowcolor_bands)
#define rdr_lowcolor_cycle       RDR__P(rdr_lowcolor_cycle)
#define rdr_set_ste_mode         RDR__P(rdr_set_ste_mode)
#define rdr_is_ste               RDR__P(rdr_is_ste)
#define rdr_prefer_truecolor     RDR__P(rdr_prefer_truecolor)
#define rdr_present              RDR__P(rdr_present)
#define rdr_present_rect         RDR__P(rdr_present_rect)
#define rdr_present_doubled      RDR__P(rdr_present_doubled)
#define rdr_present_rect_doubled RDR__P(rdr_present_rect_doubled)
#define rdr_flip_begin           RDR__P(rdr_flip_begin)
#define rdr_flip_end             RDR__P(rdr_flip_end)
#define rdr_test_hscroll         RDR__P(rdr_test_hscroll)
#define scroll_begin             RDR__P(scroll_begin)
#define scroll_pan               RDR__P(scroll_pan)
#define scroll_end               RDR__P(scroll_end)
#define rdr_scroll_left16        RDR__P(rdr_scroll_left16)
#define rdr_debug_info           RDR__P(rdr_debug_info)

/* Emit the backend's vtable. Field values are the (now namespaced) entry points. */
#define RDR_MAKE_VTABLE(nm)                                                         \
    const rdr_vtable rdr_be_##nm = {                                               \
        .init = rdr_init, .shutdown = rdr_shutdown, .get_caps = rdr_get_caps,       \
        .width = rdr_width, .height = rdr_height, .backbuffer = rdr_backbuffer,     \
        .set_palette = rdr_set_palette, .set_lowcolor = rdr_set_lowcolor,           \
        .set_lowcolor_bands = rdr_set_lowcolor_bands, .lowcolor_cycle = rdr_lowcolor_cycle, \
        .set_ste_mode = rdr_set_ste_mode, .is_ste = rdr_is_ste,                     \
        .prefer_truecolor = rdr_prefer_truecolor, .present = rdr_present,           \
        .present_rect = rdr_present_rect, .present_doubled = rdr_present_doubled,   \
        .present_rect_doubled = rdr_present_rect_doubled, .flip_begin = rdr_flip_begin, \
        .flip_end = rdr_flip_end, .test_hscroll = rdr_test_hscroll,                 \
        .scrl_begin = scroll_begin, .scrl_pan = scroll_pan,                         \
        .scrl_end = scroll_end, .scroll_left16 = rdr_scroll_left16,                 \
        .debug_info = rdr_debug_info }
#endif /* RDR_BACKEND */

#endif /* U6E_RENDER_BACKEND_H */
