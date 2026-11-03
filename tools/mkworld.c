// SPDX-License-Identifier: GPL-2.0-or-later
/* mkworld.c — host: build the world pack the engine loads at runtime (M5/M6).
 *
 * Pack "U6W3" (big-endian header; bulk arrays are byte data):
 *   0   "U6W3"
 *   4   u32 map_len            (raw MAP)
 *   8   u32 chunks_len         (raw CHUNKS)
 *   12  u32 ntiles             (2048: full combined tileset, 256 bytes each)
 *   16  u32 nobjs              (world objects, 14 bytes each: u16 x,y,tile,obj_n,qty16,quality,parent)
 *   20  u16 startX
 *   22  u16 startY
 *   24  MAP | CHUNKS | tileset(ntiles*256) | palette(768) | passable(256)
 *       | objects(nobjs * {u16 x, y, tile, obj_n, qty16, parent})  (all big-endian;
 *         parent = index of the containing object or 0xFFFF; contained x/y = 0)
 *       | font(2048: U6.CH, 256 glyphs x 8 bytes, 8x8 MSB-left, index=ASCII)
 *       | paper(64000: PAPER.BMP decoded, 320x200 8-bit indices)
 *       | u16 nport | portraits(nport * 56*64 8-bit indices)
 *       | u16 demo_npc | u16 script_len | script[script_len]
 *       | u16 nact | nact x u16 actor-tile | nact x {u16 x,u16 y,u8 z}
 *       | u16 sched_len | SCHEDULE[sched_len]
 *       | u32 look_len | look[look_len] | gettable[1024]
 *       | u8 cur_w | u8 cur_h | u8 cur_hotx | u8 cur_hoty | cursor[cur_w*cur_h]
 *       | basetile[2048]  (1024 x u16 LE: obj_n -> base tile)
 * Pack "U6WA" (=W10) adds BASETILE so the engine can recover an object's frame
 * (tile - base) for frame-aware USE (doors open/close) + future animation
 * (docs/rules/usecode_use.md). (U6W9 added the mouse cursor; U6W8 object identity
 * + LOOK names + gettable; U6W7 the NPC actor table + SCHEDULE.)
 * The font/paper/portraits are original U6 assets (loaded at runtime — never
 * committed). English glyphs 0-127, runic/gargoyle 128-255.
 * The ST runs u6map on MAP+CHUNKS, blits terrain from tileset[tile*256], and
 * draws each surface object's pre-resolved tile (transparent index 255).
 * Usage: mkworld <refdata/dos/u6> <out.world> [startX startY]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "quant.h"
#include "cvasm.h"
#include "cvhook.h"   /* progress marks for CONVERT.TOS; no-ops in the host build */
#include "mul16.h"
#include "u6lzw.h"
#include "u6lzc.h"
#include "u6lib.h"
#include "u6pal.h"
#include "u6tile.h"
#include "u6obj.h"
#include "u6shape.h"
#include "fepack.h"
#include "u6map.h"
#include "combat.h"
#include "passability.h"

#define NPORT 16            /* sample portraits shipped for the on-target demo */
#define DEMO_NPC 2          /* NPC whose conversation the on-target VM runs (Dupre) */

#define NTILES 2048
#define MAXOBJ 16384

static uint8_t *slurp(const char *d, const char *name, long *n)
{
    char p[1024]; snprintf(p, sizeof p, "%s/%s", d, name);
    FILE *f = fopen(p, "rb"); if (!f) { printf("cannot read %s\n", p); exit(2); }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*n); fread(b, 1, (size_t)*n, f); fclose(f); return b;
}
/* the size of <d>/<name>, or -1; and a read of it into a buffer the caller sized, so a file can
 * land at an offset inside a bigger block without a second copy (the Atari-side converter's
 * memory is tight) */
static long fsize_in(const char *d, const char *name)
{
    char p[1024]; snprintf(p, sizeof p, "%s/%s", d, name);
    FILE *f = fopen(p, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f); return n;
}
static void slurp_into(const char *d, const char *name, uint8_t *buf, long n)
{
    char p[1024]; snprintf(p, sizeof p, "%s/%s", d, name);
    FILE *f = fopen(p, "rb"); if (!f) { printf("cannot read %s\n", p); exit(2); }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { printf("short read %s\n", p); exit(2); }
    fclose(f);
}
/* a scratch buffer of n bytes set to `fill` (the front-end scene canvases); out of memory is fatal */
static uint8_t *scratch(size_t n, int fill)
{
    uint8_t *b = malloc(n ? n : 1);
    if (!b) { printf("out of memory (%lu bytes)\n", (unsigned long)n); exit(2); }
    memset(b, fill, n); return b;
}
static void be16(FILE *f, unsigned v){ fputc((v>>8)&0xFF,f); fputc(v&0xFF,f); }
static void be32(FILE *f, unsigned long v){ fputc((v>>24)&0xFF,f); fputc((v>>16)&0xFF,f); fputc((v>>8)&0xFF,f); fputc(v&0xFF,f); }

/* Decode one U6 shape FRAME (a U6MCGA.PTR library item / .shp frame) into a flat
 * width*height 8-bit bitmap (255 = transparent). See docs/formats/cursor.md.
 * Returns malloc'd pixels + size/hotspot; NULL on a malformed frame. */
static uint8_t *decode_shape(const uint8_t *b, int32_t blen,
                             int *ow, int *oh, int *ohx, int *ohy)
{
    if (blen < 8) return NULL;
    #define RD16(p) ((p)[0]|((p)[1]<<8))
    const uint8_t *d=b, *end=b+blen;
    int x1=RD16(d); d+=2; int hotx=RD16(d); d+=2; int width=x1+hotx+1;
    int hoty=RD16(d); d+=2; int y2=RD16(d); d+=2; int height=hoty+y2+1;
    if (width<=0||height<=0||width>1024||height>1024) return NULL;
    uint8_t *raw=malloc((size_t)width*height); memset(raw,255,(size_t)width*height);
    while (d+6<=end){
        int num=RD16(d); if(num==0) break; d+=2;
        int xpos=(int16_t)RD16(d); d+=2; int ypos=(int16_t)RD16(d); d+=2;
        if((hotx+xpos)>=width||(hoty+ypos)>=height||(hotx+xpos)<0||(hoty+ypos)<0) break;
        int n=num>>1; uint8_t *dst=raw+(hotx+xpos)+(hoty+ypos)*width;
        if(!(num&1)){ if(d+n>end) break; memcpy(dst,d,(size_t)n); d+=n; }   /* raw run */
        else { int j=0; while(j<n && d<end){                                /* encoded */
            unsigned s=*d++; int m=s>>1;
            if(s&1){ if(d>=end)break; memset(dst+j,*d++,(size_t)m); }
            else { if(d+m>end)break; memcpy(dst+j,d,(size_t)m); d+=m; }
            j+=m; } }
    }
    #undef RD16
    *ow=width; *oh=height; *ohx=hotx; *ohy=hoty; return raw;
}

/* decompress NPC `npc`'s full conversation script from CONVERSE.A (4-byte
 * offset lib, each entry LZW). Returns malloc'd bytes + *len, or NULL. */
static uint8_t *conv_script(const uint8_t *f, long flen, int npc, int32_t *len)
{
    int i=0; while(i*4+4<=flen && (f[i*4]|f[i*4+1]<<8|f[i*4+2]<<16|((uint32_t)f[i*4+3]<<24))==0) i++;
    long count=(f[i*4]|f[i*4+1]<<8|f[i*4+2]<<16|((uint32_t)f[i*4+3]<<24))/4;
    if(npc<0||npc>=count) return NULL;
    #define OFF(j) (long)(f[(j)*4]|f[(j)*4+1]<<8|f[(j)*4+2]<<16|((uint32_t)f[(j)*4+3]<<24))
    long s=OFF(npc); if(!s) return NULL;
    long e=flen; for(long j=npc+1;j<count;j++){ long o=OFF(j); if(o){e=o;break;} }
    #undef OFF
    int32_t usz=u6lzw_unpacked_size(f+s,(int32_t)(e-s));
    int32_t cap=(usz>0)?usz:(int32_t)(e-s);
    uint8_t *d=malloc((size_t)cap+4);
    int32_t got=u6lzw_decompress(f+s,(int32_t)(e-s),d,cap+4);
    if(got<=0){ free(d); return NULL; } *len=got; return d;
}

/* slurp a full path (with an optional fallback path); NULL on failure */
static uint8_t *slurp_path(const char *path, const char *fallback, long *n)
{
    FILE *f = fopen(path, "rb");
    if (!f && fallback) f = fopen(fallback, "rb");
    if (!f) return NULL;
    fseek(f,0,SEEK_END); *n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *b=malloc((size_t)*n); if(fread(b,1,(size_t)*n,f)!=(size_t)*n){fclose(f);free(b);return NULL;}
    fclose(f); return b;
}

/* Decode Nuvie's roof_map_00.dat (sparse RLE of u16-per-tile, little-endian) into a
 * per-tile indoor flag (roof tile != 0 => indoor). Format (Map::loadRoofData): a
 * stream of records [u16 skip][u8 run][run x u16 tile]; ptr advances `skip` tiles
 * then writes `run` tile values. See docs/rules/lighting.md §4. Returns roofed-tile
 * count, or -1 if the file is absent/unreadable (caller falls back to floor-tiles). */
#define WORLDW 1024
#define INDOOR_N (WORLDW*WORLDW/8)        /* the indoor bitmap: 1 bit/tile, bit x&7 of byte idx>>3 */
static long load_roof_indoor(const char *dir, uint8_t *bits /* INDOOR_N */)
{
    long rn; uint8_t *rd = NULL;
    const char *envp = getenv("ROOF_MAP");
    char relp[1200]; snprintf(relp, sizeof relp, "%s/roof_map_00.dat", dir);
    if (envp)  rd = slurp_path(envp, NULL, &rn);
    if (!rd)   rd = slurp_path(relp, "build/roof_map_00.dat", &rn);
    if (!rd || rn < 3) { free(rd); return -1; }
    memset(bits, 0, INDOOR_N);
    long p = 0, idx = 0, nroof = 0;
    while (p + 3 <= rn){
        int skip = rd[p] | (rd[p+1] << 8); p += 2;
        int run  = rd[p++];
        idx += skip;
        for (int i = 0; i < run && p + 2 <= rn; i++){
            int val = rd[p] | (rd[p+1] << 8); p += 2;
            if (idx >= 0 && idx < WORLDW*WORLDW && val){ bits[idx >> 3] |= (uint8_t)(1 << (idx & 7)); nroof++; }
            idx++;
        }
    }
    free(rd);
    return nroof;
}

/* Build the per-tile INDOOR bitmap (U6WI). See docs/rules/lighting.md §4. PRIMARY
 * source = Nuvie's roof_map_00.dat (exact ScummVM match: only the ~2433 roofed tiles
 * — Lord British's castle + one other structure — are indoor; town shops stay lit by
 * day). FALLBACK (file absent) = U6's own manmade FLOOR/CARPET tiles + wall-dilate
 * (self-contained, but over-darkens: it marks every shop interior). Output: 1 bit/
 * tile (131072 B, idx=y*1024+x), byte-RLE'd {u16 count, u8 value} big-endian summing
 * to 131072. Returns malloc'd RLE (*out_len) or NULL.
 * Worked on bit planes (128 KB each), not a byte per tile: the converter also runs on a
 * 4 MB Atari. */
static uint8_t *build_indoor_rle(const char *dir, const uint8_t *mapb, long mn,
                                 const uint8_t *chk, long cn, const uint8_t *tflag,
                                 const u6_object_t *objs, int32_t nobj, const uint8_t *bt,
                                 const uint8_t *lookd, long look_len,
                                 int startx, int starty, long *out_len)
{
    *out_len = 0;
    uint8_t *bits = malloc(INDOOR_N);     /* indoor (the fallback: floor/carpet, then the walls that join) */
    if (!bits) { printf("indoor: OOM\n"); return NULL; }
    const char *src; int32_t nfloor = 0, nwall = 0;
    uint8_t pc[256];
    for (int v = 0; v < 256; v++){ int k = 0; for (int b = 0; b < 8; b++) k += (v >> b) & 1; pc[v] = (uint8_t)k; }

    long nroof = load_roof_indoor(dir, bits);
    if (nroof >= 0){
        src = "roof_map_00.dat (ScummVM)"; nfloor = (int32_t)nroof;
    } else {
        /* fallback: floor/carpet tiles + wall-dilate */
        src = "floor-tile FALLBACK (no roof_map_00.dat)";
        u6map_t m;
        if (!u6map_load(&m, mapb, (int32_t)mn, chk, (int32_t)cn)){ printf("indoor: u6map_load failed\n"); free(bits); return NULL; }
        uint8_t *bar = calloc(INDOOR_N, 1);                  /* barrier (wall) plane */
        if (!bar) { printf("indoor: OOM\n"); free(bits); return NULL; }
        memset(bits, 0, INDOOR_N);
        static uint8_t isfloor[NTILES]; memset(isfloor, 0, sizeof isfloor);
        { int prev = -1; long pos = 0;
          while (pos + 2 <= look_len){
              int ix = lookd[pos] | (lookd[pos+1] << 8); pos += 2; if (ix >= NTILES) break;
              const char *s = (const char *)&lookd[pos]; int sl = (int)strlen(s); pos += sl + 1;
              int fl = (strstr(s, "floor") || strstr(s, "carpet")) ? 1 : 0;
              for (int t = prev+1; t <= ix && t < NTILES; t++) isfloor[t] = (uint8_t)fl;
              prev = ix;
          } }
        #define BND(t) ((t) >= 0 && (t) < NTILES && (tflag[0x800 + (t)] & 0x04))
        #define FLR(t) ((t) >= 0 && (t) < NTILES && isfloor[t])
        /* The surface is 128x128 chunks of 8x8 tiles, so a row of 8 tiles is one of 8 rows of one of
         * 1024 chunks: pack each of those once (barrier byte, floor byte; bit b = tile b), and a map
         * row is a lookup per chunk. Entry 1024 stands for a chunk number off the end: all tile 0. */
        { uint8_t cell[256];
          for (int t = 0; t < 256; t++) cell[t] = (uint8_t)((BND(t) ? 1 : 0) | (FLR(t) ? 2 : 0));
          uint8_t *packed = malloc((U6_CHUNK_COUNT + 1) * 16L);
          if (!packed) { printf("indoor: OOM\n"); free(bar); free(bits); return NULL; }
          for (long c = 0; c <= U6_CHUNK_COUNT; c++) for (int wy = 0; wy < 8; wy++){
              uint8_t f = 0, w = 0;
              for (int b = 0; b < 8; b++){
                  int t = (c < U6_CHUNK_COUNT && (c+1)*64 <= cn) ? m.chunks[c*64 + wy*8 + b] : 0;
                  uint8_t k = cell[t]; w |= (uint8_t)((k & 1) << b); f |= (uint8_t)(((k >> 1) & 1) << b); }
              packed[c*16 + wy*2] = w; packed[c*16 + wy*2 + 1] = f; }
          uint8_t *fp = bits, *bp = bar;
          for (int y = 0; y < WORLDW; y++){
              if (!(y & 63)) cv_tick();
              uint16_t rowc[U6_SURFACE_CHUNKS]; u6map_surface_row_chunks(&m, y, rowc);
              for (int cx = 0; cx < U6_SURFACE_CHUNKS; cx++){
                  unsigned c = rowc[cx]; if (c > U6_CHUNK_COUNT) c = U6_CHUNK_COUNT;
                  const uint8_t *k = packed + ((unsigned long)c << 4) + (y & 7)*2;
                  *bp++ = k[0]; *fp++ = k[1]; }
          }
          free(packed); }
        for (int32_t i = 0; i < nobj; i++){
            if (objs[i].z != 0) continue;
            int x = objs[i].x, y = objs[i].y;
            if (x < 0 || x >= WORLDW || y < 0 || y >= WORLDW) continue;
            int base = u6_basetile(bt, objs[i].obj_n);
            if (FLR(base)) bits[(long)y*(WORLDW/8) + (x >> 3)] |= (uint8_t)(1 << (x & 7));
        }
        /* wall-dilate: a barrier that is not indoor joins the inside when a neighbour (4-way, the
         * map wraps) is indoor and not a barrier. Joining only sets bits where the barrier plane is
         * set, so "indoor and not a barrier" never changes and the planes can be marked in place.
         * Bit b of byte i is x = 8i+b: the west neighbour is bit b-1 (bit 7 of byte i-1 for b=0). */
        #define G(r, i) (bits[(r) + ((i) & (WORLDW/8-1))] & (uint8_t)~bar[(r) + ((i) & (WORLDW/8-1))])
        for (int y = 0; y < WORLDW; y++){
            if (!(y & 63)) cv_tick();
            long cur = (long)y*(WORLDW/8), up = (long)((y-1)&(WORLDW-1))*(WORLDW/8), dn = (long)((y+1)&(WORLDW-1))*(WORLDW/8);
            for (int i = 0; i < WORLDW/8; i++){
                uint8_t lone = bar[cur+i] & (uint8_t)~bits[cur+i];
                if (!lone) continue;
                uint8_t g = G(cur, i);
                uint8_t nb = G(up, i) | G(dn, i) | (uint8_t)(g << 1) | (uint8_t)(G(cur, i-1) >> 7)
                                                 | (uint8_t)(g >> 1) | (uint8_t)(G(cur, i+1) << 7);
                uint8_t add = lone & nb;
                if (add){ bits[cur+i] |= add; nwall += pc[add]; }
            }
        }
        #undef G
        #undef BND
        #undef FLR
        free(bar);
        int32_t nset = 0; for (long i = 0; i < INDOOR_N; i++) nset += pc[bits[i]];
        nfloor = nset - nwall;                               /* floor tiles, before the walls joined */
    }

    /* byte-RLE: count the runs, then fill a buffer of exactly that size */
    int N = INDOOR_N; long rl = 0;
    for (int i = 0; i < N; ){ int v = bits[i], run = (int)cv_run(bits+i, N-i < 65535 ? N-i : 65535, v);
        rl += 3; i += run; }
    uint8_t *rle = malloc((size_t)rl + 16); rl = 0;
    if (!rle) { printf("indoor: OOM\n"); free(bits); return NULL; }
    for (int i = 0; i < N; ){
        int v = bits[i], run = (int)cv_run(bits+i, N-i < 65535 ? N-i : 65535, v);
        rle[rl++] = (uint8_t)((run>>8)&0xFF); rle[rl++] = (uint8_t)(run&0xFF); rle[rl++] = (uint8_t)v;
        i += run;
    }
#ifndef __m68k__                                          /* (the Atari's output is compared with the host's) */
    { int o = 0, ok = 1;                                  /* RLE round-trip self-check */
      for (long r = 0; r+3 <= rl && o < N; r += 3){ int cnt = (rle[r]<<8)|rle[r+1], vv = rle[r+2];
          while (cnt-- > 0 && o < N){ if (bits[o] != vv) ok = 0; o++; } }
      if (!ok || o != N) printf("indoor: RLE self-check FAILED (o=%d/%d)\n", o, N); }
#endif

    printf("indoor: source=%s; %d tiles%s (%.2f%%); RLE %d->%ld bytes\n",
           src, nfloor+nwall, nwall?" (incl dilated walls)":"", 100.0*(nfloor+nwall)/(WORLDW*WORLDW), N, rl);
    { struct { int x,y; const char *l; } ck[] = {
          {startx,starty,"start"},{316,367,"throne"},{313,350,"kitchen"},{303,355,"courtyard?"},
          {0,0,"open"},{500,500,"open"},{800,200,"open"} };
      for (unsigned k = 0; k < sizeof ck/sizeof*ck; k++){
          int i = ck[k].y*WORLDW + ck[k].x;
          printf("   (%d,%d) %-11s : %s\n", ck[k].x, ck[k].y, ck[k].l, ((bits[i >> 3] >> (i & 7)) & 1) ? "INDOOR" : "outdoor"); }
    }
    free(bits);
    *out_len = rl; return rle;
}

/* Darkening LUTs (U6WJ) for the lighting overlay (docs/rules/lighting.md §3). NLEV
 * brightness levels; lut[lev][i] = the palette index nearest to colour i scaled toward
 * black by lev/(NLEV-1) (lev 0 = black, NLEV-1 = identity), the lowest index on a tie.
 * The engine applies darklut[light>>5] to each map cell. Built at conversion time, not at
 * game start: 4096 nearest-colour searches over 256 colours.
 * The search walks the palette in order of red outward from the target's red and stops in
 * each direction once the red difference alone is past the best distance, which visits a
 * fraction of the 256 colours on a 68000 and finds the same entry as a full scan.
 * NLEV must match the engine's (render/gamedemo.c). Palette is RGB6 (u6pal, 0..63). */
#define NLEV 16
static void build_darklut(const uint8_t *pal, uint8_t lut[NLEV][256])
{
    uint32_t sq[511];                       /* squares of -255..255: a table read, not a muls.w (70 cycles) */
    for (int i = 0; i < 511; i++) sq[i] = (uint32_t)((i-255)*(i-255));
    const uint32_t *sq0 = sq + 255;
    /* the palette by red, index order within a red (a counting sort); first[v] = the first
     * position whose red is >= v */
    uint8_t ord[256]; int first[257] = { 0 };
    for (int j = 0; j < 256; j++) first[pal[j*3] + 1]++;
    for (int v = 0; v < 256; v++) first[v + 1] += first[v];
    { int at[256]; for (int v = 0; v < 256; v++) at[v] = first[v];
      for (int j = 0; j < 256; j++) ord[at[pal[j*3]]++] = (uint8_t)j; }
    for (int lev = 0; lev < NLEV; lev++)
        for (int i = 0; i < 256; i++){
            uint16_t rem;                   /* value * level / (NLEV-1) fits the 16-bit forms */
            int tr = divu16(mulu16(pal[i*3], (uint16_t)lev), NLEV-1, &rem), tg = divu16(mulu16(pal[i*3+1], (uint16_t)lev), NLEV-1, &rem),
                tb = divu16(mulu16(pal[i*3+2], (uint16_t)lev), NLEV-1, &rem);
            const uint32_t *sg = sq0 - tg, *sb = sq0 - tb;
            int best = 256; uint32_t bd = 0xffffffffu;
            int up = first[tr], dn = up - 1;
            while (up < 256 || dn >= 0){
                if (up < 256){
                    const uint8_t *p = pal + ord[up]*3;
                    uint32_t dr = sq0[p[0] - tr];
                    if (dr > bd) up = 256;          /* every colour further up is redder still */
                    else { uint32_t d = dr + sg[p[1]] + sb[p[2]]; int j = ord[up];
                           if (d < bd || (d == bd && j < best)){ bd = d; best = j; } up++; }
                }
                if (dn >= 0){
                    const uint8_t *p = pal + ord[dn]*3;
                    uint32_t dr = sq0[p[0] - tr];
                    if (dr > bd) dn = -1;
                    else { uint32_t d = dr + sg[p[1]] + sb[p[2]]; int j = ord[dn];
                           if (d < bd || (d == bd && j < best)){ bd = d; best = j; } dn--; }
                }
            }
            lut[lev][i] = (uint8_t)best;
        }
}

/* Load an 8-bit BMP (roof_tiles.bmp) into a top-down W*H index buffer (palette ignored;
 * it matches U6's). Tries $ROOF_TILES, build/, then <dir>/. Returns malloc'd pixels. */
static uint8_t *load_bmp8(const char *dir, int *ow, int *oh)
{
    const char *tp = getenv("ROOF_TILES"); if(!tp) tp = "build/roof_tiles.bmp";
    long bn; uint8_t *b = slurp_path(tp, NULL, &bn);
    if(!b){ char p[1200]; snprintf(p,sizeof p,"%s/roof_tiles.bmp",dir); b = slurp_path(p,NULL,&bn); }
    if(!b || bn<54 || b[0]!='B' || b[1]!='M'){ free(b); return NULL; }
    #define LE16(o) ((b)[o]|((b)[(o)+1]<<8))
    #define LE32(o) ((uint32_t)(b)[o]|((uint32_t)(b)[(o)+1]<<8)|((uint32_t)(b)[(o)+2]<<16)|((uint32_t)(b)[(o)+3]<<24))
    uint32_t doff=LE32(10); int W=(int)LE32(18), H=(int)LE32(22), bpp=LE16(28);
    if(W<=0||H<=0||bpp!=8){ printf("roof bmp %dx%d %dbpp unsupported\n",W,H,bpp); free(b); return NULL; }
    int stride=(W+3)&~3;
    uint8_t *pix=malloc((size_t)W*H);
    for(int y=0;y<H;y++) memcpy(pix+(size_t)y*W, b+doff+(size_t)(H-1-y)*stride, (size_t)W); /* flip bottom-up */
    #undef LE16
    #undef LE32
    free(b); *ow=W; *oh=H; return pix;
}

/* Decoded .SHP files. Each .SHP is one whole-file LZW stream, so decoding it once per shape was
 * most of the build time on a 68000. The front-end asks for shapes file by file but comes back
 * to the small files (BLOCKS, MONTAGE) between the big ones, so files up to SHP_SMALL bytes stay
 * decoded (about 100 KB for U6) and one slot holds the current big file (INTRO_2.SHP, 226 KB). */
#define SHP_SMALL (64L*1024)
#define SHP_SLOTS 12
typedef struct { char name[16]; uint8_t *data; int32_t len; } shp_slot_t;
static shp_slot_t shp_cache[SHP_SLOTS];   /* [0] = the big file; [1..] = small files */

static void shp_flush(void)
{
    for (int i = 0; i < SHP_SLOTS; i++) { free(shp_cache[i].data); shp_cache[i].data = NULL; shp_cache[i].name[0] = 0; }
}

/* -> the decoded bytes of `name` (owned by the cache, valid until the next call), or NULL */
static const uint8_t *shp_get(const char *dir, const char *name, int32_t *dn)
{
    for (int i = 0; i < SHP_SLOTS; i++)
        if (shp_cache[i].data && strcmp(shp_cache[i].name, name) == 0) { *dn = shp_cache[i].len; return shp_cache[i].data; }
    long n; uint8_t *raw = slurp(dir, name, &n);
    if (!raw) return NULL;
    uint8_t *d = raw; int32_t len = (int32_t)n;
    int32_t un = u6lzw_unpacked_size(raw, (int32_t)n);
    if (un > 0) { d = malloc((size_t)un); len = d ? u6lzw_decompress(raw, (int32_t)n, d, un) : -1; free(raw); }
    if (!d || len < 0) { free(d); return NULL; }
    int slot = 0;                                   /* big, or no free small slot: the big slot */
    if (len <= SHP_SMALL)
        for (int i = 1; i < SHP_SLOTS; i++) if (!shp_cache[i].data) { slot = i; break; }
    free(shp_cache[slot].data);
    shp_cache[slot].data = d; shp_cache[slot].len = len;
    snprintf(shp_cache[slot].name, sizeof shp_cache[slot].name, "%s", name);
    *dn = len; return d;
}

