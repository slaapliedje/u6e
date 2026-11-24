// SPDX-License-Identifier: GPL-2.0-or-later
/* se2pcm.c — HOST pre-renderer of Savage Empire's songs for DMA sound (the AdLib sound).
 *
 * Written from docs/formats/se_adlib.md (the DOS AdLib driver, STRAX.DRV item 1, and the player's side) and
 * docs/formats/se_music.md (the MIDI files and their 0xFE events). Plays each song of the user's MUSIC.LZC
 * through engine/smf.c (SE's sequencer, 60 Hz) into a model of the AdLib driver with the user's FM bank
 * (SAVAGE.TIM item 1), whose OPL2 register writes go to engine/oplsynth.c, 4x oversampled. Output: the U6PC
 * format of tools/m2pcm.c (signed 8-bit mono, big-endian header, a loop start):
 *   a looping song = its intro and two passes of its body, looping on the second (warmed) pass;
 *   a song that ends = the song, its release, then 0.5 s of silence that the DMA loops (so it ends).
 * The engine runs the same sequencer silently beside the DMA for the song's cues, ends and queue
 * (render/gamedemo.c music_play_se), so the two stay in step tick for tick.
 *
 * Clean-room: reads the user's own files; ships nothing (the PCM goes to build/, gitignored).
 * Usage: se2pcm SE_DIR OUT_DIR [rate]      (writes OUT_DIR/SE00.PCM .. SE19.PCM)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "u6lzc.h"
#include "../engine/smf.h"
#include "../engine/oplsynth.h"

static uint8_t *slurp(const char *dir, const char *name, long *n)
{   char p[1024]; snprintf(p,sizeof p,"%s/%s",dir,name); FILE *f=fopen(p,"rb"); if(!f) return NULL;
    fseek(f,0,SEEK_END); *n=ftell(f); fseek(f,0,SEEK_SET); uint8_t *b=malloc((size_t)*n);
    if(b && fread(b,1,(size_t)*n,f)!=(size_t)*n){ free(b); b=NULL; } fclose(f); return b; }
static void put_be32(FILE *f, uint32_t v){ uint8_t b[4]={(uint8_t)(v>>24),(uint8_t)(v>>16),(uint8_t)(v>>8),(uint8_t)v}; fwrite(b,1,4,f); }

/* ---- the bank (se_adlib.md §10, se_music.md §7.3) ---- */
static uint8_t g_bank[86*48]; static int g_nrec;
#define RB(r,o)  (g_bank[(r)*48+(o)])
#define RS16(r,o) ((int16_t)(RB(r,o)|(RB(r,(o)+1)<<8)))
#define RS8(r,o) ((int8_t)RB(r,o))

/* ---- the driver's state (se_adlib.md §2) ---- */
#define NV 9
static int v_key[NV], v_kpitch[NV], v_rec[NV], v_state[NV], v_chan[NV], v_env[NV], v_vib[NV], v_used[NV];
static uint8_t v_phase[NV];
static int ch_rec[32], ch_bend[32], ch_vol[32], ch_vrate[32], ch_vdepth[32];
static int lfree[NV], nfree, lbusy[NV], nbusy;                     /* FREE: released order; BUSY: taken order */
static const uint8_t CAR_OP[NV]={3,4,5,11,12,13,19,20,21}, MOD_OP[NV]={0,1,2,8,9,10,16,17,18};
static const uint16_t FTAB[13]={0x1E5,0x202,0x220,0x241,0x263,0x287,0x2AE,0x2D7,0x302,0x330,0x360,0x393,0x3CA};
/* the "next" value the driver reads below n 18 (se_adlib.md §5, n 7..17): bytes ahead of its F-number table */
static const uint16_t NXLOW[11]={0x0100,0x0802,0x0A09,0x1110,0x0012,0x0201,0x0908,0x100A,0x1214,0x1115,0x01E5};

