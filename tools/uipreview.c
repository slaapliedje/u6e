/* uipreview.c — HOST: render the U6 windowed UI to PNGs so the layout + the
 * real U6.CH font can be iterated against reference screenshots WITHOUT the
 * slow emulator round-trip. Loads the U6W3 world pack (build/u6.world).
 *
 * FIDELITY MODEL (chosen): author the UI at U6's NATIVE 320x200 so panel
 * geometry matches the original screenshots 1:1. The ST/Falcon display 320x200
 * natively; the TT (TT-low is 320x480) LETTERBOXES it — placed 1:1, vertically
 * centred, black bands top/bottom. No stretching. Outputs both:
 *   <out>_200.png : the native 320x200 layout (compare 1:1 to reference)
 *   <out>.png     : the 320x480 present (2x-vertical-doubled; what the TT shows)
 *
 * The papyrus border + unfurled-scroll message box are the ORIGINAL art from
 * PAPER.BMP (a whole-file-LZW U6Bmp); the map window, portrait, and text are
 * composited over it, exactly as the original does.
 *
 * Native geometry measured from the real U6 DOS/ST screenshots:
 *   border  ~8px ornate frame all around
 *   map     [8..168) x [8..168)   160x160, 11x11 compose centre-cropped
 *   status  [8..168) y[170..184)  date / Wind  (dark strip under map)
 *   icons   [8..168) y[184..192)  command-icon row
 *   party   [168..312) y[8..107)  roster: face + name + right HP, top slider
 *   message [168..312) y[109..192) parchment conversation scroll
 *
 * Dev-side preview only; NOT engine code.
 * Usage: uipreview <build/u6.world> <U6PAL> <out.png>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "u6pal.h"
#include "u6lib.h"
#include "u6lzw.h"
#include "u6conv.h"
#include "png.h"
#include "world.h"

#define PW 56
#define PH 64

#define LW 320                         /* native logical width               */
#define LH 200                         /* native logical height              */
#define SCRH 480                       /* TT-low present height               */
#define BORD 8
#define MAPX 16                        /* map content inset (papyrus + bevel) */
#define MAPY 16
#define MAPSZ 144                      /* map viewport pixels (square)        */
#define VT   11                        /* tiles composed each axis (centred)  */
#define PANX 168                       /* right panel x (matches PAPER scroll)*/
#define PANW (LW-BORD-PANX)            /* 144: right panel width              */
#define STATY 170
#define STATH 14
#define ICONY 184
#define MSGY 109
#define TILE_TRANSPARENT 255

static uint8_t        LB[LW*LH];       /* native chunky logical UI buffer     */
static const uint8_t *g_tileset, *g_objs, *g_font;
static int32_t        g_nobjs;
static u6pal_t        g_pal;
static uint8_t        g_portrait[PW*PH]; /* decoded 56x64 NPC portrait         */
static int            g_have_portrait;
static uint8_t        g_paper[LW*LH];    /* PAPER.BMP: papyrus border + scroll  */
static int            g_have_paper;
static u6conv_t       g_conv;            /* the talked-to NPC's parsed script    */
static int            g_have_conv;

/* UI palette indices chosen by nearest match (robust to palette ordering) */
static uint8_t C_BLACK,C_WHITE,C_PARCH,C_GRAY,C_DGRAY,C_GOLD,C_RED,C_BROWN,C_GREEN;

static uint8_t nearest(int r,int g,int b)
{ int best=0; long bd=1L<<30;
  for(int i=0;i<256;i++){long dr=g_pal.color[i].r-r,dg=g_pal.color[i].g-g,db=g_pal.color[i].b-b;
      long d=dr*dr+dg*dg+db*db; if(d<bd){bd=d;best=i;}} return (uint8_t)best; }

/* ---- chunky primitives on the native buffer ------------------------------ */
static void fill(int x,int y,int w,int h,uint8_t c)
{ for(int yy=0;yy<h;yy++){uint8_t*d=LB+(y+yy)*LW+x;for(int xx=0;xx<w;xx++)d[xx]=c;} }
static void frame(int x,int y,int w,int h,uint8_t c)
{ for(int xx=0;xx<w;xx++){LB[y*LW+x+xx]=c;LB[(y+h-1)*LW+x+xx]=c;}
  for(int yy=0;yy<h;yy++){LB[(y+yy)*LW+x]=c;LB[(y+yy)*LW+x+w-1]=c;} }
