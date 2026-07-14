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
 * name the exact mechanism). On the Falcon the same STE-compat registers exist but
 * the DAC must first be routed with XBIOS Devconnect (opcode 139).
 *
 * Streaming: the DMA loops a fixed ST-RAM ring. Each pump reads the current play
 * pointer, and refills the region the DMA has just passed with the next bytes from
 * the file (wrapping at the song's loop point) — so the ring always holds the
 * upcoming audio. A main-loop stall just replays the ring (a brief repeat), never
 * silence or a crash.
 */
#include "platdma.h"
#include "platmem.h"
#include "platfile.h"

#include <osbind.h>
#include <mint/falcon.h>

#define RING_SIZE  32768u          /* ST-RAM ring: ~2.6 s at 12517 Hz (power of two) */
#define HDR_SIZE   16              /* U6PC header bytes before the PCM body          */

static int       g_avail;
static uint8_t  *g_ring;           /* DMA-safe ST-RAM ring buffer                    */
static uint32_t  g_ring_size;
static uint32_t  g_rate_bits;      /* $FF8921 low bits for the requested rate        */
static int       g_is_falcon;

static plat_file g_f = PLAT_BADFILE;
static int32_t   g_nframes, g_loop_start, g_frame_pos;  /* file body geometry (frames) */
static int32_t   g_last_off;       /* last observed play offset into the ring         */
static int32_t   g_cur_off;        /* filled by dma_hw_pos() (Supexec)                */
static int       g_playing;

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
static long dma_hw_pos(void){
    volatile uint8_t *r = (volatile uint8_t *)0xFFFF8900UL;
    uint32_t cur = ((uint32_t)r[0x09]<<16) | ((uint32_t)r[0x0B]<<8) | r[0x0D];
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
        if(g_frame_pos >= g_nframes) g_frame_pos = g_loop_start;
        int32_t file_avail = g_nframes - g_frame_pos;
        int32_t ring_avail = (int32_t)g_ring_size - dst;
        int32_t chunk = n;
        if(chunk > file_avail) chunk = file_avail;
        if(chunk > ring_avail) chunk = ring_avail;
        if(chunk <= 0) break;
        plat_fseek(g_f, HDR_SIZE + g_frame_pos, PLAT_SEEK_SET);
        plat_fread(g_f, g_ring + dst, chunk);
        g_frame_pos += chunk;
        dst += chunk; if(dst >= (int32_t)g_ring_size) dst = 0;
        n -= chunk;
    }
}

int plat_dma_music_start(const char *path){
    if(!g_avail) return 0;
    plat_dma_music_stop();

    g_f = plat_fopen(path, PLAT_RDONLY);
    if(g_f == PLAT_BADFILE) return 0;
    uint8_t h[HDR_SIZE];
    if(plat_fread(g_f, h, HDR_SIZE) != HDR_SIZE ||
       h[0]!='U'||h[1]!='6'||h[2]!='P'||h[3]!='C'){ plat_fclose(g_f); g_f=PLAT_BADFILE; return 0; }
    int32_t rate = ((int32_t)h[4]<<24)|((int32_t)h[5]<<16)|((int32_t)h[6]<<8)|h[7];   /* big-endian */
    g_nframes    = ((int32_t)h[8]<<24)|((int32_t)h[9]<<16)|((int32_t)h[10]<<8)|h[11];
    g_loop_start = ((int32_t)h[12]<<24)|((int32_t)h[13]<<16)|((int32_t)h[14]<<8)|h[15];
    if(g_nframes <= 0){ plat_fclose(g_f); g_f=PLAT_BADFILE; return 0; }
    if(g_loop_start < 0 || g_loop_start >= g_nframes) g_loop_start = 0;
    g_rate_bits = rate_to_bits(rate);

    /* prime the whole ring, then start the DMA loop */
    g_frame_pos = 0;
    stream_into(0, (int32_t)g_ring_size);
    g_last_off = 0;

    if(g_is_falcon){
        /* route DMA playback -> DAC at a ~12 kHz internal-clock prescale (CLK12K=7).
         * XBIOS 139; STE/TT ignore this path (their rate is the $8921 mode bits). */
        Devconnect(DMAPLAY, DAC, CLK25M, CLK12K, 0);
    }
    (void)Supexec(dma_hw_start);
    g_playing = 1;
    return 1;
}

void plat_dma_music_pump(void){
    if(!g_playing) return;
    (void)Supexec(dma_hw_pos);
    int32_t consumed = g_cur_off - g_last_off;
    if(consumed < 0) consumed += (int32_t)g_ring_size;
    if(consumed <= 0) return;
    stream_into(g_last_off, consumed);      /* refill the region the DMA just played */
    g_last_off = g_cur_off;
}

void plat_dma_music_stop(void){
    if(g_playing){ (void)Supexec(dma_hw_stop); g_playing = 0; }
    if(g_f != PLAT_BADFILE){ plat_fclose(g_f); g_f = PLAT_BADFILE; }
}
