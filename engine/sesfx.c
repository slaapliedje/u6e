// SPDX-License-Identifier: GPL-2.0-or-later
/* sesfx.c — Savage Empire's sound effects, written from docs/rules/se_sound_effects.md (§3 the engine, §4.3 the
 * PC speaker, §5 the table) and formats/se_music.md §3.6 (the music's claiming). */
#include "sesfx.h"

#define D(p,k,t) { 0, (uint8_t)((p)+1), k, 64, t, 0, 0 }
const sesfx_desc_t SESFX_GAME[SESFX_N] = {
    D(3,60,60), D(4,48,60), D(5,60,10), D(6,60,10), D(7,60,10), D(8,60,10), D(9,60,60), D(10,60,10),
    D(11,60,10), D(12,60,30), D(13,60,60), D(14,69,60), D(15,60,60), D(16,60,60), D(17,60,60), D(18,60,60),
    D(19,60,60), D(20,60,60), D(21,36,60), D(22,60,60), D(23,60,60), D(40,60,60), D(41,60,60), D(42,60,60),
    D(43,60,60), D(44,60,60), { 0x02, 46, 48, 64, 90, 0, 0 }, D(46,60,60), D(47,60,60), D(59,77,90), D(60,60,60),
    D(61,53,60), D(62,60,60), D(63,60,60) };
const sesfx_desc_t SESFX_SPARK = D(127,64,60);
#undef D

static const music_backend_t *g_mo, *g_fo;
static uint8_t own[16];                 /* 0 nobody, 1 the music, 2 an effect */
static uint8_t prio[16];
static int16_t mprog[16], mvol[16], mpan[16], lastprog[16];   /* the music's remembered program, volume, pan; last program sent */
static uint8_t fl[8], nf, ul[8], nu;    /* the free and used lists (channels 1-8), oldest first */

typedef struct { uint8_t used, ch, key, vol, pan, prio; int16_t handle, left; const sesfx_desc_t *st; } slot_t;
static slot_t sl[32];

#define NQ 16
typedef struct { const sesfx_desc_t *d; int16_t vol, pan, handle, prio; } req_t;   /* d = 0: stop (handle < 0: all) */
static req_t q[NQ]; static volatile uint8_t qh, qt;

static void ctl(const music_backend_t *b, int c, int cc, int v){ if(b && b->control) b->control(c,cc,v); }
static void lremove(uint8_t *l, uint8_t *n, int c){ for(int i=0;i<*n;i++) if(l[i]==c){ for(int k=i+1;k<*n;k++) l[k-1]=l[k]; (*n)--; return; } }
static void to_used(int c, int p){ lremove(fl,&nf,c); lremove(ul,&nu,c); ul[nu++]=(uint8_t)c; prio[c]=(uint8_t)p; }
static void to_free(int c){ lremove(ul,&nu,c); lremove(fl,&nf,c); fl[nf++]=(uint8_t)c; }

void sesfx_init(const music_backend_t *mo, const music_backend_t *fo)
{
    g_mo=mo; g_fo=fo; nf=nu=0; qh=qt=0;
    for(int c=0;c<16;c++){ own[c]=0; prio[c]=0; mprog[c]=mvol[c]=mpan[c]=lastprog[c]=-1; }
    for(int c=1;c<=8;c++) fl[nf++]=(uint8_t)c;
    for(int i=0;i<32;i++) sl[i].used=0;
}
void sesfx_forget(void)
{   for(int i=0;i<32;i++) sl[i].used=0;
    for(int c=0;c<16;c++){ if(own[c]==2) own[c]=0; lastprog[c]=-1; }
    nf=nu=0; for(int c=1;c<=8;c++) fl[nf++]=(uint8_t)c; qh=qt; }
int sesfx_sounding(void){ for(int i=0;i<32;i++) if(sl[i].used) return 1; return 0; }

