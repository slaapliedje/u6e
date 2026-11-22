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
#include <stdint.h>
#include <stdlib.h>
#include <mint/linea.h>   /* GCURX/GCURY/MOUSE_BT line-A globals */

static uint8_t s_conterm;
static long conterm_click_off(void){ volatile uint8_t *c=(volatile uint8_t*)0x484; s_conterm=*c; *c=(uint8_t)(s_conterm&~1); return 0; }
static long conterm_put_back(void){ *(volatile uint8_t*)0x484=s_conterm; return 0; }
static void conterm_restore(void){ Supexec(conterm_put_back); }

void plat_input_init(void)
{
    /* Keyboard auto-repeat. XBIOS 35 Kbrate(initial, repeat) — both in VBL ticks
     * (~1/50 s). The INITIAL delay must be LONGER than a normal keypress, or an
     * ordinary tap auto-repeats and every menu choice / command / single step
     * double-fires (reported on real HW). ~300 ms initial means a tap = one event,
     * while deliberately HOLDING an arrow (>300 ms) still auto-walks (~60 ms/step). */
    Kbrate(15, 3);
    /* The key click goes through the sound chip, over the music: TOS's conterm ($484) bit 0 off while
     * we run, as the original ST game does. Supexec (XBIOS 38) reaches it; atexit puts it back. */
    Supexec(conterm_click_off);
    atexit(conterm_restore);
}

static int pad_take(void);
int plat_input_raw(void)
{
    if (!Bconstat(2)) return pad_take();       /* no key waiting: one the Jaguar pad made, if any */
    long k  = Bconin(2);
    int  sc = (int)((k >> 16) & 0xFF);         /* scancode */
    int  a  = (int)(k & 0xFF);                 /* ASCII    */
    int  alt = (Kbshift(-1) & 0x08) ? PLAT_KEY_ALT : 0;   /* BIOS 11 Kbshift(-1): bit 3 = Alternate held */
    return alt | (sc << 8) | a;
}

