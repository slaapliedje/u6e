# Jaguar pad

Last updated 2026-11-21. Code: `platform/platinput.c` (the "Jaguar pad" section).

A Jaguar pad (Jagpad, PowerPad) in either enhanced joystick port plays the game beside the keyboard
and mouse. It is an addition over the original (neither the DOS nor the ST game read a pad); the
keyboard and mouse are unchanged.

## Which machines

Only the 1040 STE and the Falcon have the enhanced ports. The Mega STE, the TT and the plain ST
don't, and a read of $FF9200 there is a bus error, so the reader is installed only when the `_MCH`
cookie says STE (high word 1, low word not 0x10) or Falcon (high word 3).

## The hardware

From the Falcon030 hardware reference (atari-st-tech corpus, Addendum 5 §8); the STE's ports are
the same:

| register | access | meaning |
|---|---|---|
| $FF9202 | write | drive one column low: bits 0-3 port A, 4-7 port B, the rest high |
| $FF9202 | read | rows pressed in that column, low = pressed: bits 8-11 port A, 12-15 port B |
| $FF9200 | read | low = pressed: bit 0 port A Pause (column 0), bit 1 port A fire row, bits 2-3 port B |

The four columns give:

| column | row 0 | row 1 | row 2 | row 3 | fire row |
|---|---|---|---|---|---|
| 0 | Up | Down | Left | Right | A (and Pause) |
| 1 | * | 7 | 4 | 1 | B |
| 2 | 0 | 8 | 5 | 2 | C |
| 3 | # | 9 | 6 | 3 | Option |

Both ports are read and ORed, so either port works; an empty port reads as nothing pressed. After
the scan all columns go high again (write $FFFF).

## When it is read

From a slot in the VBL queue (`_vblqueue` $456, `_nvbls` $454), freed at exit. The routine runs in
supervisor mode, so it reads the ports and the 200 Hz clock (`_hz_200` $4BA) without a trap. Taps are
caught at the VBL even while the game is busy drawing.

## What it does

The pad turns into the keys and mouse the game already reads:

- **Keys** go into a 16-entry queue. `plat_input_raw` takes one when the keyboard has none waiting,
  so every place that reads a key reads the pad too.
- **Pointer moves** go into the mouse hook's position (physical pixels, scaled from game pixels),
  and its **buttons** are ORed into `plat_mouse`'s.

| pad | key mode (default) | pointer mode |
|---|---|---|
| directions | arrow keys; two held = the keypad's 7 9 1 3 diagonals (no ASCII) | move the pointer |
| C | Return | left button, held while held |
| B | Tab | right button, held while held |
| A | held: pointer mode | held: key mode |
| Option | swap the modes | swap the modes |
| keypad 1-9, 0 | the command bar's icons in order: A)ttack C)ast T)alk L)ook G)et D)rop M)ove U)se R)est; 0 = B, combat mode | the same |
| * | Esc | Esc |
| # | Space (pass a turn) | Space |
| Pause | Ctrl-S (the save page) | Ctrl-S |

The modes follow the user's Ultima VII port (A held moves the pointer, Option swaps, C and B are the
left and right buttons), so the two games feel the same.

- A held direction repeats as the keyboard's auto-repeat does (`Kbrate(15,3)`): the first key at once,
  the next after 300 ms, then every 60 ms, and a repeat only when the queue is empty, so a busy frame
  never leaves steps queued after the pad is let go.
- The pointer moves 60 game pixels a second at first, gaining 400 a second held, up to 320; a tap
  moves it one pixel.

## Not covered

Conversations need typed keywords, so talking still needs the keyboard. A keyword picker (as the
console ports had) is the way to talk with a pad alone; it would also be needed for a Jaguar port.

## Testing

- `PAD.REQ` (test builds only): lines of `ticks bits`, 200 Hz ticks from the first VBL and the pad
  bits in hex (Up 1, Down 2, Left 4, Right 8, A 10, B 20, C 40, Option 80, Pause 100, * 200, # 400,
  keypad n = 800 << n). It is replayed in place of the ports.
- The real port reads under Hatari: a virtual gamepad (python-evdev `UInput` with ABS_X/ABS_Y and
  three buttons; the user is in the `input` group) and `--joy2 real` give Hatari's Jaguar pad A on the
  STE or Falcon. Buttons 0, 1 and 2 are A, B and C; the keypad, Option and Pause come only from the
  keyboard bindings in hatari.cfg's `[Joystick2]`.
- Checked 2026-11-21 on an emulated STE (GAMEST.PRG) and Falcon (GAME.PRG): walking, A held moving the
  pointer, B opening the inventory, C readying the thing under its cursor; with PAD.REQ, keypad 4 arming
  Look and C firing it.
