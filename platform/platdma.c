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

#define RING_SIZE  32768u          /* ST-RAM ring: ~2.6 s at 12517 Hz (power of two) */
#define HDR_SIZE   16              /* U6PC header bytes before the PCM body          */

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
    g_playing = 1; g_fresh = 1;
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
    if(consumed <= 0) return;
    stream_into(g_last_off, consumed);      /* refill the region the DMA just played */
    g_last_off = g_cur_off;
}

void plat_dma_music_stop(void){
    if(g_playing){
        if(g_is_falcon){
            (void)Buffoper(0);                                /* stop play + repeat                */
            (void)Soundcmd(ADDERIN, ADCIN | MATIN);           /* back to the boot mix: PSG audible + STE-compat DMA */
            if(g_snd_locked){ (void)Unlocksnd(); g_snd_locked = 0; }
        } else (void)Supexec(dma_hw_stop);
        g_playing = 0; }
    if(g_f != PLAT_BADFILE){ plat_fclose(g_f); g_f = PLAT_BADFILE; }
}
