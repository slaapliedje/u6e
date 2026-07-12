/* nuviewrite.c — host: WRITE a ScummVM/Nuvie U6 save (see docs/formats/nuvie_save.md).
 *
 * Proves our save-WRITE path. We now RE-SERIALIZE the two regions we model from
 * fully-decoded values, and preserve everything else byte-for-byte:
 *   - the 64 surface super-chunks: every 8-byte object record is decoded
 *     (status/x/y/z/obj_n/frame/qty/quality) and re-encoded little-endian, in the
 *     same chunk and order (chunk membership is part of the model: most map objects
 *     follow chunk=(y>>7)*8+(x>>7), but contained objects follow their container
 *     and some relocated objects are parked off-grid — so order/membership is kept).
 *   - the objlist actor arrays (position/appearance/status/str/dex/int/HP).
 * Encoding is the exact inverse of the decode, so a plain round-trip reproduces the
 * input file byte-for-byte — the strongest validation short of ScummVM. Optional
 * --move-avatar patches only the avatar's packed position bytes.
 *
 * Usage: nuviewrite <in.000> <out.000> [--move-avatar X Y]
 * All multi-byte values are little-endian (ScummVM/DOS lineage).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint16_t rd2(const uint8_t*p){ return (uint16_t)(p[0]|(p[1]<<8)); }
static uint32_t rd4(const uint8_t*p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void wr2(uint8_t*p,unsigned v){ p[0]=v&0xff; p[1]=(v>>8)&0xff; }

/* advance past one super-chunk ([u16 count][count*8]); returns object count (<0 overrun) */
static long skip_chunk(const uint8_t*d,long n,long*pos){
    if(*pos+2>n){ *pos=n; return -1; }
    uint16_t c=rd2(d+*pos); long need=2+(long)c*8;
    if(*pos+need>n){ *pos=n; return -1; }
    *pos+=need; return c;
}

/* objlist per-actor array offsets (see docs/formats/objlist.md), LE */
#define O_POS 0x100
#define O_APPR 0x400
#define O_STAT 0x800
#define O_STR 0x900
#define O_DEX 0xa00
#define O_INT 0xb00
#define O_HP  0xe00
#define NACT  256

/* --set-obj-at: replace the first surface object at (so_x,so_y,z=0) with obj_n
 * so_objn (frame 0). Used to inject a test object (e.g. a gem) into a scene for
 * observing colour cycling. -1 = disabled. */
static int so_x=-1, so_y=-1, so_objn=-1, so_frame=0, so_done=0;

/* Re-serialize the 64 surface super-chunks [start,end) in `out` by decoding each
 * 8-byte record from `in` and re-encoding it (same chunk, same order). Returns the
 * number of objects processed, or -1 on a layout error. */
static long reserialize_surface(const uint8_t*in,uint8_t*out,long start,long end){
    long ip=start, op=start, objs=0;
    for(int chunk=0;chunk<64;chunk++){
        if(ip+2>end) return -1;
        uint16_t cnt=rd2(in+ip); ip+=2;
        if(ip+(long)cnt*8>end) return -1;
        wr2(out+op,cnt); op+=2;
        for(int i=0;i<cnt;i++,objs++){
            const uint8_t*r=in+ip; uint8_t*w=out+op; ip+=8; op+=8;
            uint8_t status=r[0];                         /* full byte (location + flags) */
            unsigned x=r[1]|((r[2]&3)<<8);
            unsigned y=((r[2]&0xfc)>>2)|((r[3]&0xf)<<6);
            unsigned z=(r[3]&0xf0)>>4;
            unsigned obj_n=r[4]|((r[5]&3)<<8);
            unsigned frame=(r[5]&0xfc)>>2;
            uint8_t qty=r[6], quality=r[7];
            if(so_objn>=0 && !so_done && z==0 && (int)x==so_x && (int)y==so_y){
                obj_n=(unsigned)so_objn; frame=(unsigned)so_frame; status&=~3; /* on map */ so_done=1;
            }
            w[0]=status;
            w[1]=x&0xff; w[2]=((x>>8)&3)|((y&0x3f)<<2); w[3]=((y>>6)&0xf)|((z&0xf)<<4);
            w[4]=obj_n&0xff; w[5]=((obj_n>>8)&3)|((frame&0x3f)<<2);
            w[6]=qty; w[7]=quality;
        }
    }
    return (op==end)?objs:-1;
}

