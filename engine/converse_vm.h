// SPDX-License-Identifier: GPL-2.0-or-later
/* converse_vm.h — Ultima VI conversation/usecode VM (M6, portable engine).
 *
 * Executes an NPC's decompressed conversation script: prints text, evaluates
 * usecode expressions, branches on game state (IF/ELSE/ENDIF), and runs the
 * keyword input loop (ASK -> KEYWORDS/ANSWER -> JUMP back / BYE). Pure portable
 * C (no TOS): runs on the host (tests) and the 68000 target alike.
 *
 * Implemented FROM /docs/rules/usecode_vm.md + /docs/formats/converse.md
 * (clean-room: the opcode semantics were understood by reading Nuvie's
 * ConverseInterpret, then implemented from our own spec).
 *
 * Coroutine model for input: cvm_run() executes until the script asks for a
 * keyword (returns CVM_ASK) or the conversation ends (CVM_DONE). The caller
 * shows cvm_text(), reads a keyword, calls cvm_input(), then cvm_run() again.
 */
#ifndef U6E_CONVERSE_VM_H
#define U6E_CONVERSE_VM_H

#include <stdint.h>
#include <stdbool.h>

/* Game state the VM reads/mutates. Defaults (zeroed) make every conditional
 * take its "flag clear / not in party" branch; the host can preset state. */
#define CVM_DBW 48                 /* data-block elements a conversation can write (§6g) */
typedef struct {
    uint8_t  talk_flags[256];  /* per-NPC: 8 conversation flags (bit n)        */
    int32_t  var[64];          /* numbered conversation variables              */
    uint8_t  in_party[256];    /* 1 = NPC n is currently in the party          */
    uint32_t rng;              /* xorshift32 state (nonzero) for RAND          */
    /* --- mutation state (the conversation VM's view of the world) --- */
    int32_t  gold;             /* player gold                                  */
    int16_t  karma;            /* 0..99 (ADDKARMA/SUBKARMA)                     */
    int      avatar_num;       /* "the player" actor for GIVE/NEW/DELETE (def 0)*/
    int      portrait_npc;     /* last PORTRAIT op (host may re-render); -1=none*/
    int      mutations;        /* count of state-changing ops (for tests)      */
    uint8_t  avatar_sex;       /* var 0x10: 0=male 1=female                     */
    uint8_t  knows_gargish;    /* var 0x15: 1 = player understands Gargish      */
    uint8_t  on_quest;         /* var 0x1A: 1 = on a sacred quest               */
    int16_t  avatar_hp;        /* var 0x19: avatar health                       */
    uint8_t  wounded[256];     /* HP below max: WOUNDED reads it (usecode_vm.md §6f)            */
    uint8_t  lost_body;        /* a dead companion's body is gone: counts as carried (usecode_vm.md §6f) */
    uint8_t  resurrect_req;    /* RESURRECTs to apply: raise what a carried body names (§6f)    */
    uint8_t  poisoned[256];    /* per-NPC poison status (CURE clears)           */
    uint8_t  dead[256];        /* 1 = dead until RESURRECT                      */
    uint8_t  healed_full[256]; /* 1 = HEAL ran on this NPC this conversation (host restores HP) */
    uint8_t  worktype[256];    /* per-NPC schedule worktype (WORKTYPE)          */
    uint8_t  nearby[256];      /* 1 = NPC n stands within 5 tiles of the speaker (NPCNEARBY) */
    int16_t  player_obj[1024]; /* player's count per object number (gold: see `gold`) */
    uint8_t  player_objq[1024]; /* quality of the last one GIVE/NEW put in the pack (a key's lock) */
    uint8_t  hour;             /* game-clock hour 0..23, for the $T greeting    */
    /* party stats the scripts read and change (usecode_vm.md §6c): EXP/LVL/STR/INT/DEX ops */
    uint8_t  str[256], dex[256], intel[256], level[256];
    uint16_t exp[256];
    uint8_t  party_actor[16];  /* the party in order (index 0 = the Avatar), for NPC(index) */
    char     party_name[16][16]; /* their names, for SETNAME ($Y) */
    int      nparty;
    const uint8_t *obj_weight; /* [1024] raw weights by obj_n (TILEFLAG; 0.1 stone), or NULL */
    int32_t  party_room;       /* CANCARRY for a party member: the whole party's room, 0.1 stone */
} cvm_game_t;

