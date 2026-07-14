// SPDX-License-Identifier: GPL-2.0-or-later
/* oplsynth.c — portable integer OPL2 (YM3812) FM synthesizer (M10). See oplsynth.h.
 * Ported from the validated host proof tools/m2wav.c; no FPU / no libm at runtime
 * (log-sine + exponent tables are precomputed constants). */
#include "oplsynth.h"

#define NCH 9                   /* OPL2: nine 2-operator FM channels             */
#define ENV_MAX 0x1FF           /* 511 = full attenuation (silence)              */

/* logsin[i] = round(-log2(sin((i+0.5)*pi/512)) * 256), i=0..255 (one quarter wave).
 * exp_[i]  = round(2^(i/256) * 1024). Both are pure math constants (generated on
 * the host); an operator's amplitude for total attenuation t (1/256-octave units)
 * is (OPL_EXP[t&0xFF]*2) >> (t>>8). */
static const uint16_t OPL_LOGSIN[256] = {
    2137,1731,1543,1419,1326,1252,1190,1137,1091,1050,1013, 979,
     949, 920, 894, 869, 846, 825, 804, 785, 767, 749, 732, 717,
     701, 687, 672, 659, 646, 633, 621, 609, 598, 587, 576, 566,
     556, 546, 536, 527, 518, 509, 501, 492, 484, 476, 468, 461,
     453, 446, 439, 432, 425, 418, 411, 405, 399, 392, 386, 380,
     375, 369, 363, 358, 352, 347, 341, 336, 331, 326, 321, 316,
     311, 307, 302, 297, 293, 289, 284, 280, 276, 271, 267, 263,
     259, 255, 251, 248, 244, 240, 236, 233, 229, 226, 222, 219,
     215, 212, 209, 205, 202, 199, 196, 193, 190, 187, 184, 181,
     178, 175, 172, 169, 167, 164, 161, 159, 156, 153, 151, 148,
     146, 143, 141, 138, 136, 134, 131, 129, 127, 125, 122, 120,
     118, 116, 114, 112, 110, 108, 106, 104, 102, 100,  98,  96,
      94,  92,  91,  89,  87,  85,  83,  82,  80,  78,  77,  75,
      74,  72,  70,  69,  67,  66,  64,  63,  62,  60,  59,  57,
      56,  55,  53,  52,  51,  49,  48,  47,  46,  45,  43,  42,
      41,  40,  39,  38,  37,  36,  35,  34,  33,  32,  31,  30,
      29,  28,  27,  26,  25,  24,  23,  23,  22,  21,  20,  20,
      19,  18,  17,  17,  16,  15,  15,  14,  13,  13,  12,  12,
      11,  10,  10,   9,   9,   8,   8,   7,   7,   7,   6,   6,
       5,   5,   5,   4,   4,   4,   3,   3,   3,   2,   2,   2,
       2,   1,   1,   1,   1,   1,   1,   1,   0,   0,   0,   0,
       0,   0,   0,   0,
};
static const uint16_t OPL_EXP[256] = {
    1024,1027,1030,1032,1035,1038,1041,1044,1046,1049,1052,1055,
    1058,1061,1064,1066,1069,1072,1075,1078,1081,1084,1087,1090,
    1093,1096,1099,1102,1105,1108,1111,1114,1117,1120,1123,1126,
    1129,1132,1135,1138,1141,1144,1147,1150,1154,1157,1160,1163,
    1166,1169,1172,1176,1179,1182,1185,1188,1192,1195,1198,1201,
    1205,1208,1211,1214,1218,1221,1224,1228,1231,1234,1238,1241,
    1244,1248,1251,1255,1258,1261,1265,1268,1272,1275,1279,1282,
    1286,1289,1292,1296,1300,1303,1307,1310,1314,1317,1321,1324,
    1328,1332,1335,1339,1342,1346,1350,1353,1357,1361,1364,1368,
    1372,1376,1379,1383,1387,1391,1394,1398,1402,1406,1409,1413,
    1417,1421,1425,1429,1433,1436,1440,1444,1448,1452,1456,1460,
    1464,1468,1472,1476,1480,1484,1488,1492,1496,1500,1504,1508,
    1512,1516,1520,1525,1529,1533,1537,1541,1545,1550,1554,1558,
    1562,1566,1571,1575,1579,1584,1588,1592,1596,1601,1605,1609,
    1614,1618,1623,1627,1631,1636,1640,1645,1649,1654,1658,1663,
    1667,1672,1676,1681,1685,1690,1694,1699,1704,1708,1713,1717,
    1722,1727,1732,1736,1741,1746,1750,1755,1760,1765,1769,1774,
    1779,1784,1789,1794,1798,1803,1808,1813,1818,1823,1828,1833,
    1838,1843,1848,1853,1858,1863,1868,1873,1878,1883,1888,1893,
    1898,1904,1909,1914,1919,1924,1930,1935,1940,1945,1951,1956,
    1961,1966,1972,1977,1983,1988,1993,1999,2004,2010,2015,2020,
    2026,2031,2037,2042,
};