/* ---- the music's side (se_music.md §3.6, se_sound_effects.md §3.3): claim, drop while an effect has it ---- */
static void cut_music(int c){ if(g_mo->control) g_mo->control(c,123,0); else g_mo->note_off(c,0); }   /* (the PSG fold has no controllers: its note off frees the channel's voice) */
static int claim(int c)
{
    if(c<1 || c>8) return 1;
    if(own[c]==2) return 0;
    if(own[c]==0){ cut_music(c);
        if(mprog[c]>=0 && lastprog[c]!=mprog[c] && g_mo->program){ g_mo->program(c,mprog[c]); lastprog[c]=mprog[c]; }
        if(mvol[c]>=0) ctl(g_mo,c,7,mvol[c]);
        if(mpan[c]>=0) ctl(g_mo,c,10,mpan[c]);
        own[c]=1; }
    to_used(c,0);
    return 1;
}
static void g_program(int c,int p){ int k=claim(c); mprog[c&15]=(int16_t)p; if(k){ if(g_mo->program) g_mo->program(c,p); lastprog[c&15]=(int16_t)p; } }
static void g_on(int c,int n,int v){ if(claim(c)) g_mo->note_on(c,n,v); }
static void g_off(int c,int n){ if(claim(c)) g_mo->note_off(c,n); }
static void g_control(int c,int cc,int v){ if(cc==7) mvol[c&15]=(int16_t)v; else if(cc==10) mpan[c&15]=(int16_t)v;
    if(claim(c)) ctl(g_mo,c,cc,v); }
static void g_bend(int c,int l,int m){ if(claim(c) && g_mo->bend) g_mo->bend(c,l,m); }
static void g_reset(void)
{   if(g_mo->control){ for(int c=0;c<16;c++) if(own[c]!=2) g_mo->control(c,123,0); }
    else if(g_mo->reset) g_mo->reset();
    for(int c=0;c<16;c++) if(own[c]==1) own[c]=0; }
const music_backend_t sesfx_gate = { g_program, g_on, g_off, g_reset, g_control, g_bend };

/* ---- the effects' side ---- */
static int alloc(int p)
{   int c=-1;
    if(nf) c=fl[0];
    else for(int i=0;i<nu;i++) if(prio[ul[i]]<=p){ c=ul[i]; break; }
    if(c<0) return -1;
    to_used(c,p);
    for(int i=0;i<32;i++) if(sl[i].used && sl[i].ch==c) sl[i].used=0;   /* (an effect that had it is dropped, no note off) */
    return c; }
static void release(int c){ ctl(g_fo,c,123,0); own[c]=0; to_free(c); }
static void hand_back(int c)
{   release(c); cut_music(c);
    if(mprog[c]>=0 && g_mo->program && c!=9){ g_mo->program(c,mprog[c]); lastprog[c]=mprog[c]; }
    if(mvol[c]>=0) ctl(g_mo,c,7,mvol[c]);
    if(mpan[c]>=0) ctl(g_mo,c,10,mpan[c]);
    own[c]=1; }
static void step_start(slot_t *s)
{
    int c=s->ch;
    if(!s->st->prog1){ release(c); s->used=0; return; }
    if(own[c]!=2){ if(own[c]==1 && g_fo!=g_mo) cut_music(c);   /* (on the PSG the music's part is a voice of its own) */
        ctl(g_fo,c,123,0); own[c]=2; }
    int p=s->st->prog1-1;
    if(g_fo->program) g_fo->program(c,p);
    if(g_fo==g_mo) lastprog[c]=(int16_t)p;
    ctl(g_fo,c,7,s->vol); ctl(g_fo,c,10,s->pan);
    g_fo->note_on(c,s->st->key,s->st->vel);
    s->left=(int16_t)s->st->dur; s->key=s->st->key;
}
static void start(const req_t *r)
{
    int i=-1, c=-1;
    for(int k=0;k<32;k++) if(sl[k].used && sl[k].handle==r->handle){ i=k; break; }
    if(i>=0){ g_fo->note_off(sl[i].ch,sl[i].key); c=sl[i].ch; }
    else { for(int k=0;k<32;k++) if(!sl[k].used){ i=k; break; }
           if(i<0) return;
           c=alloc(r->prio); if(c<0) return; }
    slot_t *s=&sl[i]; s->used=1; s->ch=(uint8_t)c; s->vol=(uint8_t)r->vol; s->pan=(uint8_t)r->pan;
    s->handle=r->handle; s->prio=(uint8_t)r->prio; s->st=r->d;
    step_start(s);
}
static void stop(int handle)
{   for(int i=0;i<32;i++) if(sl[i].used && (handle<0 || sl[i].handle==handle)){ int c=sl[i].ch; sl[i].used=0;
        if(own[c]==2) hand_back(c); }
    if(handle<0) for(int c=1;c<=8;c++) if(own[c]==2) hand_back(c); }

