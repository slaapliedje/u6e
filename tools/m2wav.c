// SPDX-License-Identifier: GPL-2.0-or-later
/* m2wav.c — HOST de-risk proof for the M10 OPL2->DMA synth backend.
 *
 * Renders a U6 `.M` music file (LZW + OPL2 FM command stream, docs/formats/music.md) to a WAV by
 * running a SOFTWARE YM3812 (OPL2) FM synthesizer over the command stream. Goal: confirm BY EAR on
 * the dev PC that our .M -> OPL-register translation (instrument 11-byte layout, F-number/block,
 * key-on, per-command level, feedback/connection) reproduces the authentic DOS AdLib soundtrack,
 * BEFORE porting the synth + 8-bit DMA output to the 68000.
 *
 * Clean-room: the FM signal chain is written from the PUBLIC OPL2 math (log-sine + exponent tables,
 * 2-operator phase modulation, feedback) and from our own docs/formats/music.md — NOT transcribed
 * from AdPlug / DOSBox / Nuked-OPL. Host-only: uses float/libm at TABLE-BUILD time for clarity; the
 * per-sample path is integer, mirroring what the on-target fixed-point synth will do. Ships NO game
 * data (output WAV of the copyrighted score goes to build/, gitignored).
 *
 * Usage: m2wav FILE.M OUT.wav [seconds]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "../dataformats/u6lzw.h"

/* ---------------------------------------------------------------------------
 * OPL2 synthesis constants + tables (built from the mathematical definition).
 * The output sample rate is the OPL native ~49716 Hz so pitch constants match
 * the chip formula directly (freq = Fnum * 49716 / 2^(20-block)); a media
 * player resamples on playback. The on-target build will render at an STE/TT
 * DMA rate (e.g. 50066 or 25033 Hz) instead.
 * ------------------------------------------------------------------------- */
#define SR        49716          /* output (WAV) sample rate — OPL native       */
#define OS        4              /* oversampling: run the FM phase path at OS x  */
#define NCH       9              /* OPL2: nine 2-operator FM channels          */
/* The carrier/modulator waveform (deep feedback-7 FM = near-sawtooth) has
 * harmonics above Nyquist that alias into "static". We render the phase path at
 * OS x SR and box-average down — clean anti-aliasing for this HOST reference.
 * (The envelope is slow and not an alias source, so it ticks at the OUTPUT rate.)
 * On-target the 68000 renders at 1x the DMA rate; here we want the intended sound. */

/* logsin[i] = -log2(sin((i+0.5)*pi/512)) * 256, i in 0..255 (one quarter wave).
 * exp_[i]  = (2^(i/256))*1024, i in 0..255. Operator amplitude for a total
 * attenuation `t` (in 1/256-of-an-octave units): (exp_[t&0xFF]*2) >> (t>>8). */
static uint16_t logsin[256];
static uint16_t exp_[256];

/* Envelope / TL / KSL are carried in "attenuation units" of 0.1875 dB; the exp
 * table works in 1/256-octave (~0.0235 dB) units, so multiply by 8 (<<3) when
 * feeding attenuation into the exp path. (0.1875 / (6.0206/256) ~= 8.) */
#define ATT2EXP(a)  ((a) << 3)
#define ENV_MAX     0x1FF        /* 511 = full attenuation (silence)           */

/* Frequency multiple factor, in HALF-steps (MULT=0 means 0.5x). Standard OPL. */
static const uint8_t MULT[16] = { 1,2,4,6,8,10,12,14,16,18,20,20,24,24,30,30 };

/* Key-scale-level attenuation per octave (0/1.5/3/6 dB), in 0.1875 dB units,
 * scaled down by the KSL field (0=off). ksl_add[block][fnum_hi4] approximated
 * from the OPL KSL ROM (attenuation grows ~ log2(freq)). */
static uint8_t ksl_tab[8][16];

static void build_tables(void){
    for(int i=0;i<256;i++){
        double s = sin((i + 0.5) * M_PI / 512.0);
        logsin[i] = (uint16_t)lround(-log2(s) * 256.0);
        exp_[i]   = (uint16_t)lround(pow(2.0, i / 256.0) * 1024.0);
    }
    /* KSL: base attenuation in dB at the top of each octave, per the OPL ROM
     * (0,3,6,9,12,15,18,21... capped), then the fnum's high bits refine it. */
    for(int b=0;b<8;b++) for(int f=0;f<16;f++){
        double db = 0.0;
        if(b>0){ double base = (b-1)*6.0; double frac = f*6.0/16.0; db = base + frac; }
        if(db<0) db=0;
        ksl_tab[b][f] = (uint8_t)lround(db / 0.1875);   /* -> 0.1875 dB units   */
    }
}

