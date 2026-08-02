// SPDX-License-Identifier: GPL-2.0-or-later
/* music.c — portable U6 OPL2 command-stream sequencer (M10). See music.h + docs/formats/music.md.
 * Pure integer C: no FPU, no TOS. Drives a music_backend_t with note/program events on a 60 Hz clock. */
#include "music.h"

/* FPU-free pitch table: MIDI note = NOTE_MAP[freq_byte & 0x1F] + (freq_byte >> 5) * 12.
 * Derived from the OPL F-number table (docs/formats/music.md); matches ScummVM's MIDI_NOTE_MAP.
 * Indices 0/8/16 (and 24..31) are rests -> no pitch. */
static const int8_t NOTE_MAP[32] = {
    0,12,14,16,17,19,21,23,  0,13,15,17,18,20,22,24,  0,11,13,15,16,18,20,22,
    0, 0, 0, 0, 0, 0, 0, 0
};
static int freq_to_note(uint8_t fb){
    int idx = fb & 0x1F;
    if(idx==0 || idx==8 || idx==16 || idx>=24) return -1;   /* rest / out of range */
    int n = NOTE_MAP[idx] + (fb >> 5) * 12;
    if(n < 0) n = 0;
    if(n > 127) n = 127;
    return n;
}

/* OPL channel -> target MIDI channel via its assigned instrument's MIDI.DAT entry
 * (fallback: the OPL channel number itself when there is no map). */
static int chan_of(const music_seq_t *m, int lo){
    return (m->nmap > 0 && m->inst[lo] < m->nmap) ? m->map[m->inst[lo]].ch : lo;
}

void music_seq_start(music_seq_t *m, const uint8_t *opl, int32_t len,
                     const music_map_t *map, int nmap, const music_backend_t *be){
    m->d = opl; m->len = len; m->pos = 0; m->be = be;
    m->nmap = (nmap > 16) ? 16 : nmap; m->sp = 0; m->loop_pos = 0; m->delay = 0;
    m->playing = (opl && len > 0);
    for(int i = 0; i < m->nmap; i++) m->map[i] = map[i];
    for(int i = 0; i < 9; i++){ m->note[i] = -1; m->mch[i] = (uint8_t)i; m->inst[i] = 0; m->vel[i] = 90; }
    if(be) for(int i = 0; i < m->nmap; i++) be->program(m->map[i].ch, m->map[i].prog);
}

void music_seq_stop(music_seq_t *m){
    if(m->playing && m->be) m->be->reset();
    m->playing = 0;
}

/* Release every still-sounding note. Called when the song WRAPS (end-of-song or a top-level
 * loop) so a note that was on at the wrap point can't drone forever if the loop body doesn't
 * re-address that channel (the "hanging notes" heard on real HW). */
static void seq_all_notes_off(music_seq_t *m){
    for(int i = 0; i < 9; i++)
        if(m->note[i] >= 0){ m->be->note_off(m->mch[i], m->note[i]); m->note[i] = -1; }
}

void music_seq_tick(music_seq_t *m, int nticks){
    if(!m->playing) return;
    m->delay -= nticks;
    int guard = 0;
    while(m->playing && m->delay <= 0 && guard++ < 8192){
        if(m->pos >= m->len){                          /* ran off the end without a return: loop */
            m->pos = m->loop_pos;
            if(m->pos >= m->len){ music_seq_stop(m); break; }
            seq_all_notes_off(m);                       /* release held notes across the wrap */
            continue;
        }
        uint8_t b = m->d[m->pos++]; int hi = b >> 4, lo = b & 0xF;
        switch(hi){
          case 0x0: case 0x1: {                         /* 0x0 note-off, 0x1 note-off(old)+on(new) */
            uint8_t fb = m->d[m->pos++];
            if(lo < 9 && m->note[lo] >= 0){ m->be->note_off(m->mch[lo], m->note[lo]); m->note[lo] = -1; }
            if(hi == 0x1 && lo < 9){ int n = freq_to_note(fb);
                if(n >= 0){ int c = chan_of(m, lo); m->be->note_on(c, n, m->vel[lo]);
                            m->note[lo] = (int8_t)n; m->mch[lo] = (uint8_t)c; } }
            break; }
          case 0x2: {                                   /* note-on */
            uint8_t fb = m->d[m->pos++];
            if(lo < 9){ if(m->note[lo] >= 0) m->be->note_off(m->mch[lo], m->note[lo]);
                int n = freq_to_note(fb);
                if(n >= 0){ int c = chan_of(m, lo); m->be->note_on(c, n, m->vel[lo]);
                            m->note[lo] = (int8_t)n; m->mch[lo] = (uint8_t)c; }
                else m->note[lo] = -1; }
            break; }
          case 0x3: {                                   /* set level -> velocity (ScummVM: (0x3F-lvl)*1.5) */
            uint8_t v = m->d[m->pos++];
            if(lo < 9){ int t = (0x3F - (v & 0x3F)) * 3 / 2; if(t < 1) t = 1; if(t > 127) t = 127; m->vel[lo] = (uint8_t)t; }
            break; }
          case 0x7: { uint8_t v = m->d[m->pos++]; if(lo < 9) m->inst[lo] = v; break; }   /* program change (instr idx) */
          case 0x4: case 0x5: case 0x6: m->pos++; break;  /* modulation / slide / vibrato: consume (v1: ignore) */
          case 0x8:
            if(b == 0x82){ m->delay += m->d[m->pos++]; }                 /* DELAY (60 Hz ticks) */
            else if(b == 0x81){                                         /* branch to subsong */
                int reps = m->d[m->pos++]; int32_t off = m->d[m->pos] | (m->d[m->pos+1] << 8); m->pos += 2;
                if(m->sp < 8){ m->stk[m->sp].ret = m->pos; m->stk[m->sp].start = off; m->stk[m->sp].reps = reps; m->sp++; }
                m->pos = off;
            }
            else if(b == 0x83){ m->pos += 12; }                        /* define instrument (OPL patch): MIDI ignores */
            else if(b == 0x85 || b == 0x86){ m->pos++; }               /* fade in/out: v1 ignore */
            break;
          case 0xE: m->loop_pos = m->pos; break;                        /* set loop point */
          case 0xF:                                                     /* return / loop */
            if(m->sp > 0){ if(--m->stk[m->sp-1].reps > 0) m->pos = m->stk[m->sp-1].start;
                           else m->pos = m->stk[--m->sp].ret; }
            else { seq_all_notes_off(m); m->pos = m->loop_pos; }        /* top level: loop the whole song */
            break;
          default: break;                                              /* 0x9..0xD undefined */
        }
    }
}
