// SPDX-License-Identifier: GPL-2.0-or-later
/* convkw.c — HOST: list every NPC's conversation keywords for the bot (docs/design/bot_harness.md).
 * Reads the user's own CONVERSE.A/.B, unpacks each script and prints the keywords of its keyword
 * sections (converse.md: 0xEF list, comma-separated, ends at 0xF6; '*' = the catch-all is skipped).
 * One line per NPC: "<npc> <name> kw1,kw2,..." (the name from the script header). Prints to stdout
 * only — the bot reads it at run time; nothing derived from the game text is stored in the repo.
 *
 * With an NPC number: one line per keyword section, "<last words before it>\t<keywords>" — how
 * the bot answers a question the script poses (Lord British's Compendium question).
 *
 * Savage Empire: a folder with TALK.LZC instead (converse.md §6b); an item's byte 1 is its actor,
 * which is the number printed and the one [npc] picks.
 *
 * With "raw" after the NPC: that script unpacked, to stdout (to read its structure; never saved).
 *
 * Usage: convkw <dir holding CONVERSE.A and CONVERSE.B, or TALK.LZC> [npc [raw]] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "u6lzw.h"
#include "u6lzc.h"

static uint8_t *slurp(const char*p,long*n)
{ FILE*f=fopen(p,"rb"); if(!f) return NULL;
  fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);
  uint8_t*b=malloc((size_t)*n); if(fread(b,1,(size_t)*n,f)!=(size_t)*n){fclose(f);free(b);return NULL;}
  fclose(f); return b; }
static long off(const uint8_t*f,int i)
{ return (long)(f[i*4]|f[i*4+1]<<8|f[i*4+2]<<16|((uint32_t)f[i*4+3]<<24)); }
static long count_of(const uint8_t*f){ int i=0; while(off(f,i)==0) i++; return off(f,i)/4; }
static uint8_t *load_conv(const uint8_t*f,long flen,int npc,int32_t*len)
{ long count=count_of(f); if(npc<0||npc>=count) return NULL;
  long s=off(f,npc); if(!s) return NULL;
  long e=flen; for(int j=npc+1;j<count;j++){ long o=off(f,j); if(o){e=o;break;} }
  int32_t usz=u6lzw_unpacked_size(f+s,(int32_t)(e-s));
  int32_t cap=(usz>0)?usz:(int32_t)(e-s);
  uint8_t*d=malloc((size_t)cap+4);
  int32_t got=u6lzw_decompress(f+s,(int32_t)(e-s),d,cap+4);
  if(got<=0){free(d);return NULL;} *len=got; return d; }

static void dump(int id,const uint8_t*d,int32_t n)
{
    /* header: 0xFF id name 0xF1 ... (converse.md); the name runs to the first byte >= 0x80 */
    char name[40]; int k=0, p=2; while(p<n && d[p]<0x80 && k<39) name[k++]=(char)d[p++]; name[k]=0;
    for(int i=0;i<k;i++) if(name[i]==' ') name[i]='_';
    printf("%d %s ",id,k?name:"?");
    int first=1;
    for(int32_t i=0;i<n;i++){
        if(d[i]!=0xEF) continue;
        int32_t j=i+1; char w[64]; int wl=0;
        for(;j<n && d[j]!=0xF6 && d[j]<0x80;j++){
            char c=(char)d[j];
            if(c==','){ w[wl]=0; if(wl && strcmp(w,"*")){ printf("%s%s",first?"":",",w); first=0; } wl=0; }
            else if(wl<63 && c!=' ') w[wl++]=c;
        }
        w[wl]=0; if(wl && strcmp(w,"*")){ printf("%s%s",first?"":",",w); first=0; }
        i=j;
    }
    printf("\n");
}

