u6e - Ultima VI for the Atari ST, TT and Falcon
Version @VER@ - TEST RELEASE

A new engine for Ultima VI: The False Prophet, playable from the intro
to the ending. You need your own copy of the DOS game (GOG sells it).
This package contains no game data.

INSTALL
  1. Make a folder on your hard drive or CF card, with about 13 MB free:
     10 MB for the DOS files and 3.5 MB for the conversion.
  2. Copy every file from your DOS ULTIMA6 folder into it.
  3. Copy the program for your machine and CONVERT.TOS into it:

       ST, STE, Mega ST (4 MB)            GAMEST.PRG
       TT, Falcon, 68020+ graphics card   GAME.PRG
       Graphics card on a 68000           GAMEVC.PRG

  4. Run the program from the desktop. The first time, it offers to
     convert your files and says how long that takes: about 8 minutes
     on an ST, 1 to 2 minutes on a TT or Falcon. It then starts the game.

PLAY
  Arrow keys walk. The first letter of a command starts it:
    A)ttack  C)ast  T)alk  L)ook  G)et  D)rop  U)se  M)ove  R)est
    S)ave     B turns combat mode on and off.
  Click a member in the party roster to open their inventory, or press
  Tab (* on the keypad flips portrait and inventory). Select a thing
  there to ready it, or a readied thing to take it off.
  Commands that need a direction or a target take the arrow keys, or a
  crosshair and then Return. For something you carry, press Tab during
  the command and pick it in the inventory (D)rop starts there), or click
  it. Esc cancels. With nothing to cancel it asks whether to quit. The
  mouse works on the command bar, the party roster and the map.

JAGUAR PAD (1040 STE AND FALCON)
  A Jaguar pad in either enhanced joystick port plays beside the keyboard
  (the Mega STE and TT have no such ports). The pad walks and steers the
  crosshair as the arrow keys do. C is Return, B is Tab. Hold A and the
  pad moves the mouse pointer instead, with C and B as the left and right
  buttons; Option swaps the two. The keypad's 1 to 9 and 0 are the
  command bar's icons in order: 1 Attack, 2 Cast, 3 Talk, 4 Look, 5 Get,
  6 Drop, 7 Move, 8 Use, 9 Rest, 0 combat mode. * is Esc, # passes a
  turn (Space), and Pause opens the save page. Conversations still need
  the keyboard to type what you say.

TRANSFER A CHARACTER
  The title menu can bring your Avatar over from Ultima IV or Ultima V,
  ST or DOS. Press a drive letter to search that disk, or Return to use
  the game folder, where you can copy SAVED.GAM (Ultima V), ROST.BIN
  (Ultima IV, ST) or PARTY.SAV (Ultima IV, DOS).

SOUND
  Ctrl-Z turns the sound off and on, as in the original. F9 picks the
  music source, in the menus too.

SAVED GAMES
  S saves and Ctrl-R restores. Each opens a page of 8 slots: Up, Down or
  1-8 pick one, Enter acts, Esc cancels. Journey Onward resumes the slot
  you used last. Each slot keeps its two previous saves too (in Restore,
  Left and Right pick them), so one bad save need not cost you the game.

MAGIC
  As in the original, you begin with no spellbook and no reagents. Look
  around Lord British's castle for a book, Get it, and ready it: C)ast
  asks for a readied book. Reagents are found or bought, and your level
  limits the circles you can cast. Mages sell spells as parchments. Put
  one in your book yourself: press M, then Tab, the parchment in the
  inventory, and Return.

MUSIC
  Have the ST disks? Copy U.PRG from them next to the game and you get the
  ST release's own chip tunes too (the default without DMA sound).
  The YM2149 sound chip plays the original songs by default. F9 cycles
  through the sources: automatic, Roland MT-32 or General MIDI on the
  MIDI port, DMA sound, YM2149. The choice is saved in MIDI.CFG.

KNOWN LIMITS
  - Tested in the Hatari emulator on TOS 1.04, 3.06 and 4.04, and under
    MiNT with XaAES and MagiC 6.2, and by a player on a Mega STe.
    Real-machine reports are very welcome.
  - On a plain ST or STE, combat pauses briefly after each blow while the
    map is redrawn.
  - A TT with 4 MB and no TT-RAM draws on one screen to save
    memory, so a cutscene can tear.
  - MagiC on a 4 MB ST leaves too little memory for the game: start
    the ST without MagiC.
  - Savage Empire and Martian Dreams convert and can be walked around,
    but are not playable yet.

BUGS
  Please say which machine or emulator, which program, what you did and
  what you saw. A save (SAVED.000) from just before the problem helps.

LICENCE
  u6e is free software under the GNU General Public License, version 2
  or later: see LICENSE.TXT. Source code:
    https://github.com/slaapliedje/u6e
  Ultima is a trademark of Electronic Arts. This project is not
  affiliated with or endorsed by EA or Origin Systems.