int main(int argc,char**argv){
    if(argc<3){ printf("usage: %s <in.000> <out.000> [--move-avatar X Y]\n",argv[0]); return 2; }
    int mv_x=-1,mv_y=-1;
    for(int i=3;i<argc;i++){
        if(!strcmp(argv[i],"--move-avatar")&&i+2<argc){ mv_x=atoi(argv[i+1]); mv_y=atoi(argv[i+2]); i+=2; }
        else if(!strcmp(argv[i],"--set-obj-at")&&i+3<argc){ so_x=atoi(argv[i+1]); so_y=atoi(argv[i+2]); so_objn=atoi(argv[i+3]); i+=3;
            if(i+1<argc && argv[i+1][0]>='0' && argv[i+1][0]<='9'){ so_frame=atoi(argv[i+1]); i+=1; } }
    }

    FILE*f=fopen(argv[1],"rb"); if(!f){ printf("cannot open %s\n",argv[1]); return 2; }
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*in=malloc(n); if(fread(in,1,n,f)!=(size_t)n){ return 2; } fclose(f);

    /* locate regions */
    long pos=4;
    skip_chunk(in,n,&pos); skip_chunk(in,n,&pos);          /* inventories + eggs */
    long surf_start=pos;
    for(int i=0;i<64;i++) if(skip_chunk(in,n,&pos)<0){ printf("surface chunk %d overran\n",i); return 2; }
    long surf_end=pos;
    for(int i=0;i<5;i++) skip_chunk(in,n,&pos);            /* dungeon levels */
    if(pos+4>n){ printf("no objlist\n"); return 2; }
    uint32_t objlist_size=rd4(in+pos); pos+=4;
    long op=pos;
    if(op+0x600>n){ printf("objlist too small\n"); return 2; }
    printf("in=%ld bytes; surface[%ld..%ld]=%ld objs; objlist @%ld size=%u\n",
           n, surf_start, surf_end, (surf_end-surf_start-64*2)/8, op, objlist_size);

    uint8_t*out=malloc(n); memcpy(out,in,n);

    /* (1) re-serialize the surface super-chunks from decoded records */
    memset(out+surf_start, 0, surf_end-surf_start);
    long sobjs=reserialize_surface(in,out,surf_start,surf_end);
    if(sobjs<0){ printf("surface re-serialize failed\n"); return 2; }

    /* (2) re-serialize the objlist actor arrays we model */
    uint8_t*ol=out+op; const uint8_t*il=in+op;
    memset(ol+O_POS,0,NACT*3); memset(ol+O_APPR,0,NACT*2);
    memset(ol+O_STAT,0,NACT); memset(ol+O_STR,0,NACT);
    memset(ol+O_DEX,0,NACT); memset(ol+O_INT,0,NACT); memset(ol+O_HP,0,NACT);
    for(int i=0;i<NACT;i++){
        const uint8_t*p=il+O_POS+i*3;
        unsigned x=p[0]|((p[1]&3)<<8), y=((p[1]&0xfc)>>2)|((p[2]&0xf)<<6), z=(p[2]&0xf0)>>4;
        const uint8_t*q=il+O_APPR+i*2;
        unsigned obj_n=q[0]|((q[1]&3)<<8), frame=(q[1]&0xfc)>>2;
        if(i==1 && mv_x>=0){ x=(unsigned)mv_x; y=(unsigned)mv_y; }   /* --move-avatar */
        uint8_t*wp=ol+O_POS+i*3;
        wp[0]=x&0xff; wp[1]=((x>>8)&3)|((y&0x3f)<<2); wp[2]=((y>>6)&0xf)|((z&0xf)<<4);
        uint8_t*wq=ol+O_APPR+i*2;
        wq[0]=obj_n&0xff; wq[1]=((obj_n>>8)&3)|((frame&0x3f)<<2);
        ol[O_STAT+i]=il[O_STAT+i]; ol[O_STR+i]=il[O_STR+i];
        ol[O_DEX+i]=il[O_DEX+i]; ol[O_INT+i]=il[O_INT+i]; ol[O_HP+i]=il[O_HP+i];
    }

    long diffs=0, first=-1;
    for(long k=0;k<n;k++) if(in[k]!=out[k]){ if(first<0) first=k; diffs++; }
    if(mv_x<0) printf("round-trip (surface %ld objs + objlist rebuilt): %ld byte(s) differ (expect 0)\n", sobjs, diffs);
    else printf("move-avatar(%d,%d): %ld byte(s) differ (<=3, @objlist+0x%lx)\n",
                mv_x,mv_y, diffs, first>=0?(unsigned long)(first-op):0);

    FILE*o=fopen(argv[2],"wb"); if(!o){ printf("cannot write %s\n",argv[2]); return 2; }
    fwrite(out,1,n,o); fclose(o);
    printf("wrote %s (%ld bytes)\n", argv[2], n);
    return 0;
}
