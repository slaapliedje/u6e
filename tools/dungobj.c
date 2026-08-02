// SPDX-License-Identifier: GPL-2.0-or-later
/* dungobj.c — host-side M9 probe: derive U6 level-transition rules from the user's own object data.
 * Loads surface (LZOBJBLK, z0) + dungeon (LZDNGBLK, z1..5) objects, collects ladders (305/106) and
 * holes (308), and tests VERTICAL ALIGNMENT: does a transition at (x,y,z) have one at (x,y,z+/-1)?
 * Correlates the ladder FRAME with up/down. Feeds docs/rules/map_levels.md. Ships nothing.
 * Usage: dungobj <refdata/dos/u6> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "u6lzw.h"
#include "u6obj.h"

#define MAXOBJ 65536
static uint8_t *slurp(const char *p, long *n){ FILE*f=fopen(p,"rb"); if(!f)return NULL;
    fseek(f,0,SEEK_END); *n=ftell(f); fseek(f,0,SEEK_SET); uint8_t*b=malloc(*n>0?*n:1);
    if(b&&fread(b,1,*n,f)!=(size_t)*n){free(b);b=NULL;} fclose(f); return b; }

typedef struct { uint16_t x,y; uint8_t z,frame; uint16_t obj_n; } tr_t;
static tr_t g_tr[MAXOBJ]; static int g_ntr=0;

static void load(const char*dir,const char*file){
    char path[512]; snprintf(path,sizeof path,"%s/%s",dir,file);
    long n=0; uint8_t*lb=slurp(path,&n); if(!lb){ fprintf(stderr,"no %s\n",file); return; }
    int32_t cap=u6lzw_unpacked_size(lb,(int32_t)n); if(cap<=0||cap>8*1024*1024) cap=8*1024*1024;
    uint8_t*ob=malloc((size_t)cap); int32_t got=u6lzw_decompress(lb,(int32_t)n,ob,cap);
    if(got<=0){ fprintf(stderr,"%s decompress failed\n",file); return; }
    static u6_object_t objs[MAXOBJ]; int32_t no=u6obj_decode(ob,got,objs,MAXOBJ);
    for(int32_t i=0;i<no;i++){ u6_object_t*o=&objs[i];
        if(o->obj_n==305||o->obj_n==106||o->obj_n==308){
            g_tr[g_ntr].x=o->x; g_tr[g_ntr].y=o->y; g_tr[g_ntr].z=o->z;
            g_tr[g_ntr].frame=o->frame; g_tr[g_ntr].obj_n=o->obj_n; g_ntr++; } }
    printf("  %s: decoded %d objs\n",file,(int)no);
}
static int at(int x,int y,int z){ for(int i=0;i<g_ntr;i++) if(g_tr[i].x==x&&g_tr[i].y==y&&g_tr[i].z==z) return i; return -1; }

int main(int argc,char**argv){
    const char*dir = argc>1?argv[1]:"refdata/dos/u6";
    load(dir,"LZOBJBLK"); load(dir,"LZDNGBLK");
    printf("total transition objects: %d\n",g_ntr);

    /* ALIGNMENT + FRAME correlation: for each ladder, is there a transition directly below (z+1) or
     * above (z-1) at the same (x,y)? Tally by (obj_n,frame). */
    int fr0_below=0,fr0_above=0,fr0_n=0, fr1_below=0,fr1_above=0,fr1_n=0, hole_below=0,hole_n=0;
    int lad_any=0, lad_aligned=0;
    for(int i=0;i<g_ntr;i++){ tr_t*t=&g_tr[i];
        int below = (t->z<5) && at(t->x,t->y,t->z+1)>=0;
        int above = (t->z>0) && at(t->x,t->y,t->z-1)>=0;
        if(t->obj_n==308){ hole_n++; if(below) hole_below++; continue; }
        lad_any++; if(below||above) lad_aligned++;
        if(t->frame==0){ fr0_n++; fr0_below+=below; fr0_above+=above; }
        else           { fr1_n++; fr1_below+=below; fr1_above+=above; }
    }
    printf("\nladders: %d total, %d have a transition directly above OR below (aligned)\n",lad_any,lad_aligned);
    printf("  frame 0 (n=%d): %d connect DOWN(z+1), %d connect UP(z-1)\n",fr0_n,fr0_below,fr0_above);
    printf("  frame 1 (n=%d): %d connect DOWN(z+1), %d connect UP(z-1)\n",fr1_n,fr1_below,fr1_above);
    printf("holes(308): %d total, %d have a transition directly below(z+1)\n",hole_n,hole_below);

    /* show a couple of aligned stacks as concrete evidence */
    printf("\nsample aligned columns (x,y : z/frame per level):\n");
    int shown=0; for(int i=0;i<g_ntr && shown<6;i++){ tr_t*t=&g_tr[i];
        if(t->obj_n==308) continue; if(!(t->z<5 && at(t->x,t->y,t->z+1)>=0)) continue;  /* has one below */
        printf("  (%3d,%3d):",t->x,t->y);
        for(int z=0;z<=5;z++){ int j=at(t->x,t->y,z);
            if(j>=0) printf(" z%d=%s/f%d",z,g_tr[j].obj_n==308?"hole":"lad",g_tr[j].frame); }
        printf("\n"); shown++; }
    return 0;
}
