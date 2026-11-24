// SPDX-License-Identifier: GPL-2.0-or-later
/* platdma.c — STE/TT/Falcon 8-bit DMA sound streaming (M10). See platdma.h.
 *
 * Hardware (STE/TT DMA sound, all byte registers in the $FFFF89xx block; the DMA
 * reads ST-RAM only, so the ring buffer comes from plat_alloc_chip):
 *   $FFFF8901  control : bit0 = play enable, bit1 = repeat (loop the frame)
 *   $FFFF8903/05/07    : frame BASE  address (bits 23-16 / 15-8 / 7-0, even)
 *   $FFFF8909/0B/0D    : frame COUNT address (current DMA read pointer, RO)
 *   $FFFF890F/11/13    : frame END   address (exclusive)
 *   $FFFF8921  mode    : bit7 = mono, bits1-0 = rate (0=6258 1=12517 2=25033 3=50066 Hz)
 * Registers are programmed in supervisor mode via Supexec (per render/CLAUDE.md we
 * name the exact mechanism).
 *
 * FALCON: the DMA frame registers exist but the sound MATRIX + CODEC in front of the DAC
 * must be configured, and only through the XBIOS (Atari Compendium 4.7 "The Falcon030
 * Sound System"; docs/design/music.md). Real-hardware lesson (2026-08-15): a bare
 * Devconnect() with handshaking ON gave silence under plain TOS and garbled audio under
 * MiNT — the DAC hears DMA playback only via the 16-bit ADDER's *matrix* input, which
 * Sndstatus(1) leaves DISABLED, and handshaking makes the transfer rate variable ("may
 * negatively affect digital sound"). The sequence used, all XBIOS trap #14:
 *   Locksnd()   #128  claim the sound system (Unlocksnd #129 on stop)
 *   Sndstatus(1)#140  reset: DSP tristated, atten 0, matrix cleared, ADDERIN off, 8-bit stereo
 *   Setmode(2)  #132  8-bit MONO (what our U6PC streams are)
 *   Settracks(0,0) #133 / Setmontracks(0) #134   one stereo track pair, monitor track 0
 *   Devconnect(DMAPLAY, DAC, CLK25M, CLKOLD, NO_SHAKE) #139
 *               DMA playback -> DAC, 25.175 MHz, STE-COMPATIBLE prescaler, no handshake
 *   Soundcmd(SETPRESCALE, PRE640/320/160) #130   the STE rate: /640 = 12517 Hz etc.
 *               (exact match to the host-rendered PCM; native CLK12K would be 12292 Hz.
 *               6258 Hz does not exist on the Falcon -> nearest 12517)
 *   Soundcmd(ADDERIN, MATIN) #130   feed the 16-bit adder from the MATRIX (our DMA) ONLY
 *               while music plays — the ADC leg (YM/PSG + microphone through the codec's
 *               A/D) contributes only its analogue hiss + TOS keyclick here, since the PSG
 *               is not used while the DMA backend is live. Restored to ADCIN|MATIN (the
 *               STE-compatible boot state: PSG audible AND DMA-sound programs work) on stop.
 *   Soundcmd(LTATTEN/RTATTEN, 0) #130     full output level
 *   Setbuffer(SR_PLAY, ring, ring+size) #131 + Buffoper(SB_PLA_ENA|SB_PLA_RPT) #136  go
 *   The play position is read from the DMA frame counter $FFFF8909/0B/0D (read-only,
 *   identical on STE/TT/Falcon — Falcon register addendum) via Supexec, the same as the
 *   STE/TT path, so one code path is validated for the whole line (Buffptr #141 is the
 *   XBIOS view of the same counter; not depended upon).
 *
 * Streaming: the DMA loops a fixed ST-RAM ring. Each pump reads the current play
 * pointer, and refills the region the DMA has just passed with the next bytes from
 * the file (wrapping at the song's loop point) — so the ring always holds the
 * upcoming audio. A main-loop stall just replays the ring (a brief repeat), never
 * silence or a crash. The pump MUST be called from every wait loop the program sits
 * in (gamedemo vsync_pump + the gameplay loop) — a loop that only Vsync()s hears the
 * primed 2.6 s ring repeat forever (real-Falcon title-music bug, 2026-08-15).
 * BUG84 (corpus): the frame counter is not valid until one DMA cycle has started, so
 * start() waits a Vsync (XBIOS #37) and the first pump distrusts a huge "consumed".
 */
