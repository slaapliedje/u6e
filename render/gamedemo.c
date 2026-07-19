// SPDX-License-Identifier: GPL-2.0-or-later
/* gamedemo.c — M6 authentic U6 UI on the TT.
 *
 * Renders U6's real screen using ORIGINAL assets (loaded from the U6W4 world
 * pack at runtime, never embedded): the PAPER.BMP papyrus border + unfurled
 * scroll, the U6.CH font, real 56x64 portraits, the windowed map, and surface
 * objects. Authored at U6's NATIVE 320x200 and LETTERBOXED into TT-low's
 * 320x480 (2x-vertically-doubled via rdr_present_doubled, centred, black bands).
 *
 * TOS calls are all in render/rdr_tt.c + platform/. We only touch the chunky
 * backbuffer here, then present the 320x200 letterbox region (c2p).
 */
#include "render.h"
#include "platmem.h"
#include "platfile.h"
#include "platinput.h"
#include "plattime.h"
#include "world.h"
#include "converse_vm.h"
#include "u6actor.h"
#include "u6lzw.h"
#include "objects.h"
#include <string.h>            /* memcpy/memmove for the incremental map-scroll buffer */
#include "save.h"
#include "rng.h"
#include "combat.h"
#include "magic.h"
#include "platmidi.h"          /* M10: MIDI OUT */
#include "platdma.h"           /* M10: STE/TT/Falcon 8-bit DMA sound (pre-rendered PCM streaming) */
#include "platpsg.h"           /* M10: YM2149 PSG (plain-ST 3-voice fallback) */
#include "music.h"             /* M10: OPL2 command-stream sequencer */
#include "oplsynth.h"          /* M10: OPL2 software FM synth (for the -DBENCH throughput probe) */
#include <osbind.h>
#include <string.h>

#define LW   320               /* native logical width                       */
#define LH   200               /* native logical height                      */
#define UIY0 0                 /* compose at the top; present scales 2x (TT)   */
#define BORD 8
#define VT   11                /* 11x11 tile map window (U6's MapWindow size)  */
#define MAPX 0                 /* map window top-left (its frayed border tiles */
#define MAPY 0                 /*   form the edge; panel sits to its right)    */
#define MAPSZ (VT*16)          /* 176: full 11x11 window, avatar centred       */
#define MAP_INSET 8            /* terrain inset so the outer ring stays papyrus (no bleed) */
#define PANX 176               /* right panel x (just right of the 176 map)    */
#define PANW (LW-BORD-PANX)    /* 136: right panel width                      */
#define PW   56
#define PH   64
#define STATY 170
#define TILE_TRANSPARENT 255

enum { C_BLACK=0 };            /* U6PAL index 0 is pure black                 */

static const uint8_t *g_tileset, *g_objs, *g_font, *g_paper, *g_portraits;
static int32_t        g_nobjs;
static int            g_nport;
static int            g_demo_npc;       /* NPC whose conversation the VM runs   */
static const uint8_t *g_script; static int g_script_len;
static char           g_transcript[1024];/* VM-produced conversation transcript  */
static cvm_game_t     g_cg;
static cvm_t          g_cv;
static uint16_t       g_atile[U6_MAX_ACTORS]; /* per-actor appearance tile (0xffff=none) */
static u6actor_t      g_actors[U6_MAX_ACTORS];/* schedules (positions by game time)      */
static uint16_t       g_actx[U6_MAX_ACTORS], g_acty[U6_MAX_ACTORS]; /* MUTABLE actor positions */
static uint8_t        g_actz[U6_MAX_ACTORS];  /* live level (0xff = none/off-surface)      */
/* NPC schedule-walking (Stage 1): actors step one tile/turn toward their scheduled spot,
 * facing the move. Frame layout = base + dir*tpd (+walk); dir N=0 E=1 S=2 W=3. */
static uint16_t       g_abase[U6_MAX_ACTORS]; /* appearance basetile (dir 0, frame 0)      */
static uint8_t        g_atpd[U6_MAX_ACTORS];  /* tiles per direction (0 = non-directional) */
static uint8_t        g_facing[U6_MAX_ACTORS];/* 0=N 1=E 2=S 3=W                            */
static uint8_t        g_walkf[U6_MAX_ACTORS]; /* gait phase (toggles each step taken)        */
static uint8_t        g_amoved[U6_MAX_ACTORS];/* 1 = stepped this turn (walk frame vs stand) */
static uint8_t        g_hostile[U6_MAX_ACTORS];/* 1 = ASSAULT the party (set when attacked) (combat.md CB2) */
static uint8_t        g_ahp[U6_MAX_ACTORS], g_astr[U6_MAX_ACTORS];  /* per-actor stats (U6WC) */
static uint8_t        g_adex[U6_MAX_ACTORS], g_aint[U6_MAX_ACTORS];
static uint16_t       g_aweapon[U6_MAX_ACTORS];/* readied weapon obj_n (0=none) (U6WL) */
static uint8_t        g_aac[U6_MAX_ACTORS];    /* readied armour class (combat.md CB3b)        */
static uint8_t        g_align_hostile[U6_MAX_ACTORS]; /* 1 = monster type hostile on sight (U6WM, CB4) */
static uint8_t        g_coward[U6_MAX_ACTORS]; /* 1 = coward (worktype COMBAT_RETREAT): flees until struck (U6WN) */
static uint8_t        g_amaxhp[U6_MAX_ACTORS]; /* HP at load (=max), for the flee threshold (CB4) */
static uint8_t        g_aflee[U6_MAX_ACTORS];  /* 1 = fleeing: badly-hurt actor retreats (CB4)    */
static int            g_combat_mode;           /* C)ombat mode: party auto-fights adjacent foes (CB4) */
static uint16_t       g_aexp[U6_MAX_ACTORS];    /* per-actor running experience (U6WO + kills, capped) */
static int8_t         g_alevel[U6_MAX_ACTORS];  /* per-actor experience level (maxhp/30; raised by TRAINING) */
/* gold is the conversation VM's pool (g_cg.gold) — shared by merchant/trainer dialogs AND stat
 * training, so there's ONE gold source. Loaded from the save (party's obj_n-88 coins, pack U6WR)
 * into g_load_gold at pack-load, then copied into g_cg.gold AFTER cvm_game_init zeroes it. */
static uint16_t       g_load_gold=0;
static uint8_t        g_load_karma=0;  /* avatar karma from the save (objlist 0x1bf9, pack U6WR); -> g_cg.karma */
static uint8_t        g_load_book_present=0, g_load_book_all=0, g_load_known[16];  /* spellbook from the save (pack U6WR) */
static uint8_t        g_load_reag_present=0, g_load_reag[8];  /* avatar reagents (obj 65-72) from the save (pack U6WR) */
static uint8_t        g_trainer[U6_MAX_ACTORS];  /* 1 = level-advancing trainer (Lord British, obj_n 409; U6WP) */
static uint8_t        g_alert[U6_MAX_ACTORS];    /* aggro-on-sight latch: 1 once a hostile has PERCEIVED the party */
/* MAGIC status effects (docs/rules/magic_skills.md §5b): per-actor condition timers. */
static uint8_t        g_poison[U6_MAX_ACTORS];   /* turns of poison damage-over-time    */
static uint8_t        g_paralyze[U6_MAX_ACTORS]; /* turns paralyzed (skips its turn)     */
static uint8_t        g_protect[U6_MAX_ACTORS];  /* turns of Protection (halves damage)  */
static uint8_t        g_curse[U6_MAX_ACTORS];    /* turns cursed (attacks at a penalty)  */
static uint8_t        g_charm[U6_MAX_ACTORS];    /* turns charmed (pacified: not hostile) */
/* MAGIC summoning (docs/rules/magic_skills.md §5c): a summoned creature + its lifetime. */
static uint8_t        g_summon[U6_MAX_ACTORS];    /* 0=not summoned, 1=allied summon, 2=hostile summon */
static uint8_t        g_summon_life[U6_MAX_ACTORS];/* turns before the summon vanishes           */
static int            g_gate_dest=0;              /* Gate Travel: which moonstone to gate to next (§5d) */
/* hostile if struck (always) OR enemy-aligned. g_align_hostile is now the AUTHENTIC per-actor
 * U6 alignment (objlist status 0x800 bit 0x20 = EVIL/CHAOTIC), so a post-treaty (NEUTRAL/GOOD)
 * gargoyle is simply not hostile — no separate peace flag needed. A CHARMED actor is pacified. */
static int            actor_hostile(int i){ return (g_hostile[i] || g_align_hostile[i]) && !g_charm[i]; }
/* combat modifiers from status (§5b): a CURSED attacker rolls to-hit at a penalty;
 * a PROTECTED defender takes half damage. */
static int            status_hit_role(int att,int base){ return g_curse[att]?(base>6?base-6:1):base; }
static int            status_dmg(int def,int dmg){ return g_protect[def]?(dmg+1)/2:dmg; }
/* MAGIC (docs/rules/magic_skills.md): per-actor magic points (max = Intelligence);
 * g_known marks the spells the Avatar has in the spellbook. */
static uint8_t        g_amana[U6_MAX_ACTORS], g_amaxmana[U6_MAX_ACTORS];
static uint8_t        g_known[SPELL_COUNT];
static uint8_t        g_spell_light_turns=0; /* Light/Great Light: remaining lit turns       */
static uint8_t        g_spell_light_rad=0;   /* the boosted avatar-globe radius while lit     */
static uint8_t        g_xray_turns=0;        /* X-ray (§5e): see-through-walls turns remaining */
static uint8_t        g_ainvis[U6_MAX_ACTORS]; /* per-actor invisibility turns (Invisibility set, Reveal clears) */
static uint8_t        g_afood[U6_MAX_ACTORS];  /* per-actor food level (hunger model): ebbs, eating restores */
#define FOOD_MAX    255
#define FOOD_START  200
#define FOOD_HUNGRY 40                        /* "X is hungry." warning threshold */
static int            g_cast_spell=-1;       /* the spell the armed C)ast verb will cast       */
static int            g_casting=0;           /* 1 = the C)ast spell-selection menu is open      */
static uint8_t        g_castlist[SPELL_COUNT];/* castable spell indices, in menu (1-based) order */
static int            g_ncast=0;             /* number of spells in g_castlist                  */
static int            g_savestate;            /* 1 = world built from a (ScummVM) save state */
static uint8_t       *g_savetmpl;             /* template ScummVM save (SAVE.000), patched on save */
static int32_t        g_savetmpl_len;         /* template byte length                          */
static int32_t        g_objlist_off;          /* objlist offset within the template (-1 = none) */

/* runtime-loaded conversation + portrait libraries (4-byte LE offset tables) */
static const uint8_t *g_convA,*g_convB,*g_portA,*g_portB;
static int32_t        g_convAlen,g_convBlen,g_portAlen,g_portBlen;
static int            g_countA;            /* CONVERSE.A entry count (NPC split)  */
static int            g_talking;           /* 1 = a conversation is on screen     */
static int            g_have_pc;           /* 1 = g_cur_portrait holds a portrait  */
static char           g_talk_name[32];     /* current NPC's name (from the VM)    */
static uint8_t        g_cur_portrait[PW*PH];/* decoded 56x64 portrait             */
static uint8_t        g_script_buf[16384]; /* decompressed conversation for the VM*/
static int            g_hour;              /* current game hour 0-23 (from clock)  */
static int            g_minutes;           /* current minute-of-day (for the sky bar) */
static int            g_await_input;       /* 1 = VM paused (CVM_ASK), typing a reply */
static char           g_inbuf[CVM_INPUT];  /* the keyword/number being typed       */
static int            g_inlen;

/* object verbs + inventory (look / get / use) */
#define MAXWOBJ 12000
static wobj_t         g_wobj[MAXWOBJ];      /* mutable world objects (alive flag)   */
static objworld_t     g_ow;
static const char    *g_looktbl[2048];     /* tile -> name (into the pack's blob)  */
static const uint8_t *g_basetile;          /* 1024 x u16 LE: obj_n -> base tile    */
/* tile animation (ANIMDATA, U6WD): animated tile slots are blank in the tileset;
 * each is filled from src..src+nframes-1, cycling on g_anim_ctr. (animdata.md) */
static int16_t        g_anim_idx[2048];    /* tile -> anim entry index, or -1      */
static uint16_t       g_anim_src[32];      /* per entry: source-frame base tile     */
static uint8_t        g_anim_n[32];        /* per entry: number of frames           */
static int            g_nanim;
static uint32_t       g_anim_ctr;          /* current animation step                */
static int            g_view_has_anim;     /* 1 if an ANIMDATA tile is in the map view (perf gate) */
static uint8_t        g_objanim[1024];     /* per-obj_n frame count (0=static): fire/flame objects cycle frame_n (U6WE) */
static uint8_t        g_tflags[2048];       /* tileflags2 by tile: bit4 Foreground, bits6-7 size (U6WF) */
static uint8_t        g_tflags1[2048];      /* tileflags1 by tile: bit1 Impassable, bit2 Wall (U6WG) */
static uint8_t        g_blocked[1024*1024/8]; /* bitmap: impassable OBJECT on tile (y*1024+x) */
static uint8_t        g_sightblk[1024*1024/8]; /* bitmap: sight-blocking OBJECT on tile (closed door etc.) for LOS */
/* MAGIC field-tile layer (docs/rules/magic_skills.md §5a): temporary fire/poison/
 * sleep/energy fields placed by spells. kind: 0 fire, 1 poison, 2 sleep, 3 energy(block). */
#define MAXFIELDS 64
typedef struct { uint16_t x, y, tile, obj_n; uint8_t kind, turns; } field_t;
static field_t        g_fields[MAXFIELDS];
static int            g_nfields=0;
static uint8_t        g_asleep[256];          /* >0 = actor is asleep for N turns (sleep field) */
/* spatial bucket index over world objects (16x16-tile buckets, 64x64 grid): compose visits
 * only the buckets overlapping the view instead of scanning all ~8000 objects every frame.
 * CSR layout: g_obkt_idx[g_obkt_start[b] .. g_obkt_start[b+1]) = object indices in bucket b.
 * Rebuilt in build_blocked (the "objects changed" hook). */
#define OBKT 64
static uint16_t       g_obkt_start[OBKT*OBKT+1];
static uint16_t       g_obkt_idx[MAXWOBJ];

/* Incremental map-scroll: g_tmp_map caches the STATIC compose layer (base tiles + lower-pass
 * objects, no actors/foreground/lighting). On a 1-tile walk it is memmove-scrolled and only the
 * lead 2-cell band is recomposed; actors, foreground objects, roofs and lighting are redrawn
 * full per frame (so no actor-erase or shifted-light artifacts). See docs/design/performance.md. */
#define TMP_T (VT*16)
static uint8_t        g_tmp_map[TMP_T*TMP_T];
static int            g_tmp_valid=0, g_tmp_px=-30000, g_tmp_py=-30000;
/* cell-granular clip rect for draw_cell (default = whole view); recompose_band narrows it so a
 * band redraw composes only band cells byte-identically (multi-tile bleed into the band lands;
 * everything else is rejected per-cell). */
static int            g_clip_x0=0,g_clip_y0=0,g_clip_x1=VT,g_clip_y1=VT;
static uint8_t        g_indoor[1024*1024/8];  /* bitmap: tile is INDOOR (under a roof) -> low ambient (U6WI) */
static int            tile_is_indoor(int x,int y){ int32_t i=(int32_t)y*1024+x; return (g_indoor[i>>3]>>(i&7))&1; }
static const uint8_t *g_rtiles;               /* roof tileset (n*256, 255=transparent), points into pak (U6WK) */
static int            g_nrtiles;
static const uint8_t *g_roofmap;              /* g_nroof x {u32 pos=y*1024+x BE, u16 dense idx BE}, sorted by pos */
static int            g_nroof;
/* front-end art (U6WU): MAINMENU / TITLES shape-0 bitmaps, 8-bit indexed, point into the pak. */
static const uint8_t *g_menu_pix;  static int g_menu_w, g_menu_h;
static const uint8_t *g_title_pix; static int g_title_w, g_title_h;
static const uint8_t *g_menu_lc;   /* per-scene ST 16 + 256->16 LUT for the title screen (into the pak) */
/* intro cutscene scenes (U6WV): point into the pak (front-end Phase 2). type 0 = static
 * 320x200 hold; type 1 = horizontal pan (w-wide panorama scrolls 1x, fg foreground 2x). */
#define INTRO_MAX 16
static struct { const uint8_t *pix, *fg, *pal, *lc; int w, h, type; } g_intro[INTRO_MAX];
static int g_nintro;
static const uint8_t *g_scroll; static int g_scroll_w, g_scroll_h;  /* narration parchment (BLOCKS.SHP) */
static const uint8_t *g_tv_base, *g_tv_mouth[4];                    /* living-room TV anchor + mouth frames */
static int g_tv_bw,g_tv_bh,g_tv_bx,g_tv_by, g_tv_mw,g_tv_mh,g_tv_mx,g_tv_my, g_tv_nmouth, g_tv_frame;
static const uint8_t *g_tv_chan[2][2]; static int g_tv_nchan, g_tv_fpc;  /* TV alt-channels (57x37 program frames) for the channel cycler */
static const uint8_t *g_digit[11]; static int g_dig_w[11], g_dig_h[11], g_clk_x, g_clk_y, g_ndigit;  /* LED clock */
static const uint8_t *g_gate, *g_cover; static int g_gw,g_gh,g_gx, g_cvw,g_cvh,g_cvx,g_cvy, g_gate_y;  /* moongate rise */
static const uint8_t *g_cc_port[12]; static int g_cc_pw, g_cc_ph, g_cc_nport;  /* MONTAGE.SHP 0..11: chargen portraits (6 female @0..5, 6 male @6..11) */
static const char *g_cc_qtext[28]; static int g_cc_qlen[28], g_cc_nq;          /* gypsy question prose, extracted from U.EXE into the pack (canonical order; NOT NUL-terminated) */
#define BOOK_MAX 256
static const char *g_book[BOOK_MAX]; static int g_booklen[BOOK_MAX], g_nbook;   /* BOOK.DAT readable text (signs/books/plaques), indexed by object quality-1 (docs/formats/strings.md) */
static const uint8_t *g_gyp_scene, *g_gyp_pal, *g_gyp_scroll;                   /* gypsy "casting" bg (PALETTES.INT 5, bg only) + tall question parchment */
static int g_gyp_w, g_gyp_h, g_gscroll_w, g_gscroll_h;
/* RAW GYPSY.SHP shape table (nuvie image_load_all): the engine composites the casting scene AND
 * runs nuvie's exact pour animation (create_character gypsy_vial_anim) from these shapes by number. */
#define NGSH 200
static const uint8_t *g_gsh[NGSH]; static int g_gsh_w[NGSH], g_gsh_h[NGSH], g_ngsh;
static int g_vial_lvl[8];                 /* current level of each virtue vial (start 3=FULL; DRAINS into the jar) */
static int g_jar_lvl;                     /* central jar level 0..7 (fills 1 per pour, nuvie drain-model) */
/* transient sprites during a pour (shape index, absolute x,y; shape<0 = hidden). arm+hand lift the
 * winning vial and tip it over the jar; pour = the stream; vl = that vial's liquid following the hand. */
static int g_arm_sh=-1, g_arm_x, g_arm_y, g_hand_sh=-1, g_hand_x, g_hand_y;
static int g_pour_sh=-1, g_pour_x, g_pour_y, g_vl_sh=-1, g_vl_x, g_vl_y;
static int g_pour_vial=-1;                /* vial whose glass is hidden (carried by the hand) mid-pour */
/* JAR BUBBLES (nuvie image_bubble_effect): as virtues pour in, the jar liquid churns — every 4th
 * update, each non-transparent liquid pixel is re-randomised to one of the poured virtues' colours.
 * We speckle a WORKING COPY (g_jarbub) so the shared pack shape is never mutated. */
static uint8_t g_flask_col[8]; static int g_nflask;       /* accumulated per-virtue liquid colours (palette-5 idx) */
static int g_bubble_ctr;                                  /* fire the effect when this hits 0, then reset to 3 */
static uint8_t g_jarbub[96*96]; static int g_jarbub_w, g_jarbub_h, g_jarbub_lvl=-1;  /* speckled jar-liquid copy */
static rng_t g_bubrng;                                    /* dedicated RNG for the bubble speckle */
static const uint8_t *g_woods, *g_woods_pal;  static int g_woods_w, g_woods_h;  /* forest-path lead-in panorama (PALETTES.INT 4) */
/* storm-window intro scene (INTRO_2.SHP, PALETTES.INT 2): baked window scene + lightning frames */
static const uint8_t *g_storm_pal, *g_storm_base; static int g_storm_w, g_storm_h;
static const uint8_t *g_storm_bolt[8]; static int g_storm_boltw[8], g_storm_bolth[8], g_storm_nbolt;
static const uint8_t *g_storm_str[5]; static int g_storm_strw[5], g_storm_strh[5], g_storm_nstr, g_storm_sx, g_storm_sy;
/* per-scene ST 16 + 256->16 LUT for the chargen lead-in scenes (after each scene's 6-bit palette). */
static const uint8_t *g_gyp_lc, *g_woods_lc, *g_storm_lc;
static const uint8_t *g_forest_base, *g_win_ov; static int g_forest_w, g_forest_h, g_win_ow, g_win_oh;  /* forest reveal: frameless storm bg + window overlay that slides off */
/* stones obsidian-pickup (INTRO_3.SHP): the hand rises to grab the obsidian; bg5 = stone gone */
static const uint8_t *g_stn_hand, *g_stn_obs, *g_stn_bg5; static int g_stn_hw,g_stn_hh, g_stn_ow,g_stn_oh, g_stn_b5w,g_stn_b5h;
static const uint8_t *g_av_frame[20]; static int g_av_w[20], g_av_h[20], g_av_ox[20], g_av_oy[20], g_av_n;   /* Avatar-into-gate walk frames (INTRO_3 7..26), each cropped to its bbox + (ox,oy) offset */
/* dense roof-tile index at (x,y), or -1 if not roofed. Binary-searches the sorted map. */
static int roof_at(int x,int y){
    int32_t key=(int32_t)y*1024+x; int lo=0, hi=g_nroof-1;
    while(lo<=hi){ int m=(lo+hi)>>1; const uint8_t*e=g_roofmap+(int32_t)m*6;
        int32_t pos=((int32_t)e[0]<<24)|((int32_t)e[1]<<16)|((int32_t)e[2]<<8)|e[3];
        if(pos==key) return (e[4]<<8)|e[5];
        if(pos<key) lo=m+1; else hi=m-1; }
    return -1;
}
/* colour cycling (CLUT rotation): contiguous u6pal ramps that rotate each tick.
 * Changing the palette registers animates every pixel using those indices with NO
 * recompose/present (the shifter re-reads the CLUT) — cheap. Ranges are U6 game
 * knowledge; confirmed by observing ScummVM. (docs/formats/palette_cycling.md) */
static const struct { uint8_t lo, len; } CYCLE[] = {
    { 224, 8 },   /* fire: red->orange->yellow ramp (CONFIRMED: throne-room braziers cycle) */
    /* 232-239 (blue: force field/moongate/magic items) is a strong CANDIDATE but
     * UNCONFIRMED — couldn't observe it (ScummVM doesn't render save-injected test
     * objects observably, and no natural magic scene is reachable headlessly). Not
     * shipped until confirmed in real gameplay. (docs/formats/palette_cycling.md) */
};
static const uint8_t *g_basepal;           /* pack palette (RGB6 768B), cycle base   */
static uint8_t        g_curpal[768];        /* working palette (rotated ranges)        */
/* low-colour ST section (U6WW): host-quantised 16-colour palette + 256->16 LUT, consumed
 * only by the shifter backend (rdr_st). g_lc_pal = 16 x {r,g,b} as ST 3-bit levels 0..7. */
static uint8_t        g_lc_pal[48], g_lc_lut[256];
static int            g_have_lowcolor;      /* 1 = pack carries a U6WW low-colour section */
/* band rasters (U6WX): pointers into the pak — nb bands x {y0, 16-colour palette, 256-LUT}. */
static int            g_lcx_nb;
static const uint8_t *g_lcx_y0, *g_lcx_pal, *g_lcx_lut;
/* per-portrait conversation palettes (U6WY): the ST swaps to one while a portrait is up. */
static int            g_nconvpal;
static const uint8_t *g_convpal;            /* nconvpal x (48 pal + 256 lut), into the pak */
/* colour-cycling reserved slots (U6WZ): the shifter rotates these ST-16 slots each tick to animate a
 * U6PAL ramp (fire/water) on 16-colour hardware — the ST counterpart of the 256-colour CYCLE[] above.
 * The host reserves them in the game 16 (rdr_st inherits them in band 0 = the map). No-op on TT/Falcon. */
#define CYC_MAX 4
static int            g_lc_ncyc;            /* number of reserved cycle ranges (0 = none)         */
static uint8_t        g_lc_cyc[CYC_MAX][2]; /* per range: {first_slot, nslots} in the 16          */
/* STE 4096-colour variant (U6WS): parallel 16-colour palettes snapped to the STE's 4-bit gamut. Same
 * centroids + LUTs as the 3-bit ST palettes above; on an STE we hand these (4-bit levels) to the
 * backend instead. g_is_ste gates the choice at every set-palette site. (docs/design/palette_lowcolor.md) */
static int            g_have_ste;           /* 1 = pack carries a U6WS STE palette section          */
static int            g_is_ste;             /* 1 = STE hardware AND U6WS present -> use 4-bit path   */
static uint8_t        g_lc_pal4[48];        /* game 16, STE 4-bit levels                             */
static const uint8_t *g_lcx_pal4;           /* 2 bands x 48, STE 4-bit (into the pak)                */
static const uint8_t *g_convpal4;           /* nconvpal x 48, STE 4-bit (into the pak)               */
/* pick the palette to hand the backend for the current hardware: 4-bit STE array when g_is_ste. */
#define LCPAL(p3,p4)  (g_is_ste ? (p4) : (p3))
/* front-end base 16+LUT (the game palette carried in U6FE) — restored between cutscene scenes on ST.
 * Points into the streamed U6FE, valid only during the front-end phase (freed after). */
static const uint8_t *g_fe_base_lc;
static uint16_t       g_cyc_phase;          /* rotation phase                          */
static char           g_msg[160];          /* latest verb result line              */
static int msgcat(int n,const char*s);     /* fwd: append to g_msg (defined below)  */
static int            g_pending_verb;      /* 0/'L'/'G'/'U'/'T'/'M' awaiting a target */
static int            g_travel_active=0;   /* 1 = auto-walking toward a clicked tile      */
static int            g_travel_tx, g_travel_ty;  /* the click-to-travel destination tile  */
static int            g_show_inv;          /* 1 = inventory list shown in the panel */
static int            g_show_status;        /* 1 = character status view shown in the panel */
static int            g_status_actor=1;     /* which actor the status view shows (1 = Avatar) */

/* party roster (top-right) — now MUTABLE so conversations (JOIN/LEAVE via the
 * VM's in_party[]) actually change it. Starts with the Avatar + the three
 * starting companions; names resolved from the game's CONVERSE data; HP is a
 * placeholder. Icon = each actor's appearance tile (g_atile). */
#define MAXPARTY 8
static struct { int actor, hp; char name[16]; } g_party[MAXPARTY];
static int            g_nparty;
static uint8_t        g_sparty[MAXPARTY]; static int g_nsparty;  /* saved party list (U6WC) */
static char           g_avatar_name[16]="Avatar";  /* player name (chargen result; passed to the conversation VM as the %token) */
static const char    *g_avatar_title="milord";     /* $G honorific: milord (male) / milady (female), U6 get_gender_title */
static int            g_cc_done;                    /* set once character creation completes -> apply g_cc_* to the Avatar */

/* command-icon bar (tiles 400..409: Attack Cast Talk Look Get Drop Move Use Rest Combat) */
#define CMDX 8
#define CMDY 182
#define NCMD 10
/* mouse cursor (decoded from U6MCGA.PTR) + save-under */
#define CURMAX 64
static uint8_t        g_curpix[CURMAX*CURMAX]; static int g_cw,g_ch,g_chx,g_chy;
static uint8_t        g_cursave[CURMAX*CURMAX]; static int g_csx=-1,g_csy;
static int            g_mx,g_my;            /* logical cursor position (0..319,0..199) */

static int g_start_min = 12*60;  /* game-time of day at load (minutes); set from the
                                  * save's clock via the pack header (U6WH). noon default. */

static int tappend(int tl,const char*s){ while(*s&&tl<1023) g_transcript[tl++]=*s++; g_transcript[tl]=0; return tl; }

/* little-endian u32 (CONVERSE/PORTRAIT offset tables are LE in the originals) */
static uint32_t rd4(const uint8_t*p){
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

/* count entries in a 4-byte-offset library (skip leading zeros; first nonzero
 * offset / 4 == number of entries — the table itself precedes the data) */
static int lib_count(const uint8_t*f,int32_t flen){
    if(!f||flen<4) return 0;
    int i=0; while((int32_t)(i*4+4)<=flen && rd4(f+i*4)==0) i++;
    return (int)(rd4(f+i*4)/4);
}
/* pointer+len for entry idx, or NULL. End = next nonzero offset (or EOF). */
static const uint8_t* lib_entry(const uint8_t*f,int32_t flen,int idx,int32_t*outlen){
    if(!f||idx<0) return 0;
    int count=lib_count(f,flen);
    if(idx>=count) return 0;
    int32_t s=(int32_t)rd4(f+idx*4); if(s<=0||s>=flen) return 0;
    int32_t e=flen;
    for(int j=idx+1;j<count;j++){ int32_t o=(int32_t)rd4(f+j*4); if(o){e=o;break;} }
    if(e>flen) e=flen;
    *outlen=e-s; return f+s;
}

/* clock -> game minutes-of-day / hour (1 clock unit == 1 game minute) */
static int clock_minutes(const world_t*w){ return (int)(((uint32_t)g_start_min+w->clock)%(24u*60u)); }
static int clock_hour(const world_t*w){ return clock_minutes(w)/60; }
static uint8_t       *g_bb;            /* backbuffer                          */
static int            g_W;            /* backbuffer width (320)              */

/* row pointer for a LOGICAL y (adds the letterbox offset) */
#define ROW(y) (g_bb + (int32_t)(UIY0+(y))*g_W)

/* ---- chunky primitives (logical coords, letterboxed) -------------------- */
static void ui_frame(int x,int y,int w,int h,uint8_t c)
{ for(int xx=0;xx<w;xx++){ROW(y)[x+xx]=c;ROW(y+h-1)[x+xx]=c;}
  for(int yy=0;yy<h;yy++){ROW(y+yy)[x]=c;ROW(y+yy)[x+w-1]=c;} }
static void ui_px(int x,int y,uint8_t c){ if((unsigned)x<LW&&(unsigned)y<LH) ROW(y)[x]=c; }

/* authentic font (U6.CH): 8 bytes/glyph, MSB-left, glyph index == ASCII */
static void ui_text(int x,int y,const char*s,uint8_t col)
{ for(;*s;s++,x+=8){const uint8_t*g=g_font+(int)(unsigned char)*s*8;
    for(int gy=0;gy<8;gy++){uint8_t b=g[gy];for(int gx=0;gx<8;gx++) if(b&(0x80>>gx)) ui_px(x+gx,y+gy,col);}} }
static int ui_textw(const char*s){int n=0;while(*s++)n++;return n*8;}

/* PROPORTIONAL U6.CH rendering (the original trims each glyph to its used columns) — used for the
 * intro narration so long pages pack into fewer lines, matching the DOS layout. pglyph_w = used
 * columns + 1px gap (space = 4). Global ui_text stays fixed-width (in-game layouts depend on it). */
static int pglyph_w(unsigned char c){
    if(c==' ') return 4;
    const uint8_t*g=g_font+(int)c*8; int mx=0;
    for(int gy=0;gy<8;gy++){ uint8_t b=g[gy]; for(int gx=0;gx<8;gx++) if(b&(0x80>>gx)){ if(gx+1>mx) mx=gx+1; } }
    return mx? mx+1 : 4;
}
static int ptextw(const char*s){ int w=0; for(;*s;s++) w+=pglyph_w((unsigned char)*s); return w; }
static void ptext(int x,int y,const char*s,uint8_t col){
    for(;*s;s++){ unsigned char c=(unsigned char)*s;
        if(c!=' '){ const uint8_t*g=g_font+(int)c*8;
            for(int gy=0;gy<8;gy++){ uint8_t b=g[gy]; for(int gx=0;gx<8;gx++) if(b&(0x80>>gx)) ui_px(x+gx,y+gy,col); } }
        x+=pglyph_w(c); } }
/* word-wrap (s,len — not NUL-term) to lines whose PROPORTIONAL width <= maxpx, centered, from y by
 * dy. draw=0 => just count and return the line count; draw=1 => render + return y past the last. */
static int pwrap(const char*s,int len,int y,int maxpx,int dy,uint8_t col,int draw){
    int i=0, lines=0;
    while(i<len){
        while(i<len && s[i]==' ') i++;
        if(i>=len) break;
        int cut=len, w=0, lastsp=-1, e=i;
        for(; e<len; e++){
            if(s[e]==' ') lastsp=e;
            w += pglyph_w((unsigned char)s[e]);
            if(w>maxpx){ cut=(lastsp>i)?lastsp:e; break; }
        }
        if(e>=len) cut=len;
        if(draw){ char buf[96]; int bl=0; for(int k=i;k<cut && bl<95;k++) buf[bl++]=s[k]; buf[bl]=0;
            ptext((LW-ptextw(buf))/2, y, buf, col); }
        y+=dy; lines++;
        i=(cut>i?cut:i+1);
    }
    return draw ? y : (lines?lines:1);
}

/* ---- regions ------------------------------------------------------------- */
static void blit_paper(void)                       /* PAPER.BMP -> backbuffer */
{ for(int y=0;y<LH;y++) memcpy(ROW(y), g_paper+(int32_t)y*LW, LW); }

/* Restore the right (conversation) panel from PAPER.BMP so text can be redrawn
 * without ghosting the previous glyphs. Covers x>=PANX only — never the map,
 * border or status strip (all at x<PANX). */
static void restore_panel(void)
{ for(int y=MAPY;y<196;y++) memcpy(ROW(y)+PANX, g_paper+(int32_t)y*LW+PANX, LW-PANX); }

/* blit a W*H 8-bit indexed image at (dx,dy); index 0 = transparent (front-end art). */
static void blit_indexed(const uint8_t*pix,int iw,int ih,int dx,int dy)
{ for(int y=0;y<ih;y++){ int py=dy+y; if((unsigned)py>=(unsigned)LH) continue;
    const uint8_t*s=pix+(int32_t)y*iw; uint8_t*d=ROW(py);
    for(int x=0;x<iw;x++){ int px=dx+x; if((unsigned)px>=(unsigned)LW) continue; uint8_t v=s[x]; if(v) d[px]=v; } } }
static void clear_bb(uint8_t col){ for(int y=0;y<LH;y++){ uint8_t*d=ROW(y); for(int x=0;x<LW;x++) d[x]=col; } }

/* blit a W*H indexed image OPAQUELY (all pixels, incl. index 0) at (dx,dy) — for a
 * full-screen intro scene whose own palette defines the background colour. */
static void blit_opaque(const uint8_t*pix,int iw,int ih,int dx,int dy)
{ for(int y=0;y<ih;y++){ int py=dy+y; if((unsigned)py>=(unsigned)LH) continue;
    const uint8_t*s=pix+(int32_t)y*iw; uint8_t*d=ROW(py);
    for(int x=0;x<iw;x++){ int px=dx+x; if((unsigned)px>=(unsigned)LW) continue; d[px]=s[x]; } } }

/* Intro narration PAGES (nuvie intro.lua): each scene holds 1-2 pages; an input (space/click)
 * advances a page while the scene animates. These are OUR paraphrases matching the original's
 * page structure; the authentic text is extracted from U.EXE at build time (Phase 4). */
/* Intro narration = 15 pages, EXTRACTED from the user's own U.EXE into the pack at build time
 * (clean-room; the copyrighted text is never committed) and re-wrapped to 320px here. If U.EXE was
 * absent the pack ships 0 pages and we fall back to OUR paraphrase below (our own words, safe to
 * ship). Page order: living(0..2) storm(3..6) forest(7) stones(8..9) moongate(10..14). */
#define INTRO_NPAGES 15
static const char *g_intro_text[INTRO_NPAGES];    /* -> pack bytes (NOT NUL-term) or g_intro_fb */
static int         g_intro_tlen[INTRO_NPAGES];
static int         g_intro_have;                  /* 1 = real U.EXE narration loaded from pack */
static const char *g_intro_fb[INTRO_NPAGES]={     /* fallback paraphrase (our words) */
    "Upon your world, five seasons have passed since your triumphant homecoming from Britannia.",
    "You have traded a life of peril for a world's quiet peace -- yet no screen replaces fallen friends.",
    "Outside, a chill wind rises...",
    "...and in moments the storm is upon thee, wild and unrelenting.",
    "Tongues of lightning lash the sky, conducting an unceasing crescendo of thunder....",
    "In a cataclysm of sound and light, a bolt of searing blue fire strikes the earth!",
    "Lightning among the stones! Is this a sign from distant Britannia?",
    "You bolt from thy house, running blind in the storm. Into the forest, down the path, to the stones.",
    "Near the stones hangs the smell of damp, blasted earth. You glimpse a tiny obsidian stone in the circle!",
    "Wondering, you pick it up....",
    "From the heart of the stones a softly glowing door ascends in silence!",
    "Memories wash over you as you clutch the stone -- last cast down by Lord British to banish Blackthorn!",
    "But your joy gives way to dread. The gate to Britannia was ever blue -- as blue as the morning sky.",
    "The portal quivers and sinks away, its crimson light wanes.",
    "Desperation makes the choice an easy one.",
};
static int g_intro_page;                          /* current page WITHIN the scene (0-based) */
#define INTRO_NCAP 3
/* g_intro[] caption scenes -> base index into the 15-page list + page count. Storm(3..6) and
 * forest(7) are drawn by their own functions (storm_scene/forest_reveal), not g_intro[] scenes. */
static const int g_intro_base[INTRO_NCAP]  ={ 0, 8, 10 };   /* living, stones, moongate */
static const int g_intro_npages[INTRO_NCAP]={ 3, 2,  5 };
static void intro_caption_page(int gp);           /* fwd: parchment + page gp (def after draw_wrapped) */

/* set the scene palette scaled by num/8 (0=black .. 8=full) — for fade in/out. */
static void intro_fade(const uint8_t*pal,int num)
{ uint8_t p[768]; for(int i=0;i<768;i++){ int v=(int)pal[i]*num/8; p[i]=(uint8_t)(v>63?63:v); }
  rdr_set_palette(p,256); }

/* crimson moongate glow: rotate palette entries 144..159 by `shift` (nuvie stones_rotate_palette
 * cycles the crimson ramp so the gate pulses). Sets the whole palette from `basepal` + the rotate. */
static void intro_crimson_cycle(const uint8_t*basepal,int shift)
{ uint8_t p[768]; memcpy(p,basepal,768);
  for(int i=0;i<16;i++){ int src=144+((i+shift)&15);
    p[(144+i)*3]=basepal[src*3]; p[(144+i)*3+1]=basepal[src*3+1]; p[(144+i)*3+2]=basepal[src*3+2]; }
  rdr_set_palette(p,256); }

/* Which layers draw_intro_frame emits, so the static hold can split the frame into a
 * compose-once background and a per-frame animation overlay (dirty-rect). IF_BG omits the
 * TV mouth + LED clock (the only animated bits); IF_ANIM draws ONLY those over a restored
 * background; IF_FULL (default, e.g. the pan) draws everything. */
enum { IF_FULL=0, IF_BG=1, IF_ANIM=2 };
static int g_hold_part = IF_FULL;

/* TV channel cycler (nuvie tv-program loop): the living-room TV flips between the news anchor
 * (channel 0 = base + talking mouth), the shipped alt-channels, and generated static ("changing
 * channels"). Time-based off the 200Hz clock, so it's loop-rate independent. */
#define TV_SHOW   360                    /* ticks a channel plays (~1.8 s) */
#define TV_STATIC  70                    /* ticks of static between channels (~0.35 s) */
#define TV_UNIT   (TV_SHOW+TV_STATIC)
static void tv_state(uint32_t now, int *st, int *chan, int *fr)   /* *st=1 => static; else channel+frame */
{
    int ntot = 1 + g_tv_nchan;                                    /* anchor + alt-channels */
    uint32_t p = now % ((uint32_t)ntot*TV_UNIT);
    int slot = (int)(p / TV_UNIT);
    if((int)(p % TV_UNIT) >= TV_SHOW){ *st=1; *chan=0; *fr=0; return; }
    *st=0; *chan=slot;
    if(slot==0) *fr = g_tv_nmouth ? (int)((now/28)%g_tv_nmouth) : 0;   /* anchor mouth ~7fps */
    else        *fr = g_tv_fpc    ? (int)((now/24)%g_tv_fpc)    : 0;   /* program frames ~8fps */
}
/* fill the TV screen rect with black/white/grey noise (channel static); rng advances per pixel
 * so it shimmers frame to frame. */
static rng_t g_tv_rng; static int g_tv_rng_ready;
static void draw_tv_static(int x,int y,int w,int h)
{
    if(!g_tv_rng_ready){ rng_seed(&g_tv_rng, 0x5107u ^ plat_ticks_200hz()); g_tv_rng_ready=1; }
    for(int yy=0;yy<h;yy++){ int py=y+yy; if((unsigned)py>=(unsigned)LH) continue; uint8_t*d=ROW(py);
        for(int xx=0;xx<w;xx++){ int px=x+xx; if((unsigned)px>=(unsigned)LW) continue;
            uint32_t r=rng_range(&g_tv_rng,4); d[px]=(uint8_t)(r==0?15:(r==1?25:0)); } }
}

/* draw one intro frame at horizontal pan offset `scroll`: type 1 pans the wide panorama
 * at 1x and the transparent foreground at 2x (parallax); type 0 is a centred still. Adds
 * the narration caption (dark strip idx 254 + white idx 255 text) when show_cap. */
static void draw_intro_frame(int s,int scroll,int show_cap)
{
    int bg = (g_hold_part != IF_ANIM), anim = (g_hold_part != IF_BG);   /* layer gates */
    if(g_intro[s].type==1){                                                        /* PAN scene (living room) */
        if(bg) blit_opaque(g_intro[s].pix,g_intro[s].w,g_intro[s].h,-scroll,0);     /* room 1x */
        if(anim && g_tv_base){                                                     /* TV screen: current channel / static */
            uint32_t now=plat_ticks_200hz(); int st,chan,fr; tv_state(now,&st,&chan,&fr);
            int tx=g_tv_bx-scroll, ty=g_tv_by;
            if(st) draw_tv_static(tx,ty,g_tv_bw,g_tv_bh);                          /* "changing channels" */
            else if(chan==0){ blit_opaque(g_tv_base,g_tv_bw,g_tv_bh,tx,ty);        /* news anchor + mouth */
                if(g_tv_mouth[fr]) blit_indexed(g_tv_mouth[fr],g_tv_mw,g_tv_mh,g_tv_mx-scroll,g_tv_my); }
            else { const uint8_t*cf=g_tv_chan[chan-1][fr];                         /* alt-channel program frame */
                if(cf) blit_opaque(cf,g_tv_bw,g_tv_bh,tx,ty); } }
        if(anim && g_ndigit>=11){                                                  /* live LED clock (ticks) */
            int total=4*60+2+(int)(plat_ticks_200hz()/200);                        /* 4:02 (original), +1 min/sec */
            int h12=(total/60)%12; if(h12==0) h12=12; int mm=total%60;
            int dg[4]={ h12>=10?1:10, h12%10, mm/10, mm%10 };                       /* 10 = blank */
            static const int DX[4]={0,4,0xa,0xe};
            for(int c=0;c<4;c++){ int v=dg[c]; if(v>=0&&v<g_ndigit&&g_digit[v])
                blit_indexed(g_digit[v],g_dig_w[v],g_dig_h[v],g_clk_x+DX[c]-scroll,g_clk_y); } }
        if(bg && g_intro[s].fg) blit_indexed(g_intro[s].fg,LW,g_intro[s].h,-2*scroll,0);  /* avatar 2x */
    } else if(g_intro[s].pix){
        blit_opaque(g_intro[s].pix,g_intro[s].w,g_intro[s].h,(LW-g_intro[s].w)/2,(LH-g_intro[s].h)/2);
        if(g_intro[s].type==2 && g_gate){                                  /* moongate rises from the stones */
            blit_indexed(g_gate,g_gw,g_gh,g_gx,g_gate_y);                   /* gate at its current height */
            if(g_cover) blit_indexed(g_cover,g_cvw,g_cvh,g_cvx,g_cvy); }    /* ground cover drawn OVER it */
    } else clear_bb(254);
    if(show_cap && bg && s<INTRO_NCAP){
        int pg = g_intro_page; if(pg<0) pg=0;
        intro_caption_page(g_intro_base[s]+pg); }                      /* parchment + U.EXE narration */
}

/* save/restore a rectangle of the chunky backbuffer (for the dirty-rect hold: snapshot the
 * clean animation box once, restore it before drawing each new mouth/clock frame). */
static void save_rect(uint8_t*buf,int rx,int ry,int rw,int rh){
    for(int y=0;y<rh;y++) memcpy(buf+(int32_t)y*rw, ROW(ry+y)+rx, (size_t)rw); }
static void restore_rect(const uint8_t*buf,int rx,int ry,int rw,int rh){
    for(int y=0;y<rh;y++) memcpy(ROW(ry+y)+rx, buf+(int32_t)y*rw, (size_t)rw); }

/* blit into an arbitrary-stride chunky buffer (opaque or index-0-transparent). */
static void blit_buf(uint8_t*buf,int bw,int bh,const uint8_t*pix,int iw,int ih,int dx,int dy,int opaque)
{ for(int y=0;y<ih;y++){ int py=dy+y; if((unsigned)py>=(unsigned)bh) continue;
    const uint8_t*s=pix+(int32_t)y*iw; uint8_t*d=buf+(int32_t)py*bw;
    for(int x=0;x<iw;x++){ int px=dx+x; if((unsigned)px>=(unsigned)bw) continue;
        uint8_t v=s[x]; if(opaque||v) d[px]=v; } } }

/* Bake the whole WIDE living-room frame (panorama + TV anchor + clock + avatar, NO caption)
 * into a w x h chunky buffer for the hardware scroll. Everything at its base (1x) position;
 * the hardware pans it as a unit. (The parchment scroll shows only during the hold.) */
static void compose_wide(int s, uint8_t*wf)
{
    int w=g_intro[s].w, h=g_intro[s].h;
    memcpy(wf, g_intro[s].pix, (size_t)w*h);                          /* panorama (opaque) */
    if(g_tv_base){ blit_buf(wf,w,h,g_tv_base,g_tv_bw,g_tv_bh,g_tv_bx,g_tv_by,0);
        int mi=g_tv_nmouth?g_tv_frame%g_tv_nmouth:0;
        if(g_tv_mouth[mi]) blit_buf(wf,w,h,g_tv_mouth[mi],g_tv_mw,g_tv_mh,g_tv_mx,g_tv_my,0); }
    if(g_ndigit>=11){ int total=4*60+2+(int)(plat_ticks_200hz()/200);
        int h12=(total/60)%12; if(h12==0)h12=12; int mm=total%60;
        int dg[4]={h12>=10?1:10,h12%10,mm/10,mm%10}; static const int DX[4]={0,4,0xa,0xe};
        for(int c=0;c<4;c++){ int vv=dg[c]; if(vv>=0&&vv<g_ndigit&&g_digit[vv])
            blit_buf(wf,w,h,g_digit[vv],g_dig_w[vv],g_dig_h[vv],g_clk_x+DX[c],g_clk_y,0); } }
    if(g_intro[s].fg) blit_buf(wf,w,h,g_intro[s].fg,LW,h,0,0,0);       /* avatar (transparent) */
}

/* Intro render-cost instrumentation (BENCH only): per-phase [HOLD|PAN] x per-bucket
 * [compose|c2p+double|output] 200Hz ticks + frame counts. g_iphase gates attribution
 * (-1 = untracked: fades, initial draws, moongate rise). The compose bucket wraps
 * draw_intro_frame; the c2p/output split comes from the backend (rdr_bench_present) so the
 * hot c2p loop isn't perturbed by per-row timing. See docs/design/performance.md. */
#ifdef BENCH
enum { IPH_HOLD=0, IPH_PAN=1, IPB_COMPOSE=0, IPB_C2P=1, IPB_OUT=2 };
static uint32_t g_iph[2][3]; static uint32_t g_iph_n[2]; static int g_iphase = -1;
extern void rdr_bench_present(uint32_t*, uint32_t*);
#define BENCH_PHASE(p) (g_iphase=(p))
static void intro_compose(int s,int scroll,int cap){
    if(g_iphase<0){ draw_intro_frame(s,scroll,cap); return; }
    uint32_t t=plat_ticks_200hz(); draw_intro_frame(s,scroll,cap);
    g_iph[g_iphase][IPB_COMPOSE]+=plat_ticks_200hz()-t;
}
static void intro_present(void){
    if(g_iphase<0){ rdr_present_doubled(LH); return; }
    uint32_t c0,o0,c1,o1; rdr_bench_present(&c0,&o0);
    rdr_present_doubled(LH);
    rdr_bench_present(&c1,&o1);
    g_iph[g_iphase][IPB_C2P]+=c1-c0; g_iph[g_iphase][IPB_OUT]+=o1-o0; g_iph_n[g_iphase]++;
}
static void intro_present_rect(int x,int y,int w,int h){   /* dirty-rect present (hold) */
    if(g_iphase<0){ rdr_present_rect_doubled(x,y,w,h,LH); return; }
    uint32_t c0,o0,c1,o1; rdr_bench_present(&c0,&o0);
    rdr_present_rect_doubled(x,y,w,h,LH);
    rdr_bench_present(&c1,&o1);
    g_iph[g_iphase][IPB_C2P]+=c1-c0; g_iph[g_iphase][IPB_OUT]+=o1-o0; g_iph_n[g_iphase]++;
}
static void intro_scroll16(const uint8_t*wf,int cw,int px){   /* incremental scroll step (pan) */
    if(g_iphase<0){ rdr_scroll_left16(wf,cw,LH,px); return; }
    uint32_t t=plat_ticks_200hz(); rdr_scroll_left16(wf,cw,LH,px);
    g_iph[g_iphase][IPB_C2P]+=plat_ticks_200hz()-t; g_iph_n[g_iphase]++;   /* shift+edge c2p */
}
#else
#define BENCH_PHASE(p)              ((void)0)
#define intro_compose(s,scroll,cap) draw_intro_frame((s),(scroll),(cap))
#define intro_present()             rdr_present_doubled(LH)
#define intro_present_rect(x,y,w,h) rdr_present_rect_doubled((x),(y),(w),(h),LH)
#define intro_scroll16(wf,cw,px)    rdr_scroll_left16((wf),(cw),LH,(px))
#endif

/* INTRO CUTSCENE (front-end Phase 2): each scene carries its OWN palette (the intro uses
 * PALETTES.INT, not the game palette). The living room is a horizontal PAN (nuvie
 * scroll_lounge: room 1x, avatar/remote foreground 2x); the others are stills. Scenes
 * fade in, hold ~6s (or until a key) with the real narration, then transition out (pan or
 * fade). Esc ends it. The game palette is restored on exit. */
/* poll for an intro "advance" (nuvie: click OR key advances a page): returns 2 on Esc (skip the
 * whole intro), 1 on any key / mouse-button press (edge), 0 otherwise. */
static int g_intro_mb;
static int intro_advance(void)
{
    int key=plat_input_raw();
    if(key){ if((key&0xFF)==0x1b) return 2; return 1; }
    int mx,my,mb; plat_mouse(&mx,&my,&mb);
    int adv = (mb && !g_intro_mb); g_intro_mb=mb;
    return adv ? 1 : 0;
}
#define PAGE_MAXTICKS (10*200)   /* auto-advance a page after ~10s of no input (attract/headless) */

/* STORM WINDOW scene (nuvie window_sequence, INTRO_2.SHP / PALETTES.INT 2): a curtained window
 * onto a stormy pine forest. Static scene baked; lightning = a bright random bolt over it + a
 * whole-scene palette flash; the big strike appears from page 3. 4 input-advanced pages. Called
 * from run_intro between the living room and the stones. Narration = U.EXE pages 3..6. */
/* Compose the storm scene LAYERED so lightning sits BEHIND the window (matching nuvie: the strike/
 * flash sprites are drawn before the frame). Frameless forest bg + the (from page 3) big strike +
 * an optional random flash `bolt`, THEN the window overlay (glass+frame) ON TOP so its mullions
 * occlude the lightning, then this page's narration parchment. bolt=NULL for no flash. Falls back
 * to the baked base (frame in the bg) only if the layered assets are missing. */
static void storm_compose_ex(int page, const uint8_t *bolt, int bw, int bh, int bx, int by)
{
    if(g_forest_base && g_win_ov){
        blit_opaque(g_forest_base,g_forest_w,g_forest_h,0,0);                 /* forest, no frame */
        if(page>=2 && g_storm_nstr>0)                                         /* big strike BEHIND window */
            blit_indexed(g_storm_str[0],g_storm_strw[0],g_storm_strh[0],g_storm_sx,g_storm_sy);
        if(bolt) blit_indexed(bolt,bw,bh,bx,by);                             /* random flash BEHIND window */
        blit_indexed(g_win_ov,g_win_ow,g_win_oh,0,0);                        /* frame/glass ON TOP -> occludes */
    } else {                                                                 /* fallback: baked base */
        blit_opaque(g_storm_base,g_storm_w,g_storm_h,0,0);
        if(page>=2 && g_storm_nstr>0)
            blit_indexed(g_storm_str[0],g_storm_strw[0],g_storm_strh[0],g_storm_sx,g_storm_sy);
        if(bolt) blit_indexed(bolt,bw,bh,bx,by);
    }
    intro_caption_page(3+page);                              /* storm narration = U.EXE pages 3..6 */
}
static void storm_compose(int page){ storm_compose_ex(page,(const uint8_t*)0,0,0,0,0); }
/* FOREST REVEAL (nuvie window_sequence tail): after the storm pages, the window frame slides off
 * to the left to reveal the FRAMELESS storm forest (same sky/trees/ground) — the forest bg stays
 * put so the reveal is seamless — then the "you bolt from thy house...to the stones" narration
 * (U.EXE page 7) holds over the forest path. Runs at the tail of storm_scene, before its fade-out. */
static void forest_reveal(void)
{
    if(!g_forest_base || !g_win_ov) return;
    for(int x=0; x>=-g_win_ow; x-=16){                        /* the window frame slides off left */
        blit_opaque(g_forest_base,g_forest_w,g_forest_h,0,0);
        blit_indexed(g_win_ov,g_win_ow,g_win_oh,x,0);
        rdr_present_doubled(LH); Vsync();
        if(plat_input_raw()){ x=-g_win_ow; }                 /* a keypress snaps to fully revealed */
    }
    blit_opaque(g_forest_base,g_forest_w,g_forest_h,0,0);     /* the "...to the stones" narration */
    intro_caption_page(7);                                    /* forest narration = U.EXE page 7 */
    rdr_present_doubled(LH);
    uint32_t t0=plat_ticks_200hz();
    for(;;){ Vsync(); int a=intro_advance(); if(a) break;
        if((uint32_t)(plat_ticks_200hz()-t0)>=PAGE_MAXTICKS) break; }
}
static void storm_scene(void)
{
    if(!g_storm_base || g_storm_nbolt<=0) return;
    rng_t r; rng_seed(&r,0x570fu ^ plat_ticks_200hz());
    uint8_t flashpal[768];                                    /* whole-scene lightning brighten */
    for(int i=0;i<768;i++){ int v=(int)g_storm_pal[i]+14; flashpal[i]=(uint8_t)(v>63?63:v); }
    storm_compose(0);                                         /* fade in on page 0 (flip mode, tear-free) */
    for(int k=0;k<=8;k++){ intro_fade(g_storm_pal,k); Vsync(); }
    rdr_present_doubled(LH); rdr_set_palette((uint8_t*)g_storm_pal,256), rdr_set_lowcolor(g_storm_lc,g_storm_lc+48);
    int key=0;
    for(int page=0; page<4 && (key&0xFF)!=0x1b; page++){
        storm_compose(page); rdr_present_doubled(LH);        /* full present of this page */
        uint32_t t0=plat_ticks_200hz();
        for(;;){
            if(rng_range(&r,22)==0){                          /* a lightning bolt flashes */
                int bi=(int)rng_range(&r,(uint32_t)g_storm_nbolt);
                int bx=(int)rng_range(&r,300)-30, by=(int)rng_range(&r,90)-10;
                storm_compose_ex(page,g_storm_bolt[bi],g_storm_boltw[bi],g_storm_bolth[bi],bx,by);  /* bolt BEHIND the frame */
                rdr_set_palette(flashpal,256); rdr_present_doubled(LH);
                int hold=2+(int)rng_range(&r,3); for(int h=0;h<hold;h++) Vsync();
                storm_compose(page); rdr_set_palette((uint8_t*)g_storm_pal,256), rdr_set_lowcolor(g_storm_lc,g_storm_lc+48); rdr_present_doubled(LH);
            }
            Vsync();
            int a=intro_advance(); if(a){ key=(a==2)?0x1b:0; break; }
            if((uint32_t)(plat_ticks_200hz()-t0)>=PAGE_MAXTICKS) break;
        }
    }
    if((key&0xFF)!=0x1b) forest_reveal();                     /* window slides off -> forest path + narration */
    for(int k=8;k>=0;k--){ intro_fade(g_storm_pal,k); Vsync(); }
}

/* blit the narration parchment + scene `s` page `pg` (U.EXE narration, word-wrapped). */
static void intro_draw_caption(int s,int pg)
{
    if(s>=0 && s<INTRO_NCAP && pg>=0) intro_caption_page(g_intro_base[s]+pg);
}

/* STONES obsidian-pickup (nuvie stones_sequence): page 0 glimpse the obsidian (INTRO_3 img 3 @
 * 150,100) on the stones bg; page 1 "Wondering, you pick it up...." — the hand (img 1) rises from
 * y=0xc7 to 0x54 to grab it (dirty-rect the hand column), then the stone is gone (bg img 5).
 * Replaces the generic hold for the stones scene. Input/auto-advanced like every page. */
static void stones_pickup(int s)
{
    const uint8_t *bg=g_intro[s].pix;
    static uint8_t pbg[160*160];                     /* clean bg+obsidian of the hand column */
    int hx=0xbd, hy0=0xc7, hy1=0x54, ox=0x96, oy=0x64;
    int rx=hx&~15, rw=((hx+g_stn_hw+15)&~15)-rx; if(rx+rw>LW) rw=LW-rx;
    int ry=hy1, rh=LH-ry; if((int32_t)rw*rh>(int32_t)sizeof pbg) rh=(int)(sizeof pbg/(rw?rw:1));
    int key=0;
    /* --- page 0: glimpse the obsidian --- */
    g_intro_page=0;
    blit_opaque(bg,g_intro[s].w,g_intro[s].h,0,0);
    blit_indexed(g_stn_obs,g_stn_ow,g_stn_oh,ox,oy);
    intro_draw_caption(s,0); rdr_present_doubled(LH);
    { uint32_t t0=plat_ticks_200hz();
      for(;;){ Vsync(); int a=intro_advance(); if(a){ if(a==2) return; break; }
        if((uint32_t)(plat_ticks_200hz()-t0)>=PAGE_MAXTICKS) break; } }
    /* --- page 1: the hand rises to pick it up --- */
    g_intro_page=1;
    blit_opaque(bg,g_intro[s].w,g_intro[s].h,0,0);
    blit_indexed(g_stn_obs,g_stn_ow,g_stn_oh,ox,oy);
    intro_draw_caption(s,1);
    save_rect(pbg,rx,ry,rw,rh);                       /* clean column (bg+obsidian, no hand) */
    rdr_present_doubled(LH); rdr_flip_end();          /* single-buffer for the hand dirty-rect */
    for(int hy=hy0; hy>=hy1; hy-=2){                  /* hand rises into the circle */
        restore_rect(pbg,rx,ry,rw,rh);
        blit_indexed(g_stn_hand,g_stn_hw,g_stn_hh,hx,hy);
        intro_present_rect(rx,ry,rw,rh);
        Vsync();
        int a=plat_input_raw(); if(a){ if((a&0xFF)==0x1b) key=0x1b; break; }
    }
    /* stone picked up: swap to bg5 (obsidian gone), hold for input */
    if((key&0xFF)!=0x1b){
        blit_opaque(g_stn_bg5,g_stn_b5w,g_stn_b5h,0,0); intro_draw_caption(s,1);
        rdr_present_doubled(LH);
        uint32_t t0=plat_ticks_200hz();
        for(;;){ Vsync(); int a=intro_advance(); if(a) break;
            if((uint32_t)(plat_ticks_200hz()-t0)>=PAGE_MAXTICKS) break; }
    }
    rdr_flip_begin();                                 /* back to double-buffer for the fade/next scene */
}

/* dither-blit: draw `pix` but only pixels an ordered 4x4 Bayer pattern passes (keep 16=full,
 * 0=none) — a cheap dissolve. index 0 transparent. */
static void blit_dither(const uint8_t*pix,int iw,int ih,int dx,int dy,int keep)
{ static const int BAY[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
  for(int y=0;y<ih;y++){ int py=dy+y; if((unsigned)py>=(unsigned)LH) continue;
    const uint8_t*s=pix+(int32_t)y*iw; uint8_t*d=ROW(py);
    for(int x=0;x<iw;x++){ int px=dx+x; if((unsigned)px>=(unsigned)LW) continue;
      uint8_t v=s[x]; if(v && BAY[((y&3)<<2)|(x&3)]<keep) d[px]=v; } } }

/* AVATAR-into-gate climax (nuvie stones_sequence tail): the Avatar walks up into the risen
 * crimson moongate (the "figure"), dissolves as it enters, then the gate sinks back. Full
 * presents in flip mode (tear-free); the crimson ramp keeps cycling + the gate jitters. */
static void avatar_climax(int s)
{
    if(g_av_n<=0 || !g_gate || !g_intro[s].pix) return;
    const uint8_t *bg=g_intro[s].pix, *pal=g_intro[s].pal;
    int gy=0x5, cs=0; rng_t r; rng_seed(&r,0x4adau ^ plat_ticks_200hz());
    #define STAGE_SCENE(gyv) do{ blit_opaque(bg,g_intro[s].w,g_intro[s].h,0,0); \
        blit_indexed(g_gate,g_gw,g_gh,g_gx+(int)rng_range(&r,2),(gyv)+(int)rng_range(&r,3)); \
        if(g_cover) blit_indexed(g_cover,g_cvw,g_cvh,g_cvx,g_cvy); }while(0)
    for(int i=0;i<g_av_n;i++){                         /* the Avatar walks up into the gate */
        STAGE_SCENE(gy);
        blit_indexed(g_av_frame[i],g_av_w[i],g_av_h[i],g_av_ox[i]-2,g_av_oy[i]+0x12-3*i);
        intro_crimson_cycle(pal,++cs); rdr_present_doubled(LH); Vsync();
        if(plat_input_raw()){ i=g_av_n; }
    }
    for(int keep=14; keep>=0; keep-=2){               /* ...and dissolves as it enters */
        int L=g_av_n-1;
        STAGE_SCENE(gy);
        blit_dither(g_av_frame[L],g_av_w[L],g_av_h[L],g_av_ox[L]-2,g_av_oy[L]+0x12-3*L,keep);
        intro_crimson_cycle(pal,++cs); rdr_present_doubled(LH); Vsync(); Vsync();
    }
    for(gy=0x5; gy<0x64; gy+=3){                       /* the gate sinks back into the earth */
        STAGE_SCENE(gy);
        intro_crimson_cycle(pal,++cs); rdr_present_doubled(LH); Vsync();
    }
    #undef STAGE_SCENE
    rdr_set_palette((uint8_t*)pal,256);               /* un-rotate before the scene's fade-out */
}

static void run_intro(void)
{
    { int gk,gd=0; while((gk=plat_input_raw())&&++gd<64){ (void)gk; } }  /* flush stale boot keys */
    g_intro_mb=0;
    rdr_flip_begin();                                            /* tear-free double buffer */
    for(int s=0;s<g_nintro;s++){
        const uint8_t *pal=g_intro[s].pal;
        if(g_intro[s].lc) rdr_set_lowcolor(g_intro[s].lc, g_intro[s].lc+48);  /* ST: this scene's own 16 (no-op on TT) */
        int panlen = (g_intro[s].type==1) ? (g_intro[s].w - LW) : 0;   /* 640-320 = 320 px (pan only) */
        if(g_intro[s].type==2) g_gate_y=0x64;                     /* gate starts hidden in the earth */
        g_intro_page=0;
        draw_intro_frame(s,0,1); rdr_present_doubled(LH);
        for(int k=0;k<=8;k++){ intro_fade(pal,k); Vsync(); }      /* fade in */
        if(g_intro[s].type==2 && g_gate){                         /* the moongate ASCENDS (nuvie 0x64->0x5) */
            for(int gy=0x64; gy>0x5; gy-=2){ g_gate_y=gy;
                draw_intro_frame(s,0,1); rdr_present_doubled(LH); }
            g_gate_y=0x5; }
        int key=0;
        int animated = (g_intro[s].type==1 && g_tv_base && g_ndigit>=11);
        int npages = (s<INTRO_NCAP) ? g_intro_npages[s] : 1;
        if(animated){
            /* PAGE-DRIVEN, input-advanced hold. The TV/clock box is dirty-rect animated (fast);
             * a page change does one full recompose+present (its new caption). Single-buffer for
             * the whole scene so the rect present hits the visible screen. (intro_sequence.md) */
            int bx0=(g_clk_x<g_tv_bx?g_clk_x:g_tv_bx);            /* bbox of the clock cells + full TV screen */
            int bx1=((g_clk_x+0x12)>(g_tv_bx+g_tv_bw)?(g_clk_x+0x12):(g_tv_bx+g_tv_bw));
            int by0=(g_clk_y<g_tv_by?g_clk_y:g_tv_by);
            int by1=((g_clk_y+8)>(g_tv_by+g_tv_bh)?(g_clk_y+8):(g_tv_by+g_tv_bh));
            int rx=bx0&~15, rw=((bx1+15)&~15)-rx;                 /* align x/w to 16px (c2p groups) */
            if(rx+rw>LW) rw=LW-rx;
            int ry=by0, rh=by1-by0; if(ry+rh>LH) rh=LH-ry;
            static uint8_t holdbg[96*112];                        /* clean bg of the box (typ ~65x67) */
            if((int32_t)rw*rh > (int32_t)sizeof holdbg) rh=(int)(sizeof holdbg/(rw?rw:1));
            rdr_flip_end();                                       /* single-buffer: rect present -> visible */
            for(int page=0; page<npages && (key&0xFF)!=0x1b; page++){
                g_intro_page=page;
                g_hold_part=IF_BG;  draw_intro_frame(s,0,1); g_hold_part=IF_FULL;   /* bg incl this page's caption */
                save_rect(holdbg, rx,ry,rw,rh);                   /* snapshot the clean animation box */
                g_tv_frame=(int)(plat_ticks_200hz()/28);
                g_hold_part=IF_ANIM; draw_intro_frame(s,0,1); g_hold_part=IF_FULL; /* initial TV over bg */
                rdr_present_doubled(LH);                          /* full present of this page */
                BENCH_PHASE(IPH_HOLD);
                int last_sig=-1, last_min=-1; uint32_t t0=plat_ticks_200hz();
                for(;;){
                    uint32_t now=plat_ticks_200hz();
                    int st,chan,fr; tv_state(now,&st,&chan,&fr);
                    g_tv_frame=(int)(now/28);                     /* for compose_wide's frozen pan frame */
                    int minute=(int)(now/200);
                    int sig = st ? (int)now : (chan*10+fr);       /* static: redraw each frame; else on frame change */
                    if(sig!=last_sig || minute!=last_min){
                        restore_rect(holdbg, rx,ry,rw,rh);
#ifdef BENCH
                        uint32_t tc=plat_ticks_200hz();
#endif
                        g_hold_part=IF_ANIM; draw_intro_frame(s,0,1); g_hold_part=IF_FULL;
#ifdef BENCH
                        g_iph[IPH_HOLD][IPB_COMPOSE]+=plat_ticks_200hz()-tc;
#endif
                        intro_present_rect(rx,ry,rw,rh);
                        last_sig=sig; last_min=minute;
                    }
                    Vsync();
                    int a=intro_advance(); if(a){ key=(a==2)?0x1b:0; break; }
                    if((uint32_t)(now-t0) >= PAGE_MAXTICKS) break;   /* auto-advance */
                }
                BENCH_PHASE(-1);
            }
            /* stay SINGLE-buffer: the incremental pan below writes the visible screen directly */
        } else if(g_intro[s].type==0 && g_stn_hand){
            stones_pickup(s);                          /* stones: glimpse the obsidian + hand picks it up */
        } else {
            /* static scene (moongate): one present per page, input-advanced. The moongate (type 2)
             * PULSES: rotate the crimson ramp (144..159) each few frames while it holds. */
            int cyc = (g_intro[s].type==2);
            for(int page=0; page<npages && (key&0xFF)!=0x1b; page++){
                g_intro_page=page;
                intro_compose(s,0,1); intro_present();
                uint32_t t0=plat_ticks_200hz(); int cf=0, cs=0;
                for(;;){ Vsync();
                    if(cyc && ++cf>=5){ cf=0; intro_crimson_cycle(pal,++cs); }
                    int a=intro_advance(); if(a){ key=(a==2)?0x1b:0; break; }
                    if((uint32_t)(plat_ticks_200hz()-t0) >= PAGE_MAXTICKS) break; }
            }
            if(cyc) rdr_set_palette((uint8_t*)pal,256);   /* restore the un-rotated palette before fade-out */
        }
        if((key&0xFF)==0x1b){ for(int k=8;k>=0;k--){ intro_fade(pal,k); Vsync(); } break; }
        if(panlen>0){                                             /* pan the room away */
            static uint8_t wf[640*200];
            compose_wide(s, wf);                                  /* bake the whole frame once (1x, no parallax) */
            if(scroll_begin(wf, g_intro[s].w, g_intro[s].h)){     /* HARDWARE fine-scroll pan (Falcon/STe, later) */
                for(int sx=0; sx<=panlen; sx+=2) scroll_pan(sx);  /* base+$FF8265, VBL-committed */
                draw_intro_frame(s,panlen,1); rdr_present_doubled(LH);  /* prep g_screen = final */
                scroll_end();                                     /* video back to g_screen */
            } else {                                              /* INCREMENTAL CPU scroll (TT/Falcon) */
                for(int y=0;y<g_intro[s].h;y++)                   /* baseline: window wf[0..320] (pan_x=0) */
                    memcpy(ROW(y), wf+(int32_t)y*g_intro[s].w, LW);
                rdr_present_doubled(LH);                          /* one full present of the start of the pan */
                BENCH_PHASE(IPH_PAN);
                for(int px=16; px<=panlen; px+=16){               /* shift 16px + c2p the new edge column */
                    Vsync();                                      /* start the shift at vblank (limits tearing) */
                    intro_scroll16(wf, g_intro[s].w, px);
                    if((key=plat_input_raw())) break; }
                BENCH_PHASE(-1);
            }
            rdr_flip_begin();                                     /* re-enter double-buffer for the next scene */
        }
        if(g_intro[s].type==2) avatar_climax(s);                  /* Avatar walks into the moongate + it sinks */
        for(int k=8;k>=0;k--){ intro_fade(pal,k); Vsync(); }      /* fade out */
        if(s==0){ int gk,gd=0; while((gk=plat_input_raw())&&++gd<64){(void)gk;} g_intro_mb=0;
            storm_scene(); }                                      /* window/storm scene after the living room */
    }
    rdr_flip_end();                                              /* back to single-buffer video */
    rdr_set_palette(g_basepal,256);                               /* restore game/menu palette */
    if(g_fe_base_lc) rdr_set_lowcolor(g_fe_base_lc, g_fe_base_lc+48);  /* ST: back to the base 16 */
}

/* ---- CHARACTER CREATION (docs/rules/character_creation.md) --------------------------------
 * The gypsy's virtue tournament: 8 virtues, single-elimination (4->2->1 = 7 questions). Each
 * question pits two virtues; the chosen one wins and adjusts Str/Dex/Int. NOTE: the question
 * PROMPTS here are our OWN generic wording — the authentic U.EXE texts + gypsy visuals are a
 * later phase. Output: name, gender, portrait, Str/Dex/Int for a new game. */
static const char *const CC_VIRTUE[8] = {
    "Honesty","Compassion","Valor","Justice","Sacrifice","Honor","Spirituality","Humility" };
static const uint8_t CC_ADJ[8][3] = {   /* +Str,+Dex,+Int per WINNING virtue (spec table) */
    {0,0,2},{0,2,0},{2,0,0},{0,1,1},{1,1,0},{1,0,1},{1,1,1},{0,0,0} };

static char g_cc_name[16]; static int g_cc_gender, g_cc_portrait, g_cc_str, g_cc_dex, g_cc_int, g_cc_virtue;

/* 0-based question index for a virtue pair — canonical U6 order, A = lower-indexed virtue,
 * B = higher. 28 questions = C(8,2); the block runs Honesty|* , then Compassion|* , ... */
static int cc_question_index(int va,int vb){
    int lo=va<vb?va:vb, hi=va<vb?vb:va;
    return lo*7 - lo*(lo-1)/2 + (hi-lo) - 1;
}

/* draw text (ptr,len — NOT NUL-terminated) word-wrapped to <=maxc chars/line, each line
 * centered, from y downward by dy. Returns the y past the last line. (Question prose is
 * pre-flowed; we re-wrap it for our 320px screen.) */
static int draw_wrapped(const char*s,int len,int y,int maxc,int dy,uint8_t col){
    int i=0;
    while(i<len){
        while(i<len && s[i]==' ') i++;                 /* trim leading spaces */
        if(i>=len) break;
        int e=i, lastsp=-1;
        while(e<len && (e-i)<maxc){ if(s[e]==' ') lastsp=e; e++; }
        int cut = (e>=len) ? len : (lastsp>i ? lastsp : e);   /* break at last space, else hard */
        char buf[48]; int bl=0; for(int k=i;k<cut && bl<47;k++) buf[bl++]=s[k]; buf[bl]=0;
        ui_text((LW-ui_textw(buf))/2, y, buf, col);
        y+=dy; i=(cut>i?cut:i+1);
    }
    return y;
}
/* draw the narration parchment + intro page `gp` (U.EXE text or paraphrase fallback), PROPORTIONALLY
 * word-wrapped to the parchment width and VERTICALLY CENTERED. gp indexes the 15-page list. */
static void intro_caption_page(int gp){
    int sy = g_scroll_h>0 ? LH-g_scroll_h : LH-34;                 /* parchment at the bottom */
    if(g_scroll) blit_indexed(g_scroll,g_scroll_w,g_scroll_h,(LW-g_scroll_w)/2,sy);
    else for(int y=LH-34;y<LH-1;y++){ uint8_t*d=ROW(y); for(int xx=0;xx<LW;xx++) d[xx]=254; }
    if(gp<0 || gp>=INTRO_NPAGES || !g_intro_text[gp]) return;
    int ph = g_scroll_h>0 ? g_scroll_h : 34;                       /* parchment height */
    int maxpx = (g_scroll_w>40 ? g_scroll_w-24 : 288);             /* usable width (inside torn edges) */
    int nl = pwrap(g_intro_text[gp], g_intro_tlen[gp], 0, maxpx, 0, 0, 0);
    int dy = (ph-8)/(nl>0?nl:1); if(dy>9) dy=9; if(dy<7) dy=7;      /* comfy spacing, tighten if needed */
    int ty = sy + (ph - nl*dy)/2 + 1; if(ty < sy+2) ty = sy+2;      /* vertically center the text block */
    pwrap(g_intro_text[gp], g_intro_tlen[gp], ty, maxpx, dy, 62, 1);  /* ink 0x3e */
}

/* --- gypsy casting scene + nuvie's exact pour animation (create_character gypsy_vial_anim) ----- */

/* blit GYPSY.SHP raw shape `sh` (index 0 transparent) at absolute (x,y). */
static void gblit(int sh,int x,int y){
    if(sh<0 || sh>=g_ngsh || !g_gsh[sh] || g_gsh_w[sh]<=0) return;
    blit_indexed(g_gsh[sh], g_gsh_w[sh], g_gsh_h[sh], x, y);
}

/* --- jar bubbles (nuvie image_bubble_effect / image_bubble_effect_add_color) --- */

/* accumulate a poured virtue's liquid colour into the bubble palette (max 8, per nuvie). */
static void bubble_add_color(int col){ if(g_nflask<8) g_flask_col[g_nflask++]=(uint8_t)col; }

/* re-randomise the jar liquid: every non-transparent pixel of a WORKING COPY (g_jarbub) becomes a
 * random one of the accumulated virtue colours — the churning "essence". Refresh the copy from the
 * pack shape (silhouette) when the level changes. Never mutates the shared g_gsh[] pack data. */
static void bubble_effect(void){
    if(g_nflask<=0 || g_jar_lvl<1) return;
    int sh=8+g_jar_lvl; if(sh<0 || sh>=g_ngsh || !g_gsh[sh]) return;
    if(g_jarbub_lvl!=g_jar_lvl){                                  /* new level -> re-copy the liquid silhouette */
        int nb=g_gsh_w[sh]*g_gsh_h[sh]; if(nb>(int)sizeof g_jarbub) nb=(int)sizeof g_jarbub;
        for(int i=0;i<nb;i++) g_jarbub[i]=g_gsh[sh][i];
        g_jarbub_w=g_gsh_w[sh]; g_jarbub_h=g_gsh_h[sh]; g_jarbub_lvl=g_jar_lvl;
    }
    int n=g_jarbub_w*g_jarbub_h; if(n>(int)sizeof g_jarbub) n=(int)sizeof g_jarbub;
    for(int i=0;i<n;i++) if(g_jarbub[i]!=0) g_jarbub[i]=g_flask_col[rng_range(&g_bubrng,(uint32_t)g_nflask)];
}

/* nuvie gypsy_update_bubbles: fire the effect every 4th call (counter 0 -> effect + reset to 3). */
static void gypsy_update_bubbles(void){
    if(g_jar_lvl<1 || g_nflask<=0) return;
    if(g_bubble_ctr==0){ bubble_effect(); g_bubble_ctr=3; } else g_bubble_ctr--;
}

/* vial geometry (nuvie sprite positions; glass y=66, liquid y=77). vial_liquid shape at level L
 * (1..3) = 0x42 + GV_IMG_OFF[v] + (L-3) (GV_IMG_OFF = the full/level-3 set). */
static const int GV_VIAL_X[8]={41,62,83,104,200,221,242,263};
static const int GV_LIQ_X[8] ={44,65,86,107,203,224,245,266};
static const int GV_IMG_OFF[8]={2,5,8,0xB,0x18,0x1B,0x1E,0x21};

/* composite the casting scene: bg -> jar liquid (behind glass) -> jar glass -> each vial's
 * liquid+glass at its current level (the vial being poured is hidden, carried by the hand) ->
 * the pour transient sprites (arm, hand, the lifted vial's liquid, the pour stream). */
static void draw_gypsy_scene(void){
    blit_opaque(g_gyp_scene,g_gyp_w,g_gyp_h,0,0);
    if(g_jar_lvl>0){                                             /* jar_liquid = shape 8+level, rises y-1/level */
        if(g_nflask>0 && g_jarbub_lvl==g_jar_lvl)                /* churning "essence": the speckled working copy */
            blit_indexed(g_jarbub, g_jarbub_w, g_jarbub_h, 146, 0x53 - g_jar_lvl);
        else gblit(8+g_jar_lvl, 146, 0x53 - g_jar_lvl);
    }
    gblit(16, 142, 56);                                          /* jar glass */
    for(int v=0; v<8; v++){
        if(v==g_pour_vial) continue;                            /* glass hidden while lifted */
        int L=g_vial_lvl[v];
        if(L>0) gblit(0x42 + GV_IMG_OFF[v] + (L-3), GV_LIQ_X[v], 77);
        gblit(v<4?20:23, GV_VIAL_X[v], 66);                     /* vial glass (20 left / 23 right) */
    }
    if(g_arm_sh>=0)  gblit(g_arm_sh, g_arm_x, g_arm_y);
    if(g_hand_sh>=0) gblit(g_hand_sh, g_hand_x, g_hand_y);
    if(g_vl_sh>=0)   gblit(g_vl_sh, g_vl_x, g_vl_y);            /* the lifted vial's liquid, following the hand */
    if(g_pour_sh>=0) gblit(g_pour_sh, g_pour_x, g_pour_y);      /* the pour stream */
}

/* nuvie gypsy_start_pouring: show the pour stream for (vial,level) + advance the jar one level. */
static void gypsy_start_pour(int vial,int level){
    static const int pour_img[24]={
        0x2B,0x43,0x56,0x6B,0x24,0x74,0x19,0x2C,
        0x2C,0x44,0x57,0x6C,0x23,0x73,0x18,0x2B,
        0x2E,0x45,0x58,0x6D,0x22,0x72,0x17,0x2A };
    static const int pour_y[3]={0x32,0x37,0x40};
    static const int pour_x[24]={
        0x92,0x92,0x92,0x92,0xA9,0xA9,0xA9,0xA9,
        0x91,0x91,0x91,0x91,0xA9,0xA9,0xA9,0xA9,
        0x94,0x94,0x94,0x94,0xAA,0xAA,0xAA,0xAA };
    if(level>=1 && level<=3){
        int idx=(3-level)*8 + vial, img=pour_img[idx];
        img += (vial>=6)?9:0x42;                 /* nuvie: vial_num>6 (1-based) => 0-based vials 6,7 */
        g_pour_sh=img; g_pour_y=pour_y[3-level]-20; g_pour_x=pour_x[idx];
    }
    if(g_jar_lvl<7) g_jar_lvl++;                  /* the essence accumulates in the central jar */
    { static const int vial_colors[8]={239,14,231,103,228,5,15,219};   /* nuvie per-virtue liquid colours */
      bubble_add_color(vial_colors[vial]); g_bubble_ctr=0; }            /* start bubbling with this virtue's hue */
}
static void gypsy_stop_pour(void){ g_pour_sh=-1; }

/* nuvie gypsy_vial_anim_liquid: position the poured vial's liquid sprite to follow the hand. */
static void gvl_liquid(int img_num,int vial,int level,int hand_x,int hand_y){
    if(level==3) level=2;
    if(level<=0){ g_vl_sh=-1; return; }
    if(img_num==0xf) return;
    int si=0;
    if(img_num>0xf){
        if(img_num==0x12) si=1;
        else if(img_num>0x12){ if(img_num==0x25) return; if(img_num==0x2e) si=2; }
        else { if(img_num==0x10) si=2; else if(img_num==0x11) si=0; }
    } else { int t=img_num-9; if(t==0) si=0; else if(t==1) si=1; else if(t>=2&&t<=5) si=3; }
    static const int vliq[96]={
        0x3A,0x4F,0x64,0x7C,0x28,0x7E,0x20,0x34,
        0x36,0x4B,0x60,0x76,0x26,0x78,0x1C,0x30,
        0x36,0x4B,0x60,0x76,0x26,0x78,0x1C,0x30,
        0x3C,0x51,0x66,0x7F,0x14,0x5F,0x22,0x36,
        0x38,0x4D,0x62,0x79,0x27,0x7B,0x1E,0x32,
        0x38,0x4D,0x62,0x79,0x27,0x7B,0x1E,0x32,
        0x40,0x53,0x68,0x81,0x29,0x83,0x24,0x38,
        0x40,0x53,0x68,0x81,0x29,0x83,0x24,0x38,
        0x40,0x53,0x68,0x81,0x29,0x83,0x24,0x38,
        0x00,0x03,0x06,0x09,0x16,0x19,0x1C,0x1F,
        0x01,0x04,0x07,0x0A,0x17,0x1A,0x1D,0x20,
        0x02,0x05,0x08,0x0B,0x18,0x1B,0x1E,0x21 };
    int img_offset=(vial>=6 && si!=3)?9:0x42;
    if(level>0 && level<3){
        g_vl_sh = vliq[(level-1)*8 + si*24 + vial] + img_offset;
        static const int hy[12]={0x1B,0x13,0x13,0x16,0x0E,0x0E,0x0E,0x0E,0x0E,0x0B,0x0B,0x0B};
        g_vl_y = hand_y + hy[si*3 + level - 1] - 20;
        static const int hx[96]={
            0x4,0x4,0x4,0x4,0x14,0x14,0x14,0x14,
            0x4,0x4,0x4,0x4,0x13,0x13,0x13,0x13,
            0x4,0x4,0x4,0x4,0x13,0x13,0x13,0x13,
            0x4,0x4,0x4,0x4,0x17,0x17,0x17,0x17,
            0x3,0x3,0x3,0x3,0x17,0x17,0x17,0x17,
            0x3,0x3,0x3,0x3,0x17,0x17,0x17,0x17,
            0x3,0x3,0x3,0x3,0x1B,0x1B,0x1B,0x1B,
            0x3,0x3,0x3,0x3,0x1B,0x1B,0x1B,0x1B,
            0x3,0x3,0x3,0x3,0x1B,0x1B,0x1B,0x1B,
            0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,
            0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,
            0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3 };
        static const int hx1[8]={10,10,10,10,-13,-13,-13,-13};
        g_vl_x = hand_x + hx[si*24 + (level-1)*8 + vial] + hx1[vial];
    }
}

/* draw one animation sub-frame (scene + question parchment + caption) and present it. */
static void gypsy_anim_frame(const char*cap){
    gypsy_update_bubbles();                              /* churn the jar essence as it fills */
    draw_gypsy_scene();
    blit_indexed(g_gyp_scroll,g_gscroll_w,g_gscroll_h,0,120);
    if(cap) ui_text((LW-ui_textw(cap))/2, 150, cap, 0x3e);
    rdr_present_doubled(LH); Vsync();
}

/* nuvie gypsy_vial_anim(vial): the 17-frame arm/hand choreography — reach to the winning virtue's
 * vial, lift it, tip it over the central jar (the pour stream flows, the jar rises + the vial drains
 * one level), then set it back. `vial` (0..7) = the virtue that won this question. */
static void gypsy_pour(int vial){
    static const int vx[8]={41,62,83,104,200,221,242,263};
    static const int arm0[8]={0,0,0,0,5,5,5,5}, arm1[8]={1,1,1,1,4,4,4,4}, arm2[8]={2,2,2,2,3,3,3,3};
    static const int h0[8]={12,12,12,12,13,13,13,13}, h1[8]={9,9,9,9,17,17,17,17}, h2[8]={10,10,10,10,18,18,18,18};
    static const int h3[8]={16,16,16,16,46,46,46,46}, h4[8]={15,15,15,15,37,37,37,37};
    static const int armx[8]={93,93,93,93,172,172,172,172};
    static const int hx0[8]={29,50,71,92,202,223,244,264}, hx1[8]={107,107,107,107,170,170,170,170};
    static const int hx2[8]={109,109,109,109,168,168,168,168}, hx3[8]={112,112,112,112,165,165,165,165};
    static const int hx4[8]={10,10,10,10,-13,-13,-13,-13};
    if(vial<0||vial>7) return;
    char cap[48]; { const char*a="The gypsy pours thy "; int n=0; for(const char*p=a;*p;p++) cap[n++]=*p;
        const char*vn=CC_VIRTUE[vial]; for(const char*p=vn;*p;p++) cap[n++]=*p; cap[n++]='.'; cap[n]=0; }
    if(!g_gyp_scene){ if(g_vial_lvl[vial]>0) g_vial_lvl[vial]--; if(g_jar_lvl<7) g_jar_lvl++; return; }
    g_pour_vial=vial;
    int hnum=0xf;
    for(int idx=0; idx<=16; idx++){
        int upd=1;
        if(idx==0){ hnum=0xf; upd=0; }
        else if(idx==1||idx==15){ g_arm_sh=1+arm0[vial]; g_arm_x=vx[vial]-6; g_arm_y=21;
            hnum=h0[vial]; g_hand_sh=9+hnum; g_hand_x=vx[vial]; g_hand_y=66; }
        else if(idx==2||idx==14){ g_arm_sh=1+arm1[vial]; g_arm_x=vx[vial]-6; g_arm_y=21;
            hnum=h0[vial]; g_hand_sh=9+hnum; g_hand_x=vx[vial]; g_hand_y=29; }
        else if(idx==3||idx==13){ g_arm_sh=1+arm1[vial]; g_arm_x=vx[vial]-6; g_arm_y=21;
            hnum=h1[vial]; g_hand_sh=9+hnum; g_hand_x=hx0[vial]; g_hand_y=25; }
        else if(idx==4||idx==12){ g_arm_sh=1+arm2[vial]; g_arm_x=armx[vial]; g_arm_y=21;
            hnum=h1[vial]; g_hand_sh=9+hnum; g_hand_x=hx1[vial]; g_hand_y=20; }
        else if(idx==5||idx==11){ g_arm_sh=1+arm2[vial]; g_arm_x=armx[vial]; g_arm_y=21;
            hnum=h2[vial]; g_hand_sh=9+hnum; g_hand_x=hx1[vial]; g_hand_y=20;
            if(idx==11){ g_vial_lvl[vial]--; if(g_vial_lvl[vial]==2) gypsy_stop_pour(); }
            if(g_vial_lvl[vial]==3) gypsy_start_pour(vial,g_vial_lvl[vial]); }
        else if(idx==6||idx==10){ if(g_vial_lvl[vial]>2) upd=0;
            g_arm_sh=1+arm2[vial]; g_arm_x=armx[vial]; g_arm_y=21;
            hnum=h3[vial]; g_hand_sh=9+hnum; g_hand_x=hx2[vial]; g_hand_y=21;
            if(g_vial_lvl[vial]==2){ if(idx==6) gypsy_start_pour(vial,g_vial_lvl[vial]); else gypsy_stop_pour(); } }
        else { /* idx 7,8,9 */ if(g_vial_lvl[vial]>1) upd=0;
            g_arm_sh=1+arm2[vial]; g_arm_x=armx[vial]; g_arm_y=21;
            hnum=h4[vial]; g_hand_sh=9+hnum; g_hand_x=hx3[vial]; g_hand_y=13;
            if(g_vial_lvl[vial]==1){ if(idx==7){ gypsy_start_pour(vial,g_vial_lvl[vial]); g_vl_sh=-1; }
                                     else if(idx==9) gypsy_stop_pour(); } }
        if(upd){
            gvl_liquid(hnum, vial, g_vial_lvl[vial], g_hand_x - hx4[vial], g_hand_y+20);
            for(int j=0;j<2;j++) gypsy_anim_frame(cap);      /* ~2 sub-frames/idx (nuvie does 8, for bubbles) */
        }
    }
    g_arm_sh=g_hand_sh=g_pour_sh=g_vl_sh=-1; g_pour_vial=-1;   /* the vial glass reappears at its new level */
}

/* one A/B virtue question. Shows the gypsy's REAL prose (extracted from U.EXE) when present —
 * the A) / B) choices are inline in the text — else a generic prompt with the two virtue
 * names. A) is always the lower-indexed virtue, B) the higher. Returns the WINNING virtue. */
static int cc_ask(int qnum, int va, int vb)
{
    int lo=va<vb?va:vb, hi=va<vb?vb:va;
    int qi=cc_question_index(va,vb);
    int have=(qi>=0 && qi<g_cc_nq);
    int sel=0;                                                    /* 0 = A (lo), 1 = B (hi) */
    char hdr[20]; { const char*h="Question "; int n=0; for(const char*p=h;*p;p++) hdr[n++]=*p;
        hdr[n++]=(char)('0'+qnum); const char*o=" of 7"; for(const char*p=o;*p;p++) hdr[n++]=*p; hdr[n]=0; }
    for(;;){
        gypsy_update_bubbles();                                   /* churn the jar essence while the player reads (once it is filling) */
        if(g_gyp_scene){                                          /* authentic gypsy "casting" scene */
            draw_gypsy_scene();                                  /* gypsy + table + 8 vials at their current levels */
            blit_indexed(g_gyp_scroll,g_gscroll_w,g_gscroll_h,0,120);   /* question parchment (torn edges transparent) */
            ui_text(6, 4, hdr, 15);                               /* "Question N of 7" over the scene */
            ui_text(246, 4, "[A]", sel==0?14:24);                 /* A/B selector, top-right */
            ui_text(282, 4, "[B]", sel==1?14:24);
            if(have) draw_wrapped(g_cc_qtext[qi], g_cc_qlen[qi], 128, 38, 9, 0x3e);   /* prose, parchment ink */
            else ui_text((LW-ui_textw("Which path guides thee?"))/2, 150, "Which path guides thee?", 0x3e);
        } else {                                                  /* fallback: plain text on black */
            clear_bb(0);
            ui_text((LW-ui_textw("The Gypsy's Questions"))/2, 8, "The Gypsy's Questions", 15);
            ui_text((LW-ui_textw(hdr))/2, 24, hdr, 14);
            if(have){
                draw_wrapped(g_cc_qtext[qi], g_cc_qlen[qi], 46, 38, 12, 15);
                ui_text(LW/2-52, 168, "[ A ]", sel==0?14:33);
                ui_text(LW/2+12, 168, "[ B ]", sel==1?14:33);
                ui_text((LW-ui_textw("A / B  or  <  >  + Enter"))/2, 188, "A / B  or  <  >  + Enter", 24);
            } else {
                ui_text((LW-ui_textw("Which path guides thee?"))/2, 80, "Which path guides thee?", 15);
                char a[24], b[24]; int n;
                n=0; a[n++]='A'; a[n++]=')'; a[n++]=' '; for(const char*p=CC_VIRTUE[lo];*p;p++) a[n++]=*p; a[n]=0;
                n=0; b[n++]='B'; b[n++]=')'; b[n++]=' '; for(const char*p=CC_VIRTUE[hi];*p;p++) b[n++]=*p; b[n]=0;
                ui_text((LW-ui_textw(a))/2, 108, a, sel==0?14:33);
                ui_text((LW-ui_textw(b))/2, 128, b, sel==1?14:33);
                ui_text((LW-ui_textw("(A / B, or arrows + Enter)"))/2, 168, "(A / B, or arrows + Enter)", 24);
            }
        }
        rdr_present_doubled(LH);
        int key=plat_input_raw();
        if(!key){ Vsync(); continue; }                            /* idle: re-present so the jar bubbles animate */
        int ch=key&0xFF, sc=(key>>8)&0xFF;
        if(ch=='a'||ch=='A') return lo;
        if(ch=='b'||ch=='B') return hi;
        if(sc==0x4b||sc==0x48) sel=0;                             /* left/up  -> A */
        else if(sc==0x4d||sc==0x50) sel=1;                        /* right/down -> B */
        else if(ch==0x0d||ch==' ') return sel==0?lo:hi;
    }
}

/* name entry: type a name (letters/space, backspace, Enter). Fills g_cc_name. */
static void cc_name_entry(void)
{
    int len=0; g_cc_name[0]=0;
    for(;;){
        clear_bb(0);
        ui_text((LW-ui_textw("Character Creation"))/2, 24, "Character Creation", 15);
        ui_text((LW-ui_textw("By what name art thou known?"))/2, 70, "By what name art thou known?", 15);
        char nm[18]; int i=0; for(;i<len;i++) nm[i]=g_cc_name[i]; nm[i++]='_'; nm[i]=0;
        ui_text((LW-ui_textw(nm))/2, 110, nm, 14);
        ui_text((LW-ui_textw("(type a name, Enter when done)"))/2, 168, "(type a name, Enter when done)", 24);
        rdr_present_doubled(LH);
        int key; while(!(key=plat_input_raw())) Vsync();
        int ch=key&0xFF;
        if(ch==0x0d && len>0) break;                              /* Enter (needs a name) */
        else if(ch==0x08 && len>0) g_cc_name[--len]=0;            /* backspace */
        else if(len<13 && ((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||ch==' ')){
            g_cc_name[len++]=(char)ch; g_cc_name[len]=0; }
    }
}

/* gender select (M/F, or arrows + Enter). Fills g_cc_gender (1=male, 0=female). */
static void cc_gender(void)
{
    int sel=1;
    for(;;){
        clear_bb(0);
        ui_text((LW-ui_textw("Character Creation"))/2, 24, "Character Creation", 15);
        ui_text((LW-ui_textw("Art thou Male, or Female?"))/2, 70, "Art thou Male, or Female?", 15);
        ui_text((LW-ui_textw("Male"))/2, 108, "Male", sel==1?14:33);
        ui_text((LW-ui_textw("Female"))/2, 128, "Female", sel==0?14:33);
        rdr_present_doubled(LH);
        int key; while(!(key=plat_input_raw())) Vsync();
        int ch=key&0xFF, sc=(key>>8)&0xFF;
        if(ch=='m'||ch=='M'){ g_cc_gender=1; return; }
        if(ch=='f'||ch=='F'){ g_cc_gender=0; return; }
        if(sc==0x48) sel=1; else if(sc==0x50) sel=0;              /* up=male, down=female */
        else if(ch==0x0d||ch==' '){ g_cc_gender=sel; return; }
    }
}

/* portrait pick (nuvie create_character): cycle the 6 MONTAGE portraits for the chosen
 * gender; 'S'/up/down flips gender (montage index = gender*6 + portrait_num). Left/right or
 * space cycle; Enter confirms. Fills g_cc_portrait (and may adjust g_cc_gender). */
static void cc_portrait(void)
{
    if(g_cc_nport<12){ g_cc_portrait=0; return; }         /* no montage shipped -> skip */
    int p=0;
    for(;;){
        clear_bb(0);
        ui_text((LW-ui_textw("Choose Thy Likeness"))/2, 20, "Choose Thy Likeness", 15);
        int idx=g_cc_gender*6 + p; if(idx<0)idx=0; if(idx>=g_cc_nport)idx=g_cc_nport-1;
        int px=(LW-g_cc_pw)/2, py=60;
        blit_opaque(g_cc_port[idx], g_cc_pw, g_cc_ph, px, py);   /* current portrait (opaque) */
        for(int t=-1;t<=g_cc_pw;t++){                            /* thin frame around it */
            ROW(py-1)[px+t<0?px:(px+t>=LW?LW-1:px+t)]=14; ROW(py+g_cc_ph)[px+t<0?px:(px+t>=LW?LW-1:px+t)]=14; }
        const char*g = g_cc_gender ? "Male" : "Female";
        ui_text((LW-ui_textw(g))/2, py+g_cc_ph+8, g, 14);
        char lbl[16]; lbl[0]='('; lbl[1]=(char)('1'+p); lbl[2]=' '; lbl[3]='o'; lbl[4]='f'; lbl[5]=' '; lbl[6]='6'; lbl[7]=')'; lbl[8]=0;
        ui_text((LW-ui_textw(lbl))/2, py+g_cc_ph+24, lbl, 33);
        ui_text((LW-ui_textw("< > face   S sex   Enter ok"))/2, 182, "< > face   S sex   Enter ok", 24);
        rdr_present_doubled(LH);
        int key; while(!(key=plat_input_raw())) Vsync();
        int ch=key&0xFF, sc=(key>>8)&0xFF;
        if(sc==0x4b) p=(p+5)%6;                              /* left  */
        else if(sc==0x4d||ch==' ') p=(p+1)%6;                /* right / space */
        else if(ch=='s'||ch=='S'||sc==0x48||sc==0x50) g_cc_gender=!g_cc_gender;  /* toggle sex */
        else if(ch==0x0d||ch==0x1b){ g_cc_portrait=p; return; }   /* Enter/Esc -> accept */
    }
}

/* final summary: name, dominant virtue, and the rolled Str/Dex/Int (all from g_cc_*). */
static void cc_summary(void)
{
    if(!g_cc_name[0]){ const char*d="Avatar"; int i=0; while(d[i]){g_cc_name[i]=d[i];i++;} g_cc_name[i]=0; }
    for(;;){
        clear_bb(0);
        ui_text((LW-ui_textw("Thy Character"))/2, 30, "Thy Character", 15);
        char line[40];
        int n=0; for(const char*p=g_cc_name;*p;p++) line[n++]=*p;
        const char*t=", the "; for(const char*p=t;*p;p++) line[n++]=*p;
        for(const char*p=CC_VIRTUE[g_cc_virtue];*p;p++) line[n++]=*p; line[n]=0;
        ui_text((LW-ui_textw(line))/2, 70, line, 14);
        char s[24]; s[0]='S';s[1]='t';s[2]='r';s[3]=' '; n=4; { int x=g_cc_str; if(x>=10)s[n++]=(char)('0'+x/10); s[n++]=(char)('0'+x%10);} s[n]=0;
        char dxs[24]; dxs[0]='D';dxs[1]='e';dxs[2]='x';dxs[3]=' '; n=4; { int x=g_cc_dex; if(x>=10)dxs[n++]=(char)('0'+x/10); dxs[n++]=(char)('0'+x%10);} dxs[n]=0;
        char is[24]; is[0]='I';is[1]='n';is[2]='t';is[3]=' '; n=4; { int x=g_cc_int; if(x>=10)is[n++]=(char)('0'+x/10); is[n++]=(char)('0'+x%10);} is[n]=0;
        ui_text(90,110,s,33); ui_text(90,130,dxs,33); ui_text(90,150,is,33);
        ui_text((LW-ui_textw("Press any key"))/2, 182, "Press any key", 24);
        rdr_present_doubled(LH);
        int key; while(!(key=plat_input_raw())) Vsync();
        break;
    }
}

/* forest-path lead-in: pan the WOODS panorama left toward the gypsy's wagon (own-worded
 * caption). Palette 4 must already be set. Auto-pans, then holds until a key. */
static void cc_woods_intro(void)
{
    if(!g_woods) return;
    int panlen = g_woods_w - LW; if(panlen<0) panlen=0;
    const char*cap="A path winds to a gypsy's wagon...";
    for(int s=0;s<=panlen;s+=2){
        blit_opaque(g_woods,g_woods_w,g_woods_h,-s,0);
        ui_text((LW-ui_textw(cap))/2, 182, cap, 15);
        rdr_present_doubled(LH);
        if(plat_input_raw()) break;                          /* a key skips to the end */
    }
    for(;;){
        blit_opaque(g_woods,g_woods_w,g_woods_h,-panlen,0);
        ui_text((LW-ui_textw(cap))/2, 182, cap, 15);
        ui_text((LW-ui_textw("(press a key)"))/2, 192, "(press a key)", 15);
        rdr_present_doubled(LH);
        int key; while(!(key=plat_input_raw())) Vsync();
        break;
    }
}

/* brief "casting" lead-in on the gypsy scene (own-worded — the authentic U.EXE narration is a
 * later extraction). Palette 5 must already be set. Holds until a key. */
static void cc_gypsy_intro(void)
{
    if(!g_gyp_scene) return;
    for(int i=0;i<8;i++) g_vial_lvl[i]=3; g_jar_lvl=0;    /* vials start FULL (level 3); the gypsy DRAINS them into the jar (nuvie) */
    g_arm_sh=g_hand_sh=g_pour_sh=g_vl_sh=-1; g_pour_vial=-1;
    g_nflask=0; g_bubble_ctr=0; g_jarbub_lvl=-1; rng_seed(&g_bubrng, 0x6b0bb1e ^ plat_ticks_200hz());  /* reset jar bubbles */
    for(;;){
        draw_gypsy_scene();
        blit_indexed(g_gyp_scroll,g_gscroll_w,g_gscroll_h,0,120);
        ui_text((LW-ui_textw("The gypsy bids thee sit at her table."))/2, 138, "The gypsy bids thee sit at her table.", 0x3e);
        ui_text((LW-ui_textw("Eight vials of glowing virtue await."))/2, 150, "Eight vials of glowing virtue await.", 0x3e);
        ui_text((LW-ui_textw("(press a key)"))/2, 180, "(press a key)", 0x3e);
        rdr_present_doubled(LH);
        int key; while(!(key=plat_input_raw())) Vsync();
        break;
    }
}

/* Run character creation: name -> gender -> portrait -> virtue tournament -> Str/Dex/Int. */
static void run_chargen(void)
{
    { int gk,gd=0; while((gk=plat_input_raw())&&++gd<64){ (void)gk; } }  /* flush stale boot keys */
    cc_name_entry();
    cc_gender();
    cc_portrait();
    if(g_woods){ rdr_set_palette((uint8_t*)g_woods_pal,256), rdr_set_lowcolor(g_woods_lc,g_woods_lc+48); cc_woods_intro(); }   /* forest approach (PALETTES.INT 4) */
    int use_gypsy = (g_gyp_scene!=NULL);
    if(use_gypsy){ rdr_set_palette((uint8_t*)g_gyp_pal,256), rdr_set_lowcolor(g_gyp_lc,g_gyp_lc+48); cc_gypsy_intro(); }  /* enter the gypsy's parlour (PALETTES.INT 5) */
    int v[8]; for(int i=0;i<8;i++) v[i]=i;                        /* shuffle the 8 virtues */
    rng_t r; rng_seed(&r, 0x6e0c0de ^ plat_ticks_200hz());
    for(int i=7;i>0;i--){ int j=(int)rng_range(&r,i+1); int t=v[i]; v[i]=v[j]; v[j]=t; }
    int st=15,dx=15,in=15,q=1;
    for(int round=4; round>=1; round>>=1)                        /* 4 -> 2 -> 1 = 7 questions */
        for(int i=0;i<round;i++){
            int win=cc_ask(q++, v[i*2], v[i*2+1]);
            gypsy_pour(win);                                     /* the gypsy lifts the winner's vial + pours it into the jar */
            st+=CC_ADJ[win][0]; dx+=CC_ADJ[win][1]; in+=CC_ADJ[win][2];
            v[i]=win;
        }
    if(use_gypsy){ rdr_set_palette(g_basepal,256);                /* back to the menu/game palette */
        if(g_fe_base_lc) rdr_set_lowcolor(g_fe_base_lc, g_fe_base_lc+48); }   /* ST: base 16 */
    g_cc_str=st; g_cc_dex=dx; g_cc_int=in; g_cc_virtue=v[0];
    g_cc_done=1;                                                  /* apply g_cc_* to the Avatar at play start */
    cc_summary();
}

/* Headless capture hook: the CHARGEN.REQ byte selects ONE chargen sub-screen (each draws
 * once then blocks on input, so the AVI grab holds it). '1'/default = the full interactive
 * flow. Pre-fills sample data so later screens have something to show. */
static void chargen_capture(int which)
{
    { const char*d="Avatar"; int i=0; while(d[i]){g_cc_name[i]=d[i];i++;} g_cc_name[i]=0; }
    g_cc_gender=1; g_cc_portrait=0;
    g_cc_str=17; g_cc_dex=16; g_cc_int=18; g_cc_virtue=2;   /* sample stats for the summary */
    switch(which){
        case 'n': cc_name_entry(); break;
        case 'g': cc_gender();     break;
        case 'p': cc_portrait();   break;
        case 'W': if(g_woods_pal) rdr_set_palette((uint8_t*)g_woods_pal,256), rdr_set_lowcolor(g_woods_lc,g_woods_lc+48); cc_woods_intro(); break;  /* forest lead-in */
        case 'G': if(g_gyp_pal) rdr_set_palette((uint8_t*)g_gyp_pal,256), rdr_set_lowcolor(g_gyp_lc,g_gyp_lc+48); cc_gypsy_intro(); break;  /* gypsy scene lead-in */
        case 'q': if(g_gyp_pal) rdr_set_palette((uint8_t*)g_gyp_pal,256), rdr_set_lowcolor(g_gyp_lc,g_gyp_lc+48);   /* Justice vs Compassion on the gypsy scene */
            { static const int demo[8]={0,1,2,3,3,2,1,0}; for(int i=0;i<8;i++) g_vial_lvl[i]=demo[i]; g_jar_lvl=4;  /* mid-casting: some vials drained */
              g_arm_sh=g_hand_sh=g_pour_sh=g_vl_sh=-1; g_pour_vial=-1;
              g_nflask=0; bubble_add_color(239); bubble_add_color(14); bubble_add_color(231); bubble_add_color(103);
              g_bubble_ctr=0; g_jarbub_lvl=-1; rng_seed(&g_bubrng, plat_ticks_200hz()); }  /* jar bubbles: 4 mixed virtue hues */
            cc_ask(1, 3, 1); break;
        case 'P': if(g_gyp_pal) rdr_set_palette((uint8_t*)g_gyp_pal,256), rdr_set_lowcolor(g_gyp_lc,g_gyp_lc+48);   /* held MID-POUR frame (arm tips vial 0 over the jar) */
            { for(int i=0;i<8;i++) g_vial_lvl[i]=3; g_vial_lvl[0]=2; g_jar_lvl=3; g_pour_vial=0;
              g_nflask=0; g_jarbub_lvl=-1; rng_seed(&g_bubrng, plat_ticks_200hz());
              g_arm_sh=1+2; g_arm_x=93; g_arm_y=21; g_hand_sh=9+16; g_hand_x=109; g_hand_y=21;
              gypsy_start_pour(0,2); gvl_liquid(16,0,2,109-10,21+20); bubble_effect(); }  /* speckle the jar for the still */
            draw_gypsy_scene(); blit_indexed(g_gyp_scroll,g_gscroll_w,g_gscroll_h,0,120);
            ui_text((LW-ui_textw("The gypsy pours thy Honesty."))/2,150,"The gypsy pours thy Honesty.",0x3e);
            rdr_present_doubled(LH); { int k; while(!(k=plat_input_raw())) Vsync(); } break;
        case 'D': if(g_gyp_pal) rdr_set_palette((uint8_t*)g_gyp_pal,256), rdr_set_lowcolor(g_gyp_lc,g_gyp_lc+48);   /* run 3 full pours: drain vial 0 (3->0), jar (0->3) */
            for(int i=0;i<8;i++) g_vial_lvl[i]=3; g_jar_lvl=0;
            g_arm_sh=g_hand_sh=g_pour_sh=g_vl_sh=-1; g_pour_vial=-1;
            g_nflask=0; g_bubble_ctr=0; g_jarbub_lvl=-1; rng_seed(&g_bubrng, plat_ticks_200hz());
            gypsy_pour(0); gypsy_pour(0); gypsy_pour(0);
            draw_gypsy_scene(); blit_indexed(g_gyp_scroll,g_gscroll_w,g_gscroll_h,0,120);
            { char m[40]; int n=0; const char*a="vial0="; for(const char*p=a;*p;p++)m[n++]=*p; m[n++]=(char)('0'+g_vial_lvl[0]);
              const char*b=" jar="; for(const char*p=b;*p;p++)m[n++]=*p; m[n++]=(char)('0'+g_jar_lvl); m[n]=0;
              ui_text((LW-ui_textw(m))/2,150,m,0x3e); }
            rdr_present_doubled(LH); { int k; while(!(k=plat_input_raw())) Vsync(); } break;
        case 's': cc_summary();    break;
        case 'N':                            /* apply a sample character to the new game (no blocking UI) */
            { const char*nm="Milady"; int i=0; while(nm[i]){g_cc_name[i]=nm[i];i++;} g_cc_name[i]=0; }
            g_cc_gender=0; g_cc_portrait=2; g_cc_str=21; g_cc_dex=13; g_cc_int=19; g_cc_virtue=1;
            g_cc_done=1; break;
        default:  run_chargen();   break;    /* '1' = the whole flow */
    }
}

/* NEW-GAME character overlay: apply the created character (g_cc_*) onto the Avatar (actor 1).
 * This matches nuvie create_character's model — the WORLD state is the game's shipped start
 * (mkworld's default non-svm build seeds Lord British's castle @ (315,367) + the four-member
 * party {Avatar,Dupre,Shamino,Iolo} from the original SAVEGAME/OBJLIST), and character creation
 * OVERLAYS just six fields onto the Avatar: name, gender, portrait, Str, Dex, Int. No-op unless
 * character creation actually ran (a plain "Journey Onward" keeps the shipped default Avatar). */
static void apply_chargen(void)
{
    if(!g_cc_done) return;
    g_astr[1]=(uint8_t)g_cc_str; g_adex[1]=(uint8_t)g_cc_dex; g_aint[1]=(uint8_t)g_cc_int;
    /* Mana MAX derives from Intelligence (2xInt for the Avatar; magic_skills.md §3), so recompute
     * it now that chargen set a new Int — the load-time value (§ actor block, from the shipped
     * OBJLIST Int) is stale. Start the new game rested: current = max. */
    g_amaxmana[1]=(uint8_t)magic_max_mana(g_aint[1],1); g_amana[1]=g_amaxmana[1];
    { int o=0; while(g_cc_name[o]&&o<15){ g_avatar_name[o]=g_cc_name[o]; o++; } g_avatar_name[o]=0; }
    for(int p=0;p<g_nparty;p++) if(g_party[p].actor==1){
        int o=0; while(g_avatar_name[o]&&o<15){ g_party[p].name[o]=g_avatar_name[o]; o++; }
        g_party[p].name[o]=0; break; }
    /* Gender feeds gender-gated conversation (VM system var 0x10). CONVENTION FLIP: chargen
     * g_cc_gender is 1=male/0=female (nuvie montage order = gender*6+portrait), but the VM's
     * avatar_sex is 0=male/1=female (converse_vm.h) — so invert. gender + portrait also stay on
     * the g_cc_* globals for the status screen + save writer (objlist 0x1c72 = avatar portrait). */
    g_cg.avatar_sex = g_cc_gender ? 0 : 1;
    /* $G honorific: U6 addresses the Avatar as milord/milady by gender (nuvie get_gender_title;
     * U6 gender 0=male=milord, 1=female=milady) — the one gender-driven thing NPCs SAY. (The
     * on-map avatar SPRITE is gender-neutral in U6: obj_n 410 has a single tile, 1776, with no
     * gender variant — nuvie's U6Actor derives it from base_obj_n alone. Gender shows in the
     * portrait/status view + this honorific, not the map figure.) */
    g_avatar_title = g_cc_gender ? "milord" : "milady";
}

/* U6 MAIN MENU (docs/formats/shapes.md front-end). Shows the "Ultima VI" title +
 * the menu options (Introduction / Create a Character / Transfer a Character /
 * Acknowledgements / Journey Onward), tinted yellow->orange by distance from the
 * selected one; arrows move, Enter/Space selects. Returns the chosen index (0..4). Only
 * "Journey Onward" (4) exits to play; the others flash "Not yet available." */
/* Menu text palette indices (from MAINMENU.SHP via U6PAL): the option text is the
 * gold->orange gradient 33..36; index 14 is the bright yellow the shape bakes onto its
 * default-selected option. The real U6 menu tints EVERY option by its DISTANCE from the
 * selected one: the selection is brightest (yellow 14) and the tint ramps outward to
 * orange (36) — not a time cycle; it recentres when the selection moves. */
static const uint8_t MENU_RAMP[]={14,33,34,35,36};
enum { MENU_NRAMP = (int)(sizeof MENU_RAMP) };

/* After blitting the baked menu at (mx,my): recolour each option's text (idx 14 or the
 * gold 33..36) to MENU_RAMP[distance-from-selection], giving the yellow->orange spread. */
static void menu_highlight(int mx,int my,int mw,int mh,int sel,int rowh)
{
    for(int y=0;y<mh;y++){ uint8_t*d=ROW(my+y)+mx;
        int opt = rowh>0 ? y/rowh : 0; if(opt<0) opt=0; if(opt>=MENU_NRAMP) opt=MENU_NRAMP-1;
        int dist = opt>sel ? opt-sel : sel-opt; if(dist>=MENU_NRAMP) dist=MENU_NRAMP-1;
        uint8_t col = MENU_RAMP[dist];
        for(int x=0;x<mw;x++){ uint8_t v=d[x];
            if(v==14 || (v>=33 && v<=36)) d[x]=col; } }   /* text pixels -> distance tint */
}

static int run_menu(void)
{
    if(!g_menu_pix || g_menu_w<=0 || g_menu_w>320 || g_menu_h>200) return 4;   /* no/bad art -> play */
    int sel=4, nopt=5, note=0;
    int tx=(LW-g_title_w)/2, ty=12;
    int mw=g_menu_w, mx=(LW-mw)/2, my=ty+g_title_h+20;
    int rowh=g_menu_h/nopt;
    { int gk,gd=0; while((gk=plat_input_raw())&&++gd<64){ (void)gk; } }  /* flush stale boot keys */
    for(;;){
        /* ST: the title screen has its OWN 16 (U6WU per-scene) — tuned to the logo's metallic gradient
         * + the gold menu ramp instead of the terrain-16. Re-assert each frame so returning from the
         * Introduction / Create-a-Character sub-screens (which set their own palettes) restores it. */
        if(g_menu_lc) rdr_set_lowcolor(g_menu_lc, g_menu_lc+48);
        clear_bb(0);
        if(g_title_pix) blit_indexed(g_title_pix,g_title_w,g_title_h,tx,ty);
        blit_indexed(g_menu_pix,mw,g_menu_h,mx,my);
        menu_highlight(mx,my,mw,g_menu_h,sel,rowh);         /* yellow->orange distance tint */
        if(note) ui_text((LW-ui_textw("Not yet available."))/2, my+g_menu_h+10, "Not yet available.", 15);
        rdr_present_doubled(LH);
        int key; while(!(key=plat_input_raw())) Vsync();     /* wait for a keystroke */
        int a=key&0xFF, sc=(key>>8)&0xFF; note=0;
        if(sc==0x48) sel=(sel+nopt-1)%nopt;              /* up   */
        else if(sc==0x50) sel=(sel+1)%nopt;              /* down */
        else if(a==0x1b){ if(g_fe_base_lc) rdr_set_lowcolor(g_fe_base_lc,g_fe_base_lc+48); return 4; }  /* Esc -> Journey Onward */
        else if(a==0x0d || a==' '){                        /* select */
            if(sel==4){ if(g_fe_base_lc) rdr_set_lowcolor(g_fe_base_lc,g_fe_base_lc+48); return 4; }  /* Journey Onward -> play */
            else if(sel==0) run_intro();                  /* Introduction -> cutscene, then back */
            else if(sel==1){ run_chargen(); if(g_cc_done){ if(g_fe_base_lc) rdr_set_lowcolor(g_fe_base_lc,g_fe_base_lc+48); return 4; } }  /* Create a Character -> new game */
            else note=1;                                  /* Transfer/Ack -> not yet */
        }
    }
}

/* From an appearance tile, find its object's base tile + tiles-per-direction by
 * scanning BASETILE (obj_n -> base tile): the largest base<=tile and smallest
 * base>tile bound the object's frame run. 16 frames => 4 dir (humanoid), 8 => 2;
 * other counts => 0 (non-directional, keep the saved sprite). */
static int npc_base_fc(int atile,int*base)
{
    if(!g_basetile||atile<=0){ *base=atile; return 0; }
    int lo=-1, hi=2048;
    for(int o=0;o<1024;o++){ int b=g_basetile[o*2]|(g_basetile[o*2+1]<<8);
        if(b<=0) continue;
        if(b<=atile && b>lo) lo=b;
        if(b>atile && b<hi) hi=b; }
    if(lo<0){ *base=atile; return 0; }
    *base=lo; int fc=hi-lo;
    return fc==16?4 : fc==8?2 : 0;
}

/* one-time NPC init (after the game hour is known): resolve each actor's frame
 * layout + initial facing from its saved appearance, and seed a mutable surface
 * position for schedule-only NPCs (so they can then WALK from there). */
static void init_npcs(void)
{
    for(int i=0;i<U6_MAX_ACTORS;i++){
        if(g_atile[i]==0xffff){ g_atpd[i]=0; continue; }
        int base, tpd=npc_base_fc(g_atile[i],&base);
        g_abase[i]=(uint16_t)base; g_atpd[i]=(uint8_t)tpd; g_walkf[i]=0;
        g_facing[i]= tpd ? (uint8_t)(((g_atile[i]-base)/tpd)&3) : 2;   /* dir of saved frame */
        if(g_actz[i]==0xff && g_actors[i].nsched>0){                   /* seed from schedule */
            int si=u6sched_active(&g_actors[i],g_hour);
            if(si>=0){ u6sched_entry_t e; u6sched_decode(g_actors[i].sched+si*5,&e);
                g_actx[i]=(uint16_t)e.x; g_acty[i]=(uint16_t)e.y; g_actz[i]=e.z; }
        }
    }
}

/* scheduled surface destination for actor i at the current hour; 1 if it has one */
static int npc_dest(int i,int*dx,int*dy)
{
    if(g_actors[i].nsched<=0) return 0;
    int si=u6sched_active(&g_actors[i],g_hour); if(si<0) return 0;
    u6sched_entry_t e; u6sched_decode(g_actors[i].sched+si*5,&e);
    if(e.z!=0) return 0;
    *dx=(int)e.x; *dy=(int)e.y; return 1;
}

/* Schedule WORKTYPE -> activity class (Stage 3c; docs/formats/schedule.md §Worktypes).
 * SLEEP is data-confirmed (worktype 0x91 dominates the night hours across every NPC's
 * schedule). WANDER is a PROBABLE mapping of the common daytime "at this location" code
 * (0x90) — a small roam near the anchor; documented as a heuristic pending original-game
 * verification (no Nuvie source on hand to match). Everything else = WORK: walk to the
 * scheduled spot and stand at post (guards/shopkeepers/sitters/eaters all read as "stand"
 * until per-worktype poses land). */
enum { ACT_WORK=0, ACT_SLEEP, ACT_WANDER };
#define WT_U6_SLEEP  0x91
#define WT_U6_WANDER 0x90
#define WANDER_R     2      /* wander stays within this Chebyshev radius of its anchor */
static int npc_worktype(int i)
{
    if(g_actors[i].nsched<=0) return -1;
    int si=u6sched_active(&g_actors[i],g_hour); if(si<0) return -1;
    u6sched_entry_t e; u6sched_decode(g_actors[i].sched+si*5,&e);
    return e.worktype;
}
static int wt_activity(int wt)
{
    if(wt==WT_U6_SLEEP)  return ACT_SLEEP;
    if(wt==WT_U6_WANDER) return ACT_WANDER;
    return ACT_WORK;
}

/* is some other surface actor already on (x,y)? (keeps NPCs from stacking) */
static int npc_occupied(int self,int x,int y)
{
    for(int j=0;j<U6_MAX_ACTORS;j++){
        if(j==self||g_atile[j]==0xffff||g_actz[j]!=0) continue;
        if(g_actx[j]==x && g_acty[j]==y) return 1;
    }
    return 0;
}

/* is actor i a party member (follows the Avatar instead of a schedule)? */
static int is_party_member(int i)
{
    if(i==1) return 0;                                  /* the Avatar itself */
    for(int p=0;p<g_nparty;p++) if(g_party[p].actor==i) return 1;
    return 0;
}

/* Bounded BFS pathfinder: the FIRST step from (sx,sy) along a shortest route toward
 * (tx,ty) that goes AROUND impassable tiles, searched within a 33x33 window. Returns
 * 1 + (*odx,*ody) for the step that most reduces distance to the target (the target
 * itself if reachable), else 0. The Avatar tile is treated as blocked. (Stage 3b) */
#define PF_R 16
#define PF_W (2*PF_R+1)
static uint8_t  g_pf[PF_W*PF_W];      /* 0=unvisited, 0xff=start, else 1+firstdir */
static int16_t  g_pfq[PF_W*PF_W];
static int npc_pathstep(const world_t*w,int sx,int sy,int tx,int ty,int avx,int avy,int*odx,int*ody)
{
    static const int dxs[4]={0,1,0,-1}, dys[4]={-1,0,1,0}; /* N E S W */
    for(int i=0;i<PF_W*PF_W;i++) g_pf[i]=0;
    int head=0,tail=0, c=PF_R*PF_W+PF_R;
    g_pf[c]=0xff; g_pfq[tail++]=(int16_t)c;
    int best=-1, bestd=1<<30;
    while(head<tail){
        int ci=g_pfq[head++], cx=ci%PF_W, cy=ci/PF_W;
        int wx=sx+(cx-PF_R), wy=sy+(cy-PF_R);
        int d=(wx>tx?wx-tx:tx-wx)+(wy>ty?wy-ty:ty-wy);
        if(d<bestd){ bestd=d; best=ci; }
        if(wx==tx && wy==ty){ best=ci; break; }
        for(int k=0;k<4;k++){
            int nx=cx+dxs[k], ny=cy+dys[k];
            if(nx<0||ny<0||nx>=PF_W||ny>=PF_W) continue;
            int ni=ny*PF_W+nx; if(g_pf[ni]) continue;
            int wwx=(sx+(nx-PF_R))&(U6_SURFACE_TILES-1), wwy=(sy+(ny-PF_R))&(U6_SURFACE_TILES-1);
            if(wwx==avx && wwy==avy) continue;            /* don't path through the Avatar */
            if(!world_is_passable(w,wwx,wwy)) continue;
            g_pf[ni]=(uint8_t)(1+((ci==c)?k:(g_pf[ci]-1)));  /* remember the FIRST step */
            g_pfq[tail++]=(int16_t)ni;
        }
    }
    if(best<0 || best==c || g_pf[best]==0xff) return 0;
    int fd=g_pf[best]-1; *odx=dxs[fd]; *ody=dys[fd]; return 1;
}

static int  actor_pos(int i,int*ax,int*ay);          /* (defined below) */
static void hostile_step(const world_t*w,int i);     /* combat.md CB2 (defined below) */
static int  coward_step(const world_t*w,int i);      /* CB4 coward (defined below) */
static int  adjacent_hostile(const world_t*w,int x,int y);  /* CB4 (defined below) */
static void party_attack(int att,int def);                  /* CB4 (defined below) */
static int  sees_party(const world_t*w,int i);              /* aggro-on-sight: range + LOS (defined below) */
static int  blocks_sight(int tile);                         /* sight-blocker test (defined below) */
static void fields_tick(const world_t*w);                   /* magic field-tile layer (defined below) */
static void status_tick(void);                              /* magic status effects (defined below) */
static void ally_step(const world_t*w,int i);              /* allied-summon AI (defined below) */
static void sync_party_hp(void);                            /* roster HP <- g_ahp (defined below) */

/* One turn of NPC movement: schedule NPCs step toward their scheduled spot, party
 * members follow the Avatar; each routes around walls via npc_pathstep and turns to
 * FACE the step. Turn-based: called when the game clock advances. */
static void step_npcs(const world_t*w)
{
    if(g_spell_light_turns>0) g_spell_light_turns--;   /* Light spell decays a turn per step */
    if(g_xray_turns>0) g_xray_turns--;                 /* X-ray see-through decays a turn (§5e) */
    for(int i=0;i<U6_MAX_ACTORS;i++) if(g_ainvis[i]) g_ainvis[i]--;   /* Invisibility wears off (§5) */
    fields_tick(w);                                    /* magic fields damage/sleep + age out (§5a) */
    status_tick();                                     /* poison DoT + decay protect/curse/charm (§5b) */
    /* slow passive recovery so a play session is sustainable: +1 mana / 8 turns,
     * +1 HP / 24 turns, toward each party member's max (magic + walking rest). */
    { static uint16_t rc=0; rc++;
      for(int p=0;p<g_nparty;p++){ int a=g_party[p].actor; if(a<0||a>=U6_MAX_ACTORS) continue;
          if((rc&7)==0 && g_amana[a]<g_amaxmana[a]) g_amana[a]++;
          if((rc%24)==0 && g_ahp[a]>0 && g_ahp[a]<g_amaxhp[a]) g_ahp[a]++;
          /* HUNGER: food ebbs; at the threshold warn once, at empty starve (lose HP). */
          if((rc&3)==0 && g_afood[a]>0){ g_afood[a]--;
              if(g_afood[a]==FOOD_HUNGRY){ int n=msgcat(0,g_party[p].name); msgcat(n," is hungry."); } }
          if(g_afood[a]==0 && (rc&7)==0 && g_ahp[a]>1){ g_ahp[a]--;
              int n=msgcat(0,g_party[p].name); msgcat(n," is starving!"); } }
      if((rc%24)==0) sync_party_hp(); }
    int avx=(int)w->px, avy=(int)w->py;
    for(int i=2;i<U6_MAX_ACTORS;i++){                  /* 0/1 = Avatar */
        if(g_atile[i]==0xffff||g_actz[i]!=0){ g_amoved[i]=0;
            if(g_summon[i]){ g_summon[i]=0; g_summon_life[i]=0; }   /* a slain/gone summon frees its slot (§5c) */
            continue; }
        if(g_asleep[i]){ g_asleep[i]--; g_amoved[i]=0; continue; }  /* sleep: skip the turn (§5a/§5b) */
        if(g_paralyze[i]){ g_paralyze[i]--; g_amoved[i]=0; continue; }  /* paralyze: skip the turn (§5b) */
        if(g_summon[i]){                                          /* summoned creature: lifetime + allied AI (§5c) */
            if(g_summon_life[i]<=1){ g_atile[i]=0xffff; g_summon[i]=0;   /* lifetime expired: vanish */
                g_align_hostile[i]=g_hostile[i]=g_alert[i]=0; g_amoved[i]=0; continue; }
            g_summon_life[i]--;
            if(g_summon[i]==1){ ally_step(w,i); continue; }       /* allied: hunt hostiles */
            /* g_summon[i]==2 (hostile summon) falls through to the assault path below */
        }
        if(actor_hostile(i) && !is_party_member(i)){     /* CB2/CB4 assault — aggro-on-sight */
            if(!g_alert[i] && sees_party(w,i)) g_alert[i]=1;   /* lurk until it PERCEIVES the party */
            if(g_alert[i]) hostile_step(w,i); else g_amoved[i]=0;
            continue; }
        if(g_coward[i] && !is_party_member(i) && coward_step(w,i)) continue;         /* coward flees the party on sight */
        int x=g_actx[i], y=g_acty[i], dx,dy;
        if(is_party_member(i)){                         /* party follows the Avatar */
            if(g_combat_mode){                          /* CB4: auto-fight an adjacent foe */
                int tid=adjacent_hostile(w,x,y);
                if(tid>=0){ int tx,ty; actor_pos(tid,&tx,&ty);
                    g_facing[i]= ty<y?0:ty>y?2:tx>x?1:3; party_attack(i,tid); g_amoved[i]=0; continue; } }
            int cx=avx>x?avx-x:x-avx, cy=avy>y?avy-y:y-avy;
            if(cx<=1 && cy<=1){ g_amoved[i]=0; continue; }  /* already alongside */
            dx=avx; dy=avy;
        } else if(!npc_dest(i,&dx,&dy)){ g_amoved[i]=0; continue; }
        else if(wt_activity(npc_worktype(i))==ACT_WANDER){   /* Stage 3c: roam near the anchor */
            int ox=x-dx, oy=y-dy; if(ox<0)ox=-ox; if(oy<0)oy=-oy;
            if(ox<=WANDER_R && oy<=WANDER_R){                /* inside the roam radius */
                /* deterministic per (clock,npc) so headless runs reproduce; step ~1 turn in 3 */
                uint32_t r=((uint32_t)w->clock*2654435761u)^((uint32_t)i*40503u);
                if(r%3==0){
                    static const int DX[4]={0,1,0,-1}, DY[4]={-1,0,1,0};   /* N E S W */
                    int d=(int)((r>>13)&3), nx=(x+DX[d])&(U6_SURFACE_TILES-1), ny=(y+DY[d])&(U6_SURFACE_TILES-1);
                    int qx=nx-dx, qy=ny-dy; if(qx<0)qx=-qx; if(qy<0)qy=-qy;
                    if(qx<=WANDER_R && qy<=WANDER_R && world_is_passable(w,nx,ny) &&
                       !npc_occupied(i,nx,ny) && !(nx==avx&&ny==avy)){
                        g_actx[i]=(uint16_t)nx; g_acty[i]=(uint16_t)ny;
                        g_facing[i]=(uint8_t)d; g_walkf[i]^=1; g_amoved[i]=1; continue;
                    }
                }
                g_amoved[i]=0; continue;                     /* stand this turn */
            }
            /* strayed past the radius: fall through and walk back toward the anchor */
        }
        if(x==dx && y==dy){ g_amoved[i]=0; continue; } /* arrived: stand */
        int mx,my;
        if(npc_pathstep(w,x,y,dx,dy,avx,avy,&mx,&my)){  /* route around walls */
            int nx=(x+mx)&(U6_SURFACE_TILES-1), ny=(y+my)&(U6_SURFACE_TILES-1);
            if(npc_occupied(i,nx,ny)){ g_amoved[i]=0; continue; }  /* tile taken: wait */
            g_actx[i]=(uint16_t)nx; g_acty[i]=(uint16_t)ny;
            g_facing[i]= my<0?0 : my>0?2 : mx>0?1 : 3;  /* N E S W */
            g_walkf[i]^=1; g_amoved[i]=1;
        } else g_amoved[i]=0;                            /* no route: stand */
    }
}

/* index of a living world object of obj_n `on` at (x,y), or -1 — via the spatial bucket index
 * (g_obkt, built in build_blocked), so it scans only the ~few objects in that tile's bucket
 * instead of all g_ow.nobjs (this is called per in-view actor every frame, for sit detection). */
static int obj_idx_at(int x,int y,int on){
    int b=((y>>4)&(OBKT-1))*OBKT+((x>>4)&(OBKT-1));
    for(int k=g_obkt_start[b];k<g_obkt_start[b+1];k++){ int i=g_obkt_idx[k]; const wobj_t*o=&g_wobj[i];
        if(o->alive && o->x==x && o->y==y && o->obj_n==(uint16_t)on) return i; }
    return -1;
}
/* base (frame-0) tile for an obj_n, from the pack's BASETILE table. */
static int obj_basetile(int on){
    return (g_basetile && on>=0 && on<1024) ? (g_basetile[on*2]|(g_basetile[on*2+1]<<8)) : 0;
}
/* seated-NPC sprite frame (Nuvie U6Actor::sit_on_chair) — returns the SIT tile, or -1 if
 * the actor isn't seated. The sit frame is the 4th/"special" per-direction frame:
 *   base + dir*tpd + 3.
 *  - THRONE (327, double-width): actor on the LEFT tile (anchor one tile EAST) -> faces
 *    SOUTH (dir 2), Nuvie's hardcoded 8+3.
 *  - CHAIR (252, single tile): actor on the chair -> faces the chair's frame direction
 *    (chair tile - base(252), 0..3). */
static int npc_sit_tile(int i)
{
    if(g_atile[i]==0xffff || g_actz[i]!=0) return -1;
    int tpd=g_atpd[i]; if(tpd<4) return -1;                 /* only 16-frame humanoids have a sit frame */
    int ax=g_actx[i], ay=g_acty[i];
    if(obj_idx_at((ax+1)&(U6_SURFACE_TILES-1),ay,327)>=0)   /* throne: faces south */
        return g_abase[i] + 2*tpd + 3;
    int ci=obj_idx_at(ax,ay,252);                           /* chair: faces the chair's direction */
    if(ci>=0){ int dir=(g_wobj[ci].tile - obj_basetile(252)) & 3; return g_abase[i] + dir*tpd + 3; }
    return -1;
}

/* the tile to draw for actor i: a directional facing+gait frame for humanoids,
 * else the saved appearance. While walking, cycle the two walk frames (per-dir
 * frames 1,2 for 4-frame humanoids); standing uses frame 0. A seated NPC (on a
 * throne) uses its sit frame. (docs/formats/tileflag frame layout from Nuvie U6Actor.) */
static int npc_tile(int i)
{
    int tpd=g_atpd[i];
    if(tpd<2) return g_atile[i];
    int st=npc_sit_tile(i); if(st>=0) return st;            /* seated NPC (e.g. Lord British) */
    int wf;
    if(!g_amoved[i])    wf=0;                    /* stand */
    else if(tpd>=4)     wf=1+(g_walkf[i]&1);     /* walk frames 1,2 (avoid stand 0 / special 3) */
    else                wf=g_walkf[i]&1;          /* 2-frame actor: stand/step gait */
    return g_abase[i] + g_facing[i]*tpd + wf;
}

/* Where to draw actor i: its mutable surface position (live save or schedule-
 * seeded, then walked by step_npcs). Returns 1 (+fills *ax,*ay) if on surface. */
static int actor_pos(int i,int*ax,int*ay)
{
    if(g_atile[i]==0xffff||g_actz[i]!=0) return 0;   /* none / off-surface */
    *ax=g_actx[i]; *ay=g_acty[i]; return 1;
}

/* Rebuild g_blocked + g_sightblk: per tile, set a bit when an impassable OBJECT
 * (tileflags1 bit1 Impassable | bit2 Wall) sits on it (so world_is_passable stops NPCs/player
 * walking through wall/furniture objects), and a SIGHT bit when a sight-blocking OBJECT
 * (blocks_sight: boundary tile that is not a look-through window — a CLOSED DOOR on an open
 * floor tile is the key case) sits on it, so has_los blocks LOS through it. The two flags
 * differ: a table is impassable but see-over; an open door is passable + see-through. Rebuilt
 * when a door opens/closes (an opened door's tile is no longer a sight-blocker). (Stage 3) */
static void build_blocked(void)
{
    for(int32_t i=0;i<(int32_t)sizeof g_blocked;i++){ g_blocked[i]=0; g_sightblk[i]=0; }
    for(int32_t i=0;i<g_ow.nobjs;i++){ const wobj_t*o=&g_wobj[i]; if(!o->alive) continue;
        int t=o->tile; if(t<0||t>=2048) continue;
        int32_t idx=(int32_t)o->y*1024+o->x;
        if(g_tflags1[t]&0x06) g_blocked[idx>>3]|=(uint8_t)(1u<<(idx&7));
        if(blocks_sight(t))   g_sightblk[idx>>3]|=(uint8_t)(1u<<(idx&7));
    }
    for(int i=0;i<g_nfields;i++) if(g_fields[i].kind==3){    /* energy/force field blocks passage (§5a) */
        int32_t idx=(int32_t)g_fields[i].y*1024+g_fields[i].x;
        g_blocked[idx>>3]|=(uint8_t)(1u<<(idx&7)); }
    /* spatial bucket index (CSR): count -> prefix-sum -> fill. Lets compose_map visit only the
     * objects in the buckets overlapping the view instead of scanning all g_ow.nobjs. */
    for(int b=0;b<=OBKT*OBKT;b++) g_obkt_start[b]=0;
    for(int32_t i=0;i<g_ow.nobjs;i++){ const wobj_t*o=&g_wobj[i]; if(!o->alive) continue;
        int b=((o->y>>4)&(OBKT-1))*OBKT+((o->x>>4)&(OBKT-1)); g_obkt_start[b+1]++; }
    for(int b=0;b<OBKT*OBKT;b++) g_obkt_start[b+1]=(uint16_t)(g_obkt_start[b+1]+g_obkt_start[b]);
    { static uint16_t cur[OBKT*OBKT]; for(int b=0;b<OBKT*OBKT;b++) cur[b]=g_obkt_start[b];
      for(int32_t i=0;i<g_ow.nobjs;i++){ const wobj_t*o=&g_wobj[i]; if(!o->alive) continue;
          int b=((o->y>>4)&(OBKT-1))*OBKT+((o->x>>4)&(OBKT-1)); g_obkt_idx[cur[b]++]=(uint16_t)i; } }
}

/* substitute an animated tile slot for its current frame (animdata.md). The slot
 * itself is blank in the tileset; we draw src + (counter % nframes) instead — which
 * both animates it and FILLS it (un-animated, these tiles render as holes). */
static int anim_tile(int t)
{
    if(t>=0 && t<2048){ int e=g_anim_idx[t];
        if(e>=0 && g_anim_n[e]>0){ g_view_has_anim=1;       /* an ANIMDATA tile is in view */
            return (int)g_anim_src[e] + (int)(g_anim_ctr % g_anim_n[e]); }
    }
    return t;
}

/* tile to draw for a world object: fire/flame objects cycle their own frame_n
 * (objanim, U6WE) — offset by position so braziers flicker independently; other
 * objects fall through to anim_tile (ANIMDATA blank-slot fill). (object_anim.md) */
static int obj_tile(const wobj_t*o)
{
    int on=o->obj_n;
    if(on>=0 && on<1024 && g_objanim[on] && g_basetile){
        g_view_has_anim=1;                              /* a frame-animated object is in view */
        int b=g_basetile[on*2]|(g_basetile[on*2+1]<<8);
        return b + (int)((g_anim_ctr + o->x + o->y) % g_objanim[on]);
    }
    return anim_tile(o->tile);
}

/* blit one 16x16 tile into the compose buffer at view cell (cvx,cvy),
 * transparency-aware and clipped to the VTxVT view. `pass` selects the layer
 * (matching Nuvie's drawTopTile): <0 = draw regardless (actors); 0 = LOWER pass,
 * only non-foreground tiles; 1 = TOP pass, only Foreground/toptile tiles. The
 * Foreground decision is PER TILE (tileflags2 bit4), not per object. */
static void draw_cell(uint8_t*tmp,int T,int cvx,int cvy,int tile,int pass)
{
    if(cvx<0||cvy<0||cvx>=VT||cvy>=VT||tile<0||tile>=2048) return;
    if(cvx<g_clip_x0||cvx>=g_clip_x1||cvy<g_clip_y0||cvy>=g_clip_y1) return;  /* band clip (incremental recompose) */
    if(pass>=0 && ((g_tflags[tile]>>4)&1)!=pass) return;   /* tile belongs to other pass */
    const uint8_t*src=g_tileset+(int32_t)tile*256;
    for(int r=0;r<16;r++){uint8_t*d=tmp+(cvy*16+r)*T+cvx*16;const uint8_t*s=src+r*16;
        for(int c=0;c<16;c++) if(s[c]!=TILE_TRANSPARENT) d[c]=s[c];}
}

/* draw an object/actor whose (anim-resolved) anchor tile is `tile` at view cell
 * (vx,vy), expanding multi-tile objects up/left per tileflags2 bits6-7 — anchor is
 * the bottom-right cell, extras use the PREVIOUS tile numbers (tile-1 left,
 * tile-2 up, tile-3 corner), exactly as Nuvie's drawTile. Each tile is gated by
 * its own Foreground flag via `pass`. (docs/formats/tileflag.md.) */
static void draw_multi(uint8_t*tmp,int T,int vx,int vy,int tile,int pass)
{
    /* tileflags2 size bits — VERIFIED against Nuvie tile_manager.cpp:
     *   bit7 (0x80) = double-WIDTH,  bit6 (0x40) = double-HEIGHT.
     * (Earlier we had these two swapped, which drew every multi-tile object's
     * extra cells one tile up-and-right of where they belong — the throne/archway
     * "assembly" bug.) Anchor is the bottom-right; width extra is to the LEFT,
     * height extra ABOVE; decrement tile_num for each, width first, then height,
     * then the corner — matching Nuvie's drawTile order. */
    int f=(tile>=0&&tile<2048)?g_tflags[tile]:0;
    int dblw=f&0x80, dblh=f&0x40;
    int tn=tile;
    draw_cell(tmp,T,vx,vy,tn,pass);                                   /* anchor (x,y) */
    if(dblw){ tn--; draw_cell(tmp,T,vx-1,vy  ,anim_tile(tn),pass); }  /* dbl-width:  (x-1,y) */
    if(dblh){ tn--; draw_cell(tmp,T,vx  ,vy-1,anim_tile(tn),pass); }  /* dbl-height: (x,y-1) */
    if(dblw&&dblh){ tn--; draw_cell(tmp,T,vx-1,vy-1,anim_tile(tn),pass); } /* corner (x-1,y-1) */
}

/* ---- Lighting overlay (docs/rules/lighting.md §2-3) -------------------------
 * Per-view-cell light 0..255 = ambient (gated by tile_is_indoor + the game clock)
 * + additive light globes from emitter tiles (tileflags2 bits0-1) and the avatar.
 * Applied by remapping each map pixel through darklut[light>>5] — palette->darker-
 * palette LUTs shipped in the pack (U6WJ). NLEV must match tools/mkworld. */
#define NLEV 16
static uint8_t g_darklut[NLEV][256];
#define LIGHT_INDOOR_AMB 24       /* indoor ambient AT NIGHT (torch-lit only); by DAY interiors are full daylight */
#define LIGHT_NIGHT_AMB  56       /* outdoor night ambient (moonlight)                   */
static int light_of(int tile){ return (tile>=0&&tile<2048)?(g_tflags[tile]&0x03):0; } /* tflag2 bits0-1 */
/* Additive light globe: brightness `base` at the centre, linear falloff to 0 at radius
 * R (octagonal distance for a rounder pool). Tight R so far cells fall back to ambient
 * (black), matching ScummVM's lit-centre / dark-corners look. */
static void add_globe(int16_t*L,int cx,int cy,int base,int R){
    for(int dy=-R;dy<=R;dy++)for(int dx=-R;dx<=R;dx++){
        int x=cx+dx,y=cy+dy; if(x<0||y<0||x>=VT||y>=VT) continue;
        int ax=dx<0?-dx:dx, ay=dy<0?-dy:dy;
        int d=(ax>ay)?(ax+(ay>>1)):(ay+(ax>>1));                    /* octagonal ~Euclidean */
        if(d>=R) continue;
        int c=base*(R-d)/R; if(c>0) L[y*VT+x]+=(int16_t)c;
    }
}
/* fill light_out[VT*VT] with the per-cell light value (0..255) for the view. `inside` =
 * avatar is under a roof; only then does indoor-darkening apply (when OUTSIDE, buildings are
 * covered by drawn roofs and everything visible is exterior -> day/night ambient). */
static void compute_light(const world_t*w, uint8_t*light_out, int inside){
    int32_t left=w->px-VT/2, top=w->py-VT/2;
    int hour=clock_minutes(w)/60, day=(hour>=6 && hour<18);
    static const int BASE[4]={0,176,224,255};                       /* globe brightness by level */
    static const int RAD [4]={0,2,3,4};                             /* globe radius by level     */
    int16_t L[VT*VT];
    for(int vy=0;vy<VT;vy++)for(int vx=0;vx<VT;vx++){
        int wx=(int)((left+vx)&(U6_SURFACE_TILES-1)), wy=(int)((top+vy)&(U6_SURFACE_TILES-1));
        /* DAY: interiors are full daylight (roofs only HIDE interiors from outside, they don't
         * darken them — matches U6/ScummVM). NIGHT: indoors fall to torch-only ambient, outdoors
         * to moonlight. (Dungeons would be a separate always-dark case — not surface throne room.) */
        int amb = day ? 255 : ((inside && tile_is_indoor(wx,wy)) ? LIGHT_INDOOR_AMB : LIGHT_NIGHT_AMB);
        L[vy*VT+vx]=(int16_t)amb;
    }
    for(int vy=0;vy<VT;vy++)for(int vx=0;vx<VT;vx++){              /* terrain emitter globes */
        int e=light_of(world_tile(w,left+vx,top+vy)); if(e>0) add_globe(L,vx,vy,BASE[e],RAD[e]);
    }
    { int bx0=((int)left>>4)&(OBKT-1),bx1=(((int)left+VT-1)>>4)&(OBKT-1);   /* object emitters (bucket-indexed) */
      int by0=((int)top >>4)&(OBKT-1),by1=(((int)top +VT-1)>>4)&(OBKT-1);
      for(int by=by0;;by=(by+1)&(OBKT-1)){ for(int bx=bx0;;bx=(bx+1)&(OBKT-1)){ int bb=by*OBKT+bx;
        for(int k=g_obkt_start[bb];k<g_obkt_start[bb+1];k++){ const wobj_t*o=&g_wobj[g_obkt_idx[k]]; if(!o->alive) continue;
            int32_t vx=((int32_t)o->x-left)&(U6_SURFACE_TILES-1), vy=((int32_t)o->y-top)&(U6_SURFACE_TILES-1);
            if(vx<VT&&vy<VT){ int e=light_of(obj_tile(o)); if(e>0) add_globe(L,(int)vx,(int)vy,BASE[e],RAD[e]); } }
        if(bx==bx1) break; } if(by==by1) break; } }
    if(g_spell_light_turns>0) add_globe(L,VT/2,VT/2,255,g_spell_light_rad);  /* Light spell (magic_skills.md) */
    else                      add_globe(L,VT/2,VT/2,150,2);                 /* the avatar's own light */
    for(int i=0;i<VT*VT;i++){ int v=L[i]; if(v<0)v=0; if(v>255)v=255; light_out[i]=(uint8_t)v; }
#ifdef LIGHT_DEBUG_BINARY
    for(int vy=0;vy<VT;vy++)for(int vx=0;vx<VT;vx++){
        int wx=(int)((left+vx)&(U6_SURFACE_TILES-1)), wy=(int)((top+vy)&(U6_SURFACE_TILES-1));
        light_out[vy*VT+vx]= tile_is_indoor(wx,wy)?0:255;        /* indoor=black, outdoor=full */
    }
#endif
}

/* sight-blocker: boundary tile (tflag2 bit2) that is NOT a look-through window (bit3).
 * Walls/closed doors/doorways block; open doors + windowed-open frames see through. */
static int blocks_sight(int tile){
    if(tile<0||tile>=2048) return 0;
    int f=g_tflags[tile]; return (f&0x04) && !(f&0x08);
}
/* C1 visibility flood (docs/rules/lighting.md §1): 4-connected flood from the avatar
 * (view centre) through non-sight-blocking cells. Unreached cells aren't visible (drawn
 * black). reshapeBoundary: sight-blocker cells touching a visible cell are themselves
 * shown (so wall faces draw at the edge of vision). Fills vis[VT*VT] with 0/1. */
static void compute_visibility(const world_t*w, uint8_t*vis){
    if(g_xray_turns>0){ for(int i=0;i<VT*VT;i++) vis[i]=1; return; }   /* X-ray: see through walls (§5e) */
    int32_t left=w->px-VT/2, top=w->py-VT/2;
    uint8_t sb[VT*VT];
    for(int vy=0;vy<VT;vy++)for(int vx=0;vx<VT;vx++)
        sb[vy*VT+vx]=(uint8_t)blocks_sight(world_tile(w,left+vx,top+vy));
    { int bx0=((int)left>>4)&(OBKT-1),bx1=(((int)left+VT-1)>>4)&(OBKT-1);   /* sight-blocking objects (bucket-indexed) */
      int by0=((int)top >>4)&(OBKT-1),by1=(((int)top +VT-1)>>4)&(OBKT-1);
      for(int by=by0;;by=(by+1)&(OBKT-1)){ for(int bx=bx0;;bx=(bx+1)&(OBKT-1)){ int bb=by*OBKT+bx;
        for(int k=g_obkt_start[bb];k<g_obkt_start[bb+1];k++){ const wobj_t*o=&g_wobj[g_obkt_idx[k]]; if(!o->alive) continue;
            int32_t vx=((int32_t)o->x-left)&(U6_SURFACE_TILES-1), vy=((int32_t)o->y-top)&(U6_SURFACE_TILES-1);
            if(vx<VT&&vy<VT && blocks_sight(obj_tile(o))) sb[vy*VT+vx]=1; }
        if(bx==bx1) break; } if(by==by1) break; } }
    for(int i=0;i<VT*VT;i++) vis[i]=0;
    int stack[VT*VT], sp=0, c=(VT/2)*VT+(VT/2);
    vis[c]=1; stack[sp++]=c;
    while(sp){ int cc=stack[--sp], cx=cc%VT, cy=cc/VT;
        static const int dxs[4]={1,-1,0,0}, dys[4]={0,0,1,-1};
        for(int k=0;k<4;k++){ int nx=cx+dxs[k], ny=cy+dys[k];
            if(nx<0||ny<0||nx>=VT||ny>=VT) continue; int ni=ny*VT+nx;
            if(vis[ni]||sb[ni]) continue;
            vis[ni]=1; stack[sp++]=ni;
        }
    }
    uint8_t vis2[VT*VT]; for(int i=0;i<VT*VT;i++) vis2[i]=vis[i];
    for(int vy=0;vy<VT;vy++)for(int vx=0;vx<VT;vx++){ int i=vy*VT+vx;
        if(vis[i]||!sb[i]) continue;                               /* expand walls by 1 (8-way) */
        int seen=0;
        for(int dy=-1;dy<=1&&!seen;dy++)for(int dx=-1;dx<=1;dx++){ int nx=vx+dx,ny=vy+dy;
            if(nx>=0&&ny>=0&&nx<VT&&ny<VT&&vis[ny*VT+nx]){ seen=1; break; } }
        if(seen) vis2[i]=1;
    }
    for(int i=0;i<VT*VT;i++) vis[i]=vis2[i];
}

/* compose-phase profiling (BENCH only): accumulate per-phase 200Hz ticks across the bench's
 * compose loop so we can see where the ~240ms/compose goes (tiles / objects / light+vis /
 * composite) before optimising — see docs/design/performance.md. */
#ifdef BENCH
uint32_t g_phase[4]; static uint32_t g_pht;
#define PH_BEGIN() (g_pht=plat_ticks_200hz())
#define PH_END(i)  (g_phase[i]+=plat_ticks_200hz()-g_pht)
#else
#define PH_BEGIN() ((void)0)
#define PH_END(i)  ((void)0)
#endif

/* draw the world objects overlapping the view, for layer `pass`, via the spatial bucket index
 * (g_obkt_*) — visits only the (<=2x2) buckets the view touches instead of all g_ow.nobjs.
 * Same per-object in-view test + draw as the old full scan, so the result is identical. */
static void draw_view_objects(uint8_t*tmp,int T,int32_t left,int32_t top,int pass){
    int bx0=((int)left>>4)&(OBKT-1), bx1=(((int)left+VT-1)>>4)&(OBKT-1);
    int by0=((int)top >>4)&(OBKT-1), by1=(((int)top +VT-1)>>4)&(OBKT-1);
    for(int by=by0;;by=(by+1)&(OBKT-1)){
        for(int bx=bx0;;bx=(bx+1)&(OBKT-1)){
            int b=by*OBKT+bx;
            for(int k=g_obkt_start[b];k<g_obkt_start[b+1];k++){
                const wobj_t*o=&g_wobj[g_obkt_idx[k]]; if(!o->alive) continue;
                int32_t vx=((int32_t)o->x-left)&(U6_SURFACE_TILES-1);
                int32_t vy=((int32_t)o->y-top )&(U6_SURFACE_TILES-1);
                if(vx<VT&&vy<VT) draw_multi(tmp,T,(int)vx,(int)vy,obj_tile(o),pass);
            }
            if(bx==bx1) break;
        }
        if(by==by1) break;
    }
}

/* fill base tiles into g_tmp_map for view cells [cx0,cx1) x [cy0,cy1) (255=void -> black) */
static void fill_tiles(const world_t*w,int32_t left,int32_t top,int cx0,int cy0,int cx1,int cy1){
    for(int vy=cy0;vy<cy1;vy++)for(int vx=cx0;vx<cx1;vx++){
        int tile=anim_tile(world_tile(w,left+vx,top+vy));
        const uint8_t*src=g_tileset+(int32_t)tile*256;
        for(int r=0;r<16;r++){uint8_t*d=g_tmp_map+(vy*16+r)*TMP_T+vx*16;const uint8_t*s=src+r*16;
            for(int c=0;c<16;c++){ uint8_t v=s[c]; d[c]=(v==TILE_TRANSPARENT)?0:v; }}
    }
}
/* full STATIC rebuild of g_tmp_map: base tiles + lower-pass (pass 0) objects */
static void build_static(const world_t*w){
    int32_t left=w->px-VT/2, top=w->py-VT/2;
    fill_tiles(w,left,top,0,0,VT,VT);
    draw_view_objects(g_tmp_map,TMP_T,left,top,0);
}
/* recompose ONE band of cells into the scrolled g_tmp_map. The cell clip makes the object
 * redraw byte-identical: every object's footprint cell that lands in the band is drawn (in
 * full index order), all other cells rejected — so multi-tile bleed INTO the band lands and
 * nothing outside is touched. */
static void recompose_band(const world_t*w,int cx0,int cy0,int cx1,int cy1){
    int32_t left=w->px-VT/2, top=w->py-VT/2;
    fill_tiles(w,left,top,cx0,cy0,cx1,cy1);                     /* clear band -> base tiles */
    g_clip_x0=cx0; g_clip_y0=cy0; g_clip_x1=cx1; g_clip_y1=cy1;
    draw_view_objects(g_tmp_map,TMP_T,left,top,0);
    g_clip_x0=0; g_clip_y0=0; g_clip_x1=VT; g_clip_y1=VT;       /* restore full clip */
}
/* shift g_tmp_map so new[v] = old[v+(dx,dy)] in tiles (dx,dy in {-1,0,1}); lead band stale */
static void shift_static(int dx,int dy){
    int sx=16*dx, sy=16*dy;                                     /* dest(x,y) <- src(x+sx,y+sy) */
    int x0=sx>0?0:-sx, ww=TMP_T-(sx<0?-sx:sx); if(ww<=0) return;
    int hh=TMP_T-(sy<0?-sy:sy); if(hh<=0) return;
    if(sy>0){ for(int ny=0;ny<hh;ny++)        memmove(&g_tmp_map[ny*TMP_T+x0],&g_tmp_map[(ny+sy)*TMP_T+x0+sx],(size_t)ww); }
    else    { for(int ny=TMP_T-1;ny>=-sy;ny--) memmove(&g_tmp_map[ny*TMP_T+x0],&g_tmp_map[(ny+sy)*TMP_T+x0+sx],(size_t)ww); }
}
/* incremental scroll: shift the static buffer, recompose the lead 2-cell band (2-wide so
 * multi-tile bleed from the new edge is captured) for whichever edge(s) scrolled in. */
static void scroll_static(const world_t*w,int dx,int dy){
    shift_static(dx,dy);
    if(dx>0) recompose_band(w,VT-2,0,VT,VT);
    if(dx<0) recompose_band(w,0,0,2,VT);
    if(dy>0) recompose_band(w,0,VT-2,VT,VT);
    if(dy<0) recompose_band(w,0,0,VT,2);
}
/* set g_view_has_anim for the whole view (anim_tile sets it) — the scroll path only recomposes
 * the edge, so animated tiles in the scrolled interior must still arm the per-tick refresh. */
static void mark_view_anim(const world_t*w){
    int32_t left=w->px-VT/2, top=w->py-VT/2;
    for(int vy=0;vy<VT;vy++)for(int vx=0;vx<VT;vx++) (void)anim_tile(world_tile(w,left+vx,top+vy));
}

static void compose_map(const world_t*w)
{
    static uint8_t tmp[TMP_T*TMP_T];               /* 176x176 working buffer (static layer + dynamic) */
    const int T=TMP_T;
    g_view_has_anim=0;                              /* set by anim_tile/obj_tile if any in view */
    int32_t left=w->px-VT/2, top=w->py-VT/2;
    /* --- STATIC layer (tiles + lower-pass objects): incremental scroll on a 1-tile walk --- */
    PH_BEGIN();
    int dx=(int)w->px-g_tmp_px, dy=(int)w->py-g_tmp_py;     /* wrap to shortest signed delta */
    if(dx>512)dx-=1024; else if(dx<-512)dx+=1024;
    if(dy>512)dy-=1024; else if(dy<-512)dy+=1024;
    if(g_tmp_valid && dx>=-1&&dx<=1 && dy>=-1&&dy<=1 && (dx||dy)){
        scroll_static(w,dx,dy); mark_view_anim(w);
    } else {
        build_static(w);
    }
    g_tmp_px=(int)w->px; g_tmp_py=(int)w->py; g_tmp_valid=1;
    memcpy(tmp,g_tmp_map,sizeof tmp);
    PH_END(0);
    PH_BEGIN();
    /* --- DYNAMIC layer (per-frame): actors + avatar, then foreground objects OVER them ---
     * Sprite layering follows Nuvie's MapWindow order (docs/formats/tileflag.md): lower-pass
     * objects (in g_tmp_map) -> actors + avatar -> TOP-pass (foreground) objects. The
     * Foreground decision is PER TILE in draw_multi, so a throne's seat (non-fg, in the static
     * layer) is behind a seated NPC while its front (fg, drawn now) is over him. */
    for(int i=0;i<g_nfields;i++){                     /* field tiles UNDER actors (§5a) */
        int32_t vx=((int32_t)g_fields[i].x-left)&(U6_SURFACE_TILES-1);
        int32_t vy=((int32_t)g_fields[i].y-top )&(U6_SURFACE_TILES-1);
        if(vx<VT&&vy<VT) draw_cell(tmp,T,(int)vx,(int)vy,anim_tile(g_fields[i].tile),-1);
    }
    for(int i=0;i<U6_MAX_ACTORS;i++){
        if(i==1) continue;
        if(g_ainvis[i]) continue;                    /* Invisibility: not drawn until Reveal / it wears off */
        int ax,ay; if(!actor_pos(i,&ax,&ay)) continue;
        int32_t vx=((int32_t)ax-left)&(U6_SURFACE_TILES-1);
        int32_t vy=((int32_t)ay-top )&(U6_SURFACE_TILES-1);
        if(vx<VT&&vy<VT) draw_multi(tmp,T,(int)vx,(int)vy,anim_tile(npc_tile(i)),-1);
    }
    { int at=g_basetile?(g_basetile[410*2]|(g_basetile[410*2+1]<<8)):0;   /* avatar at centre (obj_n 410) */
      if(at>0 && at<2048) draw_multi(tmp,T,VT/2,VT/2,at,-1); }
    draw_view_objects(tmp,T,left,top,1);             /* foreground (pass 1) objects */
    PH_END(1);
    PH_BEGIN();
    /* Blit terrain shifted in by INSET px so the outer ring stays PAPYRUS (from
     * blit_paper, never overwritten) and the frayed border bites the terrain edge.
     * Start at a TILE BOUNDARY (off=0) so the visible tiles are WHOLE — not
     * half-clipped (which looked like half-drawn doors / banding). We show the
     * inner VT-1 tiles; the outermost ring is covered by the papyrus+fray border.
     * (Matches U6/ScummVM: refshots/u6_gameplay.png.) */
    /* roof drawing (U6WK / Nuvie drawRoofs): when the avatar is OUTSIDE (not under a roof),
     * overdraw roof tiles over roofed cells so buildings show rooftops, not their hidden
     * interiors. When inside, no roofs — the interior shows, darkened by lighting. */
    int inside = tile_is_indoor((int)(w->px&(U6_SURFACE_TILES-1)),(int)(w->py&(U6_SURFACE_TILES-1)));
    if(!inside && g_nroof>0){
        for(int vy=0;vy<VT;vy++)for(int vx=0;vx<VT;vx++){
            int wx=(int)((left+vx)&(U6_SURFACE_TILES-1)), wy=(int)((top+vy)&(U6_SURFACE_TILES-1));
            int di=roof_at(wx,wy); if(di<0||di>=g_nrtiles) continue;
            const uint8_t*src=g_rtiles+(int32_t)di*256;
            for(int r=0;r<16;r++){uint8_t*d=tmp+(vy*16+r)*T+vx*16; const uint8_t*s=src+r*16;
                for(int c=0;c<16;c++) if(s[c]!=TILE_TRANSPARENT) d[c]=s[c];}
        }
    }
    /* lighting overlay: bilinearly interpolate the per-cell light (cell centre at
     * tile+8px) to each pixel, then remap through darklut[light>>4] (NLEV=16). Smooth
     * gradient instead of 16x16 blocks. */
    uint8_t light[VT*VT]; compute_light(w,light,inside);
    uint8_t vis[VT*VT];   compute_visibility(w,vis);              /* C1: line-of-sight */
    if(!inside && g_nroof>0){                                     /* roofs are always visible from outside */
        for(int vy=0;vy<VT;vy++)for(int vx=0;vx<VT;vx++){
            int wx=(int)((left+vx)&(U6_SURFACE_TILES-1)), wy=(int)((top+vy)&(U6_SURFACE_TILES-1));
            if(roof_at(wx,wy)>=0) vis[vy*VT+vx]=1;
        }
    }
    int mnL=255,mxL=0;
    for(int i=0;i<VT*VT;i++){ if(!vis[i]) light[i]=0;            /* darken visible edges by walls */
        int L=light[i]; if(L<mnL)mnL=L; if(L>mxL)mxL=L; }
    PH_END(2);
    PH_BEGIN();
    int span=MAPSZ-2*MAP_INSET;                     /* 160 = 10 whole tiles */
#if !defined(LIGHT_DEBUG_VIS) && !defined(LIGHT_DEBUG_FIELD)
    if(mnL==mxL){                                    /* UNIFORM light + fully visible (outdoor daytime, the common
                                                      * walking case): one darklut level, no per-pixel bilinear/vis.
                                                      * Byte-identical to the slow path when uniform. */
        const uint8_t *lut=g_darklut[mnL>>4];
        for(int y=0;y<span;y++){ uint8_t *drow=ROW(MAPY+MAP_INSET+y)+MAPX+MAP_INSET; const uint8_t *trow=tmp+y*T;
            for(int x=0;x<span;x++) drow[x]=lut[trow[x]]; }
        PH_END(3); return;
    }
#endif
    for(int y=0;y<span;y++){
        int sy=y-8; if(sy<0)sy=0; int cy0=sy>>4, fy=sy&15, cy1=cy0+1; if(cy1>=VT)cy1=VT-1;
        const uint8_t *r0=&light[cy0*VT], *r1=&light[cy1*VT], *vr=&vis[(y>>4)*VT];
        uint8_t *drow=ROW(MAPY+MAP_INSET+y)+MAPX+MAP_INSET; const uint8_t *trow=tmp+y*T;
        for(int x=0;x<span;x++){
#ifdef LIGHT_DEBUG_VIS
            drow[x]= vr[x>>4] ? trow[x] : 0; continue;            /* isolate C1: visible=full, hidden=black */
#endif
            if(!vr[x>>4]){ drow[x]=0; continue; }                 /* C1: not visible -> black */
            int sx=x-8; if(sx<0)sx=0; int cx0=sx>>4, fx=sx&15, cx1=cx0+1; if(cx1>=VT)cx1=VT-1;
            int a=r0[cx0], b=r0[cx1], c=r1[cx0], d=r1[cx1];
            int top=a+(((b-a)*fx)>>4), bot=c+(((d-c)*fx)>>4);
            int Lp=top+(((bot-top)*fy)>>4);                        /* 0..255 interpolated light */
#ifdef LIGHT_DEBUG_FIELD
            drow[x]= Lp ? trow[x] : 0;
#else
            drow[x]= g_darklut[Lp>>4][trow[x]];                    /* 16 levels: light>>4 */
#endif
        }
    }
    PH_END(3);
}

/* blit a 16x16 tile to (sx,sy); transp skips index 255 (transparent) */
static void blit_tile(int sx,int sy,int tile,int transp)
{
    const uint8_t*src=g_tileset+(int32_t)tile*256;
    for(int y=0;y<16;y++)for(int x=0;x<16;x++){
        uint8_t v=src[y*16+x];
        if(!(transp&&v==TILE_TRANSPARENT)) ui_px(sx+x,sy+y,v);
    }
}

/* authentic map-window border: U6 overlays frayed-papyrus edge TILES (432-439)
 * on the OUTER RING of the 11x11 window (MapWindow::drawBorder), TRANSPARENT so
 * the terrain rendered underneath shows through the burn-mark gaps — a torn page
 * laid over the map, not a frame around it. Corners at the ring; edges between. */
static void draw_border_tiles(void)
{
    int x1=MAPX+(VT-1)*16, y1=MAPY+(VT-1)*16;       /* 160: last ring column/row */
    blit_tile(MAPX,MAPY,432,1); blit_tile(x1,MAPY,434,1);
    blit_tile(MAPX,y1,435,1);   blit_tile(x1,y1,437,1);
    for(int c=1;c<VT-1;c++){ int px=MAPX+c*16, py=MAPY+c*16;
        blit_tile(px,MAPY,433,1); blit_tile(px,y1,436,1);   /* top / bottom rows */
        blit_tile(MAPX,py,438,1); blit_tile(x1,py,439,1);   /* left / right cols */
    }
}

/* word-wrap `s` into a box [x..x+w) from y, up to `lines` rows of font text */
static void ui_text_wrap(int x,int y,int w,const char*s,int lines,uint8_t col)
{
    int cols=w/8, row=0;
    while(*s && row<lines){
        while(*s==' ') s++;
        if(!*s) break;
        int n=0, last=-1;
        for(const char*p=s; *p && *p!='\n'; p++,n++){ if(*p==' ') last=n; if(n>=cols) break; }
        int take = (n<cols) ? n : (last>=0?last:cols);
        char line[48]; int t=take<47?take:47;
        for(int k=0;k<t;k++) line[k]=s[k];
        line[t]=0;
        ui_text(x,y+row*9,line,col); row++;
        s+=take; if(*s=='\n') s++;
    }
}

/* blit a portrait already decoded into a flat 56x64 buffer (from talk()) */
static void draw_portrait_buf(const uint8_t*src,int lx,int ly)
{
    ui_frame(lx-1,ly-1,PW+2,PH+2,C_BLACK);
    for(int y=0;y<PH;y++)for(int x=0;x<PW;x++) ui_px(lx+x,ly+y,src[y*PW+x]);
}

/* conversation view: when talking, the NPC's REAL portrait + name on the
 * papyrus, and the VM-built transcript in the scroll box; otherwise an idle
 * prompt so the panel isn't blank as the avatar walks around. */
static void draw_inventory(uint8_t col);
static void draw_party_roster(uint8_t col);
static void draw_char_view(uint8_t col);
static void draw_sky(void);

static void draw_conversation(uint8_t col)
{
    int pxx=PANX+(PANW-PW)/2, pyy=MAPY+16;
    restore_panel();                               /* clean papyrus before text */
    if(g_casting){                                 /* C)ast spell-selection menu (magic_skills.md §6) */
        ui_text(PANX+6, 20, "Cast which spell?", col);
        for(int i=0;i<g_ncast && i<11;i++){        /* numbered list of castable spells */
            char ln[26]; int t=0, num=i+1;
            if(num>=10) ln[t++]=(char)('0'+num/10);
            ln[t++]=(char)('0'+num%10); ln[t++]=')'; ln[t++]=' ';
            const char*nm=g_spells[g_castlist[i]].name;
            for(int k=0;nm[k]&&t<24;k++) ln[t++]=nm[k];
            ln[t]=0;
            ui_text(PANX+10, 36+i*12, ln, col);
        }
        char line[24]; int t=0; line[t++]='>'; line[t++]=' ';   /* the typed selection box */
        for(int k=0;k<g_inlen && t<21;k++) line[t++]=g_inbuf[k];
        line[t++]='_'; line[t]=0;
        ui_text(PANX+6, 36+(g_ncast<11?g_ncast:11)*12+6, line, col);
    } else if(g_talking){                          /* NPC portrait replaces the roster */
        if(g_have_pc) draw_portrait_buf(g_cur_portrait,pxx,pyy);
        ui_text(PANX+(PANW-ui_textw(g_talk_name))/2, pyy+PH+4, g_talk_name, col);
        ui_text_wrap(PANX+6, 116, PANW-12, g_transcript, g_await_input?7:8, col);
        if(g_await_input){                         /* the input box: "> typed_" */
            char line[24]; int t=0; line[t++]='>'; line[t++]=' ';
            for(int k=0;k<g_inlen && t<21;k++) line[t++]=g_inbuf[k];
            line[t++]='_'; line[t]=0;
            ui_text(PANX+6, 116+7*9, line, col);
        }
    } else if(g_show_status){
        draw_char_view(col);                       /* a party member's portrait + gender + stats */
    } else if(g_show_inv){
        draw_sky();
        draw_inventory(col);
    } else {
        draw_sky();                                /* day/night bar + party roster */
        draw_party_roster(col);
        if(g_msg[0]) ui_text_wrap(PANX+6, 116, PANW-12, g_msg, 5, col);  /* scroll box */
        if(g_pending_verb){                        /* awaiting a direction */
            const char*p = g_pending_verb=='A'?"Attack-(dir)?":g_pending_verb=='L'?"Look-(dir)?":
                           g_pending_verb=='G'?"Get-(dir)?":g_pending_verb=='U'?"Use-(dir)?":
                           g_pending_verb=='C'?"Cast-(dir)?":
                           g_pending_verb=='T'?"Talk-(dir)?":"Move-(dir)?";
            ui_text(PANX+6, 150, p, col);
        }
    }
}

/* time + wind on the map window's BOTTOM frayed border (left-aligned), where U6
 * shows its date/wind line — verified against ScummVM (refshots/u6_gameplay.png).
 * Drawn after compose_map + draw_border_tiles so it sits on top of the border. */
static int put2(char*s,int k,int v){ if(v>=10) s[k++]=(char)('0'+(v/10)%10); s[k++]=(char)('0'+v%10); return k; }
static void draw_status(const world_t*w,uint8_t col)
{
    int m=clock_minutes(w), hh=m/60, mm=m%60;
    char s[40]; int k=0;
    k=put2(s,k,hh); s[k++]=':'; k=put2(s,k,mm);           /* HH:MM */
    { const char*t=" Lv"; while(*t) s[k++]=*t++; }
    s[k++]=(char)('0'+(g_alevel[1]<10?g_alevel[1]:9));    /* avatar exp level */
    { const char*t=" MP"; while(*t) s[k++]=*t++; }        /* avatar magic points (§ play) */
    k=put2(s,k,g_amana[1]); s[k++]='/'; k=put2(s,k,g_amaxmana[1]);
    s[k]=0;
    ui_text(MAPX+4, MAPY+MAPSZ-10, s, col);               /* bottom border of the map window */
}

/* Resolve NPC `id`'s name from the game's own CONVERSE data (the VM sets npcname
 * via SIDENT during the greeting). Used to label the party roster without baking
 * any game text into our source. STARTUP ONLY (reuses g_cv + g_script_buf, with
 * a throwaway game state so it never disturbs the persistent g_cg). */
static void resolve_name(int id, char *out, int cap)
{
    out[0]=0;
    const uint8_t *cf; int32_t cl; int ci;
    if(id<g_countA){ cf=g_convA; cl=g_convAlen; ci=id; }
    else           { cf=g_convB; cl=g_convBlen; ci=id-g_countA; }
    int32_t elen; const uint8_t *e=lib_entry(cf,cl,ci,&elen); if(!e) return;
    int32_t got=u6lzw_decompress(e,elen,g_script_buf,(int32_t)sizeof g_script_buf); if(got<=0) return;
    cvm_game_t cg; cvm_game_init(&cg,1);
    cvm_init(&g_cv,g_script_buf,got,&cg,id,g_avatar_name,g_avatar_title);
    cvm_run(&g_cv);
    int o=0; while(g_cv.npcname[o]&&o<cap-1){ out[o]=g_cv.npcname[o]; o++; } out[o]=0;
}

/* push the real party into the VM's in_party[] before a conversation (so INPARTY
 * / party-count checks branch correctly). */
static void sync_party_in(void)
{
    for(int i=0;i<256;i++) g_cg.in_party[i]=0;
    for(int i=0;i<g_nparty;i++){ int a=g_party[i].actor; if(a>=0&&a<256) g_cg.in_party[a]=1; }
}

/* pull conversation JOIN/LEAVE (the VM's in_party[]) back into the roster — this
 * is the cvm_game_t <-> engine wiring made visible. */
static void sync_party_out(void)
{
    for(int i=1;i<256;i++){                          /* JOIN: add new members */
        if(!g_cg.in_party[i]) continue;
        int found=0; for(int j=0;j<g_nparty;j++) if(g_party[j].actor==i){ found=1; break; }
        if(!found && g_nparty<MAXPARTY){
            g_party[g_nparty].actor=i; g_party[g_nparty].hp=g_ahp[i]?g_ahp[i]:30;
            const char *nm = (i==g_cv.npc && g_talk_name[0]) ? g_talk_name : "?";
            int o=0; while(nm[o]&&o<15){ g_party[g_nparty].name[o]=nm[o]; o++; } g_party[g_nparty].name[o]=0;
            g_nparty++;
        }
    }
    for(int j=0;j<g_nparty;){                         /* LEAVE: drop members (keep avatar) */
        int a=g_party[j].actor;
        if(a!=0 && !g_cg.in_party[a]){ for(int k=j;k<g_nparty-1;k++) g_party[k]=g_party[k+1]; g_nparty--; }
        else j++;
    }
}

/* --- inventory <-> conversation VM (gold is already the shared g_cg.gold) ------------------- */
static int basetile(int obj_n);                       /* fwd (defined with the object verbs below) */
static int is_food_objn(int on);                      /* fwd (food/drink table, below) */
/* add / remove `qty` of obj_n to the Avatar's engine inventory (merge by obj_n, new stack else). */
static void inv_add_objn(int on, int qty)
{
    for(int i=0;i<g_ow.ninv;i++) if((g_ow.inv[i].obj_n&0xff)==(on&0xff)){
        int t=g_ow.inv[i].qty+qty; g_ow.inv[i].qty=(uint16_t)(t>65535?65535:t); return; }
    if(g_ow.ninv<OBJ_INV_MAX){ g_ow.inv[g_ow.ninv].obj_n=(uint16_t)on; g_ow.inv[g_ow.ninv].tile=basetile(on);
        g_ow.inv[g_ow.ninv].qty=(uint16_t)qty; g_ow.inv[g_ow.ninv].quality=0; g_ow.ninv++; }
}
static void inv_remove_objn(int on, int qty)
{
    for(int i=0;i<g_ow.ninv && qty>0;i++) if((g_ow.inv[i].obj_n&0xff)==(on&0xff)){
        int take=g_ow.inv[i].qty<qty?g_ow.inv[i].qty:qty; g_ow.inv[i].qty-=(uint16_t)take; qty-=take;
        if(g_ow.inv[i].qty==0){ for(int k=i;k<g_ow.ninv-1;k++) g_ow.inv[k]=g_ow.inv[k+1]; g_ow.ninv--; i--; } }
}

/* BEFORE a conversation: push the engine inventory into the VM's per-obj_n counts, so scripts
 * see what the player carries (has-object checks, merchant "dost thou have..."). */
static void sync_inv_in(void)
{
    for(int i=0;i<256;i++) g_cg.player_obj[i]=0;
    for(int i=0;i<g_ow.ninv;i++){ int on=g_ow.inv[i].obj_n&0xff, q=g_ow.inv[i].qty?g_ow.inv[i].qty:1;
        int t=(int)g_cg.player_obj[on]+q; g_cg.player_obj[on]=(int16_t)(t>32767?32767:t); }
}

/* AFTER a conversation turn: pull the VM's per-obj_n changes (GIVE/NEW/DELETE) back into the
 * engine inventory. delta = the VM count - the engine's current count; add or remove that many. */
static void sync_inv_out(void)
{
    for(int on=1;on<256;on++){
        if(on==(CVM_OBJ_GOLD&0xff)) continue;         /* gold is the shared g_cg.gold */
        int have=0; for(int i=0;i<g_ow.ninv;i++) if((g_ow.inv[i].obj_n&0xff)==on) have+=g_ow.inv[i].qty?g_ow.inv[i].qty:1;
        int delta=(int)g_cg.player_obj[on]-have;
        if(delta>0) inv_add_objn(on,delta);           /* the NPC gave the player items */
        else if(delta<0) inv_remove_objn(on,-delta);  /* the NPC took items */
    }
}

/* Talk to NPC `id`: decompress its conversation from CONVERSE.A/.B, run the VM
 * for the greeting (+ auto-answer "name"), and decode its 56x64 portrait from
 * PORTRAIT.A/.B. Builds g_transcript / g_talk_name / g_cur_portrait and sets
 * g_talking. The conversation index is the NPC number; portrait index id-1. */
/* CONVERSATION-mode palette (U6WY, ST only): while an NPC portrait is up, swap the whole ST 16 to
 * one tuned for the conversation screen (portrait + parchment + a terrain anchor) so the portrait
 * renders true instead of muddy — docs/design/palette_lowcolor.md. The main loop's full present_frame
 * re-c2p's the whole backbuffer through the new LUT. No-op on TT/Falcon (rdr_set_lowcolor is a no-op
 * there — they show all 256). enter by portrait index pidx (=id-1); leave restores the banded game. */
static void conv_palette_enter(int pidx)
{
    if(g_nconvpal>0 && pidx>=0 && pidx<g_nconvpal){
        const uint8_t *base=g_convpal+(int32_t)pidx*(48+256);   /* 3-bit pal + shared 256->16 LUT */
        const uint8_t *pal = (g_is_ste && g_convpal4) ? g_convpal4+(int32_t)pidx*48 : base;   /* STE 4-bit */
        rdr_set_lowcolor(pal, base+48);
    }
}
static void conv_palette_leave(void)
{
    if(!g_have_lowcolor) return;
    rdr_set_lowcolor(LCPAL(g_lc_pal, g_lc_pal4), g_lc_lut);
    if(g_lcx_nb>=2) rdr_set_lowcolor_bands(g_lcx_nb, g_lcx_y0, LCPAL(g_lcx_pal, g_lcx_pal4), g_lcx_lut);
}

static void talk(int id)
{
    const uint8_t *cf; int32_t cl; int ci;
    if(id<g_countA){ cf=g_convA; cl=g_convAlen; ci=id; }
    else           { cf=g_convB; cl=g_convBlen; ci=id-g_countA; }
    int32_t elen; const uint8_t *e=lib_entry(cf,cl,ci,&elen);
    if(!e) return;
    int32_t got=u6lzw_decompress(e,elen,g_script_buf,(int32_t)sizeof g_script_buf);
    if(got<=0) return;

    sync_party_in(); sync_inv_in();             /* persistent g_cg + real party + inventory */
    g_cg.hour=(uint8_t)g_hour;                   /* $T greeting word tracks the game clock */
    cvm_init(&g_cv,g_script_buf,got,&g_cg,id,g_avatar_name,g_avatar_title);
    cvm_status st=cvm_run(&g_cv);
    { int o=0; while(g_cv.npcname[o]&&o<31){ g_talk_name[o]=g_cv.npcname[o]; o++; } g_talk_name[o]=0; }
    { const char*gt=cvm_text(&g_cv), *q=gt;     /* skip the look desc to the greeting */
      while(*q && *q!='"') q++;
      tappend(0, *q?q:gt); }
    sync_party_out(); sync_inv_out();           /* greeting may JOIN/LEAVE or give/take items */

    /* portrait: index id-1, library A for <98 else B (PORTRAIT.A holds 98) */
    g_have_pc=0;
    int pidx=id-1;
    if(pidx>=0){
        const uint8_t *pf; int32_t pl; int pi;
        if(pidx<98){ pf=g_portA; pl=g_portAlen; pi=pidx; }
        else        { pf=g_portB; pl=g_portBlen; pi=pidx-98; }
        int32_t pe; const uint8_t *pp=lib_entry(pf,pl,pi,&pe);
        if(pp && u6lzw_decompress(pp,pe,g_cur_portrait,(int32_t)sizeof g_cur_portrait)==PW*PH)
            g_have_pc=1;
    }
    if(g_have_pc) conv_palette_enter(pidx);   /* ST: swap to this portrait's conversation palette */
    g_talking=1;
    g_inlen=0; g_inbuf[0]=0;
    g_await_input=(st==CVM_ASK);   /* wait for the player to type a keyword/number */
}

/* close the conversation panel (BYE / walk away) */
static void end_talk(void)
{ g_talking=0; g_await_input=0; g_inlen=0; g_inbuf[0]=0; conv_palette_leave(); }   /* ST: restore game palette */

/* feed the typed keyword/number to the paused VM and resume it. The transcript
 * shows this exchange (echoed input + the NPC's reply); we keep the box open
 * while the VM still wants input, else the conversation has ended. */
static void submit_input(void)
{
    if(g_inlen==0) return;                       /* ignore an empty line */
    cvm_input(&g_cv,g_inbuf);
    cvm_status st=cvm_run(&g_cv);
    int tl=0;
    tl=tappend(tl,"> "); tl=tappend(tl,g_inbuf); tl=tappend(tl,"  ");
    tappend(tl,cvm_text(&g_cv));
    g_inlen=0; g_inbuf[0]=0;
    g_await_input=(st==CVM_ASK);
    sync_party_out(); sync_inv_out();            /* a keyword may change the party OR give/take items/gold */
}

/* index of an NPC orthogonally/diagonally adjacent to the avatar at g_hour,
 * standing on the surface (z==0), or -1 if none. */
static int find_adjacent_npc(const world_t*w)
{
    for(int i=0;i<U6_MAX_ACTORS;i++){
        if(i==1) continue;                     /* the Avatar */
        int ax,ay; if(!actor_pos(i,&ax,&ay)) continue;
        int dx=ax-(int)w->px, dy=ay-(int)w->py;
        if(dx>=-1&&dx<=1 && dy>=-1&&dy<=1 && !(dx==0&&dy==0)) return i;
    }
    return -1;
}

/* ---- object verbs: look / get / use + inventory ------------------------- */

/* format the SINGULAR look name for a tile into out[cap] (drops the plural
 * `\`-run; keeps the singular `/`-run — see docs/formats/look.md). */
static void obj_name(int tile,char*out,int cap)
{
    const char*s=(tile>=0&&tile<2048)?g_looktbl[tile]:0;
    int o=0; if(!s){ out[0]=0; return; }
    for(int i=0;s[i]&&o<cap-1;){
        char c=s[i];
        if(c=='\\'){ i++; while(s[i]>='a'&&s[i]<='z') i++; }      /* plural-only: skip */
        else if(c=='/'){ i++; while(s[i]>='a'&&s[i]<='z'&&o<cap-1) out[o++]=s[i++]; }
        else { out[o++]=c; i++; }
    }
    out[o]=0;
}

static int msgcat(int n,const char*s){ while(*s&&n<(int)sizeof g_msg-1) g_msg[n++]=*s++; g_msg[n]=0; return n; }
static int msgcat_int(int n,int v);   /* decimal append (defined below) */

/* obj_n -> base tile (LE u16) from the shipped BASETILE; an object's frame is
 * tile - basetile(obj_n). */
static int basetile(int obj_n)
{ return (obj_n>=0&&obj_n<1024&&g_basetile) ? (g_basetile[obj_n*2]|(g_basetile[obj_n*2+1]<<8)) : 0; }

/* USE dispatch keyed by object number (docs/rules/usecode_use.md). Doors toggle
 * open/close (frame +/- 4 => tile +/- 4). Returns 1 if the map must recompose. */
/* searchable container object types (objblk.md §2: Nuvie's OBJTYPE_CONTAINER set) */
static int is_container(int on){
    return on==98||on==99||on==168||on==176||on==177||on==186||on==188||on==191||on==192||on==339;
}
/* may a SEARCH spill this container right now? chest/crate/barrel only when OPEN (frame 0);
 * bags/drawers/desks/bodies at any frame (Nuvie's per-frame usecode table). */
static int container_searchable(int idx){
    int on=g_wobj[idx].obj_n;
    if(!is_container(on)) return 0;
    if(on==98||on==192||on==186) return ((int)g_wobj[idx].tile - basetile(on))==0;
    return 1;
}
/* SEARCH a container (authentic U6: "Searching here, you find …"): every DIRECT child moves
 * out of the container onto its map tile (chained containers keep their own contents, so a
 * spilled bag can be searched in turn). Appends up to 3 item names at msg position *np
 * (updates *np). Returns the number of items found (0 = "nothing"). We rebuild the object
 * buckets/blocking since new objects just appeared on the map; the caller recomposes. */
static int spill_container(int ci,int*np){
    int found=0, n=*np; uint16_t cx=g_wobj[ci].x, cy=g_wobj[ci].y;
    for(int32_t j=0;j<g_ow.nobjs;j++){
        wobj_t*o=&g_wobj[j];
        if(o->alive || o->parent!=(uint16_t)ci) continue;
        o->alive=1; o->x=cx; o->y=cy; o->origin=(uint16_t)ci; o->parent=WOBJ_PARENT_NONE;  /* remember the container for the save */
        if(found<3){ char inm[24]; obj_name(o->tile,inm,sizeof inm);
            if(found) n=msgcat(n,", ");
            n=msgcat(n,inm[0]?inm:"something"); }
        else if(found==3) n=msgcat(n,", ...");
        found++;
    }
    if(found) build_blocked();          /* new on-map objects: rebuild buckets + blocking */
    *np=n;
    return found;
}

/* Mechanism obj_n (docs/rules/usecode_use.md §Levers). ACTUATORS drive TARGETS: levers/switches
 * link to fields by QUALITY (confirmed 1:1 across the world: quality Q lever ↔ quality Q force
 * field); cranks/pull-chains (quality 0) link to a nearby portcullis by POSITION. */
static int is_actuator_objn(int o){ return o==174||o==268||o==288||o==419; }  /* switch/lever/crank/pull-chain */
static int is_field_objn(int o){ return o==51||o==175||o==317||o==318||o==319||o==320; } /* force/electric/fire/poison/protection/sleep */

static int use_object_inner(int idx)
{
    int o=g_wobj[idx].obj_n; char nm[40]; obj_name(g_wobj[idx].tile,nm,sizeof nm);
    if(o>=297 && o<=300){                            /* oaken/windowed/cedar/steel door */
        int fr=(int)g_wobj[idx].tile - basetile(o);  /* 0-3 open, 4-7 closed, 9/11 LOCKED, 13/15 MAGIC (usecode_use.md) */
        int n;
        if(fr==13||fr==15){ msgcat(0,"Magically locked."); return 0; }
        if(fr==9||fr==11){                           /* locked: matching KEY (obj 64) or a LOCKPICK (obj 63) */
            if(g_wobj[idx].quality!=0 && objworld_has(&g_ow,64,g_wobj[idx].quality)){
                g_wobj[idx].tile-=4;                 /* unlock: frame -4 -> closed (still shut) */
                msgcat(0,"Unlocked."); return 1; }
            if(objworld_count(&g_ow,63)>0){          /* a lockpick opens any lock (a success doesn't break it) */
                g_wobj[idx].tile-=4; msgcat(0,"You pick the lock."); return 1; }
            msgcat(0,"Locked."); return 0; }
        if(fr>=0 && fr<=3){ g_wobj[idx].tile+=4; n=msgcat(0,"You close the "); }
        else if(fr>=4 && fr<=7){ g_wobj[idx].tile-=4; n=msgcat(0,"You open the "); }
        else { n=msgcat(0,"Nothing happens with the "); n=msgcat(n,nm); msgcat(n,"."); return 0; }
        n=msgcat(n,nm); msgcat(n,".");
        build_blocked();   /* door passability changed (closed door blocks, open doesn't) */
        return 1;
    }
    if(o==98||o==192||o==186){                       /* chest/crate/barrel: toggle open<->closed */
        int fr=(int)g_wobj[idx].tile - basetile(o);  /* frame 0 = open, 1 = closed (Nuvie use_container) */
        if(o==98 && fr==3){ msgcat(0,"Magically locked."); return 0; }
        if(o==98 && fr==2){                          /* locked chest: quality = key id, or pick it */
            if(g_wobj[idx].quality!=0 && objworld_has(&g_ow,64,g_wobj[idx].quality)){
                g_wobj[idx].tile--;                  /* unlock: frame 2 -> 1 (closed) */
                msgcat(0,"Unlocked."); return 1; }
            if(objworld_count(&g_ow,63)>0){          /* a lockpick opens any lock */
                g_wobj[idx].tile--; msgcat(0,"You pick the lock."); return 1; }
            msgcat(0,"No effect."); return 0; }      /* Nuvie's locked-chest reply */
        if(fr==1){ g_wobj[idx].tile--;               /* open it, then search */
            int n=msgcat(0,"You open the "); n=msgcat(n,nm); n=msgcat(n,". Searching here, you find ");
            if(!spill_container(idx,&n)) n=msgcat(n,"nothing");
            msgcat(n,".");
            build_blocked(); return 1; }
        if(fr==0){ g_wobj[idx].tile++;               /* close it */
            int n=msgcat(0,"You close the "); n=msgcat(n,nm); msgcat(n,".");
            build_blocked(); return 1; }
    }
    if(container_searchable(idx)){                   /* bag/basket/drawer/desk/body...: search directly */
        int n=msgcat(0,"Searching here, you find ");
        int found=spill_container(idx,&n); if(!found) n=msgcat(n,"nothing");
        msgcat(n,".");
        return found?1:0;
    }
    if(is_actuator_objn(o)){                         /* lever/switch/crank/pull-chain: drive linked targets */
        int q=g_wobj[idx].quality, ax=g_wobj[idx].x, ay=g_wobj[idx].y, toggled=0;
        int afr=(int)g_wobj[idx].tile - basetile(o); /* flip the actuator itself (lever up<->down); not
                                                      * persisted, so it reverts in step with its (also
                                                      * non-persisted) field on reload — stays consistent */
        if(afr==0) g_wobj[idx].tile++; else if(afr==1) g_wobj[idx].tile--;
        if(q!=0){                                    /* QUALITY-LINK: toggle every matching-quality field on/off */
            for(int j=0;j<g_ow.nobjs;j++)
                if(g_wobj[j].quality==q && is_field_objn(g_wobj[j].obj_n)){ g_wobj[j].alive^=1; toggled++; }
        } else {                                     /* POSITIONAL: raise/lower the nearest portcullis (<=5 tiles) */
            int best=-1,bd=99;
            for(int j=0;j<g_ow.nobjs;j++){ if(!g_wobj[j].alive || g_wobj[j].obj_n!=310) continue;
                int dx=g_wobj[j].x-ax,dy=g_wobj[j].y-ay; if(dx<0)dx=-dx; if(dy<0)dy=-dy;
                int d=dx>dy?dx:dy; if(d<bd){ bd=d; best=j; } }
            if(best>=0 && bd<=5){ int fr=(int)g_wobj[best].tile - basetile(310);
                g_wobj[best].tile=(uint16_t)(basetile(310)+(fr==0?1:0)); /* open(->0) if closed, else close(->1) */
                g_wobj[best].dirty=1; toggled++; }
            /* DRAWBRIDGE (obj 269): a nearby span raises/lowers — frames 6-8 (raised, impassable) <->
             * 9-11 (down, passable), the +/-3 pair derived from the tiles' tileflag1 passability. */
            for(int j=0;j<g_ow.nobjs;j++){ if(!g_wobj[j].alive || g_wobj[j].obj_n!=269) continue;
                int dx=g_wobj[j].x-ax,dy=g_wobj[j].y-ay; if(dx<0)dx=-dx; if(dy<0)dy=-dy;
                if((dx>dy?dx:dy)>8) continue;             /* the whole contiguous span is within reach */
                int fr=(int)g_wobj[j].tile - basetile(269);
                if(fr>=6&&fr<=8){ g_wobj[j].tile+=3; g_wobj[j].dirty=1; toggled++; }        /* lower (walkable) */
                else if(fr>=9&&fr<=11){ g_wobj[j].tile-=3; g_wobj[j].dirty=1; toggled++; }  /* raise (blocked) */
            }
        }
        build_blocked();                             /* field/gate passability changed */
        { int n=msgcat(0,"You pull the "); n=msgcat(n,nm);
          msgcat(n, toggled?". You hear machinery move.":"."); }
        return 1;
    }
    { int fk=is_food_objn(o);                        /* eat/drink: consume one + restore food (hunger model) */
      if(fk){ int n=msgcat(0, fk==2?"You drink the ":"You eat the "); n=msgcat(n,nm); msgcat(n,".");
          if(g_wobj[idx].qty>1) g_wobj[idx].qty--; else g_wobj[idx].alive=0;   /* consumed; not persisted (reverts on reload) */
          int f=g_afood[1]+(fk==2?15:40); g_afood[1]=(uint8_t)(f>FOOD_MAX?FOOD_MAX:f);  /* feed the Avatar */
          return 1; } }
    int n=msgcat(0,"Nothing happens with the "); n=msgcat(n,nm); msgcat(n,".");
    return 0;
}

/* U)se an object; if its tile (frame) changed (a door/container opened/closed/unlocked), mark it
 * DIRTY so do_save re-serialises the new frame into the save (in-place, nuvie_save.md). */
static int use_object(int idx)
{
    uint16_t t0 = g_wobj[idx].tile;
    int r = use_object_inner(idx);
    if(g_wobj[idx].tile != t0) g_wobj[idx].dirty = 1;
    return r;
}

/* run a verb ('L'/'G'/'U') against the tile (dx,dy) from the avatar. */
/* --- readable text: books / signs / plaques / tombstones (docs/formats/strings.md) --------- */
/* readable object numbers: BOOK 151, SCROLL 152, TOMBSTONE 255, GRAVE 266, SIGN 332, SIGN_ARROW 333. */
static const int READABLE_OBJN[]={151,152,255,266,332,333};
static int is_readable_objn(int on){
    for(unsigned i=0;i<sizeof READABLE_OBJN/sizeof*READABLE_OBJN;i++) if(READABLE_OBJN[i]==on) return 1;
    return 0;
}

/* Food/drink obj_n, DERIVED from the shipped LOOK name table (docs/rules/usecode_use.md §Food):
 * 1 = eat, 2 = drink. Excludes reagents (garlic/nightshade), potions, and containers (jars/buckets).
 * U6's hunger model isn't implemented, so USE is the authentic "consume one + message" (no HP change).
 * Probable (name-derived); not cross-checked against Nuvie's use_food registration (no source on hand). */
static int is_food_objn(int on){
    switch(on){
        case 95: case 96: case 128: case 129: case 130: case 131: case 132:  /* grapes,butter,bread,meat-portion,rolls,cake,cheese */
        case 133: case 209: case 210: case 265: case 335: return 1;          /* ham,meat,ribs,fish,egg */
        case 115: case 116: case 117: return 2;                              /* wine,mead,ale (drink) */
        default: return 0;
    }
}

/* Modal paged view of readable text, word-wrapped over the papyrus page. Any key pages forward;
 * Esc closes. `txt` is `len` bytes (NOT NUL-terminated); '\n' forces a line break. */
static void book_view(const char*txt,int len)
{
    enum { CX=16, CY=18, LNH=10, COLS=36, MAXLINES=96 };
    static char ln[MAXLINES][COLS+2]; int nl=0,c=0; ln[0][0]=0;
    for(int i=0;i<len && nl<MAXLINES;){
        if(txt[i]=='\n'){ ln[nl][c]=0; if(++nl<MAXLINES) ln[nl][0]=0; c=0; i++; continue; }
        while(i<len && txt[i]==' ') i++;                          /* collapse leading spaces */
        int ws=i; while(i<len && txt[i]!=' ' && txt[i]!='\n') i++;
        int wl=i-ws; if(wl>COLS) wl=COLS;                         /* clamp an over-long word */
        if(c>0 && c+1+wl>COLS){ ln[nl][c]=0; if(++nl>=MAXLINES) break; ln[nl][0]=0; c=0; }
        if(c>0) ln[nl][c++]=' ';
        for(int k=0;k<wl && c<COLS;k++) ln[nl][c++]=txt[ws+k];
        ln[nl][c]=0;
    }
    if(c>0) nl++;
    if(nl==0) nl=1;
    int per=(LH-CY-14)/LNH; if(per<1) per=1;                       /* text lines per page */
    { int gk,gd=0; while((gk=plat_input_raw())&&++gd<64){(void)gk;} }  /* flush stale keys */
    for(int pg=0; pg*per<nl; pg++){
        blit_paper();                                             /* the papyrus page */
        for(int r=0;r<per && pg*per+r<nl;r++) ui_text(CX, CY+r*LNH, ln[pg*per+r], 0);  /* dark ink */
        const char*ft=((pg+1)*per<nl)?"(space to turn the page)":"(done)";
        ui_text((LW-ui_textw(ft))/2, LH-12, ft, 0x3e);
        rdr_present_doubled(LH);
        int k; while(!(k=plat_input_raw())) Vsync();
        if((k&0xFF)==0x1b) break;                                 /* Esc closes early */
    }
}

/* read a readable world object: entry = quality-1 (quality 0 => entry 126, strings.md). */
static void read_object(int idx)
{
    int q=g_wobj[idx].quality, e = q>0 ? q-1 : 126;
    if(e>=0 && e<g_nbook && g_book[e] && g_booklen[e]>0){ book_view(g_book[e], g_booklen[e]); msgcat(0,"Thou readest it."); }
    else msgcat(0,"Nothing is written here.");
}

/* --- map-overview render (Peer / Wizard Eye, magic_skills.md §5). Draws an 88x88-tile region
 * centred on (cx,cy) into the 176x176 map window at 2 px/tile: each tile = its terrain tile's
 * centre pixel; actors are dots (party bright, others grey); the view centre is a yellow cross. */
static void overview_render(const world_t*w,int cx,int cy)
{
    enum { R=44 };                                     /* 88 tiles across * 2 px = 176 = MAPSZ */
    for(int oy=0;oy<88;oy++){
        uint8_t*r0=ROW(MAPY+oy*2)+MAPX, *r1=ROW(MAPY+oy*2+1)+MAPX;
        for(int ox=0;ox<88;ox++){
            int wx=(cx-R+ox)&(U6_SURFACE_TILES-1), wy=(cy-R+oy)&(U6_SURFACE_TILES-1);
            int t=world_tile(w,wx,wy); uint8_t col=g_tileset[(int32_t)t*256+8*16+8];
            if(col==TILE_TRANSPARENT) col=0;
            r0[ox*2]=col; r0[ox*2+1]=col; r1[ox*2]=col; r1[ox*2+1]=col;
        }
    }
    for(int i=1;i<U6_MAX_ACTORS;i++){ int ax,ay; if(g_ainvis[i]) continue; if(!actor_pos(i,&ax,&ay)) continue;
        int ddx=((ax-cx+512)&(U6_SURFACE_TILES-1))-512, ddy=((ay-cy+512)&(U6_SURFACE_TILES-1))-512;
        if(ddx<-R||ddx>=R||ddy<-R||ddy>=R) continue;
        uint8_t col=(i==1||is_party_member(i))?15:8;   /* party white, others grey */
        ROW(MAPY+(ddy+R)*2)[MAPX+(ddx+R)*2]=col; }
    int mx=MAPX+R*2, my=MAPY+R*2;                        /* view centre = a yellow cross */
    ROW(my)[mx-1]=14; ROW(my)[mx]=14; ROW(my)[mx+1]=14; ROW(my-1)[mx]=14; ROW(my+1)[mx]=14;
}
/* Modal overview. pannable=0 (Peer): any key closes. pannable=1 (Wizard Eye): arrows scout, Esc closes. */
static void overview_view(const world_t*w,int cx,int cy,int pannable)
{
    { int gk,gd=0; while((gk=plat_input_raw())&&++gd<64){(void)gk;} }   /* flush stale keys */
    for(;;){
        overview_render(w,cx,cy);
        const char*cap=pannable?"Wizard Eye - arrows scout, Esc ends":"Peer - press a key";
        ui_text(MAPX+(MAPSZ-ui_textw(cap))/2, MAPY+MAPSZ-9, cap, 14);
        rdr_present_doubled(LH);
        int k; while(!(k=plat_input_raw())) Vsync();
        int ch=k&0xFF, sc=(k>>8)&0xFF;
        if(!pannable || ch==0x1b || ch==13) break;
        if(sc==0x48) cy=(cy-2)&(U6_SURFACE_TILES-1); else if(sc==0x50) cy=(cy+2)&(U6_SURFACE_TILES-1);
        else if(sc==0x4b) cx=(cx-2)&(U6_SURFACE_TILES-1); else if(sc==0x4d) cx=(cx+2)&(U6_SURFACE_TILES-1);
    }
}

static int do_verb(world_t*w,int verb,int dx,int dy)
{
    int tx=(int)((w->px+dx)&(U6_SURFACE_TILES-1));
    int ty=(int)((w->py+dy)&(U6_SURFACE_TILES-1));
    int idx=objworld_at(&g_ow,tx,ty);
    if(idx>=0 && (verb=='L'||verb=='U') && is_readable_objn(g_wobj[idx].obj_n)){
        read_object(idx); return 1;                              /* L)ook / U)se a readable -> show its text */
    }
    char nm[40];
    int recompose=0, n=0;
    if(verb=='L'){
        if(idx>=0) obj_name(g_wobj[idx].tile,nm,sizeof nm);
        else       obj_name(world_tile(w,tx,ty),nm,sizeof nm);   /* terrain name */
        n=msgcat(0,"You see "); n=msgcat(n,nm[0]?nm:"nothing");
        if(idx>=0 && container_searchable(idx)){                 /* U6: looking SEARCHES a container
                                                                  * (chest/crate/barrel only when OPEN) */
            n=msgcat(n,". Searching here, you find ");
            if(!spill_container(idx,&n)) n=msgcat(n,"nothing");
            recompose=1;                                          /* spilled items appear on the map */
        }
        msgcat(n,".");
    } else if(verb=='G'){
        if(idx<0){ msgcat(0,"Nothing to get there."); }
        else {
            obj_name(g_wobj[idx].tile,nm,sizeof nm);
            uint16_t q=g_wobj[idx].qty;
            if(objworld_get(&g_ow,idx)){
                n=msgcat(0,"You get ");
                if(q>1){ n=msgcat_int(n,q); n=msgcat(n," "); }    /* "You get 30 gold." */
                else n=msgcat(n,"the ");
                n=msgcat(n,nm); msgcat(n,"."); recompose=1;
            }
            else { n=msgcat(0,"You cannot move the "); n=msgcat(n,nm); msgcat(n,"."); }
        }
    } else { /* 'U' use — dispatch by object number (doors open/close, ...) */
        if(idx<0){ msgcat(0,"Nothing to use there."); }
        else recompose = use_object(idx);
    }
    return recompose;
}

/* inventory list into the panel scroll */
static void draw_inventory(uint8_t col)
{
    ui_text(PANX+(PANW-ui_textw("Inventory"))/2, 16, "Inventory", col);
    if(g_ow.ninv==0) ui_text(PANX+8, 32, "(nothing)", col);
    else for(int i=0;i<g_ow.ninv && i<15;i++){
        char line[24]; int t=0; char nm[20]; obj_name(g_ow.inv[i].tile,nm,sizeof nm);
        if(g_ow.inv[i].qty>1){ int q=g_ow.inv[i].qty; if(q>99)q=99;
            line[t++]=(char)('0'+q/10); line[t++]=(char)('0'+q%10); line[t++]=' '; }
        for(int k=0;nm[k]&&t<23;k++) line[t++]=nm[k];
        line[t]=0;
        ui_text(PANX+6, 32+i*10, line, col);
    }
    /* GOLD — the shared conversation-VM pool (merchants / trainers add + subtract it) */
    { char g[24]; int t=0; const char*lb="Gold: "; for(;*lb;lb++) g[t++]=*lb;
      int v=g_cg.gold; char tmp[8]; int nt=0; if(v<0)v=0;
      do{ tmp[nt++]=(char)('0'+v%10); v/=10; }while(v&&nt<7);
      while(nt) g[t++]=tmp[--nt]; g[t]=0;
      ui_text(PANX+6, 182, g, 14); }
}

/* U6's sun/moon strip (SunMoonStripWidget): 8 sky-ribbon tiles 352..359, with the
 * sun (361/362) and a moon (584) drawn at their arc positions by the game clock. */
static void draw_sky(void)
{
    static const signed char sy[15]={6,3,1,-1,-2,-3,-4,-4,-4,-3,-2,-1,1,3,6};
    int ax=PANX, ay=4, hh=g_minutes/60;
    for(int i=0;i<8;i++) blit_tile(ax+8+i*16, ay, 352+i, 1);     /* sky ribbon */
    if(hh>=5 && hh<=19){                                          /* sun: pos = hour-5 */
        int p=hh-5, st=(hh==5||hh==19)?361:362;
        blit_tile(ax+8+7*16-p*8, ay+sy[p], st, 1);
    }
    int mp=(hh+12)%24;                                            /* a moon, ~opposite */
    if(mp>=5 && mp<=19){ int p=mp-5; blit_tile(ax+8+7*16-p*8, ay+sy[p], 584, 1); }
}

/* the party: each member's sprite icon + name + right-aligned HP (top-right,
 * where U6 shows the roster). */
static void draw_party_roster(uint8_t col)
{
    for(int i=0;i<g_nparty && i<5;i++){
        int yy=26+i*16, a=g_party[i].actor;          /* below the sky strip */
        if(a>=0 && a<U6_MAX_ACTORS && g_atile[a]!=0xffff) blit_tile(PANX+2, yy, g_atile[a], 1);
        ui_text(PANX+20, yy+4, g_party[i].name[0]?g_party[i].name:"?", col);
        char hp[6]; int n=0, v=g_party[i].hp;
        if(v>=100){ hp[n++]=(char)('0'+v/100); v%=100; hp[n++]=(char)('0'+v/10); }
        else if(v>=10){ hp[n++]=(char)('0'+v/10); }
        hp[n++]=(char)('0'+v%10); hp[n]=0;
        ui_text(PANX+PANW-ui_textw(hp)-4, yy+4, hp, col);
    }
}

/* ---- character status view (right panel) -------------------------------- */

/* small unsigned int -> decimal string; returns the end pointer (for chaining "cur/max"). */
static char *u_num(char *b, int v){ char t[8]; int n=0; if(v<0) v=0;
    do{ t[n++]=(char)('0'+v%10); v/=10; }while(v && n<7);
    int o=0; while(n) b[o++]=t[--n]; b[o]=0; return b+o; }

/* one status row: "label" left, "val" right-aligned in the panel. */
static void stat_row(int y, const char*label, const char*val, uint8_t col)
{   ui_text(PANX+12, y, label, col);
    ui_text(PANX+PANW-ui_textw(val)-10, y, val, col); }

/* decode actor id's 56x64 portrait (PORTRAIT.A for id-1<98, else .B) into buf. 1 on success. */
static int portrait_for_actor(int id, uint8_t *buf)
{   int pidx=id-1; if(pidx<0) return 0;
    const uint8_t *pf; int32_t pl; int pi;
    if(pidx<98){ pf=g_portA; pl=g_portAlen; pi=pidx; }
    else       { pf=g_portB; pl=g_portBlen; pi=pidx-98; }
    if(!pf) return 0;
    int32_t pe; const uint8_t *pp=lib_entry(pf,pl,pi,&pe);
    return (pp && u6lzw_decompress(pp,pe,buf,PW*PH)==PW*PH); }

/* CHARACTER STATUS VIEW: the viewed party member's portrait + name + (the Avatar's created)
 * gender + Str/Dex/Int + HP/MP + Lev/Exp — where the chargen gender/portrait finally show
 * in-game. Opened by clicking a roster member; Esc or another panel click returns. */
static uint8_t g_statface[PW*PH];
static void draw_char_view(uint8_t col)
{
    int a=g_status_actor; if(a<1||a>=U6_MAX_ACTORS) a=1;
    int pxx=PANX+(PANW-PW)/2, pyy=MAPY+8;
    const uint8_t *face=0;                             /* Avatar: the created montage face; else PORTRAIT.A/.B */
    if(a==1 && g_cc_nport>=12 && g_cc_pw==PW && g_cc_ph==PH){
        int idx=g_cc_gender*6+g_cc_portrait; if(idx<0)idx=0; if(idx>=g_cc_nport)idx=g_cc_nport-1;
        face=g_cc_port[idx];
    } else if(portrait_for_actor(a,g_statface)) face=g_statface;
    if(face) draw_portrait_buf(face,pxx,pyy);          /* frames + blits the 56x64 portrait */
    else if(g_atile[a]!=0xffff) blit_tile(pxx+PW/2-8,pyy+PH/2-8,g_atile[a],0);  /* fallback: the map sprite */

    const char *nm=0; for(int p=0;p<g_nparty;p++) if(g_party[p].actor==a){ nm=g_party[p].name; break; }
    if(!nm||!nm[0]) nm=(a==1)?g_avatar_name:"?";
    ui_text(PANX+(PANW-ui_textw(nm))/2, pyy+PH+3, nm, 14);   /* name in bright yellow */

    int ty=pyy+PH+15;
    if(a==1){ const char*g=g_cc_gender?"Male":"Female";      /* only the Avatar's gender is modeled */
        ui_text(PANX+(PANW-ui_textw(g))/2, ty, g, col); ty+=13; }

    char b[16], *e;
    u_num(b,g_astr[a]); stat_row(ty,"Str",b,col); ty+=11;
    u_num(b,g_adex[a]); stat_row(ty,"Dex",b,col); ty+=11;
    u_num(b,g_aint[a]); stat_row(ty,"Int",b,col); ty+=13;
    e=u_num(b,g_ahp[a]);   *e++='/'; u_num(e,g_amaxhp[a]);   stat_row(ty,"HP",b,col); ty+=11;
    e=u_num(b,g_amana[a]); *e++='/'; u_num(e,g_amaxmana[a]); stat_row(ty,"MP",b,col); ty+=13;
    { const char*fs=g_afood[a]==0?"Starving":g_afood[a]<=FOOD_HUNGRY?"Hungry":g_afood[a]>=150?"Full":"Fed";
      stat_row(ty,"Food",fs,col); ty+=13; }                  /* hunger model */
    u_num(b,g_alevel[a]>0?g_alevel[a]:1); stat_row(ty,"Lev",b,col); ty+=11;
    u_num(b,g_aexp[a]); stat_row(ty,"Exp",b,col); ty+=11;
    if(a==1){ u_num(b,g_cg.karma); stat_row(ty,"Kar",b,col); }  /* Avatar alignment (ADDKARMA/SUBKARMA) */
}

/* ---- command-icon bar + mouse cursor ------------------------------------ */

static int verb_cmd(int v){ switch(v){case 'A':return 0;case 'C':return 1;case 'T':return 2;case 'L':return 3;
    case 'G':return 4;case 'M':return 6;case 'U':return 7;} return -1; }
static int cmd_verb(int c){ switch(c){case 0:return 'A';case 1:return 'C';case 2:return 'T';case 3:return 'L';
    case 4:return 'G';case 6:return 'M';case 7:return 'U';} return 0; }

static void restore_cmdbar(void)
{ for(int y=CMDY-1;y<CMDY+17 && y<LH;y++)
    memcpy(ROW(y)+(CMDX-1), g_paper+(int32_t)y*LW+(CMDX-1), NCMD*16+2); }

/* the 10 command icons (tiles 400..409) + a box around the armed verb */
static void draw_command_bar(uint8_t hi)
{
    restore_cmdbar();
    for(int i=0;i<NCMD;i++) blit_tile(CMDX+i*16,CMDY,400+i,0);
    int sel=verb_cmd(g_pending_verb);
    if(sel>=0) ui_frame(CMDX+sel*16-1,CMDY-1,18,18,hi);
}

/* mouse cursor (save-under so a move repaints cleanly) */
static void cursor_erase(void)
{
    if(g_csx<0) return;
    for(int y=0;y<g_ch;y++)for(int x=0;x<g_cw;x++) ROW(g_csy+y)[g_csx+x]=g_cursave[y*g_cw+x];
    g_csx=-1;
}
static void cursor_draw(int lx,int ly)
{
    int ox=lx-g_chx, oy=ly-g_chy;
    if(ox<0)ox=0;
    if(oy<0)oy=0;
    if(ox+g_cw>LW)ox=LW-g_cw;
    if(oy+g_ch>LH)oy=LH-g_ch;
    g_csx=ox; g_csy=oy;
    for(int y=0;y<g_ch;y++)for(int x=0;x<g_cw;x++){
        g_cursave[y*g_cw+x]=ROW(oy+y)[ox+x];
        uint8_t p=g_curpix[y*g_cw+x];
        if(p!=TILE_TRANSPARENT) ROW(oy+y)[ox+x]=p;
    }
}
static int g_cur_onx=-1, g_cur_ony=-1;   /* where the cursor is currently shown ON SCREEN */

/* present a frame with the cursor on top, leaving the backbuffer cursor-free so
 * the next compose never has to reason about it. (Full present clears the old
 * cursor position automatically.) */
static void present_frame(void)
{ if(g_cw){ cursor_draw(g_mx,g_my); g_cur_onx=g_csx; g_cur_ony=g_csy; rdr_present_doubled(LH); cursor_erase(); }
  else rdr_present_doubled(LH); }

/* present ONLY the map window (movement / tile animation) — ~half the c2p of a full
 * doubled present, since the panel/command-bar are static. The cursor is composited
 * if it overlaps the map. The main loop Vsyncs. */
static void present_map(void)
{ if(g_cw){ cursor_draw(g_mx,g_my); g_cur_onx=g_csx; g_cur_ony=g_csy; rdr_present_rect_doubled(MAPX,MAPY,MAPSZ,MAPSZ,LH); cursor_erase(); }
  else rdr_present_rect_doubled(MAPX,MAPY,MAPSZ,MAPSZ,LH); }

/* Cheap CURSOR-ONLY present for a bare mouse move: repaint just the old + new cursor
 * boxes (16-px-aligned) instead of the whole screen. A full present is a big c2p that on
 * the Falcon dwarfs a vblank, so doing it every mouse-move stalled colour cycling and
 * tile animation (they only advance once per loop iteration). The main loop Vsyncs. */
static void present_cursor(void)
{
    if(!g_cw) return;
    int oldx=g_cur_onx, oldy=g_cur_ony;
    cursor_draw(g_mx,g_my);                                          /* draw at the new pos */
    int nx=g_csx, ny=g_csy;
    if(oldx>=0){ int x0=oldx&~15, w=((oldx+g_cw+15)&~15)-x0;         /* clean bb -> erases old cursor */
                 rdr_present_rect_doubled(x0,oldy,w,g_ch,LH); }
    { int x0=nx&~15, w=((nx+g_cw+15)&~15)-x0;                        /* new box -> shows cursor */
      rdr_present_rect_doubled(x0,ny,w,g_ch,LH); }
    cursor_erase();
    g_cur_onx=nx; g_cur_ony=ny;
}

/* DEBUG: dump the chunky 320x200 backbuffer + its palette to a GEMDOS file, so a
 * host tool (tools/shot2png.py) can render a PIXEL-PERFECT PNG with NO emulator
 * scaling/aspect/borders in the imaging path — image pixel == logical pixel. The
 * definitive rig for tile-alignment work (vs Hatari captures which scale 2x +
 * overscan). Triggered only when SHOT.REQ is staged on C:\ (absent in normal play).
 * Format (big-endian, written by the 68000): magic 'U6SH', u16 w, u16 h, then
 * 256*3 bytes RGB6 palette, then w*h index bytes (row-major, top-down). */
static void dump_shot(void)
{
    int32_t rl=0; void *req=plat_load_file("SHOT.REQ",1,&rl);
    if(!req) return;                                   /* sentinel absent: no-op */
    plat_file f=plat_fcreate("SHOT.DAT");
    if(f==PLAT_BADFILE) return;
    const int W=320, H=200;
    uint8_t hdr[8]={ 'U','6','S','H', (uint8_t)(W>>8),(uint8_t)W, (uint8_t)(H>>8),(uint8_t)H };
    plat_fwrite(f,hdr,8);
    plat_fwrite(f,(void*)(g_basepal?g_basepal:g_curpal),768);   /* RGB6 palette */
    for(int y=0;y<H;y++) plat_fwrite(f, ROW(y), W);             /* 320 bytes/row */
    plat_fclose(f);
    Cconws("SHOT.DAT written (320x200 indexed + palette)\r\n");
}

/* Colour cycling: rotate the CLUT ranges by `g_cyc_phase` and re-upload the palette.
 * No recompose/present — the shifter shows the new colours immediately. Cheap. */
static void cycle_palette(void)
{
    if(!g_basepal) return;
    memcpy(g_curpal, g_basepal, 768);
    for(unsigned r=0;r<sizeof CYCLE/sizeof*CYCLE;r++){
        int lo=CYCLE[r].lo, len=CYCLE[r].len;
        for(int k=0;k<len;k++){
            int src=lo + ((k + g_cyc_phase) % len);
            g_curpal[(lo+k)*3+0]=g_basepal[src*3+0];
            g_curpal[(lo+k)*3+1]=g_basepal[src*3+1];
            g_curpal[(lo+k)*3+2]=g_basepal[src*3+2];
        }
    }
    rdr_set_palette(g_curpal, 256);

    /* ST SHIFTER: rdr_set_palette above is a no-op on 16-colour hardware, so animate the reserved
     * cycle slots (U6WZ) directly — rotate each range's registers by the same phase and push just
     * the 16 to the backend (it updates band 0 = the map). Guard on !g_talking: a conversation
     * palette is active then and has NO reserved slots. (docs/design/palette_lowcolor.md) */
    if(g_have_lowcolor && g_lc_ncyc && !g_talking){
        const uint8_t *basep = LCPAL(g_lc_pal, g_lc_pal4);   /* rotate the STE 4-bit game 16 when on STE */
        uint8_t cur[48]; memcpy(cur, basep, 48);
        for(int r=0;r<g_lc_ncyc;r++){
            int first=g_lc_cyc[r][0], nsl=g_lc_cyc[r][1];
            if(nsl<2 || first+nsl>16) continue;
            for(int k=0;k<nsl;k++){
                int src=first + ((k + g_cyc_phase) % nsl);
                cur[(first+k)*3+0]=basep[src*3+0];
                cur[(first+k)*3+1]=basep[src*3+1];
                cur[(first+k)*3+2]=basep[src*3+2];
            }
        }
        rdr_lowcolor_cycle(cur);
    }
    g_cyc_phase++;
}

/* exact-tile NPC lookup at the game hour (for Talk targeting) */
static int npc_at(const world_t*w,int x,int y)
{
    for(int i=0;i<U6_MAX_ACTORS;i++){
        if(i==1) continue;                     /* the Avatar */
        int ax,ay; if(!actor_pos(i,&ax,&ay)) continue;
        if(ax==x && ay==y) return i;
    }
    return -1;
}

static rng_t g_crng;                                  /* combat RNG (seeded at init) */

/* sync the roster's displayed HP from the live g_ahp[] after combat. */
static void sync_party_hp(void){
    for(int i=0;i<g_nparty;i++){ int a=g_party[i].actor;
        if(a>=0 && a<U6_MAX_ACTORS) g_party[i].hp=g_ahp[a]; }
}

/* nearest living party member (incl. the Avatar, actor 1) to (x,y); fills target
 * position + actor id. Chebyshev distance. Returns 0 if none. (combat.md CB2) */
static int nearest_party(const world_t*w,int x,int y,int*tx,int*ty,int*tid){
    int bd=1<<30,bx=0,by=0,bid=-1;
    for(int p=0;p<g_nparty;p++){ int a=g_party[p].actor, ax,ay;
        if(a==1){ ax=(int)w->px; ay=(int)w->py; }
        else { if(!actor_pos(a,&ax,&ay)) continue; if(g_ahp[a]==0) continue; }
        int cx=ax>x?ax-x:x-ax, cy=ay>y?ay-y:y-ay, d=cx>cy?cx:cy;
        if(d<bd){ bd=d; bx=ax; by=ay; bid=a; }
    }
    if(bid<0) return 0; *tx=bx; *ty=by; *tid=bid; return 1;
}

/* is there a sight-blocking OBJECT (closed door, etc.) on this tile? (g_sightblk, built in
 * build_blocked) — complements the base-map sight-blocker test (blocks_sight). */
static int sightblk_at(int x,int y){
    int32_t idx=(int32_t)(y&(U6_SURFACE_TILES-1))*1024+(x&(U6_SURFACE_TILES-1));
    return (g_sightblk[idx>>3]>>(idx&7))&1;
}
/* line-of-sight: sample the cells along (x0,y0)->(x1,y1); blocked if any INTERMEDIATE cell is
 * a sight-blocker — a base-map boundary (blocks_sight) OR a sight-blocking object on it
 * (sightblk_at). Endpoints excluded. (aggro-on-sight) */
static int has_los(const world_t*w,int x0,int y0,int x1,int y1){
    int dx=x1-x0, dy=y1-y0, adx=dx<0?-dx:dx, ady=dy<0?-dy:dy;
    int n=adx>ady?adx:ady; if(n<=1) return 1;
    for(int k=1;k<n;k++){
        int x=(x0 + dx*k/n)&(U6_SURFACE_TILES-1), y=(y0 + dy*k/n)&(U6_SURFACE_TILES-1);
        if(blocks_sight(world_tile(w,x,y)) || sightblk_at(x,y)) return 0;
    }
    return 1;
}

/* aggro-on-sight perception: does hostile actor i PERCEIVE any (living) party member —
 * within AGGRO_SEE tiles AND with line-of-sight? (combat.md §7) */
#define AGGRO_SEE 5
static int sees_party(const world_t*w,int i){
    int x=g_actx[i], y=g_acty[i];
    for(int p=0;p<g_nparty;p++){ int a=g_party[p].actor, ax,ay;
        if(a==1){ ax=(int)w->px; ay=(int)w->py; }
        else { if(!actor_pos(a,&ax,&ay)) continue; if(g_ahp[a]==0) continue; }
        int cx=ax>x?ax-x:x-ax, cy=ay>y?ay-y:y-ay, d=cx>cy?cx:cy;
        if(d<=AGGRO_SEE && has_los(w,x,y,ax,ay)) return 1;
    }
    return 0;
}

/* a hostile actor strikes a party member (combat.md §2-3). Monster melee damage is
 * derived from strength for now (max(2,str/2)); CB3 will ship the real monster table. */
static void enemy_attack(int att,int def){
    char an[24]; obj_name(npc_tile(att),an,sizeof an);
    if(!an[0]){ an[0]='f'; an[1]='o'; an[2]='e'; an[3]=0; }
    int adex=g_adex[att]?g_adex[att]:10, ddex=g_adex[def]?g_adex[def]:15;
    if(!combat_hit(status_hit_role(att,adex),ddex,&g_crng)){ int n=msgcat(0,an); msgcat(n," misses."); return; }  /* curse penalty (§5b) */
    int wm=g_aweapon[att]?weapon_dmg(g_aweapon[att])         /* armed actor uses its readied weapon */
                         :(g_astr[att]?(g_astr[att]/2>2?g_astr[att]/2:2):4);  /* else monster melee */
    int dmg=status_dmg(def,combat_damage(wm,g_aac[def],&g_crng));  /* vs armour, then Protection halves it (§5b) */
    if(dmg<=0){ int n=msgcat(0,an); msgcat(n," grazes."); return; }
    int hp=g_ahp[def]; g_ahp[def]=(dmg>=hp)?0:(uint8_t)(hp-dmg);
    char dn[20]; dn[0]=0;
    if(def==1){ const char*t="thee"; int o=0; while(t[o]){dn[o]=t[o];o++;} dn[o]=0; }
    else { for(int p=0;p<g_nparty;p++) if(g_party[p].actor==def){
              int o=0; while(g_party[p].name[o]&&o<19){dn[o]=g_party[p].name[o];o++;} dn[o]=0; break; } }
    if(!dn[0]){ dn[0]='a'; dn[1]='l'; dn[2]='l'; dn[3]='y'; dn[4]=0; }
    int n=msgcat(0,an);
    if(g_ahp[def]==0){ n=msgcat(n," kills "); n=msgcat(n,dn); msgcat(n,"!"); }
    else            { n=msgcat(n," hits ");  n=msgcat(n,dn); msgcat(n,"!"); }
    sync_party_hp();
}

/* one turn for a hostile actor: attack the nearest party member if adjacent, else path
 * one step toward it (around walls). Called from step_npcs. (combat.md CB2) */
static void hostile_step(const world_t*w,int i){
    int x=g_actx[i], y=g_acty[i], avx=(int)w->px, avy=(int)w->py, tx,ty,tid;
    if(!nearest_party(w,x,y,&tx,&ty,&tid)){ g_amoved[i]=0; return; }
    int cx=tx>x?tx-x:x-tx, cy=ty>y?ty-y:y-ty;
    if(!g_aflee[i] && cx<=1 && cy<=1){                 /* adjacent + not fleeing -> attack */
        g_facing[i]= ty<y?0 : ty>y?2 : tx>x?1 : 3;
        enemy_attack(i,tid); g_amoved[i]=0; return;
    }
    int gx=tx, gy=ty;                                  /* assault: head toward the party */
    if(g_aflee[i]){                                    /* CB4: flee -> a goal AWAY from the party */
        int ax = x>tx?1 : x<tx?-1 : 1, ay = y>ty?1 : y<ty?-1 : 0;
        gx=(x + ax*6)&(U6_SURFACE_TILES-1); gy=(y + ay*6)&(U6_SURFACE_TILES-1);
    }
    int mx,my;                                         /* step toward gx,gy (toward party / away if fleeing) */
    if(npc_pathstep(w,x,y,gx,gy,avx,avy,&mx,&my)){
        int nx=(x+mx)&(U6_SURFACE_TILES-1), ny=(y+my)&(U6_SURFACE_TILES-1);
        if(npc_occupied(i,nx,ny)){ g_amoved[i]=0; return; }
        g_actx[i]=(uint16_t)nx; g_acty[i]=(uint16_t)ny;
        g_facing[i]= my<0?0 : my>0?2 : mx>0?1 : 3; g_walkf[i]^=1; g_amoved[i]=1;
    } else g_amoved[i]=0;
}

/* coward (worktype COMBAT_RETREAT, U6WN): flees the party ON SIGHT and switches to ASSAULT
 * only when struck (do_attack clears g_coward + sets g_hostile). Returns 1 if it handled its
 * turn (fled / cowered cornered), 0 if the party is out of sight — caller then walks its
 * normal schedule. (combat.md §7: "FLEE actors that get hit switch to ASSAULT".) */
#define COWARD_SEE 5
static int coward_step(const world_t*w,int i){
    int x=g_actx[i], y=g_acty[i], avx=(int)w->px, avy=(int)w->py, tx,ty,tid;
    if(!nearest_party(w,x,y,&tx,&ty,&tid)) return 0;
    int cx=tx>x?tx-x:x-tx, cy=ty>y?ty-y:y-ty, d=cx>cy?cx:cy;
    if(d>COWARD_SEE) return 0;                                   /* party not in sight: behave normally */
    int ax = x>tx?1 : x<tx?-1 : 1, ay = y>ty?1 : y<ty?-1 : 0;   /* head AWAY from the nearest member */
    int gx=(x+ax*6)&(U6_SURFACE_TILES-1), gy=(y+ay*6)&(U6_SURFACE_TILES-1);
    int mx,my;
    if(npc_pathstep(w,x,y,gx,gy,avx,avy,&mx,&my)){
        int nx=(x+mx)&(U6_SURFACE_TILES-1), ny=(y+my)&(U6_SURFACE_TILES-1);
        if(!npc_occupied(i,nx,ny)){ g_actx[i]=(uint16_t)nx; g_acty[i]=(uint16_t)ny;
            g_facing[i]= my<0?0:my>0?2:mx>0?1:3; g_walkf[i]^=1; g_amoved[i]=1; return 1; } }
    g_amoved[i]=0; return 1;                                     /* cornered: cower in place */
}

/* tiny VT-52 console debug print "<label><n>\r\n" (visible via Hatari --conout 2). */
static void dbg_num(const char*label,int v){
    Cconws(label); char b[10]; int i=0; if(v<0) v=0;
    if(!v) b[i++]='0'; else { char t[10]; int j=0; while(v&&j<9){ t[j++]=(char)('0'+v%10); v/=10; } while(j) b[i++]=t[--j]; }
    b[i++]='\r'; b[i++]='\n'; b[i]=0; Cconws(b);
}

/* append a decimal integer to g_msg (msgcat is string-only) */
static int msgcat_int(int n,int v){
    char b[8]; int i=0; if(v<0) v=0;
    if(!v) b[i++]='0'; else { char t[8]; int j=0; while(v&&j<7){ t[j++]=(char)('0'+v%10); v/=10; } while(j) b[i++]=t[--j]; }
    b[i]=0; return msgcat(n,b);
}
/* CB4 fleeing: a badly-hurt hostile (HP <= 1/4 of its starting HP) turns to flee —
 * worktype FLEE — and retreats from the party instead of pressing the attack.
 * Returns 1 the turn it newly enters the flee state (so the caller can announce it).
 * (Authentic U6 also has coward actors that flee until first hit, then assault — a
 * per-actor worktype we don't yet ship; this models the universal wounded-flee.) */
static int check_flee(int i){
    if(i<2 || i>=U6_MAX_ACTORS || g_aflee[i]) return 0;
    int mx = g_amaxhp[i] ? g_amaxhp[i] : 30;
    if(g_ahp[i]>0 && g_ahp[i] <= mx/4){ g_aflee[i]=1; return 1; }
    return 0;
}
/* an adjacent (8-way) living hostile actor to (x,y), or -1 (CB4 combat mode). */
static int adjacent_hostile(const world_t*w,int x,int y){
    (void)w;
    for(int i=2;i<U6_MAX_ACTORS;i++){ if(!actor_hostile(i)||is_party_member(i)) continue;  /* never target the party */
        int ax,ay; if(!actor_pos(i,&ax,&ay)) continue;
        int cx=ax>x?ax-x:x-ax, cy=ay>y?ay-y:y-ay;
        if(cx<=1 && cy<=1 && (cx||cy)) return i; }
    return -1;
}
/* total experience to BE a given level (U6: maxHP = level*30; thresholds double from 100).
 * L1=0 L2=100 L3=200 L4=400 L5=800 L6=1600 L7=3200 L8=6400 (cap 8). Verified vs the save:
 * party HP 90 = level 3 with exp ~370 (in [200,400)). (combat.md §3) */
static uint32_t exp_for_level(int lvl){
    if(lvl<=1) return 0; if(lvl>8) lvl=8;
    return (uint32_t)100u << (lvl-2);
}
/* award experience to the avatar. Leveling is NOT automatic — authentic U6 gates the HP gain
 * behind TRAINING (see try_train); exp just accumulates here. */
static void grant_exp(int actor,int gain){
    if(actor<0||actor>=U6_MAX_ACTORS) return;
    int e=(int)g_aexp[actor]+gain; if(e>65535) e=65535;   /* cap (L8 needs only 6400) */
    g_aexp[actor]=(uint16_t)e;
}

/* experience awarded for slaying actor v (combat.md §3): floor((str+dex+int+hp+dmg+exp)/4)
 * from the victim's stats — hp is the creature's max HP (g_amaxhp), dmg the killing blow,
 * exp the victim's own experience value. Replaces the placeholder 2+str/2. Min 1. */
static int kill_exp(int v,int dmg){
    int hp = g_amaxhp[v] ? g_amaxhp[v] : 30;
    int e = ((int)g_astr[v]+(int)g_adex[v]+(int)g_aint[v]+hp+dmg+(int)g_aexp[v])/4;
    return e>0 ? e : 1;
}

/* can any party member advance a level (earned enough exp)? gates the talk-to-trainer path. */
static int party_can_train(void){
    for(int p=0;p<g_nparty;p++){ int m=g_party[p].actor;
        if(m>0 && m<U6_MAX_ACTORS && g_alevel[m]<8 && g_aexp[m]>=exp_for_level(g_alevel[m]+1)) return 1; }
    return 0;
}
/* STAT trainer (combat.md §3): raise actor a's LOWEST of str/dex/int by 1 in exchange for gold
 * (capped at 30). Returns 1=str, 2=dex, 3=int, 0=none (capped or too little gold). MVP
 * simplifications (documented): one gold cost, auto-picks the lowest stat (no per-trainer
 * specialty / stat-choice menu), and gold is a placeholder (not yet loaded from the save). */
#define STAT_COST 100
static int can_train_stat(int a){
    if(a<0||a>=U6_MAX_ACTORS||g_cg.gold<STAT_COST) return 0;
    int lo=g_astr[a]; if(g_adex[a]<lo)lo=g_adex[a]; if(g_aint[a]<lo)lo=g_aint[a];
    return lo<30;
}
static int train_stat(int a){
    if(!can_train_stat(a)) return 0;
    int s=g_astr[a],d=g_adex[a],n=g_aint[a];
    int lo=(s<=d && s<=n)?1 : (d<=n)?2 : 3;              /* the lowest stat */
    g_cg.gold-=STAT_COST;
    if(lo==1) g_astr[a]=(uint8_t)(s+1); else if(lo==2) g_adex[a]=(uint8_t)(d+1); else g_aint[a]=(uint8_t)(n+1);
    return lo;
}

/* TRAINING gate (authentic U6: Lord British advances thy experience level when thou hast
 * earned it). Talking to a trainer advances EVERY party member that has earned the next level
 * (the whole party is blessed): +30 max HP each, heal to full. Exp is a THRESHOLD, not spent.
 * If no level is earned, the trainer instead raises a STAT for gold (stat-trainer MVP). */
static void try_train(int id){
    if(id<0 || id>=U6_MAX_ACTORS || !g_trainer[id]) return;
    int trained=0, av=0;
    for(int p=0;p<g_nparty;p++){ int m=g_party[p].actor; if(m<=0||m>=U6_MAX_ACTORS) continue;
        while(g_alevel[m]<8 && g_aexp[m]>=exp_for_level(g_alevel[m]+1)){
            g_alevel[m]++; g_amaxhp[m]=(uint8_t)(g_alevel[m]*30); g_ahp[m]=g_amaxhp[m];
            trained++; if(m==1) av=g_alevel[1]; }
    }
    sync_party_hp();
    if(av){ int n=msgcat(0,"\"Thou art now level "); n=msgcat_int(n,av); msgcat(n,"!\""); return; }
    if(trained){ msgcat(0,"\"Thy companions grow stronger!\""); return; }
    int st=train_stat(1);                                /* no level earned: train the avatar's lowest stat for gold */
    if(st){ const char*w=st==1?"strength":st==2?"agility":"wit"; int n=msgcat(0,"\"Thy "); n=msgcat(n,w); msgcat(n," grows!\""); }
    else  { msgcat(0,"\"Earn more experience, then return.\""); }
}
/* a party member strikes a foe (CB4 combat mode): like the player's attack — readied weapon
 * vs the foe's armour, removes a slain foe + grants the avatar experience (§3). */
static void party_attack(int att,int def){
    char nm[24]; obj_name(npc_tile(def),nm,sizeof nm);
    if(!nm[0]){ nm[0]='f'; nm[1]='o'; nm[2]='e'; nm[3]=0; }
    int wobj=g_aweapon[att]?g_aweapon[att]:43, wmax=weapon_dmg(wobj); if(wmax<=0) wmax=4;
    if(!combat_hit(status_hit_role(att,g_adex[att]?g_adex[att]:15), g_adex[def]?g_adex[def]:15, &g_crng)){
        int n=msgcat(0,nm); msgcat(n," evades."); return; }
    int dmg=status_dmg(def,combat_damage(wmax, g_aac[def], &g_crng));
    if(dmg<=0){ int n=msgcat(0,nm); msgcat(n," grazed."); return; }
    int hp=g_ahp[def]?g_ahp[def]:30;
    if(dmg>=hp){ g_ahp[def]=0; g_atile[def]=0xffff;          /* slain + removed */
        int gain=kill_exp(def,dmg);                          /* §3 exp-award table */
        int n=msgcat(0,"Slew "); n=msgcat(n,nm); n=msgcat(n,"! +"); n=msgcat_int(n,gain); msgcat(n,"exp");
        grant_exp(att,gain);   /* the slayer earns the exp (party leveling); gated by training */
    } else { g_ahp[def]=(uint8_t)(hp-dmg); int n=msgcat(0,nm); n=msgcat(n," wounded.");
             if(check_flee(def)) msgcat(n," Flees!"); }
}

/* Combat A)ttack (docs/rules/combat.md CB1): melee strike on the actor at (dx,dy) from the
 * avatar. Hit check = avatar dex vs target dex + d30 (§2); damage = a sword roll, max 15,
 * less the target AC (0 until we track readied armour) (§3); applies HP/death + messages.
 * The avatar's weapon (g_avatar_weapon) sets damage + RANGE: melee weapons need an adjacent
 * target; ranged weapons (bow/crossbow) hit the first actor along the direction within range,
 * blocked by walls (§6). CB3b will read the readied weapon/armour from OBJLIST inventory. */
static int g_avatar_weapon=43;                            /* readied weapon obj_n (default sword) */
/* does (x,y) block a thrown/fired shot? (terrain or an object sight-blocker) */
static int sight_blocked_at(const world_t*w,int x,int y){
    if(blocks_sight(world_tile(w,x,y))) return 1;
    int idx=objworld_at(&g_ow,x,y);
    return (idx>=0 && blocks_sight(g_wobj[idx].tile)) ? 1 : 0;
}
static int do_attack(world_t*w,int dx,int dy)
{
    if(!dx && !dy) return 0;
    int wobj=g_avatar_weapon, range=weapon_range(wobj), wmax=weapon_dmg(wobj);
    if(wmax<=0) wmax=4;                                   /* unarmed fallback */
    int id=-1, tx=(int)w->px, ty=(int)w->py;
    for(int r=1;r<=range;r++){                            /* first actor along the line (§6) */
        tx=(int)((w->px+dx*r)&(U6_SURFACE_TILES-1)); ty=(int)((w->py+dy*r)&(U6_SURFACE_TILES-1));
        id=npc_at(w,tx,ty); if(id>=0) break;
        if(sight_blocked_at(w,tx,ty)){ id=-1; break; }    /* arrow stops at a wall */
    }
    if(id<0){ msgcat(0, range>1?"No target in range.":"Nobody to attack there."); return 1; }
    g_hostile[id]=1; g_coward[id]=0; g_alert[id]=1;       /* striking it makes it fight back (CB2); a struck coward turns to ASSAULT; a struck foe knows where you are */
    char nm[24]; obj_name(npc_tile(id),nm,sizeof nm);
    if(!nm[0]){ nm[0]='f'; nm[1]='o'; nm[2]='e'; nm[3]=0; }
    int atk=g_adex[1]?g_adex[1]:15, fdex=g_adex[id]?g_adex[id]:15;
    if(!combat_hit(status_hit_role(1,atk),fdex,&g_crng)){ /* §2 hit check; curse penalises the Avatar (§5b) */
        int n=msgcat(0,"Missed "); n=msgcat(n,nm); msgcat(n,"."); return 1; }
    int dmg=status_dmg(id,combat_damage(wmax,g_aac[id],&g_crng));  /* weapon vs armour, then Protection (§5b) */
    if(dmg<=0){ int n=msgcat(0,nm); n=msgcat(n," grazed."); return 1; }
    int hp=g_ahp[id]?g_ahp[id]:30;
    if(dmg>=hp){                                          /* killed */
        g_ahp[id]=0; g_atile[id]=0xffff;                 /* remove from world (actor_pos skips) */
        int gain=kill_exp(id,dmg);                       /* §3 exp-award table */
        int n=msgcat(0,"Killed "); n=msgcat(n,nm); n=msgcat(n,"! +"); n=msgcat_int(n,gain); msgcat(n,"exp");
        grant_exp(1,gain);   /* the Avatar's kill; gated by training */
    } else {
        g_ahp[id]=(uint8_t)(hp-dmg);
        int n=msgcat(0,"Hit "); n=msgcat(n,nm); n=msgcat(n,"!");
        if(check_flee(id)) msgcat(n," It flees!");    /* CB4: wounded below 1/4 HP -> flees */
    }
    return 1;
}

/* ---- MAGIC: spellcasting (docs/rules/magic_skills.md) --------------------- */
/* reagent bitmask of what the Avatar's inventory (g_ow) currently holds (§2). */
static uint8_t reagent_mask(void){
    uint8_t m=0;
    for(int b=0;b<REAG_COUNT;b++)
        if(objworld_count(&g_ow,(uint16_t)magic_reagent_objn(b))>0) m|=(uint8_t)(1<<b);
    return m;
}
/* spend one of each required reagent + the mana (caller already passed magic_check). */
static void magic_spend(int caster,int spell){
    for(int b=0;b<REAG_COUNT;b++)
        if(g_spells[spell].reagents & (1<<b))
            objworld_consume(&g_ow,(uint16_t)magic_reagent_objn(b),1);
    int cost=magic_mana_cost(spell);
    g_amana[caster]=(uint8_t)(g_amana[caster]>cost?g_amana[caster]-cost:0);
}

/* ---- field-tile layer (docs/rules/magic_skills.md §5a) -------------------- */
static int field_at(int x,int y){
    for(int i=0;i<g_nfields;i++) if(g_fields[i].x==x && g_fields[i].y==y) return i;
    return -1;
}
static void field_add(int x,int y,uint16_t obj_n,uint8_t kind,uint8_t turns){
    int i=field_at(x,y);
    if(i<0){ if(g_nfields>=MAXFIELDS) return; i=g_nfields++; }
    g_fields[i].x=(uint16_t)x; g_fields[i].y=(uint16_t)y; g_fields[i].obj_n=obj_n;
    g_fields[i].kind=kind; g_fields[i].turns=turns; g_fields[i].tile=(uint16_t)basetile(obj_n);
    if(kind==3) build_blocked();                      /* energy field: rebuild the passability bitmap */
}
static void field_remove(int i){
    if(i<0||i>=g_nfields) return;
    int wasblock=(g_fields[i].kind==3);
    g_fields[i]=g_fields[--g_nfields];
    if(wasblock) build_blocked();                     /* stopped blocking: recompute passability */
}
/* one game turn of field effects: damage/sleep any actor on a field, age fields out. */
static void fields_tick(const world_t*w){
    (void)w;
    for(int i=0;i<g_nfields;){
        field_t*f=&g_fields[i];
        for(int a=0;a<U6_MAX_ACTORS;a++){             /* affect any actor standing on the field */
            int ax,ay; if(!actor_pos(a,&ax,&ay)) continue;
            if((ax&(U6_SURFACE_TILES-1))!=f->x || (ay&(U6_SURFACE_TILES-1))!=f->y) continue;
            if(f->kind==0||f->kind==1){               /* fire / poison: damage */
                int dmg=combat_damage(f->kind==0?9:4,0,&g_crng), hp=g_ahp[a]?g_ahp[a]:0;
                g_ahp[a]=(dmg>=hp)?0:(uint8_t)(hp-dmg);
                if(g_ahp[a]==0 && a!=1) g_atile[a]=0xffff;   /* slain (not the Avatar) */
            } else if(f->kind==2){                    /* sleep */
                g_asleep[a]=3;
            }
        }
        if(f->turns>0) f->turns--;
        if(f->turns==0) field_remove(i); else i++;
    }
    sync_party_hp();
}

/* ---- status effects (docs/rules/magic_skills.md §5b) ---------------------- */
/* one game turn of status: poison damage-over-time, then decay every timer. */
static void status_tick(void){
    for(int a=0;a<U6_MAX_ACTORS;a++){
        if(g_poison[a]){                              /* poison: lose a little HP each turn */
            int hp=g_ahp[a];
            if(hp>0){ int d=1+rng_range(&g_crng,3); g_ahp[a]=(d>=hp)?0:(uint8_t)(hp-d);
                if(g_ahp[a]==0 && a!=1) g_atile[a]=0xffff; }   /* poisoned to death (not the Avatar) */
            g_poison[a]--;
        }
        if(g_protect[a]) g_protect[a]--;
        if(g_curse[a])   g_curse[a]--;
        if(g_charm[a])   g_charm[a]--;
    }
    sync_party_hp();                                  /* poison may have changed party HP */
}
/* first actor along (dx,dy) within range, for a targeted status spell (msg on miss). */
static int spell_target_actor(world_t*w,int dx,int dy,const char*sname){
    if(!dx&&!dy){ int n=msgcat(0,sname); msgcat(n,": aim a direction."); return -1; }
    for(int r=1;r<=5;r++){ int tx=(int)((w->px+dx*r)&(U6_SURFACE_TILES-1)), ty=(int)((w->py+dy*r)&(U6_SURFACE_TILES-1));
        int id=npc_at(w,tx,ty); if(id>=0) return id;
        if(sight_blocked_at(w,tx,ty)) break; }
    { int n=msgcat(0,sname); msgcat(n,": no target."); }
    return -1;
}

/* ---- summoning (docs/rules/magic_skills.md §5c) --------------------------- */
/* a free actor slot for a summon (searches high slots, avoiding the demo slug 250). */
static int summon_alloc(void){
    for(int i=U6_MAX_ACTORS-2;i>=160;i--) if(i!=250 && g_atile[i]==0xffff) return i;
    return -1;
}
/* place a creature actor of obj_n at (x,y). mode: 1=allied, 2=hostile. Returns its slot or -1. */
static int summon_creature(int x,int y,uint16_t obj_n,uint8_t mode,uint8_t life,uint8_t hp){
    int s=summon_alloc(); if(s<0) return -1;
    uint16_t base=basetile(obj_n);
    g_atile[s]=base; g_abase[s]=base; g_atpd[s]=0; g_facing[s]=2;
    g_actx[s]=(uint16_t)(x&(U6_SURFACE_TILES-1)); g_acty[s]=(uint16_t)(y&(U6_SURFACE_TILES-1)); g_actz[s]=0;
    g_ahp[s]=hp; g_amaxhp[s]=hp; g_astr[s]=12; g_adex[s]=12; g_aint[s]=8;
    g_aweapon[s]=0; g_aac[s]=0; g_aflee[s]=0; g_asleep[s]=g_paralyze[s]=0;
    g_summon[s]=mode; g_summon_life[s]=life;
    g_align_hostile[s]=(mode==2)?1:0; g_hostile[s]=0; g_alert[s]=(mode==2)?1:0; g_charm[s]=0;
    return s;
}
/* nearest living hostile to (x,y) within `maxd`, or -1 (fills *tx,*ty). */
static int nearest_hostile(int x,int y,int maxd,int*tx,int*ty){
    int best=-1,bd=maxd+1;
    for(int h=2;h<U6_MAX_ACTORS;h++){ if(!actor_hostile(h)||is_party_member(h)) continue;
        int ax,ay; if(!actor_pos(h,&ax,&ay)) continue;
        int cx=ax>x?ax-x:x-ax, cy=ay>y?ay-y:y-ay, d=cx>cy?cx:cy;
        if(d<bd){ bd=d; best=h; if(tx)*tx=ax; if(ty)*ty=ay; } }
    return best;
}
/* allied-summon AI (§5c): attack an adjacent hostile, else path toward the nearest one,
 * else follow the Avatar. Mirrors hostile_step but hunts hostiles instead of the party. */
static void ally_step(const world_t*w,int i){
    int x=g_actx[i], y=g_acty[i], avx=(int)w->px, avy=(int)w->py;
    int adj=adjacent_hostile(w,x,y);
    if(adj>=0){ int ax,ay; actor_pos(adj,&ax,&ay);
        g_facing[i]= ay<y?0:ay>y?2:ax>x?1:3; party_attack(i,adj); g_amoved[i]=0; return; }
    int tx=avx,ty=avy, h=nearest_hostile(x,y,10,&tx,&ty);
    int gx=(h>=0)?tx:avx, gy=(h>=0)?ty:avy;               /* hunt a foe, else regroup on the Avatar */
    int mx,my;
    if(npc_pathstep(w,x,y,gx,gy,avx,avy,&mx,&my)){
        int nx=(x+mx)&(U6_SURFACE_TILES-1), ny=(y+my)&(U6_SURFACE_TILES-1);
        if(!npc_occupied(i,nx,ny)){ g_actx[i]=(uint16_t)nx; g_acty[i]=(uint16_t)ny;
            g_facing[i]= my<0?0:my>0?2:mx>0?1:3; g_walkf[i]^=1; g_amoved[i]=1; return; } }
    g_amoved[i]=0;
}
/* place a summon of obj_n on the first free/passable tile at or beside (tx,ty). */
static int summon_near(world_t*w,int tx,int ty,uint16_t obj_n,uint8_t mode,uint8_t life,uint8_t hp){
    static const int dd[9][2]={{0,0},{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,-1},{1,-1},{-1,1}};
    for(int k=0;k<9;k++){ int x=(int)((tx+dd[k][0])&(U6_SURFACE_TILES-1)), y=(int)((ty+dd[k][1])&(U6_SURFACE_TILES-1));
        if(npc_at(w,x,y)<0 && world_is_passable(w,x,y)) return summon_creature(x,y,obj_n,mode,life,hp); }
    return -1;
}
/* Clone (§5c): copy the target actor's appearance/stats/alignment into a new slot beside it. */
static int clone_actor(world_t*w,int id){
    int ax,ay; if(!actor_pos(id,&ax,&ay)) return -1;
    static const int dd[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,-1},{1,-1},{-1,1}};
    int s=summon_alloc(); if(s<0) return -1;
    for(int k=0;k<8;k++){ int x=(int)((ax+dd[k][0])&(U6_SURFACE_TILES-1)), y=(int)((ay+dd[k][1])&(U6_SURFACE_TILES-1));
        if(npc_at(w,x,y)>=0 || !world_is_passable(w,x,y)) continue;
        int hostile=actor_hostile(id);
        g_atile[s]=g_atile[id]; g_abase[s]=g_abase[id]; g_atpd[s]=g_atpd[id]; g_facing[s]=g_facing[id];
        g_actx[s]=(uint16_t)x; g_acty[s]=(uint16_t)y; g_actz[s]=0;
        g_ahp[s]=g_ahp[id]; g_amaxhp[s]=g_amaxhp[id]; g_astr[s]=g_astr[id]; g_adex[s]=g_adex[id]; g_aint[s]=g_aint[id];
        g_aweapon[s]=g_aweapon[id]; g_aac[s]=g_aac[id]; g_aflee[s]=0; g_asleep[s]=g_paralyze[s]=g_charm[s]=0;
        g_summon[s]=(uint8_t)(hostile?2:1); g_summon_life[s]=40;
        g_align_hostile[s]=(uint8_t)(hostile?1:0); g_hostile[s]=0; g_alert[s]=(uint8_t)(hostile?1:0);
        return s;
    }
    return -1;
}
/* ---- travel (docs/rules/magic_skills.md §5d) ----------------------------- */
/* relocate the whole party to (x,y): move the Avatar there, re-seed each member on a
 * free tile beside it, and invalidate the scroll cache so the map fully rebuilds. */
static void teleport_party(world_t*w,int x,int y){
    x&=(U6_SURFACE_TILES-1); y&=(U6_SURFACE_TILES-1);
    w->px=(uint16_t)x; w->py=(uint16_t)y;
    static const int dd[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,-1},{1,-1},{-1,1}};
    int k=0;
    for(int p=0;p<g_nparty;p++){ int a=g_party[p].actor; if(a<=1) continue;   /* the Avatar is the world pos */
        int placed=0;
        for(;k<8 && !placed;k++){ int nx=(int)((x+dd[k][0])&(U6_SURFACE_TILES-1)), ny=(int)((y+dd[k][1])&(U6_SURFACE_TILES-1));
            if(npc_at(w,nx,ny)<0 && world_is_passable(w,nx,ny)){ g_actx[a]=(uint16_t)nx; g_acty[a]=(uint16_t)ny; g_actz[a]=0; placed=1; } }
        if(!placed){ g_actx[a]=(uint16_t)x; g_acty[a]=(uint16_t)y; g_actz[a]=0; }   /* no room: stack on the Avatar */
    }
    g_tmp_valid=0;   /* a jump is NOT a 1-tile scroll — force a full compose at the destination */
}
/* Telekinesis (§5e): apply the Use dispatcher to the first object along (dx,dy).
 * Returns 1 if an object was operated (a door blocks_sight but IS the object, so
 * objworld_at finds it before the sight test stops the scan). */
static int spell_telekinesis(world_t*w,int dx,int dy){
    for(int r=1;r<=5;r++){ int tx=(int)((w->px+dx*r)&(U6_SURFACE_TILES-1)), ty=(int)((w->py+dy*r)&(U6_SURFACE_TILES-1));
        int idx=objworld_at(&g_ow,tx,ty);
        if(idx>=0){ use_object(idx); return 1; }       /* operate it from afar */
        if(sight_blocked_at(w,tx,ty)) break;           /* a base-map wall stops the reach */
    }
    msgcat(0,"Nothing to move there."); return 0;
}
/* offensive bolt: first actor along (dx,dy) within range takes `wmax`-based damage. */
static int spell_bolt(world_t*w,int dx,int dy,int wmax,const char*sname){
    int range=5,id=-1,tx=(int)w->px,ty=(int)w->py;
    if(!dx&&!dy){ int n=msgcat(0,sname); msgcat(n,": aim a direction."); return 0; }
    for(int r=1;r<=range;r++){ tx=(int)((w->px+dx*r)&(U6_SURFACE_TILES-1)); ty=(int)((w->py+dy*r)&(U6_SURFACE_TILES-1));
        id=npc_at(w,tx,ty); if(id>=0) break; if(sight_blocked_at(w,tx,ty)){ id=-1; break; } }
    if(id<0){ int n=msgcat(0,sname); msgcat(n,": no target."); return 0; }
    g_hostile[id]=1; g_alert[id]=1; g_coward[id]=0;
    char foe[24]; obj_name(npc_tile(id),foe,sizeof foe); if(!foe[0]){foe[0]='f';foe[1]='o';foe[2]='e';foe[3]=0;}
    int dmg=combat_damage(wmax,g_aac[id],&g_crng), hp=g_ahp[id]?g_ahp[id]:30;
    if(dmg>=hp){ g_ahp[id]=0; g_atile[id]=0xffff; int gain=kill_exp(id,dmg);
        int n=msgcat(0,sname); n=msgcat(n," slays "); n=msgcat(n,foe); n=msgcat(n,"! +"); n=msgcat_int(n,gain); msgcat(n,"exp");
        grant_exp(1,gain); }
    else { g_ahp[id]=(uint8_t)(hp-dmg); int n=msgcat(0,sname); n=msgcat(n," hits "); n=msgcat(n,foe); msgcat(n,"!"); check_flee(id); }
    return 1;
}
/* Unlock Magic (EX POR): dispel a MAGICAL lock on the door/chest in the target
 * direction. Doors carry the lock in the tile frame (13/15 = magic-locked, one
 * orientation each); chests use frame 3. The mundane-key locks (door 9/11,
 * chest 2) are NOT this spell's job — a KEY handles those (usecode_use.md). */
static int spell_unlock(world_t*w,int dx,int dy){
    if(!dx&&!dy){ msgcat(0,"Unlock Magic: aim a direction."); return 0; }
    int tx=(int)((w->px+dx)&(U6_SURFACE_TILES-1)), ty=(int)((w->py+dy)&(U6_SURFACE_TILES-1));
    int idx=objworld_at(&g_ow,tx,ty);
    if(idx<0){ msgcat(0,"There is no lock to dispel."); return 0; }
    int o=g_wobj[idx].obj_n, fr=(int)g_wobj[idx].tile-basetile(o);
    if(o>=297&&o<=301 && (fr==13||fr==15)){          /* magic-locked door -> fully unlocked (closed) */
        g_wobj[idx].tile-=8;                         /* 13->5, 15->7 = closed (openable) */
        msgcat(0,"The magical lock dissolves!"); build_blocked(); return 1; }
    if(o==98 && fr==3){                              /* magic-locked chest -> closed */
        g_wobj[idx].tile-=2;                         /* 3 -> 1 = closed */
        msgcat(0,"The magical lock dissolves!"); return 1; }
    msgcat(0,"There is no magical lock here."); return 0;
}
/* Cast dense spell `spell` by `caster` (actor id), optional target dir (dx,dy).
 * Runs the full gate-check, then (if implemented) applies the effect + spends
 * reagents/mana. Returns 1 if the map must recompose. Sets g_msg. */
static int cast_spell(world_t*w,int caster,int spell,int dx,int dy){
    int intel = g_aint[caster]?g_aint[caster]:15;
    cast_result_t rr = magic_check(spell, spell>=0&&spell<SPELL_COUNT?g_known[spell]:0,
                                   intel, reagent_mask(), g_amana[caster]);
    if(rr!=CAST_OK){
        msgcat(0, rr==CAST_UNKNOWN  ?"Thou dost not know that spell." :
                  rr==CAST_LOWLEVEL ?"Thou art not experienced enough." :
                  rr==CAST_NOREAGENTS?"Thou hast not the reagents!" :
                  rr==CAST_NOMANA   ?"Not enough magic points!" : "Nothing happens.");
        return 0;
    }
    const char*nm=g_spells[spell].name; int applied=1, recompose=0;
    if(!strcmp(nm,"Heal")||!strcmp(nm,"Great Heal")){
        int amt = !strcmp(nm,"Great Heal")?40:20;         /* magnitudes: magic_skills.md §8 (conservative) */
        int tgt=caster, worst=0;                          /* heal the most-wounded party member */
        for(int p=0;p<g_nparty;p++){ int a=g_party[p].actor, mx=g_amaxhp[a]?g_amaxhp[a]:30, d=mx-g_ahp[a];
            if(g_ahp[a]>0 && d>worst){ worst=d; tgt=a; } }
        int mx=g_amaxhp[tgt]?g_amaxhp[tgt]:30, old=g_ahp[tgt], nv=old+amt; if(nv>mx)nv=mx; g_ahp[tgt]=(uint8_t)nv;
        int n=msgcat(0,nm); n=msgcat(n,"! +"); n=msgcat_int(n,nv-old); msgcat(n," HP");
        sync_party_hp();
    } else if(!strcmp(nm,"Harm")||!strcmp(nm,"Magic Arrow")||!strcmp(nm,"Fireball")){
        int wmax = !strcmp(nm,"Fireball")?18 : !strcmp(nm,"Harm")?6 : 10;
        if(!spell_bolt(w,dx,dy,wmax,nm)) applied=0;       /* no target -> don't spend */
        else recompose=1;
    } else if(!strcmp(nm,"Light")||!strcmp(nm,"Great Light")){
        int big=!strcmp(nm,"Great Light");
        g_spell_light_turns=(uint8_t)(big?60:30); g_spell_light_rad=(uint8_t)(big?5:3);
        int n=msgcat(0,nm); msgcat(n,"!"); recompose=1;
    } else if(!strcmp(nm,"Create Food")){                 /* conjure a portion of food into the pack */
        int on=129, merged=0;                             /* obj 129 = "portion of meat" (stackable) */
        for(int i=0;i<g_ow.ninv;i++) if(g_ow.inv[i].obj_n==on && g_ow.inv[i].qty<65535){ g_ow.inv[i].qty++; merged=1; break; }
        if(!merged){ if(g_ow.ninv<OBJ_INV_MAX){ g_ow.inv[g_ow.ninv].obj_n=(uint16_t)on;
            g_ow.inv[g_ow.ninv].tile=basetile((uint16_t)on); g_ow.inv[g_ow.ninv].qty=1; g_ow.inv[g_ow.ninv].quality=0; g_ow.ninv++; }
            else applied=0; }
        if(applied) msgcat(0,"Thou hast conjured food!"); else msgcat(0,"Thy pack is full.");
    } else if(!strcmp(nm,"Unlock Magic")){                /* dispel a MAGIC lock on a door/chest (§5) */
        if(!spell_unlock(w,dx,dy)) applied=0;             /* nothing to dispel -> don't spend */
        else recompose=1;
    } else if(!strcmp(nm,"Fire Field")||!strcmp(nm,"Poison Field")||
              !strcmp(nm,"Sleep Field")||!strcmp(nm,"Energy Field")){   /* place a field (§5a) */
        if(!dx&&!dy){ int n=msgcat(0,nm); msgcat(n,": aim a direction."); applied=0; }
        else { uint16_t on; uint8_t kind;
            if(!strcmp(nm,"Fire Field")){on=317;kind=0;} else if(!strcmp(nm,"Poison Field")){on=318;kind=1;}
            else if(!strcmp(nm,"Sleep Field")){on=320;kind=2;} else {on=51;kind=3;}
            int tx=(int)((w->px+dx)&(U6_SURFACE_TILES-1)), ty=(int)((w->py+dy)&(U6_SURFACE_TILES-1));
            field_add(tx,ty,on,kind,12);                  /* ~12-turn field (§5a) */
            int n=msgcat(0,nm); msgcat(n,"!"); recompose=1; }
    } else if(!strcmp(nm,"Dispel Field")){                /* remove a field on the target tile (§5a) */
        int tx=(int)((w->px+dx)&(U6_SURFACE_TILES-1)), ty=(int)((w->py+dy)&(U6_SURFACE_TILES-1));
        int fi=field_at(tx,ty);
        if(fi>=0){ field_remove(fi); msgcat(0,"The field vanishes."); recompose=1; }
        else { msgcat(0,"There is no field there."); applied=0; }
    } else if(!strcmp(nm,"Poison")||!strcmp(nm,"Sleep")||!strcmp(nm,"Paralyze")||
              !strcmp(nm,"Curse")||!strcmp(nm,"Charm")){   /* single-target status (§5b) */
        int id=spell_target_actor(w,dx,dy,nm);
        if(id<0) applied=0;
        else if(!strcmp(nm,"Charm") && !actor_hostile(id)){ msgcat(0,"It is not hostile."); applied=0; }
        else {
            if(!strcmp(nm,"Poison"))        g_poison[id]=10;
            else if(!strcmp(nm,"Sleep"))    g_asleep[id]=8;
            else if(!strcmp(nm,"Paralyze")) g_paralyze[id]=12;
            else if(!strcmp(nm,"Curse"))    g_curse[id]=15;
            else                            g_charm[id]=15;   /* Charm: pacify the hostile */
            char foe[24]; obj_name(npc_tile(id),foe,sizeof foe);
            int n=msgcat(0,nm); n=msgcat(n," the "); n=msgcat(n,foe[0]?foe:"foe"); msgcat(n,"!");
        }
    } else if(!strcmp(nm,"Protection")||!strcmp(nm,"Mass Protect")){   /* party buff (§5b) */
        int turns = !strcmp(nm,"Mass Protect")?30:20;
        for(int p=0;p<g_nparty;p++){ int a=g_party[p].actor; if(a>=0&&a<U6_MAX_ACTORS) g_protect[a]=(uint8_t)turns; }
        int n=msgcat(0,nm); msgcat(n," shields the party!");
    } else if(!strcmp(nm,"Conjure")||!strcmp(nm,"Summon")||!strcmp(nm,"Insect Swarm")){  /* summon allies (§5c) */
        if(!dx&&!dy){ int n=msgcat(0,nm); msgcat(n,": aim a direction."); applied=0; }
        else { int tx=(int)((w->px+dx)&(U6_SURFACE_TILES-1)), ty=(int)((w->py+dy)&(U6_SURFACE_TILES-1)), got=0;
            if(!strcmp(nm,"Conjure")) got=(summon_near(w,tx,ty,345,1,30,20)>=0);        /* 1 allied giant bat */
            else if(!strcmp(nm,"Insect Swarm")){ for(int k=0;k<3;k++) if(summon_near(w,tx,ty,344,1,20,8)>=0) got++; }  /* 3 allied insect swarms */
            else got=(summon_near(w,tx,ty,368,1,40,60)>=0);                             /* Summon: 1 allied daemon */
            if(got){ int n=msgcat(0,nm); msgcat(n,"!"); recompose=1; } else { msgcat(0,"No room to summon."); applied=0; } }
    } else if(!strcmp(nm,"Clone")){                       /* duplicate the target creature (§5c) */
        int id=spell_target_actor(w,dx,dy,nm);
        if(id<0) applied=0;
        else if(clone_actor(w,id)<0){ msgcat(0,"No room to clone."); applied=0; }
        else { char foe[24]; obj_name(npc_tile(id),foe,sizeof foe);
            int n=msgcat(0,"Cloned the "); n=msgcat(n,foe[0]?foe:"creature"); msgcat(n,"!"); recompose=1; }
    } else if(!strcmp(nm,"Help")){                        /* recall the party to Lord British (§5d) */
        int lb=-1; for(int a=2;a<U6_MAX_ACTORS;a++) if(g_trainer[a]){ lb=a; break; }
        int lx,ly;
        if(lb<0 || !actor_pos(lb,&lx,&ly)){ msgcat(0,"Lord British is beyond thy reach."); applied=0; }
        else { static const int dd[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,-1},{1,-1},{-1,1}};
            int tx=lx,ty=ly;                              /* a free tile beside Lord British */
            for(int k=0;k<8;k++){ int nx=(int)((lx+dd[k][0])&(U6_SURFACE_TILES-1)), ny=(int)((ly+dd[k][1])&(U6_SURFACE_TILES-1));
                if(npc_at(w,nx,ny)<0 && world_is_passable(w,nx,ny)){ tx=nx; ty=ny; break; } }
            teleport_party(w,tx,ty); msgcat(0,"Thou art returned to Lord British!"); recompose=1; }
    } else if(!strcmp(nm,"Gate Travel")){                 /* moongate to a moonstone destination (§5d) */
        if(!g_savetmpl || g_objlist_off<0){ msgcat(0,"No moongate answers thy call."); applied=0; }
        else { const uint8_t*ml=g_savetmpl+g_objlist_off+0x1c1b; int mx=-1,my=-1;
            for(int t=0;t<8;t++){ int j=(g_gate_dest+t)&7; const uint8_t*e=ml+j*6;   /* next SURFACE moonstone */
                int ex=e[0]|(e[1]<<8), ey=e[2]|(e[3]<<8), ez=e[4]|(e[5]<<8);
                if(ez==0 && (ex||ey) && !(ex==(int)w->px&&ey==(int)w->py)){ mx=ex; my=ey; g_gate_dest=j+1; break; } }
            if(mx<0){ msgcat(0,"The moongate flickers and fades."); applied=0; }
            else { teleport_party(w,mx,my);
                int n=msgcat(0,"The red moongate bears thee to ("); n=msgcat_int(n,mx); n=msgcat(n,","); n=msgcat_int(n,my); msgcat(n,")!"); recompose=1; } }
    } else if(!strcmp(nm,"Locate")){                      /* sextant: report the party's position (§5e) */
        int n=msgcat(0,"Thou art at ("); n=msgcat_int(n,(int)w->px); n=msgcat(n,","); n=msgcat_int(n,(int)w->py); msgcat(n,").");
    } else if(!strcmp(nm,"Detect Magic")){                /* sense nearby magic locks (§5e) */
        int cnt=0;
        for(int32_t i=0;i<g_ow.nobjs;i++){ const wobj_t*o=&g_wobj[i]; if(!o->alive) continue;
            int cx=(int)o->x-(int)w->px, cy=(int)o->y-(int)w->py; if(cx<0)cx=-cx; if(cy<0)cy=-cy;
            if(cx>10||cy>10) continue;                    /* within ~10 tiles */
            int fr=(int)o->tile-basetile(o->obj_n);
            if((o->obj_n>=297&&o->obj_n<=301 && (fr==13||fr==15)) || (o->obj_n==98&&fr==3)) cnt++; }
        int n=msgcat(0,"Thou sensest "); n=msgcat_int(n,cnt); msgcat(n, cnt==1?" magical aura.":" magical auras.");
    } else if(!strcmp(nm,"X-ray")){                       /* see through walls for a while (§5e) */
        g_xray_turns=30; msgcat(0,"Thou canst see through walls!"); recompose=1;
    } else if(!strcmp(nm,"Telekinesis")){                 /* operate a distant object (§5e) */
        if(!dx&&!dy){ msgcat(0,"Telekinesis: aim a direction."); applied=0; }
        else if(!spell_telekinesis(w,dx,dy)) applied=0;
        else recompose=1;
    } else if(!strcmp(nm,"Peer")){                         /* gem overview of the area (§5) */
        overview_view(w,(int)w->px,(int)w->py,0); msgcat(0,"Thou peerest at the land."); recompose=1;
    } else if(!strcmp(nm,"Wizard Eye")){                   /* a scouting eye — pan the overview (§5) */
        overview_view(w,(int)w->px,(int)w->py,1); msgcat(0,"The eye returns to thee."); recompose=1;
    } else if(!strcmp(nm,"Invisibility")||!strcmp(nm,"Mass Invisibility")){   /* vanish (§5) */
        if(!strcmp(nm,"Mass Invisibility")){ for(int p=0;p<g_nparty;p++) g_ainvis[g_party[p].actor]=60;
            msgcat(0,"The party fades from sight."); }
        else { int tgt=1;                                 /* aim: an actor in the direction, else self */
            if(dx||dy){ int a=npc_at(w,(int)((w->px+dx)&(U6_SURFACE_TILES-1)),(int)((w->py+dy)&(U6_SURFACE_TILES-1)));
                        if(a>=0) tgt=a; }
            g_ainvis[tgt]=60;
            int n=msgcat(0, tgt==1?"Thou fadest":"It fades"); msgcat(n," from sight."); }
        recompose=1;
    } else if(!strcmp(nm,"Reveal")){                       /* dispel invisibility nearby (§5) */
        int cnt=0;
        for(int i=1;i<U6_MAX_ACTORS;i++){ if(!g_ainvis[i]) continue; int ax,ay; if(!actor_pos(i,&ax,&ay)) continue;
            int cx=ax-(int)w->px, cy=ay-(int)w->py; if(cx<0)cx=-cx; if(cy<0)cy=-cy;
            if(cx<=5 && cy<=5){ g_ainvis[i]=0; cnt++; } }
        int n=msgcat(0,"Reveal! "); n=msgcat_int(n,cnt); msgcat(n, cnt==1?" form seen.":" forms seen."); recompose=1;
    } else { int n=msgcat(0,nm); msgcat(n,": not yet implemented."); applied=0; }
    if(applied) magic_spend(caster,spell);
    return recompose;
}
/* Seed the Avatar's spellbook + a starter reagent stock at world load (§4). The
 * Avatar knows every spell whose circle its Intelligence can reach; U6 begins the
 * game with reagents in the pack, so we stock 10 of each (we ship no save data). */
static void magic_init(void){
    /* the Avatar knows the spells whose EFFECTS we implement (the menu lists only
     * spells that actually do something; circle-gating still hides too-high ones). */
    static const char*impl[]={"Heal","Great Heal","Light","Great Light",
                              "Harm","Magic Arrow","Fireball","Create Food","Unlock Magic",
                              "Fire Field","Poison Field","Sleep Field","Energy Field","Dispel Field",
                              "Poison","Sleep","Paralyze","Curse","Charm","Protection","Mass Protect",
                              "Conjure","Insect Swarm","Clone","Summon","Help","Gate Travel",
                              "Locate","Detect Magic","X-ray","Telekinesis",
                              "Peer","Wizard Eye","Reveal","Invisibility","Mass Invisibility",0};
    for(int s=0;s<SPELL_COUNT;s++) g_known[s]=0;
    for(int i=0;impl[i];i++){ int s=magic_find(impl[i]); if(s>=0) g_known[s]=1; }
    for(int b=0;b<REAG_COUNT && g_ow.ninv<OBJ_INV_MAX;b++){
        uint16_t on=(uint16_t)magic_reagent_objn(b);
        if(objworld_count(&g_ow,on)>0) continue;          /* don't duplicate any already held */
        g_ow.inv[g_ow.ninv].obj_n=on; g_ow.inv[g_ow.ninv].tile=basetile(on);
        g_ow.inv[g_ow.ninv].qty=10; g_ow.inv[g_ow.ninv].quality=0; g_ow.ninv++;
    }
    /* the spellbook itself (obj_n 57, magic_skills.md §4): U6 begins the game with Lord British
     * handing the Avatar the spellbook, so carry it as an inventory item (the fresh save ships
     * none — magic_skills.md §8). Guarded so it is not duplicated on reload. */
    if(g_ow.ninv<OBJ_INV_MAX && objworld_count(&g_ow,57)==0){
        g_ow.inv[g_ow.ninv].obj_n=57; g_ow.inv[g_ow.ninv].tile=basetile(57);
        g_ow.inv[g_ow.ninv].qty=1; g_ow.inv[g_ow.ninv].quality=0; g_ow.ninv++;
    }
}
/* an offensive spell fires down a direction, so the menu arms a target after it. */
static int spell_needs_target(int spell){
    if(spell<0||spell>=SPELL_COUNT) return 0;
    const char*n=g_spells[spell].name;
    return !strcmp(n,"Harm")||!strcmp(n,"Magic Arrow")||!strcmp(n,"Fireball")||
           !strcmp(n,"Unlock Magic")||
           !strcmp(n,"Fire Field")||!strcmp(n,"Poison Field")||!strcmp(n,"Sleep Field")||
           !strcmp(n,"Energy Field")||!strcmp(n,"Dispel Field")||
           !strcmp(n,"Poison")||!strcmp(n,"Sleep")||!strcmp(n,"Paralyze")||
           !strcmp(n,"Curse")||!strcmp(n,"Charm")||
           !strcmp(n,"Conjure")||!strcmp(n,"Summon")||!strcmp(n,"Insect Swarm")||!strcmp(n,"Clone")||
           !strcmp(n,"Telekinesis");
}
/* open the C)ast menu: collect the Avatar's known + circle-castable spells (§4). */
static void open_cast_menu(void){
    int maxc=magic_castable_circle(g_aint[1]?g_aint[1]:15);
    g_ncast=0;
    for(int s=0;s<SPELL_COUNT && g_ncast<SPELL_COUNT;s++)
        if(g_known[s] && g_spells[s].circle<=maxc) g_castlist[g_ncast++]=(uint8_t)s;
    g_casting=1; g_inlen=0; g_inbuf[0]=0; g_show_inv=0; g_msg[0]=0; g_pending_verb=0;
}
static void close_cast_menu(void){ g_casting=0; g_inlen=0; g_inbuf[0]=0; }
/* map the typed selection (a 1-based menu number, or a spell-name prefix) to a
 * dense spell index, or -1. Pure — no side effects. */
static int cast_menu_parse(const char*s){
    if(!s||!s[0]) return -1;
    int allnum=1; for(const char*p=s;*p;p++) if(*p<'0'||*p>'9'){ allnum=0; break; }
    if(allnum){ int n=0; for(const char*p=s;*p;p++) n=n*10+(*p-'0');
        return (n>=1&&n<=g_ncast)?g_castlist[n-1]:-1; }
    for(int i=0;i<g_ncast;i++){ const char*nm=g_spells[g_castlist[i]].name; int k=0;
        while(s[k]&&nm[k]){ char a=s[k],b=nm[k];
            if(a>='A'&&a<='Z')a+=32; if(b>='A'&&b<='Z')b+=32; if(a!=b) break; k++; }
        if(!s[k]) return g_castlist[i];   /* the typed text is a prefix of this name */
    }
    return -1;
}
/* commit the typed selection: cast a self/party spell now, or arm a direction for
 * a targeted spell (the existing "(dir)?" flow resolves it). Returns 1 to recompose. */
static int cast_menu_submit(world_t*w){
    int spell=cast_menu_parse(g_inbuf);
    close_cast_menu();
    if(spell<0){ msgcat(0,"No such spell."); return 0; }
    if(spell_needs_target(spell)){
        g_cast_spell=spell; g_pending_verb='C';
        int n=msgcat(0,"Cast "); n=msgcat(n,g_spells[spell].name); msgcat(n," where?"); return 0; }
    return cast_spell(w,1,spell,0,0);          /* self/party spell (Avatar = actor 1) */
}

/* apply the armed verb to the tile (dx,dy) from the avatar. Returns 1 if the
 * map needs recomposing (Get removed an object / Move stepped). */
static int resolve_target(world_t*w,int verb,int dx,int dy)
{
    if(verb=='C'){ return cast_spell(w, g_nparty?g_party[0].actor:1, g_cast_spell, dx, dy); }
    if(verb=='A'){ return do_attack(w,dx,dy); }
    if(verb=='L'){ do_verb(w,'L',dx,dy); return 0; }
    if(verb=='U'){ do_verb(w,'U',dx,dy); return 0; }
    if(verb=='G'){ return do_verb(w,'G',dx,dy); }
    if(verb=='M'){ int sx=dx<0?-1:(dx>0?1:0), sy=dy<0?-1:(dy>0?1:0);
        if((sx||sy) && world_try_move(w,sx,sy)){ g_hour=clock_hour(w); g_minutes=clock_minutes(w); step_npcs(w); return 1; }
        return 0; }
    if(verb=='T'){ int tx=(int)((w->px+dx)&(U6_SURFACE_TILES-1)), ty=(int)((w->py+dy)&(U6_SURFACE_TILES-1));
        int id=npc_at(w,tx,ty); if(id>=0) talk(id); else msgcat(0,"No-one to talk to."); return 0; }
    return 0;
}

/* physical mouse (line-A) -> our LOGICAL coords. The TT presents the 320x200 UI
 * 2x-vertically-doubled, centred in 320x480 (top band = 40px), so x is 1:1 and
 * y un-doubles: ly = (screen_y - 40) / 2. */
static void read_mouse_logical(int *lx,int *ly,int *btn)
{
    int sx,sy; plat_mouse(&sx,&sy,btn);
    if(sx<0)sx=0;
    if(sx>=LW)sx=LW-1;
    int y=(sy-40)/2;
    if(y<0)y=0;
    if(y>=LH)y=LH-1;
    *lx=sx; *ly=y;
}

/* map a logical click to a tile delta (dx,dy) from the avatar; 1 if on the map */
static int map_click_delta(int lx,int ly,int *dx,int *dy)
{
    int span=MAPSZ-2*MAP_INSET;                       /* terrain area (inset by MAP_INSET) */
    int rx=lx-MAPX-MAP_INSET, ry=ly-MAPY-MAP_INSET;
    if(rx<0||rx>=span||ry<0||ry>=span) return 0;
    *dx=(rx/16)-VT/2; *dy=(ry/16)-VT/2;              /* tmp tile -> delta from avatar */
    return 1;
}

/* Save the live game state to a ScummVM-loadable .000 by patching the template
 * (SAVE.000) we loaded at startup. We model the avatar's position here (objlist
 * actor 1); other regions are preserved from the template. Big-endian-safe — the
 * save writer encodes every multi-byte field byte-wise (engine/save.c). */
/* R)est: camp for ~6 game-hours; the party's HP + magic points are restored to
 * max and NPCs re-place to their now-current schedule. (U6 Rest/camp — no random
 * encounter yet.) Returns 1 (recompose). */
static int do_rest(world_t *w)
{
    w->clock += 6*60;                                   /* advance 6 game-hours */
    g_hour=clock_hour(w); g_minutes=clock_minutes(w);
    init_npcs();                                        /* re-seed NPCs at their new scheduled spots */
    for(int p=0;p<g_nparty;p++){ int a=g_party[p].actor;
        if(a>=0 && a<U6_MAX_ACTORS){
            if(g_amaxhp[a]) g_ahp[a]=g_amaxhp[a];
            g_amana[a]=g_amaxmana[a]; } }
    sync_party_hp();
    msgcat(0,"Thou campest. Rested, thy powers are restored.");
    return 1;
}
static int do_save(const world_t *w)
{
    if(!g_savetmpl || g_objlist_off<0){ msgcat(0,"No save template (SAVE.000)."); return 0; }
    uint8_t *d=g_savetmpl; int32_t off=g_objlist_off;
    /* FULL objlist state: every party member's position + HP/Str/Dex/Int + experience. The
     * avatar (actor 1) uses the world position; companions use their live actor positions. */
    for(int p=0;p<g_nparty;p++){ int a=g_party[p].actor;
        if(a<1||a>=U6_MAX_ACTORS) continue;
        int ax,ay,az;
        if(a==1){ ax=(int)w->px; ay=(int)w->py; az=0; }
        else { ax=g_actx[a]; ay=g_acty[a]; az=(g_actz[a]==0xff)?0:g_actz[a]; }
        sav_set_actor_pos  (d, off, a, ax, ay, az);
        sav_set_actor_stats(d, off, a, g_ahp[a], g_astr[a], g_adex[a], g_aint[a]);
        sav_set_actor_exp  (d, off, a, g_aexp[a]);
    }
    { int mod=clock_minutes(w); sav_set_clock(d, off, mod%60, mod/60); }   /* game clock */
    if(g_cc_done) sav_set_player(d, off, g_cg.avatar_sex, g_cc_portrait, g_cg.karma);  /* created-avatar identity */
    /* OBJECT-WORLD state: re-serialise every DIRTY object in place (U6's object DB is a flat
     * record set that loads by STATUS, so counts + following offsets are preserved — save.c):
     *   - a taken item (!alive)  -> flip its on-map record to in-inventory, owner = the Avatar;
     *   - a door/container (alive, frame changed) -> patch its frame byte. */
    { int32_t surf=sav_surface_off(d, g_savetmpl_len); int patched=0;
      if(surf>=0) for(int i=0;i<g_ow.nobjs;i++){ wobj_t*o=&g_wobj[i];
          if(!o->dirty) continue;
          if(!o->alive){
              if(o->origin!=WOBJ_PARENT_NONE){                  /* taken from a container: patch its in-container record */
                  wobj_t*c=&g_wobj[o->origin];
                  patched += sav_patch_contained_taken(d, g_savetmpl_len, surf, c->x, c->y, c->obj_n,
                                                        o->obj_n, o->quality, 1);
              } else patched += sav_patch_obj_taken(d, g_savetmpl_len, surf, o->x, o->y, o->obj_n, 1);
          }
          else { int frame=(int)o->tile - basetile(o->obj_n);
                 if(frame>=0) patched += sav_patch_obj_frame(d, g_savetmpl_len, surf, o->x, o->y, o->obj_n, frame); }
      }
      (void)patched; }
    if(sav_write_file("SAVED.000", d, g_savetmpl_len)==0){ msgcat(0,"Game saved."); return 1; }
    msgcat(0,"Save failed."); return 0;
}

/* Parse the FRONT-END art block (menu/title, intro scenes, TV, clock, moongate, montage portraits,
 * chargen questions, narration, BOOK.DAT text, gypsy/woods/storm/stones/climax) from `ap`, setting
 * the g_menu_/g_intro/g_gyp_/... pointers into that buffer. Used two ways: inline from the full pack
 * (TT/Falcon), and from a streamed U6FE.PAK (ST — loaded, run, then freed). Returns the advanced ap. */
static const uint8_t *parse_frontend(const uint8_t *ap)
{
        /* front-end art (U6WU): [u16 mw][u16 mh][mw*mh][u16 tw][u16 th][tw*th] — point into pak */
        g_menu_w=(ap[0]<<8)|ap[1]; ap+=2; g_menu_h=(ap[0]<<8)|ap[1]; ap+=2;
        g_menu_pix=ap; ap+=(int32_t)g_menu_w*g_menu_h;
        g_title_w=(ap[0]<<8)|ap[1]; ap+=2; g_title_h=(ap[0]<<8)|ap[1]; ap+=2;
        g_title_pix=ap; ap+=(int32_t)g_title_w*g_title_h;
        g_menu_lc=ap; ap+=48+256;                       /* per-scene title-screen 16 + LUT (ST; ignored on TT/Falcon) */
        /* intro scenes (U6WV): [u8 n] + n x [u8 type][768 pal][u16 w][u16 h][w*h pix]
         * and, for type 1 (pan), a trailing [u16 fw][fw*h fg] — all point into the pak. */
        g_nintro=*ap++; if(g_nintro>INTRO_MAX) g_nintro=INTRO_MAX;
        for(int i=0;i<g_nintro;i++){
            g_intro[i].type=*ap++;
            g_intro[i].pal=ap; ap+=768;                    /* per-scene 6-bit palette */
            g_intro[i].lc=ap; ap+=48+256;                  /* per-scene ST 16 + 256->16 LUT (shifter) */
            g_intro[i].w=(ap[0]<<8)|ap[1]; ap+=2; g_intro[i].h=(ap[0]<<8)|ap[1]; ap+=2;
            g_intro[i].pix=ap; ap+=(int32_t)g_intro[i].w*g_intro[i].h;
            if(g_intro[i].type==1){ int fw=(ap[0]<<8)|ap[1]; ap+=2;   /* only PAN scenes have an fg layer */
                g_intro[i].fg=ap; ap+=(int32_t)fw*g_intro[i].h; }
            else g_intro[i].fg=NULL; }
        g_scroll_w=(ap[0]<<8)|ap[1]; ap+=2; g_scroll_h=(ap[0]<<8)|ap[1]; ap+=2;
        g_scroll=ap; ap+=(int32_t)g_scroll_w*g_scroll_h;      /* shared narration parchment */
        /* living-room TV anchor: base + talking-mouth frames (front-end Phase 2 anim) */
        g_tv_bw=(ap[0]<<8)|ap[1]; ap+=2; g_tv_bh=(ap[0]<<8)|ap[1]; ap+=2;
        g_tv_bx=(ap[0]<<8)|ap[1]; ap+=2; g_tv_by=(ap[0]<<8)|ap[1]; ap+=2;
        g_tv_base=ap; ap+=(int32_t)g_tv_bw*g_tv_bh;
        g_tv_mw=(ap[0]<<8)|ap[1]; ap+=2; g_tv_mh=(ap[0]<<8)|ap[1]; ap+=2;
        g_tv_mx=(ap[0]<<8)|ap[1]; ap+=2; g_tv_my=(ap[0]<<8)|ap[1]; ap+=2;
        g_tv_nmouth=*ap++; if(g_tv_nmouth>4) g_tv_nmouth=4;
        for(int k=0;k<g_tv_nmouth;k++){ g_tv_mouth[k]=ap; ap+=(int32_t)g_tv_mw*g_tv_mh; }
        /* TV alt-channels: [u8 nchan][u8 fpc][u16 cw][u16 ch] + nchan*fpc x [cw*ch] (cw*ch==tv base) */
        g_tv_nchan=*ap++; if(g_tv_nchan>2) g_tv_nchan=2;
        g_tv_fpc=*ap++; if(g_tv_fpc>2) g_tv_fpc=2;
        { int ccw=(ap[0]<<8)|ap[1]; ap+=2; int cch=(ap[0]<<8)|ap[1]; ap+=2;
          for(int c=0;c<g_tv_nchan;c++) for(int fr=0;fr<g_tv_fpc;fr++){
            g_tv_chan[c][fr]=ap; ap+=(int32_t)ccw*cch; } }
        /* LED clock digits (0-9 + blank) drawn over the blanked cells */
        g_clk_x=(ap[0]<<8)|ap[1]; ap+=2; g_clk_y=(ap[0]<<8)|ap[1]; ap+=2;
        g_ndigit=*ap++; if(g_ndigit>11) g_ndigit=11;
        for(int k=0;k<g_ndigit;k++){ g_dig_w[k]=(ap[0]<<8)|ap[1]; ap+=2; g_dig_h[k]=(ap[0]<<8)|ap[1]; ap+=2;
            g_digit[k]=ap; ap+=(int32_t)g_dig_w[k]*g_dig_h[k]; }
        /* moongate rise assets (type-2 scene): gate (rises) + ground cover */
        g_gw=(ap[0]<<8)|ap[1]; ap+=2; g_gh=(ap[0]<<8)|ap[1]; ap+=2; g_gx=(ap[0]<<8)|ap[1]; ap+=2;
        g_gate=ap; ap+=(int32_t)g_gw*g_gh;
        g_cvw=(ap[0]<<8)|ap[1]; ap+=2; g_cvh=(ap[0]<<8)|ap[1]; ap+=2;
        g_cvx=(ap[0]<<8)|ap[1]; ap+=2; g_cvy=(ap[0]<<8)|ap[1]; ap+=2;
        g_cover=ap; ap+=(int32_t)g_cvw*g_cvh;
        /* character-creation portraits (MONTAGE 0..11): [u8 n][u16 pw][u16 ph] + n x [pw*ph] */
        g_cc_nport=*ap++; if(g_cc_nport>12) g_cc_nport=12;
        g_cc_pw=(ap[0]<<8)|ap[1]; ap+=2; g_cc_ph=(ap[0]<<8)|ap[1]; ap+=2;
        for(int k=0;k<g_cc_nport;k++){ g_cc_port[k]=ap; ap+=(int32_t)g_cc_pw*g_cc_ph; }
        /* gypsy questions (from U.EXE): [u8 nq] + nq x [u16 len][len bytes] (not NUL-term) */
        g_cc_nq=*ap++; if(g_cc_nq>28) g_cc_nq=28;
        for(int q=0;q<g_cc_nq;q++){ g_cc_qlen[q]=(ap[0]<<8)|ap[1]; ap+=2;
            g_cc_qtext[q]=(const char*)ap; ap+=g_cc_qlen[q]; }
        /* intro narration (from U.EXE): [u8 np] + np x [u16 len][len bytes] (not NUL-term). Use the
         * pack text only if all 15 pages are present; else fall back to our committed paraphrase. */
        { int np=*ap++; g_intro_have=(np>=INTRO_NPAGES);
          for(int q=0;q<np;q++){ int L=(ap[0]<<8)|ap[1]; ap+=2;
              if(g_intro_have && q<INTRO_NPAGES){ g_intro_text[q]=(const char*)ap; g_intro_tlen[q]=L; }
              ap+=L; } }
        if(!g_intro_have)
            for(int q=0;q<INTRO_NPAGES;q++){ g_intro_text[q]=g_intro_fb[q]; g_intro_tlen[q]=(int)strlen(g_intro_fb[q]); }
        /* (BOOK.DAT text moved to the RESIDENT core section U6BK — the ST frees this front-end block
         * with U6FE, which nulled in-game reading. Parsed in the core-pack loader instead.) */
        /* gypsy "casting" scene: [768 pal5][u16 gw][u16 gh][gw*gh][u16 sw][u16 sh][sw*sh scroll] */
        g_gyp_pal=ap; ap+=768; g_gyp_lc=ap; ap+=48+256;
        g_gyp_w=(ap[0]<<8)|ap[1]; ap+=2; g_gyp_h=(ap[0]<<8)|ap[1]; ap+=2;
        g_gyp_scene=ap; ap+=(int32_t)g_gyp_w*g_gyp_h;
        g_gscroll_w=(ap[0]<<8)|ap[1]; ap+=2; g_gscroll_h=(ap[0]<<8)|ap[1]; ap+=2;
        g_gyp_scroll=ap; ap+=(int32_t)g_gscroll_w*g_gscroll_h;
        /* raw GYPSY.SHP shape table (nuvie image_load_all): [u16 nsh] + per shape [u16 w][u16 h][w*h].
         * The engine composites the casting scene + pour animation from these (jar 16, jar_liquid
         * 9..15, vial glass 20/23, vial_liquid 0x42+.., arm 1..6, hand 9+.., pour) by shape number. */
        g_ngsh=(ap[0]<<8)|ap[1]; ap+=2; if(g_ngsh>NGSH) g_ngsh=NGSH;
        for(int i=0;i<g_ngsh;i++){ int w=(ap[0]<<8)|ap[1]; ap+=2; int h=(ap[0]<<8)|ap[1]; ap+=2;
            g_gsh_w[i]=w; g_gsh_h[i]=h; g_gsh[i]=(w>0&&h>0)?ap:0; ap+=(int32_t)w*h; }
        /* woods forest-path lead-in: [768 pal4][u16 ww][u16 wh][ww*wh panorama] */
        g_woods_pal=ap; ap+=768; g_woods_lc=ap; ap+=48+256;
        g_woods_w=(ap[0]<<8)|ap[1]; ap+=2; g_woods_h=(ap[0]<<8)|ap[1]; ap+=2;
        g_woods=ap; ap+=(int32_t)g_woods_w*g_woods_h;
        /* storm window scene: [768 pal2][u16 bw][u16 bh][base][u8 nbolt]+nbolt[u16 w][u16 h][wh]
         * [u8 nstr][u16 sx][u16 sy]+nstr[u16 w][u16 h][wh] */
        g_storm_pal=ap; ap+=768; g_storm_lc=ap; ap+=48+256;
        g_storm_w=(ap[0]<<8)|ap[1]; ap+=2; g_storm_h=(ap[0]<<8)|ap[1]; ap+=2;
        g_storm_base=ap; ap+=(int32_t)g_storm_w*g_storm_h;
        g_storm_nbolt=*ap++; if(g_storm_nbolt>8) g_storm_nbolt=8;
        for(int k=0;k<g_storm_nbolt;k++){ g_storm_boltw[k]=(ap[0]<<8)|ap[1]; ap+=2;
            g_storm_bolth[k]=(ap[0]<<8)|ap[1]; ap+=2; g_storm_bolt[k]=ap;
            ap+=(int32_t)g_storm_boltw[k]*g_storm_bolth[k]; }
        g_storm_nstr=*ap++; if(g_storm_nstr>5) g_storm_nstr=5;
        g_storm_sx=(ap[0]<<8)|ap[1]; ap+=2; g_storm_sy=(ap[0]<<8)|ap[1]; ap+=2;
        for(int k=0;k<g_storm_nstr;k++){ g_storm_strw[k]=(ap[0]<<8)|ap[1]; ap+=2;
            g_storm_strh[k]=(ap[0]<<8)|ap[1]; ap+=2; g_storm_str[k]=ap;
            ap+=(int32_t)g_storm_strw[k]*g_storm_strh[k]; }
        /* forest reveal: [u16 fw][u16 fh][forest base][u16 ow][u16 oh][window overlay] */
        g_forest_w=(ap[0]<<8)|ap[1]; ap+=2; g_forest_h=(ap[0]<<8)|ap[1]; ap+=2;
        g_forest_base=ap; ap+=(int32_t)g_forest_w*g_forest_h;
        g_win_ow=(ap[0]<<8)|ap[1]; ap+=2; g_win_oh=(ap[0]<<8)|ap[1]; ap+=2;
        g_win_ov=ap; ap+=(int32_t)g_win_ow*g_win_oh;
        /* stones pickup: [u16 hw][u16 hh][hand][u16 ow][u16 oh][obs][u16 bw][u16 bh][bg5] */
        g_stn_hw=(ap[0]<<8)|ap[1]; ap+=2; g_stn_hh=(ap[0]<<8)|ap[1]; ap+=2;
        g_stn_hand=ap; ap+=(int32_t)g_stn_hw*g_stn_hh;
        g_stn_ow=(ap[0]<<8)|ap[1]; ap+=2; g_stn_oh=(ap[0]<<8)|ap[1]; ap+=2;
        g_stn_obs=ap; ap+=(int32_t)g_stn_ow*g_stn_oh;
        g_stn_b5w=(ap[0]<<8)|ap[1]; ap+=2; g_stn_b5h=(ap[0]<<8)|ap[1]; ap+=2;
        g_stn_bg5=ap; ap+=(int32_t)g_stn_b5w*g_stn_b5h;
        /* Avatar-into-gate climax: [u8 nav][u16 aw][u16 ah] + nav x [aw*ah] */
        g_av_n=*ap++; if(g_av_n>20) g_av_n=20;
        for(int k=0;k<g_av_n;k++){
            g_av_ox[k]=(ap[0]<<8)|ap[1]; ap+=2; g_av_oy[k]=(ap[0]<<8)|ap[1]; ap+=2;
            g_av_w[k] =(ap[0]<<8)|ap[1]; ap+=2; g_av_h[k] =(ap[0]<<8)|ap[1]; ap+=2;
            g_av_frame[k]=ap; ap+=(int32_t)g_av_w[k]*g_av_h[k]; }
        return ap;
}

/* ===================== M10: music — OPL2 .M sequencer -> MIDI OUT ===================== */
/* MIDI-OUT backend: the portable sequencer (engine/music.c) calls these. The sequencer is
 * ticked from a Timer interrupt (below), where Midiws (XBIOS) is unsafe — so we QUEUE the
 * MIDI bytes (plat_midi_queue) and the ISR drains them to the ACIA. Later the OPL2->DMA and
 * YM2149-PSG backends implement the same music_backend_t. */
static void mus_program (int ch,int prog){ uint8_t x[2]={(uint8_t)(0xC0|(ch&15)),(uint8_t)(prog&127)}; plat_midi_queue(x,2); }
static void mus_note_on (int ch,int n,int v){ uint8_t x[3]={(uint8_t)(0x90|(ch&15)),(uint8_t)(n&127),(uint8_t)(v&127)}; plat_midi_queue(x,3); }
static void mus_note_off(int ch,int n){ uint8_t x[3]={(uint8_t)(0x80|(ch&15)),(uint8_t)(n&127),0}; plat_midi_queue(x,3); }
static void mus_reset   (void){ plat_midi_all_off(); }   /* queues all-notes-off (ISR-safe) */
static const music_backend_t g_midi_be = { mus_program, mus_note_on, mus_note_off, mus_reset };
static music_seq_t g_music; static int g_music_on;
static int g_music_be_kind;   /* which sequencer backend is live: 0 none, 1 MIDI, 2 PSG */

/* PSG backend (plain ST): fold U6's 9 OPL channels into the YM2149's 3 square voices.
 * Voice allocation = reuse the channel's voice if it has one, else a free voice, else
 * steal round-robin. No timbres (square only); velocity -> 4-bit volume. Called from the
 * Timer ISR, writing the PSG registers directly (ISR-safe). */
static struct { int ch, note; } g_psgv[3];
static int g_psg_rr;
static int psg_find(int ch){ for(int v=0;v<3;v++) if(g_psgv[v].ch==ch) return v; return -1; }
static void psg_be_program(int ch,int prog){ (void)ch;(void)prog; }
static void psg_be_note_on(int ch,int note,int vel){
    int v=psg_find(ch);
    if(v<0){ for(int i=0;i<3;i++) if(g_psgv[i].ch<0){ v=i; break; } }
    if(v<0){ v=g_psg_rr; g_psg_rr=(g_psg_rr+1)%3; }
    g_psgv[v].ch=ch; g_psgv[v].note=note;
    int vol=vel>>3; if(vol<1) vol=1;
    plat_psg_voice(v, note, vol);
}
static void psg_be_note_off(int ch,int note){ (void)note;
    int v=psg_find(ch);
    if(v>=0){ plat_psg_voice_off(v); g_psgv[v].ch=-1; g_psgv[v].note=-1; }
}
/* reset() may be called from the sequencer tick (ISR) if a song ends, so silence via the
 * direct per-voice writes (ISR-safe), NOT plat_psg_silence (which uses Supexec/XBIOS). */
static void psg_be_reset(void){ for(int v=0;v<3;v++){ plat_psg_voice_off(v); g_psgv[v].ch=-1; g_psgv[v].note=-1; } }
static const music_backend_t g_psg_be = { psg_be_program, psg_be_note_on, psg_be_note_off, psg_be_reset };

/* Timer-A ISR callback (~60 Hz, steady, frame-rate independent): advance the .M sequencer
 * one tick (which drives whichever backend is live), then — for MIDI only — push a bounded
 * burst of queued MIDI to the ACIA. Runs at interrupt level: everything it calls is integer
 * + ISR-safe (no TOS). 52 bytes/tick ~= the 31250-baud MIDI budget per 60 Hz tick. */
static void music_isr_tick(void){
    if(g_music_on) music_seq_tick(&g_music, 1);
    if(g_music_be_kind == 1) plat_midi_drain(52);
}

/* .M basename (no ext, case-insensitive) -> MIDI.DAT song order (listed 1..9,0,-,=). */
static int mus_song_index(const char *fn){
    static const char *O[12]={"ULTIMA","BOOTUP","INTRO","CREATE","FOREST","HORNPIPE",
                              "ENGAGE","STONES","DUNGEON","BRIT","GARGOYLE","END"};
    for(int i=0;i<12;i++){ int k=0; while(fn[k]&&O[i][k]&&((fn[k]&~0x20)==(O[i][k]&~0x20))) k++;
        if(!fn[k] && !O[i][k]) return i; }
    return -1;
}
/* Parse the idx-th song's (ch/prog) pairs from a loaded MIDI.DAT text buffer -> map (0-based). */
static int mus_parse_map(const uint8_t *dat, int32_t len, int idx, music_map_t *map){
    int song=-1, np=0; int32_t i=0;
    while(i<len && np==0){
        int32_t ls=i; while(i<len && dat[i]!='\n') i++;
        int32_t op=-1,cl=-1,sl=0;
        for(int32_t k=ls;k<i;k++){ if(dat[k]=='(')op=k; else if(dat[k]==')'&&op>=0)cl=k; else if(dat[k]=='/'&&op>=0)sl=1; }
        i++;
        if(op<0||cl<0||!sl) continue;
        if(++song != idx) continue;
        for(int32_t k=op+1;k<cl && np<16;){
            int ch=0,pr=0;
            while(k<cl && dat[k]>='0'&&dat[k]<='9') ch=ch*10+(dat[k++]-'0');
            if(k<cl && dat[k]=='/'){ k++; while(k<cl && dat[k]>='0'&&dat[k]<='9') pr=pr*10+(dat[k++]-'0'); }
            if(ch>0){ map[np].ch=(uint8_t)((ch-1)&15); map[np].prog=(uint8_t)((pr-1)&127); np++; }
            while(k<cl && dat[k]!=',') k++; if(k<cl) k++;
        }
    }
    return np;
}
/* ---- music manager: runtime backend selection + MIDI toggle -------------------------
 * One song, three possible sinks chosen at runtime from the machine + a user preference:
 *   MIDI  -> MIDI OUT (BASE.M + MIDI.DAT), any Atari + an external synth (MT-32)
 *   DMA   -> 8-bit DMA sound (BASE.PCM, host pre-rendered), STE / TT / Falcon
 *   PSG   -> YM2149 3-voice square (BASE.M re-voiced), any Atari — the plain-ST default
 * The engine calls music_play_song(base); it never picks a backend itself. */
static int  g_music_pref;       /* MIDI.CFG: 0 auto, 1 MIDI, 2 DMA, 3 PSG          */
static int  g_music_dma_ok;     /* 8-bit DMA sound present (STE/MegaSTE/TT/Falcon) */
static int  g_music_active;     /* backend chosen for the current song: 1 MIDI 2 DMA 3 PSG */
static char g_music_cur[16];    /* current song base name ("" = none)             */
static int      g_music_group = -1;  /* current context music group (-1 = none); see music_area_group */
static uint32_t g_music_rng;         /* LCG state for the random wandering-track pick  */

/* build "BASE.EXT" (8.3) into dst */
static void music_name(char *dst, const char *base, const char *ext){
    int i=0; while(base[i] && i<12){ dst[i]=base[i]; i++; }
    for(int k=0; ext[k]; k++) dst[i++]=ext[k];
    dst[i]=0;
}

/* Start a sequencer backend (MIDI or PSG) for a .M file, driven by the steady 60 Hz Timer-A
 * tick (tempo independent of the frame rate; plays through blocking waits). `kind` = 1 MIDI /
 * 2 PSG; `load_map` pulls the MT-32 map from MIDI.DAT (MIDI only). Caller has stopped audio. */
static void music_play_seq(const char *mfile, const music_backend_t *be, int kind, int load_map){
    int32_t rl=0; uint8_t *raw=(uint8_t*)plat_load_file(mfile, 1, &rl);
    if(!raw || rl<=4) return;
    int32_t usz=u6lzw_unpacked_size(raw, rl); if(usz<=0) return;
    uint8_t *song=(uint8_t*)plat_alloc_fast((uint32_t)usz); if(!song) return;
    int32_t got=u6lzw_decompress(raw, rl, song, usz); if(got<=0) return;
    music_map_t map[16]; int nmap=0;
    if(load_map){ int32_t dl=0; uint8_t *dat=(uint8_t*)plat_load_file("MIDI.DAT", 1, &dl);
      if(dat && dl>0){ char base[16]; int i=0; for(const char*p=mfile; *p && *p!='.'&&i<15; p++) base[i++]=*p; base[i]=0;
                       int si=mus_song_index(base); if(si>=0) nmap=mus_parse_map(dat, dl, si, map); } }
    if(kind==1) plat_midi_init();
    music_seq_start(&g_music, song, got, map, nmap, be);
    g_music_on=1; g_music_be_kind=kind;
    plat_timer_start(music_isr_tick, 60);
}

/* Stop + silence ALL backends (song change or clean exit). Uninstalls the Timer-A ISR FIRST
 * — its vector points into our program memory, which TOS frees on exit, so a later 60 Hz tick
 * would jump into reused memory and crash the machine. Then silence whichever backend was live:
 * MIDI = explicit note-off per sounding note (surest MT-32 kill) + CC123/CC120 panic + flush;
 * PSG = silence the chip. (Guarded on the live backend so DMA mode never touches the MIDI ACIA.)
 * A hard kill / power-off can't run this — a dead program can't send MIDI, so an external synth
 * holds its last note until reset. */
static void music_stop_all(void){
    plat_timer_stop();
    g_music_on = 0;
    if(g_music_be_kind == 1){          /* MIDI OUT was live */
        for(int i=0;i<9;i++) if(g_music.note[i]>=0){ mus_note_off(g_music.mch[i], g_music.note[i]); g_music.note[i]=-1; }
        plat_midi_all_off();
        plat_midi_flush();
    } else if(g_music_be_kind == 2){   /* PSG was live */
        plat_psg_silence();
        for(int v=0;v<3;v++){ g_psgv[v].ch=-1; g_psgv[v].note=-1; }
    }
    g_music_be_kind = 0;
    plat_dma_music_stop();             /* stop DMA (no-op if not playing) */
}

/* play song `base` (e.g. "BRIT") through the runtime-selected backend */
static void music_play_song(const char *base){
    music_stop_all();
    { int i=0; while(base[i] && base[i]!='.' && i<15){ g_music_cur[i]=base[i]; i++; } g_music_cur[i]=0; }
    if(!g_music_cur[0]) return;
    /* resolve the backend: explicit preference wins; auto = DMA if present, else PSG (MIDI is opt-in). */
    int use = (g_music_pref==1) ? 1 :               /* MIDI */
              (g_music_pref==3) ? 3 :               /* PSG  */
                                  (g_music_dma_ok?2:3);   /* DMA-pref or auto: DMA if present, else PSG */
    g_music_active = use;
    char nm[20];
    if(use==1){ music_name(nm, g_music_cur, ".M");   music_play_seq(nm, &g_midi_be, 1, 1); }
    else if(use==2){ music_name(nm, g_music_cur, ".PCM"); plat_dma_music_start(nm); }
    else { music_name(nm, g_music_cur, ".M"); music_play_seq(nm, &g_psg_be, 2, 0); }   /* PSG */
}

/* set the backend preference at runtime (0 auto / 1 MIDI / 2 DMA / 3 PSG) and re-cue the current
 * song. The working control today is the MIDI.CFG default (authentic: the original chose the synth
 * in config, not in-game); this is the API the front-end settings screen will call to switch live. */
__attribute__((unused))
static void music_set_pref(int pref){
    if(pref<0 || pref>3) pref=0;
    if(pref == g_music_pref) return;
    g_music_pref = pref;
    if(g_music_cur[0]){ char b[16]; int i=0; while(g_music_cur[i]){ b[i]=g_music_cur[i]; i++; } b[i]=0; music_play_song(b); }
}
static int music_active_backend(void){ return g_music_active; }   /* 1 MIDI 2 DMA 3 PSG */

/* detect DMA + read the backend preference from MIDI.CFG (byte0 '1'=MIDI, '2'=DMA, '3'=PSG;
 * absent/other => auto: DMA if present, else PSG). MIDI stays opt-in since most players have no
 * MT-32; the owner selects it. Also silence the PSG so a leftover tone doesn't drone at boot. */
static void music_manager_init(void){
    g_music_dma_ok = plat_dma_available();
    g_music_cur[0] = 0;
    g_music_group = -1;                                   /* no context group chosen yet    */
    g_music_rng = (uint32_t)plat_ticks_200hz() | 1u;      /* vary the wandering pick per boot */
    plat_psg_init();
    for(int v=0;v<3;v++){ g_psgv[v].ch=-1; g_psgv[v].note=-1; }
    int32_t cl=0; void *c=plat_load_file("MIDI.CFG", 1, &cl);
    char b = (c && cl>0) ? ((const char*)c)[0] : 0;
    g_music_pref = (b=='1')?1 : (b=='2')?2 : (b=='3')?3 : 0;
}

/* ---- per-area music: pick the song GROUP for the current game context, switch on change ----
 * Faithful to the original's selection (DERIVED by READING Nuvie's SoundManager LoadNativeU6Songs,
 * not transcribed): U6 sorts its songs into GROUPS and plays a track from the group the context
 * calls for, starting on the "random" group:
 *   random   -> overworld AND towns (no distinction): a RANDOM pick of four wandering themes
 *               {BRIT "Rule Britannia", FOREST "Wanderer", STONES "Stones", ULTIMA "Ultima Theme"}
 *   combat   -> ENGAGE   "Engagement and Melee"
 *   dungeon  -> DUNGEON  "Dungeon"
 *   boat     -> HORNPIPE "Captain Johne's Hornpipe" (aboard a ship)
 *   gargoyle -> GARGOYLE "Audchar Gargl Zenmur" (the gargoyle lands)
 * We switch groups on a context CHANGE and pick a track from the group (random within the wandering
 * pool via the deterministic RNG seam; the others are one track). See docs/rules/music_areas.md.
 * NOTE: the original also re-picks a NEW random wandering track when the current one ends; we loop
 * the current track seamlessly instead (the DMA loop work) — pool rotation is a future refinement.
 * Only combat is context-detectable today; boat/dungeon/gargoyle wait on those systems. */
static int g_music_forced;   /* a SONG.REQ / scripted request pins the song (skip auto-switch) */

enum { MG_RANDOM=0, MG_COMBAT, MG_DUNGEON, MG_BOAT, MG_GARGOYLE };
static const char *const MUS_WANDER[4] = { "BRIT", "FOREST", "STONES", "ULTIMA" };

/* which music group the current game context calls for */
static int music_area_group(void){
    if(g_combat_mode) return MG_COMBAT;    /* battle                                 */
    /* future context hooks (need the systems first):
       if(player_on_boat)   return MG_BOAT;
       if(in_dungeon)       return MG_DUNGEON;
       if(in_gargoyle_land) return MG_GARGOYLE; */
    return MG_RANDOM;                       /* overworld + towns: a wandering theme    */
}
/* pick a track base name from a group (random within the wandering pool) */
static const char *music_group_pick(int g){
    switch(g){
      case MG_COMBAT:   return "ENGAGE";
      case MG_DUNGEON:  return "DUNGEON";
      case MG_BOAT:     return "HORNPIPE";
      case MG_GARGOYLE: return "GARGOYLE";
      default:                              /* MG_RANDOM: one of four, seeded high-bit pick */
        g_music_rng = g_music_rng*1103515245u + 12345u;
        return MUS_WANDER[(g_music_rng >> 24) & 3];
    }
}
static void music_update_area(void){
    if(g_music_forced) return;             /* a forced song (test hook / event) owns the music */
    int g = music_area_group();
    if(g != g_music_group){                /* context changed group -> pick a track + play  */
        g_music_group = g;
        music_play_song(music_group_pick(g));
    }
}

int main(void)
{
    platform_caps_t caps; platform_detect(&caps);
    Cconws("u6e M6 authentic UI: loading...\r\n");
    if (!plat_mem_init(&caps)) { Cconws("mem init failed\r\n"); Crawcin(); return 1; }
    plat_dma_init(&caps);       /* M10: grab an ST-RAM ring for DMA-sound music (STE/TT/Falcon) */
    music_manager_init();       /* M10: detect DMA + read the MIDI toggle default (MIDI.CFG) */

#ifdef BENCH
    /* M10 OPL2->DMA feasibility probe (gated on SBENCH.REQ): time the software FM
     * synth's steady-state throughput on THIS machine — how many mono samples/sec
     * it can render with all 9 voices sounding. Needs no game data (synthetic OPL
     * stream). BENCH.BIN = 4 big-endian u32: samples, 200Hz-ticks, samples/sec, rate.
     * Real-time is feasible iff samples/sec comfortably exceeds the chosen DMA rate. */
    {   int32_t sbl=0; void *sb=plat_load_file("SBENCH.REQ",1,&sbl);
        if(sb){
            /* one sustaining instrument (BRIT's) on all 9 voices, then loop on delays */
            static const uint8_t INST[11]={0x61,0x1b,0x62,0x56,0x00,0x21,0x00,0x84,0x55,0x00,0x0e};
            static const uint8_t IDX[9]={1,2,3,4,5,6,7,9,10};   /* non-rest F-nums    */
            uint8_t strm[64]; int p=0;
            strm[p++]=0x83; strm[p++]=0x00; for(int k=0;k<11;k++) strm[p++]=INST[k];
            for(int c=0;c<9;c++){ strm[p++]=(uint8_t)(0x70|c); strm[p++]=0x00; }
            for(int c=0;c<9;c++){ strm[p++]=(uint8_t)(0x20|c); strm[p++]=(uint8_t)(0x80|IDX[c]); }
            strm[p++]=0xE0; strm[p++]=0x82; strm[p++]=0xFF; strm[p++]=0xF0;
            int os = (sbl>0)? ((const char*)sb)[0]-'0' : 1; if(os<1||os>8) os=1;   /* SBENCH.REQ byte0='1'.. */
            int32_t rate = 25033;
            int16_t buf[512];
            opl_start(strm, p, rate, os);
            opl_render(buf,512);                        /* warm up (prime voices)      */
            uint32_t t0=plat_ticks_200hz(); int32_t done=0;
            while(done<20000){ opl_render(buf,512); done+=512; }
            uint32_t ticks=plat_ticks_200hz()-t0; if(!ticks) ticks=1;
            uint32_t sps=(uint32_t)((uint64_t)done*200/ticks);
            uint32_t r[4]={ (uint32_t)done, ticks, sps, (uint32_t)rate };
            plat_file bf=plat_fcreate("BENCH.BIN");
            if(bf!=PLAT_BADFILE){ plat_fwrite(bf,r,(int32_t)sizeof r); plat_fclose(bf);}
            dbg_num("synth OS=", os);
            dbg_num("synth samples=", (int)done);
            dbg_num("synth 200Hz-ticks=", (int)ticks);
            dbg_num("synth samples/sec=", (int)sps);
            dbg_num("target DMA rate=", (int)rate);
            Cconws("synth bench done\r\n"); return 0;
        } }
#endif

    int32_t n=0;
    /* PACK_NAME defaults to the full pack; the ST build (-DPACK_NAME) loads a slim pack (U6ST.PAK,
     * front-end art omitted) so it fits a real 4 MB ST. See docs/design/palette_lowcolor.md. */
    #ifndef PACK_NAME
    #define PACK_NAME "U6.PAK"
    #endif

    /* VIDEO.CFG (M8): byte0 'T'/'t' requests the Falcon 16-bit TRUECOLOR present; absent/other =
     * the indexed default. Read BEFORE rdr_init (which sets the video mode). No-op on the ST
     * shifter + TT backends (they stay indexed). */
    {   int32_t vl=0; void *vc=plat_load_file("VIDEO.CFG",1,&vl);
        char vb=(vc&&vl>0)?((const char*)vc)[0]:0;
        rdr_prefer_truecolor(vb=='T'||vb=='t'); }

#ifdef ST_STREAM
    /* ST: STREAM the front-end (menu/intro/chargen) off the hard drive BEFORE the gameplay core
     * loads — the 1.35 MB U6FE.PAK can't coexist with the core on a 4 MB ST. rdr_init first
     * (persistent buffers), then a scoped chip region holds U6FE just for this phase; the reset
     * frees it before the core + libs load. (platform/platmem.h; docs/design/palette_lowcolor.md) */
    if (!rdr_init(&caps)) { Cconws("no renderer\r\n"); Crawcin(); return 1; }
    plat_input_init();
    g_bb=rdr_backbuffer(); g_W=rdr_width();
    {   plat_mark fe_mark = plat_chip_mark();
        int32_t felen=0; uint8_t *fe=(uint8_t*)plat_load_file("U6FE.PAK",1,&felen);
        if (fe && felen>2356 && fe[0]=='U'&&fe[1]=='6'&&fe[2]=='F'&&fe[3]=='E'){
            /* U6FE = [4 magic][48 pal][256 lut][2048 font][art]. Activate the game 16 so the
             * front-end renders in colour (the core's U6WW isn't loaded yet). */
            rdr_set_lowcolor(fe+4, fe+4+48);
            g_fe_base_lc = fe+4;                        /* base 16 to restore between cutscene scenes */
            g_font = fe+4+48+256;                       /* font (2048 bytes) for text */
            parse_frontend(fe+4+48+256+2048);           /* set g_menu/g_intro/g_gyp/... into fe */
            int32_t ml=0;
            void*ireq=plat_load_file("INTRO.REQ",1,&ml);   if(ireq) run_intro();
            void*creq=plat_load_file("CHARGEN.REQ",1,&ml); if(creq) chargen_capture(ml>0?((uint8_t*)creq)[0]:'1');
            void*mreq=plat_load_file("MENU.REQ",1,&ml);    if(mreq) run_menu();
        }
        {   int32_t mil=0; if(plat_load_file("MEMINFO.BIN",1,&mil)){ plat_mem_info_t mi; plat_mem_get_info(&mi);
                dbg_num("MEM fe_loaded=", fe?1:0);
                dbg_num("MEM with_fe_used KB=", (int)(mi.chip_used/1024)); } }
        plat_chip_reset(fe_mark);                       /* FREE U6FE — reclaim ~1.35 MB for gameplay */
        g_font=NULL;                                     /* front-end font pointer dangles; core re-sets it.
                                                          * (BOOK text is now in the resident core U6BK, so
                                                          * it survives this free — no g_nbook reset.) */
    }
#endif
    uint8_t *pak=(uint8_t*)plat_load_file(PACK_NAME,1,&n);   /* 8.3 name (real-TOS safe) */
    /* M9: the pack magic SELECTS the game (GameDef). The same binary accepts any registered
     * game's pack; U6's magic is "U6WT". SE/MD register their own magics as they land. */
    const GameDef *gd = (pak && n>=26) ? gamedef_for_magic((const char*)pak) : NULL;
    if (!gd){
        Cconws("cannot load asset pack (unknown game magic)\r\n"); Crawcin(); return 1; }
    uint32_t map_len=((uint32_t)pak[4]<<24)|(pak[5]<<16)|(pak[6]<<8)|pak[7];
    uint32_t chunks_len=((uint32_t)pak[8]<<24)|(pak[9]<<16)|(pak[10]<<8)|pak[11];
    uint32_t ntiles=((uint32_t)pak[12]<<24)|(pak[13]<<16)|(pak[14]<<8)|pak[15];
    g_nobjs=(int32_t)(((uint32_t)pak[16]<<24)|(pak[17]<<16)|(pak[18]<<8)|pak[19]);
    int32_t startx=(pak[20]<<8)|pak[21], starty=(pak[22]<<8)|pak[23];
    g_start_min=(pak[24]<<8)|pak[25];          /* game-time of day from the save clock (U6WH) */
    const uint8_t *map=pak+26, *chunks=map+map_len;
    g_tileset=chunks+chunks_len;
    const uint8_t *palette=g_tileset+(int32_t)ntiles*256, *passable=palette+768;
    g_objs=passable+256;
    g_font=g_objs+(int32_t)g_nobjs*14;                           /* 14 bytes/object (U6WT) */
    g_paper=g_font+2048;
    g_nport=(g_paper[64000]<<8)|g_paper[64001];
    g_portraits=g_paper+64000+2;
    const uint8_t *after_actors;
    {   const uint8_t *demo=g_portraits+(int32_t)g_nport*PW*PH;  /* demo NPC block */
        g_demo_npc=(demo[0]<<8)|demo[1];
        g_script_len=(demo[2]<<8)|demo[3];
        g_script=demo+4; }
    {   const uint8_t *ab=g_script+g_script_len;                 /* actor block */
        int nact=(ab[0]<<8)|ab[1]; ab+=2;
        const uint8_t *atile=ab; ab+=(int32_t)nact*2;           /* appearance tiles  */
        const uint8_t *apos =ab; ab+=(int32_t)nact*5;           /* live positions x,y,z (U6WB) */
        const uint8_t *astat=ab; ab+=(int32_t)nact*5;           /* hp,str,dex,int,mana (U6WC, 5B/actor) */
        g_nsparty=ab[0]; ab+=1;                                 /* saved party list (U6WC) */
        const uint8_t *spl=ab; ab+=g_nsparty;
        int sched_len=(ab[0]<<8)|ab[1]; ab+=2;
        const uint8_t *sched=ab;
        for(int i=0;i<U6_MAX_ACTORS;i++){
            g_atile[i]=(i<nact)?(uint16_t)((atile[i*2]<<8)|atile[i*2+1]):0xffff;
            if(i<nact){ const uint8_t *p=apos+i*5;
                g_actx[i]=(uint16_t)((p[0]<<8)|p[1]);
                g_acty[i]=(uint16_t)((p[2]<<8)|p[3]);
                g_actz[i]=p[4];
                const uint8_t *s=astat+i*5;
                g_ahp[i]=s[0]; g_astr[i]=s[1]; g_adex[i]=s[2]; g_aint[i]=s[3]; g_amaxhp[i]=s[0];
                g_amaxmana[i]=(uint8_t)magic_max_mana(s[3], i==1);      /* 2xInt (Avatar) / Int/2 (companion), §3 */
                g_amana[i]=(s[4]==0xFF)?g_amaxmana[i]:s[4];  /* current mana from the save (objlist 0x13f1); 0xFF => full */
                g_afood[i]=FOOD_START;                       /* hunger model: begin well-fed (not save-backed yet) */
            } else { g_actz[i]=0xff; g_ahp[i]=g_astr[i]=g_adex[i]=g_aint[i]=0; g_amana[i]=g_amaxmana[i]=0; }
        }
        if(g_nsparty>MAXPARTY) g_nsparty=MAXPARTY;
        for(int i=0;i<g_nsparty;i++) g_sparty[i]=spl[i];
        if(sched_len>0) u6actor_load_schedule(g_actors,sched,sched_len);
        g_savestate=(nact>1 && g_actz[1]!=0xff);   /* live avatar position => save state */
        after_actors=sched+sched_len; }

    /* object verbs section: [u32 look_len][look payload][gettable 1024] */
    const uint8_t *gettable;
    {   const uint8_t *lk=after_actors;
        uint32_t look_len=((uint32_t)lk[0]<<24)|(lk[1]<<16)|(lk[2]<<8)|lk[3]; lk+=4;
        const char *blob=(const char*)lk;
        gettable=lk+look_len;
        int j=0; const uint8_t *p=(const uint8_t*)blob;       /* run-filled name table */
        for(int i=0;i<2048 && p+2<gettable;){
            i=p[0]|(p[1]<<8); if(i>=2048) break;
            const char *s=(const char*)(p+2);
            int len=0; while(s+len<(const char*)gettable && s[len]) len++;
            for(;j<=i;j++) g_looktbl[j]=s;
            p+=len+3;
        }
        for(;j<2048;j++) g_looktbl[j]=g_looktbl[0]?g_looktbl[0]:""; }

    /* build the mutable world-object store from the packed records */
    {   int nw=g_nobjs<MAXWOBJ?(int)g_nobjs:MAXWOBJ;
        for(int i=0;i<nw;i++){ const uint8_t*o=g_objs+(int32_t)i*14;   /* 14B records (U6WT) */
            g_wobj[i].x=(uint16_t)((o[0]<<8)|o[1]); g_wobj[i].y=(uint16_t)((o[2]<<8)|o[3]);
            g_wobj[i].tile=(uint16_t)((o[4]<<8)|o[5]); g_wobj[i].obj_n=(uint16_t)((o[6]<<8)|o[7]);
            g_wobj[i].qty=(uint16_t)((o[8]<<8)|o[9]); g_wobj[i].quality=o[11];  /* key id on locks/keys */
            g_wobj[i].parent=(uint16_t)((o[12]<<8)|o[13]);
            g_wobj[i].origin=WOBJ_PARENT_NONE;                     /* set when SPILLED (save locator) */
            g_wobj[i].alive=(g_wobj[i].parent==WOBJ_PARENT_NONE);  /* contained: off-map until spilled */
        }
        objworld_init(&g_ow,g_wobj,nw,gettable); }
    magic_init();                          /* seed spellbook + reagents (magic_skills.md §4) */

    /* mouse cursor: [u8 w][u8 h][u8 hotx][u8 hoty][w*h pixels] after gettable;
     * then BASETILE[2048] (1024 x u16 LE) for frame-aware use (doors). */
    {   const uint8_t *cur=gettable+1024;
        int cw=cur[0], ch=cur[1];
        if(cw>0&&ch>0&&cw<=CURMAX&&ch<=CURMAX){
            g_cw=cw; g_ch=ch; g_chx=cur[2]; g_chy=cur[3];
            memcpy(g_curpix,cur+4,(size_t)cw*ch);
        }
        g_basetile=cur+4+(int32_t)cw*ch; }

    /* ANIMDATA tile-animation table (U6WD): [u16 nanim] then nanim x
     * {u16 dest, u16 src, u8 nframes, u8 speed}. Build tile->entry lookup. */
    {   for(int i=0;i<2048;i++) g_anim_idx[i]=-1;
        const uint8_t *ap=g_basetile+2048;
        g_nanim=(ap[0]<<8)|ap[1]; ap+=2; if(g_nanim>32) g_nanim=32;
        for(int i=0;i<g_nanim;i++){
            int dest=(ap[0]<<8)|ap[1]; int src=(ap[2]<<8)|ap[3];
            g_anim_src[i]=(uint16_t)src; g_anim_n[i]=ap[4]; /* ap[5]=speed (unused yet) */
            if(dest>=0 && dest<2048) g_anim_idx[dest]=(int16_t)i;
            ap+=6;
        }
        /* object frame-animation table (U6WE): objanim[1024], per-obj_n frame count */
        for(int i=0;i<1024;i++) g_objanim[i]=ap[i];
        ap+=1024;
        /* tileflags2[2048] (U6WF): per-tile render flags (bit4 fg, bits6-7 size) */
        for(int i=0;i<2048;i++) g_tflags[i]=ap[i];
        ap+=2048;
        /* tileflags1[2048] (U6WG): per-tile movement flags (bit1 Impass, bit2 Wall) */
        for(int i=0;i<2048;i++) g_tflags1[i]=ap[i];
        ap+=2048;
        /* indoor bitmap (U6WI): [u32 rle_len][RLE {u16 count, u8 value}] -> g_indoor[].
         * 1 bit/tile (idx=y*1024+x); "under a roof" => low daytime ambient (docs/rules/lighting.md §4). */
        { uint32_t rl=((uint32_t)ap[0]<<24)|((uint32_t)ap[1]<<16)|((uint32_t)ap[2]<<8)|ap[3]; ap+=4;
          const int NI=1024*1024/8; int o=0;
          for(uint32_t r=0; r+3<=rl && o<NI; r+=3){ int cnt=(ap[r]<<8)|ap[r+1]; uint8_t v=ap[r+2];
              while(cnt-->0 && o<NI) g_indoor[o++]=v; }
          ap+=rl;
        }
        /* darkening LUTs (U6WJ): NLEV x 256, palette index -> darker index per light level */
        for(int lev=0;lev<NLEV;lev++){ for(int i=0;i<256;i++) g_darklut[lev][i]=ap[i]; ap+=256; }
        /* roof drawing data (U6WK): [u16 n_rtiles][n_rtiles*256][u16 nroof][nroof*{u32 pos,u16 idx}].
         * Pointers index into the loaded pak (no copy). */
        g_nrtiles=(ap[0]<<8)|ap[1]; ap+=2; g_rtiles=ap; ap+=(int32_t)g_nrtiles*256;
        g_nroof=(ap[0]<<8)|ap[1];   ap+=2; g_roofmap=ap; ap+=(int32_t)g_nroof*6;
        /* per-actor readied gear (U6WL): 256 x {u16 weapon obj_n, u8 armour class} */
        for(int i=0;i<U6_MAX_ACTORS;i++){ g_aweapon[i]=(uint16_t)((ap[0]<<8)|ap[1]); g_aac[i]=ap[2]; ap+=3; }
        /* per-actor alignment-hostile flag (U6WM): 256 x u8 */
        for(int i=0;i<U6_MAX_ACTORS;i++) g_align_hostile[i]=ap[i];
        ap+=U6_MAX_ACTORS;
        /* per-actor coward flag (U6WN): 256 x u8 */
        for(int i=0;i<U6_MAX_ACTORS;i++) g_coward[i]=ap[i];
        ap+=U6_MAX_ACTORS;
        /* per-actor experience (U6WO): 256 x u16 BE */
        for(int i=0;i<U6_MAX_ACTORS;i++){ g_aexp[i]=(uint16_t)((ap[0]<<8)|ap[1]); ap+=2; }
        /* per-actor trainer flag (U6WP): 256 x u8 */
        for(int i=0;i<U6_MAX_ACTORS;i++) g_trainer[i]=ap[i];
        ap+=U6_MAX_ACTORS;
        /* party gold (U6WR): u16 BE — applied to g_cg.gold AFTER cvm_game_init (which zeroes it) */
        g_load_gold = (ap[0]<<8)|ap[1]; ap+=2;
        /* avatar karma (U6WR cont.): u8 — objlist 0x1bf9; seeds g_cg.karma for the ztats screen */
        g_load_karma = *ap++;
        /* avatar spellbook (U6WR cont.): [u8 present][u8 all][16B known-spell bitset by U6 spell_num] */
        g_load_book_present=*ap++; g_load_book_all=*ap++;
        for(int i=0;i<16;i++) g_load_known[i]=*ap++;
        if(g_load_book_present){               /* a save-carried spellbook REPLACES the demo known-set (§4) */
            for(int s=0;s<SPELL_COUNT;s++){
                if(g_load_book_all) g_known[s]=1;
                else { int sn=magic_spell_num(s);
                       g_known[s]=(sn>=0&&sn<128&&(g_load_known[sn>>3]&(1<<(sn&7))))?1:0; } }
        }
        /* avatar reagents (U6WR cont.): [u8 present][8 x u8 counts] — obj 65..72 carried by the Avatar */
        g_load_reag_present=*ap++;
        for(int i=0;i<8;i++) g_load_reag[i]=*ap++;
        if(g_load_reag_present){               /* real carried reagents REPLACE the seeded starter stock (§2) */
            for(int b=0;b<REAG_COUNT;b++){ int on=magic_reagent_objn(b);
                objworld_consume(&g_ow,(uint16_t)on,99999);            /* clear the seed */
                if(g_load_reag[b]>0) inv_add_objn(on,g_load_reag[b]); }
        }
        /* FRONT-END art block (U6WU..avatar climax): the slim ST pack OMITS this ~1.35 MB of intro/
         * chargen art to fit a real 4 MB ST (gameplay never uses it) and writes a "NOFE" marker in
         * its place. Full packs (TT/Falcon) parse it as before. (docs/design/palette_lowcolor.md) */
        if(ap+4<=pak+n && ap[0]=='N'&&ap[1]=='O'&&ap[2]=='F'&&ap[3]=='E'){ ap+=4; }   /* slim: skip */
        else { ap = parse_frontend(ap); }   /* full pack: parse the inline front-end art block */

        /* LOW-COLOUR ST palette + 256->16 LUT (U6WW), appended last & self-guarded by a
         * "U6WW" magic (older packs stop at EOF here). [4 magic][16 x {r,g,b} 3-bit levels]
         * [256 x u8 lut]. Only the shifter backend uses it; TT/Falcon leave it untouched. */
        if(ap+308<=pak+n && ap[0]=='U'&&ap[1]=='6'&&ap[2]=='W'&&ap[3]=='W'){
            ap+=4;
            for(int i=0;i<48;i++)  g_lc_pal[i]=ap[i];  ap+=48;
            for(int i=0;i<256;i++) g_lc_lut[i]=ap[i];  ap+=256;
            g_have_lowcolor=1;
            /* BAND-RASTER low-colour (U6WX): [4 "U6WX"][u8 nb][nb u8 y0][nb*48 pal][nb*256 lut] ->
             * point into the pak (the shifter backend copies them; TT/Falcon ignore). */
            if(ap+5<=pak+n && ap[0]=='U'&&ap[1]=='6'&&ap[2]=='W'&&ap[3]=='X'){
                int nb=ap[4]; const uint8_t *q=ap+5;
                if(nb>=2 && q+(int32_t)nb*(1+48+256) <= pak+n){
                    g_lcx_nb=nb; g_lcx_y0=q; g_lcx_pal=q+nb; g_lcx_lut=q+nb+(int32_t)nb*48;
                    ap = q + (int32_t)nb*(1+48+256);        /* advance past U6WX to U6WY */
                }
            }
            /* per-portrait CONVERSATION palettes (U6WY): [4 "U6WY"][u16 nport][nport x (48+256)] */
            if(ap+6<=pak+n && ap[0]=='U'&&ap[1]=='6'&&ap[2]=='W'&&ap[3]=='Y'){
                int np=(ap[4]<<8)|ap[5]; const uint8_t *q=ap+6;
                if(np>0 && q+(int32_t)np*(48+256) <= pak+n){ g_nconvpal=np; g_convpal=q; ap=q+(int32_t)np*(48+256); }
            }
            /* COLOUR-CYCLING metadata (U6WZ): [4 "U6WZ"][u8 ncyc][ncyc x {u8 first_slot, u8 nslots}] —
             * which reserved ST-16 slots the shifter rotates each tick (fire/water). TT/Falcon ignore. */
            if(ap+5<=pak+n && ap[0]=='U'&&ap[1]=='6'&&ap[2]=='W'&&ap[3]=='Z'){
                int nc=ap[4]; const uint8_t *q=ap+5;
                if(nc>0 && nc<=CYC_MAX && q+(int32_t)nc*2 <= pak+n){
                    g_lc_ncyc=nc;
                    for(int i=0;i<nc;i++){ g_lc_cyc[i][0]=q[i*2]; g_lc_cyc[i][1]=q[i*2+1]; }
                    ap=q+(int32_t)nc*2;
                }
            }
            /* STE 4096-colour palettes (U6WS): [4 "U6WS"][48 game4][2*48 band4][u16 nconv][nconv*48 conv4]
             * — 4-bit companions of the U6WW/U6WX/U6WY palettes (share their LUTs). Used only on STE. */
            if(ap+6+48+2*48<=pak+n && ap[0]=='U'&&ap[1]=='6'&&ap[2]=='W'&&ap[3]=='S'){
                const uint8_t *q=ap+4;
                for(int i=0;i<48;i++) g_lc_pal4[i]=q[i];  q+=48;   /* game 16, 4-bit */
                g_lcx_pal4=q; q+=2*48;                             /* 2 bands, 4-bit (into the pak) */
                int nc=(q[0]<<8)|q[1]; q+=2;
                if(nc>0 && q+(int32_t)nc*48<=pak+n){ g_convpal4=q; q+=(int32_t)nc*48; }
                g_have_ste=1; ap=q;
            }
            /* BOOK.DAT readable text (U6BK): [4 "U6BK"][u16 nbook][nbook x [u16 len][bytes]]. RESIDENT
             * core section (was in the front-end block, which the ST frees) — pointers into the pak. */
            if(ap+6<=pak+n && ap[0]=='U'&&ap[1]=='6'&&ap[2]=='B'&&ap[3]=='K'){
                int nb=(ap[4]<<8)|ap[5]; const uint8_t *q=ap+6;
                g_nbook = nb>BOOK_MAX?BOOK_MAX:nb;
                for(int i=0;i<nb && q<pak+n;i++){ int L=(q[0]<<8)|q[1]; q+=2;
                    if(i<BOOK_MAX){ g_book[i]=(const char*)q; g_booklen[i]=L; } q+=L; }
                ap=q;
            }
        }
    }

    GameDef def = *gd;                 /* the game selected by the pack magic (M9) */
    def.start_x = startx; def.start_y = starty;   /* per-save start from the pack header */
    world_t w;
    if (!world_init(&w,map,(int32_t)map_len,chunks,(int32_t)chunks_len,passable,&def,1)){
        Cconws("world_init failed\r\n"); Crawcin(); return 1; }
    build_blocked();                      /* impassable-object map (walls/furniture) */
    world_set_blocked_obj(&w, g_blocked); /* player + NPCs now respect object walls */

    /* Conversation + portrait libraries, loaded at runtime (never embedded):
     * the originals' CONVERSE.A/.B + PORTRAIT.A/.B (GEMDOS 8.3 names). These let
     * us talk to ANY NPC, not just the baked demo. Missing files => idle panel. */
    /* Load these into CHIP (ST-RAM), not the fast pool: ~0.57 MB of conversation/
     * portrait data that is read-mostly (never in a hot loop, untouched by the intro),
     * so it belongs in ST-RAM — keeping the ~4 MB TT-RAM fast pool free for the asset
     * pack + backbuffer. (With them in fast, U6.PAK + libs overran the pool and rdr_init's
     * fast backbuffer alloc failed → "no renderer".) */
    /* footprint diagnostic (MEMINFO.BIN): report the ST-RAM pool state at key load points so we can
     * see how much the pack, then the conv/portrait libs, leave for rdr_init's ~96 KB of buffers. */
    plat_mem_info_t _mi; int _meminfo=0;
    {   int32_t mil=0; if(plat_load_file("MEMINFO.BIN",1,&mil)){ _meminfo=1;
            plat_mem_get_info(&_mi);
            dbg_num("MEM pool_size KB=", (int)(_mi.chip_size/1024));
            dbg_num("MEM after_pack_used KB=", (int)(_mi.chip_used/1024));
            dbg_num("MEM after_pack_free KB=", (int)((_mi.chip_size-_mi.chip_used)/1024)); } }

    g_convA=(const uint8_t*)plat_load_file("CONVERSE.A",0,&g_convAlen);
    g_convB=(const uint8_t*)plat_load_file("CONVERSE.B",0,&g_convBlen);
    g_portA=(const uint8_t*)plat_load_file("PORTRAIT.A",0,&g_portAlen);
    g_portB=(const uint8_t*)plat_load_file("PORTRAIT.B",0,&g_portBlen);
    g_countA=g_convA?lib_count(g_convA,g_convAlen):0;   /* guard: libs may fail on a tight ST pool */

    if(_meminfo){ plat_mem_get_info(&_mi);
        dbg_num("MEM chip(ST-RAM)_used KB=", (int)(_mi.chip_used/1024));
        dbg_num("MEM chip_free KB=", (int)((_mi.chip_size-_mi.chip_used)/1024));
        dbg_num("MEM have_altram=", _mi.have_altram);
        dbg_num("MEM fast(TT-RAM)_size KB=", (int)(_mi.fast_size/1024));
        dbg_num("MEM fast_used KB=", (int)(_mi.fast_used/1024));
        dbg_num("MEM RESIDENT total KB=", (int)((_mi.chip_used + (_mi.have_altram?_mi.fast_used:0))/1024));
        dbg_num("MEM libs_ok=", (g_convA&&g_convB&&g_portA&&g_portB)?1:0); }

#ifndef ST_STREAM   /* ST did rdr_init early (before streaming the front-end); TT/Falcon do it here */
    if (!rdr_init(&caps)) { Cconws("no renderer\r\n"); Crawcin(); return 1; }
    plat_input_init();                              /* fast key auto-repeat for smooth movement */
#endif
    rdr_set_palette(palette, 256);
    /* STE 4096-colour variant: if the shifter is an STE AND the pack carries U6WS 4-bit palettes,
     * switch the backend to the 4-bit register format and hand it the 4-bit arrays from here on (the
     * front-end already ran in 3-bit, which an STE displays fine via backward compatibility). */
    g_is_ste = (caps.video == VIDEO_STE) && g_have_ste;
    rdr_set_ste_mode(g_is_ste);
    /* Hand the shifter backend the pack's host-quantised 16-colour palette + 256->16 LUT (U6WW, or its
     * U6WS 4-bit companion on STE). No-op on the 256-colour TT/Falcon backends. */
    if (g_have_lowcolor) rdr_set_lowcolor(LCPAL(g_lc_pal, g_lc_pal4), g_lc_lut);
    /* ...then the band rasters (U6WX): >16 on-screen colours via a Timer-B palette split ("cheat
     * past 16 colours"). No-op on TT/Falcon; on the ST it installs the raster (see rdr_st.c). */
    if (g_lcx_nb >= 2) rdr_set_lowcolor_bands(g_lcx_nb, g_lcx_y0, LCPAL(g_lcx_pal, g_lcx_pal4), g_lcx_lut);
    g_basepal=palette;                              /* base for colour cycling (CLUT rotation) */
    g_bb=rdr_backbuffer(); g_W=rdr_width();

    /* LINCHPIN TEST for hardware scrolling: draw a 16px-stripe pattern + a marker at x=100,
     * then write $FF8265 = (digit in HSCROLL.REQ). If TT-low honours fine-scroll, the marker
     * shifts left by that many pixels. Compare captures at values 0 and 8. */
    { int32_t hl=0; void*hreq=plat_load_file("HSCROLL.REQ",1,&hl);
      if(hreq){ int v=((uint8_t*)hreq)[0]-'0'; if(v<0||v>15) v=8;
        clear_bb(0);
        for(int x=0;x<LW;x+=16) for(int y=40;y<160;y++) ROW(y)[x]=15;   /* stripe every 16px */
        for(int y=40;y<160;y++) ROW(y)[100]=31;                          /* marker column @100 */
        ui_text(8,20,"HSCROLL TEST",15);
        rdr_present_doubled(LH);
        rdr_test_hscroll(v);
        for(;;) Vsync(); } }

    /* a dark papyrus-ink colour for text: palette index ~ dark brown. The pack
     * carries the U6 palette; index 0 is black which reads fine on papyrus. */
    uint8_t ink=C_BLACK;

    g_hour=clock_hour(&w); g_minutes=clock_minutes(&w);  /* place NPCs by the game clock */
    init_npcs();                                          /* seed NPC positions/facing for schedule-walking */
    rng_seed(&g_crng, 0x6e0c0de ^ (uint32_t)g_start_min); /* combat RNG (docs/rules/combat.md) */
    if(g_aweapon[1]) g_avatar_weapon=g_aweapon[1];        /* the avatar's actual readied weapon (CB3b) */
    for(int i=0;i<U6_MAX_ACTORS;i++){                     /* per-actor level = maxHP/30 (party leveling) */
        int lv=(g_amaxhp[i]>=30)?(g_amaxhp[i]/30):1; if(lv>8) lv=8; g_alevel[i]=(int8_t)lv; }

    /* initial party. A save (U6WC) carries the real party list + per-actor HP from
     * the OBJLIST (member 0 = the player). Without a saved list we fall back to the
     * Avatar (actor 1) + the three starting companions (actors 2/3/4). Names come
     * from the game's CONVERSE data; HP from the OBJLIST stat block (g_ahp). */
    {   static const int dfl[4]={1,2,3,4};
        g_nparty=(g_nsparty>0)?g_nsparty:4;
        if(g_nparty>MAXPARTY) g_nparty=MAXPARTY;
        for(int i=0;i<g_nparty;i++){
            int a=(g_nsparty>0)?g_sparty[i]:dfl[i];
            g_party[i].actor=a;
            g_party[i].hp=(a>=0&&a<U6_MAX_ACTORS&&g_ahp[a])?g_ahp[a]:30;
            if(a==1){ const char*p="Avatar"; int o=0; while(p[o]){g_party[i].name[o]=p[o];o++;} g_party[i].name[o]=0; }
            else resolve_name(a, g_party[i].name, 16);
        }
    }
    /* place party members beside the Avatar if the save gave them no surface
     * position, so they appear and then follow him (Stage 3b party-follow). */
    {   static const int ox[4]={0,-1,1,0}, oy[4]={1,1,1,2}; int pk=0;
        for(int p=0;p<g_nparty;p++){ int a=g_party[p].actor;
            if(a<=1||a>=U6_MAX_ACTORS||g_atile[a]==0xffff) continue;
            if(g_actz[a]==0xff){ int k=(pk++)&3;
                g_actx[a]=(uint16_t)((w.px+ox[k])&(U6_SURFACE_TILES-1));
                g_acty[a]=(uint16_t)((w.py+oy[k])&(U6_SURFACE_TILES-1));
                g_actz[a]=0; } }
    }
    cvm_game_init(&g_cg, 1);                         /* persistent conversation game-state */
    g_cg.gold=g_load_gold;                           /* real party gold from the save (obj_n-88 coins, pack U6WR) */
    g_cg.karma=(int16_t)g_load_karma;                /* real avatar karma from the save (objlist 0x1bf9, pack U6WR) */

    /* Load the ScummVM save TEMPLATE (the user's own .000, staged on C:) so the
     * engine can WRITE a ScummVM-loadable save by patching it with live state.
     * Optional: absent in non-save builds (then 'S' reports no template). */
    g_objlist_off=-1;
    g_savetmpl=(uint8_t*)plat_load_file("SAVE.000",0,&g_savetmpl_len);  /* CHIP: read-mostly template; spares the fast pool */
    if(g_savetmpl){ g_objlist_off=sav_objlist_off(g_savetmpl,g_savetmpl_len);
        if(g_objlist_off<0) Cconws("SAVE.000: cannot locate objlist\r\n"); }

    /* FRONT-END: show the title + main menu on interactive boot (gated on MENU.REQ so
     * plain headless scene captures still render the game). "Journey Onward" returns
     * here and play begins. INTRO.REQ plays the cutscene directly (headless capture of
     * Phase 2 without navigating the menu). (shapes.md; Phase 1/2.) */
#ifndef ST_STREAM   /* ST ran the front-end EARLY (streamed from U6FE, then freed); TT/Falcon here */
    { int32_t ml=0;
      void*ireq=plat_load_file("INTRO.REQ",1,&ml); if(ireq) run_intro();
      void*creq=plat_load_file("CHARGEN.REQ",1,&ml);                          /* headless chargen capture */
      if(creq) chargen_capture(ml>0 ? ((uint8_t*)creq)[0] : '1');             /* byte selects the sub-screen */
      void*mreq=plat_load_file("MENU.REQ",1,&ml);  if(mreq) run_menu(); }
#endif
    apply_chargen();   /* if a character was created, it becomes the Avatar (name/stats) for play */

    /* headless NEW-GAME verification: if NEWGAME.BIN is staged, print the assembled new-game
     * state to the console (--conout 2) so a capture can assert the created character was
     * overlaid onto the shipped start (Lord British's castle @ 315,367, four-member party).
     * Pair with CHARGEN.REQ='N' (applies a deterministic sample). Absent in normal play. */
    {   int32_t glen=0; void*gg=plat_load_file("NEWGAME.BIN",1,&glen);
        if(gg){
            Cconws("NEWGAME avatar: "); Cconws(g_avatar_name[0]?g_avatar_name:"Avatar"); Cconws("\r\n");
            dbg_num("NEWGAME pos x   =", w.px);
            dbg_num("NEWGAME pos y   =", w.py);
            dbg_num("NEWGAME str     =", g_astr[1]);
            dbg_num("NEWGAME dex     =", g_adex[1]);
            dbg_num("NEWGAME int     =", g_aint[1]);
            dbg_num("NEWGAME maxmana =", g_amaxmana[1]);
            dbg_num("NEWGAME sex     =", g_cg.avatar_sex);     /* 0=male 1=female (VM var 0x10) */
            dbg_num("NEWGAME party   =", g_nparty);
            dbg_num("NEWGAME spellbk =", objworld_count(&g_ow,57));
            Cconws("NEWGAME title : "); Cconws(g_avatar_title); Cconws("\r\n");  /* $G honorific (milord/milady) */
        }
    }

    /* headless STATUS-view capture: if STATUS.REQ is staged, open the character status view for
     * the actor id in its byte (default = the Avatar, actor 1) so the first rendered frame shows
     * it. Pair with CHARGEN.REQ='N' to see the created character's portrait/gender/stats. */
    {   int32_t slen=0; void*sq=plat_load_file("STATUS.REQ",1,&slen);
        if(sq){ int id=(slen>0)?((uint8_t*)sq)[0]:1; g_status_actor=(id>=1)?id:1; g_show_status=1; } }

    if(g_savestate){
        /* World built from a save: show the loaded state as-is. When a save TEMPLATE
         * is present (SAVE.000 staged), demonstrate the ON-TARGET save writer — walk a
         * few tiles then write SAVED.000. Absent in normal play. */
        if(g_savetmpl && g_objlist_off>=0){
            for(int s=0;s<5;s++){ world_try_move(&w,0,1); step_npcs(&w); }  /* walk; party follows */
            for(int i=0;i<g_ow.nobjs;i++){                                   /* open a CLOSED door to exercise the object-world save */
                if(!g_wobj[i].alive || g_wobj[i].obj_n<297 || g_wobj[i].obj_n>300) continue;
                int fr=(int)g_wobj[i].tile - basetile(g_wobj[i].obj_n);
                if(fr>=4 && fr<=7){ use_object(i); break; } }   /* -> dirty; do_save re-serialises its frame */
            for(int i=0;i<g_ow.nobjs;i++)                                   /* take an on-map item to exercise GET persistence */
                if(g_wobj[i].alive && objworld_can_get(&g_ow,g_wobj[i].obj_n)){ objworld_get(&g_ow,i); break; }
            for(int i=0;i<g_ow.nobjs;i++){                                  /* open a CONTAINER + take a spilled child (contained-GET save) */
                int on=g_wobj[i].obj_n;
                if(!g_wobj[i].alive || (on!=98 && on!=192 && on!=186)) continue;   /* chest/crate/barrel */
                int has=0; for(int j=0;j<g_ow.nobjs;j++)
                    if(!g_wobj[j].alive && g_wobj[j].parent==(uint16_t)i && objworld_can_get(&g_ow,g_wobj[j].obj_n)){ has=1; break; }
                if(!has) continue;
                int np=0; spill_container(i,&np);                            /* children -> on-map at the container's tile */
                for(int j=0;j<g_ow.nobjs;j++)
                    if(g_wobj[j].alive && g_wobj[j].origin==(uint16_t)i && objworld_can_get(&g_ow,g_wobj[j].obj_n)){
                        objworld_get(&g_ow,j); break; }
                break;
            }
            g_hour=clock_hour(&w); g_minutes=clock_minutes(&w);
            do_save(&w);
        }
    }

    /* headless INVENTORY<->VM sync test: seed the engine inventory + gold, push it to the VM
     * (sync_inv_in), simulate a conversation give/take + gold change on the VM state, pull it back
     * (sync_inv_out), and print the reconciled engine inventory + gold to --conout 2. */
    {   int32_t ivl=0; void*ivb=plat_load_file("INVSYNC.BIN",1,&ivl);
        if(ivb){
            g_ow.ninv=0; inv_add_objn(65,3); inv_add_objn(129,1); g_cg.gold=50;   /* 3 ginseng + 1 food, 50 gold */
            dbg_num("INVSYNC before ginseng=", 3); dbg_num("INVSYNC before gold=", g_cg.gold);
            sync_inv_in();
            dbg_num("INVSYNC the VM sees ginseng=", g_cg.player_obj[65]);
            g_cg.player_obj[65]+=2; g_cg.player_obj[129]-=1; g_cg.gold-=15;        /* NPC gives 2 ginseng, takes the food, sells for 15 */
            sync_inv_out();
            int gs=0,fd=0; for(int i=0;i<g_ow.ninv;i++){ int on=g_ow.inv[i].obj_n&0xff;
                if(on==65)gs+=g_ow.inv[i].qty; if(on==129)fd+=g_ow.inv[i].qty; }
            dbg_num("INVSYNC after ginseng=", gs); dbg_num("INVSYNC after food=", fd); dbg_num("INVSYNC after gold=", g_cg.gold);
        }
    }

    /* headless READ demo (docs/formats/strings.md): if READ.BIN is staged, read a readable object.
     * byte>0 => show BOOK.DAT entry (byte-1) directly; byte 0 => find the first readable WORLD
     * object and read it (quality->entry). book_view holds the first page for the AVI capture. */
    {   int32_t rlen=0; uint8_t *rb=(uint8_t*)plat_load_file("READ.BIN",1,&rlen);
        if(rb){
            int sel=(rlen>0)?rb[0]:0;
            if(sel>0){ int e=sel-1; dbg_num("READ entry=", e);
                if(e>=0 && e<g_nbook && g_book[e]) book_view(g_book[e], g_booklen[e]); }
            else {
                int found=-1;
                for(int32_t i=0;i<g_ow.nobjs;i++)
                    if(g_wobj[i].alive && is_readable_objn(g_wobj[i].obj_n)){ found=(int)i; break; }
                if(found>=0){ dbg_num("READ obj_n=", g_wobj[found].obj_n);
                    dbg_num("READ quality=", g_wobj[found].quality); read_object(found); }
                else Cconws("READ: no readable object in the loaded world\r\n");
            }
        }
    }

    /* headless validation of NPC schedule-walking: if a sentinel NPCWALK.BIN is
     * staged (1 byte = turn count), advance that many game-turns — the clock ticks,
     * the hour rolls, and step_npcs() walks every NPC toward its schedule spot — so
     * the capture below shows NPCs that have moved/faced. Absent in normal play. */
    {   int32_t nlen=0; uint8_t *nw=(uint8_t*)plat_load_file("NPCWALK.BIN",1,&nlen);
        if(nw && nlen>0){ int turns=nw[0];
            for(int s=0;s<turns;s++){ w.clock+=10; g_hour=clock_hour(&w); g_minutes=clock_minutes(&w); step_npcs(&w); } }
    }

    /* headless validation of Stage 3c WANDER: WANDER.BIN[0]=NPC (default 62 Jaana), [1]=turns.
     * Force noon (worktype 0x90 = wander is active), seat the NPC on its noon anchor, then step
     * — printing its (x,y) each turn. Expect it to ROAM but stay within WANDER_R of the anchor. */
    {   int32_t wlen=0; uint8_t *wb=(uint8_t*)plat_load_file("WANDER.BIN",1,&wlen);
        if(wb){
            int npc=(wlen>0)?wb[0]:62, turns=(wlen>1)?wb[1]:12;
            g_start_min=12*60; w.clock=0; g_hour=clock_hour(&w); g_minutes=clock_minutes(&w);
            dbg_num("WANDER npc=", npc); dbg_num("WANDER tile!=ffff:", g_atile[npc]!=0xffff);
            int ax,ay;
            if(g_atile[npc]!=0xffff && npc_dest(npc,&ax,&ay)){
                g_actx[npc]=(uint16_t)ax; g_acty[npc]=(uint16_t)ay; g_actz[npc]=0;
                dbg_num("WANDER worktype=", npc_worktype(npc));
                dbg_num("WANDER anchor_x=", ax); dbg_num("WANDER anchor_y=", ay);
                for(int s=0;s<turns;s++){ w.clock+=7; g_hour=clock_hour(&w); g_minutes=clock_minutes(&w); step_npcs(&w);
                    dbg_num("WANDER x=", g_actx[npc]); dbg_num("WANDER y=", g_acty[npc]); }
            } else Cconws("WANDER: NPC absent or has no noon schedule\r\n");
        }
    }

    /* headless validation of Stage-USE effects (lockpick + food). LOCKPICK: force a door LOCKED,
     * put a lockpick (obj 63) in inventory, USE it → frame 9→5 (picked, closed-unlocked). FOOD:
     * make a world object a loaf of bread (obj 128, qty 2), USE twice → qty 2→1 then alive→0. */
    {   int32_t ul=0; void*ub=plat_load_file("USEFX.BIN",1,&ul);
        if(ub){
            for(int i=0;i<g_ow.nobjs;i++){ int o=g_wobj[i].obj_n;
                if(!g_wobj[i].alive || o<297 || o>300) continue;
                g_wobj[i].tile=(uint16_t)(basetile(o)+9);            /* force LOCKED (frame 9) */
                inv_add_objn(63,1);                                  /* a lockpick */
                dbg_num("PICK frame before=", (int)g_wobj[i].tile-basetile(o));
                use_object(i);
                dbg_num("PICK frame after=",  (int)g_wobj[i].tile-basetile(o));   /* expect 5 */
                break; }
            for(int i=0;i<g_ow.nobjs;i++){
                if(!g_wobj[i].alive || !objworld_can_get(&g_ow,g_wobj[i].obj_n)) continue;
                g_wobj[i].obj_n=128; g_wobj[i].tile=(uint16_t)basetile(128); g_wobj[i].qty=2;  /* -> loaf of bread */
                dbg_num("FOOD is_food=", is_food_objn(g_wobj[i].obj_n));
                use_object(i); dbg_num("FOOD qty after 1st eat=", g_wobj[i].qty);              /* expect 1 */
                use_object(i); dbg_num("FOOD alive after 2nd eat=", g_wobj[i].alive);          /* expect 0 */
                break; }
        }
    }

    /* headless validation of LEVER/SWITCH (quality-link) + CRANK (positional). Find a lever with
     * quality>0, note its matching-quality field's alive state, USE the lever -> field toggles off,
     * USE again -> back on. Then find a crank and USE it -> the nearest portcullis frame flips. */
    {   int32_t ll=0; void*lb=plat_load_file("LEVER.BIN",1,&ll);
        if(lb){
            int lev=-1; for(int i=0;i<g_ow.nobjs;i++) if(g_wobj[i].alive && g_wobj[i].obj_n==268 && g_wobj[i].quality){ lev=i; break; }
            if(lev>=0){ int q=g_wobj[lev].quality;
                int fld=-1; for(int j=0;j<g_ow.nobjs;j++) if(g_wobj[j].quality==q && is_field_objn(g_wobj[j].obj_n)){ fld=j; break; }
                dbg_num("LEVER quality=", q); dbg_num("LEVER field found=", fld>=0);
                if(fld>=0){ dbg_num("LEVER field alive0=", g_wobj[fld].alive);
                    use_object(lev); dbg_num("LEVER field alive1=", g_wobj[fld].alive);   /* expect toggled */
                    use_object(lev); dbg_num("LEVER field alive2=", g_wobj[fld].alive); } /* expect restored */
            } else Cconws("LEVER: no quality>0 lever in world\r\n");
            int crk=-1; for(int i=0;i<g_ow.nobjs;i++) if(g_wobj[i].alive && g_wobj[i].obj_n==288){ crk=i; break; }
            if(crk>=0){ int pc=-1,bd=99,ax=g_wobj[crk].x,ay=g_wobj[crk].y;
                for(int j=0;j<g_ow.nobjs;j++){ if(!g_wobj[j].alive||g_wobj[j].obj_n!=310) continue;
                    int dx=g_wobj[j].x-ax,dy=g_wobj[j].y-ay; if(dx<0)dx=-dx; if(dy<0)dy=-dy; int d=dx>dy?dx:dy; if(d<bd){bd=d;pc=j;} }
                dbg_num("CRANK portcullis dist=", bd);
                if(pc>=0){ dbg_num("CRANK pc frame0=", (int)g_wobj[pc].tile-basetile(310));
                    use_object(crk); dbg_num("CRANK pc frame1=", (int)g_wobj[pc].tile-basetile(310)); } }
        }
    }

    /* headless DRAWBRIDGE test: find a crank, USE it, and check the nearest drawbridge span's frame
     * toggles between 6-8 (raised) and 9-11 (down). */
    {   int32_t dl=0; void*db=plat_load_file("DBTEST.BIN",1,&dl);
        if(db){ int crk=-1; for(int i=0;i<g_ow.nobjs;i++) if(g_wobj[i].alive&&g_wobj[i].obj_n==288){ crk=i; break; }
            if(crk>=0){ int ax=g_wobj[crk].x,ay=g_wobj[crk].y, br=-1,bd=99;
                for(int j=0;j<g_ow.nobjs;j++){ if(!g_wobj[j].alive||g_wobj[j].obj_n!=269) continue;
                    int dx=g_wobj[j].x-ax,dy=g_wobj[j].y-ay; if(dx<0)dx=-dx; if(dy<0)dy=-dy; int d=dx>dy?dx:dy; if(d<bd){bd=d;br=j;} }
                dbg_num("DRAWBRIDGE dist=", bd);
                if(br>=0){ dbg_num("DRAWBRIDGE frame0=", (int)g_wobj[br].tile-basetile(269));
                    use_object(crk); dbg_num("DRAWBRIDGE frame1=", (int)g_wobj[br].tile-basetile(269)); } }
        }
    }

    /* headless HUNGER test: seed low food, advance turns → food ebbs to 0 and the Avatar starves
     * (loses HP); then eat a loaf → food restored. */
    {   int32_t hl=0; void*hb=plat_load_file("HUNGER.BIN",1,&hl);
        if(hb){ g_afood[1]=5; int hp0=g_ahp[1];
            for(int s=0;s<40;s++){ w.clock+=10; step_npcs(&w); }
            dbg_num("HUNGER food after starve=", g_afood[1]); dbg_num("HUNGER hp lost=", hp0-g_ahp[1]);
            for(int i=0;i<g_ow.nobjs;i++) if(g_wobj[i].alive&&objworld_can_get(&g_ow,g_wobj[i].obj_n)){
                g_wobj[i].obj_n=128; g_wobj[i].tile=(uint16_t)basetile(128); g_wobj[i].qty=1;
                int f0=g_afood[1]; use_object(i); dbg_num("HUNGER food before eat=", f0);
                dbg_num("HUNGER food after eat=", g_afood[1]); break; }
        }
    }

    /* headless LOW-COLOUR (U6WW) load check: prove the pack's low-colour section parsed to the
     * exact byte (g_have_lowcolor=1 => the whole front-end chain was consumed correctly), and
     * echo the 16-colour ST palette as shifter registers (r<<8|g<<4|b) + a couple of LUT samples. */
    {   int32_t lcl=0; void*lcb=plat_load_file("LOWCOL.BIN",1,&lcl);
        if(lcb){ dbg_num("LOWCOL have=", g_have_lowcolor);
            for(int i=0;i<16;i++) dbg_num("LOWCOL pal reg=",
                (g_lc_pal[i*3]<<8)|(g_lc_pal[i*3+1]<<4)|g_lc_pal[i*3+2]);
            dbg_num("LOWCOL lut0=", g_lc_lut[0]); dbg_num("LOWCOL lut255=", g_lc_lut[255]);
        }
    }

    /* headless RASTER test (M7 band rasters): install a 2-band split where band 1 (rows 176..199,
     * the command bar) uses the SAME LUT as band 0 but with R and B swapped in every palette entry.
     * If the Timer-B palette raster fires, the bottom band visibly shows swapped colours — an
     * unmistakable proof of the mechanism, independent of the real per-band palette data. */
    {   int32_t rl=0; void*rb=plat_load_file("RASTER.BIN",1,&rl);
        if(rb && g_have_lowcolor){
            static uint8_t y0[2]={0,176};
            static uint8_t pal[2*48], lut[2*256];
            for(int i=0;i<16;i++){
                pal[i*3]=g_lc_pal[i*3]; pal[i*3+1]=g_lc_pal[i*3+1]; pal[i*3+2]=g_lc_pal[i*3+2];
                pal[48+i*3]=g_lc_pal[i*3+2]; pal[48+i*3+1]=g_lc_pal[i*3+1]; pal[48+i*3+2]=g_lc_pal[i*3]; /* R<->B */
            }
            for(int i=0;i<256;i++){ lut[i]=g_lc_lut[i]; lut[256+i]=g_lc_lut[i]; }
            rdr_set_lowcolor_bands(2, y0, pal, lut);
            dbg_num("RASTER installed nb=", 2);
        }
    }

    /* VERTICAL-SLICE demo (M6 acceptance): compose the core gameplay beats in ONE live session —
     * MOVE, GET, CAST, USE — proving they interoperate (talk/combat/save are validated separately). */
    {   int32_t vl=0; void*vb=plat_load_file("VSLICE.BIN",1,&vl);
        if(vb){ int pass=0;
            int x0=(int)w.px,y0=(int)w.py, moved=0; int dr[4][2]={{0,1},{1,0},{0,-1},{-1,0}};   /* 1: MOVE */
            for(int k=0;k<4&&!moved;k++) for(int s=0;s<3;s++){ if(world_try_move(&w,dr[k][0],dr[k][1])){ step_npcs(&w); moved=1; } else break; }
            moved=moved&&((int)w.px!=x0||(int)w.py!=y0); dbg_num("VSLICE 1 MOVE ok=", moved); pass+=moved;
            int got=0; for(int i=0;i<g_ow.nobjs;i++) if(g_wobj[i].alive&&objworld_can_get(&g_ow,g_wobj[i].obj_n)){ got=objworld_get(&g_ow,i); break; }  /* 2: GET */
            dbg_num("VSLICE 2 GET ok=", got); pass+=(got?1:0);
            g_aint[1]=32; g_spell_light_turns=0; cast_spell(&w,1,magic_find("Light"),0,0);        /* 3: CAST */
            dbg_num("VSLICE 3 CAST ok=", g_spell_light_turns>0); pass+=(g_spell_light_turns>0);
            int used=0; for(int i=0;i<g_ow.nobjs;i++){ int o=g_wobj[i].obj_n; if(!g_wobj[i].alive||o<297||o>300) continue;  /* 4: USE a door */
                int fr=(int)g_wobj[i].tile-basetile(o); if(fr<4||fr>7) continue; uint16_t t0=g_wobj[i].tile; use_object(i);
                if(g_wobj[i].tile!=t0){ used=1; break; } }
            dbg_num("VSLICE 4 USE ok=", used); pass+=used;
            dbg_num("VSLICE beats passed of 4=", pass);
        }
    }

    /* headless combat demo (docs/rules/combat.md CB1): if COMBAT.BIN is staged, spawn a
     * weak acid slug (obj_n 364: hp 10, dex 5) just east of the avatar and A)ttack east
     * COMBAT.BIN byte 1 = CB1 (avatar kills an adjacent slug); byte 2 = CB2 (a hostile slug
     * approaches + attacks the party); byte 3 = CB3 (avatar shoots a distant slug with a bow).
     * Absent in normal play. */
    {   int32_t clen=0; uint8_t *cb=(uint8_t*)plat_load_file("COMBAT.BIN",1,&clen);
        if(cb && clen>0){
            int mode=cb[0], slot=250, bt=basetile(364);
            int sx=(mode==3)?3 : (mode>=2)?2 : 1;        /* CB3 spawns 3 east (ranged), CB2 2, CB1 1 */
            g_atile[slot]=(uint16_t)bt; g_abase[slot]=(uint16_t)bt; g_atpd[slot]=0; g_facing[slot]=2;
            g_actx[slot]=(uint16_t)((w.px+sx)&(U6_SURFACE_TILES-1));
            g_acty[slot]=(uint16_t)(w.py&(U6_SURFACE_TILES-1));
            g_actz[slot]=0; g_adex[slot]=8; g_astr[slot]=5;
            if(mode==3){                                  /* CB3: avatar bow shoots a distant slug */
                g_avatar_weapon=41; g_ahp[slot]=10;       /* bow (range 5) */
                static const int dd[4][2]={{0,1},{0,-1},{1,0},{-1,0}};
                int ddx=1,ddy=0;                          /* pick a direction with a clear 2-tile line */
                for(int k=0;k<4;k++){ int dxk=dd[k][0],dyk=dd[k][1];
                    int x1=(int)((w.px+dxk)&(U6_SURFACE_TILES-1)), y1=(int)((w.py+dyk)&(U6_SURFACE_TILES-1));
                    int x2=(int)((w.px+2*dxk)&(U6_SURFACE_TILES-1)), y2=(int)((w.py+2*dyk)&(U6_SURFACE_TILES-1));
                    if(npc_at(&w,x1,y1)<0 && npc_at(&w,x2,y2)<0 && !sight_blocked_at(&w,x1,y1) && !sight_blocked_at(&w,x2,y2)){ ddx=dxk; ddy=dyk; break; } }
                g_actx[slot]=(uint16_t)((w.px+2*ddx)&(U6_SURFACE_TILES-1));
                g_acty[slot]=(uint16_t)((w.py+2*ddy)&(U6_SURFACE_TILES-1));
                for(int s=0;s<8;s++){ int tx=(int)((w.px+2*ddx)&(U6_SURFACE_TILES-1)), ty=(int)((w.py+2*ddy)&(U6_SURFACE_TILES-1));
                    if(npc_at(&w,tx,ty)<0) break; do_attack(&w,ddx,ddy); }
            } else if(mode==4){                           /* CB4: alignment hostility + combat mode + FLEEING */
                /* A badly-hurt alignment-hostile slug a few tiles east: HP <= 1/4 of max,
                 * so check_flee puts it in the flee state; on its turns it RETREATS east
                 * (away from the party) instead of attacking. Validates the wounded-flee AI. */
                g_align_hostile[slot]=1; g_combat_mode=1; g_ahp[slot]=6; g_amaxhp[slot]=30;
                g_actx[slot]=(uint16_t)(w.px&(U6_SURFACE_TILES-1));        /* 2 tiles SOUTH of the avatar, */
                g_acty[slot]=(uint16_t)((w.py+2)&(U6_SURFACE_TILES-1));    /* so it flees SOUTH down the open carpet */
                check_flee(slot);
                { char nm[24]; obj_name(npc_tile(slot),nm,sizeof nm); int n=msgcat(0,nm); msgcat(n," flees!"); }
                dbg_num("CB4 flee slug start y=", g_acty[slot]);   /* console proof of retreat */
                for(int s=0;s<3;s++){ w.clock+=10; g_hour=clock_hour(&w); g_minutes=clock_minutes(&w); step_npcs(&w); }
                dbg_num("CB4 flee slug end   y=", g_acty[slot]);   /* should be SOUTH (larger y), away from avatar */
            } else if(mode==5){                           /* CB4 COWARD (worktype COMBAT_RETREAT): flee on sight -> assault when struck */
                #define MASK (U6_SURFACE_TILES-1)
                g_coward[slot]=1; g_ahp[slot]=30; g_amaxhp[slot]=30;          /* a coward, NOT hostile */
                g_actx[slot]=(uint16_t)(w.px&MASK); g_acty[slot]=(uint16_t)((w.py+3)&MASK);  /* 3 S, in sight */
                { char nm[24]; obj_name(npc_tile(slot),nm,sizeof nm); int n=msgcat(0,nm); msgcat(n," cowers!"); }
                dbg_num("coward flee start y=", g_acty[slot]);
                for(int s=0;s<2;s++){ w.clock+=10; g_hour=clock_hour(&w); g_minutes=clock_minutes(&w); step_npcs(&w); }
                dbg_num("coward flee end   y=", g_acty[slot]);                /* larger y => fled SOUTH (away) */
                g_hostile[slot]=1; g_coward[slot]=0;                          /* STRUCK: do_attack's transition (coward -> ASSAULT) */
                dbg_num("after-hit hostile=", g_hostile[slot]);              /* 1 */
                dbg_num("after-hit coward =", g_coward[slot]);               /* 0 */
                { int ay0=g_acty[slot];                                       /* now hostile: it should APPROACH the party */
                  for(int s=0;s<2;s++){ w.clock+=10; g_hour=clock_hour(&w); g_minutes=clock_minutes(&w); step_npcs(&w); }
                  dbg_num("assault moved-toward (y0-y)=", ay0 - g_acty[slot]); }   /* >0 => now approaches the party */
                #undef MASK
            } else if(mode==6){                           /* TRAINING GATE: exp accrues but only a trainer levels you */
                int lb=-1; for(int i=2;i<U6_MAX_ACTORS;i++) if(g_trainer[i]){ lb=i; break; }
                dbg_num("trainer actor id=", lb);            /* Lord British */
                dbg_num("level before=", g_alevel[1]);       /* 3 (avatar HP 90) */
                grant_exp(1,40);                             /* a kill's worth of exp: NO level-up by itself */
                dbg_num("after kill: level=", g_alevel[1]);  /* still 3 (gated by training) */
                g_aexp[1] = (uint16_t)(exp_for_level(g_alevel[1]+1) - 1); /* one short of level 4 */
                try_train(lb);
                dbg_num("train (short) level=", g_alevel[1]);/* still 3 */
                g_aexp[1] = (uint16_t)exp_for_level(g_alevel[1]+1);       /* now at the threshold */
                try_train(lb);
                dbg_num("train (earned) level=", g_alevel[1]);/* 4 */
                dbg_num("maxhp after =", g_amaxhp[1]);       /* 120 (= level*30) */
                /* PARTY LEVELING: a companion (actor 2) advances via the same training visit */
                dbg_num("comp(2) level before=", g_alevel[2]);
                g_aexp[2] = (uint16_t)exp_for_level(g_alevel[2]+1);   /* it has earned the next level */
                try_train(lb);
                dbg_num("comp(2) level after =", g_alevel[2]);        /* +1 */
                dbg_num("comp(2) maxhp after =", g_amaxhp[2]);        /* level*30 */
            } else if(mode==8){                           /* ALIGNMENT TUNING: only genuine combatants are hostile */
                int total=0, near=0, party_h=0;
                for(int i=2;i<U6_MAX_ACTORS;i++){
                    if(is_party_member(i)){ if(actor_hostile(i)) party_h++; continue; }
                    if(!actor_hostile(i)) continue; total++;
                    int ax,ay; if(actor_pos(i,&ax,&ay)){
                        int dx=ax>(int)w.px?ax-(int)w.px:(int)w.px-ax, dy=ay>(int)w.py?ay-(int)w.py:(int)w.py-ay;
                        if((dx>dy?dx:dy)<=12) near++; } }
                dbg_num("engine hostiles total=", total);    /* 3 (the ASSAULT gargoyles), not 101 */
                dbg_num("hostiles near avatar =", near);     /* 3 */
                dbg_num("party flagged hostile=", party_h);  /* 0 -> no friendly fire */
            } else if(mode==9){                           /* DYNAMIC AGGRO-ON-SIGHT: lurk until it PERCEIVES the party */
                g_ahp[slot]=20; g_hostile[slot]=1; g_alert[slot]=0;  /* hostile, but unaware */
                g_actx[slot]=(uint16_t)w.px;                 /* FAR: 8 tiles south, out of AGGRO_SEE(5) */
                g_acty[slot]=(uint16_t)((w.py+8)&(U6_SURFACE_TILES-1));
                dbg_num("sees party (far d=8) =", sees_party(&w,slot));   /* 0 */
                { int y0=g_acty[slot]; for(int s=0;s<5;s++) step_npcs(&w);
                  dbg_num("alerted after far    =", g_alert[slot]);       /* 0 (lurking) */
                  dbg_num("approached (far)     =", y0 - g_acty[slot]); } /* ~0 (did NOT close in) */
                g_actx[slot]=(uint16_t)w.px;                 /* NEAR: 4 tiles south, within AGGRO_SEE, LOS clear */
                g_acty[slot]=(uint16_t)((w.py+4)&(U6_SURFACE_TILES-1));
                dbg_num("sees party (near d=4)=", sees_party(&w,slot));   /* 1 */
                { int y1=g_acty[slot]; for(int s=0;s<3;s++) step_npcs(&w);
                  dbg_num("alerted after near   =", g_alert[slot]);       /* 1 (aggro!) */
                  dbg_num("approached (near)    =", y1 - g_acty[slot]); } /* >0 (closed in) */
                /* the REAL save hostiles (3 throne gargoyles, in sight at d<=3) should also have alerted */
                { int gh=0,ga=0; for(int i=2;i<U6_MAX_ACTORS;i++){ if(i==slot) continue;
                    if(g_align_hostile[i] && !is_party_member(i)){ gh++; if(g_alert[i]) ga++; } }
                  dbg_num("save gargoyles       =", gh);   /* 3 */
                  dbg_num("  of those alerted   =", ga); } /* 3 (perceived the party) */
            } else if(mode==10){                          /* LOS OBJECT SIGHT-BLOCKERS: a closed door blocks perception */
                int nsb=0; for(int32_t i=0;i<g_ow.nobjs;i++){ const wobj_t*o=&g_wobj[i]; if(o->alive && blocks_sight(o->tile)) nsb++; }
                dbg_num("sight-blocking objects=", nsb);     /* >0: build_blocked populated g_sightblk */
                g_ahp[slot]=20; g_hostile[slot]=1; g_alert[slot]=0;
                g_actx[slot]=(uint16_t)w.px; g_acty[slot]=(uint16_t)((w.py+4)&(U6_SURFACE_TILES-1));
                dbg_num("sees party (clear)   =", sees_party(&w,slot));   /* 1 (open carpet) */
                int found=-1; for(int32_t i=0;i<g_ow.nobjs;i++) if(g_wobj[i].alive && blocks_sight(g_wobj[i].tile)){ found=i; break; }
                if(found>=0){
                    uint16_t ox=g_wobj[found].x, oy=g_wobj[found].y;     /* move a real sight-blocker between hostile & avatar */
                    g_wobj[found].x=(uint16_t)w.px; g_wobj[found].y=(uint16_t)((w.py+2)&(U6_SURFACE_TILES-1));
                    build_blocked();
                    dbg_num("sees party (door btwn)=", sees_party(&w,slot)); /* 0 (object blocks LOS) */
                    g_wobj[found].x=ox; g_wobj[found].y=oy; build_blocked();  /* restore */
                    dbg_num("sees party (restored)=", sees_party(&w,slot));   /* 1 */
                } else dbg_num("(no sight-block obj)  =", -1);
            } else if(mode==11){                          /* INCREMENTAL SCROLL diff-test: scrolled static buffer == full rebuild? */
                int PX=(int)w.px, PY=(int)w.py;
                static uint8_t ref[TMP_T*TMP_T];
                int dd[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
                const char* nm[4]={"diff dx=+1 =","diff dx=-1 =","diff dy=+1 =","diff dy=-1 ="};
                for(int k=0;k<4;k++){ int ddx=dd[k][0], ddy=dd[k][1];
                    w.px=(uint16_t)((PX+ddx)&(U6_SURFACE_TILES-1)); w.py=(uint16_t)((PY+ddy)&(U6_SURFACE_TILES-1));
                    build_static(&w); memcpy(ref,g_tmp_map,sizeof ref);            /* reference: full build at P+d */
                    w.px=(uint16_t)PX; w.py=(uint16_t)PY; build_static(&w);          /* build at P */
                    w.px=(uint16_t)((PX+ddx)&(U6_SURFACE_TILES-1)); w.py=(uint16_t)((PY+ddy)&(U6_SURFACE_TILES-1));
                    scroll_static(&w,ddx,ddy);                                       /* then scroll P->P+d */
                    int diff=0; for(int i=0;i<TMP_T*TMP_T;i++) if(g_tmp_map[i]!=ref[i]) diff++;
                    dbg_num(nm[k], diff);                                            /* 0 = byte-identical */
                }
                w.px=(uint16_t)PX; w.py=(uint16_t)PY; g_tmp_valid=0;
            } else if(mode==12){                          /* STAT TRAINERS: raise the avatar's lowest stat for gold (cap 30) */
                dbg_num("gold before =", (int)g_cg.gold);
                dbg_num("str=", g_astr[1]); dbg_num("dex=", g_adex[1]); dbg_num("int=", g_aint[1]);
                int s1=train_stat(1);                        /* raises the lowest stat, -100 gold */
                dbg_num("trained stat(1str2dex3int)=", s1);
                dbg_num("gold after  =", (int)g_cg.gold);
                dbg_num("str=", g_astr[1]); dbg_num("dex=", g_adex[1]); dbg_num("int=", g_aint[1]);
                int n=0; while(train_stat(1)) n++;            /* spend the rest */
                dbg_num("further raises until broke=", n);    /* 500/100 = 5 total */
                dbg_num("gold final  =", (int)g_cg.gold);     /* < 100 */
            } else if(mode==13){                          /* AUTHENTIC ALIGNMENT: hostility = per-actor U6 alignment (status 0x800 bit 0x20) */
                dbg_num("enemy gargoyle 203 hostile=", actor_hostile(203));  /* 1 (EVIL, pre-peace) */
                dbg_num("citizen gargoyle 164 host.=", actor_hostile(164));  /* 0 (NEUTRAL — peace is per-actor) */
                dbg_num("party member 3 hostile   =", actor_hostile(3));     /* 0 (GOOD) */
                dbg_num("Lord British 5 hostile   =", actor_hostile(5));     /* 0 (NEUTRAL) */
                if(!g_hostile[164]) g_hostile[164]=1;                        /* strike the citizen gargoyle -> it defends */
                dbg_num("citizen gargoyle if struck=", actor_hostile(164));  /* 1 */
                g_hostile[164]=0;                                            /* restore */
            } else if(mode==14){                          /* CONTAINERS + STACKS: search spills contents; Get takes the stack */
                int ci=-1, nkids=0;                          /* a world CHEST holding gold (validates the open/close toggle too) */
                for(int32_t i=0;i<g_ow.nobjs && ci<0;i++){
                    if(!g_wobj[i].alive || g_wobj[i].obj_n!=98) continue;
                    if((int)g_wobj[i].tile - basetile(98) >= 2) continue;   /* skip LOCKED chests */
                    int k=0,hasgold=0;
                    for(int32_t j=0;j<g_ow.nobjs;j++) if(!g_wobj[j].alive && g_wobj[j].parent==(uint16_t)i){ k++; if(g_wobj[j].obj_n==88) hasgold=1; }
                    if(k>0 && hasgold){ ci=(int)i; nkids=k; } }
                dbg_num("container obj_n =", ci>=0?g_wobj[ci].obj_n:-1);   /* 98 = chest */
                dbg_num("children inside =", nkids);
                if(ci>=0){
                    int cx=g_wobj[ci].x, cy=g_wobj[ci].y, before=0, after=0;
                    for(int32_t j=0;j<g_ow.nobjs;j++) if(g_wobj[j].alive && g_wobj[j].x==cx && g_wobj[j].y==cy) before++;
                    dbg_num("on tile before  =", before);
                    use_object(ci);                          /* U)se the chest: open + "Searching here, you find ..." */
                    dbg_num("chest frame now =", (int)g_wobj[ci].tile - basetile(g_wobj[ci].obj_n));  /* 0 = open */
                    for(int32_t j=0;j<g_ow.nobjs;j++) if(g_wobj[j].alive && g_wobj[j].x==cx && g_wobj[j].y==cy) after++;
                    dbg_num("on tile after   =", after);     /* before + children (spilled) */
                    int gi=-1; for(int32_t j=0;j<g_ow.nobjs;j++)
                        if(g_wobj[j].alive && g_wobj[j].obj_n==88 && g_wobj[j].x==cx && g_wobj[j].y==cy){ gi=(int)j; break; }
                    dbg_num("spilled gold qty=", gi>=0?g_wobj[gi].qty:-1);  /* the stack qty16 */
                    if(gi>=0){ int inv0=g_ow.ninv;
                        objworld_get(&g_ow,gi);              /* G)et takes the WHOLE stack */
                        int q=-1; for(int i2=0;i2<g_ow.ninv;i2++) if(g_ow.inv[i2].obj_n==88) q=g_ow.inv[i2].qty;
                        dbg_num("inv gold qty    =", q);
                        dbg_num("inv stacks +    =", g_ow.ninv-inv0); }
                    use_object(ci);                          /* U)se again: closes the chest */
                    dbg_num("chest frame now =", (int)g_wobj[ci].tile - basetile(g_wobj[ci].obj_n));  /* 1 = closed */
                }
            } else if(mode==15){                          /* KEYS + LOCKS: the castle's locked door + its key (usecode_use.md) */
                int di=-1, ki=-1;                            /* door (297) at (296,357) keyid 14; key (64 q14) at (298,355) */
                for(int32_t i=0;i<g_ow.nobjs;i++){
                    if(!g_wobj[i].alive) continue;
                    if(g_wobj[i].obj_n>=297&&g_wobj[i].obj_n<=300&&g_wobj[i].x==296&&g_wobj[i].y==357) di=(int)i;
                    if(g_wobj[i].obj_n==64&&g_wobj[i].x==298&&g_wobj[i].y==355) ki=(int)i; }
                dbg_num("door frame      =", di>=0?(int)g_wobj[di].tile-basetile(g_wobj[di].obj_n):-1);  /* 11 = locked */
                dbg_num("door key id     =", di>=0?g_wobj[di].quality:-1);                                /* 14 */
                if(di>=0){ use_object(di);                    /* no key yet -> "Locked.", frame unchanged */
                    dbg_num("use w/o key: fr =", (int)g_wobj[di].tile-basetile(g_wobj[di].obj_n)); }      /* 11 */
                dbg_num("key on map qual =", ki>=0?g_wobj[ki].quality:-1);                                /* 14 */
                if(ki>=0){ dbg_num("got key         =", objworld_get(&g_ow,ki));                          /* 1 */
                    dbg_num("has key(14)     =", objworld_has(&g_ow,64,14)); }                            /* 1 */
                if(di>=0){ use_object(di);                    /* with the key -> "Unlocked." (11 -> 7) */
                    dbg_num("use w/ key: fr  =", (int)g_wobj[di].tile-basetile(g_wobj[di].obj_n));        /* 7 = closed */
                    use_object(di);                           /* now a normal closed door -> opens (7 -> 3) */
                    dbg_num("use again: fr   =", (int)g_wobj[di].tile-basetile(g_wobj[di].obj_n)); }      /* 3 = open */
                { int li=-1;                                  /* the locked chest (fr 2, key id 0) at (293,351) */
                  for(int32_t i=0;i<g_ow.nobjs;i++)
                      if(g_wobj[i].alive&&g_wobj[i].obj_n==98&&g_wobj[i].x==293&&g_wobj[i].y==351){ li=(int)i; break; }
                  if(li>=0){ dbg_num("locked chest fr =", (int)g_wobj[li].tile-basetile(98));             /* 2 */
                      dbg_num("chest searchable=", container_searchable(li));                             /* 0: Look won't spill */
                      use_object(li);                         /* keyid 0 -> "No effect.", frame unchanged */
                      dbg_num("use chest: fr   =", (int)g_wobj[li].tile-basetile(98)); } }                /* 2 */
            } else if(mode==16){                          /* MAGIC: cast Light + Heal + Fireball (magic_skills.md) */
                int av=1, intel=g_aint[av]?g_aint[av]:15;
                dbg_num("avatar int      =", g_aint[av]);
                dbg_num("mana max        =", g_amaxmana[av]);
                dbg_num("castable circle =", magic_castable_circle(intel));
                dbg_num("reagent mask    =", reagent_mask());               /* 0xff = all 8 seeded */
                /* gate: an 8th-circle spell the Avatar can't reach is refused (non-zero result) */
                int mk=magic_find("Mass Kill");
                dbg_num("masskill gate   =", (int)magic_check(mk,g_known[mk],intel,reagent_mask(),g_amana[av]));
                /* Light: brightens the map for the capture */
                cast_spell(&w,av,magic_find("Light"),0,0);
                dbg_num("light turns     =", g_spell_light_turns);          /* >0 = lit */
                /* Heal: wound the Avatar, then restore HP (consumes ginseng + 1 mana) */
                g_ahp[av]=g_amaxhp[av]>40?(uint8_t)(g_amaxhp[av]-40):1; sync_party_hp();
                dbg_num("hp before heal  =", g_ahp[av]);
                dbg_num("ginseng before  =", objworld_count(&g_ow,68));
                cast_spell(&w,av,magic_find("Heal"),0,0);
                dbg_num("hp after heal   =", g_ahp[av]);
                dbg_num("ginseng after   =", objworld_count(&g_ow,68));     /* -1 */
                dbg_num("mana after heal =", g_amana[av]);
                /* Fireball: spawn a slug down a CLEAR line (no friendly NPC) and slay it */
                { static const int dd[4][2]={{0,1},{0,-1},{1,0},{-1,0}}; int ddx=1,ddy=0;
                  for(int k=0;k<4;k++){ int dxk=dd[k][0],dyk=dd[k][1];
                    int x1=(int)((w.px+dxk)&(U6_SURFACE_TILES-1)), y1=(int)((w.py+dyk)&(U6_SURFACE_TILES-1));
                    if(npc_at(&w,x1,y1)<0 && !sight_blocked_at(&w,x1,y1)){ ddx=dxk; ddy=dyk; break; } }
                  g_atile[slot]=(uint16_t)bt; g_abase[slot]=(uint16_t)bt; g_atpd[slot]=0; g_facing[slot]=2;
                  g_actx[slot]=(uint16_t)((w.px+ddx)&(U6_SURFACE_TILES-1)); g_acty[slot]=(uint16_t)((w.py+ddy)&(U6_SURFACE_TILES-1));
                  g_actz[slot]=0; g_adex[slot]=8; g_astr[slot]=5; g_ahp[slot]=10; g_amaxhp[slot]=10;
                  cast_spell(&w,av,magic_find("Fireball"),ddx,ddy); }
                dbg_num("slug hp post-fb =", g_ahp[slot]);                   /* 0 = slain */
                dbg_num("mana post-fb    =", g_amana[av]);
            } else if(mode==17){                          /* MAGIC UI: the C)ast spell-selection menu */
                open_cast_menu();                          /* build the castable-spell list + open the menu */
                dbg_num("castable spells =", g_ncast);      /* 6 at Int 15 (Great Heal circle 4 is gated out) */
                for(int i=0;i<g_ncast;i++){ Cconws("  "); Cconws(g_spells[g_castlist[i]].name); Cconws("\r\n"); }
                dbg_num("parse '3' ->idx =", cast_menu_parse("3"));         /* the 3rd listed spell */
                dbg_num("parse 'fire'->  =", cast_menu_parse("fire"));      /* name-prefix -> Fireball */
                dbg_num("Great Heal knownbut gated (in list=0?) =",
                        (g_known[magic_find("Great Heal")] && cast_menu_parse("Great Heal")<0)?1:0);
                g_inbuf[0]='3'; g_inbuf[1]=0; g_inlen=1;    /* partial typed selection, shown in the box */
                /* leave g_casting=1 so the captured frame shows the live menu */
            } else if(mode==18){                          /* MAGIC: Create Food + Unlock Magic (magic_skills.md §5) */
                int av=1;
                int before=objworld_count(&g_ow,129);       /* Create Food: conjure a portion of meat (obj 129) */
                cast_spell(&w,av,magic_find("Create Food"),0,0);
                dbg_num("food before     =", before);
                dbg_num("food after      =", objworld_count(&g_ow,129));   /* +1 */
                dbg_num("mana post-food  =", g_amana[av]);                 /* -1 (circle 1)  */
                /* Unlock Magic: place a MAGIC-locked oaken door east + dispel it */
                int di=(int)g_ow.nobjs;
                if(di<MAXWOBJ){
                    g_wobj[di].obj_n=298; g_wobj[di].quality=0; g_wobj[di].qty=1;
                    g_wobj[di].x=(uint16_t)((w.px+1)&(U6_SURFACE_TILES-1)); g_wobj[di].y=(uint16_t)(w.py&(U6_SURFACE_TILES-1));
                    g_wobj[di].tile=(uint16_t)(basetile(298)+13);   /* 13 = magic-locked */
                    g_wobj[di].parent=WOBJ_PARENT_NONE; g_wobj[di].alive=1; g_ow.nobjs++;
                    dbg_num("door frame pre  =", (int)g_wobj[di].tile-basetile(298));  /* 13 */
                    cast_spell(&w,av,magic_find("Unlock Magic"),1,0);                   /* cast east */
                    dbg_num("door frame post =", (int)g_wobj[di].tile-basetile(298));  /* 5 = closed */
                    dbg_num("mana post-unlock=", g_amana[av]);                          /* -2 more (circle 2) */
                }
            } else if(mode==19){                          /* MAGIC: field-tile layer (magic_skills.md §5a) */
                int av=1; g_aint[av]=24;                     /* raise Int so circle 4-5 field spells are castable */
                dbg_num("castable circle =", magic_castable_circle(g_aint[av]));  /* 6 */
                /* a slug 1 tile east, to burn in a Fire Field */
                g_atile[slot]=(uint16_t)bt; g_abase[slot]=(uint16_t)bt; g_atpd[slot]=0; g_facing[slot]=2;
                g_actx[slot]=(uint16_t)((w.px+1)&(U6_SURFACE_TILES-1)); g_acty[slot]=(uint16_t)(w.py&(U6_SURFACE_TILES-1));
                g_actz[slot]=0; g_adex[slot]=8; g_astr[slot]=5; g_ahp[slot]=30; g_amaxhp[slot]=30;
                cast_spell(&w,av,magic_find("Fire Field"),1,0);            /* fire field east, onto the slug */
                dbg_num("fields placed   =", g_nfields);                    /* 1 */
                dbg_num("slug hp pre-tick=", g_ahp[slot]);                  /* 30 */
                fields_tick(&w);                                           /* one turn of field damage */
                dbg_num("slug hp post-tik=", g_ahp[slot]);                 /* < 30 = burned */
                dbg_num("mana post-fire  =", g_amana[av]);                  /* 15-4 = 11 (circle 4) */
                /* Energy Field blocks passage; Dispel Field frees it again — test a
                 * VERIFIED-PASSABLE adjacent tile (the throne room walls many directions). */
                static const int dirs[4][2]={{0,-1},{-1,0},{0,1},{1,0}};
                int edx=0,edy=0,ex=-1,ey=-1;
                for(int k=0;k<4;k++){ int tx=(int)((w.px+dirs[k][0])&(U6_SURFACE_TILES-1)), ty=(int)((w.py+dirs[k][1])&(U6_SURFACE_TILES-1));
                    if(world_is_passable(&w,tx,ty) && npc_at(&w,tx,ty)<0){ edx=dirs[k][0]; edy=dirs[k][1]; ex=tx; ey=ty; break; } }
                dbg_num("efield passable0=", ex>=0?world_is_passable(&w,ex,ey):-1);  /* 1 before */
                cast_spell(&w,av,magic_find("Energy Field"),edx,edy);
                dbg_num("efield blocked  =", ex>=0?world_is_passable(&w,ex,ey):-1);  /* 0 = blocked */
                cast_spell(&w,av,magic_find("Dispel Field"),edx,edy);
                dbg_num("efield freed    =", ex>=0?world_is_passable(&w,ex,ey):-1);  /* 1 = freed */
                dbg_num("fields remaining=", g_nfields);                    /* just the fire field east */
            } else if(mode==20){                          /* MAGIC: status effects (magic_skills.md §5b) */
                int av=1; g_aint[av]=24; g_amaxmana[av]=24; g_amana[av]=24;   /* mage-tier Int + mana */
                /* place a hostile slug down a CLEAR line (no existing NPC — the throne room's
                 * east tile holds Lord British, whose lower actor id would be targeted first) */
                static const int dd[4][2]={{0,1},{0,-1},{1,0},{-1,0}}; int ddx=1,ddy=0;
                for(int k=0;k<4;k++){ int x1=(int)((w.px+dd[k][0])&(U6_SURFACE_TILES-1)), y1=(int)((w.py+dd[k][1])&(U6_SURFACE_TILES-1));
                    if(npc_at(&w,x1,y1)<0 && world_is_passable(&w,x1,y1)){ ddx=dd[k][0]; ddy=dd[k][1]; break; } }
                g_atile[slot]=(uint16_t)bt; g_abase[slot]=(uint16_t)bt; g_atpd[slot]=0; g_facing[slot]=2;
                g_actx[slot]=(uint16_t)((w.px+ddx)&(U6_SURFACE_TILES-1)); g_acty[slot]=(uint16_t)((w.py+ddy)&(U6_SURFACE_TILES-1));
                g_actz[slot]=0; g_adex[slot]=8; g_astr[slot]=5; g_ahp[slot]=30; g_amaxhp[slot]=30; g_align_hostile[slot]=1;
                dbg_num("target is slug? =", npc_at(&w,(int)g_actx[slot],(int)g_acty[slot])==slot);  /* 1 */
                /* Charm: a hostile becomes pacified */
                dbg_num("hostile pre-charm=", actor_hostile(slot));   /* 1 */
                cast_spell(&w,av,magic_find("Charm"),ddx,ddy);
                dbg_num("hostile post-chrm=", actor_hostile(slot));   /* 0 = pacified */
                /* Poison: damage-over-time on the next tick */
                cast_spell(&w,av,magic_find("Poison"),ddx,ddy);
                dbg_num("slug hp pre-tick =", g_ahp[slot]);           /* 30 */
                status_tick();
                dbg_num("slug hp post-tick=", g_ahp[slot]);           /* < 30 = poisoned */
                dbg_num("poison turns left=", g_poison[slot]);        /* 9 */
                /* Paralyze: sets the skip timer */
                cast_spell(&w,av,magic_find("Paralyze"),ddx,ddy);
                dbg_num("paralyze turns   =", g_paralyze[slot]);      /* 12 */
                /* Protection: party takes half damage */
                cast_spell(&w,av,magic_find("Protection"),0,0);
                dbg_num("avatar protected =", g_protect[av]);         /* 20 */
                dbg_num("dmg 10 -> protect=", status_dmg(av,10));     /* 5 */
                dbg_num("dmg 10 -> unprot =", status_dmg(slot,10));   /* 10 */
                dbg_num("curse role 20->  =", status_hit_role(av,20));/* 20 (avatar not cursed) */
            } else if(mode==21){                          /* MAGIC: summoning (magic_skills.md §5c) */
                int av=1; g_aint[av]=32; g_amaxmana[av]=40; g_amana[av]=40;   /* archmage: circle 8 + mana */
                /* two distinct VERIFIED-clear directions (8-way: the throne room crowds the cardinals
                 * with party members) — d1 for allies, d2 for the slug so Clone aims a clean line */
                static const int dd[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,-1},{1,-1},{-1,1}}; int d1=-1,d2=-1;
                for(int k=0;k<8;k++){ int x=(int)((w.px+dd[k][0])&(U6_SURFACE_TILES-1)), y=(int)((w.py+dd[k][1])&(U6_SURFACE_TILES-1));
                    if(npc_at(&w,x,y)<0 && world_is_passable(&w,x,y)){ if(d1<0)d1=k; else if(d2<0){d2=k;break;} } }
                if(d1<0)d1=0; if(d2<0)d2=1;
                /* Conjure + Summon allied creatures toward d1 */
                cast_spell(&w,av,magic_find("Conjure"),dd[d1][0],dd[d1][1]);
                cast_spell(&w,av,magic_find("Summon"),dd[d1][0],dd[d1][1]);
                int nally=0; for(int a=0;a<U6_MAX_ACTORS;a++) if(g_summon[a]==1) nally++;
                dbg_num("allied summons  =", nally);                 /* 2 (bat + daemon) */
                dbg_num("mana spent      =", 40-g_amana[av]);         /* conjure4 + summon8 = 12 */
                /* place a hostile slug toward d2, then Clone it (a hostile summon) */
                int sx=(int)((w.px+dd[d2][0])&(U6_SURFACE_TILES-1)), sy=(int)((w.py+dd[d2][1])&(U6_SURFACE_TILES-1));
                g_atile[slot]=(uint16_t)basetile(365); g_abase[slot]=(uint16_t)basetile(365); g_atpd[slot]=0; g_facing[slot]=2;
                g_actx[slot]=(uint16_t)sx; g_acty[slot]=(uint16_t)sy; g_actz[slot]=0;
                g_ahp[slot]=30; g_amaxhp[slot]=30; g_adex[slot]=8; g_astr[slot]=5; g_align_hostile[slot]=1; g_summon[slot]=0;
                int nh0=0; for(int a=2;a<U6_MAX_ACTORS;a++) if(actor_hostile(a)&&!is_party_member(a)) nh0++;
                cast_spell(&w,av,magic_find("Clone"),dd[d2][0],dd[d2][1]);
                int nh1=0; for(int a=2;a<U6_MAX_ACTORS;a++) if(actor_hostile(a)&&!is_party_member(a)) nh1++;
                dbg_num("hostiles pre    =", nh0);                    /* 1 (the slug) */
                dbg_num("hostiles cloned =", nh1);                    /* 2 (slug + clone) */
                /* allied AI: advance turns; an ally hunts + strikes a hostile */
                dbg_num("slug hp pre     =", g_ahp[slot]);            /* 30 */
                for(int t=0;t<12;t++) step_npcs(&w);
                dbg_num("slug hp post12t =", g_ahp[slot]);            /* < 30 (or 0) = an ally reached it */
                /* lifetime: run past the summons' life; every summon vanishes */
                for(int t=0;t<40;t++) step_npcs(&w);
                int nsum=0; for(int a=0;a<U6_MAX_ACTORS;a++) if(g_summon[a] && g_atile[a]!=0xffff) nsum++;
                dbg_num("summons after52t=", nsum);                   /* 0 = all expired */
            } else if(mode==22){                          /* MAGIC: travel (Help + Gate Travel, magic_skills.md §5d) */
                int av=1; g_aint[av]=30; g_amaxmana[av]=40; g_amana[av]=40;   /* circle 7 + mana */
                dbg_num("avatar x start  =", (int)w.px);
                dbg_num("avatar y start  =", (int)w.py);
                int lb=-1; for(int a=2;a<U6_MAX_ACTORS;a++) if(g_trainer[a]){ lb=a; break; }
                int lx=-1,ly=-1; if(lb>=0) actor_pos(lb,&lx,&ly);
                dbg_num("Lord British x  =", lx);
                dbg_num("Lord British y  =", ly);
                /* Help: recall the party to Lord British */
                cast_spell(&w,av,magic_find("Help"),0,0);
                dbg_num("avatar x recall =", (int)w.px);
                dbg_num("avatar y recall =", (int)w.py);
                if(lb>=0){ int cx=lx>(int)w.px?lx-(int)w.px:(int)w.px-lx, cy=ly>(int)w.py?ly-(int)w.py:(int)w.py-ly;
                    dbg_num("near LB (<=1)   =", (cx<=1&&cy<=1)?1:0); }               /* 1 */
                /* Gate Travel: moongate to a moonstone destination */
                if(g_savetmpl && g_objlist_off>=0){ const uint8_t*ml=g_savetmpl+g_objlist_off+0x1c1b;
                    dbg_num("moonstone0 x    =", ml[0]|(ml[1]<<8));
                    dbg_num("moonstone0 y    =", ml[2]|(ml[3]<<8)); }
                cast_spell(&w,av,magic_find("Gate Travel"),0,0);
                dbg_num("avatar x gated  =", (int)w.px);                              /* jumped to a moonstone */
                dbg_num("avatar y gated  =", (int)w.py);
                { int a=g_nparty>1?g_party[1].actor:2, ax,ay; if(actor_pos(a,&ax,&ay)){
                    int cx=ax>(int)w.px?ax-(int)w.px:(int)w.px-ax, cy=ay>(int)w.py?ay-(int)w.py:(int)w.py-ay;
                    dbg_num("party adjacent  =", (cx<=1&&cy<=1)?1:0); } }             /* 1 */
                dbg_num("mana spent      =", 40-g_amana[av]);                          /* help1 + gate7 = 8 */
            } else if(mode==23){                          /* MAGIC: detection/utility (magic_skills.md §5e) */
                int av=1; g_aint[av]=24; g_amaxmana[av]=40; g_amana[av]=40;   /* circle 6 + mana */
                int M=U6_SURFACE_TILES-1;
                /* Locate: reports the party position */
                cast_spell(&w,av,magic_find("Locate"),0,0);
                dbg_num("locate x        =", (int)w.px);
                dbg_num("locate y        =", (int)w.py);
                /* Telekinesis: append a CLOSED door on a clear adjacent tile, operate it from afar */
                static const int dd[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,-1},{1,-1},{-1,1}}; int td=0;
                for(int k=0;k<8;k++){ int x1=(int)((w.px+dd[k][0])&M), y1=(int)((w.py+dd[k][1])&M);
                    if(npc_at(&w,x1,y1)<0 && world_is_passable(&w,x1,y1) && objworld_at(&g_ow,x1,y1)<0){ td=k; break; } }
                int tdi=(int)g_ow.nobjs;
                if(tdi<MAXWOBJ){ g_wobj[tdi].obj_n=298; g_wobj[tdi].quality=0; g_wobj[tdi].qty=1;
                    g_wobj[tdi].x=(uint16_t)((w.px+dd[td][0])&M); g_wobj[tdi].y=(uint16_t)((w.py+dd[td][1])&M);
                    g_wobj[tdi].tile=(uint16_t)(basetile(298)+4); g_wobj[tdi].parent=WOBJ_PARENT_NONE; g_wobj[tdi].alive=1; g_ow.nobjs++;
                    dbg_num("tk door pre fr  =", (int)g_wobj[tdi].tile-basetile(298));   /* 4 = closed */
                    cast_spell(&w,av,magic_find("Telekinesis"),dd[td][0],dd[td][1]);
                    dbg_num("tk door post fr =", (int)g_wobj[tdi].tile-basetile(298)); }  /* 0 = opened from afar */
                /* X-ray: see-through-walls timer */
                cast_spell(&w,av,magic_find("X-ray"),0,0);
                dbg_num("xray turns      =", g_xray_turns);          /* 30 */
                /* Detect Magic: append a MAGIC-locked door nearby, then sense it (message last) */
                int di=(int)g_ow.nobjs;
                if(di<MAXWOBJ){ g_wobj[di].obj_n=298; g_wobj[di].quality=0; g_wobj[di].qty=1;
                    g_wobj[di].x=(uint16_t)((w.px+2)&M); g_wobj[di].y=(uint16_t)(w.py&M);
                    g_wobj[di].tile=(uint16_t)(basetile(298)+13); g_wobj[di].parent=WOBJ_PARENT_NONE; g_wobj[di].alive=1; g_ow.nobjs++; }
                { int cnt=0; for(int32_t i=0;i<g_ow.nobjs;i++){ const wobj_t*o=&g_wobj[i]; if(!o->alive) continue;
                    int cx=(int)o->x-(int)w.px, cy=(int)o->y-(int)w.py; if(cx<0)cx=-cx; if(cy<0)cy=-cy; if(cx>10||cy>10) continue;
                    int fr=(int)o->tile-basetile(o->obj_n);
                    if((o->obj_n>=297&&o->obj_n<=301&&(fr==13||fr==15))||(o->obj_n==98&&fr==3)) cnt++; }
                  dbg_num("magic auras near=", cnt); }                /* >=1 */
                cast_spell(&w,av,magic_find("Detect Magic"),0,0);
                dbg_num("mana spent      =", 40-g_amana[av]);         /* locate4 + tk2 + xray5 + detect1 = 12 */
            } else if(mode==24){                          /* PLAYABILITY: mana display + regen + Rest */
                int av=1;                                   /* uses the LOADED Avatar mana (2xInt = 30) */
                dbg_num("mana max        =", g_amaxmana[av]);         /* 30 */
                for(int c=0;c<10;c++) cast_spell(&w,av,magic_find("Light"),0,0);  /* spend 10 mana */
                dbg_num("mana after casts=", g_amana[av]);           /* 20 */
                int before=g_amana[av];
                for(int s=0;s<16;s++){ w.clock+=10; g_hour=clock_hour(&w); g_minutes=clock_minutes(&w); step_npcs(&w); }
                dbg_num("mana +regen 16t =", g_amana[av]);           /* +2 (1 per 8 turns) */
                dbg_num("regen delta     =", g_amana[av]-before);    /* 2 */
                g_ahp[av]=g_amaxhp[av]>40?(uint8_t)(g_amaxhp[av]-40):1;
                do_rest(&w);
                dbg_num("mana after rest =", g_amana[av]);           /* = max 30 */
                dbg_num("hp after rest   =", g_ahp[av]);             /* = maxhp */
            } else if(mode==25){                          /* PLAY: click-to-travel pathing */
                int M=U6_SURFACE_TILES-1, startx=(int)w.px;
                /* a cardinal direction with 3 clear passable tiles ahead = a reachable target */
                static const int dd[4][2]={{1,0},{-1,0},{0,1},{0,-1}}; int di=-1;
                for(int k=0;k<4 && di<0;k++){ int ok=1;
                    for(int r=1;r<=3;r++){ int x=(int)((w.px+dd[k][0]*r)&M), y=(int)((w.py+dd[k][1]*r)&M);
                        if(!world_is_passable(&w,x,y)||npc_at(&w,x,y)>=0){ ok=0; break; } }
                    if(ok) di=k; }
                if(di<0) di=0;
                int tx=(int)((w.px+dd[di][0]*3)&M), ty=(int)((w.py+dd[di][1]*3)&M);
                dbg_num("travel start x  =", startx);
                dbg_num("travel target x =", tx);
                int steps=0, arrived=0;
                for(int t=0;t<12;t++){ int sx,sy;
                    if((int)w.px==tx && (int)w.py==ty){ arrived=1; break; }
                    if(npc_pathstep(&w,(int)w.px,(int)w.py,tx,ty,-1,-1,&sx,&sy) && (sx||sy) && world_try_move(&w,sx,sy)) steps++;
                    else break; }
                if((int)w.px==tx && (int)w.py==ty) arrived=1;
                dbg_num("travel steps    =", steps);       /* ~3 */
                dbg_num("arrived         =", arrived);      /* 1 */
                dbg_num("final x         =", (int)w.px);    /* == target */
            } else if(mode>=2){                           /* CB2: hostile assault over several turns */
                g_ahp[slot]=20; g_hostile[slot]=1;
                for(int s=0;s<12;s++){ w.clock+=10; g_hour=clock_hour(&w); g_minutes=clock_minutes(&w); step_npcs(&w); }
            } else {                                      /* CB1: avatar attacks until it dies */
                g_ahp[slot]=10;
                for(int s=0;s<8;s++){ int tx=(int)((w.px+1)&(U6_SURFACE_TILES-1)), ty=(int)(w.py&(U6_SURFACE_TILES-1));
                    if(npc_at(&w,tx,ty)<0) break; do_attack(&w,1,0); }
            }
        }
    }

    /* headless MAGIC-overview demo (§5): MAGIC.BIN[0]=1 Peer, 2 Wizard Eye (both open the modal
     * overview, so the captured frame IS the overview), 3 Invisibility+Reveal (an NPC vanishes then
     * reappears). Int is boosted so every circle is castable. Absent in normal play. */
    {   int32_t mgl=0; void*mgb=plat_load_file("MAGIC.BIN",1,&mgl);
        if(mgb){ int t=(mgl>0)?((uint8_t*)mgb)[0]:1; g_aint[1]=32;
            if(t==1) cast_spell(&w,1,magic_find("Peer"),0,0);
            else if(t==2) cast_spell(&w,1,magic_find("Wizard Eye"),0,0);
            else if(t==3){
                int npc=-1,bd=99;
                for(int i=2;i<U6_MAX_ACTORS;i++){ if(g_atile[i]==0xffff||g_actz[i]!=0) continue;
                    int cx=g_actx[i]-(int)w.px,cy=g_acty[i]-(int)w.py; if(cx<0)cx=-cx; if(cy<0)cy=-cy;
                    int d=cx>cy?cx:cy; if(d<bd){bd=d;npc=i;} }
                if(npc>=0){ dbg_num("INVIS npc=", npc); dbg_num("INVIS dist=", bd);
                    cast_spell(&w,1,magic_find("Invisibility"),g_actx[npc]-(int)w.px,g_acty[npc]-(int)w.py);
                    dbg_num("INVIS ainvis after cast=", g_ainvis[npc]);   /* expect 60 */
                    cast_spell(&w,1,magic_find("Reveal"),0,0);
                    dbg_num("REVEAL ainvis after=", g_ainvis[npc]); }     /* expect 0 (within radius 5) */
            }
        }
    }

    /* headless CONVERSATION-palette capture (M7): open a conversation before the initial draw so the
     * first frame shows a portrait under the ST's U6WY conversation palette. CONVPAL.BIN byte = NPC
     * id (0 => the nearest adjacent NPC). No-op without the sentinel. */
    {   int32_t cpl=0; uint8_t *cpb=(uint8_t*)plat_load_file("CONVPAL.BIN",1,&cpl);
        if(cpb){ int id=cpb[0];
            if(cpl>=2 && (cpb[1]&1)) g_nconvpal=0;   /* baseline: disable the swap (portrait via terrain-16) */
            if(id<=0){ for(int dy=-1;dy<=1&&id<=0;dy++) for(int dx=-1;dx<=1&&id<=0;dx++){ if(!dx&&!dy)continue;
                int t=npc_at(&w,(int)w.px+dx,(int)w.py+dy); if(t>0) id=t; } }
            if(id>0){ talk(id); dbg_num("CONVPAL talk id=", id); dbg_num("CONVPAL portrait=", g_have_pc); } }
    }

    blit_paper();
    compose_map(&w); draw_border_tiles();
    draw_conversation(ink);
    draw_status(&w,ink);
    draw_command_bar(15);
    read_mouse_logical(&g_mx,&g_my,&(int){0});
    present_frame();                               /* 320x200 UI -> 2x into 320x480 + cursor */
    dump_shot();                                   /* DEBUG: pixel-perfect 320x200 dump if SHOT.REQ staged */

#ifdef BENCH
    /* INTRO bench (gated on INTROBENCH.REQ): run the living-room scene (0) through the REAL
     * run_intro and report its per-frame cost, split HOLD vs PAN x compose/c2p+double/output.
     * BENCH.BIN = 8 big-endian u32: HOLD{compose,c2p,out,frames}, PAN{compose,c2p,out,frames}. */
    {   int32_t ibl=0; void *ib=plat_load_file("IBENCH.REQ",1,&ibl);   /* 8.3 name (GEMDOS clips >8.3) */
        if(ib){
            int save=g_nintro; if(g_nintro>1) g_nintro=1;         /* scene 0 = living room (has HOLD + PAN) */
            run_intro();
            g_nintro=save;
            rdr_shutdown();
            uint32_t hn=g_iph_n[IPH_HOLD], pn=g_iph_n[IPH_PAN];
            uint32_t r[8]={ g_iph[IPH_HOLD][0],g_iph[IPH_HOLD][1],g_iph[IPH_HOLD][2],hn,
                            g_iph[IPH_PAN][0], g_iph[IPH_PAN][1], g_iph[IPH_PAN][2], pn };
            plat_file bf=plat_fcreate("BENCH.BIN");
            if(bf!=PLAT_BADFILE){ plat_fwrite(bf,r,(int32_t)sizeof r); plat_fclose(bf); }
            dbg_num("HOLD frames=",       (int)hn);
            dbg_num("  HOLD compose ms=",  hn?(int)(g_iph[IPH_HOLD][0]*5/hn):0);
            dbg_num("  HOLD c2p+dbl ms=",  hn?(int)(g_iph[IPH_HOLD][1]*5/hn):0);
            dbg_num("  HOLD output  ms=",  hn?(int)(g_iph[IPH_HOLD][2]*5/hn):0);
            dbg_num("PAN frames=",        (int)pn);
            dbg_num("  PAN compose  ms=",  pn?(int)(g_iph[IPH_PAN][0]*5/pn):0);
            dbg_num("  PAN c2p+dbl  ms=",  pn?(int)(g_iph[IPH_PAN][1]*5/pn):0);
            dbg_num("  PAN output   ms=",  pn?(int)(g_iph[IPH_PAN][2]*5/pn):0);
            Cconws("intro bench done\r\n"); return 0;
        } }
    {   enum { N = 20 };
        /* FULL-build compose (force a rebuild each time): the no-cache baseline. */
        uint32_t tf0=plat_ticks_200hz();
        for (int i=0;i<N;i++){ g_tmp_valid=0; compose_map(&w); }
        uint32_t tf1=plat_ticks_200hz();
        /* SCROLL compose (1-tile walk each iter, alternating +/- to stay put): the WALKING cost. */
        int basepx=(int)w.px;
        compose_map(&w);                                    /* prime the cache */
        g_phase[0]=g_phase[1]=g_phase[2]=g_phase[3]=0;      /* isolate the scroll phases */
        uint32_t t0=plat_ticks_200hz();
        for (int i=0;i<N;i++){ w.px=(uint16_t)((basepx+(i&1))&(U6_SURFACE_TILES-1)); compose_map(&w); }
        uint32_t t1=plat_ticks_200hz();
        w.px=(uint16_t)basepx;
        for (int i=0;i<N;i++) rdr_present_rect_doubled(MAPX,MAPY,MAPSZ,MAPSZ,LH); /* map window, doubled */
        uint32_t t2=plat_ticks_200hz();
        rdr_shutdown();
        uint32_t r[5]={ (t1-t0),(tf1-tf0),(t2-t1),0,(uint32_t)N };  /* [0]=scroll compose, [1]=full compose, [2]=present */
        plat_file bf=plat_fcreate("BENCH.BIN");
        if (bf!=PLAT_BADFILE){ plat_fwrite(bf,r,(int32_t)sizeof r); plat_fclose(bf);}
        dbg_num("FULL  compose   ms=", (int)((tf1-tf0)*5/N));
        dbg_num("SCROLL compose  ms=", (int)((t1-t0)*5/N));
        dbg_num("  scroll ph0 static ms=", (int)(g_phase[0]*5/N));   /* incremental static layer */
        dbg_num("  scroll ph1 actors+fg ms=", (int)(g_phase[1]*5/N));
        dbg_num("  scroll ph2 light+vis ms=", (int)(g_phase[2]*5/N));
        dbg_num("  scroll ph3 composite ms=", (int)(g_phase[3]*5/N));
        dbg_num("present map doubled ms=", (int)((t2-t1)*5/N));
        Cconws("bench done\r\n"); return 0; }
#endif

    /* M10 test hook: SONG.REQ pins a specific song (base name, e.g. "BRIT") via the runtime-selected
     * backend and DISABLES per-area auto-switching — for `make music-tt/dma/st`. Absent in normal
     * play, where music_update_area() drives the music from the game context instead. */
    {   int32_t rl=0; void *req=plat_load_file("SONG.REQ",1,&rl);
        if(req && rl>0){ char nm[16]; int i=0; const char*s=(const char*)req;
            while(i<15 && s[i] && s[i]!='\r' && s[i]!='\n' && s[i]!=' ' && s[i]!='.') { nm[i]=s[i]; i++; } nm[i]=0;
            if(nm[0]){ music_play_song(nm); g_music_forced=1; }
            dbg_num("music backend (1=MIDI 2=DMA 3=PSG)=", music_active_backend()); } }

    int prev_btn=0;
    uint32_t last_anim=plat_ticks_200hz(), last_cyc=last_anim;  /* 200Hz-clock cadence (loop-rate independent) */
    for (;;) {                                       /* interactive; holds frame for capture */
        int mlx,mly,mbtn; read_mouse_logical(&mlx,&mly,&mbtn);
        int mouse_moved=(mlx!=g_mx||mly!=g_my);
        int click=(mbtn&1)&&!(prev_btn&1);           /* left-button press edge */
        prev_btn=mbtn; g_mx=mlx; g_my=mly;
        int anim_redraw=0;                           /* set when the animation tick fires */
        int moved=0;                                 /* set on a clean walk (map-only present) */

        int key = plat_input_raw();
        /* During gameplay (NOT text entry), drain any backlog to the NEWEST key so a
         * held arrow doesn't queue up behind a slow compose and lag the avatar. Text
         * entry (conversation / cast menu) keeps every keystroke. */
        if (key && !g_await_input && !g_casting) {
            int k2, guard=0; while ((k2=plat_input_raw()) && ++guard<64) key=k2;
        }
        int redraw=0;
        if (key) {
            int a = key & 0xFF;
            g_travel_active=0;                       /* any key press cancels click-to-travel */
            if (g_await_input) {                     /* typing a conversation reply */
                if      (a==0x0d) { submit_input(); redraw=1; }              /* Enter  */
                else if (a==0x08) { if(g_inlen){ g_inbuf[--g_inlen]=0; } redraw=1; } /* Bksp */
                else if (a==0x1b) { end_talk(); redraw=1; }                  /* Esc: leave */
                else if (a>=0x20 && a<0x7f && g_inlen<CVM_INPUT-1) {         /* a char */
                    g_inbuf[g_inlen++]=(char)a; g_inbuf[g_inlen]=0; redraw=1;
                }
            } else if (g_casting) {                  /* choosing a spell in the C)ast menu */
                if      (a==0x0d) { if(cast_menu_submit(&w)){ compose_map(&w); draw_border_tiles(); } redraw=1; }  /* Enter: select */
                else if (a==0x08) { if(g_inlen){ g_inbuf[--g_inlen]=0; } redraw=1; }                              /* Bksp */
                else if (a==0x1b) { close_cast_menu(); redraw=1; }                                               /* Esc: cancel */
                else if (a>=0x20 && a<0x7f && g_inlen<CVM_INPUT-1) {                                             /* a char/digit */
                    g_inbuf[g_inlen++]=(char)a; g_inbuf[g_inlen]=0; redraw=1;
                }
            } else {
                plat_input_t in = plat_input_map(key);
                int dx=0,dy=0;
                switch (in) {
                case IN_UP: dy=-1; break; case IN_DOWN: dy=1; break;
                case IN_LEFT: dx=-1; break; case IN_RIGHT: dx=1; break;
                default: break;
                }
                if (g_show_status) { g_show_status=0; redraw=1; }  /* any key returns from the status view (then still acts) */
                if (g_pending_verb) {                /* armed verb awaiting a direction */
                    if (dx||dy) { if(resolve_target(&w,g_pending_verb,dx,dy)){ compose_map(&w); draw_border_tiles(); }
                                  g_pending_verb=0; redraw=1; }
                    else if (a==0x1b) { g_pending_verb=0; g_msg[0]=0; redraw=1; }   /* cancel */
                }
                else if (a=='a'||a=='A') { g_pending_verb='A'; g_msg[0]=0; g_show_inv=0; redraw=1; }  /* A)ttack */
                else if (a=='c'||a=='C') { open_cast_menu(); redraw=1; }                              /* C)ast: open the spell menu */
                else if (a=='b'||a=='B') { g_combat_mode=!g_combat_mode; g_show_inv=0;                /* B)attle: combat-mode toggle (moved off 'c') */
                    msgcat(0, g_combat_mode?"Combat mode!":"Peace mode."); redraw=1; }
                else if (a=='l'||a=='L') { g_pending_verb='L'; g_msg[0]=0; g_show_inv=0; redraw=1; }
                else if (a=='g'||a=='G') { g_pending_verb='G'; g_msg[0]=0; g_show_inv=0; redraw=1; }
                else if (a=='u'||a=='U') { g_pending_verb='U'; g_msg[0]=0; g_show_inv=0; redraw=1; }
                else if (a=='i'||a=='I') { g_show_inv=!g_show_inv; g_msg[0]=0; redraw=1; }
                else if (a=='s'||a=='S') { g_show_inv=0; do_save(&w); redraw=1; }  /* write SAVED.000 */
                else if (a=='r'||a=='R') { g_show_inv=0; do_rest(&w);                /* R)est: restore HP+mana */
                    compose_map(&w); draw_border_tiles(); draw_status(&w,ink); redraw=1; }
                else if (in==IN_QUIT) break;
                else if (in==IN_TALK) {              /* talk to an adjacent NPC (a trainer advances thy level) */
                    if (!g_talking) { int id=find_adjacent_npc(&w);
                        if(id>=0){   /* a trainer advances thy level only when thou hast earned it; else talk normally */
                            if(g_trainer[id] && (party_can_train() || can_train_stat(1))) try_train(id);
                            else talk(id);
                            redraw=1; } }
                }
                else if (dx||dy) {                   /* walk */
                    if (g_talking||g_show_inv||g_msg[0]) {   /* closing a panel needs a full present */
                        end_talk(); g_show_inv=0; g_msg[0]=0; redraw=1; }
                    if (world_try_move(&w,dx,dy)){ g_hour=clock_hour(&w); g_minutes=clock_minutes(&w);
                        step_npcs(&w);                   /* NPCs walk their schedules one step */
                        compose_map(&w); draw_border_tiles(); draw_status(&w,ink); moved=1; }  /* map-only */
                }
            }
        }
        if (click) {                                 /* mouse: command bar / map */
            if (g_my>=CMDY && g_my<CMDY+16 && g_mx>=CMDX && g_mx<CMDX+NCMD*16) {
                int icon=(g_mx-CMDX)/16, v=cmd_verb(icon);   /* select a command */
                if (icon==9){ g_combat_mode=!g_combat_mode; g_pending_verb=0; g_show_inv=0;  /* Combat icon toggles combat */
                    msgcat(0, g_combat_mode?"Combat mode!":"Peace mode."); }
                else if (icon==8){ g_pending_verb=0; g_show_inv=0; do_rest(&w);              /* Rest icon: camp + restore */
                    compose_map(&w); draw_border_tiles(); draw_status(&w,ink); }
                else if (v=='C'){ open_cast_menu(); }        /* Cast icon opens the spell menu */
                else if (v){ g_pending_verb=v; g_msg[0]=0; g_show_inv=0; }
                else  { msgcat(0,"Not yet implemented."); g_pending_verb=0; g_show_inv=0; }
                redraw=1;
            } else if (g_show_status) {               /* a click while viewing a character -> back to the roster */
                g_show_status=0; redraw=1;
            } else if (!g_talking && !g_casting && !g_show_inv &&
                       g_mx>=PANX && g_my>=26 && g_my<26+g_nparty*16) {   /* click a roster member -> their status (authentic U6) */
                int row=(g_my-26)/16;
                if (row>=0 && row<g_nparty){ g_status_actor=g_party[row].actor;
                    g_show_status=1; g_pending_verb=0; g_msg[0]=0; redraw=1; }
            } else {
                int dx,dy;
                if (!g_casting && map_click_delta(g_mx,g_my,&dx,&dy)) {
                    if (g_talking||g_show_inv){ end_talk(); g_show_inv=0; }
                    if (g_pending_verb) {                       /* an armed verb acts on that ONE tile */
                        if (resolve_target(&w,g_pending_verb,dx,dy)){ compose_map(&w); draw_border_tiles(); }
                        g_pending_verb=0;
                    } else if (dx||dy) {                        /* plain click: WALK there (click-to-travel) */
                        g_travel_tx=(int)((w.px+dx)&(U6_SURFACE_TILES-1));
                        g_travel_ty=(int)((w.py+dy)&(U6_SURFACE_TILES-1));
                        g_travel_active=1;
                    }
                    redraw=1;
                }
            }
        }
        /* click-to-travel: while a destination is set and the player is idle, step ONE tile
         * toward it each loop (pathfinding around walls). Any key press cancels it (above). */
        if (g_travel_active && !key && !g_talking && !g_show_inv && !g_casting && !g_pending_verb) {
            if ((int)w.px==g_travel_tx && (int)w.py==g_travel_ty) g_travel_active=0;   /* arrived */
            else { int sx,sy;
                if (npc_pathstep(&w,(int)w.px,(int)w.py,g_travel_tx,g_travel_ty,-1,-1,&sx,&sy)
                    && (sx||sy) && world_try_move(&w,sx,sy)) {
                    g_hour=clock_hour(&w); g_minutes=clock_minutes(&w); step_npcs(&w);
                    compose_map(&w); draw_border_tiles(); draw_status(&w,ink); moved=1;
                    if ((int)w.px==g_travel_tx && (int)w.py==g_travel_ty) g_travel_active=0;
                } else g_travel_active=0;                       /* blocked / no path -> stop */
            }
        }
        /* Animation + colour cycling are paced by the 200Hz SYSTEM CLOCK, not a per-loop
         * counter, so their speed is independent of the frame/loop rate. (Bug fixed: with a
         * per-iteration counter, moving the mouse — which forces a full present — slowed the
         * loop and dragged cycling/animation down with it.) 33 ticks ~= 6 fps, 23 ~= 8.7 fps. */
        uint32_t now = plat_ticks_200hz();

        /* M10: the MIDI .M tick now runs on a steady Timer-A interrupt (music_isr_tick), not here,
         * so tempo is frame-rate independent and survives blocking waits. The DMA path still needs
         * a main-loop pump for its disk refill (can't do GEMDOS I/O from an ISR; the 2.6 s ring
         * gives ample slack). */
        plat_dma_music_pump();  /* M10: refill the DMA-sound ring from the streaming PCM file */
        music_update_area();    /* M10: start/switch the area's song (overworld <-> battle) on change */

        /* tile-animation tick: recompose + present only when an ANIMDATA tile (water/fountain)
         * is actually in view — most scenes have none, so this is free there. */
        if (g_view_has_anim && (uint32_t)(now - last_anim) >= 33){ last_anim = now; g_anim_ctr++;
            compose_map(&w); draw_border_tiles(); draw_status(&w,ink); anim_redraw=1; }

        /* colour cycling: rotate the CLUT — no recompose/present needed, the shifter shows the
         * new colours straight away (cheap; runs every scene, even while idle / mouse-moving). */
        if ((uint32_t)(now - last_cyc) >= 23){ last_cyc = now; cycle_palette(); }

        /* Present the smallest region that changed: panel/cursor changes need the full
         * doubled present; a clean walk or the animation tick only need the map window
         * (~half the c2p). (docs/design/performance.md) */
        if (redraw){ draw_conversation(ink); draw_status(&w,ink); draw_command_bar(15); }
        /* Exactly ONE Vsync per loop iteration paces the frame. present_frame() (full
         * doubled present) Vsyncs internally, so don't Vsync again after it — the old code
         * did both, halving the rate to 30Hz whenever the mouse moved. The cheaper paths
         * (map-only present, or idle) rely on the explicit Vsync here. */
        if (redraw) present_frame();                           /* full present (Vsyncs internally) */
        else { if (moved||anim_redraw) present_map();           /* map window (walk / tile anim) */
               else if (mouse_moved) present_cursor();         /* bare mouse move: cheap cursor-only */
               Vsync(); }
    }
    music_stop_all();   /* uninstall the Timer-A ISR + silence MIDI/DMA before returning to TOS */
    rdr_shutdown();
    return 0;
}
