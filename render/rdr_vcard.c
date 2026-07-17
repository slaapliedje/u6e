// SPDX-License-Identifier: GPL-2.0-or-later
/* rdr_vcard.c — graphics-card renderer backend via the VDI (M8, goals #2/#3).
 *
 * Unlike the direct-hardware ST/TT/Falcon backends, a graphics card is reached THROUGH GEM/VDI: the
 * card ships a VDI driver (fVDI / NVDI) and we present into whatever mode the user has set (we do NOT
 * change the card's resolution — see render/CLAUDE.md). We compose into our own canonical 8-bit
 * chunky backbuffer exactly like the other backends, then present it with a single VDI raster copy:
 *   c2p the backbuffer to an 8-plane STANDARD-format MFDB, then vro_cpyfm it to the screen. The VDI
 *   converts that standard bitmap to the card's real pixel format (8/16/24/32 bpp) using the logical
 *   colour table we load with vs_color — so this one path works on an indexed OR a truecolour card
 *   without us knowing the card's byte layout. Never per-tile VDI ops (project rule).
 *
 * The 320x240 game frame is shown 1:1, centred on the (larger) card screen.
 *
 * AES/VDI calls (named for verification): appl_init/appl_exit (AES 10/19), graf_handle (AES 77),
 * v_opnvwk/v_clsvwk (VDI 100/101), vs_color (VDI 14), vro_cpyfm (VDI 109), Getcookie('EdDI').
 * Caps of the validation rig (ARAnyM+fVDI+AFROS, docs/design/vcard-aranym-rig.md): EdDI present,
 * 800x608, 32bpp truecolour (fVDI does the 8->32 transform at blit).
 */
#include "render.h"

#include <gem.h>
#include <osbind.h>
#include <mint/cookie.h>
#include <string.h>

#define VW 320                 /* canonical game frame (same as the Falcon backend) */
#define VH 240
#define VWPL (VW / 16)          /* words per line per plane = 20 */

/* chunky 8-bit -> VDI STANDARD-format 8-plane bitmap. Standard form is WHOLE-BITMAP plane-sequential:
 * all (VH*VWPL) words of plane 0, then plane 1, ... (device-independent; vr_trnfm turns it into the
 * card's real format). Local + portable C — the VDI blit dominates, so this is fine. */
static void c2p_frame(const uint8_t *src, uint16_t *dst)
{
    const int32_t plane_words = (int32_t)VH * VWPL;          /* words per plane for the whole frame */
    for (int y = 0; y < VH; y++) {
        const uint8_t *sp = src + (int32_t)y * VW;
        for (int g = 0; g < VWPL; g++) {
            uint16_t plane[8] = {0,0,0,0,0,0,0,0};
            const uint8_t *px = sp + g * 16;
            for (int b = 0; b < 16; b++) {
                uint16_t bit = (uint16_t)(0x8000u >> b);
                uint8_t v = px[b];
                for (int p = 0; p < 8; p++) if (v & (1u << p)) plane[p] |= bit;
            }
            int32_t woff = (int32_t)y * VWPL + g;             /* word offset within a plane */
            for (int p = 0; p < 8; p++) dst[p * plane_words + woff] = plane[p];
        }
    }
}

#define VPLANES 8              /* 8-bit indexed backbuffer -> 8 standard planes      */

/* Static buffers: a graphics-card machine has ample RAM, and this keeps the backend free of the
 * pool allocator (so it runs standalone under GEM/MiNT for the vcard test). ~150 KB BSS. */
static uint8_t   g_back[VW * VH];      /* chunky 8-bit backbuffer                      */
static uint8_t   g_planar[VW * VH];    /* 8-plane STANDARD-format c2p output           */
static uint8_t   g_device[VW * VH * 4];/* DEVICE-format copy (vr_trnfm target; up to 32bpp) */
static short     g_handle;             /* our VDI virtual workstation                  */
static int       g_have_eddi;          /* EdDI cookie present (card / extended VDI)     */
static int       g_scr_w, g_scr_h;     /* current card resolution                       */
static int       g_ox, g_oy;           /* top-left where the 320x240 frame is centred   */
static MFDB      g_src, g_dev, g_dst;   /* standard c2p / device (vr_trnfm) / screen MFDBs */
static int       g_ready;

/* ---- direct VDI calls via the vdi() trap --------------------------------------------
 * libgem's vs_color (and friends) fault here (bus error), so we drive the VDI parameter block
 * ourselves for the few opcodes we need. */