#include "platdma.h"
#include "platmem.h"
#include "platfile.h"

#include <osbind.h>
#include <mint/falcon.h>
#include <string.h>

#define RING_SIZE  32768u          /* ST-RAM ring: ~2.6 s at 12517 Hz (power of two) */
#define HDR_SIZE   16              /* U6PC header bytes before the PCM body          */
#define PUMP_CHUNK 4096            /* refill granularity: fewer, larger, aligned disk reads */

static int       g_avail;
static uint8_t  *g_ring;           /* DMA-safe ST-RAM ring buffer                    */
static uint32_t  g_ring_size;
static uint32_t  g_rate_bits;      /* $FF8921 low bits for the requested rate        */
static int       g_is_falcon;
static int       g_snd_locked;     /* Falcon: we hold the Locksnd() semaphore        */

static plat_file g_f = PLAT_BADFILE;
static int32_t   g_nframes, g_loop_start, g_frame_pos;  /* file body geometry (frames) */
static int32_t   g_last_off;       /* last observed play offset into the ring         */
static int32_t   g_cur_off;        /* filled by dma_hw_pos() (Supexec)                */
static int       g_playing;
static int       g_fresh;          /* first pump after start (distrust a stale counter) */
static int32_t   g_hw_prev;        /* last ring offset plat_dma_music_elapsed saw            */
static uint32_t  g_elapsed;        /* frames played since the start (loops included)         */
static int32_t   g_play_rate;      /* the song's rate (frames a second)                      */
static int32_t   g_mix_to;         /* (the effect mixer's, below) */

/* ---- resident song cache (platdma.h) ------------------------------------------------------
 * One arena, songs placed by a ring allocator (head advances; wrapping evicts whatever the new
 * extent overlaps = oldest first). An entry FILLS progressively while its song streams from disk
 * the first time (stream_into copies each contiguous chunk), and is COMPLETE once the file has
 * been read end to end (which the sequential first pass guarantees at its first loop wrap); a
 * complete entry needs no file at all — its header facts are kept in the entry. */
#define CACHE_MAX 16
typedef struct { char name[14]; uint32_t off, len; int32_t rate, loop_start; int32_t fill; int complete; int used; } cache_ent_t;
static uint8_t    *g_cache;  static uint32_t g_cache_size, g_cache_head, g_cache_used;
static cache_ent_t g_cent[CACHE_MAX];
static cache_ent_t *g_ent;         /* entry for the playing song (filling or complete), or NULL */
static int         g_from_cache;   /* current song is served from RAM                             */

/* ---- supervisor-mode register helpers (run via Supexec) ------------------- */
static long dma_hw_start(void){
    volatile uint8_t *r = (volatile uint8_t *)0xFFFF8900UL;
    uint32_t base = (uint32_t)g_ring, end = base + g_ring_size;
    r[0x01] = 0x00;                                   /* stop DMA                    */
    r[0x03] = (base>>16)&0xFF; r[0x05] = (base>>8)&0xFF; r[0x07] = base&0xFE;  /* base */
    r[0x0F] = (end >>16)&0xFF; r[0x11] = (end >>8)&0xFF; r[0x13] = end &0xFE;  /* end  */
    r[0x21] = (uint8_t)(0x80 | g_rate_bits);          /* mono + sample rate          */
    r[0x01] = 0x03;                                   /* enable + repeat (loop)      */
    return 0;
}
/* The frame counter is three byte registers read one after another; the effect mixer's interrupt (plat_dma_fx_isr)
 * reads them too, and one landing between these reads garbled the position (a ring of music skipped, 2026-11-24
 * in Hatari). So the three reads run with interrupts masked (IPL 7 in the status register; we are in supervisor
 * mode under Supexec). */
static long dma_hw_pos(void){
    volatile uint8_t *r = (volatile uint8_t *)0xFFFF8900UL;
    uint16_t sr; __asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) :: "memory");
    uint32_t cur = ((uint32_t)r[0x09]<<16) | ((uint32_t)r[0x0B]<<8) | r[0x0D];
    __asm__ volatile("move.w %0,%%sr" :: "d"(sr) : "memory");
    int32_t off = (int32_t)(cur - (uint32_t)g_ring);
    if(off < 0 || off >= (int32_t)g_ring_size) off = 0;
    g_cur_off = off;
    return 0;
}
static long dma_hw_stop(void){
    *(volatile uint8_t *)0xFFFF8901UL = 0x00;         /* clear enable + repeat       */
    return 0;
}