/* Decode shape `idx` of a U6 .SHP (whole-file LZW) -> malloc'd W*H 8-bit indexed
 * pixels + dims, or NULL (docs/formats/shapes.md; front-end title/menu/intro art). */
static uint8_t *load_shape(const char *dir, const char *name, int idx, int *ow, int *oh)
{
    int32_t dn; const uint8_t *d = shp_get(dir, name, &dn);
    cv_tick();
    if(!d) return NULL;
    int w=0,h=0; uint8_t *out=NULL;                 /* decoded straight into a block of its size */
    if(!u6shape_size(d,dn,idx,&w,&h) || (int32_t)w*h > 512*512 || !(out=malloc((size_t)w*h))) return NULL;
    if(!u6shape_decode(d,dn,idx,&w,&h,out,(int32_t)w*h)){ free(out); return NULL; }
    *ow=w; *oh=h;
    return out;
}

/* like load_shape, and also returns the shape's hotspot (origin) — nuvie draws a sprite at
 * (x - hot_x, y - hot_y); the intro's lightning bolts/strikes are the shapes that need it. */
static uint8_t *load_shape_hot(const char *dir, const char *name, int idx, int *ow, int *oh, int *hx, int *hy)
{
    int32_t dn; const uint8_t *d = shp_get(dir, name, &dn); *hx=*hy=0;
    if(!d) return NULL;
    u6shape_hotspot(d,dn,idx,hx,hy);
    return load_shape(dir,name,idx,ow,oh);
}

/* Load PALETTES.INT sub-palette `idx` as 256 x 6-bit RGB (768B, values 0..63) — the
 * intro/cutscene palettes. Format (per nuvie ScriptCutscene::set_palette): 0x240 bytes
 * per palette, 256 colours x 3 channels x 6 bits BIT-PACKED (LSB-first), value is the
 * DAC 6-bit level (our pack palette is 6-bit). Returns 1 on success. (shapes.md /
 * palette_cycling.md.) Also forces idx 254=black, 255=white for the caption strip. */
static int load_intpal(const char *dir, int idx, uint8_t out[768])
{
    long n; uint8_t *raw = slurp(dir, "PALETTES.INT", &n);
    if(!raw) return 0;
    long base = (long)idx*0x240;
    if(base+0x240 > n){ free(raw); return 0; }
    const uint8_t *buf = raw + base;
    for(int i=0;i<256;i++) for(int j=0;j<3;j++){
        int bit=i*18+j*6, bp=bit/8, sh=bit%8;
        int c = ((buf[bp] + (buf[bp+1]<<8)) >> sh) & 0x3F;
        out[i*3+j] = (uint8_t)c;                       /* keep 6-bit for the pack */
    }
    out[254*3]=out[254*3+1]=out[254*3+2]=0;            /* caption strip: black */
    out[255*3]=out[255*3+1]=out[255*3+2]=63;           /* caption text : white */
    free(raw);
    return 1;
}

/* One intro-scene layer: shape index + screen offset (nuvie sprite x,y). Most shapes
 * self-position (hotspot 0,0), but overlay sprites are placed by the intro at a fixed
 * (dx,dy) — e.g. the lounge avatar at (0,63), the remote at (143,91). */
typedef struct { int idx, dx, dy; } ishape_t;

/* Composite a list of shapes from a U6 .SHP onto a cw x ch canvas: each block at
 * (dx+leftX+blockx, dy+upperY+blocky). Block pixels are written LITERALLY (a covered
 * index 0 is opaque black); only pixels no block covers stay as the caller's prefill
 * (0 = black for a full-screen background, U6SHAPE_TRANSPARENT for an overlay layer).
 * Rebuilds whole intro scenes (base + overlays, or a wide pan panorama) at build
 * time. (shapes.md "Transparency"; nuvie.) */
static void compose_shapes(const char *dir, const char *file, const ishape_t *shp, int nsh,
                           uint8_t *canvas, int cw, int ch)
{
    int32_t dn; const uint8_t *d = shp_get(dir, file, &dn);
    if(!d) return;
    uint32_t off0 = (uint32_t)(d[4]|(d[5]<<8)|(d[6]<<16)|((uint32_t)d[7]<<24));
    int nshapes = (int)((off0-4)/4);
    const uint8_t *end = d+dn;
    for(int k=0;k<nsh;k++){
        int idx=shp[k].idx, ox=shp[k].dx, oy=shp[k].dy;
        if(idx<0||idx>=nshapes) continue;
        uint32_t off = (uint32_t)(d[4+idx*4]|(d[5+idx*4]<<8)|(d[6+idx*4]<<16)|((uint32_t)d[7+idx*4]<<24));
        if(off+8>(uint32_t)dn) continue;
        const uint8_t *sh=d+off;
        int lX=sh[2]|(sh[3]<<8), uY=sh[4]|(sh[5]<<8);
        const uint8_t *p=sh+8;
        while(p+6<=end){
            int dl=p[0]|(p[1]<<8); p+=2; if(dl==0) break;
            int blen=dl>>1, flag=dl&1;
            int px=ox+lX+(int16_t)(p[0]|(p[1]<<8)); p+=2;
            int py=oy+uY+(int16_t)(p[0]|(p[1]<<8)); p+=2;
            int wrote=0;
            /* the block's row (NULL off the canvas; canvases are < 32768 wide and high, so a
             * 16-bit multiply); each run is clipped to it once */
            uint8_t *row=((unsigned)py<(unsigned)ch)?canvas+muls16((int16_t)py,(int16_t)cw):NULL;
            while(wrote<blen && p<end){
                if(flag){ int b=*p++, cnt=b>>1;
                    if(b&1){ uint8_t v=(p<end)?*p++:0;
                        int a=px<0?0:px, e=px+cnt>cw?cw:px+cnt;
                        if(row&&a<e) memset(row+a,v,(size_t)(e-a));
                        px+=cnt; }
                    else   { int n=cnt<end-p?cnt:(int)(end-p);
                        int a=px<0?0:px, e=px+n>cw?cw:px+n;
                        if(row&&a<e) memcpy(row+a,p+(a-px),(size_t)(e-a));
                        p+=n; px+=n; }
                    wrote+=cnt;
                } else { int n=blen-wrote<end-p?blen-wrote:(int)(end-p);    /* plain literal: the rest of the block */
                    int a=px<0?0:px, e=px+n>cw?cw:px+n;
                    if(row&&a<e) memcpy(row+a,p+(a-px),(size_t)(e-a));
                    p+=n; px+=n; wrote+=n; }
            }
        }
    }
}

/* Extract the gypsy's 28 virtue questions from the user's U.EXE at build time. CLEAN-ROOM:
 * these strings are NOT in a data file and are NOT shipped or committed — they live only in
 * the user's own executable and go straight into the (never-committed) build-artifact pack.
 * Located STRUCTURALLY, not by any hardcoded copyrighted anchor: partition the file into
 * "pages" (contiguous NUL-terminated word-wrap fragments joined until a '%c' terminator) and
 * take the unique run of 28 consecutive pages that each contain " A) " and " B) " and end in
 * "?". They come out in canonical U6 order (Q1 Honesty|Compassion .. Q28 Spirituality|Humility);
 * in every question the A) option is the lower-indexed virtue, B) the higher. Fills qbuf[q]
 * (NUL-terminated, quote/%c stripped). Returns the number extracted (0 = U.EXE absent/unknown). */
#define GQ_MAX 28
#define GQ_LEN 320
static int extract_questions(const char *dir, char qbuf[GQ_MAX][GQ_LEN])
{
    char path[1024]; snprintf(path,sizeof path,"%s/U.EXE",dir);
    long n; uint8_t *d=slurp_path(path,NULL,&n);
    if(!d){ printf("chargen questions: U.EXE not found -> generic prompts\n"); return 0; }
    /* the pages of the current run of question pages (a ring of the last GQ_MAX), not every page */
    typedef struct { long off; char text[GQ_LEN]; } page_t;
    page_t *pg=malloc(sizeof(page_t)*GQ_MAX); int npg=0, run=0;
    if(!pg){ free(d); printf("chargen questions: out of memory -> generic prompts\n"); return 0; }
    long i=0;
    while(i<n && npg<2048 && run<GQ_MAX){
        long j=i; while(j<n && d[j]!=0) j++;               /* first NUL-terminated string */
        int flen=(int)(j-i), ok=flen>0;
        for(int k=0;k<flen && ok;k++){ uint8_t c=d[i+k]; if(c<32||c>=127) ok=0; }
        if(!ok){ i=j+1; continue; }
        char acc[GQ_LEN]; int al=0; long k=i; int done=0;   /* join fragments until "%c" */
        while(!done){
            long m=k; while(m<n && d[m]!=0) m++;
            int fl=(int)(m-k), fok=fl>0;
            for(int t=0;t<fl && fok;t++){ uint8_t c=d[k+t]; if(c<32||c>=127) fok=0; }
            if(!fok) break;
            for(int t=0;t<fl && al<GQ_LEN-1;t++) acc[al++]=(char)d[k+t];
            k=m+1;
            if(al>=2 && acc[al-2]=='%' && acc[al-1]=='c') done=1;
            if(al>=GQ_LEN-1) break;
        }
        if(done && al>=2){
            acc[al-2]=0; al-=2;                              /* strip the "%c" terminator */
            int qf = (al>4 && strstr(acc," A) ") && strstr(acc," B) ") && acc[al-1]=='?');
            run = qf ? run+1 : 0;
            if(qf){ pg[npg%GQ_MAX].off=i; memcpy(pg[npg%GQ_MAX].text,acc,(size_t)al+1); }
            npg++;
            i=k;
        } else i=j+1;
    }
    if(run<GQ_MAX){ printf("chargen questions: no 28-run found in U.EXE -> generic prompts\n"); free(pg); free(d); return 0; }
    int best=npg-GQ_MAX;                                     /* the first page of the run */
    for(int q=0;q<GQ_MAX;q++){
        const char*t=pg[(best+q)%GQ_MAX].text; if(t[0]=='"') t++;    /* drop the dangling open-quote */
        snprintf(qbuf[q],GQ_LEN,"%s",t);
    }
    printf("chargen questions: extracted %d from U.EXE @%#lx\n", GQ_MAX, pg[best%GQ_MAX].off);
    free(pg); free(d); return GQ_MAX;
}

/* Extract the intro cutscene NARRATION (15 pages) from the user's U.EXE at build time. CLEAN-ROOM:
 * these strings live only in the user's own executable and go straight into the (never-committed)
 * build-artifact pack — never shipped or hardcoded. Located STRUCTURALLY, not by any copyrighted
 * anchor: the narration is the contiguous run of prose fragments immediately after the intro asset-
 * filename block (a run of >=5 "introN.ext" fragments), grouped into PAGES by the rule "join
 * NUL-terminated word-wrap fragments until one whose last byte is sentence punctuation (.!?)".
 * Internal '\n' line-breaks -> spaces (the engine re-wraps to 320px). Stops at the gypsy region
 * (first fragment containing "%c") or IP_MAX pages. Fills pages[] (NUL-terminated). Returns count. */
#define IP_MAX 16
#define IP_LEN 256
static int extract_intro(const char *dir, char pages[IP_MAX][IP_LEN])
{
    char path[1024]; snprintf(path,sizeof path,"%s/U.EXE",dir);
    long n; uint8_t *d=slurp_path(path,NULL,&n);
    if(!d){ printf("intro narration: U.EXE not found -> engine paraphrase\n"); return 0; }
    /* find the intro asset-filename block: a run of >=5 consecutive "intro...<.ext>" fragments
     * (printable, no spaces, contains '.'). The narration begins at the first prose fragment after. */
    long i=0, start=-1; int run=0;
    while(i<n){
        long j=i; while(j<n && d[j]) j++;
        int L=(int)(j-i);
        int isfn=(L>=8 && L<=12 && !memcmp(d+i,"intro",5));
        if(isfn){ int sp=0,dot=0; for(int k=0;k<L;k++){ if(d[i+k]==' ')sp=1; if(d[i+k]=='.')dot=1; }
                  if(sp||!dot) isfn=0; }
        if(isfn) run++;
        else { if(run>=5){ start=i; break; } run=0; }
        i=j+1;
    }
    if(start<0){ printf("intro narration: filename block not found -> engine paraphrase\n"); free(d); return 0; }
    int np=0; long p=start;
    while(p<n && np<IP_MAX){
        long j=p; while(j<n && d[j]) j++;                  /* peek first fragment of this page */
        int L=(int)(j-p), ok=(L>0);
        for(int k=0;k<L && ok;k++){ uint8_t c=d[p+k]; if(!((c>=32&&c<127)||c==10)) ok=0; }
        if(!ok) break;                                     /* non-text fragment -> region ends */
        int haspc=0; for(int k=0;k+1<L;k++) if(d[p+k]=='%'&&d[p+k+1]=='c') haspc=1;
        if(haspc) break;                                   /* gypsy/question region -> stop */
        char acc[IP_LEN]; int al=0; long k=p;              /* join fragments until one ends in .!? */
        for(;;){
            long m=k; while(m<n && d[m]) m++;
            int fl=(int)(m-k), fok=(fl>0);
            for(int t=0;t<fl && fok;t++){ uint8_t c=d[k+t]; if(!((c>=32&&c<127)||c==10)) fok=0; }
            if(!fok) break;
            for(int t=0;t<fl && al<IP_LEN-1;t++){ char c=(char)d[k+t]; acc[al++]=(c=='\n')?' ':c; }
            char last=(char)d[m-1]; k=m+1;
            if(last=='.'||last=='!'||last=='?') break;      /* sentence end -> page complete */
            if(al>=IP_LEN-1) break;
        }
        acc[al]=0;
        while(al>0 && acc[al-1]==' ') acc[--al]=0;          /* trim trailing spaces */
        char*t=acc; while(*t==' ') t++;                     /* trim leading spaces */
        snprintf(pages[np],IP_LEN,"%s",t); np++;
        p=k;
    }
    printf("intro narration: extracted %d pages from U.EXE @%#lx\n", np, (unsigned long)start);
    free(d); return np;
}

/* Extract the intro's two DATA blocks from the user's U.EXE at build time (intro_sequence.md §1b):
 * the gypsy casting choreography (vial/arm/hand/pour positions, liquid sprite numbers, virtue
 * colours) and the living-room TV (channel programs, sprite offsets, news images). CLEAN-ROOM: the
 * bytes live only in the user's executable and go into the never-committed pack; we know each
 * block by its offset, length, Adler-32 and CRC-32 only. A U.EXE build where the block moved is
 * found by rolling the Adler-32 over the file, then confirmed by the CRC. Not found => length 0
 * and the engine leaves those animations out. */
typedef struct { const char *name; long off; int len; uint32_t adler, crc; } ublk_t;
static const ublk_t UBLK[2]={
    { "gypsy", 0x16a25, 1190, 0x56109063u, 0xd8fa202au },
    { "tv",    0x17374,  788, 0x20884298u, 0x3c5527f8u },
};
static uint32_t crc32_of(const uint8_t *p, int n)
{
    uint32_t c=0xffffffffu;
    for(int i=0;i<n;i++){ c^=p[i]; for(int k=0;k<8;k++) c=(c>>1)^(0xedb88320u&(0u-(c&1u))); }
    return ~c;
}
static long find_ublk(const uint8_t *d, long n, const ublk_t *b)
{
    if(b->off+b->len<=n && crc32_of(d+b->off,b->len)==b->crc) return b->off;
    if(n<b->len) return -1;
    uint32_t a=1, s=0;                                   /* Adler-32 of the first window */
    for(int i=0;i<b->len;i++){ a=(a+d[i])%65521u; s=(s+a)%65521u; }
    /* the roll below reduces by subtraction, not %: each divide is a library call on a 68000 */
    uint32_t lenout[256]; for(int v=0;v<256;v++) lenout[v]=(uint32_t)((b->len*(uint64_t)v)%65521u);
    for(long o=0;;o++){
        if(((s<<16)|a)==b->adler && crc32_of(d+o,b->len)==b->crc) return o;
        if(o+b->len>=n) return -1;
        uint32_t out=d[o], in=d[o+b->len];               /* roll: drop d[o], add d[o+len] */
        a=a+65521u-out+in;                     while(a>=65521u) a-=65521u;   /* < 2*65521 */
        s=s+65521u-lenout[out]+a-1u+65521u;    while(s>=65521u) s-=65521u;   /* < 4*65521 */
    }
}
/* Writes [u8 nblk] + nblk x [u16 len][len bytes] into the front-end TEXT section. */
static void write_ublk(const char *dir, FILE *fe)
{
    char path[1024]; snprintf(path,sizeof path,"%s/U.EXE",dir);
    long n=0; uint8_t *d=slurp_path(path,NULL,&n);
    fputc(2,fe);
    for(int k=0;k<2;k++){
        long o=d?find_ublk(d,n,&UBLK[k]):-1;
        if(o<0){ be16(fe,0); printf("intro data: %s block not found in U.EXE -> left out\n",UBLK[k].name); continue; }
        be16(fe,(unsigned)UBLK[k].len); fwrite(d+o,1,(size_t)UBLK[k].len,fe);
        printf("intro data: %s block %d bytes from U.EXE @%#lx\n",UBLK[k].name,UBLK[k].len,o);
    }
    free(d);
}

/* ---- CHARGEN CEREMONY TEXT (name/gender prompts + approach + gypsy greeting + flask outro) ----
 * Same clean-room rule as the questions/intro: these strings live ONLY in the user's U.EXE and go
 * straight into the never-committed build-artifact pack. Located STRUCTURALLY.
 *
 * U.EXE text convention (verified by inspecting the user's file): word-wrap fragments are NUL-
 * terminated; a display PAGE spans consecutive fragments and ends when a fragment ends in
 *   "%P" (pause marker) | "\n\n" (paragraph) | "%c" (closing curly-quote, at the true end) |
 *   a sentence . ! ? WHILE OUTSIDE an open quote ('"' opens, "%c" closes).
 * That single rule keeps multi-sentence quoted dialogue (and the 28 questions) intact yet splits
 * plain prose per sentence. "%c" (closing quote) is dropped for display; "%s" (player name) is
 * kept verbatim for the engine to substitute; "%P"/trailing "\n\n" are stripped. */
#define CG_LEN 320
#define CG_N   16
#define GARG_N 24                                       /* gargoyle §1B is ~18 pages */
typedef struct {
    char name[CG_LEN], gender[CG_LEN];              /* the two "?\n\n%P" prompts (name, gender)   */
    char approach[CG_N][CG_LEN]; int napproach;     /* woods-approach narration pages (WOODS bg)  */
    char greeting[CG_N][CG_LEN]; int ngreet;        /* gypsy greeting pages (GYPSY bg)            */
    char flask[CG_N][CG_LEN];    int nflask;        /* flask -> sleep -> wake outro pages         */
    char garg[GARG_N][CG_LEN];   int ngarg;         /* §1B gargoyle-sacrifice new-game opening    */
} chargen_text_t;

/* Read one quote-aware page from d[*pp..]; fills out[] (NUL-terminated, markers stripped, leading
 * '"' dropped). *reason: 0 sentence, 1 %c, 2 \n\n, 3 %P, 4 EOF/non-text. Returns text length. */
static int cg_next_page(const uint8_t *d, long n, long *pp, char *out, int cap, int *reason)
{
    long p=*pp; int al=0, open=0, rsn=4;
    while(p<n){
        long m=p; while(m<n && d[m]) m++;                    /* one NUL-terminated fragment */
        int fl=(int)(m-p);
        if(fl==0){ p=m+1; if(al>0){ rsn=2; break; } continue; }   /* empty fragment = break */
        int ftext=1; for(int k=0;k<fl;k++){ uint8_t c=d[p+k]; if(!((c>=32&&c<127)||c==10)){ ftext=0; break; } }
        if(!ftext){ if(al==0) p=m+1; rsn=4; break; }          /* non-text -> region ends */
        for(int k=0;k<fl;k++){                                /* append, tracking quotes + markers */
            char c=(char)d[p+k];
            if(c=='"'){ open=1; continue; }                   /* opening quote: track state, drop glyph */
            if(c=='%' && k+1<fl){ char nx=(char)d[p+k+1];
                if(nx=='c'){ open=0; k++; continue; }         /* closing curly-quote: drop */
                if(nx=='P'){ k++; continue; } }               /* pause marker: drop */
            if(c=='\n') c=' ';                                /* internal newline -> space */
            if(al<cap-1) out[al++]=c;
        }
        char t0=(char)d[m-1], t1=(fl>=2)?(char)d[m-2]:0;
        p=m+1;
        if(t1=='%'&&t0=='P'){ rsn=3; break; }
        if(t1=='%'&&t0=='c'){ rsn=1; break; }
        if(t1=='\n'&&t0=='\n'){ rsn=2; break; }
        if((t0=='.'||t0=='!'||t0=='?') && !open){ rsn=0; break; }
        /* else: word-wrap continuation -> keep joining */
    }
    while(al>0 && out[al-1]==' ') al--;                       /* trim trailing spaces */
    out[al]=0; *pp=p; if(reason) *reason=rsn;
    return al;
}

/* Fill `o` from U.EXE. Returns 1 on success (approach+greeting+flask all found), else 0. */
static int extract_chargen(const char *dir, chargen_text_t *o)
{
    memset(o,0,sizeof *o);
    char path[1024]; snprintf(path,sizeof path,"%s/U.EXE",dir);
    long n; uint8_t *d=slurp_path(path,NULL,&n);
    if(!d){ printf("chargen text: U.EXE not found -> placeholder prompts\n"); return 0; }

    /* (1) name + gender = the first two fragments ending in "?\n\n%P" (strip "\n\n%P"). */
    { int got=0; long i=0;
      while(i<n && got<2){ long j=i; while(j<n && d[j]) j++; int L=(int)(j-i);
        if(L>=6 && d[j-1]=='P'&&d[j-2]=='%'&&d[j-3]=='\n'&&d[j-4]=='\n'&&d[j-5]=='?'){
            int keep=L-4; if(keep>CG_LEN-1) keep=CG_LEN-1;    /* drop trailing "\n\n%P", keep '?' */
            char *dst = got==0?o->name:o->gender;
            memcpy(dst,d+i,(size_t)keep); dst[keep]=0; got++; }
        i=j+1; } }

    /* (2) find the intro asset-filename block (>=5 "introN.ext" fragments); prose starts after it. */
    long start=-1; { long i=0; int run=0;
      while(i<n){ long j=i; while(j<n && d[j]) j++; int L=(int)(j-i);
        int isfn=(L>=8 && L<=12 && !memcmp(d+i,"intro",5));
        if(isfn){ int sp=0,dot=0; for(int k=0;k<L;k++){ if(d[i+k]==' ')sp=1; if(d[i+k]=='.')dot=1; } if(sp||!dot) isfn=0; }
        if(isfn) run++; else { if(run>=5){ start=i; break; } run=0; }
        i=j+1; } }
    if(start<0){ printf("chargen text: intro filename block not found -> placeholder\n"); free(d); return 0; }

    /* (3) walk pages from `start`. Skip the summoning narration (leading .!?-ended pages) up to the
     *     first %c-ended page ("Welcome, O Seeker!") = approach page 1. Then:
     *       approach = 3 pages; greeting = pages until the first question (A)/B)/?);
     *       skip the 28-question run; flask = pages after it, through the title-flourish page. */
    long p=start; char buf[CG_LEN]; int rsn; int seen_welcome=0, in_flask=0, past_q=0;
    while(p<n){
        int L=cg_next_page(d,n,&p,buf,CG_LEN,&rsn);
        if(L<=0 && rsn==4) break;
        if(L<=0) continue;
        int qflag = (strstr(buf," A) ") && strstr(buf," B) ") && buf[L-1]=='?');
        if(!seen_welcome){ if(rsn==1) seen_welcome=1; else continue; }   /* wait for "Welcome" (%c) */
        if(!past_q){
            if(qflag){ past_q=1; continue; }                             /* first question: stop pre-Q collection */
            if(o->napproach<3){ snprintf(o->approach[o->napproach++],CG_LEN,"%s",buf); }
            else if(o->ngreet<CG_N){ snprintf(o->greeting[o->ngreet++],CG_LEN,"%s",buf); }
            continue;
        }
        if(qflag) continue;                                              /* still inside the question run */
        if(!in_flask){                                                   /* flask outro, thru the "Ultima VI" flourish */
            if(o->nflask<CG_N) snprintf(o->flask[o->nflask++],CG_LEN,"%s",buf);
            if(strstr(buf,"Ultima VI")) in_flask=1;
            continue;
        }
        /* §1B gargoyle-sacrifice opening: the narrative run right after the flask. It ends where the
         * game's non-narrative SYSTEM messages begin (the "game in progress" notice)
         * — that content transition is the structural stop boundary (plus the GARG_N cap). */
        if(strstr(buf,"in progress") || strstr(buf,"Please run")) break;
        if(o->ngarg<GARG_N) snprintf(o->garg[o->ngarg++],CG_LEN,"%s",buf);
    }
    int ok = (o->napproach>0 && o->ngreet>0 && o->nflask>0);
    printf("chargen text: name=%d gender=%d approach=%d greeting=%d flask=%d gargoyle=%d (%s)\n",
           o->name[0]?1:0, o->gender[0]?1:0, o->napproach, o->ngreet, o->nflask, o->ngarg, ok?"ok":"INCOMPLETE");
    if(getenv("CHARGEN_DUMP")){
        printf("  NAME  : %s\n  GENDER: %s\n", o->name, o->gender);
        for(int k=0;k<o->napproach;k++) printf("  appr[%d]: %s\n",k,o->approach[k]);
        for(int k=0;k<o->ngreet;k++)    printf("  greet[%d]: %s\n",k,o->greeting[k]);
        for(int k=0;k<o->nflask;k++)    printf("  flask[%d]: %s\n",k,o->flask[k]);
        for(int k=0;k<o->ngarg;k++)     printf("  garg[%d]: %s\n",k,o->garg[k]);
    }
    free(d); return ok;
}