/* Operator output magnitude for a phase index (10-bit) + total attenuation
 * (0.1875 dB units) + waveform select (0..3). Returns a signed ~[-4084,4084]. */
static int op_out(uint16_t phase, int att, int ws){
    int idx = phase & 0x3FF;                /* 1024-point wave                 */
    int quad = idx >> 8;                    /* which quarter                   */
    int sub  = idx & 0xFF;
    int neg  = 0, mute = 0;
    switch(ws){
      case 0:                               /* full sine                       */
        if(quad & 1) sub = 0xFF - sub;
        if(quad & 2) neg = 1;
        break;
      case 1:                               /* half sine (drop negative half)  */
        if(quad & 2){ mute = 1; }
        else if(quad & 1) sub = 0xFF - sub;
        break;
      case 2:                               /* abs sine (both humps positive)  */
        if(quad & 1) sub = 0xFF - sub;
        break;
      case 3:                               /* quarter/pulse sine              */
        if(quad & 1){ mute = 1; }
        break;
    }
    if(mute) return 0;
    int t = logsin[sub] + ATT2EXP(att);
    if(t > 0x1FFF) t = 0x1FFF;              /* clamp to silence                */
    int mag = (exp_[t & 0xFF] << 1) >> (t >> 8);
    return neg ? -mag : mag;
}

/* --- per-operator state -------------------------------------------------- */
typedef struct {
    uint32_t phase, pinc;       /* 20-bit phase accumulator; increment/sample  */
    int      env;               /* current attenuation 0..ENV_MAX (0=loud)     */
    int      state;             /* 0 off, 1 attack, 2 decay, 3 sustain, 4 rel  */
    long     efrac;             /* sub-unit envelope accumulator (units*256)    */
    int      out1, out2;        /* last two outputs (for feedback averaging)   */
    /* register-derived params */
    uint8_t  mult, ksr, egt, am, vib;   /* 0x20                                */
    uint8_t  ksl, tl;                   /* 0x40                                */
    uint8_t  ar, dr;                    /* 0x60                                */
    uint8_t  sl, rr;                    /* 0x80                                */
    uint8_t  ws;                        /* 0xE0                                */
} Op;

typedef struct {
    Op  op[2];                  /* [0]=modulator, [1]=carrier                  */
    int fnum, block, keyon;
    int fb, cnt;                /* 0xC0: feedback 0..7, connection 0=FM 1=AM   */
    int m_att, c_att;           /* per-output-sample total attenuations (cached)*/
} Chan;

static Chan chip[NCH];
static uint32_t lfo_am, lfo_vib;        /* global tremolo / vibrato phase      */

/* rate (0..63) -> per-sample env delta in 0.1875 dB units, for decay/release
 * (linear-in-dB). Doubles every +4 of rate; calibrated so rate ~48 fully
 * decays (511 units) in ~13 ms at 49716 Hz. Attack handled separately. */
static int decay_step(int rate){
    if(rate <= 0) return 0;
    if(rate > 63) rate = 63;
    /* base 0.1 units/sample at rate 40; <<((rate-40)/4) either way. Use a
     * 256x fixed-point return (units*256 per sample) for sub-unit precision. */
    int e = rate - 40;                  /* -40..23                             */
    long v = 26;                        /* 0.1 units * 256 ~= 26               */
    if(e >= 0) v <<= (e / 4); else v >>= ((-e) / 4);
    if(v < 1) v = 1;
    if(v > (511<<8)) v = (511<<8);
    return (int)v;                      /* units*256 per sample                */
}

static int eff_rate(Chan *c, Op *o, int base){
    if(base == 0) return 0;
    int keycode = (c->block << 1) | ((c->fnum >> 9) & 1);
    int ks = o->ksr ? keycode : (keycode >> 2);
    int r = base * 4 + ks;
    return r > 63 ? 63 : r;
}

