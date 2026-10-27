// SPDX-License-Identifier: GPL-2.0-or-later
/* wtrun.c — host harness: run one real U6 conversation through the engine's VM and report
 * the WORKTYPE changes it makes (docs/rules/usecode_vm.md §6b). Reads the user's own
 * CONVERSE.A/.B (never shipped); prints NPC numbers and worktype codes, plus the VM text
 * of each turn when -v is given. Usage: wtrun <refdata dir> <npc> [keyword ...] [-v] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "converse_vm.c"
#include "u6lzw.h"

static uint8_t *slurp(const char *p, long *n){ FILE *f=fopen(p,"rb"); if(!f) return 0; fseek(f,0,SEEK_END); *n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *b=malloc((size_t)*n); if(b && fread(b,1,(size_t)*n,f)!=(size_t)*n){ free(b); b=0; } fclose(f); return b; }
static uint32_t rd4le(const uint8_t *p){ return p[0]|(p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
/* 4-byte-offset library (docs/formats/library.md): count = first nonzero offset / 4 */
static int lib_cnt(const uint8_t *d, long n){ int z=0; while((long)(z*4+4)<=n && rd4le(d+z*4)==0) z++; return (int)(rd4le(d+z*4)/4); }
static const uint8_t *lib_ent(const uint8_t *d, long n, int i, int32_t *len){
    int cnt=lib_cnt(d,n); if(i<0||i>=cnt) return 0; uint32_t off=rd4le(d+i*4); if(!off||off>=(uint32_t)n) return 0;
    uint32_t end=(uint32_t)n; for(int j=i+1;j<cnt;j++){ uint32_t o=rd4le(d+j*4); if(o){ end=o; break; } }
    *len=(int32_t)(end-off); return d+off; }

static uint8_t script[65536];
static cvm_game_t g;
static uint8_t before[256];
static void report(const char *when){
    for(int i=0;i<256;i++) if(g.worktype[i]!=before[i]){ printf("  WORKTYPE npc=%d -> 0x%02x (%s)\n",i,g.worktype[i],when); before[i]=g.worktype[i]; }
}
int main(int argc, char **argv)
{
    if(argc<3){ printf("usage: wtrun <refdata dir> <npc> [keyword ...] [-v]\n"); return 2; }
    const char *dir=argv[1]; int npc=atoi(argv[2]); int verbose=0;
    char pa[512], pb[512]; snprintf(pa,sizeof pa,"%s/CONVERSE.A",dir); snprintf(pb,sizeof pb,"%s/CONVERSE.B",dir);
    long na,nb; uint8_t *A=slurp(pa,&na), *B=slurp(pb,&nb); if(!A||!B){ printf("no CONVERSE.A/.B under %s\n",dir); return 2; }
    int ca=lib_cnt(A,na); int32_t elen; const uint8_t *e=(npc<ca)?lib_ent(A,na,npc,&elen):lib_ent(B,nb,npc-ca,&elen);
    if(!e){ printf("npc %d: no script\n",npc); return 1; }
    int32_t len=u6lzw_decompress(e,elen,script,sizeof script); if(len<=0){ printf("npc %d: bad script\n",npc); return 1; }
    cvm_game_init(&g,1);
    memcpy(before,g.worktype,256);
    cvm_t vm; cvm_init(&vm,script,len,&g,npc,"Avatar","milord");
    cvm_status st=cvm_run(&vm);
    printf("npc %d (%s): greeting %s\n",npc,vm.npcname,st==CVM_ASK?"asks":"done");
    if(verbose) printf("%s\n",cvm_text(&vm));
    report("greeting");
    for(int a=3;a<argc;a++){
        if(!strcmp(argv[a],"-v")){ verbose=1; continue; }
        if(st!=CVM_ASK){ printf("  (conversation over before '%s')\n",argv[a]); break; }
        cvm_input(&vm,argv[a]); st=cvm_run(&vm);
        printf("> %s  [%s]\n",argv[a],st==CVM_ASK?"asks":"done");
        if(verbose) printf("%s\n",cvm_text(&vm));
        report(argv[a]);
    }
    free(A); free(B); return 0;
}