static void opl(int r,int v){ opl_raw_write(r,v); }
static void lremove(int *l,int *n,int v){ for(int i=0;i<*n;i++) if(l[i]==v){ memmove(l+i,l+i+1,(size_t)(*n-i-1)*sizeof *l); (*n)--; return; } }

static int pitch_word(int P)                                       /* §5: 1/256 semitone -> block<<10 | fnum */
{   P=(int16_t)P; int n=P>>8, frac=P&0xFF;
    int q=(n+6)/12, blk=q-2; if(blk<0) blk=0; if(blk>7) blk=7;
    int s=(n+6)%12; if(s<0) s+=12;                                 /* (n >= -6 in SE's data: the C remainder is s) */
    int f=FTAB[s];
    if(frac){ int nx;
        if(n>=18) nx=FTAB[s+1];
        else if(n>=7) nx=NXLOW[n-7];
        else if(n>=-5) nx=NXLOW[n+5];
        else nx=FTAB[s+1];
        f=f+((int16_t)(uint16_t)((nx-f)*frac)>>8); }
    return (int)(uint16_t)((blk<<10)+f); }

static void load_instr(int v,int r)                                 /* §4.2 */
{   static const int R5[5]={0x20,0x40,0x60,0x80,0xE0};
    for(int k=0;k<5;k++) opl(R5[k]+MOD_OP[v],RB(r,k));
    for(int k=0;k<5;k++) opl(R5[k]+CAR_OP[v],RB(r,5+k));
    opl(0xC0+v,RB(r,0x0A)); }

static int key_pitch(int key,int r)                                 /* §4.1 step 3 */
{   int d=key-60, n=RS8(r,0x27);
    if(n>=0) return 60*256+(int16_t)((d*256)>>n);
    return 60*256+(int16_t)((-d*256)>>(-n-1)); }

static void drv_note(int vel,int key,int c)                         /* §4: the record chain */
{   if(c<0||c>=32) return;
    int r=ch_rec[c]; if(r<0) return;
    for(;;){ int v=-1;
        if(vel){ if(nfree){ v=lfree[0]; lremove(lfree,&nfree,v); lbusy[nbusy++]=v; }
                 else if(r==ch_rec[c] && nbusy){ v=lbusy[0]; lremove(lbusy,&nbusy,v); lbusy[nbusy++]=v; opl(0xA0+v,0); opl(0xB0+v,0); }
                 if(v>=0){ v_state[v]=2; v_key[v]=key; v_used[v]=1;
                     if(v_chan[v]!=c || v_rec[v]!=r){ load_instr(v,r); v_chan[v]=c; v_rec[v]=r; } } }
        else { for(int i=0;i<NV;i++) if(v_state[i]>1 && v_key[i]==key && v_chan[i]==c && v_rec[i]==r){ v=i; break; }
               if(v>=0){ v_state[v]=0; lremove(lbusy,&nbusy,v); lfree[nfree++]=v; } }
        if(v>=0){
            if(vel){ v_vib[v]=0; v_phase[v]=0; v_env[v]=RS16(r,0x12); }
            v_kpitch[v]=key_pitch(key,r);
            int W=pitch_word(v_kpitch[v]+ch_bend[c]+v_env[v]+v_vib[v]+RS16(r,0x24));
            if(!vel){ opl(0xA0+v,W&0xFF); opl(0xB0+v,(W>>8)&0xFF); }
            else {
                int sc=RB(r,0x0C), sm=RB(r,0x0D);
                if(sc || ch_vol[c]<0x100){ int base=RB(r,0x06), t=(int16_t)(((63-vel)>>(7-sc))+(base&63));
                    int prod=(int16_t)(uint16_t)(ch_vol[c]*(63-t)), out=63-(prod>>8);   /* (the product's low 16 bits, then >> 8) */
                    if(out<0) out=0; if(out>63) out=63; opl(0x40+CAR_OP[v],(base&0xC0)+out); }
                if(sm){ int base=RB(r,0x01), t=((63-vel)>>(7-sm))+(base&63); if(t<0) t=0; if(t>63) t=63; opl(0x40+MOD_OP[v],(base&0xC0)+t); }
                W=(W+0x2000)&0xFFFF; opl(0xA0+v,W&0xFF); opl(0xB0+v,(W>>8)&0xFF); } }
        if(!RB(r,0x26) || r+1>=g_nrec) break;
        r++; } }