/* ---- init / capability ---------------------------------------------------- */
void plat_dma_init(const platform_caps_t *caps){
    machine_t m = caps ? caps->machine : MACHINE_ST;
    g_is_falcon = (m == MACHINE_FALCON);
    g_avail = (m==MACHINE_STE || m==MACHINE_MEGASTE || m==MACHINE_TT || m==MACHINE_FALCON);
    if(g_avail){
        g_ring = (uint8_t *)plat_alloc_chip(RING_SIZE);   /* ST-RAM, DMA-safe, aligned */
        g_ring_size = RING_SIZE;
        if(!g_ring) g_avail = 0;
    }
}
int plat_dma_available(void){ return g_avail; }
int plat_dma_music_playing(void){ return g_playing; }

/* map a sample rate to the STE/TT mode-register rate bits (nearest of the 4 rates) */
static uint32_t rate_to_bits(int32_t rate){
    if(rate >= 37549) return 3;      /* 50066 */
    if(rate >= 18775) return 2;      /* 25033 */
    if(rate >=  9388) return 1;      /* 12517 */
    return 0;                        /*  6258 */
}

/* Read `n` bytes of PCM from the file into the ring at ring offset `dst`, wrapping
 * the ring AND wrapping the file at the song loop point. Advances g_frame_pos. */
static void stream_into(int32_t dst, int32_t n){
    while(n > 0){
        if(g_frame_pos >= g_nframes){
            g_frame_pos = g_loop_start;
            /* first loop wrap of a filling entry: the whole body has streamed past -> RESIDENT.
             * Close the file: from here on the ring is refilled by memcpy, the disk stays idle. */
            if(g_ent && !g_ent->complete && g_ent->fill >= g_nframes){
                g_ent->complete = 1; g_from_cache = 1;
                if(g_f != PLAT_BADFILE){ plat_fclose(g_f); g_f = PLAT_BADFILE; } }
        }
        int32_t file_avail = g_nframes - g_frame_pos;
        int32_t ring_avail = (int32_t)g_ring_size - dst;
        int32_t chunk = n;
        if(chunk > file_avail) chunk = file_avail;
        if(chunk > ring_avail) chunk = ring_avail;
        if(chunk <= 0) break;
        if(g_from_cache){
            memcpy(g_ring + dst, g_cache + g_ent->off + (uint32_t)g_frame_pos, (size_t)chunk);
        } else {
            plat_fseek(g_f, HDR_SIZE + g_frame_pos, PLAT_SEEK_SET);
            plat_fread(g_f, g_ring + dst, chunk);
            /* progressive fill: the first pass reads the body sequentially from 0, so a chunk that
             * starts exactly at the fill mark extends it (a re-read after a wrap never does) */
            if(g_ent && !g_ent->complete && g_frame_pos == g_ent->fill){
                memcpy(g_cache + g_ent->off + (uint32_t)g_frame_pos, g_ring + dst, (size_t)chunk);
                g_ent->fill += chunk; }
        }
        g_frame_pos += chunk;
        dst += chunk; if(dst >= (int32_t)g_ring_size) dst = 0;
        n -= chunk;
    }
}

/* ---- cache management ---- */
void plat_dma_music_cache_init(uint32_t bytes, int prefer_fast){
    if(!g_avail || g_cache || bytes < 65536u) return;
    g_cache = (uint8_t *)(prefer_fast ? plat_alloc_fast(bytes) : plat_alloc_chip(bytes));
    if(!g_cache) return;
    g_cache_size = bytes; g_cache_head = 0; g_cache_used = 0;
    for(int i=0;i<CACHE_MAX;i++) g_cent[i].used = 0;
}
uint32_t plat_dma_music_cache_size(void){ return g_cache ? g_cache_size : 0; }
uint32_t plat_dma_music_cache_used(void){ return g_cache_used; }
int      plat_dma_music_cached(void){ return g_playing && g_from_cache; }