static void sections(const uint8_t*d,int32_t n)
{
    for(int32_t i=0;i<n;i++){
        if(d[i]!=0xEF) continue;
        int32_t e=i; while(e>0 && d[e-1]>=0x80) e--;           /* skip the opcodes before the list */
        int32_t b=e; while(b>0 && d[b-1]<0x80 && e-b<70) b--; /* the text run before them */
        for(int32_t k=b;k<e;k++){ char c=(char)d[k]; putchar(c=='\t'||c=='\n'||c=='\r'?' ':c); }
        putchar('\t');
        int32_t j=i+1; for(;j<n && d[j]!=0xF6 && d[j]<0x80;j++) putchar(d[j]);
        putchar('\n'); i=j;
    }
    /* a question that JUMPs (0xB0 addr32, converse.md) straight into its own keyword loop (0xF7 0xEF):
     * SE's waking questions. The text before the jump, then the keywords there. */
    for(int32_t i=1;i+5<=n;i++){
        if(d[i]!=0xB0 || d[i-1]>=0x80) continue;
        uint32_t t=(uint32_t)(d[i+1]|d[i+2]<<8|d[i+3]<<16|(uint32_t)d[i+4]<<24);
        if(t+2>=(uint32_t)n || d[t]!=0xF7 || d[t+1]!=0xEF) continue;
        int32_t b=i; while(b>0 && d[b-1]<0x80 && i-b<120) b--;
        for(int32_t k=b;k<i;k++){ char c=(char)d[k]; if(c!='@') putchar(c=='\t'||c=='\n'||c=='\r'?' ':c); }
        putchar('\t');
        for(uint32_t j=t+2;j<(uint32_t)n && d[j]!=0xF6 && d[j]<0x80;j++) putchar(d[j]);
        putchar('\n');
    }
}

static int g_raw;   /* convkw <dir> <npc> raw: the unpacked script itself, to stdout */

/* SE: every TALK.LZC item, or only the one whose byte 1 is npc (npc < 0: all) */
static int talk_lzc(const char*dir,int npc)
{
    char p[512]; snprintf(p,sizeof p,"%s/TALK.LZC",dir);
    long n=0; uint8_t*f=slurp(p,&n); if(!f) return -1;
    u6lzc_t lc; if(!u6lzc_open(&lc,f,(int32_t)n,U6LZC_COMP_SE)){ fprintf(stderr,"bad %s\n",p); return 2; }
    static uint8_t d[1<<17]; int found=0;
    for(int i=0;i<u6lzc_count(&lc);i++){
        int32_t got=u6lzc_read(&lc,i,d,sizeof d);
        if(got<=2 || d[0]!=0xFF) continue;
        if(npc<0) dump(d[1],d,got);
        else if(d[1]==npc){ if(g_raw) fwrite(d,1,(size_t)got,stdout); else sections(d,got); found=1; }
    }
    free(f); return npc<0||found ? 0 : 1;
}

int main(int argc,char**argv)
{
    if(argc<2){ fprintf(stderr,"usage: convkw <dir>\n"); return 2; }
    g_raw = argc>3 && !strcmp(argv[3],"raw");
    { int r=talk_lzc(argv[1],argc>2?atoi(argv[2]):-1); if(r>=0) return r; }
    char pa[512], pb[512]; snprintf(pa,sizeof pa,"%s/CONVERSE.A",argv[1]); snprintf(pb,sizeof pb,"%s/CONVERSE.B",argv[1]);
    long la=0, lb=0; uint8_t*a=slurp(pa,&la), *b=slurp(pb,&lb);
    if(!a){ fprintf(stderr,"cannot read %s\n",pa); return 2; }
    long ca=count_of(a), cb=b?count_of(b):0;
    if(argc>2){
        int i=atoi(argv[2]); int32_t n; uint8_t*d=(i<ca)?load_conv(a,la,i,&n):load_conv(b,lb,(int)(i-ca),&n);
        if(!d) return 1;
        if(g_raw) fwrite(d,1,(size_t)n,stdout); else sections(d,n); free(d); return 0;
    }
    for(long i=0;i<ca+cb;i++){
        int32_t n; uint8_t*d=(i<ca)?load_conv(a,la,(int)i,&n):load_conv(b,lb,(int)(i-ca),&n);
        if(!d) continue;
        dump((int)i,d,n); free(d);
    }
    return 0;
}
