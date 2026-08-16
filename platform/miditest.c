// SPDX-License-Identifier: GPL-2.0-or-later
/* miditest.c — MIDITEST.PRG: a tiny stand-alone MIDI cable/port check for any Atari
 * (ST/STE/TT/Falcon; plain 68000 build). Run it from the desktop before blaming the
 * game's MIDI music backend.
 *
 *   OUT test  : sends General MIDI "System On", program 0 (Acoustic Grand) on channel 1,
 *               a C-major scale and a C-major chord out the MIDI OUT port. If the cable
 *               Atari MIDI OUT -> instrument MIDI IN is right (and the instrument listens
 *               on channel 1 / omni), you HEAR the scale. Repeat with SPACE.
 *   IN test   : then it listens on MIDI IN and prints every received message decoded
 *               (Note On/Off with note name + velocity, program/controller/pitch-bend,
 *               raw hex for the rest). If the cable instrument MIDI OUT -> Atari MIDI IN
 *               is right, playing keys on the instrument prints lines here.
 *   H (auto)  : plays the HURRIAN HYMN (h.6, the oldest notated melody, c.1400 BCE Ugarit —
 *               the title tune of the author's Royal Game of Ur ports, ~/dev/ur music.c: melody
 *               after the Dumbrill/Levy reconstruction, arranged into an ~11 s loop) on a GM
 *               harp — a proper little tune to judge the instrument by. Any key skips it.
 *   T         : toggles MIDI THRU (echo what comes in back out — useful to check the round
 *               trip; leave OFF if the instrument has Local Control on, or notes double).
 *   Esc/Q     : all-notes-off on every channel and exit.
 *
 * TOS calls (named per platform/CLAUDE.md): Midiws (XBIOS #12) to transmit; Bconstat(3)
 * (BIOS #1, device 3 = MIDI) / Bconin(3) (BIOS #2) to poll + read MIDI IN — TOS fills the
 * MIDI receive buffer from the 6850 ACIA interrupt, so no register access is needed;
 * Cconws (GEMDOS #9), Cconis (GEMDOS #0x0B), Crawcin (GEMDOS #7) for the console; Supexec
 * (XBIOS #38) to read the 200 Hz system tick _hz_200 ($4BA) for the note timing.
 */
#include <stdint.h>
#include <osbind.h>

static void put(const char *s){ Cconws(s); }
static void nl(void){ Cconws("\r\n"); }
static void put_dec(int n){ char b[12]; int i=11; b[i]=0; unsigned u=(unsigned)(n<0?-n:n);
    if(!u) b[--i]='0'; while(u){ b[--i]=(char)('0'+u%10); u/=10; } if(n<0) b[--i]='-'; put(b+i); }
static void put_hex2(int v){ static const char h[]="0123456789ABCDEF"; char b[3]={h[(v>>4)&15],h[v&15],0}; put(b); }

/* ---- 200 Hz tick (system variable _hz_200 at $4BA, supervisor-only) ---- */
static volatile uint32_t g_hz;
static long rd_hz200(void){ g_hz=*(volatile uint32_t*)0x4BAUL; return 0; }
static uint32_t ticks(void){ (void)Supexec(rd_hz200); return g_hz; }
static void wait_ticks(uint32_t n){ uint32_t t0=ticks(); while((uint32_t)(ticks()-t0)<n){ } }

/* ---- MIDI OUT ---- */
static void midi_send(const uint8_t *b, int n){ if(n>0) Midiws(n-1,(const void*)b); }
static void note_on (int ch,int n,int v){ uint8_t m[3]={(uint8_t)(0x90|ch),(uint8_t)n,(uint8_t)v}; midi_send(m,3); }
static void note_off(int ch,int n){ uint8_t m[3]={(uint8_t)(0x80|ch),(uint8_t)n,0}; midi_send(m,3); }
static void all_off(void){ for(int ch=0;ch<16;ch++){ uint8_t m[6]={(uint8_t)(0xB0|ch),123,0,(uint8_t)(0xB0|ch),120,0}; midi_send(m,6); } }

static void out_test(void)
{
    static const uint8_t GMRESET[6]={0xF0,0x7E,0x7F,0x09,0x01,0xF7};
    static const uint8_t PROG0[2]={0xC0,0x00};             /* channel 1, program 0 = Acoustic Grand */
    static const int SCALE[8]={60,62,64,65,67,69,71,72};   /* C4 .. C5 */
    put("OUT: GM reset, piano on ch 1, then a"); nl();
    put("     C-major scale + chord..."); nl();
    midi_send(GMRESET,6); wait_ticks(20);
    midi_send(PROG0,2);   wait_ticks(5);
    for(int i=0;i<8;i++){ note_on(0,SCALE[i],100); wait_ticks(45); note_off(0,SCALE[i]); wait_ticks(5); }
    note_on(0,60,100); note_on(0,64,100); note_on(0,67,100); wait_ticks(200);
    note_off(0,60); note_off(0,64); note_off(0,67);
    put("OUT: sent. Silent? check Atari OUT ->"); nl();
    put("     instrument IN, and that it listens"); nl();
    put("     on channel 1 (or omni)."); nl();
}

/* ---- the Hurrian Hymn (from the author's ~/dev/ur src/common/music.c, same data) ----
 * (MIDI note, duration in eighth-note ticks); 0xFF = rest. B4=71 C5=72 D5=74 E5=76 F5=77 G5=79 A5=81.
 * Notated at quarter = 120 -> an eighth = 250 ms = 50 ticks of the 200 Hz clock. */
