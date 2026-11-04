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
     convert your files and says how long that takes: about 3 minutes
     on an ST, under a minute on a TT or Falcon. It then starts the game.

PLAY
  Arrow keys walk. The first letter of a command starts it:
    A)ttack  C)ast  T)alk  L)ook  G)et  D)rop  U)se  M)ove  E)quip
    R)est  I)nventory  S)ave     B turns combat mode on and off.
  Commands that need a direction or a target take the arrow keys, or a
  crosshair and then Return. Esc cancels. With nothing to cancel it asks
  whether to quit. The mouse works on the command bar, the party roster
  and the map.

TRANSFER A CHARACTER
  The title menu can bring your Avatar over from Ultima IV or Ultima V,
  ST or DOS. Press a drive letter to search that disk, or Return to use
  the game folder, where you can copy SAVED.GAM (Ultima V), ROST.BIN
  (Ultima IV, ST) or PARTY.SAV (Ultima IV, DOS).

SOUND
  Ctrl-Z turns the sound off and on, as in the original. F1 picks the
  music source.

SAVED GAMES
  S saves and Ctrl-R restores. Each opens a page of 8 slots: Up, Down or
  1-8 pick one, Enter acts, Esc cancels. Journey Onward resumes the slot
  you used last. Each slot keeps its two previous saves too (in Restore,
  Left and Right pick them), so one bad save need not cost you the game.

MAGIC
  As in the original, you begin with no spellbook and no reagents. Look
  around Lord British's castle for a book, Get it, and E)quip it: C)ast
  asks for a readied book. Reagents are found or bought, and your level
  limits the circles you can cast. Mages sell spells as parchments. Put
  one in your book yourself: press M twice, M again until it is selected,
  then Return.

MUSIC
  Have the ST disks? Copy U.PRG from them next to the game and you get the
  ST release's own chip tunes too (the default without DMA sound).
  The YM2149 sound chip plays the original songs by default. F1 cycles
  through the sources: automatic, Roland MT-32 or General MIDI on the
  MIDI port, DMA sound, YM2149. The choice is saved in MIDI.CFG.

KNOWN LIMITS
  - Tested in the Hatari emulator on TOS 1.04, 3.06 and 4.04, and under
    MiNT with XaAES and MagiC 6.2. Real-machine reports are very welcome.
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
