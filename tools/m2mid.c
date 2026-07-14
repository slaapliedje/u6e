// SPDX-License-Identifier: GPL-2.0-or-later
/* m2mid.c — HOST tool: extract the note stream from a U6 .M music file (LZW + OPL2 FM command
 * stream, docs/formats/music.md) and export a Standard MIDI File, to (a) validate the decode BY EAR
 * on the dev PC and (b) prototype the note-extraction the M10 MIDI-OUT backend needs.
 *
 * Clean-room: the .M format was UNDERSTOOD from our own byte analysis + reading the AdPlug u6m
 * player; this code is written from docs/formats/music.md, not transcribed. It ships NO game data.
 *
 * Pitch: freq_byte low-5-bits index a 24-entry OPL F-number table, top-3-bits add to the block;
 * freq = Fnum*49716/2^(20-block); MIDI note = round(69 + 12*log2(freq/440)). Timing: .M is 60 Hz;
 * we write the SMF at 60 PPQ + tempo 1e6 us/qn, so 1 .M tick = 1 SMF tick = 1/60 s.
 *
 * Usage: m2mid FILE.M OUT.mid
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "../dataformats/u6lzw.h"

/* 24-entry OPL F-number table (lo,hi). Entries 0/8/16 = {0,0} = rest. (From the format spec.) */
static const struct { uint8_t lo, hi; } FREQ[24] = {
  {0x00,0x00},{0x58,0x01},{0x82,0x01},{0xB0,0x01},{0xCC,0x01},{0x03,0x02},{0x41,0x02},{0x86,0x02},
  {0x00,0x00},{0x6A,0x01},{0x96,0x01},{0xC7,0x01},{0xE4,0x01},{0x1E,0x02},{0x5F,0x02},{0xA8,0x02},
  {0x00,0x00},{0x47,0x01},{0x6E,0x01},{0x9A,0x01},{0xB5,0x01},{0xE9,0x01},{0x24,0x02},{0x66,0x02}
};

/* packed OPL frequency byte -> MIDI note number, or -1 for a rest / out of range. */
static int freq_to_midi(uint8_t fb){
    int idx = fb & 0x1F, octave = fb >> 5;
    if(idx >= 24) return -1;
    int lo = FREQ[idx].lo, hi = FREQ[idx].hi;
    if(lo==0 && hi==0) return -1;                 /* rest */
    int fnum  = ((hi & 3) << 8) | lo;             /* 10-bit F-number */
    int block = ((hi >> 2) & 7) + octave;         /* OPL block/octave + the packed octave */
    if(block < 0) block = 0;
    if(block > 9) block = 9;
    double freq = (double)fnum * 49716.0 / (double)(1L << (20 - block));
    if(freq < 8.0) return -1;
    long note = lround(69.0 + 12.0 * log2(freq / 440.0));
    if(note < 0) note = 0;
    if(note > 127) note = 127;
    return (int)note;
}

static uint8_t *slurp(const char *p, long *n){
    FILE *f=fopen(p,"rb"); if(!f){ perror(p); exit(1);}
    fseek(f,0,SEEK_END); *n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *b=malloc(*n); if(fread(b,1,*n,f)!=(size_t)*n) exit(1); fclose(f); return b;
}

/* --- authentic Roland MT-32 mapping from MIDI.DAT (the user's own U6 data, human-readable text).
 * Each song lists "(midi_channel/program,...)" pairs, indexed by INSTRUMENT number: the .M's 0x7n
 * "program change" sets an OPL channel's instrument index -> pair[index] = (MIDI channel, MT-32
 * program). channel 1..16, program 1..128 (1-based in the file). (docs/formats/music.md) */