/* apply the 11 instrument bytes (mod[5], car[5], fbc) to a channel */
static void set_instrument(Chan *c, const uint8_t *p){
    for(int k=0;k<2;k++){
        Op *o = &c->op[k]; const uint8_t *b = p + k*5;
        o->am=(b[0]>>7)&1; o->vib=(b[0]>>6)&1; o->egt=(b[0]>>5)&1; o->ksr=(b[0]>>4)&1; o->mult=b[0]&0xF;
        o->ksl=(b[1]>>6)&3; o->tl=b[1]&0x3F;
        o->ar=(b[2]>>4)&0xF; o->dr=b[2]&0xF;
        o->sl=(b[3]>>4)&0xF; o->rr=b[3]&0xF;
        o->ws=b[4]&3;
    }
    c->fb=(p[10]>>1)&7; c->cnt=p[10]&1;
}

static void recalc_pinc(Chan *c){
    for(int k=0;k<2;k++){
        Op *o=&c->op[k];
        uint32_t base = ((uint32_t)c->fnum << c->block);  /* Fnum * 2^block     */
        /* freq = pinc*(SR*OS)/2^20; want fnum*SR*2^block/2^20 => /OS here.     */
        o->pinc = (base * MULT[o->mult]) / (2 * OS);      /* * mult/2, per OS    */
    }
}

static void key_on(Chan *c, int fnum, int block){
    c->fnum=fnum; c->block=block; c->keyon=1;
    recalc_pinc(c);
    for(int k=0;k<2;k++){
        Op *o=&c->op[k];
        /* Restart the envelope, but do NOT zero the running phase: a hard phase
         * reset on every retrigger makes the carrier waveform jump (a click).
         * Only realign phase when the voice was fully silent (masked by attack). */
        if(o->state == 0){ o->phase = 0; o->out1 = o->out2 = 0; }
        o->state=1; o->efrac=0;
    }
}

static void key_off(Chan *c){
    c->keyon=0;
    for(int k=0;k<2;k++) if(c->op[k].state) c->op[k].state=4;  /* -> release   */
}

/* advance one operator's envelope by one sample; returns current attenuation. */
static void env_tick(Chan *c, Op *o){
    switch(o->state){
      case 1: {                                 /* attack (exp approach to 0)  */
        int r = eff_rate(c, o, o->ar);
        if(r >= 62){ o->env = 0; o->state = 2; break; }   /* instant           */
        /* exponential: reduce remaining attenuation by a rate-scaled fraction */
        int step = (o->env * (r+1)) >> 9;
        if(step < 1) step = 1;
        o->env -= step;
        if(o->env <= 0){ o->env = 0; o->state = 2; }
        break; }
      case 2: {                                 /* decay to sustain level      */
        int r = eff_rate(c, o, o->dr);
        o->efrac += decay_step(r);
        int whole = (int)(o->efrac >> 8); o->efrac &= 0xFF;
        o->env += whole;
        int sl_att = (o->sl==15) ? ENV_MAX : o->sl*16;   /* SL: 3dB steps       */
        if(o->env >= sl_att){ o->env = sl_att; o->state = 3; }
        break; }
      case 3:                                   /* sustain                     */
        if(!o->egt){                            /* percussive: keep releasing  */
            o->state = 4;
        }
        break;
      case 4: {                                 /* release                     */
        int r = eff_rate(c, o, o->rr);
        o->efrac += decay_step(r);
        int whole = (int)(o->efrac >> 8); o->efrac &= 0xFF;
        o->env += whole;
        if(o->env >= ENV_MAX){ o->env = ENV_MAX; o->state = 0; }
        break; }
      default: break;
    }
}

/* Envelope + attenuation update: ONCE per output sample (envelopes are slow and
 * are not the alias source, so they run at 1x). Caches m_att/c_att for chan_sub. */
static void chan_env(Chan *c){
    Op *m = &c->op[0], *cr = &c->op[1];
    env_tick(c, m); env_tick(c, cr);
    int kbase = ksl_tab[c->block][ (c->fnum>>6)&0xF ]; /* 6dB/oct amount        */
    /* KSL field 0/1/2/3 = 0 / 1.5 / 3 / 6 dB per octave => base >> (3-ksl).    */
    int mksl = m->ksl ? (kbase >> (3 - m->ksl)) : 0;
    int cksl = cr->ksl ? (kbase >> (3 - cr->ksl)) : 0;
    int ma = m->env  + m->tl*4  + mksl;         /* TL: 0.75dB -> *4 (0.1875)   */
    int ca = cr->env + cr->tl*4 + cksl;
    c->m_att = ma > ENV_MAX ? ENV_MAX : ma;
    c->c_att = ca > ENV_MAX ? ENV_MAX : ca;
}

/* One OVERSAMPLED waveform step: advance phases, apply feedback + FM, return the
 * channel's signed output. Called OS times per output sample. */