#define REST 0xFF
static const uint8_t HYMN[][2] = {
    /* Phrase 1 — the descending-tetrachord theme */ {77,1},{76,1},{74,1},{72,1},{71,4},
    /* Phrase 2 — the rising B answer            */ {71,1},{REST,1},{71,1},{72,1},{74,2},{REST,1},
    /* Phrase 3 — theme restated                 */ {77,1},{76,1},{74,1},{72,1},{71,4},
    /* Phrase 4 — the answer, a step higher      */ {71,1},{REST,1},{72,1},{74,1},{76,2},{REST,1},
    /* Phrase 5 — rising line to the top         */ {71,1},{72,1},{74,1},{76,1},{77,2},{79,2},
    /* Phrase 6 — gentle descent                 */ {81,1},{79,1},{77,1},{76,1},{74,1},{72,1},
    /* Phrase 7 — cadence home                   */ {74,1},{72,1},{71,1},{71,4} };
#define HYMN_N ((int)(sizeof HYMN / sizeof HYMN[0]))
static void hymn_test(void)
{
    static const uint8_t HARP[2]={0xC0,46};                /* GM 46 = Orchestral Harp (lyre-ish) */
    put("OUT: the Hurrian Hymn on a GM harp (any"); nl();
    put("     key skips)..."); nl();
    midi_send(HARP,2); wait_ticks(5);
    for(int i=0;i<HYMN_N;i++){
        int n=HYMN[i][0], d=HYMN[i][1]*50;
        if(n!=REST) note_on(0,n,96);
        wait_ticks((uint32_t)(d>8?d-8:d));                 /* a hair short: separate repeated notes */
        if(n!=REST) note_off(0,n);
        wait_ticks(8);
        if(Cconis()){ (void)Crawcin(); break; }
    }
}

/* ---- MIDI IN decode (running status aware) ---- */
static const char *NOTE_NAME[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
static void put_note(int n){ put(NOTE_NAME[n%12]); put_dec(n/12-1); }
static int g_status, g_need, g_have; static uint8_t g_data[2]; static int g_thru;

static void show_msg(int st, const uint8_t *d)
{
    int ch=(st&15)+1, hi=st&0xF0;
    put("  IN: ");
    switch(hi){
        case 0x90: put(d[1]?"Note On  ":"Note Off "); put("ch "); put_dec(ch); put("  "); put_note(d[0]); put("  vel "); put_dec(d[1]); break;
        case 0x80: put("Note Off "); put("ch "); put_dec(ch); put("  "); put_note(d[0]); break;
        case 0xA0: put("Poly AT  ch "); put_dec(ch); put("  "); put_note(d[0]); put("  "); put_dec(d[1]); break;
        case 0xB0: put("Control  ch "); put_dec(ch); put("  cc "); put_dec(d[0]); put(" = "); put_dec(d[1]); break;
        case 0xC0: put("Program  ch "); put_dec(ch); put("  "); put_dec(d[0]); break;
        case 0xD0: put("Chan AT  ch "); put_dec(ch); put("  "); put_dec(d[0]); break;
        case 0xE0: put("Bend     ch "); put_dec(ch); put("  "); put_dec(((d[1]<<7)|d[0])-8192); break;
        default:   put("status "); put_hex2(st); put(" "); put_hex2(d[0]); put(" "); put_hex2(d[1]); break;
    }
    nl();
}
static void in_byte(int b)
{
    if(b>=0xF8){ return; }                                 /* realtime (clock/active sensing): ignore */
    if(b>=0xF0){ put("  IN: system "); put_hex2(b); nl(); g_status=0; return; }
    if(b&0x80){ g_status=b; g_have=0; int hi=b&0xF0; g_need=(hi==0xC0||hi==0xD0)?1:2; return; }
    if(!g_status) return;
    g_data[g_have++]=(uint8_t)b;
    if(g_have>=g_need){
        show_msg(g_status,g_data);
        if(g_thru){ uint8_t m[3]={(uint8_t)g_status,g_data[0],g_data[1]}; midi_send(m,g_need+1); }
        g_have=0;                                          /* running status: keep g_status */
    }
}

int main(void)
{
    put("\033E");                                          /* VT52 clear screen */
    put("u6e MIDITEST - MIDI port / cable check"); nl();
    put("Cables: Atari OUT -> instrument IN"); nl();
    put("          (you hear the scale)"); nl();
    put("        instrument OUT -> Atari IN"); nl();
    put("          (your playing prints here)"); nl(); nl();
    out_test(); nl();
    hymn_test(); nl();
    put("IN: listening... play the instrument."); nl();
    put("    SPACE = scale  H = hymn  T = thru"); nl();
    put("    Esc/Q = quit"); nl(); nl();
    uint32_t last=ticks(); int quiet_note=0;
    for(;;){
        int got=0;
        while(Bconstat(3)){ in_byte((int)(Bconin(3)&0xFF)); got=1; }
        if(got) last=ticks();
        else if(!quiet_note && (uint32_t)(ticks()-last)>200*8){   /* 8 s of nothing */
            put("  (nothing received yet - if you are"); nl();
            put("   playing, check instrument OUT ->"); nl();
            put("   Atari IN)"); nl(); quiet_note=1; }
        if(Cconis()){
            int k=(int)(Crawcin()&0xFF);
            if(k==0x1b||k=='q'||k=='Q') break;
            if(k==' '){ out_test(); }
            if(k=='h'||k=='H'){ hymn_test(); }
            if(k=='t'||k=='T'){ g_thru=!g_thru; put(g_thru?"MIDI thru ON":"MIDI thru OFF"); nl(); }
        }
    }
    all_off();
    put("all notes off, bye."); nl();
    return 0;
}