/* Extract BOOK.DAT's readable-text entries (signs / plaques / tombstones / books / scrolls).
 * BOOK.DAT is a library (docs/formats/strings.md): a 2-byte-LE offset index — offset[0] = the
 * index size, so entry count = offset[0]/2 — followed by NUL-terminated ASCII entries (\n = line
 * break). COPYRIGHTED game text: extracted at BUILD time, NEVER committed. Fills books[i] (NUL-
 * terminated, \n preserved). Returns the entry count. The engine indexes by (object quality - 1). */
#define BOOK_MAX 256
#define BOOK_LEN 2048
static int extract_books(const char *dir, FILE *f)   /* writes [u16 nbook][nbook x [u16 len][bytes]] */
{
    long n; uint8_t *d = slurp(dir, "BOOK.DAT", &n);
    int idxsz = (d && n >= 4) ? (d[0] | (d[1]<<8)) : 0; /* offset[0] = index size in bytes */
    int cnt = idxsz/2;
    if(!d || n < 4){ free(d); be16(f,0); return 0; }
    if(cnt<1 || cnt>BOOK_MAX || idxsz>n){ free(d); printf("BOOK.DAT: bad index (%d)\n", idxsz); be16(f,0); return 0; }
    be16(f,(unsigned)cnt);
    for(int i=0;i<cnt;i++){
        int off = d[i*2] | (d[i*2+1]<<8), t=0;
        if(off>=idxsz && off<(int)n)                 /* up to the entry's NUL (\n preserved) */
            while(off+t<(int)n && d[off+t] && t<BOOK_LEN-1) t++;
        be16(f,(unsigned)t); if(t) fwrite(d+off,1,(size_t)t,f);
    }
    free(d);
    return cnt;
}

/* Build roof DRAWING data (U6WK) from Nuvie's roof_tiles.bmp + roof_map_00.dat.
 * roof_map values index roof_tiles.bmp (5 cols x N rows of 16x16; palette == U6's,
 * transparent index 255). Ship only DISTINCT used roof tiles (dense-indexed) + a sorted
 * [u32 pos, u16 idx] map. Engine overdraws these over roofed cells when the avatar is
 * OUTSIDE (Nuvie drawRoofs). Returns malloc'd tileset (*out_ntiles x 256) + *out_map
 * (nroof x 6 bytes), or NULL if no roof data. See docs/rules/lighting.md §Stage E. */
static uint8_t *build_roof(const char *dir, int *out_ntiles, uint8_t **out_map, long *out_maplen)
{
    *out_ntiles=0; *out_map=NULL; *out_maplen=0;
    int W,H; uint8_t *bmp=load_bmp8(dir,&W,&H);
    if(!bmp){ printf("roof: no roof_tiles.bmp -> roofs disabled\n"); return NULL; }
    int cols=W/16;
    const char *mp=getenv("ROOF_MAP"); if(!mp) mp="build/roof_map_00.dat";
    long rn; uint8_t *rd=slurp_path(mp,NULL,&rn);
    if(!rd){ char p[1200]; snprintf(p,sizeof p,"%s/roof_map_00.dat",dir); rd=slurp_path(p,NULL,&rn); }
    if(!rd){ free(bmp); printf("roof: no roof_map_00.dat -> roofs disabled\n"); return NULL; }
    int32_t *rpos=malloc(sizeof(int32_t)*8192); int *rval=malloc(sizeof(int)*8192), *remap=malloc(sizeof(int)*4096); int nroof=0;
    if(!rpos||!rval||!remap){ free(rpos); free(rval); free(remap); free(rd); free(bmp); printf("roof: out of memory\n"); return NULL; }
    { long p=0,idx=0; while(p+3<=rn && nroof<8192){ int sk=rd[p]|(rd[p+1]<<8); p+=2; int run=rd[p++]; idx+=sk;
        for(int i=0;i<run && p+2<=rn && nroof<8192;i++){ int val=rd[p]|(rd[p+1]<<8); p+=2;
            if(val){ rpos[nroof]=(int32_t)idx; rval[nroof]=val; nroof++; } idx++; } } }
    free(rd);
    for(int i=0;i<4096;i++) remap[i]=-1;
    int ntiles=0;
    for(int i=0;i<nroof;i++){ int v=rval[i]; if(v>=0 && v<4096 && remap[v]<0) remap[v]=ntiles++; }
    uint8_t *tiles=malloc((size_t)ntiles*256);
    for(int v=0;v<4096;v++){ int di=remap[v]; if(di<0) continue;
        int col=v%cols, row=v/cols;
        for(int ty=0;ty<16;ty++)for(int tx=0;tx<16;tx++)
            tiles[di*256+ty*16+tx] = bmp[(size_t)(row*16+ty)*W + (col*16+tx)];
    }
    free(bmp);
    uint8_t *map=malloc((size_t)nroof*6); long ml=0;
    for(int i=0;i<nroof;i++){ uint32_t pp=(uint32_t)rpos[i]; int di=remap[rval[i]];
        map[ml++]=(uint8_t)((pp>>24)&0xFF); map[ml++]=(uint8_t)((pp>>16)&0xFF);
        map[ml++]=(uint8_t)((pp>>8)&0xFF);  map[ml++]=(uint8_t)(pp&0xFF);
        map[ml++]=(uint8_t)((di>>8)&0xFF);  map[ml++]=(uint8_t)(di&0xFF); }
    printf("roof: %d roofed tiles, %d distinct roof-tiles; map %ld B, tileset %d B\n",
           nroof, ntiles, ml, ntiles*256);
    free(rpos); free(rval); free(remap);
    *out_ntiles=ntiles; *out_map=map; *out_maplen=ml; return tiles;
}

/* Parse the actor-inventory chunk (path: [u16 count][count*8] object records) for each
 * actor's READIED gear (docs/formats/objlist.md §3b): a record is readied when
 * (status & 0x18) == 0x18; its owner actor = the x field (b[1]|((b[2]&3)<<8)). Fills
 * weapon[256] (readied weapon obj_n, 0=none) + ac[256] (Σ armour_ac). (combat.md CB3b) */
static void build_actor_gear(const char *path, uint16_t weapon[256], uint8_t ac[256])
{
    for (int i = 0; i < 256; i++){ weapon[i] = 0; ac[i] = 0; }
    if (!path) return;
    long n; uint8_t *d = slurp_path(path, NULL, &n);
    if (!d || n < 2){ free(d); return; }
    int count = d[0] | (d[1] << 8); long p = 2; int nready = 0;
    for (int o = 0; o < count && p + 8 <= n; o++, p += 8){
        const uint8_t *b = d + p;
        if ((b[0] & 0x18) != 0x18) continue;                 /* readied (equipped) only */
        int owner = b[1] | ((b[2] & 0x03) << 8); if (owner < 0 || owner >= 256) continue;
        int obj_n = b[4] | ((b[5] & 0x03) << 8);
        if (weapon_dmg(obj_n) > 0 && weapon[owner] == 0) weapon[owner] = (uint16_t)obj_n;
        if (armour_ac(obj_n) > 0){ int t = ac[owner] + armour_ac(obj_n); ac[owner] = (uint8_t)(t > 255 ? 255 : t); }
        nready++;
    }
    free(d);
    printf("gear: %d readied items; readied party gear:", nready);
    for (int i = 1; i <= 8; i++) if (weapon[i] || ac[i]) printf(" a%d(weapon=%d,ac=%d)", i, weapon[i], ac[i]);
    printf("\n");
}

/* sum the party's spendable GOLD from the inventory chunk: coins are obj_n 88 (OBJ_U6_GOLD),
 * carried in actors' inventories (objlist.md). Party gold = Σ qty of obj_n-88 records owned by
 * a party member (owner actor in the `party` set), including coins nested in a bag. inv chunk =
 * [u16 count][count*8 records]; obj_n=b4|((b5&3)<<8), owner=b1|((b2&3)<<8), qty16=(b7<<8)|b6. */
static int party_gold(const char *path, const uint8_t *objl, long oln){
    if(!path || !objl) return 0;
    long n; uint8_t *d=slurp_path(path, NULL, &n);
    if(!d || n<2){ free(d); return 0; }
    int cnt=d[0]|(d[1]<<8), gold=0;
    if(2+(long)cnt*8>n) cnt=(int)((n-2)/8);
    for(int k=0;k<cnt;k++){ const uint8_t *r=d+2+(long)k*8;
        int objn=r[4]|((r[5]&3)<<8); if(objn!=88) continue;                 /* OBJ_U6_GOLD */
        /* resolve the OWNER: an in-CONTAINER record's x/y bits are a PARENT record index
         * (((y&0x3f)<<10)|x, objblk.md §2) — walk up to the in-inventory holder, whose x
         * IS the owner actor number. (e.g. gold inside a carried bag.) */
        int rec=k, depth=0, owner=-1;
        while(depth++<8){ const uint8_t *b=d+2+(long)rec*8;
            int x=b[1]|((b[2]&3)<<8), loc=b[0]&0x18;
            if(loc==0x08){ int pl=(((((b[2]>>2)|((b[3]&0xf)<<6)))&0x3f)<<10)|x;
                if(pl<0||pl>=cnt||pl==rec) break; rec=pl; continue; }
            if(loc==0x10||loc==0x18) owner=x;
            break; }
        if(owner<1 || owner>=256 || 0x800+owner>=oln || !(objl[0x800+owner]&0x80)) continue; /* party only */
        gold += (r[7]<<8)|r[6];
    }
    free(d);
    return gold;
}

/* Scan the inventory chunk for the Avatar's spellbook (obj 57, in-inventory, owner=1) and collect
 * the known spell numbers from its spell children (obj 58, in-container, quality = U6 spell_num;
 * quality 255 = MAGIC_ALL_SPELLS = every spell). Fills a 128-bit set `known` (spell_num 0..127);
 * sets *all=1 for the all-spells marker. Returns 1 if the Avatar HAS a spellbook. (Nuvie
 * Magic::spellbook_has_spell scans the book by OBJ_MATCH_QUALITY — magic_skills.md §4.) */
static int scan_spellbook(const char *path, uint8_t known[16], int *all){
    *all=0; for(int i=0;i<16;i++) known[i]=0;
    if(!path) return 0;
    long n; uint8_t *d=slurp_path(path,NULL,&n);
    if(!d||n<2){ free(d); return 0; }
    int cnt=d[0]|(d[1]<<8); if(2+(long)cnt*8>n) cnt=(int)((n-2)/8);
    int book=-1;
    for(int k=0;k<cnt;k++){ const uint8_t *r=d+2+(long)k*8;
        int objn=r[4]|((r[5]&3)<<8), loc=r[0]&0x18, owner=r[1]|((r[2]&3)<<8);
        if(objn==57 && (loc==0x10||loc==0x18) && owner==1){ book=k; break; } }   /* OBJ_U6_SPELLBOOK */
    if(book<0){ free(d); return 0; }
    for(int k=0;k<cnt;k++){ const uint8_t *r=d+2+(long)k*8;
        int objn=r[4]|((r[5]&3)<<8), loc=r[0]&0x18;
        if(objn!=58 || loc!=0x08) continue;                                       /* OBJ_U6_SPELL, in-container */
        int x=r[1]|((r[2]&3)<<8), pl=((((r[2]>>2)|((r[3]&0xf)<<6))&0x3f)<<10)|x;   /* parent link (objblk.md §2) */
        if(pl!=book) continue;
        int sn=r[7];                                                              /* quality = spell number */
        if(sn==255) *all=1;                                                        /* MAGIC_ALL_SPELLS */
        else if(sn<128) known[sn>>3]|=(uint8_t)(1<<(sn&7));
    }
    free(d);
    return 1;
}

/* Sum the Avatar's REAGENTS (obj 65..72 = SulfAsh..Mandrake) from the inventory chunk into
 * counts[8] (index obj_n-65). Reagents live in a carried bag, so resolve each record's owner up
 * the parent chain (like party_gold) and keep only actor 1 (the Avatar). qty16 = (b7<<8)|b6 (a
 * reagent's quality is 0, so this is just the byte count). Returns 1 if the Avatar carries any. */
static int scan_reagents(const char *path, uint8_t counts[8]){
    for(int i=0;i<8;i++) counts[i]=0;
    if(!path) return 0;
    long n; uint8_t *d=slurp_path(path,NULL,&n);
    if(!d||n<2){ free(d); return 0; }
    int cnt=d[0]|(d[1]<<8); if(2+(long)cnt*8>n) cnt=(int)((n-2)/8);
    int any=0;
    for(int k=0;k<cnt;k++){ const uint8_t *r=d+2+(long)k*8;
        int objn=r[4]|((r[5]&3)<<8); if(objn<65||objn>72) continue;
        int rec=k, depth=0, owner=-1;                              /* resolve owner up the container chain */
        while(depth++<8){ const uint8_t *b=d+2+(long)rec*8;
            int x=b[1]|((b[2]&3)<<8), loc=b[0]&0x18;
            if(loc==0x08){ int pl=((((b[2]>>2)|((b[3]&0xf)<<6))&0x3f)<<10)|x; if(pl<0||pl>=cnt||pl==rec) break; rec=pl; continue; }
            if(loc==0x10||loc==0x18) owner=x; break; }
        if(owner!=1) continue;                                     /* Avatar only */
        int q=(r[7]<<8)|r[6]; if(q>255) q=255;
        int c=counts[objn-65]+q; counts[objn-65]=(uint8_t)(c>255?255:c); any=1;
    }
    free(d);
    return any;
}

/* Low-colour ST section (U6WW): quantise the 256-colour VGA art to an OPTIMISED 16
 * chosen from the ST's 512-colour (3-bit/channel) hardware gamut — NOT the fixed EGA 16
 * (docs/design/palette_lowcolor.md; the "lazy EGA port" we deliberately beat). ALL work is
 * HOST-side (median-cut, tools/quant.h); the ST does only a 256->16 LUT lookup + 4-plane c2p.
 * The palette is USAGE-WEIGHTED by the tileset pixel histogram, so colours covering more of
 * the art get finer boxes. Fills:
 *   pal_lvl[48] = 16 x {u8 r,u8 g,u8 b} as raw ST 3-bit levels 0..7 (rdr_st packs r<<8|g<<4|b
 *                 straight into a shifter colour register — no runtime maths)
 *   lut[256]    = lut[i] = the 16-colour index nearest to U6PAL colour i (transparent 255 too)
 * Returns the weighted RMS error (0..255 per channel) for the build log. U6PAL is RGB6 (0..63).
 * The side-by-side vs-EGA quality comparison lives in the tools/quantize QA target. */
/* 6-bit VGA level -> 8-bit, (v*255+31)/63, from a table: a 32-bit divide is a library call on
 * the 68000 the Atari-side converter runs on. */
static int rgb6to8(int v)
{
    static int16_t t[256]; static int ready;
    if (!ready) { for (int i = 0; i < 256; i++) t[i] = (int16_t)((i*255+31)/63); ready = 1; }
    return (unsigned)v < 256 ? t[v] : (v*255+31)/63;
}
#define RGB6TO8(v) rgb6to8(v)

/* a 256-colour 6-bit palette as r,g,b bytes in 8 bits; the same palette comes back ~330 times */
static const uint8_t *pal6to8(const uint8_t *pal6)
{
    static uint8_t last[768], pal8[768]; static int have;
    if (!have || memcmp(last, pal6, 768)) {
        for (int i = 0; i < 768; i++) pal8[i] = (uint8_t)RGB6TO8(pal6[i]);
        memcpy(last, pal6, 768); have = 1; }
    return pal8;
}

/* Colour cycling on the ST (docs/formats/palette_cycling.md): the engine can't push a rotated
 * 256-colour palette to 16-colour hardware, so we RESERVE a small contiguous block of the 16 for
 * each rotated U6PAL ramp and rotate just those registers at runtime. fire = U6PAL 224..231
 * (red->orange->yellow, confirmed cycling: throne-room braziers). Reserving costs general slots so
 * keep it small; band 0 (the map) inherits these slots since it copies the U6WW palette. Mirrors
 * the engine's 256-colour CYCLE[] {224,8}. Emitted as the U6WZ pack section {first_slot, nslots}. */
#define ST_CYC_LO    224
#define ST_CYC_LEN   8
#define ST_CYC_SLOTS 3

/* build a weighted RGB8 colour list from a freq[256] U6PAL-index histogram (skips empty entries). */
static int colors_from_freq(const long freq[256], const uint8_t *palb, CCol *col)
{
    int ncol=0;
    for (int i=0;i<256;i++){ if(!freq[i]) continue;
        col[ncol].r=RGB6TO8(palb[i*3]); col[ncol].g=RGB6TO8(palb[i*3+1]); col[ncol].b=RGB6TO8(palb[i*3+2]);
        col[ncol].w=freq[i]; ncol++; }
    return ncol;
}

/* median-cut a weighted RGB8 colour list to 16 on the ST 3-bit gamut: fill pal_lvl[48] (raw 3-bit
 * levels the ST packs straight into a colour register) + lut[256] (nearest of the 16 to each U6PAL
 * colour). snap[16] returns the snapped RGB8 palette (for the RMS metric). Returns the box count. */
static int quantize_colors(CCol *col, int ncol, const uint8_t *palb,
                           uint8_t pal_lvl[48], uint8_t pal_lvl4[48], uint8_t lut[256], CCol snap[16], int nslots)
{
    if (nslots > 16) nslots = 16; if (nslots < 1) nslots = 1;
    CCol pal[16]; int npal=q_median_cut(col,ncol,nslots,pal);
    /* STE 4-bit levels (optional): snap the SAME centroids to the 4-bit gamut (0..15), BEFORE the
     * 3-bit snap below mutates pal. The ST 3-bit and STE 4-bit palettes share centroids + LUT (the
     * nearest box is the same at either precision); only the displayed intensity is finer on STE. */
    q_lv_init();
    if (pal_lvl4) for (int i=0;i<16;i++){ int r=0,g=0,b=0;
        if(i<npal){ r=q_lv4[pal[i].r]; g=q_lv4[pal[i].g]; b=q_lv4[pal[i].b]; }
        pal_lvl4[i*3]=(uint8_t)r; pal_lvl4[i*3+1]=(uint8_t)g; pal_lvl4[i*3+2]=(uint8_t)b; }
    q_snap_pal(pal,npal,3);
    for (int i=0;i<16;i++){ int r=0,g=0,b=0;               /* slots >= npal stay black (a cycle-reserve pass may overwrite them) */
        if(i<npal){ r=q_lv3[pal[i].r]; g=q_lv3[pal[i].g]; b=q_lv3[pal[i].b]; }
        pal_lvl[i*3]=(uint8_t)r; pal_lvl[i*3+1]=(uint8_t)g; pal_lvl[i*3+2]=(uint8_t)b; }
    q_lut(pal,npal,pal6to8(palb),lut);                     /* nearest among the npal general boxes only */
    for (int i=0;i<npal;i++) snap[i]=pal[i];
    return npal;
}

/* Reserve the top `nres` slots of a 16 for a rotated U6PAL ramp (colour cycling on the ST): fill
 * slots [16-nres..15] with nres samples of the ramp [src_lo, src_lo+src_len) (snapped to 3-bit) and
 * route lut[src_lo..src_lo+src_len) onto them IN RAMP ORDER, so when the engine rotates those
 * registers the shifter animates the ramp (fire/water). The general quantise must have used only
 * (16-nres) boxes (call quantize_colors with nslots=16-nres) so no non-ramp pixel lands on these
 * slots. (docs/formats/palette_cycling.md; the ST counterpart of the engine's 256-colour CYCLE[].) */
static void reserve_cycle(uint8_t pal_lvl[48], uint8_t lut[256], const uint8_t *palb,
                          int nres, int src_lo, int src_len, int maxlvl)
{
    int base = 16 - nres;
    for (int s=0; s<nres; s++){
        int si = src_lo + (s*src_len + src_len/2)/nres;          /* centre-sample the ramp */
        if (si >= src_lo+src_len) si = src_lo+src_len-1;
        pal_lvl[(base+s)*3+0]=(uint8_t)((RGB6TO8(palb[si*3+0])*maxlvl+127)/255);   /* maxlvl = 7 (ST) or 15 (STE) */
        pal_lvl[(base+s)*3+1]=(uint8_t)((RGB6TO8(palb[si*3+1])*maxlvl+127)/255);
        pal_lvl[(base+s)*3+2]=(uint8_t)((RGB6TO8(palb[si*3+2])*maxlvl+127)/255);
    }
    if (lut) for (int i=0; i<src_len; i++)                       /* source ramp index -> reserved slot */
        lut[src_lo+i] = (uint8_t)(base + (i*nres)/src_len);
}

/* weighted RMS (0..255/ch) of a freq[256] histogram remapped through lut into the snapped palette. */
static double quant_rms(const long freq[256], const uint8_t *palb, const uint8_t lut[256], const CCol snap[16])
{
    double se=0; long wsum=0;
    for (int i=0;i<256;i++){ if(!freq[i]) continue;
        int r=RGB6TO8(palb[i*3]), g=RGB6TO8(palb[i*3+1]), b=RGB6TO8(palb[i*3+2]);
        long j=lut[i], dr=snap[j].r-r, dg=snap[j].g-g, db=snap[j].b-b;
        se += (double)(dr*dr+dg*dg+db*db)*(double)freq[i]; wsum += freq[i]; }
    return wsum ? sqrt(se/(3.0*(double)wsum)) : 0.0;
}

/* U6WW single global palette: quantise the whole 2048-tile set (usage-weighted, transparent 255
 * excluded) to the scene-neutral optimised 16. Fills pal_lvl[48] + lut[256]; returns weighted RMS. */
/* terrain colour histogram: every pixel of every tile but the transparent 255, counted as main()
 * writes the tileset (the low-colour and conversation palettes both weigh by it) */
static long g_terr[256];
static void terrain_count(const uint8_t tile[256])
{
    cv_hist(tile, 256, g_terr);
    g_terr[255] = 0;
}

static double build_lowcolor(const uint8_t *palb,
                             uint8_t pal_lvl[48], uint8_t pal_lvl4[48], uint8_t lut[256])
{
    static long freq[256]; for (int i=0;i<256;i++) freq[i]=g_terr[i];
    /* Hold the fire ramp OUT of the median-cut: it gets ST_CYC_SLOTS dedicated slots (reserve_cycle
     * below) so the engine can rotate it. Leaving it in would waste a general box on a colour we
     * pin anyway, and let non-fire pixels land on the (rotating) reserved slots. */
    for (int i=ST_CYC_LO;i<ST_CYC_LO+ST_CYC_LEN;i++) freq[i]=0;
    static CCol col[256]; int ncol=colors_from_freq(freq,palb,col);
    CCol snap[16]; quantize_colors(col,ncol,palb,pal_lvl,pal_lvl4,lut,snap,16-ST_CYC_SLOTS);
    reserve_cycle(pal_lvl, lut, palb,ST_CYC_SLOTS,ST_CYC_LO,ST_CYC_LEN,7);       /* fire -> slots 13..15, 3-bit + LUT */
    if(pal_lvl4) reserve_cycle(pal_lvl4,NULL,palb,ST_CYC_SLOTS,ST_CYC_LO,ST_CYC_LEN,15); /* STE 4-bit fire slots (LUT shared) */
    double rms=quant_rms(freq,palb,lut,snap);
    long wsum=0; for (int i=0;i<256;i++) wsum+=freq[i];
    printf("lowcolor (U6WW): optimised %d-colour ST palette from %d used colours, %ld px; weighted RMS %.2f "
           "(+%d slots reserved for fire cycling 224-231)\n",
           16-ST_CYC_SLOTS, ncol, wsum, rms, ST_CYC_SLOTS);
    for (int i=0;i<16;i++)
        printf("   pal[%2d] rgb3(%d,%d,%d) ~rgb6(%2d,%2d,%2d)\n", i,
               pal_lvl[i*3],pal_lvl[i*3+1],pal_lvl[i*3+2], pal_lvl[i*3]*9,pal_lvl[i*3+1]*9,pal_lvl[i*3+2]*9);
    return rms;
}

/* U6WX band 1 (command bar, rows 182..199): quantise the STATIC bottom-strip content — the command
 * icon tiles (400..409) + the parchment under them (PAPER rows 182..199) — to its own 16, so the
 * command bar renders in icon-optimised colours (and band 0 stops spending slots on icon-only hues).
 * SCENE-NEUTRAL: this content is identical in every scene, so it never hurts other maps. */
static void build_command_band(u6tileset_t *ts, const uint8_t *paper, const uint8_t *palb,
                               uint8_t pal_lvl[48], uint8_t pal_lvl4[48], uint8_t lut[256])
{
    static long freq[256]; for (int i=0;i<256;i++) freq[i]=0;
    for (int y=182;y<200;y++) for (int x=0;x<320;x++){ int c=paper[y*320+x]; if(c!=255) freq[c]++; }
    for (int t=400;t<410;t++){ uint8_t tile[256]; u6tile_decode(ts,t,tile);
        for (int p=0;p<256;p++){ int c=tile[p]; if(c!=255) freq[c]++; } }
    static CCol col[256]; int ncol=colors_from_freq(freq,palb,col);
    CCol snap[16]; quantize_colors(col,ncol,palb,pal_lvl,pal_lvl4,lut,snap,16);   /* command bar has no cycling ramp */
    long wsum=0; for (int i=0;i<256;i++) wsum+=freq[i];
    printf("lowcolor (U6WX band1): command bar 16 from icons 400-409 + parchment, %d colours, %ld px; RMS %.2f\n",
           ncol, wsum, quant_rms(freq,palb,lut,snap));
}

/* PER-SCENE low-colour palette (front-end intro/gypsy/woods/storm): quantise a scene's OWN 256-colour
 * palette (its PALETTES.INT palette, RGB6) weighted by the scene's pixel usage to its own optimised
 * ST 16 + 256->16 LUT, so the ST renders each cutscene scene in colour instead of the game tileset
 * 16. Fills lc[48+256] = [16 x {r,g,b} 3-bit levels][256 lut]. (docs/design/palette_lowcolor.md) */