static int chan_sub(Chan *c){
    Op *m = &c->op[0], *cr = &c->op[1];
    m->phase  += m->pinc;
    cr->phase += cr->pinc;

    int fb = 0;                                 /* modulator self-feedback     */
    if(c->fb) fb = (m->out1 + m->out2) >> (9 - c->fb);
    int mout = op_out((uint16_t)((m->phase >> 10) + fb), c->m_att, m->ws);
    m->out2 = m->out1; m->out1 = mout;

    if(c->cnt)                                  /* AM: both operators to output*/
        return op_out((uint16_t)(cr->phase >> 10), c->c_att, cr->ws) + mout;
    return op_out((uint16_t)((cr->phase >> 10) + (mout >> 1)), c->c_att, cr->ws);
}

/* 1-pole low-pass, modelling the AdLib/OPL analog reconstruction filter — a mild
 * extra on top of the oversample box-average. y += (x-y)*K>>8; K=256 = bypass. */
#define LP_K 200
static long lp_y;

/* mix all channels into one 16-bit output sample (envelope at 1x, FM at OS x) */
static int16_t chip_render(void){
    for(int i=0;i<NCH;i++) chan_env(&chip[i]);
    long acc = 0;
    for(int s=0;s<OS;s++)
        for(int i=0;i<NCH;i++) acc += chan_sub(&chip[i]);
    acc /= OS;                                  /* box-average the oversamples  */
    lfo_am += 1; lfo_vib += 1;                  /* (LFO reserved; unused v1)   */
    acc >>= 1;                                  /* headroom for 9 voices       */
    lp_y += ((acc - lp_y) * LP_K) >> 8;         /* output low-pass             */
    acc = lp_y;
    if(acc >  32767) acc =  32767;
    if(acc < -32768) acc = -32768;
    return (int16_t)acc;
}

/* --- OPL F-number table (docs/formats/music.md): freq byte -> fnum, block --- */
static const struct { uint8_t lo, hi; } FREQ[24] = {
  {0x00,0x00},{0x58,0x01},{0x82,0x01},{0xB0,0x01},{0xCC,0x01},{0x03,0x02},{0x41,0x02},{0x86,0x02},
  {0x00,0x00},{0x6A,0x01},{0x96,0x01},{0xC7,0x01},{0xE4,0x01},{0x1E,0x02},{0x5F,0x02},{0xA8,0x02},
  {0x00,0x00},{0x47,0x01},{0x6E,0x01},{0x9A,0x01},{0xB5,0x01},{0xE9,0x01},{0x24,0x02},{0x66,0x02}
};
/* returns 1 + fills fnum/block, or 0 for a rest */
static int freq_decode(uint8_t fb, int *fnum, int *block){
    int idx = fb & 0x1F, octave = fb >> 5;
    if(idx >= 24) return 0;
    int lo = FREQ[idx].lo, hi = FREQ[idx].hi;
    if(lo==0 && hi==0) return 0;
    *fnum  = ((hi & 3) << 8) | lo;
    int bl = ((hi >> 2) & 7) + octave;
    if(bl < 0) bl = 0;
    if(bl > 7) bl = 7;
    *block = bl;
    return 1;
}

/* --- .M command sequencer: follows delays/branches/loops, drives the chip,
 * renders audio between delays. Mirrors engine/music.c control flow. -------- */
static uint8_t *slurp(const char *p, long *n){
    FILE *f=fopen(p,"rb"); if(!f){ perror(p); exit(1);}
    fseek(f,0,SEEK_END); *n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *b=malloc(*n); if(fread(b,1,*n,f)!=(size_t)*n) exit(1); fclose(f); return b;
}

/* growable little-endian WAV sample buffer */
static int16_t *wav; static long wlen, wcap;
static void emit(int16_t s){ if(wlen>=wcap){ wcap=wcap?wcap*2:1<<16; wav=realloc(wav,wcap*sizeof*wav);} wav[wlen++]=s; }
static void render_ticks(int ticks){
    /* one 60 Hz tick = SR/60 samples */
    long ns = (long)ticks * SR / 60;
    for(long i=0;i<ns;i++) emit(chip_render());
}

