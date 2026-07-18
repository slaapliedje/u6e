// SPDX-License-Identifier: GPL-2.0-or-later
/* rdr_vcard.c — graphics-card renderer backend via the VDI (M8, goals #2/#3).
 *
 * Unlike the direct-hardware ST/TT/Falcon backends, a graphics card is reached THROUGH GEM/VDI: the
 * card ships a VDI driver (fVDI / NVDI) and we present into whatever mode the user has set (we do NOT
 * change the card's resolution — see render/CLAUDE.md). We compose into our own canonical 8-bit
 * chunky backbuffer exactly like the other backends, then present it with a single VDI raster copy.
 * We ask vq_scrninfo for the card's real depth + pixel layout and pick one of two present paths:
 *   INDEXED (<=8bpp): c2p the backbuffer to an 8-plane bitmap, vr_trnfm it to the driver's device
 *     form, then vro_cpyfm to the screen; the VDI colour table (vs_color) maps our indices.
 *   TRUECOLOUR (16/24/32bpp): expand each index through the card's exact bit layout to a CHUNKY
 *     device pixel, then vro_cpyfm that to the screen (the driver reads a 16/24/32-plane source as
 *     chunky). No hard-coded masks — the layout comes from vq_scrninfo. See docs/formats/vdi_scrninfo.md.
 * Never per-tile VDI ops (project rule). The 320x240 frame is shown 1:1, centred on the card screen.
 *
 * AES/VDI calls (named for verification): appl_init/appl_exit (AES 10/19), graf_handle (AES 77),
 * v_opnvwk/v_clsvwk (VDI 100/101), vs_color (VDI 14), vq_scrninfo (VDI 102 sub 1), vr_trnfm (VDI 110),
 * vro_cpyfm (VDI 109), Getcookie('EdDI'). Validation rig: ARAnyM+fVDI+AFROS, docs/design/vcard-aranym-rig.md.
 */
#include "render.h"

#include <gem.h>
#include <osbind.h>
#include <mint/cookie.h>
#include <mint/linea.h>       /* line-A VPLANES: real screen depth (indexed vs truecolour) */
#include <string.h>
#include "platmem.h"          /* plat_alloc_fast — present buffers come from the pool, not BSS */

#define RDR_BACKEND vcard     /* namespace this backend's entry points; emit rdr_be_vcard (below) */
#include "render_backend.h"

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

#define SRC_PLANES 8              /* 8-bit indexed backbuffer -> 8 standard planes      */

/* Present buffers (~460 KB) — allocated from the pool in rdr_init, NOT static: in the unified binary
 * only the SELECTED backend should spend RAM, and 460 KB of BSS on every build would tip the tight TT
 * pack pool into "no renderer". A card machine has the RAM; plat_alloc_fast (Alt-RAM preferred) is
 * fine since these are VDI source rasters, not shifter-DMA'd. Freed implicitly at exit (like rdr_tt). */
static uint8_t  *g_back;               /* chunky 8-bit backbuffer                      */
static uint8_t  *g_planar;             /* 8-plane STANDARD-format c2p output           */
static uint8_t  *g_device;             /* DEVICE-format copy (vr_trnfm target; up to 32bpp) */
int              rdr_vcard_force_depth = 0;  /* test hook: override detected screen bpp (0 = auto) */
static int       g_bpp;                /* screen depth: 8 indexed, 15/16/24/32 truecolour (vq_scrninfo) */
static int       g_truecolor;          /* screen is direct-colour (vq_scrninfo clut==2 / bit_depth>8)   */
static uint32_t  g_lut[256];           /* index -> device pixel (truecolour expand)     */
/* Truecolour pixel layout, read from vq_scrninfo so it matches ANY card/config with no hard-coded
 * masks: g_ch_bits[ch] = bits per channel (R,G,B), g_ch_pos[ch][j] = the SCREEN bit position of that
 * channel's bit j (j=0 = LSB). See docs/formats/vdi_scrninfo.md. */
