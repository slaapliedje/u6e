// SPDX-License-Identifier: GPL-2.0-or-later
/* tflagprobe.c — HOST diagnostic (not shipped). Reads the user's TILEFLAG +
 * LOOK.LZD to (a) find the throne's tiles and their tileflags2, and (b) decode
 * the bits6-7 "size" (double width/height) encoding empirically by listing which
 * named objects carry which size value. Clean-room: derive the spec from data. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "u6lzw.h"
#include "u6tile.h"

#define NTILES 2048

static uint8_t *slurp(const char *path, long *n){
    FILE *f=fopen(path,"rb"); if(!f){ *n=-1; return NULL; }
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *b=malloc(sz>0?sz:1); *n=(long)fread(b,1,sz,f); fclose(f); return b;
}

int main(int argc,char**argv){
    const char *dir = argc>1?argv[1]:"refdata/dos/u6";
    char p[512];
    long tfn,lkn;
    snprintf(p,sizeof p,"%s/TILEFLAG",dir); uint8_t *tf=slurp(p,&tfn);
    snprintf(p,sizeof p,"%s/LOOK.LZD",dir); uint8_t *lk=slurp(p,&lkn);
    if(!tf||tfn<0x1C00){ printf("TILEFLAG missing/short (%ld)\n",tfn); return 1; }
    if(!lk||lkn<=0){ printf("LOOK.LZD missing\n"); return 1; }

    int32_t ulen=u6lzw_unpacked_size(lk,(int32_t)lkn);
    uint8_t *look=malloc(ulen>0?ulen:1);
    if(ulen<=0||u6lzw_decompress(lk,(int32_t)lkn,look,ulen)!=ulen){ printf("LOOK decode fail\n"); return 1; }

    /* build tile->name (run-fill, singular form only: drop '\'-runs, keep '/'-runs) */
    static char *name[NTILES];
    int prev=-1; int32_t pos=0;
    while(pos+2<=ulen){
        int index=look[pos]|(look[pos+1]<<8); pos+=2;
        if(index>=NTILES) break;
        char *s=(char*)&look[pos]; int sl=(int)strlen(s); pos+=sl+1;
        /* render singular form */
        static char buf[128]; int bi=0; int mode=0; /* 0=common 1=plural 2=singular */
        for(int i=0;i<sl&&bi<127;i++){ char c=s[i];
            if(c=='\\'){mode=1;continue;} if(c=='/'){mode=2;continue;}
            if(mode==1 && ((c>='a'&&c<='z')||(c>='A'&&c<='Z'))) continue; /* plural-only: skip */
            buf[bi++]=c; if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z'))) mode=0;
        }
        buf[bi]=0;
        char *ns=strdup(buf);
        for(int t=prev+1;t<=index&&t<NTILES;t++) name[t]=ns;
        prev=index;
    }

    /* (a) all tiles whose name mentions throne / chair / seat */
    printf("=== tiles named throne/chair/seat (tile: flags2 hex  fg=bit4  size=(bits6-7)) ===\n");
    for(int t=0;t<NTILES;t++){ if(!name[t]) continue;
        if(strstr(name[t],"throne")||strstr(name[t],"chair")||strstr(name[t],"seat")){
            uint8_t f2=tf[0x800+t];
            printf("  tile %4d  flags2=%02x  fg=%d  size=%d   \"%s\"\n",
                   t,f2,(f2>>4)&1,(f2>>6)&3,name[t]);
        }
    }
    /* (b) size-bit histogram + sample names per size value (decode the encoding) */
    printf("\n=== (bits6-7) size histogram over all 2048 tiles, with sample names ===\n");
    for(int sz=0;sz<4;sz++){ int cnt=0; printf("size=%d:",sz);
        for(int t=0;t<NTILES;t++) if(((tf[0x800+t]>>6)&3)==sz) cnt++;
        printf(" %d tiles. samples:",cnt);
        int shown=0;
        for(int t=512;t<NTILES&&shown<8;t++) if(((tf[0x800+t]>>6)&3)==sz && name[t] && strcmp(name[t],"nothing")){
            printf(" [%d \"%s\"]",t,name[t]); shown++; }
        printf("\n");
    }
    /* (c) foreground (toptile) count */
    int fg=0; for(int t=0;t<NTILES;t++) if(tf[0x800+t]&0x10) fg++;
    printf("\nforeground (bit4) tiles: %d / 2048\n",fg);

    /* (d) render throne tiles as ASCII to confirm double-height stacking direction.
     * If T is the BOTTOM half and T-1 the TOP, then tile 1200 (top) should show the
     * throne back/upper, and 1201 (bottom) the seat/lower. */
    long mtn,otn,tin,mkn;
    snprintf(p,sizeof p,"%s/MAPTILES.VGA",dir); uint8_t *mtc=slurp(p,&mtn);
    snprintf(p,sizeof p,"%s/OBJTILES.VGA",dir); uint8_t *ot=slurp(p,&otn);
    snprintf(p,sizeof p,"%s/TILEINDX.VGA",dir); uint8_t *ti=slurp(p,&tin);
    snprintf(p,sizeof p,"%s/MASKTYPE.VGA",dir); uint8_t *mkc=slurp(p,&mkn);
    if(mtc&&ot&&ti){
        int32_t musz=u6lzw_unpacked_size(mtc,(int32_t)mtn);
        int32_t comb_len=musz+(int32_t)otn; uint8_t *comb=malloc(comb_len);
        u6lzw_decompress(mtc,(int32_t)mtn,comb,musz); memcpy(comb+musz,ot,(size_t)otn);
        u6tileset_t ts; u6tile_init(&ts,comb,comb_len,ti,(int32_t)tin);
        if(mkc){ int32_t msz=u6lzw_unpacked_size(mkc,(int32_t)mkn); uint8_t *msk=malloc(msz);
            if(u6lzw_decompress(mkc,(int32_t)mkn,msk,msz)==msz) u6tile_set_masktype(&ts,msk); }
        int show[]={706,707,708,709};  /* bed: anchor 709 (size=3, 2x2) -> 708 left, 707 up, 706 up-left */
        for(unsigned k=0;k<sizeof show/sizeof*show;k++){ int t=show[k]; uint8_t b[256];
            u6tile_decode(&ts,t,b);
            uint8_t f2=tf[0x800+t];
            printf("\n--- tile %d  \"%s\"  fg=%d size=%d ---\n",t,name[t]?name[t]:"?",(f2>>4)&1,(f2>>6)&3);
            for(int y=0;y<16;y++){ for(int x=0;x<16;x++){ uint8_t v=b[y*16+x];
                putchar(v==255?'.':(v<64?'#':(v<160?'o':':'))); } putchar('\n'); }
        }
    }
    return 0;
}
