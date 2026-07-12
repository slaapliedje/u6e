// SPDX-License-Identifier: GPL-2.0-or-later
/* npcsheet.c — HOST: prove the NPC# -> portrait + name mapping end to end with
 * real data. For each NPC number it pulls the NAME from that NPC's conversation
 * (CONVERSE.A index == actor number) and the PORTRAIT from PORTRAIT.A at index
 * (actor_number - 1) (per Nuvie PortraitU6: portrait = actor_num-1; .A holds
 * 0..97). Renders a grid of portrait + "NN name" so the mapping is verifiable.
 *
 * Usage: npcsheet <refdata/dir> <out.png> [firstNPC lastNPC]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "u6lib.h"
#include "u6lzw.h"
#include "u6pal.h"
#include "u6conv.h"
#include "png.h"

#define PW 56
#define PH 64

static uint8_t g_font[2048];

static uint8_t *slurp(const char*d,const char*name,long*n)
{ char p[1024]; snprintf(p,sizeof p,"%s/%s",d,name);
  FILE*f=fopen(p,"rb"); if(!f){printf("cannot read %s\n",p);exit(2);}
  fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);
  uint8_t*b=malloc((size_t)*n); if(fread(b,1,(size_t)*n,f)!=(size_t)*n){printf("short read\n");exit(2);}
  fclose(f); return b; }

/* CONVERSE.A/.B 4-byte-offset library: decompress NPC `npc`'s script */
static long idx_off(const uint8_t*f,int i)
{ return (long)(f[i*4]|f[i*4+1]<<8|f[i*4+2]<<16|(uint32_t)f[i*4+3]<<24); }
static uint8_t *load_conv(const uint8_t*f,long flen,int npc,int32_t*len)
{ int i=0; while(idx_off(f,i)==0)i++; long count=idx_off(f,i)/4;
  if(npc<0||npc>=count) return NULL;
  long s=idx_off(f,npc); if(!s) return NULL;
  long e=flen; for(int j=npc+1;j<count;j++){long o=idx_off(f,j); if(o){e=o;break;}}
  int32_t usz=u6lzw_unpacked_size(f+s,(int32_t)(e-s));
  int32_t cap=(usz>0)?usz:(int32_t)(e-s);
  uint8_t*d=malloc((size_t)cap+4);
  int32_t got=u6lzw_decompress(f+s,(int32_t)(e-s),d,cap+4);
  if(got<=0){free(d);return NULL;} *len=got; return d; }

/* PORTRAIT.A: 4-byte lib of LZW 56x64; decode item i */
static int decode_portrait(const uint8_t*lib,long liblen,int i,uint8_t*dst)
{ u6lib_t L; if(!u6lib_open(&L,lib,(int32_t)liblen,4,false)) return 0;
  int32_t sz; const uint8_t*b=u6lib_entry(&L,i,&sz);
  if(!b||u6lzw_unpacked_size(b,sz)!=PW*PH) return 0;
  return u6lzw_decompress(b,sz,dst,PW*PH)==PW*PH; }

/* draw text in U6.CH font (MSB-left, index=ASCII) into an indexed buffer */
static void text(uint8_t*buf,int W,int H,int x,int y,const char*s,uint8_t col)
{ for(;*s;s++,x+=8){const uint8_t*g=g_font+(int)(unsigned char)*s*8;
    for(int gy=0;gy<8;gy++){uint8_t b=g[gy];for(int gx=0;gx<8;gx++)
        if((b&(0x80>>gx))&&x+gx>=0&&x+gx<W&&y+gy>=0&&y+gy<H) buf[(y+gy)*W+x+gx]=col;}} }
static int textw(const char*s){int n=0;while(*s++)n++;return n*8;}

int main(int argc,char**argv)
{
    const char*dir=argc>1?argv[1]:"refdata/dos/u6";
    const char*out=argc>2?argv[2]:"build/npcsheet.png";
    int first=argc>3?atoi(argv[3]):2, last=argc>4?atoi(argv[4]):37;

    long an,pan,paln,fn;
    uint8_t*conv=slurp(dir,"CONVERSE.A",&an);
    uint8_t*porA=slurp(dir,"PORTRAIT.A",&pan);
    uint8_t*palb=slurp(dir,"U6PAL",&paln);
    uint8_t*font=slurp(dir,"U6.CH",&fn); if(fn==2048) memcpy(g_font,font,2048);
    u6pal_t pal; u6pal_parse(&pal,palb,(int32_t)paln);
    uint8_t INK=0, BG=1;                              /* black ink; pick a light bg */
    { long bd=1<<30; for(int i=0;i<256;i++){long dr=pal.color[i].r-232,dg=pal.color[i].g-216,db=pal.color[i].b-160;
        long d=dr*dr+dg*dg+db*db; if(d<bd){bd=d;BG=(uint8_t)i;}} }

    int N=last-first+1; if(N<1) N=1;
    enum { COLS=6 };
    int rows=(N+COLS-1)/COLS, CW=PW+8, CH=PH+20, W=COLS*CW, H=rows*CH;
    uint8_t*buf=malloc((size_t)W*H);
    for(int i=0;i<W*H;i++) buf[i]=BG;

    static uint8_t pbuf[PW*PH]; int shown=0;
    for(int k=0;k<N;k++){
        int npc=first+k; int cx=(k%COLS)*CW, cy=(k/COLS)*CH;
        /* name from this NPC's conversation */
        char name[U6CONV_NAME]="?"; int32_t cl; uint8_t*d=load_conv(conv,an,npc,&cl);
        if(d){ u6conv_t c; if(u6conv_parse(&c,d,cl,"Avatar")&&c.name[0]) snprintf(name,sizeof name,"%s",c.name); free(d); }
        /* portrait at index npc-1 */
        if(decode_portrait(porA,pan,npc-1,pbuf)){
            for(int y=0;y<PH;y++)for(int x=0;x<PW;x++){uint8_t v=pbuf[y*PW+x];
                if(v!=255) buf[(cy+2+y)*W+(cx+4+x)]=v;}
            shown++;
        }
        char lbl[40]; snprintf(lbl,sizeof lbl,"%d %s",npc,name);
        if(textw(lbl)>CW) lbl[CW/8]=0;               /* clip to cell */
        text(buf,W,H,cx+2,cy+PH+5,lbl,INK);
    }
    printf("rendered NPCs %d..%d (%d portraits decoded)\n",first,last,shown);

    uint8_t*img=malloc((size_t)W*H*3);
    for(int i=0;i<W*H;i++){u6_rgb c=pal.color[buf[i]];img[i*3]=c.r;img[i*3+1]=c.g;img[i*3+2]=c.b;}
    int rc=png_write_rgb(out,W,H,img);
    printf("wrote %s (%dx%d)\n",out,W,H);
    return rc;
}