static void px(int x,int y,uint8_t c){ if(x>=0&&x<LW&&y>=0&&y<LH) LB[y*LW+x]=c; }

/* authentic font: 8 bytes/glyph, MSB-left, glyph index == ASCII code */
static void text(int x,int y,const char*s,uint8_t col)
{ for(;*s;s++,x+=8){const uint8_t*g=g_font+(int)(unsigned char)*s*8;
    for(int gy=0;gy<8;gy++){uint8_t b=g[gy];for(int gx=0;gx<8;gx++) if(b&(0x80>>gx)) px(x+gx,y+gy,col);}} }
static int textw(const char*s){ int n=0; while(*s++)n++; return n*8; }
static char *put_uint(char*p,unsigned v){char t[10];int n=0;do{t[n++]=(char)('0'+v%10);v/=10;}while(v&&n<10);while(n)*p++=t[--n];*p=0;return p;}

/* ---- map window: compose 11x11 then centre-crop to 160x160 --------------- */
static void compose_map(const world_t*w)
{
    static uint8_t tmp[VT*16*VT*16];   /* 176x176 */
    const int T=VT*16;
    int32_t left=w->px-VT/2, top=w->py-VT/2;
    for(int vy=0;vy<VT;vy++)for(int vx=0;vx<VT;vx++){
        int tile=world_tile(w,left+vx,top+vy);
        const uint8_t*src=g_tileset+(int32_t)tile*256;
        for(int r=0;r<16;r++){uint8_t*d=tmp+(vy*16+r)*T+vx*16;const uint8_t*s=src+r*16;
            for(int c=0;c<16;c++)d[c]=s[c];}
    }
    for(int32_t i=0;i<g_nobjs;i++){const uint8_t*o=g_objs+i*6;
        int32_t vx=(((o[0]<<8)|o[1])-left)&(U6_SURFACE_TILES-1);
        int32_t vy=(((o[2]<<8)|o[3])-top )&(U6_SURFACE_TILES-1);
        if(vx<VT&&vy<VT){const uint8_t*src=g_tileset+(int32_t)(((o[4]<<8)|o[5]))*256;
            for(int r=0;r<16;r++){uint8_t*d=tmp+((int)vy*16+r)*T+(int)vx*16;const uint8_t*s=src+r*16;
                for(int c=0;c<16;c++) if(s[c]!=TILE_TRANSPARENT)d[c]=s[c];}}
    }
    /* avatar marker in the very centre */
    int ac=T/2; for(int y=-6;y<6;y++)for(int x=-4;x<4;x++)
        if(x==-4||x==3||y==-6||y==5) tmp[(ac+y)*T+(ac+x)]=C_WHITE;
    /* centre-crop the inner 160x160 into the logical buffer (edges clipped) */
    int off=(T-MAPSZ)/2;               /* 8 */
    for(int y=0;y<MAPSZ;y++)for(int x=0;x<MAPSZ;x++)
        LB[(MAPY+y)*LW+(MAPX+x)]=tmp[(off+y)*T+(off+x)];
}

/* blit a 16x16 tile from the tileset to (sx,sy); transp skips index 255 */
static void blit_tile(int sx,int sy,int tile,int transp)
{
    const uint8_t*src=g_tileset+(int32_t)tile*256;
    for(int y=0;y<16;y++)for(int x=0;x<16;x++){
        uint8_t v=src[y*16+x];
        if(!(transp&&v==TILE_TRANSPARENT)) px(sx+x,sy+y,v);
    }
}

/* the authentic map-window border: U6 overlays frayed-papyrus edge TILES around
 * the viewport (MapWindow::drawBorder uses tiles 432-439). The map content is
 * the inner 9x9 at (MAPX,MAPY); the 16px border ring sits just outside it.
 *   432 UL  433 top  434 UR  435 LL  436 bottom  437 LR  438 left  439 right
 * Drawn transparently so the burn-mark gaps show the papyrus underneath. */
