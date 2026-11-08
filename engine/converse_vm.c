// SPDX-License-Identifier: GPL-2.0-or-later
/* converse_vm.c — Ultima VI conversation/usecode VM. See converse_vm.h.
 *
 * Two-phase interpreter (mirrors the original design, understood via Nuvie,
 * implemented from /docs):
 *   - collect_input(): read one instruction = an opcode + its value args, with
 *     0xA7 (EVAL) markers reducing sub-expressions on a value stack (eval/evop).
 *   - do_frame()/do_ctrl(): manage IF/ELSE/ENDIF + KEYWORDS/ANSWER block frames
 *     (a run flag + break toggle), then execute the opcode if the frame runs.
 * Text bytes print (when running) with $P/$G/$N/#var substitution. ASK pauses
 * for a keyword; JUMP loops back to ASK; BYE ends.
 */
#include "converse_vm.h"

/* ---- opcode constants (subset we act on) -------------------------------- */
enum {
    OP_CANCARRY=0x9a, OP_HASOBJ=0x9f,
    OP_RAND=0xa0, OP_IF=0xa1, OP_ENDIF=0xa2, OP_ELSE=0xa3, OP_SETF=0xa4,
    OP_CLEARF=0xa5, OP_DECL=0xa6, OP_EVAL=0xa7, OP_ASSIGN=0xa8, OP_FLAG=0xab,
    OP_JUMP=0xb0, OP_VAR=0xb2, OP_SVAR=0xb3, OP_DATA=0xb4, OP_DPRINT=0xb5,
    OP_BYE=0xb6, OP_ENDDATA=0xb8, OP_NEW=0xb9, OP_DELETE=0xba, OP_INVENTORY=0xbe,
    OP_PORTRAIT=0xbf, OP_ADDKARMA=0xc4, OP_SUBKARMA=0xc5, OP_INPARTY=0xc6,
    OP_GIVE=0xc9, OP_JOIN=0xca, OP_WAIT=0xcb, OP_LEAVE=0xcc, OP_WORKTYPE=0xcd,
    OP_EVENT=0xd1, OP_RESURRECT=0xd6, OP_SETNAME=0xd8, OP_HEAL=0xd9, OP_WOUNDED=0xda,
    OP_CURE=0xdb, OP_POISONED=0xdc, OP_NPC=0xdd, OP_SETNAME1=0xdf,
    OP_ENDANSWER=0xee, OP_KEYWORDS=0xef,
    OP_SLOOK=0xf1, OP_SCONVERSE=0xf2, OP_SPREFIX=0xf3, OP_ANSWER=0xf6,
    OP_ASK=0xf7, OP_ASKC=0xf8, OP_INPUTSTR=0xf9, OP_INPUT=0xfb, OP_INPUTNUM=0xfc,
    OP_SIDENT=0xff, OP_NPCSELF=0xeb
};

/* ---- byte classification (matches the original) -------------------------- */
static bool is_print(uint32_t b){ return b==0x0a||(b>=0x20&&b<=0x7a)||b==0x7e||b==0x7b; }
static bool is_datasize(uint32_t b){ return b==0xd2||b==0xd3||b==0xd4; }
static bool is_valop(uint32_t b){
    switch(b){
      case 0x81:case 0x82:case 0x83:case 0x84:case 0x85:case 0x86:
      case 0x90:case 0x91:case 0x92:case 0x93:case 0x94:case 0x95:
      case 0x9a:case 0x9b:case 0x9d:case 0x9f:case 0xa0:case 0xa7:
      case 0xab:case 0xb2:case 0xb3:case 0xb4:case 0xb7:case 0xbb:case 0xc6:
      case 0xc7:case 0xca:case 0xcc:case 0xd7:case 0xda:case 0xdc:
      case 0xdd:case 0xe0:case 0xe1:case 0xe2:case 0xe3:case 0xe4: return true;
      default: return false;
    }
}
static bool is_ctrl(uint32_t b){ return (b>=0xa1||b==0x9c||b==0x9e)&&!is_valop(b)&&!is_datasize(b); }