/* frequency multiple factor in HALF-steps (MULT=0 => 0.5x). Standard OPL. */
static const uint8_t MULT[16] = { 1,2,4,6,8,10,12,14,16,18,20,20,24,24,30,30 };

/* 24-entry OPL F-number table (lo,hi): freq byte low5 -> (fnum, block base);
 * entries 0/8/16 = rest. (docs/formats/music.md) */
static const uint8_t FREQ_LO[24] = {
  0x00,0x58,0x82,0xB0,0xCC,0x03,0x41,0x86, 0x00,0x6A,0x96,0xC7,0xE4,0x1E,0x5F,0xA8,
  0x00,0x47,0x6E,0x9A,0xB5,0xE9,0x24,0x66
};
static const uint8_t FREQ_HI[24] = {
  0x00,0x01,0x01,0x01,0x01,0x02,0x02,0x02, 0x00,0x01,0x01,0x01,0x01,0x02,0x02,0x02,
  0x00,0x01,0x01,0x01,0x01,0x01,0x02,0x02
};

/* --- per-operator / per-channel state ------------------------------------- */
typedef struct {
    uint32_t phase, pinc;
    int32_t  env;               /* attenuation 0..ENV_MAX (0 = loud)            */
    int32_t  state;             /* 0 off, 1 attack, 2 decay, 3 sustain, 4 rel   */
    int32_t  efrac;             /* sub-unit envelope accumulator (units*256)    */
    int32_t  out1, out2;        /* last two outputs (feedback averaging)        */
    uint8_t  mult, ksr, egt, ksl, tl, ar, dr, sl, rr, ws;
} Op;

typedef struct {
    Op  op[2];                  /* [0]=modulator, [1]=carrier                   */
    int32_t fnum, block, keyon, fb, cnt;
    int32_t m_att, c_att;       /* cached per-output-sample attenuations        */
} Chan;

/* --- module state --------------------------------------------------------- */
static Chan    g_chip[NCH];
static uint8_t g_instdef[16][11];
static int32_t g_rate, g_os;
static int32_t g_lp_y;
static int32_t g_playing;

/* sequencer cursor / timing */
static const uint8_t *g_d; static int32_t g_len, g_pos, g_loop;
static struct { int32_t ret, start, reps; } g_stk[8]; static int32_t g_sp;
static int32_t g_sub, g_subrem;         /* samples until next batch; 60ths rem  */

/* operator amplitude for phase index (10-bit) + attenuation + waveform 0..3 */
static int32_t op_out(uint32_t phase, int32_t att, int32_t ws){
    int32_t idx = phase & 0x3FF, quad = idx >> 8, sub = idx & 0xFF, neg = 0, mute = 0;
    switch(ws){
      case 0: if(quad & 1) sub = 0xFF - sub; if(quad & 2) neg = 1; break;
      case 1: if(quad & 2) mute = 1; else if(quad & 1) sub = 0xFF - sub; break;
      case 2: if(quad & 1) sub = 0xFF - sub; break;
      case 3: if(quad & 1) mute = 1; break;
      default: break;
    }
    if(mute) return 0;
    int32_t t = OPL_LOGSIN[sub] + (att << 3);   /* 0.1875dB env -> exp units    */
    if(t > 0x1FFF) t = 0x1FFF;
    int32_t mag = (OPL_EXP[t & 0xFF] << 1) >> (t >> 8);
    return neg ? -mag : mag;
}

static void set_instrument(Chan *c, const uint8_t *p){
    for(int k=0;k<2;k++){
        Op *o=&c->op[k]; const uint8_t *b=p+k*5;
        o->egt=(b[0]>>5)&1; o->ksr=(b[0]>>4)&1; o->mult=b[0]&0xF;
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
        uint32_t base = ((uint32_t)c->fnum << c->block);
        o->pinc = (base * MULT[o->mult]) / (2 * g_os);  /* freq/OS: phase runs OSx */
    }
}

