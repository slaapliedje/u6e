// SPDX-License-Identifier: GPL-2.0-or-later
/* sesfxtest.c — host test of engine/sesfx.c against docs/rules/se_sound_effects.md.
 *
 * 1. The engine's channel rules (§3.3, §3.4, §3.6, formats/se_music.md §3.6), checked on a recorded MIDI stream:
 *    an effect takes a free channel first, else the music channel quiet the longest; All Notes Off on take and on
 *    release; the music's events dropped while the effect has the channel; the music's claim at its next event.
 * 2. If SE_DIR is given: the user's SAVAGE.TIM item 2 through the speaker model (§4.3), each GAME.EXE effect for its
 *    descriptor's length, written as square-wave WAVs into OUT_DIR (sfxNN.wav) for listening and spectrograms.
 * Usage: sesfxtest [SE_DIR OUT_DIR]      Prints PASS/FAIL lines; exit 1 on a failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "u6lzc.h"
#include "../engine/sesfx.h"

static char g_log[8192]; static int g_ln;
static void lg(const char *f,int a,int b,int c){ g_ln+=snprintf(g_log+g_ln,sizeof g_log-(size_t)g_ln,f,a,b,c); }
static void m_prog(int c,int p){ lg("P%d:%d ",c,p,0); }
static void m_on(int c,int n,int v){ lg("N%d:%d:%d ",c,n,v); }
static void m_off(int c,int n){ lg("O%d:%d ",c,n,0); }
static void m_reset(void){ lg("R ",0,0,0); }
static void m_ctl(int c,int cc,int v){ lg("C%d:%d:%d ",c,cc,v); }
static const music_backend_t MO={ m_prog,m_on,m_off,m_reset,m_ctl,0 };
static int g_fail;
static void expect(const char *what,const char *want)
{   int ok=strcmp(g_log,want)==0; printf("%s %s\n",ok?"PASS":"FAIL",what);
    if(!ok){ printf("  want: %s\n  got:  %s\n",want,g_log); g_fail=1; }
    g_ln=0; g_log[0]=0; }

static uint8_t *slurp(const char *dir,const char *name,long *n)
{   char p[1024]; snprintf(p,sizeof p,"%s/%s",dir,name); FILE *f=fopen(p,"rb"); if(!f) return NULL;
    fseek(f,0,SEEK_END); *n=ftell(f); fseek(f,0,SEEK_SET); uint8_t *b=malloc((size_t)*n);
    if(b && fread(b,1,(size_t)*n,f)!=(size_t)*n){ free(b); b=NULL; } fclose(f); return b; }
static void le32(FILE *f,uint32_t v){ for(int i=0;i<4;i++) fputc((int)((v>>(8*i))&255),f); }
static void le16(FILE *f,unsigned v){ fputc((int)(v&255),f); fputc((int)((v>>8)&255),f); }

int main(int argc,char **argv)
{
    /* 1. channel rules: the music plays on channels 1-3 (0-based), then an effect, then another */
    sesfx_init(&MO,&MO);
    const music_backend_t *G=&sesfx_gate;
    G->program(1,10); G->control(1,7,100); G->note_on(1,60,90);
    G->program(2,20); G->note_on(2,62,90);
    expect("the music claims each channel at its first event",
           "C1:123:0 P1:10 C1:7:100 N1:60:90 C2:123:0 P2:20 N2:62:90 ");
    sesfx_play(&SESFX_GAME[0],127,63,0,2); sesfx_tick();
    expect("an effect takes a free channel first (channel 3, the head of the free list)",
           "C3:123:0 P3:3 C3:7:127 C3:10:63 N3:60:64 ");
    /* fill the free list: effects on other handles take 4-8 */
    for(int h=1;h<=5;h++) sesfx_play(&SESFX_GAME[2],127,63,h,2);
    sesfx_tick(); g_ln=0; g_log[0]=0;
    G->note_on(2,64,90); g_ln=0; g_log[0]=0;  /* the music touches channel 2: now 1 is the quietest */
    sesfx_play(&SESFX_GAME[4],127,63,6,2); sesfx_tick();
    expect("with no free channel the effect steals the music channel quiet the longest (1)",
           "C1:123:0 P1:7 C1:7:127 C1:10:63 N1:60:64 ");
    G->note_on(1,65,90); G->note_off(1,60);
    expect("the music's events on a borrowed channel are dropped","");
    for(int t=0;t<7;t++) sesfx_tick();       /* (ticks 4-10; the five started at tick 2, id 4 at tick 3) */
    g_ln=0; g_log[0]=0;
    sesfx_tick();
    expect("10 ticks on (the first is the tick that starts them): note off, All Notes Off, the channel freed",
           "O4:60 C4:123:0 O5:60 C5:123:0 O6:60 C6:123:0 O7:60 C7:123:0 O8:60 C8:123:0 ");
    sesfx_tick();
    expect("and the one that stole channel 1 a tick later", "O1:60 C1:123:0 ");
    G->note_on(1,67,90);
    expect("the music claims it back at its next event (its program again: the effect's was the last sent)",
           "C1:123:0 P1:10 C1:7:100 N1:67:90 ");
    sesfx_play(&SESFX_GAME[0],127,63,0,2); sesfx_tick();   /* handle 0 sounds still: reused, its channel kept */
    expect("a new effect on a sounding handle cuts the old one and keeps its channel",
           "O3:60 P3:3 C3:7:127 C3:10:63 N3:60:64 ");
    sesfx_stop_all(); sesfx_tick();
    expect("stop-all hands the channel back to the music at once",
           "C3:123:0 C3:123:0 ");

    /* 2. the speaker records */
    if(argc>=3){
        long fl; uint8_t *b=slurp(argv[1],"SAVAGE.TIM",&fl); u6lzc_t lc;
        if(!b || !u6lzc_open(&lc,b,(int32_t)fl,U6LZC_COMP_SE)){ printf("FAIL SAVAGE.TIM missing\n"); return 1; }
        static uint8_t it2[4096]; int32_t n=u6lzc_read(&lc,2,it2,(int32_t)sizeof it2); free(b);
        if(!sespk_load(it2,n)){ printf("FAIL SAVAGE.TIM item 2\n"); return 1; }
        printf("PASS speaker records: %d\n",it2[0]);
        const int RATE=7680*4;
        for(int id=0;id<SESFX_N;id++){
            sesfx_init(&MO,&sespk_be); sesfx_play(&SESFX_GAME[id],127,63,0,2);
            static int8_t pcm[7680*4*4]; long np=0; uint32_t ph=0; long sounding=0;
            for(int t=0;t<240 && np+RATE/60<(long)sizeof pcm;t++){   /* 4 s at most */
                sesfx_tick(); g_ln=0;
                for(int k=0;k<128;k++){ sespk_run(1); int f=sespk_freq(); if(f) sounding++;
                    for(int o=0;o<4;o++){ ph+=(uint32_t)((uint64_t)f*65536u/(uint32_t)RATE); pcm[np++]=(int8_t)(f?((ph&0x8000)?40:-40):0); } }
                if(!sesfx_sounding() && !sespk_freq() && t>2) break; }
            char p[1024]; snprintf(p,sizeof p,"%s/sfx%02d.wav",argv[2],id); FILE *f=fopen(p,"wb");
            if(!f){ printf("FAIL cannot write %s\n",p); return 1; }
            fwrite("RIFF",1,4,f); le32(f,36+(uint32_t)np); fwrite("WAVEfmt ",1,8,f); le32(f,16); le16(f,1); le16(f,1);
            le32(f,RATE); le32(f,RATE); le16(f,1); le16(f,8); fwrite("data",1,4,f); le32(f,(uint32_t)np);
            for(long i=0;i<np;i++) fputc((uint8_t)(pcm[i]+128),f);
            fclose(f);
            printf("     sfx%02d: %4ld ms of sound in %4ld ms\n",id,sounding*1000/7680,np*1000/RATE); }
    }
    return g_fail;
}