/* ---- little helpers ------------------------------------------------------ */
static uint32_t rd2(const uint8_t*p){ return (uint32_t)(p[0]|(p[1]<<8)); }
static uint32_t rd4(const uint8_t*p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static char lc(char c){ return (c>='A'&&c<='Z')?(char)(c+32):c; }
static int  slen(const char*s){ int n=0; while(s[n])n++; return n; }
static void outc(cvm_t*vm,char c){ if(vm->outlen<CVM_OUT-1) vm->out[vm->outlen++]=c; }
static void outs(cvm_t*vm,const char*s){ if(s) while(*s) outc(vm,*s++); }
#define SVI(n) ((unsigned)(n) < CVM_NSVAR ? (unsigned)(n) : 0)
static void set_svar(cvm_t*vm,unsigned idx,const char*s){ idx=SVI(idx); int k=0;
    while(s[k]&&k<39){ vm->svar[idx][k]=s[k]; k++; } vm->svar[idx][k]=0; }

void cvm_game_init(cvm_game_t*g,uint32_t seed){
    for(int i=0;i<256;i++){ g->talk_flags[i]=0; g->in_party[i]=0;
        g->poisoned[i]=0; g->dead[i]=0; g->wounded[i]=0; g->healed_full[i]=0; g->worktype[i]=0; g->nearby[i]=0;
        g->str[i]=g->dex[i]=g->intel[i]=0; g->level[i]=1; g->exp[i]=0; }
    for(int i=0;i<1024;i++){ g->player_obj[i]=0; g->player_objq[i]=0; }
    for(int i=0;i<16;i++){ g->party_actor[i]=0; g->party_name[i][0]=0; } g->nparty=0;
    g->obj_weight=0; g->per_ten=0; g->inv_count=0; g->inv_new=0; g->inv_del=0; g->inv_give=0; g->inv_room=0; g->inv_seen=0; g->inv_resurrect=0; g->dialect=0; g->event=0; for(int i=0;i<256;i++) g->talk_flags_hi[i]=0;
    g->party_room=1000; g->lost_body=0; g->resurrect_req=0;
    for(int i=0;i<64;i++) g->var[i]=0;
    g->rng = seed ? seed : 0x1234567u;
    g->gold=0; g->karma=0; g->avatar_num=0; g->portrait_npc=-1; g->mutations=0;
    g->avatar_sex=0; g->knows_gargish=0; g->on_quest=0; g->avatar_hp=30;
}
static int is_player(cvm_t*vm,int n){ return n==vm->g->avatar_num || n==0 || n==1 || (n>0 && n<256 && vm->g->in_party[n]); }   /* the pooled pack (§ game state) */
/* reads of one member's things: the pooled pack is the leader's, a companion holds nothing (§6f) */
static int actor_of(cvm_t*vm,int n){ return n==0?(vm->g->nparty>0?vm->g->party_actor[0]:1):n; }
static int holds(cvm_t*vm,int n){ int lead=vm->g->nparty>0?vm->g->party_actor[0]:1; return n==0 || n==lead || n==vm->g->avatar_num || (n==1 && lead==1); }

/* resolve a numeric "system" var (read-only, from game state). Returns 1 and
 * sets *out if `idx` is a system var; 0 if it's an ordinary local var. */
static int sysvar(cvm_t*vm,uint32_t idx,int32_t*out){
    cvm_game_t*g=vm->g; int n;
    switch(idx){
      case CVM_VAR_SEX:    *out=g->avatar_sex; return 1;
      case CVM_VAR_KARMA:  *out=g->karma; return 1;
      case CVM_VAR_GARGF:  *out=g->knows_gargish; return 1;
      case CVM_VAR_PARTYLIVE: case CVM_VAR_PARTYALL:                /* the reference: party size MINUS the leader (both vars; usecode_vm.md §6c) */
          n=0; for(int i=0;i<256;i++) if(g->in_party[i]) n++; *out=n>0?n-1:0; return 1;
      case CVM_VAR_HP:     *out=g->avatar_hp; return 1;
      case CVM_VAR_QUESTF: *out=g->on_quest; return 1;
      case CVM_VAR_WORKTYPE: *out=g->worktype[vm->npc & 0xff]; return 1;
      default: return 0;
    }
}
static uint32_t rng_next(cvm_game_t*g){ uint32_t x=g->rng; x^=x<<13; x^=x>>17; x^=x<<5; return g->rng=x; }

/* ---- frames (IF / KEYWORDS) + run flag ----------------------------------- */
static int  running(cvm_t*vm){ return vm->nfr? vm->fr[vm->nfr-1].run : 1; }
static void set_run(cvm_t*vm,int r){ if(vm->nfr) vm->fr[vm->nfr-1].run=(uint8_t)(r?1:0); }
static uint8_t get_break(cvm_t*vm){ return vm->nfr? vm->fr[vm->nfr-1].break_c : 0; }
static void set_break(cvm_t*vm,uint8_t c){ if(vm->nfr) vm->fr[vm->nfr-1].break_c=c; }
static void enter(cvm_t*vm,uint8_t c){
    if(vm->nfr>=CVM_MAXFRAME) return;
    uint8_t run = vm->nfr? vm->fr[vm->nfr-1].run : 1;
    vm->fr[vm->nfr].start_c=c; vm->fr[vm->nfr].break_c=0; vm->fr[vm->nfr].run=run; vm->nfr++;
}
static void leave(cvm_t*vm){ if(vm->nfr) vm->nfr--; }

static void do_frame(cvm_t*vm,uint8_t c){
    switch(c){
      case OP_IF:        enter(vm,OP_IF); break;
      case OP_ENDIF:     leave(vm); break;
      case OP_KEYWORDS:  if(!vm->nfr||vm->fr[vm->nfr-1].start_c!=OP_KEYWORDS) enter(vm,OP_KEYWORDS); break;
      case OP_ENDANSWER: leave(vm); break;
      default: break;
    }
    if(c && c==get_break(vm)) set_run(vm,!running(vm));
}

/* ---- value collection + expression evaluation ---------------------------- */
static void add_val(cvm_t*vm,uint32_t v,uint8_t d){ if(vm->nin<CVM_MAXVAL){ vm->in[vm->nin].v=v; vm->in[vm->nin].d=d; vm->nin++; } }

/* read one value at pc (handles 0xd2/d3/d4 literals); returns dtype in *d */
static uint32_t read_value(cvm_t*vm,uint8_t*d){
    uint8_t b = vm->s[vm->pc++]; uint32_t v=b; *d=0;
    if(b==0xd3){ if(vm->pc<vm->len){ v=vm->s[vm->pc++]; } *d=0xd3; }
    else if(b==0xd2){ if(vm->pc+4<=vm->len){ v=rd4(vm->s+vm->pc); vm->pc+=4; } *d=0xd2; }
    else if(b==0xd4){ if(vm->pc+2<=vm->len){ v=rd2(vm->s+vm->pc); vm->pc+=2; } *d=0xd4; }
    return v;
}

static int npc_num(cvm_t*vm,uint32_t n){ return (n!=OP_NPCSELF)?(int)n:vm->npc; }

/* the pooled pack: how many of obj o the player has / add n (n<0 removes, floor 0); gold is `gold` */
static int32_t cnt(cvm_t*vm,uint32_t o){ o&=0x3ff; return o==CVM_OBJ_GOLD?vm->g->gold:vm->g->player_obj[o]; }
static void add(cvm_t*vm,uint32_t o,uint32_t ql,int32_t n){
    o&=0x3ff; cvm_game_t*g=vm->g;
    if(o==CVM_OBJ_GOLD){ int32_t v=g->gold+n; g->gold=v<0?0:v; return; }
    int32_t v=g->player_obj[o]+n; g->player_obj[o]=(int16_t)(v<0?0:v>32767?32767:v);
    if(n>0) g->player_objq[o]=(uint8_t)ql; }
/* data-block accessors (defined below; used by evop's DATA/INDEXOF ops) */
static int      db_string(cvm_t*vm, uint32_t loc, uint32_t idx, char*out, int cap);
static uint32_t db_integer(cvm_t*vm, uint32_t loc, uint32_t idx);
static uint32_t db_find(cvm_t*vm, uint32_t loc, const char*s);

/* apply one operator/query to the value stack (returns new sp) */
static int evop(cvm_t*vm,uint32_t*st,int sp){
    if(sp<1) return sp;
    uint32_t op=st[--sp];
    uint32_t a=0,b=0;
    #define POP() (sp>0?st[--sp]:0)
    #define PUSH(x) do{ if(sp<CVM_MAXVAL) st[sp++]=(uint32_t)(x);}while(0)
    switch(op){
      case 0x81: b=POP();a=POP(); PUSH(a>b); break;                 /* GT  */
      case 0x82: b=POP();a=POP(); PUSH(a>=b); break;                /* GE  */
      case 0x83: b=POP();a=POP(); PUSH(a<b); break;                 /* LT  */
      case 0x84: b=POP();a=POP(); PUSH(a<=b); break;                /* LE  */
      case 0x85: b=POP();a=POP(); PUSH(a!=b); break;                /* NE  */
      case 0x86: b=POP();a=POP(); PUSH(a==b); break;                /* EQ  */
      case 0x90: b=POP();a=POP(); PUSH(a+b); break;                 /* ADD */
      case 0x91: b=POP();a=POP(); PUSH(a-b); break;                 /* SUB */
      case 0x92: b=POP();a=POP(); PUSH(a*b); break;                 /* MUL */
      case 0x93: b=POP();a=POP(); PUSH(b?a/b:0); break;             /* DIV */
      case 0x94: b=POP();a=POP(); PUSH((a||b)?1:0); break;          /* LOR */
      case 0x95: b=POP();a=POP(); PUSH((a&&b)?1:0); break;          /* LAND*/
      case OP_RAND:{ b=POP();a=POP(); PUSH(a>=b?a:a+(rng_next(vm->g)%(b-a+1))); } break;
      case OP_FLAG:{ b=POP();a=POP(); int nn=npc_num(vm,a)&0xff;    /* (npc,flag); SE has 8-15 too (converse.md §6c) */
                     if(b<=7) PUSH((vm->g->talk_flags[nn]>>b)&1);
                     else if(b<=15 && vm->g->dialect==CVM_DIALECT_SE) PUSH((vm->g->talk_flags_hi[nn]>>(b-8))&1);
                     else PUSH(0); } break;
      case OP_VAR:{ a=POP(); int32_t sv; if(sysvar(vm,a,&sv)) PUSH((uint32_t)sv);
                    else PUSH(a<64?(uint32_t)vm->g->var[a]:0); } break;
      case OP_INPARTY:{ a=POP(); PUSH(vm->g->in_party[npc_num(vm,a)&0xff]?1:0); } break;
      case OP_JOIN:{ a=POP(); vm->g->in_party[npc_num(vm,a)&0xff]=1; vm->g->mutations++; PUSH(0); } break;
      case OP_LEAVE:{ a=POP(); vm->g->in_party[npc_num(vm,a)&0xff]=0; vm->g->mutations++; PUSH(0); } break;
      case OP_NPC:{ uint32_t ig=POP(), pi=POP(); (void)ig;            /* NPC(party index, ?) -> that member's actor number (§6c) */
                    PUSH(pi<(uint32_t)vm->g->nparty ? (uint32_t)vm->g->party_actor[pi] : 0u); } break;
      case OP_HASOBJ:{ uint32_t q=POP(),o=POP(); int np=npc_num(vm,POP());  /* (npc,obj,qual): quality 0 = any */
                       if(vm->g->inv_count){ PUSH(vm->g->inv_count(actor_of(vm,np),(int)(o&0x3ff),q?(int)q:-1)>0?1:0); break; }
                       PUSH((holds(vm,np)&&cnt(vm,o)>0)?1:0); } break;
      case OP_DATA:{ uint32_t idx=POP(),loc=POP();                  /* DATA(loc,idx) */
                     if(vm->decl_t==0xb3){ unsigned sv=SVI(vm->decl_v); /* string -> $n var */
                         db_string(vm,loc,idx,vm->svar[sv],40); PUSH(vm->decl_v); }
                     else PUSH(db_integer(vm,loc,idx)); } break;
      case 0xb7:{ uint32_t sv=POP(),loc=POP();                      /* INDEXOF(loc,svar) */
                  PUSH(db_find(vm,loc,vm->svar[SVI(sv)])); } break;
      case 0xbb:{ uint32_t o=POP(); int np=npc_num(vm,POP());       /* OBJCOUNT(npc,obj) */
                  if(vm->g->inv_count){ PUSH((uint32_t)vm->g->inv_count(actor_of(vm,np),(int)(o&0x3ff),-1)); break; }
                  PUSH(holds(vm,np)?(uint32_t)cnt(vm,o):0); } break;
      case 0xc7:{ uint32_t ql=POP(); uint32_t o=POP()&0x3ff;       /* OBJINPARTY(obj,qual): the carrier or 0x8001 (§6f) */
                  if(vm->g->inv_count){ uint32_t who=0x8001u; for(int p=0;p<vm->g->nparty;p++){ int a_=vm->g->party_actor[p]; if(vm->g->inv_seen(a_)&&vm->g->inv_count(a_,(int)o,ql?(int)ql:-1)>0){ who=(uint32_t)a_; break; } } PUSH(who); break; }
                  (void)ql;
                  int has=cnt(vm,o)>0 || (o==339 && (cnt(vm,354)>0 || vm->g->lost_body));
                  PUSH(has?(uint32_t)(vm->g->nparty>0?vm->g->party_actor[0]:1):0x8001u); } break;
      case OP_CANCARRY:{ a=POP()&0xff;                                /* CANCARRY(actor) -> room, 0.1 stone (§6c) */
                         if(vm->g->inv_room){ int32_t r_=vm->g->inv_room(actor_of(vm,(int)a)); PUSH((uint32_t)(r_<0?0:r_)); break; }
                         int32_t r=(is_player(vm,(int)a)||vm->g->in_party[a])?vm->g->party_room:(int32_t)vm->g->str[a]*20;
                         PUSH((uint32_t)(r<0?0:r)); } break;
      case OP_SVAR: a=POP(); PUSH(a); break;                        /* SVAR -> svar handle */
      case 0x9b:{ uint32_t q=POP(), o=POP()&0x3ff;                   /* WEIGHT(obj,qty): raw x qty, /10 if weighed per ten */
                  uint32_t wt=(vm->g->obj_weight?vm->g->obj_weight[o]:0)*q;
                  if(vm->g->per_ten ? vm->g->per_ten((int)o) : (o==88||(o>=65&&o<=72))) wt/=10;
                  PUSH(wt); } break;
      case 0x9d: a=POP(); PUSH(0); break;                           /* HORSED      */
      case 0xd7: a=POP(); PUSH(vm->g->nearby[npc_num(vm,a)&0xff]); break;   /* NPCNEARBY: within 5 tiles of the speaker (usecode_vm.md §6) */
      case OP_WOUNDED:{ int np=npc_num(vm,POP())&0xff; PUSH((vm->g->wounded[np]||vm->g->dead[np])?1:0); } break;   /* HP below max (§6f) */
      case OP_POISONED:{ int np=npc_num(vm,POP())&0xff; PUSH(vm->g->poisoned[np]?1:0); } break;
      case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4:{        /* EXP/LVL/STR/INT/DEX(npc, delta): value+delta, stored (§6c) */
          int32_t dl=(int32_t)POP(); int np=npc_num(vm,POP())&0xff; int32_t v;
          cvm_game_t*g=vm->g;                                        /* DOS: add in the field's width, compare unsigned (usecode_vm.md §6c) */
          #define CAP30(x) ((uint8_t)(x)>30?30:(uint8_t)(x))
          switch(op){
            case 0xe0: { uint16_t e=(uint16_t)(g->exp[np]+dl); if(e>9999) e=9999; g->exp[np]=e; v=e; } break;
            case 0xe1: g->level[np]=(uint8_t)(g->level[np]+dl); v=g->level[np]; break;
            case 0xe2: g->str[np]=CAP30(g->str[np]+dl); v=g->str[np]; break;
            case 0xe3: g->intel[np]=CAP30(g->intel[np]+dl); v=g->intel[np]; break;
            default:   g->dex[np]=CAP30(g->dex[np]+dl); v=g->dex[np]; break; }
          #undef CAP30
          if(dl) g->mutations++;
          PUSH((uint32_t)v); } break;
      default: PUSH(0); break;                                      /* unknown op  */
    }
    #undef POP
    #undef PUSH
    return sp;
}

/* reduce in[vi..] via the operator stack, replacing them with the result(s) */
static void eval(cvm_t*vm,int vi){
    uint32_t st[CVM_MAXVAL]; int sp=0;
    for(int v=vi; v<vm->nin; v++){
        uint32_t val=vm->in[v].v; uint8_t ds=vm->in[v].d;
        if(sp<CVM_MAXVAL) st[sp++]=val;
        if(is_valop(val)&&!ds) sp=evop(vm,st,sp);
    }
    vm->nin=vi;
    if(sp==0) add_val(vm,0,0);
    else for(int k=0;k<sp;k++) add_val(vm,st[k],0);
}

/* read text run at pc into itext (raw bytes; printable run) */
static void read_itext(cvm_t*vm){
    int o=0;
    while(vm->pc<vm->len && is_print(vm->s[vm->pc])){
        if(o<CVM_ITEXT-1) vm->itext[o++]=(char)vm->s[vm->pc];
        vm->pc++;
    }
    vm->itext[o]='\0';
}

/* collect one control instruction starting at a ctrl byte */
static void collect_input(cvm_t*vm){
    vm->nin=0; vm->itext[0]='\0';
    uint8_t op=vm->s[vm->pc];
    if(op==OP_JUMP){ add_val(vm,vm->s[vm->pc++],0);
        if(vm->pc+4<=vm->len){ add_val(vm,rd4(vm->s+vm->pc),0); vm->pc+=4; } else add_val(vm,0,0); return; }
    if(op==OP_SIDENT){ add_val(vm,vm->s[vm->pc++],0);
        if(vm->pc<vm->len) add_val(vm,vm->s[vm->pc++],0);
        read_itext(vm); return; }
    if(op==OP_KEYWORDS||op==OP_ASKC||op==OP_SLOOK){ add_val(vm,vm->s[vm->pc++],0); read_itext(vm); return; }
    /* standard: opcode + value expression (EVAL reduces) */
    int arg_num=1;
    do {
        uint8_t p=vm->s[vm->pc];
        if(is_print(p) && (vm->pc+1>=vm->len || !is_valop(vm->s[vm->pc+1]))) break;
        if(p==OP_EVAL){ vm->pc++; if(running(vm)) eval(vm,arg_num++); }
        else { uint8_t d; uint32_t v=read_value(vm,&d); add_val(vm,v,d); }
    } while(vm->pc<vm->len && !is_ctrl(vm->s[vm->pc]));
}

/* comma-separated keyword list match (input prefix == a keyword; '*' = any) */
static bool check_keywords(const char*keys,const char*input){
    if(keys[0]=='*'&&keys[1]=='\0') return true;
    const char*k=keys;
    while(*k){
        const char*t=k; int L=0; while(t[L]&&t[L]!=',') L++;
        bool m=true;
        for(int i=0;i<L;i++){ if(lc(input[i])!=lc(k[i])){ m=false; break; } }
        if(m && L>0) return true;
        k+=L; if(*k==',') k++;
    }
    return false;
}

/* format + print a text run [s,s+n) honouring $P/$G/$N/#var; 0x0a -> space */
static void print_text(cvm_t*vm,const uint8_t*s,int n){
    int cap=-1;                                   /* '`': capitalise what comes next (usecode_vm.md §6d) */
    int32_t last=0;                               /* the last #n printed, for the '\\' / '/' markers (§6e) */
    for(int i=0;i<n;){
        char c=(char)s[i];
        if(cap>=0 && cap<vm->outlen){ char*f=&vm->out[cap]; if(*f>='a'&&*f<='z') *f=(char)(*f-32); cap=-1; }
        if(c=='`'){ cap=vm->outlen; i++; continue; }
        if(c=='$'&&i+1<n){ char k=(char)s[i+1];
            if(k=='P') outs(vm,vm->player);
            else if(k=='G') outs(vm,vm->gtitle);
            else if(k=='N') outs(vm,vm->npcname);
            else if(k>='0'&&k<='9') outs(vm,vm->svar[k-'0']); /* string var $0..$9 */
            else if(k=='Z') outs(vm,vm->svar[CVM_SVAR_INPUT]); /* previous input */
            else if(k=='Y') outs(vm,vm->svar[CVM_SVAR_YSTR]);  /* Y-string */
            else if(k=='T'){                                   /* time-of-day greeting word */
                int h=vm->g->hour; const char*t=(h<12)?"morning":(h<18)?"afternoon":"evening";
                outs(vm,t); }                                  /* words per U6 GAME.EXE $T table */
            /* other tokens not modelled: drop (no literal) */
            i+=2; continue; }
        if(c=='#'&&i+1<n&&s[i+1]>='0'&&s[i+1]<='9'){ int vi=0; i++;
            while(i<n&&s[i]>='0'&&s[i]<='9'){ vi=vi*10+(s[i]-'0'); i++; }
            int32_t vv=(vi<64)?vm->g->var[vi]:0; char num[12]; int p=0,neg=0; last=vv;
            uint32_t u=(uint32_t)vv; if(vv<0){neg=1;u=(uint32_t)(-vv);}
            char tmp[12]; int t=0; do{ tmp[t++]=(char)('0'+u%10); u/=10; }while(u); if(neg)tmp[t++]='-';
            while(t) num[p++]=tmp[--t];
            num[p]=0; outs(vm,num); continue; }
        if(c=='\\'||c=='/'){ int show=(c=='/')?(last==1):(last!=1); i++;   /* plural / singular letters (§6e) */
            while(i<n&&s[i]>='a'&&s[i]<='z'){ if(show) outc(vm,(char)s[i]); i++; } continue; }
        if(c=='@'){ if(vm->g->kw_mark) outc(vm,'\x01'); i++; continue; }   /* a keyword follows: the game may colour it */
        if(c==0x0a){ outc(vm,' '); i++; continue; }
        outc(vm,c); i++;
    }
    if(cap>=0 && cap<vm->outlen){ char*f=&vm->out[cap]; if(*f>='a'&&*f<='z') *f=(char)(*f-32); }
}

/* The conversation can carry a DATA BLOCK: a region of the script at byte
 * offset `loc` holding either non-print-terminated strings (indexed 0,1,2,...)
 * or 2-byte LE integers (at loc + i*2). DATA/DPRINT/INDEXOF reference it. */

/* copy the i-th data-block string at `loc` into out[cap]; len, or -1 if absent */
static int db_string(cvm_t*vm, uint32_t loc, uint32_t idx, char*out, int cap){
    if(loc>=(uint32_t)vm->len) return -1;
    const uint8_t*db=vm->s; int32_t p=(int32_t)loc;
    for(uint32_t e=0;e<idx;e++){
        if(p>=vm->len || db[p]==OP_ENDDATA) return -1;
        while(p<vm->len && is_print(db[p])) p++;   /* skip string ... */
        p++;                                        /* ... and its terminator */
    }
    if(p>=vm->len || db[p]==OP_ENDDATA){ out[0]=0; return -1; }
    int d=0;
    while(p<vm->len && is_print(db[p]) && d<cap-1) out[d++]=(char)db[p++];
    out[d]=0; return d;
}

/* the i-th 2-byte LE integer in the data block at `loc` */
static uint32_t db_integer(cvm_t*vm, uint32_t loc, uint32_t idx){
    int32_t p=(int32_t)loc + (int32_t)idx*2;
    if(p<0 || p+2>vm->len) return 0;
    for(int k=vm->ndbw-1;k>=0;k--) if(vm->dbw_pos[k]==p) return vm->dbw_val[k];   /* written this conversation (§6g) */
    return rd2(vm->s+p);
}
/* ASSIGN into a data-block element (§6g): kept beside the read-only script, one entry per element */
static void db_set_integer(cvm_t*vm, uint32_t loc, uint32_t idx, uint32_t v){
    int32_t p=(int32_t)loc + (int32_t)idx*2;
    if(p<0 || p+2>vm->len) return;
    for(int k=0;k<vm->ndbw;k++) if(vm->dbw_pos[k]==p){ vm->dbw_val[k]=(uint16_t)v; return; }
    if(vm->ndbw<CVM_DBW){ vm->dbw_pos[vm->ndbw]=p; vm->dbw_val[vm->ndbw]=(uint16_t)v; vm->ndbw++; }
}

/* find the data-block item at `loc` whose name (clamped to 4 chars, keyword
 * style) matches `s`; returns its index, or the item count if not found. Lets
 * the player pick a shop item by NAME rather than number. */
static uint32_t db_find(cvm_t*vm, uint32_t loc, const char*s){
    if(loc>=(uint32_t)vm->len) return 0;
    const uint8_t*db=vm->s; int32_t p=(int32_t)loc; uint32_t i=0;
    while(p<vm->len && db[p]!=OP_ENDDATA){
        if(is_print(db[p])){
            char item[8]; int d=0;
            while(p<vm->len && is_print(db[p])){ if(d<7) item[d++]=(char)db[p]; p++; }
            item[d]=0; p++;                 /* skip terminator */
            if(d>4) item[4]=0;              /* clamp to 4 chars (keyword match) */
            if(check_keywords(item,s)) return i;
        } else p++;
        i++;
    }
    return i;
}

/* execute the collected control instruction (in[0]=opcode, in[1..]=args) */
static void do_ctrl(cvm_t*vm){
    uint32_t op=vm->in[0].v; int ai=1;
    #define ARG() (ai<vm->nin?vm->in[ai++].v:0)
    switch(op){
      case OP_IF: set_break(vm,OP_ELSE); set_run(vm,ARG()?1:0); break;
      case OP_ENDIF: case OP_ELSE: break;
      case OP_SETF: case OP_CLEARF:{ int nn=npc_num(vm,ARG())&0xff; uint32_t f=ARG(); uint8_t*fb=0;   /* SE: flags 8-15 in a second byte (§6c) */
          if(f<=7) fb=&vm->g->talk_flags[nn]; else if(f<=15 && vm->g->dialect==CVM_DIALECT_SE){ fb=&vm->g->talk_flags_hi[nn]; f-=8; }
          if(fb){ if(op==OP_SETF) *fb|=(uint8_t)(1u<<f); else *fb&=(uint8_t)~(1u<<f); vm->g->mutations++; } } break;
      case OP_EVENT:{ uint32_t n=ARG(); if(vm->g->dialect==CVM_DIALECT_SE && vm->g->event){ vm->g->event((int)n); vm->g->mutations++; } } break;   /* SE: a scripted event (§6c) */
      case OP_DECL: vm->db_lvar=0;                                  /* loc,n,VAR,DATA / loc,k,DATA: a data-block element (§6g) */
          if(vm->nin>=5 && vm->in[3].v==OP_VAR && !vm->in[3].d && vm->in[4].v==OP_DATA && !vm->in[4].d){
              vm->db_lvar=1; vm->db_loc=vm->in[1].v; vm->db_off=(uint32_t)vm->g->var[vm->in[2].v&63]; }
          else if(vm->nin>=4 && vm->in[3].v==OP_DATA && !vm->in[3].d){ vm->db_lvar=1; vm->db_loc=vm->in[1].v; vm->db_off=vm->in[2].v; }
          vm->decl_v=ARG(); vm->decl_t=(uint8_t)ARG(); break;
      case OP_ASSIGN:{ uint32_t v=ARG();   /* string assign (decl_t 0xb3) already stored in svar[]; */
          if(vm->db_lvar) db_set_integer(vm,vm->db_loc,vm->db_off,v);
          else if(vm->decl_t!=0xb3 && vm->decl_v==CVM_VAR_QUESTF){ vm->g->on_quest=(uint8_t)(v?1:0); vm->g->mutations++; }   /* the quest flag is the game's (usecode_vm.md) */
          else if(vm->decl_t!=0xb3 && vm->decl_v<64) vm->g->var[vm->decl_v]=(int32_t)v; } break;  /* don't clobber the int var */
      case OP_SETNAME1: if(vm->g->dialect!=CVM_DIALECT_SE) break;     /* SE: $Y = "you" for 1, else as SETNAME (§6c) */
          if(ai<vm->nin && npc_num(vm,vm->in[ai].v)==1){ set_svar(vm,CVM_SVAR_YSTR,"you"); break; }
          /* fall through */
      case OP_SETNAME:{ int n=npc_num(vm,ARG())&0xff; const char*nm=0;   /* $Y = that actor's name: a party member's from the roster, the speaker's from its script (§6c) */
          for(int p=0;p<vm->g->nparty;p++) if(vm->g->party_actor[p]==n){ nm=vm->g->party_name[p]; break; }
          if(!nm && n==vm->npc) nm=vm->npcname; if(!nm) nm="";
          int o=0; while(nm[o]&&o<39){ vm->svar[CVM_SVAR_YSTR][o]=nm[o]; o++; } vm->svar[CVM_SVAR_YSTR][o]=0; } break;
      case OP_JUMP:{ uint32_t loc=ARG(); if(loc<(uint32_t)vm->len) vm->pc=(int32_t)loc; vm->nfr=0; } break;
      case OP_BYE: vm->done=1; break;
      case OP_SIDENT:{ ARG(); int o=0; while(vm->itext[o]&&o<CVM_ITEXT-1){ vm->npcname[o]=vm->itext[o]; o++; } vm->npcname[o]=0; } break;
      case OP_SLOOK: outs(vm,"You see "); print_text(vm,(const uint8_t*)vm->itext,slen(vm->itext)); outc(vm,'\n'); break;
      case OP_KEYWORDS: if(vm->answer_mode!=2) vm->answer_mode = check_keywords(vm->itext,vm->input)?1:0; break;
      case OP_ANSWER: set_break(vm,OP_KEYWORDS);
                      if(vm->answer_mode==1){ set_run(vm,1); vm->answer_mode=2; } else set_run(vm,0); break;
      case OP_INPUTSTR: case OP_INPUT: case OP_INPUTNUM: vm->decl_v=ARG(); vm->decl_t=(uint8_t)ARG(); break;
      /* ---- game-mutation opcodes (act on cvm_game_t) ---- */
      case OP_ADDKARMA: if(vm->g->dialect==CVM_DIALECT_SE) break;   /* not in SE (§6c) */
                        { int k=vm->g->karma+(int)ARG(); vm->g->karma=(int16_t)(k>99?99:k); vm->g->mutations++; } break;
      case OP_SUBKARMA: if(vm->g->dialect==CVM_DIALECT_SE) break;
                        { int k=vm->g->karma-(int)ARG(); vm->g->karma=(int16_t)(k<0?0:k); vm->g->mutations++; } break;
      case OP_GIVE:{ uint32_t o=ARG(), ql=ARG(); int fr=npc_num(vm,ARG()),to=npc_num(vm,ARG());  /* obj,qual,from,to */
                     if(vm->g->inv_give){ vm->g->inv_give(actor_of(vm,fr),actor_of(vm,to),(int)(o&0x3ff),ql?(int)ql:-1); vm->g->mutations++; break; }
                     if(is_player(vm,to)) add(vm,o,ql,1);
                     if(is_player(vm,fr)&&cnt(vm,o)>0) add(vm,o,0,-1);
                     vm->g->mutations++; } break;
      case OP_NEW:{ int np=npc_num(vm,ARG()); uint32_t o=ARG(), ql=ARG(); uint32_t q=ARG();  /* npc,obj,qual,quant */
                    if(vm->g->inv_new){ vm->g->inv_new(actor_of(vm,np),(int)(o&0x3ff),(int)ql,(int)q); vm->g->mutations++; break; }
                    if(is_player(vm,np)) add(vm,o,ql,(int32_t)q);
                    vm->g->mutations++; } break;
      case OP_DELETE:{ int np=npc_num(vm,ARG()); uint32_t o=ARG(); uint32_t dq=ARG(); uint32_t q=ARG();  /* npc,obj,qual,quant */
                       if(vm->g->inv_del){ vm->g->inv_del(actor_of(vm,np),(int)(o&0x3ff),dq?(int)dq:-1,(int)q); vm->g->mutations++; break; }
                       if(is_player(vm,np)) add(vm,o,0,-(int32_t)q);
                       vm->g->mutations++; } break;
      case OP_HEAL:{ int np=npc_num(vm,ARG())&0xff; vm->g->wounded[np]=0; vm->g->healed_full[np]=1; vm->g->mutations++; } break;   /* full HP only: no cure, the dead stay dead (§6f) */
      case OP_CURE:{ int np=npc_num(vm,ARG())&0xff; vm->g->poisoned[np]=0; vm->g->mutations++; } break;
      case OP_RESURRECT:{ int rc=npc_num(vm,ARG()); cvm_game_t*g=vm->g;
          if(g->inv_resurrect){ g->inv_resurrect(actor_of(vm,rc)); g->mutations++; break; }
          if(g->resurrect_req<255) g->resurrect_req++; g->mutations++;   /* the carrier's body rises (§6f) */
          if(g->player_obj[339]>0) g->player_obj[339]--;                 /* ... and leaves the pack now, so a script that asks */
          else if(g->player_obj[354]>0) g->player_obj[354]--;            /* again ("another slain friend?") sees it gone */
          else if(g->lost_body) g->lost_body=0; } break;
      case OP_PORTRAIT: vm->g->portrait_npc=npc_num(vm,ARG()); break;
      case OP_WORKTYPE:{ int np=npc_num(vm,ARG())&0xff; vm->g->worktype[np]=(uint8_t)ARG(); vm->g->mutations++; } break;
      case OP_WAIT: outc(vm,'*'); break;            /* page-break marker */
      case OP_INVENTORY: break;                     /* opens inventory UI; no state */
      case OP_DPRINT:{ uint32_t loc=ARG(),idx=ARG(); char buf[256];  /* data-block string */
                       if(db_string(vm,loc,idx,buf,sizeof buf)>=0) print_text(vm,(const uint8_t*)buf,slen(buf)); } break;
      /* recognised but not modelled here (consume args; no desync) */
      case OP_SCONVERSE: case OP_SPREFIX: case OP_ENDANSWER:
      case OP_ENDDATA: case OP_ASK: case OP_ASKC: default: break;
    }
    #undef ARG
}

/* ---- public API ---------------------------------------------------------- */
void cvm_init(cvm_t*vm,const uint8_t*script,int32_t len,cvm_game_t*g,int npc,
              const char*player,const char*gtitle){
    for(int i=0;i<(int)sizeof(*vm);i++) ((char*)vm)[i]=0;
    vm->s=script; vm->len=len; vm->pc=0; vm->g=g; vm->npc=npc;
    vm->player=player?player:"Avatar"; vm->gtitle=gtitle?gtitle:"milord";
}

void cvm_input(cvm_t*vm,const char*keyword){
    int o=0; if(keyword) while(keyword[o]&&o<CVM_INPUT-1){ vm->input[o]=keyword[o]; o++; }
    vm->input[o]='\0';
    set_svar(vm, CVM_SVAR_INPUT, vm->input);   /* $Z = previous input (used by INDEXOF) */
    if(vm->input_mode==2){                      /* INPUT*: store into the declared var */
        if(vm->decl_t==0xb3) set_svar(vm, vm->decl_v, vm->input);
        else { int n=0,i=0,neg=0; if(vm->input[0]=='-'){neg=1;i=1;}
            while(vm->input[i]>='0'&&vm->input[i]<='9'){ n=n*10+(vm->input[i]-'0'); i++; }
            if(vm->decl_v<64) vm->g->var[vm->decl_v]=neg?-n:n; }
        vm->input_mode=0;
    }
}

const char *cvm_text(const cvm_t*vm){ return vm->out; }

cvm_status cvm_run(cvm_t*vm){
    long steps=0;                       /* guard against malformed JUMP loops */
    vm->outlen=0; vm->out[0]='\0';
    while(!vm->done && vm->pc<vm->len){
        if(++steps > CVM_MAXSTEPS){ vm->done=1; vm->out[vm->outlen]='\0'; return CVM_ERROR; }
        uint8_t b=vm->s[vm->pc];
        if(is_print(b)){
            int start=vm->pc;
            while(vm->pc<vm->len && is_print(vm->s[vm->pc])) vm->pc++;
            if(running(vm)) print_text(vm,vm->s+start,vm->pc-start);
        } else if(is_ctrl(b)){
            collect_input(vm);
            do_frame(vm,(uint8_t)vm->in[0].v);
            if(running(vm)){
                if(vm->in[0].v==OP_ASK || vm->in[0].v==OP_ASKC){ vm->answer_mode=0; vm->input_mode=1; vm->out[vm->outlen]='\0';   /* ASKC: one of the listed characters (§6c) */
                    int k=0; if(vm->in[0].v==OP_ASKC) while(vm->itext[k] && k<(int)sizeof vm->askc-1){ vm->askc[k]=vm->itext[k]; k++; }
                    vm->askc[k]='\0'; vm->input_num=0; return CVM_ASK; }
                do_ctrl(vm);
                if(vm->done){ vm->out[vm->outlen]='\0'; return CVM_DONE; }
                uint32_t op=vm->in[0].v;     /* INPUT*: pause for a value, stored into the decl'd var */
                if(op==OP_INPUTSTR||op==OP_INPUT||op==OP_INPUTNUM){ vm->input_mode=2; vm->askc[0]='\0'; vm->input_num=(op==OP_INPUTNUM);
                    vm->out[vm->outlen]='\0'; return CVM_ASK; }
            }
        } else {
            vm->pc++;   /* stray byte */
        }
    }
    vm->out[vm->outlen]='\0';
    return CVM_DONE;
}