static void key_on(Chan *c, int fnum, int block){
    c->fnum=fnum; c->block=block; c->keyon=1;
    recalc_pinc(c);
    for(int k=0;k<2;k++){
        Op *o=&c->op[k];
        if(o->state == 0){ o->phase=0; o->out1=o->out2=0; }  /* clean start only */
        o->state=1; o->efrac=0;
    }
}
static void key_off(Chan *c){
    c->keyon=0;
    for(int k=0;k<2;k++) if(c->op[k].state) c->op[k].state=4;
}

/* rate 0..63 -> per-sample env delta (0.1875 dB units * 256) for decay/release */
static int32_t decay_step(int32_t rate){
    if(rate <= 0) return 0;
    if(rate > 63) rate = 63;
    int32_t e = rate - 40; int32_t v = 26;
    if(e >= 0) v <<= (e / 4); else v >>= ((-e) / 4);
    if(v < 1) v = 1;
    if(v > (511<<8)) v = (511<<8);
    return v;
}
static int32_t eff_rate(Chan *c, Op *o, int32_t base){
    if(base == 0) return 0;
    int32_t keycode = (c->block << 1) | ((c->fnum >> 9) & 1);
    int32_t r = base*4 + (o->ksr ? keycode : (keycode >> 2));
    return r > 63 ? 63 : r;
}

static void env_tick(Chan *c, Op *o){
    switch(o->state){
      case 1: {                                 /* attack (exp approach to 0)   */
        int32_t r = eff_rate(c, o, o->ar);
        if(r >= 62){ o->env=0; o->state=2; break; }
        int32_t step = (o->env * (r+1)) >> 9; if(step < 1) step = 1;
        o->env -= step; if(o->env <= 0){ o->env=0; o->state=2; }
        break; }
      case 2: {                                 /* decay to sustain level       */
        o->efrac += decay_step(eff_rate(c, o, o->dr));
        o->env += o->efrac >> 8; o->efrac &= 0xFF;
        int32_t sl_att = (o->sl==15) ? ENV_MAX : o->sl*16;
        if(o->env >= sl_att){ o->env=sl_att; o->state=3; }
        break; }
      case 3: if(!o->egt) o->state=4; break;    /* sustain (percussive->release)*/
      case 4:                                    /* release                      */
        o->efrac += decay_step(eff_rate(c, o, o->rr));
        o->env += o->efrac >> 8; o->efrac &= 0xFF;
        if(o->env >= ENV_MAX){ o->env=ENV_MAX; o->state=0; }
        break;
      default: break;
    }
}

/* envelope + attenuation update: once per OUTPUT sample */
static void chan_env(Chan *c){
    Op *m=&c->op[0], *cr=&c->op[1];
    env_tick(c, m); env_tick(c, cr);
    int32_t kbase = c->block ? ((c->block-1)*32 + 2*((c->fnum>>6)&0xF)) : 0; /* KSL 6dB/oct */
    int32_t mksl = m->ksl  ? (kbase >> (3 - m->ksl))  : 0;
    int32_t cksl = cr->ksl ? (kbase >> (3 - cr->ksl)) : 0;
    int32_t ma = m->env  + m->tl*4  + mksl;
    int32_t ca = cr->env + cr->tl*4 + cksl;
    c->m_att = ma > ENV_MAX ? ENV_MAX : ma;
    c->c_att = ca > ENV_MAX ? ENV_MAX : ca;
}
/* one OVERSAMPLED waveform step (called g_os times per output sample) */
static int32_t chan_sub(Chan *c){
    Op *m=&c->op[0], *cr=&c->op[1];
    m->phase += m->pinc; cr->phase += cr->pinc;
    int32_t fb = c->fb ? ((m->out1 + m->out2) >> (9 - c->fb)) : 0;
    int32_t mout = op_out((m->phase >> 10) + fb, c->m_att, m->ws);
    m->out2 = m->out1; m->out1 = mout;
    if(c->cnt) return op_out(cr->phase >> 10, c->c_att, cr->ws) + mout;
    return op_out((cr->phase >> 10) + (mout >> 1), c->c_att, cr->ws);
}

#define LP_K 200                                /* mild output low-pass (AdLib) */
static int16_t render_sample(void){
    for(int i=0;i<NCH;i++) chan_env(&g_chip[i]);
    int32_t acc = 0;
    for(int s=0;s<g_os;s++) for(int i=0;i<NCH;i++) acc += chan_sub(&g_chip[i]);
    acc /= g_os;
    acc >>= 1;                                  /* headroom for 9 voices        */
    g_lp_y += ((acc - g_lp_y) * LP_K) >> 8;
    acc = g_lp_y;
    if(acc >  32767) acc =  32767;
    if(acc < -32768) acc = -32768;
    return (int16_t)acc;
}