static int name_eq(const char *a, const char *b){ int i=0; for(;i<13;i++){ char x=a[i],y=b[i]; if(x>='a'&&x<='z') x-=32; if(y>='a'&&y<='z') y-=32; if(x!=y) return 0; if(!x) return 1; } return 1; }
static cache_ent_t *cache_find(const char *path){
    if(!g_cache) return 0;
    for(int i=0;i<CACHE_MAX;i++) if(g_cent[i].used && name_eq(g_cent[i].name, path)) return &g_cent[i];
    return 0;
}
/* Place a new entry of `len` body bytes: ring allocation at the head; wrapping to 0 when it would
 * run off the end; evict every entry the new extent overlaps (the oldest ones by construction). */
static cache_ent_t *cache_alloc(const char *path, uint32_t len){
    if(!g_cache || len == 0 || len > g_cache_size) return 0;
    uint32_t start = g_cache_head; if(start + len > g_cache_size) start = 0;
    uint32_t end = start + len;
    for(int i=0;i<CACHE_MAX;i++){ cache_ent_t *e=&g_cent[i]; if(!e->used) continue;
        if(e->off < end && start < e->off + e->len){ e->used = 0; g_cache_used -= e->len; } }
    cache_ent_t *e = 0;
    for(int i=0;i<CACHE_MAX;i++) if(!g_cent[i].used){ e=&g_cent[i]; break; }
    if(!e){ /* table full: evict the entry at the lowest offset >= start (oldest region) */
        uint32_t best=0xFFFFFFFFu; for(int i=0;i<CACHE_MAX;i++) if(g_cent[i].off>=start && g_cent[i].off<best){ best=g_cent[i].off; e=&g_cent[i]; }
        if(!e) e=&g_cent[0];
        g_cache_used -= e->len; }
    { int i=0; for(;i<13 && path[i];i++) e->name[i]=path[i]; e->name[i]=0; }
    e->off = start; e->len = len; e->fill = 0; e->complete = 0; e->used = 1;
    g_cache_used += len;
    g_cache_head = (end + 15u) & ~15u; if(g_cache_head >= g_cache_size) g_cache_head = 0;
    return e;
}

int plat_dma_music_start(const char *path){
    if(!g_avail) return 0;
    plat_dma_music_stop();

    int32_t rate;
    g_ent = cache_find(path); g_from_cache = 0;
    if(g_ent && g_ent->complete){
        /* RESIDENT: no file at all — geometry from the entry, ring refilled from RAM */
        rate = g_ent->rate; g_nframes = (int32_t)g_ent->len; g_loop_start = g_ent->loop_start;
        g_from_cache = 1;
    } else {
        g_f = plat_fopen(path, PLAT_RDONLY);
        if(g_f == PLAT_BADFILE) return 0;
        uint8_t h[HDR_SIZE];
        if(plat_fread(g_f, h, HDR_SIZE) != HDR_SIZE ||
           h[0]!='U'||h[1]!='6'||h[2]!='P'||h[3]!='C'){ plat_fclose(g_f); g_f=PLAT_BADFILE; return 0; }
        rate         = ((int32_t)h[4]<<24)|((int32_t)h[5]<<16)|((int32_t)h[6]<<8)|h[7];   /* big-endian */
        g_nframes    = ((int32_t)h[8]<<24)|((int32_t)h[9]<<16)|((int32_t)h[10]<<8)|h[11];
        g_loop_start = ((int32_t)h[12]<<24)|((int32_t)h[13]<<16)|((int32_t)h[14]<<8)|h[15];
        if(g_nframes <= 0){ plat_fclose(g_f); g_f=PLAT_BADFILE; return 0; }
        if(g_loop_start < 0 || g_loop_start >= g_nframes) g_loop_start = 0;
        /* a partially filled entry from an interrupted earlier play restarts its fill from 0;
         * a new song gets a slot if the arena can hold it (else it just streams) */
        if(g_ent){ g_ent->fill = 0; }
        else g_ent = cache_alloc(path, (uint32_t)g_nframes);
        if(g_ent){ g_ent->rate = rate; g_ent->loop_start = g_loop_start; }
    }
    g_rate_bits = rate_to_bits(rate); g_play_rate = rate;

    /* prime the whole ring, then start the DMA loop */
    g_frame_pos = 0;
    stream_into(0, (int32_t)g_ring_size);
    g_last_off = 0;

    if(g_is_falcon){
        /* Falcon: configure matrix + codec through the XBIOS (see the header comment for the
         * call list + opcodes), then hand it the ring as a repeating play buffer. */
        g_snd_locked = (Locksnd() == 1);
        (void)Sndstatus(1);                                   /* known state (matrix/adder cleared) */
        (void)Setmode(MODE_MONO8);                            /* 8-bit mono stream                  */
        (void)Settracks(0, 0);
        (void)Setmontracks(0);
        (void)Devconnect(DMAPLAY, DAC, CLK25M, CLKOLD, NO_SHAKE);   /* STE-compat prescaler, no handshake */
        { int pre = (g_rate_bits==3) ? PRE160 : (g_rate_bits==2) ? PRE320 : PRE640;   /* 6258 -> 12517 */
          (void)Soundcmd(SETPRESCALE, pre); }
        (void)Soundcmd(ADDERIN, MATIN);                       /* adder <- matrix (DMA) only: no ADC hiss */
        (void)Soundcmd(LTATTEN, 0); (void)Soundcmd(RTATTEN, 0);
        (void)Setbuffer(SR_PLAY, (long)g_ring, (long)(g_ring + g_ring_size));
        (void)Buffoper(SB_PLA_ENA | SB_PLA_RPT);              /* play + repeat the ring            */
    } else {
        (void)Supexec(dma_hw_start);                          /* STE/TT: raw frame registers       */
    }
    (void)Vsync();                                            /* BUG84: counter valid after a DMA cycle */
    g_mix_to = -1;
    g_playing = 1; g_fresh = 1; g_hw_prev = 0; g_elapsed = 0;
    return 1;
}