static void draw_border_tiles(void)
{
    int x0=MAPX-16, x1=MAPX+MAPSZ, y0=MAPY-16, y1=MAPY+MAPSZ;
    blit_tile(x0,y0,432,1); blit_tile(x1,y0,434,1);
    blit_tile(x0,y1,435,1); blit_tile(x1,y1,437,1);
    for(int c=0;c<MAPSZ/16;c++){ int p=MAPX+c*16;
        blit_tile(p,y0,433,1); blit_tile(p,y1,436,1);
        blit_tile(x0,MAPY+c*16,438,1); blit_tile(x1,MAPY+c*16,439,1);
    }
}

/* party roster: portrait box + name + right-aligned HP, top slider bar */
static void draw_party(void)
{
    static const char*name[5]={"Avatar","Iolo","Shamino","Dupre","Sherry"};
    static const int  hp[5]   ={ 30, 26, 24, 30, 12 };
    fill(PANX,MAPY,PANW,STATY-MAPY-2,C_PARCH);
    frame(PANX,MAPY,PANW,STATY-MAPY-2,C_BROWN);
    /* slider bar across the top (U6 scrolls the roster) */
    fill(PANX+3,MAPY+3,PANW-6,6,C_GREEN); frame(PANX+3,MAPY+3,PANW-6,6,C_BROWN);
    for(int i=0;i<5;i++){
        int ry=MAPY+12+i*18;
        fill (PANX+4,ry,16,16,C_DGRAY); frame(PANX+4,ry,16,16,C_BLACK); /* portrait box */
        text (PANX+24,ry+4,name[i],C_BROWN);
        char s[8]; put_uint(s,(unsigned)hp[i]);
        text (PANX+PANW-6-textw(s),ry+4,s,C_BROWN);
    }
}

/* message / conversation scroll (bottom-right), authentic font */
static void draw_message(void)
{
    static const char*line[]={
        "A voice in the","darkness intones,","\"KAL LOR!\"","",
        "Avatar:","Thou art in Britannia.","",
        "Iolo: \"Well met!\"",
    };
    fill (PANX,MSGY,PANW,LH-BORD-MSGY,C_PARCH);
    frame(PANX,MSGY,PANW,LH-BORD-MSGY,C_BROWN);
    for(unsigned i=0;i<sizeof line/sizeof*line;i++)
        text(PANX+5,MSGY+4+(int)i*9,line[i],C_BROWN);
}

/* decode a 56x64 portrait (item i) from a PORTRAIT.* library buffer into dst */
static int decode_portrait(const uint8_t*lib_d,long lib_n,int i,uint8_t*dst)
{
    u6lib_t lib;
    if(!u6lib_open(&lib,lib_d,(int32_t)lib_n,4,false) &&
       !u6lib_open(&lib,lib_d,(int32_t)lib_n,4,true)  &&
       !u6lib_open_auto(&lib,lib_d,(int32_t)lib_n)) return 0;
    int32_t sz; const uint8_t*blob=u6lib_entry(&lib,i,&sz);
    if(!blob||sz<=0||u6lzw_unpacked_size(blob,sz)!=PW*PH) return 0;
    return u6lzw_decompress(blob,sz,dst,PW*PH)==PW*PH;
}

/* load PAPER.BMP (whole-file LZW U6Bmp: [u16 w][u16 h][w*h indices]) into
 * g_paper if it is the expected 320x200 game-screen background */
static int load_paper(const char*path)
{
    FILE*f=fopen(path,"rb"); if(!f) return 0;
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*c=malloc((size_t)n); if(fread(c,1,(size_t)n,f)!=(size_t)n){fclose(f);free(c);return 0;} fclose(f);
    int32_t un=u6lzw_unpacked_size(c,(int32_t)n);
    if(un<4){free(c);return 0;}
    uint8_t*d=malloc((size_t)un);
    int ok=(u6lzw_decompress(c,(int32_t)n,d,un)==un);
    int w=d[0]|(d[1]<<8), h=d[2]|(d[3]<<8);
    if(ok && w==LW && h==LH && 4+(long)w*h<=un){ memcpy(g_paper,d+4,(size_t)LW*LH); ok=1; } else ok=0;
    free(c); free(d); return ok;
}