/* freq byte -> fnum/block; returns 1 (note) or 0 (rest) */
static int freq_decode(uint8_t fb, int *fnum, int *block){
    int idx = fb & 0x1F, octave = fb >> 5;
    if(idx >= 24) return 0;
    int lo = FREQ_LO[idx], hi = FREQ_HI[idx];
    if(lo==0 && hi==0) return 0;
    *fnum = ((hi & 3) << 8) | lo;
    int bl = ((hi >> 2) & 7) + octave;
    if(bl < 0) bl = 0;
    if(bl > 7) bl = 7;
    *block = bl;
    return 1;
}

/* process commands until the next DELAY; return its tick count (>=1). Control
 * flow mirrors engine/music.c: 0x81 branch (stack), 0xE loop-point, 0xF return. */
static int32_t seq_step(void){
    int guard = 0;
    while(guard++ < 8192){
        if(g_pos >= g_len){ g_pos = g_loop; if(g_pos >= g_len){ g_playing=0; return 1; } continue; }
        uint8_t b=g_d[g_pos++]; int hi=b>>4, lo=b&0xF;
        switch(hi){
          case 0x0: { uint8_t fb=g_d[g_pos++]; (void)fb; if(lo<NCH) key_off(&g_chip[lo]); break; }
          case 0x1: case 0x2: { uint8_t fb=g_d[g_pos++]; int fn,bl;
            if(lo<NCH){ if(hi==0x1) key_off(&g_chip[lo]);
                if(freq_decode(fb,&fn,&bl)) key_on(&g_chip[lo], fn, bl); else key_off(&g_chip[lo]); }
            break; }
          case 0x3: { uint8_t v=g_d[g_pos++]; if(lo<NCH) g_chip[lo].op[1].tl = v & 0x3F; break; }
          case 0x4: { uint8_t v=g_d[g_pos++]; if(lo<NCH) g_chip[lo].op[0].tl = v & 0x3F; break; }
          case 0x5: case 0x6: g_pos++; break;
          case 0x7: { uint8_t v=g_d[g_pos++];
            if(lo<NCH && v<16){ set_instrument(&g_chip[lo], g_instdef[v]); recalc_pinc(&g_chip[lo]); } break; }
          case 0x8:
            if(b==0x82){ return g_d[g_pos++]; }                         /* DELAY  */
            else if(b==0x81){ int reps=g_d[g_pos++]; int32_t off=g_d[g_pos]|(g_d[g_pos+1]<<8); g_pos+=2;
                if(g_sp<8){ g_stk[g_sp].ret=g_pos; g_stk[g_sp].start=off; g_stk[g_sp].reps=reps; g_sp++; } g_pos=off; }
            else if(b==0x83){ int idx=g_d[g_pos++]; if(idx<16){ for(int k=0;k<11;k++) g_instdef[idx][k]=g_d[g_pos+k]; } g_pos+=11; }
            else if(b==0x85||b==0x86){ g_pos++; }
            break;
          case 0xE: g_loop=g_pos; break;
          case 0xF:
            if(g_sp>0){ if(--g_stk[g_sp-1].reps>0) g_pos=g_stk[g_sp-1].start; else g_pos=g_stk[--g_sp].ret; }
            else g_pos=g_loop;
            break;
          default: break;
        }
    }
    return 1;   /* runaway guard (no delay found) */
}

void opl_start(const uint8_t *stream, int32_t len, int32_t rate, int oversample){
    for(int i=0;i<NCH;i++){ Chan z; for(unsigned j=0;j<sizeof z;j++) ((uint8_t*)&z)[j]=0; g_chip[i]=z;
        g_chip[i].op[0].env=ENV_MAX; g_chip[i].op[1].env=ENV_MAX; }
    for(int i=0;i<16;i++) for(int k=0;k<11;k++) g_instdef[i][k]=0;
    g_d=stream; g_len=len; g_pos=0; g_loop=0; g_sp=0; g_lp_y=0;
    g_rate=rate>0?rate:25033; g_os=oversample<1?1:oversample;
    g_sub=0; g_subrem=0;
    g_playing = (stream && len>0);
}

void opl_render(int16_t *buf, int n){
    for(int i=0;i<n;i++){
        if(g_playing && --g_sub <= 0){
            int32_t ticks = seq_step();
            /* samples-per-tick = rate/60, carrying the remainder for exact timing */
            int32_t num = ticks * g_rate + g_subrem;
            g_sub = num / 60; g_subrem = num - g_sub*60;
            if(g_sub < 1) g_sub = 1;
        }
        buf[i] = g_playing ? render_sample() : 0;
    }
}

void opl_stop(void){ g_playing = 0; }
int  opl_playing(void){ return g_playing; }
