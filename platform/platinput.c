// SPDX-License-Identifier: GPL-2.0-or-later
/* platinput.c — keyboard polling via BIOS console.
 *
 * TOS calls (named for verification):
 *   Bconstat(2) : BIOS 1 — nonzero if a CON char is waiting (non-blocking).
 *   Bconin(2)   : BIOS 2 — read it: bits 16-23 = IKBD scancode, bits 0-7 = ASCII.
 * Arrow scancodes: up 0x48, down 0x50, left 0x4B, right 0x4D.
 */
#include "platinput.h"
#include <osbind.h>
#include <mint/linea.h>   /* GCURX/GCURY/MOUSE_BT line-A globals */

void plat_input_init(void)
{
    /* Keyboard auto-repeat. XBIOS 35 Kbrate(initial, repeat) — both in VBL ticks
     * (~1/50 s). The INITIAL delay must be LONGER than a normal keypress, or an
     * ordinary tap auto-repeats and every menu choice / command / single step
     * double-fires (reported on real HW). ~300 ms initial means a tap = one event,
     * while deliberately HOLDING an arrow (>300 ms) still auto-walks (~60 ms/step). */
    Kbrate(15, 3);
}

int plat_input_raw(void)
{
    if (!Bconstat(2)) return 0;                /* no key waiting */
    long k  = Bconin(2);
    int  sc = (int)((k >> 16) & 0xFF);         /* scancode */
    int  a  = (int)(k & 0xFF);                 /* ASCII    */
    return (sc << 8) | a;
}

plat_input_t plat_input_map(int key)
{
    int sc = (key >> 8) & 0xFF;                /* scancode */
    int a  = key & 0xFF;                       /* ASCII    */
    if (!key) return IN_NONE;

    /* Movement is the ARROW KEYS (authentic U6; the numeric keypad too — TODO).
     * NO WASD: that convention postdates U6 by ~15 years AND U6 is keyboard-driven
     * with single-letter COMMANDS — A)ttack, G)et, K)limb, L)ook, S)ave, U)se, ... —
     * so hijacking letters for movement would clobber the command keys. Letters are
     * reserved for commands (only T)alk + Esc wired so far). */
    switch (sc) {
    case 0x48: return IN_UP;
    case 0x50: return IN_DOWN;
    case 0x4B: return IN_LEFT;
    case 0x4D: return IN_RIGHT;
    default: break;
    }
    switch (a) {
    case 't': case 'T': return IN_TALK;   /* U6 'T'alk command */
    case 27:            return IN_QUIT;   /* Esc: the game asks before it quits */
    default: return IN_NONE;
    }
}

plat_input_t plat_input_poll(void)
{
    return plat_input_map(plat_input_raw());
}

/* ---- IKBD mouse hook (mousevec) ----------------------------------------------------------------
 * The pointer used to come from line-A's GCURX/GCURY, which TOS's own mouse handler keeps up to date —
 * but a replacement VDI (NVDI, on the user's Falcon) keeps its mouse position elsewhere, so there the
 * pointer never moved ("no mouse on the main menu"). We now take the IKBD relative-mouse packets
 * ourselves: XBIOS 34 Kbdvbase() yields the vector table; our handler gets A0 = the 3-byte packet
 * ($F8|buttons, signed dx, signed dy; IKBD: bit 1 = left button, bit 0 = right), accumulates the
 * position in PHYSICAL screen pixels clamped to the size the renderer declared, and ends with RTS
 * (no chaining: the AES pointer is hidden while we run; the old vector returns on shutdown, which is
 * also registered with atexit). XBIOS 38 Supexec writes the vector in supervisor mode. The handler
 * runs at interrupt level: the stub saves d0-d2/a0-a2 around the C routine, no OS calls inside. */
#include <mint/ostruct.h>
#include <stdlib.h>
#include <stdint.h>
static volatile int16_t s_mx, s_my;   /* position, physical pixels */
static volatile uint8_t s_mb;         /* buttons in line-A order: bit0 left, bit1 right */
static int16_t s_mw, s_mh; static int s_hooked;
static _KBDVECS *s_kv; static void (*s_old_mousevec)(void *);
void plat_mouse_packet_c(const uint8_t *p)
{
    int x = s_mx + (int)(int8_t)p[1], y = s_my + (int)(int8_t)p[2];
    if (x < 0) x = 0;
    if (x >= s_mw) x = s_mw - 1;
    if (y < 0) y = 0;
    if (y >= s_mh) y = s_mh - 1;
    s_mx = (int16_t)x; s_my = (int16_t)y;
    s_mb = (uint8_t)(((p[0] & 2) ? 1 : 0) | ((p[0] & 1) ? 2 : 0));
}
__asm__(
"\t.text\n"
"\t.globl _plat_mouse_stub\n"
"_plat_mouse_stub:\n"
"\tmovem.l d0-d2/a0-a2,-(sp)\n"
"\tmove.l a0,-(sp)\n"
"\tjsr _plat_mouse_packet_c\n"
"\taddq.l #4,sp\n"
"\tmovem.l (sp)+,d0-d2/a0-a2\n"
"\trts\n");
extern void plat_mouse_stub(void *);
static long mouse_hook_set(void){ s_old_mousevec = s_kv->mousevec; s_kv->mousevec = plat_mouse_stub; return 0; }
static long mouse_hook_clr(void){ s_kv->mousevec = s_old_mousevec; return 0; }
void plat_mouse_hook_shutdown(void)
{
    if (!s_hooked) return;
    Supexec(mouse_hook_clr); s_hooked = 0;
}
void plat_mouse_hook_init(int phys_w, int phys_h)
{
    if (s_hooked || phys_w <= 0 || phys_h <= 0) return;
    s_mw = (int16_t)phys_w; s_mh = (int16_t)phys_h;
    s_mx = (int16_t)(phys_w / 2); s_my = (int16_t)(phys_h / 2); s_mb = 0;   /* the pointer starts at the screen's centre */
    s_kv = Kbdvbase(); if (!s_kv) return;
    Supexec(mouse_hook_set); s_hooked = 1;
    atexit(plat_mouse_hook_shutdown);                     /* never leave our vector behind */
}
void plat_mouse(int *x, int *y, int *buttons)
{
    if (s_hooked) { if(x)*x=s_mx; if(y)*y=s_my; if(buttons)*buttons=s_mb; return; }
    if (!__aline) { if(x)*x=0; if(y)*y=0; if(buttons)*buttons=0; return; }
    if (x)       *x = GCURX;       /* line-A graphics-cursor X (current mode px) */
    if (y)       *y = GCURY;       /* line-A graphics-cursor Y                   */
    if (buttons) *buttons = MOUSE_BT & 3;   /* bit0 left, bit1 right            */
}