/* load + parse NPC `npc`'s conversation from a CONVERSE.A/.B buffer (4-byte
 * offset library; each entry LZW). Fills g_conv (name + greeting + sections). */
static int load_conv(const uint8_t*f,long flen,int npc)
{
    int i=0; while(i*4+4<=flen && (f[i*4]|f[i*4+1]<<8|f[i*4+2]<<16|((uint32_t)f[i*4+3]<<24))==0) i++;
    long count=(f[i*4]|f[i*4+1]<<8|f[i*4+2]<<16|((uint32_t)f[i*4+3]<<24))/4;
    if(npc<0||npc>=count) return 0;
    #define OFF(j) (long)(f[(j)*4]|f[(j)*4+1]<<8|f[(j)*4+2]<<16|((uint32_t)f[(j)*4+3]<<24))
    long s=OFF(npc); if(!s) return 0;
    long e=flen; for(long j=npc+1;j<count;j++){ long o=OFF(j); if(o){e=o;break;} }
    #undef OFF
    int32_t usz=u6lzw_unpacked_size(f+s,(int32_t)(e-s));
    int32_t cap=(usz>0)?usz:(int32_t)(e-s);
    uint8_t*d=malloc((size_t)cap+4);
    int32_t got=u6lzw_decompress(f+s,(int32_t)(e-s),d,cap+4);
    int ok = got>0 && u6conv_parse(&g_conv,d,got,"Avatar");
    free(d); return ok;
}

/* draw `s` word-wrapped into a box [x..x+w) starting at y, max `lines` rows */
static void text_wrap(int x,int y,int w,const char*s,int lines,uint8_t col)
{
    int cols=w/8, row=0;
    while(*s && row<lines){
        while(*s==' ') s++;
        const char*end=s; int n=0,last=-1;
        for(const char*p=s; *p && *p!='\n'; p++,n++){ if(*p==' ') last=n; if(n>=cols){ if(last>=0)n=last; break; } end=p+1; }
        int take = (*s && (*(s+n)=='\0'||*(s+n)=='\n')) ? n : (last>=0?last:n);
        if(take>cols) take=cols;
        char line[64]; int t=take<63?take:63; for(int k=0;k<t;k++) line[k]=s[k]; line[t]=0;
        text(x,y+row*9,line,col); row++;
        s+=take; if(*s=='\n') s++;
        (void)end;
    }
}

/* blit the decoded portrait into the logical buffer at (lx,ly) with a frame */
static void draw_portrait(int lx,int ly)
{
    frame(lx-1,ly-1,PW+2,PH+2,C_BROWN);
    for(int y=0;y<PH;y++)for(int x=0;x<PW;x++) px(lx+x,ly+y,g_portrait[y*PW+x]);
}

/* conversation view (U6 face-to-face): the talked-to NPC's REAL portrait + name
 * top-right (portrait = npc-1, name from the NPC's conversation), the NPC's REAL
 * greeting word-wrapped in the unfurled scroll below. */
static void draw_conversation(void)
{
    /* papyrus (PAPER.BMP) is the background; only fill a panel in the fallback */
    if(!g_have_paper){ fill(PANX,MAPY,PANW,LH-BORD-MAPY,C_PARCH);
                       frame(PANX,MAPY,PANW,LH-BORD-MAPY,C_BROWN); }
    int pxx=PANX+(PANW-PW)/2, pyy=MAPY+8;       /* portrait in top-right papyrus */
    if(g_have_portrait) draw_portrait(pxx,pyy);
    else { fill(pxx,pyy,PW,PH,C_DGRAY); frame(pxx,pyy,PW,PH,C_BLACK); }
    const char*name = (g_have_conv && g_conv.name[0]) ? g_conv.name : "?";
    text(PANX+(PANW-textw(name))/2, pyy+PH+4, name, C_BROWN);
    /* NPC's real greeting in the unfurled scroll box, word-wrapped */
    if(g_have_conv) text_wrap(PANX+6, 116, PANW-12, g_conv.greeting, 8, C_BROWN);
}

/* date/wind status line under the map. On papyrus we draw dark text directly;
 * in the fallback we draw a dark strip + a placeholder command-icon row. */