static void drv_bend(int msb,int lsb,int c)                         /* §5.1 */
{   if(c<0||c>=32) return;
    int b=(msb<<7)+lsb-0x2000, range=ch_rec[c]>=0?RB(ch_rec[c],0x0E):0;
    ch_bend[c]=(int16_t)((b*range)>>8);
    for(int v=0;v<NV;v++) if(v_state[v]>1 && v_chan[v]==c){
        int off=v_rec[v]>=0?RS16(v_rec[v],0x24):0;
        int W=pitch_word(v_kpitch[v]+ch_bend[c]+v_env[v]+v_vib[v]+off)|0x2000;
        opl(0xA0+v,W&0xFF); opl(0xB0+v,(W>>8)&0xFF); } }

static void drv_ctrl(int val,int cc,int c)                          /* §7 */
{   if(c==9) for(int k=10;k<=25;k++) drv_ctrl(val,cc,k);           /* channel 10 reaches the drum channels 11-26 */
    if(c<0||c>=32) return;
    int r=ch_rec[c];
    if(cc==1){ if(r>=0) ch_vdepth[c]=((RB(r,0x0F)*val)>>7)+RB(r,0x11); }
    else if(cc==7) ch_vol[c]=val+128;
    else if(cc==121){ drv_ctrl(0,1,c); drv_ctrl(127,7,c); drv_bend(0x40,0,c); }
    else if(cc==123){ for(int v=0;v<NV;v++) if(v_state[v]>1 && v_chan[v]==c) drv_note(0,v_key[v],c); } }
/* (the recursion above sends channel 10's cc121 to 11-26 twice over; harmless, as DOS's does the same work) */

static void drv_prog(int p,int c)                                   /* §8 */
{   if(c<0||c>=32) return;
    int r=0; for(int i=0;i<g_nrec;i++) if(RB(i,0x2F)==(p&0xFF)){ r=i; break; }
    for(int v=0;v<NV;v++) if(v_chan[v]==c){ drv_note(0,v_key[v],c); v_chan[v]=-1; v_rec[v]=-1; }
    ch_rec[c]=r; ch_vrate[c]=RB(r,0x10); ch_vdepth[c]=RB(r,0x11); }

static void drv_timer(void)                                          /* §6 */
{   for(int v=0;v<NV;v++){ if(!v_used[v]) continue;
        int r=v_rec[v]>=0?v_rec[v]:0, off=v_rec[v]>=0?RS16(v_rec[v],0x24):0, c=v_chan[v], changed=0, e=-1;
        switch(v_state[v]){ case 2: e=0x14; break; case 3: e=0x18; break; case 4: e=0x1C; break; case 0: e=0x20; break; }
        if(e>=0){ int rate=RS16(r,e), tgt=RS16(r,e+2); unsigned d=(unsigned)(uint16_t)(tgt-v_env[v]); int dd=(int16_t)d; unsigned ad=(unsigned)(dd<0?-dd:dd);
            if(ad<(unsigned)(uint16_t)rate){ v_env[v]=tgt; v_state[v]++; }
            else if(v_env[v]<tgt) v_env[v]=(int16_t)(v_env[v]+rate); else v_env[v]=(int16_t)(v_env[v]-rate);
            changed=1; }
        if(c>=0 && c<32 && ch_vrate[c]){ v_phase[v]=(uint8_t)(v_phase[v]+ch_vrate[c]); int p=(int8_t)v_phase[v];
            if(p>63 || p<-64) p=(int8_t)(uint8_t)(0x80-v_phase[v]);
            v_vib[v]=(int16_t)(uint16_t)(ch_vdepth[c]*p)>>4; changed=1; }
        if(changed || off){ int bend=(c>=0&&c<32)?ch_bend[c]:2;      /* (a cleared channel reads +2, §6.3) */
            int W=pitch_word(v_kpitch[v]+bend+v_env[v]+v_vib[v]+off); if(v_state[v]>1) W|=0x2000;
            opl(0xA0+v,W&0xFF); opl(0xB0+v,(W>>8)&0xFF); } } }

