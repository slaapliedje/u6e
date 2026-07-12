// SPDX-License-Identifier: GPL-2.0-or-later
/* convvm.c — HOST: drive a real NPC conversation through the full usecode VM
 * (engine/converse_vm). Decompresses NPC N's script from CONVERSE.A and runs it,
 * feeding a list of keywords (default: name, job, bye) and printing each
 * exchange. Validates the VM end-to-end (greeting, keyword loop, branching).
 *
 * Usage: convvm <CONVERSE.A> <npc> [kw1 kw2 ...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "u6lzw.h"
#include "converse_vm.h"

static uint8_t *slurp(const char*p,long*n)
{ FILE*f=fopen(p,"rb"); if(!f){printf("cannot read %s\n",p);exit(2);}
  fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);
  uint8_t*b=malloc((size_t)*n); if(fread(b,1,(size_t)*n,f)!=(size_t)*n){printf("short read\n");exit(2);}
  fclose(f); return b; }

static long off(const uint8_t*f,int i)
{ return (long)(f[i*4]|f[i*4+1]<<8|f[i*4+2]<<16|((uint32_t)f[i*4+3]<<24)); }

static uint8_t *load_conv(const uint8_t*f,long flen,int npc,int32_t*len)
{ int i=0; while(off(f,i)==0)i++; long count=off(f,i)/4;
  if(npc<0||npc>=count) return NULL;
  long s=off(f,npc); if(!s) return NULL;
  long e=flen; for(int j=npc+1;j<count;j++){ long o=off(f,j); if(o){e=o;break;} }
  int32_t usz=u6lzw_unpacked_size(f+s,(int32_t)(e-s));
  int32_t cap=(usz>0)?usz:(int32_t)(e-s);
  uint8_t*d=malloc((size_t)cap+4);
  int32_t got=u6lzw_decompress(f+s,(int32_t)(e-s),d,cap+4);
  if(got<=0){free(d);return NULL;} *len=got; return d; }

int main(int argc,char**argv)
{
    if(argc<3){ printf("usage: convvm <CONVERSE.A> <npc> [kw...]\n"); return 2; }
    const char*path=argv[1]; int npc=atoi(argv[2]);
    static const char*defkw[]={"name","job","bye"};
    const char**kw = (argc>3)?(const char**)&argv[3]:defkw;
    int nkw = (argc>3)?(argc-3):3;

    long flen; uint8_t*f=slurp(path,&flen);
    int32_t len; uint8_t*d=load_conv(f,flen,npc,&len);
    if(!d){ printf("NPC %d: no conversation\n",npc); return 2; }

    cvm_game_t g; cvm_game_init(&g,0xC0FFEE);
    cvm_t vm; cvm_init(&vm,d,len,&g,npc,"Avatar","milord");

    int turn=0;
    for(;;){
        cvm_status st=cvm_run(&vm);
        printf("%s",cvm_text(&vm));
        if(st==CVM_DONE){ printf("\n[conversation ended]\n"); break; }
        if(st==CVM_ERROR){ printf("\n[VM error]\n"); break; }
        /* st==CVM_ASK: feed the next scripted keyword */
        if(turn>=nkw){ printf("\n[no more keywords -> bye]\n"); cvm_input(&vm,"bye"); }
        else { printf("\n>> %s\n", kw[turn]); cvm_input(&vm,kw[turn]); turn++; }
        if(turn>200){ printf("[loop guard]\n"); break; }
    }

    /* rough opcode presence (byte scan) to spot DPRINT/DATA users */
    { int ndp=0,nda=0,nio=0,nis=0; for(int32_t k=0;k<len;k++){ if(d[k]==0xb5)ndp++; if(d[k]==0xb4)nda++; if(d[k]==0xb7)nio++; if(d[k]==0xf9)nis++; }
      printf("\n[opcodes] DPRINT~%d DATA~%d INDEXOF~%d INPUTSTR~%d", ndp, nda, nio, nis); }
    /* show any game-state mutations the conversation made */
    printf("\n[state] mutations=%d  karma=%d  gold=%d", g.mutations, g.karma, g.gold);
    if(g.portrait_npc>=0) printf("  portrait->%d", g.portrait_npc);
    printf("\n  party:"); for(int i=0;i<256;i++) if(g.in_party[i]) printf(" %d",i);
    printf("\n  flags:"); for(int i=0;i<256;i++) if(g.talk_flags[i]) printf(" npc%d=0x%02x",i,g.talk_flags[i]);
    printf("\n  inv:");   for(int i=0;i<256;i++) if(g.player_obj[i]) printf(" obj%d x%d",i,g.player_obj[i]);
    printf("\n");
    free(d); free(f);
    return 0;
}