static void build_scene_lc(const uint8_t *pal6, const uint8_t *pix, int npix, uint8_t lc[48+256])
{
    static long freq[256]; for (int i=0;i<256;i++) freq[i]=0;
    cv_hist(pix,npix,freq);
    freq[U6SHAPE_TRANSPARENT]=0;                 /* uncovered/transparent pixels never reach the screen */
    static CCol col[256]; int ncol=0;
    for (int i=0;i<256;i++){ if(!freq[i]) continue;
        col[ncol].r=RGB6TO8(pal6[i*3]); col[ncol].g=RGB6TO8(pal6[i*3+1]); col[ncol].b=RGB6TO8(pal6[i*3+2]);
        col[ncol].w=freq[i]; ncol++; }
    CCol pal[16]; int np = ncol ? q_median_cut(col,ncol,16,pal) : 0;
    q_snap_pal(pal,np,3);
    uint8_t *lvl=lc; uint8_t *lut=lc+48;
    for (int i=0;i<16;i++){ int r=0,g=0,b=0;
        if(i<np){ r=q_lv3[pal[i].r]; g=q_lv3[pal[i].g]; b=q_lv3[pal[i].b]; }
        lvl[i*3]=(uint8_t)r; lvl[i*3+1]=(uint8_t)g; lvl[i*3+2]=(uint8_t)b; }
    if (np) q_lut(pal,np,pal6to8(pal6),lut); else memset(lut,0,256);
}
#undef RGB6TO8

/* U6WY per-portrait CONVERSATION palettes: when an NPC's portrait is up (talk()), the ST swaps its
 * whole 16 to one tuned for the CONVERSATION screen so the portrait renders true (the skin tones /
 * cool greys the terrain-16 lacks) instead of muddy brown — see docs/design/palette_lowcolor.md and
 * tools/splitcheck. Each palette is quantised from the PORTRAIT (up-weighted ~50% — it's the focus)
 * + the TERRAIN histogram (~35%, so the frozen map stays recognisable) + PARCHMENT (~15%, the text
 * panel). Indexed EXACTLY as the engine loads portraits: idx<98 -> PORTRAIT.A[idx], else B[idx-98].
 * [4 "U6WY"][u16 nport][nport x (48 pal 3-bit levels + 256 lut)]. */
static void build_conv_palettes(FILE *f, const uint8_t *paper, const uint8_t *palb,
                                const uint8_t *portA, long portAlen, const uint8_t *portB, long portBlen,
                                uint8_t *conv4, int *nport_out)   /* conv4 (nport*48) = STE 4-bit companions for U6WS */
{
    static long terr[256], parch[256]; for (int i=0;i<256;i++){ terr[i]=g_terr[i]; parch[i]=0; }
    for (int i=0;i<320*200;i++){ int c=paper[i]; if(c!=255) parch[c]++; }
    long St=0,Sc=0; for (int i=0;i<256;i++){ St+=terr[i]; Sc+=parch[i]; }
    if(!St) St=1; if(!Sc) Sc=1;

    u6lib_t A,B; int okA=u6lib_open(&A,portA,(int32_t)portAlen,4,false);
    int okB=(portB && portBlen>0) && u6lib_open(&B,portB,(int32_t)portBlen,4,false);
    int cA=okA?u6lib_count(&A):0, cB=okB?u6lib_count(&B):0;
    int nport = 98 + cB;                                        /* engine split: A=0..97, B=98+ */

    /* comb for a colour the portrait doesn't use: round(terr*35000/St + parch*15000/Sc), the
     * same for every portrait, so worked out once (64-bit divides are library calls on a 68000) */
    static long nopf[256];
    { uint64_t D = (uint64_t)St*(uint64_t)Sc;
      for (int i=0;i<256;i++){
          uint64_t N = (uint64_t)terr[i]*35000u*(uint64_t)Sc + (uint64_t)parch[i]*15000u*(uint64_t)St;
          uint64_t q = N/D, r = N - q*D;
          nopf[i] = (long)(q + (2*r >= D)); } }
    /* For a colour the portrait does use, comb = round(pf*50000/Sp + terr*35000/St + parch*15000/Sc).
     * Split each term into whole + fraction: the wholes add up, and the rounding only asks how many
     * of 1/2, 3/2, 5/2 the fractions ra/Sp + rb/St + rc/Sc reach. With G = rb/St + rc/Sc = gn/StSc
     * (per colour, worked out once), the fraction reaches (2k-1)/2 when
     *   2*ra*StSc >= Sp*T[k],  T[k] = (2k-1)*StSc - 2*gn   (always, when T[k] <= 0),
     * which takes a 16-bit divide and 16x64 multiplies per colour instead of 64-bit divides; the
     * result is the same as rounding the exact sum over the common denominator Sp*St*Sc. */
    const uint64_t StSc = (uint64_t)St*(uint64_t)Sc;
    static long qbc[256];  static int64_t thr[256][3];
    for (int i=0;i<256;i++){
        uint64_t B = (uint64_t)terr[i]*35000u, C = (uint64_t)parch[i]*15000u;
        uint64_t qb = B/(uint64_t)St, rb = B - qb*(uint64_t)St, qc = C/(uint64_t)Sc, rc = C - qc*(uint64_t)Sc;
        uint64_t gn = rb*(uint64_t)Sc + rc*(uint64_t)St;
        qbc[i] = (long)(qb + qc);
        for (int k=0;k<3;k++) thr[i][k] = (int64_t)((uint64_t)(2*k+1)*StSc) - (int64_t)(2*gn); }

    fwrite("U6WY",1,4,f); be16(f,(unsigned)nport);
    int made=0;
    for (int idx=0; idx<nport; idx++){
        cv_tick();
        uint8_t port[56*64]; int have=0;
        const u6lib_t *lib=(idx<98)?&A:&B; int li=(idx<98)?idx:idx-98;
        int libok=(idx<98)?okA:okB, libc=(idx<98)?cA:cB;
        if (libok && li<libc){ int32_t sz; const uint8_t *blob=u6lib_entry(lib,li,&sz);
            if (blob && u6lzw_unpacked_size(blob,sz)==56*64 && u6lzw_decompress(blob,sz,port,56*64)==56*64) have=1; }
        static uint8_t pal_lvl[48], lut[256];
        static long comb[256];
        if (!have){                                            /* no portrait -> ship terrain-16 (near-no-op swap) */
            for (int i=0;i<256;i++) comb[i]=terr[i];
        } else {
            static long pf[256]; for(int i=0;i<256;i++) pf[i]=0;
            cv_hist(port,56*64,pf);
            long Sp=56*64-pf[255]; pf[255]=0;
            if(Sp<1) Sp=1;
            /* portrait 50% / terrain 35% / parchment 15%: comb = round(pf*50000/Sp + terr*35000/St
             * + parch*15000/Sc), exactly (see qbc/thr above). Sp <= 56*64, so pf*50000 < 2^32,
             * the quotient <= 50000 and 2*ra < 2*Sp all fit the 16-bit forms. */
            for (int i=0;i<256;i++){
                if (!pf[i]) { comb[i]=nopf[i]; continue; }   /* the same fraction with Sp cancelled */
                uint16_t ra; long c = (long)divu16(mulu16((uint16_t)pf[i], 50000u), (uint16_t)Sp, &ra) + qbc[i];
                uint64_t lhs = mulu16x64((uint16_t)(2*ra), StSc);
                for (int k=0;k<3;k++){
                    if (thr[i][k] > 0 && lhs < mulu16x64((uint16_t)Sp, (uint64_t)thr[i][k])) break;
                    c++; }
                comb[i]=c; }
            made++;
        }
        static CCol col[256]; int nc=colors_from_freq(comb,palb,col); CCol snap[16];
        uint8_t pal4[48];
        quantize_colors(col,nc,palb,pal_lvl,pal4,lut,snap,16);   /* portrait palette: no cycling ramp */
        fwrite(pal_lvl,1,48,f); fwrite(lut,1,256,f);
        if (conv4) memcpy(conv4+(size_t)idx*48, pal4, 48);        /* STE 4-bit companion for U6WS */
    }
    if (nport_out) *nport_out = nport;
    printf("conv palettes (U6WY): %d slots, %d with portraits (PORTRAIT.A=%d, PORTRAIT.B=%d)\n",
           nport, made, cA, cB);
}

/* ------------------------------------------------------------------------ *
 * M9 multi-game: a per-game input profile + a self-contained CORE-pack emitter.
 * The full U6 pack (main() below) bakes in ~20 U6-only sub-sections (chargen,
 * PAPER background, portraits, intro cutscenes, ST low-colour palettes, roof
 * lighting, ...). To bring up Savage/Martian we first emit just the CORE world
 * — geometry + terrain tiles + palette + passability + objects — which is all
 * that is needed to host-validate that SE/MD data decodes with our U6 loaders.
 * The core pack is a strict prefix of the U6WT layout (through the objects
 * section); P2 extends the runtime reader to consume it. See multigame.md.
 *
 * Selected by $MKWORLD_GAME (u6|savage|martian); unset => the full U6 path. */
typedef struct {
    const char *id;          /* MKWORLD_GAME value            */
    char        magic[4];    /* core-pack magic (per game)    */
    const char *palette;     /* palette filename in <dir>     */
    const char *font;        /* WOU proportional font (NULL = none) */
    const char *faces;       /* portrait library .LZC (NULL = none) */
    const char *talk;        /* conversation library .LZC (NULL = none) */
} GameProfile;

static const GameProfile G_PROFILES[] = {
    { "u6",      { 'U','6','W','T' }, "U6PAL", NULL,         NULL,          NULL       },
    { "savage",  { 'S','E','W','T' }, "SEPAL", "SAVAGE.FNT", "FACES.LZC",   "TALK.LZC" },
    { "martian", { 'M','D','W','T' }, "MDPAL", "FONTS.LZC",  "MDFACES.LZC", "TALK.LZC" },
};

static const GameProfile *profile_for(const char *id)
{
    if (!id) return NULL;
    for (unsigned i = 0; i < sizeof G_PROFILES / sizeof G_PROFILES[0]; i++)
        if (strcmp(G_PROFILES[i].id, id) == 0) return &G_PROFILES[i];
    return NULL;
}

/* Load a CORE asset that may be wrapped three ways and return malloc'd LOGICAL
 * bytes (*outlen): (1) an .LZC single-item container — SE/MD wrap MAPTILES/
 * MASKTYPE/LZOBJBLK this way (docs/formats/lzc.md); (2) a bare LZW container —
 * the U6-style wrapping; (3) raw. `marker` is the game's LZC compressed flag
 * (U6LZC_COMP_SE/_MD). Verified host-side: SE/MD compressed core files are LZC
 * single stored items, the rest (MAP/CHUNKS/TILEINDX/OBJTILES/BASETILE/TILEFLAG)
 * are raw in all three games. */
/* ---- THE ENDING (docs/design/ending.md §0): narration from the user's END.EXE ----
 * CLEAN-ROOM: the text lives only in the user's own executable and goes straight into the
 * never-committed pack. Located STRUCTURALLY: END.EXE holds a block of thirty number words
 * ("zero".."twenty-nine": a run of >=20 consecutive short lowercase fragments without spaces);
 * the narration is the contiguous run of printable prose fragments right after it — 42 pre-wrapped
 * lines (15 pages) + the 3 congratulation lines. Returns the line count (0 = absent). */
#define EN_MAX 48
#define EN_LEN 96
static int extract_ending(const char *dir, char lines[EN_MAX][EN_LEN], char nums[30][16], int *nnum)
{
    char path[1024]; snprintf(path,sizeof path,"%s/END.EXE",dir);
    long n; uint8_t *d=slurp_path(path,NULL,&n); *nnum=0;
    if(!d){ printf("ending text: END.EXE not found -> engine notice only\n"); return 0; }
    /* 1. the number-word table: >=20 short lowercase words, NUL-padded (empty fragments between) */
    long i=0, after=-1; int run=0; long wpos[64]; int nw=0;
    while(i<n){
        long j=i; while(j<n && d[j]) j++;
        int L=(int)(j-i);
        if(L==0){ i=j+1; continue; }                              /* padding inside the table */
        int word=(L>=3 && L<=12);
        if(word) for(int k=0;k<L;k++){ uint8_t c=d[i+k]; if(!((c>='a'&&c<='z')||c=='-')){ word=0; break; } }
        if(word){ if(run<64) wpos[run]=i; run++; }
        else { if(run>=20){ after=i; break; } run=0; }
        i=j+1;
    }
    if(after<0){ printf("ending text: number-word block not found -> engine notice only\n"); free(d); return 0; }
    nw=run>64?64:run;
    { int from=nw>30?nw-30:0, cnt=0;                               /* the last 30 words = zero..twenty-nine */
      for(int k=from;k<nw;k++){ long p=wpos[k]; int L=0; while(p+L<n && d[p+L] && L<15) L++; memcpy(nums[cnt],d+p,(size_t)L); nums[cnt][L]=0; cnt++; }
      *nnum=cnt; }
    /* 2. the first narration line is the printable TAIL of the fragment after the table (it follows a
     *    pointer table with no NUL between); 3. then consecutive printable fragments, 42 lines in all */
    int nl=0;
    { long j=after; while(j<n && d[j]) j++;
      long t=j; while(t>after && d[t-1]>=32 && d[t-1]<127) t--;
      int L=(int)(j-t); if(L>0 && L<EN_LEN){ memcpy(lines[nl],d+t,(size_t)L); lines[nl][L]=0; nl++; }
      long p=j+1;
      while(p<n && nl<42){
          long q=p; while(q<n && d[q]) q++; int Lq=(int)(q-p), ok=(Lq>0 && Lq<EN_LEN);
          for(int k=0;k<Lq && ok;k++){ uint8_t c=d[p+k]; if(!(c>=32&&c<127)) ok=0; }
          if(!ok) break;
          memcpy(lines[nl],d+p,(size_t)Lq); lines[nl][Lq]=0; nl++; p=q+1;
      }
      /* 4. the congratulations: an all-capitals word (control bytes stripped), the "Thou hast completed..."
       *    sentence head, and the "Report thy feat..." tail after the ".\n" of a later fragment */
      int got=0;
      for(int f=0; f<16 && p<n && got<3; f++){
          long q=p; while(q<n && d[q]) q++; int Lq=(int)(q-p);
          char buf[EN_LEN]; int bl=0, caps=0, sp=0, nlpos=-1;
          for(int k=0;k<Lq && bl<EN_LEN-1;k++){ uint8_t c=d[p+k]; if(c=='\n' && nlpos<0) nlpos=bl; if(c>=32&&c<127){ buf[bl++]=(char)c; if(c>='A'&&c<='Z') caps++; if(c==' ') sp++; } }
          buf[bl]=0;
          if(got==0){ if(bl>=10 && caps==bl){ snprintf(lines[nl++],EN_LEN,"%s",buf); got=1; } }
          else if(got==1){ if(bl>=30 && sp>=4){ while(bl>0 && buf[bl-1]==' ') buf[--bl]=0; snprintf(lines[nl++],EN_LEN,"%s",buf); got=2; } }
          else { if(nlpos>=0 && bl-nlpos>=20){ snprintf(lines[nl++],EN_LEN,"%s",buf+nlpos); got=3; } }
          p=q+1;
      } }
    printf("ending text: extracted %d lines + %d number words from END.EXE @%#lx\n", nl, *nnum, (unsigned long)after);
    free(d); return nl;
}
/* an ILBM palette file's CMAP (256 x 8-bit RGB) as the pack's 6-bit RGB */
static int load_lbm_pal6(const char *dir, const char *name, uint8_t out[768])
{
    long n; uint8_t *d=slurp(dir,name,&n); if(!d) return 0;
    int ok=0;
    for(long i=0;i+8<=n;i++) if(!memcmp(d+i,"CMAP",4)){
        long sz=((long)d[i+4]<<24)|((long)d[i+5]<<16)|((long)d[i+6]<<8)|d[i+7];
        if(sz>=768 && i+8+768<=n){ for(int k=0;k<768;k++) out[k]=(uint8_t)(d[i+8+k]>>2); ok=1; }
        break; }
    free(d); return ok;
}

static uint8_t *load_core_asset(const char *dir, const char *name, uint8_t marker, long *outlen)
{
    long n; uint8_t *b = slurp(dir, name, &n);
    u6lzc_t lc;
    if (u6lzc_open(&lc, b, (int32_t)n, marker) && u6lzc_count(&lc) >= 1){   /* .LZC wrap */
        int32_t isz; uint8_t fl; const uint8_t *p = u6lzc_entry(&lc, 0, &isz, &fl);
        if (p){
            int32_t cap = u6lzc_is_compressed(&lc, 0) ? u6lzw_unpacked_size(p, isz) : isz;
            if (cap <= 0) cap = isz;
            uint8_t *out = malloc((size_t)cap);
            int32_t got = u6lzc_read(&lc, 0, out, cap);
            free(b);
            if (got > 0){ *outlen = got; return out; }
            printf("%s: LZC item0 decode failed\n", name); free(out); return NULL;
        }
    }
    int32_t un = u6lzw_unpacked_size(b, (int32_t)n);                        /* bare LZW (U6 style) */
    if (un > 0 && n >= 6 && b[4] == 0x00 && (b[5] & 1)){
        uint8_t *out = malloc((size_t)un);
        int32_t got = u6lzw_decompress(b, (int32_t)n, out, un);
        free(b);
        if (got == un){ *outlen = un; return out; }
        printf("%s: LZW decode failed\n", name); free(out); return NULL;
    }
    *outlen = n; return b;                                                  /* raw */
}

/* Convert a WOU proportional font (SE `SAVAGE.FNT` / MD `FONTS.LZC`, itself
 * LZC-wrapped LZW) to the U6.CH 8x8 1bpp format (256 glyphs x 8 bytes, MSB-left,
 * bit set = ink) so the engine's existing text renderer can draw it. See
 * docs/formats/fonts.md. WOU layout (decompressed): [0]=height(8) [3]=transparent
 * pixel value; [4..259] widths[256]; [260..515] glyph-offset LOW bytes + [516..771]
 * HIGH bytes -> u16 absolute glyph offset; glyph = width*8 bytes, ROW-major
 * (8 rows x width), a pixel is INK iff its byte != the transparent value. Widths
 * are <=9 (one glyph is 9); columns past 8 are clipped. Returns 1 on success. */
static int convert_wou_font(const char *dir, const char *name, uint8_t marker, uint8_t out[2048])
{
    long fl; uint8_t *f = load_core_asset(dir, name, marker, &fl);
    if (!f || fl < 772){ printf("%s: WOU font missing/short (%ld)\n", name, f ? fl : -1); return 0; }
    int transp = f[3];
    for (int ch = 0; ch < 256; ch++){
        int w = f[4 + ch];
        long g = f[260 + ch] | ((long)f[516 + ch] << 8);
        for (int r = 0; r < 8; r++){
            uint8_t bits = 0;
            for (int col = 0; col < w && col < 8; col++)
                if (g + (long)r * w + col < fl && f[g + (long)r * w + col] != transp)
                    bits |= (uint8_t)(0x80 >> col);
            out[ch * 8 + r] = bits;
        }
    }
    return 1;
}

/* Decode the SE/MD portrait library (`FACES.LZC` / `MDFACES.LZC`) and stream a
 * "CFAC" section to `out`: [4 "CFAC"][u16 nface][u16 w][u16 h][nface x w*h indices].
 * Each .LZC item IS a U6 shape file (docs/formats/shapes.md) — byte0 = size, an
 * offset table, then one shape with an rightX/leftX/upperY/lowerY header + RLE
 * pixel blocks; SE faces are 79x85. We ship every present face that matches the
 * first face's dimensions (SEPAL indices, index 0 = background). Returns nface. */
static int convert_faces(const char *dir, const char *name, uint8_t marker, FILE *out)
{
    long fl; uint8_t *b = slurp(dir, name, &fl);
    u6lzc_t lc;
    if (!u6lzc_open(&lc, b, (int32_t)fl, marker)){ printf("%s: not an .LZC\n", name); free(b); return 0; }
    int ni = u6lzc_count(&lc);
    uint8_t *px = scratch(256 * 256, 0);
    #define px_cap (256 * 256)
    int FW = 0, FH = 0, nvalid = 0;
    for (int it = 0; it < ni; it++){                     /* pass 1: max dims (faces vary 79x85/79x86) */
        int32_t sz; uint8_t flg; const uint8_t *p = u6lzc_entry(&lc, it, &sz, &flg);
        if (!p) continue;
        int w, h; if (!u6shape_decode(p, sz, 0, &w, &h, px, px_cap)) continue;
        if (w > FW) FW = w; if (h > FH) FH = h; nvalid++;
    }
    if (nvalid == 0 || FW <= 0){ printf("%s: no decodable faces\n", name); free(px); free(b); return 0; }
    /* SLOT-INDEXED bank: emit ALL ni slots at a uniform FWxFH cell so bank[i] is
     * FACES.LZC slot i (the SE actor->portrait map is face(actor N) = slot N-1;
     * see docs/formats/portraits.md). Absent/undecodable slots -> blank (index 0
     * = transparent); shorter faces are top-left padded to FH. */
    fwrite("CFAC", 1, 4, out); be16(out, (unsigned)ni); be16(out, (unsigned)FW); be16(out, (unsigned)FH);
    uint8_t *cell = scratch((size_t)FW * FH, 0);
    for (int it = 0; it < ni; it++){                     /* pass 2: one cell per slot */
        memset(cell, 0, (size_t)FW * FH);
        int32_t sz; uint8_t flg; const uint8_t *p = u6lzc_entry(&lc, it, &sz, &flg);
        int w = 0, h = 0;
        if (p && u6shape_decode(p, sz, 0, &w, &h, px, px_cap) && w <= FW && h <= FH){
            for (int i = 0; i < w * h; i++) if (px[i] == U6SHAPE_TRANSPARENT) px[i] = 0;   /* opaque cell */
            for (int y = 0; y < h; y++) memcpy(cell + (size_t)y * FW, px + (size_t)y * w, (size_t)w); }
        fwrite(cell, 1, (size_t)FW * FH, out);
    }
    printf("  +CFAC: %s -> %d slots %dx%d (%d present, %d B)\n",
           name, ni, FW, FH, nvalid, ni * FW * FH);
    #undef px_cap
    free(cell); free(px); free(b);
    return ni;
}

/* Decode the SE/MD conversation library (`TALK.LZC`) into a "CTLK" section that
 * maps ACTOR NUMBER -> {name, look-description}. Each present TALK item begins
 * [0xff][u8 actor][name...][0xf1][description...][0x0a][0x2a][0xf2 usecode...],
 * i.e. the item self-describes its actor (byte 1) — no ordering assumption. We
 * ship only the plain leading text (name + "look" description); the usecode
 * script after 0xf2 is a separate milestone (the conversation VM). Section:
 * [4 "CTLK"][u16 nact=256][u16 blob_len][256 x u16 off][blob of "name\0desc\0"].
 * off[a] = 0xffff when actor a has no conversation. See docs/design/multigame.md. */
static int convert_talk(const char *dir, const char *name, uint8_t marker, FILE *out)
{
    long fl; uint8_t *b = slurp(dir, name, &fl);
    u6lzc_t lc;
    if (!u6lzc_open(&lc, b, (int32_t)fl, marker)){ printf("%s: not an .LZC\n", name); free(b); return 0; }
    int ni = u6lzc_count(&lc);
    enum { NDEC = 1 << 17, NBLOB = 1 << 15 };
    uint8_t *dec = scratch(NDEC, 0), *blob = scratch(NBLOB, 0);
    static uint16_t off[256];
    for (int i = 0; i < 256; i++) off[i] = 0xFFFF;
    int blen = 0, nnpc = 0;
    for (int it = 0; it < ni; it++){
        int32_t sz; uint8_t flg; if (!u6lzc_entry(&lc, it, &sz, &flg)) continue;
        int32_t got = u6lzc_read(&lc, it, dec, NDEC);
        if (got < 3 || dec[0] != 0xFF) continue;
        int actor = dec[1];
        if (actor <= 0 || actor >= 256 || off[actor] != 0xFFFF) continue;   /* first wins */
        int j = 2;
        int ns = j; while (j < got && dec[j] >= 32 && dec[j] < 127) j++;     /* name run */
        int nlen = j - ns;
        while (j < got && !(dec[j] >= 32 && dec[j] < 127)) j++;              /* skip 0xf1 sep */
        int ds = j; while (j < got && dec[j] >= 32 && dec[j] < 127) j++;     /* description run */
        int dlen = j - ds;
        if (nlen <= 0) continue;
        if (blen + nlen + dlen + 2 > NBLOB) break;                          /* blob full */
        off[actor] = (uint16_t)blen;
        memcpy(blob + blen, dec + ns, (size_t)nlen); blen += nlen; blob[blen++] = 0;
        memcpy(blob + blen, dec + ds, (size_t)dlen); blen += dlen; blob[blen++] = 0;
        nnpc++;
    }
    free(dec); free(b);
    if (nnpc == 0){ printf("%s: no conversations decoded\n", name); free(blob); return 0; }
    fwrite("CTLK", 1, 4, out); be16(out, 256); be16(out, (unsigned)blen);
    for (int i = 0; i < 256; i++) be16(out, off[i]);
    fwrite(blob, 1, (size_t)blen, out);
    printf("  +CTLK: %s -> %d NPC names/descriptions (blob %d B)\n", name, nnpc, blen);
    free(blob);
    return nnpc;
}

/* Build the CORE world pack for `prof` from the game data in `dir`. Shares the
 * MAP/CHUNKS/tileset(MASKTYPE-aware)/palette/passable/objects pipeline with the
 * U6 path — only the palette filename and pack magic vary. All three games use
 * 2048 tiles and byte-identical MAP/CHUNKS geometry (verified host-side). */