void plat_dma_music_pump(void){
    if(!g_playing) return;
    (void)Supexec(dma_hw_pos);                                /* frame counter $FFFF8909/0B/0D -> g_cur_off */
    int32_t consumed = g_cur_off - g_last_off;
    if(consumed < 0) consumed += (int32_t)g_ring_size;
    if(g_fresh){                                              /* first read after start: a stale counter (BUG84)
                                                               * would claim most of the ring — don't overwrite
                                                               * what the DMA is about to play; skip this pump
                                                               * (g_last_off stays 0 = the true start position) */
        g_fresh = 0;
        if(consumed > (int32_t)(g_ring_size/2)) return;
    }
    /* CHUNKED refill (real-Falcon 2026-08-16: a tiny Fseek+Fread EVERY frame audibly ground the
     * machine and slowed the intro — each one is a SCSI transaction with ms of fixed overhead).
     * Let ~PUMP_CHUNK bytes accumulate, then read them in ONE call whose file offset ENDS on a
     * 512-byte boundary, so every following read starts sector-aligned and GEMDOS can DMA the
     * whole thing straight into the ring (no partial-sector bounce). Slack stays ample: the DMA
     * is at most CHUNK+a frame ahead of the unfilled region of a 32 KB ring (>= 1 s at 25 kHz). */
    if(consumed < PUMP_CHUNK) return;
    int32_t n = consumed;
    int32_t fpos = HDR_SIZE + g_frame_pos;              /* file offset the next byte comes from */
    { int32_t end = (fpos + n) & ~511; n = end - fpos; } /* stop on a sector boundary */
    if(n <= 0) return;
    stream_into(g_last_off, n);             /* refill (part of) the region the DMA just played */
    g_last_off += n; if(g_last_off >= (int32_t)g_ring_size) g_last_off -= (int32_t)g_ring_size;
}

/* frames played since plat_dma_music_start, loops included, from the hardware's frame counter (call more often than
 * once a ring, 2.6 s; the gameplay and wait loops do). A stale first reading (BUG84) more than half a ring on is not counted. */
uint32_t plat_dma_music_elapsed(void){
    if(!g_playing) return g_elapsed;
    (void)Supexec(dma_hw_pos);
    int32_t d = g_cur_off - g_hw_prev; if(d < 0) d += (int32_t)g_ring_size;
    if(!(g_elapsed == 0 && d > (int32_t)(g_ring_size/2))){ g_elapsed += (uint32_t)d; g_hw_prev = g_cur_off; }
    return g_elapsed; }
int32_t plat_dma_music_rate(void){ return g_play_rate; }

