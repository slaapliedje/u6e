/* npcsched.c — HOST: decode NPC actors (OBJLIST) + daily schedules (SCHEDULE),
 * place every NPC at its scheduled location for a given game hour, report a few
 * named NPCs' routines (names cross-referenced from CONVERSE.A), and render a
 * town (the densest NPC cluster) with the NPCs drawn at their positions.
 *
 * Usage: npcsched <SCHEDULE> <OBJLIST> <BASETILE> <U6PAL> <CONVERSE.A>
 *                 <world.pack> <out.png> [hour]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "u6actor.h"
#include "u6lzw.h"
#include "u6conv.h"
#include "u6pal.h"
#include "u6obj.h"
#include "png.h"
#include "world.h"

static uint8_t *slurp(const char*p,long*n)
{ FILE*f=fopen(p,"rb"); if(!f){printf("cannot read %s\n",p);return NULL;}
  fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);
  uint8_t*b=malloc((size_t)*n); if(fread(b,1,(size_t)*n,f)!=(size_t)*n){printf("short read %s\n",p);} fclose(f); return b; }

/* CONVERSE.A name for actor `npc` (conversation index == actor number) */
static void npc_name(const uint8_t*cf,long cn,int npc,char*out,int cap)
{
    out[0]=0; if(!cf) return;
    #define OFF(j) (long)(cf[(j)*4]|cf[(j)*4+1]<<8|cf[(j)*4+2]<<16|((uint32_t)cf[(j)*4+3]<<24))
    int i=0; while(i*4+4<=cn && OFF(i)==0) i++;
    long count=OFF(i)/4; if(npc<0||npc>=count) return;
    long s=OFF(npc); if(!s) return;
    long e=cn; for(long j=npc+1;j<count;j++){ long o=OFF(j); if(o){e=o;break;} }
    #undef OFF
    int32_t usz=u6lzw_unpacked_size(cf+s,(int32_t)(e-s)); int32_t cap2=usz>0?usz:(int32_t)(e-s);
    uint8_t*d=malloc((size_t)cap2+4); int32_t got=u6lzw_decompress(cf+s,(int32_t)(e-s),d,cap2+4);
    if(got>0){ u6conv_t c; if(u6conv_parse(&c,d,got,"Avatar")) snprintf(out,cap,"%s",c.name); }
    free(d);
}