static void drv_init(void)
{   for(int v=0;v<NV;v++){ v_key[v]=0xFF; v_kpitch[v]=0; v_rec[v]=0; v_state[v]=1; v_chan[v]=-1; v_env[v]=0; v_vib[v]=0; v_phase[v]=0; v_used[v]=0; lfree[v]=v; }
    nfree=NV; nbusy=0;
    for(int c=0;c<32;c++){ ch_rec[c]=0; ch_bend[c]=0; ch_vol[c]=0x100; ch_vrate[c]=0; ch_vdepth[c]=0; }
    opl(0x01,0x20); opl(0xBD,0); opl(0x08,0);
    static const uint8_t DRUMPROG[17]={128,114,131,113,134,135,133,132,129,136,141,143,144,145,147,140,139};   /* channels 10-26 */
    for(int k=0;k<17;k++) drv_prog(DRUMPROG[k],9+k); }

/* ---- the player's side: the drum remap (se_music.md §7.3) and the backend smf.c drives ---- */
static uint8_t DR_CH[128], DR_NOTE[128];
static void drum_init(void)
{   static const uint8_t T[][3]={{35,10,48},{36,10,48},{37,18,48},{38,11,48},{40,12,48},{41,13,42},{43,13,42},{42,17,71},{44,16,71},
        {45,13,47},{47,13,47},{46,14,71},{48,13,52},{50,13,52},{49,15,79},{51,19,77},{56,21,71},{60,26,72},{61,26,79},{62,25,79},
        {63,20,64},{64,20,58},{67,21,89},{68,21,84},{69,22,48},{70,23,72},{73,24,36},{75,20,96}};
    memset(DR_CH,0,sizeof DR_CH); for(unsigned i=0;i<sizeof T/3;i++){ DR_CH[T[i][0]]=T[i][1]; DR_NOTE[T[i][0]]=T[i][2]; } }
static void be_note_on(int ch,int n,int v)
{   if(ch==9){ if(n<0||n>127||!DR_CH[n]) return; drv_note(v,DR_NOTE[n],DR_CH[n]-1); return; } drv_note(v,n,ch); }
static void be_note_off(int ch,int n)
{   if(ch==9){ if(n<0||n>127||!DR_CH[n]) return; drv_note(0,DR_NOTE[n],DR_CH[n]-1); return; } drv_note(0,n,ch); }
static void be_prog(int ch,int p){ drv_prog(p,ch); }
static void be_ctrl(int ch,int cc,int v){ drv_ctrl(v,cc,ch); }
static void be_bend(int ch,int lsb,int msb){ drv_bend(msb,lsb,ch); }
static void be_reset(void){ }
static const music_backend_t BE={ be_prog, be_note_on, be_note_off, be_reset, be_ctrl, be_bend };

