/* cycleprobe.c — host diagnostic: decode ANIMMASK (LZW -> 2048-byte per-tile map)
 * and, for each distinct value (candidate colour-cycle GROUP), find which palette
 * indices the tiles of that group actually use. If a group maps to a tight palette-
 * index range, that range is its colour-cycle range. Clean-room: from our own data.
 * Usage: cycleprobe <ANIMMASK.VGA> <u6.world>
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "u6lzw.h"

static uint32_t be32(const uint8_t*p){return ((uint32_t)p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];}

int main(int argc,char**argv){
    if(argc<3){ printf("usage: %s <ANIMMASK.VGA> <u6.world>\n",argv[0]); return 2; }
    /* decompress ANIMMASK */
    FILE*f=fopen(argv[1],"rb"); if(!f){printf("open mask\n");return 2;}
    fseek(f,0,SEEK_END); long mn=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*mc=malloc(mn); if(fread(mc,1,mn,f)!=(size_t)mn)return 2; fclose(f);
    int32_t usz=u6lzw_unpacked_size(mc,(int32_t)mn);
    uint8_t*mask=malloc(usz); u6lzw_decompress(mc,(int32_t)mn,mask,usz);
    printf("ANIMMASK -> %d bytes\n", usz);

    /* pack: tileset + palette */
    FILE*g=fopen(argv[2],"rb"); if(!g){printf("open pack\n");return 2;}
    fseek(g,0,SEEK_END); long pn=ftell(g); fseek(g,0,SEEK_SET);
    uint8_t*d=malloc(pn); if(fread(d,1,pn,g)!=(size_t)pn)return 2; fclose(g);
    uint32_t map_len=be32(d+4),chunks_len=be32(d+8),ntiles=be32(d+12);
    const uint8_t*tileset=d+24+map_len+chunks_len;
    const uint8_t*pal=tileset+(long)ntiles*256;

    /* value histogram */
    int hist[256]={0}; for(int t=0;t<usz && t<(int)ntiles;t++) hist[mask[t]]++;
    printf("group values (value: #tiles): ");
    for(int v=1;v<256;v++) if(hist[v]) printf("%d:%d ",v,hist[v]);
    printf("\n\n");

    /* per group: which palette indices do its tiles' pixels use? (min..max + count) */
    printf("group  #tiles  palette-index range used (excl 255)   sample tiles\n");
    for(int v=1;v<256;v++){
        if(!hist[v]) continue;
        int lo=256,hi=-1; int idxcount[256]={0}; int shown=0; int samp[6]; int ns=0;
        for(int t=0;t<usz && t<(int)ntiles;t++){
            if(mask[t]!=v) continue;
            if(ns<6) samp[ns++]=t;
            const uint8_t*s=tileset+(long)t*256;
            for(int p=0;p<256;p++){ uint8_t ci=s[p]; if(ci==255) continue;
                idxcount[ci]++; if(ci<lo)lo=ci; if(ci>hi)hi=ci; }
        }
        (void)shown;
        printf("%3d   %5d   [%3d..%3d]  top:", v, hist[v], lo, hi);
        /* top 6 indices by usage */
        for(int k=0;k<6;k++){ int bi=-1,bc=0; for(int i=0;i<256;i++) if(idxcount[i]>bc){bc=idxcount[i];bi=i;}
            if(bi<0)break; printf(" %d(%d)",bi,bc); idxcount[bi]=0; }
        printf("   tiles:"); for(int i=0;i<ns;i++) printf(" %d",samp[i]);
        printf("\n");
    }
    (void)pal;
    return 0;
}
