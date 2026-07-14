// SPDX-License-Identifier: GPL-2.0-or-later
/* musictest.c — HOST unit test for the portable sequencer (engine/music.c): decompress a .M, load
 * the MT-32 map from MIDI.DAT, run music_seq_tick() 1 tick at a time through a capture backend that
 * records events into a Standard MIDI File, for MAXSEC seconds of playback (the sequencer follows the
 * real loop/branch structure, so it loops — we cap it). Validates the sequencer without an emulator.
 * Usage: musictest FILE.M OUT.mid [seconds]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../dataformats/u6lzw.h"
#include "../engine/music.h"

/* ---- MIDI.DAT parse (same as tools/m2mid) ---- */
static struct { int ch, prog; } g_pairs[16]; static int g_np;
static int song_index(const char *fn){
    static const char *O[12]={"ULTIMA","BOOTUP","INTRO","CREATE","FOREST","HORNPIPE",
                              "ENGAGE","STONES","DUNGEON","BRIT","GARGOYLE","END"};
    for(int i=0;i<12;i++) if(strcasecmp(fn,O[i])==0) return i;
    return -1;
}
static void load_mididat(const char *path, int si){
    g_np=0; FILE*f=fopen(path,"rb"); if(!f) return; char line[512]; int s=-1;
    while(fgets(line,sizeof line,f)){
        char*op=strchr(line,'('),*cl=op?strchr(op,')'):0; if(!op||!cl||!strchr(op,'/')) continue;
        if(++s!=si) continue;
        for(char*p=op+1;p<cl;){ int c=0,pr=0; if(sscanf(p,"%d/%d",&c,&pr)==2&&g_np<16){g_pairs[g_np].ch=c;g_pairs[g_np].prog=pr;g_np++;}
            char*cm=strchr(p,','); if(!cm||cm>cl)break; p=cm+1; }
        break;
    }
    fclose(f);
}
static uint8_t *slurp(const char *p, long *n){ FILE*f=fopen(p,"rb"); if(!f){perror(p);exit(1);}
    fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET); uint8_t*b=malloc(*n); if(fread(b,1,*n,f)!=(size_t)*n)exit(1); fclose(f); return b; }

/* ---- capture backend: record events at the current global tick into a growable SMF track ---- */
static uint8_t *trk; static long tlen, tcap; static uint32_t g_tick, g_last; static int g_events, g_notes;
static void tb(uint8_t v){ if(tlen>=tcap){tcap=tcap?tcap*2:8192; trk=realloc(trk,tcap);} trk[tlen++]=v; }
static void vlq(uint32_t v){ uint8_t s[5];int i=0; s[i++]=v&0x7F; while((v>>=7))s[i++]=0x80|(v&0x7F); while(i)tb(s[--i]); }
static void emit(uint8_t st,uint8_t a,uint8_t b,int n){ vlq(g_tick-g_last); g_last=g_tick; tb(st); tb(a); if(n>1)tb(b); g_events++; }
static void be_program(int ch,int prog){ emit(0xC0|(ch&15),prog&127,0,1); }
static void be_noteon (int ch,int n,int v){ emit(0x90|(ch&15),n&127,v&127,2); g_notes++; }
static void be_noteoff(int ch,int n){ emit(0x80|(ch&15),n&127,0,2); }
static void be_reset(void){}
static const music_backend_t CAP = { be_program, be_noteon, be_noteoff, be_reset };

int main(int argc,char**argv){
    if(argc<3){ fprintf(stderr,"usage: %s FILE.M OUT.mid [seconds]\n",argv[0]); return 1; }
    int secs = argc>3 ? atoi(argv[3]) : 60;
    long n; uint8_t*src=slurp(argv[1],&n);
    int32_t usz=u6lzw_unpacked_size(src,(int32_t)n), cap=usz>0?usz:(int32_t)n;
    uint8_t*d=malloc(cap+16); int32_t got=u6lzw_decompress(src,(int32_t)n,d,cap);
    if(got<=0){ fprintf(stderr,"decode failed\n"); return 2; }

    /* MT-32 map from MIDI.DAT (beside the .M), 1-based -> 0-based for music_map_t. */
    { const char*b=strrchr(argv[1],'/'); b=b?b+1:argv[1]; char base[64]; int i=0;
      while(b[i]&&b[i]!='.'&&i<63){base[i]=b[i];i++;} base[i]=0;
      char dat[1024]; const char*sl=strrchr(argv[1],'/');
      if(sl) snprintf(dat,sizeof dat,"%.*sMIDI.DAT",(int)(sl-argv[1]+1),argv[1]); else snprintf(dat,sizeof dat,"MIDI.DAT");
      int si=song_index(base); if(si>=0) load_mididat(dat,si); }
    music_map_t map[16]; for(int i=0;i<g_np;i++){ map[i].ch=(uint8_t)((g_pairs[i].ch-1)&15); map[i].prog=(uint8_t)((g_pairs[i].prog-1)&127); }

    /* tempo meta (60 PPQ, 1e6 us/qn => 60 ticks/s). */
    tb(0);tb(0xFF);tb(0x51);tb(3);tb(0x0F);tb(0x42);tb(0x40);

    music_seq_t m; music_seq_start(&m, d, got, map, g_np, &CAP);
    int total = secs*60;
    for(g_tick=0; g_tick<(uint32_t)total && m.playing; g_tick++) music_seq_tick(&m, 1);

    tb(0);tb(0xFF);tb(0x2F);tb(0);                              /* end of track */
    FILE*o=fopen(argv[2],"wb"); if(!o){perror(argv[2]);return 3;}
    uint8_t hdr[14]={'M','T','h','d',0,0,0,6,0,0,0,1,0,60}; fwrite(hdr,1,14,o);
    uint8_t th[8]={'M','T','r','k',(uint8_t)(tlen>>24),(uint8_t)(tlen>>16),(uint8_t)(tlen>>8),(uint8_t)tlen};
    fwrite(th,1,8,o); fwrite(trk,1,tlen,o); fclose(o);
    printf("%s: %d events, %d note-ons over %ds, playing=%d, map[%d]\n",
           argv[1], g_events, g_notes, secs, m.playing, g_np);
    return 0;
}