static void put(const sesfx_desc_t *d,int vol,int pan,int handle,int prio)
{   uint8_t n=(uint8_t)((qh+1)%NQ); if(n==qt) return;
    q[qh].d=d; q[qh].vol=(int16_t)vol; q[qh].pan=(int16_t)pan; q[qh].handle=(int16_t)handle; q[qh].prio=(int16_t)prio; qh=n; }
void sesfx_play(const sesfx_desc_t *d,int vol,int pan,int handle,int prio){ if(d) put(d,vol,pan,handle,prio); }
void sesfx_stop_handle(int handle){ put(0,0,0,handle,0); }
void sesfx_stop_all(void){ put(0,0,0,-1,0); }

void sesfx_tick(void)
{
    if(!g_fo) return;
    while(qt!=qh){ const req_t *r=&q[qt]; if(r->d) start(r); else stop(r->handle); qt=(uint8_t)((qt+1)%NQ); }
    for(int i=0;i<32;i++){ slot_t *s=&sl[i]; if(!s->used || (s->st->flags&0x04)) continue;
        if(--s->left>0) continue;
        g_fo->note_off(s->ch,s->key);
        if(s->st->flags&0x02){ int g=s->st->glide;
            if(s->key!=g){ s->key=(uint8_t)(s->key<g?s->key+1:s->key-1); g_fo->note_on(s->ch,s->key,s->st->vel); s->left=(int16_t)s->st->dur; continue; }
            if(s->st->flags&0x08){ s->key=s->st->key; g_fo->note_on(s->ch,s->key,s->st->vel); s->left=(int16_t)s->st->dur; continue; } }
        if(s->st->flags&0x01){ s->st++; step_start(s); }
        else { release(s->ch); s->used=0; } }
}

/* ---- the PC speaker (§4.3): one voice; records of 16 words, run in 1/7680 s ticks ---- */
static const uint16_t PT[48]={256,260,264,267,271,275,279,283,287,292,296,300,304,309,313,318,323,327,332,337,342,347,352,
    357,362,367,373,378,384,389,395,401,406,412,418,424,431,437,443,450,456,463,470,476,483,490,497,505};
#define SPK_MAX 40
static int16_t rec[SPK_MAX][16]; static int nrec;
static int8_t rch[16];                  /* the record each channel's last program chose (-1 none) */
static struct { int on, ch, key, r, ingap; uint16_t F,W,P,S,T,rep,gcnt; int32_t scnt; int gate; uint16_t f; } sp;
static uint32_t spr=0x87654321u;
static int srand_(int a,int b){ spr=spr*3u+((spr&0xffffu)<<16); return a+(int)((spr>>16)%(uint32_t)(b-a+1)); }

int sespk_load(const uint8_t *b,int32_t len)
{   nrec=0; for(int c=0;c<16;c++) rch[c]=-1; sp.on=0;
    if(!b || len<1) return 0;
    int n=b[0]; if(n>SPK_MAX) n=SPK_MAX; if(1+32L*n>len) n=(int)((len-1)/32);
    for(int r=0;r<n;r++) for(int w=0;w<16;w++) rec[r][w]=(int16_t)(b[1+r*32+w*2]|(b[2+r*32+w*2]<<8));
    nrec=n; return n>0; }