struct pair { int ch, prog; };
static struct pair g_map[16]; static int g_nmap;
/* .M basename -> song order in MIDI.DAT (entries listed 1..9,0,-,= in that fixed order). */
static int song_index(const char *fn){
    static const char *ORDER[12] = { "ULTIMA","BOOTUP","INTRO","CREATE","FOREST","HORNPIPE",
                                     "ENGAGE","STONES","DUNGEON","BRIT","GARGOYLE","END" };
    for(int i=0;i<12;i++) if(strcasecmp(fn,ORDER[i])==0) return i;
    return -1;
}
/* Load the (song_index)th song's (ch/prog) pairs from MIDI.DAT text. Returns pair count (0 if none). */
static int load_mididat(const char *path, int song_index){
    g_nmap=0;
    FILE *f=fopen(path,"rb"); if(!f) return 0;
    char line[512]; int song=-1;
    while(fgets(line,sizeof line,f)){
        char *op=strchr(line,'('), *cl=op?strchr(op,')'):NULL;
        if(!op||!cl||!strchr(op,'/')) continue;      /* only the "N: Name (c/p,...)" song lines */
        if(++song != song_index) continue;
        for(char *s=op+1; s<cl; ){                   /* parse c/p,c/p,... */
            int ch=0,pr=0; if(sscanf(s,"%d/%d",&ch,&pr)==2 && g_nmap<16){ g_map[g_nmap].ch=ch; g_map[g_nmap].prog=pr; g_nmap++; }
            char *comma=strchr(s,','); if(!comma||comma>cl) break; s=comma+1;
        }
        break;
    }
    fclose(f); return g_nmap;
}

/* --- growable MIDI track byte buffer --- */
static uint8_t *trk; static long tlen, tcap;
static void tb(uint8_t v){ if(tlen>=tcap){ tcap=tcap?tcap*2:4096; trk=realloc(trk,tcap);} trk[tlen++]=v; }
static void vlq(uint32_t v){ uint8_t s[5]; int i=0; s[i++]=v&0x7F; while((v>>=7)) s[i++]=0x80|(v&0x7F);
                             while(i) tb(s[--i]); }                        /* MIDI variable-length delta */
static uint32_t g_dt;                                                      /* pending delta ticks */
static void ev3(uint8_t st,uint8_t a,uint8_t b){ vlq(g_dt); g_dt=0; tb(st); tb(a); tb(b); }