plat_input_t plat_input_map(int key)
{
    int sc = (key >> 8) & 0xFF;                /* scancode */
    int a  = key & 0xFF;                       /* ASCII    */
    if (!key) return IN_NONE;

    /* Movement is the ARROW KEYS and the numeric keypad, eight ways as DOS has them (Up Down Left Right
     * Home PgUp End PgDn there; the keypad digits and Clr/Home here: docs/rules/movement_turns.md).
     * NO WASD: that convention postdates U6 by ~15 years AND U6 is keyboard-driven
     * with single-letter COMMANDS — A)ttack, G)et, K)limb, L)ook, S)ave, U)se, ... —
     * so hijacking letters for movement would clobber the command keys. Letters are
     * reserved for commands (only T)alk + Esc wired so far). */
    switch (sc) {
    case 0x48: return IN_UP;
    case 0x50: return IN_DOWN;
    case 0x4B: return IN_LEFT;
    case 0x4D: return IN_RIGHT;
    case 0x68: return IN_UP;   case 0x6E: return IN_DOWN;  case 0x6A: return IN_LEFT;  case 0x6C: return IN_RIGHT;   /* keypad 8 2 4 6 */
    case 0x67: case 0x47: return IN_UPLEFT;  case 0x69: return IN_UPRIGHT;                                            /* keypad 7 (Clr/Home) 9 */
    case 0x6D: return IN_DOWNLEFT;            case 0x6F: return IN_DOWNRIGHT;                                          /* keypad 1 3 */
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
static volatile uint8_t s_pad_mb;     /* the Jaguar pad's, the same way (below) */
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
    if (s_hooked) { if(x)*x=s_mx; if(y)*y=s_my; if(buttons)*buttons=s_mb|s_pad_mb; return; }
    if (!__aline) { if(x)*x=0; if(y)*y=0; if(buttons)*buttons=0; return; }
    if (x)       *x = GCURX;       /* line-A graphics-cursor X (current mode px) */
    if (y)       *y = GCURY;       /* line-A graphics-cursor Y                   */
    if (buttons) *buttons = MOUSE_BT & 3;   /* bit0 left, bit1 right            */
}

/* ---- Jaguar pad (docs/design/jagpad.md) --------------------------------------------------------
 * A Jaguar pad in either enhanced joystick port of a 1040 STE or a Falcon plays the game beside the
 * keyboard and mouse. Hardware (Atari Falcon030 HW ref., Addendum 5 §8; the STE has the same ports):
 *   $FF9202 write: drive one column low, bits 0-3 port A, 4-7 port B (the other bits high);
 *   $FF9202 read : the rows pressed in it, low = pressed, bits 8-11 port A, 12-15 port B;
 *   $FF9200 read : low = pressed, bit 0 port A pin 6 (Pause, column 0 only), bit 1 port A pin 10
 *                  (A/B/C/Option by column), bits 2-3 the same for port B.
 * The Mega STe, TT and plain ST have no such ports (a read there is a bus error), so only an STE or
 * Falcon by the _MCH cookie installs the reader. It runs from the VBL queue ($456/_nvbls $454, the
 * slot freed at exit) in supervisor mode, times itself by _hz_200 ($4BA), and puts what it makes
 * where the rest of this file already looks: keys into a queue plat_input_raw drains, the pointer
 * into the mouse hook's position, its buttons beside the mouse's. */
enum { PAD_UP=1<<0, PAD_DOWN=1<<1, PAD_LEFT=1<<2, PAD_RIGHT=1<<3, PAD_A=1<<4, PAD_B=1<<5, PAD_C=1<<6,
       PAD_OPTION=1<<7, PAD_PAUSE=1<<8, PAD_STAR=1<<9, PAD_HASH=1<<10, PAD_K0=1<<11 /* .. K9 = 1<<20 */ };
#define PAD_KEY(n) (PAD_K0<<(n))
#define PAD_DIRS (PAD_UP|PAD_DOWN|PAD_LEFT|PAD_RIGHT)
/* the rows each column gives: the four in $FF9202, then the one in $FF9200 */
static const uint32_t pad_rows[4][5] = {
    { PAD_UP,     PAD_DOWN,   PAD_LEFT,   PAD_RIGHT,  PAD_A      },
    { PAD_STAR,   PAD_KEY(7), PAD_KEY(4), PAD_KEY(1), PAD_B      },
    { PAD_KEY(0), PAD_KEY(8), PAD_KEY(5), PAD_KEY(2), PAD_C      },
    { PAD_HASH,   PAD_KEY(9), PAD_KEY(6), PAD_KEY(3), PAD_OPTION },
};
/* Raw keys, as plat_input_raw returns them: (scancode << 8) | ASCII. The keypad's 1-9 and 0 are the
 * command bar's ten icons in order (A)ttack C)ast T)alk L)ook G)et D)rop M)ove U)se R)est B)attle). */
static const uint16_t pad_cmd[10] = {
    0x3062 /* 0 b */, 0x1E61 /* 1 a */, 0x2E63 /* 2 c */, 0x1474 /* 3 t */, 0x266C /* 4 l */,
    0x2267 /* 5 g */, 0x2064 /* 6 d */, 0x326D /* 7 m */, 0x1675 /* 8 u */, 0x1372 /* 9 r */ };
#define KEY_RETURN 0x1C0D
#define KEY_ESC    0x011B
#define KEY_TAB    0x0F09
#define KEY_SPACE  0x3920
#define KEY_CTRL_S 0x1F13
/* the key each direction pair gives, by up/down/left/right bits: arrows, and the keypad's 7 9 1 3 for
 * the diagonals with no ASCII (so a "How many?" never sees a digit) */
static const uint16_t pad_dir_key[16] = {
    0,      0x4800, 0x5000, 0,      0x4B00, 0x6700, 0x6D00, 0x4B00,
    0x4D00, 0x6900, 0x6F00, 0x4D00, 0,      0x4800, 0x5000, 0 };
/* repeat a held direction as the keyboard does (Kbrate 15/3 above): ~300 ms, then every ~60 ms */
#define PAD_DELAY  60     /* 200 Hz ticks */
#define PAD_REPEAT 12
/* the pointer's pace in game pixels a second: from SLOW, gaining GAIN a second held, up to FAST */
#define PTR_SLOW 60
#define PTR_GAIN 400
#define PTR_FAST 320

static volatile uint16_t s_padq[16];          /* keys made by the VBL, taken by plat_input_raw */
static volatile uint8_t  s_padq_in, s_padq_out;
static uint32_t s_pad_held, s_pad_dir_t, s_pad_ptr_t, s_pad_last_t;
static int      s_pad_ptr_mode;               /* Option: the pad moves the pointer (A held swaps) */
static int32_t  s_pad_cx, s_pad_cy;           /* pointer movement not yet whole pixels (x 1000) */
static void   (**s_pad_vblq)(void);
static int      s_pad_slot = -1;

static void pad_put(uint16_t k)
{
    uint8_t n = (uint8_t)((s_padq_in + 1) & 15);
    if (k && n != s_padq_out) { s_padq[s_padq_in] = k; s_padq_in = n; }
}

/* the buttons held on the pads in both ports, together (a port with nothing in it reads as nothing) */
static uint32_t pad_read(void)
{
    volatile uint16_t *buttons = (volatile uint16_t *)0xFFFF9200UL, *matrix = (volatile uint16_t *)0xFFFF9202UL;
    uint32_t bits = 0;
    for (int col = 0; col < 4; col++) {
        *matrix = (uint16_t)~(0x11 << col);
        uint16_t rows = (uint16_t)~*matrix, but = (uint16_t)~*buttons;
        for (int port = 0; port < 2; port++) {
            unsigned these = (unsigned)(rows >> (8 + port * 4));
            for (int row = 0; row < 4; row++) if (these & (1u << row)) bits |= pad_rows[col][row];
            if (but & (2u << (port * 2))) bits |= pad_rows[col][4];
            if (col == 0 && (but & (1u << (port * 2)))) bits |= PAD_PAUSE;
        }
    }
    *matrix = 0xFFFF;
    return bits;
}

#ifndef U6E_RELEASE
/* PAD.REQ (test builds): a script of "ticks bits" lines (200 Hz ticks from the first VBL, the PAD_
 * bits in hex) replayed in place of the ports, so an emulator run can play the pad */
static uint16_t s_pad_scr_t[64]; static uint32_t s_pad_scr_b[64]; static int s_pad_scr_n, s_pad_scr_i;
static uint32_t s_pad_scr_t0, s_pad_scr_bits; static int s_pad_scr_on;
#endif

static void pad_vbl(void)
{
    uint32_t now = *(volatile uint32_t *)0x4BAUL;        /* _hz_200 */
    uint32_t bits;
#ifndef U6E_RELEASE
    if (s_pad_scr_on) {
        if (!s_pad_scr_t0) s_pad_scr_t0 = now ? now : 1;
        while (s_pad_scr_i < s_pad_scr_n && now - s_pad_scr_t0 >= s_pad_scr_t[s_pad_scr_i])
            s_pad_scr_bits = s_pad_scr_b[s_pad_scr_i++];
        bits = s_pad_scr_bits;
    } else
#endif
    bits = pad_read();
    uint32_t pressed = bits & ~s_pad_held;
    if (pressed & PAD_OPTION) s_pad_ptr_mode = !s_pad_ptr_mode;
    int pointing = s_pad_ptr_mode != ((bits & PAD_A) != 0);

    /* buttons: in key mode C is Return and B is Tab; while pointing, C and B are the mouse's left and
     * right buttons, held while held */
    if (pointing) s_pad_mb = (uint8_t)(((bits & PAD_C) ? 1 : 0) | ((bits & PAD_B) ? 2 : 0));
    else {
        s_pad_mb = 0;
        if (pressed & PAD_C) pad_put(KEY_RETURN);
        if (pressed & PAD_B) pad_put(KEY_TAB);
    }
    if (pressed & PAD_STAR)  pad_put(KEY_ESC);
    if (pressed & PAD_HASH)  pad_put(KEY_SPACE);
    if (pressed & PAD_PAUSE) pad_put(KEY_CTRL_S);
    for (int n = 0; n < 10; n++) if (pressed & PAD_KEY(n)) pad_put(pad_cmd[n]);

    uint32_t dirs = bits & PAD_DIRS, was = s_pad_held & PAD_DIRS;
    if (pointing && dirs) {                               /* the pad moves the pointer */
        int dx = ((dirs & PAD_RIGHT) != 0) - ((dirs & PAD_LEFT) != 0);
        int dy = ((dirs & PAD_DOWN) != 0) - ((dirs & PAD_UP) != 0);
        if (!s_hooked) { /* no hook: nothing to move */ }
        else if (dirs != was || !(s_pad_held & PAD_A) != !(bits & PAD_A)) {   /* a tap: one pixel */
            s_pad_ptr_t = now;
            s_pad_cx = dx * 1000; s_pad_cy = dy * 1000;
        } else {
            int32_t speed = PTR_SLOW + (int32_t)(now - s_pad_ptr_t) * 5 * PTR_GAIN / 1000;
            if (speed > PTR_FAST) speed = PTR_FAST;
            int32_t step = speed * (int32_t)(now - s_pad_last_t) * 5;   /* game px x 1000 */
            s_pad_cx += dx * step; s_pad_cy += dy * step;
        }
        if (s_hooked) {                                   /* game pixels -> physical (the hook's units) */
            int x = s_mx + (int)(s_pad_cx / 1000) * s_mw / 320, y = s_my + (int)(s_pad_cy / 1000) * s_mh / 200;
            s_pad_cx %= 1000; s_pad_cy %= 1000;
            if (x < 0) x = 0;
            if (x >= s_mw) x = s_mw - 1;
            if (y < 0) y = 0;
            if (y >= s_mh) y = s_mh - 1;
            s_mx = (int16_t)x; s_my = (int16_t)y;
        }
    } else if (!pointing && dirs) {                       /* the pad walks: arrow keys, repeated as held */
        if (dirs != was || (pressed & PAD_DIRS) || ((s_pad_held ^ bits) & PAD_A)) {
            pad_put(pad_dir_key[dirs]); s_pad_dir_t = now + PAD_DELAY;
        } else if ((int32_t)(now - s_pad_dir_t) >= 0) {
            if (s_padq_in == s_padq_out) pad_put(pad_dir_key[dirs]);   /* one at a time: no backlog to overrun */
            s_pad_dir_t = now + PAD_REPEAT;
        }
    }
    s_pad_held = bits;
    s_pad_last_t = now;
}

static long pad_install(void)                             /* supervisor (Supexec) */
{
    int nvbls = *(volatile int16_t *)0x454UL;             /* _nvbls */
    s_pad_vblq = *(void (***)(void))0x456UL;              /* _vblqueue */
    for (int i = 0; i < nvbls; i++) if (!s_pad_vblq[i]) { s_pad_vblq[i] = &pad_vbl; s_pad_slot = i; break; }
    return 0;
}
static long pad_remove(void){ if (s_pad_slot >= 0) { s_pad_vblq[s_pad_slot] = 0; s_pad_slot = -1; } return 0; }
static void pad_shutdown(void){ Supexec(pad_remove); }

void plat_pad_init(int has_ports)
{
    int on = has_ports;
#ifndef U6E_RELEASE
    {   int fd = (int)Fopen("PAD.REQ", 0);
        if (fd >= 0) {
            static char buf[2048]; long n = Fread(fd, sizeof buf - 1, buf); Fclose(fd);
            if (n > 0) { buf[n] = 0; char *p = buf;
                while (*p && s_pad_scr_n < 64) {
                    unsigned long t = strtoul(p, &p, 10), b = strtoul(p, &p, 16);
                    s_pad_scr_t[s_pad_scr_n] = (uint16_t)t; s_pad_scr_b[s_pad_scr_n++] = (uint32_t)b;
                    while (*p && *p != '\n') p++;
                    while (*p == '\n' || *p == '\r') p++; }
                s_pad_scr_on = 1; on = 1; } } }
#endif
    if (!on || s_pad_slot >= 0) return;
    Supexec(pad_install);
    if (s_pad_slot >= 0) atexit(pad_shutdown);
}

int plat_pad_active(void){ return s_pad_slot >= 0; }

/* the next key the pad made, or 0 */
static int pad_take(void)
{
    if (s_padq_out == s_padq_in) return 0;
    int k = s_padq[s_padq_out]; s_padq_out = (uint8_t)((s_padq_out + 1) & 15);
    return k;
}