static short vc_control[12], vc_intin[8], vc_ptsin[8], vc_intout[128], vc_ptsout[128];
static VDIPB vc_pb = { vc_control, vc_intin, vc_ptsin, vc_intout, vc_ptsout };
static void vdi_set_color(short idx, short r, short g, short b)
{
    vc_control[0] = 14;         /* vs_color */
    vc_control[1] = 0; vc_control[3] = 4; vc_control[5] = 0; vc_control[6] = g_handle;
    vc_intin[0] = idx; vc_intin[1] = r; vc_intin[2] = g; vc_intin[3] = b;
    vdi(&vc_pb);
}
static void vdi_clip_off(void)      /* opcode 129 (vs_clip), flag 0 = clipping OFF */
{
    vc_control[0] = 129;
    vc_control[1] = 2; vc_control[3] = 1; vc_control[5] = 0; vc_control[6] = g_handle;
    vc_intin[0] = 0;
    vc_ptsin[0] = 0; vc_ptsin[1] = 0; vc_ptsin[2] = 32767; vc_ptsin[3] = 32767;
    vdi(&vc_pb);
}
/* (a solid-rectangle "does any VDI draw reach the screen?" diagnostic lived here during bring-up;
 * removed now that the full c2p->vr_trnfm->vro_cpyfm present is validated on the card.) */
static void set_mfdb(int i7, const MFDB *m)   /* stash a 32-bit MFDB ptr in contrl[i7],[i7+1] */
{
    uint32_t p = (uint32_t)(uintptr_t)m;
    vc_control[i7]   = (short)(p >> 16);
    vc_control[i7+1] = (short)(p & 0xFFFF);
}
/* vr_trnfm (opcode 110): convert the STANDARD bitmap g_src into the card's DEVICE format g_dev. */
static void vdi_trnfm(void)
{
    vc_control[0] = 110; vc_control[1] = 0; vc_control[3] = 0; vc_control[5] = 0; vc_control[6] = g_handle;
    set_mfdb(7, &g_src); set_mfdb(9, &g_dev);
    vdi(&vc_pb);
}
/* vro_cpyfm (opcode 109): copy g_dev (device format) -> screen, S_ONLY. */
static void vdi_cpyfm(short *pxy)
{
    vc_control[0] = 109; vc_control[1] = 4; vc_control[3] = 1; vc_control[5] = 0; vc_control[6] = g_handle;
    set_mfdb(7, &g_dev); set_mfdb(9, &g_dst);
    vc_intin[0] = 3;                           /* S_ONLY (replace) */
    for (int k = 0; k < 8; k++) vc_ptsin[k] = pxy[k];
    vdi(&vc_pb);
}

/* ---- interface: init / shutdown ------------------------------------------ */
bool rdr_init(const platform_caps_t *caps)
{
    (void)caps;
    if (appl_init() < 0) return false;              /* need the AES up (card runs under GEM) */

    long val;
    g_have_eddi = (Getcookie(C_EdDI, &val) == C_FOUND);

    short wc, wh, ww, wb;
    g_handle = graf_handle(&wc, &wh, &ww, &wb);
    short work_in[16], work_out[64];
    work_in[0] = (short)(Getrez() + 2);             /* current VDI device id */
    for (int i = 1; i < 10; i++) work_in[i] = 1;
    work_in[10] = 2;                                /* raster coordinates */
    v_opnvwk(work_in, &g_handle, work_out);
    if (g_handle == 0) { appl_exit(); return false; }
    vdi_clip_off();                                 /* full-frame blits must not be clipped */

    /* Take the screen from the (multitasking) AES: under XaAES/GEM another app owns the display and
     * redraws it, so a bare blit to the physical screen is overdrawn. Lock screen updates + mouse
     * control and hide the cursor — the game runs fullscreen, like the other backends. */
    wind_update(BEG_UPDATE);
    wind_update(BEG_MCTRL);
    graf_mouse(M_OFF, 0L);

    g_scr_w = work_out[0] + 1;
    g_scr_h = work_out[1] + 1;
    if (g_scr_w < VW || g_scr_h < VH) { v_clsvwk(g_handle); appl_exit(); return false; }
    g_ox = (g_scr_w - VW) / 2;                      /* centre the 320x240 frame */
    g_oy = (g_scr_h - VH) / 2;

    for (int32_t i = 0; i < (int32_t)VW * VH; i++) { g_back[i] = 0; g_planar[i] = 0; }

    /* source MFDB: our 8-plane STANDARD-format bitmap (c2p output) */
    g_src.fd_addr    = g_planar;
    g_src.fd_w       = VW;
    g_src.fd_h       = VH;
    g_src.fd_wdwidth = VW / 16;                     /* 20 words per plane-row */
    g_src.fd_stand   = 1;                           /* 1 = standard (device-independent) form */
    g_src.fd_nplanes = VPLANES;
    g_src.fd_r1 = g_src.fd_r2 = g_src.fd_r3 = 0;
    /* device MFDB: vr_trnfm converts the standard bitmap into this (the card's real format), which
     * vro_cpyfm then blits to the screen (a standard-format source alone does NOT blit). */
    g_dev = g_src;
    g_dev.fd_addr  = g_device;
    g_dev.fd_stand = 0;                             /* 0 = device-specific form */
    /* dest MFDB: fd_addr = 0 => the physical screen; the VDI fills the geometry itself */
    memset(&g_dst, 0, sizeof g_dst);

    g_ready = 1;
    return true;
}

void rdr_shutdown(void)
{
    graf_mouse(M_ON, 0L);
    wind_update(END_MCTRL);
    wind_update(END_UPDATE);
    if (g_handle) v_clsvwk(g_handle);
    appl_exit();
    g_ready = 0;
}