int main(int argc,char **argv)
{
    if(argc<3){ fprintf(stderr,"usage: %s SE_DIR OUT_DIR [rate]\n",argv[0]); return 1; }
    int32_t rate=argc>3?atoi(argv[3]):12517; const int OS=4;
    long fl; uint8_t *b=slurp(argv[1],"SAVAGE.TIM",&fl); u6lzc_t lc;
    if(!b || !u6lzc_open(&lc,b,(int32_t)fl,U6LZC_COMP_SE)){ fprintf(stderr,"SAVAGE.TIM: missing\n"); return 2; }
    static uint8_t tim[8192]; int32_t tn=u6lzc_read(&lc,1,tim,(int32_t)sizeof tim); free(b);
    if(tn<1+48 || tim[0]==0){ fprintf(stderr,"SAVAGE.TIM item 1: bad\n"); return 2; }
    g_nrec=tim[0]>86?86:tim[0]; memcpy(g_bank,tim+1,(size_t)g_nrec*48);
    b=slurp(argv[1],"MUSIC.LZC",&fl);
    if(!b || !u6lzc_open(&lc,b,(int32_t)fl,U6LZC_COMP_SE)){ fprintf(stderr,"MUSIC.LZC: missing\n"); return 2; }
    drum_init();
    long cap=(long)rate*600; int16_t *pcm16=malloc((size_t)cap*sizeof *pcm16); int8_t *pcm8=malloc((size_t)cap);
    static uint8_t song[200000];
    for(int n=0;n<u6lzc_count(&lc) && n<20;n++){
        int32_t sl=u6lzc_read(&lc,n,song,(int32_t)sizeof song); if(sl<=0) continue;
        opl_raw_begin(rate,OS); drv_init();
        smf_seq_t s; if(!smf_start(&s,song,sl,&BE)){ fprintf(stderr,"song %d: not a MIDI file\n",n); continue; }
        for(int c=1;c<=9;c++) drv_ctrl(0,121,c);                    /* the song's start: cc121 on channels 2-10 (§7.1) */
        int whole=0;                                                 /* a loop back to the very start: the song is its own body */
        long off=0, j1=-1, j2=-1, endat=-1; int32_t rem=0;
        while(off<cap){
            drv_timer(); s.jumped=0; smf_tick(&s);                  /* each tick: the driver's effects, then the events */
            if(s.jumped){ if(j1<0) j1=off; else { j2=off; long d=(j2-j1)-j1; whole=(d<0?-d:d)<=rate/30; break; } }   /* (as long as the first: it loops to the start) */
            if(!s.playing && endat<0) endat=off;
            int32_t num=rate+rem, k=num/60; rem=num-k*60;            /* rate/60 samples a tick, the remainder carried */
            if(off+k>cap) k=(int32_t)(cap-off);
            opl_raw_render(pcm16+off,k); off+=k;
            if(endat>=0 && off-endat>=rate*2) break;                 /* (2 s of release after the end) */
        }
        long total, loop, first=0;
        if(j2>0 && whole){ first=j1; total=j2-j1; loop=0; }           /* the second (warmed) pass alone, looped whole */
        else if(j2>0){ total=j2; loop=j1; }                          /* the intro and two passes, looped on the second */
        else { long sil=rate/2; if(off+sil>cap) off=cap-sil; for(long i=0;i<sil;i++) pcm16[off+i]=0; loop=off; total=off+sil; }
        int16_t *src=pcm16+first;
        long peak=1; for(long i=0;i<total;i++){ int a=src[i]; if(a<0) a=-a; if(a>peak) peak=a; }
        double gain=120.0*256.0/(double)peak; if(gain>64.0) gain=64.0; if(gain<1.0) gain=1.0;
        for(long i=0;i<total;i++){ int v=(int)(src[i]*gain/256.0); if(v>127) v=127; if(v<-128) v=-128; pcm8[i]=(int8_t)v; }
        char op[1024]; snprintf(op,sizeof op,"%s/SE%02d.PCM",argv[2],n);
        FILE *o=fopen(op,"wb"); if(!o){ perror(op); return 3; }
        fwrite("U6PC",1,4,o); put_be32(o,(uint32_t)rate); put_be32(o,(uint32_t)total); put_be32(o,(uint32_t)loop);
        fwrite(pcm8,1,(size_t)total,o); fclose(o);
        printf("SE%02d: %.1f s, %s at %.1f s\n",n,(double)total/rate,j2>0?(whole?"loops whole (warmed pass)":"loops"):"ends, silence looped",(double)loop/rate);
    }
    free(b); free(pcm16); free(pcm8); return 0;
}
