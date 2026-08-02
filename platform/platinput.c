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
    case 27:            return IN_QUIT;   /* Esc exits the demo (dev convenience) */
    default: return IN_NONE;
    }
}

plat_input_t plat_input_poll(void)
{
    return plat_input_map(plat_input_raw());
}

void plat_mouse(int *x, int *y, int *buttons)
{
    if (!__aline) { if(x)*x=0; if(y)*y=0; if(buttons)*buttons=0; return; }
    if (x)       *x = GCURX;       /* line-A graphics-cursor X (current mode px) */
    if (y)       *y = GCURY;       /* line-A graphics-cursor Y                   */
    if (buttons) *buttons = MOUSE_BT & 3;   /* bit0 left, bit1 right            */
}