static int       g_ch_bits[3];
static uint8_t   g_ch_pos[3][8];
static short     g_handle;             /* our VDI virtual workstation                  */
static int       g_have_eddi;          /* EdDI cookie present (card / extended VDI)     */
static int       g_scr_w, g_scr_h;     /* current card resolution                       */
static int       g_ox, g_oy;           /* top-left where the 320x240 frame is centred   */
static MFDB      g_src, g_dev, g_dst;   /* standard c2p / device (vr_trnfm) / screen MFDBs */
static int       g_ready;

/* ---- direct VDI calls via the vdi() trap --------------------------------------------
 * libgem's vs_color (and friends) fault here (bus error), so we drive the VDI parameter block
 * ourselves for the few opcodes we need. */
static short vc_control[12], vc_intin[8], vc_ptsin[8], vc_intout[288], vc_ptsout[128];
static VDIPB vc_pb = { vc_control, vc_intin, vc_ptsin, vc_intout, vc_ptsout };
/* vq_scrninfo (opcode 102, sub 1, flag 2): returns the Device struct (272 words) describing the
 * card's EXACT pixel format — the authoritative depth + truecolour bit layout. See
 * docs/formats/vdi_scrninfo.md for the word offsets. */
static void vdi_scrninfo(short *out)
{
    vc_control[0] = 102; vc_control[1] = 0; vc_control[3] = 1; vc_control[5] = 1; vc_control[6] = g_handle;
    vc_intin[0] = 2;
    vc_pb.intout = out;                 /* Device struct is 272 words — its own buffer */
    vdi(&vc_pb);
    vc_pb.intout = vc_intout;
}
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

/* ---- truecolour pixel layout ---------------------------------------------
 * Fall back to the "usual bit order" layout (565 / 555 / 0x00RRGGBB, red in the high bits) when
 * vq_scrninfo is unavailable — this matches every ordinary fVDI/NVDI mode. */
static void tc_default_layout(int bpp)
{
    int i;
    if (bpp >= 24) {                                /* 8-8-8, RGB in the low 24 bits */
        g_ch_bits[0] = g_ch_bits[1] = g_ch_bits[2] = 8;
        for (i = 0; i < 8; i++) { g_ch_pos[0][i] = (uint8_t)(16 + i);
                                  g_ch_pos[1][i] = (uint8_t)(8 + i);
                                  g_ch_pos[2][i] = (uint8_t)i; }
    } else if (bpp == 16) {                         /* RGB 565 */
        g_ch_bits[0] = 5; g_ch_bits[1] = 6; g_ch_bits[2] = 5;
        for (i = 0; i < 5; i++) g_ch_pos[0][i] = (uint8_t)(11 + i);
        for (i = 0; i < 6; i++) g_ch_pos[1][i] = (uint8_t)(5 + i);
        for (i = 0; i < 5; i++) g_ch_pos[2][i] = (uint8_t)i;
    } else {                                        /* 15: RGB 555 */
        g_ch_bits[0] = g_ch_bits[1] = g_ch_bits[2] = 5;
        for (i = 0; i < 5; i++) { g_ch_pos[0][i] = (uint8_t)(10 + i);
                                  g_ch_pos[1][i] = (uint8_t)(5 + i);
                                  g_ch_pos[2][i] = (uint8_t)i; }
    }
}

