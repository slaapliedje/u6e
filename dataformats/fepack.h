// SPDX-License-Identifier: GPL-2.0-or-later
/* fepack.h — U6FE.PAK (front-end pack) layout shared by the host packer (tools/mkworld.c) and
 * the engine loader (render/gamedemo.c). Our own container; documented in
 * docs/design/memory_streaming.md ("U6FE.PAK v2").
 *
 *   [4  "U6F2"]                      magic (v2 = sectioned)
 *   [768 base palette]               U6PAL as RGB6 — the menu/title/game palette, so the
 *                                    front-end can run BEFORE the core pack is loaded
 *   [48  low-colour palette]         ST 16 (3-bit levels) + [256 lut] 256->16 (palette_lowcolor.md)
 *   [2048 font]                      U6.CH 8x8 glyphs for front-end text
 *   [u8 nsec] + nsec x [u8 id][u32 off][u32 len]   section directory; off = from file start
 *   sections...                      each section is self-describing (same byte layouts the
 *                                    inline U6WU..U6WV block always had, split at these seams)
 *
 * The directory lets the engine load the front-end either WHOLE (resident, big-RAM machines)
 * or a GROUP of sections at a time into a scoped region (streamed, 4 MB machines): the same
 * per-section binders run in both cases. Section ids are stable; new sections append. */
#ifndef U6E_FEPACK_H
#define U6E_FEPACK_H

#define FEPACK_MAGIC        "U6F2"
#define FEPACK_HDR_BASEPAL  4                    /* 768 */
#define FEPACK_HDR_LCPAL    (4+768)              /* 48  */
#define FEPACK_HDR_LCLUT    (4+768+48)           /* 256 */
#define FEPACK_HDR_FONT     (4+768+48+256)       /* 2048 */
#define FEPACK_HDR_DIR      (4+768+48+256+2048)  /* u8 nsec + nsec*9 */
#define FEPACK_DIR_ENT      9
#define FEPACK_MAX_SEC      32
#define FEPACK_HDR_MAX      (FEPACK_HDR_DIR + 1 + FEPACK_MAX_SEC*FEPACK_DIR_ENT)   /* 3413 */

enum {
    FES_MENU = 0,   /* main menu + title art + per-scene menu ST 16                       */
    FES_INTRO,      /* intro scenes (living room pan / stones / gate-rise)                  */
    FES_TV,         /* living-room TV sprites + finger + LED clock digits                 */
    FES_GATE,       /* moongate gate + ground cover                                        */
    FES_CCPORT,     /* character-creation portraits (MONTAGE 0..11)                        */
    FES_TEXT,       /* gypsy questions + intro narration + chargen ceremony text (U.EXE)  */
    FES_GYPSY,      /* gypsy casting scene + scroll + GYPSY.SHP shapes                     */
    FES_WOODS,      /* woods forest-path panorama                                          */
    FES_STORM,      /* storm window: palette, bolts, strikes, 15 animation layers          */
    FES_STONES,     /* stones pickup: hand, obsidian, bg5, clutch hand                     */
    FES_AVATAR,     /* Avatar-into-gate climax frames                                      */
    FES_GARG,       /* §1B gargoyle-sacrifice scene                                         */
    FES_VELLUM,     /* chargen vellum screens                                              */
    FES_FSET,       /* U6.SET proportional front-end font                                  */
    FES_LOGOS,      /* ORIGIN / Lord British boot logos                                     */
    FES_SCROLL,     /* shared narration parchment (BLOCKS.SHP 2) — intro captions + gargoyle */
    FES_END,        /* the ending cutscene: ENDPAL + END.SHP shapes + END.EXE text (docs/design/ending.md) */
    FES_CURSOR,     /* the mouse pointer (U6MCGA.PTR arrow): [u8 w][u8 h][u8 hotx][u8 hoty][w*h] — the same bytes as
                       the core pack's U6W9, shipped here too so the MAIN MENU can show it before the core loads */
    FES_N
};

/* Load groups (bitmasks) the engine streams as units. COMMON stays for the whole front-end;
 * INTRO / CHARGEN come and go with their runners. */
#define FEG_COMMON  ((1u<<FES_MENU)|(1u<<FES_TEXT)|(1u<<FES_FSET)|(1u<<FES_SCROLL)|(1u<<FES_CURSOR))
#define FEG_INTRO   ((1u<<FES_INTRO)|(1u<<FES_TV)|(1u<<FES_GATE)|(1u<<FES_STORM)|(1u<<FES_STONES)|(1u<<FES_AVATAR)|(1u<<FES_LOGOS))
#define FEG_CHARGEN ((1u<<FES_CCPORT)|(1u<<FES_GYPSY)|(1u<<FES_WOODS)|(1u<<FES_GARG)|(1u<<FES_VELLUM))

#endif /* U6E_FEPACK_H */
