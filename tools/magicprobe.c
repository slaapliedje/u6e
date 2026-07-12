/* magicprobe.c — host diagnostic for the MAGIC milestone (clean-room: derive
 * facts from the user's own data, not from anyone's source).
 *
 *   1. obj_n -> name for reagents / spellbook / spell / mana keywords
 *      (LOOK.LZD tile-indexed names + BASETILE obj_n->tile).
 *   2. objlist per-actor 1-byte arrays for the party (Avatar/Dupre/Shamino/Iolo)
 *      so we can spot a MANA/magic-points field next to str/dex/int/hp.
 *
 * Usage: magicprobe <refdata/dos/u6 dir> <objlist>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../dataformats/u6lzw.h"
#include "../dataformats/u6obj.h"

#define NTILES 2048

static uint8_t *slurp(const char *dir, const char *name, long *n){
    char path[512]; snprintf(path,sizeof path,"%s/%s",dir,name);
    FILE*f=fopen(path,"rb"); if(!f){ fprintf(stderr,"cannot open %s\n",path); return NULL; }
    fseek(f,0,SEEK_END); *n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*d=malloc(*n>0?*n:1); if(fread(d,1,*n,f)!=(size_t)*n){fclose(f);return NULL;} fclose(f); return d;
}

static int matches(const char*s){
    static const char*kw[]={"rat","insect","mongbat","bat","snake","spider","gremlin","slug","wisp","daemon",0};
    char low[128]; int i; for(i=0;s[i]&&i<127;i++) low[i]=(s[i]>='A'&&s[i]<='Z')?s[i]+32:s[i]; low[i]=0;
    for(int k=0;kw[k];k++) if(strstr(low,kw[k])) return 1; return 0;
}

int main(int argc,char**argv){
    if(argc<3){ printf("usage: %s <refdata dir> <objlist>\n",argv[0]); return 2; }
    long bn,lkn; uint8_t*bt=slurp(argv[1],"BASETILE",&bn);
    uint8_t*lkc=slurp(argv[1],"LOOK.LZD",&lkn);
    if(!bt||!lkc) return 2;
    int32_t look_len=u6lzw_unpacked_size(lkc,(int32_t)lkn);
    uint8_t*lookd=malloc(look_len>0?look_len:1);
    if(look_len<=0||u6lzw_decompress(lkc,(int32_t)lkn,lookd,look_len)!=look_len){ printf("LOOK decode failed\n"); return 2; }

    /* tile -> name (run-fill: a name applies from its tile index up to the next) */
    static const char*tname[NTILES]; long pos=0; const char*cur="";
    for(int t=0;t<NTILES;t++) tname[t]="";
    while(pos+2<=look_len){
        int ix=lookd[pos]|(lookd[pos+1]<<8); pos+=2; if(ix>=NTILES) break;
        const char*s=(const char*)&lookd[pos]; int sl=(int)strlen(s); pos+=sl+1;
        cur=s; tname[ix]=cur;
        /* run-fill forward until the next explicit index is handled by loop */
    }
    /* forward-fill blanks */
    cur="";
    for(int t=0;t<NTILES;t++){ if(tname[t][0]) cur=tname[t]; else tname[t]=cur; }

    printf("=== obj_n -> name (magic-related) ===\n");
    for(int o=0;o<1024;o++){
        uint16_t base=u6_basetile(bt,(uint16_t)o);
        if(base>=NTILES) continue;
        const char*nm=tname[base];
        if(nm[0]&&matches(nm)) printf("  obj_n %3d (0x%02x)  tile %4u  \"%s\"\n",o,o,base,nm);
    }

    /* objlist mana hunt */
    long on; uint8_t*d=slurp(".",argv[2],&on);
    if(!d){ long dummy; d=slurp("",argv[2],&dummy); on=dummy; }
    if(d){
        printf("\n=== objlist scan, a1..a4 = Avatar,Dupre,Shamino,Iolo (objlist %ld bytes) ===\n", on);
        printf("known: 0x900 str, 0xa00 dex, 0xb00 int, 0xc00 exp(2B LE), 0xe00 hp\n");
        int intv[5]; for(int a=1;a<=4;a++) intv[a]=d[0xb00+a];
        for(long b=0x600;b+8<=on && b<0x2000;b+=0x100){
            /* 2-byte arrays span two 0x100 blocks; label & skip the exp pair */
            if(b==0xd00) continue;
            printf("0x%04lx:", b);
            int looks_mana=1;
            for(int a=1;a<=4;a++){ unsigned v=d[b+a]; printf(" %3u",v);
                if(!(v<=intv[a] && v>0)) looks_mana=0; }
            const char*lbl = b==0x800?" status":b==0x900?" str":b==0xa00?" dex":b==0xb00?" int":
                  b==0xc00?" exp(lo bytes)":b==0xe00?" hp":"";
            printf("%s", lbl);
            if(!*lbl && looks_mana) printf("   <== <=int, mana candidate?");
            printf("\n");
        }
        /* exp read correctly (2-byte LE) */
        printf("exp(2B): ");
        for(int a=1;a<=4;a++){ unsigned v=d[0xc00+a*2]|(d[0xc00+a*2+1]<<8); printf(" %u",v);} printf("\n");
    }
    return 0;
}
