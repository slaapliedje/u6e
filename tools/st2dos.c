// SPDX-License-Identifier: GPL-2.0-or-later
/* st2dos.c — the ST release's files -> the DOS-shaped files mksave + mkworld read (docs/formats/st_data.md).
 * Usage: st2dos <ST folder> <out folder>. The originals are only read; the converter deletes the out folder after. Everything is explicit
 * byte order, so it runs on the host and on the 68000. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "u6lzw.h"
#include <sys/stat.h>
#include "cvhook.h"

static char g_dir[512], g_out[512];        /* the player's ST files, never written; where the DOS-shaped ones go */
static const char *in_dir(const char *name){ static char p[640]; snprintf(p,sizeof p,"%s/%s",g_out,name); return p; }
static uint8_t *slurp(const char *name,long *n){
    static char sp[640]; snprintf(sp,sizeof sp,"%s/%s",g_dir,name);
    FILE *f=fopen(sp,"rb"); if(!f) return NULL;
    fseek(f,0,SEEK_END); *n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *b=malloc((size_t)*n+1); if(b && fread(b,1,(size_t)*n,f)!=(size_t)*n){ free(b); b=NULL; }
    fclose(f); return b; }
static int spit(const char *name,const uint8_t *b,long n){
    FILE *f=fopen(in_dir(name),"wb"); if(!f){ printf("st2dos: cannot write %s\n",name); return 0; }
    long w=(long)fwrite(b,1,(size_t)n,f); fclose(f); return w==n; }