int main(int argc, char**argv){
    if(argc<3){ fprintf(stderr,"usage: %s FILE.M OUT.mid\n",argv[0]); return 1; }
    long n; uint8_t *src=slurp(argv[1],&n);
    int32_t usz=u6lzw_unpacked_size(src,(int32_t)n), cap=usz>0?usz:(int32_t)n;
    uint8_t *d=malloc(cap+16);
    int32_t got=u6lzw_decompress(src,(int32_t)n,d,cap);
    if(got<=0){ fprintf(stderr,"decode failed\n"); return 2; }

    /* authentic MT-32 mapping: find MIDI.DAT beside the .M, match this song by basename. */
    { const char *b=strrchr(argv[1],'/'); b=b?b+1:argv[1];
      char base[64]; int i=0; while(b[i] && b[i]!='.' && i<63){ base[i]=b[i]; i++; } base[i]=0;
      char dat[1024]; const char *sl=strrchr(argv[1],'/');
      if(sl){ int d0=(int)(sl-argv[1]+1); snprintf(dat,sizeof dat,"%.*sMIDI.DAT",d0,argv[1]); }
      else snprintf(dat,sizeof dat,"MIDI.DAT");
      int si=song_index(base);
      if(si>=0) load_mididat(dat, si);
    }

    /* tempo meta: 1,000,000 us/quarter (with 60 PPQ => 60 ticks/sec). */
    tb(0); tb(0xFF); tb(0x51); tb(3); tb(0x0F); tb(0x42); tb(0x40);
    /* program-changes: instrument index i -> (MIDI channel, MT-32 program) from MIDI.DAT (at t=0). */
    for(int i=0;i<g_nmap;i++){ int mch=(g_map[i].ch-1)&15, pr=(g_map[i].prog-1)&127;
        vlq(0); tb(0xC0|mch); tb((uint8_t)pr); }

    int cur[9];  for(int i=0;i<9;i++) cur[i]=-1;                /* current sounding MIDI note per OPL channel */
    int cmch[9]; for(int i=0;i<9;i++) cmch[i]=i;                /* the MIDI channel that note was sent on     */
    int vel[9];  for(int i=0;i<9;i++) vel[i]=90;                /* per-channel velocity from the 0x3n level    */
    int inst[9]; for(int i=0;i<9;i++) inst[i]=0;               /* OPL channel's assigned instrument index     */
    int32_t p=0; int nnote=0, minn=127, maxn=0;
    while(p<got){
        uint8_t bb=d[p]; int hi=bb>>4, lo=bb&0xF; int32_t start=p; (void)start; p++;
        int need; switch(hi){
          case 0x0: case 0x1: case 0x2: case 0x3: case 0x4: case 0x5: case 0x6: case 0x7: need=1; break;
          case 0x8: need = (bb==0x81)?3 : (bb==0x83)?12 : (bb==0x82||bb==0x85||bb==0x86)?1 : 0; break;
          default:  need=0; break;                              /* 0xE/0xF/undef: 0 operands */
        }
        if(p+need>got) break;
        if(bb==0x82){ g_dt += d[p]; }                           /* DELAY: accumulate SMF delta ticks */
        else if(hi==0x7 && lo<9){ inst[lo]=d[p]; }              /* PROGRAM CHANGE: OPL ch -> instrument index */
        else if(hi==0x3 && lo<9){                               /* SET LEVEL: OPL 6-bit attenuation -> velocity */
            int v=(0x3F-(d[p]&0x3F))*3/2; if(v<1)v=1; if(v>127)v=127;   /* ScummVM: (0x3F-lvl)*1.5 */
            vel[lo]=v;
        }
        else if(hi<=0x2 && lo<9){                               /* note command on channel lo */
            /* route via the OPL channel's instrument -> MIDI channel (MIDI.DAT), or fall back to OPL ch. */
            int mch = (g_nmap>0 && inst[lo]<g_nmap) ? (g_map[inst[lo]].ch-1)&15 : lo;
            int newn = (hi==0x0)? -1 : freq_to_midi(d[p]);      /* 0x0=off; 0x1/0x2 set a pitch */
            if(hi!=0x2){                                        /* 0x0 and 0x1 turn the old note off */
                if(cur[lo]>=0){ ev3(0x80|cmch[lo], cur[lo], 0); cur[lo]=-1; }
            }
            if(hi!=0x0 && newn>=0){                             /* 0x1/0x2 turn a new note on */
                if(cur[lo]>=0) ev3(0x80|cmch[lo], cur[lo], 0);  /* safety off (0x2 case) */
                ev3(0x90|mch, newn, vel[lo]); cur[lo]=newn; cmch[lo]=mch; nnote++;
                if(newn<minn) minn=newn;
                if(newn>maxn) maxn=newn;
            }
        }
        p+=need;
    }
    for(int i=0;i<9;i++) if(cur[i]>=0) ev3(0x80|cmch[i], cur[i], 0);  /* all-notes-off at end */
    tb(0); tb(0xFF); tb(0x2F); tb(0);                           /* end of track */

    /* write the SMF (format 0, 1 track, 60 PPQ) */
    FILE *o=fopen(argv[2],"wb"); if(!o){ perror(argv[2]); return 3; }
    uint8_t hdr[14]={'M','T','h','d',0,0,0,6, 0,0, 0,1, 0,60};
    fwrite(hdr,1,14,o);
    uint8_t th[8]={'M','T','r','k',(uint8_t)(tlen>>24),(uint8_t)(tlen>>16),(uint8_t)(tlen>>8),(uint8_t)tlen};
    fwrite(th,1,8,o); fwrite(trk,1,tlen,o); fclose(o);
    printf("%s -> %s : %d note-ons, range %d..%d, %ld B; MT-32 map[%d]:",
           argv[1], argv[2], nnote, nnote?minn:0, maxn, tlen, g_nmap);
    for(int i=0;i<g_nmap;i++) printf(" ch%d/pg%d", g_map[i].ch, g_map[i].prog);
    fputs(g_nmap?"\n":" (none - fallback ch=OPL)\n", stdout);
    return 0;
}