static int find(int p){ for(int r=0;r<nrec;r++) if(rec[r][4]==p) return r; return -1; }
static void load(int r){ sp.r=r; sp.F=(uint16_t)rec[r][0]; sp.W=(uint16_t)rec[r][2]; sp.P=(uint16_t)rec[r][5]; sp.S=(uint16_t)rec[r][6];
    sp.T=(uint16_t)(rec[r][7]?rec[r][7]:1); sp.rep=(uint16_t)rec[r][13]; sp.gate=1; sp.scnt=0; sp.ingap=0; }
static void spk_start(int r,int key,int ch){ sp.on=1; sp.ch=ch; sp.key=key; sp.f=0; load(r); }
static void spk_step(void)
{
    const int16_t *R=rec[sp.r]; uint16_t f=sp.F, w=sp.W; int kf=R[14];
    int u=sp.key-60; if(kf<0) u=-u; u<<=((kf>=0?2-kf:kf+3)&3);
    while(u<0){ u+=48; f>>=1; w>>=1; }
    while(u>47){ u-=48; f=(uint16_t)(f<<1); w=(uint16_t)(w<<1); }
    if(u>0){ f=(uint16_t)(((uint32_t)f*PT[u])>>8); w=(uint16_t)(((uint32_t)w*PT[u])>>8); }
    if(w) for(int k=0;k<R[1];k++) f=(uint16_t)(f+srand_(0,w)-(w>>1));
    if(R[3]) f=(uint16_t)(f>>(srand_(0,R[3])&15));
    if((int16_t)sp.P<100) sp.gate=srand_(0,100)<=(int16_t)sp.P;
    if((int16_t)f<19) f=32767;
    sp.f=f;
    sp.F=(uint16_t)(sp.F+R[8]); { int16_t nw=(int16_t)(sp.W+R[9]); sp.W=(uint16_t)(nw<0?0:nw); }
    sp.P=(uint16_t)(sp.P+R[10]); sp.S=(uint16_t)(sp.S+R[11]);
}
static void spk_after_t(void)
{   const int16_t *R=rec[sp.r];
    if(sp.rep>0){ uint16_t k=(uint16_t)(sp.rep-1); load(sp.r); sp.rep=k; return; }
    if(R[15]){ int r=find(R[15]-1); if(r<0) r=0; load(r); return; }
    sp.on=0; }
void sespk_run(int n)
{
    while(n>0 && sp.on){
        if(!sp.ingap && sp.scnt<=0){ spk_step(); sp.scnt=sp.S?sp.S:1; }
        int32_t k=n;
        if(sp.ingap){ if(sp.gcnt<k) k=sp.gcnt; }
        else { if(sp.T<k) k=sp.T; if(sp.scnt<k) k=sp.scnt; }
        if(k<1) k=1;
        n-=(int)k;
        if(sp.ingap){ sp.gcnt=(uint16_t)(sp.gcnt-k); if(!sp.gcnt){ sp.ingap=0; spk_after_t(); } }
        else { sp.scnt-=k; sp.T=(uint16_t)(sp.T-k);
            if(!sp.T){ uint16_t g=(uint16_t)rec[sp.r][12]; if(g){ sp.ingap=1; sp.gcnt=g; } else spk_after_t(); } } }
}
int sespk_freq(void){ return (sp.on && !sp.ingap && sp.gate && sp.f!=32767) ? sp.f : 0; }
static void k_prog(int c,int p){ if(sp.on && sp.ch==c) sp.on=0; rch[c&15]=(int8_t)find(p); }
static void k_on(int c,int n,int v){ (void)v; if(rch[c&15]>=0) spk_start(rch[c&15],n,c); }
static void k_off(int c,int n){ if(sp.on && sp.ch==c && sp.key==n) sp.on=0; }
static void k_ctl(int c,int cc,int v){ (void)c; (void)v; if(cc==123) sp.on=0; }
static void k_reset(void){ sp.on=0; }
const music_backend_t sespk_be = { k_prog, k_on, k_off, k_reset, k_ctl, 0 };
