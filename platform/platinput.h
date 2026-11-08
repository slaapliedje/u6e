// SPDX-License-Identifier: GPL-2.0-or-later
/* platinput.h — normalized input events for /engine (M5).
 *
 * The engine never reads the keyboard directly; it polls this. M5 needs only
 * the movement directions + quit; richer events (mouse, action keys) get added
 * as the UI grows. Non-blocking: returns IN_NONE when no key is waiting.
 */
#ifndef U6E_PLATINPUT_H
#define U6E_PLATINPUT_H

typedef enum {
    IN_NONE = 0,
    IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT,
    IN_UPLEFT, IN_UPRIGHT, IN_DOWNLEFT, IN_DOWNRIGHT,   /* the keypad's 7 9 1 3 (and Clr/Home): DOS walks the diagonals */
    IN_TALK,          /* 'T': talk to an adjacent NPC */
    IN_QUIT
} plat_input_t;

/* One-time input setup: fast keyboard auto-repeat (Kbrate) for smooth held-key
 * movement. Call once at startup. */
void         plat_input_init(void);

plat_input_t plat_input_poll(void);

/* Raw non-blocking key read for text entry (conversation keyword/number typing):
 * returns 0 if no key is waiting, else (scancode << 8) | ascii, | PLAT_KEY_ALT while Alternate is held. The caller picks
 * interpretation by mode — map it to a movement event with plat_input_map(), or
 * read the low byte as the typed character. */
#define PLAT_KEY_ALT 0x10000   /* in a raw key: Alternate was held (Alt+letter picks a default command) */
int          plat_input_raw(void);
plat_input_t plat_input_map(int key);   /* classify a raw key as a movement event */

/* Mouse state from the line-A globals (the system tracks the IKBD position even
 * with the sprite hidden). *x,*y are in physical screen pixels; *buttons bit0 =
 * left, bit1 = right. Requires line-A init (done by the renderer). */
void         plat_mouse(int *x, int *y, int *buttons);
/* IKBD mouse hook: take the relative-mouse packets ourselves (XBIOS 34 Kbdvbase mousevec) so the
 * pointer works on every TOS and replacement VDI (NVDI keeps its own position, not line-A's). The
 * renderer calls init with the PHYSICAL mode size right after it set the mode; shutdown restores the
 * vector (also registered with atexit). plat_mouse then reports the hook's position; line-A stays
 * the fallback when no hook is installed. */
void         plat_mouse_hook_init(int phys_w, int phys_h);
void         plat_mouse_hook_shutdown(void);

#endif
