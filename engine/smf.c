// SPDX-License-Identifier: GPL-2.0-or-later
/* smf.c — Savage Empire's MIDI-file sequencer, written from docs/formats/se_music.md §3. */
#include "smf.h"

static uint32_t be32(const uint8_t *p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static int32_t vlq(const smf_seq_t *s, int t, int32_t *p)
{   int32_t v=0; for(int k=0;k<4 && *p<s->end[t];k++){ uint8_t b=s->d[(*p)++]; v=(v<<7)|(b&0x7f); if(!(b&0x80)) break; } return v; }
static void set_tempo(smf_seq_t *s, uint32_t us)
{   uint32_t q=us/(uint32_t)s->div; s->scale=(q*4887u)/1243u; }   /* (q <= 34952: no overflow) */   /* §3.7, truncating as DOS */
static void all_off(smf_seq_t *s, int ch){ if(s->be->control) s->be->control(ch,123,0); }

int smf_start(smf_seq_t *s, const uint8_t *d, int32_t len, const music_backend_t *be)
{
    s->playing=0; s->d=d; s->len=len; s->be=be; s->ntr=0; s->acc=0; s->ending=0; s->now=0; s->jumped=0;
    if(len<14 || d[0]!='M'||d[1]!='T'||d[2]!='h'||d[3]!='d') return 0;
    int fmt=(d[8]<<8)|d[9], nt=(d[10]<<8)|d[11]; s->div=(d[12]<<8)|d[13];
    if(fmt>2 || s->div<=0 || (s->div&0x8000)) return 0;
    int32_t p=8+(int32_t)be32(d+4);
    while(s->ntr<nt && s->ntr<SMF_MAXTR && p+8<=len){
        int32_t l=(int32_t)be32(d+p+4);
        if(d[p]=='M'&&d[p+1]=='T'&&d[p+2]=='r'&&d[p+3]=='k'){ int t=s->ntr++;
            s->pos[t]=p+8; s->end[t]=p+8+l>len?len:p+8+l; s->run[t]=0; s->live[t]=1;
            s->wait[t]=vlq(s,t,&s->pos[t]); }
        p+=8+l; }
    if(!s->ntr) return 0;
    set_tempo(s,500000);                 /* (no reset here: the caller has silenced the backend, and a PSG reset may only run */
    s->playing=1; return 1;              /*  in supervisor mode, as from the timer interrupt) */
}

void smf_stop(smf_seq_t *s){ if(s->playing && s->be && s->be->reset) s->be->reset(); s->playing=0; }

/* one event of track t (its delta already counted down); 0 = the song is over (an exit point) */
static int event(smf_seq_t *s, int t)
{
    const uint8_t *d=s->d; int32_t *p=&s->pos[t];
    if(*p>=s->end[t]){ s->live[t]=0; return 1; }
    uint8_t st=d[*p];
    if(st==0xFF){ (*p)++; if(*p>=s->end[t]){ s->live[t]=0; return 1; } uint8_t ty=d[(*p)++]; int32_t l=vlq(s,t,p);
        if(ty==0x51 && l>=3 && *p+3<=s->end[t]) set_tempo(s,((uint32_t)d[*p]<<16)|((uint32_t)d[*p+1]<<8)|d[*p+2]);
        if(ty==0x2F){ s->live[t]=0; return 1; }
        *p+=l; }
    else if(st==0xF0||st==0xF7){ (*p)++; int32_t l=vlq(s,t,p); *p+=l; }
    else if(st==0xFE){                                     /* FE type len data (len one byte; running status untouched) */
        if(*p+3>s->end[t]){ s->live[t]=0; return 1; }
        uint8_t ty=d[*p+1], l=d[*p+2]; int32_t data=*p+3; *p=data+l;
        int ch=s->run[t]&15;
        if(ty==0 && l>=2 && !s->ending){ int32_t w=d[data]|(d[data+1]<<8); all_off(s,ch); *p=data-w; s->jumped=1; }
        else if(ty==1 && s->ending){ for(int c=1;c<=9;c++) all_off(s,c); return 0; }
        else if(ty==2 && l>=2 && s->ending){ int32_t w=d[data]|(d[data+1]<<8); all_off(s,ch); *p=data-w; }
        else if(ty==3 && l>=1) s->now=d[data];
        if(*p<0 || *p>=s->end[t]){ s->live[t]=0; return 1; } }
    else {
        if(st&0x80){ s->run[t]=st; (*p)++; }
        uint8_t rs=s->run[t]; int ch=rs&15, a=0, b=0;
        switch(rs&0xF0){
        case 0x80: a=d[(*p)++]; b=d[(*p)++]; s->be->note_off(ch,a); break;
        case 0x90: a=d[(*p)++]; b=d[(*p)++]; if(b) s->be->note_on(ch,a,b); else s->be->note_off(ch,a); break;
        case 0xA0: *p+=2; break;
        case 0xB0: a=d[(*p)++]; b=d[(*p)++]; if(s->be->control) s->be->control(ch,a,b); break;
        case 0xC0: a=d[(*p)++]; if(s->be->program) s->be->program(ch,a); break;
        case 0xD0: *p+=1; break;
        case 0xE0: a=d[(*p)++]; b=d[(*p)++]; if(s->be->bend) s->be->bend(ch,a,b); break;
        default: s->live[t]=0; return 1; } }
    if(*p>=s->end[t]) s->live[t]=0; else s->wait[t]=vlq(s,t,p);
    return 1;
}

void smf_tick(smf_seq_t *s)
{
    if(!s->playing) return;
    for(int guard=0;guard<4096;guard++){
        int t=-1; for(int k=0;k<s->ntr;k++) if(s->live[k] && (t<0 || s->wait[k]<s->wait[t])) t=k;   /* ties: the lowest track */
        if(t<0){ smf_stop(s); return; }                                     /* every track ended */
        int32_t w=s->wait[t];
        if(w){ s->acc+=(uint32_t)w*s->scale; for(int k=0;k<s->ntr;k++) if(s->live[k]) s->wait[k]-=w; }
        if(s->acc>=0x10000u) break;
        if(!event(s,t)){ smf_stop(s); return; } }
    if(s->acc>=0x10000u) s->acc-=0x10000u; else s->acc=0;
}