int main(int argc, char**argv){
    if(argc<3){ fprintf(stderr,"usage: %s FILE.M OUT.wav [seconds]\n",argv[0]); return 1; }
    double maxsec = (argc>3)? atof(argv[3]) : 30.0;
    long maxsamp = (long)(maxsec * SR);

    long n; uint8_t *src=slurp(argv[1],&n);
    int32_t usz=u6lzw_unpacked_size(src,(int32_t)n), cap=usz>0?usz:(int32_t)n;
    uint8_t *d=malloc(cap+16);
    int32_t got=u6lzw_decompress(src,(int32_t)n,d,cap);
    if(got<=0){ fprintf(stderr,"decode failed\n"); return 2; }

    build_tables();
    memset(chip,0,sizeof chip);
    for(int i=0;i<NCH;i++){ chip[i].op[0].env=ENV_MAX; chip[i].op[1].env=ENV_MAX; }

    uint8_t instdef[16][11]; memset(instdef,0,sizeof instdef);

    /* control-flow state (same shape as engine/music.c) */
    int32_t pos=0, loop_pos=0;
    struct { int32_t ret, start; int reps; } stk[8]; int sp=0;
    int guard=0;

    while(wlen < maxsamp && guard++ < 2000000){
        if(pos >= got){ pos = loop_pos; if(pos>=got) break; continue; }
        uint8_t b=d[pos++]; int hi=b>>4, lo=b&0xF;
        switch(hi){
          case 0x0: {                                   /* note off             */
            uint8_t fb=d[pos++]; (void)fb; if(lo<NCH) key_off(&chip[lo]); break; }
          case 0x1: case 0x2: {                          /* note (re)trigger     */
            uint8_t fb=d[pos++]; int fn,bl;
            if(lo<NCH){
                if(hi==0x1) key_off(&chip[lo]);
                if(freq_decode(fb,&fn,&bl)) key_on(&chip[lo], fn, bl);
                else key_off(&chip[lo]);
            }
            break; }
          case 0x3: {                                    /* carrier level (TL)   */
            uint8_t v=d[pos++]; if(lo<NCH) chip[lo].op[1].tl = v & 0x3F; break; }
          case 0x4: {                                    /* modulator level (TL) */
            uint8_t v=d[pos++]; if(lo<NCH) chip[lo].op[0].tl = v & 0x3F; break; }
          case 0x5: case 0x6: pos++; break;              /* slide / vibrato: v1  */
          case 0x7: {                                    /* set instrument idx   */
            uint8_t v=d[pos++]; if(lo<NCH && v<16){ set_instrument(&chip[lo], instdef[v]); recalc_pinc(&chip[lo]); } break; }
          case 0x8:
            if(b==0x82){ int t=d[pos++]; render_ticks(t); }
            else if(b==0x81){ int reps=d[pos++]; int32_t off=d[pos]|(d[pos+1]<<8); pos+=2;
                if(sp<8){ stk[sp].ret=pos; stk[sp].start=off; stk[sp].reps=reps; sp++; } pos=off; }
            else if(b==0x83){ int idx=d[pos++]; if(idx<16) memcpy(instdef[idx], &d[pos], 11); pos+=11; }
            else if(b==0x85||b==0x86){ pos++; }          /* fade: v1 ignore      */
            break;
          case 0xE: loop_pos=pos; break;                 /* loop point           */
          case 0xF:                                       /* return / loop        */
            if(sp>0){ if(--stk[sp-1].reps>0) pos=stk[sp-1].start; else pos=stk[--sp].ret; }
            else pos=loop_pos;
            break;
          default: break;
        }
    }
    free(src); free(d);

    /* write 16-bit mono WAV */
    FILE *o=fopen(argv[2],"wb"); if(!o){ perror(argv[2]); return 3; }
    long bytes = wlen*2; uint32_t chunk=36+bytes, byterate=SR*2;
    uint8_t h[44]={'R','I','F','F', chunk,chunk>>8,chunk>>16,chunk>>24, 'W','A','V','E',
      'f','m','t',' ', 16,0,0,0, 1,0, 1,0, SR&0xFF,(SR>>8)&0xFF,(SR>>16)&0xFF,(SR>>24)&0xFF,
      byterate&0xFF,(byterate>>8)&0xFF,(byterate>>16)&0xFF,(byterate>>24)&0xFF, 2,0, 16,0,
      'd','a','t','a', bytes,bytes>>8,bytes>>16,bytes>>24};
    fwrite(h,1,44,o); fwrite(wav,2,wlen,o); fclose(o);
    printf("%s -> %s : %ld samples (%.1fs @ %d Hz), %d B decompressed\n",
           argv[1], argv[2], wlen, (double)wlen/SR, SR, got);
    return 0;
}