static void draw_status(void)
{
    uint8_t col = g_have_paper ? C_BLACK : C_WHITE;
    if(!g_have_paper) fill(MAPX,STATY,MAPSZ,STATH,C_DGRAY);
    text(MAPX+4,STATY+3,"6-04-0161 Wind:",col);
    text(MAPX+4+textw("6-04-0161 Wind:"),STATY+3,"N",col);
    if(!g_have_paper){
        int n=10,iw=(MAPSZ-2)/n;            /* placeholder command-icon row */
        for(int i=0;i<n;i++){int ix=MAPX+1+i*iw;fill(ix,ICONY,iw-1,LH-BORD-ICONY,C_BROWN);
            frame(ix,ICONY,iw-1,LH-BORD-ICONY,C_BLACK);}
    }
}

/* ornate border placeholder (real serpent-scale art decode pending) */
static void draw_border(void)
{
    fill(0,0,LW,BORD,C_GOLD); fill(0,LH-BORD,LW,BORD,C_GOLD);
    fill(0,0,BORD,LH,C_GOLD); fill(LW-BORD,0,BORD,LH,C_GOLD);
    frame(1,1,LW-2,LH-2,C_BROWN); frame(BORD-1,BORD-1,LW-2*BORD+2,LH-2*BORD+2,C_BROWN);
    for(int x=BORD;x<LW-BORD;x+=12){px(3,0+0,0);px(x,3,C_RED);px(x,LH-4,C_RED);}
    for(int y=BORD;y<LH-BORD;y+=12){px(3,y,C_RED);px(LW-4,y,C_RED);}
    /* the lone command icon U6 tucks at the bottom-left of the right panel */
    fill(PANX+2,LH-BORD-12,12,11,C_RED); frame(PANX+2,LH-BORD-12,12,11,C_BLACK);
}

static uint8_t *slurp(const char*p,long*n)
{ FILE*f=fopen(p,"rb");if(!f){printf("cannot read %s\n",p);exit(2);}
  fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);
  uint8_t*b=malloc((size_t)*n);if(fread(b,1,(size_t)*n,f)!=(size_t)*n){printf("short read\n");exit(2);}fclose(f);return b; }

/* write the native 320x200 buffer to a PNG (1:1) */
static int write_png_logical(const char*path)
{ uint8_t*img=malloc((size_t)LW*LH*3);
  for(int i=0;i<LW*LH;i++){u6_rgb c=g_pal.color[LB[i]];img[i*3]=c.r;img[i*3+1]=c.g;img[i*3+2]=c.b;}
  int rc=png_write_rgb(path,LW,LH,img);free(img);return rc; }

/* write the TT present: the 320x200 UI scaled 2x VERTICALLY (each row doubled ->
 * 400 rows), vertically centred in 320x480 with black bands (top/bottom 40px).
 * Matches rdr_present_doubled() on the TT. (ST/Falcon would show 320x200 native.) */
static int write_png_doubled(const char*path)
{ uint8_t*img=malloc((size_t)LW*SCRH*3); int top=(SCRH-LH*2)/2;        /* 40 */
  for(int Y=0;Y<SCRH;Y++){int ly=(Y-top)/2; uint8_t inband=(Y>=top&&Y<top+LH*2);
      for(int x=0;x<LW;x++){uint8_t idx= inband?LB[ly*LW+x]:C_BLACK; u6_rgb c=g_pal.color[idx];
          long o=((long)Y*LW+x)*3; img[o]=c.r;img[o+1]=c.g;img[o+2]=c.b;}}
  int rc=png_write_rgb(path,LW,SCRH,img);free(img);return rc; }