static int emit_core_pack(const GameProfile *prof, const char *dir,
                          const char *out, int startx, int starty,
                          const char *objlist_path)
{
    uint8_t marker = (strcmp(prof->id, "savage") == 0)  ? U6LZC_COMP_SE
                   : (strcmp(prof->id, "martian") == 0) ? U6LZC_COMP_MD : U6LZC_COMP_SNIFF;
    if (startx == 0 && starty == 0 && objlist_path){            /* 0 0 = where the actor list puts the player */
        long ol; uint8_t *o = slurp_path(objlist_path, NULL, &ol);
        if (o && ol >= 0x106){ const uint8_t *p = o + 0x100 + 3;  /* actor 1's position (LE 3 bytes) */
            startx = p[0] | ((p[1] & 3) << 8); starty = (p[1] >> 2) | ((p[2] & 0xf) << 6); }
        free(o); }
    long mn, cn, pn, on, in, bn, tfn, mtl, mskl, uz;
    cv_step("Map and objects");
    uint8_t *mapb = slurp(dir, "MAP", &mn),          *chk = slurp(dir, "CHUNKS", &cn);
    uint8_t *palb = slurp(dir, prof->palette, &pn),  *ti  = slurp(dir, "TILEINDX.VGA", &in);
    uint8_t *bt   = slurp(dir, "BASETILE", &bn);
    on = fsize_in(dir, "OBJTILES.VGA"); if (on < 0){ printf("cannot read %s/OBJTILES.VGA\n", dir); exit(2); }
    uint8_t *tflag = slurp(dir, "TILEFLAG", &tfn);
    uint8_t *mtc = load_core_asset(dir, "MAPTILES.VGA", marker, &mtl);   /* LZC/LZW -> logical */
    uint8_t *msk = load_core_asset(dir, "MASKTYPE.VGA", marker, &mskl);
    uint8_t *od  = load_core_asset(dir, "LZOBJBLK",     marker, &uz);
    if (pn < 768)   { printf("%s too small (%ld, need >=768)\n", prof->palette, pn); return 2; }
    if (tfn < 0x1C00){ printf("TILEFLAG too small (%ld, need 7168)\n", tfn); return 2; }
    if (!mtc || !msk || !od){ printf("core asset decode failed\n"); return 2; }

    u6pal_t pal; u6pal_parse(&pal, palb, (int32_t)pn);   /* validate the palette parses */

    /* passability from TILEFLAG (shared Impassable|Wall rule) */
    static uint8_t passable[256]; compute_passable_tflag(tflag, passable);

    /* objects: LZOBJBLK -> world decode -> repack surface roots + contained chains */
    u6_object_t *objs = malloc(sizeof(u6_object_t) * MAXOBJ);
    int32_t nobj = u6obj_decode_world(od, (int32_t)uz, objs, MAXOBJ);
    free(od);
    int nsurf = 0;
    { int32_t *shipidx = malloc(sizeof(int32_t) * MAXOBJ);
      if (!shipidx){ printf("out of memory (objects)\n"); return 2; }
      for (int32_t i = 0; i < nobj; i++){
          int keep; uint16_t newpar = U6OBJ_PARENT_NONE;
          if (objs[i].parent == U6OBJ_PARENT_NONE) keep = (objs[i].z == 0);
          else { int32_t r = shipidx[objs[i].parent]; keep = (r >= 0); if (keep) newpar = (uint16_t)r; }
          shipidx[i] = keep ? nsurf : -1;
          if (keep){ objs[nsurf] = objs[i]; objs[nsurf].parent = newpar;
              if (newpar != U6OBJ_PARENT_NONE){ objs[nsurf].x = 0; objs[nsurf].y = 0; objs[nsurf].z = 0xF; }
              nsurf++; } }
      free(shipidx);
      nobj = nsurf;
      u6_object_t *fit = realloc(objs, sizeof(u6_object_t) * (size_t)(nobj ? nobj : 1));
      if (fit) objs = fit; }

    cv_step("Tiles");
    /* combined tileset (maptiles ++ objtiles), MASKTYPE-aware decode (after the objects, whose
     * decode buffer is the other big block) */
    int32_t comb_len = (int32_t)mtl + (int32_t)on;
    uint8_t *comb = malloc((size_t)comb_len);
    if (!comb){ printf("out of memory (tileset)\n"); return 2; }
    memcpy(comb, mtc, (size_t)mtl); free(mtc);
    slurp_into(dir, "OBJTILES.VGA", comb + mtl, on);
    u6tileset_t ts; u6tile_init(&ts, comb, comb_len, ti, (int32_t)in);
    if (mskl < NTILES){ printf("MASKTYPE too small (%ld, need >=%d)\n", mskl, NTILES); return 2; }
    u6tile_set_masktype(&ts, msk);

    FILE *f = fopen(out, "wb"); if (!f){ printf("cannot write %s\n", out); return 2; }
    fwrite(prof->magic, 1, 4, f);                     /* per-game core magic (SEWT/MDWT/U6WT) */
    be32(f, (unsigned long)mn); be32(f, (unsigned long)cn);
    be32(f, NTILES); be32(f, (unsigned long)nobj);
    be16(f, (unsigned)startx); be16(f, (unsigned)starty);
    be16(f, 12*60);                                   /* start_min = noon (core packs have no save clock) */
    fwrite(mapb, 1, (size_t)mn, f); fwrite(chk, 1, (size_t)cn, f);
    for (int t = 0; t < NTILES; t++){ uint8_t tile[256]; u6tile_decode(&ts, t, tile); fwrite(tile, 1, 256, f); }
    free(comb); free(msk); free(ti);
    fwrite(palb, 1, 768, f);
    fwrite(passable, 1, 256, f);
    for (int32_t i = 0; i < nobj; i++){               /* {u16 x,y,tile,obj_n,qty16,quality,parent} BE */
        int tile = u6_basetile(bt, objs[i].obj_n) + objs[i].frame;
        int contained = (objs[i].parent != U6OBJ_PARENT_NONE);
        be16(f, contained ? 0 : objs[i].x); be16(f, contained ? 0 : objs[i].y);
        be16(f, (unsigned)tile); be16(f, objs[i].obj_n);
        be16(f, objs[i].quantity); be16(f, objs[i].quality); be16(f, objs[i].parent); }
    free(objs); free(mapb); free(chk);

    /* Optional "CLOK" section (object identity): SE/MD LOOK.LZC decodes to the
     * SAME run-fill tile->name table as U6's LOOK.LZD ([u16 tile][name\0]...),
     * plus a precomputed gettable[obj_n] from TILEFLAG. Magic-guarded so the
     * core pack can grow; the loader parses whatever sections are present.
     * [4 "CLOK"][u32 look_len][look][gettable 1024]. */
    long lookl = 0; uint8_t *lookd = load_core_asset(dir, "LOOK.LZC", marker, &lookl);
    int nget = 0;
    if (lookd && lookl > 0){
        fwrite("CLOK", 1, 4, f); be32(f, (unsigned long)lookl); fwrite(lookd, 1, lookl, f);
        uint8_t gettable[1024];
        for (int o = 0; o < 1024; o++){
            uint8_t w = tflag[0x1000 + o];                 /* weight block, by obj_n */
            uint16_t base = u6_basetile(bt, (uint16_t)o);
            int toptile = (base < NTILES) && (tflag[0x800 + base] & 0x10);
            gettable[o] = (w != 0 && w != 255 && !toptile) ? 1 : 0; nget += gettable[o];
        }
        fwrite(gettable, 1, 1024, f);
    }

    /* Optional "CTF1" section: per-tile tileflags1 (2048 B; bit1 Impassable, bit2 Wall). Gives the
     * core-pack (SE/MD) engine OBJECT passability — walls/furniture/CLOSED DOORS block — and the
     * open/closed passability a USE-a-door toggle reads (docs/formats/se_md_objn.md). [4 "CTF1"][2048]. */
    if (tflag){ fwrite("CTF1", 1, 4, f); fwrite(tflag, 1, 2048, f); }

    /* Optional "CFNT" section: the game's WOU proportional font converted to the
     * U6.CH 8x8 1bpp format so the shared text renderer can draw it.
     * [4 "CFNT"][2048 font]. */
    uint8_t font8[2048]; int have_font = 0;
    if (prof->font && convert_wou_font(dir, prof->font, marker, font8)){
        fwrite("CFNT", 1, 4, f); fwrite(font8, 1, 2048, f); have_font = 1;
    }

    /* Optional "CFAC" section: the game's portrait library (79x85 U6 shapes),
     * slot-indexed so face(actor N) = FACES slot N-1 (docs/formats/portraits.md). */
    cv_step("Portraits");
    if (prof->faces) convert_faces(dir, prof->faces, marker, f);

    /* Optional "CTLK" section: per-actor NPC name + look-description from TALK.LZC. */
    cv_step("Conversation art");
    if (prof->talk) convert_talk(dir, prof->talk, marker, f);

    /* Optional "CACT" section: the initial ACTOR ROSTER. SE/MD ship NO
     * savegame/objlist (U6 does); the 256-actor roster is generated by GAME.EXE
     * at new-game. We CAPTURE it from a ScummVM new-game save via tools/nuviesave
     * (see docs/design/multigame.md + memory se-actor-capture). objlist layout
     * (DOS little-endian, objlist.md): appearance@0x400 (2B: obj_n|frame),
     * position@0x100 (3B packed x/y/z), status@0x800 (bit 0x80 = in party),
     * stats @0x900(str)/0xA00(dex)/0xB00(int)/0xE00(hp).
     * Section = [4 "CACT"][u16 nact][nact x u16 tile]
     *           [nact x {u16 x,u16 y,u8 z}][nact x {u8 hp,str,dex,int}]
     *           [u8 nparty][nparty x u8 idx].
     * Appearance obj_n+frame is resolved to an ABSOLUTE tile here (via BASETILE)
     * because the core loader stubs BASETILE. The avatar (actor 1) is planted at
     * the demo start; other party members get "no position" (z=0xff) so the
     * engine spawns them beside the avatar (party-follow, gamedemo). */
    cv_step("People");
    int have_cact = 0, cact_npc = 0, cact_party = 0;
    if (objlist_path){
        long oln; uint8_t *objl = slurp_path(objlist_path, NULL, &oln);
        if (objl && oln >= 0x600){
            enum { CN = 256 };
            static uint16_t ct[CN], cx[CN], cy[CN]; static uint8_t cz[CN];
            static uint8_t chp[CN], cst[CN], cdx[CN], cit[CN], cpl[CN];
            int cnp = 0;
            for (int i = 0; i < CN; i++){
                ct[i]=0xFFFF; cx[i]=0; cy[i]=0; cz[i]=0xFF;
                chp[i]=cst[i]=cdx[i]=cit[i]=0;
                const uint8_t *q = objl + 0x400 + i*2;          /* appearance (LE) */
                uint16_t obj_n=(uint16_t)(q[0]|((q[1]&3)<<8));
                uint8_t  fr   =(uint8_t)((q[1]&0xfc)>>2);
                int in_party  = (oln>0x800+i) && (objl[0x800+i]&0x80);
                if (obj_n){
                    int tt = u6_basetile(bt, obj_n) + fr;
                    if (tt>=0 && tt<NTILES){ ct[i]=(uint16_t)tt; cact_npc++; }
                    const uint8_t *p = objl + 0x100 + i*3;       /* position (LE 3B) */
                    cx[i]=(uint16_t)(p[0]|((p[1]&3)<<8));
                    cy[i]=(uint16_t)(((p[1]&0xfc)>>2)|((p[2]&0xf)<<6));
                    cz[i]=(uint8_t)((p[2]&0xf0)>>4);
                }
                if (oln >= 0xF00){                               /* stat blocks */
                    chp[i]=objl[0xE00+i]; cst[i]=objl[0x900+i];
                    cdx[i]=objl[0xA00+i]; cit[i]=objl[0xB00+i];
                }
                if (i>0 && in_party && cnp<CN) cpl[cnp++]=(uint8_t)i;
            }
            /* plant the avatar at the demo start; party-mates spawn beside him */
            if (ct[1]!=0xFFFF){ cx[1]=(uint16_t)startx; cy[1]=(uint16_t)starty; cz[1]=0; }
            for (int k=0;k<cnp;k++){ int a=cpl[k]; if(a!=1) cz[a]=0xFF; }
            fwrite("CACT",1,4,f); be16(f,CN);
            for(int i=0;i<CN;i++) be16(f,ct[i]);
            for(int i=0;i<CN;i++){ be16(f,cx[i]); be16(f,cy[i]); fputc(cz[i],f); }
            for(int i=0;i<CN;i++){ fputc(chp[i],f);fputc(cst[i],f);fputc(cdx[i],f);fputc(cit[i],f); }
            fputc(cnp,f); for(int i=0;i<cnp;i++) fputc(cpl[i],f);
            cact_party = cnp; have_cact = 1;
        }
        if (objl) free(objl);
    }
    fclose(f);

    printf("core pack: game=%s magic=%.4s -> %s\n", prof->id, prof->magic, out);
    printf("  MAP=%ld CHUNKS=%ld tiles=%d objects=%d palette=%s(%ld) start=(%d,%d)\n",
           mn, cn, NTILES, nobj, prof->palette, pn, startx, starty);
    if (lookl > 0)  printf("  +CLOK: look table %ld B, %d/1024 gettable obj_n\n", lookl, nget);
    if (have_font)  printf("  +CFNT: %s -> 8x8 font (2048 B)\n", prof->font);
    if (have_cact)  printf("  +CACT: %d actors placed (party=%d) from captured objlist %s\n",
                           cact_npc, cact_party, objlist_path);
    free(palb); free(bt); free(tflag); free(lookd);
    return 0;
}