int main(int argc,char**argv)
{
    if(argc<8){ printf("usage: npcsched SCHEDULE OBJLIST BASETILE U6PAL CONVERSE.A world.pack out.png [hour]\n"); return 2; }
    int hour = argc>8 ? atoi(argv[8]) : 12;
    long sn,on,bn,pn,cn,wn;
    uint8_t *sch=slurp(argv[1],&sn), *obj=slurp(argv[2],&on), *bt=slurp(argv[3],&bn);
    uint8_t *palb=slurp(argv[4],&pn), *cf=slurp(argv[5],&cn), *pak=slurp(argv[6],&wn);
    if(!sch||!obj||!bt||!palb||!pak){ printf("missing input\n"); return 2; }

    static u6actor_t act[U6_MAX_ACTORS];
    if(!u6actor_load_objlist(act,obj,(int32_t)on)){ printf("OBJLIST decode failed\n"); return 2; }
    if(!u6actor_load_schedule(act,sch,(int32_t)sn)){ printf("SCHEDULE decode failed\n"); return 2; }

    /* world pack (U6W3+) -> map/chunks/tileset/palette/passable for terrain+tiles */
    if(memcmp(pak,"U6W",3)!=0){ printf("not a world pack\n"); return 2; }
    uint32_t map_len=((uint32_t)pak[4]<<24)|(pak[5]<<16)|(pak[6]<<8)|pak[7];
    uint32_t chk_len=((uint32_t)pak[8]<<24)|(pak[9]<<16)|(pak[10]<<8)|pak[11];
    uint32_t ntiles =((uint32_t)pak[12]<<24)|(pak[13]<<16)|(pak[14]<<8)|pak[15];
    const uint8_t*map=pak+24,*chunks=map+map_len,*tileset=chunks+chk_len;
    const uint8_t*palette=tileset+(int32_t)ntiles*256,*passable=palette+768;
    u6pal_t pal; u6pal_parse(&pal,palb,(int32_t)pn);
    GameDef def={"u6",0,0,0}; world_t w;
    if(!world_init(&w,map,(int32_t)map_len,chunks,(int32_t)chk_len,passable,&def,1)){ printf("world_init failed\n"); return 2; }

    /* place every NPC at its scheduled position for `hour`; tally placements */
    static int16_t px[U6_MAX_ACTORS], py[U6_MAX_ACTORS]; static int placed[U6_MAX_ACTORS];
    int nplaced=0;
    for(int i=0;i<U6_MAX_ACTORS;i++){
        placed[i]=0; if(act[i].obj_n==0) continue;
        int si=u6sched_active(&act[i],hour);
        if(si>=0){ u6sched_entry_t e; u6sched_decode(act[i].sched+si*5,&e);
            if(e.z==0){ px[i]=e.x; py[i]=e.y; placed[i]=1; nplaced++; } }
    }
    printf("decoded %d actors; %d placed on the surface at hour %d\n", U6_MAX_ACTORS, nplaced, hour);

    /* report a few NPCs' schedules (with names) */
    int who[]={2,3,4,5,16,56,62};
    for(unsigned k=0;k<sizeof who/sizeof*who;k++){ int i=who[k];
        char nm[32]; npc_name(cf,cn,i,nm,sizeof nm);
        printf("\nNPC %d (%s): %d schedule entries\n", i, nm[0]?nm:"?", act[i].nsched);
        for(int j=0;j<act[i].nsched && j<8;j++){ u6sched_entry_t e; u6sched_decode(act[i].sched+j*5,&e);
            printf("  %02d:00  worktype 0x%02x  @ (%d,%d,%d)\n", e.hour, e.worktype, e.x, e.y, e.z); }
        int si=u6sched_active(&act[i],hour);
        if(si>=0){ u6sched_entry_t e; u6sched_decode(act[i].sched+si*5,&e);
            printf("  -> at %02d:00 stands at (%d,%d)\n", hour, e.x, e.y); }
    }

    /* find the densest 24x24 NPC cluster (a town) to centre the render on */
    int bestcx=512,bestcy=512,bestn=-1;
    for(int i=0;i<U6_MAX_ACTORS;i++){ if(!placed[i]) continue;
        int n=0; for(int j=0;j<U6_MAX_ACTORS;j++){ if(!placed[j]) continue;
            int dx=px[j]-px[i], dy=py[j]-py[i]; if(dx>-5&&dx<6&&dy>-5&&dy<6) n++; }  /* within an 11x11 window */
        if(n>bestn){ bestn=n; bestcx=px[i]; bestcy=py[i]; } }
    printf("\nbusiest spot at hour %d: (%d,%d) with %d NPCs nearby\n", hour, bestcx, bestcy, bestn);

    /* render a 24x24-tile region around the cluster: terrain + NPC tiles */
    enum { VT=24, PXW=VT*16 };
    int left=bestcx-VT/2, top=bestcy-VT/2;
    static uint8_t bb[PXW*PXW];
    for(int vy=0;vy<VT;vy++)for(int vx=0;vx<VT;vx++){
        int tile=world_tile(&w,left+vx,top+vy);
        const uint8_t*src=tileset+(int32_t)tile*256;
        for(int r=0;r<16;r++)for(int c=0;c<16;c++) bb[(vy*16+r)*PXW+vx*16+c]=src[r*16+c]; }
    for(int i=0;i<U6_MAX_ACTORS;i++){ if(!placed[i]) continue;
        int vx=px[i]-left, vy=py[i]-top; if(vx<0||vx>=VT||vy<0||vy>=VT) continue;
        int tile=u6_basetile(bt,act[i].obj_n)+act[i].frame; if(tile<0||tile>=(int)ntiles) continue;
        const uint8_t*src=tileset+(int32_t)tile*256;
        for(int r=0;r<16;r++)for(int c=0;c<16;c++){ uint8_t v=src[r*16+c]; if(v!=255) bb[(vy*16+r)*PXW+vx*16+c]=v; } }

    uint8_t*img=malloc((size_t)PXW*PXW*3);
    for(int i=0;i<PXW*PXW;i++){u6_rgb c=pal.color[bb[i]];img[i*3]=c.r;img[i*3+1]=c.g;img[i*3+2]=c.b;}
    int rc=png_write_rgb(argv[7],PXW,PXW,img);
    printf("wrote %s (%dx%d) centred (%d,%d)\n",argv[7],PXW,PXW,left,top);
    return rc;
}
