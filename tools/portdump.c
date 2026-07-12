/* portdump.c — HOST: decode U6 character portraits and render a contact sheet
 * PNG, to validate the format decode against real data.
 *
 * Format (understood from Nuvie portraits/PortraitU6.cpp, implemented from our
 * own /docs/formats/portraits.md): PORTRAIT.A/.B/.Z are LIBRARIES with 4-byte
 * LE offsets; each entry is an LZW-compressed 56x64 8-bit-indexed bitmap
 * (56*64 = 3584 bytes). NPC portrait index = actor_num-1; .A holds 0..97,
 * .B holds 98.. ; .Z holds the avatar portrait(s). Pixels use the U6 palette.
 * (Nuvie additionally dithers for EGA; the VGA path we target uses raw pixels.)
 *
 * Usage: portdump <refdata/dir> <out.png>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "u6lib.h"
#include "u6lzw.h"
#include "u6pal.h"
#include "png.h"

#define PW 56
#define PH 64
#define PXLS (PW*PH)

static uint8_t *slurp(const char*d,const char*name,long*n)
{ char p[1024]; snprintf(p,sizeof p,"%s/%s",d,name);
  FILE*f=fopen(p,"rb"); if(!f){printf("cannot read %s\n",p);return NULL;}
  fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);
  uint8_t*b=malloc((size_t)*n); if(fread(b,1,(size_t)*n,f)!=(size_t)*n){printf("short read\n");exit(2);}
  fclose(f); return b; }

/* decode portrait item `i` from a library buffer into dst[PXLS]; 1 on success */
static int decode_one(const uint8_t*libdata,long liblen,int i,uint8_t*dst)
{
    u6lib_t lib;
    if(!u6lib_open(&lib,libdata,(int32_t)liblen,4,false)){
        /* some libs carry a 4-byte total-size prefix; try that, then auto */
        if(!u6lib_open(&lib,libdata,(int32_t)liblen,4,true) &&
           !u6lib_open_auto(&lib,libdata,(int32_t)liblen)) return 0; }
    int32_t sz; const uint8_t*blob=u6lib_entry(&lib,i,&sz);
    if(!blob||sz<=0) return 0;
    int32_t un=u6lzw_unpacked_size(blob,sz);
    if(un!=PXLS) return 0;                          /* not a 56x64 portrait */
    return u6lzw_decompress(blob,sz,dst,PXLS)==PXLS;
}

static int lib_count(const uint8_t*libdata,long liblen)
{ u6lib_t lib;
  if(!u6lib_open(&lib,libdata,(int32_t)liblen,4,false) &&
     !u6lib_open(&lib,libdata,(int32_t)liblen,4,true) &&
     !u6lib_open_auto(&lib,libdata,(int32_t)liblen)) return 0;
  return (int)u6lib_count(&lib); }

int main(int argc,char**argv)
{
    const char*dir=argc>1?argv[1]:"refdata/dos/u6";
    const char*out=argc>2?argv[2]:"build/portraits.png";

    long an,bn,zn,pn;
    uint8_t*A=slurp(dir,"PORTRAIT.A",&an), *B=slurp(dir,"PORTRAIT.B",&bn);
    uint8_t*Z=slurp(dir,"PORTRAIT.Z",&zn), *palb=slurp(dir,"U6PAL",&pn);
    if(!A||!B||!Z||!palb) return 2;
    u6pal_t pal; u6pal_parse(&pal,palb,(int32_t)pn);
    printf("PORTRAIT.A items=%d  .B items=%d  .Z items=%d\n",
           lib_count(A,an),lib_count(B,bn),lib_count(Z,zn));

    /* contact sheet: 16 from .A, 8 from .B, up to 8 from .Z (avatar) */
    enum { COLS=8, GAP=2, CW=PW+GAP, CH=PH+GAP };
    struct { uint8_t*buf; long len; int first,count; } grp[3]={
        {A,an,0,16},{B,bn,0,8},{Z,zn,0,8} };
    int cells=0; for(int g=0;g<3;g++)cells+=grp[g].count;
    int rows=(cells+COLS-1)/COLS, SW=COLS*CW, SH=rows*CH;
    uint8_t*sheet=malloc((size_t)SW*SH); memset(sheet,0,(size_t)SW*SH);

    static uint8_t pbuf[PXLS]; int cell=0,ok=0;
    for(int g=0;g<3;g++) for(int k=0;k<grp[g].count;k++,cell++){
        int cx=(cell%COLS)*CW, cy=(cell/COLS)*CH;
        if(decode_one(grp[g].buf,grp[g].len,grp[g].first+k,pbuf)){
            ok++;
            for(int y=0;y<PH;y++)for(int x=0;x<PW;x++)
                sheet[(cy+y)*SW+(cx+x)]=pbuf[y*PW+x];
        }
    }
    printf("decoded %d/%d portraits\n",ok,cells);

    uint8_t*img=malloc((size_t)SW*SH*3);
    for(int i=0;i<SW*SH;i++){u6_rgb c=pal.color[sheet[i]];
        img[i*3]=c.r;img[i*3+1]=c.g;img[i*3+2]=c.b;}
    int rc=png_write_rgb(out,SW,SH,img);
    printf("wrote %s (%dx%d)\n",out,SW,SH);
    return rc;
}