int main(int argc, char **argv)
{
    /* M9: $MKWORLD_GAME selects a non-U6 core pack (savage/martian). Unset or
     * "u6" => the full U6 world pack built by the rest of main(). */
    { const char *gid = getenv("MKWORLD_GAME");
      if (gid && strcmp(gid, "u6") != 0){
          const GameProfile *prof = profile_for(gid);
          if (!prof){ printf("unknown MKWORLD_GAME '%s' (u6|savage|martian)\n", gid); return 2; }
          const char *cdir = argc > 1 ? argv[1] : "refdata/dos/savage/SAVAGE";
          const char *cout = argc > 2 ? argv[2] : "build/se.world";
          int csx = argc > 3 ? atoi(argv[3]) : 512;
          int csy = argc > 4 ? atoi(argv[4]) : 512;
          /* argv[5] (or $MKWORLD_OBJLIST) = a captured objlist (tools/nuviesave
           * output) -> ships the initial actor roster as a "CACT" section. */
          const char *cobjl = argc > 5 ? argv[5] : getenv("MKWORLD_OBJLIST");
          return emit_core_pack(prof, cdir, cout, csx, csy, cobjl);
      } }

    const char *dir = argc > 1 ? argv[1] : "refdata/dos/u6";
    const char *out = argc > 2 ? argv[2] : "build/u6.world";
    int startx = argc > 3 ? atoi(argv[3]) : 336;
    int starty = argc > 4 ? atoi(argv[4]) : 336;
    /* optional ScummVM-save mode: argv[5]=objblk stream, argv[6]=objlist (both
     * extracted by tools/nuviesave from a save). LITTLE-endian (DOS lineage); the
     * world is then built from that exact save state. (docs/formats/nuvie_save.md) */
    const char *svm_objblk  = argc > 5 ? argv[5] : NULL;
    const char *svm_objlist = argc > 6 ? argv[6] : NULL;
    const char *svm_inv     = argc > 7 ? argv[7] : NULL;   /* actor-inventory chunk (readied gear, CB3b) */
    int svm = (svm_objblk && svm_objlist) ? 1 : 0;
    /* The front-end/intro art ALWAYS goes to a separate, sectioned U6FE.PAK (dataformats/fepack.h;
     * docs/design/memory_streaming.md): the engine loads it whole on big-RAM machines or streams it
     * a group of sections at a time on 4 MB ones. (MKWORLD_SLIM used to select this; it is now the
     * only mode — one U6.PAK + U6FE.PAK pair serves ST/TT/Falcon.) */
    int slim = 1; (void)getenv("MKWORLD_SLIM");
    char fe_path[1024];                          /* the front-end pack, a sibling of the core pack */
    { const char *s=strrchr(out,'/'); int d=s?(int)(s-out)+1:0;
      snprintf(fe_path,sizeof fe_path,"%.*sU6FE.PAK",d,out); }

    long mn,cn,pn,tn,on,bn,in,zn,fn;
    cv_step("Map and objects");
    uint8_t *mapb=slurp(dir,"MAP",&mn), *chk=slurp(dir,"CHUNKS",&cn);
    uint8_t *palb=slurp(dir,"U6PAL",&pn);
    uint8_t *bt=slurp(dir,"BASETILE",&bn), *lz=slurp(dir,"LZOBJBLK",&zn);
    uint8_t *font=slurp(dir,"U6.CH",&fn);          /* 2048-byte authentic font */
    long adn; uint8_t *adat=slurp(dir,"ANIMDATA",&adn);   /* tile-animation table */
    if (fn != 2048) { printf("U6.CH is %ld bytes, expected 2048\n", fn); return 2; }

    u6pal_t pal; u6pal_parse(&pal, palb, (int32_t)pn);


    static uint8_t passable[256];   /* filled from TILEFLAG below (after it loads) */

    /* objects: decode on-map, keep surface (z==0), pre-resolve tile. Default =
     * DOS LZOBJBLK (decompress). ScummVM-save mode = the extracted surface
     * super-chunk stream (already an OBJBLK stream, uncompressed). */
    uint8_t *od; int32_t uz;
    if (svm){ long sz; od = slurp_path(svm_objblk, NULL, &sz); uz=(int32_t)sz;
        if(!od){ printf("cannot read svm objblk %s\n",svm_objblk); return 2; } }
    else { uz = u6lzw_unpacked_size(lz, (int32_t)zn); od = malloc(uz); u6lzw_decompress(lz, (int32_t)zn, od, uz); }
    free(lz);
    u6_object_t *objs = malloc(sizeof(u6_object_t)*MAXOBJ);
    int32_t nobj = u6obj_decode_world(od, uz, objs, MAXOBJ);   /* on-map + in-container chains */
    free(od);
    /* REPACK to the shipped world set: on-map surface (z==0) roots + the contained chains
     * rooted in them, remapping each parent link to the packed index (parents always precede
     * children in decode order, so one forward pass suffices). objblk.md §2 Containment. */
    int nsurf = 0, ncontained = 0;
    { int32_t *shipidx = malloc(sizeof(int32_t)*MAXOBJ);
      if (!shipidx){ printf("out of memory (objects)\n"); return 2; }
      for (int32_t i=0;i<nobj;i++){
          int keep; uint16_t newpar=U6OBJ_PARENT_NONE;
          if (objs[i].parent==U6OBJ_PARENT_NONE) keep = (objs[i].z==0);
          else { int32_t r=shipidx[objs[i].parent]; keep=(r>=0); if(keep){ newpar=(uint16_t)r; ncontained++; } }
          shipidx[i] = keep ? nsurf : -1;
          if (keep){ objs[nsurf]=objs[i]; objs[nsurf].parent=newpar;
              if (newpar!=U6OBJ_PARENT_NONE){       /* contained: raw x/y/z bits are link garbage — */
                  objs[nsurf].x=0; objs[nsurf].y=0; objs[nsurf].z=0xF; }  /* neutralize (off-surface) */
              nsurf++; }
      }
      free(shipidx);
      nobj = nsurf;
      u6_object_t *fit = realloc(objs, sizeof(u6_object_t)*(size_t)(nobj ? nobj : 1));   /* give back the rest */
      if (fit) objs = fit; }

    /* object look-names (LOOK.LZD, whole-file LZW; tile-indexed run-fill table)
     * + a precomputed gettable[obj_n] from TILEFLAG (see docs/formats/look.md,
     * tileflag.md). gettable = weight in (0,255) AND base tile not a toptile. */
    long lkn, tfn;
    uint8_t *lkc = slurp(dir,"LOOK.LZD",&lkn);
    int32_t look_len = u6lzw_unpacked_size(lkc,(int32_t)lkn);
    uint8_t *lookd = malloc((size_t)(look_len>0?look_len:1));
    if (look_len<=0 || u6lzw_decompress(lkc,(int32_t)lkn,lookd,look_len)!=look_len){
        printf("LOOK.LZD decode failed (got %d)\n", look_len); return 2; }
    free(lkc);
    uint8_t *tflag = slurp(dir,"TILEFLAG",&tfn);
    if (tfn < 0x1C00){ printf("TILEFLAG too small (%ld, need 7168)\n",tfn); return 2; }
    compute_passable_tflag(tflag, passable);   /* real terrain passability (Impassable|Wall) */
    static uint8_t gettable[1024]; int ngettable=0;
    for (int o=0;o<1024;o++){
        uint8_t w = tflag[0x1000+o];                 /* weights block, by obj_n   */
        uint16_t base = u6_basetile(bt,(uint16_t)o);
        int toptile = (base<NTILES) && (tflag[0x800+base]&0x10); /* flags2 bit4   */
        gettable[o] = (w!=0 && w!=255 && !toptile) ? 1 : 0;
        ngettable += gettable[o];
    }
    /* STACKABLE object types (objblk.md §2): the LOOK string of the object's base tile
     * carries a plural form (a '\' splits singular\plural) => quantity is 16-bit
     * qty16=(quality<<8)|qty. Build per-tile plural flags from the run-fill table,
     * then map by base tile. */
    static uint8_t tile_plural[NTILES]; static uint8_t stackable[1024]; int nstack=0;
    { int prev=-1; long pos=0;
      while (pos+2<=look_len){
          int ix = lookd[pos] | (lookd[pos+1]<<8); pos+=2; if (ix>=NTILES) break;
          const char *s=(const char*)&lookd[pos]; int sl=(int)strlen(s); pos+=sl+1;
          int pl = (strchr(s,'\\')!=NULL);
          for (int t=prev+1;t<=ix && t<NTILES;t++) tile_plural[t]=(uint8_t)pl;
          prev=ix; }
      for (int o=0;o<1024;o++){ uint16_t base=u6_basetile(bt,(uint16_t)o);
          stackable[o] = (base<NTILES) ? tile_plural[base] : 0; nstack+=stackable[o]; } }

    /* Object frame-animation table (objanim[obj_n] = frame count, 0 = static).
     * CAUTION: in U6 an object's frame_n usually encodes STATE or ORIENTATION, NOT an
     * animation sequence — a fireplace's 4 frames face 4 walls, a campfire/torch has
     * lit/unlit frames, etc. So we CANNOT just cycle every fire-named object's frames
     * (that made fireplaces "rotate"). Only objects U6 actually animates belong here,
     * and only over their animation frames. This set is GAME LOGIC (no data flag),
     * confirmed by OBSERVING ScummVM cycle the tile. Currently just the magical
     * brazier (obj_n 206: 4 colour frames, gray/orange/blue/purple — the throne-room
     * braziers visibly cycle in ScummVM). Extend per-object only after verifying the
     * frames are an animation, not state/orientation. (docs/rules/object_anim.md) */
    static const struct { int obj_n, nframes; } ANIM_OBJ[] = {
        { 0, 0 },     /* (none) — frame_n is state/orientation, not animation. Fire/brazier
                       * glow is handled by COLOUR CYCLING (CLUT), not frame swaps. */
    };
    static uint8_t objanim[1024]; int nobjanim=0;
    for (unsigned i=0;i<sizeof ANIM_OBJ/sizeof*ANIM_OBJ;i++){
        int o=ANIM_OBJ[i].obj_n, nf=ANIM_OBJ[i].nframes;
        if(o>0 && o<1024 && nf>=2 && nf<=8){ objanim[o]=(uint8_t)nf; nobjanim++; }
    }

    /* mouse cursor: U6MCGA.PTR (whole-file LZW) -> 4-byte library; item 0 = the
     * arrow pointer (a shape frame). Decode to a flat bitmap + hot spot here so
     * the runtime just blits it. See docs/formats/cursor.md. */
    long curn; uint8_t *curc = slurp(dir,"U6MCGA.PTR",&curn);
    int32_t cursz = u6lzw_unpacked_size(curc,(int32_t)curn);
    uint8_t *curd = malloc((size_t)(cursz>0?cursz:1));
    int cw=0,ch=0,chx=0,chy=0; uint8_t *cpix=NULL;
    /* decompressed PTR = [u32 filesize][u32 offsets...][shapes]; offsets are
     * absolute from file start. num = (offset[0]-4)/4. Item 0 = the arrow. */
    if (cursz>=12 && u6lzw_decompress(curc,(int32_t)curn,curd,cursz)==cursz){
        #define LE32(p) ((uint32_t)(p)[0]|((uint32_t)(p)[1]<<8)|((uint32_t)(p)[2]<<16)|((uint32_t)(p)[3]<<24))
        uint32_t off0=LE32(curd+4);
        if (off0>=8 && (int32_t)off0<cursz){
            int num=(int)((off0-4)/4);
            uint32_t i0=LE32(curd+4), i1=(num>1)?LE32(curd+8):(uint32_t)cursz;
            if ((int32_t)i1>(int32_t)i0 && (int32_t)i1<=cursz)
                cpix=decode_shape(curd+i0,(int32_t)(i1-i0),&cw,&ch,&chx,&chy);
        }
        #undef LE32
    }
    free(curc); free(curd);
    if (!cpix||cw<=0||ch<=0||cw>64||ch>64){ printf("U6MCGA.PTR cursor decode failed\n"); return 2; }

    /* authentic background: PAPER.BMP (whole-file LZW U6Bmp -> 320x200 indices) */
    long ppn; uint8_t *paperc = slurp(dir,"PAPER.BMP",&ppn);
    int32_t pun = u6lzw_unpacked_size(paperc,(int32_t)ppn);
    uint8_t *paperd = malloc((size_t)pun);
    u6lzw_decompress(paperc,(int32_t)ppn,paperd,pun); free(paperc);
    int pw=paperd[0]|(paperd[1]<<8), ph=paperd[2]|(paperd[3]<<8);
    if (pw!=320||ph!=200){ printf("PAPER.BMP %dx%d unexpected\n",pw,ph); return 2; }
    uint8_t *paper = paperd+4;                      /* 64000 pixels */


    /* NPC actors: appearance from OBJLIST (savegame), schedules from SCHEDULE.
     * We pre-resolve each actor's tile (basetile(obj_n)+frame) and ship that +
     * the raw SCHEDULE so the engine places NPCs by game time on-target. */
    #define NACT 256
    long oln; uint8_t *objl;
    if (svm){ long sz; objl=slurp_path(svm_objlist,NULL,&sz); oln=sz; }
    else { char olp[1100]; snprintf(olp,sizeof olp,"%s/SAVEGAME/OBJLIST",dir);
        objl=slurp_path(olp, "refdata/st/u6/SAVEGAME/OBJLIST", &oln); }
    long scn; uint8_t *schd = slurp(dir,"SCHEDULE",&scn);
    static uint16_t actile[NACT], actx[NACT], acty[NACT]; static uint8_t actz[NACT];
    /* per-actor stats (1 byte each => endianness-agnostic; same on ST & DOS):
     * HP @0xE00, strength @0x900, dex @0xA00, int @0xB00; in-party flag @0x800
     * (bit 0x80). See docs/formats/objlist.md. */
    static uint8_t achp[NACT], astr[NACT], adex[NACT], aint[NACT];
    static uint8_t amana[NACT];   /* current magic points @objlist 0x13f1 (Nuvie ActorManager); 0xFF = unknown */
    static uint8_t ahostile[NACT];   /* 1 = monster type hostile to the party by alignment (CB4) */
    static uint8_t acoward[NACT];    /* 1 = coward: worktype COMBAT_RETREAT, flees until struck (CB4 follow-on) */
    static uint16_t aexp[NACT];      /* experience (objlist 0xc00) for exp-based leveling (combat.md §3) */
    static uint8_t atrainer[NACT];   /* 1 = level-advancing trainer (Lord British, obj_n 409) */
    static uint8_t party[NACT]; int nparty=0;
    int nact_ok=0;
    for (int i=0;i<NACT;i++){
        uint16_t t=0xFFFF; actx[i]=0; acty[i]=0; actz[i]=0xFF;   /* 0xFF = no live position */
        achp[i]=astr[i]=adex[i]=aint[i]=0; amana[i]=0xFF; ahostile[i]=0; acoward[i]=0; aexp[i]=0; atrainer[i]=0;
        if (objl && oln>=0xF00){   /* stat arrays present (1 byte each, both byte orders) */
            achp[i]=objl[0xE00+i]; astr[i]=objl[0x900+i];
            adex[i]=objl[0xA00+i]; aint[i]=objl[0xB00+i];
            if (oln>0x13f1+i) amana[i]=objl[0x13f1+i];    /* current mana @0x13f1 (Nuvie ActorManager) */
            /* experience @objlist+0xc00 (2 bytes): LE on DOS/ScummVM, BE on ST. (combat.md §3) */
            { const uint8_t *e=objl+0xc00+i*2;
              aexp[i]=(uint16_t)(svm ? (e[0]|(e[1]<<8)) : (e[1]|(e[0]<<8))); }
            /* worktype @objlist+0x11f1 (Nuvie actor_manager). 0x07 = COMBAT_RETREAT -> coward. */
            if (oln>0x11f1+i) acoward[i]=(uint8_t)(objl[0x11f1+i]==0x07);
            if (svm && i>0 && (objl[0x800+i]&0x80) && nparty<NACT) party[nparty++]=(uint8_t)i;
        }
        if (objl && oln>=0x600){
            const uint8_t *q=objl+0x400+i*2;                     /* appearance word */
            uint16_t obj_n, fr;
            if (svm){ obj_n=(uint16_t)(q[0]|((q[1]&3)<<8)); fr=(uint8_t)((q[1]&0xfc)>>2); }   /* DOS LE */
            else    { obj_n=(uint16_t)(q[1]|((q[0]&3)<<8)); fr=(uint8_t)((q[0]&0xfc)>>2); }   /* ST  BE */
            if (obj_n){ int tt=u6_basetile(bt,obj_n)+fr; if (tt>=0 && tt<NTILES){ t=(uint16_t)tt; nact_ok++; }
                /* hostility = the AUTHENTIC U6 per-actor ALIGNMENT (objlist.md): status byte
                 * (0x800) bit 0x20 set ⇒ EVIL(0x20)/CHAOTIC(0x60) ⇒ enemy. Party=GOOD(0x40),
                 * townsfolk/citizen-gargoyles=NEUTRAL(0x00) are not hostile — so the gargoyle
                 * "peace" is handled per-actor (post-treaty gargoyles are NEUTRAL in the save).
                 * (Supersedes the old obj_n+worktype heuristic and the separate peace flag.) */
                ahostile[i]=(uint8_t)( oln>0x800+i && (objl[0x800+i]&0x20) );
                atrainer[i]=(uint8_t)(obj_n==409); }             /* Lord British: level-advancing trainer */
            if (svm){   /* live actor position (LE 3-byte packing) -> place NPCs exactly */
                const uint8_t *p=objl+0x100+i*3;
                actx[i]=(uint16_t)(p[0]|((p[1]&3)<<8));
                acty[i]=(uint16_t)(((p[1]&0xfc)>>2)|((p[2]&0xf)<<6));
                actz[i]=(uint8_t)((p[2]&0xf0)>>4);
            }
        }
        actile[i]=t;
    }
    if (!objl) printf("warning: OBJLIST not found (NPCs will be absent)\n");
    if (!schd) printf("warning: SCHEDULE not found (NPCs will be absent)\n");
    if (svm && startx==0 && starty==0){ startx=actx[1]; starty=acty[1];   /* start where the Avatar (actor 1) stands */
        printf("ScummVM-save mode: start at avatar (%d,%d)\n", startx, starty); }
    else if (svm) printf("ScummVM-save mode: start overridden to (%d,%d) (avatar at %d,%d)\n", startx, starty, actx[1], acty[1]);

    /* game-time of day: from the OBJLIST clock (U6 stores it at objlist+0x1bf3 = minute,
     * +0x1bf4 = hour) — the pristine new-game OBJLIST says 08:00, a save its own time.
     * Starting at the real time keeps NPCs at their schedule-correct spots (e.g. Lord
     * British ON his throne at 08:00 for the canonical (307,352) start, instead of
     * walking to his noon dining spot). Noon only if there is no OBJLIST at all;
     * $START_MIN below overrides either. */
    int start_min = 12*60;
    if (objl && oln > 0x1bf5){
        int mm=objl[0x1bf3], hh=objl[0x1bf4];
        if (mm<60 && hh<24){ start_min = hh*60 + mm;
            printf("save clock: %02d:%02d (start_min=%d)\n", hh, mm, start_min); }
    }

    /* test hook: $START_MIN (minutes 0..1439) overrides the game time-of-day, for
     * validating night lighting without a night save. (docs/rules/lighting.md Stage D) */
    { const char *sm = getenv("START_MIN");
      if (sm){ int vv = atoi(sm); if (vv>=0 && vv<24*60){ start_min = vv;
          printf("START_MIN override: %02d:%02d\n", vv/60, vv%60); } } }

    /* indoor bitmap (U6WI): per-tile "under a roof" via building-enclosure flood.
     * Gates daylight to outdoors so castle interiors are dark away from torches. */
    long indoor_rle_len = 0; uint8_t *indoor_rle = NULL;   /* built once the tileset is freed, below */

    FILE *f = fopen(out, "wb"); if (!f) { printf("cannot write %s\n", out); return 2; }
    fwrite("U6WT", 1, 4, f);   /* U6WT: 14B object records {x,y,tile,obj_n,qty16,quality,parent} (quality = key id on locks/keys); U6WS 12B; U6WR gold; U6WP trainer; U6WO exp */
    be32(f,(unsigned long)mn); be32(f,(unsigned long)cn);
    be32(f, NTILES); be32(f,(unsigned long)nsurf);
    be16(f,(unsigned)startx); be16(f,(unsigned)starty);
    be16(f,(unsigned)start_min);
    fwrite(mapb,1,(size_t)mn,f); fwrite(chk,1,(size_t)cn,f);
    cv_step("Tiles");
    /* combined tileset blob + per-tile index (loaded here, at its first use, and freed after
     * U6WX band 1 below: it is the converter's biggest block) */
    uint8_t *mtc=slurp(dir,"MAPTILES.VGA",&tn), *ti=slurp(dir,"TILEINDX.VGA",&in);
    on=fsize_in(dir,"OBJTILES.VGA"); if(on<0){ printf("cannot read %s/OBJTILES.VGA\n",dir); exit(2); }
    int32_t musz = u6lzw_unpacked_size(mtc, (int32_t)tn);
    int32_t comb_len = musz + (int32_t)on;
    uint8_t *comb = malloc(comb_len);
    u6lzw_decompress(mtc, (int32_t)tn, comb, musz); free(mtc);
    slurp_into(dir, "OBJTILES.VGA", comb + musz, on);
    u6tileset_t ts; u6tile_init(&ts, comb, comb_len, ti, (int32_t)in);

    /* MASKTYPE.VGA (LZW): per-tile encoding so transparent / pixel-block tiles
     * (e.g. the map-window border tiles 432-439) decode correctly, not as raw
     * 256-byte garbage. */
    long mskn; uint8_t *mskc = slurp(dir,"MASKTYPE.VGA",&mskn);
    int32_t msksz = u6lzw_unpacked_size(mskc,(int32_t)mskn);
    uint8_t *msk = malloc((size_t)(msksz>0?msksz:1));
    if (msksz < NTILES || u6lzw_decompress(mskc,(int32_t)mskn,msk,msksz)!=msksz){
        printf("MASKTYPE.VGA decode failed (got %d, need >=%d)\n",msksz,NTILES); return 2; }
    free(mskc);
    u6tile_set_masktype(&ts, msk);
    memset(g_terr, 0, sizeof g_terr);
    for (int t=0;t<NTILES;t++){ uint8_t tile[256];          /* full combined tileset */
        if (!(t & 63)) cv_tick();
        u6tile_decode(&ts,t,tile);                          /* MASKTYPE-aware decode */
        terrain_count(tile);
        fwrite(tile,1,256,f); }
    /* U6WX band 1 (the command bar's 16) is the tileset's last use: work it out now, so the
     * 400 KB tileset is freed before the front-end art (written below U6WW) */
    static uint8_t cb_pal[48], cb_pal4[48], cb_lut[256];
    build_command_band(&ts, paperd+4, palb, cb_pal, cb_pal4, cb_lut);
    free(comb); free(msk); free(ti); comb=NULL;
    cv_step("Indoor map");
    indoor_rle = build_indoor_rle(dir, mapb, mn, chk, cn, tflag, objs, nobj, bt,
                                  lookd, look_len, startx, starty, &indoor_rle_len);
    fwrite(palb,1,768,f);
    fwrite(passable,1,256,f);
    for (int32_t i=0;i<nobj;i++){                 /* repacked world objects (surface roots + contained chains) */
        int tile = u6_basetile(bt, objs[i].obj_n) + objs[i].frame;
        int contained = (objs[i].parent != U6OBJ_PARENT_NONE);
        unsigned q16 = objs[i].quantity;          /* stackables: quality is the high byte (objblk.md §2) */
        if (stackable[objs[i].obj_n]) q16 |= (unsigned)objs[i].quality << 8;
        be16(f, contained ? 0 : objs[i].x);       /* {u16 x, y, render tile, obj_n, qty16, quality, parent} */
        be16(f, contained ? 0 : objs[i].y);       /* contained: x/y meaningless until spilled */
        be16(f, (unsigned)tile); be16(f, objs[i].obj_n);
        be16(f, q16);
        be16(f, stackable[objs[i].obj_n] ? 0 : objs[i].quality);   /* KEY ID on locks/keys (consumed by qty16 on stackables) */
        be16(f, objs[i].parent); }
    printf("objects: %d shipped (%d contained in containers; %d stackable types)\n",
           (int)nobj, ncontained, nstack);
    free(objs); free(mapb); free(chk); objs=NULL;
    fwrite(font,1,2048,f);                         /* authentic U6.CH font */
    fwrite(paper,1,64000,f);                        /* PAPER.BMP 320x200 background */
    /* sample portraits (PORTRAIT.A items 0..NPORT-1: 4-byte lib of LZW 56x64) */
    long prn; uint8_t *pra = slurp(dir,"PORTRAIT.A",&prn);
    u6lib_t plib;
    if (!u6lib_open(&plib,pra,(int32_t)prn,4,false)){ printf("PORTRAIT.A open failed\n"); return 2; }
    uint8_t (*portbuf)[56*64] = malloc((size_t)NPORT*56*64);
    if (!portbuf){ printf("out of memory (portraits)\n"); return 2; }
    for (int i=0;i<NPORT;i++){ int32_t sz; const uint8_t *blob=u6lib_entry(&plib,i,&sz);
        if (!blob||u6lzw_unpacked_size(blob,sz)!=56*64){ printf("portrait %d bad\n",i); return 2; }
        u6lzw_decompress(blob,sz,portbuf[i],56*64); }
    free(pra);                         /* read again, with PORTRAIT.B, for the conversation palettes */
    be16(f,NPORT);                                  /* portrait count, then 56x64 each */
    for (int i=0;i<NPORT;i++) fwrite(portbuf[i],1,56*64,f);
    free(portbuf);

    /* demo NPC: ship the FULL decompressed conversation script so the engine's
     * usecode VM runs it on-target. [u16 npc][u16 script_len][script bytes].
     * (Portrait = NPC-1, already in the shipped portrait set.) */
    long ccn; uint8_t *ccb = slurp(dir,"CONVERSE.A",&ccn);
    int32_t scr_len=0; uint8_t *scr=conv_script(ccb,ccn,DEMO_NPC,&scr_len); free(ccb);
    if (!scr) printf("warning: demo NPC %d script not decompressed\n", DEMO_NPC);
    be16(f,DEMO_NPC); be16(f,(unsigned)(scr?scr_len:0));
    if (scr&&scr_len>0) fwrite(scr,1,(size_t)scr_len,f);

    /* actor block: [u16 nact][nact x u16 appearance-tile]
     *              [nact x {u16 x, u16 y, u8 z}]          (live positions; z=0xFF none)
     *              [nact x {u8 hp, u8 str, u8 dex, u8 int}] (stats; U6WC)
     *              [u8 nparty][nparty x u8 actor-id]       (saved party; 0 if none)
     *              [u16 sched_len][SCHEDULE] */
    be16(f,NACT);
    for (int i=0;i<NACT;i++) be16(f,actile[i]);
    for (int i=0;i<NACT;i++){ be16(f,actx[i]); be16(f,acty[i]); fputc(actz[i],f); }
    for (int i=0;i<NACT;i++){ fputc(achp[i],f); fputc(astr[i],f); fputc(adex[i],f); fputc(aint[i],f); fputc(amana[i],f); }  /* U6WC now 5B/actor: +current mana */
    fputc(nparty,f); for (int i=0;i<nparty;i++) fputc(party[i],f);
    be16(f,(unsigned)((schd&&scn>0)?scn:0));
    if (schd&&scn>0) fwrite(schd,1,(size_t)scn,f);

    /* object verbs: [u32 look_len][LOOK payload][gettable 1024 by obj_n] */
    be32(f,(unsigned long)look_len);
    fwrite(lookd,1,(size_t)look_len,f); free(lookd);
    fwrite(gettable,1,1024,f);

    /* mouse cursor: [u8 w][u8 h][u8 hotx][u8 hoty][w*h pixels] (255=transparent) */
    fputc(cw,f); fputc(ch,f); fputc(chx,f); fputc(chy,f);
    fwrite(cpix,1,(size_t)cw*ch,f);

    cv_step("World tables");
    /* BASETILE (2048 bytes = 1024 obj_n x u16 LE): obj_n -> base tile, so the
     * engine can recover an object's frame (tile - base) for frame-aware use
     * (doors) and future animation. Shipped verbatim (little-endian). */
    fwrite(bt,1,(size_t)(bn<2048?bn:2048),f);
    if(bn<2048){ for(long k=bn;k<2048;k++) fputc(0,f); }
    free(bt);

    /* ANIMDATA tile-animation table (U6WD): [u16 nanim] then nanim x
     * {u16 dest_tile, u16 src_base, u8 nframes, u8 speed}. The engine fills the
     * (blank) dest slot from src..src+nframes-1, cycling on its anim timer.
     * Source is little-endian: count u16, then tile[32] u16, src[32] u16, frames[32]
     * u8, speed[32] u8. (docs/formats/animdata.md) */
    int nanim=0;
    if (adat && adn>=194){
        nanim=adat[0]|(adat[1]<<8); if(nanim>32) nanim=32;
        const uint8_t *at=adat+2, *as=adat+2+32*2, *af=adat+2+32*4, *asp=adat+2+32*5;
        be16(f,(unsigned)nanim);
        for (int i=0;i<nanim;i++){
            be16(f, at[i*2]|(at[i*2+1]<<8));        /* dest tile  */
            be16(f, as[i*2]|(as[i*2+1]<<8));        /* src base   */
            fputc(af[i],f); fputc(asp[i],f);        /* nframes, speed */
        }
    } else { be16(f,0); }

    /* object frame-animation table (U6WE): objanim[1024] = per-obj_n frame count
     * (0 = static). The engine cycles frame_n for fire/flame objects. */
    fwrite(objanim,1,1024,f);

    /* tileflags2[2048] (U6WF): per-TILE render flags (bit4 Foreground/toptile,
     * bits6-7 multi-tile size). The renderer uses these for the foreground pass
     * and double-width/height objects. Shipped verbatim. (docs/formats/tileflag.md) */
    fwrite(tflag+0x800,1,2048,f);

    /* tileflags1[2048] (U6WG): per-TILE movement flags (bit1 Impassable, bit2 Wall).
     * The engine builds an object-blocking map so NPCs/player can't walk through
     * impassable OBJECTS (walls/furniture/closed doors). (docs/formats/tileflag.md) */
    fwrite(tflag+0x000,1,2048,f);

    /* indoor bitmap (U6WI): [u32 rle_len][RLE pairs {u16 count, u8 value}].
     * 1 bit/tile (idx=y*1024+x); decodes to 131072 bytes. See docs/rules/lighting.md. */
    be32(f,(unsigned long)indoor_rle_len);
    if (indoor_rle && indoor_rle_len>0) fwrite(indoor_rle,1,(size_t)indoor_rle_len,f);
    free(indoor_rle);

    /* darkening LUTs (U6WJ): NLEV x 256 bytes for the lighting overlay (lighting.md §3) */
    { static uint8_t darklut[NLEV][256]; build_darklut(palb, darklut);
      fwrite(darklut, 1, NLEV*256, f); }

    /* roof drawing data (U6WK): [u16 n_rtiles][n_rtiles*256 roof tiles (255=transparent)]
     * [u16 nroof][nroof x {u32 pos=y*1024+x, u16 dense roof-tile idx}]. The engine overdraws
     * roofs over roofed cells when the avatar is OUTSIDE. (docs/rules/lighting.md Stage E) */
    { int n_rtiles=0; uint8_t *roof_map=NULL; long roof_map_len=0;
      uint8_t *roof_tiles=build_roof(dir,&n_rtiles,&roof_map,&roof_map_len);
      be16(f,(unsigned)n_rtiles);
      if (roof_tiles && n_rtiles>0) fwrite(roof_tiles,1,(size_t)n_rtiles*256,f);
      be16(f,(unsigned)(roof_map_len/6));
      if (roof_map && roof_map_len>0) fwrite(roof_map,1,(size_t)roof_map_len,f);
      free(roof_tiles); free(roof_map); }

    /* per-actor readied combat gear (U6WL): 256 x {u16 weapon obj_n, u8 armour class}
     * parsed from the save's actor-inventory chunk. (docs/rules/combat.md CB3b) */
    { static uint16_t gw[256]; static uint8_t gac[256];
      build_actor_gear(svm_inv, gw, gac);
      for (int i=0;i<256;i++){ be16(f,gw[i]); fputc(gac[i],f); } }

    /* per-actor READIED ITEM LIST (U6WQ, magic-guarded — docs/rules/equipment.md §6): the actual
     * items each actor has equipped in the source save, so the engine can ready/unready live
     * (U6WL above stays as the folded weapon/AC for older engines; the engine re-folds from this).
     * [4 "U6WQ"][u16 n] + n x [u8 actor][u8 count][count x u16 obj_n]. */
    { fwrite("U6WQ",1,4,f);
      long npos=ftell(f); be16(f,0);
      int nact_q=0, nitems_q=0;
      if(svm_inv){ long invn; uint8_t *inv=slurp_path(svm_inv,NULL,&invn);
        if(inv && invn>=2){ int icount=inv[0]|(inv[1]<<8);
          for(int a=0;a<256;a++){ uint16_t items[8]; int ni=0;
            long q=2;
            for(int o=0;o<icount && q+8<=invn;o++,q+=8){ const uint8_t*b=inv+q;
              if((b[0]&0x18)!=0x18) continue;                    /* readied only (objlist.md §3b) */
              int owner=b[1]|((b[2]&0x03)<<8); if(owner!=a) continue;
              int obj_n=b[4]|((b[5]&0x03)<<8);
              if(ni<8) items[ni++]=(uint16_t)obj_n; }
            if(ni){ fputc(a,f); fputc(ni,f); for(int k=0;k<ni;k++) be16(f,items[k]);
                    nact_q++; nitems_q+=ni; } } }
        free(inv); }
      long endq=ftell(f); fseek(f,npos,SEEK_SET); be16(f,(unsigned)nact_q); fseek(f,endq,SEEK_SET);
      printf("readied lists (U6WQ): %d actors, %d items\n", nact_q, nitems_q); }
    /* STACKABLE object types (U6W5, magic-guarded): 1024-bit bitmap by obj_n (bit set = the type
     * stacks, its record count is qty16 = quality<<8 | qty — objblk.md §2). The on-target save
     * writer needs it to emit correct pack records (save_load.md §6). */
    { fwrite("U6W5",1,4,f);
      for(int o=0;o<1024;o+=8){ int b=0; for(int k=0;k<8;k++) if(stackable[o+k]) b|=1<<k; fputc(b,f); } }

    /* object WEIGHTS by obj_n (U6WB, magic-guarded): the TILEFLAG weight block verbatim (tileflag.md,
     * block 0x1000) for M)ove's movable test — weight 0 = fixed scenery (rules/usecode_use.md). */
    { fwrite("U6WB",1,4,f); fwrite(tflag+0x1000,1,1024,f); }
    /* per-tile TILEFLAGS3 (U6W3, magic-guarded): the TILEFLAG block at 0x1400 verbatim (tileflag.md) — bit3 is
     * the Vanish / Replicate flag, bit1 Support, bits 6-7 the article (rules/magic_skills.md §5f). */
    { fwrite("U6W3",1,4,f); fwrite(tflag+0x1400,1,2048,f); }
    free(tflag);

    /* per-actor hostile-by-alignment flag (U6WM): 256 x u8 (combat.md §7 / CB4) */
    { int nh=0; for (int i=0;i<NACT;i++){ fputc(ahostile[i],f); nh+=ahostile[i]; }
      printf("alignment: %d/%d actors EVIL/CHAOTIC (status 0x800 bit 0x20 = enemy)\n", nh, NACT); }
    /* per-actor coward flag (U6WN): 256 x u8 — worktype COMBAT_RETREAT (combat.md §7 / CB4) */
    { int nc=0; for (int i=0;i<NACT;i++){ fputc(acoward[i],f); nc+=acoward[i]; }
      printf("cowards: %d/%d actors (worktype COMBAT_RETREAT)\n", nc, NACT); }
    /* per-actor experience (U6WO): 256 x u16 BE — for exp-based leveling (combat.md §3) */
    for (int i=0;i<NACT;i++) be16(f, aexp[i]);
    printf("exp: avatar=%u party=[%u,%u,%u]\n", aexp[1], aexp[2], aexp[3], aexp[4]);
    /* per-actor trainer flag (U6WP): 256 x u8 — Lord British advances thy level (combat.md §3) */
    { int nt=0; for (int i=0;i<NACT;i++){ fputc(atrainer[i],f); nt+=atrainer[i]; }
      printf("trainers: %d/%d actors (Lord British)\n", nt, NACT); }
    /* party GOLD (U6WR): u16 BE — Σ obj_n-88 coins over party members (objlist.md; combat.md §3) */
    { int gold = svm ? party_gold(svm_inv, objl, oln) : 0;
      be16(f, (uint16_t)(gold>65535?65535:gold));
      printf("gold: party=%d (obj_n 88 coins)\n", gold); }
    /* avatar KARMA (U6WR cont.): u8 — objlist 0x1bf9 (objlist.md §Player fields). The ztats
     * screen shows it; conversation ADDKARMA/SUBKARMA then move it. 0 when no save (demo world). */
    { int karma = (svm && objl && oln>0x1bf9) ? objl[0x1bf9] : 0;
      fputc((uint8_t)karma, f);
      printf("karma: avatar=%d (objlist 0x1bf9)\n", karma); }
    free(objl); objl=NULL;
    /* avatar SPELLBOOK (U6WR cont.): [u8 present][u8 all][16B known-spell bitset by U6 spell_num].
     * Loaded from the save's spellbook (obj 57 -> spell 58 by quality); present=0 => engine keeps its
     * demo/implemented set. (magic_skills.md §4; Nuvie Magic::spellbook_has_spell.) */
    { uint8_t known[16]; int all=0;
      int has = svm ? scan_spellbook(svm_inv, known, &all) : 0;
      fputc((uint8_t)has, f); fputc((uint8_t)all, f);
      if(!has){ for(int i=0;i<16;i++) fputc(0,f); }
      else { for(int i=0;i<16;i++) fputc(known[i],f); }
      { int nsp=0; for(int i=0;i<16;i++){ uint8_t b=has?known[i]:0; while(b){ nsp+=b&1; b>>=1; } }
        printf("spellbook: present=%d all-spells=%d known=%d (obj 57->58 by quality)\n", has, all, nsp); } }
    /* avatar REAGENTS (U6WR cont.): [u8 present][8 x u8 counts] — obj 65..72 carried by the Avatar.
     * present=0 => engine keeps its seeded starter stock (magic_skills.md §2). */
    { uint8_t rc[8]={0}; int has = svm ? scan_reagents(svm_inv, rc) : 0;
      fputc((uint8_t)has, f); for(int i=0;i<8;i++) fputc(has?rc[i]:0, f);
      printf("reagents: present=%d [%d,%d,%d,%d,%d,%d,%d,%d] (obj 65-72 carried by Avatar)\n",
             has, rc[0],rc[1],rc[2],rc[3],rc[4],rc[5],rc[6],rc[7]); }
    long o_frontend = ftell(f);   /* footprint: front-end/intro art starts here (U6WU..U6WV) */
    /* The slim ST pack OMITS the whole front-end/intro art block (~1.35 MB) to fit a real 4 MB ST —
     * gameplay never uses it — and writes a "NOFE" marker the engine loader skips. */
    /* The front-end/intro art is written to `fe`: for the full pack (TT/Falcon) that's the core pak
     * itself (inline, as before); for the slim ST pack it's a SEPARATE U6FE.PAK the engine STREAMS
     * (loads, runs the menu/intro/chargen, frees) so it never stays resident during gameplay. The
     * core slim pak gets a "NOFE" marker in its place. U6FE = [4 "U6FE"][48 low-colour palette]
     * [256 lut][2048 font][front-end block] — the palette/lut let the ST render the front-end in the
     * game's optimised 16 while its core (U6WW) isn't loaded yet. Computed once, reused for U6WW. */
    static uint8_t g_lc_pal[48], g_lc_pal4[48], g_lc_lut[256];   /* g_lc_pal4 = STE 4-bit game 16 (U6WS) */
    build_lowcolor(palb, g_lc_pal, g_lc_pal4, g_lc_lut);   /* the game 16 + 256->16 LUT (also shipped as U6WW) */
    FILE *fe = f;
    /* U6FE.PAK v2 section directory (fepack.h): [u8 nsec] + nsec x [u8 id][u32 off][u32 len], written
     * as a placeholder here and PATCHED at the end (fe_dir_close) once every section's extent is known.
     * FE_SEC(id) marks the start of section `id`; the previous section ends there. */
    long fe_dir_pos = 0, fe_sec_off[FEPACK_MAX_SEC], fe_sec_len[FEPACK_MAX_SEC]; int fe_nsec = 0, fe_cur = -1;
    for(int i=0;i<FEPACK_MAX_SEC;i++){ fe_sec_off[i]=0; fe_sec_len[i]=0; }
    static const char *const fe_label[FES_N] = { "Main menu", "Intro scenes", "Intro: TV", "Intro: moongate",
        "Portraits", "Intro text", "Gypsy", "Woods", "Storm", "Stones", "Into the gate", "Gargoyles",
        "Vellum", "Font", "Logos", "Scroll", "Ending", "Pointer" };
    #define FE_SEC(id) do { long _p=ftell(fe); if(fe_cur>=0) fe_sec_len[fe_cur]=_p-fe_sec_off[fe_cur]; \
                            cv_step(fe_label[(id)]); fe_cur=(id); fe_sec_off[fe_cur]=_p; if(fe_cur+1>fe_nsec) fe_nsec=fe_cur+1; } while(0)
    if(slim){ fwrite("NOFE",1,4,f);
        fe = fopen(fe_path,"wb");
        if(fe){ fwrite(FEPACK_MAGIC,1,4,fe); fwrite(palb,1,768,fe); fwrite(g_lc_pal,1,48,fe); fwrite(g_lc_lut,1,256,fe); fwrite(font,1,2048,fe);
                fe_dir_pos=ftell(fe); fputc(FES_N,fe); for(int i=0;i<FES_N*FEPACK_DIR_ENT;i++) fputc(0,fe);   /* placeholder dir */
                printf("front-end/intro art -> %s (sectioned: %d sections + base palette + ST 16 + font)\n", fe_path, (int)FES_N); }
        else  { fe = f; printf("WARN: cannot open %s; front-end inline\n", fe_path); } }
    {
    FE_SEC(FES_MENU);
    /* FRONT-END art (U6WU): MAINMENU.SHP shape 0 + TITLES.SHP shape 0 (shapes.md).
     * [u16 mw][u16 mh][mw*mh indices][u16 tw][u16 th][tw*th indices]. 0x0 if absent. */
    { int mw=0,mh=0,tw=0,th=0;
      uint8_t *menu =load_shape(dir,"MAINMENU.SHP",0,&mw,&mh);
      uint8_t *title=load_shape(dir,"TITLES.SHP",  0,&tw,&th);
      be16(fe,(unsigned)mw); be16(fe,(unsigned)mh); if(menu &&mw>0&&mh>0) fwrite(menu, 1,(size_t)mw*mh,fe);
      be16(fe,(unsigned)tw); be16(fe,(unsigned)th); if(title&&tw>0&&th>0) fwrite(title,1,(size_t)tw*th,fe);
      /* PER-SCENE ST 16 for the TITLE SCREEN: quantise the "Ultima VI" logo + menu box (U6PAL) to
       * their own 16 so the ST renders the logo's metallic gradient instead of the terrain-16's
       * dithered grey. The menu text-highlight ramp (U6PAL 14 + gold 33..36, recoloured per option
       * by menu_highlight) is up-weighted so it keeps a distinct yellow->orange spread after the cut.
       * Shipped after the title pixels: [48 ST-3bit levels][256 lut]. (docs/design/palette_lowcolor.md) */
      { uint8_t lc[48+256];
        enum { NCOMB = 320*200*2 }; uint8_t *comb=scratch(NCOMB,0); int np=0;
        if(menu &&mw>0&&mh>0) for(int i=0;i<mw*mh;i++) comb[np++]=menu[i];
        if(title&&tw>0&&th>0) for(int i=0;i<tw*th;i++) comb[np++]=title[i];
        static const uint8_t RAMP[]={14,33,34,35,36};   /* mirror gamedemo MENU_RAMP */
        for(unsigned r=0;r<sizeof RAMP/sizeof*RAMP;r++)  /* up-weight the ramp so it keeps distinct slots */
            for(int k=0;k<600 && np<NCOMB;k++) comb[np++]=RAMP[r];
        build_scene_lc(palb, comb, np, lc); free(comb);
        fwrite(lc,1,sizeof lc,fe); }
      printf("front-end: menu %dx%d, title %dx%d (+per-scene title 16)\n", mw,mh,tw,th);
      free(menu); free(title); }
    FE_SEC(FES_INTRO);
    /* INTRO SCENES (U6WV): [u8 n] then per scene [u8 type][768B pal(6-bit)] + layers.
     *   type 0 (static hold): [u16 w=320][u16 h=200][320*200 idx]
     *   type 1 (horizontal pan): [u16 pw=640][u16 h=200][640*200 panorama]
     *                            [u16 fw=320][320*200 foreground (255 = transparent)]
     * Each scene ships its own PALETTES.INT palette (living room=1, stones/moongate=3).
     * The living room PANS (nuvie scroll_lounge): the room panorama scrolls 1x, the
     * avatar/remote foreground 2x (parallax). Others are static. (shapes.md; intro.lua.) */
    { static const struct { const char *file; ishape_t bg[6]; int nbg;
                            ishape_t fg[4]; int nfg; int pal; int type; } SC[] = {  /* type 0=static 1=pan 2=gate-rise */
        /* living room: panorama = room bg(0) + bg1(1@320, has a transparent window) with
         * the twilight field(15) placed BEHIND bg1's window (panorama 450,20 — nuvie floats
         * it in as a slow parallax layer; we bake it behind the window so it's not black).
         * Foreground = avatar/chair(13@0,63), pans at 2x. (The FINGER on the remote, 14@143,91, is
         * NOT baked: nuvie shows it only while the TV shows static = a channel change; shipped
         * separately with the TV sprite set below.) */
        {"INTRO_1.SHP", {{15,450,20},{0,0,0},{1,320,0}}, 3, {{13,0,63}}, 1, 1, 1},
        {"INTRO_3.SHP", {{0,0,0}}, 1, {{0,0,0}}, 0, 3, 0},   /* ring of standing stones (lightning-lit, obsidian present) */
        {"INTRO_3.SHP", {{5,0,0}}, 1, {{0,0,0}}, 0, 3, 2},   /* gate RISES: bg = shape 5 (obsidian gone, scene re-lit by the
                                                                gate's glow — nuvie sets bg.image=img[5] before the rise; the
                                                                gate_cover strip (shape 4) is a cut-out of THIS ground) */
      };
      int nsc = (int)(sizeof SC/sizeof SC[0]);
      fputc(nsc, fe);
      for(int i=0;i<nsc;i++){
        uint8_t pal[768]; if(!load_intpal(dir, SC[i].pal, pal)) memset(pal,0,sizeof pal);
        fputc(SC[i].type, fe);
        fwrite(pal,1,768,fe);
        uint8_t lc[48+256];                                   /* per-scene ST 16 + 256->16 LUT (after the pal) */
        if(SC[i].type != 1){                                  /* type 0 (static) or 2 (gate-rise): bg only */
          uint8_t *canvas=scratch(320*200,0);
          compose_shapes(dir, SC[i].file, SC[i].bg, SC[i].nbg, canvas, 320, 200);
          build_scene_lc(pal, canvas, 320*200, lc); fwrite(lc,1,sizeof lc,fe);
          be16(fe,320); be16(fe,200); fwrite(canvas,1,320*200,fe); free(canvas);
          printf("intro scene %d: type %d %s shape0=%d pal %d\n", i, SC[i].type, SC[i].file, SC[i].bg[0].idx, SC[i].pal);
        } else {
          uint8_t *pano=scratch(640*200,0), *fg=scratch(320*200,U6SHAPE_TRANSPARENT);   /* fg = overlay layer */
          compose_shapes(dir, SC[i].file, SC[i].bg, SC[i].nbg, pano, 640, 200);
          compose_shapes(dir, SC[i].file, SC[i].fg, SC[i].nfg, fg,   320, 200);
          /* blank the 4 LED digit cells (over the baked "88:88") so the engine can draw
           * a live ticking clock over black; leave the colon (gap between cells). */
          { static const int CX[]={0xdd,0xdd+4,0xdd+0xa,0xdd+0xe};
            for(int c=0;c<4;c++) for(int yy=0x14;yy<0x14+5;yy++)
              for(int xx=CX[c];xx<CX[c]+3;xx++) pano[yy*640+xx]=0; }
          build_scene_lc(pal, pano, 640*200, lc); fwrite(lc,1,sizeof lc,fe);   /* quantise the panorama */
          be16(fe,640); be16(fe,200); fwrite(pano,1,640*200,fe);
          be16(fe,320);              fwrite(fg,1,320*200,fe);
          free(pano); free(fg);
          printf("intro scene %d: PAN %s pal %d (640 panorama + 320 fg)\n", i, SC[i].file, SC[i].pal);
        } }
      FE_SEC(FES_SCROLL);
      /* shared narration parchment scroll (BLOCKS.SHP shape 2, 320x48) drawn at the
       * bottom over the scene, 255 = torn/transparent edges. [u16 w][u16 h][w*h]. Its own
       * section (COMMON group): the intro captions AND the §1B gargoyle scene use it. */
      { int sw=0,sh=0; uint8_t *scr=load_shape(dir,"BLOCKS.SHP",2,&sw,&sh);
        be16(fe,(unsigned)sw); be16(fe,(unsigned)sh);
        if(scr&&sw>0&&sh>0) fwrite(scr,1,(size_t)sw*sh,fe);
        printf("intro scroll: BLOCKS.SHP[2] %dx%d\n", sw, sh); free(scr); }
      FE_SEC(FES_TV);
      /* living-room TV (nuvie display_tv program interpreter — docs/design/intro_sequence.md §1):
       * the 57x37 screen at (0xE5,0x32) shows sprites INTRO_1.SHP 16+image (image 0..52) placed by
       * per-image x/y offsets, up to 5 per 50 ms step, plus generated static; the Avatar's FINGER on
       * the remote (shape 14 @143,91) shows only during static. Ship the raw sprite set + finger:
       * [u16 bx][u16 by][u8 nimg] + nimg x [u16 w][u16 h][pix] + [u16 fw][u16 fh][u16 fx][u16 fy][pix]. */
      { be16(fe,0xe5); be16(fe,0x32); int nimg=53; fputc(nimg,fe); long tb=0;
        for(int k=0;k<nimg;k++){ int w=0,h=0; uint8_t*tp=load_shape(dir,"INTRO_1.SHP",16+k,&w,&h);
          be16(fe,(unsigned)w); be16(fe,(unsigned)h); if(tp&&w>0){ fwrite(tp,1,(size_t)w*h,fe); tb+=(long)w*h; } free(tp); }
        int fw=0,fh=0; uint8_t*fp=load_shape(dir,"INTRO_1.SHP",14,&fw,&fh);
        be16(fe,(unsigned)fw); be16(fe,(unsigned)fh); be16(fe,143); be16(fe,91); if(fp&&fw>0) fwrite(fp,1,(size_t)fw*fh,fe); free(fp);
        printf("intro TV: %d program sprites (%ld bytes) + finger %dx%d\n", nimg, tb, fw, fh); }
      /* clock digits: INTRO_1.SHP 2..12 = LED glyphs 0..9 + blank (3x5), drawn over the
       * blanked cells at (0xdd,0x14) with x-steps 0,4,10,14. [u16 cx][u16 cy][u8 nd] +
       * nd x [u16 w][u16 h][w*h]. */
      { be16(fe,0xdd); be16(fe,0x14); int nd=11; fputc(nd,fe);
        for(int k=0;k<nd;k++){ int w=0,h=0; uint8_t*dg=load_shape(dir,"INTRO_1.SHP",2+k,&w,&h);
          be16(fe,(unsigned)w); be16(fe,(unsigned)h); if(dg&&w>0) fwrite(dg,1,(size_t)w*h,fe); free(dg); }
        printf("intro clock: 11 LED digits @ 0xdd,0x14\n"); }
      FE_SEC(FES_GATE);
      /* moongate rise (type-2 scene): the gate (INTRO_3.SHP shape 2) rises at x=0x7c; the
       * ground cover (shape 4 @ 0x5e,0x66) is drawn OVER it so it emerges from the earth.
       * [u16 gw][u16 gh][u16 gx][gate] [u16 cw][u16 ch][u16 cx][u16 cy][cover]. (nuvie stones.) */
      { int gw=0,gh=0; uint8_t*g=load_shape(dir,"INTRO_3.SHP",2,&gw,&gh);
        be16(fe,(unsigned)gw); be16(fe,(unsigned)gh); be16(fe,0x7c);
        if(g&&gw>0) fwrite(g,1,(size_t)gw*gh,fe); free(g);
        int cvw=0,cvh=0; uint8_t*cv=load_shape(dir,"INTRO_3.SHP",4,&cvw,&cvh);
        be16(fe,(unsigned)cvw); be16(fe,(unsigned)cvh); be16(fe,0x5e); be16(fe,0x66);
        if(cv&&cvw>0) fwrite(cv,1,(size_t)cvw*cvh,fe); free(cv);
        printf("intro moongate: gate %dx%d @124 + cover %dx%d @94,102\n", gw,gh, cvw,cvh); }
      FE_SEC(FES_CCPORT);
      /* CHARACTER-CREATION portraits (MONTAGE.SHP 0..11 = 6 female @0..5, 6 male @6..11,
       * each 56x64; montage index = gender*6 + portrait_num — nuvie create_character). The
       * new-game portrait picker renders these OPAQUELY under the boot palette (portrait skin
       * tones match; PALETTES.INT 0), and the created face doubles as the in-game Avatar
       * portrait — so uncovered pixels are baked BLACK here. [u8 n][u16 pw][u16 ph] + n x [pw*ph idx]. */
      { int nmp=12, pw0=0,ph0=0; uint8_t *p0=load_shape(dir,"MONTAGE.SHP",0,&pw0,&ph0);
        fputc(nmp,fe); be16(fe,(unsigned)pw0); be16(fe,(unsigned)ph0); free(p0);
        for(int k=0;k<nmp;k++){ int w=0,h=0; uint8_t*mp=load_shape(dir,"MONTAGE.SHP",k,&w,&h);
          if(mp){ long np=(long)w*h; for(long i=0;i<np;i++) if(mp[i]==U6SHAPE_TRANSPARENT) mp[i]=0; }   /* opaque consumer */
          if(mp&&w==pw0&&h==ph0) fwrite(mp,1,(size_t)w*h,fe);
          else { static uint8_t z[56*64]; memset(z,0,sizeof z); fwrite(z,1,(size_t)pw0*ph0,fe); }
          free(mp); }
        printf("chargen portraits: %d montage %dx%d\n", nmp, pw0, ph0); }
      FE_SEC(FES_TEXT);
      /* gypsy virtue questions (extracted from U.EXE at build time; NEVER shipped/committed):
       * [u8 nq] + nq x [u16 len][len bytes] (quote/%c stripped). 0 => engine uses generic prompts. */
      { static char qb[GQ_MAX][GQ_LEN]; int nq=extract_questions(dir, qb);
        fputc(nq,fe);
        for(int q=0;q<nq;q++){ int L=(int)strlen(qb[q]); be16(fe,(unsigned)L); fwrite(qb[q],1,(size_t)L,fe); } }
      /* intro NARRATION pages (extracted from U.EXE at build time; NEVER shipped/committed):
       * [u8 np] + np x [u16 len][len bytes]. 0 => engine uses its own paraphrase. Engine re-wraps
       * each page to 320px. Page order = living(3) storm(4) forest(1) stones(2) moongate(5). */
      { static char ib[IP_MAX][IP_LEN]; int nip=extract_intro(dir, ib);
        fputc(nip,fe);
        for(int q=0;q<nip;q++){ int L=(int)strlen(ib[q]); be16(fe,(unsigned)L); fwrite(ib[q],1,(size_t)L,fe);
          if(getenv("INTRO_DUMP")) printf("  intro[%2d] (%d) %s\n", q, L, ib[q]); } }
      /* CHARGEN CEREMONY TEXT (extracted from U.EXE at build time; NEVER shipped/committed). Layout:
       *   [u16 nameL][name][u16 genderL][gender]
       *   [u8 nApproach] + n x [u16 L][bytes]   (woods-approach narration pages)
       *   [u8 nGreet]    + n x [u16 L][bytes]   (gypsy greeting pages)
       *   [u8 nFlask]    + n x [u16 L][bytes]   (flask -> sleep -> wake outro pages)
       * All zero/empty => engine falls back to its own paraphrase. Strings keep "%s" (name splice). */
      { static chargen_text_t cg; extract_chargen(dir, &cg);
        int nl=(int)strlen(cg.name), gl=(int)strlen(cg.gender);
        be16(fe,(unsigned)nl); fwrite(cg.name,1,(size_t)nl,fe);
        be16(fe,(unsigned)gl); fwrite(cg.gender,1,(size_t)gl,fe);
        fputc(cg.napproach,fe);
        for(int q=0;q<cg.napproach;q++){ int L=(int)strlen(cg.approach[q]); be16(fe,(unsigned)L); fwrite(cg.approach[q],1,(size_t)L,fe); }
        fputc(cg.ngreet,fe);
        for(int q=0;q<cg.ngreet;q++){ int L=(int)strlen(cg.greeting[q]); be16(fe,(unsigned)L); fwrite(cg.greeting[q],1,(size_t)L,fe); }
        fputc(cg.nflask,fe);
        for(int q=0;q<cg.nflask;q++){ int L=(int)strlen(cg.flask[q]); be16(fe,(unsigned)L); fwrite(cg.flask[q],1,(size_t)L,fe); }
        fputc(cg.ngarg,fe);                                    /* §1B gargoyle-sacrifice opening pages */
        for(int q=0;q<cg.ngarg;q++){ int L=(int)strlen(cg.garg[q]); be16(fe,(unsigned)L); fwrite(cg.garg[q],1,(size_t)L,fe); } }
      write_ublk(dir, fe);                                     /* intro data blocks (U.EXE): gypsy + TV tables */
      /* NOTE: BOOK.DAT readable text moved OUT of the front-end block into the RESIDENT core section
       * U6BK (below) — on the ST the front-end (U6FE) is FREED after chargen, which nulled in-game
       * book/sign reading. Books belong with gameplay data, not the transient front-end. */
      FE_SEC(FES_GYPSY);
      /* GYPSY SCENE (character-creation "casting", PALETTES.INT 5). To run nuvie's EXACT pour
       * animation (create_character gypsy_vial_anim), we ship the RAW GYPSY.SHP shape table so
       * the engine can address arm/hand/vial/vial_liquid/pour/jar sprites by shape number, just
       * as nuvie's image_load_all("gypsy.shp") does. The bg is shape 0 @ (0,-20) ALONE — the
       * vials + jar are separate sprites drawn (and animated) by the engine. Ships pal5 + the
       * tall question parchment (BLOCKS.SHP 3). All GYPSY.SHP hotspots are 0 so nuvie's sprite
       * x,y are absolute. Layout:
       * [768 pal5][u16 gw][u16 gh][bg][u16 sw][u16 sh][scroll][u16 nsh] + nsh x [u16 w][u16 h][w*h]. */
      { static const ishape_t GYP[] = { {0,0,-20} };    /* bg only: gypsy + empty table (no vials/jar) */
        uint8_t p5[768]; if(!load_intpal(dir,5,p5)) memset(p5,0,sizeof p5);
        fwrite(p5,1,768,fe);
        uint8_t *gsc=scratch(320*200,0);
        compose_shapes(dir,"GYPSY.SHP",GYP,(int)(sizeof GYP/sizeof GYP[0]),gsc,320,200);
        { uint8_t lc[48+256]; build_scene_lc(p5,gsc,320*200,lc); fwrite(lc,1,sizeof lc,fe); }  /* gypsy ST 16 */
        be16(fe,320); be16(fe,200); fwrite(gsc,1,320*200,fe); free(gsc);
        int sw=0,sh=0; uint8_t*sc=load_shape(dir,"BLOCKS.SHP",3,&sw,&sh);
        be16(fe,(unsigned)sw); be16(fe,(unsigned)sh);
        if(sc&&sw>0&&sh>0) fwrite(sc,1,(size_t)sw*sh,fe); free(sc);
        /* RAW GYPSY.SHP shape table 0..GYP_NSH-1: [u16 nsh] + per shape [u16 w][u16 h][w*h, 255 =
         * transparent]. The engine composites the casting scene + the pour animation from these
         * (jar 16, jar_liquid 9..15, vial glass 20/23, vial_liquid 0x42+.., arm 1..6, hand 9+.., pour). */
        { enum { GYP_NSH = 200 }; be16(fe, GYP_NSH); int nshipped=0;
          for(int i=0;i<GYP_NSH;i++){ int w=0,h=0; uint8_t*sp=load_shape(dir,"GYPSY.SHP",i,&w,&h);
            if(sp&&w>0&&h>0){ be16(fe,(unsigned)w); be16(fe,(unsigned)h); fwrite(sp,1,(size_t)w*h,fe); nshipped++; }
            else { be16(fe,0); be16(fe,0); }
            free(sp); }
          printf("gypsy scene: bg 320x200 pal5 + scroll BLOCKS.SHP[3] %dx%d + %d/%d raw shapes (nuvie pour animation)\n", sw,sh,nshipped,GYP_NSH); } }
      FE_SEC(FES_WOODS);
      /* WOODS forest-path lead-in (PALETTES.INT 4): the approach to the gypsy's wagon. Panorama
       * = WOODS.SHP[0] (320 wide) + WOODS.SHP[1] (191) @x=320 = 511 wide; the engine pans it
       * left (nuvie scroll_lounge-style). [768 pal4][u16 ww][u16 wh][ww*wh]. */
      { uint8_t p4[768]; if(!load_intpal(dir,4,p4)) memset(p4,0,sizeof p4);
        fwrite(p4,1,768,fe);
        uint8_t *wpano=scratch(511*200,0);
        static const ishape_t WD[]={{0,0,0},{1,320,0}};
        compose_shapes(dir,"WOODS.SHP",WD,2,wpano,511,200);
        { uint8_t lc[48+256]; build_scene_lc(p4,wpano,511*200,lc); fwrite(lc,1,sizeof lc,fe); }  /* woods ST 16 */
        be16(fe,511); be16(fe,200); fwrite(wpano,1,511*200,fe); free(wpano);
        printf("gypsy woods: WOODS.SHP panorama 511x200 pal4\n"); }
      FE_SEC(FES_STORM);
      /* STORM WINDOW scene (INTRO_2.SHP, PALETTES.INT 2 — nuvie window_sequence/display_window): a
       * curtained, mullioned window onto a stormy pine forest, ANIMATED IN LAYERS (2026-08-16, real-
       * Falcon report: "no rain or clouds; the lightning flashes behind the trees and lights the
       * ground"). Draw order = nuvie's sprite order: sky(1) -> 2 big scrolling clouds(0) + 5 small
       * (2/3) -> lightning bolt(11-18, random, BEHIND the ground+trees) -> ground(10@76) -> trees(8)
       * -> strike(19-23 @158,114) -> 100 rain drops(4-7) -> frame(28) -> glass(26 / 27 = white flash)
       * -> door leaves(24/25). Palette 0x58/0x5a/0x5c = the ground highlights that light up blue on a
       * flash. Everything ships as raw layers (255 = transparent) so the engine composites per step:
       * [768 pal2][48+256 lc][u8 nbolt]+nbolt x [u16 w][u16 h][pix][u8 nstr][u16 sx][u16 sy]+nstr x
       * [u16 w][u16 h][pix], then in this order each as [u16 w][u16 h][pix]: sky, ground, trees,
       * cloud big, cloud 2, cloud 3, rain 4,5,6,7, frame 28, glass 26, glass 27, door 24, door 25. */
      { uint8_t p2[768]; if(!load_intpal(dir,2,p2)) memset(p2,0,sizeof p2);
        fwrite(p2,1,768,fe);
        uint8_t *sbase=scratch(320*200,0);
        static const ishape_t WIN[]={{1,0,0},{10,0,76},{8,0,0},{28,0,0},{26,57,0}};   /* for the ST 16 only */
        compose_shapes(dir,"INTRO_2.SHP",WIN,(int)(sizeof WIN/sizeof WIN[0]),sbase,320,200);
        { uint8_t lc[48+256]; build_scene_lc(p2,sbase,320*200,lc); fwrite(lc,1,sizeof lc,fe); }  /* storm ST 16 */
        free(sbase);
        /* bolts + strikes carry their HOTSPOT: nuvie draws a sprite at (x - hot_x, y - hot_y) — the
         * strikes' hotspot is the ground-splash point (e.g. 57,130), so (158,114) is where the bolt HITS;
         * the small bolts' hotspot is their right edge. [u16 w][u16 h][u16 hx][u16 hy][pix]. */
        static const int BOLT[]={11,12,13,14,15,16,17,18}; int nbolt=(int)(sizeof BOLT/sizeof BOLT[0]);
        fputc(nbolt,fe);
        for(int k=0;k<nbolt;k++){ int w=0,h=0,hx=0,hy=0; uint8_t*bp=load_shape_hot(dir,"INTRO_2.SHP",BOLT[k],&w,&h,&hx,&hy);
          be16(fe,(unsigned)w); be16(fe,(unsigned)h); be16(fe,(unsigned)hx); be16(fe,(unsigned)hy); if(bp&&w>0) fwrite(bp,1,(size_t)w*h,fe); free(bp); }
        static const int STK[]={19,20,21,22,23}; int nstk=(int)(sizeof STK/sizeof STK[0]);
        fputc(nstk,fe); be16(fe,158); be16(fe,114);     /* nuvie strike sprite position (= its ground point) */
        for(int k=0;k<nstk;k++){ int w=0,h=0,hx=0,hy=0; uint8_t*sp=load_shape_hot(dir,"INTRO_2.SHP",STK[k],&w,&h,&hx,&hy);
          be16(fe,(unsigned)w); be16(fe,(unsigned)h); be16(fe,(unsigned)hx); be16(fe,(unsigned)hy); if(sp&&w>0) fwrite(sp,1,(size_t)w*h,fe); free(sp); }
        static const int LAY[]={1,10,8,0,2,3,4,5,6,7,28,26,27,24,25}; long lbytes=0;
        for(unsigned k=0;k<sizeof LAY/sizeof LAY[0];k++){ int w=0,h=0; uint8_t*lp=load_shape(dir,"INTRO_2.SHP",LAY[k],&w,&h);
          be16(fe,(unsigned)w); be16(fe,(unsigned)h); if(lp&&w>0){ fwrite(lp,1,(size_t)w*h,fe); lbytes+=(long)w*h; } free(lp); }
        printf("storm window: pal2 + %d bolts + %d strikes + 15 animation layers (%ld bytes)\n", nbolt, nstk, lbytes); }
      FE_SEC(FES_STONES);
      /* STONES obsidian-pickup assets (INTRO_3.SHP, palette 3 — nuvie stones_sequence): the hand
       * (img 1) rises into the circle to grab the obsidian stone (img 3); afterwards the stone is
       * gone (bg img 5). The engine animates the hand rising over the stones bg. Layout:
       * [u16 hw][u16 hh][hand][u16 ow][u16 oh][obs][u16 bw][u16 bh][bg5]. */
      { int hw=0,hh=0; uint8_t*hand=load_shape(dir,"INTRO_3.SHP",1,&hw,&hh);
        be16(fe,(unsigned)hw); be16(fe,(unsigned)hh); if(hand&&hw>0) fwrite(hand,1,(size_t)hw*hh,fe); free(hand);
        int ow=0,oh=0; uint8_t*obs=load_shape(dir,"INTRO_3.SHP",3,&ow,&oh);
        be16(fe,(unsigned)ow); be16(fe,(unsigned)oh); if(obs&&ow>0) fwrite(obs,1,(size_t)ow*oh,fe); free(obs);
        int bw=0,bh=0; uint8_t*bg5=load_shape(dir,"INTRO_3.SHP",5,&bw,&bh);
        be16(fe,(unsigned)bw); be16(fe,(unsigned)bh); if(bg5&&bw>0) fwrite(bg5,1,(size_t)bw*bh,fe); free(bg5);
        /* + the "clutch the stone" hand (img 6, 114x134): after the gate rises the hand lifts the
         * obsidian to the eye (x 0x9b, y 0xc7 -> 0x44) while the whole scene scrolls up 65 px
         * (nuvie stones_sequence @911-955); appended [u16 w][u16 h][pix]. */
        int cw=0,chh=0; uint8_t*clutch=load_shape(dir,"INTRO_3.SHP",6,&cw,&chh);
        be16(fe,(unsigned)cw); be16(fe,(unsigned)chh); if(clutch&&cw>0) fwrite(clutch,1,(size_t)cw*chh,fe); free(clutch);
        printf("stones pickup: hand %dx%d + obsidian %dx%d + bg5 %dx%d + clutch hand %dx%d\n", hw,hh, ow,oh, bw,bh, cw,chh); }
      FE_SEC(FES_AVATAR);
      /* AVATAR-into-gate climax (INTRO_3.SHP frames 7..26 — nuvie stones_sequence tail): the Avatar
       * walks up into the risen moongate and dissolves. The source shapes are full 320x200 canvases
       * that are ~empty (uncovered = transparent) except for the small figure, so each is CROPPED to
       * its covered bounding box + a per-frame (ox,oy) offset — else 20x64000B blows the fast pool.
       * Engine draws each at (ox-2, oy + 0x12 - 3*i). [u8 nav] + nav x [u16 ox][u16 oy][u16 w][u16 h][w*h]. */
      { int nav=20; long apre=ftell(fe);
        fputc(nav,fe);
        long tot=0;
        for(int k=0;k<nav;k++){ int w=0,h=0; uint8_t*ap=load_shape(dir,"INTRO_3.SHP",7+k,&w,&h);
          int x0=w,y0=h,x1=-1,y1=-1;
          if(ap){ int aw=w, ah=h; const uint8_t *ar=ap;
            for(int y=0;y<ah;y++,ar+=aw){                     /* in from each edge: the middle can't move the box */
              int l=(int)cv_run(ar,aw,U6SHAPE_TRANSPARENT); if(l==aw) continue;
              int r=aw-1; while(r>x1 && ar[r]==U6SHAPE_TRANSPARENT) r--;
              if(l<x0)x0=l; if(r>x1)x1=r; if(y<y0)y0=y; if(y>y1)y1=y; } }
          if(x1<0){ x0=y0=0; x1=y1=0; }                       /* empty frame -> 1x1 at (0,0) */
          int cw=x1-x0+1, ch=y1-y0+1;
          be16(fe,(unsigned)x0); be16(fe,(unsigned)y0); be16(fe,(unsigned)cw); be16(fe,(unsigned)ch);
          for(int y=0;y<ch;y++) if(ap) fwrite(ap+(size_t)(y0+y)*w+x0,1,(size_t)cw,fe);
                                else { uint8_t z=U6SHAPE_TRANSPARENT; fwrite(&z,1,1,fe); }
          tot+=(long)cw*ch; free(ap); }
        printf("avatar climax: %d cropped frames, %ld px total (%ld bytes)\n", nav, tot, ftell(fe)-apre); } }
      FE_SEC(FES_GARG);
      /* GARGOYLE-SACRIFICE opening (§1B, docs/design/chargen_authentic.md §1B.1): the RAW 13-shape
       * INTRO.SHP table + PALETTES.INT 7 + the two extra BLOCKS.SHP scrolls (0 small / 1 short;
       * shape 2 already ships as the shared narration scroll). The engine composites the animated
       * scene per frame from the sprite list — no baked canvas, nearly every layer moves.
       * [768 pal7][48+256 ST lc][u16 nsh] + nsh x [u16 w][u16 h][w*h] + 2 x [u16 w][u16 h][w*h]. */
      { enum { GG_NSH = 13 };
        uint8_t p7[768]; if(!load_intpal(dir,7,p7)) memset(p7,0,768);
        fwrite(p7,1,768,fe);
        /* ST 16: weight the quantizer with the full mid-scene tableau (bg + hordes at their
         * arrived spots + priest + bound Avatar + altar + ropes + risen gate). */
        { static const ishape_t GG[] = { {6,0,0},{3,-4,77},{4,182,79},{8,144,30},{11,222,44},
                                         {9,49,68},{5,0,112},{12,210,132},{7,120,58} };
          uint8_t *gsc=scratch(320*200,0);
          compose_shapes(dir,"INTRO.SHP",GG,(int)(sizeof GG/sizeof GG[0]),gsc,320,200);
          { uint8_t lc[48+256]; build_scene_lc(p7,gsc,320*200,lc); fwrite(lc,1,sizeof lc,fe); }
          free(gsc); }
        be16(fe, GG_NSH); { int nshipped=0; long gpre=ftell(fe);
        for(int i=0;i<GG_NSH;i++){ int w=0,h=0; uint8_t*sp=load_shape(dir,"INTRO.SHP",i,&w,&h);
          if(sp&&w>0&&h>0){ be16(fe,(unsigned)w); be16(fe,(unsigned)h); fwrite(sp,1,(size_t)w*h,fe); nshipped++; }
          else { be16(fe,0); be16(fe,0); }
          free(sp); }
        for(int b=0;b<2;b++){ int w=0,h=0; uint8_t*sc=load_shape(dir,"BLOCKS.SHP",b,&w,&h);
          if(sc&&w>0&&h>0){ be16(fe,(unsigned)w); be16(fe,(unsigned)h); fwrite(sc,1,(size_t)w*h,fe); }
          else { be16(fe,0); be16(fe,0); }
          free(sc); }
        printf("gargoyle scene: pal7 + %d/%d raw INTRO.SHP shapes + blocks 0/1 (%ld bytes, animated §1B)\n",
               nshipped, GG_NSH, ftell(fe)-gpre); } }
      FE_SEC(FES_VELLUM);
      /* CHARGEN VELLUM screens (nuvie create_character; chargen_authentic.md §3a-3c): VELLUM1.SHP
       * shape 0 = the 284x118 parchment drawn at (16,80) under the title logo, + the four button
       * LABELS of the portrait screen: 3 Sex (58x16 @94,112), 18 Portrait (90x16 @62,129),
       * 16 Continue (66x16 @86,146), 4 Exit (72x16 @80,163). Menu palette (U6PAL ~ PALETTES.INT 0).
       * [48+256 ST lc][u8 n=5] + n x [u16 w][u16 h][w*h] (255 = transparent). */
      { static const int VEL[5]={0,3,18,16,4}; static const int VX[5]={16,94,62,86,80}, VY[5]={80,112,129,146,163};
        uint8_t *vsc=scratch(320*200,0);
        uint8_t *vp[5]; int vw[5],vh[5];
        for(int k=0;k<5;k++){ vw[k]=vh[k]=0; vp[k]=load_shape(dir,"VELLUM1.SHP",VEL[k],&vw[k],&vh[k]);
          if(vp[k]){ const uint8_t *sr=vp[k]; int sw=vw[k], sh=vh[k]; uint8_t *dr=vsc+VY[k]*320L;
            for(int y=0;y<sh;y++,sr+=sw,dr+=320) if(VY[k]+y<200) for(int x=0;x<sw;x++){ uint8_t v=sr[x];
              if(v!=U6SHAPE_TRANSPARENT && VX[k]+x<320) dr[VX[k]+x]=v; } } }
        { int mw=0,mh=0; uint8_t *m0=load_shape(dir,"MONTAGE.SHP",0,&mw,&mh);   /* + a face at (195,118) */
          if(m0){ for(int y=0;y<mh;y++) for(int x=0;x<mw;x++){ int sx=195+x, sy=118+y; if(sx<320&&sy<200) vsc[sy*320+sx]=(m0[y*mw+x]==U6SHAPE_TRANSPARENT)?0:m0[y*mw+x]; } free(m0); } }
        for(int k=0;k<400;k++) vsc[k]=0x48;                     /* weight the parchment ink (0x48) */
        { uint8_t lc[48+256]; build_scene_lc(palb, vsc, 320*200, lc); fwrite(lc,1,sizeof lc,fe); }
        free(vsc);
        fputc(5,fe);
        for(int k=0;k<5;k++){ be16(fe,(unsigned)vw[k]); be16(fe,(unsigned)vh[k]);
          if(vp[k]&&vw[k]>0&&vh[k]>0) fwrite(vp[k],1,(size_t)vw[k]*vh[k],fe); free(vp[k]); }
        printf("chargen vellum: VELLUM1.SHP parchment %dx%d + 4 button labels\n", vw[0], vh[0]); }
      FE_SEC(FES_FSET);
      /* FRONT-END FONT: U6.SET (docs/formats/fonts.md "U6.SET"), the proportional font nuvie's
       * cutscene/chargen text uses (image_print) — a DIFFERENT typeface from the 8x8 U6.CH (60 of 94
       * printable glyphs differ) with per-glyph widths (1 leading blank column, 0 trailing; space=4).
       * Shipped decoded: [u8 height (0 = absent)][256 x u8 width][256 x 8 x u16 row bits, BE, bit15 =
       * leftmost]. The engine's proportional text (ptext/pwrap/fe_print) uses it for ALL front-end
       * text; U6.CH stays the in-game font. */
      { long sn=0; uint8_t *sraw=slurp(dir,"U6.SET",&sn); int ok=0;
        if(sraw && sn>4){ int32_t un=u6lzw_unpacked_size(sraw,(int32_t)sn);
          if(un>=0x304){ uint8_t *sd=malloc((size_t)un); int32_t got=u6lzw_decompress(sraw,(int32_t)sn,sd,un);
            if(got==un && sd[0]>0 && sd[0]<=8){ int h=sd[0], ink=sd[2];
              fputc(h,fe); for(int c=0;c<256;c++) fputc(sd[4+c],fe);
              for(int c=0;c<256;c++){ int w=sd[4+c]; long g=sd[0x104+c]|((long)sd[0x204+c]<<8);
                for(int r=0;r<8;r++){ unsigned bits=0;
                  if(r<h) for(int x=0;x<w&&x<16;x++){ long o=g+(long)r*w+x; if(o<un && sd[o]==ink) bits|=0x8000u>>x; }
                  fputc((bits>>8)&0xFF,fe); fputc(bits&0xFF,fe); } }
              ok=1; printf("front-end font: U6.SET %d px, %d bytes decoded (ink %d)\n", h, un, ink); }
            free(sd); } }
        free(sraw);
        if(!ok){ fputc(0,fe); printf("front-end font: U6.SET missing/undecodable -> engine falls back to U6.CH\n"); } }
      FE_SEC(FES_LOGOS);
      /* BOOT LOGOS (nuvie play(): INTRO_1.SHP 0x45 "ORIGIN Presents", 0x46 "A Lord British Production",
       * each faded in/out under PALETTES.INT 0 before the summoning story). Full-screen but mostly
       * black, so shipped run-length coded: [768 pal0] + 2 x [u16 w][u16 h][u32 rlelen][rle: (u8 run
       * 1..255, u8 value)*] over the 320x200 canvas (uncovered = black; the logo shapes use no 255). */
      { uint8_t p0[768]; if(!load_intpal(dir,0,p0)) memset(p0,0,sizeof p0);
        fwrite(p0,1,768,fe);
        static const int LOGO[2]={0x45,0x46};
        for(int k=0;k<2;k++){ int w=0,h=0; uint8_t *lp=load_shape(dir,"INTRO_1.SHP",LOGO[k],&w,&h);
          if(!lp||w<=0||h<=0){ be16(fe,0); be16(fe,0); be32(fe,0); free(lp); continue; }
          long n=0; long tot=(long)w*h; uint8_t *rle=scratch((size_t)tot*2,0);
          for(long i=0;i<tot;){ uint8_t v=lp[i]; if(v==U6SHAPE_TRANSPARENT) v=0; long r=1;
            while(i+r<tot && r<255){ uint8_t u=lp[i+r]; if(u==U6SHAPE_TRANSPARENT) u=0; if(u!=v) break; r++; }
            rle[n++]=(uint8_t)r; rle[n++]=v; i+=r; }
          be16(fe,(unsigned)w); be16(fe,(unsigned)h); be32(fe,(uint32_t)n); fwrite(rle,1,(size_t)n,fe); free(rle);
          printf("boot logo %d: INTRO_1.SHP %d %dx%d -> %ld RLE bytes\n", k, LOGO[k], w, h, n); free(lp); } }
      FE_SEC(FES_END);
      /* THE ENDING (docs/design/ending.md §2): ENDPAL.LBM as 6-bit + an ST 16 for the tableau + the RAW
       * 13-shape END.SHP table (shape 0, the see-through wall, keyed on colour 0 by the reference: its
       * index-0 pixels become 255 = transparent) + the narration lines and number words extracted from
       * the user's END.EXE (never shipped). [768 pal][48+256 lc][u8 nsh] + nsh x [u16 w][u16 h][w*h]
       * + [u8 nlines] + n x [u16 len][bytes] + [u8 nnum] + n x [u16 len][bytes]. */
      { enum { EN_NSH=13 };
        uint8_t ep[768]; if(!load_lbm_pal6(dir,"ENDPAL.LBM",ep)) memset(ep,0,768);
        fwrite(ep,1,768,fe);
        { static const ishape_t ES[] = { {7,0,0},{8,0,0},{3,9,30},{10,1,133} };   /* chamber + tableau + gate + scroll */
          uint8_t *esc=scratch(320*200,0);
          compose_shapes(dir,"END.SHP",ES,(int)(sizeof ES/sizeof ES[0]),esc,320,200);
          uint8_t lc[48+256]; build_scene_lc(ep,esc,320*200,lc); fwrite(lc,1,sizeof lc,fe); free(esc); }
        fputc(EN_NSH,fe); long epre=ftell(fe); int nshipped=0;
        for(int i=0;i<EN_NSH;i++){ int w=0,h=0; uint8_t*sp=load_shape(dir,"END.SHP",i,&w,&h);
          if(sp&&w>0&&h>0){ if(i==0){ long np=(long)w*h; for(long k=0;k<np;k++) if(sp[k]==0) sp[k]=U6SHAPE_TRANSPARENT; }
            be16(fe,(unsigned)w); be16(fe,(unsigned)h); fwrite(sp,1,(size_t)w*h,fe); nshipped++; }
          else { be16(fe,0); be16(fe,0); }
          free(sp); }
        static char el[EN_MAX][EN_LEN]; static char en[30][16]; int nnum=0; int nl=extract_ending(dir,el,en,&nnum);
        fputc(nl,fe); for(int q=0;q<nl;q++){ int L=(int)strlen(el[q]); be16(fe,(unsigned)L); fwrite(el[q],1,(size_t)L,fe); }
        fputc(nnum,fe); for(int q=0;q<nnum;q++){ int L=(int)strlen(en[q]); be16(fe,(unsigned)L); fwrite(en[q],1,(size_t)L,fe); }
        printf("ending: endpal + %d/%d END.SHP shapes + %d text lines + %d number words (%ld bytes)\n", nshipped, EN_NSH, nl, nnum, ftell(fe)-epre); }
      /* the mouse pointer for the front-end (fepack.h FES_CURSOR): the core pack's U6W9 bytes again, so the
       * main menu (which runs before the core loads) can draw it */
      FE_SEC(FES_CURSOR);
      fputc(cw,fe); fputc(ch,fe); fputc(chx,fe); fputc(chy,fe); fwrite(cpix,1,(size_t)cw*ch,fe);
    }   /* end front-end/intro art block */
    if(fe!=f){                                    /* close the section directory + the file */
        { long _p=ftell(fe); if(fe_cur>=0) fe_sec_len[fe_cur]=_p-fe_sec_off[fe_cur]; }
        fseek(fe, fe_dir_pos, SEEK_SET); fputc(FES_N,fe);
        for(int i=0;i<FES_N;i++){ fputc(i,fe); be32(fe,(unsigned long)fe_sec_off[i]); be32(fe,(unsigned long)fe_sec_len[i]); }
        fseek(fe, 0, SEEK_END);
        long biggest=0; for(int i=0;i<FES_N;i++) if(fe_sec_len[i]>biggest) biggest=fe_sec_len[i];
        long g_common=0,g_intro=0,g_cg=0;
        for(int i=0;i<FES_N;i++){ if(FEG_COMMON&(1u<<i)) g_common+=fe_sec_len[i]; if(FEG_INTRO&(1u<<i)) g_intro+=fe_sec_len[i]; if(FEG_CHARGEN&(1u<<i)) g_cg+=fe_sec_len[i]; }
        printf("U6FE.PAK: %ld KB total; groups common %ld KB / intro %ld KB / chargen %ld KB; largest section %ld KB\n",
               ftell(fe)/1024, g_common/1024, g_intro/1024, g_cg/1024, biggest/1024);
        fclose(fe); }
    #undef FE_SEC

    long o_lowcolor = ftell(f);   /* footprint: front-end/intro art ends here, low-colour starts */
    printf("footprint: core+gameplay %ld KB | front-end/intro art %ld KB (U6WU..U6WV)\n",
           o_frontend/1024, (o_lowcolor-o_frontend)/1024);
    cv_step("Colour tables");
    /* LOW-COLOUR ST palette + 256->16 LUT (U6WW): appended LAST so older loaders (which stop
     * at EOF) are unaffected, and guarded by a "U6WW" magic so the reader detects desync.
     * [4 "U6WW"][16 x {u8 r,u8 g,u8 b} ST 3-bit levels][256 x u8 lut]. The shifter backend
     * (rdr_st) consumes it; TT/Falcon/vcard ignore it (they show all 256 VGA colours). */
    /* STE 4-bit companion palettes (hoisted so U6WS can emit them after U6WY/U6WZ): band 0 = the game
     * 4-bit palette; band 1 = the command bar 4-bit; conv4 = the per-portrait 4-bit palettes. */
    static uint8_t bpal4[2*48]; static uint8_t g_conv4[256*48]; int g_nconv4=0;
    { uint8_t *pal_lvl=g_lc_pal, *lut=g_lc_lut;   /* computed once above (also shipped in U6FE) */
      fwrite("U6WW",1,4,f); fwrite(pal_lvl,1,48,f); fwrite(lut,1,256,f);

      /* BAND-RASTER low-colour (U6WX): >16 on-screen colours on the ST via a Timer-B palette split
       * (docs/design/palette_lowcolor.md). 2 horizontal bands: band 0 (rows 0..181, map+panel+status)
       * = the scene-neutral global 16 above; band 1 (rows 182..199, command bar) = a 16 tuned to the
       * command icons + parchment. SCENE-NEUTRAL (band 1's content is identical in every scene).
       * [4 "U6WX"][u8 nb][nb u8 y0][nb*48 pal 3-bit levels][nb*256 lut]. */
      { static uint8_t bpal[2*48], blut[2*256]; uint8_t y0[2]={0,182};
        memcpy(bpal, pal_lvl, 48); memcpy(blut, lut, 256);            /* band 0 = the U6WW map palette */
        memcpy(bpal4, g_lc_pal4, 48);                                 /* band 0 4-bit = the game 4-bit  */
        memcpy(bpal+48, cb_pal, 48); memcpy(bpal4+48, cb_pal4, 48); memcpy(blut+256, cb_lut, 256);  /* band 1 = command bar (3+4-bit) */
        fwrite("U6WX",1,4,f); fputc(2,f); fwrite(y0,1,2,f); fwrite(bpal,1,2*48,f); fwrite(blut,1,2*256,f); } }

    /* per-portrait CONVERSATION palettes (U6WY): the ST swaps to one while an NPC's portrait is up.
     * MUST precede U6WZ — the engine parses WW, WX, WY, WZ, WS in that order. Also fills g_conv4. */
    cv_step("Portrait palettes");
    { long prbn=0; uint8_t *prb = slurp_path("refdata/dos/u6/PORTRAIT.B", NULL, &prbn);   /* full set for U6WY conv palettes */
      if(!prb){ char pbp[1100]; snprintf(pbp,sizeof pbp,"%s/PORTRAIT.B",dir); prb=slurp_path(pbp,NULL,&prbn); }
      pra = slurp(dir,"PORTRAIT.A",&prn);
      build_conv_palettes(f, paper, palb, pra, prn, prb, prbn, g_conv4, &g_nconv4);
      free(pra); free(prb); }

    /* COLOUR-CYCLING metadata (U6WZ): which reserved ST-16 slots the engine rotates each tick to
     * animate a U6PAL ramp on 16-colour hardware. The slots are identical in band 0 (the map), which
     * copies the U6WW palette. [4 "U6WZ"][u8 ncyc][ncyc x {u8 first_slot, u8 nslots}]. fire = the top
     * ST_CYC_SLOTS slots (build_lowcolor's reserve_cycle). (palette_cycling.md.) */
    { fwrite("U6WZ",1,4,f); fputc(1,f); fputc((uint8_t)(16-ST_CYC_SLOTS),f); fputc((uint8_t)ST_CYC_SLOTS,f);
      printf("lowcolor (U6WZ): 1 cycle range -> ST slots %d..%d (fire 224-231)\n", 16-ST_CYC_SLOTS, 15); }

    /* STE 4096-colour variant (U6WS): the STE has 4 bits/channel (vs the ST's 3), so it ships a
     * PARALLEL set of 16-colour palettes snapped to the finer 4-bit gamut — same centroids + LUT as
     * the 3-bit ST palettes (they share the U6WW/U6WX/U6WY LUTs), just finer intensity. The backend
     * uses these (STE register format) on an STE; the plain ST ignores U6WS. Levels are raw 0..15 (the
     * rdr_st STE rotated-LSB packing is applied at set-palette time). Order mirrors U6WW/U6WX/U6WY:
     * [4 "U6WS"][48 game4][2*48 band4][u16 nconv][nconv*48 conv4]. (docs/design/palette_lowcolor.md) */
    { fwrite("U6WS",1,4,f); fwrite(g_lc_pal4,1,48,f); fwrite(bpal4,1,2*48,f);
      be16(f,(unsigned)g_nconv4); fwrite(g_conv4,1,(size_t)g_nconv4*48,f);
      printf("lowcolor (U6WS): STE 4-bit palettes — game + 2 bands + %d conv slots\n", g_nconv4); }

    cv_step("Books");
    /* BOOK.DAT readable text (U6BK): signs / plaques / tombstones / books / scrolls. A RESIDENT core
     * section (moved out of the front-end block, which the ST FREES with U6FE — that nulled in-game
     * reading). COPYRIGHTED game text, extracted at build time, NEVER committed. Engine indexes by
     * (object quality - 1). [4 "U6BK"][u16 nbook][nbook x [u16 len][bytes]] (\n preserved). */
    { fwrite("U6BK",1,4,f); int nb=extract_books(dir, f);
      printf("books: %d BOOK.DAT entries (U6BK, resident core section)\n", nb); }
    fclose(f);
    printf("demo NPC %d script = %d bytes; %d actors w/ tiles, SCHEDULE %ld bytes; "
           "look %d bytes, %d/1024 gettable; cursor %dx%d hot(%d,%d)\n",
           DEMO_NPC, scr?scr_len:0, nact_ok, (schd?scn:0), look_len, ngettable,
           cw, ch, chx, chy);
    printf("stats: party=%d [", nparty);
    for (int i=0;i<nparty;i++) printf("%s%d(hp%d)", i?",":"", party[i], achp[party[i]]);
    printf("]  avatar hp/str/dex/int=%d/%d/%d/%d mana=%d\n", achp[1],astr[1],adex[1],aint[1],amana[1]);
    printf("animdata: %d animated tiles%s; %d frame-animated objects\n",
           nanim, (adat&&adn>=194)?"":" (ANIMDATA missing!)", nobjanim);

    int npass=0; for(int i=0;i<256;i++) npass+=passable[i];
    printf("wrote %s: start(%d,%d) %d tiles, %d surface objects, %d/256 passable, "
           "+paper(320x200) +%d portraits\n",
           out, startx, starty, NTILES, nsurf, npass, NPORT);
    shp_flush();
    free(palb); free(font); free(adat); free(cpix); free(paperd); free(schd); free(scr);
    return 0;
}