void rdr_get_caps(rdr_caps *o) { o->width = VW; o->height = VH; o->colors = 256; o->indexed = true; }
int  rdr_width(void)  { return VW; }
int  rdr_height(void) { return VH; }
uint8_t *rdr_backbuffer(void) { return g_back; }

/* ---- palette: load the VDI logical colour table (index -> RGB for the blit) --------
 * U6 channels are 6-bit (0..63); the VDI wants 0..1000 per channel. vro_cpyfm of our 8-bit
 * source uses this table to produce the card's true colours. (vdi_set_color is above.) */
void rdr_set_palette(const uint8_t *rgb6, int n)
{
    if (n > 256) n = 256;
    for (int i = 0; i < n; i++)
        vdi_set_color((short)i,
                      (short)(((int)(rgb6[i*3+0] & 0x3F) * 1000) / 63),
                      (short)(((int)(rgb6[i*3+1] & 0x3F) * 1000) / 63),
                      (short)(((int)(rgb6[i*3+2] & 0x3F) * 1000) / 63));
}

/* ---- present: c2p -> vr_trnfm (device format) -> vro_cpyfm centred to the screen ---- */
static void blit_rect(int x, int y, int w, int h)
{
    if (!g_ready) return;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x + w > VW) w = VW - x;
    if (y + h > VH) h = VH - y;
    if (w <= 0 || h <= 0) return;
    short pxy[8];
    pxy[0] = (short)x;          pxy[1] = (short)y;              /* source rect in the device bitmap */
    pxy[2] = (short)(x + w - 1);pxy[3] = (short)(y + h - 1);
    pxy[4] = (short)(g_ox + x); pxy[5] = (short)(g_oy + y);     /* dest rect on the screen          */
    pxy[6] = (short)(g_ox + x + w - 1); pxy[7] = (short)(g_oy + y + h - 1);
    vdi_cpyfm(pxy);
}

/* build the device bitmap from a full VW-wide, VH-tall chunky source (c2p -> standard -> device) */
static void build_device(const uint8_t *src)
{
    c2p_frame(src, (uint16_t *)g_planar);
    vdi_trnfm();
}

void rdr_present(void)                       { build_device(g_back); blit_rect(0, 0, VW, VH); }
void rdr_present_rect(int x, int y, int w, int h) { build_device(g_back); blit_rect(x, y, w, h); }

/* On a card the frame is shown 1:1 (no TT-style vertical doubling); present the top `lh` rows. */
void rdr_present_doubled(int lh)
{
    if (lh < 1) lh = 1;
    if (lh > VH) lh = VH;
    build_device(g_back);
    blit_rect(0, 0, VW, lh);
}
void rdr_present_rect_doubled(int x, int y, int w, int h, int lh) { (void)lh; rdr_present_rect(x, y, w, h); }

/* ---- no-ops on a 256/truecolour card (the shifter-only / HW-scroll features) ---------- */
void rdr_set_lowcolor(const uint8_t *pal16, const uint8_t *lut256) { (void)pal16; (void)lut256; }
void rdr_lowcolor_cycle(const uint8_t *pal16) { (void)pal16; }
void rdr_set_ste_mode(int on) { (void)on; }
int  rdr_is_ste(void) { return 0; }
void rdr_set_lowcolor_bands(int nb, const uint8_t *y0, const uint8_t *pal, const uint8_t *lut)
{ (void)nb; (void)y0; (void)pal; (void)lut; }
void rdr_prefer_truecolor(int on) { (void)on; }           /* the card mode is user-set, not ours */
void rdr_flip_begin(void) { }
void rdr_flip_end(void)   { }
bool scroll_begin(const uint8_t *chunky, int cw, int ch) { (void)chunky; (void)cw; (void)ch; return false; }
void scroll_pan(int pan_x) { (void)pan_x; }
void scroll_end(void) { }
void rdr_test_hscroll(int v) { (void)v; }

/* Incremental scroll: no HW pan through VDI — c2p the whole visible window and blit (the CPU
 * fallback the engine expects when scroll_begin() returned false). */
void rdr_scroll_left16(const uint8_t *chunky, int cw, int lh, int pan_x)
{
    if (lh < 1) lh = 1;
    if (lh > VH) lh = VH;
    if (pan_x < 0) pan_x = 0;
    for (int y = 0; y < lh; y++)          /* copy the VW-wide window into the backbuffer, then blit */
        memcpy(g_back + (int32_t)y * VW, chunky + (int32_t)y * cw + pan_x, VW);
    build_device(g_back);
    blit_rect(0, 0, VW, lh);
}

void rdr_debug_info(uint32_t out[6])
{
    out[0] = (uint32_t)g_scr_w;
    out[1] = (uint32_t)g_scr_h;
    out[2] = (uint32_t)(uintptr_t)g_planar;
    out[3] = (uint32_t)g_have_eddi;
    out[4] = (uint32_t)(uint16_t)g_handle;
    out[5] = (uint32_t)VW * VH;
}