int main(int argc,char**argv)
{
    const char*pak_path=argc>1?argv[1]:"build/u6.world";
    const char*pal_path=argc>2?argv[2]:"refdata/dos/u6/U6PAL";
    const char*out     =argc>3?argv[3]:"build/uipreview.png";
    const char*mode    =argc>4?argv[4]:"conv";        /* "conv" | "roster"  */
    int   npc          =argc>5?atoi(argv[5]):5;       /* NPC# (default L.B.) */

    long pn; uint8_t*pak=slurp(pak_path,&pn);
    if(pn<24||memcmp(pak,"U6W6",4)!=0){printf("not a U6W6 pack\n");return 2;}
    uint32_t map_len=(pak[4]<<24)|(pak[5]<<16)|(pak[6]<<8)|pak[7];
    uint32_t chk_len=(pak[8]<<24)|(pak[9]<<16)|(pak[10]<<8)|pak[11];
    uint32_t ntiles =(pak[12]<<24)|(pak[13]<<16)|(pak[14]<<8)|pak[15];
    g_nobjs=(int32_t)(((uint32_t)pak[16]<<24)|(pak[17]<<16)|(pak[18]<<8)|pak[19]);
    int32_t sx=(pak[20]<<8)|pak[21], sy=(pak[22]<<8)|pak[23];
    const uint8_t*map=pak+24,*chunks=map+map_len;
    g_tileset=chunks+chk_len;
    const uint8_t*palette=g_tileset+(int32_t)ntiles*256,*passable=palette+768;
    g_objs=passable+256; g_font=g_objs+(int32_t)g_nobjs*6;

    long paln; uint8_t*palb=slurp(pal_path,&paln);
    u6pal_parse(&g_pal,palb,(int32_t)paln);
    C_BLACK=nearest(0,0,0);       C_WHITE=nearest(255,255,255);
    C_PARCH=nearest(232,216,160); C_GRAY =nearest(150,150,150);
    C_DGRAY=nearest(60,40,30);    C_GOLD =nearest(210,150,30);
    C_RED  =nearest(190,30,20);   C_BROWN=nearest(120,60,20); C_GREEN=nearest(40,170,40);

    /* load a real NPC portrait from the same refdata dir as the palette */
    char dir[1024]; const char*sl=strrchr(pal_path,'/');
    if(sl) snprintf(dir,sizeof dir,"%.*s",(int)(sl-pal_path),pal_path); else snprintf(dir,sizeof dir,".");
    char ppath[1100]; snprintf(ppath,sizeof ppath,"%s/PORTRAIT.A",dir);
    long porn; uint8_t*porb=slurp(ppath,&porn);
    g_have_portrait=decode_portrait(porb,porn,npc-1,g_portrait);   /* portrait = npc-1 */
    if(!g_have_portrait) printf("warning: portrait for NPC %d not decoded\n",npc);

    /* the talked-to NPC's conversation: name + greeting (CONVERSE.A index = npc) */
    char cpath[1100]; snprintf(cpath,sizeof cpath,"%s/CONVERSE.A",dir);
    long cn; uint8_t*cb=slurp(cpath,&cn);
    g_have_conv=load_conv(cb,cn,npc);
    if(!g_have_conv) printf("warning: conversation for NPC %d not parsed\n",npc);
    else printf("NPC %d = \"%s\"\n",npc,g_conv.name);

    /* load the authentic papyrus background (PAPER.BMP) */
    char bpath[1100]; snprintf(bpath,sizeof bpath,"%s/PAPER.BMP",dir);
    g_have_paper=load_paper(bpath);
    if(!g_have_paper) printf("warning: PAPER.BMP not loaded (using placeholder border)\n");

    GameDef def={"u6",sx,sy,0}; world_t w;
    if(!world_init(&w,map,(int32_t)map_len,chunks,(int32_t)chk_len,passable,&def,1)){
        printf("world_init failed\n");return 2;}

    if(g_have_paper) memcpy(LB,g_paper,(size_t)LW*LH); else fill(0,0,LW,LH,C_BLACK);
    compose_map(&w); draw_border_tiles();
    if(strcmp(mode,"roster")==0){ draw_party(); draw_message(); }
    else                          draw_conversation();
    draw_status();
    if(!g_have_paper) draw_border();           /* papyrus replaces the frame */

    char p200[1024]; snprintf(p200,sizeof p200,"%.*s_200.png",
        (int)(strrchr(out,'.')?strrchr(out,'.')-out:(long)strlen(out)),out);
    int rc=write_png_logical(p200) | write_png_doubled(out);
    printf("wrote %s (320x200) + %s (320x480 2x-doubled)  start(%d,%d) %d objs\n",p200,out,sx,sy,g_nobjs);
    return rc;
}