/* Assemble a device pixel from 8-bit R/G/B using the reported bit layout (see docs). */
static uint32_t tc_pixel(int r8, int g8, int b8)
{
    int v[3] = { r8, g8, b8 };
    uint32_t px = 0;
    for (int ch = 0; ch < 3; ch++) {
        int nb = g_ch_bits[ch];
        if (nb <= 0) continue;
        int cv = v[ch] >> (8 - nb);                 /* top nb bits of the channel */
        for (int j = 0; j < nb; j++)
            if (cv & (1 << j)) px |= (uint32_t)1u << g_ch_pos[ch][j];
    }
    return px;
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

    /* Present buffers from the pool (see the decl above). Do this before taking the screen so a
     * failure unwinds cleanly. plat_alloc_fast needs plat_mem_init() first — the game does that at
     * startup; the standalone vcardrender harness calls it too. */
    g_back   = (uint8_t *)plat_alloc_fast((uint32_t)VW * VH);
    g_planar = (uint8_t *)plat_alloc_fast((uint32_t)VW * VH);
    g_device = (uint8_t *)plat_alloc_fast((uint32_t)VW * VH * 4);
    if (!g_back || !g_planar || !g_device) { v_clsvwk(g_handle); appl_exit(); return false; }

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

    /* Screen depth + truecolour pixel layout. v_opnvwk work_out + vq_extnd both report 256 colours
     * on a card and line-A VPLANES misreports a truecolour screen as 8 planes, so ask vq_scrninfo:
     * bit_depth is authoritative and (for clut==2 / bit_depth>8) it hands us the exact per-channel
     * bit positions for the expand. See docs/formats/vdi_scrninfo.md. */
    g_truecolor = 0;
    g_ch_bits[0] = g_ch_bits[1] = g_ch_bits[2] = 0;
    if (g_have_eddi) {
        static short si[288];
        vdi_scrninfo(si);
        short clut = si[1], depth = si[2];          /* si[0]=format, si[1]=clut, si[2]=bit_depth */
        if (depth == 8 || depth == 15 || depth == 16 || depth == 24 || depth == 32) {
            g_bpp = depth;
            g_truecolor = (clut == 2) || (depth > 8);
            if (g_truecolor) {
                g_ch_bits[0] = si[8]; g_ch_bits[1] = si[9]; g_ch_bits[2] = si[10];
                for (int j = 0; j < g_ch_bits[0] && j < 8; j++) g_ch_pos[0][j] = (uint8_t)si[16 + j];
                for (int j = 0; j < g_ch_bits[1] && j < 8; j++) g_ch_pos[1][j] = (uint8_t)si[32 + j];
                for (int j = 0; j < g_ch_bits[2] && j < 8; j++) g_ch_pos[2][j] = (uint8_t)si[48 + j];
            }
        } else {
            linea0(); g_bpp = VPLANES;               /* scrninfo gave junk — fall back to line-A */
        }
    } else {
        linea0(); g_bpp = VPLANES;                    /* no EdDI: base VDI, indexed only */
    }
    if (rdr_vcard_force_depth) g_bpp = rdr_vcard_force_depth;   /* test hook */
    if (g_bpp != 8 && g_bpp != 15 && g_bpp != 16 && g_bpp != 24 && g_bpp != 32) g_bpp = 8;
    if (g_bpp > 8) g_truecolor = 1;
    /* If truecolour but vq_scrninfo gave no layout (forced depth, or older driver), assume the usual
     * 565/555/888 order — correct for every ordinary fVDI/NVDI mode. */
    if (g_truecolor && g_ch_bits[0] == 0) tc_default_layout(g_bpp);

    for (int32_t i = 0; i < (int32_t)VW * VH; i++) { g_back[i] = 0; g_planar[i] = 0; }

    /* source MFDB: our 8-plane STANDARD-format bitmap (c2p output) */
    g_src.fd_addr    = g_planar;
    g_src.fd_w       = VW;
    g_src.fd_h       = VH;
    g_src.fd_wdwidth = VW / 16;                     /* 20 words per plane-row */
    g_src.fd_stand   = 1;                           /* 1 = standard (device-independent) form */
    g_src.fd_nplanes = SRC_PLANES;
    g_src.fd_r1 = g_src.fd_r2 = g_src.fd_r3 = 0;
    /* device MFDB. INDEXED: vr_trnfm converts the standard bitmap into this 8-plane device form.
     * TRUECOLOUR: we expand our indices straight into g_device as chunky device pixels and vro_cpyfm
     * that to the screen. fd_nplanes = real bpp (the driver reads 16/24/32-plane sources as chunky).
     * fd_wdwidth STAYS VW/16 (=20) at every depth: the driver's byte pitch = fd_wdwidth*nplanes*2, so
     * it must be the per-plane word width, NOT scaled by bpp (that was the garble). See docs. */
    g_dev = g_src;
    g_dev.fd_addr    = g_device;
    g_dev.fd_stand   = 0;                           /* 0 = device-specific form */
    if (g_truecolor) g_dev.fd_nplanes = (short)g_bpp;
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
    for (int i = 0; i < n; i++) {
        int r6 = rgb6[i*3+0] & 0x3F, g6 = rgb6[i*3+1] & 0x3F, b6 = rgb6[i*3+2] & 0x3F;
        if (!g_truecolor)
            /* INDEXED: load the VDI colour table (vro_cpyfm maps our indices through it). */
            vdi_set_color((short)i, (short)(r6*1000/63), (short)(g6*1000/63), (short)(b6*1000/63));
        else
            /* TRUECOLOUR: precompute the device pixel (6-bit -> 8-bit channels, then the card's
             * exact bit layout from vq_scrninfo — no hard-coded 565/888 masks). */
            g_lut[i] = tc_pixel(r6*255/63, g6*255/63, b6*255/63);
    }
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

/* build the device bitmap (g_dev) from a full VW-wide, VH-tall chunky source.
 * INDEXED card (<=8bpp): c2p to a VDI standard 8-plane bitmap, then vr_trnfm to the 8-plane device
 * form (the driver reads an 8-plane source as bitplanes and converts to chunky; a naked standard
 * source alone won't blit).
 * TRUECOLOUR card (16/24/32bpp): expand each index to its device pixel (g_lut) as a CHUNKY raster —
 * the driver reads a 16/24/32-plane source as chunky (2/3/4 bytes per pixel). Row stride is the
 * natural pixels*bpp/8; the source MFDB's fd_wdwidth stays VW/16 (the driver scales by nplanes*2). */
static void build_device(const uint8_t *src)
{
    if (!g_truecolor) {
        c2p_frame(src, (uint16_t *)g_planar);
        vdi_trnfm();
        return;
    }
    int32_t npix = (int32_t)VW * VH;
    if (g_bpp == 32) {
        uint32_t *d = (uint32_t *)g_device;
        for (int32_t i = 0; i < npix; i++) d[i] = g_lut[src[i]];
    } else if (g_bpp == 24) {                        /* 3 bytes/pixel, R,G,B big-endian */
        uint8_t *d = g_device;
        for (int32_t i = 0; i < npix; i++) {
            uint32_t px = g_lut[src[i]];
            *d++ = (uint8_t)(px >> 16); *d++ = (uint8_t)(px >> 8); *d++ = (uint8_t)px;
        }
    } else {                                         /* 15/16-bit, 2 bytes/pixel */
        uint16_t *d = (uint16_t *)g_device;
        for (int32_t i = 0; i < npix; i++) d[i] = (uint16_t)g_lut[src[i]];
    }
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
    out[2] = (uint32_t)g_bpp;              /* detected screen depth (vq_scrninfo bit_depth) */
    out[3] = (uint32_t)g_have_eddi;
    out[4] = (uint32_t)(uint16_t)g_handle;
    /* truecolour flag + per-channel bit counts (R,G,B), so the harness can verify the layout */
    out[5] = ((uint32_t)g_truecolor << 24) | ((uint32_t)(g_ch_bits[0] & 0xFF) << 16)
           | ((uint32_t)(g_ch_bits[1] & 0xFF) << 8) | (uint32_t)(g_ch_bits[2] & 0xFF);
}

RDR_MAKE_VTABLE(vcard);       /* -> const rdr_vtable rdr_be_vcard */