/* ---- sound effects mixed into the ring (platdma.h) ---- */
#define FXV 8                       /* voices */
#define FX_FADE 1024                /* a stop's fade, samples (~80 ms at 12517 Hz, standing in for the AdLib's release): >>10 */
typedef struct { const int8_t *p; int32_t n, i; int16_t handle, fade; uint16_t age; } fxv_t;
static volatile fxv_t g_fxv[FXV];
static uint8_t *g_fxbank;  static int32_t g_fxn, g_fxsongs;  static const uint8_t *g_fxdir, *g_fxscale;  static const int8_t *g_fxdata;
static int8_t   g_fxlut[256];       /* sample -> sample at the playing song's balance (plat_dma_fx_song) */
static int32_t g_mix_to = -1;       /* ring offset the mixing has reached (-1 = restart at the play position) */
static uint16_t g_fx_age;

static uint32_t be32(const uint8_t *b){ return ((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3]; }
int plat_dma_fx_load(const char *path)
{
    if(g_fxbank) return 1;
    if(!g_avail) return 0;
    plat_file f = plat_fopen(path, PLAT_RDONLY); if(f == PLAT_BADFILE) return 0;
    uint8_t h[16]; int32_t len = 0;
    if(plat_fread(f, h, 16) == 16 && h[0]=='U'&&h[1]=='6'&&h[2]=='F'&&h[3]=='X'){
        int32_t n = (int32_t)be32(h+8), ns = (int32_t)be32(h+12);
        len = plat_fsize(f);
        if(n <= 0 || n > 64 || ns < 0 || ns > 64 || len < 16 + n*8 + ns*2) len = 0;
        else { g_fxn = n; g_fxsongs = ns; } }
    if(len <= 0){ plat_fclose(f); return 0; }
    uint8_t *m = 0; uint32_t need = ((uint32_t)len + 15u) & ~15u;
    if(g_cache && g_cache_size >= need + 1024u*1024u){        /* from the song arena's end: evict what lies there */
        uint32_t at = g_cache_size - need;
        for(int i=0;i<CACHE_MAX;i++){ cache_ent_t *e=&g_cent[i]; if(e->used && e->off + e->len > at){ if(e==g_ent){ g_ent=0; g_from_cache=0; } e->used=0; g_cache_used-=e->len; } }
        g_cache_size = at; if(g_cache_head >= at) g_cache_head = 0;
        m = g_cache + at; }
    else { plat_mem_info_t mi; plat_mem_get_info(&mi);         /* a block of its own, leaving half a megabyte */
        long ff = mi.have_altram ? (long)mi.fast_size - (long)mi.fast_used : 0, cf = (long)mi.chip_size - (long)mi.chip_used;
        if((ff > cf ? ff : cf) > (long)need + 512L*1024L) m = (uint8_t *)plat_alloc_fast(need); }
    if(!m){ plat_fclose(f); g_fxn = 0; return 0; }
    plat_fseek(f, 0, PLAT_SEEK_SET);
    if(plat_fread(f, m, len) != len){ plat_fclose(f); g_fxn = 0; return 0; }
    plat_fclose(f);
    g_fxbank = m; g_fxdir = m + 16; g_fxscale = g_fxdir + g_fxn*8; g_fxdata = (const int8_t *)(g_fxscale + g_fxsongs*2);
    for(int v=0;v<FXV;v++) g_fxv[v].p = 0;
    plat_dma_fx_song(-1);
    return 1;
}
void plat_dma_fx_song(int n)
{
    int sc = 256;
    if(g_fxbank && n >= 0 && n < g_fxsongs) sc = (g_fxscale[n*2] << 8) | g_fxscale[n*2+1];
    for(int i=0;i<256;i++){ int v = ((i - 128) * sc) >> 8; g_fxlut[i] = (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v); }
}
int plat_dma_fx_count(void){ return g_fxbank ? (int)g_fxn : 0; }
int plat_dma_fx_sounding(void){ for(int v=0;v<FXV;v++) if(g_fxv[v].p) return 1; return 0; }

/* the main program starts and stops voices while the interrupt mixes: a voice is taken out (p = 0) before its
 * fields change and put back by its pointer last (a 32-bit store, atomic on the 68000) */
void plat_dma_fx_play(int n, int handle)
{
    if(!g_fxbank || n < 0 || n >= g_fxn) return;
    int32_t off = (int32_t)be32(g_fxdir + n*8), len = (int32_t)be32(g_fxdir + n*8 + 4);
    if(len <= 0) return;
    int v = -1;
    for(int k=0;k<FXV;k++) if(g_fxv[k].p && g_fxv[k].handle == handle){ v = k; break; }
    if(v < 0) for(int k=0;k<FXV;k++) if(!g_fxv[k].p){ v = k; break; }
    if(v < 0){ uint16_t best = 0; for(int k=0;k<FXV;k++){ uint16_t a = (uint16_t)(g_fx_age - g_fxv[k].age); if(a >= best){ best = a; v = k; } } }
    g_fxv[v].p = 0;
    g_fxv[v].n = len; g_fxv[v].i = 0; g_fxv[v].handle = (int16_t)handle; g_fxv[v].fade = 0; g_fxv[v].age = g_fx_age++;
    g_fxv[v].p = g_fxdata + off;
}
void plat_dma_fx_stop(int handle)
{
    for(int k=0;k<FXV;k++) if(g_fxv[k].p && (handle < 0 || g_fxv[k].handle == handle) && !g_fxv[k].fade) g_fxv[k].fade = FX_FADE;
}

/* mix ahead of the DMA: up to LEAD samples past the play position (~25 ms; the interrupt comes every ~4 ms) */
void plat_dma_fx_isr(void)
{
    if(!g_playing || !g_fxbank) return;
    volatile uint8_t *r = (volatile uint8_t *)0xFFFF8900UL;
    uint32_t a = ((uint32_t)r[0x09]<<16) | ((uint32_t)r[0x0B]<<8) | r[0x0D];
    int32_t cur = (int32_t)(a - (uint32_t)g_ring), RS = (int32_t)g_ring_size;
    if(cur < 0 || cur >= RS) return;
    int32_t lead = g_play_rate / 40;
    int32_t ahead = g_mix_to < 0 ? -1 : g_mix_to - cur; if(ahead < 0 && g_mix_to >= 0) ahead += RS;
    if(g_mix_to < 0 || ahead > RS/2){ g_mix_to = cur; ahead = 0; }     /* (re)start, or the play position overtook us */
    int32_t cnt = lead - ahead; if(cnt <= 0) return;
    for(int k=0;k<FXV;k++){
        volatile fxv_t *vp = &g_fxv[k]; const int8_t *p = vp->p; if(!p) continue;
        int32_t i = vp->i, n = vp->n, pos = g_mix_to, left = cnt; int fade = vp->fade;
        while(left > 0 && i < n){
            int32_t run = RS - pos; if(run > left) run = left; if(run > n - i) run = n - i;
            int8_t *d = (int8_t *)g_ring + pos; const int8_t *s = p + i;
            const int8_t *L = g_fxlut + 128;
            if(fade){ for(int32_t j=0;j<run && fade>0;j++){ int x = d[j] + ((L[s[j]]*fade)>>10); d[j] = (int8_t)(x>127?127:x<-128?-128:x); fade--; }
                      if(fade <= 0){ i = n; break; } }
            else for(int32_t j=0;j<run;j++){ int x = d[j] + L[s[j]]; d[j] = (int8_t)(x>127?127:x<-128?-128:x); }
            i += run; pos += run; if(pos >= RS) pos = 0; left -= run; }
        if(vp->p == p){ if(i >= n) vp->p = 0; else { vp->i = i; if(vp->fade) vp->fade = (int16_t)fade; } }
    }
    g_mix_to += cnt; if(g_mix_to >= RS) g_mix_to -= RS;
}

void plat_dma_music_stop(void){
    for(int k=0;k<FXV;k++) g_fxv[k].p = 0;
    g_mix_to = -1;
    if(g_playing){
        if(g_is_falcon){
            (void)Buffoper(0);                                /* stop play + repeat                */
            (void)Soundcmd(ADDERIN, ADCIN | MATIN);           /* back to the boot mix: PSG audible + STE-compat DMA */
            if(g_snd_locked){ (void)Unlocksnd(); g_snd_locked = 0; }
        } else (void)Supexec(dma_hw_stop);
        g_playing = 0; }
    if(g_f != PLAT_BADFILE){ plat_fclose(g_f); g_f = PLAT_BADFILE; }
}
