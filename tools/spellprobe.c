/* spellprobe.c — host diagnostic for save-backed spellbook/mana (clean-room).
 * Dumps the actor-inventory chunk records for the spellbook (57), spell objects
 * (58) and reagents (65-72), decoding owner/frame/qty/quality per objlist.md §3b,
 * so we can see how KNOWN SPELLS are recorded. Also scans the objlist for a mana
 * field next to the party stats.
 * Usage: spellprobe <inv-chunk> <objlist> */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int main(int argc,char**argv){
    if(argc<3){ printf("usage: %s <inv-chunk> <objlist>\n",argv[0]); return 2; }
    FILE*f=fopen(argv[1],"rb"); if(!f){ printf("cannot open %s\n",argv[1]); return 2; }
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*d=malloc(n); if(fread(d,1,n,f)!=(size_t)n) return 2; fclose(f);
    int cnt=d[0]|(d[1]<<8);
    printf("inventory chunk: %ld bytes, %d records\n\n", n, cnt);

    printf("=== spellbook(57) / spell(58) / reagent(65-72) records ===\n");
    printf("  loc: 08=in-container 10=carried 18=readied\n");
    for(int i=0;i<cnt;i++){
        const uint8_t*b=d+2+i*8;
        int status=b[0], loc=status&0x18, owner=b[1]|((b[2]&0x03)<<8);
        int obj_n=b[4]|((b[5]&0x03)<<8), frame=b[5]>>2, qty=b[6], qual=b[7];
        if(obj_n==57||obj_n==58||(obj_n>=65&&obj_n<=72))
            printf("  rec %3d: obj_n %3d  owner %3d  loc 0x%02x  frame %2d  qty %3d  quality %3d\n",
                   i, obj_n, owner, loc, frame, qty, qual);
    }
    /* count spell(58) records per owner + show frame/quality spread (the spell number) */
    printf("\n=== spell(58) numbers (frame or quality = spell id?) ===\n");
    int nspell=0;
    for(int i=0;i<cnt;i++){ const uint8_t*b=d+2+i*8; int obj_n=b[4]|((b[5]&0x03)<<8);
        if(obj_n==58){ int frame=b[5]>>2, qual=b[7], owner=b[1]|((b[2]&0x03)<<8);
            printf("  spell rec: owner %d frame %d quality %d  (circle by/16: f=%d q=%d)\n",
                   owner,frame,qual, frame/16+1, qual/16+1); nspell++; } }
    printf("  total spell(58) records: %d\n", nspell);

    /* objlist mana hunt: per-actor 1-byte arrays for actors 1..4 (Avatar,Dupre,Shamino,Iolo) */
    FILE*g=fopen(argv[2],"rb"); if(g){
        fseek(g,0,SEEK_END); long on=ftell(g); fseek(g,0,SEEK_SET);
        uint8_t*o=malloc(on); if(fread(o,1,on,g)!=(size_t)on) return 2; fclose(g);
        printf("\n=== objlist per-actor bytes, a1..a4 (int = 15,17,18,17) — mana <= int? ===\n");
        for(long base=0x600;base+8<=on && base<0x1a00;base+=0x100){
            if(base==0xd00) continue;                     /* mid 2-byte exp array */
            printf("0x%04lx:", base);
            for(int a=1;a<=4;a++) printf(" %3u", o[base+a]);
            const char*lbl=base==0x900?" str":base==0xa00?" dex":base==0xb00?" int":base==0xe00?" hp":"";
            printf("%s\n", lbl);
        }
        free(o);
    }
    return 0;
}