/* well-known object numbers used in conversations (U6) */
#define CVM_OBJ_GOLD 88        /* gold coin: counted in `gold`, not player_obj  */

typedef enum { CVM_RUN, CVM_ASK, CVM_DONE, CVM_ERROR } cvm_status;

#define CVM_OUT      1024      /* display text accumulated per run             */
#define CVM_MAXVAL   64        /* values in one collected instruction          */
#define CVM_MAXFRAME 24        /* IF / KEYWORDS nesting                        */
#define CVM_ITEXT    256       /* keyword list / name / description scratch    */
#define CVM_INPUT    32
#define CVM_MAXSTEPS 200000   /* per-run instruction cap (anti-hang guard)    */
#define CVM_NSVAR    40       /* string-var slots (covers $0-$9, $Y=0x22, $Z=0x23) */
#define CVM_SVAR_YSTR  0x22   /* $Y */
#define CVM_SVAR_INPUT 0x23   /* $Z = the player's previous input            */
/* numeric "system" var indices the script reads via VAR/#n (read-only) */
#define CVM_VAR_SEX       0x10
#define CVM_VAR_KARMA     0x14
#define CVM_VAR_GARGF     0x15
#define CVM_VAR_PARTYLIVE 0x17
#define CVM_VAR_PARTYALL  0x18
#define CVM_VAR_HP        0x19
#define CVM_VAR_QUESTF    0x1A
#define CVM_VAR_WORKTYPE  0x20

typedef struct {
    const uint8_t *s; int32_t len; int32_t pc;
    cvm_game_t *g;
    int         npc;
    const char *player;        /* $P substitution */
    const char *gtitle;        /* $G substitution */
    char        npcname[CVM_ITEXT]; /* $N (set by SIDENT) */

    struct { uint32_t v; uint8_t d; } in[CVM_MAXVAL]; int nin;
    char        itext[CVM_ITEXT];   /* per-instruction text (keywords/name/desc) */
    struct { uint8_t start_c, break_c, run; } fr[CVM_MAXFRAME]; int nfr;

    char        out[CVM_OUT]; int outlen;   /* text since last pause */
    char        input[CVM_INPUT];           /* player keyword */
    int         answer_mode;                /* 0=NO 1=YES 2=DONE */
    uint32_t    decl_v; uint8_t decl_t;     /* declared input/string var (DECL) */
    uint8_t     db_lvar; uint32_t db_loc, db_off;   /* DECL into a data-block element (usecode_vm.md §6g) */
    int32_t     dbw_pos[CVM_DBW]; uint16_t dbw_val[CVM_DBW]; int ndbw;   /* data-block writes, newest last */
    char        svar[CVM_NSVAR][40];        /* string vars (DATA string, input $Z, ...) */
    int         input_mode;                 /* 0=none 1=keyword(ASK) 2=var(INPUT*) */
    char        askc[16];                   /* at an ASKC pause: the characters it accepts ("yn"), else "" */
    int         input_num;                  /* at an INPUTNUM pause: digits only */
    int         done;
} cvm_t;

void        cvm_game_init(cvm_game_t *g, uint32_t seed);

/* script = ONE decompressed conversation (from CONVERSE.A/.B via u6lzw). */
void        cvm_init(cvm_t *vm, const uint8_t *script, int32_t len,
                     cvm_game_t *g, int npc, const char *player, const char *gtitle);

cvm_status  cvm_run(cvm_t *vm);                 /* run until ASK / DONE */
void        cvm_input(cvm_t *vm, const char *keyword); /* answer an ASK */
const char *cvm_text(const cvm_t *vm);          /* text produced by the last run */

#endif