static uint32_t rbe32(const uint8_t *p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static unsigned rbe16(const uint8_t *p){ return ((unsigned)p[0]<<8)|p[1]; }
static unsigned rle16(const uint8_t *p){ return p[0]|((unsigned)p[1]<<8); }
static void wle16(uint8_t *p,unsigned v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void wle32(uint8_t *p,uint32_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void swap16s(uint8_t *b,long n){ for(long i=0;i+1<n;i+=2){ uint8_t t=b[i]; b[i]=b[i+1]; b[i+1]=t; } }

/* The U6 LZW container without compressing: 9-bit literals, a clear before the table could grow */
static long lzw_plain(const uint8_t *src,long n,uint8_t *dst){
    wle32(dst,(uint32_t)n); long o=4; uint32_t acc=0; int bits=0, run=0;
#define PUT9(c) do{ acc|=(uint32_t)(c)<<bits; bits+=9; while(bits>=8){ dst[o++]=(uint8_t)acc; acc>>=8; bits-=8; } }while(0)
    PUT9(0x100);
    for(long i=0;i<n;i++){ PUT9(src[i]); if(++run==200){ PUT9(0x100); run=0; } }
    PUT9(0x101);
    if(bits) dst[o++]=(uint8_t)acc;
    return o; }
static int spit_lzw(const char *name,const uint8_t *b,long n){
    uint8_t *z=malloc((size_t)(n+n/4+64)); if(!z) return 0;
    long zn=lzw_plain(b,n,z); int ok=spit(name,z,zn); free(z); return ok; }

/* ---- GAME.PRG: Pack-Ice, backward (st_data.md) ---- */
typedef struct { uint8_t *m; long a5,a6; int d7,x; } ice_t;
static int ice_bit(ice_t *s){
    int old=s->d7, res=(old<<1)&0xff, c=(old&0x80)?1:0; s->d7=res; s->x=c;
    if(res) return c;
    int b=s->m[--s->a5], v=(b<<1)+c; s->d7=v&0xff; s->x=(v&0x100)?1:0; return s->x; }
static unsigned ice_bits(ice_t *s,int n){ unsigned d=0; for(int i=(n&0xffff)+1;i>0;i--) d=((d<<1)|(unsigned)ice_bit(s))&0xffff; return d; }
static int sb8(int x){ x&=0xff; return x>=0x80?x-0x100:x; }
static int s16v(unsigned x){ x&=0xffff; return x>=0x8000?(int)x-0x10000:(int)x; }
static long ice_depack(const uint8_t *prg,long pn,uint8_t **out){
    if(pn<28+0x200||rbe16(prg)!=0x601a) return -1;
    long tx=(long)rbe32(prg+2); const uint8_t *t=prg+28; if(28+tx>pn) return -1;
    long a4=0x0c+(long)s16v(rbe16(t+0x0c));
    long a5=a4+(long)rbe32(t+0x12), a6=a4+(long)rbe32(t+0x1a);
    uint8_t *m=malloc((size_t)a6+16); if(!m) return -1;
    memcpy(m,t,(size_t)tx); if(a6>tx) memset(m+tx,0,(size_t)(a6-tx));
    ice_t s; s.m=m; s.a5=a5; s.a6=a6; s.d7=0; s.x=0;
    for(;;){
        if(ice_bit(&s)){ unsigned d1=0;
            if(ice_bit(&s)){ long a1=0x176; int d3=4; unsigned hi;
                for(;;){ a1-=4; uint32_t d0=rbe32(m+a1); d1=ice_bits(&s,(int)(d0&0xffff)); hi=(d0>>16)&0xffff; if((d1&0xffff)!=hi) break; if(--d3<0) break; }
                d1=(d1+rbe32(m+a1+0x14))&0xffff; }
            for(unsigned k=(d1&0xffff)+1;k>0;k--){ s.a6--; s.a5--; m[s.a6]=m[s.a5]; } }
        if(!(s.a6>a4)) break;
        int d2=3; for(;;){ if(ice_bit(&s)==0) break; if(--d2<0) break; }
        unsigned d1=0; int d0=sb8(m[0x18a+1+d2]);
        if(d0>=0) d1=ice_bits(&s,d0);
        unsigned d4=(m[0x18a+6+d2]+d1)&0xffff;
        if(d4==0){ int bits=5,add=-1; if(ice_bit(&s)){ bits=8; add=0x3f; } d1=(ice_bits(&s,bits)+(unsigned)add)&0xffff; }
        else { d2=1; for(;;){ if(ice_bit(&s)==0) break; if(--d2<0) break; }
            d0=sb8(m[0x194+1+d2]); d1=ice_bits(&s,d0);
            d1=(d1+rbe16(m+0x194+6+d2*2))&0xffff;
            if(d1&0x8000) d1=(d1-d4)&0xffff; }
        long a1=s.a6+2+s16v(d4)+s16v(d1);
        s.a6--; a1--; m[s.a6]=m[a1];
        for(unsigned k=(d4&0xffff)+1;k>0;k--){ s.a6--; a1--; m[s.a6]=m[a1]; } }
    memmove(m,m+a4,(size_t)(a6-a4)); *out=m; return a6-a4; }

/* ---- palettes ---- */
static void put_lv(uint8_t *rgb,unsigned c){ rgb[0]=(uint8_t)(((c>>8)&15)*21/5); rgb[1]=(uint8_t)(((c>>4)&15)*21/5); rgb[2]=(uint8_t)((c&15)*21/5); }
/* PALETTES.INT: 8 x 256 colours of 3 x 6 bits, LSB first */
#define VGA_MAP  0x3d65aL    /* in GAME.PRG's image: DOS colour -> ST colour, 256 bytes */
#define GAME_PAL 0x3da1eL    /* in GAME.PRG's image: the 16 colours in play */
static uint8_t g_rep[16];
static void pal_slot(uint8_t *out,int idx,int at,unsigned w){
    uint8_t *b=out+(long)idx*0x240, c[3]; put_lv(c,w);
    for(int j=0;j<3;j++){ int bit=at*18+j*6, bp=bit/8, sh=bit%8; unsigned v=(unsigned)c[j]<<sh; b[bp]|=(uint8_t)v; b[bp+1]|=(uint8_t)(v>>8); } }

/* ---- *.NEW -> *.SHP ---- */
static int new_to_shp(const char *nw,const char *shp,int band){
    long zn; uint8_t *z=slurp(nw,&zn); if(!z){ printf("st2dos: %s not found\n",nw); return 0; }
    int32_t un=u6lzw_unpacked_size(z,(int32_t)zn); uint8_t *d=malloc((size_t)un+1);
    if(!d||u6lzw_decompress(z,(int32_t)zn,d,un)!=un){ printf("st2dos: %s: bad LZW\n",nw); return 0; }
    free(z);
    int cnt=(int)(rbe32(d)/4);
    uint8_t *o=malloc((size_t)un*4+(size_t)cnt*8+4096); if(!o) return 0;       /* a pixel byte per bit, and block headers */
    long at=4+(long)cnt*4;
    static uint8_t row[1024];
    for(int i=0;i<cnt;i++){ const uint8_t *s=d+rbe32(d+i*4);
        cv_tick();
        wle32(o+4+i*4,(uint32_t)at);
        int hx=(int)rbe16(s), w1=(int)rbe16(s+2), hy=(int)rbe16(s+4), h1=(int)rbe16(s+6), pl=(int)rbe16(s+8), w=w1+1, h=h1+1, bpr=((w1>>4)+1)*2;
        wle16(o+at,(unsigned)(w1-hx)); wle16(o+at+2,(unsigned)hx); wle16(o+at+4,(unsigned)hy); wle16(o+at+6,(unsigned)(h1-hy)); at+=8;
        const uint8_t *pp[4]; const uint8_t *n=s+10; for(int k=0;k<4;k++){ pp[k]=NULL; if(pl&(1<<k)){ pp[k]=n; n+=(long)bpr*h; } }
        for(int y=0;y<h;y++){
            for(int x=0;x<w;x++){ int c=0, m=0x80>>(x&7), bx=x>>3;
                for(int k=0;k<4;k++) if(pp[k] && (pp[k][(long)y*bpr+bx]&m)) c|=1<<k;
                row[x]=(uint8_t)(c?c+band:0); }
            for(int x=0;x<w;){ if(!row[x]){ x++; continue; }          /* colour 0 is left uncovered: transparent */
                int e=x; while(e<w&&row[e]) e++;
                wle16(o+at,(unsigned)((e-x)<<1)); wle16(o+at+2,(unsigned)(uint16_t)(x-hx)); wle16(o+at+4,(unsigned)(uint16_t)(y-hy)); at+=6;
                memcpy(o+at,row+x,(size_t)(e-x)); at+=e-x; x=e; } }
        wle16(o+at,0); at+=2; }
    wle32(o,(uint32_t)at);
    int ok=spit_lzw(shp,o,at); free(o); free(d); return ok; }

/* ---- tiles ---- */
static int tiles(void){
    long mn,on,in,kn; uint8_t *mt=slurp("MAPTILES.EGA",&mn), *ob=slurp("OBJTILES.EGA",&on), *ti=slurp("TILEINDX.EGA",&in), *kz=slurp("MASKTYPE.EGA",&kn);
    if(!mt||!ob||!ti||!kz||in<4096) return 0;
    int32_t ku=u6lzw_unpacked_size(kz,(int32_t)kn); uint8_t *mk=malloc((size_t)ku+1); if(!mk||u6lzw_decompress(kz,(int32_t)kn,mk,ku)!=ku) return 0;
    uint8_t *out=malloc(2048L*256), *nix=malloc(4096), *nmk=malloc(2048); if(!out||!nix||!nmk) return 0;
    long stride=(long)rle16(mt); const uint8_t *mb=mt+2;
    for(int t=0;t<2048;t++){ unsigned v=rle16(ti+t*2); const uint8_t *buf; long o, blen;
        if(!(t&63)) cv_tick();
        if(v>=0x6000){ buf=mb; o=(long)v-0x6000; blen=mn-2; } else { buf=ob; o=(long)v*16; blen=on; }
        uint8_t *px=out+(long)t*256; int k=mk[t]&3, y0=0, rows=0, any_trans=0;
        memset(px,255,256);
        if(o+3<blen){ y0=buf[o]; rows=buf[o+1]; }
        if(y0+rows>16) rows=16-y0;
        if(rows>0 && k==0){
            for(int r=0;r<rows;r++) for(int x=0;x<16;x++){ int c=0, m=0x80>>(x&7), bx=x>>3;
                for(int p=0;p<4;p++){ long q=o+2+p*stride+r*2+bx; if(q<blen && (buf[q]&m)) c|=1<<p; }
                px[(y0+r)*16+x]=(uint8_t)c; } }
        else if(rows>0){ int wide=k==3?2:1, xoff=k==1?8:0; long p=o+3; const uint8_t *mask=buf+p, *pl[4]={0,0,0,0}; p+=(long)rows*wide;
            while(p<blen && buf[p]!=0xff){ int n=buf[p++]; if(n<4) pl[n]=buf+p; p+=(long)rows*wide; }
            for(int r=0;r<rows;r++) for(int bx=0;bx<wide*8;bx++){ int b=r*wide+(bx>>3), m=0x80>>(bx&7);
                if(mask[b]&m) continue;
                int c=0; for(int q=0;q<4;q++) if(pl[q] && (pl[q][b]&m)) c|=1<<q;
                px[(y0+r)*16+xoff+bx]=(uint8_t)c; } }
        for(int i=0;i<256;i++){ if(px[i]==255) any_trans=1; else px[i]=g_rep[px[i]&15]; }
        wle16(nix+t*2,(unsigned)(t*16)); nmk[t]=any_trans?5:0; }
    int ok=spit_lzw("MAPTILES.VGA",out,512L*256) && spit("OBJTILES.VGA",out+512L*256,1536L*256) && spit("TILEINDX.VGA",nix,4096) && spit_lzw("MASKTYPE.VGA",nmk,2048);
    free(mt); free(ob); free(ti); free(kz); free(mk); free(out); free(nix); free(nmk);
    return ok; }

/* ---- the saved game: the two libraries mksave reads ---- */
/* a block: BE count, then 8-byte records with the object and the quantity words big-endian */
static long block_in(const char *nm,uint8_t *o){
    long n; uint8_t *b=slurp(nm,&n); if(!b){ printf("st2dos: %s not found\n",nm); return -1; }
    swap16s(b,2); for(long r=2;r+8<=n;r+=8){ swap16s(b+r+4,4); }
    memcpy(o,b,(size_t)n); free(b); return n; }
static int saved(void){
    uint8_t *o=malloc(512L*1024); if(!o) return 0; long at=0, n; char nm[32];
    for(int i=0;i<64;i++){ int x=(i&1)|(((i>>2)&1)<<1)|(((i>>3)&1)<<2), y=((i>>1)&1)|(((i>>4)&3)<<1);   /* DOS's order of the blocks */
        snprintf(nm,sizeof nm,"SAVEGAME/OBJBLK%c%c",'A'+x,'A'+y);
        if((n=block_in(nm,o+at))<0) return 0;
        at+=n; }
    if(!spit_lzw("LZOBJBLK",o,at)) return 0;
    at=0;
    for(int k=0;k<5;k++){ snprintf(nm,sizeof nm,"SAVEGAME/OBJBLK%cI",'A'+k); if((n=block_in(nm,o+at))<0) return 0; at+=n; }
    { uint8_t *b=slurp("SAVEGAME/OBJLIST",&n); if(!b||n<0x1c80){ printf("st2dos: SAVEGAME/OBJLIST\n"); return 0; }
      swap16s(b+0x400,0x400); swap16s(b+0xc00,0x200); swap16s(b+0x15f1,0x200); swap16s(b+0x1bf7,2); swap16s(b+0x1bfd,4); swap16s(b+0x1c19,48);
      long keep=n>7283?7283:n; memcpy(o+at,b,(size_t)keep); at+=keep; free(b); }
    int ok=spit_lzw("LZDNGBLK",o,at); free(o); return ok; }

static int swapped(const char *name){ long n; uint8_t *b=slurp(name,&n); if(!b) return 0; swap16s(b,n); int ok=spit(name,b,n); free(b); return ok; }
/* the rest mkworld reads, as they are */
static const char *const AS_IS[]={"MAP","CHUNKS","TILEFLAG","LOOK.LZD","PAPER.BMP","NEWMAGIC.BMP","STARPOS.DAT","U6.CH","U6.SET","CONVERSE.A","CONVERSE.B","PORTRAIT.A","PORTRAIT.B","PORTRAIT.Z",0};

static int copy_as(const char *from,const char *to){ long n; uint8_t *b=slurp(from,&n); if(!b) return 0; int ok=spit(to,b,n); free(b); return ok; }

/* mkworld reads its text and tables from the DOS programs, which keep them in another order. The ST's: file offsets in
 * the known release (st_data.md); strings padded to a word; words big-endian. */
#define UT_GYPSY 94580L     /* the two intro tables, 1190 and 788 bytes */
#define UT_TV    93764L
#define UT_NARR  0x9b14L    /* the intro's narration, which DOS has right after a list of its intro files */
#define ET_ZERO  0xc398L    /* in END.PRG: the number words zero..twenty-nine, to their end */
#define ET_WEND  0xc51dL
#define ET_TEXT  0x97aL     /* the ending's narration */
#define END_PAL  0xc6f6L    /* the ending's 16 colours, from the text segment */
static long unpad(uint8_t *b,long n){       /* drop the pad bytes between strings */
    long o=0;
    for(long i=0;i<n;i++){
        if(b[i]==0 && i+2<n && b[i+1]==0 && i>0 && ((b[i-1]>=32 && b[i-1]<127)||b[i-1]==10) && ((b[i+2]>=32&&b[i+2]<127)||b[i+2]==10)) continue;
        b[o++]=b[i]; }
    return o; }
static int u_as_exe(void){
    long n; uint8_t *b=slurp("U.PRG",&n); if(!b) return 0;
    if(UT_GYPSY+1190>n||UT_NARR>=n){ free(b); printf("st2dos: U.PRG not as expected\n"); return 0; }
    swap16s(b+UT_GYPSY,1190); swap16s(b+UT_TV,788);
    int ok=spit("GYPSY.TBL",b+UT_GYPSY,1190)&&spit("TV.TBL",b+UT_TV,788);
    for(long i=0;i+5<n;i++) if(!memcmp(b+i,"intro_",6)) b[i]='I';          /* the real list of files: not the marker */
    FILE *f=fopen(in_dir("U.EXE"),"wb"); if(!f) return 0;
    long m=unpad(b,UT_NARR); fwrite(b,1,(size_t)m,f); fputc(0,f);
    for(int k=0;k<6;k++) fwrite("intro_1.new",1,12,f);                     /* the marker mkworld looks for: 12 bytes with the NUL */
    { uint8_t *t=b+UT_NARR; long tn=unpad(t,n-UT_NARR); fwrite(t,1,(size_t)tn,f); }
    fclose(f); free(b); return ok; }
static int end_as_exe(void){
    long n; uint8_t *b=slurp("END.PRG",&n); if(!b) return 0;
    if(ET_WEND>=n){ free(b); printf("st2dos: END.PRG not as expected\n"); return 0; }
    FILE *f=fopen(in_dir("END.EXE"),"wb"); if(!f) return 0;
    fwrite(b+ET_ZERO,1,(size_t)(ET_WEND-ET_ZERO),f); fputc(0,f);
    { uint8_t *t=b+ET_TEXT; long tn=unpad(t,ET_ZERO-ET_TEXT);              /* the date line, in the pieces DOS has it in */
      static const char ST[]="%s, \0%s,\n\0and \0%s.\n\0", DOS[]="%s\0, \0%s\0,\n\0and \0%s\0.\n";
      for(long i=0;i<tn;i++){ if(i+(long)sizeof ST-1<=tn && !memcmp(t+i,ST,sizeof ST-1)){ fwrite(DOS,1,sizeof DOS-1,f); i+=(long)sizeof ST-2; } else fputc(t[i],f); } }
    fclose(f); free(b); return 1; }

int main(int argc,char **argv){
    snprintf(g_dir,sizeof g_dir,"%s",argc>1?argv[1]:"ST");
    snprintf(g_out,sizeof g_out,"%s",argc>2?argv[2]:"STTMP");
    mkdir(g_out,0755);
    long n, pn;
    /* the palettes */
    uint8_t *up=slurp("U.PRG",&n), *gp=slurp("GAME.PRG",&pn), *img=NULL; if(!up||!gp){ printf("st2dos: U.PRG, GAME.PRG not found\n"); return 2; }
    long in=ice_depack(gp,pn,&img); if(in<0x3da1e + 32){ printf("st2dos: GAME.PRG not as expected\n"); return 2; }
    /* The game's own table gives each DOS colour its ST colour, so the game palette is that table through the 16 colours,
     * and the VGA-indexed art (portraits, paper) needs no change; the tiles take the first DOS index of their colour. The
     * front end has its own palette, holding the menu's and the vellum's colours as well (st_data.md). */
    { const uint8_t *tbl=up+28+0x178c6, *vga=img+VGA_MAP, *gam=img+GAME_PAL; uint8_t pal[768];
      static const uint8_t MAP[8]={0,0,9,1,3,4,6,2};               /* the DOS palette's scene -> the ST one's */
      for(int v=0;v<256;v++) put_lv(pal+v*3,rbe16(gam+(vga[v]&15)*2));
      for(int c=1;c<16;c++){ g_rep[c]=0; for(int v=48;v<224&&!g_rep[c];v++) if((vga[v]&15)==c) g_rep[c]=(uint8_t)v;
          if(!g_rep[c]){ printf("st2dos: no index for colour %d\n",c); return 2; } }
      if(!spit("U6PAL",pal,768)) return 2;
      for(int i=0;i<16;i++){ put_lv(pal+(16+i)*3,rbe16(tbl+6*32+i*2)); put_lv(pal+(32+i)*3,rbe16(tbl+5*32+i*2)); }
      if(!spit("FEPAL",pal,768)) return 2;
      uint8_t *pi=calloc(8,0x240); if(!pi) return 2;
      for(int i=0;i<8;i++) for(int c=0;c<16;c++){ pal_slot(pi,i,c,rbe16(tbl+MAP[i]*32+c*2)); pal_slot(pi,i,16+c,rbe16(tbl+6*32+c*2)); pal_slot(pi,i,32+c,rbe16(tbl+5*32+c*2)); }
      if(!spit("PALETTES.INT",pi,8L*0x240)) return 2;
      free(pi); }
    free(gp); free(img); free(up);
    if(!tiles()){ printf("st2dos: tiles failed\n"); return 2; }
    if(!saved()) return 2;
    static const struct { const char *n,*s; int band; } ART[]={ {"MAINMENU.NEW","MAINMENU.SHP",32},{"TITLE.NEW","TITLES.SHP",32},{"BLOCKS.NEW","BLOCKS.SHP",16},{"INTRO.NEW","INTRO.SHP",0},
        {"INTRO_1.NEW","INTRO_1.SHP",0},{"INTRO_2.NEW","INTRO_2.SHP",0},{"INTRO_3.NEW","INTRO_3.SHP",0},{"GYPSY.NEW","GYPSY.SHP",0},{"MONTAGE.NEW","MONTAGE.SHP",16},
        {"VELLUM1.NEW","VELLUM1.SHP",16},{"WOODS.NEW","WOODS.SHP",0},{"END.NEW","END.SHP",0} };
    for(unsigned i=0;i<sizeof ART/sizeof ART[0];i++) if(!new_to_shp(ART[i].n,ART[i].s,ART[i].band)) return 2;
    if(!swapped("BASETILE")||!swapped("ANIMDATA")||!swapped("SCHEDULE")) return 2;
    { uint8_t *b=slurp("BOOK.DAT",&n); if(!b) return 2; int idx=(int)rbe16(b); swap16s(b,idx); if(!spit("BOOK.DAT",b,n)) return 2; free(b); }
    if(!copy_as("INTRO.PTR","U6MCGA.PTR")) return 2;      /* the pointer, in the DOS layout */
    for(int i=0;AS_IS[i];i++){ cv_tick(); if(!copy_as(AS_IS[i],AS_IS[i])){ printf("st2dos: %s not found\n",AS_IS[i]); return 2; } }
    if(!u_as_exe()||!end_as_exe()) return 2;
    { long en; uint8_t *ep=slurp("END.PRG",&en); static uint8_t lbm[12+8+768]; if(!ep) return 2;   /* the ending's palette, as DOS keeps it */
      memcpy(lbm,"FORM\0\0\3\0PBM CMAP\0\0\3\0",20);
      for(int i=0;i<16&&28+END_PAL+32<en;i++){ unsigned c=rbe16(ep+28+END_PAL+i*2); lbm[20+i*3]=(uint8_t)(((c>>8)&15)*17); lbm[21+i*3]=(uint8_t)(((c>>4)&15)*17); lbm[22+i*3]=(uint8_t)((c&15)*17); }
      free(ep); if(!spit("ENDPAL.LBM",lbm,sizeof lbm)) return 2; }
    printf("st2dos: done\n");
    return 0; }
